#include "qpi.h"

using namespace QPI;

struct HM25 : ContractBase {
    struct State {
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
    };

    struct Echo_input {};
    struct Echo_output {};

    struct Burn_input {};
    struct Burn_output {};

    struct Deposit_input { uint64 amount; };
    struct Deposit_output {};

    struct Withdraw_input { uint64 amount; };
    struct Withdraw_output {};

    struct UpdatePrice_input { uint64 price; };
    struct UpdatePrice_output {};

    struct OpenPos_input { bit isLong; uint64 margin; uint64 leverage; };
    struct OpenPos_output {};

    struct ClosePos_input {};
    struct ClosePos_output { uint64 pnl; uint64 returnAmount; };

    struct GetStats_input {};
    struct GetStats_output {
        uint64 numberOfEchoCalls;
        uint64 numberOfBurnCalls;
    };

    struct GetBalance_input {};
    struct GetBalance_output { uint64 balance; };

    struct GetPosition_input {};
    struct GetPosition_output {
        uint64 entryPrice;
        uint64 size;
        uint64 leverage;
        bit isLong;
        bit isOpen;
    };

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
        if (reward == 0) return;

        uint64 current = state.balances.contains(user) ? state.balances.get(user) : 0;
        state.balances.set(user, current + reward);
    } _

    PUBLIC_PROCEDURE(Withdraw) {
        id user = qpi.invocator();
        if (!state.balances.contains(user)) return;

        uint64 current = state.balances.get(user);
        if (current < input.amount) return;

        state.balances.set(user, current - input.amount);
        qpi.transfer(user, input.amount);
    } _

    PUBLIC_PROCEDURE(UpdatePrice) {
        if (input.price == 0) return;
        state.latestPrice = input.price;
    } _

    PUBLIC_PROCEDURE(OpenPos) {
        id user = qpi.invocator();
        if (state.positions.contains(user) && state.positions.get(user).isOpen) return;

        uint64 balance = state.balances.contains(user) ? state.balances.get(user) : 0;
        if (balance < input.margin || input.leverage == 0 || state.latestPrice == 0) return;

        if (input.margin > UINT64_MAX / input.leverage) return;

        state.balances.set(user, balance - input.margin);
        State::PosRec pos{state.latestPrice, input.margin * input.leverage, input.leverage, input.isLong, 1};
        state.positions.set(user, pos);
    } _

    PUBLIC_PROCEDURE(ClosePos) {
        id user = qpi.invocator();
        if (!state.positions.contains(user)) return;

        State::PosRec pos = state.positions.get(user);
        if (!pos.isOpen || state.latestPrice == 0) return;

        sint64 pnl = 0;
        uint64 priceDiff = (pos.isLong ? (state.latestPrice >= pos.entryPrice ? state.latestPrice - pos.entryPrice : pos.entryPrice - state.latestPrice)
                                       : (pos.entryPrice >= state.latestPrice ? pos.entryPrice - state.latestPrice : state.latestPrice - pos.entryPrice));

        sint64 signedCalc = (sint64)(priceDiff * pos.size / pos.entryPrice);
        pnl = pos.isLong ? (state.latestPrice >= pos.entryPrice ? signedCalc : -signedCalc)
                         : (pos.entryPrice >= state.latestPrice ? signedCalc : -signedCalc);

        uint64 margin = pos.size / pos.leverage;
        uint64 retAmt = margin;
        if (pnl > 0) retAmt += (uint64)pnl;
        else if ((uint64)(-pnl) < margin) retAmt -= (uint64)(-pnl);
        else retAmt = 0;

        uint64 current = state.balances.contains(user) ? state.balances.get(user) : 0;
        state.balances.set(user, current + retAmt);

        pos.isOpen = 0;
        state.positions.set(user, pos);

        output.pnl = (pnl > 0 ? (uint64)pnl : 0);
        output.returnAmount = retAmt;
    } _

    PUBLIC_FUNCTION(GetStats) {
        output.numberOfEchoCalls = state.numberOfEchoCalls;
        output.numberOfBurnCalls = state.numberOfBurnCalls;
    } _

    PUBLIC_FUNCTION(GetBalance) {
        id user = qpi.invocator();
        output.balance = (state.balances.contains(user) ? state.balances.get(user) : 0);
    } _

    PUBLIC_FUNCTION(GetPosition) {
        id user = qpi.invocator();
        if (state.positions.contains(user)) {
            State::PosRec p = state.positions.get(user);
            output.entryPrice = p.entryPrice;
            output.size = p.size;
            output.leverage = p.leverage;
            output.isLong = p.isLong;
            output.isOpen = p.isOpen;
        } else {
            output = {0,0,0,0,0};
        }
    } _

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
