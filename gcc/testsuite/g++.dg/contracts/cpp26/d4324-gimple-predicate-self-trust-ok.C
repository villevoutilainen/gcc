// The built-in GIMPLE-pass engine's own self-trust case for named
// predicates (-fcontract-conveyor-proofs-gimple): g's own declared
// precondition "is_opened(p)" is trusted for the rest of g's own body
// (cg_seed_predicate_self_trust seeds ssa_default_def(g, p) into the
// dominator walk's own root/seed state), so the read() call inside
// g's own body is discharged purely from that seeded fact. See
// gcc/cp/contracts-gimple.cc and ~/gimple-contract-analysis.md.
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
  static bool is_opened (io_facility*) conveyor { return true; }
  void open () post<conveyor_ctrl_v>(is_opened (this)) {}
  void read () pre<conveyor_ctrl_v>(is_opened (this)) {}
};

void g (io_facility *p) pre<conveyor_ctrl_v>(io_facility::is_opened (p))
{
  p->read ();
}

int main ()
{
  io_facility f;
  f.open ();
  g (&f);
  return 0;
}
