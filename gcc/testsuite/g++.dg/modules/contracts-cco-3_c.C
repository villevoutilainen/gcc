// { dg-additional-options { -std=c++26 -fmodules-ts -fcontracts -fcontract-control-objects -fcontracts-client-check=pre } }

import cco3_one;
import cco3_two;
#include <contracts>

int own_fails;
struct o_t
{
  static constexpr bool
  is_ignored (std::contracts::assertion_static_info) noexcept { return false; }
  constexpr void
  operator() (const std::contracts::assertion_context& c) const
  { if (!c.check ()) ++own_fails; }
};
inline constexpr o_t o{};

short g (short s, int *p) pre<o> (s > *p) { return s; }

int
main ()
{
  int z = 0;
  f1 (1); f2 (2.0, 1, 'x'); g (1, &z);
  if (fails_a || fails_b || own_fails)
    __builtin_abort ();
  f1 (0);		// Caller-side and definition checks: 2.
  f2 (0.0, 1, 'x');	// 2.
  f2 (2.0, 1, 0);	// 2.
  g (0, &z);		// 2.
  if (fails_a != 2 || fails_b != 4 || own_fails != 2)
    __builtin_abort ();
}
