// D4324: assertion_static_info::is_virtual () and overrides_virtual ()
// describe the function the contract belongs to, on both sides of the
// check (P4445 BUG-05, where both were always false).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontracts-client-check=all" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

// Bit 0: seen non-virtual.  Bit 1: seen virtual, not overriding.
// Bit 2: seen virtual and overriding.  Bit 3: inconsistent.
template<int N> int seen;

template<int N>
struct rec {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    bool v = ctx.static_info ().is_virtual ();
    bool o = ctx.static_info ().overrides_virtual ();
    if (o && !v)
      seen<N> |= 8;
    else
      seen<N> |= !v ? 1 : !o ? 2 : 4;
  }
};

template<int N> inline constexpr rec<N> rec_v{};

struct B
{
  void plain () pre<rec_v<0>>(true) {}
  virtual void f () pre<rec_v<1>>(true) {}
  virtual ~B () pre<rec_v<3>>(true) {}
};

struct D : B
{
  void f () override pre<rec_v<2>>(true) {}
  ~D () override pre<rec_v<4>>(true) {}
};

struct E : D
{
  void f () override pre<rec_v<5>>(true) { contract_assert<rec_v<6>>(true); }
};

int main ()
{
  {
    E e;
    B& b = e;
    b.plain ();
    b.f ();		// B::f's caller-side check; E::f's definition side
    e.D::f ();		// D::f, both sides
  }
  if (seen<0> != 1)	// B::plain: non-virtual
    __builtin_abort ();
  if (seen<1> != 2)	// B::f: virtual, overrides nothing
    __builtin_abort ();
  if (seen<2> != 4)	// D::f: overrides B::f
    __builtin_abort ();
  if (seen<5> != 4)	// E::f: overrides D::f
    __builtin_abort ();
  if (seen<6> != 1)	// contract_assert: not a function contract
    __builtin_abort ();
  if (seen<4> != 4)	// ~D overrides ~B
    __builtin_abort ();
  if (seen<3> != 2)	// ~B: virtual, overrides nothing
    __builtin_abort ();
  return 0;
}
