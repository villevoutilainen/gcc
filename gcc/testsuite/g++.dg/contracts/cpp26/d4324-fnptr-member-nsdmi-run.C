// D4324: a contracted function-pointer data member that also has a default
// member initializer: its pre clause is parsed at class completion like any
// other, and a call through it is checked (P4445 BUG-06, which was an ICE).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int violations;

struct count {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) ++violations; }	// observes: returns and continues
};

inline constexpr count count_v{};

void f (int, int) {}

struct S
{
  void (*fp) (int a, int b) pre<count_v>(a <= b) = f;
};

template<typename T>
struct ST
{
  void (*fp) (T a, T b) pre<count_v>(a <= b) = f;
};

int main ()
{
  S s;
  s.fp (1, 2);
  if (violations != 0)
    __builtin_abort ();
  s.fp (2, 1);
  if (violations != 1)
    __builtin_abort ();
  ST<int> t;
  t.fp (2, 1);
  if (violations != 2)
    __builtin_abort ();
  return 0;
}
