// D4324 (experimental): with both is_ignored and compute_semantic,
// is_ignored decides first, seeing the semantic before compute_semantic
// (the build's, here observe); an assertion it ignores is not computed a
// semantic.  If it doesn't ignore one, compute_semantic decides its
// semantic, and computing ignore ignores it too.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

int checks;
evaluation_semantic last;

struct both_t
{
  // Sees the build's observe: ignores every contract_assert.  (Seeing the
  // computed enforce instead, it would ignore none.)
  static constexpr bool
  is_ignored (assertion_static_info i) noexcept
  {
    return (i.kind () == assertion_kind::assert
	    && i.semantic () == evaluation_semantic::observe);
  }
  // Postconditions ignored, the rest enforced.
  static constexpr evaluation_semantic
  compute_semantic (assertion_static_info i)
  {
    return (i.kind () == assertion_kind::post ? evaluation_semantic::ignore
	    : evaluation_semantic::enforce);
  }
  constexpr void
  operator() (const assertion_context& c) const
  {
    ++checks;
    last = c.semantic ();
    c.check ();
  }
};
inline constexpr both_t both{};

int f (int x) pre<both> (x > 0) post<both> (r: r > 5) { return x; }
void g (int x) { contract_assert<both> (x > 0); }

int
main ()
{
  f (1);
  // Only the precondition was checked, with the computed enforce.
  if (checks != 1 || last != evaluation_semantic::enforce)
    __builtin_abort ();
  g (0);
  if (checks != 1)
    __builtin_abort ();
}
