// D4324/P2680: -fcontract-conveyor-proofs, predicate-chaining proof --
// an intervening call taking f's address (touch (&f)) invalidates the
// is_opened (f) fact open()'s own postcondition established, since the
// analysis has no way to know an arbitrary, uncontracted function
// didn't change f's logical state (shared-substrate invalidation rule
// 2).  read()'s own precondition can therefore no longer be proven, so
// the best available answer is "cannot verify," not silent acceptance,
// and not a false claim of a proven violation either.  See
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
  static bool is_opened (io_facility*) conveyor { return true; }
  void open () post<conveyor_ctrl_v>(is_opened (this)) {} // { dg-warning "cannot verify postcondition" }
  void read () pre<conveyor_ctrl_v>(is_opened (this)) {}
};

void touch (io_facility *) {}

void caller ()
{
  io_facility f;
  f.open ();
  touch (&f);
  f.read (); // { dg-warning "cannot verify" }
}

int main () { caller (); return 0; }
