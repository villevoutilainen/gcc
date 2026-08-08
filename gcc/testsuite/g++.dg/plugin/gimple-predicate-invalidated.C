// gimple_object_address_plugin.cc: invalidation -- an intervening call
// to an ordinary, uncontracted function ('unrelated(&f)') receiving
// f's own address between open() and read() must invalidate the
// is_opened(f) fact established by open(), since there is no way to
// know unrelated() didn't close it (invalidate_predicate_call_args,
// mirroring contracts.cc's own oa_invalidate_symbolic_facts_for_call_
// args). See ~/gimple-contract-analysis.md.
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

struct io_facility {
  static bool is_opened (io_facility*) { return true; }
  void open () post<conveyor_ctrl_v>(is_opened (this)) {}
  void read () pre<conveyor_ctrl_v>(is_opened (this)) {}
};

void unrelated (io_facility *) {}

void invalidated_caller ()
{
  io_facility f;
  f.open ();
  unrelated (&f);
  f.read (); // { dg-warning "gimple-oa: cannot verify" }
}

int main () { invalidated_caller (); return 0; }
