#include "QuantFixedMemPool.h"

#include <iostream>
#include <thread>
#include <chrono>

#pragma pack(push, 1)

struct Order {
    int32_t instrument_id;
    double price;
    int32_t volume;
    bool is_buy;
    int64_t order_id;
    int64_t create_time;
};

#pragma pack(pop)

int main() {
    QuantFixedMemPool pool(sizeof(Order), 1024);
    std::cout 
        << "Memory pool initialized with block size: "
        << pool.block_size() << " bytes"
        << " and block count: " << 1024
        << std::endl;

    constexpr int TEST_COUNT = 500;
    Order* orders[TEST_COUNT] = {nullptr};

    for (int i = 0; i < TEST_COUNT; ++i) {
        orders[i] = static_cast<Order*>(pool.alloc());
        if (orders[i] == nullptr) {
            throw std::runtime_error("Allocation failed at index " + std::to_string(i));
        }

        orders[i]->instrument_id = 1;
        orders[i]->price = 100.5 + i;
        orders[i]->volume = 1000 + i;
        orders[i]->is_buy = (i % 2 == 0);
        orders[i]->order_id = 10000 + i;
        orders[i]->create_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    std::cout << "Allocated " << TEST_COUNT << " orders. "
              << "Alloc count: " << pool.alloc_count() 
              << ", Free count: " << pool.free_count() 
              << std::endl;

    for (int i = 0; i < TEST_COUNT; ++i) {
        pool.free(orders[i]);
        orders[i] = nullptr;
    }

    std::cout << "Freed " << TEST_COUNT << " orders. "
              << "Alloc count: " << pool.alloc_count() 
              << ", Free count: " << pool.free_count() 
              << std::endl;

    constexpr int PERF_TEST_COUNT = 1000000;
    auto start_time = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < PERF_TEST_COUNT; ++i) {
        void* ptr = pool.alloc();
        pool.free(ptr);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    std::cout << "Performance test completed in " << duration.count() << " microseconds." << std::endl;

    return 0;
}

