// D4324: base_contract<D>() stands for D::h's preconditions even when D
// inherits them instead of writing them out, through a chain of such
// inheritances too (P4445 BUG-04, where it was vacuously true).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int violations;

struct rep {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) ++violations; }	// observes: returns and continues
};

struct inh : rep {
  static constexpr bool inherited (sc::assertion_static_info) { return true; }
};

struct B
{
  virtual void h (int x) pre<inh{}>(x > 0) {}
  virtual int g (int x) post<inh{}>(r: r > 0) { return x; }
};
struct D : B
{
  void h (int) override {}					// inherits x > 0
  int g (int x) override { return x; }				// inherits r > 0
};
struct D2 : D { void h (int) override {} };			// inherits it again
struct E : D2
{
  void h (int x) override pre<rep{}>(std::contracts::base_contract<D2>()) {}
};
struct F : D
{
  void h (int x) override pre<rep{}>(std::contracts::base_contract<D>()) {}
  int g (int x) override post<rep{}>(r: std::contracts::base_contract<D>())
  { return x; }
};

int main ()
{
  E e;
  e.h (1);
  if (violations != 0)
    __builtin_abort ();
  e.h (-1);			// base_contract<D2>() is x > 0: violated
  if (violations == 0)
    __builtin_abort ();
  int after_e = violations;
  F f;
  f.h (-1);			// base_contract<D>() is x > 0: violated
  if (violations == after_e)
    __builtin_abort ();
  int after_f = violations;
  f.g (1);			// r > 0 holds
  if (violations != after_f)
    __builtin_abort ();
  f.g (-1);			// base_contract<D>() in a post is r > 0: violated
  if (violations == after_f)
    __builtin_abort ();
  return 0;
}
