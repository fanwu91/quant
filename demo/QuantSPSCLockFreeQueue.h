#ifndef __QUANT_SPSC_LOCK_FREE_QUEUE_H__ 
#define __QUANT_SPSC_LOCK_FREE_QUEUE_H__ 

#include "Constants.h"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <random>

template <typename T>
class QuantSPSCLockFreeQueue {
public:
    explicit QuantSPSCLockFreeQueue(size_t capacity) : capacity_(capacity), mask_(capacity - 1) {
        if ((capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument("capacity must be a power of 2");
        }
        
        buffer_ = static_cast<T*>(std::aligned_alloc(CACHE_LINE_SIZE, capacity * sizeof(T)));

        if (buffer_ == nullptr) {
            throw std::bad_alloc();
        }
    }

    QuantSPSCLockFreeQueue(const QuantSPSCLockFreeQueue&) = delete;
    QuantSPSCLockFreeQueue& operator=(const QuantSPSCLockFreeQueue&) = delete;

    ~QuantSPSCLockFreeQueue() noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t tail = tail_.load(std::memory_order_relaxed);

        while (head != tail) {
            buffer_[head & mask_].~T();
            head = (head + 1) & mask_;
        }

        std::free(buffer_);
    }
    
    bool enqueue(T&& value) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t next_tail = (tail + 1) & mask_;

        // full
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        new (&buffer_[tail & mask_]) T(std::move(value));
        tail_.store(next_tail, std::memory_order_release);

        return true;
    }

    bool enqueue(const T& value) {
        T tmp = value;
        return enqueue(std::move(tmp));
    }

    bool dequeue(T& out) {
        size_t head = head_.load(std::memory_order_relaxed);

        // empty
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        out = std::move(buffer_[head & mask_]);
        buffer_[head & mask_].~T();
        head_.store(head + 1, std::memory_order_release);

        return true;
    }

    size_t size() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        return tail - head;
    }




private:
    const size_t capacity_;
    const size_t mask_;
    T* buffer_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_ {0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_ {0};
};

#endif
