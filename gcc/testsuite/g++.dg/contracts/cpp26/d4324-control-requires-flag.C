// D4324: naming a contract control type (pre<T>/post<T>/contract_assert<T>)
// requires -fcontract-control-objects; under plain -fcontracts it is
// rejected with a specific diagnostic rather than silently accepted.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fsyntax-only" }

struct T {};

int f (int x) pre<T>(x > 0) { return x; }	// { dg-error "contract control types are only available with .-fcontract-control-objects." }

int g (int x) pre(x > 0) { return x; }		// bare form still works without the flag

void h (int x)
{
  contract_assert<T>(x > 0);	// { dg-error "contract control types are only available with .-fcontract-control-objects." }
  contract_assert (x > 0);	// bare form still works without the flag
}
