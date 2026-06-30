#include "symbolizer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>

namespace dl::analyzer {

namespace {

struct SymResult {
    std::string func;
    std::string file;
    int         line = 0;
    std::string module;   // basename
    uintptr_t   offset = 0;
};

bool file_exists(const std::string& p) {
    struct stat st;
    return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string basename_of(const std::string& path) {
    auto pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

// 在搜索路径里按 basename 找；命中第一个存在的文件即返回。
std::string resolve_module_file(const Module& m,
                                const std::vector<std::string>& search_paths) {
    if (file_exists(m.path)) return m.path;
    std::string bn = basename_of(m.path);
    if (bn.empty() || bn == "-") return {};
    for (const auto& dir : search_paths) {
        std::string cand = dir;
        if (!cand.empty() && cand.back() != '/') cand += '/';
        cand += bn;
        if (file_exists(cand)) return cand;
    }
    return {};
}

// 按 base 升序的 modules 上二分：找到包含 pc 的那个 module；找不到返回 nullptr。
const Module* find_module(const ModuleMap& modules, uintptr_t pc) {
    // upper_bound by base，再回退一位
    auto it = std::upper_bound(
        modules.begin(), modules.end(), pc,
        [](uintptr_t v, const Module& m) { return v < m.base; });
    if (it == modules.begin()) return nullptr;
    --it;
    if (pc < it->base || pc >= it->base + it->size) return nullptr;
    return &*it;
}

// 用双向管道开一个子进程：父进程的 to_child 可写、from_child 可读。
// 走 fork+execvp 避免 popen 不能双工。
struct Child {
    pid_t pid = -1;
    FILE* in  = nullptr;   // 父→子 stdin
    FILE* out = nullptr;   // 子 stdout→父
};

Child spawn_addr2line(const std::string& exe_path) {
    Child c;
    int p2c[2], c2p[2];
    if (pipe(p2c) != 0) return c;
    if (pipe(c2p) != 0) { close(p2c[0]); close(p2c[1]); return c; }

    pid_t pid = fork();
    if (pid < 0) {
        close(p2c[0]); close(p2c[1]); close(c2p[0]); close(c2p[1]);
        return c;
    }
    if (pid == 0) {
        // child
        dup2(p2c[0], 0);
        dup2(c2p[1], 1);
        close(p2c[0]); close(p2c[1]); close(c2p[0]); close(c2p[1]);
        // -f: 输出函数名；-C: demangle；-e: 目标 ELF。
        // 注意：故意不带 -i —— 它会把内联帧也展开，但没有分隔符，无法稳定按
        // "每输入一对输出"对齐解析。这里牺牲 inline 展开换稳定的两行一对协议。
        execlp("addr2line", "addr2line", "-f", "-C", "-e",
               exe_path.c_str(), (char*)nullptr);
        _exit(127);
    }
    // parent
    close(p2c[0]);
    close(c2p[1]);
    c.pid = pid;
    c.in  = fdopen(p2c[1], "w");
    c.out = fdopen(c2p[0], "r");
    return c;
}

void close_child(Child& c) {
    if (c.in)  { fclose(c.in);  c.in  = nullptr; }
    if (c.out) { fclose(c.out); c.out = nullptr; }
    if (c.pid > 0) {
        int st = 0;
        waitpid(c.pid, &st, 0);
        c.pid = -1;
    }
}

// 解析 addr2line 输出的 "file:line"。失败 line=0；列号若存在（"file:line (discriminator N)"）忽略。
void parse_file_line(const std::string& s, std::string& file, int& line) {
    file.clear(); line = 0;
    if (s == "??:0" || s == "??:?") return;
    auto pos = s.rfind(':');
    if (pos == std::string::npos) { file = s; return; }
    file = s.substr(0, pos);
    std::string tail = s.substr(pos + 1);
    // tail 可能形如 "42" 或 "42 (discriminator 3)" 或 "?"
    int v = 0;
    for (char ch : tail) {
        if (ch >= '0' && ch <= '9') { v = v * 10 + (ch - '0'); }
        else break;
    }
    line = v;
}

// 对一个 module 内全部 offset 跑 addr2line，把结果写回 results（key=pc）。
// addr2line 对每个输入地址输出至少两行（函数+文件:行），如果有 inline 帧会输出多对，
// 以"再次输出一对（最外层）"或紧接的下一个地址输入分隔。我们用 `-i` 时每个输入地址
// 后跟若干对，再跟一个分隔符行（实际上是空行——不同 binutils 版本行为不一）。
// 为了稳健：每输入一个地址后写一个哨兵地址（0），用它的输出做分界。
void symbolize_one_module(const std::string& exe_path,
                          const std::vector<uintptr_t>& offsets,
                          const std::vector<uintptr_t>& pcs,
                          std::unordered_map<uintptr_t, SymResult>& results,
                          const std::string& module_basename) {
    Child c = spawn_addr2line(exe_path);
    if (c.pid < 0 || !c.in || !c.out) {
        // 起子进程失败：所有 PC 退化为只填模块信息
        for (size_t i = 0; i < pcs.size(); ++i) {
            SymResult& r = results[pcs[i]];
            r.module = module_basename;
            r.offset = offsets[i];
        }
        if (c.pid > 0) close_child(c);
        return;
    }

    // 写：每个地址写一行；末尾写一个哨兵 "0" 让 addr2line 输出 "??\n??:0\n"，
    // 这样我们读两行就走下一个地址的解析。
    for (uintptr_t off : offsets) {
        std::fprintf(c.in, "0x%lx\n", (unsigned long)off);
    }
    std::fflush(c.in);
    fclose(c.in);
    c.in = nullptr;

    // 读：不带 -i 时，每个输入地址固定输出两行 —— func\nfile:line\n —— 严格按
    // 输入顺序一一对应。
    char* buf = nullptr;
    size_t cap = 0;
    auto read_line = [&](std::string& out) -> bool {
        ssize_t n = getline(&buf, &cap, c.out);
        if (n < 0) { out.clear(); return false; }
        out.assign(buf, static_cast<size_t>(n));
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        return true;
    };

    for (size_t i = 0; i < offsets.size(); ++i) {
        std::string fline, lline;
        if (!read_line(fline)) break;
        if (!read_line(lline)) break;
        SymResult r;
        r.module = module_basename;
        r.offset = offsets[i];
        r.func = (fline == "??") ? std::string() : std::move(fline);
        parse_file_line(lline, r.file, r.line);
        results[pcs[i]] = std::move(r);
    }
    free(buf);
    close_child(c);
}

}  // namespace

void symbolize_all(const ModuleMap& modules,
                   std::vector<Event>& events,
                   const std::vector<std::string>& search_paths) {
    if (events.empty()) return;

    // 1) 收集所有 PC（去重）
    std::unordered_set<uintptr_t> uniq;
    for (auto& ev : events) {
        for (auto& f : ev.bt) uniq.insert(f.pc);
    }
    if (uniq.empty()) return;

    // 2) 按模块分桶：模块 idx → (pcs, offsets)
    struct Bucket {
        const Module* m = nullptr;
        std::vector<uintptr_t> pcs;
        std::vector<uintptr_t> offsets;
    };
    std::unordered_map<const Module*, Bucket> buckets;
    std::unordered_map<uintptr_t, SymResult> results;

    for (uintptr_t pc : uniq) {
        const Module* m = find_module(modules, pc);
        if (!m) {
            SymResult r;
            r.offset = pc;     // 找不到模块，offset 退化为绝对地址
            results[pc] = std::move(r);
            continue;
        }
        Bucket& b = buckets[m];
        b.m = m;
        b.pcs.push_back(pc);
        b.offsets.push_back(pc - m->base);
    }

    // 3) 每个模块开一个 addr2line 进程，批量解析
    for (auto& kv : buckets) {
        Bucket& b = kv.second;
        std::string exe = resolve_module_file(*b.m, search_paths);
        std::string module_bn = basename_of(b.m->path);
        if (exe.empty()) {
            // 找不到 ELF 文件：所有 PC 退化为只填模块 basename + 偏移
            for (size_t i = 0; i < b.pcs.size(); ++i) {
                SymResult r;
                r.module = module_bn;
                r.offset = b.offsets[i];
                results[b.pcs[i]] = std::move(r);
            }
            std::fprintf(stderr,
                         "[deadlock_analyze] missing symbol file for module: %s "
                         "(set DEADLOCK_SYM_SEARCH_PATH or --sym-search-path)\n",
                         b.m->path.c_str());
            continue;
        }
        symbolize_one_module(exe, b.offsets, b.pcs, results, module_bn);
    }

    // 4) 写回每个事件的 Frame
    for (auto& ev : events) {
        for (auto& f : ev.bt) {
            auto it = results.find(f.pc);
            if (it == results.end()) continue;
            f.func   = it->second.func;
            f.file   = it->second.file;
            f.line   = it->second.line;
            f.module = it->second.module;
            f.offset = it->second.offset;
        }
    }
}

}  // namespace dl::analyzer
