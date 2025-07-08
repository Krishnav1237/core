using namespace qpi;

struct HM25 : public ContractBase {
  // ─── I/O structs ───────────────────────────────────────────────────────────
  struct Echo_input    {}; struct Echo_output    {};
  struct Burn_input    {}; struct Burn_output    {};
  struct GetStats_input{}; struct GetStats_output{ uint64 numberOfEchoCalls, numberOfBurnCalls; };

  struct Deposit_input  { uint64 amount; }; struct Deposit_output{};
  struct Withdraw_input { uint64 amount; }; struct Withdraw_output{};

  struct UpdatePrice_input{ uint64 price; }; struct UpdatePrice_output{};

  struct OpenPos_input  { bit isLong; uint64 margin, leverage; };
  struct OpenPos_output {};
  struct ClosePos_input {}; struct ClosePos_output{};

  struct GetBalance_input{}; struct GetBalance_output{ uint64 balance; };

  // ─── Persistent state ───────────────────────────────────────────────────────
  uint64 numberOfEchoCalls;
  uint64 numberOfBurnCalls;
  uint64 latestPrice;

  // user → collateral
  HashMap<id, uint64, 1024> balances;

  // position record
  struct PosRec {
    uint64 entryPrice;
    uint64 size;
    uint64 leverage;
    bit    isLong;
    bit    isOpen;
  };
  HashMap<id, PosRec, 1024> positions;

  // ─── User procedures ────────────────────────────────────────────────────────

  PUBLIC_PROCEDURE(Echo) {
    state.numberOfEchoCalls = state.numberOfEchoCalls + 1;
    uint64 reward = invocationReward();
    if (reward > 0) transfer(invocator(), reward);
  } _

  PUBLIC_PROCEDURE(Burn) {
    state.numberOfBurnCalls = state.numberOfBurnCalls + 1;
    uint64 reward = invocationReward();
    if (reward > 0) burn(reward);
  } _

  PUBLIC_PROCEDURE(Deposit) {
    // zero‐check
    if (input.amount == 0) abort();
    id user = invocator();
    uint64 old = balances.get(user);
    balances.set(user, old + input.amount);
    balances.cleanupIfNeeded();
  } _

  PUBLIC_PROCEDURE(Withdraw) {
    if (input.amount == 0) abort();
    id user = invocator();
    uint64 bal = balances.get(user);
    if (bal < input.amount) abort();
    balances.set(user, bal - input.amount);
    balances.cleanupIfNeeded();
  } _

  PUBLIC_PROCEDURE(UpdatePrice) {
    // price must be > 0
    if (input.price == 0) abort();
    state.latestPrice = input.price;
  } _

  PUBLIC_PROCEDURE(OpenPos) {
    id user = invocator();
    PosRec pr = positions.get(user);
    if (pr.isOpen) abort();

    // lock margin
    uint64 bal = balances.get(user);
    if (bal < input.margin) abort();
    balances.set(user, bal - input.margin);

    // size = margin * leverage
    uint64 posSize = mul(input.margin, input.leverage);
    PosRec np;
    np.entryPrice = state.latestPrice;
    np.size       = posSize;
    np.leverage   = input.leverage;
    np.isLong     = input.isLong;
    np.isOpen     = 1;
    positions.set(user, np);
    balances.cleanupIfNeeded();
  } _

  PUBLIC_PROCEDURE(ClosePos) {
    id user = invocator();
    PosRec pr = positions.get(user);
    if (!pr.isOpen) abort();

    uint64 priceNow = state.latestPrice;
    // diff = abs(priceNow – entryPrice)
    uint64 diff = pr.isLong
                 ? (priceNow - pr.entryPrice)
                 : (pr.entryPrice - priceNow);

    // notionalDiv = size / entryPrice
    uint64 notionalDiv = div(pr.size, pr.entryPrice);
    uint64 pnl = mul(diff, notionalDiv);

    // return = margin + pnl (margin = size / leverage)
    uint64 margin = div(pr.size, pr.leverage);
    uint64 payout = margin + pnl;

    // credit back
    uint64 bal = balances.get(user);
    balances.set(user, bal + payout);

    // close position
    pr.isOpen = 0;
    positions.set(user, pr);
    balances.cleanupIfNeeded();
  } _

  // ─── Read‐only functions ────────────────────────────────────────────────────

  PUBLIC_FUNCTION(GetStats) {
    output.numberOfEchoCalls = state.numberOfEchoCalls;
    output.numberOfBurnCalls = state.numberOfBurnCalls;
  } _

  PUBLIC_FUNCTION(GetBalance) {
    output.balance = balances.get(invocator());
  } _
// ─── Registration & Initialization ─────────────────────────────────────────

  REGISTER_USER_FUNCTIONS_AND_PROCEDURES {
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

  INITIALIZE {
    state.numberOfEchoCalls = 0;
    state.numberOfBurnCalls = 0;
    state.latestPrice       = 0;
    // HashMaps begin empty
  } _
};