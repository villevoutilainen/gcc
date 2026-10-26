// D4324: an 'if' with a constant condition -- 'if constexpr' after
// instantiation, or 'if (true)' -- takes one arm unconditionally, so a fact
// established there holds after the if (the AST engine merged the arms as
// if either could be taken); one established in the untaken arm doesn't.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct V { int x; int get () const conveyor { return x; } };
V *mk ();

template<class P>
int f ()
{
  P p = mk ();
  if constexpr (__is_pointer (P))
    contract_assert<std::contracts::never_proven_conveyor_v>
      (std::is_object_address (__builtin_addressof (*p)));
  return p->get ();
}

int g () { return f<V *> (); }

int h ()
{
  V *p = mk ();
  if (true)
    contract_assert<std::contracts::never_proven_conveyor_v>
      (std::is_object_address (__builtin_addressof (*p)));
  return p->get ();
}

int k ()
{
  V *p = mk ();
  if (false)
    contract_assert<std::contracts::never_proven_conveyor_v>
      (std::is_object_address (__builtin_addressof (*p)));
  return p->get (); // { dg-error "cannot prove 'is_object_address'" }
}
