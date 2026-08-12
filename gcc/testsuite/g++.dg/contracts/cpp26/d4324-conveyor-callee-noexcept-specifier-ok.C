// D4324: a noexcept-specifier's own operand is a contextually converted
// constant expression of type bool (the same category as an
// explicit-specifier's), so a call reached only there can never
// generate executed code and so is exempt from the conveyor
// callee-must-be-conveyor check, the same way a converted constant
// expression or an unevaluated operand is. This specifically covers a
// *deferred* noexcept-specifier -- a dependent one, on a function
// template, whose evaluation is postponed past the initial parse
// (pt.cc's maybe_instantiate_noexcept, via DEFERRED_NOEXCEPT_ARGS) --
// since tsubst_expr rebuilds any call in it via the ordinary
// call-building machinery, before build_noexcept_spec's own evaluation
// of the result ever runs. Found via a real regression: extending
// _GLIBCXX_CONVEYOR_ASSERTIONS to <ranges>'s customization points hit
// ranges::__access::_Begin::operator()'s own
// 'noexcept(_S_noexcept<_Tp&>())', a function template calling its own
// private, non-conveyor consteval helper purely to compute its
// exception specification. See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

namespace not_conveyor
{
  constexpr bool helper (int x) { return x > 0; } // not declared conveyor
}

template<typename _Tp>
bool
f (_Tp x) noexcept (not_conveyor::helper (sizeof (_Tp))) conveyor
{ return true; }

int main () { return f (1) ? 0 : 1; }
