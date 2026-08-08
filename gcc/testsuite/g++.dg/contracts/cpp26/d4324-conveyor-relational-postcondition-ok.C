// D4324: item 6 for relational facts -- a callee's own postcondition
// relating its return value to one of its OTHER parameters (e.g.
// 'post<ctrl>(r: r < q)') establishes a relational fact for whatever
// decl the call's own result gets assigned to (oa_establish_
// relational_from_call in contracts.cc), oriented against THIS call's
// own substituted argument for that other parameter -- 'int y =
// make_val (x, q);' establishes "y < q" (q being caller's own q, not
// make_val's own). consumer's own precondition "y < q" is then
// discharged purely by matching against that established fact, never
// resolving q to any numeric value. Isolated from the (separately
// tested) precondition obligation at the call to make_val itself via
// caller's own self-trusted "x < q". See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

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
inline constexpr conveyor_ctrl ctrl_v{};

int make_val (int x, int const q) pre<ctrl_v> (x < q) post<ctrl_v> (r: r < q) { return x; }
int consumer (int y, int const q) pre<ctrl_v> (y < q) { return y; }

int caller (int x, int const q) pre<ctrl_v> (x < q)
{
  int y = make_val (x, q);
  return consumer (y, q);
}

int main () { return caller (2, 5) - 2; }
