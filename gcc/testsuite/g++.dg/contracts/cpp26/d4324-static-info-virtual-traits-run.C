// D4324: the control object's compile-time queries (is_ignored, inherited,
// ...) get the same assertion_static_info::is_virtual () and
// overrides_virtual () as its operator () does: those of the function the
// contract belongs to (P4445 BUG-05).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int ran;

struct only_virtual {
  static constexpr bool
  is_ignored (sc::assertion_static_info i) { return !i.is_virtual (); }
  void operator() (const sc::assertion_context&) const { ran += 1; }
};

struct only_overrides {
  static constexpr bool
  is_ignored (sc::assertion_static_info i) { return !i.overrides_virtual (); }
  void operator() (const sc::assertion_context&) const { ran += 100; }
};

// Inherited by an override only when its function is virtual; decided
// while the overriding class is being completed.
struct inh_if_virtual {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool
  inherited (sc::assertion_static_info i) { return i.is_virtual (); }
  void
  operator() (const sc::assertion_context& c) const
  { if (!c.check ()) ran += 10000; }
};

struct B
{
  void plain () pre<only_virtual{}>(true) {}
  virtual void f () pre<only_virtual{}>(true) {}
  virtual void g () pre<only_overrides{}>(true) {}
  virtual void h (int x) pre<inh_if_virtual{}>(x > 0) {}
};

struct D : B
{
  void g () override pre<only_overrides{}>(true) {}
  void h (int) override {}
};

int main ()
{
  D d;
  B& b = d;
  b.plain ();		// ignored: not virtual
  if (ran != 0)
    __builtin_abort ();
  b.f ();		// virtual: checked on both sides
  if (ran != 2)
    __builtin_abort ();
  b.g ();		// B::g's caller side ignored, D::g's definition side not
  if (ran != 102)
    __builtin_abort ();
  d.h (-1);		// D::h inherited B::h's contract: violated on both sides
  if (ran != 20102)
    __builtin_abort ();
  return 0;
}
