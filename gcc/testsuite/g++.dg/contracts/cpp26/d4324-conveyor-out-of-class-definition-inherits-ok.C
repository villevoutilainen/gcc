// D4324: a conveyor function declared with a never_proven precondition
// for its reference parameter (it only casts, as std::move does) and
// defined out of class, or after a forward declaration, without
// repeating its contracts.  The definition still gets the implicit
// precondition synthesized for that parameter; it used to count as a
// contract list of its own, rejected for having fewer contracts than the
// first declaration, and replacing that declaration's, so callers were
// asked to prove the never_proven precondition's operand after all.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
#include <type_traits>

struct util
{
  template<class T>
  static std::remove_reference_t<T>& access (T&& ref) noexcept conveyor
    pre<std::contracts::never_proven_conveyor_v>
      (std::is_object_address (__builtin_addressof (ref)));

  static int& get (int& ref) noexcept conveyor
    pre<std::contracts::never_proven_conveyor_v>
      (std::is_object_address (__builtin_addressof (ref)));
};

template<class T>
std::remove_reference_t<T>& util::access (T&& ref) noexcept conveyor
{ return static_cast<std::remove_reference_t<T>&> (ref); }

int& util::get (int& ref) noexcept conveyor
{ return ref; }

int& cast (int& ref) noexcept conveyor
  pre<std::contracts::never_proven_conveyor_v>
    (std::is_object_address (__builtin_addressof (ref)));

int& cast (int& ref) noexcept conveyor
{ return ref; }

struct S { int v; };

// The references these take prove nothing: the callees ask nothing.
int a (S&& other) { return util::access (other).v; }
int b (int& r) { return util::get (r) + cast (r); }

int main ()
{
  S s { 1 };
  int i = 2;
  return a (static_cast<S&&> (s)) + b (i) == 5 ? 0 : 1;
}
