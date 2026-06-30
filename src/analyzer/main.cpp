// 离线分析器（v3）：
//
// 读事件流（含模块表），先用 addr2line 对所有 PC 做离线符号化，再重建每个
// 线程的持锁栈，按"持有 X 时请求 Y"的规则建依赖图，跑 Tarjan + DFS 找环，
// 输出报告。
#include "trace_reader.h"
#include "symbolizer.h"
#include "graph.h"
#include "report.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
    fprintf(stderr,
        "Usage: %s [options] <event-file>\n"
        "Options:\n"
        "  --rwlock-strict           Report pure rd->rd cycles too (default: skip)\n"
        "  --max-per-scc N           Max simple cycles per SCC (default: 32)\n"
        "  --sym-search-path DIRS    ':'-separated dirs to find .so by basename\n"
        "                            (overrides $DEADLOCK_SYM_SEARCH_PATH)\n"
        "  -o <file>                 Write report to <file> (default: stdout)\n"
        "  -h, --help                Show this help\n",
        argv0);
}

// 把 "a:b:c" 这种字符串切到 vector，跳过空段。
void split_paths(const char* s, std::vector<std::string>& out) {
    if (!s) return;
    std::string cur;
    for (const char* p = s; *p; ++p) {
        if (*p == ':') {
            if (!cur.empty()) out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur += *p;
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
}

}  // namespace

int main(int argc, char** argv) {
    using namespace dl::analyzer;

    bool rwlock_strict = false;
    int max_per_scc = 32;
    const char* out_path = nullptr;
    const char* ev_path = nullptr;
    const char* sym_path_cli = nullptr;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(argv[0]); return 0; }
        else if (!strcmp(a, "--rwlock-strict")) rwlock_strict = true;
        else if (!strcmp(a, "--max-per-scc") && i + 1 < argc) max_per_scc = atoi(argv[++i]);
        else if (!strcmp(a, "--sym-search-path") && i + 1 < argc) sym_path_cli = argv[++i];
        else if (!strcmp(a, "-o") && i + 1 < argc) out_path = argv[++i];
        else if (a[0] == '-') { fprintf(stderr, "unknown option: %s\n", a); usage(argv[0]); return 2; }
        else if (!ev_path) ev_path = a;
        else { fprintf(stderr, "unexpected: %s\n", a); usage(argv[0]); return 2; }
    }
    if (!ev_path) { usage(argv[0]); return 2; }

    std::ifstream ifs(ev_path);
    if (!ifs) { fprintf(stderr, "cannot open: %s\n", ev_path); return 1; }

    int pid = 0;
    ModuleMap modules;
    std::vector<Event> events;
    if (!read_trace(ifs, pid, modules, events)) {
        fprintf(stderr, "invalid event log (require v3): %s\n", ev_path);
        return 1;
    }

    // 解析符号搜索路径：CLI > 环境变量
    std::vector<std::string> search_paths;
    if (sym_path_cli) {
        split_paths(sym_path_cli, search_paths);
    } else {
        split_paths(getenv("DEADLOCK_SYM_SEARCH_PATH"), search_paths);
    }

    symbolize_all(modules, events, search_paths);

    Graph g;
    replay(events, g);

    std::ostream* out = &std::cout;
    std::ofstream ofs;
    if (out_path) {
        ofs.open(out_path, std::ios::out | std::ios::trunc);
        if (!ofs) { fprintf(stderr, "cannot open output: %s\n", out_path); return 1; }
        out = &ofs;
    }
    *out << "(source: " << ev_path << "  pid=" << pid
         << "  modules=" << modules.size()
         << "  events=" << events.size() << ")\n";
    int cycles = report(*out, g, rwlock_strict, max_per_scc);
    out->flush();
    return cycles > 0 ? 3 : 0;
}
