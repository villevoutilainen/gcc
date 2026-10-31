// D4324: in a non-mutable '[this]' lambda, a trust of a member read
// through the captured 'this' holds for the body's own reads of it, as
// it does in a member function: the body reloads '__closure->__this',
// which it can't change -- a generic lambda's instantiation too.  A
// member call on the referent still ends
// what was known of it, there as here.  The diagnostic names the
// member without crashing on the captured 'this'.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

struct C
{
  int v;
  void touch () conveyor pre<conveyor_assert_v> (std::is_object_address (this)) {}
};
struct D { int v; };
void use (D *p) conveyor pre<conveyor_assert_v> (std::is_object_address (p)) {}

struct S
{
  C &c;
  D *p;
  template<typename F> void visit (F f) { int x = 0; f (x); }
  void g ()
  {
    visit ([this] (auto &) {
      contract_assert<never_proven_conveyor_v>
	(std::is_object_address (__builtin_addressof (this->c)));
      this->c.touch ();
    });
    auto a = [this] {
      contract_assert<never_proven_conveyor_v> (std::is_object_address (p));
      use (p);
    };
    a ();
    auto b = [this] {
      contract_assert<never_proven_conveyor_v>
	(std::is_object_address (__builtin_addressof (c)));
      c.touch ();
      c.touch (); // { dg-error "cannot prove 'is_object_address' for 'S::c'" }
    };
    b ();
    auto u = [this] {
      use (p); // { dg-error "cannot prove 'is_object_address' for 'S::p'" }
    };
    u ();
  }
};

void h (S &s) { s.g (); }
