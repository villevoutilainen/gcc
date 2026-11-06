// D4324: const access in a conveyor function's declaration means nothing
// is written through it.  A conveyor function writes through const access
// -- a mutable member of a const object, or through a cast that removes
// const -- only to an object that began its lifetime within its own
// evaluation (P4445: routes by which a strict contract assertion had side
// effects).  A cast that removes const may also be the function's return
// value: its declaration shows what it hands out, and a caller writing
// through that is checked at the caller.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct M
{
  mutable int hits = 0;
  mutable int cache[2] = {};
  bool touch () const conveyor { hits = 1; return true; }	// { dg-error "write to mutable member 'M::hits'" }
  bool bump () const conveyor { ++hits; return true; }		// { dg-error "write to mutable member 'M::hits'" }
  bool slot () const conveyor { cache[0] = 1; return true; }	// { dg-error "write to mutable member 'M::cache'" }
  int read () const conveyor { return hits; }
};

bool touch_param (const M &m) conveyor { m.hits = 2; return true; }	// { dg-error "write to mutable member 'M::hits'" }

// A const object that began its lifetime here.
bool touch_local () conveyor { const M m{}; m.hits = 3; return m.hits == 3; }
bool touch_copy (const M m) conveyor { m.hits = 4; return true; }

void set (int *p) conveyor
  pre<std::contracts::conveyor_assert_v>(std::is_object_address (p))
{ *p = 7; }

bool sneaky (const int &a) conveyor
{ set (const_cast<int *> (&a)); return true; }	// { dg-error "cast removes 'const' from 'a'" }

bool sneaky_c (const int *a) conveyor
{ set ((int *) a); return true; }		// { dg-error "cast removes 'const'" }

bool own_cast () conveyor
{ const int x = 1; int *p = const_cast<int *> (&x); return p != nullptr; }

struct N
{
  N *next;
  N *self () const conveyor { return const_cast<N *> (this); }
};
