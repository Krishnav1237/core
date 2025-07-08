using namespace QPI;  // Qubic Programming Interface

struct HM25 : public ContractBase {
    // ─── I/O Structs ────────────────────────────────────────────────────────
    struct Echo_input    {};
    struct Echo_output   {};

    struct Burn_input    {};
    struct Burn_output   {};

    struct GetStats_input{};
    struct GetStats_output {
        uint64 numberOfEchoCalls;
        uint64 numberOfBurnCalls;
    };

    struct Deposit_input  { uint64 amount; };
    struct Deposit_output {};

    struct Withdraw_input { uint64 amount; };
    struct Withdraw_output{};

    struct UpdatePrice_input { uint64 price; };
    struct UpdatePrice_output{};

    struct GetBalance_input  {};
    struct GetBalance_output { uint64 balance; };

    struct OpenPos_input  { bit isLong; uint64 margin; uint64 leverage; };
    struct OpenPos_output {};

    struct ClosePos_input {};
    struct ClosePos_output{ uint64 pnl; uint64 returnAmount; };

    struct GetPosition_input {};
    struct GetPosition_output {
        uint64 entryPrice;
        uint64 size;
        uint64 leverage;
        bit isLong;
        bit isOpen;
    };

    // ─── STATE ──────────────────────────────────────────────────────────────
    uint64 numberOfEchoCalls;
    uint64 numberOfBurnCalls;
    uint64 latestPrice;
    HashMap<id, uint64, 10000> balances;

    struct PosRec {
        uint64 entryPrice;
        uint64 size;
        uint64 leverage;
        bit isLong;
        bit isOpen;
    };
    HashMap<id, PosRec, 10000> positions;

    // ─── PROCEDURES ─────────────────────────────────────────────────────────
    PUBLIC_PROCEDURE(Echo) {
        state.numberOfEchoCalls++;
        uint64 reward = qpi.invocationReward();
        if (reward > 0) {
            qpi.transfer(qpi.invocator(), reward);
        }
    } _

    PUBLIC_PROCEDURE(Burn) {
        state.numberOfBurnCalls++;
        uint64 reward = qpi.invocationReward();
        if (reward > 0) {
            qpi.burn(reward);
        }
    } _

    PUBLIC_PROCEDURE(Deposit) {
        id user = qpi.invocator();
        uint64 reward = qpi.invocationReward();
        if (reward == 0) {
            // No deposit amount provided
            return;
        }

        uint64 currentBalance = 0;
        if (state.balances.contains(user)) {
            currentBalance = state.balances.get(user);
        }

        uint64 newBalance = currentBalance + reward;
        state.balances.set(user, newBalance);
    } _

    PUBLIC_PROCEDURE(Withdraw) {
        id user = qpi.invocator();

        if (!state.balances.contains(user)) {
            // No balance for user
            return;
        }

        uint64 currentBalance = state.balances.get(user);
        if (currentBalance < input.amount) {
            // Insufficient balance
            return;
        }

        uint64 newBalance = currentBalance - input.amount;
        state.balances.set(user, newBalance);
        qpi.transfer(user, input.amount);
    } _

    PUBLIC_PROCEDURE(UpdatePrice) {
        if (input.price == 0) {
            // Invalid price
            return;
        }
        state.latestPrice = input.price;
    } _

    PUBLIC_PROCEDURE(OpenPos) {
        id user = qpi.invocator();

        // Check if user already has an open position
        if (state.positions.contains(user)) {
            PosRec existingPos = state.positions.get(user);
            if (existingPos.isOpen) {
                // Position already open
                return;
            }
        }

        // Check if user has sufficient balance
        uint64 userBalance = 0;
        if (state.balances.contains(user)) {
            userBalance = state.balances.get(user);
        }

        if (userBalance < input.margin) {
            // Insufficient margin
            return;
        }

        // Check for valid leverage (should be > 0)
        if (input.leverage == 0) {
            // Invalid leverage
            return;
        }

        // Check if price is available
        if (state.latestPrice == 0) {
            // No price available
            return;
        }

        // Deduct margin from balance
        uint64 newBalance = userBalance - input.margin;
        state.balances.set(user, newBalance);

        // Create new position
        PosRec newPos;
        newPos.entryPrice = state.latestPrice;
        newPos.size = input.margin * input.leverage;
        newPos.leverage = input.leverage;
        newPos.isLong = input.isLong;
        newPos.isOpen = 1;

        state.positions.set(user, newPos);
    } _

    PUBLIC_PROCEDURE(ClosePos) {
        id user = qpi.invocator();

        if (!state.positions.contains(user)) {
            // No position found
            return;
        }

        PosRec pos = state.positions.get(user);
        if (!pos.isOpen) {
            // Position not open
            return;
        }

        if (state.latestPrice == 0) {
            // No current price available
            return;
        }

        // Calculate P&L
        uint64 priceDiff = 0;
        sint64 pnl = 0;

        if (pos.isLong) {
            if (state.latestPrice > pos.entryPrice) {
                priceDiff = state.latestPrice - pos.entryPrice;
                pnl = (sint64)((priceDiff * pos.size) / pos.entryPrice);
            } else {
                priceDiff = pos.entryPrice - state.latestPrice;
                pnl = -(sint64)((priceDiff * pos.size) / pos.entryPrice);
            }
        } else {
            if (pos.entryPrice > state.latestPrice) {
                priceDiff = pos.entryPrice - state.latestPrice;
                pnl = (sint64)((priceDiff * pos.size) / pos.entryPrice);
            } else {
                priceDiff = state.latestPrice - pos.entryPrice;
                pnl = -(sint64)((priceDiff * pos.size) / pos.entryPrice);
            }
        }

        // Calculate return amount (margin + P&L)
        uint64 margin = pos.size / pos.leverage;
        uint64 returnAmount = margin;

        if (pnl > 0) {
            returnAmount += (uint64)pnl;
        } else if (pnl < 0 && (uint64)(-pnl) < margin) {
            returnAmount -= (uint64)(-pnl);
        } else if (pnl < 0) {
            returnAmount = 0; // Total loss
        }

        // Update user balance
        uint64 currentBalance = 0;
        if (state.balances.contains(user)) {
            currentBalance = state.balances.get(user);
        }
        state.balances.set(user, currentBalance + returnAmount);

        // Close position
        pos.isOpen = 0;
        state.positions.set(user, pos);

        // Set output values
        output.pnl = (pnl >= 0) ? (uint64)pnl : 0;
        output.returnAmount = returnAmount;
    } _

    // ─── READ-ONLY FUNCTIONS ─────────────────────────────────────────────────
    PUBLIC_FUNCTION(GetStats) {
        output.numberOfBurnCalls = state.numberOfBurnCalls;
        output.numberOfEchoCalls = state.numberOfEchoCalls;
    } _

    PUBLIC_FUNCTION(GetBalance) {
        id user = qpi.invocator();
        if (state.balances.contains(user)) {
            output.balance = state.balances.get(user);
        } else {
            output.balance = 0;
        }
    } _

    PUBLIC_FUNCTION(GetPosition) {
        id user = qpi.invocator();
        if (state.positions.contains(user)) {
            PosRec pos = state.positions.get(user);
            output.entryPrice = pos.entryPrice;
            output.size = pos.size;
            output.leverage = pos.leverage;
            output.isLong = pos.isLong;
            output.isOpen = pos.isOpen;
        } else {
            output.entryPrice = 0;
            output.size = 0;
            output.leverage = 0;
            output.isLong = 0;
            output.isOpen = 0;
        }
    } _

    // ─── REGISTRATION & INITIALIZATION ─────────────────────────────────────
    REGISTER_USER_FUNCTIONS_AND_PROCEDURES {
        REGISTER_USER_PROCEDURE(Echo,        1);
        REGISTER_USER_PROCEDURE(Burn,        2);
        REGISTER_USER_PROCEDURE(Deposit,     3);
        REGISTER_USER_PROCEDURE(Withdraw,    4);
        REGISTER_USER_PROCEDURE(UpdatePrice, 5);
        REGISTER_USER_PROCEDURE(OpenPos,     6);
        REGISTER_USER_PROCEDURE(ClosePos,    7);

        REGISTER_USER_FUNCTION(GetStats,     1);
        REGISTER_USER_FUNCTION(GetBalance,   2);
        REGISTER_USER_FUNCTION(GetPosition,  3);
    } _

    INITIALIZE {
        state.numberOfEchoCalls = 0;
        state.numberOfBurnCalls = 0;
        state.latestPrice = 0;
    } _
};