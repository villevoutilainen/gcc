// D4324: a call reached only while building or evaluating a converted
// constant expression -- an explicit-specifier's own operand, here --
// can never generate executed code (a manifestly constant expression
// can't have side effects or UB by the core language's own rules), so
// it's exempt from the conveyor callee-must-be-conveyor check the same
// way an unevaluated operand is. Found via a real regression extending
// _GLIBCXX_CONVEYOR_ASSERTIONS to <map>: std::pair's own conditionally-
// explicit constructors (explicit(bool), where bool comes from a
// constexpr helper like _S_convertible()) made completing std::pair<K,
// V> as a type impossible from inside std::_Rb_tree::erase() -- an
// ordinary, never-conveyor function -- purely because that completion
// happened to be needed there to instantiate _Rb_tree_iterator<pair<K,
// V>>. See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for std::map" { ! hostedlib } }

#define _GLIBCXX_CONVEYOR_ASSERTIONS
#include <map>

// Deliberately NOT declared conveyor -- _GLIBCXX_CONVEYOR_ASSERTIONS
// routes every __glibcxx_assert condition through the conveyor-checked
// control object regardless of whether the *enclosing* function is
// itself conveyor, exactly like the erase() overload this calls. An
// explicit is_object_address(&m) precondition is needed regardless of
// that -- a separate, ordinary Q1 caller obligation for erase()'s own
// is_object_address(this) precondition, unrelated to what this test
// itself exercises (the converted-constant-expression exemption).
auto
use (std::map<int, int>& m, std::map<int, int>::iterator it)
  pre<std::contracts::conveyor_assert_v>(std::is_object_address (&m))
{
  return m.erase (it);
}

int main () { return 0; }
