cmake_minimum_required(VERSION 3.15)
project(deadlock_detect CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter -fno-omit-frame-pointer)

# 内部耗时埋点开关：cmake -DDL_PROFILE=ON 时打开（默认关闭，零运行时开销）
option(DL_PROFILE "Enable internal profiling counters" OFF)
if(DL_PROFILE)
    add_compile_definitions(DL_PROFILE_ENABLED=1)
    message(STATUS "DL profiling: ENABLED")
endif()

# runtime：仅采集事件。符号化已挪到离线 analyzer，runtime 不再依赖 libbacktrace。
add_library(deadlock_detect SHARED
    src/real_symbols.cpp
    src/backtrace.cpp
    src/bypass.cpp
    src/thread_state.cpp
    src/config.cpp
    src/event_types.cpp
    src/event_log.cpp
    src/backend.cpp
    src/module_map.cpp
    src/interpose_mutex.cpp
    src/interpose_rwlock.cpp
    src/interpose_spin.cpp
    src/interpose_cond.cpp
    src/lib_init.cpp
)
set_target_properties(deadlock_detect PROPERTIES OUTPUT_NAME deadlock_detect)
target_link_options(deadlock_detect PRIVATE -Wl,--no-undefined -Wl,-z,now -rdynamic)
target_link_libraries(deadlock_detect PRIVATE pthread dl)

# 离线分析器现在是 analyzer/ 下的 Python 实现（dl_analyzer 包 + wrapper 脚本），
# 不再由 CMake 构建。tests/ 通过 wrapper 调用即可。

enable_testing()
add_subdirectory(tests)
