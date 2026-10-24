// D4324: as d4324-conveyor-control-terminates-facts.C, for a conveyor
// P3400 label (a library-defined control object deriving from
// std::contracts::P3400::label_base) that follows the build
// configuration: its semantic comes from the group named after its
// level (-fcontracts-group-evaluation-semantic=), else the TU's.  Its
// operator() reports and continues under observe and terminates
// otherwise; whether a given contract terminates is settled at compile
// time from the label's own data (its 'this'), the contract's static
// info, and a const local computed from them.  Here, with the default
// semantic (enforce), both levels establish their preconditions; see
// d4324-conveyor-control-terminates-facts-label-group.C for a level
// configured to observe.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
#include <contract_labels>
namespace sc = std::contracts;
namespace P3400 = std::contracts::P3400;

inline constexpr char lvl_a[] = "A", lvl_b[] = "B";
[[noreturn]] void die (const char *);
void note (const char *);

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

int ga (int *p) conveyor pre<level_label{lvl_a}> (std::is_object_address (p)) { return *p; }
int gb (int *p) conveyor pre<level_label{lvl_b}> (std::is_object_address (p)) { return *p; }
