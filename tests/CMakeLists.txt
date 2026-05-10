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
