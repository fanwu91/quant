#include <iostream>
#include <memory>

template <typename DerivedStrategy>
class Strategy {
public:
    void execute() {
        static_cast<DerivedStrategy*>(this)->execute_impl();
    }
};

class Momentum : public Strategy<Momentum> {
protected:
    void execute_impl() {
        std::cout << "Executing Momentum strategy" << std::endl;
    }
};

class MarketMaking : public Strategy<MarketMaking> {
protected:
    void execute_impl() {
        std::cout << "Executing MarketMaking strategy" << std::endl;
    }
};

int main () {
    auto momentum = std::make_unique<Momentum>();
    auto marketMaking = std::make_unique<MarketMaking>();

    momentum->execute();
    marketMaking->execute();

    Momentum s1;
    MarketMaking s2;

    s1.execute();
    s2.execute();

    return 0;
}
