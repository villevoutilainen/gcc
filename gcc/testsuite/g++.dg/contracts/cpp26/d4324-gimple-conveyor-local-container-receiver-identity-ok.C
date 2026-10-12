// D4324 bounds-proving demo, GIMPLE mirror of d4324-conveyor-local-
// container-receiver-identity-ok.C -- see that file's own comment for
// the full story of root causes #1 and #2, found on the AST side.
//
// A THIRD, GIMPLE-only root cause surfaced once #1 and #2 were fixed
// there: the GIMPLE engine's own receiver-identity comparisons (cg_
// check_call_relational_fact and three siblings: the self-trust call-
// relational/call-call-relational checks, and cg_shifted_comparison_
// no_wrap_ok_p) compared raw tree pointers with a bare '!='/'==', with
// NO stripping at all -- unlike the AST side, which at least called
// oa_strip_to_relational_operand first. '&test' materialized for
// 'test.size ()' and '&test' materialized for 'test[x]' are two
// separately-allocated ADDR_EXPR nodes wrapping the identical VAR_DECL
// 'test', so the bare pointer comparison always failed even once the
// AST-side's own two fixes were ported over (confirmed via a temporary
// debug print, not gdb alone: gdb's own variable tracking proved
// unreliable at exact PCs in this optimized build, giving inconsistent
// "optimized out" vs. seemingly-NULL readings for the same variable at
// different breakpoint locations). Fixed by exporting oa_strip_to_
// receiver_identity (contracts.cc/contracts.h, previously static) so
// this file's own 4 analogous comparison sites can reuse it instead of
// comparing raw pointers.
//
// Root cause #2's own GIMPLE-side analogue also needed its own,
// separate fix, not just reusing contracts.cc's own oa_underlying_
// param_operand: cg_cond_is_bare_param (the GIMPLE-native analogue of
// that function) required SSA_NAME_IS_DEFAULT_DEF in addition to a
// PARM_DECL match. That requirement exists to pick out a reassignable
// PARAMETER's one canonical (incoming-argument) SSA version; a local
// variable has no such ambiguity to resolve (its own SSA name is
// already uniquely tied to its own one defining statement, and the
// established fact is keyed on that exact SSA name, not the decl, so
// SSA's own uniqueness guarantee alone is sound). Fixed by adding an
// ALLOW_VAR_DECL parameter, passed true from all 4 of this function's
// own call sites (cg_match_shifted_comparison_against_call and cg_
// refine_relational_edge_into) -- confirmed, unlike the AST-side
// oa_match_comparison_against_call, every one of this function's own
// callers is already the "within-function, branch-derived fact"
// family, with no separate "declared contract text" caller needing the
// stricter default preserved.
// D4324 fix (2026-10-12), unrelated to this file's own receiver-
// identity fix: use_local_container_unguarded_bad's own dg-warning
// below was previously silenced entirely -- root cause was a genuine
// instantiation-ordering bug: an ordinary, non-template caller gets
// its own GIMPLE contracts pass run eagerly, the moment its own body
// is gimplified, well before GCC's own ordinary deferred-template-
// instantiation schedule would otherwise instantiate std::vector<int>
// ::operator[]/size () (both template member functions). Consulting a
// callee's own contract-activity cache (populated once, at that
// callee's own finish_function time) before the callee has even been
// instantiated silently found nothing and treated every one of its
// obligations as inactive, with no diagnostic at all -- this file's
// own original receiver-identity/local-index fixes above are
// unaffected either way. Fixed in pass_contracts_gimple::execute
// (contracts-gimple.cc) by force-instantiating every callee this
// function's own body calls before consulting that cache.
// { dg-do compile { target c++26 } }
// Unrelated noise from std::vector's own transitive <string>/<ranges>
// include chain (std::to_string, __max_size_type, etc. -- all genuinely
// conveyor-tagged, genuinely unprovable in isolation, but nothing to do
// with what this file is testing): pruned rather than enumerated, since
// enumerating every line would be both verbose and fragile against any
// future libstdc++ header change.
// { dg-prune-output "/bits/(basic_string|max_size_type|alloc_traits|stdexcept_except)\.h:" }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

#include <vector>

int get_index ();

// The original repro: a local, value-typed container, literal-shaped
// guard. Receiver identity (root cause #1/#3) is the only thing needed
// here, since 'x' is already a bare parameter.
int use_local_container_ok (int x)
{
  std::vector<int> test{1, 2, 3};
  if (x >= 0 && x < test.size ())
    return test[x]; // proven safe, no diagnostic
  return 0;
}

// The index itself is a local variable, assigned from an opaque call's
// return value -- exercises root cause #2 specifically (root cause
// #1/#3 alone is not enough to verify this).
int use_local_idx_from_call_ok ()
{
  std::vector<int> test{1, 2, 3};
  int idx = get_index ();
  if (idx >= 0 && idx < test.size ())
    return test[idx]; // proven safe, no diagnostic
  return 0;
}

// A by-value vector parameter -- like a local variable, a non-reference,
// directly-addressable object requiring '&v' to form 'this'; exercises
// the identical receiver-identity shape as use_local_container_ok, just
// sourced from a parameter instead of a local declaration.
int use_byvalue_param_ok (std::vector<int> v, int x)
{
  if (x >= 0 && x < v.size ())
    return v[x]; // proven safe, no diagnostic
  return 0;
}

// Negative control: an unguarded local-variable container must still
// correctly warn -- confirms none of the three fixes introduces a
// false negative.
int use_local_container_unguarded_bad ()
{
  std::vector<int> test{1, 2, 3};
  int idx = get_index ();
  return test[idx]; // { dg-warning {cannot verify that [^\n]* satisfies the precondition} }
}

int main ()
{
  return use_local_container_ok (0) + use_local_idx_from_call_ok ()
	 + use_byvalue_param_ok (std::vector<int>{1, 2, 3}, 0)
	 + use_local_container_unguarded_bad ();
}
