// gimple_object_address_plugin.cc: item 6 for relational facts --
// make_val's own postcondition relating its return value to another
// parameter ('post<ctrl>(r: r < q)') establishes a relational fact for
// the SSA name 'y' gets assigned to (call_postcondition_relation_p/
// get_relational), oriented against THIS call's own substituted
// argument for q. consumer's own precondition "y < q" is then
// discharged purely by matching, never resolving q to any value. See
// ~/gimple-contract-analysis.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

int make_val (int x, int const q) pre<conveyor_ctrl_v> (x < q) post<conveyor_ctrl_v> (r: r < q) { return x; }
int consumer (int y, int const q) pre<conveyor_ctrl_v> (y < q) { return y; }

int caller (int x, int const q) pre<conveyor_ctrl_v> (x < q)
{
  int y = make_val (x, q);
  return consumer (y, q);
}

int main () { return caller (2, 5) - 2; }
