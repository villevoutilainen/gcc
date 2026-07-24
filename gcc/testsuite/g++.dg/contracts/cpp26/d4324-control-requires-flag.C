// D4324: naming a contract control object (pre<obj>/post<obj>/
// contract_assert<obj>) requires -fcontract-control-objects; under plain
// -fcontracts it is rejected with a specific diagnostic rather than
// silently accepted.  The diagnostic fires as soon as '<' is seen, before
// the interior is parsed, so it doesn't matter here whether T names a type
// or an object.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fsyntax-only" }

struct T {};

int f (int x) pre<T>(x > 0) { return x; }	// { dg-error "contract control objects are only available with .-fcontract-control-objects." }

int g (int x) pre(x > 0) { return x; }		// bare form still works without the flag

void h (int x)
{
  contract_assert<T>(x > 0);	// { dg-error "contract control objects are only available with .-fcontract-control-objects." }
  contract_assert (x > 0);	// bare form still works without the flag
}
