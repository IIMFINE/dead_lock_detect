#include "module_map.h"
#include "bypass.h"

#include <cstdint>
#include <cstring>
#include <elf.h>
#include <link.h>
#include <limits.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace dl {

namespace {

// 从一段 PT_NOTE 内存里找 NT_GNU_BUILD_ID（n_name="GNU"），命中后 hex 编码到 out。
// 命中返回 true；找不到返回 false。note 段格式：
//   Elf64_Nhdr { n_namesz, n_descsz, n_type } | name(n_namesz, 4 字节对齐) | desc(n_descsz, 4 字节对齐)
bool extract_build_id(const uint8_t* base, size_t size, std::string& out) {
    const uint8_t* p   = base;
    const uint8_t* end = base + size;
    while (p + sizeof(ElfW(Nhdr)) <= end) {
        ElfW(Nhdr) nh;
        std::memcpy(&nh, p, sizeof(nh));
        const uint8_t* name = p + sizeof(nh);
        const uint8_t* desc = name + ((nh.n_namesz + 3) & ~3u);
        const uint8_t* next = desc + ((nh.n_descsz + 3) & ~3u);
        if (next > end || next < p) return false;
        if (nh.n_type == NT_GNU_BUILD_ID && nh.n_namesz >= 4 &&
            std::memcmp(name, "GNU", 4) == 0 && nh.n_descsz > 0) {
            static const char hex[] = "0123456789abcdef";
            out.clear();
            out.reserve(nh.n_descsz * 2);
            for (uint32_t i = 0; i < nh.n_descsz; ++i) {
                out.push_back(hex[(desc[i] >> 4) & 0xF]);
                out.push_back(hex[desc[i] & 0xF]);
            }
            return true;
        }
        p = next;
    }
    return false;
}

struct ModuleEntry {
    uintptr_t   base = 0;
    uintptr_t   size = 0;
    std::string build_id = "-";
    std::string path;
};

int iter_cb(struct dl_phdr_info* info, size_t /*sz*/, void* data) {
    auto* out = static_cast<std::vector<ModuleEntry>*>(data);

    // size 取所有 PT_LOAD 段中 (p_vaddr + p_memsz) 的最大值；近似覆盖完整模块。
    uintptr_t span = 0;
    const uint8_t* note_seg = nullptr;
    size_t note_seg_size = 0;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type == PT_LOAD) {
            uintptr_t end = ph.p_vaddr + ph.p_memsz;
            if (end > span) span = end;
        } else if (ph.p_type == PT_NOTE && note_seg == nullptr) {
            note_seg      = reinterpret_cast<const uint8_t*>(info->dlpi_addr + ph.p_vaddr);
            note_seg_size = ph.p_memsz;
        }
    }
    if (span == 0) return 0;

    ModuleEntry e;
    e.base = info->dlpi_addr;
    e.size = span;
    e.path = (info->dlpi_name && info->dlpi_name[0])
                 ? info->dlpi_name
                 : std::string{};
    if (note_seg) {
        std::string bid;
        if (extract_build_id(note_seg, note_seg_size, bid)) {
            e.build_id = std::move(bid);
        }
    }
    out->push_back(std::move(e));
    return 0;
}

void fill_main_program_path(std::string& path) {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        path = buf;
    }
}

}  // namespace

int dump_module_map(FILE* fp) {
    if (!fp) return -1;
    ScopedBypass _bp;

    std::vector<ModuleEntry> mods;
    mods.reserve(64);
    dl_iterate_phdr(&iter_cb, &mods);

    int idx = 0;
    for (auto& m : mods) {
        if (m.path.empty()) fill_main_program_path(m.path);
        // 路径里可能含空白/制表符——按惯例 trace 文件不预期这种情况；保持原样。
        std::fprintf(fp, "M\t%d\t0x%lx\t0x%lx\t%s\t%s\n",
                     idx,
                     static_cast<unsigned long>(m.base),
                     static_cast<unsigned long>(m.size),
                     m.build_id.c_str(),
                     m.path.empty() ? "-" : m.path.c_str());
        ++idx;
    }
    return idx;
}

}  // namespace dl
