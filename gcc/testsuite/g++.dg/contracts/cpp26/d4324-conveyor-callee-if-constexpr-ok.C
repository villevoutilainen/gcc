// D4324: a call reached only inside an 'if constexpr' condition can
// never generate executed code for the discarded branch -- and the
// condition itself is a manifestly constant expression, so it can't
// have side effects or UB either -- so it's exempt from the conveyor
// callee-must-be-conveyor check the same way an unevaluated operand or
// a converted constant expression is. Found via a real regression
// extending _GLIBCXX_CONVEYOR_ASSERTIONS to <ranges>'s concat_view:
// std::variant's own _M_valid() (if constexpr (__never_valueless<...>
// ())...) made valueless_by_exception() unusable from conveyor-
// restricted code, blocking concat_view's own iterator comparisons.
// The call is rebuilt (and, for a member of a class template like this
// one, checked) during template substitution of the 'if constexpr'
// condition itself, before finish_if_stmt_cond's own cxx_constant_value
// call -- already exempt -- gets a chance to run. See .claude/plans/
// well-we-last-discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for std::vector" { ! hostedlib } }

#define _GLIBCXX_CONVEYOR_ASSERTIONS
#include <ranges>
#include <vector>

auto
use (std::vector<int>& v1, std::vector<int>& v2)
{
  auto cv = std::views::concat (v1, v2);
  auto it = cv.begin ();
  ++it;
  return it == cv.end ();
}

int main () { return 0; }
