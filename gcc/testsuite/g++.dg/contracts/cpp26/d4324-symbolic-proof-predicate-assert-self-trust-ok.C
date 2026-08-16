// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// a mid-body contract_assert establishing a shared-substrate fact for
// the rest of the *same function's* own body -- the contract_assert-
// as-fact-source escape hatch the classic is_object_address/nonzero/
// range facts already have, extended to the named-predicate map.  The
// contract_assert's own "is_opened (this)" is nothing but the very
// fact being established here (nothing establishes it beforehand), so
// it is itself only ever "cannot verify" -- warns, per the never_
// proven/analyzed_conveyor discrepancy fix (same session), but still
// establishes itself as a trusted fact regardless, so the read () call
// right after it can prove read()'s own precondition of the same
// shape with no diagnostic of its own.  See .claude/plans/well-we-
// last-discussed-ethereal-duckling.md.
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
  void read () pre<symbolic_ctrl_v>(is_opened (this)) {}
  void g ()
  {
    contract_assert<symbolic_ctrl_v>(is_opened (this)); // { dg-warning "cannot verify" }
    read ();
  }
};

int main ()
{
  io_facility f;
  f.g ();
  return 0;
}
