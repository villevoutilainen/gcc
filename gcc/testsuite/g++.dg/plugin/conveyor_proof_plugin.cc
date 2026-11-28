/* conveyor-proof: the AST engine's conveyor-flavor proof checks, as a
   plugin.

   An alternative to the built-in AST engine's proof checks, with facts
   of its own: the plugin walks each function's body itself
   (ast_proof_facts.h, shared with the symbolic plugin) at
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
  if (!oa_contract_conveyor_active_public (contract, owner))
    return false;
  bool forced = oa_contract_conveyor_analysis_forced_public (contract, owner);
  if (flag_contract_analysis_proofs)
    return contract_analysis_plugin_proofs_p (true) || forced;
  return !forced;
}

/* The same for a contract_assert's or postcondition's check, which only
   the -fcontract-X-proofs flag asks for (not the older -gimple one).  */

static bool
plugin_checks_assertion_p (tree contract, tree owner)
{
  if (!oa_contract_conveyor_active_public (contract, owner))
    return false;
  bool forced = oa_contract_conveyor_analysis_forced_public (contract, owner);
  if (flag_contract_analysis_proofs)
    return contract_analysis_plugin_assertion_proofs_p (true) || forced;
  return !forced;
}

/* Context oa_precondition_field_range_obligations's own callback below
   needs: CALL/CALLEE identify the call site check_call is examining;
   ENV is that same call site's own environment, passed straight
   through.  */

struct range_ctx
{
  tree call;
  tree callee;
  apf::state *env;
};

/* oa_precondition_field_range_obligations's own callback: one
   (CONTRACT, FIELD, BASE_PARM, required [lo,hi]) match for one of
   CALLEE's own ptr->field preconditions -- CONTRACT could belong to
   either flavor (the underlying map is shared), so only this plugin's
   own conveyor-active matches are its obligation to check.  */

static void
field_range_callback (tree contract, tree field, tree base_parm,
		       bool has_lo, tree lo, bool has_hi, tree hi, void *data)
{
  range_ctx *ctx = (range_ctx *) data;
  bool strict
    = oa_contract_conveyor_strict_public (contract, ctx->callee);
  /* An analyzed_/proven_ precondition is the built-in engine's to check
     (oa_contract_conveyor_analysis_forced_public).  */
  if (!plugin_checks_p (contract, ctx->callee))
    return;
  tree substituted = oa_substitute_call_arg_public (ctx->callee, ctx->call, base_parm);
  if (!substituted)
    return;

  oa_proof_result r = apf::check_field_range (ctx->env, true, substituted,
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

/* The floating-point analogue of field_range_callback immediately
   above, via oa_precondition_float_field_range_obligations/oa_env_
   check_float_field_range_fact -- without this, a float-bounded field
   precondition (e.g. 'pre<ctrl>(this->value >= 0.0)') was invisible to
   this plugin entirely, since field_range_callback's own [lo,hi]
   machinery is integer-only.  */

static void
float_field_range_callback (tree contract, tree field, tree base_parm,
			     bool has_lo, tree lo, bool has_hi, tree hi,
			     void *data)
{
  range_ctx *ctx = (range_ctx *) data;
  bool strict
    = oa_contract_conveyor_strict_public (contract, ctx->callee);
  /* An analyzed_/proven_ precondition is the built-in engine's to check
     (oa_contract_conveyor_analysis_forced_public).  */
  if (!plugin_checks_p (contract, ctx->callee))
    return;
  tree substituted = oa_substitute_call_arg_public (ctx->callee, ctx->call, base_parm);
  if (!substituted)
    return;

  oa_proof_result r = apf::check_field_range (ctx->env, true, substituted,
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
   than a ptr->field access, named in one of CALLEE's own preconditions.  */

static void
call_range_callback (tree contract, tree callee_fn, tree receiver_parm,
		      bool has_lo, tree lo, bool has_hi, tree hi, void *data)
{
  range_ctx *ctx = (range_ctx *) data;
  bool strict
    = oa_contract_conveyor_strict_public (contract, ctx->callee);
  /* An analyzed_/proven_ precondition is the built-in engine's to check
     (oa_contract_conveyor_analysis_forced_public).  */
  if (!plugin_checks_p (contract, ctx->callee))
    return;
  tree substituted = oa_substitute_call_arg_public (ctx->callee, ctx->call, receiver_parm);
  if (!substituted)
    return;

  oa_proof_result r = apf::check_call_range (ctx->env, true, substituted,
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

/* A constructor's or destructor's receiver -- the object passed as its
   'this' -- must be provably an object address, whether or not the
   callee has contracts, as the built-in engine checks it at a plain
   constructor or destructor call ('T::T (&obj)', an explicit destructor
   call, a member or base subobject's constructor): an error where that
   is a rule (conveyor code, a proven_ callee precondition), otherwise a
   warning, not given in a system header.  (A 'new T ()''s receiver, the
   slot of the INIT_EXPR around the AGGR_INIT_EXPR, isn't reached from a
   call site.)  */

static void
check_cdtor_receiver (tree call, tree callee, apf::state *env)
{
  if (TREE_CODE (call) != CALL_EXPR || call_expr_nargs (call) < 1
      || !(DECL_CONSTRUCTOR_P (callee) || DECL_DESTRUCTOR_P (callee)))
    return;
  tree receiver = CALL_EXPR_ARG (call, 0);
  /* A callee whose own precondition already requires
     std::is_object_address (this) -- a conveyor one's implicit one -- has
     it checked by the mandatory check: reporting it again would repeat
     that diagnostic (the built-in engine reports a parameter once).  */
  for (tree contract : fn_contracts (callee))
    {
      if (!PRECONDITION_P (contract))
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;
      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree arg;
	  if (!is_object_address_call_p (*conjuncts[i], &arg))
	    continue;
	  STRIP_NOPS (arg);
	  if (arg == DECL_ARGUMENTS (callee)
	      || (TREE_CODE (arg) == PARM_DECL && is_this_parameter (arg)))
	    return;
	}
    }
  bool strict = oa_cdtor_receiver_strict_public (call);
  if (!strict && in_system_header_at (EXPR_LOCATION (call)))
    return;
  oa_unprovable_reason reason = OA_UNPROVABLE_NONE;
  if (apf::check_object_address (env, true, receiver, &reason))
    return;
  if (strict)
    error_at (EXPR_LOCATION (call), "cannot prove %<is_object_address%> for "
	      "%qE, required by the precondition of %qD", receiver, callee);
  else
    warning_at (EXPR_LOCATION (call), 0, "cannot verify %<is_object_address%> "
		"for %qE, required by the precondition of %qD", receiver,
		callee);
  if (const char *why = oa_unprovable_reason_text (reason))
    inform (EXPR_LOCATION (call), "%s", why);
}

/* A callee's 'x != 0' precondition conjunct (oa_nonzero_conjunct_p, or
   a reference parameter's 'x != 0'): checked for X's argument, read as
   a value, with apf::check_nonzero.  Returns false if CONJUNCT isn't
   that shape.  */

static bool
check_nonzero_conjunct (tree conjunct, tree call, tree callee,
			apf::state *env, bool strict)
{
  tree nz_arg;
  if (!oa_nonzero_conjunct_p (conjunct, &nz_arg))
    {
      tree param, const_val;
      tree_code code;
      if (!oa_match_simple_comparison (conjunct, &param, &code, &const_val)
	  || code != NE_EXPR || !integer_zerop (const_val)
	  || !TYPE_REF_P (TREE_TYPE (param)))
	return false;
      nz_arg = param;
    }
  tree substituted = oa_substitute_value_arg_public (callee, call, nz_arg);
  if (!substituted)
    return false;
  oa_unprovable_reason reason = OA_UNPROVABLE_NONE;
  switch (apf::check_nonzero (env, true, substituted, &reason))
    {
    case OA_PROVEN_TRUE:
      break;
    case OA_PROVEN_FALSE:
      error_at (EXPR_LOCATION (call),
		"argument %qE provably violates the precondition of %qD",
		substituted, callee);
      break;
    case OA_UNKNOWN:
      if (strict)
	error_at (EXPR_LOCATION (call), "cannot prove that %qE is nonzero, "
		  "as required by the precondition of %qD", substituted,
		  callee);
      else
	warning_at (EXPR_LOCATION (call), 0, "cannot verify that %qE is "
		    "nonzero, as required by the precondition of %qD",
		    substituted, callee);
      if (const char *why = oa_unprovable_reason_text (reason))
	inform (EXPR_LOCATION (call), "%s", why);
      break;
    }
  return true;
}

/* A callee's precondition conjunct with no proof rule (none of the
   shapes below, which are the built-in engine's): true or false only if
   CALL's arguments make it a constant, else reported as unverifiable --
   never passed over in silence.  */

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

/* Whether CONJUNCT, a callee's precondition conjunct, is one of the
   shapes this plugin (as the built-in engine) has a proof rule for.  */

static bool
conjunct_covered_p (tree conjunct, apf::state *env)
{
  tree a, b, c, d;
  tree_code code;
  bool negated;
  widest_int offset;
  return (is_object_address_call_p (conjunct, &a)
	  || oa_match_simple_comparison (conjunct, &a, &code, &b)
	  || oa_match_comparison_against_param (conjunct, &a, &code, &b)
	  || (oa_match_comparison_against_call (conjunct, &a, &code, &b, &c,
						/*allow_symbolic_accessor=*/
						  false)
	      && TREE_CODE (b) == PARM_DECL)
	  || oa_match_shifted_comparison_against_call_text
	       (conjunct, &a, &code, &b, &c, &offset,
		/*allow_symbolic_accessor=*/false)
	  || oa_match_call_against_call (conjunct, &a, &b, &code, &c, &d,
					 /*allow_symbolic_accessor=*/false)
	  || oa_nonzero_conjunct_p (conjunct, &a)
	  || oa_match_ne_constant_public (conjunct, &a, &b)
	  || oa_match_predicate_conjunct (conjunct, &a, &b, &negated)
	  || oa_match_general_comparison_public (conjunct, &a, &code, &b));
}

/* A callee's 'E != C' precondition conjunct, C a nonzero constant (or E
   not a parameter): E's argument (or E, substituted) must be provably
   different from C.  Returns false if CONJUNCT isn't that shape.  */

static bool
check_ne_conjunct (tree conjunct, tree call, tree callee,
		   apf::state *env, bool strict)
{
  tree ne_expr, ne_cst;
  if (!oa_match_ne_constant_public (conjunct, &ne_expr, &ne_cst))
    return false;
  tree substituted
    = (TREE_CODE (ne_expr) == PARM_DECL
       ? oa_substitute_value_arg_public (callee, call, ne_expr)
       : oa_substitute_call_expr_public (callee, call, ne_expr));
  if (!substituted)
    {
      report_uncovered_conjunct (call, callee, conjunct, strict);
      return true;
    }
  oa_unprovable_reason reason = OA_UNPROVABLE_NONE;
  switch (apf::check_ne_constant (env, true, substituted, ne_cst, &reason))
    {
    case OA_PROVEN_TRUE:
      break;
    case OA_PROVEN_FALSE:
      error_at (EXPR_LOCATION (call),
		"argument %qE provably violates the precondition of %qD",
		substituted, callee);
      break;
    case OA_UNKNOWN:
      if (strict)
	error_at (EXPR_LOCATION (call), "cannot prove that %qE satisfies the "
		  "precondition of %qD", substituted, callee);
      else
	warning_at (EXPR_LOCATION (call), 0, "cannot verify that %qE "
		    "satisfies the precondition of %qD", substituted, callee);
      if (const char *why = oa_unprovable_reason_text (reason))
	inform (EXPR_LOCATION (call), "%s", why);
      break;
    }
  return true;
}

static void
check_call (tree call, tree callee, apf::state *env, void * /*data*/)
{
  check_cdtor_receiver (call, callee, env);
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

      /* D4324 (see .claude/plans/lazy-stirring-pearl.md, Tier 3b):
	 combine every bare "param OP const" conjunct about the SAME
	 parameter into one overall verdict before diagnosing, mirroring
	 contracts.cc's own oa_handle_precondition_simple_range_
	 obligation (which groups all such conjuncts into a single
	 [lo,hi] interval and checks/diagnoses that once) -- this plugin
	 used to check and diagnose each conjunct independently, so a
	 two-conjunct bound like 'percentage >= 0 && percentage <= 100'
	 could get two separate diagnostics where the built-in engine
	 emits (at most) one. Each direction is still checked exactly as
	 before (apf::check_comparison already consults the substituted
	 expression's own full established range per call, the same data
	 a combined check would use); only the DIAGNOSIS is deferred and
	 merged, not the proof itself. FALSE dominates (a single
	 contradicting conjunct makes the whole precondition false,
	 regardless of what any other conjunct about the same parameter
	 says); otherwise UNKNOWN dominates TRUE.  */
      auto_vec<tree> range_params;
      auto_vec<oa_proof_result> range_verdicts;
      auto_vec<tree> range_diag_exprs;
      bool strict = oa_contract_conveyor_strict_public (contract, callee);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	if (!conjunct_covered_p (*conjuncts[i], env))
	  report_uncovered_conjunct (call, callee, *conjuncts[i], strict);
      /* 'param - receiver.accessor () OP K' and its kin: a call-relational
	 fact with an offset, as the built-in engine checks it.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rhs_receiver, rhs_callee;
	  tree_code rel_code;
	  widest_int offset = 0;
	  if (!oa_match_shifted_comparison_against_call_text
		(*conjuncts[i], &rel_param, &rel_code, &rhs_receiver,
		 &rhs_callee, &offset, /*allow_symbolic_accessor=*/false)
	      || TREE_CODE (rhs_receiver) != PARM_DECL)
	    continue;
	  tree sub_param
	    = oa_substitute_value_arg_public (callee, call, rel_param);
	  tree sub_receiver
	    = oa_substitute_call_arg_public (callee, call, rhs_receiver);
	  switch (apf::check_call_relational
		    (env, true, sub_param, rel_code, sub_receiver, rhs_callee,
		     offset))
	    {
	    case OA_PROVEN_TRUE:
	      break;
	    case OA_PROVEN_FALSE:
	      error_at (EXPR_LOCATION (call), "argument %qE provably violates "
			"the precondition of %qD", sub_param, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    case OA_UNKNOWN:
	      if (strict)
		error_at (EXPR_LOCATION (call), "cannot prove that %qE "
			  "satisfies the precondition of %qD",
			  sub_param ? sub_param : rel_param, callee);
	      else
		warning_at (EXPR_LOCATION (call), 0, "cannot verify that %qE "
			    "satisfies the precondition of %qD",
			    sub_param ? sub_param : rel_param, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    }
	}
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  if (check_nonzero_conjunct (*conjuncts[i], call, callee, env,
				      strict)
	      || check_ne_conjunct (*conjuncts[i], call, callee, env, strict))
	    continue;
	  tree param, const_val;
	  tree_code code;
	  if (oa_match_simple_comparison (*conjuncts[i], &param, &code,
					  &const_val))
	    {
	      tree substituted = oa_substitute_call_arg_public (callee, call, param);
	      if (!substituted)
		continue;

	      oa_proof_result r
		= apf::check_comparison (env, true, substituted, code,
					 const_val);

	      unsigned idx;
	      for (idx = 0; idx < range_params.length (); ++idx)
		if (range_params[idx] == param)
		  break;
	      if (idx == range_params.length ())
		{
		  range_params.safe_push (param);
		  range_verdicts.safe_push (OA_PROVEN_TRUE);
		  range_diag_exprs.safe_push (substituted);
		}

	      if (range_verdicts[idx] == OA_PROVEN_FALSE)
		/* Already provably false from an earlier conjunct about
		   this same parameter -- stays false regardless.  */;
	      else if (r == OA_PROVEN_FALSE)
		{
		  range_verdicts[idx] = OA_PROVEN_FALSE;
		  range_diag_exprs[idx] = substituted;
		}
	      else if (r == OA_UNKNOWN)
		{
		  range_verdicts[idx] = OA_UNKNOWN;
		  range_diag_exprs[idx] = substituted;
		}
	      continue;
	    }

	  /* Not a bare "param OP const" -- try the general, compound-
	     expression form (e.g. 'percentage + this->m_value < 100.0'),
	     via oa_match_general_comparison_public/oa_substitute_call_
	     expr_public. Previously unrecognized by this plugin entirely,
	     with no diagnostic at all -- see .claude/plans/lazy-stirring-
	     pearl.md.  */
	  {
	    tree gen_expr, gen_const_val;
	    tree_code gen_code;
	    if (oa_match_general_comparison_public (*conjuncts[i], &gen_expr,
						     &gen_code, &gen_const_val))
	      {
		/* A 'ptr->field OP const' conjunct (integer or floating
		   bound): the field-range mechanisms (field_range_callback,
		   float_field_range_callback) combine a field's bounds and
		   diagnose them once, as the built-in engine does.  */
		{
		  tree f, pe, cv;
		  tree_code fc;
		  if (oa_match_field_range_comparison (*conjuncts[i], &f, &pe,
						       &fc, &cv))
		    continue;
		}

		tree substituted
		  = oa_substitute_call_expr_public (callee, call, gen_expr);
		if (!substituted)
		  continue;

		oa_proof_result r
		  = apf::check_comparison (env, true, substituted, gen_code,
					     gen_const_val);
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
		    if (strict)
		      error_at (EXPR_LOCATION (call),
				"cannot prove that %qE satisfies the "
				"precondition of %qD", substituted, callee);
		    else
		      warning_at (EXPR_LOCATION (call), 0,
				  "cannot verify that %qE satisfies the "
				  "precondition of %qD", substituted, callee);
		    inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    break;
		  }
		continue;
	      }
	  }

	  /* Not a comparison against a literal -- try "param OP another of
	     CALLEE's own parameters" instead (e.g. 'pre<ctrl>(x < q)'),
	     via oa_match_comparison_against_param/
	     apf::check_relational -- the same relational-fact
	     tracking -fcontract-conveyor-proofs itself gained (see
	     .claude/plans/well-we-last-discussed-ethereal-duckling.md):
	     neither PARAM's own value nor OTHER's is ever resolved except
	     when both substitute to already-literal arguments at this
	     specific call site.  */
	  tree rel_param, rel_other;
	  tree_code rel_code;
	  if (oa_match_comparison_against_param (*conjuncts[i], &rel_param,
						  &rel_code, &rel_other))
	    {
	      tree sub_param = oa_substitute_call_arg_public (callee, call, rel_param);
	      tree sub_other = oa_substitute_call_arg_public (callee, call, rel_other);
	      oa_proof_result r
		= apf::check_relational (env, true, sub_param, rel_code,
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
			    "cannot prove that %qE satisfies the "
			    "precondition of %qD",
			    "cannot verify that %qE satisfies the "
			    "precondition of %qD",
			    sub_param ? sub_param : rel_param, callee);
		  inform (DECL_SOURCE_LOCATION (callee), "declared here");
		  break;
		}
	      continue;
	    }

	  /* The call analogue of the relational check just above (e.g.
	     'pre<ctrl>(i < v.size ())'), via oa_match_comparison_against_
	     call/apf::check_call_relational.  */
	  {
	    tree rel_param2, rhs_receiver, rhs_callee;
	    tree_code rel_code2;
	    if (oa_match_comparison_against_call (*conjuncts[i], &rel_param2,
						   &rel_code2, &rhs_receiver,
						   &rhs_callee,
						   /*allow_symbolic_accessor=*/
						     false))
	      {
		tree sub_param = oa_substitute_call_arg_public (callee, call, rel_param2);
		tree sub_receiver = oa_substitute_call_arg_public (callee, call, rhs_receiver);
		oa_proof_result r
		  = apf::check_call_relational (env, true, sub_param, rel_code2,
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
			      "cannot prove that %qE satisfies the "
			      "precondition of %qD",
			      "cannot verify that %qE satisfies the "
			      "precondition of %qD",
			      sub_param ? sub_param : rel_param2, callee);
		    inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    break;
		  }
		continue;
	      }
	  }

	  /* The call-vs-call analogue of the check just above (e.g.
	     'pre<ctrl>(v.size () < w.size ())'), via oa_match_call_against_
	     call/apf::check_call_call_relational.  */
	  {
	    tree lhs_receiver, lhs_callee, rhs_receiver, rhs_callee;
	    tree_code call_code;
	    if (oa_match_call_against_call (*conjuncts[i], &lhs_receiver,
					      &lhs_callee, &call_code,
					      &rhs_receiver, &rhs_callee,
					      /*allow_symbolic_accessor=*/false))
	      {
		tree sub_lhs_receiver
		  = oa_substitute_call_arg_public (callee, call, lhs_receiver);
		tree sub_rhs_receiver
		  = oa_substitute_call_arg_public (callee, call, rhs_receiver);
		oa_proof_result r
		  = apf::check_call_call_relational
		      (env, true, sub_lhs_receiver, lhs_callee, call_code,
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
			      "cannot prove that %qD called on %qE satisfies "
			      "the precondition of %qD",
			      "cannot verify that %qD called on %qE "
			      "satisfies the precondition of %qD",
			      lhs_callee,
			      apf::strip (sub_lhs_receiver ? sub_lhs_receiver : lhs_receiver),
			      callee);
		    inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    break;
		  }
		continue;
	      }
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

	  tree substituted = oa_substitute_call_arg_public (callee, call, arg_decl);
	  if (!substituted)
	    continue;

	  oa_proof_result pr
	    = apf::check_predicate (env, true, substituted, pred_fn, !negated);
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

      for (unsigned p = 0; p < range_params.length (); ++p)
	{
	  switch (range_verdicts[p])
	    {
	    case OA_PROVEN_TRUE:
	      /* Nothing to report: every conjunct about this parameter
		 was discharged.  */
	      break;
	    case OA_PROVEN_FALSE:
	      error_at (EXPR_LOCATION (call),
			"argument %qE provably violates the precondition "
			"of %qD", range_diag_exprs[p], callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    case OA_UNKNOWN:
	      unproven (EXPR_LOCATION (call), strict,
			"cannot prove that %qE satisfies the precondition of "
			"%qD",
			"cannot verify that %qE satisfies the precondition "
			"of %qD",
			range_diag_exprs[p], callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    }
	}
    }

  range_ctx ctx = { call, callee, env };
  oa_precondition_field_range_obligations (callee, field_range_callback, &ctx);
  oa_precondition_float_field_range_obligations (callee, float_field_range_callback,
						  &ctx);
  oa_precondition_call_range_obligations (callee, call_range_callback, &ctx);
}

/* One contract_assert statement's own condition, checked by the walk at
   every ASSERTION_STMT in program order.  Filtered to this plugin's own
   conveyor-active asserts, exactly as check_call filters to
   conveyor-active preconditions.  */

static void
check_assert (tree stmt, apf::state *env, void *data)
{
  tree fndecl = (tree) data;
  /* As the built-in engine: a never_proven assertion is trusted as
     written; a bare std::is_object_address conjunct is the mandatory
     check's business; an unprovable conjunct is an error only for a
     strict (proven_conveyor) assertion.  */
  if (!plugin_checks_assertion_p (stmt, fndecl)
      || oa_contract_never_proven_public (stmt, fndecl))
    return;
  bool strict = oa_contract_conveyor_strict_public (stmt, fndecl);

  tree cond = CONTRACT_CONDITION (stmt);
  if (cond == NULL_TREE || cond == error_mark_node)
    return;

  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree oa_arg;
      if (is_object_address_call_p (*conjuncts[i], &oa_arg))
	continue;
      auto_vec<tree *> disjuncts;
      oa_collect_disjuncts_public (conjuncts[i], &disjuncts);

      oa_proof_result verdict;
      tree diag_expr;
      if (disjuncts.length () <= 1)
	{
	  /* Not actually disjunctive -- oa_collect_disjuncts_public's own
	     "no top-level '||' is a single disjunct of itself" fallback
	     (mirroring oa_collect_conjuncts_public's identical treatment
	     of a nested '&&'), so this is just an ordinary conjunct.  */
	  diag_expr = *conjuncts[i];
	  verdict = apf::check_conjunct (env, true, diag_expr);
	}
      else
	{
	  diag_expr = *conjuncts[i];
	  verdict = OA_PROVEN_FALSE;
	  for (unsigned d = 0; d < disjuncts.length (); ++d)
	    {
	      oa_proof_result r
		= apf::check_conjunct (env, true, *disjuncts[d]);
	      if (r == OA_PROVEN_TRUE)
		{
		  verdict = OA_PROVEN_TRUE;
		  break;
		}
	      if (r == OA_UNKNOWN)
		verdict = OA_UNKNOWN;
	    }
	}

      switch (verdict)
	{
	case OA_PROVEN_TRUE:
	  /* Nothing to report: the assertion is discharged.  */
	  break;
	case OA_PROVEN_FALSE:
	  error_at (EXPR_LOCATION (diag_expr),
		    "%<contract_assert%> condition %qE is provably false",
		    diag_expr);
	  break;
	case OA_UNKNOWN:
	  if (strict)
	    error_at (EXPR_LOCATION (diag_expr),
		      "cannot prove %<contract_assert%> condition %qE",
		      diag_expr);
	  else
	    warning_at (EXPR_LOCATION (diag_expr), 0,
			"cannot verify %<contract_assert%> condition %qE",
			diag_expr);
	  break;
	}
    }
}

/* A function's own conveyor
   postcondition, checked against ENV, the environment at the function's
   exit (merged across every return), conjunct by conjunct, the same way
   the built-in engine does it: a never_proven postcondition is trusted
   as written; a bare std::is_object_address conjunct is the mandatory
   check's business; an unprovable conjunct is an error only for a strict
   (proven_conveyor) postcondition.  */

static void
check_post (tree contract, apf::state *env, void *data)
{
  tree fndecl = (tree) data;
  if (!plugin_checks_assertion_p (contract, fndecl)
      || oa_contract_never_proven_public (contract, fndecl))
    return;
  tree cond = CONTRACT_CONDITION (contract);
  if (cond == NULL_TREE || cond == error_mark_node)
    return;
  bool strict = oa_contract_conveyor_strict_public (contract, fndecl);

  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree conjunct = *conjuncts[i];
      tree oa_arg;
      if (is_object_address_call_p (conjunct, &oa_arg))
	continue;
      switch (apf::check_conjunct (env, true, conjunct))
	{
	case OA_PROVEN_TRUE:
	  break;
	case OA_PROVEN_FALSE:
	  error_at (EXPR_LOCATION (conjunct),
		    "postcondition condition %qE is provably false", conjunct);
	  break;
	case OA_UNKNOWN:
	  if (strict)
	    error_at (EXPR_LOCATION (conjunct),
		      "cannot prove postcondition condition %qE", conjunct);
	  else
	    warning_at (EXPR_LOCATION (conjunct), 0,
			"cannot verify postcondition condition %qE", conjunct);
	  break;
	}
    }
}

/* PLUGIN_PRE_GENERICIZE: fires once per non-template function, body
   still in its pre-genericize GENERIC form, which the walk
   (ast_proof_facts.h) reads.  */

static void
conveyor_proof_pre_genericize (void *event_data, void * /*data*/)
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
		     conveyor_proof_pre_genericize, NULL);
  return 0;
}
