// D4324/P3400: compute_semantic and group_names can now be genuine,
// non-static per-instance members again -- not forced static, the way
// this header's initial port had to make them before the compiler
// gained real (non-dummy-object) non-static control-object trait
// dispatch (see gcc/cp/contracts.cc's contract_control_bool_member and
// this header's label_base<Self>/semantic_computation_label/
// identification_label) -- matching the real P3400 paper's own model of
// a materialized, per-instance control object. Two objects of the exact
// same type, carrying different instance data, are dispatched
// differently below: something categorically impossible when these
// facets had to be static (shared by the whole type, never the
// instance).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe -D_P3400_FAKE_GROUP_CONFIG=\"only_first=ignore\"" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <exception>
#include <cstdlib>

namespace sc = std::contracts;
namespace P3400 = std::contracts::P3400;

// ---- A stateful semantic_computation_label ----
// Escalates to quick_enforce only when *this instance's* own severity
// crosses a threshold; otherwise passes the configured semantic through
// unchanged. Both objects below share the exact same type.
struct escalate_above_t : P3400::label_base<escalate_above_t>
{
  using assertion_control_object = escalate_above_t;
  int severity;

  constexpr escalate_above_t (int __severity) noexcept
  : severity (__severity) { }

  constexpr sc::evaluation_semantic
  compute_semantic (sc::evaluation_semantic __in) const noexcept
  { return severity > 5 ? sc::evaluation_semantic::quick_enforce : __in; }
};

inline constexpr escalate_above_t low_severity (2);
inline constexpr escalate_above_t high_severity (9);

// Under this TU's -fcontract-evaluation-semantic=observe: low_severity's
// compute_semantic leaves the semantic at observe (checked, but doesn't
// terminate); high_severity's escalates the same kind of violation to
// quick_enforce (terminates).
int f_low (int x) pre<low_severity>(x > 0) { return x; }
int f_high (int x) pre<high_severity>(x > 0) { return x; }

// ---- A stateful identification_label ----
// group_names is a real, non-static per-instance member -- not a fixed
// static array shared by the whole type.
struct dynamic_group_t : P3400::label_base<dynamic_group_t>
{
  using assertion_control_object = dynamic_group_t;
  P3400::group_name_view group_names;

  constexpr dynamic_group_t (const char* const* __names, __SIZE_TYPE__ __n) noexcept
  : group_names{ __names, __n } { }
};

inline constexpr const char* const only_first_names[] = { "only_first" };
inline constexpr const char* const only_second_names[] = { "only_second" };
inline constexpr dynamic_group_t first_group (only_first_names, 1);
inline constexpr dynamic_group_t second_group (only_second_names, 1);

// _P3400_FAKE_GROUP_CONFIG="only_first=ignore" (see dg-additional-options
// above): first_group's own instance group_names matches and is ignored
// entirely; second_group -- the exact same type, different instance
// data -- doesn't match and stays fully checked (observe: continues
// after the violation). This is only possible because the group-config
// lookup consults each object's own group_names, not a value shared by
// the whole type.
int g_first (int x) pre<first_group>(x > 0) { return x; }
int g_second (int x) pre<second_group>(x > 0) { return x; }

// combined_label must thread a non-static operand's real instance
// group_names through correctly too: its own group_names is now a
// regular data member, computed at construction from the real
// _M_lhs/_M_rhs instances (not a class-scope static constant computed
// purely from _LHS/_RHS's types).
int h_combined (int x) pre<first_group | P3400::opt>(x > 0) { return x; }

int step = 0;

void
my_terminate ()
{
  // Only f_high's escalation (the last, deliberately-terminating call)
  // may ever reach here; anything earlier reaching this point is a bug.
  if (step == 5)
    std::exit (0);
  std::exit (1);
}

int
main ()
{
  std::set_terminate (my_terminate);

  step = 1;
  if (f_low (-1) != -1)
    __builtin_abort ();

  step = 2;
  if (g_first (-1) != -1)
    __builtin_abort ();

  step = 3;
  if (g_second (-1) != -1)
    __builtin_abort ();

  step = 4;
  if (h_combined (-1) != -1)
    __builtin_abort ();

  step = 5;
  f_high (-1);
  __builtin_abort ();	// unreachable in the expected run
}
