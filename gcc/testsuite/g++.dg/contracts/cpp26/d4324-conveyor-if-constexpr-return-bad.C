// D4324: companion negative case for d4324-conveyor-if-constexpr-
// return-ok.C -- confirms the 'if constexpr' return-path fix is narrow:
// when the condition genuinely selects the branch with no return
// (falling through the whole function), that instantiation is still
// correctly rejected, even though a *different* instantiation of the
// same template (selecting the other branch) is fine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

template <bool B>
int
f (int x) conveyor // { dg-error "conveyor function .* with non-.void. return type must contain a .return. statement" }
{
  if constexpr (B)
    return x;
  // No 'else': falls through when B is false.
} // { dg-warning "no return statement in function returning non-void" }

int main () { return f<false> (1); }
