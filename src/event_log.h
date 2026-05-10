#pragma once

#include "event_types.h"
#include <cstdio>
#include <cstdint>

namespace dl {

// 事件日志（v2）。文本格式，但 op/kind 用数值，减小体积、避免字符串比较：
//
// HEADER\tDEADLOCK_EVENTS\tv2\tpid=<pid>
// E\t<ts_ns>\t<tid>\t<op_int>\t<kind_int>\t0x<addr>\t<rc_or_flags>\t<frame_count>
// F\t0x<pc>\t<func>\t<module>\t0x<offset>
// ...
//
// LOCK_PRE 在调用真实 lock 之前写入（捕捉"请求"瞬间），真死锁卡在 real lock 里时
// LOCK_POST 永不出现；这由离线分析器识别。

void log_init(const char* path);
void log_close();
bool log_enabled();

void log_event(EvOp op, EvKind kind, const void* addr,
               long rc_or_flags, int frame_skip, int frame_depth);

}  // namespace dl
