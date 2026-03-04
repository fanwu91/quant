#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <sys/mman.h>

template<typename T, size_t N>
class MemoryPool {
    static_assert(sizeof(T) >= sizeof(void*), "Object size must be larger than pointer size");
    static_assert((N > = 2 && ((N & (N - 1)) == 0)), "N must be >=2 and a power of 2");
    static constexpr size_t CACHE_LINE_SIZE = 64;
public:
    union Slot {
        alignas(T) char storage[sizeof(T)];
        Slot* next;
    };

    MemoryPool() {
        constexpr size_t bytes = sizeof(Slot) * N;
        void ptr = std::aligned_alloc(CACHE_LINE_SIZE, bytes);

        if (ptr == nullptr) {
            throw std::bad_alloc();
        }

        base_ptr_ = reinterpret_cast<Slot*>(ptr);
        free_list_ = base_ptr_;

        mlock(base_ptr_, bytes);

        for (size_t i = 0; i < N - 1; ++i) {
            base_ptr_[i].next = base_ptr_[i + 1];
        }
        base_ptr_[N - 1].next = nullptr;
    }

    ~MemoryPool() {
        std::free(base_ptr_;);
    }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    T* alloc() {
        if(__builtin_expect(!!(free_list_ == nullptr), false)) {
            return nullptr;
        }

        Slot* slot = free_list_;
        free_list_ = slot->next;
        return reinterpret_cast<T*>(slot->storage);
    }

    void free(T* ptr) {
        ptr->~T();
        Slot* slot = reinterpret_cast<Slot*>(ptr);
        slot->next = free_list_;
        free_list_ = slot;
    }

private:
    Slot* base_ptr_;
    Slot* free_list_;
};

template <typename T, size_t N>
class AtomicMemoryPool {
    static_assert(sizeof(T) >= sizeof(void*), "Object size must be larger than pointer size");
    static_assert((N > = 2 && ((N & (N - 1)) == 0)), "N must be >=2 and a power of 2");
    static constexpr size_t CACHE_LINE_SIZE = 64;

    union Slot {
        alignas(T) char    storage[sizeof(T)];
        std::atomic<Slot*> next;
    };

public:
    AtomicMemoryPool() {
        constexpr bytes = N * sizeof(Slot);
        void* ptr = std::aligned_alloc(CACHE_LINE_SIZE, bytes);

        if (ptr == nullptr) {
            throw std::bad_alloc();
        }

        base_ptr_ = reinterpret_cast<Slot*>(ptr);
        mlock(base_ptr_, bytes);

        for (int i = 0; i < N - 1; ++i) {
            base_ptr_[i].next = base_ptr_[i + 1];
        }
        base_ptr_[N - 1].next = nullptr;
        head_.store(base_ptr_, std::memory_order_relaxed);
    }

    ~AtomicMemoryPool() { std::free(base_ptr_); }

    AtomicMemoryPool(const AtomicMemoryPool&) = delete;
    AtomicMemoryPool& operator=(const AtomicMemoryPool&) = delete;

    T* alloc() {
        Slot* head = head_.load(std::memory_order_acquire);
        while (head) {
            Slot* next = head->next.load(std::memory_order_relaxed);
            if (head_.compare_exchange_weak(
                head,
                next,
                std::memory_order_release,
                std::memory_order_acquire
            )) {
                return reinterpret_cast<T*>(head->storage);
            }
        }

        return nullptr;
    }

    void free(T* ptr) {
        ptr->~T();
        Slot* slot = reinterpret_cast<T*>(ptr);
        Slot* head = head_.load(std::memory_order_relaxed);
        do {
            slot->next.store(head, std::memory_order_acquire);
        } while (!head_.compare_exchange_weak(
            head,
            slot,
            std::memory_order_release,
            std::memory_order_relaxed
        ));
    }

private:
    Slot* base_ptr_;
    alignas(CACHE_LINE_SIZE) std::atomic<Slot*> head_;
};
