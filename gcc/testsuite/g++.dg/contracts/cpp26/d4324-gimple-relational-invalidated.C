// The built-in GIMPLE-pass engine's own version of the reassignment
// case -- unlike the AST-walk (which needs an explicit oa_env::
// relational_invalidate_involving call on every reassignment), this
// engine needs NO invalidation logic at all: 'x = x + 1;' produces a
// brand-new SSA name by construction, so established_rel's own entry
// for the OLD SSA name of x is simply never looked up again when
// checking the call below, which sees the NEW SSA name instead -- see
// this file's own top comment on SSA identity. Confirms this holds
// empirically for relational facts too, not just is_object_address/
// nonzero/range.
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
inline constexpr conveyor_ctrl ctrl_v{};

int f (int x, int const q) pre<ctrl_v> (x < q) post<ctrl_v> (r: r < q) { return x; }
int g (int x, int const q) pre<ctrl_v> (x < q)
{
  x = x + 1;
  return f (x, q); // { dg-warning "cannot verify that .x. satisfies the precondition" }
}

int main () { return g (2, 5) - 3; }
