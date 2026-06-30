#include "trace_reader.h"

#include <algorithm>
#include <istream>
#include <string>
#include <utility>

namespace dl::analyzer {

namespace {

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { parts.push_back(std::move(cur)); cur.clear(); }
        else cur += c;
    }
    parts.push_back(std::move(cur));
    return parts;
}

bool parse_hex(const std::string& s, uintptr_t& out) {
    size_t off = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) off = 2;
    out = 0;
    for (size_t i = off; i < s.size(); ++i) {
        char c = s[i]; int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        out = (out << 4) | d;
    }
    return s.size() > off;
}

}  // namespace

bool read_trace(std::istream& in, int& pid_out,
                ModuleMap& modules_out,
                std::vector<Event>& events_out) {
    std::string line;
    if (!std::getline(in, line)) return false;
    auto parts = split_tab(line);
    if (parts.size() < 3 || parts[0] != "HEADER" || parts[1] != "DEADLOCK_EVENTS")
        return false;
    // 严格要求 v3：trace 格式已不再带在线符号化字段，旧版本无法直接消费。
    if (parts[2] != "v3") return false;
    pid_out = 0;
    for (auto& p : parts) if (p.rfind("pid=", 0) == 0) pid_out = std::stoi(p.substr(4));

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto p = split_tab(line);
        if (p.empty()) continue;

        if (p[0] == "M") {
            // M\t<idx>\t<base>\t<size>\t<build-id|->\t<path>
            if (p.size() < 6) continue;
            Module m;
            try { m.idx = std::stoi(p[1]); } catch (...) { continue; }
            if (!parse_hex(p[2], m.base)) continue;
            if (!parse_hex(p[3], m.size)) continue;
            m.build_id = p[4];
            m.path     = p[5];
            modules_out.push_back(std::move(m));
            continue;
        }

        if (p[0] != "E") continue;
        if (p.size() < 8) continue;
        Event ev;
        try {
            ev.ts_ns = std::stoull(p[1]);
            ev.tid   = std::stoull(p[2]);
            ev.op    = static_cast<EvOp>(std::stoi(p[3]));
            ev.kind  = static_cast<EvKind>(std::stoi(p[4]));
        } catch (...) { continue; }
        if (!parse_hex(p[5], ev.addr)) continue;
        try { ev.rc = std::stol(p[6]); } catch (...) { ev.rc = 0; }
        size_t nf = 0;
        try { nf = std::stoul(p[7]); } catch (...) { continue; }
        ev.bt.reserve(nf);
        for (size_t i = 0; i < nf; ++i) {
            if (!std::getline(in, line)) break;
            auto fp = split_tab(line);
            // v3: F\t<pc>
            if (fp.size() < 2 || fp[0] != "F") break;
            Frame f;
            if (!parse_hex(fp[1], f.pc)) continue;
            ev.bt.push_back(std::move(f));
        }
        events_out.push_back(std::move(ev));
    }

    // 按 base 排序，方便后续按 PC 二分查找
    std::sort(modules_out.begin(), modules_out.end(),
              [](const Module& a, const Module& b) { return a.base < b.base; });
    return true;
}

}  // namespace dl::analyzer
