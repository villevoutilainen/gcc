// Axiom contracts (~/gcc-axiom-contracts.md): a function declared
// 'symbolic' has no definition and could never be evaluated at
// runtime, so calling it anywhere other than directly inside a
// contract_assert/pre/post condition is ill-formed -- mirroring the
// existing std::is_object_address stray-use restriction.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

bool is_opened (int* p) symbolic;

void bad_use (int* p)
{
  if (is_opened (p)) // { dg-error "declared .symbolic., may only be used directly inside" }
    return;
}

int main () { return 0; }
