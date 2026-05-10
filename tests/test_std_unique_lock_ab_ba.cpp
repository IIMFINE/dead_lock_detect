// 两线程 AB-BA：结构化绑定 + std::unique_lock + try_lock()，
// 其中用到 C++17 的 structured bindings 与 if-init-statement。
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <tuple>

static std::mutex A;
static std::mutex B;
static std::atomic<int> phase{0};

static auto take_pair() {
    // C++17：直接用聚合/元组返回，调用方使用 structured bindings 解包。
    return std::tuple{&A, &B};
}

static void th1_ab() {
    auto [first, second] = take_pair();           // structured bindings (C++17)
    std::unique_lock lk1(*first);                 // CTAD (C++17)
    phase.fetch_add(1);
    while (phase.load() < 2) std::this_thread::sleep_for(std::chrono::microseconds(100));
    if (std::unique_lock lk2(*second, std::try_to_lock); lk2) {  // if-init (C++17)
        (void)lk2;
    }
}

static void th2_ab() {
    auto [first, second] = take_pair();
    std::unique_lock lk1(*second);
    phase.fetch_add(1);
    while (phase.load() < 2) std::this_thread::sleep_for(std::chrono::microseconds(100));
    if (std::unique_lock lk2(*first, std::try_to_lock); lk2) {
        (void)lk2;
    }
}

int main() {
    std::thread t1(th1_ab);
    std::thread t2(th2_ab);
    t1.join();
    t2.join();
    return 0;
}
