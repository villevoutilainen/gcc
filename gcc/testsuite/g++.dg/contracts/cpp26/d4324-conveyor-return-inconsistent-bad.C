// D4324, Increment O: "all exit paths return" is now checked for real
// (not just "at least one return statement exists"), reusing
// oa_stmt_terminates_p directly -- only one branch of the if returns,
// so the function can fall off its own end; must be rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (bool flag) conveyor // { dg-error "conveyor function .int f\\(bool\\). with non-.void. return type must contain a .return. statement" }
{
  if (flag)
    return 1;
} // { dg-warning "control reaches end of non-void function" }

int main () { return f (true) - 1; }
