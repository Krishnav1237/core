#include <string>
#include <unordered_map>
include <stdexcept>
#include <cstdint>
using namespace QPI;

// --- CustomHash for unordered_map ---
struct CustomHash {
    static uint64_t splitmix64(uint64_t x) {
        x += 0x9e3779b97f4a7c15;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
        x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
        return x ^ (x >> 31);
    }
    size_t operator()(const std::string &s) const noexcept {
        static std::hash<std::string> hasher;
        return splitmix64(hasher(s));
    }
};

// --- CollateralVault Component ---
struct UserBalance { double totalCollateral = 0.0; };
class CollateralVault {
public:
    void depositCollateral(const std::string &user, double amount) {
        if (amount <= 0) throw std::invalid_argument("Deposit must be positive");
        balances_[user].totalCollateral += amount;
    }
    void withdrawCollateral(const std::string &user, double amount) {
        auto it = balances_.find(user);
        if (it == balances_.end() || it->second.totalCollateral < amount)
            throw std::runtime_error("Insufficient collateral");
        it->second.totalCollateral -= amount;
    }
    double getBalance(const std::string &user) const {
        auto it = balances_.find(user);
        return it == balances_.end() ? 0.0 : it->second.totalCollateral;
    }
private:
    std::unordered_map<std::string, UserBalance, CustomHash> balances_;
};

// --- PriceOracle Component ---
class PriceOracle {
public:
    void updatePrice(double price) { if (price <= 0) throw std::invalid_argument("Price must be positive"); latestPrice_ = price; }
    double getPrice() const { return latestPrice_; }
private:
    double latestPrice_ = 0.0;
};

// --- PositionManager Component ---
struct Position {
    std::string trader;
    double entryPrice;
    double size;
    double leverage;
    bool isLong;
    bool isOpen;
};
class PositionManager {
public:
    PositionManager(CollateralVault &vault): vault_(vault) {}
    void openPosition(const std::string &trader, double margin, bool isLong, double leverage, double currentPrice) {
        if (margin <= 0 || leverage <= 0) throw std::invalid_argument("Margin/leverage must be positive");
        auto it = positions_.find(trader);
        if (it != positions_.end() && it->second.isOpen)
            throw std::runtime_error("Position already open");
        vault_.withdrawCollateral(trader, margin);
        Position pos{trader, currentPrice, margin*leverage, leverage, isLong, true};
        positions_[trader] = pos;
    }
    void closePosition(const std::string &trader, double currentPrice) {
        auto it = positions_.find(trader);
        if (it==positions_.end() || !it->second.isOpen)
            throw std::runtime_error("No open position");
        Position &pos = it->second;
        double diff = pos.isLong ? (currentPrice-pos.entryPrice) : (pos.entryPrice-currentPrice);
        double pnl = diff * (pos.size/pos.entryPrice);
        double ret = (pos.size/pos.leverage)+pnl;
        if (ret<0) ret=0;
        vault_.depositCollateral(trader, ret);
        pos.isOpen = false;
    }
    Position getPosition(const std::string &trader) const {
        auto it = positions_.find(trader);
        if (it==positions_.end()||!it->second.isOpen) throw std::runtime_error("No open position");
        return it->second;
    }
private:
    CollateralVault &vault_;
    std::unordered_map<std::string, Position, CustomHash> positions_;
};

// --- LiquidationEngine Component ---
class LiquidationEngine {
public:
    LiquidationEngine(PositionManager &pm, PriceOracle &oracle): pm_(pm), oracle_(oracle) {}
    void checkLiquidation(const std::string &trader, double maintenanceMarginRatio=0.1) {
        try {
            Position pos = pm_.getPosition(trader);
            double price = oracle_.getPrice();
            double diff = pos.isLong ? (price-pos.entryPrice) : (pos.entryPrice-price);
            double pnl = diff*(pos.size/pos.entryPrice);
            double margin = pos.size/pos.leverage;
            if (pnl < -margin*(1-maintenanceMarginRatio)) pm_.closePosition(trader, price);
        } catch(...) {}
    }
private:
    PositionManager &pm_;
    PriceOracle &oracle_;
};

// --- Qubic Contract HM25 (merged) ---
struct HM25 : public ContractBase {
    // Qubic input/output structs
    struct Echo_input{};
    struct Echo_output{};
    struct Burn_input{};
    struct Burn_output{};
    struct GetStats_input {};
    struct GetStats_output { uint64 numberOfEchoCalls; uint64 numberOfBurnCalls; };

    // On‑chain state
    uint64 numberOfEchoCalls;
    uint64 numberOfBurnCalls;
    // Shared DEX modules
    CollateralVault vault;
    PriceOracle    oracle;
    PositionManager pm{vault};
    LiquidationEngine engine{pm, oracle};

    // Echo: return invocation reward to caller
    PUBLIC_PROCEDURE(Echo)
        state.numberOfEchoCalls++;
        if(qpi.invocationReward()>0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
    _

    // Burn: destroy invocation reward
    PUBLIC_PROCEDURE(Burn)
        state.numberOfBurnCalls++;
        if(qpi.invocationReward()>0) qpi.burn(qpi.invocationReward());
    _

    // Get stats: how many calls
    PUBLIC_FUNCTION(GetStats)
        output.numberOfBurnCalls = state.numberOfBurnCalls;
        output.numberOfEchoCalls = state.numberOfEchoCalls;
    _

    // Register contract interface
    REGISTER_USER_FUNCTIONS_AND_PROCEDURES
        REGISTER_USER_PROCEDURE(Echo, 1);
        REGISTER_USER_PROCEDURE(Burn, 2);
        REGISTER_USER_FUNCTION(GetStats, 1);
    _

    INITIALIZE
        state.numberOfEchoCalls = 0;
        state.numberOfBurnCalls = 0;
        // Optional: initialize DEX state here
    _
};