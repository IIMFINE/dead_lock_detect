"""事件操作码 / 类型，对应 runtime 端 src/event_types.h 的 EvOp / EvKind。

仅复制数值常量；离线分析器靠数值匹配，不依赖名字。
"""

from __future__ import annotations

from enum import IntEnum


class EvOp(IntEnum):
    INIT                = 1
    DESTROY             = 2
    LOCK_PRE            = 3
    LOCK_POST           = 4
    TRYLOCK_PRE         = 5
    TRYLOCK_POST        = 6
    TIMEDLOCK_PRE       = 7
    TIMEDLOCK_POST      = 8
    UNLOCK              = 9
    RDLOCK_PRE          = 10
    RDLOCK_POST         = 11
    TRYRDLOCK_PRE       = 12
    TRYRDLOCK_POST      = 13
    TIMEDRDLOCK_PRE     = 14
    TIMEDRDLOCK_POST    = 15
    WRLOCK_PRE          = 16
    WRLOCK_POST         = 17
    TRYWRLOCK_PRE       = 18
    TRYWRLOCK_POST      = 19
    TIMEDWRLOCK_PRE     = 20
    TIMEDWRLOCK_POST    = 21
    COND_WAIT_PRE       = 22
    COND_WAIT_POST      = 23
    COND_TIMEDWAIT_PRE  = 24
    COND_TIMEDWAIT_POST = 25


class EvKind(IntEnum):
    MUTEX  = 1
    RWLOCK = 2
    SPIN   = 3
    COND   = 4


class LockKind(IntEnum):
    MUTEX     = 0
    RWLOCK_RD = 1
    RWLOCK_WR = 2
    SPIN      = 3


_KIND_NAMES = {
    LockKind.MUTEX:     "mutex",
    LockKind.RWLOCK_RD: "rwlock(rd)",
    LockKind.RWLOCK_WR: "rwlock(wr)",
    LockKind.SPIN:      "spinlock",
}


def kind_name(k: LockKind) -> str:
    return _KIND_NAMES.get(k, "?")
