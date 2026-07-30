// D4324: a control object named as a temporary (e.g.
// noexcept_assert("message"), or any other aggregate-paren-initialized
// per-instance-state object -- see d4324-control-object-state.C) must
// work the same way inside a template as it already does outside one.
// A temporary control-object expression parsed inside a template is
// left as an unresolved, functional-cast-shaped node regardless of
// whether it actually mentions a template parameter -- tsubst_contract
// used to only re-substitute the control object when
// uses_template_parms said it needed to, and then via plain tsubst
// rather than tsubst_expr, either of which left that raw node in
// place: the constexpr evaluator has no case for it at all (an ICE),
// and even fixing just the first half surfaces tsubst's own "sorry,
// unimplemented" for the same raw node -- tsubst_expr is what actually
// finishes resolving it into a real, evaluatable construction.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

struct probe {
  const char* message = nullptr;
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    if (!ctx.check ())
      __builtin_abort ();
  }
};

template <typename T>
struct V {
  T arr[4];

  constexpr T&
  at (int n)
  {
    contract_assert<probe("n < 4")>(n < 4);
    return arr[n];
  }
};

int main ()
{
  V<int> v{{1, 2, 3, 4}};
  return v.at (2) - 3;
}
