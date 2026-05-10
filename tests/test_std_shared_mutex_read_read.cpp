// 使用 C++17 新增的 <shared_mutex> 里 std::shared_mutex，
// 以及 CTAD 的 std::shared_lock。两线程相反顺序只做共享加锁，
// 读-读边应被过滤，分析器不应报告环。
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

static std::shared_mutex A;
static std::shared_mutex B;
static std::atomic<int> ready{0};

template <auto N>
static void reader(std::shared_mutex& first, std::shared_mutex& second) {
    static_assert(N > 0);                       // auto NTTP (C++17)
    std::shared_lock lk1(first);                // CTAD
    ready.fetch_add(1);
    while (ready.load() < 2) std::this_thread::sleep_for(std::chrono::microseconds(100));
    std::shared_lock lk2(second);
    (void)lk1; (void)lk2;
}

int main() {
    std::vector<std::thread> ts;
    ts.emplace_back(reader<1>, std::ref(A), std::ref(B));
    ts.emplace_back(reader<2>, std::ref(B), std::ref(A));
    for (auto& t : ts) t.join();
    return 0;
}
