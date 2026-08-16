// D4324/Stage 2a: conveyor-flavor counterpart of the basic field-alias
// repro (d4324-symbolic-proof-predicate-field-alias-invalidated-
// unknown.C), mirroring how d4324-conveyor-proof-predicate-alias-
// invalidated-unknown.C duplicates Stage 1's own core test across
// flavors -- m_predicate_fact_map and its new m_field_alias_target
// sibling are shared substrate either way. Here the underlying
// control-object check still runs at runtime regardless of the static
// prover's own conclusion, so the pre-fix version of this bug was
// "only" a missed diagnostic, not a silent runtime failure -- but a
// missed diagnostic is still the wrong answer, and the same fix closes
// it here too. See .claude/plans/well-we-last-discussed-ethereal-
// duckling.md.
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

struct file { bool opened = false; };
bool is_opened (const file *f) conveyor { return f != nullptr; }
struct holder { file *ptr; };

void open_it (file * const f) post<conveyor_ctrl_v> (is_opened (f)) { f->opened = true; } // { dg-warning "cannot verify postcondition" }
void mutate_via_alias (file *f) { f->opened = true; }
void use_it (file * const f) pre<conveyor_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file *p = &f;
  holder h;
  h.ptr = p;
  open_it (p);
  mutate_via_alias (h.ptr);
  use_it (p); // { dg-warning "cannot verify" }
  return 0;
}
