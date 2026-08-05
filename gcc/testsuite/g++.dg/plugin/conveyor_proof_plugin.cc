/* conveyor-proof: a standalone GCC plugin demonstrating static
   fact-tracing for ordinary, non-conveyor calling code that calls
   functions carrying conveyor contracts -- see
   /home/claude-dude/.claude/plans/well-we-last-discussed-ethereal-
   duckling.md for the full design and rationale.

   Reuses the existing D4324 "oa_*" object-address analysis engine
   (gcc/cp/contracts.cc) via its small, deliberately-designed public API
   (oa_walk_function_calls / oa_env_check_comparison / etc., contracts.h)
   rather than reimplementing fact tracking from scratch.  The engine
   itself is unmodified in what it does mandatorily; this plugin only
   asks it a question (via a callback invoked at every call site) that
   the compiler's own mandatory pass never asks: whether a callee's
   plain comparison-shaped precondition conjunct ("param > 0", and so
   on -- not just std::is_object_address(param), which the compiler
   already checks unconditionally) is proven, proven false, or unknown,
   given the caller's own established facts (including, where
   applicable, facts chained in from an earlier call's postcondition).

   Contracts remain a runtime-check language feature regardless of this
   plugin; what changes is purely this additional, opt-in (only active
   when the plugin is loaded) static check -- which, like any real
   static checker, does reject the program outright when it *proves* a
   violation (not merely when it fails to prove correctness).

   A second, sibling check connects a postcondition and a precondition
   that both call the *same* named predicate function ("check_it (r)" /
   "check_it (x)"), purely by name and argument identity.  This used to
   be a purely syntactic, single-hop check (recognizing only the case
   where the precondition's own argument was itself a direct call, e.g.
   "consume (produce ())"), reimplemented locally in this file because no
   persistent per-function fact map existed yet for named predicates.
   Named-predicate facts are now tracked by the same real, cross-
   statement engine -fcontract-symbolic-proofs uses for its own
   obligations (m_predicate_fact_map is a shared substrate, not
   symbolic-exclusive -- see oa_contract_fact_tracking_active_p in
   contracts.cc), so this plugin now queries that engine directly via
   the exported oa_match_predicate_conjunct/oa_env_check_predicate_fact,
   the same way the numeric check above already did.  This is a strict
   capability upgrade: an object whose identity persists across
   statements (e.g. "f.open (); f.read ();") is now provable too, not
   just the direct-nested-call shape.

   This works even when the predicate function is declared `conveyor`
   with its definition never visible in this translation unit: the
   whole premise of a conveyor function is that it's trusted to be
   well-defined by construction, so this connection never needs to
   evaluate or inspect what the predicate actually computes.

   Unlike a numeric comparison, an opaque predicate call has no
   "provenly false" verdict from its *value* -- but it does have one
   from its *polarity*: if an earlier call's postcondition establishes
   "!check_it (r)" while a later call's precondition requires
   "check_it (x)" for that same value, that is exactly as provable a
   contradiction as a numeric range check ever finds, still without
   ever evaluating check_it itself.  */

#include "gcc-plugin.h"
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "intl.h"
#include "cp/cp-tree.h"
#include "cp/contracts.h"
#include "diagnostic.h"
#include "stringpool.h"

int plugin_is_GPL_compatible;

/* One call site's precondition-obligation check, invoked by
   oa_walk_function_calls at every call in program order.  */

static void
check_call (tree call, tree callee, oa_analysis_env *env, void * /*data*/)
{
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      if (!oa_contract_conveyor_active_public (contract, callee))
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree param, const_val;
	  tree_code code;
	  if (oa_match_simple_comparison (*conjuncts[i], &param, &code,
					  &const_val))
	    {
	      /* Positional correspondence between CALLEE's own PARM_DECLs
		 and CALL's actual argument expressions -- same bare-
		 parameter-only scope limit the existing engine's own
		 is_object_address matching has.  */
	      tree substituted = NULL_TREE;
	      unsigned argno = 0;
	      for (tree p = DECL_ARGUMENTS (callee); p; p = DECL_CHAIN (p), ++argno)
		if (p == param)
		  {
		    if (argno < (unsigned) call_expr_nargs (call))
		      substituted = CALL_EXPR_ARG (call, argno);
		    break;
		  }
	      if (!substituted)
		continue;

	      oa_proof_result r
		= oa_env_check_comparison (env, substituted, code, const_val);
	      switch (r)
		{
		case OA_PROVEN_TRUE:
		  /* Nothing to report: the obligation is discharged.  */
		  break;
		case OA_PROVEN_FALSE:
		  error_at (EXPR_LOCATION (call),
			    "argument %qE provably violates the precondition "
			    "of %qD", substituted, callee);
		  inform (DECL_SOURCE_LOCATION (callee), "declared here");
		  break;
		case OA_UNKNOWN:
		  warning_at (EXPR_LOCATION (call), 0,
			      "cannot verify that %qE satisfies the "
			      "precondition of %qD", substituted, callee);
		  inform (DECL_SOURCE_LOCATION (callee), "declared here");
		  break;
		}
	      continue;
	    }

	  /* Not a plain comparison -- try the "predicate function applied
	     to a bare parameter" shape instead (see this file's own top
	     comment: connecting a postcondition and a precondition
	     through a conveyor-declared predicate function, now via the
	     real, cross-statement-tracked engine rather than a purely
	     syntactic, single-hop reimplementation).  */
	  tree pred_fn, arg_decl;
	  bool negated;
	  if (!oa_match_predicate_conjunct (*conjuncts[i], &pred_fn, &arg_decl,
					    &negated))
	    /* Recognized by neither check -- e.g. is_object_address, which
	       the compiler already checks mandatorily.  Not this plugin's
	       obligation to discharge.  */
	    continue;

	  tree matched_parm = NULL_TREE;
	  unsigned argno = 0;
	  for (tree p = DECL_ARGUMENTS (callee); p; p = DECL_CHAIN (p), ++argno)
	    if (p == arg_decl)
	      {
		matched_parm = p;
		break;
	      }
	  if (!matched_parm || argno >= (unsigned) call_expr_nargs (call))
	    continue;

	  tree substituted = CALL_EXPR_ARG (call, argno);
	  oa_proof_result pr
	    = oa_env_check_predicate_fact (env, substituted, pred_fn, !negated);
	  switch (pr)
	    {
	    case OA_PROVEN_TRUE:
	      /* Nothing to report: the obligation is discharged, chained
		 through the engine's own established fact -- without ever
		 looking at PRED_FN's own definition.  */
	      break;
	    case OA_PROVEN_FALSE:
	      error_at (EXPR_LOCATION (call),
			"argument %qE provably violates the precondition of "
			"%qD: %qD (%qE) is established %s, but the "
			"precondition requires it to be %s",
			substituted, callee, pred_fn, substituted,
			negated ? "true" : "false", negated ? "false" : "true");
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    case OA_UNKNOWN:
	      warning_at (EXPR_LOCATION (call), 0,
			  "cannot verify that %qD (%qE) holds, as required by "
			  "the precondition of %qD", pred_fn, substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    }
	}
    }
}

/* PLUGIN_PRE_GENERICIZE: fires once per non-template function, body
   still in its pre-genericize GENERIC form -- exactly when
   oa_walk_function_calls needs it (see contracts.h).  */

static void
conveyor_proof_pre_genericize (void *event_data, void * /*data*/)
{
  tree fndecl = (tree) event_data;
  if (TREE_CODE (fndecl) != FUNCTION_DECL)
    return;
  oa_walk_function_calls (fndecl, check_call, NULL);
}

int
plugin_init (struct plugin_name_args *plugin_info,
	     struct plugin_gcc_version * /*version*/)
{
  const char *plugin_name = plugin_info->base_name;

  register_callback (plugin_name, PLUGIN_PRE_GENERICIZE,
		     conveyor_proof_pre_genericize, NULL);
  return 0;
}
