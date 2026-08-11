// D4324: companion negative case for d4324-conveyor-callee-converted-
// constant-expr-ok.C -- confirms the converted-constant-expression
// exemption in conveyor_restrictions_active_p is narrow: merely having
// a class's explicit-specifier call a constexpr helper (exempt, since
// that's a converted constant expression, never executed) does not
// exempt an ACTUAL, executed call to that same class's constructor from
// conveyor-restricted code -- that's an ordinary, executed call, still
// subject to the usual callee-must-be-conveyor rule.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

namespace not_conveyor
{
  constexpr bool is_narrow () { return true; }
}

struct Box
{
  explicit(not_conveyor::is_narrow ()) Box (int) { } // fine: exempt
};

int f (int x) conveyor
{
  Box b (x); // { dg-error "not declared .conveyor." }
  return x;
}

int main () { return f (1); }
