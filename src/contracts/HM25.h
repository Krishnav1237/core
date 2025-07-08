// HM25.h
// A combined DEX + Echo/Burn contract for Qubic.
// - No #includes or other preprocessor directives
// - Uses only Qubic‐approved macros, container types, and integer math

using namespace qpi;

struct HM25 : public ContractBase
{
    // ─── Input & Output structs ─────────────────────────────────────────────────

    struct Echo_input    {};    struct Echo_output    {};
    struct Burn_input    {};    struct Burn_output    {};
    struct GetStats_input{};    struct GetStats_output{ uint64 numberOfEchoCalls; uint64 numberOfBurnCalls; };

    struct Deposit_input  { uint64 amount; };  struct Deposit_output  {};
    struct Withdraw_input { uint64 amount; };  struct Withdraw_output {};

    struct UpdatePrice_input { uint64 price; }; struct UpdatePrice_output {};

    struct OpenPos_input   { bit isLong; uint64 margin; uint64 leverage; };
    struct OpenPos_output  {};
    struct ClosePos_input  {};
    struct ClosePos_output {};

    struct GetBalance_input{}; struct GetBalance_output{ uint64 balance; };

    // ─── Persistent state ────────────────────────────────────────────────────────

    uint64 numberOfEchoCalls;
    uint64 numberOfBurnCalls;

    uint64 latestPrice;
    // user → collateral
    HashMap<id, uint64, 1024> balances;
    // position record
    struct PosRec { uint64 entryPrice, size, leverage; bit isLong, isOpen; };
    HashMap<id, PosRec, 1024> positions;

    // ─── Procedures ──────────────────────────────────────────────────────────────

    PUBLIC_PROCEDURE(Echo)
    {
        state.numberOfEchoCalls = add(state.numberOfEchoCalls, 1);
        uint64 reward = qpi.invocationReward();
        if (reward > 0) qpi.transfer(qpi.invocator(), reward);
    } _

    PUBLIC_PROCEDURE(Burn)
    {
        state.numberOfBurnCalls = add(state.numberOfBurnCalls, 1);
        uint64 reward = qpi.invocationReward();
        if (reward > 0) qpi.burn(reward);
    } _

    PUBLIC_PROCEDURE(Deposit)
    {
        // amount must be > 0
        if (input.amount == 0) qpi.abort();
        uint64 old = balances.get(qpi.invocator());
        balances[qpi.invocator()] = add(old, input.amount);
        balances.cleanupIfNeeded();
    } _

    PUBLIC_PROCEDURE(Withdraw)
    {
        // amount must be > 0
        if (input.amount == 0) qpi.abort();
        uint64 bal = balances.get(qpi.invocator());
        if (bal < input.amount) qpi.abort();
        balances[qpi.invocator()] = sub(bal, input.amount);
        balances.cleanupIfNeeded();
    } _

    PUBLIC_PROCEDURE(UpdatePrice)
    {
        // price must be nonzero
        if (input.price == 0) qpi.abort();
        state.latestPrice = input.price;
    } _

    PUBLIC_PROCEDURE(OpenPos)
    {
        id user = qpi.invocator();
        PosRec pr = positions.get(user);
        // no double‐open
        if (pr.isOpen) qpi.abort();
        // lock collateral
        uint64 bal = balances.get(user);
        if (bal < input.margin) qpi.abort();
        balances[user] = sub(bal, input.margin);

        // record position: size = margin * leverage
        uint64 posSize = mul(input.margin, input.leverage);
        PosRec np = { state.latestPrice, posSize, input.leverage, input.isLong, 1 };
        positions[user] = np;
        balances.cleanupIfNeeded();
    } _

    PUBLIC_PROCEDURE(ClosePos)
    {
        id user = qpi.invocator();
        PosRec pr = positions.get(user);
        if (!pr.isOpen) qpi.abort();

        // price difference
        uint64 priceNow = state.latestPrice;
        uint64 diff = pr.isLong
                      ? sub(priceNow, pr.entryPrice)
                      : sub(pr.entryPrice, priceNow);

        // notionalDiv = size / entryPrice (use safe div)
        uint64 notionalDiv = div(pr.size, pr.entryPrice);
        uint64 pnl = mul(diff, notionalDiv);

        // return = margin + pnl  (margin = size / leverage)
        uint64 returnMargin = div(pr.size, pr.leverage);
        uint64 payout = add(returnMargin, pnl);
// credit back
        uint64 bal = balances.get(user);
        balances[user] = add(bal, payout);

        // mark closed
        pr.isOpen = 0;
        positions[user] = pr;
        balances.cleanupIfNeeded();
    } _

    // ─── Read‐only functions ─────────────────────────────────────────────────────

    PUBLIC_FUNCTION(GetStats)
    {
        output.numberOfBurnCalls = state.numberOfBurnCalls;
        output.numberOfEchoCalls = state.numberOfEchoCalls;
    } _

    PUBLIC_FUNCTION(GetBalance)
    {
        output.balance = balances.get(qpi.invocator());
    } _

    // ─── Registration ───────────────────────────────────────────────────────────

    REGISTER_USER_FUNCTIONS_AND_PROCEDURES
    {
        REGISTER_USER_PROCEDURE(Echo,         1);
        REGISTER_USER_PROCEDURE(Burn,         2);
        REGISTER_USER_PROCEDURE(Deposit,      3);
        REGISTER_USER_PROCEDURE(Withdraw,     4);
        REGISTER_USER_PROCEDURE(UpdatePrice,  5);
        REGISTER_USER_PROCEDURE(OpenPos,      6);
        REGISTER_USER_PROCEDURE(ClosePos,     7);

        REGISTER_USER_FUNCTION(GetStats,     1);
        REGISTER_USER_FUNCTION(GetBalance,   2);
    } _

    // ─── Initialization ─────────────────────────────────────────────────────────

    INITIALIZE
    {
        state.numberOfEchoCalls  = 0;
        state.numberOfBurnCalls  = 0;
        state.latestPrice        = 0;
        // HashMaps start empty
    } _
};