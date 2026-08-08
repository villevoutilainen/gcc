// The built-in GIMPLE-pass engine's own named-predicate check for a
// *persistent object* (see gcc/cp/contracts-gimple.cc and
// ~/gimple-contract-analysis.md), gated by
// -fcontract-conveyor-proofs-gimple: open()'s own postcondition
// establishes is_opened(this) via a dominator-tree-walk-based forward
// dataflow (cg_predicate_dom_walker), consulted by read()'s own
// precondition on a later, separate call. Checked through both a
// plain-object receiver ('f.open()') and a pointer receiver
// ('fp->open()'), confirming both resolve to "the same object"
// correctly.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

void dot_receiver ()
{
  io_facility f;
  f.open ();
  f.read ();
}

void pointer_receiver ()
{
  io_facility f;
  io_facility *fp = &f;
  fp->open ();
  fp->read ();
}

int main ()
{
  dot_receiver ();
  pointer_receiver ();
  return 0;
}
