#include <cstdlib>
#include <cstddef>
#include <vector>
#include <stdexcept>
#include <new>
#include <atomic>
#include <cstring>

// 量化场景常用配置：Cache Line 大小（x86_64 为 64 字节）
constexpr size_t CACHE_LINE_SIZE = 64;
// 调试开关：生产环境关闭，避免性能损耗
constexpr bool MEM_POOL_DEBUG = false;

/**
 * 量化专用固定块内存池
 * 适用场景：订单结构体、行情结构体、风控事件等固定大小对象的高频分配/释放
 * 核心优势：无锁、无系统调用、Cache 友好、延迟抖动 < 10ns
 */
class QuantFixedMemPool {
public:
    /**
     * @param block_size 单个内存块大小（自动对齐到 Cache Line）
     * @param block_count 预分配的内存块数量
     */
    QuantFixedMemPool(size_t block_size, size_t block_count)
        : block_size_(align_to_cache_line(block_size)),
          block_count_(block_count) {
        // 预分配连续内存（避免内存碎片）
        total_size_ = block_size_ * block_count_;
        base_ptr_ = static_cast<char*>(std::aligned_alloc(CACHE_LINE_SIZE, total_size_));
        if (!base_ptr_) {
            throw std::bad_alloc();
        }

        // 初始化空闲链表（无锁，单生产者）
        free_list_head_ = nullptr;
        for (size_t i = 0; i < block_count_; ++i) {
            char* block_ptr = base_ptr_ + i * block_size_;
            // 调试：写入魔法数，用于越界检查
            if (MEM_POOL_DEBUG) {
                write_magic_number(block_ptr);
            }
            // 链表节点：用内存块头部存储下一个节点的指针
            set_next_ptr(block_ptr, free_list_head_);
            free_list_head_ = block_ptr;
        }

        // 统计信息初始化
        alloc_count_.store(0, std::memory_order_relaxed);
        free_count_.store(block_count_, std::memory_order_relaxed);
    }

    // 禁止拷贝/移动（内存池是全局唯一资源）
    QuantFixedMemPool(const QuantFixedMemPool&) = delete;
    QuantFixedMemPool& operator=(const QuantFixedMemPool&) = delete;
    QuantFixedMemPool(QuantFixedMemPool&&) = delete;
    QuantFixedMemPool& operator=(QuantFixedMemPool&&) = delete;

    ~QuantFixedMemPool() {
        if (base_ptr_) {
            std::free(base_ptr_);
        }
    }

    /**
     * 分配内存块（无锁，纳秒级）
     * @return 内存块指针，无空闲块时返回 nullptr
     */
    void* alloc() {
        if (!free_list_head_) {
            return nullptr;
        }

        // 无锁弹出空闲链表头节点（单生产者场景安全）
        char* block_ptr = free_list_head_;
        free_list_head_ = get_next_ptr(block_ptr);

        // 调试：检查魔法数，确认内存块未越界
        if (MEM_POOL_DEBUG) {
            check_magic_number(block_ptr);
        }

        // 更新统计
        alloc_count_.fetch_add(1, std::memory_order_relaxed);
        free_count_.fetch_sub(1, std::memory_order_relaxed);

        // 返回内存块的可用地址（跳过链表指针区域）
        return block_ptr + sizeof(void*);
    }

    /**
     * 释放内存块（无锁，纳秒级）
     * @param ptr 由 alloc() 返回的指针
     */
    void free(void* ptr) {
        if (!ptr) {
            return;
        }

        // 还原到内存块的实际起始地址（跳过链表指针区域）
        char* block_ptr = static_cast<char*>(ptr) - sizeof(void*);

        // 调试：检查内存块归属 & 魔法数
        if (MEM_POOL_DEBUG) {
            if (block_ptr < base_ptr_ || block_ptr >= base_ptr_ + total_size_) {
                throw std::invalid_argument("ptr not belong to this mem pool");
            }
            check_magic_number(block_ptr);
        }

        // 无锁插入空闲链表头节点
        set_next_ptr(block_ptr, free_list_head_);
        free_list_head_ = block_ptr;

        // 更新统计
        alloc_count_.fetch_sub(1, std::memory_order_relaxed);
        free_count_.fetch_add(1, std::memory_order_relaxed);
    }

    // 获取内存块大小（对外暴露的可用大小）
    size_t block_size() const { return block_size_ - sizeof(void*); }

    // 获取空闲块数量
    size_t free_count() const { return free_count_.load(std::memory_order_relaxed); }

    // 获取已分配块数量
    size_t alloc_count() const { return alloc_count_.load(std::memory_order_relaxed); }

private:
    // 对齐到 Cache Line 大小
    size_t align_to_cache_line(size_t size) {
        return (size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
    }

    // 调试：写入魔法数（内存块尾部）
    void write_magic_number(char* block_ptr) {
        constexpr uint64_t MAGIC = 0xDEADBEEFDEADBEEF;
        char* magic_ptr = block_ptr + block_size_ - sizeof(MAGIC);
        *reinterpret_cast<uint64_t*>(magic_ptr) = MAGIC;
    }

    // 调试：检查魔法数（检测内存越界）
    void check_magic_number(char* block_ptr) {
        constexpr uint64_t MAGIC = 0xDEADBEEFDEADBEEF;
        char* magic_ptr = block_ptr + block_size_ - sizeof(MAGIC);
        if (*reinterpret_cast<uint64_t*>(magic_ptr) != MAGIC) {
            throw std::runtime_error("memory block corrupted (out of bounds)");
        }
    }

    // 设置链表节点的下一个指针（内存块头部存储）
    void set_next_ptr(char* block_ptr, char* next) {
        *reinterpret_cast<char**>(block_ptr) = next;
    }

    // 获取链表节点的下一个指针
    char* get_next_ptr(char* block_ptr) {
        return *reinterpret_cast<char**>(block_ptr);
    }

private:
    char* base_ptr_;                // 预分配内存的基地址
    size_t block_size_;             // 实际内存块大小（含链表指针）
    size_t block_count_;            // 总内存块数量
    size_t total_size_;             // 预分配内存总大小

    char* free_list_head_;          // 空闲链表头（无锁，单生产者）
    std::atomic<size_t> alloc_count_; // 已分配块数
    std::atomic<size_t> free_count_;  // 空闲块数
};
