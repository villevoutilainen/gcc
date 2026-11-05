// D4324: an element or a field of an owned object is owned at any depth,
// in both engines: '*this''s member array's element, a union member's
// array element, a nested struct's array element.  The AST engine looked
// one level deep only, rejecting 'this->buf[0]' while accepting '*buf'
// (the same object) -- found with libc++'s basic_string, whose
// __get_short_pointer passes '__rep_.__s.__data_[0]' to pointer_to.
// { dg-do compile { target c++26 } }
// { dg-options "-fcontracts -fcontract-control-objects" }
// { dg-additional-options "-fcontract-analysis-engine=ast" { target c++26 } }

#include <contracts>

template <class T>
T *
id (T &x) conveyor
  pre<std::contracts::never_proven_conveyor_v> (std::is_object_address (__builtin_addressof (x)))
{
  return __builtin_addressof (x);
}

struct A
{
  char buf[4];
  union U { char d[4]; long l; } u;
  struct S { char d[4]; } s;
  char *sub () conveyor { return id (buf[0]); }
  char *der () conveyor { return id (*buf); }
  char *uni () conveyor { return id (u.d[0]); }
  char *str () conveyor { return id (s.d[1]); }
};

char *g (A &a) conveyor { return id (a.s.d[0]); }

int
main ()
{
  A a{};
  return a.sub () != a.der ();
}
