// D4324/P2680: -fcontract-conveyor-proofs, predicate-chaining proof,
// OA_PROVEN_TRUE case -- connects produce's postcondition
// ("check_it (r)") to consume's precondition ("check_it (x)") purely by
// name + argument identity.  check_it is a conveyor function; its
// definition is trusted by construction, so the connection holds
// without ever evaluating it.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

// conveyor must be repeated identically on every redeclaration.
bool check_it (int v) conveyor { return v > 0; }

int produce () post<conveyor_ctrl_v>(r: check_it (r))
{
  return 1;
}

void consume (int x) pre<conveyor_ctrl_v>(check_it (x))
{
  (void) x;
}

void caller ()
{
  consume (produce ());
}

int main () { caller (); return 0; }
