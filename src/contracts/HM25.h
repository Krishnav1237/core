#pragma once
#include <qpi.h>
using namespace QPI;  // Qubic Programming Interface

struct HM25 : public ContractBase {
    // ─── Input/Output Structs ───────────────────────────────────────────────
    struct Echo_input    {};
    struct Echo_output   {};

    struct Burn_input    {};
    struct Burn_output   {};

    struct GetStats_input {};
    struct GetStats_output {
        uint64 numberOfEchoCalls;
        uint64 numberOfBurnCalls;
    };

    struct Deposit_input  { uint64 amount; };
    struct Deposit_output {};

    struct Withdraw_input { uint64 amount; };
    struct Withdraw_output{};

    struct UpdatePrice_input  { uint64 price; };
    struct UpdatePrice_output {};

    struct GetBalance_input  {};
    struct GetBalance_output { uint64 balance; };

    struct OpenPos_input      { bit isLong; uint64 margin; uint64 leverage; };
    struct OpenPos_output     {};
    struct ClosePos_input     {};
    struct ClosePos_output    {};

    // ─── Persistent State (aligned across nodes) ───────────────────────────
    uint64 numberOfEchoCalls;
    uint64 numberOfBurnCalls;

    // DEX State
    uint64 latestPrice;
    collection<id, uint64> balances;                     // user → collateral
    struct PosRec { uint64 entryPrice; uint64 size; uint64 leverage; bit isLong; bit isOpen; };
    collection<id, PosRec> positions;                    // user → open position

    // ─── Procedures (mutate state) ─────────────────────────────────────────
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
        uint64 bal = balances.get(user);
        balances[user] = bal + input.amount;
    } _

    PUBLIC_PROCEDURE(Withdraw) {
        id user = qpi.invocator();
        uint64 bal = balances.get(user);
        if (bal < input.amount) qpi.abort();
        balances[user] = bal - input.amount;
    } _

    PUBLIC_PROCEDURE(UpdatePrice) {
        if (input.price == 0) qpi.abort();
        state.latestPrice = input.price;
    } _

    PUBLIC_PROCEDURE(OpenPos) {
        id user = qpi.invocator();
        PosRec pr = positions.get(user);
        if (pr.isOpen) qpi.abort();
        uint64 bal = balances.get(user);
        if (bal < input.margin) qpi.abort();
        balances[user] = bal - input.margin;
        PosRec np = { state.latestPrice, input.margin * input.leverage, input.leverage, input.isLong, 1 };
        positions[user] = np;
    } _

    PUBLIC_PROCEDURE(ClosePos) {
        id user = qpi.invocator();
        PosRec pr = positions.get(user);
        if (!pr.isOpen) qpi.abort();
        uint64 price = state.latestPrice;
        uint64 diff = pr.isLong ? price - pr.entryPrice : pr.entryPrice - price;
        uint64 notionalDiv = div(pr.size, pr.entryPrice);
        uint64 pnl = diff * notionalDiv;
        uint64 ret = div(pr.size, pr.leverage);
        ret = ret + pnl;
        balances[user] = balances.get(user) + ret;
        pr.isOpen = 0;
        positions[user] = pr;
    } _

    // ─── Functions (read-only) ─────────────────────────────────────────────
    PUBLIC_FUNCTION(GetStats) {
        output.numberOfBurnCalls = state.numberOfBurnCalls;
        output.numberOfEchoCalls = state.numberOfEchoCalls;
    } _

    PUBLIC_FUNCTION(GetBalance) {
        output.balance = balances.get(qpi.invocator());
    } _
// ─── Registration ───────────────────────────────────────────────────────
    REGISTER_USER_FUNCTIONS_AND_PROCEDURES {
        REGISTER_USER_PROCEDURE(Echo, 1);
        REGISTER_USER_PROCEDURE(Burn, 2);
        REGISTER_USER_PROCEDURE(Deposit, 3);
        REGISTER_USER_PROCEDURE(Withdraw,4);
        REGISTER_USER_PROCEDURE(UpdatePrice,5);
        REGISTER_USER_PROCEDURE(OpenPos, 6);
        REGISTER_USER_PROCEDURE(ClosePos,7);
        REGISTER_USER_FUNCTION(GetStats, 1);
        REGISTER_USER_FUNCTION(GetBalance,2);
    } _

    // ─── Initialization ─────────────────────────────────────────────────────
    INITIALIZE {
        state.numberOfEchoCalls  = 0;
        state.numberOfBurnCalls  = 0;
        state.latestPrice        = 0;
        // QPI collections start empty by default
    } _
};