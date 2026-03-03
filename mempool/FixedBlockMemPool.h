#include <cstdlib>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <thread>
#include <iostream>
#include <vector>

constexpr size_t CACHE_LINE_SIZE = 64;

class FixedBlockMemPool {
public:
    FixedBlockMemPool(size_t block_size, size_t block_count) : block_size_(block_size), block_count_(block_count) {
        size_t total_size = block_size_ * block_count_;
        base_ptr_ = static_cast<char*>(std::aligned_alloc(CACHE_LINE_SIZE, total_size));

        if (base_ptr_ == nullptr) {
            throw std::bad_alloc();
        }

        free_list_.reserve(block_count);
        for (size_t i = 0; i < block_count; ++i) {
            free_list_.push_back(base_ptr_ + i * block_size);
        }

    }

    ~FixedBlockMemPool() noexcept {
        std::free(base_ptr_);
    }

    void* allocate() {
        if (free_list_.empty()) {
            return nullptr;
        }

        void* ptr = free_list_.back();
        free_list_.pop_back();

        return ptr;
    }

    void deallocate(void* ptr) {
        if (ptr == nullptr) {
            return;
        }

        free_list_.push_back(ptr);
    }
private:
    char* base_ptr_; // Base pointer to the memory pool
    size_t block_size_; // Size of each block
    size_t block_count_; // Total number of blocks
    std::vector<void*> free_list_; // List of free blocks
};