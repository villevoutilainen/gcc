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
   ranges) need flow-sensitive state and get a dominator walk.  */

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
#include "domwalk.h"
#include "hash-traits.h"

int plugin_is_GPL_compatible;

/* Whether the function being checked is conveyor code: a conveyor
   function, or the core of a predicate (its condition, outlined).  */

static bool
conveyor_code_p (tree fndecl)
{
  return (DECL_DECLARED_CONVEYOR_P (fndecl)
	  || CONTRACT_HELPER (fndecl) == ldf_contract_predicate_helper);
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
      if (!POSTCONDITION_P (contract))
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
      if (!POSTCONDITION_P (contract))
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
      if (!POSTCONDITION_P (contract))
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

/* A fourth fact shape, named predicates for a *persistent object*
   (e.g. 'is_opened(this)') -- fundamentally different from the three
   above: those are properties of a VALUE (does this SSA name's value
   satisfy P), answerable by walking backward through SSA_NAME_DEF_STMT/
   PHI, exactly the kind of question SSA form is built for. A named
   predicate is a property of *persistent, aliasable memory* (does the
   object THIS POINTER CURRENTLY DENOTES currently satisfy P) -- an
   ordinary call passing that same pointer value onward could have
   mutated the pointee's state without changing the pointer's own SSA
   value at all, so this needs genuine forward, flow-sensitive
   reasoning (establish here, invalidate there, propagated along
   actual control flow), not a backward value walk.

   Rather than a general worklist CFG dataflow (a full "AND across
   every predecessor, iterate to a fixpoint" framework), this exploits
   a simpler, exactly-sufficient property: a fact is available at
   block B if and only if it was established somewhere that
   *dominates* B, without an invalidating call anywhere on the (unique,
   by definition of dominance) path from there to B. A dominator-tree
   preorder walk (class dom_walker, domwalk.h) gives exactly this for
   free: processing each block using its own immediate dominator's
   already-computed exit state as its starting point automatically
   yields the "true on every path reaching here" semantics
   oa_env::predicate_fact_merge_with otherwise computes by hand-rolled
   AND-merging at explicit if/loop join points -- if a fact was
   established on only one arm of an if, the merge block's own
   immediate dominator is some common ancestor *above* that if
   (dominance requires there to be no OTHER way in), so the fact is
   correctly and automatically absent there, with no explicit merge
   step written anywhere in this file.

   IDENTITY is deliberately simple: an SSA_NAME's own identity is
   itself (an ordinary reassignment produces a brand-new SSA_NAME by
   construction, so nothing needs to be done to "invalidate" an old
   value going out of scope -- whoever still holds the OLD SSA name
   still correctly sees whatever was true of it); '&decl' resolves to
   DECL directly, so a plain-object receiver ('f.open()') and a
   pointer receiver ('hp->open()') both key correctly off "the same
   object" across separate calls on the same variable.

   A fifth shape, ptr->field ranges (e.g. 'this->count in [0, N)'),
   shares this exact same dominator-walk substrate: it's the same kind
   of persistent-memory fact as a named predicate, just keyed by
   (IDENTITY, FIELD_DECL) instead of IDENTITY alone and carrying an
   oa_range_lite payload instead of (PRED_FN, POLARITY) -- so both are
   threaded through ONE walk, sharing establish/invalidate/consult
   ordering per statement, rather than two separate walks.  */

struct oa_predicate_fact_lite { tree pred_fn; bool polarity; };
typedef pair_hash<nofree_ptr_hash<tree_node>, nofree_ptr_hash<tree_node>>
  oa_field_key_hash;

/* Both persistent-memory fact maps for one program point, threaded
   together through the dominator walk below.  */
struct dom_fact_state
{
  hash_map<tree, oa_predicate_fact_lite> pred;
  hash_map<oa_field_key_hash, oa_range_lite> field;
};

static tree
gimple_object_identity (tree val)
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
    return val;
  return NULL_TREE;
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
	  if (!oa_match_predicate_conjunct (*conjuncts[i], &pred_fn, &arg_decl,
					     &negated))
	    continue;
	  if (TREE_CODE (arg_decl) != PARM_DECL)
	    continue;
	  tree ssa = ssa_default_def (fun, arg_decl);
	  if (ssa)
	    seed.pred.put (ssa, { pred_fn, !negated });
	}

      auto_vec<field_group_lite> field_groups;
      collect_field_range_groups (cond, &field_groups);
      for (unsigned g = 0; g < field_groups.length (); ++g)
	{
	  tree ssa = ssa_default_def (fun, field_groups[g].ptr_expr);
	  if (ssa)
	    seed.field.put ({ssa, field_groups[g].field}, field_groups[g].range);
	}
    }
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
	  if (!oa_match_predicate_conjunct (*conjuncts[i], &pred_fn, &arg_decl,
					     &negated))
	    continue;

	  unsigned argno;
	  if (!find_param_position (callee, arg_decl, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);
	  tree identity = gimple_object_identity (substituted);

	  bool required = !negated;
	  oa_predicate_fact_lite *fact
	    = identity ? state.pred.get (identity) : NULL;
	  if (fact && fact->pred_fn == pred_fn)
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

/* Rule 2 (see contracts.cc's own oa_invalidate_symbolic_facts_for_call_
   args, whose exact discipline this mirrors): a tracked object's fact
   must be invalidated by *any* call taking its address or receiving it
   as a bare pointer, whether or not that call has any contracts of its
   own at all -- there's no way to know an arbitrary, uncontracted
   function didn't change the pointee's logical state.  Drops every
   tracked field for the same identity too, matching contracts.cc's own
   contract_field_range_invalidate_all's whole-object granularity.  */

static void
invalidate_persistent_facts_for_call_args (gcall *call, dom_fact_state &state)
{
  unsigned n = gimple_call_num_args (call);
  for (unsigned i = 0; i < n; ++i)
    {
      tree identity = gimple_object_identity (gimple_call_arg (call, i));
      if (!identity)
	continue;
      state.pred.remove (identity);

      auto_vec<std::pair<tree, tree>> to_remove;
      for (auto it : state.field)
	if (it.first.first == identity)
	  to_remove.safe_push (it.first);
      for (unsigned j = 0; j < to_remove.length (); ++j)
	state.field.remove (to_remove[j]);
    }
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
      if (!POSTCONDITION_P (contract))
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
	    continue;

	  unsigned argno;
	  if (!find_param_position (callee, arg_decl, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);
	  tree identity = gimple_object_identity (substituted);
	  if (identity)
	    state.pred.put (identity, { pred_fn, !negated });
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
    }
}

/* The dominator-preorder walk itself: BLOCK_OUT[bb] is bb's own exit
   state, computed once (a dominator-tree preorder walk visits each
   block exactly once) from its immediate dominator's already-computed
   BLOCK_OUT entry (or SEED, for the root). Heap-allocated per block
   (rather than stored by value) purely to avoid requiring hash_map's
   value type to itself be a cheaply-copyable hash_map; freed in the
   destructor.  */

class predicate_dom_walker : public dom_walker
{
public:
  predicate_dom_walker (dom_fact_state *seed_)
    : dom_walker (CDI_DOMINATORS), seed (seed_)
  {}

  ~predicate_dom_walker ()
  {
    for (auto it : block_out)
      delete it.second;
  }

  edge before_dom_children (basic_block) final override;

  dom_fact_state *seed;
  hash_map<basic_block, dom_fact_state *> block_out;
};

edge
predicate_dom_walker::before_dom_children (basic_block bb)
{
  dom_fact_state *state = new dom_fact_state ();

  basic_block idom = get_immediate_dominator (CDI_DOMINATORS, bb);
  const dom_fact_state *from = seed;
  if (idom)
    {
      dom_fact_state **parent = block_out.get (idom);
      from = parent ? *parent : NULL;
    }
  if (from)
    {
      for (auto it : from->pred)
	state->pred.put (it.first, it.second);
      for (auto it : from->field)
	state->field.put (it.first, it.second);
    }

  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (!is_gimple_call (stmt))
	continue;
      gcall *call = as_a <gcall *> (stmt);
      /* Same order as contracts.cc's own oa_scan_calls_in_expr: consult
	 using the state as it stood *before* this call, then invalidate,
	 then establish whatever this same call's own postcondition
	 guarantees.  */
      consult_persistent_facts (call, *state);
      invalidate_persistent_facts_for_call_args (call, *state);
      establish_persistent_facts_for_call (call, *state);
    }

  block_out.put (bb, state);
  return NULL;
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

struct rel_fact_lite { tree_code code; tree rhs; };

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
      if (!POSTCONDITION_P (contract))
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
	      && TREE_CODE (param) == PARM_DECL && !params.contains (param))
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

	  while (TREE_CODE (arg) == NON_LVALUE_EXPR || CONVERT_EXPR_P (arg)
		 || TREE_CODE (arg) == VIEW_CONVERT_EXPR)
	    arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
	  if (TREE_CODE (arg) != PARM_DECL)
	    continue;
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

      /* Relational obligations against another of the callee's own
	 parameters ('pre (x < q)'): both substituted positionally.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rel_other;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_param (*conjuncts[i], &rel_param,
						   &rel_code, &rel_other))
	    continue;

	  unsigned param_argno, other_argno;
	  if (!find_param_position (callee, rel_param, &param_argno)
	      || !find_param_position (callee, rel_other, &other_argno)
	      || param_argno >= gimple_call_num_args (call)
	      || other_argno >= gimple_call_num_args (call))
	    continue;

	  tree sub_param = gimple_call_arg (call, param_argno);
	  tree sub_other = gimple_call_arg (call, other_argno);

	  /* Both literals: plain constant folding.  */
	  if (TREE_CODE (sub_param) == INTEGER_CST
	      && TREE_CODE (sub_other) == INTEGER_CST)
	    {
	      if (!oa_relational_literal_holds (rel_code, sub_param, sub_other))
		error_at (gimple_location (call), "argument %qE provably "
			  "violates the precondition of %qD", sub_param,
			  callee);
	      continue;
	    }

	  tree_code fact_code;
	  tree fact_rhs;
	  if (get_relational (sub_param, established_rel, &fact_code, &fact_rhs)
	      && oa_relational_code_implies (fact_code, rel_code)
	      && fact_rhs == sub_other)
	    continue;

	  report_unproven (call, strict, strict
			   ? ("cannot prove that %qE satisfies the "
				"precondition of %qD")
			   : ("cannot verify that %qE satisfies the "
				"precondition of %qD"), sub_param, callee);
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

  hash_set<tree> established;
  hash_set<tree> established_nz;
  hash_map<tree, oa_range_lite> established_range;
  hash_map<tree, rel_fact_lite> established_rel;
  seed_self_trust (fun, established, established_nz, established_range,
		    established_rel);

  gimple_ranger *ranger = enable_ranger (fun, false);
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      if (gcall *call = dyn_cast <gcall *> (gsi_stmt (gsi)))
	check_call (call, established, established_nz, established_range,
		    established_rel, ranger);
  disable_ranger (fun);

  /* Facts about persistent memory (named predicates, field ranges) are
     per program point: a dominator walk of their own.  */
  dom_fact_state predicate_seed;
  seed_predicate_self_trust (fun, predicate_seed);
  predicate_dom_walker pdw (&predicate_seed);
  pdw.walk (ENTRY_BLOCK_PTR_FOR_FN (fun));
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
