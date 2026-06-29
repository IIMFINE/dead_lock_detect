// 离线分析器（v2）：
//
// 读事件流，重建每个线程的持锁栈，按"持有 X 时请求 Y"的规则建依赖图，
// 跑 Tarjan + DFS 找环，输出报告。
//
// 运行时不再做任何分析。
#include "trace_reader.h"
#include "graph.h"
#include "report.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

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
    using namespace dl::analyzer;

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
