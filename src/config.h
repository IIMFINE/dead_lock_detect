#pragma once

namespace dl {

struct Config {
    bool disabled          = false;
    const char* trace_path = nullptr;   // 运行时 dump trace 的输出路径
    int  bt_depth          = 16;
    int  bt_skip           = 3;
    int  max_locks         = 1'000'000;
    int  max_edges         = 2'000'000;
    int  dump_signal       = 0;         // 0 表示不注册
};

const Config& config();
void init_config_from_env();

}  // namespace dl
