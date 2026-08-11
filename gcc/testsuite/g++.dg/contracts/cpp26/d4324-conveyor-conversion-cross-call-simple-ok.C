// D4324: the cross-call follow-up to d4324-conveyor-conversion-simple-
// ok.C -- forwarding a class-typed decl BY VALUE to a *different*
// function needing the same range fact, for a non-trivially-copyable
// type (a user-provided copy ctor and destructor). At this pre-
// genericize stage the call argument is '&TARGET_EXPR<D.NNNN,
// AGGR_INIT_EXPR(wrap::wrap(const wrap&), D.NNNN, ..., q)>' (found via
// -fdump-tree-original): a real copy-constructor call, not the plain
// copy a trivially-copyable type gets. oa_strip_conversion_call now
// recognizes this shape (a copy/move constructor called with exactly
// one *user-visible* argument, always the AGGR_INIT_EXPR's own last
// operand -- an extra, compiler-internal leading argument was found by
// direct testing to sometimes precede it) and resolves through to q
// itself, so g's own self-trust-seeded "q < 5" is found when checking
// f's own precondition at 'f (q)', without ever resolving wrap's
// value. See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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
inline constexpr conveyor_ctrl ctrl_v{};

struct wrap {
  int v;
  wrap (int v_) : v (v_) {}
  wrap (const wrap &other) : v (other.v) {}
  ~wrap () {}
  operator int () const conveyor { return v; }
};

int f (wrap x) pre<ctrl_v> (x < 5) { return x; }

int g (wrap q) pre<ctrl_v> (q < 5)
{
  return f (q);
}

int main () { return g (wrap (2)) - 2; } // { dg-warning "cannot verify" }
