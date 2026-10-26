// D4324/P2680: a string literal is an lvalue of static storage duration,
// so a reference parameter bound to one ('opt == "x"' against a
// 'const _Up&' parameter) has its is_object_address precondition
// proven.  The AST engine had no rule for it; the GIMPLE engine did.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

template<typename T>
int
length (const T& s) conveyor
{
  return sizeof (s);
}

int
first (const char (&s)[3]) conveyor
{
  return s[0];
}

int
f ()
{
  return length ("x") + first ("ab") + length (u8"yz");
}
