// D4324: a call reached only through an unevaluated operand (decltype,
// sizeof, noexcept, or the discarded branch of a requires-expression)
// never generates any executed code, so it can't introduce UB regardless
// of whether its callee is declared 'conveyor' -- conveyor_restrictions_
// active_p returns false for the whole duration of such an operand.
// Found via a real regression: <type_traits>'s own __or_/__and_ (a
// pure, body-less SFINAE overload pair used only to compute a base-class
// type via decltype) made ordinary traits like is_scalar_v unusable from
// conveyor-restricted code. See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

namespace not_conveyor
{
  struct true_type { static const bool value = true; };
  struct false_type { static const bool value = false; };

  // Never defined -- like the real std::__or_fn, this is only ever used
  // inside a decltype to select a return type via overload resolution,
  // never actually called.
  true_type f ();

  template <class... _Bn>
    auto or_fn (int) -> decltype (f ());
  template <class... _Bn>
    auto or_fn (...) -> false_type;

  template <class... _Bn>
    struct Or : decltype (or_fn<_Bn...> (0))
    { };
}

int g (int x) conveyor
{
  if (sizeof (not_conveyor::f ()) == sizeof (not_conveyor::true_type))
    return x;
  if constexpr (requires { not_conveyor::f (); })
    return x;
  return not_conveyor::Or<int>::value ? x : x;
}

int main () { return g (1); }
