// Shared declarations for the conveyor_proof_plugin.cc test scenarios
// -- see .claude/plans/stateless-jumping-shore.md.  Definitions live in
// conveyor-proof-defs.cc, a separate translation unit pulled in via
// dg-additional-sources, so each scenario test only ever sees these
// declarations -- never the actual bodies.

#include <contracts>

namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

// Numeric-comparison demo: a callee's plain comparison-shaped
// precondition, not just std::is_object_address.
void use_positive (int x) pre<conveyor_ctrl_v> (x > 0);
int  compute_positive () post<conveyor_ctrl_v> (r: r > 0);
int  compute_negative () post<conveyor_ctrl_v> (r: r < 0);

// Predicate-chaining demo: check_it is a conveyor function whose own
// definition is never visible to any scenario test file (only to
// conveyor-proof-defs.cc) -- per the conveyor function rules, it's
// trusted to be well-defined by construction, so the plugin never
// needs to see its body to connect a postcondition to a precondition
// through it.
bool check_it (int v) conveyor;
int  produce () post<conveyor_ctrl_v> (r: check_it (r));
int  produce_bad () post<conveyor_ctrl_v> (r: !check_it (r));
void consume (int x) pre<conveyor_ctrl_v> (check_it (x));
