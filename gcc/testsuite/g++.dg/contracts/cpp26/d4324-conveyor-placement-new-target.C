// D4324: the standard placement operator new returns its pointer argument,
// so the object it constructs is at that address -- the GIMPLE engine
// treated the call's result as unknown, unlike the AST engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
#include <new>

struct V { int x; V () conveyor : x (0) { } };

struct H
{
  V *p;
  V *get () const
    post<std::contracts::never_proven_conveyor_v> (r: std::is_object_address (r))
  { return p; }

  void make ()
  {
    contract_assert<std::contracts::never_proven_conveyor_v>
      (std::is_object_address (this));
    ::new ((void *) this->get ()) V;
  }
};

void f (H &h) { h.make (); }
