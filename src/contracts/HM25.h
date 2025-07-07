using namespace qpi;  // Qubic Programming Interface :contentReference[oaicite:4]{index=4}

struct HM25 : public ContractBase {
    // ─── Input/Output Structs ───────────────────────────────────────────────
    struct Echo_input    {};  struct Echo_output   {};
    struct Burn_input    {};  struct Burn_output   {};
    struct GetStats_input{};  struct GetStats_output { uint64 numberOfEchoCalls; uint64 numberOfBurnCalls; };

    struct Deposit_input  { uint64 amount; };
    struct Deposit_output {};
    struct Withdraw_input { uint64 amount; };
    struct Withdraw_output{};

    struct UpdatePrice_input  { uint64 price;  };
    struct UpdatePrice_output {};

    struct GetBalance_input   {};
    struct GetBalance_output  { uint64 balance; };

    struct OpenPos_input      { bit isLong; uint64 margin; uint64 leverage; };
    struct OpenPos_output     {};
    struct ClosePos_input     {};
    struct ClosePos_output    {};

    // ─── Persistent State (aligned across nodes) ───────────────────────────
    uint64 numberOfEchoCalls;
    uint64 numberOfBurnCalls;

    // DEX State
    uint64 latestPrice;
    collection<id, uint64> balances;                      // user → collateral :contentReference[oaicite:5]{index=5}
    struct PosRec { uint64 entryPrice; uint64 size; uint64 leverage; bit isLong; bit isOpen; };
    collection<id, PosRec> positions;                     // user → open position

    // ─── Procedures (mutate state) ─────────────────────────────────────────
    PUBLIC_PROCEDURE(Echo) {
        state.numberOfEchoCalls++;
        uint64 reward = qpi.invocationReward();
        if (reward > 0) qpi.transfer(qpi.invocator(), reward);
    } _  // qpi.transfer/burn mechanics :contentReference[oaicite:6]{index=6}

    PUBLIC_PROCEDURE(Burn) {
        state.numberOfBurnCalls++;
        uint64 reward = qpi.invocationReward();
        if (reward > 0) qpi.burn(reward);
    } _

    PUBLIC_PROCEDURE(Deposit) {
        // Collateral deposit
        balances[invocator()] = balances.get(invocator()) + input.amount;
    } _

    PUBLIC_PROCEDURE(Withdraw) {
        uint64 bal = balances.get(invocator());
        if (bal < input.amount) qpi.abort();   // insufficient funds
        balances[invocator()] = bal - input.amount;
    } _

    PUBLIC_PROCEDURE(UpdatePrice) {
        // Oracle update (off-chain feeds call this)
        if (input.price == 0) qpi.abort();
        state.latestPrice = input.price;
    } _

    PUBLIC_PROCEDURE(OpenPos) {
        // Open leveraged long/short
        id user = invocator();
        PosRec pr = positions.get(user);
        if (pr.isOpen) qpi.abort();            // already open
        // lock margin
        uint64 bal = balances.get(user);
        if (bal < input.margin) qpi.abort();
        balances[user] = bal - input.margin;
        // record position
        PosRec np{ state.latestPrice, input.margin * input.leverage, input.leverage, input.isLong, 1 };
        positions[user] = np;
    } _

    PUBLIC_PROCEDURE(ClosePos) {
        id user = invocator();
        PosRec pr = positions.get(user);
        if (!pr.isOpen) qpi.abort();
        // PnL = diff * (size / entryPrice)
        uint64 price = state.latestPrice;
        uint64 diff = pr.isLong ? price - pr.entryPrice : pr.entryPrice - price;
        // size / entryPrice → use div() per QPI rules :contentReference[oaicite:7]{index=7}
        uint64 notionalDiv = div(pr.size, pr.entryPrice);
        uint64 pnl = mul(diff, notionalDiv);
        // return = margin + pnl
        uint64 ret = pr.size / pr.leverage;
        ret = add(ret, pnl);
        balances[user] = add(balances.get(user), ret);
        // close
        pr.isOpen = 0;
        positions[user] = pr;
    } _

    // ─── Functions (read-only) ─────────────────────────────────────────────
    PUBLIC_FUNCTION(GetStats) {
        output.numberOfBurnCalls = state.numberOfBurnCalls;
        output.numberOfEchoCalls = state.numberOfEchoCalls;
    } _
PUBLIC_FUNCTION(GetBalance) {
        output.balance = balances.get(invocator());
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
        // QPI collections start empty by default :contentReference[oaicite:8]{index=8}
    } _
};