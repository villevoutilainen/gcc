// D4324: a conveyor violation rejects the program but leaves the code
// valid C++, so template instantiation doesn't go into its error recovery
// for it.  That recovery treats a constraint it won't check any more as
// satisfied, which here would make It(It<true>) viable for a copy of an
// It<false> and complete the ill-formed It<true>.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int plain (int x) { return x; }	// not conveyor

template<bool C> struct Bad;
template<> struct Bad<false> { using type = int; };

template<bool C>
struct It
{
  typename Bad<C>::type v {};	// ill-formed for C == true
  It () = default;
  It (It<!C> o) requires C : v (o.v) {}
};

template<class T>
int
use (T x)
{
  contract_assert<std::contracts::conveyor_assert_v> (plain (x) > 0);	// { dg-error "call to .int plain.int.., which is not declared .conveyor." }
  It<false> i;
  It<false> j (i);
  return j.v;
}

int main () { return use (1); }
