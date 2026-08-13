/* symbolic-proof: a standalone GCC plugin demonstrating static fact-
   tracing for ordinary calling code that calls functions carrying
   symbolic (axiom) contracts -- the symbolic-side counterpart of
   conveyor_proof_plugin.cc, see /home/claude-dude/.claude/plans/well-
   we-last-discussed-ethereal-duckling.md for the full design and
   rationale.

   Reuses the existing D4324 "oa_*" object-address analysis engine
   (gcc/cp/contracts.cc) via its small, deliberately-designed public API
   (oa_walk_function_calls / oa_match_predicate_conjunct /
   oa_env_check_predicate_fact / oa_env_check_scalar_range_fact /
   oa_env_check_field_range_fact / etc., contracts.h), the same way the
   conveyor plugin does, rather than reimplementing fact tracking from
   scratch.

   Covers the same three shapes -fcontract-symbolic-proofs itself
   verifies (the static prover reached parity with Mechanism A/B's own
   runtime-checked shapes in the commit immediately preceding this
   plugin's addition):

   - Named-predicate identity ("is_opened (this)"-style): a
     postcondition establishes a named predicate for an object's own
     identity; a later precondition on that same object consults it.
   - Bare-scalar range ("post (r: r >= 40 && r < 100)"-style): a
     postcondition establishes a range for its own by-value return
     result; a later precondition on the value it was assigned to
     consults it.
   - ptr->field range ("post (this->count >= 40 && this->count < 100)"-
     style): a postcondition establishes a range for a persistent
     object's own field; a later precondition on that same object's
     field consults it.

   Unlike -fcontract-symbolic-proofs itself, this plugin needs no
   -fcontract-symbolic-proofs flag: oa_walk_function_calls arms the
   shared fact-tracking substrate (m_predicate_fact_map/m_contract_
   scalar_range_map/m_contract_field_range_map) independent of that
   flag whenever a plugin is driving the walk (see
   oa_contract_fact_tracking_active_p's own comment in contracts.cc),
   so the plugin gets full, real, cross-statement-tracked fact tracking
   -- the same engine the built-in checker uses -- purely through
   -fcontract-control-objects.

   A fourth check covers a relational precondition against another of
   the same callee's own parameters ("pre<ctrl>(x < q)"-style, q not a
   literal), via oa_match_comparison_against_param/oa_env_check_
   relational_fact (REQUIRE_CONVEYOR false, the allowed direction: a
   conveyor-established relational fact is trustworthy enough for this
   symbolic obligation too) -- the same relational-fact tracking
   -fcontract-symbolic-proofs itself gained (see .claude/plans/well-we-
   last-discussed-ethereal-duckling.md), closing what used to be a
   silent gap shared with the conveyor plugin.  */

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

/* Shared context the two range-obligation callbacks below need: CALL/
   CALLEE identify the call site check_call is examining; ENV is that
   same call site's own environment, passed straight through.  */

struct range_ctx
{
  tree call;
  tree callee;
  oa_analysis_env *env;
};

/* Positional correspondence between CALLEE's own PARM_DECLs and CALL's
   actual argument expressions -- same convention every other call-site
   check in this file (and its conveyor sibling) uses.  */

static tree
substitute_arg (tree callee, tree call, tree param)
{
  unsigned argno = 0;
  for (tree p = DECL_ARGUMENTS (callee); p; p = DECL_CHAIN (p), ++argno)
    if (p == param)
      return argno < (unsigned) call_expr_nargs (call)
	? CALL_EXPR_ARG (call, argno) : NULL_TREE;
  return NULL_TREE;
}

/* oa_precondition_scalar_range_obligations's own callback: one
   (CONTRACT, PARAM, required [lo,hi]) match for one of CALLEE's own
   bare-scalar symbolic preconditions.  */

static void
scalar_range_callback (tree /*contract*/, tree param, bool has_lo, tree lo,
			bool has_hi, tree hi, void *data)
{
  range_ctx *ctx = (range_ctx *) data;
  tree substituted = substitute_arg (ctx->callee, ctx->call, param);
  if (!substituted)
    return;

  oa_proof_result r = oa_env_check_scalar_range_fact (ctx->env, substituted,
						       has_lo, lo, has_hi, hi);
  switch (r)
    {
    case OA_PROVEN_TRUE:
      /* Nothing to report: the obligation is discharged.  */
      break;
    case OA_PROVEN_FALSE:
      error_at (EXPR_LOCATION (ctx->call),
		"argument %qE provably violates the precondition of %qD",
		substituted, ctx->callee);
      inform (DECL_SOURCE_LOCATION (ctx->callee), "declared here");
      break;
    case OA_UNKNOWN:
      warning_at (EXPR_LOCATION (ctx->call), 0,
		  "cannot verify that %qE satisfies the precondition of %qD",
		  substituted, ctx->callee);
      inform (DECL_SOURCE_LOCATION (ctx->callee), "declared here");
      break;
    }
}

/* oa_precondition_field_range_obligations's own callback: one
   (CONTRACT, FIELD, BASE_PARM, required [lo,hi]) match for one of
   CALLEE's own ptr->field symbolic preconditions.  */

static void
field_range_callback (tree contract, tree field, tree base_parm,
		       bool has_lo, tree lo, bool has_hi, tree hi, void *data)
{
  range_ctx *ctx = (range_ctx *) data;
  /* oa_precondition_field_range_obligations is shared with the
     conveyor plugin (CALLEE could carry preconditions of either
     flavor) -- only this contract's own symbolic-active matches are
     this plugin's obligation to check.  */
  if (!oa_contract_symbolic_active_public (contract, ctx->callee))
    return;
  tree substituted = substitute_arg (ctx->callee, ctx->call, base_parm);
  if (!substituted)
    return;

  oa_proof_result r = oa_env_check_field_range_fact (ctx->env, substituted,
						      field, has_lo, lo,
						      has_hi, hi,
						      /*require_conveyor=*/false);
  switch (r)
    {
    case OA_PROVEN_TRUE:
      /* Nothing to report: the obligation is discharged.  */
      break;
    case OA_PROVEN_FALSE:
      error_at (EXPR_LOCATION (ctx->call),
		"argument %qE provably violates the precondition of %qD: "
		"%qD is established outside the required range",
		substituted, ctx->callee, field);
      inform (DECL_SOURCE_LOCATION (ctx->callee), "declared here");
      break;
    case OA_UNKNOWN:
      warning_at (EXPR_LOCATION (ctx->call), 0,
		  "cannot verify that field %qD of %qE satisfies the "
		  "precondition of %qD", field, substituted, ctx->callee);
      inform (DECL_SOURCE_LOCATION (ctx->callee), "declared here");
      break;
    }
}

/* oa_precondition_call_range_obligations's own callback: the call-range
   analogue of field_range_callback immediately above, for a call to a
   DECL_DECLARED_CONVEYOR_P accessor (e.g. 'n < this->size ()') rather
   than a ptr->field access, named in one of CALLEE's own symbolic
   preconditions.  */

static void
call_range_callback (tree contract, tree callee_fn, tree receiver_parm,
		      bool has_lo, tree lo, bool has_hi, tree hi, void *data)
{
  range_ctx *ctx = (range_ctx *) data;
  if (!oa_contract_symbolic_active_public (contract, ctx->callee))
    return;
  tree substituted = substitute_arg (ctx->callee, ctx->call, receiver_parm);
  if (!substituted)
    return;

  oa_proof_result r = oa_env_check_call_range_fact (ctx->env, substituted,
						      callee_fn, has_lo, lo,
						      has_hi, hi,
						      /*require_conveyor=*/false);
  switch (r)
    {
    case OA_PROVEN_TRUE:
      /* Nothing to report: the obligation is discharged.  */
      break;
    case OA_PROVEN_FALSE:
      error_at (EXPR_LOCATION (ctx->call),
		"argument %qE provably violates the precondition of %qD: "
		"%qD is established outside the required range",
		substituted, ctx->callee, callee_fn);
      inform (DECL_SOURCE_LOCATION (ctx->callee), "declared here");
      break;
    case OA_UNKNOWN:
      warning_at (EXPR_LOCATION (ctx->call), 0,
		  "cannot verify that %qD called on %qE satisfies the "
		  "precondition of %qD", callee_fn, substituted, ctx->callee);
      inform (DECL_SOURCE_LOCATION (ctx->callee), "declared here");
      break;
    }
}

/* One call site's precondition-obligation check, invoked by
   oa_walk_function_calls at every call in program order -- the
   predicate-identity shape is checked directly here (a single
   conjunct, no combining needed); the two range shapes delegate their
   own shape-matching/conjunct-combining entirely to the exported
   oa_precondition_scalar_range_obligations/oa_precondition_field_range_
   obligations, since that logic is already solved once, shared with
   the built-in checker.  */

static void
check_call (tree call, tree callee, oa_analysis_env *env, void * /*data*/)
{
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      if (!oa_contract_symbolic_active_public (contract, callee))
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree pred_fn, arg_decl;
	  bool negated;
	  if (!oa_match_predicate_conjunct (*conjuncts[i], &pred_fn, &arg_decl,
					    &negated))
	    /* Not a predicate-identity conjunct -- e.g. a range shape,
	       which oa_precondition_scalar_range_obligations/oa_
	       precondition_field_range_obligations handle separately
	       below, or is_object_address, already mandatory.  */
	    continue;

	  tree substituted = substitute_arg (callee, call, arg_decl);
	  if (!substituted)
	    continue;

	  oa_proof_result pr
	    = oa_env_check_predicate_fact (env, substituted, pred_fn, !negated,
					    /*require_conveyor=*/false);
	  switch (pr)
	    {
	    case OA_PROVEN_TRUE:
	      /* Nothing to report: the obligation is discharged.  */
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

      /* Not a predicate-identity conjunct -- try "param OP another of
	 CALLEE's own parameters" instead (e.g. 'pre<ctrl>(x < q)'), via
	 the engine's own oa_match_comparison_against_param/oa_env_check_
	 relational_fact -- the same relational-fact tracking
	 -fcontract-symbolic-proofs itself gained (see .claude/plans/
	 well-we-last-discussed-ethereal-duckling.md). REQUIRE_CONVEYOR is
	 false here (the allowed direction: a conveyor-established fact
	 satisfies this symbolic obligation too), unlike the conveyor
	 plugin's own identical-looking check.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rel_other;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_param (*conjuncts[i], &rel_param,
						   &rel_code, &rel_other))
	    continue;

	  tree sub_param = substitute_arg (callee, call, rel_param);
	  tree sub_other = substitute_arg (callee, call, rel_other);
	  oa_proof_result r
	    = oa_env_check_relational_fact (env, sub_param, rel_code,
					     sub_other, /*require_conveyor=*/false);
	  switch (r)
	    {
	    case OA_PROVEN_TRUE:
	      break;
	    case OA_PROVEN_FALSE:
	      error_at (EXPR_LOCATION (call),
			"argument %qE provably violates the precondition "
			"of %qD", sub_param, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    case OA_UNKNOWN:
	      warning_at (EXPR_LOCATION (call), 0,
			  "cannot verify that %qE satisfies the "
			  "precondition of %qD",
			  sub_param ? sub_param : rel_param, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    }
	}

      /* The call analogue of the relational loop just above (e.g.
	 'pre<ctrl>(i < v.size ())'), via oa_match_comparison_against_
	 call/oa_env_check_call_relational_fact -- same allowed-direction
	 discipline (REQUIRE_CONVEYOR false).  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rhs_receiver, rhs_callee;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_call (*conjuncts[i], &rel_param,
						  &rel_code, &rhs_receiver,
						  &rhs_callee,
						  /*allow_symbolic_accessor=*/true))
	    continue;

	  tree sub_param = substitute_arg (callee, call, rel_param);
	  tree sub_receiver = substitute_arg (callee, call, rhs_receiver);
	  oa_proof_result r
	    = oa_env_check_call_relational_fact (env, sub_param, rel_code,
						  sub_receiver, rhs_callee,
						  /*require_conveyor=*/false);
	  switch (r)
	    {
	    case OA_PROVEN_TRUE:
	      break;
	    case OA_PROVEN_FALSE:
	      error_at (EXPR_LOCATION (call),
			"argument %qE provably violates the precondition "
			"of %qD", sub_param, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    case OA_UNKNOWN:
	      warning_at (EXPR_LOCATION (call), 0,
			  "cannot verify that %qE satisfies the "
			  "precondition of %qD",
			  sub_param ? sub_param : rel_param, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    }
	}

      /* The call-vs-call analogue of the relational loop just above
	 (e.g. 'pre<ctrl>(v.size () < w.size ())'), via oa_match_call_
	 against_call/oa_env_check_call_call_relational_fact -- same
	 allowed-direction discipline (REQUIRE_CONVEYOR false).  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree lhs_receiver, lhs_callee, rhs_receiver, rhs_callee;
	  tree_code call_code;
	  if (!oa_match_call_against_call (*conjuncts[i], &lhs_receiver,
					     &lhs_callee, &call_code,
					     &rhs_receiver, &rhs_callee,
					     /*allow_symbolic_accessor=*/true))
	    continue;

	  tree sub_lhs_receiver = substitute_arg (callee, call, lhs_receiver);
	  tree sub_rhs_receiver = substitute_arg (callee, call, rhs_receiver);
	  oa_proof_result r
	    = oa_env_check_call_call_relational_fact
		(env, sub_lhs_receiver, lhs_callee, call_code,
		 sub_rhs_receiver, rhs_callee, /*require_conveyor=*/false);
	  switch (r)
	    {
	    case OA_PROVEN_TRUE:
	      break;
	    case OA_PROVEN_FALSE:
	      error_at (EXPR_LOCATION (call),
			"argument %qE provably violates the precondition "
			"of %qD", sub_lhs_receiver, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    case OA_UNKNOWN:
	      warning_at (EXPR_LOCATION (call), 0,
			  "cannot verify that %qD called on %qE satisfies "
			  "the precondition of %qD", lhs_callee,
			  sub_lhs_receiver ? sub_lhs_receiver : lhs_receiver,
			  callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    }
	}
    }

  range_ctx ctx = { call, callee, env };
  oa_precondition_scalar_range_obligations (callee, scalar_range_callback, &ctx);
  oa_precondition_field_range_obligations (callee, field_range_callback, &ctx);
  oa_precondition_call_range_obligations (callee, call_range_callback, &ctx);
}

/* PLUGIN_PRE_GENERICIZE: fires once per non-template function, body
   still in its pre-genericize GENERIC form -- exactly when
   oa_walk_function_calls needs it (see contracts.h).  */

static void
symbolic_proof_pre_genericize (void *event_data, void * /*data*/)
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
		     symbolic_proof_pre_genericize, NULL);
  return 0;
}
