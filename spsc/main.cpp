#include "QuantSPSCLockFreeQueue.h"

struct TestMarketData {
    int32_t instrument_id;
    double last_price;
    double ask1;
    double bid1;
    int64_t timestamp;
};

constexpr size_t QUEUE_CAPACITY = 1024 * 1024; // 1 million
constexpr int64_t TEST_OPS = 10000000; // 10 million

void producer(QuantSPSCLockFreeQueue<TestMarketData>& queue, std::atomic<bool>& start_flag) {
    while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> price_dist(100.0, 0.1);

    for (int64_t i = 0; i < TEST_OPS; ++i) {
        TestMarketData md;
        md.instrument_id = 1;
        md.last_price = price_dist(gen);
        md.bid1 = md.last_price - 0.01;
        md.ask1 = md.last_price + 0.01;
        md.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        while (!queue.enqueue(std::move(md))) {
            std::this_thread::yield();
        }
    }
}

void consumer(QuantSPSCLockFreeQueue<TestMarketData>& queue, std::atomic<bool>& start_flag, std::vector<int64_t>& latencies) {
    while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    latencies.reserve(TEST_OPS);
    TestMarketData md;

    for (int64_t i = 0; i < TEST_OPS; ++i) {
        while (!queue.dequeue(md)) {
            std::this_thread::yield();
        }

        int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        latencies.push_back(now - md.timestamp);
    }
}

int main() {
    try {

        std::cout << "=== Quant SPSC Lock-Free Queue Benchmark ===" << std::endl;
        std::cout << "Queue Capacity: " << QUEUE_CAPACITY << std::endl;
        std::cout << "Test Operations: " << TEST_OPS << std::endl;

        QuantSPSCLockFreeQueue<TestMarketData> queue(QUEUE_CAPACITY);
        std::atomic<bool> start_flag { false };
        std::vector<int64_t> latencies;

        std::thread producer_thread(producer, std::ref(queue), std::ref(start_flag));
        std::thread consumer_thread(consumer, std::ref(queue), std::ref(start_flag), std::ref(latencies));

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto start_time = std::chrono::high_resolution_clock::now();
        start_flag.store(true, std::memory_order_release);
        
        producer_thread.join();
        consumer_thread.join();
        auto end_time = std::chrono::high_resolution_clock::now();

        // how many ops per second
        auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        double ops_per_sec = (TEST_OPS) / (total_duration / 1000000.0); 

        // latency stats
        std::sort(latencies.begin(), latencies.end());
        int64_t p50 = latencies[latencies.size() / 2];
        int64_t p99 = latencies[latencies.size() * 99 / 100];
        int64_t p999 = latencies[latencies.size() * 999 / 1000];
        int64_t max_latency = latencies.back();
        int64_t min_latency = latencies.front();

        // print results
        std::cout << std::endl;
        std::cout << "=== Benchmark Results ===" << std::endl;
        std::cout << "Total Duration: " << total_duration << " ms" << std::endl;
        std::cout << "Throughput: " << ops_per_sec / 1000000 << " M ops/sec" << std::endl;
        std::cout << "Latency Statistics (ns):" << std::endl;
        std::cout << "  P50: " << p50 << " ns" << std::endl;
        std::cout << "  P99: " << p99 << " ns" << std::endl;
        std::cout << "  P99.9: " << p999 << " ns" << std::endl;
        std::cout << "  Min: " << min_latency << " ns" << std::endl;
        std::cout << "  Max: " << max_latency << " ns" << std::endl;  
    } catch(const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    

    return 0;
}
