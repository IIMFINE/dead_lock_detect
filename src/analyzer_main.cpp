// 离线分析器（v2）：
//
// 读事件流，重建每个线程的持锁栈，按"持有 X 时请求 Y"的规则建依赖图，
// 跑 Tarjan + DFS 找环，输出报告。
//
// 运行时不再做任何分析。
#include "event_types.h"
#include "backtrace.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace dl;

namespace {

struct Frame {
    uintptr_t pc = 0;
    std::string func;
    std::string module;
    uintptr_t offset = 0;
    std::string file;
    int line = 0;
};
using Bt = std::vector<Frame>;

struct LockId {
    uintptr_t addr = 0;
    uint32_t  gen  = 0;
    bool operator==(const LockId& o) const noexcept { return addr == o.addr && gen == o.gen; }
};
struct LockIdHash {
    size_t operator()(const LockId& id) const noexcept {
        return std::hash<uint64_t>{}(id.addr ^ (uint64_t(id.gen) << 32));
    }
};

enum class LockKind : uint8_t { Mutex, RwlockRd, RwlockWr, Spin };
const char* kind_name(LockKind k) {
    switch (k) {
        case LockKind::Mutex:    return "mutex";
        case LockKind::RwlockRd: return "rwlock(rd)";
        case LockKind::RwlockWr: return "rwlock(wr)";
        case LockKind::Spin:     return "spinlock";
    }
    return "?";
}

struct LockMeta {
    LockKind kind = LockKind::Mutex;
    bool recursive = false;
    uint32_t gen = 1;
    bool alive = true;
    Bt init_bt;
    uint64_t init_tid = 0;
};

struct LockNode {
    LockId id;
    LockKind kind;
    int hold_count = 1;
    Bt acquire_bt;
    uint64_t acquire_tid = 0;
};

struct EdgeKey {
    LockId from, to;
    bool operator==(const EdgeKey& o) const noexcept { return from == o.from && to == o.to; }
};
struct EdgeKeyHash {
    size_t operator()(const EdgeKey& e) const noexcept {
        LockIdHash h; return h(e.from) * 1315423911u ^ h(e.to);
    }
};
struct EdgeInfo {
    uint64_t tid;
    LockKind from_kind, to_kind;
    Bt bt_from, bt_to;
};

// ---- 事件读取 ----
struct Event {
    uint64_t ts_ns;
    uint64_t tid;
    EvOp  op;
    EvKind kind;
    uintptr_t addr;
    long rc;
    Bt bt;
};

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { parts.push_back(std::move(cur)); cur.clear(); }
        else cur += c;
    }
    parts.push_back(std::move(cur));
    return parts;
}

std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == '\\') out += '\\';
            else if (n == 't') out += '\t';
            else if (n == 'n') out += '\n';
            else { out += s[i]; out += n; }
            ++i;
        } else out += s[i];
    }
    return out;
}

bool parse_hex(const std::string& s, uintptr_t& out) {
    size_t off = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) off = 2;
    out = 0;
    for (size_t i = off; i < s.size(); ++i) {
        char c = s[i]; int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        out = (out << 4) | d;
    }
    return s.size() > off;
}

bool read_events(std::istream& in, int& pid_out, std::vector<Event>& out) {
    std::string line;
    if (!std::getline(in, line)) return false;
    auto parts = split_tab(line);
    if (parts.size() < 3 || parts[0] != "HEADER" || parts[1] != "DEADLOCK_EVENTS")
        return false;
    pid_out = 0;
    for (auto& p : parts) if (p.rfind("pid=", 0) == 0) pid_out = std::stoi(p.substr(4));

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto p = split_tab(line);
        if (p.size() < 8 || p[0] != "E") continue;
        Event ev;
        ev.ts_ns = std::stoull(p[1]);
        ev.tid   = std::stoull(p[2]);
        ev.op    = static_cast<EvOp>(std::stoi(p[3]));
        ev.kind  = static_cast<EvKind>(std::stoi(p[4]));
        parse_hex(p[5], ev.addr);
        ev.rc    = std::stol(p[6]);
        size_t nf = std::stoul(p[7]);
        ev.bt.reserve(nf);
        for (size_t i = 0; i < nf; ++i) {
            if (!std::getline(in, line)) break;
            auto fp = split_tab(line);
            if (fp.size() < 5 || fp[0] != "F") break;
            Frame f;
            parse_hex(fp[1], f.pc);
            f.func   = unescape(fp[2]);
            f.module = unescape(fp[3]);
            parse_hex(fp[4], f.offset);
            if (fp.size() >= 7) {
                f.file = unescape(fp[5]);
                try { f.line = std::stoi(fp[6]); } catch (...) { f.line = 0; }
            }
            ev.bt.push_back(std::move(f));
        }
        out.push_back(std::move(ev));
    }
    return true;
}

// ---- 图重建 ----
struct Graph {
    std::unordered_map<uintptr_t, uint32_t> addr_gen;        // 当前活动 gen
    std::unordered_map<LockId, LockMeta, LockIdHash> meta;
    std::unordered_map<EdgeKey, EdgeInfo, EdgeKeyHash> edges;
    std::unordered_map<uint64_t, std::vector<LockNode>> stacks;  // per-tid 持锁栈

    LockId ensure_gen(uintptr_t addr) {
        auto it = addr_gen.find(addr);
        if (it == addr_gen.end()) {
            addr_gen[addr] = 1;
            return {addr, 1};
        }
        return {addr, it->second};
    }

    void on_init(EvKind k, uintptr_t addr, bool recursive, uint64_t tid, const Bt& bt) {
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

    void on_destroy(uintptr_t addr) {
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

    // acquire 事件（LOCK_PRE / RDLOCK_PRE / WRLOCK_PRE / SPIN LOCK_PRE 等）
    void on_acquire_pre(uintptr_t addr, LockKind k, uint64_t tid, const Bt& bt) {
        LockId id = ensure_gen(addr);
        // 元数据若不存在（静态初始化 mutex / 未劫持到 init），补登记
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
                // 非递归锁的重入 = 自锁，仍建自环边的意义不大；这里只返回
                n.hold_count++;
                return;
            }
        }

        // 遍历持锁栈，每把已持锁 -> 新锁 建边
        for (const auto& held : stk) {
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

        // 压入持锁栈（POST 事件若失败再弹出；大多数场景 PRE 压栈即可）
        LockNode n;
        n.id = id;
        n.kind = k;
        n.hold_count = 1;
        n.acquire_bt = bt;
        n.acquire_tid = tid;
        stk.push_back(std::move(n));
    }

    void on_acquire_post(uintptr_t addr, uint64_t tid, long rc) {
        if (rc == 0) return;
        // 失败：弹出刚刚在 PRE 里压进去的这把锁
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

    void on_unlock(uintptr_t addr, uint64_t tid) {
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

    // cond_wait_pre：m_addr 在事件 rc 槽里；wait 期间把 mutex 临时移出
    void on_cond_pre(uintptr_t m_addr, uint64_t tid) {
        on_unlock(m_addr, tid);
    }
    void on_cond_post(uintptr_t m_addr, uint64_t tid, const Bt& bt) {
        auto it = addr_gen.find(m_addr);
        if (it == addr_gen.end()) return;
        LockId id{m_addr, it->second};
        LockNode n;
        n.id = id; n.kind = LockKind::Mutex; n.hold_count = 1;
        n.acquire_bt = bt; n.acquire_tid = tid;
        stacks[tid].push_back(std::move(n));
    }
};

// ---- 事件流回放 ----
void replay(const std::vector<Event>& events, Graph& g) {
    uint64_t pending_cond_mutex[1024] = {0};  // 简化：仅按 tid 低位索引；真实实现按 tid 映射
    std::unordered_map<uint64_t, uintptr_t> cond_mutex_of_tid;

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
            case EvOp::TIMEDLOCK_PRE: {
                LockKind k = (e.kind == EvKind::SPIN) ? LockKind::Spin : LockKind::Mutex;
                g.on_acquire_pre(e.addr, k, e.tid, e.bt);
                break;
            }
            case EvOp::LOCK_POST:
            case EvOp::TRYLOCK_POST:
            case EvOp::TIMEDLOCK_POST:
                g.on_acquire_post(e.addr, e.tid, e.rc);
                break;

            case EvOp::RDLOCK_PRE:
            case EvOp::TRYRDLOCK_PRE:
            case EvOp::TIMEDRDLOCK_PRE:
                g.on_acquire_pre(e.addr, LockKind::RwlockRd, e.tid, e.bt);
                break;
            case EvOp::RDLOCK_POST:
            case EvOp::TRYRDLOCK_POST:
            case EvOp::TIMEDRDLOCK_POST:
                g.on_acquire_post(e.addr, e.tid, e.rc);
                break;

            case EvOp::WRLOCK_PRE:
            case EvOp::TRYWRLOCK_PRE:
            case EvOp::TIMEDWRLOCK_PRE:
                g.on_acquire_pre(e.addr, LockKind::RwlockWr, e.tid, e.bt);
                break;
            case EvOp::WRLOCK_POST:
            case EvOp::TRYWRLOCK_POST:
            case EvOp::TIMEDWRLOCK_POST:
                g.on_acquire_post(e.addr, e.tid, e.rc);
                break;

            case EvOp::UNLOCK:
                g.on_unlock(e.addr, e.tid);
                break;

            case EvOp::COND_WAIT_PRE:
            case EvOp::COND_TIMEDWAIT_PRE: {
                uintptr_t m_addr = static_cast<uintptr_t>(static_cast<unsigned long>(e.rc));
                cond_mutex_of_tid[e.tid] = m_addr;
                g.on_cond_pre(m_addr, e.tid);
                break;
            }
            case EvOp::COND_WAIT_POST:
            case EvOp::COND_TIMEDWAIT_POST: {
                auto it = cond_mutex_of_tid.find(e.tid);
                if (it != cond_mutex_of_tid.end()) {
                    g.on_cond_post(it->second, e.tid, e.bt);
                    cond_mutex_of_tid.erase(it);
                }
                break;
            }
        }
    }
    (void)pending_cond_mutex;
}

// ---- Tarjan ----
struct SccCtx {
    std::unordered_map<LockId, std::vector<LockId>, LockIdHash> adj;
    std::unordered_map<LockId, int, LockIdHash> idx, low;
    std::unordered_map<LockId, bool, LockIdHash> on_stack;
    std::vector<LockId> stk;
    int counter = 0;
    std::vector<std::vector<LockId>> sccs;
};
void tarjan_visit(SccCtx& c, const LockId& v) {
    c.idx[v] = c.low[v] = c.counter++;
    c.stk.push_back(v);
    c.on_stack[v] = true;
    auto it = c.adj.find(v);
    if (it != c.adj.end()) {
        for (const auto& w : it->second) {
            if (c.idx.find(w) == c.idx.end()) {
                tarjan_visit(c, w);
                c.low[v] = std::min(c.low[v], c.low[w]);
            } else if (c.on_stack[w]) {
                c.low[v] = std::min(c.low[v], c.idx[w]);
            }
        }
    }
    if (c.low[v] == c.idx[v]) {
        std::vector<LockId> scc;
        while (true) {
            LockId w = c.stk.back(); c.stk.pop_back();
            c.on_stack[w] = false;
            scc.push_back(w);
            if (w == v) break;
        }
        c.sccs.push_back(std::move(scc));
    }
}

struct CycleEdge { LockId from, to; const EdgeInfo* info; };
struct Cycle { std::vector<CycleEdge> edges; };

std::vector<Cycle> find_cycles(const Graph& g, int max_per_scc) {
    SccCtx c;
    for (const auto& [key, info] : g.edges) {
        c.adj[key.from].push_back(key.to);
        if (c.adj.find(key.to) == c.adj.end()) c.adj[key.to] = {};
        (void)info;
    }
    for (auto& [node, _] : c.adj) {
        if (c.idx.find(node) == c.idx.end()) tarjan_visit(c, node);
    }

    std::vector<Cycle> out;
    for (auto& scc : c.sccs) {
        if (scc.size() < 2) continue;
        std::unordered_set<LockId, LockIdHash> in_scc(scc.begin(), scc.end());
        int emitted = 0;
        for (const auto& start : scc) {
            if (emitted >= max_per_scc) break;
            std::unordered_map<LockId, bool, LockIdHash> on_path;
            std::vector<LockId> path;
            std::function<bool(const LockId&)> dfs = [&](const LockId& u) -> bool {
                on_path[u] = true;
                path.push_back(u);
                auto it = c.adj.find(u);
                if (it != c.adj.end()) {
                    for (const auto& v : it->second) {
                        if (in_scc.find(v) == in_scc.end()) continue;
                        if (v == start && path.size() >= 2) {
                            Cycle cy;
                            for (size_t i = 0; i < path.size(); ++i) {
                                LockId from = path[i];
                                LockId to   = (i + 1 < path.size()) ? path[i + 1] : start;
                                auto eit = g.edges.find(EdgeKey{from, to});
                                if (eit == g.edges.end()) { cy.edges.clear(); break; }
                                cy.edges.push_back({from, to, &eit->second});
                            }
                            if (!cy.edges.empty()) {
                                out.push_back(std::move(cy));
                                if (++emitted >= max_per_scc) return true;
                            }
                        } else if (!on_path[v]) {
                            if (dfs(v)) return true;
                        }
                    }
                }
                on_path[u] = false;
                path.pop_back();
                return false;
            };
            dfs(start);
        }
    }

    // 去重（按旋转归一化）
    auto canon = [](Cycle& cy) {
        if (cy.edges.empty()) return;
        size_t mi = 0;
        LockIdHash h;
        for (size_t i = 1; i < cy.edges.size(); ++i)
            if (h(cy.edges[i].from) < h(cy.edges[mi].from)) mi = i;
        std::rotate(cy.edges.begin(), cy.edges.begin() + mi, cy.edges.end());
    };
    for (auto& cy : out) canon(cy);
    std::sort(out.begin(), out.end(), [](const Cycle& a, const Cycle& b){
        if (a.edges.size() != b.edges.size()) return a.edges.size() < b.edges.size();
        LockIdHash h;
        for (size_t i = 0; i < a.edges.size(); ++i) {
            if (!(a.edges[i].from == b.edges[i].from))
                return h(a.edges[i].from) < h(b.edges[i].from);
        }
        return false;
    });
    out.erase(std::unique(out.begin(), out.end(), [](const Cycle& a, const Cycle& b){
        if (a.edges.size() != b.edges.size()) return false;
        for (size_t i = 0; i < a.edges.size(); ++i) {
            if (!(a.edges[i].from == b.edges[i].from) || !(a.edges[i].to == b.edges[i].to))
                return false;
        }
        return true;
    }), out.end());
    return out;
}

// ---- 报告 ----
void print_bt(std::ostream& o, const Bt& bt, const char* indent) {
    if (bt.empty()) { o << indent << "<empty>\n"; return; }
    char line[1024];
    for (size_t i = 0; i < bt.size(); ++i) {
        const auto& f = bt[i];
        if (!f.file.empty()) {
            const char* fname = f.file.c_str();
            const char* slash = strrchr(fname, '/');
            snprintf(line, sizeof(line),
                     "%s#%zu  0x%016lx  %s+0x%lx  (%s)  at %s:%d\n",
                     indent, i, (unsigned long)f.pc,
                     f.func.c_str(), (unsigned long)f.offset,
                     f.module.c_str(),
                     slash ? slash + 1 : fname, f.line);
        } else {
            snprintf(line, sizeof(line),
                     "%s#%zu  0x%016lx  %s+0x%lx  (%s)\n",
                     indent, i, (unsigned long)f.pc,
                     f.func.c_str(), (unsigned long)f.offset,
                     f.module.c_str());
        }
        o << line;
    }
}

int report(std::ostream& o, const Graph& g, bool rwlock_strict, int max_per_scc) {
    auto cycles = find_cycles(g, max_per_scc);

    if (!rwlock_strict) {
        cycles.erase(std::remove_if(cycles.begin(), cycles.end(), [](const Cycle& cy){
            for (const auto& e : cy.edges) {
                if (!(e.info->from_kind == LockKind::RwlockRd &&
                      e.info->to_kind   == LockKind::RwlockRd)) return false;
            }
            return true;
        }), cycles.end());
    }

    o << "=== Deadlock Detector Report ===\n";
    o << "nodes=" << g.meta.size()
      << " edges=" << g.edges.size()
      << " cycles=" << cycles.size() << "\n";

    if (cycles.empty()) { o << "(no cycles detected)\n=== End Report ===\n"; return 0; }

    int ci = 0;
    for (const auto& cy : cycles) {
        ++ci;
        o << "\n-- Cycle #" << ci << " (length=" << cy.edges.size() << ") --\n";
        for (const auto& e : cy.edges) {
            auto mit = g.meta.find(e.from);
            if (mit == g.meta.end()) continue;
            char line[256];
            snprintf(line, sizeof(line),
                     "  Lock 0x%lx [%s] gen=%u\n",
                     (unsigned long)e.from.addr,
                     kind_name(e.info->from_kind),
                     e.from.gen);
            o << line;
            if (!mit->second.init_bt.empty()) {
                o << "    init at:\n";
                print_bt(o, mit->second.init_bt, "      ");
            }
        }
        for (const auto& e : cy.edges) {
            char line[256];
            snprintf(line, sizeof(line),
                     "  Edge 0x%lx [%s] -> 0x%lx [%s]   tid=%lu\n",
                     (unsigned long)e.from.addr, kind_name(e.info->from_kind),
                     (unsigned long)e.to.addr,   kind_name(e.info->to_kind),
                     (unsigned long)e.info->tid);
            o << line;
            o << "    holding 0x" << std::hex << e.from.addr << std::dec << " at:\n";
            print_bt(o, e.info->bt_from, "      ");
            o << "    requesting 0x" << std::hex << e.to.addr << std::dec << " at:\n";
            print_bt(o, e.info->bt_to, "      ");
        }
    }
    o << "=== End Report ===\n";
    return static_cast<int>(cycles.size());
}

void usage(const char* argv0) {
    fprintf(stderr,
        "Usage: %s [options] <event-file>\n"
        "Options:\n"
        "  --rwlock-strict      Report pure rd->rd cycles too (default: skip)\n"
        "  --max-per-scc N      Max simple cycles per SCC (default: 32)\n"
        "  -o <file>            Write report to <file> (default: stdout)\n"
        "  -h, --help           Show this help\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    bool rwlock_strict = false;
    int max_per_scc = 32;
    const char* out_path = nullptr;
    const char* ev_path = nullptr;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(argv[0]); return 0; }
        else if (!strcmp(a, "--rwlock-strict")) rwlock_strict = true;
        else if (!strcmp(a, "--max-per-scc") && i + 1 < argc) max_per_scc = atoi(argv[++i]);
        else if (!strcmp(a, "-o") && i + 1 < argc) out_path = argv[++i];
        else if (a[0] == '-') { fprintf(stderr, "unknown option: %s\n", a); usage(argv[0]); return 2; }
        else if (!ev_path) ev_path = a;
        else { fprintf(stderr, "unexpected: %s\n", a); usage(argv[0]); return 2; }
    }
    if (!ev_path) { usage(argv[0]); return 2; }

    std::ifstream ifs(ev_path);
    if (!ifs) { fprintf(stderr, "cannot open: %s\n", ev_path); return 1; }

    int pid = 0;
    std::vector<Event> events;
    if (!read_events(ifs, pid, events)) {
        fprintf(stderr, "invalid event log: %s\n", ev_path);
        return 1;
    }

    Graph g;
    replay(events, g);

    std::ostream* out = &std::cout;
    std::ofstream ofs;
    if (out_path) {
        ofs.open(out_path, std::ios::out | std::ios::trunc);
        if (!ofs) { fprintf(stderr, "cannot open output: %s\n", out_path); return 1; }
        out = &ofs;
    }
    *out << "(source: " << ev_path << "  pid=" << pid << "  events=" << events.size() << ")\n";
    int cycles = report(*out, g, rwlock_strict, max_per_scc);
    out->flush();
    return cycles > 0 ? 3 : 0;
}
