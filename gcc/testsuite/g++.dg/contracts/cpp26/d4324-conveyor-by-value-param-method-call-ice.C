// D4324: regression guard for a genuine ICE, "internal compiler error:
// in cxx_eval_indirect_ref, at cp/constexpr.cc:7734" (line number as of
// the fix), reproducible with plain -fcontracts -fcontract-control-
// objects -- no conveyor/symbolic flag needed at all -- so it is NOT
// specific to this file's own static-analysis engines; it is a general
// constexpr-evaluation robustness bug, only ever reachable in practice
// through a declared precondition on a BY-VALUE class-typed parameter
// (never a reference) that calls a method on that parameter.
//
// Root cause: a precondition's own synthesized predicate-core clone
// function (build_predicate_core_function_1, contracts.cc) copies the
// by-value parameter's PARM_DECL verbatim, and its cached, constexpr-
// evaluable body contains an ordinary 'ADDR_EXPR (parm)' (to form 'this'
// for the method call), built back when that parameter's own declared
// type was still a plain by-value class type. Later, cp_genericize's own
// "fix up the types of parms passed by invisible reference" step (cp-
// gimplify.cc) mutates THAT SAME shared PARM_DECL's TREE_TYPE in place
// to a REFERENCE_TYPE -- an ABI-lowering detail for the clone's own
// real, non-constexpr-evaluated compiled form -- leaving the ADDR_EXPR's
// own operand with a type that no longer matches what the ADDR_EXPR was
// built against. cxx_fold_indirect_ref's own "*&p -> p" fast path then
// wrongly declines (it compares against the operand's now-mutated type),
// falls through to the DR1188 type-punning diagnostic path, which
// instead compares against the ADDR_EXPR's own (still-correct, never
// mutated) type, finds the types similar after all, and trips its own
// internal-consistency gcc_assert.
//
// Confirmed this is reachable even when the precondition's own
// evaluation is never directly requested: any OTHER, unrelated
// expression in the same TU that happens to speculatively, quietly
// constant-evaluate a call to a function with such a precondition (e.g.
// fold_for_warn's own "-Wreturn-local-addr"-style folding of an
// unrelated return statement, performed via cp_fold/maybe_constant_value
// for warning purposes only, regardless of whether the call is ever
// actually used in a real constant expression) is enough to trigger it
// -- exactly what main's own return statement below does.
//
// Fixed in cxx_fold_indirect_ref (constexpr.cc): when the ADDR_EXPR's
// own operand is such an invisible-reference parameter
// (is_invisiref_parm), use the ADDR_EXPR's own type instead of the
// operand's for the "*&p -> p" comparison, exactly like cp_genericize_r's
// own ADDR_EXPR case (cp-gimplify.cc) already does for the real compiled
// form.
//
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

#include <contracts>
#include <vector>
namespace sc = std::contracts;

int at_byvalue (std::vector<int> v, int idx)
  pre<sc::conveyor_assert_v> (idx >= 0 && idx < (int) v.size ())
{
  return v[idx];
}

int main ()
{
  return at_byvalue ({1, 2, 3}, 1) == 2 ? 0 : 1;
}
