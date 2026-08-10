// D4324/Stage 3: pins the disclosed, still-open field/array-slot-write
// residual gap as current, honest behavior, mirroring Stage 2a's own
// nested-declines-ok.C role -- this stage's own parameter-alias-group
// sweep is deliberately scoped to whole-object invalidation only (Rule
// 1's whole-object branch, Rule 2, and oa_handle_loop's two per-
// reassigned-decl sites); a parameter that is itself a pointer to a
// struct, mutated through a narrower field write (Stage 2a's own
// single-slot 'field_alias_invalidate'/'contract_field_range_
// invalidate'), does NOT get this treatment. So 'h1'/'h2' (both
// 'holder *' parameters, exact-type-matched, definitely groupable in
// principle) still silently discharge here, even though calling this
// with 'g(&h, &h)' would make 'h1->ptr' and 'h2->ptr' the exact same
// slot -- an honestly documented, real residual gap of the same shape
// this whole stage otherwise fixes, not a soundness claim. See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct file { bool opened = false; };
bool is_opened (const file *f) symbolic;
struct holder { file *ptr; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

void g (holder *h1, holder *h2)
{
  open_it (h1->ptr);
  mutate_via_alias (h2->ptr);
  use_it (h1->ptr); // known gap: silently discharges, no diagnostic
}

int main ()
{
  file f;
  holder h;
  h.ptr = &f;
  g (&h, &h);
  return 0;
}
