// D4324: a contract_assert condition that constructs a temporary class
// object (e.g. via an implicit converting constructor, to compare
// against a value of a different type) has a TARGET_EXPR in its
// condition tree. The outliner's capture-finding walk (see
// d4324-assert-captures-local.C) must not mistake the TARGET_EXPR's own
// compiler-synthesized slot for a captured local -- that slot is
// already correctly given a fresh copy by the existing, generic
// copy_tree_body_r/remap_save_expr machinery; wrongly "capturing" it as
// an extra parameter instead rebinds it to a passed-in argument, and
// corrupts the temporary's own initialization entirely (this used to
// ICE in cp_genericize_target_expr for exactly this shape).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

int calls = 0;

struct probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    calls++;
    if (!ctx.check ())
      __builtin_abort ();
  }
};
inline constexpr probe probe_v{};

struct Widget {
  int v;
  constexpr Widget (int x) : v (x) {}
  constexpr bool operator!= (const Widget& o) const { return v != o.v; }
};

struct S {
  constexpr int
  check (const Widget& r) noexcept
  {
    // "r != 0" constructs a temporary Widget(0) to compare against.
    contract_assert<probe_v>(r != 0);
    return r.v;
  }
};

int main ()
{
  S s;
  Widget w (5);
  int r = s.check (w);
  if (r != 5)
    __builtin_abort ();
  if (calls != 1)
    __builtin_abort ();
  return 0;
}
