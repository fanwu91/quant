#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

// 单生产者-单消费者（SPSC）无锁环形队列
// 核心特性：
// 1. 生产者独占write_idx_，消费者独占read_idx_，无并发写冲突
// 2. read_idx_cache_是生产者私有变量，仅生产者线程访问/修改
// 3. 队列大小必须是2的幂（保证& MASK等价于取模，提升性能）
template <typename T>
class SPSCLockFreeQueue {
public:
    // 构造函数：初始化队列大小（必须是2的幂）
    explicit SPSCLockFreeQueue(size_t capacity) 
        : capacity_(next_power_of_two(capacity))
        , MASK_(capacity_ - 1)
        , slots_(capacity_)
        , read_idx_cache_(0)  // 生产者私有缓存：仅生产者线程修改/访问
    {
        if (capacity_ < 2) {
            throw std::invalid_argument("队列容量必须≥2且为2的幂");
        }
        // 原子变量初始化（默认memory_order_relaxed）
        write_idx_.store(0, std::memory_order_relaxed);
        read_idx_.store(0, std::memory_order_relaxed);
    }

    // 禁止拷贝/移动（避免原子变量和缓存的竞争）
    SPSCLockFreeQueue(const SPSCLockFreeQueue&) = delete;
    SPSCLockFreeQueue& operator=(const SPSCLockFreeQueue&) = delete;
    SPSCLockFreeQueue(SPSCLockFreeQueue&&) = delete;
    SPSCLockFreeQueue& operator=(SPSCLockFreeQueue&&) = delete;

    ~SPSCLockFreeQueue() = default;

    // 生产者：尝试入队（无阻塞，noexcept保证不抛异常）
    bool try_push(const T& item) noexcept {
        // 1. 读生产者自己的写索引（relaxed：无内存序约束，仅自己访问）
        const size_t wi = write_idx_.load(std::memory_order_relaxed);
        const size_t next_wi = (wi + 1) & MASK_;

        // 2. 先对比生产者私有缓存（普通内存访问，极廉价）
        //    这个缓存仅生产者线程修改，无并发问题，是线程私有！
        if (next_wi == read_idx_cache_) {
            // 3. 缓存显示队列满，刷新缓存（acquire：保证后续能看到消费者写的slots_）
            read_idx_cache_ = read_idx_.load(std::memory_order_acquire);
            // 4. 再次判断：真满则返回false
            if (next_wi == read_idx_cache_) {
                return false;
            }
        }

        // 5. 写数据到环形缓冲区（此时消费者看不到，因为write_idx_未更新）
        slots_[wi] = item;

        // 6. 发布写索引（release：保证之前的slots_写操作对消费者可见）
        write_idx_.store(next_wi, std::memory_order_release);

        return true;
    }

    // 消费者：尝试出队（无阻塞，noexcept保证不抛异常）
    bool try_pop(T& item) noexcept {
        // 1. 读消费者自己的读索引（relaxed：仅自己访问）
        const size_t ri = read_idx_.load(std::memory_order_relaxed);
        // 2. 读生产者的写索引（acquire：保证后续能看到生产者写的slots_）
        const size_t wi = write_idx_.load(std::memory_order_acquire);

        // 3. 队列为空
        if (ri == wi) {
            return false;
        }

        // 4. 读数据（此时生产者不会覆盖，因为write_idx_ > ri）
        item = slots_[ri];

        // 5. 发布读索引（release：保证之前的slots_读操作完成）
        const size_t next_ri = (ri + 1) & MASK_;
        read_idx_.store(next_ri, std::memory_order_release);

        return true;
    }

    // 获取队列当前元素数量（仅供参考，无锁场景下不保证精确）
    size_t size() const noexcept {
        const size_t wi = write_idx_.load(std::memory_order_relaxed);
        const size_t ri = read_idx_.load(std::memory_order_relaxed);
        return (wi - ri) & MASK_;
    }

    // 检查队列是否为空（仅供参考）
    bool empty() const noexcept {
        return write_idx_.load(std::memory_order_relaxed) == read_idx_.load(std::memory_order_relaxed);
    }

private:
    // 辅助函数：计算大于等于n的最小2的幂（保证MASK=capacity-1的位运算有效）
    static size_t next_power_of_two(size_t n) {
        if (n == 0) return 2;
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    // ========== 核心变量 ==========
    const size_t capacity_;  // 队列容量（2的幂）
    const size_t MASK_;      // 容量-1，用于环形缓冲区取模
    std::vector<T> slots_;   // 环形缓冲区

    // 生产者独占变量（仅生产者线程访问/修改）
    std::atomic<size_t> write_idx_;  // 生产者写索引（原子变量，消费者只读）
    size_t read_idx_cache_;          // 🔥 生产者私有缓存：read_idx_的副本（非原子！线程私有！）

    // 消费者独占变量（仅消费者线程访问/修改）
    std::atomic<size_t> read_idx_;   // 消费者读索引（原子变量，生产者只读）
};

// ========== 测试代码（验证线程私有和性能优化） ==========
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    // 初始化队列（容量8，2的幂）
    SPSCLockFreeQueue<int> queue(8);

    // 生产者线程：持续入队
    std::thread producer([&queue]() {
        int count = 0;
        while (count < 1000000) {  // 入队100万次
            if (queue.try_push(count)) {
                count++;
            }
            // 模拟生产耗时（可选）
            // std::this_thread::yield();
        }
        std::cout << "生产者完成：入队" << count << "个元素\n";
    });

    // 消费者线程：持续出队
    std::thread consumer([&queue]() {
        int item = 0;
        int count = 0;
        while (count < 1000000) {  // 出队100万次
            if (queue.try_pop(item)) {
                count++;
            }
            // 模拟消费耗时（可选）
            // std::this_thread::yield();
        }
        std::cout << "消费者完成：出队" << count << "个元素\n";
    });

    // 等待线程结束
    producer.join();
    consumer.join();

    std::cout << "队列最终大小：" << queue.size() << "\n";
    return 0;
}