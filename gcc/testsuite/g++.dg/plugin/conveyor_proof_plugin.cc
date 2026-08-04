/* conveyor-proof: a standalone GCC plugin demonstrating static
   fact-tracing for ordinary, non-conveyor calling code that calls
   functions carrying conveyor contracts -- see
   /home/claude-dude/.claude/plans/stateless-jumping-shore.md for the
   full design and rationale.

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

   A second, sibling check (oa_predicate_conjunct_shape /
   oa_predicate_check_inner_call) connects a postcondition and a
   precondition that both call the *same* named predicate function
   ("check_it (r)" / "check_it (x)"), purely by name and argument
   identity -- entirely independent of oa_env/oa_range_fact, since a
   named, uninterpreted predicate call isn't a numeric fact at all.
   This works even when that predicate function is declared `conveyor`
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

/* Recognize CONJUNCT as "pred_fn (decl)" or its negation
   "!pred_fn (decl)" -- a call to some ordinary FUNCTION_DECL with
   exactly one argument, itself a bare PARM_DECL/VAR_DECL (never a
   general expression -- same "bare decl only" scope limit the
   numeric-comparison matching already has).  Fills PRED_FN_OUT/
   ARG_DECL_OUT/NEGATED_OUT.  Used both for a precondition's own
   conjunct ("check_it (x)", ARG_DECL_OUT = the callee's own parameter
   x) and a postcondition's ("!check_it (r)", ARG_DECL_OUT =
   POSTCONDITION_IDENTIFIER, NEGATED_OUT = true) -- see check_call's own
   use of this below for how the two get connected, including how a
   mismatched polarity between the two is a genuine, provable
   contradiction (see oa_predicate_check_inner_call's own comment).

   This is the whole point of the sibling example this function exists
   for: PRED_FN (e.g. "check_it") can be a conveyor-declared function
   whose own definition is never visible here (declared only, defined in
   some other TU) -- none of this ever needs to evaluate or even see
   PRED_FN's body.  The connection this establishes is purely syntactic
   (the same predicate function, named identically, applied to
   identical-by-construction values across a call boundary), which is
   exactly the trust a conveyor-declared predicate is supposed to
   license: it's assumed well-defined by construction (see the
   `conveyor` function-specifier's own restrictions), so propagating
   "PRED_FN holds (or doesn't) for this value" across the boundary
   doesn't require inspecting what PRED_FN actually computes -- only
   that it's the *same* call, applied to the *same* value.  */

static bool
oa_predicate_conjunct_shape (tree conjunct, tree *pred_fn_out,
			    tree *arg_decl_out, bool *negated_out)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  bool negated = false;
  if (TREE_CODE (c) == TRUTH_NOT_EXPR)
    {
      negated = true;
      c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
      while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
	c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
    }

  if (TREE_CODE (c) != CALL_EXPR || call_expr_nargs (c) != 1)
    return false;

  tree fn = cp_get_callee_fndecl_nofold (c);
  if (!fn || TREE_CODE (fn) != FUNCTION_DECL)
    return false;

  tree arg = STRIP_ANY_LOCATION_WRAPPER (CALL_EXPR_ARG (c, 0));
  if (TREE_CODE (arg) != PARM_DECL && !VAR_P (arg))
    return false;

  *pred_fn_out = fn;
  *arg_decl_out = arg;
  *negated_out = negated;
  return true;
}

/* Is SUBSTITUTED (the caller's actual argument expression for a
   precondition conjunct whose polarity is NEGATED -- "pred_fn (param)"
   if false, "!pred_fn (param)" if true) itself a direct call whose own
   callee's postcondition asserts PRED_FN (at some polarity) for its own
   result -- e.g. "consume (produce ())", where produce's postcondition
   is "post<ctrl> (r: check_it (r))" or "post<ctrl> (r: !check_it (r))",
   and consume's precondition is "pre<ctrl> (check_it (x))"?  Purely
   syntactic (same PRED_FN identity, applied to produce's own
   POSTCONDITION_IDENTIFIER) -- never looks at PRED_FN's own definition,
   matching the whole point of this check (see
   oa_predicate_conjunct_shape's own comment).

   Returns OA_PROVEN_TRUE if a matching postcondition conjunct is found
   with the *same* polarity as NEGATED (the precondition's own
   requirement is discharged); OA_PROVEN_FALSE if one is found with the
   *opposite* polarity (a genuine, provable contradiction: the
   postcondition guarantees PRED_FN's negation of what the precondition
   requires, for the very same value); OA_UNKNOWN if SUBSTITUTED isn't a
   direct call, or no matching conjunct (of either polarity) for this
   PRED_FN is found at all.  */

static oa_proof_result
oa_predicate_check_inner_call (tree substituted, tree pred_fn, bool negated)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (substituted);
  if (TREE_CODE (c) != CALL_EXPR)
    return OA_UNKNOWN;
  tree inner_callee = cp_get_callee_fndecl_nofold (c);
  if (!inner_callee || TREE_CODE (inner_callee) != FUNCTION_DECL)
    return OA_UNKNOWN;

  for (tree as = get_fn_contract_specifiers (inner_callee); as;
       as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      if (!oa_contract_conveyor_active_public (contract, inner_callee))
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree inner_pred_fn, inner_arg_decl;
	  bool inner_negated;
	  if (!oa_predicate_conjunct_shape (*conjuncts[i], &inner_pred_fn,
					   &inner_arg_decl, &inner_negated)
	      || inner_pred_fn != pred_fn
	      || inner_arg_decl != POSTCONDITION_IDENTIFIER (contract))
	    continue;

	  return (inner_negated == negated) ? OA_PROVEN_TRUE : OA_PROVEN_FALSE;
	}
    }
  return OA_UNKNOWN;
}

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
	     to a bare parameter" shape instead (see
	     oa_predicate_conjunct_shape's own comment: this is the sibling
	     check connecting a postcondition and a precondition through a
	     conveyor-declared predicate function whose own definition is
	     never visible here).  */
	  tree pred_fn, arg_decl;
	  bool negated;
	  if (!oa_predicate_conjunct_shape (*conjuncts[i], &pred_fn, &arg_decl,
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
	    = oa_predicate_check_inner_call (substituted, pred_fn, negated);
	  switch (pr)
	    {
	    case OA_PROVEN_TRUE:
	      /* Nothing to report: the obligation is discharged, chained
		 through the inner call's own postcondition -- without ever
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
