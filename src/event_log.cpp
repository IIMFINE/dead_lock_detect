#include "event_log.h"
#include "backtrace.h"
#include "bypass.h"
#include "real_symbols.h"
#include "thread_state.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

namespace dl {

static FILE* g_fp = nullptr;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

bool log_enabled() { return g_fp != nullptr; }

static std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t";  break;
            case '\n': out += "\\n";  break;
            default:   out += c;
        }
    }
    return out;
}

void log_init(const char* path) {
    if (g_fp) return;
    char final_path[512];
    const char* pct = path ? strstr(path, "%p") : nullptr;
    if (pct) {
        std::string tmp = path;
        size_t pos = tmp.find("%p");
        char pidbuf[16];
        snprintf(pidbuf, sizeof(pidbuf), "%d", getpid());
        tmp.replace(pos, 2, pidbuf);
        snprintf(final_path, sizeof(final_path), "%s", tmp.c_str());
    } else if (path && *path) {
        snprintf(final_path, sizeof(final_path), "%s", path);
    } else {
        snprintf(final_path, sizeof(final_path), "/tmp/deadlock.%d.events", getpid());
    }
    g_fp = fopen(final_path, "w");
    if (!g_fp) {
        fprintf(stderr, "[deadlock] failed to open event log: %s\n", final_path);
        return;
    }
    setvbuf(g_fp, nullptr, _IOFBF, 1 << 20);
    fprintf(g_fp, "HEADER\tDEADLOCK_EVENTS\tv2\tpid=%d\n", getpid());
    fprintf(stderr, "[deadlock] event log: %s\n", final_path);
}

void log_close() {
    if (!g_fp) return;
    real::pthread_mutex_lock(&g_mu);
    fflush(g_fp);
    fclose(g_fp);
    g_fp = nullptr;
    real::pthread_mutex_unlock(&g_mu);
}

static uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull + ts.tv_nsec;
}

void log_event(EvOp op, EvKind kind, const void* addr,
               long rc_or_flags, int frame_skip, int frame_depth) {
    if (!g_fp) return;
    // 整个日志路径期间关闭劫持：libbacktrace/malloc/stdio 都可能再次进入
    // pthread_mutex_lock，不能把这些自身的加锁也记录到依赖图。
    ScopedBypass _bp;

    Backtrace bt = capture_backtrace(frame_skip, frame_depth);

    std::string buf;
    buf.reserve(256 + bt.size() * 96);
    char line[256];
    snprintf(line, sizeof(line),
             "E\t%lu\t%lu\t%d\t%d\t0x%lx\t%ld\t%zu\n",
             (unsigned long)now_ns(),
             (unsigned long)current_tid(),
             static_cast<int>(op),
             static_cast<int>(kind),
             (unsigned long)reinterpret_cast<uintptr_t>(addr),
             rc_or_flags,
             bt.size());
    buf.append(line);
    for (uintptr_t pc : bt) {
        SymbolInfo s = symbolize(pc);
        snprintf(line, sizeof(line), "F\t0x%lx\t", (unsigned long)pc);
        buf.append(line);
        buf.append(escape(s.function));
        buf.append("\t");
        buf.append(escape(s.module));
        buf.append("\t");
        snprintf(line, sizeof(line), "0x%lx\t", (unsigned long)s.offset);
        buf.append(line);
        buf.append(escape(s.file));
        buf.append("\t");
        snprintf(line, sizeof(line), "%d\n", s.line);
        buf.append(line);
    }

    real::pthread_mutex_lock(&g_mu);
    fwrite(buf.data(), 1, buf.size(), g_fp);
    real::pthread_mutex_unlock(&g_mu);
}

}  // namespace dl
