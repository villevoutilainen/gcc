// D4324: ptr->field range identity resolution (oa_object_identity_decl,
// same shared helper d4324-conveyor-proof-predicate-conversion-ok.C
// exercises for the named-predicate shape) now looks through a
// wrapper's own implicit conversion operator. produce_count()'s
// postcondition establishes t->count in [40,100) for the object 'ref'
// converts to; consume_count()'s precondition requires t->count in
// [20,1000) on the same object, reached via ref's own conversion
// operator on a separate statement. See .claude/plans/well-we-last-
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
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

struct thing { int count; };
struct thing_ref { thing *t; operator thing* () const { return t; } };

void produce_count (thing * const t)
  post<conveyor_ctrl_v> (t->count >= 40 && t->count < 100)
{ t->count = 55; }
void consume_count (thing * const t)
  pre<conveyor_ctrl_v> (t->count >= 20 && t->count < 1000)
{ }

int main ()
{
  thing t;
  thing_ref ref{&t};
  produce_count (ref);
  consume_count (ref);
  return 0;
}
