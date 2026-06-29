#pragma once
//
// Graph：重建 per-tid 持锁栈 + lock-order 边集 + 锁元数据。
//
// 接收事件流（replay）后即可被 cycles/report 消费。
//
#include "types.h"
#include <unordered_map>
#include <vector>

namespace dl::analyzer {

struct Graph {
    std::unordered_map<uintptr_t, uint32_t> addr_gen;        // 当前活动 gen
    std::unordered_map<LockId, LockMeta, LockIdHash> meta;
    std::unordered_map<EdgeKey, EdgeInfo, EdgeKeyHash> edges;
    std::unordered_map<uint64_t, std::vector<LockNode>> stacks;  // per-tid 持锁栈

    LockId ensure_gen(uintptr_t addr);

    void on_init(EvKind k, uintptr_t addr, bool recursive, uint64_t tid, const Bt& bt);
    void on_destroy(uintptr_t addr);

    // acquire 成功：建边 + 压栈
    void on_acquire(uintptr_t addr, LockKind k, uint64_t tid, const Bt& bt);

    // try/timed acquire 失败：只建边（"曾请求"信号），不压栈
    void on_acquire_attempt(uintptr_t addr, LockKind k, uint64_t tid, const Bt& bt);

    void on_unlock(uintptr_t addr, uint64_t tid);

    // cond_wait 返回（POST 时 m 已重新持有）：先模拟释放，再模拟重新获取并建边
    void on_cond_post(uintptr_t m_addr, uint64_t tid, const Bt& bt);

private:
    void build_edges_to(const LockId& id, LockKind k, uint64_t tid, const Bt& bt);
};

// 回放事件流到 Graph。
void replay(const std::vector<Event>& events, Graph& g);

}  // namespace dl::analyzer
