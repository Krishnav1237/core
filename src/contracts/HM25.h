#include <string>
#include <unordered_map>
#include <stdexcept>
#include <cstdint>
#include <utility>
#include <algorithm>

using namespace QPI;

// --- Optimized CustomHash for unordered_map ---
struct CustomHash {
    static constexpr uint64_t splitmix64(uint64_t x) noexcept {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }

    size_t operator()(const std::string& s) const noexcept {
        static const std::hash<std::string> hasher;
        return splitmix64(hasher(s));
    }
};

// --- CollateralVault Component ---
struct UserBalance {
    double totalCollateral = 0.0;

    // Optimized operations
    inline void deposit(double amount) noexcept { totalCollateral += amount; }
    inline bool canWithdraw(double amount) const noexcept { return totalCollateral >= amount; }
    inline void withdraw(double amount) noexcept { totalCollateral -= amount; }
};

class CollateralVault {
public:
    void depositCollateral(const std::string& user, double amount) {
        if (amount <= 0.0) [[unlikely]] {
            throw std::invalid_argument("Deposit must be positive");
        }
        balances_[user].deposit(amount);
    }

    void withdrawCollateral(const std::string& user, double amount) {
        if (amount <= 0.0) [[unlikely]] {
            throw std::invalid_argument("Withdrawal must be positive");
        }

        auto it = balances_.find(user);
        if (it == balances_.end() || !it->second.canWithdraw(amount)) [[unlikely]] {
            throw std::runtime_error("Insufficient collateral");
        }

        it->second.withdraw(amount);
    }

    double getBalance(const std::string& user) const noexcept {
        auto it = balances_.find(user);
        return it == balances_.end() ? 0.0 : it->second.totalCollateral;
    }

    // Optimized check without exception
    bool hasBalance(const std::string& user, double amount) const noexcept {
        auto it = balances_.find(user);
        return it != balances_.end() && it->second.canWithdraw(amount);
    }

private:
    std::unordered_map<std::string, UserBalance, CustomHash> balances_;
};

// --- PriceOracle Component ---
class PriceOracle {
public:
    void updatePrice(double price) {
        if (price <= 0.0) [[unlikely]] {
            throw std::invalid_argument("Price must be positive");
        }
        latestPrice_ = price;
    }

    double getPrice() const noexcept { return latestPrice_; }

    bool hasValidPrice() const noexcept { return latestPrice_ > 0.0; }

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

    // Optimized PnL calculation
    inline double calculatePnL(double currentPrice) const noexcept {
        const double priceDiff = isLong ? (currentPrice - entryPrice) : (entryPrice - currentPrice);
        return priceDiff * (size / entryPrice);
    }

    inline double getMargin() const noexcept {
        return size / leverage;
    }

    inline double calculateLiquidationThreshold(double maintenanceMarginRatio) const noexcept {
        return -getMargin() * (1.0 - maintenanceMarginRatio);
    }
};

class PositionManager {
public:
    explicit PositionManager(CollateralVault& vault) : vault_(vault) {}

    void openPosition(const std::string& trader, double margin, bool isLong, double leverage, double currentPrice) {
        if (margin <= 0.0 || leverage <= 0.0) [[unlikely]] {
            throw std::invalid_argument("Margin and leverage must be positive");
        }

        if (currentPrice <= 0.0) [[unlikely]] {
            throw std::invalid_argument("Current price must be positive");
        }

        auto it = positions_.find(trader);
        if (it != positions_.end() && it->second.isOpen) [[unlikely]] {
            throw std::runtime_error("Position already open");
        }

        // Check collateral before withdrawing
        if (!vault_.hasBalance(trader, margin)) [[unlikely]] {
            throw std::runtime_error("Insufficient collateral");
        }

        vault_.withdrawCollateral(trader, margin);

        const double positionSize = margin * leverage;
        Position pos{trader, currentPrice, positionSize, leverage, isLong, true};
        positions_[trader] = std::move(pos);
    }

    void closePosition(const std::string& trader, double currentPrice) {
        if (currentPrice <= 0.0) [[unlikely]] {
            throw std::invalid_argument("Current price must be positive");
        }

        auto it = positions_.find(trader);
        if (it == positions_.end() || !it->second.isOpen) [[unlikely]] {
            throw std::runtime_error("No open position");
        }

        Position& pos = it->second;
        const double pnl = pos.calculatePnL(currentPrice);
        const double returnAmount = std::max(0.0, pos.getMargin() + pnl);

        vault_.depositCollateral(trader, returnAmount);
        pos.isOpen = false;
    }

    const Position& getPosition(const std::string& trader) const {
        auto it = positions_.find(trader);
        if (it == positions_.end() || !it->second.isOpen) [[unlikely]] {
            throw std::runtime_error("No open position");
        }
        return it->second;
    }

    bool hasOpenPosition(const std::string& trader) const noexcept {
        auto it = positions_.find(trader);
        return it != positions_.end() && it->second.isOpen;
    }

private:
    CollateralVault& vault_;
    std::unordered_map<std::string, Position, CustomHash> positions_;
};

// --- LiquidationEngine Component ---
class LiquidationEngine {
public:
    LiquidationEngine(PositionManager& pm, PriceOracle& oracle) : pm_(pm), oracle_(oracle) {}

    bool checkLiquidation(const std::string& trader, double maintenanceMarginRatio = 0.1) noexcept {
        try {
            if (!pm_.hasOpenPosition(trader)) return false;

            const double currentPrice = oracle_.getPrice();
            if (currentPrice <= 0.0) return false;

            const Position& pos = pm_.getPosition(trader);
            const double pnl = pos.calculatePnL(currentPrice);
            const double liquidationThreshold = pos.calculateLiquidationThreshold(maintenanceMarginRatio);

            if (pnl < liquidationThreshold) {
                pm_.closePosition(trader, currentPrice);
                return true;
            }
        } catch (...) {
            // Silently handle exceptions as per original design
        }
        return false;
    }

private:
    PositionManager& pm_;
    PriceOracle& oracle_;
};

// --- Qubic Contract HM25 (merged) ---
struct HM25 : public ContractBase {
    // Qubic input/output structs
    struct Echo_input {};
    struct Echo_output {};
    struct Burn_input {};
    struct Burn_output {};
    struct GetStats_input {};
    struct GetStats_output {
        uint64 numberOfEchoCalls;
        uint64 numberOfBurnCalls;
    };

    // On-chain state
    uint64 numberOfEchoCalls;
    uint64 numberOfBurnCalls;

    // Shared DEX modules
    CollateralVault vault;
    PriceOracle oracle;
    PositionManager pm{vault};
    LiquidationEngine engine{pm, oracle};

    // Echo: return invocation reward to caller
    PUBLIC_PROCEDURE(Echo)
        ++state.numberOfEchoCalls;
        const auto reward = qpi.invocationReward();
        if (reward > 0) [[likely]] {
            qpi.transfer(qpi.invocator(), reward);
        }
    _

    // Burn: destroy invocation reward
    PUBLIC_PROCEDURE(Burn)
        ++state.numberOfBurnCalls;
        const auto reward = qpi.invocationReward();
        if (reward > 0) [[likely]] {
            qpi.burn(reward);
        }
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
        // DEX components are initialized by their constructors
    _
};