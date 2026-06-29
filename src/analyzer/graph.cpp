#include "graph.h"

#include <utility>

namespace dl::analyzer {

const char* kind_name(LockKind k) {
    switch (k) {
        case LockKind::Mutex:    return "mutex";
        case LockKind::RwlockRd: return "rwlock(rd)";
        case LockKind::RwlockWr: return "rwlock(wr)";
        case LockKind::Spin:     return "spinlock";
    }
    return "?";
}

LockId Graph::ensure_gen(uintptr_t addr) {
    auto it = addr_gen.find(addr);
    if (it == addr_gen.end()) {
        addr_gen[addr] = 1;
        return {addr, 1};
    }
    return {addr, it->second};
}

void Graph::on_init(EvKind k, uintptr_t addr, bool recursive, uint64_t tid, const Bt& bt) {
    uint32_t gen = 1;
    auto it = addr_gen.find(addr);
    if (it != addr_gen.end()) gen = it->second + 1;
    addr_gen[addr] = gen;
    LockId id{addr, gen};
    LockMeta m;
    m.kind = (k == EvKind::RWLOCK) ? LockKind::RwlockWr  // 仅占位；rd/wr 用事件区分
           : (k == EvKind::SPIN)   ? LockKind::Spin
                                   : LockKind::Mutex;
    m.recursive = recursive;
    m.gen = gen;
    m.alive = true;
    m.init_bt = bt;
    m.init_tid = tid;
    meta[id] = std::move(m);
}

void Graph::on_destroy(uintptr_t addr) {
    auto it = addr_gen.find(addr);
    if (it == addr_gen.end()) return;
    LockId id{addr, it->second};
    auto mit = meta.find(id);
    if (mit != meta.end()) mit->second.alive = false;
    // 清除涉及该 LockId 的所有边
    for (auto eit = edges.begin(); eit != edges.end(); ) {
        if (eit->first.from == id || eit->first.to == id) eit = edges.erase(eit);
        else ++eit;
    }
}

void Graph::on_acquire(uintptr_t addr, LockKind k, uint64_t tid, const Bt& bt) {
    LockId id = ensure_gen(addr);
    if (meta.find(id) == meta.end()) {
        LockMeta m; m.kind = k; m.recursive = false; m.gen = id.gen;
        m.alive = true; m.init_bt.clear(); m.init_tid = tid;
        meta[id] = std::move(m);
    }

    auto& stk = stacks[tid];

    // 递归 mutex 重入：若同一 LockId 已在栈中，不建边、不重复压栈
    for (auto& n : stk) {
        if (n.id == id) {
            auto it = meta.find(id);
            if (it != meta.end() && it->second.recursive) {
                n.hold_count++;
                return;
            }
            n.hold_count++;
            return;
        }
    }

    build_edges_to(id, k, tid, bt);

    LockNode n;
    n.id = id;
    n.kind = k;
    n.hold_count = 1;
    n.acquire_bt = bt;
    n.acquire_tid = tid;
    stk.push_back(std::move(n));
}

void Graph::on_acquire_attempt(uintptr_t addr, LockKind k, uint64_t tid, const Bt& bt) {
    LockId id = ensure_gen(addr);
    if (meta.find(id) == meta.end()) {
        LockMeta m; m.kind = k; m.recursive = false; m.gen = id.gen;
        m.alive = true; m.init_bt.clear(); m.init_tid = tid;
        meta[id] = std::move(m);
    }
    build_edges_to(id, k, tid, bt);
}

void Graph::on_unlock(uintptr_t addr, uint64_t tid) {
    auto& stk = stacks[tid];
    auto it = addr_gen.find(addr);
    if (it == addr_gen.end()) return;
    LockId id{addr, it->second};
    for (auto rit = stk.rbegin(); rit != stk.rend(); ++rit) {
        if (rit->id == id) {
            if (rit->hold_count > 1) rit->hold_count--;
            else stk.erase(std::next(rit).base());
            return;
        }
    }
}

void Graph::on_cond_post(uintptr_t m_addr, uint64_t tid, const Bt& bt) {
    on_unlock(m_addr, tid);
    on_acquire(m_addr, LockKind::Mutex, tid, bt);
}

void Graph::build_edges_to(const LockId& id, LockKind k, uint64_t tid, const Bt& bt) {
    auto& stk = stacks[tid];
    for (const auto& held : stk) {
        if (held.id == id) continue;  // 同锁重入不建自环
        EdgeKey key{held.id, id};
        if (edges.find(key) == edges.end()) {
            EdgeInfo info;
            info.tid = tid;
            info.from_kind = held.kind;
            info.to_kind = k;
            info.bt_from = held.acquire_bt;
            info.bt_to = bt;
            edges.emplace(key, std::move(info));
        }
    }
}

// ---- 事件流回放 ----
void replay(const std::vector<Event>& events, Graph& g) {
    for (const auto& e : events) {
        switch (e.op) {
            case EvOp::INIT: {
                bool rec = (e.kind == EvKind::MUTEX && (e.rc & 1));
                g.on_init(e.kind, e.addr, rec, e.tid, e.bt);
                break;
            }
            case EvOp::DESTROY:
                g.on_destroy(e.addr);
                break;

            case EvOp::LOCK_PRE:
            case EvOp::TRYLOCK_PRE:
            case EvOp::TIMEDLOCK_PRE:
                // 兼容旧 trace 文件：新数据流不再生成 PRE 事件
                break;
            case EvOp::LOCK_POST: {
                if (e.rc != 0) break;
                LockKind k = (e.kind == EvKind::SPIN) ? LockKind::Spin : LockKind::Mutex;
                g.on_acquire(e.addr, k, e.tid, e.bt);
                break;
            }
            case EvOp::TRYLOCK_POST:
            case EvOp::TIMEDLOCK_POST: {
                LockKind k = (e.kind == EvKind::SPIN) ? LockKind::Spin : LockKind::Mutex;
                if (e.rc == 0) g.on_acquire(e.addr, k, e.tid, e.bt);
                else           g.on_acquire_attempt(e.addr, k, e.tid, e.bt);
                break;
            }

            case EvOp::RDLOCK_PRE:
            case EvOp::TRYRDLOCK_PRE:
            case EvOp::TIMEDRDLOCK_PRE:
                break;
            case EvOp::RDLOCK_POST:
                if (e.rc != 0) break;
                g.on_acquire(e.addr, LockKind::RwlockRd, e.tid, e.bt);
                break;
            case EvOp::TRYRDLOCK_POST:
            case EvOp::TIMEDRDLOCK_POST:
                if (e.rc == 0) g.on_acquire(e.addr, LockKind::RwlockRd, e.tid, e.bt);
                else           g.on_acquire_attempt(e.addr, LockKind::RwlockRd, e.tid, e.bt);
                break;

            case EvOp::WRLOCK_PRE:
            case EvOp::TRYWRLOCK_PRE:
            case EvOp::TIMEDWRLOCK_PRE:
                break;
            case EvOp::WRLOCK_POST:
                if (e.rc != 0) break;
                g.on_acquire(e.addr, LockKind::RwlockWr, e.tid, e.bt);
                break;
            case EvOp::TRYWRLOCK_POST:
            case EvOp::TIMEDWRLOCK_POST:
                if (e.rc == 0) g.on_acquire(e.addr, LockKind::RwlockWr, e.tid, e.bt);
                else           g.on_acquire_attempt(e.addr, LockKind::RwlockWr, e.tid, e.bt);
                break;

            case EvOp::UNLOCK:
                g.on_unlock(e.addr, e.tid);
                break;

            case EvOp::COND_WAIT_PRE:
            case EvOp::COND_TIMEDWAIT_PRE:
                // 兼容旧 trace；新数据流将 mutex 地址放在 POST 的 rc 槽
                break;
            case EvOp::COND_WAIT_POST:
            case EvOp::COND_TIMEDWAIT_POST: {
                uintptr_t m_addr = static_cast<uintptr_t>(static_cast<unsigned long>(e.rc));
                if (m_addr != 0) g.on_cond_post(m_addr, e.tid, e.bt);
                break;
            }
        }
    }
}

}  // namespace dl::analyzer
