// D4324: 'conveyor(auto)' is only well-formed on a function template
// (including a member template, a member -- template or not -- of a
// class template, or a generic lambda's call operator): those are the
// only contexts where more than one instantiation of the declaration
// could ever exist. An ordinary, fully concrete function has exactly
// one "instantiation", itself, so auto-deduction has nothing to do
// beyond what plain 'conveyor' already does, while trading its loud,
// immediate error for conveyor(auto)'s own silent-downgrade-on-
// violation behavior -- strictly worse there, so it's rejected
// outright.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

bool f (int x) conveyor(auto) // { dg-error "may only be used in a template" }
{ return x > 0; }

struct S
{
  bool g (int x) conveyor(auto) // { dg-error "may only be used in a template" }
  { return x > 0; }
};

int
main ()
{
  S s;
  return f (1) && s.g (1) ? 0 : 1;
}
