/* gimple-object-address: the GIMPLE engine's proof checks, as a plugin.

   An alternative to the built-in GIMPLE engine's (contracts-gimple.cc)
   proof checks, with facts and verdicts of its own.  The engine keeps what
   isn't a proof check: the lowering, the eager per-function scheduling,
   the mandatory checks (the conveyor rules, is_object_address in conveyor
   code, a conveyor callee's mandatory preconditions) and the contracts
   whose control object forces analysis (analyzed_X, proven_X).  The
   plugin registers a function checker with cg_register_function_check;
   the engine calls it for every function it checks, with the function in
   SSA form and the symbolic contracts' markers in place.

   It runs under -fcontract-analysis-proofs=plugin, which leaves the
   engines no proof checks, and checks the flavors the proof flags ask
   for (contract_analysis_plugin_proofs_p), and every contract whose
   control object forces analysis.  Diagnostics are the engine's own,
   word for word.

   Only the contract-text shape recognizers (is_object_address_call_p,
   oa_nonzero_conjunct_p, oa_match_*), the contract-property queries
   (which flavors a contract is active in, whether it is strict or
   forced) and the lowering readers (cg_contract_check_call_public) are
   shared with the engine.

   Facts are kept per SSA name, from three sources: self-trust (the
   function's own declared precondition, on ssa_default_def of the
   parameter), item 6 (a callee's declared postcondition about its
   result), and the code itself (PHI merges, copies, conversions, the
   ranger).  Facts about persistent memory (named predicates, field
   ranges) need flow-sensitive state: a fixed-point must-analysis.  */

#include "gcc-plugin.h"
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "basic-block.h"
#include "cp/cp-tree.h"
#include "cp/contracts.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "is-a.h"
#include "tree-dfa.h"
#include "tree-pass.h"
#include "context.h"
#include "diagnostic.h"
#include "stringpool.h"
#include "function.h"
#include "plugin-version.h"
#include "dominance.h"
#include "gimple-range.h"
#include "hash-traits.h"
#include "tree-ssa-alias.h"
#include "tree-cfg.h"
#include "dumpfile.h"
#include "ssa-iterators.h"

int plugin_is_GPL_compatible;

/* A conjunct holds, fails, or neither is known.  */
enum verdict { VERDICT_UNKNOWN, VERDICT_TRUE, VERDICT_FALSE };

/* Whether the function being checked is conveyor code: a conveyor
   function, or the core of a predicate (its condition, outlined).  */

static bool
conveyor_code_p (tree fndecl)
{
  return (DECL_DECLARED_CONVEYOR_P (fndecl)
	  || CONTRACT_HELPER (fndecl) == ldf_contract_predicate_helper);
}

/* Whether CONTRACT, a postcondition of CALLEE, establishes its condition
   where a call to CALLEE returns: it is checked, in either flavor, and its
   failed check stops execution, so execution past the call has it true.
   *CONVEYOR: established in the conveyor flavor, which one-way trust
   requires of a fact a conveyor obligation uses.  */

static bool
postcondition_establishes_p (tree contract, tree callee,
			     bool *conveyor = NULL)
{
  if (!POSTCONDITION_P (contract)
      || !contract_failure_terminates_p (contract, callee))
    return false;
  bool c = oa_contract_conveyor_active_public (contract, callee);
  bool s = oa_contract_symbolic_active_public (contract, callee);
  if (conveyor)
    *conveyor = c;
  return c || s;
}

/* Whether CONTRACT, a precondition of CALLEE, is this plugin's to check
   at a call in the function being checked -- the engine's gating, with
   the proof checks the plugin's.  Not when it is active in no flavor the
   proof flags ask for, unless its control object forces analysis; nor
   when it is a mandatory obligation (a conveyor callee's precondition in
   conveyor code whose failed check doesn't stop execution: the engine's).
   *AS_CONVEYOR: checked in the conveyor flavor, the stricter one, when
   active in both; its is_object_address conjuncts are then mandatory, the
   engine's.  *STRICT: an unproven conjunct is an error.  */

static bool
call_obligation_p (tree contract, tree callee, bool *as_conveyor,
		   bool *strict)
{
  if (!PRECONDITION_P (contract)
      || !contract_precondition_caller_obligation_p (contract, callee))
    return false;
  bool conveyor = oa_contract_conveyor_active_public (contract, callee);
  bool symbolic = oa_contract_symbolic_active_public (contract, callee);
  bool mandatory_ctx = (conveyor && DECL_DECLARED_CONVEYOR_P (callee)
			&& conveyor_code_p (current_function_decl));
  if (mandatory_ctx && !contract_failure_terminates_p (contract, callee))
    return false;
  if (!(conveyor
	&& (contract_analysis_plugin_proofs_p (true)
	    || oa_contract_conveyor_analysis_forced_public (contract, callee)))
      && !(symbolic
	   && (contract_analysis_plugin_proofs_p (false)
	       || oa_contract_symbolic_analysis_forced_public (contract,
							       callee))))
    return false;
  *as_conveyor = conveyor;
  *strict = ((conveyor && oa_contract_conveyor_strict_public (contract, callee))
	     || (symbolic
		 && oa_contract_symbolic_strict_public (contract, callee)));
  return true;
}

/* Report an unproven obligation of CALL's callee: an error if STRICT,
   else a warning.  */

static void ATTRIBUTE_GCC_DIAG (3,4)
report_unproven (gcall *call, bool strict, const char *gmsgid, ...)
{
  auto_diagnostic_group d;
  va_list ap;
  va_start (ap, gmsgid);
  emit_diagnostic_valist (strict ? diagnostics::kind::error
				 : diagnostics::kind::warning,
			  gimple_location (call), 0, gmsgid, &ap);
  va_end (ap);
}

/* While alive, a diagnostic issued about CALL gets a note where its
   callee is declared, as the engine's do.  */

class callee_declared_note
{
  tree m_callee;
  int m_count;
public:
  explicit callee_declared_note (gcall *call)
    : m_callee (gimple_call_fndecl (call)),
      m_count (errorcount + warningcount)
  { }
  ~callee_declared_note ()
  {
    if (m_callee && errorcount + warningcount > m_count)
      inform (DECL_SOURCE_LOCATION (m_callee), "declared here");
  }
};

/* Positional correspondence between CALLEE's own PARM_DECLs and CALL's
   actual argument expressions -- the GIMPLE-level analogue of
   contracts.cc's own oa_substitute_call_arg, just keyed by
   gimple_call_arg instead of CALL_EXPR_ARG.  */

static bool
find_param_position (tree callee, tree parm, unsigned *argno_out)
{
  unsigned argno = 0;
  for (tree p = DECL_ARGUMENTS (callee); p; p = DECL_CHAIN (p), ++argno)
    if (p == parm)
      {
	*argno_out = argno;
	return true;
      }
  return false;
}

/* Item 6's own shape, read declaratively: does CALLEE have a declared
   postcondition whose condition names is_object_address(r) for r ==
   its own POSTCONDITION_IDENTIFIER (its named result)?  If so, ANY
   successful call to CALLEE unconditionally guarantees its return
   value is an object address -- no argument substitution needed at
   all, since a postcondition's guarantee about its own return value
   holds regardless of the caller's own context.  Mirrors contracts.cc's
   own oa_call_postcondition_object_address_p exactly, but -- per this
   whole prototype's own design -- reads CALLEE's *declared* condition
   tree directly rather than anything derived from CALLEE's own,
   possibly-not-yet-processed GIMPLE body, so it works regardless of
   whichever order the pass manager happens to visit functions in (see
   ~/gimple-contract-analysis.md, Section 3's own "ordering" caveat).  */

static bool
call_postcondition_guarantees_object_address_p (tree callee)
{
  for (tree contract : fn_contracts (callee))
    {
      if (!postcondition_establishes_p (contract, callee))
	continue;
      tree result_id = POSTCONDITION_IDENTIFIER (contract);
      if (!result_id)
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
	  STRIP_ANY_LOCATION_WRAPPER (arg);
	  if (arg == result_id)
	    return true;
	}
    }
  return false;
}

/* Same idea, for nonzero-ness -- mirrors contracts.cc's own
   oa_call_postcondition_nonzero_p exactly, again reading CALLEE's
   *declared* postcondition text only.  */

static bool
call_postcondition_guarantees_nonzero_p (tree callee)
{
  for (tree contract : fn_contracts (callee))
    {
      if (!postcondition_establishes_p (contract, callee))
	continue;
      tree result_id = POSTCONDITION_IDENTIFIER (contract);
      if (!result_id)
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree decl;
	  if (oa_nonzero_conjunct_p (*conjuncts[i], &decl) && decl == result_id)
	    return true;
	}
    }
  return false;
}

/* A third fact shape, general numeric ranges (conveyor's own item-8
   numeric checking, e.g. 'pre<ctrl>(x >= 20 && x < 100)') -- unlike
   is_object_address/nonzero, this one is NOT resolved by a hand-rolled
   recursive SSA/PHI walk: GCC's own on-demand ranger (class
   gimple_ranger, gimple-range.h) already computes exactly this kind of
   "what values can this SSA name take here" query, handling PHI merges,
   copy propagation, and comparison-derived refinement far more
   completely than a bespoke walker could -- see ~/gimple-contract-
   analysis.md, Section 5. This still needs its own established-facts
   map for self-trust/item 6, though: the ranger only ever derives
   facts from *real code* in the function it's asked about, and has no
   way to know about a *declared* precondition/postcondition's own
   axiomatic range for a parameter/result identifier it never actually
   constrains via a visible comparison or assignment.  */

struct oa_range_lite
{
  bool has_lo = false, has_hi = false;
  widest_int lo = 0, hi = 0;
};

/* Combine CODE/VAL (one comparison conjunct, e.g. 'x >= 20') into R --
   mirrors contracts.cc's own oa_tighten_range_bound.  */

static void
tighten_range_bound (oa_range_lite &r, tree_code code, const widest_int &val)
{
  switch (code)
    {
    case GT_EXPR:
      if (!r.has_lo || val + 1 > r.lo) { r.has_lo = true; r.lo = val + 1; }
      break;
    case GE_EXPR:
      if (!r.has_lo || val > r.lo) { r.has_lo = true; r.lo = val; }
      break;
    case LT_EXPR:
      if (!r.has_hi || val - 1 < r.hi) { r.has_hi = true; r.hi = val - 1; }
      break;
    case LE_EXPR:
      if (!r.has_hi || val < r.hi) { r.has_hi = true; r.hi = val; }
      break;
    case EQ_EXPR:
      r.has_lo = true; r.lo = val;
      r.has_hi = true; r.hi = val;
      break;
    default:
      break;
    }
}

/* Accumulate every 'TARGET OP const'-shaped conjunct of CONJUNCTS
   (oa_match_simple_comparison, already exported) naming TARGET into a
   single combined range in *OUT -- e.g. 'x >= 20 && x < 100' becomes
   [20, 100). Returns false (leaving *OUT untouched) if no conjunct
   named TARGET at all.  */

static bool
extract_conjunct_range (vec<tree *> &conjuncts, tree target, oa_range_lite *out)
{
  oa_range_lite acc;
  bool any = false;
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree param, const_val;
      tree_code code;
      if (oa_match_simple_comparison (*conjuncts[i], &param, &code, &const_val)
	  && param == target && TREE_CODE (const_val) == INTEGER_CST)
	{
	  tighten_range_bound (acc, code, wi::to_widest (const_val));
	  any = true;
	}
    }
  if (any)
    *out = acc;
  return any;
}

/* Item 6 for ranges: does CALLEE's own declared postcondition
   unconditionally guarantee a range for its own return value?  */

static bool
call_postcondition_range_p (tree callee, oa_range_lite *out)
{
  for (tree contract : fn_contracts (callee))
    {
      if (!postcondition_establishes_p (contract, callee))
	continue;
      tree result_id = POSTCONDITION_IDENTIFIER (contract);
      if (!result_id)
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      if (extract_conjunct_range (conjuncts, result_id, out))
	return true;
    }
  return false;
}

/* Which bounds of the ranger's range of integer VAL to trust.  The
   ranger takes signed overflow, being undefined, not to happen, and so
   clamps the range of an operation that can overflow at the type's bound
   the overflow would cross: that bound is only as good as the assumption
   being proven.  A bound of an arithmetic result is trusted when interval
   arithmetic on its operands' trusted bounds keeps it inside the type; an
   operand's untrusted or unknown bound is unbounded.  A copy or a
   conversion keeps its operand's trust; any other operation on an operand
   with an untrusted bound is untrusted.  A cycle through loop PHIs is
   assumed trusted, each operation in it still checked.  */

struct bounds_trust { bool lo, hi; };

static bounds_trust
trusted_bounds (tree val, gimple_ranger *ranger, hash_set<tree> &visiting,
		unsigned depth = 0)
{
  const bounds_trust all = { true, true }, none = { false, false };
  if (TREE_CODE (val) != SSA_NAME || !ranger)
    return all;
  if (depth > 12)
    return none;
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (!def || gimple_nop_p (def) || is_gimple_call (def))
    return all;
  if (visiting.add (val))
    return all;

  bounds_trust t = all;
  if (gphi *phi = dyn_cast <gphi *> (def))
    for (unsigned i = 0; i < gimple_phi_num_args (phi); ++i)
      {
	bounds_trust a = trusted_bounds (gimple_phi_arg_def (phi, i), ranger,
					 visiting, depth + 1);
	t.lo &= a.lo;
	t.hi &= a.hi;
      }
  else if (is_gimple_assign (def))
    {
      tree_code code = gimple_assign_rhs_code (def);
      tree type = TREE_TYPE (val);
      bool undefined_overflow = (INTEGRAL_TYPE_P (type)
				 && TYPE_OVERFLOW_UNDEFINED (type));
      bool arith = (undefined_overflow
		    && (code == PLUS_EXPR || code == MINUS_EXPR
			|| code == MULT_EXPR || code == NEGATE_EXPR
			|| code == ABS_EXPR || code == LSHIFT_EXPR));
      if (!arith)
	{
	  bool copy = code == SSA_NAME || CONVERT_EXPR_CODE_P (code);
	  for (unsigned i = 1; i < gimple_num_ops (def); ++i)
	    {
	      bounds_trust a = trusted_bounds (gimple_op (def, i), ranger,
					       visiting, depth + 1);
	      if (copy)
		{
		  t.lo &= a.lo;
		  t.hi &= a.hi;
		}
	      else if (!a.lo || !a.hi)
		t = none;
	    }
	  /* The minimum divided by -1 overflows.  */
	  if (undefined_overflow
	      && (code == TRUNC_DIV_EXPR || code == TRUNC_MOD_EXPR))
	    {
	      tree op0 = gimple_assign_rhs1 (def);
	      tree op1 = gimple_assign_rhs2 (def);
	      if (TREE_CODE (op1) != INTEGER_CST || integer_minus_onep (op1))
		{
		  int_range_max r;
		  if (TREE_CODE (op0) != SSA_NAME
		      || !ranger->range_of_expr (r, op0, def)
		      || r.undefined_p () || r.varying_p ()
		      || (wi::to_widest (TYPE_MIN_VALUE (type))
			  >= widest_int::from (r.lower_bound (), SIGNED)))
		    t = none;
		}
	    }
	}
      else
	{
	  /* OP's bounds at DEF: unbounded where unknown or untrusted.  */
	  struct bnd { bool has_lo, has_hi; widest_int lo, hi; };
	  auto bounds_of = [&] (tree op) -> bnd
	    {
	      bnd b = { false, false, 0, 0 };
	      if (TREE_CODE (op) == INTEGER_CST)
		{
		  b.has_lo = b.has_hi = true;
		  b.lo = b.hi = wi::to_widest (op);
		  return b;
		}
	      int_range_max r;
	      if (TREE_CODE (op) != SSA_NAME
		  || !INTEGRAL_TYPE_P (TREE_TYPE (op))
		  || !ranger->range_of_expr (r, op, def)
		  || r.undefined_p () || r.varying_p ())
		return b;
	      bounds_trust a = trusted_bounds (op, ranger, visiting, depth + 1);
	      signop sgn = TYPE_SIGN (TREE_TYPE (op));
	      b.has_lo = a.lo;
	      b.has_hi = a.hi;
	      b.lo = widest_int::from (r.lower_bound (), sgn);
	      b.hi = widest_int::from (r.upper_bound (), sgn);
	      return b;
	    };
	  bool unary = code == NEGATE_EXPR || code == ABS_EXPR;
	  bnd a = bounds_of (gimple_assign_rhs1 (def));
	  bnd b = unary ? bnd { true, true, 0, 0 }
			: bounds_of (gimple_assign_rhs2 (def));
	  bool has_lo = false, has_hi = false;
	  widest_int lo = 0, hi = 0;
	  switch (code)
	    {
	    case PLUS_EXPR:
	      has_lo = a.has_lo && b.has_lo, lo = a.lo + b.lo;
	      has_hi = a.has_hi && b.has_hi, hi = a.hi + b.hi;
	      break;
	    case MINUS_EXPR:
	      has_lo = a.has_lo && b.has_hi, lo = a.lo - b.hi;
	      has_hi = a.has_hi && b.has_lo, hi = a.hi - b.lo;
	      break;
	    case NEGATE_EXPR:
	      has_lo = a.has_hi, lo = -a.hi;
	      has_hi = a.has_lo, hi = -a.lo;
	      break;
	    case ABS_EXPR:
	      has_lo = a.has_lo, lo = 0;
	      has_hi = a.has_lo && a.has_hi, hi = wi::smax (-a.lo, a.hi);
	      break;
	    case MULT_EXPR:
	      if (a.has_lo && a.has_hi && b.has_lo && b.has_hi)
		{
		  widest_int c[4] = { a.lo * b.lo, a.lo * b.hi, a.hi * b.lo,
				      a.hi * b.hi };
		  lo = hi = c[0];
		  for (auto &v : c)
		    lo = wi::smin (lo, v), hi = wi::smax (hi, v);
		  has_lo = has_hi = true;
		}
	      else if (a.has_lo && b.has_lo && !wi::neg_p (a.lo)
		       && !wi::neg_p (b.lo))
		has_lo = true, lo = a.lo * b.lo;
	      else if (a.has_hi && b.has_hi && !wi::neg_p (-a.hi)
		       && !wi::neg_p (-b.hi))
		has_lo = true, lo = a.hi * b.hi;
	      else if (a.has_lo && b.has_hi && !wi::neg_p (a.lo)
		       && !wi::neg_p (-b.hi))
		has_hi = true, hi = a.lo * b.hi;
	      else if (a.has_hi && b.has_lo && !wi::neg_p (-a.hi)
		       && !wi::neg_p (b.lo))
		has_hi = true, hi = a.hi * b.lo;
	      break;
	    case LSHIFT_EXPR:
	      if (b.has_lo && b.has_hi && !wi::neg_p (b.lo)
		  && b.hi < TYPE_PRECISION (type) && a.has_lo && a.has_hi)
		{
		  widest_int f = wi::lshift (widest_int (1), b.hi.to_uhwi ());
		  lo = wi::smin (wi::smin (a.lo * f, a.hi * f), a.lo);
		  hi = wi::smax (wi::smax (a.lo * f, a.hi * f), a.hi);
		  has_lo = has_hi = true;
		}
	      break;
	    default:
	      break;
	    }
	  widest_int tmin = wi::to_widest (TYPE_MIN_VALUE (type));
	  widest_int tmax = wi::to_widest (TYPE_MAX_VALUE (type));
	  t.lo = has_lo && lo >= tmin && lo <= tmax;
	  t.hi = has_hi && hi >= tmin && hi <= tmax;
	}
    }
  visiting.remove (val);
  return t;
}

/* VAL's range at AT as the ranger computes it from the code, including
   the conditions guarding AT, in VR; false unless both its bounds are to
   be trusted (trusted_bounds).  */

static bool
trusted_range_at (tree val, gimple *at, gimple_ranger *ranger,
		  int_range_max &vr)
{
  if (!ranger || (TREE_CODE (val) != SSA_NAME
		  && TREE_CODE (val) != INTEGER_CST)
      || !INTEGRAL_TYPE_P (TREE_TYPE (val))
      || !ranger->range_of_expr (vr, val, at)
      || vr.undefined_p () || vr.varying_p ())
    return false;
  hash_set<tree> visiting;
  bounds_trust t = trusted_bounds (val, ranger, visiting);
  return t.lo && t.hi;
}

/* Resolve VAL's own range, trying, in order: a literal constant; a
   self-trusted/item-6 fact in ESTABLISHED_RANGE (keyed exactly like
   ESTABLISHED/ESTABLISHED_NZ elsewhere in this file); item 6 via a
   GIMPLE_CALL def-stmt; and finally RANGER's own general-dataflow
   answer (the "conveyor's own general m_range_map fallback" idea this
   session's earlier AST-walk work already used for symbolic's own
   scalar-range checking, here just backed by a real ranger instead of
   a hand-rolled map).  A multi-sub-range irange's own outer envelope
   (lowest lower_bound, highest upper_bound) is a sound, if slightly
   coarser, stand-in for the full value set when checking "is
   ESTABLISHED entirely inside REQUIRED" -- gaps inside the envelope
   only ever make the real value set a stricter subset.  */

static bool
established_range_of (tree val, hash_map<tree, oa_range_lite> &established_range,
		       gimple_ranger *ranger, gimple *at, oa_range_lite *out)
{
  if (val == NULL_TREE)
    return false;

  if (TREE_CODE (val) == INTEGER_CST)
    {
      widest_int v = wi::to_widest (val);
      out->has_lo = out->has_hi = true;
      out->lo = out->hi = v;
      return true;
    }

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  if (oa_range_lite *found = established_range.get (val))
    {
      *out = *found;
      /* Narrowed by what the code says at AT ('n != 10').  */
      int_range_max vr;
      if (found->has_lo && found->has_hi
	  && trusted_range_at (val, at, ranger, vr))
	{
	  tree type = TREE_TYPE (val);
	  signop sgn = TYPE_SIGN (type);
	  if (wi::fits_to_tree_p (found->lo, type)
	      && wi::fits_to_tree_p (found->hi, type))
	    {
	      int_range_max known (type,
				   wide_int::from (found->lo,
						   TYPE_PRECISION (type), sgn),
				   wide_int::from (found->hi,
						   TYPE_PRECISION (type), sgn));
	      known.intersect (vr);
	      if (!known.undefined_p ())
		{
		  out->lo = widest_int::from (known.lower_bound (), sgn);
		  out->hi = widest_int::from (known.upper_bound (), sgn);
		}
	    }
	}
      return true;
    }

  gimple *def = SSA_NAME_DEF_STMT (val);
  if (def && is_gimple_call (def))
    {
      tree callee = gimple_call_fndecl (as_a <gcall *> (def));
      if (callee && call_postcondition_range_p (callee, out))
	return true;
    }
  else if (def && is_gimple_assign (def))
    {
      /* A plain copy or value-preserving conversion -- e.g. 'int y =
	 make_val();' gimplifies to a temporary holding the call's own
	 result, then an ordinary copy into y ('_4 = make_val (); y_5 =
	 _4;'), so item 6's own guarantee on _4 must be checked one hop
	 back from y's own def-stmt, not just at y's def-stmt itself
	 (which the ranger's own ordinary dataflow reasoning wouldn't
	 catch either, since an uninterpreted call gives it nothing to
	 propagate).  */
      enum tree_code code = gimple_assign_rhs_code (def);
      if ((CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	  && established_range_of (gimple_assign_rhs1 (def), established_range,
				    ranger, at, out))
	return true;
      /* An operand's range plus or minus a constant, where every result
	 stays in the type (no overflow, no wrapping).  */
      oa_range_lite r;
      if ((code == PLUS_EXPR || code == MINUS_EXPR)
	  && TREE_CODE (gimple_assign_rhs2 (def)) == INTEGER_CST
	  && INTEGRAL_TYPE_P (TREE_TYPE (val))
	  && established_range_of (gimple_assign_rhs1 (def), established_range,
				   ranger, at, &r)
	  && r.has_lo && r.has_hi)
	{
	  widest_int k = wi::to_widest (gimple_assign_rhs2 (def));
	  if (code == MINUS_EXPR)
	    k = -k;
	  widest_int lo = r.lo + k, hi = r.hi + k;
	  tree type = TREE_TYPE (val);
	  if (lo >= wi::to_widest (TYPE_MIN_VALUE (type))
	      && hi <= wi::to_widest (TYPE_MAX_VALUE (type)))
	    {
	      out->has_lo = out->has_hi = true;
	      out->lo = lo;
	      out->hi = hi;
	      return true;
	    }
	}
    }

  int_range_max vr;
  if (trusted_range_at (val, at, ranger, vr))
    {
      signop sgn = TYPE_SIGN (TREE_TYPE (val));
      out->has_lo = out->has_hi = true;
      out->lo = widest_int::from (vr.lower_bound (), sgn);
      out->hi = widest_int::from (vr.upper_bound (), sgn);
      return true;
    }

  return false;
}

/* Facts about persistent memory: named predicates of an object
   ('is_opened (this)'), ranges of its fields ('this->count in [0, N)')
   and relations among objects and values.  Unlike a value's facts, a call
   passing the object's address on can change them without changing any
   SSA value, so they need forward, flow-sensitive reasoning: established
   here, ended there, and holding at a point only if they hold on every
   path to it (persistent_facts_walk).  An object's identity is the
   variable whose address it is, or the pointer value
   (gimple_object_identity).  */

/* A predicate fact about one object: PRED_FN holds of it (POLARITY) or
   not; CONVEYOR if established in the conveyor flavor.  */

struct pred_fact { tree pred_fn; bool polarity; bool conveyor; };

/* The predicate facts about one object, one per predicate function: 'ok
   (p)' and 'p.get ()' both hold after 'if (ok (p) && p.get ())'.  One
   beyond the capacity drops the oldest, which only loses precision.  */

#define PRED_FACTS_MAX 8

struct pred_facts
{
  pred_fact f[PRED_FACTS_MAX];
  unsigned char n = 0;
  pred_fact *find (tree pred_fn)
  {
    for (unsigned i = 0; i < n; ++i)
      if (f[i].pred_fn == pred_fn)
	return &f[i];
    return NULL;
  }
  void put (const pred_fact &fact)
  {
    if (pred_fact *p = find (fact.pred_fn))
      {
	*p = fact;
	return;
      }
    if (n == PRED_FACTS_MAX)
      {
	for (unsigned i = 1; i < n; ++i)
	  f[i - 1] = f[i];
	--n;
      }
    f[n++] = fact;
  }
};
typedef pair_hash<nofree_ptr_hash<tree_node>, nofree_ptr_hash<tree_node>>
  oa_field_key_hash;

/* A relation (oa_relation_conjunct_shape): PRED_FN of the identities
   IDS, in order -- an object's (gimple_object_identity), or a value's: a
   constant, the SSA value a scalar is a copy of, the variable holding an
   aggregate, or a member of one (oa_relation_member_slot).  */

struct relation_fact_lite
{
  tree pred_fn;
  tree ids[OA_RELATION_MAX_ARGS];
  unsigned n;
  bool polarity;
  bool conveyor;
};

struct rel_fact_lite { tree_code code; tree rhs; };
static bool provable_nonzero_p (tree, hash_set<tree> &, hash_set<tree> &);
static bool get_relational (tree, hash_map<tree, rel_fact_lite> &,
			    tree_code *, tree *);

/* The value facts of the function being checked, for the checks made
   during the persistent-fact walk (check_assertion).  */

struct value_facts
{
  hash_set<tree> *established, *established_nz;
  hash_map<tree, oa_range_lite> *established_range;
  hash_map<tree, rel_fact_lite> *established_rel;
  gimple_ranger *ranger;
};
static value_facts *current_value_facts;

/* The values of TYPE satisfying 'V CODE CST', a fact (NaN excluded: it
   satisfies no comparison but '!='), over-approximated by closed bounds;
   false if CODE isn't one of '<', '<=', '>', '>=', '=='.  */

static bool
frange_for_comparison (tree type, tree_code code, tree cst, frange *out)
{
  REAL_VALUE_TYPE inf, ninf;
  real_inf (&inf);
  real_inf (&ninf, true);
  const REAL_VALUE_TYPE &k = *TREE_REAL_CST_PTR (cst);
  switch (code)
    {
    case LT_EXPR: case LE_EXPR: *out = frange (type, ninf, k); break;
    case GT_EXPR: case GE_EXPR: *out = frange (type, k, inf); break;
    case EQ_EXPR: *out = frange (type, k, k); break;
    default: return false;
    }
  out->clear_nan ();
  return true;
}

/* Whether every value in FR satisfies 'v CODE CST' (VERDICT_TRUE), none
   does (VERDICT_FALSE), or neither is known: a NaN makes the comparison
   false, so all only if none can be one.  */

static verdict
frange_verdict (const frange &fr, tree_code code, tree cst)
{
  if (fr.undefined_p () || fr.varying_p ())
    return VERDICT_UNKNOWN;
  if (fr.known_isnan ())
    return code == NE_EXPR ? VERDICT_TRUE : VERDICT_FALSE;
  const REAL_VALUE_TYPE &lb = fr.lower_bound (), &ub = fr.upper_bound ();
  const REAL_VALUE_TYPE *k = TREE_REAL_CST_PTR (cst);
  bool all = false, none = false;
  switch (code)
    {
    case LT_EXPR:
      all = real_less (&ub, k);
      none = !real_less (&lb, k);
      break;
    case LE_EXPR:
      all = !real_less (k, &ub);
      none = real_less (k, &lb);
      break;
    case GT_EXPR:
      all = real_less (k, &lb);
      none = !real_less (k, &ub);
      break;
    case GE_EXPR:
      all = !real_less (&lb, k);
      none = real_less (&ub, k);
      break;
    case EQ_EXPR:
      all = real_equal (&lb, k) && real_equal (&ub, k);
      none = real_less (&ub, k) || real_less (k, &lb);
      break;
    default:
      return VERDICT_UNKNOWN;
    }
  if (all && !fr.maybe_isnan ())
    return VERDICT_TRUE;
  return none ? VERDICT_FALSE : VERDICT_UNKNOWN;
}

/* Whether HAVE lies within NEED (no NaN possible, NEED excluding it):
   VERDICT_TRUE; whether they can't meet: VERDICT_FALSE.  */

static verdict
frange_requirement_verdict (const frange &have, const frange &need)
{
  if (have.undefined_p ())
    return VERDICT_UNKNOWN;
  if (have.known_isnan ())
    return VERDICT_FALSE;
  frange both = have;
  both.intersect (need);
  if (both.undefined_p ())
    return VERDICT_FALSE;
  if (!have.maybe_isnan ()
      && !real_less (&have.lower_bound (), &need.lower_bound ())
      && !real_less (&need.upper_bound (), &have.upper_bound ()))
    return VERDICT_TRUE;
  return VERDICT_UNKNOWN;
}

/* Each 'ptr->field CODE C' conjunct of COND with C a floating-point
   constant, grouped by (pointer, field) and intersected; the pointer a
   parameter (or with ALLOW_VAR a variable).  */

struct float_field_group { tree field; tree ptr_expr; frange range; };

static void
collect_float_field_groups (tree cond, vec<float_field_group> *out,
			    bool allow_var = false)
{
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree field, ptr_expr, cst;
      tree_code code;
      if (!oa_match_field_range_comparison (*conjuncts[i], &field, &ptr_expr,
					     &code, &cst)
	  || TREE_CODE (cst) != REAL_CST
	  || !SCALAR_FLOAT_TYPE_P (TREE_TYPE (field)))
	continue;
      ptr_expr = oa_strip_symbolic_ptr_expr_public (ptr_expr);
      if (TREE_CODE (ptr_expr) != PARM_DECL
	  && !(allow_var && VAR_P (ptr_expr)))
	continue;
      frange r;
      if (!frange_for_comparison (TREE_TYPE (field), code, cst, &r))
	continue;
      float_field_group *found = NULL;
      for (unsigned j = 0; j < out->length () && !found; ++j)
	if ((*out)[j].field == field && (*out)[j].ptr_expr == ptr_expr)
	  found = &(*out)[j];
      if (found)
	found->range.intersect (r);
      else
	out->safe_push ({ field, ptr_expr, r });
    }
}

static tree scalar_value_origin (tree);

/* The floating-point values parameters were trusted to have on entry
   (their own preconditions), keyed by the incoming SSA value.  */

static hash_map<tree, frange> *established_frange;

/* The range of floating-point value V at AT: a constant's, or the
   ranger's, narrowed by what V was trusted to be.  False if unknown.  */

static bool
float_range_at (tree v, gimple *at, gimple_ranger *ranger, frange &fr)
{
  if (TREE_CODE (v) == REAL_CST)
    {
      fr = frange (v, v);
      if (!real_isnan (TREE_REAL_CST_PTR (v)))
	fr.clear_nan ();
      return true;
    }
  if (TREE_CODE (v) != SSA_NAME || !SCALAR_FLOAT_TYPE_P (TREE_TYPE (v)))
    return false;
  if (!ranger || !ranger->range_of_expr (fr, v, at) || fr.undefined_p ())
    fr.set_varying (TREE_TYPE (v));
  if (established_frange)
    if (frange *t = established_frange->get (scalar_value_origin (v)))
      fr.intersect (*t);
  return !fr.varying_p () && !fr.undefined_p ();
}

/* Whether floating-point value V satisfies 'V CODE CST' at AT, by the
   ranger's range: for all values only if none can be a NaN (a NaN makes
   the comparison false), for none if the bounds rule it out.  */

static verdict
float_verdict (tree v, tree_code code, tree cst, gimple *at,
	       gimple_ranger *ranger)
{
  frange fr;
  if (!float_range_at (v, at, ranger, fr))
    return VERDICT_UNKNOWN;
  return frange_verdict (fr, code, cst);
}

/* What an accessor call on an object gives: a range; CONVEYOR if
   established in the conveyor flavor.  */

struct call_range_fact { oa_range_lite r; bool conveyor; };

/* A value V related to an accessor call: '(V - OFFSET) CODE
   RECEIVER.ACCESSOR ()', RECEIVER an object identity.  */

struct call_rel_fact
{
  tree_code code;
  tree receiver;
  tree accessor;
  widest_int offset;
  bool conveyor;
};

/* A value V related to another value: 'V CODE OTHER'.  */

struct value_rel_fact { tree_code code; tree other; bool conveyor; };

/* The persistent-memory facts at one program point, threaded through the
   fact walk below.  Relations are keyed by relation_key.  */

struct dom_fact_state
{
  hash_map<tree, pred_facts> pred;
  hash_map<oa_field_key_hash, oa_range_lite> field;
  hash_map<tree, relation_fact_lite> rel;
  /* The locals whose address a call that may end lifetimes was given: it
     may have kept it, so any later such call may change them.  On any
     path, so a union at joins.  */
  hash_set<tree> escaped;
  /* For a variable holding an aggregate value a relation may be about,
     the objects that value was obtained from, a TREE_LIST of their
     identities: a call that may end them ends the value's relations (the
     derived-value rule).  Absent: not known, so any call that may end
     lifetimes and is given a route does.  */
  hash_map<tree, tree> val_src;
  /* An accessor's result on an object, keyed (identity, accessor).  */
  hash_map<oa_field_key_hash, call_range_fact> call;
  /* Values related to accessor calls and to other values, keyed by the
     value (value_key); learned from branches and self-trust.  */
  hash_map<tree, call_rel_fact> call_rel;
  hash_map<tree, value_rel_fact> vrel;
  /* A floating-point member's range, keyed (identity, field).  */
  hash_map<oa_field_key_hash, frange> ffield;
};

/* The object pointer VAL points to: a variable whose address it is, or
   the pointer value itself, through copies, conversions and PHIs whose
   arguments all point to the same one.  NULL_TREE if VAL isn't a
   pointer.  */

static tree
gimple_object_identity_1 (tree val, hash_set<tree> &visiting)
{
  if (val == NULL_TREE)
    return NULL_TREE;
  if (TREE_CODE (val) == ADDR_EXPR)
    {
      tree op = TREE_OPERAND (val, 0);
      if (DECL_P (op) && (VAR_P (op) || TREE_CODE (op) == PARM_DECL))
	return op;
      return NULL_TREE;
    }
  if (TREE_CODE (val) == SSA_NAME && POINTER_TYPE_P (TREE_TYPE (val)))
    {
      if (visiting.add (val))
	return val;
      gimple *def = SSA_NAME_DEF_STMT (val);
      if (def && gimple_code (def) == GIMPLE_PHI)
	{
	  tree shared = NULL_TREE;
	  for (unsigned i = 0; i < gimple_phi_num_args (def); ++i)
	    {
	      tree id = gimple_object_identity_1 (gimple_phi_arg_def (def, i),
						  visiting);
	      if (!id || (shared && shared != id))
		return val;
	      shared = id;
	    }
	  return shared ? shared : val;
	}
      if (def && is_gimple_assign (def))
	{
	  tree_code code = gimple_assign_rhs_code (def);
	  if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME
	      || code == ADDR_EXPR)
	    {
	      tree id = gimple_object_identity_1 (gimple_assign_rhs1 (def),
						  visiting);
	      return id ? id : val;
	    }
	}
      return val;
    }
  if (VAR_P (val) || TREE_CODE (val) == PARM_DECL)
    return val;
  return NULL_TREE;
}

static tree
gimple_object_identity (tree val)
{
  hash_set<tree> visiting;
  return gimple_object_identity_1 (val, visiting);
}

/* The SSA value scalar VAL is a copy of.  */

static tree
scalar_value_origin (tree val)
{
  while (TREE_CODE (val) == SSA_NAME)
    {
      gimple *def = SSA_NAME_DEF_STMT (val);
      if (!def || !gimple_assign_single_p (def)
	  || TREE_CODE (gimple_assign_rhs1 (def)) != SSA_NAME)
	break;
      val = gimple_assign_rhs1 (def);
    }
  return val;
}

/* The value VAL stands for in a fact: VAL, or what a conversion of it
   converts ('(size_t) idx' compared with 'v.size ()').  */

static tree
value_key (tree val)
{
  if (TREE_CODE (val) == SSA_NAME && !SSA_NAME_IS_DEFAULT_DEF (val))
    {
      gimple *def = SSA_NAME_DEF_STMT (val);
      if (def && is_gimple_assign (def)
	  && CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (def))
	  && TREE_CODE (gimple_assign_rhs1 (def)) == SSA_NAME)
	val = gimple_assign_rhs1 (def);
    }
  return scalar_value_origin (val);
}

/* Whether VAL is the result of a call to a conveyor accessor (a member
   function of one argument, the object), through a copy: *ACCESSOR and
   *RECEIVER, the object pointer.  */

static bool
call_shape (tree val, tree *accessor, tree *receiver)
{
  if (TREE_CODE (val) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (def && gimple_assign_single_p (def)
      && TREE_CODE (gimple_assign_rhs1 (def)) == SSA_NAME)
    def = SSA_NAME_DEF_STMT (gimple_assign_rhs1 (def));
  gcall *call = def ? dyn_cast <gcall *> (def) : NULL;
  tree fn = call ? gimple_call_fndecl (call) : NULL_TREE;
  if (!fn || !DECL_OBJECT_MEMBER_FUNCTION_P (fn)
      || gimple_call_num_args (call) != 1 || !DECL_DECLARED_CONVEYOR_P (fn))
    return false;
  *accessor = fn;
  *receiver = gimple_call_arg (call, 0);
  return true;
}

/* Whether VAL is a load of a member: *FIELD of the object *BASE (a
   pointer, or the object variable).  */

static bool
field_shape (tree val, tree *field, tree *base)
{
  if (TREE_CODE (val) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (!def || !gimple_assign_single_p (def)
      || TREE_CODE (gimple_assign_rhs1 (def)) != COMPONENT_REF)
    return false;
  tree rhs = gimple_assign_rhs1 (def);
  if (TREE_CODE (TREE_OPERAND (rhs, 1)) != FIELD_DECL)
    return false;
  tree obj = TREE_OPERAND (rhs, 0);
  *field = TREE_OPERAND (rhs, 1);
  *base = (TREE_CODE (obj) == MEM_REF && integer_zerop (TREE_OPERAND (obj, 1))
	   ? TREE_OPERAND (obj, 0) : obj);
  return true;
}

/* CODE with its operands' roles exchanged, or (NEGATE) its negation;
   ERROR_MARK if there is none to use ('!(a == b)').  */

static tree_code
swapped_code (tree_code code)
{
  return swap_tree_comparison (code);
}

static tree_code
negated_code (tree_code code)
{
  switch (code)
    {
    case LT_EXPR: return GE_EXPR;
    case LE_EXPR: return GT_EXPR;
    case GT_EXPR: return LE_EXPR;
    case GE_EXPR: return LT_EXPR;
    default: return ERROR_MARK;
    }
}

/* The key of relation PRED_FN (IDS[0..N)).  */

static tree
relation_key (tree pred_fn, const tree *ids, unsigned n)
{
  char buf[24 * (OA_RELATION_MAX_ARGS + 1)];
  int len = snprintf (buf, sizeof buf, "%p", (void *) pred_fn);
  for (unsigned i = 0; i < n; ++i)
    len += snprintf (buf + len, sizeof buf - len, ":%p", (void *) ids[i]);
  return get_identifier (buf);
}

static void
relation_put (dom_fact_state &state, tree pred_fn, const tree *ids,
	      unsigned n, bool polarity, bool conveyor)
{
  relation_fact_lite f;
  f.pred_fn = pred_fn;
  f.n = n;
  for (unsigned i = 0; i < n; ++i)
    f.ids[i] = ids[i];
  f.polarity = polarity;
  f.conveyor = conveyor;
  state.rel.put (relation_key (pred_fn, ids, n), f);
}

/* STATE's relations with ID, or a member of the value ID is, among their
   arguments end.  */

static void
relation_drop (dom_fact_state &state, tree id)
{
  if (!id || !state.rel.elements ())
    return;
  auto_vec<tree, 8> keys;
  for (auto it : state.rel)
    for (unsigned i = 0; i < it.second.n; ++i)
      if (it.second.ids[i] == id
	  || oa_relation_member_slot_base (it.second.ids[i]) == id)
	{
	  keys.safe_push (it.first);
	  break;
	}
  for (tree k : keys)
    state.rel.remove (k);
}

/* The identity of VAL, a GIMPLE operand that is argument I of relation
   PRED_FN; NULL_TREE if it has none.  */

static tree
relation_operand_identity (tree pred_fn, unsigned i, tree val)
{
  if (!val)
    return NULL_TREE;
  if (oa_relation_arg_kind_of (pred_fn, i) == OA_RELATION_VALUE)
    {
      if (TREE_CODE (val) == INTEGER_CST)
	return val;
      if (TREE_CODE (val) == SSA_NAME)
	return scalar_value_origin (val);
      if ((VAR_P (val) || TREE_CODE (val) == PARM_DECL)
	  && AGGREGATE_TYPE_P (TREE_TYPE (val)))
	return val;
      if (TREE_CODE (val) == COMPONENT_REF)
	return oa_relation_member_slot (val);
      return NULL_TREE;
    }
  return gimple_object_identity (val);
}

/* The GIMPLE operand at CALL, to CALLEE, that ARG, an argument of a
   relation conjunct of CALLEE's contract, stands for: a constant; the
   argument CALL passes for a parameter, or for a value read through a
   reference parameter the variable it is bound to; a member of either;
   or what CALL's result initializes, for RESULT_NAME, the result of a
   postcondition.  NULL_TREE if none.  */

static tree
relation_call_operand (gcall *call, tree callee, tree arg, tree result_name)
{
  tree s = oa_relation_strip_arg (arg);
  if (TREE_CODE (s) == INTEGER_CST)
    return s;
  if (TREE_CODE (s) == COMPONENT_REF
      && TREE_CODE (TREE_OPERAND (s, 1)) == FIELD_DECL)
    {
      auto_vec<tree, 4> fields;
      while (TREE_CODE (s) == COMPONENT_REF
	     && TREE_CODE (TREE_OPERAND (s, 1)) == FIELD_DECL)
	{
	  fields.safe_push (TREE_OPERAND (s, 1));
	  s = oa_relation_strip_arg (TREE_OPERAND (s, 0));
	}
      bool deref = INDIRECT_REF_P (s);
      tree b = deref ? oa_relation_strip_arg (TREE_OPERAND (s, 0)) : s;
      tree r = NULL_TREE;
      if (result_name && b == result_name)
	r = gimple_call_lhs (call);
      else if (TREE_CODE (b) == PARM_DECL)
	{
	  unsigned argno;
	  if (find_param_position (callee, b, &argno)
	      && argno < gimple_call_num_args (call))
	    {
	      tree a = gimple_call_arg (call, argno);
	      if (!deref)
		r = a;
	      else if (TREE_CODE (a) == ADDR_EXPR)
		r = TREE_OPERAND (a, 0);
	    }
	}
      if (!r || !(VAR_P (r) || TREE_CODE (r) == PARM_DECL))
	return NULL_TREE;
      for (unsigned i = fields.length (); i-- > 0; )
	r = build3 (COMPONENT_REF, TREE_TYPE (fields[i]), r, fields[i],
		    NULL_TREE);
      return r;
    }
  bool deref = INDIRECT_REF_P (s);
  tree base = deref ? oa_relation_strip_arg (TREE_OPERAND (s, 0)) : s;
  if (result_name && base == result_name)
    {
      tree lhs = gimple_call_lhs (call);
      if (lhs && (TREE_CODE (lhs) == SSA_NAME || VAR_P (lhs)
		  || TREE_CODE (lhs) == PARM_DECL))
	return lhs;
      return NULL_TREE;
    }
  unsigned argno;
  if (TREE_CODE (base) != PARM_DECL
      || !find_param_position (callee, base, &argno)
      || argno >= gimple_call_num_args (call))
    return NULL_TREE;
  tree a = gimple_call_arg (call, argno);
  if (deref && TYPE_REF_P (TREE_TYPE (base)) && !POINTER_TYPE_P (TREE_TYPE (s)))
    {
      if (TREE_CODE (a) == ADDR_EXPR
	  && (VAR_P (TREE_OPERAND (a, 0))
	      || TREE_CODE (TREE_OPERAND (a, 0)) == PARM_DECL))
	return TREE_OPERAND (a, 0);
      return NULL_TREE;
    }
  return a;
}

/* The identities, at CALL to CALLEE, of the arguments ARGS of relation
   PRED_FN, into IDS; false if one has none.  */

static bool
relation_call_identities (gcall *call, tree callee, tree pred_fn,
			  const vec<tree> &args, tree result_name, tree *ids)
{
  for (unsigned i = 0; i < args.length (); ++i)
    if (!(ids[i] = relation_operand_identity
		     (pred_fn, i, relation_call_operand (call, callee, args[i],
							 result_name))))
      return false;
  return true;
}

/* Whether conjunct C is a relation (oa_relation_conjunct_shape): that,
   not a one-object predicate, is what it is checked and established as.  */

static bool
relation_conjunct_p (tree c)
{
  tree pred_fn;
  bool negated;
  auto_vec<tree> args;
  return oa_relation_conjunct_shape (c, &pred_fn, &args, &negated);
}

/* The identity, in FUN's body, of ARG, argument I of relation PRED_FN in
   one of FUN's own preconditions: a parameter's value at entry.  */

static tree
relation_self_identity (function *fun, tree pred_fn, unsigned i, tree arg)
{
  tree s = oa_relation_strip_arg (arg);
  if (TREE_CODE (s) == INTEGER_CST)
    return s;
  /* A member of a parameter passed by value ('r.first').  */
  if (TREE_CODE (s) == COMPONENT_REF)
    {
      tree b = s;
      while (TREE_CODE (b) == COMPONENT_REF)
	b = oa_relation_strip_arg (TREE_OPERAND (b, 0));
      return (TREE_CODE (b) == PARM_DECL
	      && oa_relation_arg_kind_of (pred_fn, i) == OA_RELATION_VALUE
	      ? oa_relation_member_slot (s) : NULL_TREE);
    }
  bool deref = INDIRECT_REF_P (s);
  tree base = deref ? oa_relation_strip_arg (TREE_OPERAND (s, 0)) : s;
  if (TREE_CODE (base) != PARM_DECL)
    return NULL_TREE;
  if (oa_relation_arg_kind_of (pred_fn, i) == OA_RELATION_VALUE
      && AGGREGATE_TYPE_P (TREE_TYPE (base)) && !deref)
    return base;
  tree ssa = ssa_default_def (fun, base);
  return ssa ? relation_operand_identity (pred_fn, i, ssa) : NULL_TREE;
}

/* A ptr->field range conjunct group, exactly like contracts.cc's own
   oa_symbolic_field_group, just built from the exported oa_match_
   field_range_comparison/oa_strip_symbolic_ptr_expr_public primitives
   directly rather than through oa_precondition_field_range_
   obligations's own PRECONDITION_P/oa_contract_fact_tracking_active_p-
   gated iteration -- that gate turned out, by direct testing, to
   answer false at GIMPLE-pass time for a plainly conveyor-active
   contract (its own dependency on contract_side_of/DECL_LANG_SPECIFIC
   apparently isn't reliable this late), so this collects the shape
   itself instead, matching this whole prototype's already-established
   choice (Sections 8/9's own is_object_address/nonzero/predicate work)
   to skip flavor-gating entirely rather than depend on machinery only
   ever exercised at front-end time before now.  */

struct field_group_lite { tree field; tree ptr_expr; oa_range_lite range; };

static void
collect_field_range_groups (tree cond, vec<field_group_lite> *out)
{
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree field, ptr_expr, const_val;
      tree_code code;
      if (!oa_match_field_range_comparison (*conjuncts[i], &field, &ptr_expr,
					     &code, &const_val)
	  || TREE_CODE (const_val) != INTEGER_CST)
	continue;
      ptr_expr = oa_strip_symbolic_ptr_expr_public (ptr_expr);
      if (TREE_CODE (ptr_expr) != PARM_DECL)
	continue;

      field_group_lite *found = NULL;
      for (unsigned j = 0; j < out->length () && !found; ++j)
	if ((*out)[j].field == field && (*out)[j].ptr_expr == ptr_expr)
	  found = &(*out)[j];
      if (!found)
	{
	  field_group_lite g;
	  g.field = field;
	  g.ptr_expr = ptr_expr;
	  out->safe_push (g);
	  found = &out->last ();
	}
      tighten_range_bound (found->range, code, wi::to_widest (const_val));
    }
}

/* Seed SEED from FNDECL's own declared precondition -- self-trust, the
   exact analogue of seed_self_trust above, just for predicate/field-
   range facts: keyed by ssa_default_def(fun, parm) instead of a bare
   hash_set entry, since these facts carry a payload rather than being
   plain booleans.  */

static void
seed_predicate_self_trust (function *fun, dom_fact_state &seed)
{
  tree fndecl = fun->decl;
  for (tree contract : fn_contracts (fndecl))
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
	  tree pred_fn, arg_decl;
	  bool negated;
	  if (relation_conjunct_p (*conjuncts[i])
	      || !oa_match_predicate_conjunct (*conjuncts[i], &pred_fn,
					       &arg_decl, &negated))
	    continue;
	  if (TREE_CODE (arg_decl) != PARM_DECL)
	    continue;
	  tree ssa = ssa_default_def (fun, arg_decl);
	  if (ssa)
	    seed.pred.get_or_insert (ssa).put
	      ({ pred_fn, !negated,
		 oa_contract_conveyor_active_public (contract, fndecl) });
	}

      /* Relations among the parameters.  */
      bool conveyor = oa_contract_conveyor_active_public (contract, fndecl);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree pred_fn;
	  bool negated;
	  auto_vec<tree> args;
	  if (!oa_relation_conjunct_shape (*conjuncts[i], &pred_fn, &args,
					   &negated))
	    continue;
	  tree ids[OA_RELATION_MAX_ARGS];
	  bool all = true;
	  for (unsigned a = 0; a < args.length () && all; ++a)
	    all = (ids[a] = relation_self_identity (fun, pred_fn, a, args[a]));
	  if (all)
	    relation_put (seed, pred_fn, ids, args.length (), !negated,
			  conveyor);
	}

      /* A parameter related to an accessor call on another, and an
	 accessor's result compared with a constant.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree param, recv, acc, cst;
	  tree_code code;
	  if (oa_match_comparison_against_call (*conjuncts[i], &param, &code,
						&recv, &acc, !conveyor)
	      && TREE_CODE (param) == PARM_DECL)
	    {
	      tree pv = ssa_default_def (fun, param);
	      tree rb = oa_strip_symbolic_ptr_expr_public (recv);
	      tree rv = (TREE_CODE (rb) == PARM_DECL
			 ? ssa_default_def (fun, rb) : NULL_TREE);
	      tree id = rv ? gimple_object_identity (rv) : NULL_TREE;
	      if (pv && id)
		seed.call_rel.put (value_key (pv),
				   { code, id, acc, 0, conveyor });
	    }
	  else if (oa_match_call_range_comparison (*conjuncts[i], &recv, &acc,
						   &code, &cst, !conveyor)
		   && TREE_CODE (cst) == INTEGER_CST)
	    {
	      tree rb = oa_strip_symbolic_ptr_expr_public (recv);
	      tree rv = (TREE_CODE (rb) == PARM_DECL
			 ? ssa_default_def (fun, rb) : NULL_TREE);
	      tree id = rv ? gimple_object_identity (rv) : NULL_TREE;
	      if (!id)
		continue;
	      call_range_fact *old = seed.call.get ({id, acc});
	      call_range_fact f = old ? *old : call_range_fact { {}, conveyor };
	      tighten_range_bound (f.r, code, wi::to_widest (cst));
	      seed.call.put ({id, acc}, f);
	    }
	}

      /* A reference parameter compared with a constant ('pre (r > 0)'
	 for 'int &r'): a fact about the object it is bound to.  */
      auto_vec<tree> ref_params;
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree param, cst;
	  tree_code code;
	  if (oa_match_simple_comparison (*conjuncts[i], &param, &code, &cst)
	      && TREE_CODE (param) == PARM_DECL && TYPE_REF_P (TREE_TYPE (param))
	      && TREE_CODE (cst) == INTEGER_CST
	      && !ref_params.contains (param))
	    ref_params.safe_push (param);
	}
      for (tree param : ref_params)
	{
	  oa_range_lite r;
	  tree ssa = ssa_default_def (fun, param);
	  tree id = ssa ? gimple_object_identity (ssa) : NULL_TREE;
	  if (id && extract_conjunct_range (conjuncts, param, &r))
	    seed.field.put ({id, NULL_TREE}, r);
	}

      auto_vec<float_field_group> float_groups;
      collect_float_field_groups (cond, &float_groups);
      for (auto &g : float_groups)
	if (tree ssa = ssa_default_def (fun, g.ptr_expr))
	  seed.ffield.put ({ssa, g.field}, g.range);

      auto_vec<field_group_lite> field_groups;
      collect_field_range_groups (cond, &field_groups);
      for (unsigned g = 0; g < field_groups.length (); ++g)
	{
	  tree pe = field_groups[g].ptr_expr;
	  /* An aggregate passed by value is the parameter itself.  */
	  tree id = (POINTER_TYPE_P (TREE_TYPE (pe)) || TYPE_REF_P (TREE_TYPE (pe))
		     ? ssa_default_def (fun, pe) : pe);
	  if (id)
	    seed.field.put ({id, field_groups[g].field}, field_groups[g].range);
	}
    }
}

/* Whether '(V - D) CODE X' entails 'V REQUIRED X' for the offset D, once
   CODE implies REQUIRED.  */

static bool
offset_compatible_p (const widest_int &d, tree_code required)
{
  switch (required)
    {
    case LT_EXPR: case LE_EXPR: return d <= 0;
    case GT_EXPR: case GE_EXPR: return d >= 0;
    case EQ_EXPR: return d == 0;
    default: return false;
    }
}

/* Whether '(V - D) CODE X' contradicts 'V REQUIRED X': the ranges of
   V - X they allow are disjoint.  */

static bool
offset_contradicts_p (tree_code code, const widest_int &d, tree_code required)
{
  oa_range_lite have, need;
  if (code == NE_EXPR || required == NE_EXPR)
    return false;
  tighten_range_bound (have, code, d);
  tighten_range_bound (need, required, 0);
  return ((have.has_hi && need.has_lo && have.hi < need.lo)
	  || (need.has_hi && have.has_lo && need.hi < have.lo));
}

/* Whether every pair of values from A and B satisfies 'a CODE b'
   (VERDICT_TRUE), none does (VERDICT_FALSE), or neither is known.  */

static verdict
range_pair_verdict (const oa_range_lite &a, tree_code code,
		    const oa_range_lite &b)
{
  switch (code)
    {
    case LT_EXPR:
      if (a.has_hi && b.has_lo && a.hi < b.lo) return VERDICT_TRUE;
      if (a.has_lo && b.has_hi && a.lo >= b.hi) return VERDICT_FALSE;
      break;
    case LE_EXPR:
      if (a.has_hi && b.has_lo && a.hi <= b.lo) return VERDICT_TRUE;
      if (a.has_lo && b.has_hi && a.lo > b.hi) return VERDICT_FALSE;
      break;
    case GT_EXPR:
      if (a.has_lo && b.has_hi && a.lo > b.hi) return VERDICT_TRUE;
      if (a.has_hi && b.has_lo && a.hi <= b.lo) return VERDICT_FALSE;
      break;
    case GE_EXPR:
      if (a.has_lo && b.has_hi && a.lo >= b.hi) return VERDICT_TRUE;
      if (a.has_hi && b.has_lo && a.hi < b.lo) return VERDICT_FALSE;
      break;
    case EQ_EXPR:
      if (a.has_lo && a.has_hi && b.has_lo && b.has_hi && a.lo == a.hi
	  && b.lo == b.hi && a.lo == b.lo)
	return VERDICT_TRUE;
      if ((a.has_hi && b.has_lo && a.hi < b.lo)
	  || (a.has_lo && b.has_hi && a.lo > b.hi))
	return VERDICT_FALSE;
      break;
    default:
      break;
    }
  return VERDICT_UNKNOWN;
}

/* The fact relating value V to an accessor call: V's own, or, for 'Y + K'
   or 'Y - K' of a signed type (no wrapping), Y's with the offset moved.
   Into *F; false if none.  */

static bool
get_call_rel (tree v, dom_fact_state &state, call_rel_fact *f)
{
  v = value_key (v);
  if (call_rel_fact *g = state.call_rel.get (v))
    {
      *f = *g;
      return true;
    }
  gimple *def = TREE_CODE (v) == SSA_NAME ? SSA_NAME_DEF_STMT (v) : NULL;
  if (!def || !is_gimple_assign (def)
      || (gimple_assign_rhs_code (def) != PLUS_EXPR
	  && gimple_assign_rhs_code (def) != MINUS_EXPR)
      || TREE_CODE (gimple_assign_rhs2 (def)) != INTEGER_CST)
    return false;
  tree y = gimple_assign_rhs1 (def);
  call_rel_fact *g = state.call_rel.get (value_key (y));
  if (!g)
    return false;
  widest_int k = wi::to_widest (gimple_assign_rhs2 (def));
  bool plus = gimple_assign_rhs_code (def) == PLUS_EXPR;
  /* A wrapping type: only if Y's range keeps the result in it.  */
  if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (v)))
    {
      oa_range_lite r;
      tree type = TREE_TYPE (v);
      if (!current_value_facts
	  || !established_range_of (y, *current_value_facts->established_range,
				    current_value_facts->ranger, def, &r))
	return false;
      widest_int lo = plus ? r.lo + k : r.lo - k;
      widest_int hi = plus ? r.hi + k : r.hi - k;
      if (!r.has_lo || !r.has_hi
	  || lo < wi::to_widest (TYPE_MIN_VALUE (type))
	  || hi > wi::to_widest (TYPE_MAX_VALUE (type)))
	return false;
    }
  *f = *g;
  /* (Y - A) CODE X and V = Y + K: (V - (A + K)) CODE X.  */
  f->offset += plus ? k : -k;
  return true;
}

/* CALL's own callee's declared precondition, checked against STATE as
   it stands right before CALL -- the consult side, for both facts.  */

static void
consult_persistent_facts (gcall *call, dom_fact_state &state)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  callee_declared_note note (call);
  for (tree contract : fn_contracts (callee))
    {
      bool as_conveyor, strict;
      if (!call_obligation_p (contract, callee, &as_conveyor, &strict))
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
	  if (relation_conjunct_p (*conjuncts[i])
	      || !oa_match_predicate_conjunct (*conjuncts[i], &pred_fn,
					       &arg_decl, &negated))
	    continue;

	  unsigned argno;
	  if (!find_param_position (callee, arg_decl, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);
	  tree identity = gimple_object_identity (substituted);

	  bool required = !negated;
	  pred_facts *facts = identity ? state.pred.get (identity) : NULL;
	  pred_fact *fact = facts ? facts->find (pred_fn) : NULL;
	  if (fact && as_conveyor && !fact->conveyor)
	    fact = NULL;
	  if (fact)
	    {
	      if (fact->polarity == required)
		continue;
	      /* Established at the opposite polarity: a contradiction.  */
	      error_at (gimple_location (call), "argument %qE provably "
			"violates the precondition of %qD: %qD (%qE) is "
			"established %s, but the precondition requires it to "
			"be %s", substituted, callee, pred_fn, substituted,
			fact->polarity ? "true" : "false",
			required ? "true" : "false");
	      continue;
	    }

	  if (strict)
	    report_unproven (call, true, "cannot prove that %qD (%qE) holds, "
			     "as required by the precondition of %qD", pred_fn,
			     substituted, callee);
	  else
	    report_unproven (call, false, "cannot verify that %qD (%qE) holds, "
			     "as required by the precondition of %qD", pred_fn,
			     substituted, callee);
	}

      /* A parameter related to an accessor call on another ('i <
	 v.size ()', or shifted: 'i - v.size () < 10'): by a fact relating
	 the argument to that call on that object, else by the argument's
	 range against the accessor's.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree param, recv, acc;
	  tree_code code;
	  widest_int need_offset = 0;
	  if (!oa_match_comparison_against_call (*conjuncts[i], &param, &code,
						 &recv, &acc, !as_conveyor)
	      && !oa_match_shifted_comparison_against_call_text
		    (*conjuncts[i], &param, &code, &recv, &acc, &need_offset,
		     !as_conveyor))
	    continue;
	  unsigned pn, rn;
	  if (!find_param_position (callee, param, &pn)
	      || !find_param_position (callee, recv, &rn)
	      || pn >= gimple_call_num_args (call)
	      || rn >= gimple_call_num_args (call))
	    continue;
	  tree sub_param = gimple_call_arg (call, pn);
	  tree rid = gimple_object_identity (gimple_call_arg (call, rn));
	  verdict v = VERDICT_UNKNOWN;
	  call_rel_fact f;
	  if (rid && get_call_rel (sub_param, state, &f) && f.receiver == rid
	      && f.accessor == acc && (!as_conveyor || f.conveyor))
	    {
	      widest_int d = f.offset - need_offset;
	      if (oa_relational_code_implies (f.code, code)
		  && offset_compatible_p (d, code))
		v = VERDICT_TRUE;
	      else if (offset_contradicts_p (f.code, d, code))
		v = VERDICT_FALSE;
	    }
	  oa_range_lite pr;
	  call_range_fact *cr = rid ? state.call.get ({rid, acc}) : NULL;
	  if (v == VERDICT_UNKNOWN && need_offset == 0 && cr
	      && (!as_conveyor || cr->conveyor) && current_value_facts
	      && established_range_of (sub_param,
				       *current_value_facts->established_range,
				       current_value_facts->ranger, call, &pr))
	    v = range_pair_verdict (pr, code, cr->r);
	  if (v == VERDICT_TRUE)
	    continue;
	  if (v == VERDICT_FALSE)
	    error_at (gimple_location (call), "argument %qE provably violates "
		      "the precondition of %qD", sub_param, callee);
	  else
	    report_unproven (call, strict, strict
			     ? "cannot prove that %qE satisfies the "
			       "precondition of %qD"
			     : "cannot verify that %qE satisfies the "
			       "precondition of %qD", sub_param, callee);
	}

      /* A parameter related to another ('x < q'): both literal, a
	 self-trusted or branch-derived fact, or their ranges.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rel_other;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_param (*conjuncts[i], &rel_param,
						   &rel_code, &rel_other))
	    continue;
	  unsigned pn, on;
	  if (!find_param_position (callee, rel_param, &pn)
	      || !find_param_position (callee, rel_other, &on)
	      || pn >= gimple_call_num_args (call)
	      || on >= gimple_call_num_args (call))
	    continue;
	  tree a = gimple_call_arg (call, pn), b = gimple_call_arg (call, on);
	  verdict v = VERDICT_UNKNOWN;
	  if (TREE_CODE (a) == INTEGER_CST && TREE_CODE (b) == INTEGER_CST)
	    v = (oa_relational_literal_holds (rel_code, a, b)
		 ? VERDICT_TRUE : VERDICT_FALSE);
	  for (int side = 0; side < 2 && v == VERDICT_UNKNOWN; ++side)
	    {
	      tree from = value_key (side ? b : a), to = value_key (side ? a : b);
	      tree_code need = side ? swapped_code (rel_code) : rel_code;
	      value_rel_fact *g = state.vrel.get (from);
	      if (g && g->other == to && (!as_conveyor || g->conveyor)
		  && oa_relational_code_implies (g->code, need))
		v = VERDICT_TRUE;
	      tree_code fc;
	      tree frhs;
	      if (v == VERDICT_UNKNOWN && current_value_facts
		  && get_relational (side ? b : a,
				     *current_value_facts->established_rel,
				     &fc, &frhs)
		  && frhs == (side ? a : b)
		  && oa_relational_code_implies (fc, need))
		v = VERDICT_TRUE;
	    }
	  oa_range_lite ra, rb;
	  if (v == VERDICT_UNKNOWN && current_value_facts
	      && established_range_of (a, *current_value_facts->established_range,
				       current_value_facts->ranger, call, &ra)
	      && established_range_of (b, *current_value_facts->established_range,
				       current_value_facts->ranger, call, &rb))
	    v = range_pair_verdict (ra, rel_code, rb);
	  if (v == VERDICT_TRUE)
	    continue;
	  if (v == VERDICT_FALSE)
	    error_at (gimple_location (call), "argument %qE provably violates "
		      "the precondition of %qD", a, callee);
	  else
	    report_unproven (call, strict, strict
			     ? "cannot prove that %qE satisfies the "
			       "precondition of %qD"
			     : "cannot verify that %qE satisfies the "
			       "precondition of %qD", a, callee);
	}

      /* Relations: proven by a fact, refuted by one of the other
	 polarity.  A conveyor obligation needs one a conveyor contract
	 established.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree pred_fn;
	  bool negated;
	  auto_vec<tree> args;
	  if (!oa_relation_conjunct_shape (*conjuncts[i], &pred_fn, &args,
					   &negated))
	    continue;
	  tree ids[OA_RELATION_MAX_ARGS];
	  relation_fact_lite *f = NULL;
	  if (relation_call_identities (call, callee, pred_fn, args, NULL_TREE,
					ids))
	    f = state.rel.get (relation_key (pred_fn, ids, args.length ()));
	  if (f && as_conveyor && !f->conveyor)
	    f = NULL;
	  bool required = !negated;
	  if (f && f->polarity == required)
	    continue;
	  if (f)
	    error_at (gimple_location (call), "%qD provably fails the "
		      "precondition of %qD: the relation is established %s, "
		      "but the precondition requires it to be %s", pred_fn,
		      callee, f->polarity ? "true" : "false",
		      required ? "true" : "false");
	  else if (strict)
	    report_unproven (call, true, "cannot prove that %qD holds, as "
			     "required by the precondition of %qD", pred_fn,
			     callee);
	  else
	    report_unproven (call, false, "cannot verify that %qD holds, as "
			     "required by the precondition of %qD", pred_fn,
			     callee);
	}

      auto_vec<float_field_group> float_groups;
      collect_float_field_groups (cond, &float_groups);
      for (auto &g : float_groups)
	{
	  unsigned argno;
	  if (!find_param_position (callee, g.ptr_expr, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);
	  tree id = gimple_object_identity (substituted);
	  frange *have = id ? state.ffield.get ({id, g.field}) : NULL;
	  verdict v = (have ? frange_requirement_verdict (*have, g.range)
		       : VERDICT_UNKNOWN);
	  if (v == VERDICT_TRUE)
	    continue;
	  if (v == VERDICT_FALSE)
	    error_at (gimple_location (call), "argument %qE provably violates "
		      "the precondition of %qD", substituted, callee);
	  else
	    report_unproven (call, strict, strict
			     ? "cannot prove that field %qD of %qE satisfies "
			       "the precondition of %qD"
			     : "cannot verify that field %qD of %qE satisfies "
			       "the precondition of %qD",
			     g.field, substituted, callee);
	}

      auto_vec<field_group_lite> field_groups;
      collect_field_range_groups (cond, &field_groups);
      for (unsigned g = 0; g < field_groups.length (); ++g)
	{
	  unsigned argno;
	  if (!find_param_position (callee, field_groups[g].ptr_expr, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);
	  tree identity = gimple_object_identity (substituted);
	  oa_range_lite &required = field_groups[g].range;

	  oa_range_lite *have
	    = identity ? state.field.get ({identity, field_groups[g].field})
		       : NULL;
	  if (have
	      && (!required.has_lo || (have->has_lo && have->lo >= required.lo))
	      && (!required.has_hi || (have->has_hi && have->hi <= required.hi)))
	    continue;
	  /* Disjoint from what is required: a contradiction.  */
	  if (have
	      && ((required.has_lo && have->has_hi && have->hi < required.lo)
		  || (required.has_hi && have->has_lo
		      && have->lo > required.hi)))
	    {
	      error_at (gimple_location (call), "argument %qE provably "
			"violates the precondition of %qD", substituted,
			callee);
	      continue;
	    }

	  if (strict)
	    report_unproven (call, true, "cannot prove that field %qD of %qE "
			     "satisfies the precondition of %qD",
			     field_groups[g].field, substituted, callee);
	  else
	    report_unproven (call, false, "cannot verify that field %qD of %qE "
			     "satisfies the precondition of %qD",
			     field_groups[g].field, substituted, callee);
	}
    }
}


/* Whether CALL may end the lifetime of an object it is given a route to
   ('delete p', 'free (p)'): not an internal, const or pure function, a
   builtin other than free and realloc, a contract check, or a conveyor,
   symbolic or conversion function.  */

static bool
call_may_end_lifetimes_p (gcall *call)
{
  if (gimple_call_internal_p (call)
      || (gimple_call_flags (call) & (ECF_CONST | ECF_PURE)))
    return false;
  tree sv, cd, dc;
  if (cg_contract_check_call_public (call, &sv, &cd, &dc))
    return false;
  if (gimple_call_builtin_p (call, BUILT_IN_NORMAL))
    {
      built_in_function code = DECL_FUNCTION_CODE (gimple_call_fndecl (call));
      return code == BUILT_IN_FREE || code == BUILT_IN_REALLOC;
    }
  tree named = contract_call_named_fn (gimple_call_fn (call));
  if (named && TREE_CODE (named) == FUNCTION_DECL
      && (DECL_CONV_FN_P (named) || DECL_DECLARED_CONVEYOR_P (named)
	  || DECL_DECLARED_SYMBOLIC_P (named)))
    return false;
  return true;
}

/* Does pointer ARG, of object IDENTITY, point to an object no fact can
   name: one loaded from memory or returned by a call, or either of two
   objects (a PHI)?  The address of a decl, a parameter's incoming value
   and a global pointer variable's value are named.  */

static bool
unnamed_pointer_p (tree arg, tree identity)
{
  if (TREE_CODE (arg) == ADDR_EXPR)
    return !DECL_P (get_base_address (TREE_OPERAND (arg, 0)));
  if (!identity)
    return true;
  if (TREE_CODE (identity) != SSA_NAME || SSA_NAME_IS_DEFAULT_DEF (identity))
    return false;
  gimple *def = SSA_NAME_DEF_STMT (identity);
  if (!def || gimple_code (def) == GIMPLE_PHI || is_gimple_call (def))
    return true;
  if (gimple_assign_single_p (def))
    {
      tree rhs = gimple_assign_rhs1 (def);
      if (VAR_P (rhs) && is_global_var (rhs))
	return false;
      return (TREE_CODE (rhs) != ADDR_EXPR
	      && (handled_component_p (rhs) || TREE_CODE (rhs) == MEM_REF
		  || VAR_P (rhs) || TREE_CODE (rhs) == PARM_DECL));
    }
  return false;
}

/* What STATE knows of object IDENTITY ends: its predicates, the
   relations it is in, and its fields' ranges (only FIELD's, if given).  */

static void
forget_object (dom_fact_state &state, tree identity, tree field = NULL_TREE)
{
  state.pred.remove (identity);
  relation_drop (state, identity);
  auto_vec<std::pair<tree, tree>> drop;
  for (auto it : state.field)
    if (it.first.first == identity && (!field || it.first.second == field))
      drop.safe_push (it.first);
  for (auto &k : drop)
    state.field.remove (k);
  drop.truncate (0);
  for (auto it : state.ffield)
    if (it.first.first == identity && (!field || it.first.second == field))
      drop.safe_push (it.first);
  for (auto &k : drop)
    state.ffield.remove (k);
  /* What its accessors give may change with any of its fields.  */
  drop.truncate (0);
  for (auto it : state.call)
    if (it.first.first == identity)
      drop.safe_push (it.first);
  for (auto &k : drop)
    state.call.remove (k);
  auto_vec<tree, 8> keys;
  for (auto it : state.call_rel)
    if (it.second.receiver == identity)
      keys.safe_push (it.first);
  for (tree k : keys)
    state.call_rel.remove (k);
}

/* What STATE knows of every object ends.  */

static void
forget_all (dom_fact_state &state)
{
  state.pred.empty ();
  state.field.empty ();
  state.rel.empty ();
  state.call.empty ();
  state.call_rel.empty ();
  state.ffield.empty ();
}

/* OBJ (an identity) may have changed: so may the objects of FUN's
   parameters that may alias it, if it is one's.  */

static void
forget_object_and_aliases (dom_fact_state &state, function *fun, tree obj)
{
  forget_object (state, obj);
  tree parm = (TREE_CODE (obj) == SSA_NAME && SSA_NAME_IS_DEFAULT_DEF (obj)
	       ? SSA_NAME_VAR (obj) : obj);
  if (!parm || TREE_CODE (parm) != PARM_DECL || DECL_CONTEXT (parm) != fun->decl)
    return;
  for (tree sib = DECL_ARGUMENTS (fun->decl); sib; sib = DECL_CHAIN (sib))
    if (sib != parm && oa_could_alias_as_parameters_public (parm, sib))
      {
	tree id = ssa_default_def (fun, sib);
	forget_object (state, id ? id : sib);
      }
}

/* The facts CALL may end, whether or not it has contracts of its own: an
   object it is given a route to may change, except through a pointer or
   reference to const to a callee that can't end lifetimes (or cast the
   constness away and still be conveyor-checked); a scalar it is given
   loses its predicates (a file descriptor to 'close'); a member's address
   may be stored to; and given a pointer to non-const to an object no fact
   names, it may change any object.  An aggregate passed by value is a
   copy.  */

static void
invalidate_persistent_facts_for_call_args (gcall *call, function *fun,
					   dom_fact_state &state)
{
  bool may_end = call_may_end_lifetimes_p (call);
  tree parm_types = (gimple_call_fntype (call)
		     ? TYPE_ARG_TYPES (gimple_call_fntype (call)) : NULL_TREE);
  for (unsigned i = 0; i < gimple_call_num_args (call);
       ++i, parm_types = parm_types ? TREE_CHAIN (parm_types) : NULL_TREE)
    {
      tree arg = gimple_call_arg (call, i);
      bool const_route = (parm_types && INDIRECT_TYPE_P (TREE_VALUE (parm_types))
			  && TYPE_READONLY (TREE_TYPE (TREE_VALUE (parm_types))));
      if (const_route && !may_end)
	continue;
      if (AGGREGATE_TYPE_P (TREE_TYPE (arg)))
	continue;
      tree identity = gimple_object_identity (arg);
      if (!identity)
	{
	  if (TREE_CODE (arg) == SSA_NAME)
	    {
	      tree v = scalar_value_origin (arg);
	      state.pred.remove (v);
	      relation_drop (state, v);
	    }
	}
      else
	forget_object_and_aliases (state, fun, identity);

      /* A member's address ('set (&this->d)').  */
      tree addr = arg;
      if (TREE_CODE (addr) == SSA_NAME && !SSA_NAME_IS_DEFAULT_DEF (addr)
	  && gimple_assign_single_p (SSA_NAME_DEF_STMT (addr)))
	addr = gimple_assign_rhs1 (SSA_NAME_DEF_STMT (addr));
      if (TREE_CODE (addr) == ADDR_EXPR
	  && TREE_CODE (TREE_OPERAND (addr, 0)) == COMPONENT_REF)
	{
	  tree ref = TREE_OPERAND (addr, 0);
	  tree obj = TREE_OPERAND (ref, 0);
	  tree id = gimple_object_identity (TREE_CODE (obj) == MEM_REF
					    ? TREE_OPERAND (obj, 0) : obj);
	  if (id)
	    forget_object (state, id, TREE_OPERAND (ref, 1));
	}

      if (POINTER_TYPE_P (TREE_TYPE (arg))
	  && !TYPE_READONLY (TREE_TYPE (TREE_TYPE (arg)))
	  && !FUNC_OR_METHOD_TYPE_P (TREE_TYPE (TREE_TYPE (arg)))
	  && TREE_CODE (arg) != INTEGER_CST && !const_route
	  && unnamed_pointer_p (arg, identity))
	forget_all (state);
    }
}

/* A store to LHS, not an SSA name: the object it writes into may change
   -- its predicates, and the written field's range; through a pointer to
   an object no fact names, any object may.  */

static void
invalidate_for_store (tree lhs, function *fun, dom_fact_state &state)
{
  if (TREE_CODE (lhs) == SSA_NAME)
    return;
  tree field = (TREE_CODE (lhs) == COMPONENT_REF
		? TREE_OPERAND (lhs, 1) : NULL_TREE);
  tree base = get_base_address (lhs);
  if (!base)
    return;
  tree id;
  if (DECL_P (base))
    id = base;
  else if (TREE_CODE (base) == MEM_REF)
    {
      tree ptr = TREE_OPERAND (base, 0);
      id = gimple_object_identity (ptr);
      if (unnamed_pointer_p (ptr, id))
	{
	  forget_all (state);
	  return;
	}
    }
  else
    return;
  if (!id)
    return;
  state.pred.remove (id);
  relation_drop (state, id);
  forget_object (state, id, field ? field : NULL_TREE);
  forget_object_and_aliases (state, fun, id);
}

/* The object a scalar in memory, REF, is -- '*p' (P's object) or an
   address-taken variable -- for its facts, keyed (identity, NULL_TREE);
   NULL_TREE if REF isn't one.  */

static tree
scalar_memory_identity (tree ref)
{
  if (!INTEGRAL_TYPE_P (TREE_TYPE (ref)))
    return NULL_TREE;
  if (TREE_CODE (ref) == MEM_REF && integer_zerop (TREE_OPERAND (ref, 1)))
    return gimple_object_identity (TREE_OPERAND (ref, 0));
  if ((VAR_P (ref) || TREE_CODE (ref) == PARM_DECL) && !is_global_var (ref))
    return ref;
  return NULL_TREE;
}

/* STMT loads a scalar from memory: if its facts give it a range, the SSA
   value loaded has it.  */

static void
record_load_range (gimple *stmt, dom_fact_state &state)
{
  tree lhs = gimple_assign_lhs (stmt);
  if (TREE_CODE (lhs) != SSA_NAME || !current_value_facts)
    return;
  tree rhs = gimple_assign_rhs1 (stmt);
  tree id = scalar_memory_identity (rhs);
  tree field = NULL_TREE;
  tree base;
  if (!id && field_shape (lhs, &field, &base) && INTEGRAL_TYPE_P (TREE_TYPE (lhs)))
    id = gimple_object_identity (base);
  if (oa_range_lite *r = id ? state.field.get ({id, field}) : NULL)
    current_value_facts->established_range->put (lhs, *r);
}

/* After a store of RHS to LHS at STMT: a member's range is the stored
   value's.  */

static void
record_field_store (tree lhs, tree rhs, gimple *stmt, dom_fact_state &state)
{
  /* A scalar in memory: its range is the stored value's.  */
  if (tree sid = (current_value_facts ? scalar_memory_identity (lhs)
		  : NULL_TREE))
    {
      oa_range_lite r;
      if (established_range_of (rhs, *current_value_facts->established_range,
				current_value_facts->ranger, stmt, &r))
	state.field.put ({sid, NULL_TREE}, r);
      return;
    }
  if (TREE_CODE (lhs) != COMPONENT_REF || !current_value_facts
      || !(INTEGRAL_TYPE_P (TREE_TYPE (lhs))
	   || SCALAR_FLOAT_TYPE_P (TREE_TYPE (lhs))))
    return;
  tree field = TREE_OPERAND (lhs, 1);
  tree obj = TREE_OPERAND (lhs, 0);
  tree id;
  if (TREE_CODE (obj) == MEM_REF && integer_zerop (TREE_OPERAND (obj, 1)))
    id = gimple_object_identity (TREE_OPERAND (obj, 0));
  else if (VAR_P (obj) || TREE_CODE (obj) == PARM_DECL)
    id = obj;
  else
    return;
  if (!id)
    return;
  if (SCALAR_FLOAT_TYPE_P (TREE_TYPE (lhs)))
    {
      frange fr;
      if (float_range_at (rhs, stmt, current_value_facts->ranger, fr))
	state.ffield.put ({id, field}, fr);
      return;
    }
  oa_range_lite r;
  if (established_range_of (rhs, *current_value_facts->established_range,
			    current_value_facts->ranger, stmt, &r))
    state.field.put ({id, field}, r);
}

/* CALL's own callee's declared postcondition establishing a fact about
   one of CALLEE's own (persistent, non-return-value) parameters -- the
   capability the original prototype's own top comment listed as out of
   scope ("a postcondition establishing a fact about a *persistent
   parameter* ... for a later, separate call site").  Order relative to
   the invalidation step above matters, matching oa_scan_calls_in_expr's
   own "invalidate then establish" discipline: this call's own
   postcondition must win over its own (necessarily stale-by-then)
   invalidation of the same identity.  */

static void
establish_persistent_facts_for_call (gcall *call, dom_fact_state &state)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  for (tree contract : fn_contracts (callee))
    {
      if (!postcondition_establishes_p (contract, callee))
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
	  if (relation_conjunct_p (*conjuncts[i])
	      || !oa_match_predicate_conjunct (*conjuncts[i], &pred_fn,
					       &arg_decl, &negated))
	    continue;

	  unsigned argno;
	  if (!find_param_position (callee, arg_decl, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);
	  tree identity = gimple_object_identity (substituted);
	  bool conveyor = false;
	  postcondition_establishes_p (contract, callee, &conveyor);
	  if (identity)
	    state.pred.get_or_insert (identity).put ({ pred_fn, !negated,
						       conveyor });
	}

      bool conveyor = false;
      postcondition_establishes_p (contract, callee, &conveyor);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree pred_fn;
	  bool negated;
	  auto_vec<tree> args;
	  tree ids[OA_RELATION_MAX_ARGS];
	  if (oa_relation_conjunct_shape (*conjuncts[i], &pred_fn, &args,
					  &negated)
	      && relation_call_identities (call, callee, pred_fn, args,
					   POSTCONDITION_IDENTIFIER (contract),
					   ids))
	    relation_put (state, pred_fn, ids, args.length (), !negated,
			  conveyor);
	}

      auto_vec<field_group_lite> field_groups;
      collect_field_range_groups (cond, &field_groups);
      for (unsigned g = 0; g < field_groups.length (); ++g)
	{
	  unsigned argno;
	  if (!find_param_position (callee, field_groups[g].ptr_expr, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);
	  tree identity = gimple_object_identity (substituted);
	  if (identity)
	    state.field.put ({identity, field_groups[g].field},
			      field_groups[g].range);
	}

      auto_vec<float_field_group> float_groups;
      collect_float_field_groups (cond, &float_groups);
      for (auto &g : float_groups)
	{
	  unsigned argno;
	  if (!find_param_position (callee, g.ptr_expr, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  if (tree id = gimple_object_identity (gimple_call_arg (call, argno)))
	    state.ffield.put ({id, g.field}, g.range);
	}
    }
}

/* A store to LHS: the relations of the value it writes end.  Through a
   pointer to no variable a fact names, it may be any whose address was
   taken.  */

static void
relation_store (tree lhs, dom_fact_state &state)
{
  tree base = get_base_address (lhs);
  if (!base)
    return;
  if (DECL_P (base))
    {
      relation_drop (state, base);
      state.val_src.remove (base);
      return;
    }
  if (TREE_CODE (base) != MEM_REF)
    return;
  tree id = gimple_object_identity (TREE_OPERAND (base, 0));
  if (id && DECL_P (id))
    {
      relation_drop (state, id);
      state.val_src.remove (id);
      return;
    }
  auto_vec<tree, 8> keys;
  for (auto it : state.rel)
    for (unsigned i = 0; i < it.second.n; ++i)
      {
	tree v = it.second.ids[i];
	if (tree b = oa_relation_member_slot_base (v))
	  v = b;
	if (DECL_P (v) && TREE_ADDRESSABLE (v))
	  {
	    keys.safe_push (it.first);
	    break;
	  }
      }
  for (tree k : keys)
    state.rel.remove (k);
}

/* A whole copy of an aggregate value, RHS (a variable or a member of
   one) to variable LHS: the copy has the value's relations.  */

static void
relation_copy (tree lhs, tree rhs, dom_fact_state &state)
{
  if (!(VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL)
      || lhs == rhs || !AGGREGATE_TYPE_P (TREE_TYPE (lhs)))
    return;
  if (TREE_CODE (rhs) == COMPONENT_REF)
    {
      rhs = oa_relation_member_slot (rhs);
      if (!rhs)
	return;
    }
  else if (!(VAR_P (rhs) || TREE_CODE (rhs) == PARM_DECL))
    return;
  tree src_of = rhs;
  if (tree b = oa_relation_member_slot_base (rhs))
    src_of = b;
  if (state.escaped.contains (src_of))
    state.escaped.add (lhs);
  if (tree *src = state.val_src.get (src_of))
    state.val_src.put (lhs, *src);
  auto_vec<relation_fact_lite, 4> copies;
  for (auto it : state.rel)
    {
      relation_fact_lite f = it.second;
      bool hit = false;
      for (unsigned i = 0; i < f.n; ++i)
	if (oa_relation_arg_kind_of (f.pred_fn, i) != OA_RELATION_VALUE)
	  continue;
	else if (f.ids[i] == rhs)
	  {
	    f.ids[i] = lhs;
	    hit = true;
	  }
	else if (oa_relation_member_slot_base (f.ids[i]) == rhs)
	  if (tree t = oa_relation_member_slot_rebase (f.ids[i], lhs))
	    {
	      f.ids[i] = t;
	      hit = true;
	    }
      if (hit)
	copies.safe_push (f);
    }
  for (auto &f : copies)
    relation_put (state, f.pred_fn, f.ids, f.n, f.polarity, f.conveyor);
}

/* The call whose bool result TESTED is, through copies, conversions and
   negations ('!ok (x)' is '_2 = _1 ^ 1'), flipping *TRUTH for each
   negation; NULL if it isn't one.  */

static gcall *
tested_call (tree tested, bool *truth)
{
  for (int depth = 0; depth < 8 && TREE_CODE (tested) == SSA_NAME; ++depth)
    {
      gimple *def = SSA_NAME_DEF_STMT (tested);
      if (gcall *call = def ? dyn_cast <gcall *> (def) : NULL)
	return call;
      if (!def || !is_gimple_assign (def)
	  || TREE_CODE (gimple_assign_rhs1 (def)) != SSA_NAME)
	return NULL;
      tree_code code = gimple_assign_rhs_code (def);
      tree rhs1 = gimple_assign_rhs1 (def);
      bool boolean = TREE_CODE (TREE_TYPE (rhs1)) == BOOLEAN_TYPE;
      if (code == TRUTH_NOT_EXPR
	  || (code == BIT_NOT_EXPR && boolean)
	  || (code == BIT_XOR_EXPR && boolean
	      && integer_onep (gimple_assign_rhs2 (def))))
	*truth = !*truth;
      else if (!(gimple_assign_ssa_name_copy_p (def)
		 || (CONVERT_EXPR_CODE_P (code)
		     && INTEGRAL_TYPE_P (TREE_TYPE (rhs1))
		     && (boolean
			 || TYPE_PRECISION (TREE_TYPE (rhs1))
			    <= TYPE_PRECISION (TREE_TYPE (tested))))))
	return NULL;
      tested = rhs1;
    }
  return NULL;
}

/* The comparison 'LHS CODE RHS' holds (ASSERTED_TRUE) or not on an edge:
   what it establishes, into STATE -- a range of a member or of an
   accessor's result compared with a constant, a value related to an
   accessor call, or to another value.  */

static void
refine_comparison (tree lhs, tree rhs, tree_code code, bool asserted_true,
		   dom_fact_state &state)
{
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR && code != NE_EXPR)
    return;
  /* 'x != C' for a member or an accessor's result: an endpoint of its
     known range excluded.  */
  if ((code == NE_EXPR) == asserted_true)
    {
      tree e = lhs, k = rhs;
      if (TREE_CODE (e) == INTEGER_CST)
	std::swap (e, k);
      tree acc, recv, fld, bas;
      oa_range_lite *r = NULL;
      if (TREE_CODE (k) != INTEGER_CST)
	return;
      if (field_shape (e, &fld, &bas))
	{
	  tree id = gimple_object_identity (bas);
	  r = id ? state.field.get ({id, fld}) : NULL;
	}
      else if (call_shape (e, &acc, &recv))
	{
	  tree id = gimple_object_identity (recv);
	  call_range_fact *f = id ? state.call.get ({id, acc}) : NULL;
	  r = f ? &f->r : NULL;
	}
      if (r)
	{
	  widest_int c = wi::to_widest (k);
	  if (r->has_hi && r->hi == c)
	    r->hi -= 1;
	  else if (r->has_lo && r->lo == c)
	    r->lo += 1;
	}
      return;
    }
  if (code == NE_EXPR)
    code = EQ_EXPR;
  else if (!asserted_true && (code = negated_code (code)) == ERROR_MARK)
    return;
  if (TREE_CODE (lhs) == INTEGER_CST && TREE_CODE (rhs) != INTEGER_CST)
    {
      std::swap (lhs, rhs);
      code = swapped_code (code);
    }
  tree accessor, receiver, field, base;
  /* 'v.size () - x CODE K' or 'x - v.size () CODE K': x related to the
     call, shifted.  An unsigned subtraction can wrap: trusted only with
     the fact that it doesn't ('x <= v.size ()', resp. '>=').  */
  if (TREE_CODE (rhs) == INTEGER_CST && TREE_CODE (lhs) == SSA_NAME)
    {
      gimple *def = SSA_NAME_DEF_STMT (lhs);
      if (def && is_gimple_assign (def)
	  && gimple_assign_rhs_code (def) == MINUS_EXPR
	  && TREE_CODE (gimple_assign_rhs1 (def)) == SSA_NAME
	  && TREE_CODE (gimple_assign_rhs2 (def)) == SSA_NAME)
	{
	  tree a = gimple_assign_rhs1 (def), b = gimple_assign_rhs2 (def);
	  bool call_first = call_shape (a, &accessor, &receiver);
	  tree acc2, recv2;
	  if (call_first != call_shape (b, &acc2, &recv2))
	    {
	      if (!call_first)
		{
		  accessor = acc2;
		  receiver = recv2;
		}
	      tree x = value_key (call_first ? b : a);
	      tree id = gimple_object_identity (receiver);
	      if (!id || call_shape (x, &acc2, &recv2))
		return;
	      if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (lhs)))
		{
		  call_rel_fact *g = state.call_rel.get (x);
		  bool no_wrap
		    = (g && g->receiver == id && g->accessor == accessor
		       && g->offset == 0
		       && (call_first
			   ? g->code == LT_EXPR || g->code == LE_EXPR
			   : g->code == GT_EXPR || g->code == GE_EXPR));
		  if (!no_wrap)
		    return;
		}
	      widest_int k = wi::to_widest (rhs);
	      /* call - x CODE K: (x - -K) CODE' call, CODE' swapped;
		 x - call CODE K: (x - K) CODE call.  */
	      tree_code c = call_first ? swapped_code (code) : code;
	      state.call_rel.put (x, { c, id, accessor,
				       call_first ? -k : k, true });
	      return;
	    }
	}
    }
  if (TREE_CODE (rhs) == INTEGER_CST)
    {
      if (call_shape (lhs, &accessor, &receiver))
	{
	  tree id = gimple_object_identity (receiver);
	  if (!id)
	    return;
	  call_range_fact *old = state.call.get ({id, accessor});
	  call_range_fact f = old ? *old : call_range_fact { {}, true };
	  tighten_range_bound (f.r, code, wi::to_widest (rhs));
	  state.call.put ({id, accessor}, f);
	}
      else if (field_shape (lhs, &field, &base))
	{
	  tree id = gimple_object_identity (base);
	  if (!id)
	    return;
	  oa_range_lite *old = state.field.get ({id, field});
	  oa_range_lite r = old ? *old : oa_range_lite ();
	  tighten_range_bound (r, code, wi::to_widest (rhs));
	  state.field.put ({id, field}, r);
	}
      return;
    }
  if (TREE_CODE (lhs) != SSA_NAME || TREE_CODE (rhs) != SSA_NAME)
    return;
  tree l_acc, l_recv, r_acc, r_recv;
  bool l_call = call_shape (lhs, &l_acc, &l_recv);
  bool r_call = call_shape (rhs, &r_acc, &r_recv);
  if (l_call && r_call)
    return;
  if (l_call || r_call)
    {
      tree value = l_call ? rhs : lhs;
      tree_code c = l_call ? swapped_code (code) : code;
      tree id = gimple_object_identity (l_call ? l_recv : r_recv);
      if (id)
	state.call_rel.put (value_key (value),
			    { c, id, l_call ? l_acc : r_acc, 0, true });
      return;
    }
  state.vrel.put (value_key (lhs), { code, value_key (rhs), true });
  if (code != EQ_EXPR)
    state.vrel.put (value_key (rhs),
		    { swapped_code (code), value_key (lhs), true });
}

/* The facts taking edge E establishes, into STATE: E is a true or false
   edge of a condition testing a value against zero, the result of a call
   to a one-argument predicate that gives the same result again
   (oa_call_repeatable_p) -- it holds of the argument, or not.  */

static void
refine_edge (edge e, dom_fact_state &state)
{
  if (!(e->flags & (EDGE_TRUE_VALUE | EDGE_FALSE_VALUE)))
    return;
  gcond *cond = safe_dyn_cast <gcond *> (*gsi_last_bb (e->src));
  if (!cond)
    return;
  tree_code code = gimple_cond_code (cond);
  tree lhs = gimple_cond_lhs (cond);
  tree rhs = gimple_cond_rhs (cond);
  bool edge_true = (e->flags & EDGE_TRUE_VALUE) != 0;
  if ((code != NE_EXPR && code != EQ_EXPR) || !integer_zerop (rhs)
      || TREE_CODE (lhs) != SSA_NAME)
    {
      refine_comparison (lhs, rhs, code, edge_true, state);
      return;
    }
  bool truth = edge_true == (code == NE_EXPR);
  gcall *call = tested_call (lhs, &truth);
  if (!call)
    {
      /* A comparison computed into the tested value ('_2 = _1 > 3').  */
      gimple *def = SSA_NAME_DEF_STMT (lhs);
      if (def && is_gimple_assign (def)
	  && TREE_CODE_CLASS (gimple_assign_rhs_code (def)) == tcc_comparison)
	refine_comparison (gimple_assign_rhs1 (def), gimple_assign_rhs2 (def),
			   gimple_assign_rhs_code (def), truth, state);
      else
	/* A value compared with zero ('if (x.f == 0)').  */
	refine_comparison (lhs, rhs, code, edge_true, state);
      return;
    }
  tree callee = gimple_call_fndecl (call);
  if (!callee || gimple_call_num_args (call) != 1
      || !oa_call_repeatable_p (callee))
    return;
  tree arg = gimple_call_arg (call, 0);
  tree identity = gimple_object_identity (arg);
  if (!identity && TREE_CODE (arg) == SSA_NAME)
    identity = arg;
  if (identity)
    state.pred.get_or_insert (identity).put ({ callee, truth, true });
}




/* Whether every value of R satisfies 'v CODE C' (true), none does
   (false), or neither is known.  */

static verdict
range_verdict (const oa_range_lite &r, tree_code code, const widest_int &c)
{
  bool all = false, none = false;
  switch (code)
    {
    case LT_EXPR:
      all = r.has_hi && r.hi < c;
      none = r.has_lo && r.lo >= c;
      break;
    case LE_EXPR:
      all = r.has_hi && r.hi <= c;
      none = r.has_lo && r.lo > c;
      break;
    case GT_EXPR:
      all = r.has_lo && r.lo > c;
      none = r.has_hi && r.hi <= c;
      break;
    case GE_EXPR:
      all = r.has_lo && r.lo >= c;
      none = r.has_hi && r.hi < c;
      break;
    case EQ_EXPR:
      all = r.has_lo && r.has_hi && r.lo == c && r.hi == c;
      none = (r.has_hi && r.hi < c) || (r.has_lo && r.lo > c);
      break;
    case NE_EXPR:
      all = (r.has_hi && r.hi < c) || (r.has_lo && r.lo > c);
      none = r.has_lo && r.has_hi && r.lo == c && r.hi == c;
      break;
    default:
      break;
    }
  return all ? VERDICT_TRUE : none ? VERDICT_FALSE : VERDICT_UNKNOWN;
}

/* The value field FIELD of the object pointer PTR designates holds just
   before statement AT: the value the latest store to it stored, found
   walking back through the statements that can't modify it; NULL_TREE if
   unknown.  */

static tree
field_value_at (tree ptr, tree field, gimple *at)
{
  if (TREE_CODE (ptr) != SSA_NAME || !POINTER_TYPE_P (TREE_TYPE (ptr))
      || !(INTEGRAL_TYPE_P (TREE_TYPE (field))
	   || POINTER_TYPE_P (TREE_TYPE (field))))
    return NULL_TREE;
  tree vuse = gimple_vuse (at);
  if (!vuse)
    return NULL_TREE;
  tree obj = build_simple_mem_ref (ptr);
  if (!types_compatible_p (TYPE_MAIN_VARIANT (TREE_TYPE (obj)),
			   TYPE_MAIN_VARIANT (DECL_CONTEXT (field))))
    return NULL_TREE;
  tree ref_tree = build3 (COMPONENT_REF, TREE_TYPE (field), obj, field,
			  NULL_TREE);
  ao_ref ref;
  ao_ref_init (&ref, ref_tree);
  for (int steps = 0; steps < 64 && vuse; ++steps)
    {
      gimple *d = SSA_NAME_DEF_STMT (vuse);
      if (!d || gimple_nop_p (d) || is_a <gphi *> (d))
	return NULL_TREE;
      if (gimple_assign_single_p (d))
	{
	  tree lhs = gimple_assign_lhs (d);
	  if (TREE_CODE (lhs) == COMPONENT_REF
	      && TREE_OPERAND (lhs, 1) == field
	      && TREE_CODE (TREE_OPERAND (lhs, 0)) == MEM_REF
	      && TREE_OPERAND (TREE_OPERAND (lhs, 0), 0) == ptr
	      && integer_zerop (TREE_OPERAND (TREE_OPERAND (lhs, 0), 1)))
	    {
	      if (gimple_clobber_p (d))
		return NULL_TREE;
	      tree rhs = gimple_assign_rhs1 (d);
	      return (TREE_CODE (rhs) == SSA_NAME || CONSTANT_CLASS_P (rhs)
		      ? rhs : NULL_TREE);
	    }
	}
      if (stmt_may_clobber_ref_p_1 (d, &ref, false))
	return NULL_TREE;
      vuse = gimple_vuse (d);
    }
  return NULL_TREE;
}

/* The verdict on conjunct C of a contract_assert or postcondition at its
   check CALL (STRUCT_VAR and DECLS from cg_contract_check_call_public),
   from the facts: value facts and the ranger for the values its
   variables had, STATE for the predicates and relations of the objects
   they designate.  A conveyor check (REQUIRE_CONVEYOR) uses only
   conveyor facts.  */

static verdict
assertion_verdict (tree c, gcall *call, tree struct_var, tree decls,
		   bool require_conveyor, dom_fact_state &state)
{
  value_facts &vf = *current_value_facts;
  auto value_of = [&] (tree decl) -> tree
    {
      decl = tree_strip_any_location_wrapper (decl);
      if (!VAR_P (decl) && TREE_CODE (decl) != PARM_DECL)
	return NULL_TREE;
      return cg_contract_check_decl_value_public (call, struct_var, decls,
						  decl);
    };
  auto scalar_value_of = [&] (tree decl) -> tree
    {
      decl = tree_strip_any_location_wrapper (decl);
      if (TYPE_REF_P (TREE_TYPE (decl)) && !DECL_BY_REFERENCE (decl))
	return NULL_TREE;
      tree v = value_of (decl);
      return v && INTEGRAL_TYPE_P (TREE_TYPE (v)) ? v : NULL_TREE;
    };
  auto range_of = [&] (tree v, oa_range_lite *r)
    {
      return established_range_of (v, *vf.established_range, vf.ranger, call,
				   r);
    };

  tree decl, cst, other;
  tree_code code;
  if (oa_nonzero_conjunct_p (c, &decl))
    {
      tree v = scalar_value_of (decl);
      if (!v)
	return VERDICT_UNKNOWN;
      hash_set<tree> in_progress;
      int_range_max vr;
      if (provable_nonzero_p (v, *vf.established_nz, in_progress)
	  || (trusted_range_at (v, call, vf.ranger, vr)
	      && !vr.contains_p (wi::zero (TYPE_PRECISION (TREE_TYPE (v))))))
	return VERDICT_TRUE;
      oa_range_lite r;
      if (range_of (v, &r))
	return range_verdict (r, NE_EXPR, 0);
      return VERDICT_UNKNOWN;
    }
  if (oa_match_simple_comparison_var_public (c, &decl, &code, &cst)
      && TREE_CODE (cst) == INTEGER_CST)
    {
      tree v = scalar_value_of (decl);
      oa_range_lite r;
      if (v && range_of (v, &r))
	return range_verdict (r, code, wi::to_widest (cst));
      return VERDICT_UNKNOWN;
    }
  /* A floating-point variable compared with a constant: the ranger's
     range at the check.  */
  if (oa_match_simple_comparison_var_public (c, &decl, &code, &cst)
      && TREE_CODE (cst) == REAL_CST)
    {
      decl = tree_strip_any_location_wrapper (decl);
      tree v = (TYPE_REF_P (TREE_TYPE (decl)) && !DECL_BY_REFERENCE (decl)
		? NULL_TREE : value_of (decl));
      return v ? float_verdict (v, code, cst, call, vf.ranger)
	       : VERDICT_UNKNOWN;
    }
  if (oa_match_comparison_against_param (c, &decl, &code, &other, true))
    {
      tree va = scalar_value_of (decl), vb = scalar_value_of (other);
      if (!va || !vb)
	return VERDICT_UNKNOWN;
      for (int side = 0; side < 2; ++side)
	{
	  tree from = side ? vb : va, to = side ? va : vb;
	  tree_code need = side ? swap_tree_comparison (code) : code;
	  tree_code fact_code;
	  tree fact_rhs;
	  if (get_relational (from, *vf.established_rel, &fact_code, &fact_rhs)
	      && fact_rhs == to && oa_relational_code_implies (fact_code, need))
	    return VERDICT_TRUE;
	}
      oa_range_lite ra, rb;
      if (range_of (va, &ra) && range_of (vb, &rb)
	  && ra.has_lo && ra.has_hi && rb.has_lo && rb.has_hi)
	{
	  /* Every pair of values, from the ranges' corners.  */
	  verdict lo = range_verdict (ra, code, rb.lo);
	  verdict hi = range_verdict (ra, code, rb.hi);
	  if (lo == hi && lo != VERDICT_UNKNOWN)
	    return lo;
	}
      return VERDICT_UNKNOWN;
    }
  tree field, ptr_expr;
  if (oa_match_field_range_comparison (c, &field, &ptr_expr, &code, &cst)
      && TREE_CODE (cst) == REAL_CST)
    {
      tree base = oa_strip_symbolic_ptr_expr_public (ptr_expr);
      tree p = (VAR_P (base) || TREE_CODE (base) == PARM_DECL
		? value_of (base) : NULL_TREE);
      tree id = p ? gimple_object_identity (p) : NULL_TREE;
      frange *have = id ? state.ffield.get ({id, field}) : NULL;
      return have ? frange_verdict (*have, code, cst) : VERDICT_UNKNOWN;
    }
  /* A member compared with a constant ('v > 0'): the value last stored
     to it, else what the facts say of the object's field.  */
  if (oa_match_field_range_comparison (c, &field, &ptr_expr, &code, &cst)
      && TREE_CODE (cst) == INTEGER_CST)
    {
      tree base = oa_strip_symbolic_ptr_expr_public (ptr_expr);
      tree p = (VAR_P (base) || TREE_CODE (base) == PARM_DECL
		? value_of (base) : NULL_TREE);

      if (!p)
	return VERDICT_UNKNOWN;
      oa_range_lite r;
      if (tree v = field_value_at (p, field, call))
	if (range_of (v, &r))
	  return range_verdict (r, code, wi::to_widest (cst));
      if (tree id = gimple_object_identity (p))
	if (oa_range_lite *f = state.field.get ({id, field}))
	  return range_verdict (*f, code, wi::to_widest (cst));
      return VERDICT_UNKNOWN;
    }
  tree pred_fn, pred_arg;
  bool negated;
  auto_vec<tree> args;
  if (oa_relation_conjunct_shape (c, &pred_fn, &args, &negated))
    {
      tree ids[OA_RELATION_MAX_ARGS];
      for (unsigned i = 0; i < args.length (); ++i)
	{
	  tree a = oa_relation_strip_arg (args[i]);
	  if (TREE_CODE (a) == INTEGER_CST)
	    ids[i] = a;
	  else
	    {
	      tree v = value_of (a);
	      ids[i] = v ? relation_operand_identity (pred_fn, i, v) : NULL_TREE;
	    }
	  if (!ids[i])
	    return VERDICT_UNKNOWN;
	}
      relation_fact_lite *f
	= state.rel.get (relation_key (pred_fn, ids, args.length ()));
      if (!f || (require_conveyor && !f->conveyor))
	return VERDICT_UNKNOWN;
      return f->polarity == !negated ? VERDICT_TRUE : VERDICT_FALSE;
    }
  if (oa_match_predicate_conjunct (c, &pred_fn, &pred_arg, &negated))
    {
      tree v = value_of (pred_arg);
      tree id = v ? gimple_object_identity (v) : NULL_TREE;
      if (!id && v && TREE_CODE (v) == SSA_NAME)
	id = scalar_value_origin (v);
      pred_facts *facts = id ? state.pred.get (id) : NULL;
      pred_fact *f = facts ? facts->find (pred_fn) : NULL;
      if (!f || (require_conveyor && !f->conveyor))
	return VERDICT_UNKNOWN;
      return f->polarity == !negated ? VERDICT_TRUE : VERDICT_FALSE;
    }
  return VERDICT_UNKNOWN;
}

/* CALL, if a conveyor or symbolic contract_assert's or postcondition's
   check this plugin makes (the flavors the proof flags ask for, or a
   control object forcing analysis; not a never-proven one): each
   conjunct but is_object_address (mandatory, the engine's) is checked
   against the facts -- provably false is an error, unverified a warning
   (an error if strict).  A symbolic postcondition's symbolic predicates
   and relations are its trusted axioms.  */

static void
check_assertion (gcall *call, dom_fact_state &state)
{
  tree struct_var, cond, decls;
  tree contract = cg_contract_check_call_public (call, &struct_var, &cond,
						 &decls);
  if (!contract || cond == NULL_TREE || cond == error_mark_node)
    return;
  bool is_post = POSTCONDITION_P (contract);
  if (TREE_CODE (contract) != ASSERTION_STMT && !is_post)
    return;
  tree owner = DECL_ORIGIN (current_function_decl);
  bool conveyor_ok = oa_contract_conveyor_active_public (contract, owner);
  bool symbolic_ok = oa_contract_symbolic_active_public (contract, owner);
  if ((!conveyor_ok && !symbolic_ok)
      || oa_contract_never_proven_public (contract, owner))
    return;

  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  for (int flavor = 0; flavor < 2; ++flavor)
    {
      bool conveyor = flavor == 0;
      bool analysis
	= (conveyor
	   ? conveyor_ok
	     && (contract_analysis_plugin_proofs_p (true)
		 || oa_contract_conveyor_analysis_forced_public (contract, owner))
	   : symbolic_ok
	     && (contract_analysis_plugin_proofs_p (false)
		 || oa_contract_symbolic_analysis_forced_public (contract,
								 owner)));
      if (!analysis)
	continue;
      bool strict = (conveyor
		     ? oa_contract_conveyor_strict_public (contract, owner)
		     : oa_contract_symbolic_strict_public (contract, owner));
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree c = *conjuncts[i];
	  tree t;
	  if (is_object_address_call_p (c, &t))
	    continue;
	  tree pred_fn, pred_arg;
	  bool negated;
	  if (is_post && !conveyor
	      && ((oa_match_predicate_conjunct (c, &pred_fn, &pred_arg,
						&negated)
		   && DECL_DECLARED_SYMBOLIC_P (pred_fn))
		  || relation_conjunct_p (c)))
	    continue;
	  verdict v = assertion_verdict (c, call, struct_var, decls, conveyor,
					 state);
	  location_t loc = EXPR_LOCATION (c);
	  if (loc == UNKNOWN_LOCATION)
	    loc = gimple_location (call);
	  if (v == VERDICT_FALSE)
	    {
	      if (is_post)
		error_at (loc, "postcondition condition %qE is provably false",
			  c);
	      else
		error_at (loc, "%<contract_assert%> condition %qE is provably "
			  "false", c);
	    }
	  else if (v == VERDICT_UNKNOWN)
	    {
	      if (is_post)
		emit_diagnostic (strict ? diagnostics::kind::error
					: diagnostics::kind::warning,
				 loc, 0, strict
				 ? "cannot prove postcondition condition %qE"
				 : "cannot verify postcondition condition %qE",
				 c);
	      else
		emit_diagnostic (strict ? diagnostics::kind::error
					: diagnostics::kind::warning,
				 loc, 0, strict
				 ? "cannot prove %<contract_assert%> condition %qE"
				 : "cannot verify %<contract_assert%> condition "
				   "%qE", c);
	    }
	}
    }
}

/* The persistent-memory facts are a forward must-analysis: what holds
   on entry to a block is what holds on every edge into it -- the meet of
   its predecessors' exit facts, each refined by what taking its edge
   establishes -- iterated in reverse postorder to a fixed point (a
   loop's facts start optimistic and only shrink).  Only the last pass,
   over the fixed entry facts, reports.  */

/* Copy FROM into TO.  */

static void
copy_state (dom_fact_state &to, const dom_fact_state &from)
{
  to.pred.empty ();
  to.field.empty ();
  to.rel.empty ();
  for (auto it : from.pred)
    to.pred.put (it.first, it.second);
  for (auto it : from.field)
    to.field.put (it.first, it.second);
  for (auto it : from.rel)
    to.rel.put (it.first, it.second);
  to.escaped.empty ();
  for (tree t : from.escaped)
    to.escaped.add (t);
  to.val_src.empty ();
  for (auto it : from.val_src)
    to.val_src.put (it.first, it.second);
  to.call.empty ();
  for (auto it : from.call)
    to.call.put (it.first, it.second);
  to.call_rel.empty ();
  for (auto it : from.call_rel)
    to.call_rel.put (it.first, it.second);
  to.vrel.empty ();
  for (auto it : from.vrel)
    to.vrel.put (it.first, it.second);
  to.ffield.empty ();
  for (auto it : from.ffield)
    to.ffield.put (it.first, it.second);
}

/* Meet B into A: what holds in both.  A field's ranges join to the
   interval covering both; a fact established in the conveyor flavor on
   one side only is no longer one.  */

static void
meet_state (dom_fact_state &a, const dom_fact_state &b)
{
  auto_vec<tree, 8> drop;
  for (auto it : a.pred)
    {
      const pred_facts *other
	= const_cast<hash_map<tree, pred_facts> &> (b.pred).get (it.first);
      pred_facts kept;
      if (other)
	for (unsigned i = 0; i < it.second.n; ++i)
	  {
	    pred_fact f = it.second.f[i];
	    pred_fact *g = const_cast<pred_facts *> (other)->find (f.pred_fn);
	    if (g && g->polarity == f.polarity)
	      {
		f.conveyor &= g->conveyor;
		kept.put (f);
	      }
	  }
      if (kept.n)
	it.second = kept;
      else
	drop.safe_push (it.first);
    }
  for (tree k : drop)
    a.pred.remove (k);

  auto_vec<std::pair<tree, tree>, 8> fdrop;
  for (auto it : a.field)
    {
      const oa_range_lite *o
	= const_cast<hash_map<oa_field_key_hash, oa_range_lite> &>
	    (b.field).get (it.first);
      if (!o)
	{
	  fdrop.safe_push (it.first);
	  continue;
	}
      oa_range_lite &r = it.second;
      r.has_lo = r.has_lo && o->has_lo;
      if (r.has_lo)
	r.lo = wi::smin (r.lo, o->lo);
      r.has_hi = r.has_hi && o->has_hi;
      if (r.has_hi)
	r.hi = wi::smax (r.hi, o->hi);
      if (!r.has_lo && !r.has_hi)
	fdrop.safe_push (it.first);
    }
  for (auto &k : fdrop)
    a.field.remove (k);

  drop.truncate (0);
  for (auto it : a.rel)
    {
      const relation_fact_lite *o
	= const_cast<hash_map<tree, relation_fact_lite> &> (b.rel)
	    .get (it.first);
      if (!o || o->polarity != it.second.polarity)
	drop.safe_push (it.first);
      else
	it.second.conveyor &= o->conveyor;
    }
  for (tree k : drop)
    a.rel.remove (k);

  for (tree t : const_cast<hash_set<tree> &> (b.escaped))
    a.escaped.add (t);
  drop.truncate (0);
  for (auto it : a.val_src)
    {
      const tree *o = const_cast<hash_map<tree, tree> &> (b.val_src)
			.get (it.first);
      if (!o)
	{
	  drop.safe_push (it.first);
	  continue;
	}
      for (tree l = *o; l; l = TREE_CHAIN (l))
	if (!purpose_member (TREE_PURPOSE (l), it.second))
	  it.second = tree_cons (TREE_PURPOSE (l), NULL_TREE, it.second);
    }
  for (tree k : drop)
    a.val_src.remove (k);

  fdrop.truncate (0);
  for (auto it : a.call)
    {
      const call_range_fact *o
	= const_cast<hash_map<oa_field_key_hash, call_range_fact> &>
	    (b.call).get (it.first);
      oa_range_lite &r = it.second.r;
      if (!o)
	{
	  fdrop.safe_push (it.first);
	  continue;
	}
      r.has_lo = r.has_lo && o->r.has_lo;
      if (r.has_lo)
	r.lo = wi::smin (r.lo, o->r.lo);
      r.has_hi = r.has_hi && o->r.has_hi;
      if (r.has_hi)
	r.hi = wi::smax (r.hi, o->r.hi);
      it.second.conveyor &= o->conveyor;
      if (!r.has_lo && !r.has_hi)
	fdrop.safe_push (it.first);
    }
  for (auto &k : fdrop)
    a.call.remove (k);
  drop.truncate (0);
  for (auto it : a.call_rel)
    {
      const call_rel_fact *o
	= const_cast<hash_map<tree, call_rel_fact> &> (b.call_rel)
	    .get (it.first);
      if (!o || o->code != it.second.code || o->receiver != it.second.receiver
	  || o->accessor != it.second.accessor || o->offset != it.second.offset)
	drop.safe_push (it.first);
      else
	it.second.conveyor &= o->conveyor;
    }
  for (tree k : drop)
    a.call_rel.remove (k);
  drop.truncate (0);
  for (auto it : a.vrel)
    {
      const value_rel_fact *o
	= const_cast<hash_map<tree, value_rel_fact> &> (b.vrel).get (it.first);
      if (!o || o->code != it.second.code || o->other != it.second.other)
	drop.safe_push (it.first);
      else
	it.second.conveyor &= o->conveyor;
    }
  for (tree k : drop)
    a.vrel.remove (k);
  fdrop.truncate (0);
  for (auto it : a.ffield)
    {
      const frange *o = const_cast<hash_map<oa_field_key_hash, frange> &>
			  (b.ffield).get (it.first);
      if (!o)
	fdrop.safe_push (it.first);
      else
	{
	  it.second.union_ (*o);
	  if (it.second.varying_p ())
	    fdrop.safe_push (it.first);
	}
    }
  for (auto &k : fdrop)
    a.ffield.remove (k);
}

/* Whether A and B hold the same facts.  */

static bool
same_state (const dom_fact_state &a, const dom_fact_state &b)
{
  auto &bp = const_cast<hash_map<tree, pred_facts> &> (b.pred);
  auto &bf = const_cast<hash_map<oa_field_key_hash, oa_range_lite> &> (b.field);
  auto &br = const_cast<hash_map<tree, relation_fact_lite> &> (b.rel);
  if (a.pred.elements () != b.pred.elements ()
      || a.field.elements () != b.field.elements ()
      || a.rel.elements () != b.rel.elements ())
    return false;
  for (auto it : a.pred)
    {
      pred_facts *o = bp.get (it.first);
      if (!o || o->n != it.second.n)
	return false;
      for (unsigned i = 0; i < it.second.n; ++i)
	{
	  pred_fact *g = o->find (it.second.f[i].pred_fn);
	  if (!g || g->polarity != it.second.f[i].polarity
	      || g->conveyor != it.second.f[i].conveyor)
	    return false;
	}
    }
  for (auto it : a.field)
    {
      oa_range_lite *o = bf.get (it.first);
      if (!o || o->has_lo != it.second.has_lo || o->has_hi != it.second.has_hi
	  || (o->has_lo && o->lo != it.second.lo)
	  || (o->has_hi && o->hi != it.second.hi))
	return false;
    }
  for (auto it : a.rel)
    {
      relation_fact_lite *o = br.get (it.first);
      if (!o || o->polarity != it.second.polarity
	  || o->conveyor != it.second.conveyor)
	return false;
    }
  auto &be = const_cast<hash_set<tree> &> (b.escaped);
  auto &bv = const_cast<hash_map<tree, tree> &> (b.val_src);
  if (a.escaped.elements () != b.escaped.elements ()
      || a.val_src.elements () != b.val_src.elements ())
    return false;
  for (tree t : a.escaped)
    if (!be.contains (t))
      return false;
  auto &bc = const_cast<hash_map<oa_field_key_hash, call_range_fact> &>
	       (b.call);
  auto &bcr = const_cast<hash_map<tree, call_rel_fact> &> (b.call_rel);
  auto &bvr = const_cast<hash_map<tree, value_rel_fact> &> (b.vrel);
  auto &bff = const_cast<hash_map<oa_field_key_hash, frange> &> (b.ffield);
  if (a.ffield.elements () != b.ffield.elements ())
    return false;
  for (auto it : a.ffield)
    {
      frange *o = bff.get (it.first);
      if (!o || !(*o == it.second))
	return false;
    }
  if (a.call.elements () != b.call.elements ()
      || a.call_rel.elements () != b.call_rel.elements ()
      || a.vrel.elements () != b.vrel.elements ())
    return false;
  for (auto it : a.call)
    {
      call_range_fact *o = bc.get (it.first);
      const oa_range_lite &r = it.second.r;
      if (!o || o->conveyor != it.second.conveyor
	  || o->r.has_lo != r.has_lo || o->r.has_hi != r.has_hi
	  || (r.has_lo && o->r.lo != r.lo) || (r.has_hi && o->r.hi != r.hi))
	return false;
    }
  for (auto it : a.call_rel)
    {
      call_rel_fact *o = bcr.get (it.first);
      if (!o || o->code != it.second.code || o->receiver != it.second.receiver
	  || o->accessor != it.second.accessor || o->offset != it.second.offset
	  || o->conveyor != it.second.conveyor)
	return false;
    }
  for (auto it : a.vrel)
    {
      value_rel_fact *o = bvr.get (it.first);
      if (!o || o->code != it.second.code || o->other != it.second.other
	  || o->conveyor != it.second.conveyor)
	return false;
    }
  for (auto it : a.val_src)
    {
      tree *o = bv.get (it.first);
      if (!o || list_length (*o) != list_length (it.second))
	return false;
      for (tree l = it.second; l; l = TREE_CHAIN (l))
	if (!purpose_member (TREE_PURPOSE (l), *o))
	  return false;
    }
  return true;
}

/* Whether a call given the route objects ROUTES (any route if ANY_ROUTE)
   may end SOURCE, an object a value was obtained from: one of ROUTES, a
   local whose address was handed out, or, for a pointer value, what it
   was obtained from -- a call's routes, the object a load reads, a PHI's
   values -- transitively; obtained from nothing nameable, anything.  */

static bool
source_ended_p (tree source, const vec<tree> &routes, bool any_route,
		dom_fact_state &state, int depth = 0)
{
  if (!source || depth > 4)
    return false;
  for (tree r : routes)
    if (r == source || operand_equal_p (r, source, 0))
      return true;
  if (DECL_P (source) && state.escaped.contains (source))
    return true;
  if (TREE_CODE (source) != SSA_NAME || !POINTER_TYPE_P (TREE_TYPE (source))
      || SSA_NAME_IS_DEFAULT_DEF (source))
    return false;
  tree id = gimple_object_identity (source);
  if (id && id != source)
    return source_ended_p (id, routes, any_route, state, depth + 1);
  auto_vec<tree, 4> sources;
  gimple *def = SSA_NAME_DEF_STMT (source);
  if (gcall *call = dyn_cast <gcall *> (def))
    {
      for (unsigned i = 0; i < gimple_call_num_args (call); ++i)
	{
	  tree arg = gimple_call_arg (call, i);
	  if (POINTER_TYPE_P (TREE_TYPE (arg)) && TREE_CODE (arg) != INTEGER_CST)
	    if (tree r = gimple_object_identity (arg))
	      sources.safe_push (r);
	}
    }
  else if (gphi *phi = dyn_cast <gphi *> (def))
    {
      for (unsigned i = 0; i < gimple_phi_num_args (phi); ++i)
	if (TREE_CODE (gimple_phi_arg_def (phi, i)) == SSA_NAME)
	  sources.safe_push (gimple_phi_arg_def (phi, i));
    }
  else if (gimple_assign_load_p (def))
    {
      tree base = get_base_address (gimple_assign_rhs1 (def));
      if (base && TREE_CODE (base) == MEM_REF)
	base = gimple_object_identity (TREE_OPERAND (base, 0));
      if (base && (DECL_P (base) || TREE_CODE (base) == SSA_NAME))
	sources.safe_push (base);
    }
  if (sources.is_empty ())
    return any_route;
  for (tree src : sources)
    if (source_ended_p (src, routes, any_route, state, depth + 1))
      return true;
  return false;
}

/* Whether such a call may end the value ID, a relation's argument.  */

static bool
value_ended_p (tree id, const vec<tree> &routes, bool any_route,
	       dom_fact_state &state)
{
  if (TREE_CODE (id) == INTEGER_CST)
    return false;
  if (tree base = oa_relation_member_slot_base (id))
    id = base;
  if (TREE_CODE (id) == SSA_NAME)
    {
      if (!POINTER_TYPE_P (TREE_TYPE (id)))
	return false;
      if (SSA_NAME_IS_DEFAULT_DEF (id))
	return any_route;
      return source_ended_p (id, routes, any_route, state);
    }
  if (state.escaped.contains (id))
    return true;
  tree *src = state.val_src.get (id);
  if (!src)
    return any_route;
  for (tree l = *src; l; l = TREE_CHAIN (l))
    if (source_ended_p (TREE_PURPOSE (l), routes, any_route, state))
      return true;
  return false;
}

/* What CALL, which may end lifetimes, ends besides what it is given: the
   facts about the locals whose address was handed out before, and the
   relations of the values it may end.  Then the locals it is given the
   address of are handed out, and a value its result initializes was
   obtained from what it is given.  */

static void
process_lifetime_call (gcall *call, dom_fact_state &state)
{
  auto_vec<tree, 8> routes;
  bool any_route = false;
  for (unsigned i = 0; i < gimple_call_num_args (call); ++i)
    {
      tree arg = gimple_call_arg (call, i);
      if (!POINTER_TYPE_P (TREE_TYPE (arg)) || TREE_CODE (arg) == INTEGER_CST)
	continue;
      any_route = true;
      if (TREE_CODE (arg) == SSA_NAME)
	routes.safe_push (arg);
      if (tree id = gimple_object_identity (arg))
	routes.safe_push (id);
    }

  auto_vec<tree, 8> esc;
  for (tree t : state.escaped)
    esc.safe_push (t);
  for (tree t : esc)
    forget_object (state, t);

  auto_vec<tree, 8> ended;
  for (auto it : state.rel)
    for (unsigned i = 0; i < it.second.n; ++i)
      if (oa_relation_arg_kind_of (it.second.pred_fn, i) == OA_RELATION_VALUE
	  && value_ended_p (it.second.ids[i], routes, any_route, state))
	{
	  ended.safe_push (it.first);
	  break;
	}
  for (tree k : ended)
    state.rel.remove (k);

  for (unsigned i = 0; i < gimple_call_num_args (call); ++i)
    {
      tree id = gimple_object_identity (gimple_call_arg (call, i));
      if (id && DECL_P (id) && !is_global_var (id))
	state.escaped.add (id);
    }
  tree lhs = gimple_call_lhs (call);
  if (lhs && (VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL))
    {
      if (routes.is_empty ())
	state.val_src.remove (lhs);
      else
	{
	  tree list = NULL_TREE;
	  for (tree r : routes)
	    if (!purpose_member (r, list))
	      list = tree_cons (r, NULL_TREE, list);
	  state.val_src.put (lhs, list);
	}
    }
}

static void check_call (gcall *, hash_set<tree> &, hash_set<tree> &,
			hash_map<tree, oa_range_lite> &,
			hash_map<tree, rel_fact_lite> &, gimple_ranger *);

/* BB's statements, from the facts on entry in STATE to those on exit:
   stores end the relations of what they write, a copy of a value takes
   its relations; at a call, its callee's preconditions are checked
   against the facts before it (if REPORT), then the facts about what it
   is given end, then its postconditions establish theirs.  */

static void
transfer_block (basic_block bb, function *fun, dom_fact_state &state,
		bool report)
{
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (gimple_assign_single_p (stmt) || is_gimple_call (stmt))
	if (tree lhs = gimple_get_lhs (stmt))
	  {
	    invalidate_for_store (lhs, fun, state);
	    relation_store (lhs, state);
	    if (gimple_assign_single_p (stmt))
	      {
		relation_copy (lhs, gimple_assign_rhs1 (stmt), state);
		record_field_store (lhs, gimple_assign_rhs1 (stmt), stmt,
				    state);
	      }
	  }
      /* A load of a scalar from memory with a known range: the value
	 loaded has it.  Recorded in the reporting pass only, whose facts
	 are the fixed point's; the walk's order puts it before the uses.  */
      if (report && gimple_assign_load_p (stmt))
	record_load_range (stmt, state);
      gcall *call = dyn_cast <gcall *> (stmt);
      if (!call)
	continue;
      if (report)
	{
	  value_facts &vf = *current_value_facts;
	  check_call (call, *vf.established, *vf.established_nz,
		      *vf.established_range, *vf.established_rel, vf.ranger);
	  consult_persistent_facts (call, state);
	  check_assertion (call, state);
	}
      if (call_may_end_lifetimes_p (call))
	process_lifetime_call (call, state);
      invalidate_persistent_facts_for_call_args (call, fun, state);
      establish_persistent_facts_for_call (call, state);
    }
}

/* The facts on entry to BB, into STATE, from the exit facts OUT of its
   predecessors computed so far (SEED for the function's entry); false if
   none of them is computed yet.  */

static bool
entry_state (basic_block bb, function *fun, const dom_fact_state &seed,
	     hash_map<basic_block, dom_fact_state *> &out,
	     dom_fact_state &state)
{
  bool any = false;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      dom_fact_state from;
      if (e->src == ENTRY_BLOCK_PTR_FOR_FN (fun))
	copy_state (from, seed);
      else if (dom_fact_state **o = out.get (e->src))
	copy_state (from, **o);
      else
	continue;
      refine_edge (e, from);
      if (!any)
	copy_state (state, from);
      else
	meet_state (state, from);
      any = true;
    }
  return any;
}

/* The fixed point, then the reporting pass.  */

static void
persistent_facts_walk (function *fun, const dom_fact_state &seed)
{
  int *rpo = XNEWVEC (int, n_basic_blocks_for_fn (fun));
  int n = pre_and_rev_post_order_compute_fn (fun, NULL, rpo, false);
  hash_map<basic_block, dom_fact_state *> out;
  bool changed = true;
  for (int round = 0; changed && round < 32; ++round)
    {
      changed = false;
      for (int i = 0; i < n; ++i)
	{
	  basic_block bb = BASIC_BLOCK_FOR_FN (fun, rpo[i]);
	  dom_fact_state state;
	  if (!entry_state (bb, fun, seed, out, state))
	    continue;
	  transfer_block (bb, fun, state, false);
	  dom_fact_state **o = out.get (bb);
	  if (o && same_state (**o, state))
	    continue;
	  if (!o)
	    out.put (bb, new dom_fact_state);
	  copy_state (**out.get (bb), state);
	  changed = true;
	}
    }
  /* Not settled: no facts are trusted rather than ones that might not
     hold.  */
  dom_fact_state none;
  for (int i = 0; i < n; ++i)
    {
      basic_block bb = BASIC_BLOCK_FOR_FN (fun, rpo[i]);
      dom_fact_state state;
      if (changed || !entry_state (bb, fun, seed, out, state))
	copy_state (state, none);
      transfer_block (bb, fun, state, true);
    }
  /* Blocks the entry doesn't reach: checked too, with no facts.  */
  auto_sbitmap reached (last_basic_block_for_fn (fun));
  bitmap_clear (reached);
  for (int i = 0; i < n; ++i)
    bitmap_set_bit (reached, rpo[i]);
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    if (!bitmap_bit_p (reached, bb->index))
      {
	dom_fact_state state;
	transfer_block (bb, fun, state, true);
      }
  for (auto it : out)
    delete it.second;
  XDELETEVEC (rpo);
}

/* Is VAL (a real GIMPLE operand: an SSA_NAME, an invariant address, or
   a constant) provably an object address, given ESTABLISHED (the SSA
   names this function's own declared precondition already trusts, via
   IN_PROGRESS's own function seed_self_trust below)?  IN_PROGRESS
   guards against infinite recursion on a loop-carried PHI -- revisiting
   an SSA name already being resolved is conservatively treated as
   "not (yet) provable," the same "must be provable, else treated as
   unprovable" discipline the AST-walk uses throughout, here falling
   out for free from the cycle guard rather than a hand-written
   loop-header merge rule.  */

static bool
provable_object_address_p (tree val, hash_set<tree> &established,
			    hash_set<tree> &in_progress)
{
  if (val == NULL_TREE)
    return false;

  /* Trivial case: taking the address of any decl is definitionally an
     object address, regardless of any tracked fact -- mirrors
     oa_provable_p's own ADDR_EXPR short-circuit.  */
  if (TREE_CODE (val) == ADDR_EXPR)
    return true;

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  if (established.contains (val))
    return true;

  if (in_progress.contains (val))
    return false;

  in_progress.add (val);
  bool result = false;

  gimple *def = SSA_NAME_DEF_STMT (val);
  if (def && gimple_code (def) == GIMPLE_PHI)
    {
      /* A PHI node's own operands ARE the "every incoming value must
	 satisfy it" merge oa_env::merge_with otherwise hand-rolls --
	 reified as a real node, not a parallel data structure kept in
	 sync by hand.  */
      result = true;
      unsigned n = gimple_phi_num_args (def);
      for (unsigned i = 0; i < n; ++i)
	if (!provable_object_address_p (gimple_phi_arg_def (def, i),
					 established, in_progress))
	  {
	    result = false;
	    break;
	  }
    }
  else if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if (code == ADDR_EXPR)
	result = true;
      else if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	/* A plain copy or a value-preserving conversion: whatever's
	   true of the RHS is true here too.  */
	result = provable_object_address_p (gimple_assign_rhs1 (def),
					     established, in_progress);
    }
  else if (def && is_gimple_call (def))
    {
      /* Item 6: VAL is a call's own return value -- if the (direct)
	 callee's own declared postcondition unconditionally guarantees
	 it, that's a fact about VAL regardless of anything else.  Only
	 ever consults CALLEE's *declared* text (see the function above),
	 never CALLEE's own GIMPLE body -- so this works even if CALLEE
	 hasn't been visited by this pass yet at all.  */
      tree callee = gimple_call_fndecl (as_a <gcall *> (def));
      if (callee)
	result = call_postcondition_guarantees_object_address_p (callee);
    }

  in_progress.remove (val);
  return result;
}

/* Nonzero-ness's own counterpart of provable_object_address_p
   immediately above -- same three-part structure (trivial constant
   case, established-fact lookup, PHI-merge, copy/conversion
   propagation, item 6's own call-return-value guarantee), just for a
   different fact.  ESTABLISHED_NZ is a wholly separate hash_set from
   is_object_address's own ESTABLISHED (matching contracts.cc's own
   m_map/m_nz_map being two separate maps for two separate facts).  */

static bool
provable_nonzero_p (tree val, hash_set<tree> &established_nz,
		     hash_set<tree> &in_progress)
{
  if (val == NULL_TREE)
    return false;

  if (TREE_CODE (val) == INTEGER_CST)
    return !integer_zerop (val);

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  if (established_nz.contains (val))
    return true;

  if (in_progress.contains (val))
    return false;

  in_progress.add (val);
  bool result = false;

  gimple *def = SSA_NAME_DEF_STMT (val);
  if (def && gimple_code (def) == GIMPLE_PHI)
    {
      result = true;
      unsigned n = gimple_phi_num_args (def);
      for (unsigned i = 0; i < n; ++i)
	if (!provable_nonzero_p (gimple_phi_arg_def (def, i),
				  established_nz, in_progress))
	  {
	    result = false;
	    break;
	  }
    }
  else if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	result = provable_nonzero_p (gimple_assign_rhs1 (def),
				      established_nz, in_progress);
    }
  else if (def && is_gimple_call (def))
    {
      tree callee = gimple_call_fndecl (as_a <gcall *> (def));
      if (callee)
	result = call_postcondition_guarantees_nonzero_p (callee);
    }

  in_progress.remove (val);
  return result;
}

/* A sixth fact shape: a *relational* fact between two of the same
   function's own parameters ("x < q", q not a literal) -- see
   oa_match_comparison_against_param's own comment in contracts.cc for
   the full rationale (this recognizes the shape; neither PARAM nor
   OTHER is ever resolved to a value). Flavor-agnostic like every other
   fact this prototype tracks (no conveyor/symbolic distinction at
   all), so no provenance tag is needed here, unlike contracts.cc's own
   oa_relational_fact or contracts-gimple.cc's own cg_rel_fact.  */


/* Item 6 for relational facts: does CALLEE's own declared postcondition
   guarantee a relation between its own return value and one of its
   OTHER parameters (e.g. 'post<ctrl>(r: r < q)'), via the shared
   oa_match_result_relation?  Mirrors call_postcondition_range_p's own
   structure exactly, one shape over.  */

static bool
call_postcondition_relation_p (tree callee, gcall *call, tree_code *code_out,
				tree *rhs_out)
{
  for (tree contract : fn_contracts (callee))
    {
      if (!postcondition_establishes_p (contract, callee))
	continue;
      tree result_id = POSTCONDITION_IDENTIFIER (contract);
      if (!result_id)
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree_code code;
	  tree other_param;
	  if (!oa_match_result_relation (*conjuncts[i], result_id, &code,
					  &other_param))
	    continue;

	  unsigned argno;
	  if (!find_param_position (callee, other_param, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;

	  *code_out = code;
	  *rhs_out = gimple_call_arg (call, argno);
	  return true;
	}
    }
  return false;
}

/* VAL's own established relational fact, if any -- tries, in order: a
   self-trusted fact in ESTABLISHED_REL (keyed exactly like ESTABLISHED/
   ESTABLISHED_NZ elsewhere in this file); a copy/conversion one hop
   back (the same shape established_range_of already handles); and
   item 6 via a GIMPLE_CALL def-stmt (call_postcondition_relation_p
   immediately above).  Mirrors established_range_of's own recursive
   structure, one shape over.  */

static bool
get_relational (tree val, hash_map<tree, rel_fact_lite> &established_rel,
		 tree_code *code_out, tree *rhs_out)
{
  if (val == NULL_TREE || TREE_CODE (val) != SSA_NAME)
    return false;

  if (rel_fact_lite *found = established_rel.get (val))
    {
      *code_out = found->code;
      *rhs_out = found->rhs;
      return true;
    }

  gimple *def = SSA_NAME_DEF_STMT (val);
  if (def && is_gimple_call (def))
    {
      tree callee = gimple_call_fndecl (as_a <gcall *> (def));
      if (callee
	  && call_postcondition_relation_p (callee, as_a <gcall *> (def),
					     code_out, rhs_out))
	return true;
    }
  else if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if ((CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	  && get_relational (gimple_assign_rhs1 (def), established_rel,
			      code_out, rhs_out))
	return true;
    }

  return false;
}

/* Seed ESTABLISHED/ESTABLISHED_NZ from FNDECL's own declared
   precondition: an is_object_address(p)/'p != 0'-shaped conjunct
   naming one of FNDECL's own parameters is trusted as an axiom for the
   rest of FNDECL's own body (self-trust) -- the GIMPLE-level analogue
   of oa_handle_precondition_stmt's own fact-seeding, just keyed by SSA
   name (ssa_default_def)
   instead of a raw PARM_DECL in a hand-rolled map.  Reads FNDECL's
   *declared* condition tree directly (get_fn_contract_specifiers/
   CONTRACT_CONDITION) -- never FNDECL.pre's own outlined GIMPLE body,
   which is the whole point of this design (see this file's own top
   comment).  */

static void
seed_self_trust (function *fun, hash_set<tree> &established,
		  hash_set<tree> &established_nz,
		  hash_map<tree, oa_range_lite> &established_range,
		  hash_map<tree, rel_fact_lite> &established_rel)
{
  tree fndecl = fun->decl;
  for (tree contract : fn_contracts (fndecl))
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
	  hash_set<tree> *target;
	  if (is_object_address_call_p (*conjuncts[i], &arg))
	    {
	      STRIP_ANY_LOCATION_WRAPPER (arg);
	      target = &established;
	    }
	  else if (oa_nonzero_conjunct_p (*conjuncts[i], &arg))
	    target = &established_nz;
	  else
	    continue;

	  if (TREE_CODE (arg) != PARM_DECL)
	    continue;
	  tree ssa = ssa_default_def (fun, arg);
	  if (ssa)
	    target->add (ssa);
	}

      /* A relational conjunct against another of the SAME function's
	 own parameters -- trust it unconditionally for the rest of this
	 function's own body, mirroring the two boolean facts above.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rel_other;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_param (*conjuncts[i], &rel_param,
						   &rel_code, &rel_other))
	    continue;
	  tree ssa_param = ssa_default_def (fun, rel_param);
	  tree ssa_other = ssa_default_def (fun, rel_other);
	  if (ssa_param && ssa_other)
	    established_rel.put (ssa_param, { rel_code, ssa_other });
	}

      /* Range conjuncts need their own pass: several conjuncts can
	 name the SAME param ('x >= 20 && x < 100'), so they must be
	 grouped and combined (extract_conjunct_range) rather than
	 handled one at a time like the two boolean facts above.  */
      auto_vec<tree> params;
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree param, const_val;
	  tree_code code;
	  if (oa_match_simple_comparison (*conjuncts[i], &param, &code, &const_val)
	      && TREE_CODE (param) == PARM_DECL
	      && !TYPE_REF_P (TREE_TYPE (param)) && !params.contains (param))
	    params.safe_push (param);
	}
      for (unsigned p = 0; p < params.length (); ++p)
	{
	  oa_range_lite range;
	  if (!extract_conjunct_range (conjuncts, params[p], &range))
	    continue;
	  tree ssa = ssa_default_def (fun, params[p]);
	  if (ssa)
	    established_range.put (ssa, range);
	}
    }
}

/* Whether one of this plugin's rules checks CONJUNCT, a precondition
   conjunct, at a call: an is_object_address, nonzero, predicate or
   field-range conjunct on a parameter, a parameter compared with an
   integer constant or another parameter, or a relation.  Any other is
   reported (report_uncovered), never passed over.  */

static bool
conjunct_covered_p (tree conjunct)
{
  tree a, b, c;
  tree_code code;
  bool negated;
  widest_int offset;
  tree d;
  if (is_object_address_call_p (conjunct, &a)
      || oa_match_comparison_against_param (conjunct, &a, &code, &b)
      || relation_conjunct_p (conjunct))
    return true;
  /* Both flavors' shapes: the rule itself rejects a symbolic accessor in a
     conveyor obligation, reporting it.  */
  if (oa_match_comparison_against_call (conjunct, &a, &code, &b, &c, true)
      || oa_match_shifted_comparison_against_call_text (conjunct, &a, &code,
							&b, &c, &offset, true))
    return true;
  (void) d;
  if (oa_nonzero_conjunct_p (conjunct, &a))
    return true;
  if (oa_match_simple_comparison (conjunct, &a, &code, &b))
    return (TREE_CODE (a) == PARM_DECL
	    && (TREE_CODE (b) == INTEGER_CST
		|| (TREE_CODE (b) == REAL_CST
		    && SCALAR_FLOAT_TYPE_P (TREE_TYPE (a)))));
  if (oa_match_predicate_conjunct (conjunct, &a, &b, &negated))
    return true;
  if (oa_match_field_range_comparison (conjunct, &a, &b, &code, &c))
    return ((TREE_CODE (c) == INTEGER_CST
	     || (TREE_CODE (c) == REAL_CST && SCALAR_FLOAT_TYPE_P (TREE_TYPE (a))))
	    && TREE_CODE (oa_strip_symbolic_ptr_expr_public (b)) == PARM_DECL);
  return false;
}

/* CONJUNCT, a precondition conjunct of CALLEE, with CALL's literal
   arguments for its parameters, folded: a constant if every parameter it
   reads has one, else NULL_TREE.  */

static tree
fold_conjunct_at_call (tree conjunct, gcall *call, tree callee)
{
  struct data_t { gcall *call; tree callee; bool ok; };
  data_t d = { call, callee, true };
  tree copy = unshare_expr (conjunct);
  cp_walk_tree (&copy, [](tree *tp, int *walk_subtrees, void *d_) -> tree
    {
      data_t *dd = (data_t *) d_;
      if (TYPE_P (*tp))
	{
	  *walk_subtrees = 0;
	  return NULL_TREE;
	}
      if (TREE_CODE (*tp) != PARM_DECL)
	return NULL_TREE;
      unsigned argno;
      tree arg = NULL_TREE;
      if (!TYPE_REF_P (TREE_TYPE (*tp))
	  && find_param_position (dd->callee, *tp, &argno)
	  && argno < gimple_call_num_args (dd->call))
	arg = gimple_call_arg (dd->call, argno);
      if (!arg || TREE_CODE (arg) != INTEGER_CST)
	{
	  dd->ok = false;
	  return *tp;
	}
      *tp = fold_convert (TREE_TYPE (*tp), arg);
      *walk_subtrees = 0;
      return NULL_TREE;
    }, &d, NULL);
  if (!d.ok)
    return NULL_TREE;
  tree folded = tree_strip_any_location_wrapper (cp_fully_fold (copy));
  return TREE_CODE (folded) == INTEGER_CST ? folded : NULL_TREE;
}

/* CONJUNCT, a precondition conjunct of CALLEE no rule checks, at CALL:
   true or false only if CALL's arguments make it a constant, otherwise
   unverified.  */

static void
report_uncovered (tree conjunct, gcall *call, tree callee, bool strict)
{
  location_t loc = gimple_location (call);
  if (tree folded = fold_conjunct_at_call (conjunct, call, callee))
    {
      if (integer_zerop (folded))
	error_at (loc, "the arguments provably violate the precondition %qE "
		  "of %qD", conjunct, callee);
      return;
    }
  if (strict)
    report_unproven (call, true, "cannot prove the precondition %qE of %qD",
		     conjunct, callee);
  else
    report_unproven (call, false, "cannot verify the precondition %qE of "
		     "%qD", conjunct, callee);
}

/* CALL's callee's preconditions this plugin checks (call_obligation_p),
   against the facts: each is_object_address (param), 'param != 0',
   'param OP other-param' and 'param OP constant' conjunct, with CALL's
   arguments for the parameters.  */

static void
check_call (gcall *call, hash_set<tree> &established,
	     hash_set<tree> &established_nz,
	     hash_map<tree, oa_range_lite> &established_range,
	     hash_map<tree, rel_fact_lite> &established_rel,
	     gimple_ranger *ranger)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  callee_declared_note note (call);
  hash_set<tree> reported_oa, reported_nz;
  for (tree contract : fn_contracts (callee))
    {
      bool as_conveyor, strict;
      if (!call_obligation_p (contract, callee, &as_conveyor, &strict))
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree arg;
	  bool is_oa;
	  if (is_object_address_call_p (*conjuncts[i], &arg))
	    {
	      STRIP_ANY_LOCATION_WRAPPER (arg);
	      is_oa = true;
	      /* Mandatory in the conveyor flavor: the engine's.  */
	      if (as_conveyor)
		continue;
	    }
	  else if (oa_nonzero_conjunct_p (*conjuncts[i], &arg))
	    is_oa = false;
	  else
	    continue;

	  tree written = arg;
	  while (TREE_CODE (arg) == NON_LVALUE_EXPR || CONVERT_EXPR_P (arg)
		 || TREE_CODE (arg) == VIEW_CONVERT_EXPR)
	    arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
	  /* Of another operand ('&p->m', 'n + 1'): no rule, unverified.  */
	  if (TREE_CODE (arg) != PARM_DECL)
	    {
	      if (is_oa && !reported_oa.add (written))
		report_unproven (call, strict, strict
				 ? "cannot prove %<is_object_address%> for "
				   "%qE, as required by the precondition of %qD"
				 : "cannot verify %<is_object_address%> for "
				   "%qE, as required by the precondition of %qD",
				 written, callee);
	      else if (!is_oa)
		report_uncovered (*conjuncts[i], call, callee, strict);
	      continue;
	    }
	  /* A conveyor callee's reference parameters and 'this' are the
	     engine's (reference safety).  */
	  if ((TYPE_REF_P (TREE_TYPE (arg)) || is_this_parameter (arg))
	      && DECL_DECLARED_CONVEYOR_P (callee))
	    continue;

	  unsigned argno;
	  if (!find_param_position (callee, arg, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;

	  tree substituted = gimple_call_arg (call, argno);
	  hash_set<tree> in_progress;
	  if (is_oa)
	    {
	      if (provable_object_address_p (substituted, established,
					      in_progress)
		  || reported_oa.add (arg))
		continue;
	      if (strict)
		report_unproven (call, true, "cannot prove %<is_object_address%> "
				 "for %qE, as required by the precondition of "
				 "%qD", substituted, callee);
	      else
		report_unproven (call, false, "cannot verify "
				 "%<is_object_address%> for %qE, as required "
				 "by the precondition of %qD", substituted,
				 callee);
	      continue;
	    }

	  tree literal = STRIP_ANY_LOCATION_WRAPPER (substituted);
	  if (TREE_CODE (literal) == INTEGER_CST)
	    {
	      if (integer_zerop (literal) && !reported_nz.add (arg))
		{
		  error_at (gimple_location (call), "argument %qE provably "
			    "violates the precondition of %qD", literal,
			    callee);
		  inform (gimple_location (call), "%qE is established to be "
			  "exactly zero, but the precondition requires it to "
			  "be nonzero", literal);
		}
	      continue;
	    }
	  int_range_max vr;
	  if (provable_nonzero_p (substituted, established_nz, in_progress)
	      || (trusted_range_at (substituted, call, ranger, vr)
		  && !vr.contains_p (wi::zero (TYPE_PRECISION
						 (TREE_TYPE (substituted))))))
	    continue;
	  /* Exactly zero where CALL is ('x == 0 && f (x)').  */
	  tree zero_at;
	  if (trusted_range_at (substituted, call, ranger, vr)
	      && vr.singleton_p (&zero_at) && integer_zerop (zero_at))
	    {
	      if (!reported_nz.add (arg))
		{
		  error_at (gimple_location (call), "argument %qE provably "
			    "violates the precondition of %qD", substituted,
			    callee);
		  inform (gimple_location (call), "%qE is established to be "
			  "exactly zero, but the precondition requires it to "
			  "be nonzero", substituted);
		}
	      continue;
	    }
	  if (reported_nz.add (arg))
	    continue;
	  if (strict)
	    report_unproven (call, true, "cannot prove that %qE is nonzero, as "
			     "required by the precondition of %qD",
			     substituted, callee);
	  else
	    report_unproven (call, false, "cannot verify that %qE is nonzero, "
			     "as required by the precondition of %qD",
			     substituted, callee);
	}

      for (unsigned i = 0; i < conjuncts.length (); ++i)
	if (!conjunct_covered_p (*conjuncts[i]))
	  report_uncovered (*conjuncts[i], call, callee, strict);

      /* A floating-point parameter compared with a constant ('pre (x <=
	 100.0)'): the argument's range.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree parm, cst;
	  tree_code code;
	  unsigned argno;
	  if (!oa_match_simple_comparison (*conjuncts[i], &parm, &code, &cst)
	      || TREE_CODE (cst) != REAL_CST || TREE_CODE (parm) != PARM_DECL
	      || !SCALAR_FLOAT_TYPE_P (TREE_TYPE (parm))
	      || !find_param_position (callee, parm, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree arg = gimple_call_arg (call, argno);
	  verdict v = float_verdict (arg, code, cst, call, ranger);
	  if (v == VERDICT_TRUE)
	    continue;
	  if (v == VERDICT_FALSE)
	    error_at (gimple_location (call), "argument %qE provably violates "
		      "the precondition of %qD", arg, callee);
	  else
	    report_unproven (call, strict, strict
			     ? "cannot prove that %qE satisfies the "
			       "precondition of %qD"
			     : "cannot verify that %qE satisfies the "
			       "precondition of %qD", arg, callee);
	}

      /* Range obligations ('pre (x >= 20 && x < 100)'), grouped per
	 parameter, since one precondition can constrain several, each
	 through more than one conjunct.  */
      auto_vec<tree> params;
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree param, const_val;
	  tree_code code;
	  if (oa_match_simple_comparison (*conjuncts[i], &param, &code, &const_val)
	      && TREE_CODE (param) == PARM_DECL
	      && TREE_CODE (const_val) == INTEGER_CST
	      && !params.contains (param))
	    params.safe_push (param);
	}
      for (unsigned p = 0; p < params.length (); ++p)
	{
	  oa_range_lite required;
	  if (!extract_conjunct_range (conjuncts, params[p], &required))
	    continue;

	  unsigned argno;
	  if (!find_param_position (callee, params[p], &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);

	  oa_range_lite have;
	  if (established_range_of (substituted, established_range, ranger,
				     call, &have)
	      && (!required.has_lo || (have.has_lo && have.lo >= required.lo))
	      && (!required.has_hi || (have.has_hi && have.hi <= required.hi)))
	    continue;

	  report_unproven (call, strict, strict
			   ? ("cannot prove that %qE satisfies the "
				"precondition of %qD")
			   : ("cannot verify that %qE satisfies the "
				"precondition of %qD"), substituted, callee);
	}
    }
}

/* The function checker the engine calls (cg_register_function_check).  */

static void
check_function (function *fun, void *)
{
  /* The engine's own proof checks run otherwise.  */
  if (!flag_contract_analysis_proofs)
    return;
  /* For debugging: the function as checked, with its virtual operands.  */
  if (getenv ("GIMPLE_OA_PLUGIN_DUMP"))
    dump_function_to_file (fun->decl, stderr, TDF_VOPS);

  hash_set<tree> established;
  hash_set<tree> established_nz;
  hash_map<tree, oa_range_lite> established_range;
  hash_map<tree, rel_fact_lite> established_rel;
  seed_self_trust (fun, established, established_nz, established_range,
		    established_rel);

  /* A floating-point parameter compared with a constant in the
     function's own preconditions.  */
  hash_map<tree, frange> trusted_frange;
  for (tree contract : fn_contracts (fun->decl))
    {
      tree cond = CONTRACT_CONDITION (contract);
      if (!PRECONDITION_P (contract) || !cond || cond == error_mark_node)
	continue;
      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree parm, cst;
	  tree_code code;
	  frange r;
	  tree ssa;
	  if (oa_match_simple_comparison (*conjuncts[i], &parm, &code, &cst)
	      && TREE_CODE (cst) == REAL_CST && TREE_CODE (parm) == PARM_DECL
	      && SCALAR_FLOAT_TYPE_P (TREE_TYPE (parm))
	      && (ssa = ssa_default_def (fun, parm))
	      && frange_for_comparison (TREE_TYPE (parm), code, cst, &r))
	    {
	      if (frange *old = trusted_frange.get (ssa))
		old->intersect (r);
	      else
		trusted_frange.put (ssa, r);
	    }
	}
    }
  established_frange = &trusted_frange;

  gimple_ranger *ranger = enable_ranger (fun, false);

  /* Facts about persistent memory (named predicates, field ranges) are
     per program point: a forward analysis of their own.  */

  value_facts vf = { &established, &established_nz, &established_range,
		     &established_rel, ranger };
  current_value_facts = &vf;
  dom_fact_state predicate_seed;
  seed_predicate_self_trust (fun, predicate_seed);
  persistent_facts_walk (fun, predicate_seed);
  current_value_facts = NULL;
  established_frange = NULL;
  disable_ranger (fun);
}

int
plugin_init (struct plugin_name_args *plugin_info,
	     struct plugin_gcc_version *version)
{
  if (!plugin_default_version_check (version, &gcc_version))
    return 1;

  cg_register_function_check (check_function, NULL);
  return 0;
}
