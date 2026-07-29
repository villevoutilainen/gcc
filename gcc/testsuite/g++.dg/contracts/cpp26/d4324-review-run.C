// D4324: a review control object logs the violation and returns proceed,
// so execution continues past the failed assertion.  This verifies the
// end-to-end control-object dispatch with a real operator() at runtime.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

bool logged = false;
namespace sc = std::contracts;

struct my_review {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify (sc::evaluation_semantic) { return false; }
  static constexpr bool assumable (sc::evaluation_semantic) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (ctx.check ()) return; logged = true; }		// returns -> continue
};

inline constexpr my_review my_review_v{};

int f (int x) pre<my_review_v>(x > 0) { return x; }

int main ()
{
  int r = f (-1);
  if (!logged)
    __builtin_abort ();
  if (r != -1)
    __builtin_abort ();
  return 0;
}
