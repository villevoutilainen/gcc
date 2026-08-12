// D4324: companion negative case for d4324-conveyor-callee-if-constexpr-
// ok.C -- confirms the 'if constexpr'-condition exemption in
// conveyor_restrictions_active_p is narrow: a call inside one of the
// if's own *branches* (not its condition) is an ordinary, potentially-
// executed statement, still subject to the usual callee-must-be-
// conveyor rule, even though the condition that selects which branch
// runs is itself exempt.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

namespace not_conveyor
{
  constexpr bool always_true () { return true; }
  bool helper ();
}

int f (int x) conveyor
{
  if constexpr (not_conveyor::always_true ()) // fine: exempt
    {
      if (not_conveyor::helper ()) // { dg-error "not declared .conveyor." }
	return x;
    }
  return 0;
}

int main () { return f (1); }
