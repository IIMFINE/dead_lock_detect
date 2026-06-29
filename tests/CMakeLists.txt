set(TESTS
    test_ab_ba
    test_three_cycle
    test_recursive
    test_cond_wait
    test_rwlock
    test_rwlock_read_read
    test_destroy_reuse
    test_clean
    test_single_thread_inversion
    test_std_mutex_ab_ba
    test_std_unique_lock_ab_ba
    test_std_scoped_lock
    test_std_shared_mutex_read_read
    test_async_pipeline
    test_async_fork
)

foreach(t ${TESTS})
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE pthread)
    set_target_properties(${t} PROPERTIES ENABLE_EXPORTS ON)
endforeach()

set(LIB_PATH $<TARGET_FILE:deadlock_detect>)
set(ANALYZER $<TARGET_FILE:deadlock_analyze>)
set(DL_OUT_DIR "/tmp/dead_lock_detect")

# 每个用例：先用 LD_PRELOAD 跑被测程序产生 trace，再调用分析器生成报告，再 grep 断言
function(add_dl_test name expect_fragment)
    add_test(NAME ${name} COMMAND bash -c
        "set -e; \
         mkdir -p ${DL_OUT_DIR}; \
         TRACE=${DL_OUT_DIR}/dl_${name}.trace; \
         REPORT=${DL_OUT_DIR}/dl_${name}.report; \
         rm -f $TRACE $REPORT; \
         LD_PRELOAD=${LIB_PATH} DEADLOCK_TRACE=$TRACE $<TARGET_FILE:${name}>; \
         ${ANALYZER} -o $REPORT $TRACE || true; \
         grep -q -e ${expect_fragment} $REPORT || { echo '--- report ---'; cat $REPORT; exit 1; }")
endfunction()

function(add_dl_test_no_cycle name)
    add_test(NAME ${name} COMMAND bash -c
        "set -e; \
         mkdir -p ${DL_OUT_DIR}; \
         TRACE=${DL_OUT_DIR}/dl_${name}.trace; \
         REPORT=${DL_OUT_DIR}/dl_${name}.report; \
         rm -f $TRACE $REPORT; \
         LD_PRELOAD=${LIB_PATH} DEADLOCK_TRACE=$TRACE $<TARGET_FILE:${name}>; \
         ${ANALYZER} -o $REPORT $TRACE; rc=$?; \
         if [ $rc -ne 0 ]; then echo '--- report ---'; cat $REPORT; exit 1; fi; \
         grep -q 'cycles=0' $REPORT || { echo '--- report ---'; cat $REPORT; exit 1; }")
endfunction()

add_dl_test(test_ab_ba        "'-- Cycle'")
add_dl_test(test_three_cycle  "'length=3'")
add_dl_test_no_cycle(test_recursive)
add_dl_test_no_cycle(test_cond_wait)
add_dl_test(test_rwlock       "'-- Cycle'")
add_dl_test_no_cycle(test_rwlock_read_read)
add_dl_test_no_cycle(test_destroy_reuse)
add_dl_test_no_cycle(test_clean)
add_dl_test(test_single_thread_inversion "'-- Cycle'")

# 基于 C++17 <mutex>/<shared_mutex> 的用例
add_dl_test(test_std_mutex_ab_ba        "'-- Cycle'")
add_dl_test(test_std_unique_lock_ab_ba  "'-- Cycle'")
add_dl_test_no_cycle(test_std_scoped_lock)
add_dl_test_no_cycle(test_std_shared_mutex_read_read)

# 异步管道回归：8 线程 × 100k 次 lock/unlock，断言 trace 中 LOCK_POST 行数对齐
# 新数据流不再生成 LOCK_PRE（lock-order inference 不需要），断言只检 POST=4
# op 值参见 src/event_types.h：LOCK_POST=4
add_test(NAME test_async_pipeline COMMAND bash -c
    "set -e; \
     mkdir -p ${DL_OUT_DIR}; \
     TRACE=${DL_OUT_DIR}/dl_test_async_pipeline.trace; \
     rm -f $TRACE; \
     LD_PRELOAD=${LIB_PATH} DEADLOCK_TRACE=$TRACE $<TARGET_FILE:test_async_pipeline>; \
     PRE=$(awk -F'\\t' '$1==\"E\" && $4==\"3\"' $TRACE | wc -l); \
     POST=$(awk -F'\\t' '$1==\"E\" && $4==\"4\"' $TRACE | wc -l); \
     echo PRE=$PRE POST=$POST; \
     [ \"$PRE\" = \"0\" ] || { echo FAIL pre should be 0 in new data flow; exit 1; }; \
     [ \"$POST\" = \"800000\" ] || { echo FAIL post; exit 1; }")

# fork 回归：父进程 trace 有事件、子进程不创建独立 trace 也不写父 trace
add_test(NAME test_async_fork COMMAND bash -c
    "set -e; \
     mkdir -p ${DL_OUT_DIR}; \
     TRACE=${DL_OUT_DIR}/dl_test_async_fork.trace; \
     rm -f $TRACE $TRACE.*; \
     LD_PRELOAD=${LIB_PATH} DEADLOCK_TRACE=$TRACE $<TARGET_FILE:test_async_fork>; \
     LINES=$(wc -l < $TRACE); \
     echo LINES=$LINES; \
     [ \"$LINES\" -gt 0 ] || { echo FAIL empty; exit 1; }; \
     ls $TRACE.* 2>/dev/null && { echo FAIL child trace exists; exit 1; } || true")

# benchmark 程序：不进入 ctest，由外部脚本调用
add_executable(bench_prodcons bench_prodcons.cpp)
target_link_libraries(bench_prodcons PRIVATE pthread)
target_compile_options(bench_prodcons PRIVATE -O2)
set_target_properties(bench_prodcons PROPERTIES ENABLE_EXPORTS ON)

# 单独量 glibc ::backtrace() 自身耗时
add_executable(bench_backtrace bench_backtrace.cpp)
target_link_libraries(bench_backtrace PRIVATE pthread)
target_compile_options(bench_backtrace PRIVATE -O2 -fno-omit-frame-pointer)
set_target_properties(bench_backtrace PROPERTIES ENABLE_EXPORTS ON)

# fp walk 对照测试（dlopen runtime，对比 glibc backtrace）
add_executable(test_fp_walk test_fp_walk.cpp)
target_link_libraries(test_fp_walk PRIVATE pthread dl)
target_compile_options(test_fp_walk PRIVATE -O2 -fno-omit-frame-pointer)
set_target_properties(test_fp_walk PROPERTIES ENABLE_EXPORTS ON)

# ThreadRing 独立单测（不链接 deadlock_detect）
add_executable(test_ring_buffer test_ring_buffer.cpp)
target_link_libraries(test_ring_buffer PRIVATE pthread)
add_test(NAME test_ring_buffer COMMAND test_ring_buffer)
