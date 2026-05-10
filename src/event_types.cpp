#include "event_types.h"
#include <cstring>

namespace dl {

const char* ev_op_name(EvOp op) noexcept {
    switch (op) {
        case EvOp::INIT:                return "INIT";
        case EvOp::DESTROY:             return "DESTROY";
        case EvOp::LOCK_PRE:            return "LOCK_PRE";
        case EvOp::LOCK_POST:           return "LOCK_POST";
        case EvOp::TRYLOCK_PRE:         return "TRYLOCK_PRE";
        case EvOp::TRYLOCK_POST:        return "TRYLOCK_POST";
        case EvOp::TIMEDLOCK_PRE:       return "TIMEDLOCK_PRE";
        case EvOp::TIMEDLOCK_POST:      return "TIMEDLOCK_POST";
        case EvOp::UNLOCK:              return "UNLOCK";
        case EvOp::RDLOCK_PRE:          return "RDLOCK_PRE";
        case EvOp::RDLOCK_POST:         return "RDLOCK_POST";
        case EvOp::TRYRDLOCK_PRE:       return "TRYRDLOCK_PRE";
        case EvOp::TRYRDLOCK_POST:      return "TRYRDLOCK_POST";
        case EvOp::TIMEDRDLOCK_PRE:     return "TIMEDRDLOCK_PRE";
        case EvOp::TIMEDRDLOCK_POST:    return "TIMEDRDLOCK_POST";
        case EvOp::WRLOCK_PRE:          return "WRLOCK_PRE";
        case EvOp::WRLOCK_POST:         return "WRLOCK_POST";
        case EvOp::TRYWRLOCK_PRE:       return "TRYWRLOCK_PRE";
        case EvOp::TRYWRLOCK_POST:      return "TRYWRLOCK_POST";
        case EvOp::TIMEDWRLOCK_PRE:     return "TIMEDWRLOCK_PRE";
        case EvOp::TIMEDWRLOCK_POST:    return "TIMEDWRLOCK_POST";
        case EvOp::COND_WAIT_PRE:       return "COND_WAIT_PRE";
        case EvOp::COND_WAIT_POST:      return "COND_WAIT_POST";
        case EvOp::COND_TIMEDWAIT_PRE:  return "COND_TIMEDWAIT_PRE";
        case EvOp::COND_TIMEDWAIT_POST: return "COND_TIMEDWAIT_POST";
    }
    return "?";
}

const char* ev_kind_name(EvKind k) noexcept {
    switch (k) {
        case EvKind::MUTEX:  return "MUTEX";
        case EvKind::RWLOCK: return "RWLOCK";
        case EvKind::SPIN:   return "SPIN";
        case EvKind::COND:   return "COND";
    }
    return "?";
}

bool ev_op_from_name(const char* s, EvOp& out) noexcept {
    for (int i = 1; i <= static_cast<int>(EvOp::COND_TIMEDWAIT_POST); ++i) {
        EvOp op = static_cast<EvOp>(i);
        if (strcmp(s, ev_op_name(op)) == 0) { out = op; return true; }
    }
    return false;
}

bool ev_kind_from_name(const char* s, EvKind& out) noexcept {
    for (int i = 1; i <= static_cast<int>(EvKind::COND); ++i) {
        EvKind k = static_cast<EvKind>(i);
        if (strcmp(s, ev_kind_name(k)) == 0) { out = k; return true; }
    }
    return false;
}

}  // namespace dl
