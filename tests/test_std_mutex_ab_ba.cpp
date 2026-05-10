// 两线程 AB-BA：使用 C++17 CTAD 的 std::lock_guard（无模板尖括号）。
// std::mutex 在 libstdc++ 下走 pthread_mutex_lock，会被劫持层捕获，
// 形成 A->B、B->A 的依赖环。
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <array>

static std::mutex A;
static std::mutex B;
static std::atomic<int> phase{0};

// C++17：if-with-init + CTAD。try_lock 成功就立即 unlock；失败也无妨——
// 劫持层在 pre_acquire 就把请求动作入图，足够构造环边。
template <typename M>
static void touch_try(M& m) {
    if (std::unique_lock lk(m, std::try_to_lock); lk.owns_lock()) {
        // CTAD 推导 std::unique_lock<M>
        (void)lk;
    }
}

static void th1_ab() {
    std::lock_guard lk(A);   // CTAD
    phase.fetch_add(1);
    while (phase.load() < 2) std::this_thread::sleep_for(std::chrono::microseconds(100));
    touch_try(B);
}

static void th2_ab() {
    std::lock_guard lk(B);   // CTAD
    phase.fetch_add(1);
    while (phase.load() < 2) std::this_thread::sleep_for(std::chrono::microseconds(100));
    touch_try(A);
}

int main() {
    std::array threads{std::thread(th1_ab), std::thread(th2_ab)};  // CTAD on std::array
    for (auto& t : threads) t.join();
    return 0;
}
