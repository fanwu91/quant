#pragma once

#include <iostream>
#include <thread>
#include <functional>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <stdexcept>

namespace hft {
namespace thread {

// HFT线程配置结构体（封装绑核、优先级参数）
struct HFTThreadConfig {
    int cpu_id = 1;                // 目标绑定核心（默认isolcpus隔离的core 1）
    int realtime_priority = 50;    // SCHED_FIFO优先级（1-99）
    bool validate_affinity = true; // 是否验证亲和性绑定结果
    bool restore_sched = false;    // 析构时是否恢复原调度策略（HFT场景一般设false）
};

// 核心封装类：剥离绑核、设优先级等通用逻辑
template <typename Func>
class HFTThread {
public:
    // 构造函数：接收配置和业务逻辑
    HFTThread(HFTThreadConfig config, Func&& business_logic)
        : config_(std::move(config)),
          business_logic_(std::forward<Func>(business_logic)),
          thread_(nullptr),
          original_policy_(0),
          original_sched_param_{0} {
        // 提前检查配置合法性
        validate_config();
    }

    // 禁止拷贝（线程不可拷贝），允许移动
    HFTThread(const HFTThread&) = delete;
    HFTThread& operator=(const HFTThread&) = delete;
    HFTThread(HFTThread&&) = default;
    HFTThread& operator=(HFTThread&&) = default;

    // 启动线程（核心：模板方法模式的流程骨架）
    void start() {
        if (thread_) {
            throw std::runtime_error("HFTThread already started");
        }

        // 启动线程，执行封装的流程
        thread_ = std::make_unique<std::thread>([this]() {
            // 步骤1：保存原调度策略（如需恢复）
            if (config_.restore_sched) {
                pthread_getschedparam(pthread_self(), &original_policy_, &original_sched_param_);
            }

            // 步骤2：绑定CPU核心（通用逻辑）
            if (!pin_to_cpu(config_.cpu_id)) {
                std::cerr << "Warning: Failed to pin thread to CPU " << config_.cpu_id << std::endl;
                if (config_.validate_affinity) return; // 验证失败则退出
            }

            // 步骤3：设置实时优先级（通用逻辑）
            if (!set_realtime_priority(config_.realtime_priority)) {
                std::cerr << "Warning: Failed to set realtime priority " << config_.realtime_priority << std::endl;
            }

            // 步骤4：验证亲和性（通用逻辑）
            if (config_.validate_affinity && !validate_affinity(config_.cpu_id)) {
                std::cerr << "Error: CPU affinity validation failed for CPU " << config_.cpu_id << std::endl;
                return;
            }

            // 步骤5：执行核心业务逻辑（开发者只需关注这部分）
            try {
                business_logic_();
            } catch (const std::exception& e) {
                std::cerr << "Business logic threw exception: " << e.what() << std::endl;
            }

            // 步骤6：恢复原调度策略（可选）
            if (config_.restore_sched) {
                pthread_setschedparam(pthread_self(), original_policy_, &original_sched_param_);
            }
        });
    }

    // 等待线程结束
    void join() {
        if (thread_) {
            thread_->join();
        }
    }

    ~HFTThread() {
        if (thread_ && thread_->joinable()) {
            thread_->join();
        }
    }

private:
    // 私有：验证配置合法性（通用逻辑）
    void validate_config() const {
        if (config_.cpu_id < 0 || config_.cpu_id >= CPU_SETSIZE) {
            throw std::invalid_argument("Invalid CPU ID: " + std::to_string(config_.cpu_id));
        }
        if (config_.realtime_priority < sched_get_priority_min(SCHED_FIFO) ||
            config_.realtime_priority > sched_get_priority_max(SCHED_FIFO)) {
            throw std::invalid_argument("Invalid realtime priority: " + std::to_string(config_.realtime_priority));
        }
    }

    // 私有：绑定CPU核心（通用逻辑）
    bool pin_to_cpu(int cpu_id) const {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);
        return pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0;
    }

    // 私有：设置实时优先级（通用逻辑）
    bool set_realtime_priority(int priority) const {
        sched_param param{.sched_priority = priority};
        return pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
    }

    // 私有：验证亲和性（通用逻辑）
    bool validate_affinity(int cpu_id) const {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        if (pthread_getaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0) {
            return false;
        }
        // 确保线程只绑定到目标核心（无其他核心）
        return CPU_ISSET(cpu_id, &cpuset) && CPU_COUNT(&cpuset) == 1;
    }

    // 成员变量
    HFTThreadConfig config_;
    Func business_logic_;
    std::unique_ptr<std::thread> thread_;
    int original_policy_;
    sched_param original_sched_param_;
};

// 便捷创建函数（简化调用，无需手动指定模板参数）
template <typename Func>
auto make_hft_thread(HFTThreadConfig config, Func&& business_logic) {
    return HFTThread<Func>(std::move(config), std::forward<Func>(business_logic));
}

}  // namespace thread
}  // namespace hft