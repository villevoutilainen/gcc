// D4324: companion negative case for d4324-conveyor-callee-unevaluated-
// operand-ok.C -- confirms the unevaluated-operand exemption in
// conveyor_restrictions_active_p is narrow: a call that's actually
// evaluated (not just named inside decltype/sizeof/noexcept/a discarded
// requires-clause) must still be rejected, even when it sits right next
// to, or even inside the same expression as, an otherwise-exempt
// unevaluated operand.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

bool helper ();

int f (int x) conveyor
{
  bool unused = sizeof (helper ()) == sizeof (bool); // unevaluated: fine
  if (helper ()) // { dg-error "not declared .conveyor." }
    return x;
  return unused ? x : x + 1;
}

int main () { return f (1); }
