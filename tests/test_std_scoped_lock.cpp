// std::scoped_lock 是 C++17 新增的 variadic RAII 锁。
// 配合 CTAD、fold expression 与 if constexpr 一起使用。
// 注意：劫持层记录真实 pthread_mutex_lock 顺序，所以同进程内多次调用
// 必须保持一致的锁序，否则会留下反向依赖边（这是检测器的正确行为）。
#include <mutex>
#include <type_traits>

static std::mutex A;
static std::mutex B;
static std::mutex C;

template <typename... Ms>
static void lock_all_and_release(Ms&... ms) {
    static_assert(sizeof...(Ms) >= 1, "need at least one mutex");
    static_assert(((std::is_same_v<std::remove_reference_t<Ms>, std::mutex>) && ...),
                  "only std::mutex supported here");
    std::scoped_lock lk(ms...);           // CTAD -> scoped_lock<Ms...>
    if constexpr (sizeof...(Ms) == 1) {
        (void)lk;
    } else {
        (void)lk;
    }
}

int main() {
    lock_all_and_release(A);              // 单参退化
    lock_all_and_release(A, B);           // 两锁
    lock_all_and_release(A, B, C);        // 三锁，与上面同序
    return 0;
}
