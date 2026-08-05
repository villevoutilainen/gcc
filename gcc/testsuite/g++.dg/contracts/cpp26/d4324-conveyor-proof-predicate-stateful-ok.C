// D4324/P2680: -fcontract-conveyor-proofs, predicate-chaining proof,
// object-identity persisting across statements -- closes the gap where
// conveyor's own predicate checking used to be purely syntactic and
// single-hop (only ever recognizing "consume (produce ())"-style direct
// nesting): is_opened (this) established by f.open()'s postcondition
// now survives to f.read()'s precondition through the same shared,
// real fact-tracking engine -fcontract-symbolic-proofs uses for its own
// obligations (m_predicate_fact_map is a shared substrate, not
// symbolic-exclusive -- see oa_contract_fact_tracking_active_p).  See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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

struct io_facility {
  static bool is_opened (io_facility*) { return true; }
  void open () post<conveyor_ctrl_v>(is_opened (this)) {}
  void read () pre<conveyor_ctrl_v>(is_opened (this)) {}
};

int main ()
{
  io_facility f;
  f.open ();
  f.read ();
  return 0;
}
