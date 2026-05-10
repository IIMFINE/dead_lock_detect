#include "config.h"

#include <cstdlib>
#include <cstring>
#include <signal.h>

namespace dl {

static Config g_cfg;

const Config& config() { return g_cfg; }

static bool env_flag(const char* name) {
    const char* v = getenv(name);
    if (!v || !*v) return false;
    if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "no") == 0) return false;
    return true;
}

static int env_int(const char* name, int def) {
    const char* v = getenv(name);
    if (!v || !*v) return def;
    char* end = nullptr;
    long n = strtol(v, &end, 10);
    if (end == v) return def;
    return static_cast<int>(n);
}

static int parse_signal(const char* s) {
    if (!s || !*s) return 0;
    if (s[0] >= '0' && s[0] <= '9') return atoi(s);
    struct { const char* name; int sig; } tab[] = {
        {"SIGUSR1", SIGUSR1}, {"SIGUSR2", SIGUSR2}, {"USR1", SIGUSR1}, {"USR2", SIGUSR2},
    };
    for (auto& e : tab) if (strcmp(s, e.name) == 0) return e.sig;
    return 0;
}

void init_config_from_env() {
    g_cfg.disabled        = env_flag("DEADLOCK_DISABLE");
    g_cfg.trace_path      = getenv("DEADLOCK_TRACE");
    g_cfg.bt_depth        = env_int("DEADLOCK_BACKTRACE_DEPTH", 16);
    g_cfg.bt_skip         = env_int("DEADLOCK_SKIP_FRAMES", 3);
    g_cfg.max_locks       = env_int("DEADLOCK_MAX_LOCKS", 1'000'000);
    g_cfg.max_edges       = env_int("DEADLOCK_MAX_EDGES", 2'000'000);
    g_cfg.dump_signal     = parse_signal(getenv("DEADLOCK_DUMP_ON_SIGNAL"));
}

}  // namespace dl
