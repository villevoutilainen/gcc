// D4324: the implicit preconditions a template's reference parameters get
// (see d4324-ast-template-control-object-implicit-pre.C) in three shapes:
// - a parameter pack of a function template whose return type is deduced
//   (instantiated while its caller is being parsed): its contracts must
//   refer to the instantiation's own parameters, not copies of them
//   ("redeclaration of 'it#0'");
// - an argument type with an overloaded operator& returning void: the
//   object's address is meant, not that operator ("invalid use of void
//   expression");
// - a forwarding reference instantiated for a function: it refers to no
//   object, so its callers owe nothing for it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

#define NP std::contracts::never_proven_conveyor_v

template <class... I>
decltype(auto)
sum (const I &...it) pre<NP> (true)
{
  return (0 + ... + it);
}

int
use_sum ()
{
  int a = 1, b = 2;
  return sum (a, b);
}

template <class... I>
int
isum (const I &...it) pre<NP> (true)
{
  return (0 + ... + it);
}

struct NoAddr
{
  int v;
  void operator& () const;
};

template <class T>
const T &
same (const T &x) pre<NP> (true)
{
  return x;
}

int
use_same ()
{
  NoAddr n{3};
  return same (n).v;
}

template <class T, class U>
T
exchange (T &obj, U &&nv) pre<NP> (std::is_object_address (__builtin_addressof (obj)))
{
  T old = obj;
  obj = nv;
  return old;
}

void e ();
void (*fp) ();

void
use_exchange ()
{
  exchange (fp, e);
}

// One error per function: no follow-on errors after a conveyor violation.
void
bad1 (int &a)
{
  isum (a);	// { dg-error "cannot prove" }
}

void
bad2 (NoAddr &n)
{
  same (n);	// { dg-error "cannot prove" }
}
