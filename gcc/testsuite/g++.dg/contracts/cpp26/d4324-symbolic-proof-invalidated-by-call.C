// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs
// invalidation rule 2 -- unrelated() has no contract at all, but taking
// f's address means the analysis has no way to know it didn't change f's
// logical state, so the previously-established is_opened(this) fact must
// be invalidated.  By the time write_data() runs, that fact is gone, so
// the sound answer is "cannot verify" again, not silent acceptance.
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

void unrelated (io_facility* p) { (void) p; }

int main ()
{
  io_facility f;
  f.open ();
  unrelated (&f);
  f.write_data (); // { dg-warning "cannot verify" }
  return 0;
}
