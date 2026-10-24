// P4222 Initialization profile: a class with no scalar storage at
// any depth (an empty class, one built only of empty classes, or an
// array of them) has nothing default initialization could leave
// indeterminate, so leaving it without an initializer needs no
// [[uninit]].  A class that does hold storage still does.
// { dg-do compile { target c++11 } }
// { dg-options "-O0" }

[[profiles::enforce(std::init)]];

class Foo { public: void run () {} };
struct E2 { Foo a; Foo b; };
struct D : Foo {};
struct BF { int : 0; };
struct DI : Foo { int x; };
struct U { union { int i; float f; }; };

void
test ()
{
  Foo foo;
  foo.run ();
  E2 e2;
  D d;
  BF bf;
  Foo arr[3];
  struct Local {} local;
  DI di;	// { dg-error "not initialized and not marked" }
  U u;		// { dg-error "not initialized and not marked" }
  (void) e2; (void) d; (void) bf; (void) arr; (void) local;
  (void) di; (void) u;
}
