// D4324: an override with a covariant return type returns 'Derived *' where
// its base returns 'Base *'.  An inherited postcondition, and
// base_contract<Base> () in the override's own, see the result as a call
// through the base does: converted to the base's return type, adjusted to
// the base subobject, which here isn't at offset zero, a null pointer
// staying null; and for a reference return, the reference likewise.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int holds = 0, violated = 0;

template <bool Inherit>
struct count {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool inherited (sc::assertion_static_info) { return Inherit; }
  void operator() (const sc::assertion_context& ctx) const
  { ++(ctx.check () ? holds : violated); }
};
inline constexpr count<true> inherited_v{};
inline constexpr count<false> own_v{};

struct Other { long pad[4] = {}; virtual ~Other () = default; };

struct Base {
  int id = 1;
  virtual Base* clone (const bool null)
    post<inherited_v> (r: null ? r == nullptr : r != nullptr && r->id == 1)
  { return null ? nullptr : this; }
  virtual Base& self () post<inherited_v> (r: r.id == 1) { return *this; }
  virtual ~Base () = default;
};

// Base is at a non-zero offset in Derived.
struct Derived : Other, Base {
  Derived* clone (const bool null) override { return null ? nullptr : this; }
  Derived& self () override { return *this; }
};

struct Bad : Other, Base {
  Bad () { id = 2; }
  Bad* clone (const bool) override { return this; }   // id is 2: violates
};

struct Composed : Other, Base {
  int extra = 7;
  Composed* clone (const bool null) override
    post<own_v> (r: sc::base_contract<Base> () && (null || r->extra == 7))
  { return null ? nullptr : this; }
};

#define EXPECT(h, v) \
  do { if (holds != (h) || violated != (v)) __builtin_abort (); \
       holds = violated = 0; } while (0)

int main ()
{
  Derived d;
  if (static_cast<void*> (static_cast<Base*> (&d)) == static_cast<void*> (&d))
    __builtin_abort ();

  // The caller (Derived::clone is virtual) and the definition each check
  // the inherited postcondition.
  d.clone (false);
  EXPECT (2, 0);
  d.clone (true);
  EXPECT (2, 0);
  d.self ();
  EXPECT (2, 0);

  // Through Base: Base::clone's own, by the caller, and the inherited one
  // in Derived::clone.
  Base& b = d;
  b.clone (false);
  EXPECT (2, 0);

  Bad bad;
  bad.clone (false);
  EXPECT (0, 2);

  Composed c;
  c.clone (false);
  EXPECT (2, 0);
  c.clone (true);
  EXPECT (2, 0);
  return 0;
}
