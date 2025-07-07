#include <string>
#include <unordered_map>
#include <stdexcept>
#include <cstdint>

// Remove the problematic using directive
// using namespace QPI;

// --- CustomHash for unordered_map ---
struct CustomHash {
    static uint64_t splitmix64(uint64_t x) {
        x += 0x9e3779b97f4a7c15;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
        x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
        return x ^ (x >> 31);
    }
    size_t operator()(const std::string &s) const {
        static std::hash<std::string> hasher;
        return splitmix64(hasher(s));
    }
};

// --- CollateralVault Component ---
struct UserBalance {
    double totalCollateral;
    UserBalance() : totalCollateral(0.0) {}
    UserBalance(double balance) : totalCollateral(balance) {}
};

class CollateralVault {
public:
    void depositCollateral(const std::string &user, double amount) {
        if (amount <= 0) throw std::invalid_argument("Deposit must be positive");
        if (balances_.find(user) == balances_.end()) {
            balances_[user] = UserBalance(0.0);
        }
        balances_[user].totalCollateral += amount;
    }

    void withdrawCollateral(const std::string &user, double amount) {
        std::unordered_map<std::string, UserBalance, CustomHash>::iterator it = balances_.find(user);
        if (it == balances_.end() || it->second.totalCollateral < amount)
            throw std::runtime_error("Insufficient collateral");
        it->second.totalCollateral -= amount;
    }

    double getBalance(const std::string &user) const {
        std::unordered_map<std::string, UserBalance, CustomHash>::const_iterator it = balances_.find(user);
        return it == balances_.end() ? 0.0 : it->second.totalCollateral;
    }

    bool hasBalance(const std::string &user, double amount) const {
        std::unordered_map<std::string, UserBalance, CustomHash>::const_iterator it = balances_.find(user);
        return it != balances_.end() && it->second.totalCollateral >= amount;
    }

private:
    std::unordered_map<std::string, UserBalance, CustomHash> balances_;
};

// --- PriceOracle Component ---
class PriceOracle {
public:
    PriceOracle() : latestPrice_(0.0) {}

    void updatePrice(double price) {
        if (price <= 0) throw std::invalid_argument("Price must be positive");
        latestPrice_ = price;
    }

    double getPrice() const { return latestPrice_; }
    bool hasValidPrice() const { return latestPrice_ > 0; }

private:
    double latestPrice_;
};

// --- PositionManager Component ---
struct Position {
    std::string trader;
    double entryPrice;
    double size;
    double leverage;
    bool isLong;
    bool isOpen;

    Position() : entryPrice(0.0), size(0.0), leverage(0.0), isLong(false), isOpen(false) {}

    Position(const std::string &t, double entry, double s, double lev, bool longPos, bool open)
        : trader(t), entryPrice(entry), size(s), leverage(lev), isLong(longPos), isOpen(open) {}

    double calculatePnL(double currentPrice) const {
        double priceDiff = isLong ? (currentPrice - entryPrice) : (entryPrice - currentPrice);
        return priceDiff * (size / entryPrice);
    }

    double getMargin() const {
        return size / leverage;
    }
};

class PositionManager {
public:
    PositionManager(CollateralVault &vault) : vault_(vault) {}

    void openPosition(const std::string &trader, double margin, bool isLong, double leverage, double currentPrice) {
        if (margin <= 0 || leverage <= 0) throw std::invalid_argument("Margin and leverage must be positive");
        if (currentPrice <= 0) throw std::invalid_argument("Current price must be positive");

        std::unordered_map<std::string, Position, CustomHash>::iterator it = positions_.find(trader);
        if (it != positions_.end() && it->second.isOpen)
            throw std::runtime_error("Position already open");

        vault_.withdrawCollateral(trader, margin);

        double positionSize = margin * leverage;
        Position pos(trader, currentPrice, positionSize, leverage, isLong, true);
        positions_[trader] = pos;
    }

    void closePosition(const std::string &trader, double currentPrice) {
        if (currentPrice <= 0) throw std::invalid_argument("Current price must be positive");

        std::unordered_map<std::string, Position, CustomHash>::iterator it = positions_.find(trader);
        if (it == positions_.end() || !it->second.isOpen)
            throw std::runtime_error("No open position");

        Position &pos = it->second;
        double pnl = pos.calculatePnL(currentPrice);
        double returnAmount = pos.getMargin() + pnl;
        if (returnAmount < 0) returnAmount = 0;

        vault_.depositCollateral(trader, returnAmount);
        pos.isOpen = false;
    }

    Position getPosition(const std::string &trader) const {
        std::unordered_map<std::string, Position, CustomHash>::const_iterator it = positions_.find(trader);
        if (it == positions_.end() || !it->second.isOpen)
            throw std::runtime_error("No open position");
        return it->second;
    }

    bool hasOpenPosition(const std::string &trader) const {
        std::unordered_map<std::string, Position, CustomHash>::const_iterator it = positions_.find(trader);
        return it != positions_.end() && it->second.isOpen;
    }

private:
    CollateralVault &vault_;
    std::unordered_map<std::string, Position, CustomHash> positions_;
};

// --- LiquidationEngine Component ---
class LiquidationEngine {
public:
    LiquidationEngine(PositionManager &pm, PriceOracle &oracle) : pm_(pm), oracle_(oracle) {}

    bool checkLiquidation(const std::string &trader, double maintenanceMarginRatio) {
        try {
            if (!pm_.hasOpenPosition(trader)) return false;

            double currentPrice = oracle_.getPrice();
            if (currentPrice <= 0) return false;

            Position pos = pm_.getPosition(trader);
            double pnl = pos.calculatePnL(currentPrice);
            double margin = pos.getMargin();
            double liquidationThreshold = -margin * (1.0 - maintenanceMarginRatio);

            if (pnl < liquidationThreshold) {
                pm_.closePosition(trader, currentPrice);
                return true;
            }
        } catch (...) {
            // Handle exceptions silently
        }
        return false;
    }

    void checkLiquidation(const std::string &trader) {
        checkLiquidation(trader, 0.1);
    }

private:
    PositionManager &pm_;
    PriceOracle &oracle_;
};

// --- Qubic Contract HM25 (merged) ---
struct HM25 : public QPI::ContractBase {  // Use explicit QPI:: namespace
    // Qubic input/output structs
    struct Echo_input{};
    struct Echo_output{};
    struct Burn_input{};
    struct Burn_output{};
    struct GetStats_input {};
    struct GetStats_output {
        QPI::uint64 numberOfEchoCalls;  // Use QPI:: prefix
        QPI::uint64 numberOfBurnCalls;  // Use QPI:: prefix
    };

    // On-chain state
    QPI::uint64 numberOfEchoCalls;  // Use QPI:: prefix
    QPI::uint64 numberOfBurnCalls;  // Use QPI:: prefix

    // Shared DEX modules - declared as pointers to avoid constructor issues
    CollateralVault* vault;
    PriceOracle* oracle;
    PositionManager* pm;
    LiquidationEngine* engine;

    // Echo: return invocation reward to caller
    PUBLIC_PROCEDURE(Echo)
        state.numberOfEchoCalls++;
        if (qpi.invocationReward() > 0) {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
        }
    _

    // Burn: destroy invocation reward
    PUBLIC_PROCEDURE(Burn)
        state.numberOfBurnCalls++;
        if (qpi.invocationReward() > 0) {
            qpi.burn(qpi.invocationReward());
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

        // Initialize DEX components
        state.vault = new CollateralVault();
        state.oracle = new PriceOracle();
        state.pm = new PositionManager(*state.vault);
        state.engine = new LiquidationEngine(*state.pm, *state.oracle);
    _
};