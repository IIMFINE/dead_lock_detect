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

# 探测 GCC 自带的 libbacktrace（ libbacktrace.a + backtrace.h，随 gcc 分发）
find_path(LIBBACKTRACE_INCLUDE_DIR backtrace.h
    HINTS /usr/lib/gcc/x86_64-linux-gnu/11/include
          /usr/lib/gcc/x86_64-linux-gnu/12/include
          /usr/lib/gcc/x86_64-linux-gnu/13/include
          /usr/lib/gcc/x86_64-linux-gnu/14/include
          /usr/include)
find_library(LIBBACKTRACE_LIB NAMES backtrace
    HINTS /usr/lib/gcc/x86_64-linux-gnu/11
          /usr/lib/gcc/x86_64-linux-gnu/12
          /usr/lib/gcc/x86_64-linux-gnu/13
          /usr/lib/gcc/x86_64-linux-gnu/14
          /usr/lib/x86_64-linux-gnu)
if(NOT LIBBACKTRACE_INCLUDE_DIR OR NOT LIBBACKTRACE_LIB)
    message(FATAL_ERROR "libbacktrace not found (expected GCC's libbacktrace.a + backtrace.h)")
endif()
message(STATUS "libbacktrace: ${LIBBACKTRACE_LIB} (inc=${LIBBACKTRACE_INCLUDE_DIR})")

# runtime：仅采集事件
add_library(deadlock_detect SHARED
    src/real_symbols.cpp
    src/backtrace.cpp
    src/thread_state.cpp
    src/config.cpp
    src/event_types.cpp
    src/event_log.cpp
    src/interpose_mutex.cpp
    src/interpose_rwlock.cpp
    src/interpose_spin.cpp
    src/interpose_cond.cpp
    src/lib_init.cpp
)
set_target_properties(deadlock_detect PROPERTIES OUTPUT_NAME deadlock_detect)
target_include_directories(deadlock_detect PRIVATE ${LIBBACKTRACE_INCLUDE_DIR})
target_link_options(deadlock_detect PRIVATE -Wl,--no-undefined -Wl,-z,now -rdynamic)
target_link_libraries(deadlock_detect PRIVATE ${LIBBACKTRACE_LIB} pthread dl)

# 离线分析器：独立二进制；只依赖事件类型与 backtrace 帮助
add_executable(deadlock_analyze
    src/analyzer_main.cpp
    src/event_types.cpp
    src/backtrace.cpp
    src/thread_state.cpp
    src/config.cpp
    src/real_symbols.cpp  # backtrace.cpp 引用了 real::，但分析器自身不用劫持；链接需要
)
set_target_properties(deadlock_analyze PROPERTIES ENABLE_EXPORTS ON)
target_include_directories(deadlock_analyze PRIVATE ${LIBBACKTRACE_INCLUDE_DIR})
target_link_libraries(deadlock_analyze PRIVATE ${LIBBACKTRACE_LIB} pthread dl)

enable_testing()
add_subdirectory(tests)
