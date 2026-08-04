// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs
// invalidation rule 1 -- reassigning f wholesale invalidates any
// previously-established fact keyed on its identity, whatever f's type
// (a symbolic fact is not limited to pointer/integral objects the way
// the is_object_address/range tracking is).  By the time write_data()
// runs, the earlier is_opened(this) fact from open() is gone, so the
// sound answer is "cannot verify," not silent acceptance.
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
  int state = 0;
  static bool is_opened (io_facility*) symbolic;
  void open () post<symbolic_ctrl_v>(is_opened (this)) { state = 1; }
  void write_data () pre<symbolic_ctrl_v>(is_opened (this)) { (void) state; }
};

int main ()
{
  io_facility f;
  f.open ();
  f = io_facility ();
  f.write_data (); // { dg-warning "cannot verify" }
  return 0;
}
