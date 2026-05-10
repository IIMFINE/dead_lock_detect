#pragma once

#include <cstdint>

namespace dl {

// 事件操作码。离线分析器只需要读数值，不做字符串比较。
enum class EvOp : uint8_t {
    INIT             = 1,
    DESTROY          = 2,
    LOCK_PRE         = 3,
    LOCK_POST        = 4,
    TRYLOCK_PRE      = 5,
    TRYLOCK_POST     = 6,
    TIMEDLOCK_PRE    = 7,
    TIMEDLOCK_POST   = 8,
    UNLOCK           = 9,
    RDLOCK_PRE       = 10,
    RDLOCK_POST      = 11,
    TRYRDLOCK_PRE    = 12,
    TRYRDLOCK_POST   = 13,
    TIMEDRDLOCK_PRE  = 14,
    TIMEDRDLOCK_POST = 15,
    WRLOCK_PRE       = 16,
    WRLOCK_POST      = 17,
    TRYWRLOCK_PRE    = 18,
    TRYWRLOCK_POST   = 19,
    TIMEDWRLOCK_PRE  = 20,
    TIMEDWRLOCK_POST = 21,
    COND_WAIT_PRE    = 22,
    COND_WAIT_POST   = 23,
    COND_TIMEDWAIT_PRE  = 24,
    COND_TIMEDWAIT_POST = 25,
};

enum class EvKind : uint8_t {
    MUTEX  = 1,
    RWLOCK = 2,
    SPIN   = 3,
    COND   = 4,
};

const char* ev_op_name(EvOp op) noexcept;
const char* ev_kind_name(EvKind k) noexcept;
bool ev_op_from_name(const char* s, EvOp& out) noexcept;
bool ev_kind_from_name(const char* s, EvKind& out) noexcept;

// 常用宏：在劫持函数里写事件时一行搞定，避免散落的裸字符串
#define DL_EV(op, kind, addr, rc)                                \
    ::dl::log_event(::dl::EvOp::op, ::dl::EvKind::kind,          \
                    (addr), (long)(rc),                          \
                    ::dl::config().bt_skip,                      \
                    ::dl::config().bt_depth)

}  // namespace dl
