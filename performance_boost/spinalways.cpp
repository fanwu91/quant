#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>

std::atomic<bool> data_ready { false };

void spin_wait() {
    auto start_time = std::chrono::high_resolution_clock::now();

    while (!data_ready.load(std::memory_order_acquire)) {
        // Busy-wait loop
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto wait_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
    std::cout << "Spin duration : " << wait_duration.count() << " nanoseconds." << std::endl;
}

void sleep_wait() {
    auto start_time = std::chrono::high_resolution_clock::now();

    while (!data_ready.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(1)); // Sleep for 1ns
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto wait_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
    std::cout << "Sleep duration: " << wait_duration.count() << " nanoseconds." << std::endl;
}

int main() {
    std::thread spin_thread(spin_wait);
    std::thread sleep_thread(sleep_wait);

    // Simulate some work before setting data_ready
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    data_ready.store(true, std::memory_order_release);

    spin_thread.join();
    sleep_thread.join();

    return 0;
}
