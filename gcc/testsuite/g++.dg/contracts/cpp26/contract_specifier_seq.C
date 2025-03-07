// generic assert contract parsing checks
//   check omitted, 'default', 'audit', and 'axiom' contract levels parse
//   check that all concrete semantics parse
//   check omitted, '%default' contract roles parse
//   ensure that an invalid contract level 'invalid' errors
//   ensure that a predicate referencing an undefined variable errors
//   ensure that a missing colon after contract level errors
//   ensure that an invalid contract role 'invalid' errors
//   ensure that a missing colon after contract role errors
// { dg-do compile }
// { dg-options "-std=c++2a -fcontracts -fcontracts-nonattr" }

// one erroneous contract
int a() pre(f(3) > 2);  // { dg-error "was not declared" }

// erroneous contract in the middle
int b(int i)
  pre(true)
  pre(f(3) > 2) // { dg-error "was not declared" "" { target *-*-* } }
  post(true);

void c(int i)
  post(true) -> int ; // { dg-error "expected initializer" }


void f1(int i) pre(true) [[pre:i>0]]; // { dg-error "was not declared" }
void f2(int i) pre(k>0) [[pre:i>0]]; // { dg-error "was not declared" }
void f3(int i) pre(true [[pre:i>0]]); // { dg-error "was not declared" }
// { dg-error "shall only introduce an attribute" "" { target *-*-* } .-1 }
// { dg-error "expected" "" { target *-*-* } .-2 }

namespace other{

  static_assert (__cpp_contracts >= 201906);


  auto f(int x) -> int pre(x >= 0);

  auto f(int x) pre(x >= 0) -> int pre(x >= 0); // { dg-error "expected initializer before" }

  auto f(int x) pre(x >= 0) -> int; // { dg-error "expected initializer before" }

    template <class T>
  void g(int x) requires(true) pre(x >= 0);

  template <class T>
  void g2(int x) pre(x >= 0) requires(true); // { dg-error "expected initializer" }

  template <class T>
  void g3(int x) pre(x >= 0) requires(true) pre(x >= 0); // { dg-error "expected initializer" }

}

int main()
{
}
