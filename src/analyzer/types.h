#pragma once
//
// Analyzer 内部共享类型：锁标识、元数据、持锁栈节点、边、事件、模块表。
// 全部在 dl::analyzer 命名空间，避免与 runtime 重名。

#include "../event_types.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dl::analyzer {

// 一帧调用栈。trace_reader 只填 pc；symbolizer 跑完后回填其余字段。
struct Frame {
    uintptr_t pc = 0;
    std::string func;
    std::string module;     // 模块 basename（如 libdeadlock_detect.so）
    uintptr_t offset = 0;   // 模块内偏移 = pc - module.base
    std::string file;
    int line = 0;
};
using Bt = std::vector<Frame>;

// trace 头部记录的模块映射（一次进程快照）。analyzer 用 base/size 做 PC 归属判断，
// 用 path 喂给 addr2line 做符号化。build_id 来自 PT_NOTE/NT_GNU_BUILD_ID，目前
// 仅做记录、未用于符号文件路径校验。
struct Module {
    int          idx = 0;
    uintptr_t    base = 0;
    uintptr_t    size = 0;
    std::string  build_id;   // "-" 表示缺失
    std::string  path;       // trace 里记的绝对路径；可能在 analyzer 机器上不存在
};
using ModuleMap = std::vector<Module>;   // 按 base 升序，供二分查找

enum class LockKind : uint8_t { Mutex, RwlockRd, RwlockWr, Spin };
const char* kind_name(LockKind k);

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

// trace 文件中一条事件（含其后续 frame 行）的解析结果
struct Event {
    uint64_t ts_ns;
    uint64_t tid;
    EvOp  op;
    EvKind kind;
    uintptr_t addr;
    long rc;
    Bt bt;
};

}  // namespace dl::analyzer
