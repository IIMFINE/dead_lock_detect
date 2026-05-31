#include "backtrace.h"
#include "bypass.h"
#include "config.h"

#include <execinfo.h>
#include <dlfcn.h>
#include <cxxabi.h>
#include <libgen.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <mutex>

#include <backtrace.h>   // libbacktrace (GCC)

namespace dl {

// ------------- 符号缓存（pc -> SymbolInfo） -------------
//
// 同一个进程里 PC -> 符号的映射在采集期内是稳定的；典型程序不同 PC 数量
// 远小于事件数，命中率拉满后 backend 的 symbolize 几乎零成本。
//
// 三个调用方：runtime 的 backend 线程、main 线程的 dump 路径、离线分析器
// （analyzer 进程内可能由多个解析路径并发触发）。统一用一把 std::mutex
// 保护，与 g_mod_mu 同款模式。
//   - runtime backend 线程整段 g_bypass_depth=1000，std::mutex 底层的
//     pthread_mutex_lock 会被我们的 wrapper 短路到 real:: 实现，不会回环；
//   - analyzer 进程没被 LD_PRELOAD，wrapper 不存在，更不需要 bypass。
static std::mutex                                g_sym_mu;
static std::unordered_map<uintptr_t, SymbolInfo> g_sym_cache;

static bool symbol_cache_lookup(uintptr_t pc, SymbolInfo& out) {
    std::lock_guard<std::mutex> lk(g_sym_mu);
    auto it = g_sym_cache.find(pc);
    if (it == g_sym_cache.end()) return false;
    out = it->second;
    return true;
}

void symbol_cache_put(uintptr_t pc, SymbolInfo info) {
    std::lock_guard<std::mutex> lk(g_sym_mu);
    g_sym_cache[pc] = std::move(info);
}

void symbol_cache_clear() {
    std::lock_guard<std::mutex> lk(g_sym_mu);
    g_sym_cache.clear();
}

// ------------- 抓栈 -------------
Backtrace capture_backtrace(int skip, int max_depth) {
    if (max_depth <= 0) return {};
    // 抓栈内部不能被我们自己重新劫持
    ScopedBypass _b;

    const int total = skip + max_depth + 2;
    void** buf = static_cast<void**>(alloca(sizeof(void*) * total));
    int n = ::backtrace(buf, total);
    int start = skip < n ? skip : n;
    Backtrace bt;
    bt.reserve(n - start);
    for (int i = start; i < n; ++i) {
        bt.push_back(reinterpret_cast<uintptr_t>(buf[i]));
    }
    return bt;
}

// ------------- libbacktrace per-module state cache -------------
namespace {

std::mutex g_mod_mu;
std::unordered_map<std::string, struct backtrace_state*> g_mods;

void bt_err_silent(void* /*data*/, const char* /*msg*/, int /*errnum*/) {}

struct backtrace_state* get_module_state(const char* path) {
    if (!path || !*path) return nullptr;
    std::lock_guard<std::mutex> lk(g_mod_mu);
    auto it = g_mods.find(path);
    if (it != g_mods.end()) return it->second;
    // threaded=1：多线程 runtime 需要 libbacktrace 内部自己加锁
    struct backtrace_state* st =
        backtrace_create_state(path, /*threaded=*/1, bt_err_silent, nullptr);
    g_mods.emplace(path, st);
    return st;
}

std::string demangle(const char* name) {
    if (!name || !*name) return "??";
    int status = 0;
    char* d = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string out = (status == 0 && d) ? d : name;
    if (d) free(d);
    return out;
}

struct PcInfoCtx {
    SymbolInfo* out;
    bool        filled_func;   // function 已填（来自 DWARF 或 symtab）
    bool        filled_line;   // file:line 已填（来自 DWARF）
};

int pcinfo_cb(void* data, uintptr_t /*pc*/, const char* filename,
              int lineno, const char* function) {
    auto* ctx = static_cast<PcInfoCtx*>(data);
    // 只取最内层命中（第一条）；若想展开 inline 帧，改成 return 0 继续
    if (function && *function && !ctx->filled_func) {
        ctx->out->function = demangle(function);
        ctx->filled_func = true;
    }
    if (filename && *filename && !ctx->filled_line) {
        ctx->out->file = filename;
        if (lineno > 0) ctx->out->line = lineno;
        ctx->filled_line = true;
    }
    return 0;
}

void syminfo_cb(void* data, uintptr_t /*pc*/, const char* symname,
                uintptr_t symval, uintptr_t /*symsize*/) {
    auto* ctx = static_cast<PcInfoCtx*>(data);
    if (symname && *symname && !ctx->filled_func) {
        ctx->out->function = demangle(symname);
        if (symval) ctx->out->offset = ctx->out->pc - symval;
        ctx->filled_func = true;
    }
}

}  // namespace

// ------------- symbolize -------------
SymbolInfo symbolize(uintptr_t pc) {
    SymbolInfo cached;
    if (symbol_cache_lookup(pc, cached)) return cached;

    SymbolInfo s{};
    s.pc = pc;

    // dladdr 先填 module + 粗粒度函数名（静态符号通常拿不到）
    Dl_info info{};
    const char* mod_path = nullptr;
    if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname) {
        mod_path = info.dli_fname;
        uintptr_t fbase = reinterpret_cast<uintptr_t>(info.dli_fbase);
        char* path_copy = strdup(info.dli_fname);
        s.module = basename(path_copy);
        free(path_copy);
        if (info.dli_sname) {
            s.function = demangle(info.dli_sname);
            s.offset   = pc - reinterpret_cast<uintptr_t>(info.dli_saddr);
        } else {
            s.function = "??";
            s.offset   = pc - fbase;
        }
    } else {
        s.module   = "??";
        s.function = "??";
        s.offset   = pc;
    }

    // 升级：libbacktrace 读 DWARF 拿 function + file:line
    if (mod_path) {
        if (auto* st = get_module_state(mod_path)) {
            PcInfoCtx ctx{&s, /*filled_func=*/(s.function != "??"),
                             /*filled_line=*/false};
            backtrace_pcinfo(st, pc, pcinfo_cb, bt_err_silent, &ctx);
            if (!ctx.filled_func) {
                backtrace_syminfo(st, pc, syminfo_cb, bt_err_silent, &ctx);
            }
        }
    }

    // 写回 cache：同一 PC 后续解析直接走 lookup，跳过 dladdr / DWARF。
    // 多个线程可能同时算同一个 PC，最后一次写覆盖前面的，结果等价。
    symbol_cache_put(pc, s);
    return s;
}

// ------------- 输出 -------------
void format_backtrace(std::ostream& out, const Backtrace& bt, const char* indent) {
    if (bt.empty()) {
        out << indent << "<empty backtrace>\n";
        return;
    }
    char line[1024];
    for (size_t i = 0; i < bt.size(); ++i) {
        SymbolInfo s = symbolize(bt[i]);
        if (!s.file.empty()) {
            const char* fname = s.file.c_str();
            const char* slash = strrchr(fname, '/');
            snprintf(line, sizeof(line),
                     "%s#%zu  0x%016lx  %s+0x%lx  (%s)  at %s:%d\n",
                     indent, i, (unsigned long)bt[i],
                     s.function.c_str(), (unsigned long)s.offset,
                     s.module.c_str(),
                     slash ? slash + 1 : fname, s.line);
        } else {
            snprintf(line, sizeof(line),
                     "%s#%zu  0x%016lx  %s+0x%lx  (%s)\n",
                     indent, i, (unsigned long)bt[i],
                     s.function.c_str(), (unsigned long)s.offset,
                     s.module.c_str());
        }
        out << line;
    }
}

}  // namespace dl
