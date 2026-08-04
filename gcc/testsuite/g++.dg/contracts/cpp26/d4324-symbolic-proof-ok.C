// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// OA_PROVEN_TRUE case -- open()'s postcondition establishes is_opened(this)
// for f, and write_data()'s precondition requires exactly that same fact
// on the same object, with nothing invalidating it in between.  Proven,
// so it's discharged silently, without ever evaluating is_opened itself
// (it has no body -- it is purely symbolic).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

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
