// D4324 (GIMPLE engine) (P4445 2.2.2.5): only a union's active member is an object.
// A member a write, an initialization or a copy makes active, on every
// path, or a precondition or trust states active, is accessed freely;
// so is the common initial sequence of the active member's struct.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

using namespace std::contracts;

union U { int i; float f; };

// Written, initialized, value-initialized, copied, assigned whole.
float written () conveyor { U u{.i = 1}; u.f = 2; return u.f; }
int initialized () conveyor { U u{.i = 1}; return u.i; }
int value_initialized () conveyor { U u{}; return u.i; }
int copied () conveyor { U u{.f = 1}; U v = u; v = U{.i = 3}; return v.i; }
float copied_member () conveyor { U u{.f = 1}; U v = u; return v.f; }
// On every path.
int branches (int k) conveyor { U u{}; if (k) u.i = 1; else u.i = 2; return u.i; }
int loop () conveyor { U u{.i = 0}; for (int j = 0; j < 3; ++j) u.i = j; return u.i; }
// A pointer to the active member.
float pointer () conveyor { U u{.f = 1}; float *p = &u.f; return *p; }
// A write through a member of a member starts the member, [class.union].
struct S { int x; int y; };
union W { int i; S s; };
int chain () conveyor { W w{.i = 1}; w.s.x = 3; return w.s.x; }
// The common initial sequence of the active member's struct.
struct A { int tag; int a; };
struct B { int tag; float b; };
union V { A a; B b; };
int common_initial () conveyor { V v{.a = {1, 2}}; return v.b.tag; }
// A precondition states the active member; its callers prove it.
float get (const U &u) conveyor
  pre<conveyor_assert_v> (std::is_object_address (&u.f))
{ return u.f; }
float caller () conveyor { U u{.f = 1}; return get (u); }
struct H
{
  U u;
  float get () const conveyor
    pre<conveyor_assert_v> (std::is_object_address (&u.f))
  { return u.f; }
};
float member_caller () conveyor { H h{}; h.u.f = 2; return h.get (); }
// A tagged union: the code that checks the tag trusts the member.
struct T { int tag; union { int i; float f; }; };
float tagged (const T &t) conveyor
{
  if (t.tag == 1)
    {
      contract_assert<never_proven_conveyor_v> (std::is_object_address (&t.f));
      contract_assert<conveyor_assert_v> (t.f > 0);
      return t.f;
    }
  return 0;
}
