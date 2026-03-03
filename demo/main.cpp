#include "AsyncLogger.h"
#include "FixedBlockMemPool.h"
#include "QuantSPSCLockFreeQueue.h"

#pragma pack(push, 1)

struct MarketData {
    int32_t instrument_id;
    double last_price;
    double ask1;
    double bid1;
    int32_t ask1_volume;
    int32_t bid1_volume;
    int64_t timestamp;
};

struct Order {
    int32_t instrument_id;
    double price;
    int32_t volume;
    bool is_buy;
    int32_t order_id;
    int64_t create_time;
};

constexpr size_t QUEUE_CAPACITY = 1024 * 8; // 8K orders
QuantSPSCLockFreeQueue<Order> order_queue(QUEUE_CAPACITY);
QuantSPSCLockFreeQueue<MarketData> market_data_queue(QUEUE_CAPACITY);
FixedBlockMemPool order_pool(sizeof(Order), QUEUE_CAPACITY);
AsyncLogger logger("trading_system.log");

// UDP market data receiver thread
void market_data_producer() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> price_dist(100.0, 0.1);

    while (true) {
        MarketData md;
        md.instrument_id = 1;
        md.last_price = price_dist(gen);
        md.ask1 = md.last_price + 0.01;
        md.bid1 = md.last_price - 0.01;
        md.ask1_volume = 100;
        md.bid1_volume = 100;
        md.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        if (!market_data_queue.enqueue(md)) {
            logger.log("Market data queue is full, dropping data for instrument " + std::to_string(md.instrument_id));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(300)); // Simulate 1ms market data interval
    }
}

// strategy thread, consumes market data and produces orders
void strategy_consumer_producer() {
    MarketData md;
    int64_t order_id = 0;

    while (true) {
        if (market_data_queue.dequeue(md)) {
            double spread = md.ask1 - md.bid1;
            if (spread > 0.02) {
                Order* order = static_cast<Order*>(order_pool.allocate());
                if (!order) {
                    logger.log("Order pool exhausted, cannot create new order for instrument " + std::to_string(md.instrument_id));
                    continue;
                }
                order->instrument_id = md.instrument_id;
                order->price = md.ask1;
                order->volume = 10;
                order->is_buy = true;
                order->order_id = ++order_id;
                order->create_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();

                if (!order_queue.enqueue(*order)) {
                    logger.log("Order queue is full, dropping order for instrument " + std::to_string(order->instrument_id));
                    order_pool.deallocate(order);
                } else {
                    char log_msg[128];
                    std::snprintf(
                        log_msg,
                        sizeof(log_msg),
                        "Created order: id=%d, price=%.2f, volume=%d",
                        order->order_id,
                        order->price,
                        order->volume
                    );
                    logger.log(log_msg);
                }
            }
        }
    }
}
 
// order gateway thread, consumes orders and simulates sending to exchange
void order_gateway_worker() {
    Order order;
    while (true) {
        if (order_queue.dequeue(order)) {
            int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            int64_t latency = now - order.create_time;
            char log_msg[128];
            std::snprintf(
                log_msg,
                sizeof(log_msg),
                "Sent order: id=%d, latency=%lld ns",
                order.order_id,
                latency
            );
            logger.log(log_msg);
            order_pool.deallocate(&order);
        }
    }
}

int main() {
    std::thread market_data_thread(market_data_producer);
    std::thread strategy_thread(strategy_consumer_producer);
    std::thread order_gateway_thread(order_gateway_worker);

    market_data_thread.join();
    strategy_thread.join();
    order_gateway_thread.join();

    return 0;
}

#pragma pack(pop)