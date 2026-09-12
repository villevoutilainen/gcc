// P4222 Initialization profile: pointer arithmetic on a [[ref_to_
// uninit]]-flavored pointer must not lose the flavor -- 'p + n' and
// '&p[n]' (the latter folding to the former, address-of-dereference
// cancelling out) never change WHETHER the pointer traces back to
// [[ref_to_uninit]] memory, only where within the same storage it
// points, matching invalidation-profile-gimple.cc's own identical
// POINTER_PLUS_EXPR handling and the paper's own "a [[ref_to_uninit]]
// cast to a pointer yields a [[ref_to_uninit]]" reasoning.
//
// ip_arg_uninit_flavored_p_1 (init-profile-gimple.cc) had no case for
// this at all: at full-SSA level 'p + n' is a genuinely different
// GIMPLE shape from the plain-copy case its own SSA_NAME handling
// already chased through ('_4 = _1 + _3;' is a two-operand
// GIMPLE_ASSIGN, so gimple_assign_single_p is false and that existing
// branch never fired), silently losing the flavor and falling through
// to "some other computed value -- not itself a pointer".
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

#include <utility>

struct S { int* elem [[ref_to_uninit]]; };

void other (int* q [[ref_to_uninit]]);

int f (S &s, int i)
{
  other (s.elem + i);
  other (&s.elem[i]);
  return *std::now_init (s.elem + i);
}
