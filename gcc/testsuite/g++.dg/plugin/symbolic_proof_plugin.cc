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

/* oa_precondition_scalar_range_obligations's own callback: one
   (CONTRACT, PARAM, required [lo,hi]) match for one of CALLEE's own
   bare-scalar symbolic preconditions.  */

static void
scalar_range_callback (tree /*contract*/, tree param, bool has_lo, tree lo,
			bool has_hi, tree hi, void *data)
{
  range_ctx *ctx = (range_ctx *) data;
  tree substituted = oa_substitute_call_arg_public (ctx->callee, ctx->call, param);
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
  /* An analyzed_/proven_ precondition is the built-in engine's to check
     (oa_contract_symbolic_analysis_forced_public).  */
  if (!oa_contract_symbolic_active_public (contract, ctx->callee)
      || oa_contract_symbolic_analysis_forced_public (contract, ctx->callee))
    return;
  tree substituted = oa_substitute_call_arg_public (ctx->callee, ctx->call, base_parm);
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
  /* An analyzed_/proven_ precondition is the built-in engine's to check
     (oa_contract_symbolic_analysis_forced_public).  */
  if (!oa_contract_symbolic_active_public (contract, ctx->callee)
      || oa_contract_symbolic_analysis_forced_public (contract, ctx->callee))
    return;
  tree substituted = oa_substitute_call_arg_public (ctx->callee, ctx->call, receiver_parm);
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
  for (tree contract : fn_contracts (callee))
    {
      if (!PRECONDITION_P (contract))
	continue;
      if (!oa_contract_symbolic_active_public (contract, callee))
	continue;
      /* An analyzed_/proven_symbolic precondition is the built-in
	 engine's to check, whatever the command line says.  */
      if (oa_contract_symbolic_analysis_forced_public (contract, callee))
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      bool strict = oa_contract_symbolic_strict_public (contract, callee);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree pred_fn, arg_decl;
	  bool negated;
	  /* 'std::is_object_address (q)' and 'n != 0' in a symbolic
	     precondition, as the built-in engine checks them (not the
	     mandatory check's business: it covers conveyor contracts).  */
	  {
	    tree arg;
	    if (is_object_address_call_p (*conjuncts[i], &arg))
	      {
		STRIP_ANY_LOCATION_WRAPPER (arg);
		tree substituted
		  = oa_substitute_call_arg_public (callee, call, arg);
		oa_unprovable_reason reason = OA_UNPROVABLE_NONE;
		if (substituted
		    && !oa_env_check_symbolic_object_address (env, substituted,
							      &reason))
		  {
		    if (strict)
		      error_at (EXPR_LOCATION (call), "cannot prove "
				"%<is_object_address%> for %qE, as required by "
				"the precondition of %qD", substituted, callee);
		    else
		      warning_at (EXPR_LOCATION (call), 0, "cannot verify "
				  "%<is_object_address%> for %qE, as required "
				  "by the precondition of %qD", substituted,
				  callee);
		    if (const char *why = oa_unprovable_reason_text (reason))
		      inform (EXPR_LOCATION (call), "%s", why);
		    inform (DECL_SOURCE_LOCATION (callee), "declared here");
		  }
		continue;
	      }
	    if (oa_nonzero_conjunct_p (*conjuncts[i], &arg))
	      {
		tree substituted
		  = oa_substitute_call_arg_public (callee, call, arg);
		if (!substituted)
		  continue;
		switch (oa_env_check_symbolic_nonzero (env, substituted))
		  {
		  case OA_PROVEN_TRUE:
		    break;
		  case OA_PROVEN_FALSE:
		    error_at (EXPR_LOCATION (call), "argument %qE provably "
			      "violates the precondition of %qD", substituted,
			      callee);
		    inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    break;
		  case OA_UNKNOWN:
		    if (strict)
		      error_at (EXPR_LOCATION (call), "cannot prove that %qE "
				"is nonzero, as required by the precondition "
				"of %qD", substituted, callee);
		    else
		      warning_at (EXPR_LOCATION (call), 0, "cannot verify that "
				  "%qE is nonzero, as required by the "
				  "precondition of %qD", substituted, callee);
		    inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    break;
		  }
		continue;
	      }
	  }
	  /* A symbolic relation of two or more of the callee's parameters
	     ('related (a, b)'), checked as the built-in engine does.  */
	  auto_vec<tree> rel_args;
	  if (oa_relation_conjunct_shape (*conjuncts[i], &pred_fn, &rel_args,
					  &negated))
	    {
	      switch (oa_env_check_call_relation (env, call, callee, pred_fn,
						  rel_args, negated))
		{
		case OA_PROVEN_TRUE:
		  break;
		case OA_PROVEN_FALSE:
		  error_at (EXPR_LOCATION (call),
			    "%qD provably fails the precondition of %qD: the "
			    "relation is established %s, but the precondition "
			    "requires it to be %s", pred_fn, callee,
			    negated ? "true" : "false", negated ? "false" : "true");
		  inform (DECL_SOURCE_LOCATION (callee), "declared here");
		  break;
		case OA_UNKNOWN:
		  if (strict)
		    error_at (EXPR_LOCATION (call), "cannot prove that %qD "
			      "holds, as required by the precondition of %qD",
			      pred_fn, callee);
		  else
		    warning_at (EXPR_LOCATION (call), 0, "cannot verify that "
				"%qD holds, as required by the precondition of "
				"%qD", pred_fn, callee);
		  inform (DECL_SOURCE_LOCATION (callee), "declared here");
		  break;
		}
	      continue;
	    }
	  if (!oa_match_predicate_conjunct (*conjuncts[i], &pred_fn, &arg_decl,
					    &negated))
	    /* Not a predicate-identity conjunct -- e.g. a range shape,
	       which oa_precondition_scalar_range_obligations/oa_
	       precondition_field_range_obligations handle separately
	       below, or is_object_address, already mandatory.  */
	    continue;

	  tree substituted = oa_substitute_call_arg_public (callee, call, arg_decl);
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

	  tree sub_param = oa_substitute_call_arg_public (callee, call, rel_param);
	  tree sub_other = oa_substitute_call_arg_public (callee, call, rel_other);
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

	  tree sub_param = oa_substitute_call_arg_public (callee, call, rel_param);
	  tree sub_receiver = oa_substitute_call_arg_public (callee, call, rhs_receiver);
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

	  tree sub_lhs_receiver = oa_substitute_call_arg_public (callee, call, lhs_receiver);
	  tree sub_rhs_receiver = oa_substitute_call_arg_public (callee, call, rhs_receiver);
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

/* A symbolic contract_assert's or postcondition's conjuncts, against
   ENV, as the built-in -fcontract-symbolic-proofs checks them: a
   never_proven one is trusted as written, a bare std::is_object_address
   conjunct is the mandatory check's business, an unprovable conjunct is
   an error for a strict (proven_symbolic) one.  For a postcondition
   (POST), a conjunct naming a symbolic predicate or relation is an
   axiom: there is no body to check it against.  */

static void
check_symbolic_conjuncts (tree contract, oa_analysis_env *env, tree fndecl,
			  bool post)
{
  if (!oa_contract_symbolic_active_public (contract, fndecl)
      || oa_contract_never_proven_public (contract, fndecl)
      || oa_contract_symbolic_analysis_forced_public (contract, fndecl))
    return;
  tree cond = CONTRACT_CONDITION (contract);
  if (cond == NULL_TREE || cond == error_mark_node)
    return;
  bool strict = oa_contract_symbolic_strict_public (contract, fndecl);
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree conjunct = *conjuncts[i];
      tree oa_arg, pred_fn, arg_decl;
      bool negated;
      if (is_object_address_call_p (conjunct, &oa_arg))
	continue;
      if (post)
	{
	  auto_vec<tree> rel_args;
	  if ((oa_match_predicate_conjunct (conjunct, &pred_fn, &arg_decl,
					    &negated)
	       && DECL_DECLARED_SYMBOLIC_P (pred_fn))
	      || oa_relation_conjunct_shape (conjunct, &pred_fn, &rel_args,
					     &negated))
	    continue;
	}
      switch (oa_check_assertion_conjunct_public (env, conjunct,
						   /*require_conveyor=*/false))
	{
	case OA_PROVEN_TRUE:
	  break;
	case OA_PROVEN_FALSE:
	  if (post)
	    error_at (EXPR_LOCATION (conjunct),
		      "postcondition condition %qE is provably false", conjunct);
	  else
	    error_at (EXPR_LOCATION (conjunct),
		      "%<contract_assert%> condition %qE is provably false",
		      conjunct);
	  break;
	case OA_UNKNOWN:
	  if (strict && post)
	    error_at (EXPR_LOCATION (conjunct),
		      "cannot prove postcondition condition %qE", conjunct);
	  else if (strict)
	    error_at (EXPR_LOCATION (conjunct),
		      "cannot prove %<contract_assert%> condition %qE", conjunct);
	  else if (post)
	    warning_at (EXPR_LOCATION (conjunct), 0,
			"cannot verify postcondition condition %qE", conjunct);
	  else
	    warning_at (EXPR_LOCATION (conjunct), 0,
			"cannot verify %<contract_assert%> condition %qE",
			conjunct);
	  break;
	}
    }
}

static void
check_assert (tree stmt, oa_analysis_env *env, void *data)
{
  check_symbolic_conjuncts (stmt, env, (tree) data, /*post=*/false);
}

static void
check_post (tree contract, oa_analysis_env *env, void *data)
{
  check_symbolic_conjuncts (contract, env, (tree) data, /*post=*/true);
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
  oa_walk_function_calls (fndecl, check_call, NULL, check_assert, fndecl,
			  check_post, fndecl);
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
