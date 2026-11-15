// D4324: a call given a route to an object may change it: what is known of
// its value ends at a call that isn't conveyor, given any route, and at a
// conveyor call given a route to write through (P4445 BUG-17).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>
namespace sc = std::contracts;

int quot (int a, int b) conveyor
  pre<sc::proven_conveyor_v> (b != 0) pre<sc::proven_conveyor_v> (a > -__INT_MAX__);

struct S
{
  int v;
  void keep ();
  int get () const;
  void set (int) conveyor;
  int peek () const conveyor;
};
void show (const S &);
void showi (const int &);
void showp (const int *);
void mod (S &);
int cv (const S &) conveyor;

// A call that isn't conveyor, given any route -- a const one too, since it
// may write a mutable member, or any member after a const_cast -- ends what
// is known of the object's value.
int r_cref () { S s; s.v = 5; show (s); return quot (10, s.v); }        // { dg-error "cannot prove that .s.S::v. is nonzero" }
int r_cthis () { S s; s.v = 5; (void) s.get (); return quot (10, s.v); } // { dg-error "cannot prove that .s.S::v. is nonzero" }
int r_cint () { int x = 5; showi (x); return quot (10, x); }            // { dg-error "cannot prove that .x. is nonzero" }
int r_cptr () { int x = 5; showp (&x); return quot (10, x); }           // { dg-error "cannot prove that .x. is nonzero" }
int r_ref () { S s; s.v = 5; mod (s); return quot (10, s.v); }         // { dg-error "cannot prove that .s.S::v. is nonzero" }
int r_this () { S s; s.v = 5; s.keep (); return quot (10, s.v); }      // { dg-error "cannot prove that .s.S::v. is nonzero" }
// So does a conveyor call given a route it may write through.
int r_convset () { S s; s.v = 5; s.set (0); return quot (10, s.v); }   // { dg-error "cannot prove that .s.S::v. is nonzero" }
// A conveyor call given only a const route can't change it.
int r_conv () { S s; s.v = 5; (void) cv (s); return quot (10, s.v); }
int r_convpeek () { S s; s.v = 5; (void) s.peek (); return quot (10, s.v); }
