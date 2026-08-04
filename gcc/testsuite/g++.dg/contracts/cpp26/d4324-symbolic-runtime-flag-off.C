// Axiom contracts ("the gem"): with -fcontract-symbolic-runtime-checks
// absent, a symbolic contract must still generate zero runtime code at
// all -- identical to today's default -- even though the source is
// otherwise identical to d4324-symbolic-runtime-ok.C. No call to any of
// the four runtime record-store functions may appear anywhere.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fdump-tree-gimple" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct io_facility {
  static bool is_opened (io_facility*) symbolic;
  void open () post<symbolic_ctrl_v>(is_opened (this)) {}
  void write_data () pre<symbolic_ctrl_v>(is_opened (this)) {}
};

int main ()
{
  io_facility f;
  f.open ();
  f.write_data ();
  return 0;
}

// { dg-final { scan-tree-dump-not "__contracts_symbolic" "gimple" } }
