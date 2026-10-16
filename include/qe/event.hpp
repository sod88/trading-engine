#pragma once
// Core data types and the events that flow through the engine.
//
// The whole system is event-driven: the data feed produces MarketEvents,
// strategies react by producing OrderEvents, and the broker answers with
// FillEvents. Swapping the backtest broker for a live one later does not
// change anything above this layer.

#include <string>
#include <variant>

namespace qe {

struct Bar {
    std::string symbol;
    std::string date;  // ISO "YYYY-MM-DD"
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
};

enum class Side { Buy, Sell };

inline const char* to_string(Side s) { return s == Side::Buy ? "BUY" : "SELL"; }

struct Order {
    std::string symbol;
    Side side = Side::Buy;
    long quantity = 0;         // always positive; direction comes from `side`
    std::string created_date;  // date of the bar the decision was made on
};

struct Fill {
    std::string symbol;
    std::string date;  // date of the bar the order was executed on
    Side side = Side::Buy;
    long quantity = 0;
    double price = 0.0;  // execution price, slippage included
    double commission = 0.0;
};

struct MarketEvent { Bar bar; };
struct OrderEvent  { Order order; };
struct FillEvent   { Fill fill; };

using Event = std::variant<MarketEvent, OrderEvent, FillEvent>;

}  // namespace qe
