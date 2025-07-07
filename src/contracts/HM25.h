using namespace QPI;

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
    collection<id, uint64> balances;
    struct PosRec { uint64 entryPrice; uint64 size; uint64 leverage; bit isLong; bit isOpen; };
    collection<id, PosRec> positions;

    // ─── Procedures (mutate state) ─────────────────────────────────────────
    PUBLIC_PROCEDURE(Echo) {
        state.numberOfEchoCalls++;
        uint64 reward = invocationReward();
        if (reward > 0) transfer(invocator(), reward);
    } _

    PUBLIC_PROCEDURE(Burn) {
        state.numberOfBurnCalls++;
        uint64 reward = invocationReward();
        if (reward > 0) burn(reward);
    } _

    PUBLIC_PROCEDURE(Deposit) {
        id user = invocator();
        uint64 bal = balances.get(user);
        balances.set(user, add(bal, input.amount));
    } _

    PUBLIC_PROCEDURE(Withdraw) {
        id user = invocator();
        uint64 bal = balances.get(user);
        if (bal < input.amount) abort();
        balances.set(user, bal - input.amount);
    } _

    PUBLIC_PROCEDURE(UpdatePrice) {
        if (input.price == 0) abort();
        state.latestPrice = input.price;
    } _

    PUBLIC_PROCEDURE(OpenPos) {
        id user = invocator();
        PosRec pr = positions.get(user);
        if (pr.isOpen) abort();
        uint64 bal = balances.get(user);
        if (bal < input.margin) abort();
        balances.set(user, bal - input.margin);
        PosRec np = { state.latestPrice, mul(input.margin, input.leverage), input.leverage, input.isLong, 1 };
        positions.set(user, np);
    } _

    PUBLIC_PROCEDURE(ClosePos) {
        id user = invocator();
        PosRec pr = positions.get(user);
        if (!pr.isOpen) abort();
        uint64 price = state.latestPrice;
        uint64 diff = pr.isLong ? price - pr.entryPrice : pr.entryPrice - price;
        uint64 notionalDiv = div(pr.size, pr.entryPrice);
        uint64 pnl = mul(diff, notionalDiv);
        uint64 ret = div(pr.size, pr.leverage);
        ret = add(ret, pnl);
        uint64 bal = balances.get(user);
        balances.set(user, add(bal, ret));
        pr.isOpen = 0;
        positions.set(user, pr);
    } _

    // ─── Functions (read-only) ─────────────────────────────────────────────
    PUBLIC_FUNCTION(GetStats) {
        output.numberOfBurnCalls = state.numberOfBurnCalls;
        output.numberOfEchoCalls = state.numberOfEchoCalls;
    } _

    PUBLIC_FUNCTION(GetBalance) {
        output.balance = balances.get(invocator());
    } _

    // ─── Registration & Initialization ─────────────────────────────────────
    REGISTER_USER_FUNCTIONS_AND_PROCEDURES {
        REGISTER_USER_PROCEDURE(Echo,        1);
        REGISTER_USER_PROCEDURE(Burn,        2);
        REGISTER_USER_PROCEDURE(Deposit,     3);
        REGISTER_USER_PROCEDURE(Withdraw,    4);
        REGISTER_USER_PROCEDURE(UpdatePrice, 5);
        REGISTER_USER_PROCEDURE(OpenPos,     6);
        REGISTER_USER_PROCEDURE(ClosePos,    7);

        REGISTER_USER_FUNCTION (GetStats,    1);
        REGISTER_USER_FUNCTION (GetBalance,  2);
    } _
INITIALIZE {
        state.numberOfEchoCalls = 0;
        state.numberOfBurnCalls = 0;
        state.latestPrice       = 0;
    } _
};