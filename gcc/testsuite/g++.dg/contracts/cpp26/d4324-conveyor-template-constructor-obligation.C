// D4324: a call to a constructor of a class template's instance owes its
// precondition, like a call to any other member: the constructor's clones,
// which the call resolves to, get its substituted contracts when a caller
// first needs them, not only once its definition is instantiated at the
// end of the file -- after such a caller was checked.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
struct A { };
template <class X> struct S2 { S2 (const X& a) pre<std::contracts::conveyor_assert_v> (std::is_object_address (&a)) {} };
template <class X> struct S3 { constexpr S3 (const X& a) pre<std::contracts::conveyor_assert_v> (std::is_object_address (&a)) {} };
template <class X> struct M2 { int m (const X& a) pre<std::contracts::conveyor_assert_v> (std::is_object_address (&a)) { return 0; } };
S2<A> g2 (const A& a) { return S2<A> (a); }	// { dg-error "cannot prove .is_object_address." }
void h2 (const A& a) { S2<A> s (a); }		// { dg-error "cannot prove .is_object_address." }
template <class T> S3<T> f3 (const T& a) { return S3<T> (a); }	// { dg-error "cannot prove .is_object_address." }
int k2 (const A& a) { M2<A> m; return m.m (a); }	// { dg-error "cannot prove .is_object_address." }
void ok () { A a; S2<A> s (a); S3<A> t (a); f3 (a); M2<A> m; m.m (a); }
