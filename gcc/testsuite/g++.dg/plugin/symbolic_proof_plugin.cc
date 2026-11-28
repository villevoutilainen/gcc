/* symbolic-proof: the AST engine's symbolic-flavor proof checks, as a
   plugin.

   An alternative to the built-in AST engine's proof checks, with facts
   of its own: the plugin walks each function's body itself
   (ast_proof_facts.h, shared with the conveyor plugin) at
   PLUGIN_PRE_GENERICIZE, keeping what the code, the function's own
   preconditions (self-trust), contract_asserts and callees'
   postconditions establish, and checks against those facts, conjunct
   by conjunct, each call's callee preconditions, each contract_assert
   and the function's own postconditions.  Diagnostics are the engine's,
   word for word.

   Only the contract-text shape recognizers (oa_match_*,
   is_object_address_call_p, oa_nonzero_conjunct_p, ...), the
   contract-property queries (which flavors a contract is active in,
   strictness, forcing) and the argument substitution helpers are
   shared with the engine.

   Under -fcontract-analysis-proofs=plugin the engines make no proof
   checks: the plugin checks the flavors the proof flags ask for and
   every contract whose control object forces analysis.  Otherwise it
   checks what the built-in engine doesn't (active, not forced).  */

#define INCLUDE_VECTOR
#define INCLUDE_ALGORITHM
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
#include "g++.dg/plugin/ast_proof_facts.h"

int plugin_is_GPL_compatible;

/* Report an unproven obligation at LOC: an error worded PROVE if STRICT,
   else a warning worded VERIFY (the same arguments).  */

static void
unproven (location_t loc, bool strict, const char *prove, const char *verify,
	  ...)
{
  auto_diagnostic_group d;
  va_list ap;
  va_start (ap, verify);
  emit_diagnostic_valist (strict ? diagnostics::kind::error
				 : diagnostics::kind::warning,
			  loc, 0, strict ? prove : verify, &ap);
  va_end (ap);
}

/* Whether this plugin checks CONTRACT (of OWNER) in its flavor.  Under
   -fcontract-analysis-proofs=plugin: when the proof flags ask for the
   flavor, or the control object forces analysis.  Otherwise (the
   built-in engine checks): when the flavor is active and the engine
   doesn't check it already (a forcing control object).  */

static bool
plugin_checks_p (tree contract, tree owner)
{
  if (!oa_contract_symbolic_active_public (contract, owner))
    return false;
  bool forced = oa_contract_symbolic_analysis_forced_public (contract, owner);
  if (flag_contract_analysis_proofs)
    return contract_analysis_plugin_proofs_p (false) || forced;
  return !forced;
}

/* The same for a contract_assert's or postcondition's check, which only
   the -fcontract-X-proofs flag asks for (not the older -gimple one).  */

static bool
plugin_checks_assertion_p (tree contract, tree owner)
{
  if (!oa_contract_symbolic_active_public (contract, owner))
    return false;
  bool forced = oa_contract_symbolic_analysis_forced_public (contract, owner);
  if (flag_contract_analysis_proofs)
    return contract_analysis_plugin_assertion_proofs_p (false) || forced;
  return !forced;
}

/* Shared context the two range-obligation callbacks below need: CALL/
   CALLEE identify the call site check_call is examining; ENV is that
   same call site's own environment, passed straight through.  */

struct range_ctx
{
  tree call;
  tree callee;
  apf::state *env;
};

/* oa_precondition_scalar_range_obligations's own callback: one
   (CONTRACT, PARAM, required [lo,hi]) match for one of CALLEE's own
   bare-scalar symbolic preconditions.  */

static void
scalar_range_callback (tree contract, tree param, bool has_lo, tree lo,
			bool has_hi, tree hi, void *data)
{
  range_ctx *ctx = (range_ctx *) data;
  bool strict
    = oa_contract_symbolic_strict_public (contract, ctx->callee);
  tree substituted = oa_substitute_call_arg_public (ctx->callee, ctx->call, param);
  if (!substituted)
    return;

  oa_proof_result r = apf::check_scalar_range (ctx->env, false, substituted,
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
      unproven (EXPR_LOCATION (ctx->call), strict,
		"cannot prove that %qE satisfies the precondition of %qD",
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
  bool strict
    = oa_contract_symbolic_strict_public (contract, ctx->callee);
  /* oa_precondition_field_range_obligations is shared with the
     conveyor plugin (CALLEE could carry preconditions of either
     flavor) -- only this contract's own symbolic-active matches are
     this plugin's obligation to check.  */
  /* An analyzed_/proven_ precondition is the built-in engine's to check
     (oa_contract_symbolic_analysis_forced_public).  */
  if (!plugin_checks_p (contract, ctx->callee))
    return;
  tree substituted = oa_substitute_call_arg_public (ctx->callee, ctx->call, base_parm);
  if (!substituted)
    return;

  oa_proof_result r = apf::check_field_range (ctx->env, false, substituted,
						      field, has_lo, lo,
						      has_hi, hi);
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
      unproven (EXPR_LOCATION (ctx->call), strict,
		"cannot prove that field %qD of %qE satisfies the "
		"precondition of %qD",
		"cannot verify that field %qD of %qE satisfies the "
		"precondition of %qD",
		field, substituted, ctx->callee);
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
  bool strict
    = oa_contract_symbolic_strict_public (contract, ctx->callee);
  /* An analyzed_/proven_ precondition is the built-in engine's to check
     (oa_contract_symbolic_analysis_forced_public).  */
  if (!plugin_checks_p (contract, ctx->callee))
    return;
  tree substituted = oa_substitute_call_arg_public (ctx->callee, ctx->call, receiver_parm);
  if (!substituted)
    return;

  oa_proof_result r = apf::check_call_range (ctx->env, false, substituted,
						      callee_fn, has_lo, lo,
						      has_hi, hi);
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
      unproven (EXPR_LOCATION (ctx->call), strict,
		"cannot prove that %qD called on %qE satisfies the "
		"precondition of %qD",
		"cannot verify that %qD called on %qE satisfies the "
		"precondition of %qD",
		callee_fn, apf::strip (substituted), ctx->callee);
      inform (DECL_SOURCE_LOCATION (ctx->callee), "declared here");
      break;
    }
}

/* One call site's precondition-obligation check, made by the walk at
   every call in program order -- the
   predicate-identity shape is checked directly here (a single
   conjunct, no combining needed); the two range shapes delegate their
   own shape-matching/conjunct-combining entirely to the exported
   oa_precondition_scalar_range_obligations/oa_precondition_field_range_
   obligations, since that logic is already solved once, shared with
   the built-in checker.  */

/* Whether CONJUNCT, a symbolic precondition conjunct, is one of the
   shapes this plugin (as the built-in engine) has a proof rule for.  */

static bool
conjunct_covered_p (tree conjunct, apf::state *env)
{
  tree a, b, c, d;
  tree_code code;
  bool negated;
  widest_int offset;
  auto_vec<tree> rel_args;
  return (is_object_address_call_p (conjunct, &a)
	  || oa_nonzero_conjunct_p (conjunct, &a)
	  || oa_match_simple_comparison (conjunct, &a, &code, &b)
	  || oa_match_general_comparison_public (conjunct, &a, &code, &b)
	  || oa_match_comparison_against_param (conjunct, &a, &code, &b)
	  || oa_match_comparison_against_call (conjunct, &a, &code, &b, &c,
					       /*allow_symbolic_accessor=*/true)
	  || oa_match_shifted_comparison_against_call_text
	       (conjunct, &a, &code, &b, &c, &offset,
		/*allow_symbolic_accessor=*/true)
	  || oa_match_call_against_call (conjunct, &a, &b, &code, &c, &d,
					 /*allow_symbolic_accessor=*/true)
	  || oa_match_predicate_conjunct (conjunct, &a, &b, &negated)
	  || oa_relation_conjunct_shape (conjunct, &a, &rel_args, &negated)
	  || oa_unsupported_symbolic_predicate_p (conjunct, &a));
}

/* A precondition conjunct with no proof rule: true or false only if
   CALL's arguments make it a constant, else reported as unverifiable,
   as the built-in engine reports it.  */

static void
report_uncovered_conjunct (tree call, tree callee, tree conjunct, bool strict)
{
  location_t loc = EXPR_LOCATION (call);
  if (tree folded = oa_fold_conjunct_at_call_public (callee, call, conjunct))
    {
      if (integer_zerop (folded))
	{
	  error_at (loc, "the arguments provably violate the precondition "
		    "%qE of %qD", conjunct, callee);
	  inform (DECL_SOURCE_LOCATION (callee), "declared here");
	}
      return;
    }
  if (strict)
    error_at (loc, "cannot prove the precondition %qE of %qD", conjunct,
	      callee);
  else
    warning_at (loc, 0, "cannot verify the precondition %qE of %qD",
		conjunct, callee);
  inform (DECL_SOURCE_LOCATION (callee), "declared here");
}

static void
check_call (tree call, tree callee, apf::state *env, void * /*data*/)
{
  for (tree contract : fn_contracts (callee))
    {
      if (!PRECONDITION_P (contract))
	continue;
      if (!plugin_checks_p (contract, callee))
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      bool strict = oa_contract_symbolic_strict_public (contract, callee);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	if (!conjunct_covered_p (*conjuncts[i], env))
	  report_uncovered_conjunct (call, callee, *conjuncts[i], strict);
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
		    && !apf::check_object_address (env, false, substituted,
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
		switch (apf::check_nonzero (env, false, substituted, NULL))
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
	      switch (apf::check_call_relation (env, call, callee, pred_fn,
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
	  /* A symbolic predicate called in a form the analysis can't track
	     a fact for.  */
	  if (!oa_match_predicate_conjunct (*conjuncts[i], &pred_fn, &arg_decl,
					    &negated)
	      && oa_unsupported_symbolic_predicate_p (*conjuncts[i], &pred_fn))
	    {
	      if (strict)
		error_at (EXPR_LOCATION (call), "cannot prove that %qD holds, "
			  "as required by the precondition of %qD", pred_fn,
			  callee);
	      else
		warning_at (EXPR_LOCATION (call), 0, "cannot verify that %qD "
			    "holds, as required by the precondition of %qD",
			    pred_fn, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
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

	  /* As the built-in check: another call's result has its
	     postcondition's facts; a by-value class parameter is a copy,
	     with none of the argument's.  */
	  oa_proof_result pr
	    = apf::check_predicate (env, false, substituted, pred_fn, !negated);
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
	      unproven (EXPR_LOCATION (call), strict,
			"cannot prove that %qD (%qE) holds, as required by "
			"the precondition of %qD",
			"cannot verify that %qD (%qE) holds, as required by "
			"the precondition of %qD",
			pred_fn, substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    }
	}

      /* Not a predicate-identity conjunct -- try "param OP another of
	 CALLEE's own parameters" instead (e.g. 'pre<ctrl>(x < q)'), via
	 oa_match_comparison_against_param and apf::check_relational --
	 the same relational-fact tracking
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
	    = apf::check_relational (env, false, sub_param, rel_code,
					     sub_other);
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
	      unproven (EXPR_LOCATION (call), strict,
			"cannot prove that %qE satisfies the precondition of "
			"%qD",
			"cannot verify that %qE satisfies the precondition "
			"of %qD",
			sub_param ? sub_param : rel_param, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    }
	}

      /* The call analogue of the relational loop just above (e.g.
	 'pre<ctrl>(i < v.size ())'), via oa_match_comparison_against_
	 call/apf::check_call_relational -- same allowed-direction
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
	    = apf::check_call_relational (env, false, sub_param, rel_code,
						  sub_receiver, rhs_callee);
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
	      unproven (EXPR_LOCATION (call), strict,
			"cannot prove that %qE satisfies the precondition of "
			"%qD",
			"cannot verify that %qE satisfies the precondition "
			"of %qD",
			sub_param ? sub_param : rel_param, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    }
	}

      /* The call-vs-call analogue of the relational loop just above
	 (e.g. 'pre<ctrl>(v.size () < w.size ())'), via oa_match_call_
	 against_call/apf::check_call_call_relational -- same
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
	    = apf::check_call_call_relational
		(env, false, sub_lhs_receiver, lhs_callee, call_code,
		 sub_rhs_receiver, rhs_callee);
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
	      unproven (EXPR_LOCATION (call), strict,
			"cannot prove that %qD called on %qE satisfies the "
			"precondition of %qD",
			"cannot verify that %qD called on %qE satisfies the "
			"precondition of %qD",
			lhs_callee,
			apf::strip (sub_lhs_receiver ? sub_lhs_receiver : lhs_receiver),
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
check_symbolic_conjuncts (tree contract, apf::state *env, tree fndecl,
			  bool post)
{
  if (!plugin_checks_assertion_p (contract, fndecl)
      || oa_contract_never_proven_public (contract, fndecl))
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
      switch (apf::check_conjunct (env, false, conjunct))
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
check_assert (tree stmt, apf::state *env, void *data)
{
  check_symbolic_conjuncts (stmt, env, (tree) data, /*post=*/false);
}

static void
check_post (tree contract, apf::state *env, void *data)
{
  check_symbolic_conjuncts (contract, env, (tree) data, /*post=*/true);
}

/* PLUGIN_PRE_GENERICIZE: fires once per non-template function, body
   still in its pre-genericize GENERIC form, which the walk
   (ast_proof_facts.h) reads.  */

static void
symbolic_proof_pre_genericize (void *event_data, void * /*data*/)
{
  tree fndecl = (tree) event_data;
  if (TREE_CODE (fndecl) != FUNCTION_DECL)
    return;
  apf::hooks h;
  h.check_call = [] (tree call, tree callee, apf::state *s)
    { check_call (call, callee, s, NULL); };
  h.check_assert = [] (tree stmt, apf::state *s)
    { check_assert (stmt, s, current_function_decl); };
  h.check_post = [] (tree contract, apf::state *s)
    { check_post (contract, s, current_function_decl); };
  apf::walk_function (fndecl, h);
}

int
plugin_init (struct plugin_name_args *plugin_info,
	     struct plugin_gcc_version * /*version*/)
{
  const char *plugin_name = plugin_info->base_name;

  contract_register_analysis_plugin ();
  register_callback (plugin_name, PLUGIN_PRE_GENERICIZE,
		     symbolic_proof_pre_genericize, NULL);
  return 0;
}
