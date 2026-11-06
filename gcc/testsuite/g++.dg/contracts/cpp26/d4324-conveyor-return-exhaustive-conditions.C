// D4324: every path of a non-void conveyor function must end in a return;
// conditions that together cover every case aren't taken into account,
// and the diagnostic says so rather than that the function has no return
// statement (P4445 BUG-26).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int a) conveyor // { dg-error "may reach its end without a .return. statement" }
{ // { dg-message "every path must end in a .return." "" { target *-*-* } .-1 }
  if (a > 0) return a; else if (a <= 0) return -1;
} // { dg-warning "control reaches end of non-void function" }

int g (int a) conveyor { if (a > 0) return a; else return -1; }
