// D4324/Stage 5: the exact IILE-nested scenario this feature's own
// design review caught as a real crash/UB path, not a hypothetical one
// -- oa_resolve_iile_call constructs a fresh 'oa_env inner_env;' of its
// own to walk an immediately-invoked lambda's own body, and a fresh,
// default-constructed oa_env's own field-object-key cache pointer is
// null. Without this feature's own explicit propagation fix
// (inner_env.set_field_object_key_cache (env.field_object_key_cache
// ())), the first '&h->f' resolved while establishing OPEN_IT's own
// postcondition inside such a closure's body would dereference that
// null cache and crash the compiler outright.
//
// Confirmed via direct testing (not assumed) that, with the fix,
// compilation and execution both succeed with no crash -- but also
// confirmed that USE_IT's own outer consult still reports "cannot
// verify", not a silent discharge: 'h' as read inside the closure's own
// body is a distinct by-reference capture-proxy VAR_DECL, not the
// outer PARM_DECL, so oa_field_object_identity's own base resolution
// (oa_object_identity_decl) produces a *different* (base, FIELD_DECL)
// key inside the closure than 'h' itself produces in the enclosing
// function -- a real, narrower, separate residual gap (capture-proxy
// identity redirection for this specific resolver was never
// implemented; the only existing proxy redirection, in oa_provable_p,
// is for the unrelated is_object_address boolean-provability query,
// not for m_predicate_fact_map identity resolution). This test locks
// in that this residual gap manifests as a merely-imprecise, always-
// sound "cannot verify" -- never a crash, and never a silently wrong
// "verified" -- which is what this fix is actually responsible for.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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
struct holder { file f; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

void g (holder *h)
{
  int dummy;
  contract_assert<symbolic_ctrl_v>
    (std::is_object_address ([&]{ open_it (&h->f); return &dummy; }()));
  use_it (&h->f); // { dg-warning "cannot verify" }
}

int main ()
{
  holder h;
  g (&h);
  return 0;
}
