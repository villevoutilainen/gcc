// D4324/P2680: -fcontract-conveyor-proofs, ptr->field range proof,
// OA_PROVEN_FALSE case -- produce_count_bad()'s postcondition
// establishes this->count in [200,300), fully disjoint from
// consume_count()'s required [20,100) -- a genuine, provable
// violation, closing the same field-range gap as d4324-conveyor-proof-
// field-range-ok.C.  See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
// { dg-do compile { target c++26 } }
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
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

struct thing {
  int count;
  void produce_count_bad ()
    post<conveyor_ctrl_v>(this->count >= 200
			  && this->count < 300)
  { count = 250; }
  void consume_count ()
    pre<conveyor_ctrl_v>(this->count >= 20 && this->count < 100)
  { }
};

void caller ()
{
  thing t;
  t.produce_count_bad ();
  t.consume_count (); // { dg-error "provably violates the precondition" }
                      // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
}

int main () { caller (); return 0; }
