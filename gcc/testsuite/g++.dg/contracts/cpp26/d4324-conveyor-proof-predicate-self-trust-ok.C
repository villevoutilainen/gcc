// D4324/P2680: -fcontract-conveyor-proofs, a function's *own*
// precondition establishing a shared-substrate fact for the rest of
// *its own body* -- g()'s precondition "is_opened (this)" is trusted,
// so the read () call inside g()'s own body can prove read()'s own
// precondition of the same shape, entirely from g's own body walk.
// This is the conveyor-flavored mirror of d4324-symbolic-proof-
// predicate-self-trust-ok.C: predicate self-trust didn't exist for
// *either* control-object flavor before, since it's a shared-substrate
// capability (m_predicate_fact_map), not one of the classic is_object_
// address/nonzero/range facts conveyor's own self-trust already had.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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
  static bool is_opened (io_facility*) conveyor { return true; }
  void open () post<conveyor_ctrl_v>(is_opened (this)) {} // { dg-warning "cannot verify postcondition" }
  void read () pre<conveyor_ctrl_v>(is_opened (this)) {}
  void g () pre<conveyor_ctrl_v>(is_opened (this))
  {
    read ();
  }
};

int main ()
{
  io_facility f;
  f.open ();
  f.g ();
  return 0;
}
