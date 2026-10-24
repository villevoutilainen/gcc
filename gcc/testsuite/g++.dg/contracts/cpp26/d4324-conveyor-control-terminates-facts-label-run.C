// D4324: the conveyor P3400 label of
// d4324-conveyor-control-terminates-facts-label.C at run time, level a
// configured to observe: a failed level-a assertion is reported and
// execution continues.  Settling the label's semantic at compile time
// must leave its operator() itself untouched (substituting into a
// shared by-value argument once left a reference to a compile-time
// stand-in in the generated code).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontracts-group-evaluation-semantic=lvl.a:observe" }

#include <contracts>
#include <contract_labels>
namespace sc = std::contracts;
namespace P3400 = std::contracts::P3400;

inline constexpr char lvl_a[] = "A", lvl_b[] = "B";
int notes;
[[noreturn]] void die (const char *) { __builtin_abort (); }
void note (const char *) { ++notes; }

struct level_label : P3400::label_base<level_label>
{
  using assertion_control_object = level_label;
  static constexpr const char *s_a[2] = { "lvl.a", "lvl" };
  static constexpr const char *s_b[2] = { "lvl.b", "lvl" };
  const char *d_level;
  P3400::group_name_view group_names;
  constexpr level_label (const char *level)
    : d_level (level), group_names{ level == lvl_a ? s_a : s_b, 2 } { }
  static constexpr bool is_conveyor (sc::assertion_static_info) noexcept { return true; }
  static constexpr bool never_proven (sc::assertion_static_info) noexcept { return true; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  constexpr void operator() (const sc::assertion_context &ctx) const
  {
    if (ctx.check ())
      return;
    const sc::evaluation_semantic sem
      = this->__effective_semantic (this->__configured_semantic (ctx.static_info ()));
    if (sem == sc::evaluation_semantic::observe)
      {
	note (ctx.comment ());
	return;
      }
    die (ctx.comment ());
  }
};

int
check_a (int x)
{
  contract_assert<level_label{lvl_a}> (x > 0);
  return x;
}

int
check_b (int x)
{
  contract_assert<level_label{lvl_b}> (x > 0);
  return x;
}

int
main ()
{
  if (check_a (-1) != -1 || notes != 1)
    __builtin_abort ();
  if (check_b (5) != 5 || notes != 1)
    __builtin_abort ();
}
