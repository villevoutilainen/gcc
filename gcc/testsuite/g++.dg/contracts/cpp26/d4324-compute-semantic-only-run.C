// D4324 (experimental): is_ignored and compute_semantic are mutually
// optional.  A control object with only compute_semantic is ignored
// where the semantic it computes is ignore, and otherwise sees that
// semantic in its operator().
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

int checks, fails;
evaluation_semantic last;

struct only_compute_t
{
  // Postconditions ignored, preconditions observed.
  static constexpr evaluation_semantic
  compute_semantic (assertion_static_info i)
  {
    return (i.kind () == assertion_kind::post ? evaluation_semantic::ignore
	    : evaluation_semantic::observe);
  }
  constexpr void
  operator() (const assertion_context& c) const
  {
    ++checks;
    if (!c.check ())
      {
	++fails;
	last = c.semantic ();
      }
  }
};
inline constexpr only_compute_t only_compute{};

int f (int x) pre<only_compute> (x > 0) post<only_compute> (r: r > 5)
{ return x; }

int
main ()
{
  f (0);
  // The precondition was checked, observed; the postcondition ignored.
  if (checks != 1 || fails != 1 || last != evaluation_semantic::observe)
    __builtin_abort ();
}
