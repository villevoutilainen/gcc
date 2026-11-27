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
   ever evaluating check_it itself.

   A third check covers the ptr->field shape ("this->count >= 40 &&
   this->count < 100"-style), via oa_precondition_field_range_
   obligations/oa_env_check_field_range_fact -- the same shared
   substrate and exported API symbolic_proof_plugin.cc uses for its own
   field-range obligations (m_contract_field_range_map is genuinely
   shared, not symbolic-exclusive: CALLEE could carry preconditions of
   either flavor, so this plugin filters matches to its own conveyor-
   active ones, exactly as the symbolic plugin filters to its own
   symbolic-active ones).

   A fourth check covers a relational precondition against another of
   the same callee's own parameters ("pre<ctrl>(x < q)"-style, q not a
   literal), via oa_match_comparison_against_param/oa_env_check_
   relational_fact -- the same relational-fact tracking -fcontract-
   conveyor-proofs itself gained (see .claude/plans/well-we-last-
   discussed-ethereal-duckling.md), closing what used to be a silent
   gap: neither the plain-comparison check above (which requires an
   already-literal bound) nor any other check here recognized this
   shape at all.

   A fifth check (see .claude/plans/lazy-stirring-pearl.md, Tier 3b)
   covers a plain contract_assert statement's own condition -- entirely
   invisible to this plugin before, since oa_walk_function_calls only
   ever fired CHECK_CALL at a CALL_EXPR/AGGR_INIT_EXPR, never at a
   contract_assert (ASSERTION_STMT). Uses oa_walk_function_calls's new
   ASSERT_CALLBACK parameter plus the new oa_check_assertion_conjunct_
   public/oa_collect_disjuncts_public exports: unlike every check above,
   a contract_assert's own condition needs no positional-argument
   substitution at all (it already refers directly to the enclosing
   function's own live decls), so oa_check_assertion_conjunct_public
   consults ENV with the conjunct's own operands unchanged. Also the
   first check here to recognize a top-level '||' at all: a disjunctive
   conjunct ('x > 0 || x < -10') is split via oa_collect_disjuncts_
   public and each disjunct checked independently -- PROVEN_TRUE if any
   one disjunct alone is provable, PROVEN_FALSE only if every disjunct
   is independently provable false, else UNKNOWN.  */

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

/* Context oa_precondition_field_range_obligations's own callback below
   needs: CALL/CALLEE identify the call site check_call is examining;
   ENV is that same call site's own environment, passed straight
   through.  */

struct range_ctx
{
  tree call;
  tree callee;
  oa_analysis_env *env;
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
  /* An analyzed_/proven_ precondition is the built-in engine's to check
     (oa_contract_conveyor_analysis_forced_public).  */
  if (!oa_contract_conveyor_active_public (contract, ctx->callee)
      || oa_contract_conveyor_analysis_forced_public (contract, ctx->callee))
    return;
  tree substituted = oa_substitute_call_arg_public (ctx->callee, ctx->call, base_parm);
  if (!substituted)
    return;

  oa_proof_result r = oa_env_check_field_range_fact (ctx->env, substituted,
						      field, has_lo, lo,
						      has_hi, hi,
						      /*require_conveyor=*/true);
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
  /* An analyzed_/proven_ precondition is the built-in engine's to check
     (oa_contract_conveyor_analysis_forced_public).  */
  if (!oa_contract_conveyor_active_public (contract, ctx->callee)
      || oa_contract_conveyor_analysis_forced_public (contract, ctx->callee))
    return;
  tree substituted = oa_substitute_call_arg_public (ctx->callee, ctx->call, base_parm);
  if (!substituted)
    return;

  oa_proof_result r = oa_env_check_float_field_range_fact (ctx->env, substituted,
							     field, has_lo, lo,
							     has_hi, hi,
							     /*require_conveyor=*/true);
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
   than a ptr->field access, named in one of CALLEE's own preconditions.  */

static void
call_range_callback (tree contract, tree callee_fn, tree receiver_parm,
		      bool has_lo, tree lo, bool has_hi, tree hi, void *data)
{
  range_ctx *ctx = (range_ctx *) data;
  /* An analyzed_/proven_ precondition is the built-in engine's to check
     (oa_contract_conveyor_analysis_forced_public).  */
  if (!oa_contract_conveyor_active_public (contract, ctx->callee)
      || oa_contract_conveyor_analysis_forced_public (contract, ctx->callee))
    return;
  tree substituted = oa_substitute_call_arg_public (ctx->callee, ctx->call, receiver_parm);
  if (!substituted)
    return;

  oa_proof_result r = oa_env_check_call_range_fact (ctx->env, substituted,
						      callee_fn, has_lo, lo,
						      has_hi, hi,
						      /*require_conveyor=*/true);
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
   oa_walk_function_calls at every call in program order.  */

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
check_cdtor_receiver (tree call, tree callee, oa_analysis_env *env)
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
  if (oa_env_check_object_address (env, receiver, &reason))
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
   a value, with oa_env_check_nonzero.  Returns false if CONJUNCT isn't
   that shape.  */

static bool
check_nonzero_conjunct (tree conjunct, tree call, tree callee,
			oa_analysis_env *env, bool strict)
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
  switch (oa_env_check_nonzero (env, substituted, &reason))
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
conjunct_covered_p (tree conjunct, oa_analysis_env *env)
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
	  || oa_match_shifted_comparison_against_call_public
	       (conjunct, env, &a, &code, &b, &c, &offset,
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
		   oa_analysis_env *env, bool strict)
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
  switch (oa_env_check_ne_constant (env, substituted, ne_cst, &reason))
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
check_call (tree call, tree callee, oa_analysis_env *env, void * /*data*/)
{
  check_cdtor_receiver (call, callee, env);
  for (tree contract : fn_contracts (callee))
    {
      if (!PRECONDITION_P (contract))
	continue;
      if (!oa_contract_conveyor_active_public (contract, callee))
	continue;
      /* An analyzed_/proven_conveyor precondition is the built-in
	 engine's to check, whatever the command line says.  */
      if (oa_contract_conveyor_analysis_forced_public (contract, callee))
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
	 before (oa_env_check_comparison already consults the substituted
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
	  if (!oa_match_shifted_comparison_against_call_public
		(*conjuncts[i], env, &rel_param, &rel_code, &rhs_receiver,
		 &rhs_callee, &offset, /*allow_symbolic_accessor=*/false)
	      || TREE_CODE (rhs_receiver) != PARM_DECL)
	    continue;
	  tree sub_param
	    = oa_substitute_value_arg_public (callee, call, rel_param);
	  tree sub_receiver
	    = oa_substitute_call_arg_public (callee, call, rhs_receiver);
	  switch (oa_env_check_call_relational_fact
		    (env, sub_param, rel_code, sub_receiver, rhs_callee,
		     /*require_conveyor=*/true, offset))
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

	      /* A float bound has its own query: oa_env_check_comparison
		 tracks integer ranges only.  */
	      oa_proof_result r
		= (TREE_CODE (const_val) == REAL_CST
		   ? oa_env_check_float_comparison (env, substituted, code,
						    const_val)
		   : oa_env_check_comparison (env, substituted, code,
					      const_val));

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
		/* A REAL_CST bound here is the dedicated float-field-range
		   mechanism's own obligation (oa_precondition_float_field_
		   range_obligations/float_field_range_callback above,
		   consulted separately after this loop) -- oa_env_check_
		   comparison's own [lo,hi] machinery is integer-only, so
		   deliberately skip (not warn) here rather than double-
		   diagnose the same conjunct the dedicated path already
		   handles correctly.  */
		if (TREE_CODE (gen_const_val) == REAL_CST)
		  continue;
		/* Likewise a 'ptr->field OP const' conjunct: the field-range
		   mechanism (oa_precondition_field_range_obligations/
		   field_range_callback) combines a field's bounds and
		   diagnoses them once, as the built-in engine does.  */
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
		  = oa_env_check_comparison (env, substituted, gen_code,
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
	     via the engine's own oa_match_comparison_against_param/
	     oa_env_check_relational_fact -- the same relational-fact
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
		= oa_env_check_relational_fact (env, sub_param, rel_code,
						 sub_other, /*require_conveyor=*/true);
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
	      continue;
	    }

	  /* The call analogue of the relational check just above (e.g.
	     'pre<ctrl>(i < v.size ())'), via oa_match_comparison_against_
	     call/oa_env_check_call_relational_fact.  */
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
		  = oa_env_check_call_relational_fact (env, sub_param, rel_code2,
							sub_receiver, rhs_callee,
							/*require_conveyor=*/true);
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
				sub_param ? sub_param : rel_param2, callee);
		    inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    break;
		  }
		continue;
	      }
	  }

	  /* The call-vs-call analogue of the check just above (e.g.
	     'pre<ctrl>(v.size () < w.size ())'), via oa_match_call_against_
	     call/oa_env_check_call_call_relational_fact.  */
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
		  = oa_env_check_call_call_relational_fact
		      (env, sub_lhs_receiver, lhs_callee, call_code,
		       sub_rhs_receiver, rhs_callee, /*require_conveyor=*/true);
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
	    = oa_env_check_predicate_fact (env, substituted, pred_fn, !negated,
					    /*require_conveyor=*/true);
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
	      warning_at (EXPR_LOCATION (call), 0,
			  "cannot verify that %qE satisfies the "
			  "precondition of %qD", range_diag_exprs[p], callee);
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

/* One contract_assert statement's own condition, invoked by
   oa_walk_function_calls's new ASSERT_CALLBACK parameter at every
   ASSERTION_STMT in program order (see this file's own top comment,
   fifth check). Filtered to this plugin's own conveyor-active asserts,
   exactly as check_call filters to conveyor-active preconditions.  */

static void
check_assert (tree stmt, oa_analysis_env *env, void *data)
{
  tree fndecl = (tree) data;
  /* As the built-in engine: a never_proven assertion is trusted as
     written; a bare std::is_object_address conjunct is the mandatory
     check's business; an unprovable conjunct is an error only for a
     strict (proven_conveyor) assertion.  */
  if (!oa_contract_conveyor_active_public (stmt, fndecl)
      || oa_contract_never_proven_public (stmt, fndecl)
      || oa_contract_conveyor_analysis_forced_public (stmt, fndecl))
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
	  verdict = oa_check_assertion_conjunct_public (env, diag_expr,
							 /*require_conveyor=*/true);
	}
      else
	{
	  diag_expr = *conjuncts[i];
	  verdict = OA_PROVEN_FALSE;
	  for (unsigned d = 0; d < disjuncts.length (); ++d)
	    {
	      oa_proof_result r
		= oa_check_assertion_conjunct_public (env, *disjuncts[d],
						       /*require_conveyor=*/true);
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

/* POST_CALLBACK for oa_walk_function_calls: a function's own conveyor
   postcondition, checked against ENV, the environment at the function's
   exit (merged across every return), conjunct by conjunct, the same way
   the built-in engine does it: a never_proven postcondition is trusted
   as written; a bare std::is_object_address conjunct is the mandatory
   check's business; an unprovable conjunct is an error only for a strict
   (proven_conveyor) postcondition.  */

static void
check_post (tree contract, oa_analysis_env *env, void *data)
{
  tree fndecl = (tree) data;
  if (!oa_contract_conveyor_active_public (contract, fndecl)
      || oa_contract_never_proven_public (contract, fndecl)
      || oa_contract_conveyor_analysis_forced_public (contract, fndecl))
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
      switch (oa_check_assertion_conjunct_public (env, conjunct,
						   /*require_conveyor=*/true))
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
   still in its pre-genericize GENERIC form -- exactly when
   oa_walk_function_calls needs it (see contracts.h).  */

static void
conveyor_proof_pre_genericize (void *event_data, void * /*data*/)
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
		     conveyor_proof_pre_genericize, NULL);
  return 0;
}
