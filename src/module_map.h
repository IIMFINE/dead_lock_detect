#pragma once
//
// 进程已加载模块快照 —— 写入 trace 头部供离线 analyzer 反向定位 PC。
//
// 出发点：runtime 不再做符号化，只把 (pc) 写进 trace；要把 pc 翻回 file:line
// 必须知道当时 pc 落在哪个 .so 的哪个偏移上。所以日志打开时立刻 dump 一段
// 模块地址表（基址、跨度、Build-ID、路径），后面所有 F 行只剩裸 PC，离线
// 时由 analyzer 用 addr2line 解符号。
//
// 输出格式（每个模块一行，制表符分隔）：
//   M\t<idx>\t<base-hex>\t<size-hex>\t<build-id-hex|->\t<path>
//
// build-id 取自 ELF 的 PT_NOTE/NT_GNU_BUILD_ID；缺失时写 "-"。
// 主程序 dl_iterate_phdr 给的 name 是空串，回退到 /proc/self/exe。

#include <cstdio>

namespace dl {

// 把当前进程的模块表写到 fp（紧跟 HEADER 之后调用）。返回写出条目数；
// 失败时返回 -1，写过的部分仍保留在 fp 里。
int dump_module_map(FILE* fp);

}  // namespace dl
