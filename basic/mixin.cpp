#include <iostream>
#include <cstdint>
#include <string>
#include <functional>

// ========== 定义风控事件类型 ==========
struct RiskEvent {
    std::string type;   // 事件类型："order_size_exceed"
    std::string msg;    // 事件描述
    uint32_t size;      // 触发事件的下单量
};

// ========== CRTP Mixin 1：日志功能（监听风控事件） ==========
template <typename Derived>
class WithLog {
public:
    // 日志模块监听风控事件的回调
    void on_risk_event(const RiskEvent& event) {
        std::cout << "[日志-风控] " << event.type << "：" << event.msg << "\n";
    }
};

// ========== CRTP Mixin 2：风控功能（只抛事件，不处理） ==========
template <typename Derived>
class WithRiskControl {
public:
    bool check_order_size(uint32_t size) {
        if (size > MAX_ORDER_SIZE) {
            // 1. 构造风控事件
            RiskEvent event{
                "order_size_exceed",
                "下单量超限：" + std::to_string(size),
                size
            };
            // 2. 抛出事件（调用子类的事件分发方法）
            static_cast<Derived*>(this)->dispatch_risk_event(event);
            return false;
        }
        return true;
    }

protected:
    static constexpr uint32_t MAX_ORDER_SIZE = 1000;
};

// ========== 核心策略：整合事件分发 ==========
class MyStrategy
    : public WithLog<MyStrategy>
    , public WithRiskControl<MyStrategy>
{
    friend class WithLog<MyStrategy>;
    friend class WithRiskControl<MyStrategy>;

public:
    // 事件分发中心：解耦事件产生和处理
    void dispatch_risk_event(const RiskEvent& event) {
        // 日志模块处理风控事件（可按需添加/删除）
        this->on_risk_event(event);
        
        // 想加告警？只需要加this->on_risk_alert(event);
        // 想加风控记录？只需要加this->record_risk_event(event);
    }
};

// ========== 测试 ==========
int main() {
    MyStrategy s;
    s.check_order_size(1500); // 风控抛事件 → 日志处理事件

    // 想删掉日志？只需要：
    // 1. 删掉继承WithLog<MyStrategy>
    // 2. 注释掉dispatch_risk_event里的on_risk_event
    // 风控模块完全不受影响
    return 0;
}