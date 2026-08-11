// D4324: the establish/invalidate vs. consult asymmetry this whole
// increment turns on (see .claude/plans/well-we-last-discussed-
// ethereal-duckling.md's own "establish/invalidate vs. consult
// asymmetry" section) -- copy-construction lookthrough is sound only at
// *consult* sites (a copy has the same value/state as its source at the
// moment of copying), never at *establish* sites (a callee that
// receives a copy and asserts something about it tells us nothing
// about the caller's own original). oa_object_identity_decl's own
// establish-side fix (this increment) deliberately adds only
// oa_strip_conversion_operator_call, never the full oa_strip_
// conversion_call with copy-construction lookthrough -- open_copy()'s
// own postcondition, about its own by-value parameter, must not be
// treated as establishing anything about 'original' at the call site in
// main(), so use_it(&original)'s precondition must still be reported as
// unverifiable (not silently, incorrectly discharged), even though
// 'original' really was opened directly beforehand -- the analysis
// simply has no path that would let it know that, and must not invent
// one. See that plan file for the full design rationale.
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

struct file {
  bool opened = false;
  file () = default;
  file (const file &other) : opened (other.opened) {}
};
bool is_opened (const file *f) conveyor { return f->opened; }

void set_opened (file *f) { f->opened = true; }
void open_copy (const file f) post<conveyor_ctrl_v> (is_opened (&f)) { }
void use_it (file * const f) pre<conveyor_ctrl_v> (is_opened (f)) { }

int main ()
{
  file original;
  set_opened (&original);
  open_copy (original);
  use_it (&original); // { dg-warning "cannot verify" }
  return 0;
}
