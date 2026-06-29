#include "trace_reader.h"

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

std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == '\\') out += '\\';
            else if (n == 't') out += '\t';
            else if (n == 'n') out += '\n';
            else { out += s[i]; out += n; }
            ++i;
        } else out += s[i];
    }
    return out;
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

bool read_events(std::istream& in, int& pid_out, std::vector<Event>& out) {
    std::string line;
    if (!std::getline(in, line)) return false;
    auto parts = split_tab(line);
    if (parts.size() < 3 || parts[0] != "HEADER" || parts[1] != "DEADLOCK_EVENTS")
        return false;
    pid_out = 0;
    for (auto& p : parts) if (p.rfind("pid=", 0) == 0) pid_out = std::stoi(p.substr(4));

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto p = split_tab(line);
        if (p.size() < 8 || p[0] != "E") continue;
        Event ev;
        ev.ts_ns = std::stoull(p[1]);
        ev.tid   = std::stoull(p[2]);
        ev.op    = static_cast<EvOp>(std::stoi(p[3]));
        ev.kind  = static_cast<EvKind>(std::stoi(p[4]));
        parse_hex(p[5], ev.addr);
        ev.rc    = std::stol(p[6]);
        size_t nf = std::stoul(p[7]);
        ev.bt.reserve(nf);
        for (size_t i = 0; i < nf; ++i) {
            if (!std::getline(in, line)) break;
            auto fp = split_tab(line);
            if (fp.size() < 5 || fp[0] != "F") break;
            Frame f;
            parse_hex(fp[1], f.pc);
            f.func   = unescape(fp[2]);
            f.module = unescape(fp[3]);
            parse_hex(fp[4], f.offset);
            if (fp.size() >= 7) {
                f.file = unescape(fp[5]);
                try { f.line = std::stoi(fp[6]); } catch (...) { f.line = 0; }
            }
            ev.bt.push_back(std::move(f));
        }
        out.push_back(std::move(ev));
    }
    return true;
}

}  // namespace dl::analyzer
