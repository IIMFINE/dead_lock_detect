#include "report.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ostream>

namespace dl::analyzer {

namespace {

void print_bt(std::ostream& o, const Bt& bt, const char* indent) {
    if (bt.empty()) { o << indent << "<empty>\n"; return; }
    char line[1024];
    for (size_t i = 0; i < bt.size(); ++i) {
        const auto& f = bt[i];
        if (!f.file.empty()) {
            const char* fname = f.file.c_str();
            const char* slash = strrchr(fname, '/');
            snprintf(line, sizeof(line),
                     "%s#%zu  0x%016lx  %s+0x%lx  (%s)  at %s:%d\n",
                     indent, i, (unsigned long)f.pc,
                     f.func.c_str(), (unsigned long)f.offset,
                     f.module.c_str(),
                     slash ? slash + 1 : fname, f.line);
        } else {
            snprintf(line, sizeof(line),
                     "%s#%zu  0x%016lx  %s+0x%lx  (%s)\n",
                     indent, i, (unsigned long)f.pc,
                     f.func.c_str(), (unsigned long)f.offset,
                     f.module.c_str());
        }
        o << line;
    }
}

}  // namespace

int report(std::ostream& o, const Graph& g, bool rwlock_strict, int max_per_scc) {
    auto cycles = find_cycles(g, max_per_scc);

    if (!rwlock_strict) {
        cycles.erase(std::remove_if(cycles.begin(), cycles.end(), [](const Cycle& cy){
            for (const auto& e : cy.edges) {
                if (!(e.info->from_kind == LockKind::RwlockRd &&
                      e.info->to_kind   == LockKind::RwlockRd)) return false;
            }
            return true;
        }), cycles.end());
    }

    o << "=== Deadlock Detector Report ===\n";
    o << "nodes=" << g.meta.size()
      << " edges=" << g.edges.size()
      << " cycles=" << cycles.size() << "\n";

    if (cycles.empty()) { o << "(no cycles detected)\n=== End Report ===\n"; return 0; }

    int ci = 0;
    for (const auto& cy : cycles) {
        ++ci;
        o << "\n-- Cycle #" << ci << " (length=" << cy.edges.size() << ") --\n";
        for (const auto& e : cy.edges) {
            auto mit = g.meta.find(e.from);
            if (mit == g.meta.end()) continue;
            char line[256];
            snprintf(line, sizeof(line),
                     "  Lock 0x%lx [%s] gen=%u\n",
                     (unsigned long)e.from.addr,
                     kind_name(e.info->from_kind),
                     e.from.gen);
            o << line;
            if (!mit->second.init_bt.empty()) {
                o << "    init at:\n";
                print_bt(o, mit->second.init_bt, "      ");
            }
        }
        for (const auto& e : cy.edges) {
            char line[256];
            snprintf(line, sizeof(line),
                     "  Edge 0x%lx [%s] -> 0x%lx [%s]   tid=%lu\n",
                     (unsigned long)e.from.addr, kind_name(e.info->from_kind),
                     (unsigned long)e.to.addr,   kind_name(e.info->to_kind),
                     (unsigned long)e.info->tid);
            o << line;
            o << "    holding 0x" << std::hex << e.from.addr << std::dec << " at:\n";
            print_bt(o, e.info->bt_from, "      ");
            o << "    requesting 0x" << std::hex << e.to.addr << std::dec << " at:\n";
            print_bt(o, e.info->bt_to, "      ");
        }
    }
    o << "=== End Report ===\n";
    return static_cast<int>(cycles.size());
}

}  // namespace dl::analyzer
