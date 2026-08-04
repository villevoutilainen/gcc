// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// OA_UNKNOWN case -- write_data()'s precondition requires is_opened(this),
// but nothing on this path ever called open() (or anything else whose
// postcondition establishes that fact), so there is no established fact
// to check against either way.  The sound answer is "cannot verify," not
// silent acceptance, and not a false claim of a proven violation.
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
  f.write_data (); // { dg-warning "cannot verify" }
  return 0;
}
