/* gimple-object-address: an experimental prototype demonstrating the
   "Option A, front-end-assisted" design from ~/gimple-contract-
   analysis.md -- doing D4324's mandatory item 6/7/8 checks (is_object_
   address, nonzero-divisor; more fact shapes to follow, see the
   analysis's own Section 8) as a GIMPLE pass, run right after SSA is
   built (before any optimization pass), instead of the existing
   AST-walk (gcc/cp/contracts.cc's own oa_* machinery, hooked at
   PLUGIN_PRE_GENERICIZE).

   This is an ADDITIONAL alternative, not a replacement: the existing
   mandatory checks (contracts.cc) keep running exactly as before, so a
   test compiled with this plugin will typically see BOTH the existing
   check's own diagnostic AND this pass's own (distinctly worded,
   "gimple-oa:"-prefixed) diagnostic for the same construct -- that
   overlap is expected and is not a bug in either engine.

   Design (see ~/gimple-contract-analysis.md, section 4, "the wrapper-
   parameter problem, and its resolution"): this pass NEVER looks at a
   contract's own outlined GIMPLE machinery (F.pre/F.post/the
   predicate-core function/the thunk) -- it reads a function's
   *declared* precondition/postcondition text directly off its own
   FUNCTION_DECL (get_fn_contract_specifiers/CONTRACT_CONDITION,
   exactly the same front-end API the existing AST-walk itself uses,
   and the exported is_object_address_call_p/oa_nonzero_conjunct_p
   recognizers, unchanged), and does the "is this argument provably
   true, right here" part with ordinary GIMPLE/SSA reasoning:
   SSA_NAME_DEF_STMT plus a recursive PHI-argument walk (a PHI's own
   "reaches from every incoming edge" is exactly the merge rule
   oa_env::merge_with hand-rolls at the AST level, but reified as a
   real node here instead of a hand-maintained env). Because contract
   *text* is read declaratively, this sidesteps both the wrapper-
   parameter correspondence problem (Section 4.1 of the analysis) and
   the fact that a `symbolic` contract's own condition may generate no
   GIMPLE at all (Section 2.2) -- neither matters, since this pass
   never needs the contract's own generated code, only its declared
   text plus the ordinary code around it.

   Every fact shape below (is_object_address, nonzero -- more to
   follow) shares this exact same three-part structure, mirroring the
   "classic" m_map/m_nz_map maps' own self-trust/consult/item-6 trio in
   contracts.cc itself:
   1. Self-trust: a function's own declared precondition seeds a fact
      onto ssa_default_def(fun, parm) at function entry.
   2. Call-site consult: a direct callee's own declared precondition is
      checked against the caller's actual gimple_call_arg, substituted
      positionally (find_param_position, the GIMPLE analogue of
      oa_substitute_call_arg).
   3. Item 6: a direct callee's own declared postcondition
      unconditionally guaranteeing the fact for its own return value is
      recognized wherever a GIMPLE_CALL's own result feeds into a later
      obligation.
   Provability itself is a single recursive SSA walk per fact shape:
   SSA_NAME_DEF_STMT dispatch on GIMPLE_PHI (AND over every incoming
   value, cycle-guarded via an IN_PROGRESS hash_set for loop-carried
   PHIs -- conservatively false, the same "must be provable, else
   unprovable" discipline used throughout), GIMPLE_ASSIGN (a trivial
   shape, or propagate through a copy/conversion), and GIMPLE_CALL
   (item 6).

   Deliberately narrow scope, kept out on purpose (see the analysis's
   own "phased plan," section 6, and its own "Section 8"/"Section 9"
   validation-results writeups for how this evolved):
   - ASSERTION_STMT (contract_assert, which has no single fixed
     get_fn_contract_specifiers-style declarative home the way pre/post
     do), and a postcondition establishing a fact about a *persistent
     parameter* (as opposed to the return value) for a later, separate
     call site, are both still out of scope for every fact shape below.
   - Only DIRECT calls (a resolved GIMPLE_CALL callee FUNCTION_DECL)
     are consulted; an indirect/virtual call gets no consult at all,
     matching the same limitation the existing AST-walk already has.
   - IILE recursion is explicitly, permanently out of scope for this
     prototype (not merely deferred) -- an immediately-invoked closure
     always reports "cannot verify" here, by design.  */

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

int plugin_is_GPL_compatible;

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
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
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
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
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
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
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
		       gimple_ranger *ranger, oa_range_lite *out)
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
				    ranger, out))
	return true;
    }

  if (ranger)
    {
      int_range_max vr;
      if (ranger->range_of_expr (vr, val) && !vr.undefined_p ()
	  && !vr.varying_p ())
	{
	  unsigned n = vr.num_pairs ();
	  if (n > 0)
	    {
	      out->has_lo = out->has_hi = true;
	      out->lo = widest_int::from (vr.lower_bound (0), SIGNED);
	      out->hi = widest_int::from (vr.upper_bound (n - 1), SIGNED);
	      return true;
	    }
	}
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
   object" across separate calls on the same variable.  */

struct oa_predicate_fact_lite { tree pred_fn; bool polarity; };

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

/* Seed SEED from FNDECL's own declared precondition -- self-trust, the
   exact analogue of seed_self_trust above, just for predicate facts:
   keyed by ssa_default_def(fun, parm) instead of a bare hash_set
   entry, since a predicate fact carries a (PRED_FN, POLARITY) payload
   rather than being a plain boolean.  */

static void
seed_predicate_self_trust (function *fun,
			    hash_map<tree, oa_predicate_fact_lite> &seed)
{
  tree fndecl = fun->decl;
  for (tree as = get_fn_contract_specifiers (fndecl); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
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
	    seed.put (ssa, { pred_fn, !negated });
	}
    }
}

/* CALL's own callee's declared precondition, checked against STATE as
   it stands right before CALL -- the consult side.  */

static void
consult_predicate_call (gcall *call, hash_map<tree, oa_predicate_fact_lite> &state)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
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

	  unsigned argno;
	  if (!find_param_position (callee, arg_decl, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);
	  tree identity = gimple_object_identity (substituted);

	  bool required = !negated;
	  oa_predicate_fact_lite *fact = identity ? state.get (identity) : NULL;
	  if (fact && fact->pred_fn == pred_fn && fact->polarity == required)
	    continue; /* Proven true: silently discharged.  */

	  warning_at (gimple_location (call), 0,
		      "gimple-oa: cannot verify that %qD (%qE) holds, as "
		      "required by the precondition of %qD",
		      pred_fn, substituted, callee);
	}
    }
}

/* Rule 2 (see contracts.cc's own oa_invalidate_symbolic_facts_for_call_
   args, whose exact discipline this mirrors): a tracked object's fact
   must be invalidated by *any* call taking its address or receiving it
   as a bare pointer, whether or not that call has any contracts of its
   own at all -- there's no way to know an arbitrary, uncontracted
   function didn't change the pointee's logical state.  */

static void
invalidate_predicate_call_args (gcall *call,
				 hash_map<tree, oa_predicate_fact_lite> &state)
{
  unsigned n = gimple_call_num_args (call);
  for (unsigned i = 0; i < n; ++i)
    {
      tree identity = gimple_object_identity (gimple_call_arg (call, i));
      if (identity)
	state.remove (identity);
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
establish_predicate_call (gcall *call,
			   hash_map<tree, oa_predicate_fact_lite> &state)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
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
	    state.put (identity, { pred_fn, !negated });
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
  predicate_dom_walker (hash_map<tree, oa_predicate_fact_lite> *seed_)
    : dom_walker (CDI_DOMINATORS), seed (seed_)
  {}

  ~predicate_dom_walker ()
  {
    for (auto it : block_out)
      delete it.second;
  }

  edge before_dom_children (basic_block) final override;

  hash_map<tree, oa_predicate_fact_lite> *seed;
  hash_map<basic_block, hash_map<tree, oa_predicate_fact_lite> *> block_out;
};

edge
predicate_dom_walker::before_dom_children (basic_block bb)
{
  hash_map<tree, oa_predicate_fact_lite> *state
    = new hash_map<tree, oa_predicate_fact_lite> ();

  basic_block idom = get_immediate_dominator (CDI_DOMINATORS, bb);
  if (idom)
    {
      hash_map<tree, oa_predicate_fact_lite> **parent
	= block_out.get (idom);
      if (parent)
	for (auto it : **parent)
	  state->put (it.first, it.second);
    }
  else
    for (auto it : *seed)
      state->put (it.first, it.second);

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
      consult_predicate_call (call, *state);
      invalidate_predicate_call_args (call, *state);
      establish_predicate_call (call, *state);
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
		  hash_map<tree, oa_range_lite> &established_range)
{
  tree fndecl = fun->decl;
  for (tree as = get_fn_contract_specifiers (fndecl); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
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

/* For CALL's own callee, check every is_object_address(param)/'param
   != 0'-shaped conjunct of its own declared precondition against
   CALL's own actual argument, substituted positionally
   (find_param_position) exactly the way the AST-walk's own
   oa_substitute_call_arg already does -- again, only CALLEE's
   *declared* condition is ever consulted, never CALLEE.pre's own
   outlined body.  */

static void
check_call (gcall *call, hash_set<tree> &established,
	     hash_set<tree> &established_nz,
	     hash_map<tree, oa_range_lite> &established_range,
	     gimple_ranger *ranger)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
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
	  bool is_oa;
	  if (is_object_address_call_p (*conjuncts[i], &arg))
	    {
	      STRIP_ANY_LOCATION_WRAPPER (arg);
	      is_oa = true;
	    }
	  else if (oa_nonzero_conjunct_p (*conjuncts[i], &arg))
	    is_oa = false;
	  else
	    continue;

	  if (TREE_CODE (arg) != PARM_DECL)
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
					      in_progress))
		continue; /* Proven true: silently discharged.  */
	      warning_at (gimple_location (call), 0,
			  "gimple-oa: cannot verify %<is_object_address%> for "
			  "%qE, as required by the precondition of %qD",
			  substituted, callee);
	    }
	  else
	    {
	      if (provable_nonzero_p (substituted, established_nz, in_progress))
		continue; /* Proven true: silently discharged.  */
	      warning_at (gimple_location (call), 0,
			  "gimple-oa: cannot verify that %qE is nonzero, as "
			  "required by the precondition of %qD",
			  substituted, callee);
	    }
	}

      /* Range obligations: same per-param grouping as seed_self_trust's
	 own range handling, since one precondition can constrain
	 several distinct parameters, each via more than one conjunct.  */
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
	  oa_range_lite required;
	  if (!extract_conjunct_range (conjuncts, params[p], &required))
	    continue;

	  unsigned argno;
	  if (!find_param_position (callee, params[p], &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);

	  oa_range_lite established_r;
	  if (established_range_of (substituted, established_range, ranger,
				     &established_r)
	      && (!required.has_lo
		  || (established_r.has_lo && established_r.lo >= required.lo))
	      && (!required.has_hi
		  || (established_r.has_hi && established_r.hi <= required.hi)))
	    continue; /* Proven true: silently discharged.  */

	  warning_at (gimple_location (call), 0,
		      "gimple-oa: cannot verify that %qE satisfies the "
		      "precondition of %qD", substituted, callee);
	}
    }
}

namespace {

const pass_data pass_data_gimple_object_address =
{
  GIMPLE_PASS,			/* type */
  "gimple_object_address",	/* name */
  OPTGROUP_NONE,		/* optinfo_flags */
  TV_NONE,			/* tv_id */
  PROP_ssa,			/* properties_required */
  0,				/* properties_provided */
  0,				/* properties_destroyed */
  0,				/* todo_flags_start */
  0,				/* todo_flags_finish */
};

class pass_gimple_object_address : public gimple_opt_pass
{
public:
  pass_gimple_object_address (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_gimple_object_address, ctxt)
  {}

  bool gate (function *) final override { return true; }
  unsigned int execute (function *) final override;
};

unsigned int
pass_gimple_object_address::execute (function *fun)
{
  hash_set<tree> established;
  hash_set<tree> established_nz;
  hash_map<tree, oa_range_lite> established_range;
  seed_self_trust (fun, established, established_nz, established_range);

  calculate_dominance_info (CDI_DOMINATORS);
  gimple_ranger *ranger = enable_ranger (fun, false);

  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (is_gimple_call (stmt))
	  check_call (as_a <gcall *> (stmt), established, established_nz,
		      established_range, ranger);
      }

  disable_ranger (fun);

  /* Named-predicate facts get their own, separate dominator-tree-based
     walk (see predicate_dom_walker's own comment) rather than folding
     into the FOR_EACH_BB_FN loop above: that loop's own three fact
     shapes are consulted using a single, function-wide ESTABLISHED
     set/map (correct for them, since a backward SSA walk needs no
     block-order-sensitive state at all), whereas predicate facts are
     inherently per-program-point and need the dominator walk's own
     per-block state threading.  */
  hash_map<tree, oa_predicate_fact_lite> predicate_seed;
  seed_predicate_self_trust (fun, predicate_seed);
  predicate_dom_walker pdw (&predicate_seed);
  pdw.walk (ENTRY_BLOCK_PTR_FOR_FN (fun));

  return 0;
}

} // anon namespace

static gimple_opt_pass *
make_pass_gimple_object_address (gcc::context *ctxt)
{
  return new pass_gimple_object_address (ctxt);
}

int
plugin_init (struct plugin_name_args *plugin_info,
	     struct plugin_gcc_version *version)
{
  const char *plugin_name = plugin_info->base_name;

  if (!plugin_default_version_check (version, &gcc_version))
    return 1;

  struct register_pass_info pass_info;
  pass_info.pass = make_pass_gimple_object_address (g);
  pass_info.reference_pass_name = "ssa";
  pass_info.ref_pass_instance_number = 1;
  pass_info.pos_op = PASS_POS_INSERT_AFTER;

  register_callback (plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &pass_info);
  return 0;
}
