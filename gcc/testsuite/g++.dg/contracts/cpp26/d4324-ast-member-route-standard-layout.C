// D4324: a non-conveyor call given a route to a member of an object ends
// the object's is_object_address fact too where the callee may legally get
// from the member to the object: a member of a union or a standard-layout
// class (pointer-interconvertibility, offsetof), a base class subobject (a
// static_cast down), an element of an array; level by level outward.  A
// member of any other class ends alone, and its object's other members
// stay.  A reference returned by a member call on an object counts as a
// route to one of its members.  A trust of the member, or of the object,
// makes it hold again.  The same in both engines.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>
#define TRUST(x) contract_assert<std::contracts::never_proven_conveyor_v>(std::is_object_address(&(x)))
#define CA std::contracts::conveyor_assert_v
struct A { int v; };
void touch(A&);
void touchc(const A&);
void touchp(A*);
// standard-layout
struct S { int n; A a; A& get_a() conveyor { return a; }
  int size() const conveyor pre<CA>(std::is_object_address(this)) { return n; } };
// not standard-layout: a virtual function
struct N { int n; A a; virtual void vf();
  A& get_a() conveyor { return a; }
  int size() const conveyor pre<CA>(std::is_object_address(this)) { return n; } };
int sl_ref(S& s)    { TRUST(s); touch(s.a);        return s.size(); } // { dg-error "cannot prove .is_object_address." }
int sl_cref(S& s)   { TRUST(s); touchc(s.a);       return s.size(); } // { dg-error "cannot prove .is_object_address." }
int sl_ptr(S& s)    { TRUST(s); touchp(&s.a);      return s.size(); } // { dg-error "cannot prove .is_object_address." }
int sl_acc(S& s)    { TRUST(s); touch(s.get_a());  return s.size(); } // { dg-error "cannot prove .is_object_address." }
int sl_p(S* p)      { TRUST(*p); touch(p->a);      return p->size(); } // { dg-error "cannot prove .is_object_address." }
int nsl_ref(N& s)   { TRUST(s); touch(s.a);        return s.size(); }
int nsl_ptr(N& s)   { TRUST(s); touchp(&s.a);      return s.size(); }
int nsl_acc(N& s)   { TRUST(s); touch(s.get_a());  return s.size(); }
int nsl_p(N* p)     { TRUST(*p); touch(p->a);      return p->size(); }
// nested: non-SL outer holding an SL inner
struct O { S in; virtual void vf(); };
int nest_in(O& o)   { TRUST(o); TRUST(o.in); touch(o.in.a); return o.in.size(); } // { dg-error "cannot prove .is_object_address." }
struct O2 { int k; S in; };   // SL outer
int nest_sl(O2& o)  { TRUST(o.in); touch(o.in.a); return o.in.size(); } // { dg-error "cannot prove .is_object_address." }
// re-trust of a member after its end
int retrust(O& o)   { TRUST(o); TRUST(o.in); touch(o.in.a); TRUST(o.in); return o.in.size(); }
// base subobject route reaches the derived object, standard-layout or not
struct D : S { virtual void vf(); };
void touchs(S&);
int base_route(D& d) { TRUST(d); touchs(d); return d.size(); } // { dg-error "cannot prove .is_object_address." }
// array element reaches the array; member of non-SL stops there
struct NA { A arr[4]; virtual void vf();
  int size() const conveyor pre<CA>(std::is_object_address(this)) { return 0; } };
int arr_el(NA& n)   { TRUST(n); touch(n.arr[1]); return n.size(); }
// a member of a non-standard-layout object ends alone
struct Q { int v; int get() const conveyor pre<CA>(std::is_object_address(this)) { return v; } };
struct NQ { Q q; Q r; virtual void vf(); };
void touchq(Q&);
int nsl_member(NQ& n) { TRUST(n); touchq(n.q); return n.q.get(); } // { dg-error "cannot prove .is_object_address." }
int nsl_other(NQ& n)  { TRUST(n); touchq(n.q); return n.r.get(); }
int nsl_whole(NQ& n)  { TRUST(n); touchq(n.q); TRUST(n); return n.q.get(); }
