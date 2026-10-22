/* D4324 contract static analysis, GIMPLE-pass alternative.

   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* An in-tree, built-in counterpart of the experimental testsuite
   plugin gcc/testsuite/g++.dg/plugin/gimple_object_address_plugin.cc,
   gated by two new, independent flags (-fcontract-conveyor-proofs-
   gimple/-fcontract-symbolic-proofs-gimple) mirroring contracts.cc's
   own -fcontract-conveyor-proofs/-fcontract-symbolic-proofs, run as a
   real gimple_opt_pass right after SSA is built ("ssa"), instead of
   contracts.cc's own AST-walk (hooked at PLUGIN_PRE_GENERICIZE).

   Registered from init_contracts (contracts.cc) via a direct
   register_pass call, the same way a plugin's own
   PLUGIN_PASS_MANAGER_SETUP callback would -- NOT listed in
   passes.def, since passes.def/pass-instances.def are shared by every
   language driver via libbackend.a, and make_pass_contracts_gimple
   only exists in the C++ front end's own object file; referencing it
   from passes.def leaves cc1/lto1 with an unresolved symbol at link
   time.

   Full design rationale, the validated plugin prototype this ports
   from, and the one-way-trust model this replicates:
   ~/gimple-contract-analysis.md (outside this repo).

   Design (same as the plugin prototype): never analyzes a contract's
   own outlined GIMPLE machinery (F.pre/F.post/the predicate-core
   function/the thunk) -- reads a function's *declared* precondition/
   postcondition text directly off its own FUNCTION_DECL
   (get_fn_contract_specifiers/CONTRACT_CONDITION), and does the "is
   this argument provably true, right here" part with ordinary GIMPLE/
   SSA reasoning. Flavor (conveyor-active vs symbolic-active) is read
   via the *cached* accessors (oa_contract_conveyor_active_cached_p/
   oa_contract_symbolic_active_cached_p) -- calling the real, semantic-
   analysis-backed oa_contract_conveyor_active_p/oa_contract_symbolic_
   active_p directly from here was tried first and found, by direct
   testing, to silently answer incorrectly this late; see the cache's
   own comment in contracts.cc for the full account.

   One-way trust, replicated from the AST-walk's own shared substrate
   (see oa_predicate_fact's own comment in contracts.cc): a fact
   established by a conveyor-active contract is trustworthy enough for
   a symbolic-flavored obligation to rely on; a fact established by a
   symbolic-active contract must never satisfy a conveyor-flavored
   obligation. CONVEYOR_ESTABLISHED, carried alongside every fact this
   file tracks, is exactly that provenance tag.

   Full parity with the validated plugin prototype: is_object_address,
   nonzero-ness, general numeric ranges, named predicates, and
   ptr->field ranges, each with self-trust, call-site consult, and
   item 6's postcondition-return-value guarantee (the two persistent-
   object fact shapes -- named predicates, ptr->field ranges -- via
   their own dominator-tree dataflow instead, see cg_predicate_dom_
   walker below). IILE recursion remains permanently out of scope, per
   the same instruction that applied to the plugin prototype.

   One-way trust applies to is_object_address/nonzero/named-predicates/
   ptr->field-ranges (see cg_fact/cg_pred_fact/cg_field_fact below) but
   deliberately NOT to general ranges: mirroring contracts.cc's own
   m_contract_scalar_range_map (consulted identically by both
   -fcontract-conveyor-proofs and -fcontract-symbolic-proofs, with no
   conveyor_established tag at all, unlike oa_predicate_fact/
   oa_contract_field_range_fact, which do carry it), a range fact here
   carries no flavor provenance -- only whether the specific contract
   that established it was flavor-enabled at all (so self-trust/
   consult still only happen when the matching -fcontract-*-proofs-
   gimple flag is on).  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "predict.h"
#include "function.h"
#include "dominance.h"
#include "cfg.h"
#include "basic-block.h"
#include "cp-tree.h"
#include "contracts.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-walk.h"
#include "is-a.h"
#include "tree-dfa.h"
#include "tree-pass.h"
#include "context.h"
#include "diagnostic.h"
#include "intl.h"
#include "stringpool.h"
#include "gimple-range.h"
#include "cfganal.h"
#include "hash-traits.h"
#include "tree-pretty-print.h"
#include "cgraph.h"
#include "cfghooks.h"
#include "gimple-ssa.h"
#include "bitmap.h"
#include "sbitmap.h"
#include "opts.h"

/* Positional correspondence between CALLEE's own PARM_DECLs and CALL's
   actual argument expressions -- the GIMPLE-level analogue of
   contracts.cc's own oa_substitute_call_arg.  */

static bool
cg_find_param_position (tree callee, tree parm, unsigned *argno_out)
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

/* One tracked fact: is VAL provable, and was the specific contract
   that established it conveyor-active (the one-way-trust provenance
   tag, see this file's own top comment)?  */

struct cg_fact { bool conveyor_established; };

/* A *relational* fact -- "the SSA name this is keyed on CODE RHS
   holds", e.g. for 'pre<ctrl>(x < q)', keyed on x's own SSA name, with
   CODE LT_EXPR and RHS q's own SSA name. Structurally identical to
   cg_fact (same one-way-trust tag), just carrying a second SSA name
   and a comparison code instead of nothing. Mirrors contracts.cc's own
   oa_relational_fact exactly -- see that struct's own comment and
   oa_match_comparison_against_param's for why neither side is ever
   resolved to a value.  */

/* A numeric-interval fact -- see this file's own top comment for why,
   unlike cg_fact, this carries no conveyor_established provenance tag
   (mirroring contracts.cc's own m_contract_scalar_range_map). Defined
   here (rather than alongside cg_established_range_of further below,
   which also uses it) because cg_rel_fact/cg_call_rel_fact's own OFFSET
   field, immediately below, needs it before that point in the file.  */

struct cg_range_lite
{
  bool has_lo = false, has_hi = false;
  widest_int lo = 0, hi = 0;
};

/* This engine's own local mirror of contracts.cc's oa_range_fact_text
   (see .claude/plans/lazy-stirring-pearl.md) -- same rationale and
   calling convention, just over this file's own CG_RANGE_LITE rather
   than sharing contracts.cc's private OA_RANGE_FACT type.  */

static char *
cg_range_lite_text (const cg_range_lite &fact, char *buf, size_t n)
{
  if (fact.has_lo && fact.has_hi && fact.lo == fact.hi)
    snprintf (buf, n, "exactly " HOST_WIDE_INT_PRINT_DEC, fact.lo.to_shwi ());
  else if (fact.has_lo && fact.has_hi)
    snprintf (buf, n, "in [" HOST_WIDE_INT_PRINT_DEC ", "
	      HOST_WIDE_INT_PRINT_DEC "]",
	      fact.lo.to_shwi (), fact.hi.to_shwi ());
  else if (fact.has_lo)
    snprintf (buf, n, ">= " HOST_WIDE_INT_PRINT_DEC, fact.lo.to_shwi ());
  else if (fact.has_hi)
    snprintf (buf, n, "<= " HOST_WIDE_INT_PRINT_DEC, fact.hi.to_shwi ());
  else
    snprintf (buf, n, "unconstrained");
  return buf;
}

/* OFFSET (D4324 Commit 2, generalized to an interval in Commit 4):
   mirrors contracts.cc's own oa_relational_fact exactly -- the fact
   actually holds for '(the SSA name this is keyed on - OFFSET) CODE
   RHS', not literally 'SSA name CODE RHS'; self-trust-established facts
   always have OFFSET an exact-zero point interval, and a later 'name2 =
   name +/- k' shifts a copy of NAME's own fact for NAME2 by
   accumulating k into OFFSET. OFFSET reuses cg_range_lite (this file's
   own numeric-interval type) rather than a single widest_int so K need
   not itself be a compile-time constant (Commit 4). See cg_get_
   relational's own comment for the transfer function.  */
struct cg_rel_fact { tree_code code; tree rhs; cg_range_lite offset; bool conveyor_established; };

/* Item 8's overflow check: mirrors contracts.cc's own oa_type_bound_fact
   exactly -- a decl has been compared less-than (HAS_UPPER_WITNESS) or
   greater-than (HAS_LOWER_WITNESS) *something* of a no-wider integral
   type, established via oa_match_type_bounded_comparison (shared,
   exported from contracts.cc -- a pure tree-shape matcher, no oa_env
   involved, so no reimplementation needed here). See cg_provably_safe_
   unit_shift_p's own comment for the full soundness argument.  */
struct cg_type_bound_fact { bool has_upper_witness = false, has_lower_witness = false; };

/* The call analogue of cg_rel_fact immediately above -- "the SSA name
   this is keyed on CODE RHS_RECEIVER.RHS_CALLEE () holds", e.g. for
   'pre<ctrl>(i < v.size ())'.  Mirrors contracts.cc's own oa_call_
   relational_fact exactly.  */

struct cg_call_rel_fact
{
  tree_code code;
  tree rhs_receiver;
  tree rhs_callee;
  /* See cg_rel_fact's own comment on OFFSET.  */
  cg_range_lite offset;
  bool conveyor_established;
};

/* Composite key for a (identity, FUNCTION_DECL/FIELD_DECL)-keyed map --
   pair_hash (hash-traits.h) combines two ordinary pointer-hash traits,
   the same idiom contracts.cc's own oa_field_key_hash uses. Defined here
   (rather than alongside cg_dom_fact_state further below, which also
   uses it) because cg_call_call_rel_fact immediately below, and
   cg_seed_self_trust/cg_check_call's own hash_map parameters, need it
   before that point in the file.  */
typedef pair_hash<nofree_ptr_hash<tree_node>, nofree_ptr_hash<tree_node>>
  cg_field_key_hash;

/* The call-vs-call analogue of cg_call_rel_fact immediately above --
   "the (LHS_RECEIVER identity, LHS_CALLEE) pair this is keyed on CODE
   RHS_RECEIVER.RHS_CALLEE () holds", e.g. for 'pre<ctrl>(v.size () < w.
   size ())'. Unlike cg_call_rel_fact (keyed on a single SSA name/decl,
   LHS is implicit), this shape's own LHS is itself a call, so it can't
   be the map key by itself either -- mirrors contracts.cc's own
   oa_call_call_relational_fact exactly, including the reason it's a
   distinct struct rather than a reused one.  */
struct cg_call_call_rel_fact
{
  tree_code code;
  tree rhs_receiver;
  tree rhs_callee;
  bool conveyor_established;
};

/* D4324 Commit 4: mirrors contracts.cc's own oa_range_fact_exact/
   _negate/_accumulate/_equal exactly, for cg_range_lite instead of
   oa_range_fact (no BASE field here to carry along -- cg_range_lite
   never had one).  */

static cg_range_lite
cg_range_lite_exact (widest_int val)
{
  cg_range_lite r;
  r.has_lo = r.has_hi = true;
  r.lo = r.hi = val;
  return r;
}

static void
cg_range_lite_negate (cg_range_lite &r)
{
  bool old_has_lo = r.has_lo, old_has_hi = r.has_hi;
  widest_int old_lo = r.lo, old_hi = r.hi;
  r.has_lo = old_has_hi;
  r.has_hi = old_has_lo;
  if (r.has_lo)
    r.lo = -old_hi;
  if (r.has_hi)
    r.hi = -old_lo;
}

static void
cg_range_lite_accumulate (cg_range_lite &acc, const cg_range_lite &shift)
{
  acc.has_lo = acc.has_lo && shift.has_lo;
  acc.has_hi = acc.has_hi && shift.has_hi;
  if (acc.has_lo)
    acc.lo += shift.lo;
  if (acc.has_hi)
    acc.hi += shift.hi;
}

static bool
cg_range_lite_equal (const cg_range_lite &a, const cg_range_lite &b)
{
  return a.has_lo == b.has_lo && a.has_hi == b.has_hi
	 && (!a.has_lo || a.lo == b.lo) && (!a.has_hi || a.hi == b.hi);
}

/* Standard interval multiplication -- mirrors contracts.cc's own
   oa_range_multiply exactly (see that function's own comment for the
   full corner-product rationale, including the partial/one-sided-
   result correction), just for cg_range_lite instead of oa_range_fact
   (no BASE field to decline here -- cg_range_lite never had one, so
   there is no pointer-tracked-range case to rule out).  */

static bool
cg_range_lite_multiply (const cg_range_lite &a, const cg_range_lite &b,
			  bool *has_lo_out, widest_int *lo_out,
			  bool *has_hi_out, widest_int *hi_out)
{
  *has_lo_out = *has_hi_out = false;

  if (a.has_lo && a.has_hi && b.has_lo && b.has_hi)
    {
      widest_int corner0 = a.lo * b.lo;
      widest_int corner1 = a.lo * b.hi;
      widest_int corner2 = a.hi * b.lo;
      widest_int corner3 = a.hi * b.hi;
      *lo_out = wi::smin (wi::smin (corner0, corner1), wi::smin (corner2, corner3));
      *hi_out = wi::smax (wi::smax (corner0, corner1), wi::smax (corner2, corner3));
      *has_lo_out = *has_hi_out = true;
      return true;
    }

  bool a_nonneg = a.has_lo && a.lo >= 0;
  bool a_nonpos = a.has_hi && a.hi <= 0;
  bool b_nonneg = b.has_lo && b.lo >= 0;
  bool b_nonpos = b.has_hi && b.hi <= 0;

  if (a_nonneg && b_nonneg)
    {
      *lo_out = a.lo * b.lo;
      *has_lo_out = true;
      if (a.has_hi && b.has_hi)
	{
	  *hi_out = a.hi * b.hi;
	  *has_hi_out = true;
	}
    }
  else if (a_nonpos && b_nonpos)
    {
      *lo_out = a.hi * b.hi;
      *has_lo_out = true;
      if (a.has_lo && b.has_lo)
	{
	  *hi_out = a.lo * b.lo;
	  *has_hi_out = true;
	}
    }
  else if (a_nonneg && b_nonpos)
    {
      *hi_out = a.lo * b.hi;
      *has_hi_out = true;
      if (a.has_hi && b.has_lo)
	{
	  *lo_out = a.hi * b.lo;
	  *has_lo_out = true;
	}
    }
  else if (a_nonpos && b_nonneg)
    {
      *hi_out = a.hi * b.lo;
      *has_hi_out = true;
      if (a.has_lo && b.has_hi)
	{
	  *lo_out = a.lo * b.hi;
	  *has_lo_out = true;
	}
    }

  return *has_lo_out || *has_hi_out;
}

/* Combine CODE/VAL (one comparison conjunct, e.g. 'x >= 20') into R --
   mirrors contracts.cc's own oa_tighten_range_bound.  */

static void
cg_tighten_range_bound (cg_range_lite &r, tree_code code, const widest_int &val)
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

/* The range-vs-range analogue of cg_tighten_range_bound above: folds
   OTHER's own worst-case bound (not a single literal point) into R --
   mirrors contracts.cc's own oa_tighten_range_bound_from_range, used only
   by cg_collect_call_range_groups_parametric below, where the "other
   side" of a call-range conjunct is itself a range (a substituted
   parameter's own established range), not an already-known literal.  */

static void
cg_tighten_range_bound_from_range (cg_range_lite &r, tree_code code,
				     const cg_range_lite &other)
{
  switch (code)
    {
    case LT_EXPR:
      if (other.has_hi && (!r.has_hi || r.hi > other.hi - 1))
	{ r.has_hi = true; r.hi = other.hi - 1; }
      break;
    case LE_EXPR:
      if (other.has_hi && (!r.has_hi || r.hi > other.hi))
	{ r.has_hi = true; r.hi = other.hi; }
      break;
    case GT_EXPR:
      if (other.has_lo && (!r.has_lo || r.lo < other.lo + 1))
	{ r.has_lo = true; r.lo = other.lo + 1; }
      break;
    case GE_EXPR:
      if (other.has_lo && (!r.has_lo || r.lo < other.lo))
	{ r.has_lo = true; r.lo = other.lo; }
      break;
    case EQ_EXPR:
      if (other.has_lo && (!r.has_lo || r.lo < other.lo))
	{ r.has_lo = true; r.lo = other.lo; }
      if (other.has_hi && (!r.has_hi || r.hi > other.hi))
	{ r.has_hi = true; r.hi = other.hi; }
      break;
    default:
      break;
    }
}

/* The GIMPLE mirror of contracts.cc's own oa_range_pair_relation (see the
   bounds-proving demo, .claude/plans/lazy-stirring-pearl.md) -- compares
   two independently-tracked cg_range_lite values related by an arbitrary
   comparison CODE, reusing oa_range_subsumption_result (contracts.h, now
   shared with the AST engine for exactly this reuse) rather than a
   parallel enum. A deliberate, small duplication of the AST helper's own
   logic operating on GIMPLE's own range struct, matching this file's
   existing convention of mirroring AST helpers with GIMPLE's own types
   (e.g. cg_tighten_range_bound above already mirrors oa_tighten_range_
   bound the same way) rather than reaching across the AST/GIMPLE
   boundary for the computation itself.  */

static enum oa_range_subsumption_result
cg_range_pair_relation (const cg_range_lite &a, tree_code code,
			  const cg_range_lite &b)
{
  switch (code)
    {
    case LT_EXPR:
      if (a.has_hi && b.has_lo && a.hi < b.lo) return OA_RANGE_SUBSUMED;
      if (a.has_lo && b.has_hi && a.lo >= b.hi) return OA_RANGE_DISJOINT;
      return OA_RANGE_PARTIAL;
    case LE_EXPR:
      if (a.has_hi && b.has_lo && a.hi <= b.lo) return OA_RANGE_SUBSUMED;
      if (a.has_lo && b.has_hi && a.lo > b.hi) return OA_RANGE_DISJOINT;
      return OA_RANGE_PARTIAL;
    case GT_EXPR:
      if (a.has_lo && b.has_hi && a.lo > b.hi) return OA_RANGE_SUBSUMED;
      if (a.has_hi && b.has_lo && a.hi <= b.lo) return OA_RANGE_DISJOINT;
      return OA_RANGE_PARTIAL;
    case GE_EXPR:
      if (a.has_lo && b.has_hi && a.lo >= b.hi) return OA_RANGE_SUBSUMED;
      if (a.has_hi && b.has_lo && a.hi < b.lo) return OA_RANGE_DISJOINT;
      return OA_RANGE_PARTIAL;
    case EQ_EXPR:
      if (a.has_lo && a.has_hi && b.has_lo && b.has_hi
	  && a.lo == a.hi && b.lo == b.hi && a.lo == b.lo)
	return OA_RANGE_SUBSUMED;
      if ((a.has_hi && b.has_lo && a.hi < b.lo)
	  || (a.has_lo && b.has_hi && a.lo > b.hi))
	return OA_RANGE_DISJOINT;
      return OA_RANGE_PARTIAL;
    default:
      return OA_RANGE_PARTIAL;
    }
}

/* Accumulate every 'TARGET OP const'-shaped conjunct of CONJUNCTS
   naming TARGET into a single combined range in *OUT -- e.g. 'x >= 20
   && x < 100' becomes [20, 100).  Returns false (leaving *OUT
   untouched) if no conjunct named TARGET at all.  */

static bool
cg_extract_conjunct_range (vec<tree *> &conjuncts, tree target,
			    cg_range_lite *out)
{
  cg_range_lite acc;
  bool any = false;
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree param, const_val;
      tree_code code;
      if (oa_match_simple_comparison (*conjuncts[i], &param, &code, &const_val)
	  && param == target && TREE_CODE (const_val) == INTEGER_CST)
	{
	  cg_tighten_range_bound (acc, code, wi::to_widest (const_val));
	  any = true;
	}
    }
  if (any)
    *out = acc;
  return any;
}

/* Item 6 for ranges: does CALLEE have a declared, flavor-matching (per
   REQUIRE_CONVEYOR/REQUIRE_SYMBOLIC) postcondition that unconditionally
   guarantees a range for its own return value?  */

static bool
cg_call_postcondition_range_p (tree callee, bool require_conveyor,
				bool require_symbolic, cg_range_lite *out)
{
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      bool conveyor_active = oa_contract_conveyor_active_cached_p (contract);
      bool symbolic_active = oa_contract_symbolic_active_cached_p (contract);
      if (require_conveyor && !conveyor_active)
	continue;
      if (require_symbolic && !symbolic_active)
	continue;
      if (!require_conveyor && !require_symbolic)
	continue;
      tree result_id = POSTCONDITION_IDENTIFIER (contract);
      if (!result_id)
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      if (cg_extract_conjunct_range (conjuncts, result_id, out))
	return true;
    }
  return false;
}

/* If VAL is an SSA_NAME whose def-stmt is a call through an implicit,
   single-argument conversion operator (DECL_CONV_FN_P) -- e.g.
   'wrap::operator int (q)' (a reference-passed receiver) or
   'wrap::operator int (&q)' (a by-value receiver) -- return that
   receiver, with one ADDR_EXPR peeled off if present; otherwise return
   VAL itself, unchanged.  The GIMPLE-level analogue of contracts.cc's
   own oa_strip_conversion_call, minus the ordinary-wrapper/TARGET_EXPR
   handling that function also does: GIMPLE has no equivalent nodes for
   either (SSA form and gimplification already normalize both away), so
   a call through a conversion operator is the entire remaining shape to
   recognize here. Shared by every consult function below that used to
   stop at "the callee's own postcondition, or nothing" for a
   GIMPLE_CALL def-stmt, and by cg_check_call's own relational
   obligation check (which needs to resolve a *second*, non-recursed-
   into operand the same way before comparing it against an already-
   established fact's own RHS).

   Also chases a plain SSA copy/conversion hop (an ordinary GIMPLE_ASSIGN
   whose own rhs code is CONVERT_EXPR-like or a bare SSA_NAME copy)
   before/between conversion-operator calls, and keeps going until
   neither shape applies -- mirroring cg_get_relational's own identical
   def-stmt walk. Found via direct testing (d4324-gimple-conversion-
   relational-ok.C, debug-instrumented): a multi-argument call's own
   argument-evaluation order can leave one argument's conversion-operator
   result referenced *directly* by the call (so cg_get_relational's own
   internal, self-recursing walk -- used for the first, "sub_param" side
   of a relational obligation -- resolves it fine) while another
   argument's identical conversion-operator result first gets copied
   through an extra SSA temp before being passed (so this function's own
   single-shot, non-looping version -- used standalone for the *second*,
   "sub_other" side, which has no such internal recursion of its own --
   stopped one hop early, at the copy, never reaching the call
   underneath). The two operands must resolve through an *equally deep*
   walk or a genuinely identical value is spuriously reported as two
   different objects.  */

static tree
cg_resolve_conversion_receiver (tree val)
{
  tree cur = val;
  bool saw_conversion_call = false;
  while (cur != NULL_TREE && TREE_CODE (cur) == SSA_NAME)
    {
      gimple *def = SSA_NAME_DEF_STMT (cur);
      if (!def)
	break;
      if (is_gimple_assign (def))
	{
	  enum tree_code code = gimple_assign_rhs_code (def);
	  if (!CONVERT_EXPR_CODE_P (code) && code != SSA_NAME)
	    break;
	  cur = gimple_assign_rhs1 (def);
	  continue;
	}
      if (!is_gimple_call (def))
	break;
      gcall *call = as_a <gcall *> (def);
      tree callee = gimple_call_fndecl (call);
      if (!callee || !DECL_CONV_FN_P (callee) || gimple_call_num_args (call) != 1)
	break;
      tree receiver = gimple_call_arg (call, 0);
      if (TREE_CODE (receiver) == ADDR_EXPR)
	receiver = TREE_OPERAND (receiver, 0);
      cur = receiver;
      saw_conversion_call = true;
    }
  /* Only commit to whatever CUR ended up at if the walk actually passed
     through a real conversion-operator call somewhere along the way --
     an ordinary value with no such call anywhere in its own def chain
     (e.g. a plain 'int y = make_val (x, q);' with no conversion operator
     in sight) must come back completely unresolved, VAL itself, not
     whatever unrelated SSA name a *plain* copy/convert hop happened to
     land on. Confirmed via direct testing (a genuine regression caught
     by the full narrow contracts suite): chasing a plain copy hop
     unconditionally, with no such gate, made this function -- now also
     reached generically via cg_resolve_call_argument for every call
     argument, not just ones actually involving a conversion operator --
     walk straight past an ordinary decl-keyed relational fact's own
     recognizable identity into its own value-producing call's opaque
     result, breaking both the self-trust key match (a spurious "wrong
     identity" where the two sides used to agree) and this argument's
     own diagnostic name (printed as '<unknown>' instead of the real
     parameter name).  */
  return saw_conversion_call ? cur : val;
}

/* If ARG is 'ADDR_EXPR (temp)' for some local VAR_DECL TEMP -- the
   shape a class-typed argument forwarded BY VALUE to CALL takes when
   its own type isn't trivially copyable (Itanium ABI "non-trivial for
   the purposes of calls": passed by invisible reference, materialized
   into a temporary the caller constructs) -- search backward through
   CALL's own containing basic block (from CALL towards the block's
   start) for a GIMPLE_CALL whose own first argument is the same
   ADDR_EXPR (TEMP) and whose callee is a copy or move constructor;
   if found, return its own *last* argument (a copy/move constructor
   has exactly one user-visible parameter, always last -- see
   contracts.cc's own oa_strip_conversion_call, which found by direct
   testing that an extra, compiler-internal leading argument can
   precede it, so the count alone isn't reliable), recursed through
   cg_resolve_conversion_receiver too in case of a further chained
   conversion-operator call. Otherwise return ARG unchanged.

   Confirmed via -fdump-tree-ssa that the constructing call and the
   forwarding call remain in the same basic block even when the
   temporary's own type has a non-trivial destructor (whose GENERIC-
   level try/finally region collapses away here, since no exception
   path is actually reachable) -- this deliberately does not search
   beyond CALL's own block, consistent with this file's own "decline
   rather than guess" discipline for any shape reached a different
   way.  */

static tree
cg_resolve_copy_construction_receiver (gcall *call, tree arg)
{
  if (TREE_CODE (arg) != ADDR_EXPR)
    return arg;
  tree temp = TREE_OPERAND (arg, 0);
  if (!VAR_P (temp))
    return arg;

  for (gimple_stmt_iterator gsi = gsi_for_stmt (call); !gsi_end_p (gsi);
       gsi_prev (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == call || !is_gimple_call (stmt))
	continue;
      gcall *ctor_call = as_a <gcall *> (stmt);
      if (gimple_call_num_args (ctor_call) < 1)
	continue;
      tree ctor_recv = gimple_call_arg (ctor_call, 0);
      if (TREE_CODE (ctor_recv) != ADDR_EXPR || TREE_OPERAND (ctor_recv, 0) != temp)
	continue;
      tree ctor = gimple_call_fndecl (ctor_call);
      if (!ctor || !(DECL_COPY_CONSTRUCTOR_P (ctor) || DECL_MOVE_CONSTRUCTOR_P (ctor)))
	continue;
      unsigned nargs = gimple_call_num_args (ctor_call);
      if (nargs < 2)
	continue;
      tree source = gimple_call_arg (ctor_call, nargs - 1);
      if (TREE_CODE (source) == ADDR_EXPR)
	source = TREE_OPERAND (source, 0);
      return cg_resolve_conversion_receiver (source);
    }
  return arg;
}

/* Whether statement A dominates statement B: A comes first in B's block,
   or A's block dominates B's.  */

static bool
cg_stmt_dominates_p (gimple *a, gimple *b)
{
  basic_block ba = gimple_bb (a), bb = gimple_bb (b);
  if (!ba || !bb)
    return false;
  if (ba == bb)
    {
      for (gimple_stmt_iterator gsi = gsi_for_stmt (a); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	if (gsi_stmt (gsi) == b)
	  return true;
      return false;
    }
  return (dom_info_available_p (CDI_DOMINATORS)
	  && dominated_by_p (CDI_DOMINATORS, bb, ba));
}

/* The value scalar variable DECL, an address-taken local or parameter
   that therefore lives in memory, holds just before statement AT: an
   SSA name or constant, or NULL_TREE if unknown.  A load of DECL in the
   same memory state that dominates AT gives it -- so do the facts a
   branch established on that load ('if (y < 50)').  Otherwise the
   store that state comes from does, found by walking back through
   statements that can't modify DECL.  */

static tree
cg_memory_value_at (tree decl, gimple *at)
{
  if (!(VAR_P (decl) || TREE_CODE (decl) == PARM_DECL)
      || !(INTEGRAL_TYPE_P (TREE_TYPE (decl))
	   || SCALAR_FLOAT_TYPE_P (TREE_TYPE (decl))
	   || POINTER_TYPE_P (TREE_TYPE (decl))))
    return NULL_TREE;
  tree vuse = gimple_vuse (at);
  if (!vuse)
    return NULL_TREE;

  imm_use_iterator it;
  use_operand_p use_p;
  FOR_EACH_IMM_USE_FAST (use_p, it, vuse)
    {
      gimple *u = USE_STMT (use_p);
      if (u != at && gimple_assign_single_p (u)
	  && gimple_assign_rhs1 (u) == decl
	  && TREE_CODE (gimple_assign_lhs (u)) == SSA_NAME
	  && cg_stmt_dominates_p (u, at))
	return gimple_assign_lhs (u);
    }

  ao_ref ref;
  ao_ref_init (&ref, decl);
  for (int steps = 0; steps < 64 && vuse; ++steps)
    {
      gimple *d = SSA_NAME_DEF_STMT (vuse);
      if (!d || gimple_nop_p (d) || is_a <gphi *> (d))
	return NULL_TREE;
      if (gimple_assign_single_p (d)
	  && operand_equal_p (gimple_assign_lhs (d), decl, 0))
	{
	  if (gimple_clobber_p (d))
	    return NULL_TREE;
	  tree rhs = gimple_assign_rhs1 (d);
	  return (TREE_CODE (rhs) == SSA_NAME || CONSTANT_CLASS_P (rhs)
		  ? rhs : NULL_TREE);
	}
      if (stmt_may_clobber_ref_p_1 (d, &ref, false))
	return NULL_TREE;
      vuse = gimple_vuse (d);
    }
  return NULL_TREE;
}

/* If VAL is an SSA name loaded from a scalar variable in memory, the
   value that variable held at the load (cg_memory_value_at); else
   NULL_TREE.  */

static tree
cg_forwarded_load_value (tree val)
{
  if (TREE_CODE (val) != SSA_NAME)
    return NULL_TREE;
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (!def || !gimple_assign_single_p (def) || !gimple_vuse (def))
    return NULL_TREE;
  tree rhs = gimple_assign_rhs1 (def);
  if (!VAR_P (rhs) && TREE_CODE (rhs) != PARM_DECL)
    return NULL_TREE;
  tree v = cg_memory_value_at (rhs, def);
  return v != val ? v : NULL_TREE;
}

/* CALL's own ARGNO-th actual argument, resolved through both a
   conversion-operator call (cg_resolve_conversion_receiver) and a
   copy/move-constructor materialization (cg_resolve_copy_construction_
   receiver above) -- the single entry point cg_check_call/cg_call_
   postcondition_relation_p use instead of bare gimple_call_arg, so a
   class-typed argument forwarded from the caller's own decl (via
   conversion, via copy, or both chained) is recognized uniformly.
   Copy-construction resolution must run first: its own input shape
   (ADDR_EXPR of a memory temp) is exactly what cg_resolve_conversion_
   receiver's own SSA_NAME check would otherwise reject outright.  */

static tree
cg_resolve_call_argument (gcall *call, unsigned argno)
{
  tree arg = gimple_call_arg (call, argno);
  arg = cg_resolve_copy_construction_receiver (call, arg);
  arg = cg_resolve_conversion_receiver (arg);
  return arg;
}

/* As cg_resolve_call_argument, for a value obligation ('pre (x < 100)',
   'pre (x != 0)'): a reference-to-scalar parameter names the object it's
   bound to, so its argument, that object's address ('&local'), stands
   for the object's value at CALL (cg_memory_value_at).  A class-typed
   reference keeps its identity, which field facts are keyed on, and an
   is_object_address obligation needs the address itself.  */

static tree
cg_resolve_value_argument (gcall *call, unsigned argno)
{
  tree arg = cg_resolve_call_argument (call, argno);
  if (TREE_CODE (arg) == ADDR_EXPR)
    if (tree callee = gimple_call_fndecl (call))
      {
	tree parm = DECL_ARGUMENTS (callee);
	for (unsigned i = 0; parm && i < argno; ++i)
	  parm = DECL_CHAIN (parm);
	tree obj = TREE_OPERAND (arg, 0);
	if (parm && TYPE_REF_P (TREE_TYPE (parm))
	    && (VAR_P (obj) || TREE_CODE (obj) == PARM_DECL))
	  if (tree v = cg_memory_value_at (obj, call))
	    return v;
      }
  return arg;
}


/* Blocks entered only through the edge testing a short-circuit
   condition's result ('if (retval != 0)' after 'a && b'), each with the
   edges into the join that set that result to the value the edge needs
   (cg_bool_phi_source_edges).  The fact walk takes its state from those
   edges (cg_compute_in_state) and records them here, so a range query
   can do the same: the ranger alone can't see through the PHI of
   constants, so it loses what each conjunct established.  Set for the
   duration of the check of one function.  */

static hash_map<basic_block, auto_vec<edge> *> *cg_short_circuit_sources;

/* Is VAL, at STMT, provably an object address?  As
   cg_provable_object_address_p, except that if VAL is a PHI in the join
   of a short-circuit condition whose guarded block dominates STMT, only
   the arguments arriving along the edges that make the condition true
   matter: only those paths reach STMT ('if (a && (p = &x) != nullptr)'
   leaves p null only where the condition is false).  */

static bool cg_provable_object_address_p (tree, hash_map<tree, cg_fact> &,
					  bool, hash_set<tree> &,
					  oa_unprovable_reason *);

static bool
cg_provable_object_address_at_p (tree val, gimple *stmt,
				 hash_map<tree, cg_fact> &established,
				 bool require_conveyor,
				 oa_unprovable_reason *reason)
{
  hash_set<tree> in_progress;
  if (cg_provable_object_address_p (val, established, require_conveyor,
				    in_progress, reason))
    return true;
  if (!cg_short_circuit_sources || TREE_CODE (val) != SSA_NAME
      || !stmt || !gimple_bb (stmt) || !dom_info_available_p (CDI_DOMINATORS))
    return false;
  gphi *phi = dyn_cast <gphi *> (SSA_NAME_DEF_STMT (val));
  if (!phi)
    return false;
  for (basic_block bb = gimple_bb (stmt); bb;
       bb = get_immediate_dominator (CDI_DOMINATORS, bb))
    {
      auto_vec<edge> **srcs = cg_short_circuit_sources->get (bb);
      if (!srcs)
	continue;
      if ((*srcs)->is_empty ())
	return false;
      for (edge qe : **srcs)
	{
	  if (qe->dest != gimple_bb (phi))
	    return false;
	  hash_set<tree> ip;
	  if (!cg_provable_object_address_p (PHI_ARG_DEF_FROM_EDGE (phi, qe),
					     established, require_conveyor, ip,
					     NULL))
	    return false;
	}
      return true;
    }
  return false;
}

/* Narrow VR, the ranger's range of VAL at STMT, by the union of VAL's
   ranges along the short-circuit edges the nearest dominating recorded
   block stands for -- every path to STMT passes through one of them.  */

static void
cg_narrow_by_short_circuit (vrange &vr, tree val, gimple *stmt,
			    gimple_ranger *ranger)
{
  if (!cg_short_circuit_sources || !stmt || !gimple_bb (stmt)
      || TREE_CODE (val) != SSA_NAME
      || !dom_info_available_p (CDI_DOMINATORS))
    return;
  basic_block def_bb = (SSA_NAME_IS_DEFAULT_DEF (val) ? NULL
			: gimple_bb (SSA_NAME_DEF_STMT (val)));
  for (basic_block bb = gimple_bb (stmt); bb;
       bb = get_immediate_dominator (CDI_DOMINATORS, bb))
    {
      auto_vec<edge> **srcs = cg_short_circuit_sources->get (bb);
      if (!srcs)
	continue;
      value_range u (TREE_TYPE (val));
      u.set_undefined ();
      for (edge qe : **srcs)
	{
	  /* VAL must already be defined where the edge leaves.  */
	  if (def_bb && !dominated_by_p (CDI_DOMINATORS, qe->src, def_bb))
	    return;
	  value_range r (TREE_TYPE (val));
	  if (!ranger->range_on_edge (r, qe, val))
	    return;
	  u.union_ (r);
	}
      if (!u.undefined_p ())
	vr.intersect (u);
      return;
    }
}


/* Resolve VAL's own range, trying, in order: a literal constant; a
   self-trusted/item-6 fact in ESTABLISHED_RANGE (keyed exactly like
   ESTABLISHED/ESTABLISHED_NZ elsewhere in this file, gated the same
   REQUIRE_CONVEYOR/REQUIRE_SYMBOLIC way for item 6, since that reads a
   *declared* postcondition, which does need flavor-matching); a copy/
   conversion one hop back; and finally RANGER's own general-dataflow
   answer, unconditionally (real code, not a flavor-tagged axiom -- see
   this file's own top comment). A multi-sub-range irange's own outer
   envelope (lowest lower_bound, highest upper_bound) is a sound, if
   slightly coarser, stand-in for the full value set.

   AT_STMT (default NULL, preserving every pre-existing caller's own
   behavior unchanged) makes RANGER's own query context-sensitive to one
   specific program point rather than VAL's whole-function range --
   passed straight through to range_of_expr's own identical STMT
   parameter. Needed by any caller reasoning about a branch-derived
   refinement visible only right before one particular use (e.g. 'if (b
   > 0) return a / b;' -- the divisor's own *global* SSA range is not
   provably nonzero, but its range right at the division is); found via
   direct testing that omitting STMT silently answers a different,
   coarser question instead of merely being slightly less precise.  */

static bool
cg_established_range_of (tree val, hash_map<tree, cg_range_lite> &established_range,
			  hash_map<tree, cg_range_lite> &scalar_range_cache,
			  gimple_ranger *ranger, bool require_conveyor,
			  bool require_symbolic, cg_range_lite *out,
			  gimple *at_stmt = NULL);
static bool
cg_induction_variable_bound_1 (tree, hash_map<tree, cg_range_lite> &,
			       hash_map<tree, cg_range_lite> &, gimple_ranger *,
			       cg_range_lite *);

/* Why a fact about a value could not be established or does not decide
   a call's precondition, found by the fact walk for the "cannot verify"
   diagnostics of the later pass over the calls: diagnostic text only,
   set for the duration of one function's check.  */

static hash_map<tree, int> *cg_unprovable_hints;

/* The call-vs-call conjuncts of a call's precondition that
   cg_check_call_relational_fact proved from branches -- per conjunct,
   unlike its per-call verdicts, so a proven one silences no other.  */

typedef pair_hash <nofree_ptr_hash <gimple>, nofree_ptr_hash <tree_node> >
  cg_call_conjunct_hash;
static hash_set<cg_call_conjunct_hash> *cg_proven_call_call_conjuncts;

static void
cg_note_unprovable_hint (tree val, oa_unprovable_reason reason)
{
  if (cg_unprovable_hints && val && reason != OA_UNPROVABLE_NONE)
    cg_unprovable_hints->put (val, reason);
}

/* If VAL is a load, the other loads of the same location from the same
   memory state (the same virtual use) whose values are available at
   STMT, in *TWINS: the same value -- whose range at STMT a condition may
   have narrowed ('x < 100 && ++x > 0' for a reference X loads *X
   twice).  */

static void
cg_dominating_load_twins (tree val, gimple *stmt, vec<tree> *twins)
{
  if (TREE_CODE (val) != SSA_NAME || !stmt || !gimple_bb (stmt)
      || !dom_info_available_p (CDI_DOMINATORS))
    return;
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (!def || !gimple_assign_single_p (def) || !gimple_vuse (def))
    return;
  tree ref = gimple_assign_rhs1 (def);
  if (TREE_THIS_VOLATILE (ref)
      || !(TREE_CODE (ref) == MEM_REF || handled_component_p (ref)
	   || VAR_P (ref) || TREE_CODE (ref) == PARM_DECL))
    return;
  tree vuse = gimple_vuse (def);
  imm_use_iterator it;
  gimple *use;
  FOR_EACH_IMM_USE_STMT (use, it, vuse)
    {
      if (use == def || !gimple_assign_single_p (use)
	  || gimple_vuse (use) != vuse
	  || TREE_CODE (gimple_assign_lhs (use)) != SSA_NAME
	  || !operand_equal_p (gimple_assign_rhs1 (use), ref, 0)
	  || !types_compatible_p (TREE_TYPE (gimple_assign_lhs (use)),
				  TREE_TYPE (val)))
	continue;
      basic_block ub = gimple_bb (use), sb = gimple_bb (stmt);
      if (!ub)
	continue;
      bool available = false;
      if (ub != sb)
	available = dominated_by_p (CDI_DOMINATORS, sb, ub);
      else
	for (gimple_stmt_iterator gsi = gsi_start_bb (ub); !gsi_end_p (gsi);
	     gsi_next (&gsi))
	  {
	    if (gsi_stmt (gsi) == stmt)
	      break;
	    if (gsi_stmt (gsi) == use)
	      {
		available = true;
		break;
	      }
	  }
      if (available)
	twins->safe_push (gimple_assign_lhs (use));
    }
}

/* Which of the bounds of the ranger's range of integer VAL to trust.
   The ranger takes an overflow, being undefined, not to happen: of an
   operation whose result can overflow it gives the bound the overflow
   would cross clamped to the type's, as the AST engine's composition
   does not -- each bound composed of the operands' must itself be
   representable, or it is unknown ('x * 2' for x near INT_MIN: "below
   zero" to the ranger, unknown here).  An operand's unknown bound is
   unbounded.  A cycle through a loop's PHIs is assumed trusted, each
   operation in it still checked.  Cached for one function's check
   (cg_trusted_bounds_cache): a distrusted bound always, both trusted
   only once no such assumption is pending.  */

struct cg_trust { bool lo, hi; };
static hash_map<tree, cg_trust> *cg_trusted_bounds_cache;

static cg_trust
cg_trusted_bounds (tree val, gimple_ranger *ranger, unsigned depth = 0)
{
  const cg_trust all = { true, true }, none = { false, false };
  if (TREE_CODE (val) != SSA_NAME || !ranger)
    return all;
  if (cg_trusted_bounds_cache)
    if (cg_trust *known = cg_trusted_bounds_cache->get (val))
      return *known;
  if (depth > 12)
    return none;
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (!def || gimple_nop_p (def) || is_gimple_call (def))
    return all;
  static hash_set<tree> *assumed;
  if (!assumed)
    assumed = new hash_set<tree>;
  if (assumed->add (val))
    return all;
  cg_trust t = all;
  if (gphi *phi = dyn_cast <gphi *> (def))
    for (unsigned i = 0; i < gimple_phi_num_args (phi); ++i)
      {
	cg_trust a = cg_trusted_bounds (gimple_phi_arg_def (phi, i), ranger,
					depth + 1);
	t.lo &= a.lo;
	t.hi &= a.hi;
      }
  else if (is_gimple_assign (def))
    {
      tree_code code = gimple_assign_rhs_code (def);
      tree type = TREE_TYPE (val);
      bool arith = (INTEGRAL_TYPE_P (type) && TYPE_OVERFLOW_UNDEFINED (type)
		    && (code == PLUS_EXPR || code == MINUS_EXPR
			|| code == MULT_EXPR || code == NEGATE_EXPR
			|| code == ABS_EXPR || code == LSHIFT_EXPR));
      if (!arith)
	{
	  /* A copy or conversion keeps its operand's; anything else
	     derived from a distrusted bound is distrusted.  */
	  bool copy = (code == SSA_NAME || CONVERT_EXPR_CODE_P (code));
	  for (unsigned i = 1; i < gimple_num_ops (def); ++i)
	    {
	      cg_trust a = cg_trusted_bounds (gimple_op (def, i), ranger,
					      depth + 1);
	      if (copy)
		t.lo &= a.lo, t.hi &= a.hi;
	      else if (!a.lo || !a.hi)
		t = none;
	    }
	  /* INT_MIN / -1.  */
	  if ((code == TRUNC_DIV_EXPR || code == TRUNC_MOD_EXPR)
	      && INTEGRAL_TYPE_P (type) && TYPE_OVERFLOW_UNDEFINED (type))
	    {
	      int_range_max a, b;
	      tree op0 = gimple_assign_rhs1 (def), op1 = gimple_assign_rhs2 (def);
	      if (TREE_CODE (op1) != INTEGER_CST || integer_minus_onep (op1))
		{
		  bool may_min
		    = (TREE_CODE (op0) != SSA_NAME
		       || !ranger->range_of_expr (a, op0, def) || a.varying_p ()
		       || a.undefined_p ()
		       || wi::to_widest (TYPE_MIN_VALUE (type))
			  >= widest_int::from (a.lower_bound (), SIGNED));
		  if (may_min)
		    t = none;
		}
	    }
	}
      else
	{
	  /* An operand's bounds at DEF: unbounded where unknown or
	     distrusted.  */
	  struct bnd { bool has_lo, has_hi; widest_int lo, hi; };
	  auto bounds = [&] (tree op) -> bnd
	    {
	      bnd b = { false, false, 0, 0 };
	      if (TREE_CODE (op) == INTEGER_CST)
		{
		  b.has_lo = b.has_hi = true;
		  b.lo = b.hi = wi::to_widest (op);
		  return b;
		}
	      int_range_max r;
	      if (TREE_CODE (op) != SSA_NAME || !INTEGRAL_TYPE_P (TREE_TYPE (op))
		  || !ranger->range_of_expr (r, op, def)
		  || r.undefined_p () || r.varying_p ())
		return b;
	      cg_trust a = cg_trusted_bounds (op, ranger, depth + 1);
	      signop sgn = TYPE_SIGN (TREE_TYPE (op));
	      b.has_lo = a.lo;
	      b.has_hi = a.hi;
	      b.lo = widest_int::from (r.lower_bound (), sgn);
	      b.hi = widest_int::from (r.upper_bound (), sgn);
	      return b;
	    };
	  bool unary = (code == NEGATE_EXPR || code == ABS_EXPR);
	  bnd a = bounds (gimple_assign_rhs1 (def));
	  bnd b = unary ? bnd { true, true, 0, 0 }
			: bounds (gimple_assign_rhs2 (def));
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
	      /* |x| <= max (-lo, hi); >= 0 unless it overflows.  */
	      has_lo = a.has_lo, lo = 0;
	      if (a.has_lo && wi::neg_p (a.lo))
		lo = 0;
	      has_hi = a.has_lo && a.has_hi;
	      hi = wi::smax (-a.lo, a.hi);
	      break;
	    case MULT_EXPR:
	      /* Each bound from the corners, where every corner the
		 operands' signs can reach is finite.  */
	      if (a.has_lo && a.has_hi && b.has_lo && b.has_hi)
		{
		  widest_int p[4] = { a.lo * b.lo, a.lo * b.hi, a.hi * b.lo,
				      a.hi * b.hi };
		  lo = hi = p[0];
		  for (auto &v : p)
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
		  && b.hi < TYPE_PRECISION (type)
		  && a.has_lo && a.has_hi)
		{
		  widest_int f = wi::lshift (widest_int (1), b.hi.to_uhwi ());
		  widest_int p[2] = { a.lo * f, a.hi * f };
		  lo = wi::smin (wi::smin (p[0], p[1]), a.lo);
		  hi = wi::smax (wi::smax (p[0], p[1]), a.hi);
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
  assumed->remove (val);
  if (cg_trusted_bounds_cache && (!t.lo || !t.hi || assumed->is_empty ()))
    cg_trusted_bounds_cache->put (val, t);
  return t;
}

/* The ranger's range of integer VAL at STMT in *OUT, without the bounds
   cg_trusted_bounds distrusts; SGN reads them.  Narrowed by a short-
   circuit condition's outcome if SHORT_CIRCUIT.  False if no bound is
   left.  */

static bool
cg_trusted_ranger_range (tree val, gimple *stmt, gimple_ranger *ranger,
			 bool short_circuit, signop sgn, cg_range_lite *out)
{
  int_range_max vr;
  if (!ranger || TREE_CODE (val) != SSA_NAME
      || !INTEGRAL_TYPE_P (TREE_TYPE (val))
      || !ranger->range_of_expr (vr, val, stmt))
    return false;
  if (short_circuit)
    cg_narrow_by_short_circuit (vr, val, stmt, ranger);
  if (vr.undefined_p () || vr.varying_p () || vr.num_pairs () == 0)
    return false;
  cg_trust t = cg_trusted_bounds (val, ranger);
  out->has_lo = t.lo;
  out->has_hi = t.hi;
  out->lo = widest_int::from (vr.lower_bound (0), sgn);
  out->hi = widest_int::from (vr.upper_bound (vr.num_pairs () - 1), sgn);
  return t.lo || t.hi;
}

static bool
cg_established_range_of (tree val, hash_map<tree, cg_range_lite> &established_range,
			  hash_map<tree, cg_range_lite> &scalar_range_cache,
			  gimple_ranger *ranger, bool require_conveyor,
			  bool require_symbolic, cg_range_lite *out,
			  gimple *at_stmt)
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

  /* A decl-keyed fact (an address-taken, by-value class-typed parameter
     provably never written to anywhere in this function -- see
     cg_decl_safe_for_conversion_tracking_p) is a leaf: no SSA def-stmt
     exists for a plain decl, so there is nothing further to walk.  */
  if (VAR_P (val) || TREE_CODE (val) == PARM_DECL)
    {
      if (cg_range_lite *found = established_range.get (val))
	{
	  *out = *found;
	  return true;
	}
      if (cg_range_lite *found = scalar_range_cache.get (val))
	{
	  *out = *found;
	  return true;
	}
      return false;
    }

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  if (cg_range_lite *found = established_range.get (val))
    {
      *out = *found;
      return true;
    }
  /* SCALAR_RANGE_CACHE: a postcondition relating its own return value
     to a call-range-eligible accessor (e.g. 'post<ctrl>(r: r < this->
     size ())'), already resolved once, for this exact call, by cg_
     predicate_facts_walk's own cg_compose_call_result_range -- see that
     function's own comment for why this needs a separate cache from
     ESTABLISHED_RANGE (a different pass's own dominator-tracked state
     feeds it) and why a flat, non-recursive lookup is sound here.  */
  if (cg_range_lite *found = scalar_range_cache.get (val))
    {
      *out = *found;
      return true;
    }

  gimple *def = SSA_NAME_DEF_STMT (val);
  if (def && is_gimple_call (def))
    {
      tree callee = gimple_call_fndecl (as_a <gcall *> (def));
      if (callee && cg_call_postcondition_range_p (callee, require_conveyor,
						     require_symbolic, out))
	return true;
      /* A class-typed operand reached via its own implicit conversion
	 operator (e.g. 'q.operator int()') -- recurse into the
	 receiver, the same lookthrough contracts.cc's own oa_get_range
	 applies at the AST level.  */
      tree receiver = cg_resolve_conversion_receiver (val);
      if (receiver != val
	  && cg_established_range_of (receiver, established_range,
				       scalar_range_cache, ranger,
				       require_conveyor, require_symbolic, out,
				       at_stmt))
	return true;
    }
  else if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if ((CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	  && cg_established_range_of (gimple_assign_rhs1 (def), established_range,
				       scalar_range_cache, ranger, require_conveyor,
				       require_symbolic, out, at_stmt))
	return true;
      /* A load from a variable in memory: what was stored there.  */
      if (tree v = cg_forwarded_load_value (val))
	if (cg_established_range_of (v, established_range, scalar_range_cache,
				     ranger, require_conveyor, require_symbolic,
				     out, at_stmt))
	  return true;
    }
  /* Also known, besides the ranger's: a join's union, a loop
     counter's bound, an earlier load's range.  */
  cg_range_lite extra;
  bool have_extra = false;
  if (gphi *phi = dyn_cast <gphi *> (def))
    {
      /* A join (a loop's head among them): the union of what each
	 incoming value is known to be -- each being, say, a call
	 result its postcondition bounds.  One reaching back to this
	 PHI is unknown.  */
      static hash_set<tree> *in_progress;
      if (!in_progress)
	in_progress = new hash_set<tree>;
      if (!in_progress->add (val))
	{
	  cg_range_lite u;
	  bool all = gimple_phi_num_args (phi) > 0;
	  for (unsigned i = 0; all && i < gimple_phi_num_args (phi); ++i)
	    {
	      cg_range_lite r;
	      if (!cg_established_range_of (gimple_phi_arg_def (phi, i),
					    established_range,
					    scalar_range_cache, ranger,
					    require_conveyor, require_symbolic,
					    &r))
		all = false;
	      else if (i == 0)
		u = r;
	      else
		{
		  u.has_lo = u.has_lo && r.has_lo;
		  u.has_hi = u.has_hi && r.has_hi;
		  if (u.has_lo)
		    u.lo = wi::smin (u.lo, r.lo);
		  if (u.has_hi)
		    u.hi = wi::smax (u.hi, r.hi);
		}
	    }
	  in_progress->remove (val);
	  if (all && (u.has_lo || u.has_hi))
	    {
	      extra = u;
	      have_extra = true;
	    }
	}
      /* A loop counter: the ranger, without loop information here,
	 knows the bound the loop's condition gives it but not the one
	 its start and direction do.  */
      cg_range_lite iv;
      if (cg_induction_variable_bound_1 (val, established_range,
					 scalar_range_cache, ranger, &iv))
	{
	  if (!have_extra)
	    extra = iv;
	  else
	    {
	      if (iv.has_lo && (!extra.has_lo || iv.lo > extra.lo))
		extra.has_lo = true, extra.lo = iv.lo;
	      if (iv.has_hi && (!extra.has_hi || iv.hi < extra.hi))
		extra.has_hi = true, extra.hi = iv.hi;
	    }
	  have_extra = true;
	}
    }

  /* The same value, loaded earlier: its range at AT_STMT, which a
     condition may have narrowed.  */
  auto_vec<tree> twins;
  if (ranger && INTEGRAL_TYPE_P (TREE_TYPE (val)))
    cg_dominating_load_twins (val, at_stmt, &twins);
  for (tree twin : twins)
      {
	cg_range_lite t;
	if (cg_trusted_ranger_range (twin, at_stmt, ranger, false,
				     TYPE_SIGN (TREE_TYPE (twin)), &t))
	  {
	    if (!have_extra)
	      extra = t;
	    else
	      {
		if (t.has_lo && (!extra.has_lo || t.lo > extra.lo))
		  extra.has_lo = true, extra.lo = t.lo;
		if (t.has_hi && (!extra.has_hi || t.hi < extra.hi))
		  extra.has_hi = true, extra.hi = t.hi;
	      }
	    have_extra = true;
	  }
      }

  /* An integer's: a pointer's range is another kind (a reference
     parameter's argument is one).  */
  {
    cg_range_lite r;
    if (cg_trusted_ranger_range (val, at_stmt, ranger, true,
				 TYPE_SIGN (TREE_TYPE (val)), &r))
      {
	*out = r;
	/* Both hold: the narrower of each bound.  */
	if (have_extra)
	  {
	    if (extra.has_lo && (!out->has_lo || extra.lo > out->lo))
	      out->has_lo = true, out->lo = extra.lo;
	    if (extra.has_hi && (!out->has_hi || extra.hi < out->hi))
	      out->has_hi = true, out->hi = extra.hi;
	  }
	return true;
      }
  }
  if (have_extra)
    {
      *out = extra;
      return true;
    }

  return false;
}

/* Item 6's own shape, read declaratively: does CALLEE have a declared,
   flavor-matching (per REQUIRE_CONVEYOR/REQUIRE_SYMBOLIC) postcondition
   whose condition names is_object_address(r)/'r != 0' for r == its own
   POSTCONDITION_IDENTIFIER?  MATCH_FN is is_object_address_call_p or
   oa_nonzero_conjunct_p, sharing this one implementation between both
   fact shapes (mirroring the plugin prototype's own two separate, near-
   identical call_postcondition_guarantees_*_p functions, folded into
   one here since the flavor-gating logic must now be duplicated
   identically in both anyway).  */

static bool
cg_call_postcondition_guarantees_p (tree callee, bool require_conveyor,
				     bool require_symbolic,
				     bool (*match_fn) (tree, tree *))
{
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      bool conveyor_active = oa_contract_conveyor_active_cached_p (contract);
      bool symbolic_active = oa_contract_symbolic_active_cached_p (contract);
      if (require_conveyor && !conveyor_active)
	continue;
      if (require_symbolic && !symbolic_active)
	continue;
      if (!require_conveyor && !require_symbolic)
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
	  if (!match_fn (*conjuncts[i], &arg))
	    continue;
	  STRIP_ANY_LOCATION_WRAPPER (arg);
	  /* '&r' for a reference result R arrives as a conversion of R
	     itself (references need no ADDR_EXPR); for a pointer or class
	     result named the same way it is ADDR_EXPR (R).  Matches
	     contracts.cc's own result-identifier match.  */
	  while (TREE_CODE (arg) == NON_LVALUE_EXPR
		 || CONVERT_EXPR_P (arg)
		 || TREE_CODE (arg) == VIEW_CONVERT_EXPR)
	    arg = TREE_OPERAND (arg, 0);
	  if (TREE_CODE (arg) == ADDR_EXPR)
	    arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
	  if (REFERENCE_REF_P (arg))
	    arg = TREE_OPERAND (arg, 0);
	  if (arg == result_id)
	    return true;
	}
    }
  return false;
}

static bool cg_provable_object_address_p (tree, hash_map<tree, cg_fact> &,
					  bool, hash_set<tree> &,
					  oa_unprovable_reason *);

/* The facts the array-bounds checks consult, set for the duration of
   one function's check (see cg_check_array_bounds_ub).  */

struct cg_array_bounds_ctx
{
  hash_map<tree, cg_range_lite> *established_range;
  hash_map<tree, cg_range_lite> *scalar_range_cache;
  gimple_ranger *ranger;
};

static cg_array_bounds_ctx *cg_array_bounds;

static bool cg_pointer_array_offset (tree, gimple *, tree *, cg_range_lite *,
				     int = 0);
static bool cg_value_range_at (tree, gimple *, hash_map<tree, cg_range_lite> &,
			       hash_map<tree, cg_range_lite> &, gimple_ranger *,
			       bool, cg_range_lite *);

/* Do SSA values A and B hold the same value because they're computed
   alike from the same operands (or are the same)?  Only side-effect-free
   arithmetic, conversions and address computations, not loads.  */

static bool
cg_same_computed_value_p (tree a, tree b, int depth)
{
  if (a == b)
    return true;
  if (TREE_CODE (a) == INTEGER_CST && TREE_CODE (b) == INTEGER_CST)
    return tree_int_cst_equal (a, b);
  if (TREE_CODE (a) != SSA_NAME || TREE_CODE (b) != SSA_NAME || depth > 6)
    return false;
  gimple *da = SSA_NAME_DEF_STMT (a), *db = SSA_NAME_DEF_STMT (b);
  if (!is_gimple_assign (da) || !is_gimple_assign (db))
    return false;
  tree_code ca = gimple_assign_rhs_code (da);
  if (ca != gimple_assign_rhs_code (db)
      || !types_compatible_p (TREE_TYPE (a), TREE_TYPE (b)))
    return false;
  if (!(ca == POINTER_PLUS_EXPR || ca == PLUS_EXPR || ca == MINUS_EXPR
	|| ca == MULT_EXPR || CONVERT_EXPR_CODE_P (ca) || ca == SSA_NAME))
    return false;
  unsigned n = gimple_num_ops (da);
  if (n != gimple_num_ops (db))
    return false;
  for (unsigned i = 1; i < n; ++i)
    if (!cg_same_computed_value_p (gimple_op (da, i), gimple_op (db, i),
				   depth + 1))
      return false;
  return true;
}

/* The statement whose obligations are being checked, for a range
   query the object-address prover makes (an element's index).  */
static gimple *cg_prover_at;

/* Is ADDR, an ADDR_EXPR, an object's address: of a variable, parameter,
   return slot or literal, or of a subobject ('&p->field', '&p[i]') of an
   object a provable pointer P points to?  Like the AST engine's
   oa_provable_p, which requires the base of '&p->field' to be provable
   too.  */

static bool
cg_addr_provable_p (tree addr, hash_map<tree, cg_fact> &established,
		    bool require_conveyor, hash_set<tree> &in_progress,
		    oa_unprovable_reason *reason_out)
{
  tree base = get_base_address (TREE_OPERAND (addr, 0));
  if (!base)
    return false;
  /* An element of an array is an object if its index is within the
     array ('&arr[idx]'), as in the AST engine.  */
  for (tree ref = TREE_OPERAND (addr, 0); handled_component_p (ref);
       ref = TREE_OPERAND (ref, 0))
    if (TREE_CODE (ref) == ARRAY_REF)
      {
	tree idx = TREE_OPERAND (ref, 1);
	tree lo = array_ref_low_bound (ref), hi = array_ref_up_bound (ref);
	if (!lo || !hi || TREE_CODE (lo) != INTEGER_CST
	    || TREE_CODE (hi) != INTEGER_CST)
	  return false;
	cg_range_lite r;
	if (TREE_CODE (idx) == INTEGER_CST)
	  {
	    r.has_lo = r.has_hi = true;
	    r.lo = r.hi = wi::to_widest (idx);
	  }
	else if (!(TREE_CODE (idx) == SSA_NAME && cg_array_bounds
		   && cg_value_range_at (idx,
					 cg_prover_at ? cg_prover_at
					 : SSA_NAME_DEF_STMT (idx),
					 *cg_array_bounds->established_range,
					 *cg_array_bounds->scalar_range_cache,
					 cg_array_bounds->ranger,
					 require_conveyor, &r)))
	  return false;
	if (!r.has_lo || !r.has_hi || r.lo < wi::to_widest (lo)
	    || r.hi > wi::to_widest (hi))
	  return false;
      }
  if (DECL_P (base) || CONSTANT_CLASS_P (base)
      || TREE_CODE (base) == STRING_CST)
    return true;
  if (TREE_CODE (base) == MEM_REF)
    return cg_provable_object_address_p (TREE_OPERAND (base, 0), established,
					 require_conveyor, in_progress,
					 reason_out);
  return false;
}

/* If CALL immediately invokes a lambda ('[&] { return &a; } ()') whose
   every return is the address of one of its captures by reference, the
   value the caller stored for that capture in the closure just before
   the call: what the call returns.  If every return is instead the
   value of a pointer captured by reference ('[&] { return p; } ()'),
   the value last stored to that pointer before the call, in its block.
   As the AST engine's oa_resolve_iile_call, for these shapes.  */

static tree
cg_iile_returned_capture_address (gcall *call)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee || !LAMBDA_FUNCTION_P (callee) || gimple_call_num_args (call) < 1
      || !DECL_SAVED_TREE (callee))
    return NULL_TREE;
  tree closure_addr = gimple_call_arg (call, 0);
  if (TREE_CODE (closure_addr) != ADDR_EXPR
      || !VAR_P (TREE_OPERAND (closure_addr, 0)))
    return NULL_TREE;
  tree closure = TREE_OPERAND (closure_addr, 0);

  /* The capture field every return in the body returns the address of.  */
  struct data_t { tree field; bool bad; int deref; };
  data_t data = { NULL_TREE, false, -1 };
  tree body = DECL_SAVED_TREE (callee);
  cp_walk_tree_without_duplicates (&body, [](tree *tp, int *walk_subtrees,
					     void *d) -> tree
    {
      data_t *data = (data_t *) d;
      if (TREE_CODE (*tp) == LAMBDA_EXPR)
	{
	  *walk_subtrees = 0;
	  return NULL_TREE;
	}
      if (TREE_CODE (*tp) != RETURN_EXPR)
	return NULL_TREE;
      tree v = TREE_OPERAND (*tp, 0);
      if (v && (TREE_CODE (v) == INIT_EXPR || TREE_CODE (v) == MODIFY_EXPR))
	v = TREE_OPERAND (v, 1);
      bool returns_pointer = v && POINTER_TYPE_P (TREE_TYPE (v));
      while (v && (CONVERT_EXPR_P (v) || TREE_CODE (v) == NON_LVALUE_EXPR))
	v = TREE_OPERAND (v, 0);
      /* '&a': the address, not the value, of the object the capture
	 refers to -- what its field holds.  */
      tree field = NULL_TREE;
      int deref = 0;
      if (v && returns_pointer && is_capture_proxy (v)
	  && !TYPE_REF_P (TREE_TYPE (v))
	  && REFERENCE_REF_P (DECL_VALUE_EXPR (v))
	  && TREE_CODE (TREE_OPERAND (DECL_VALUE_EXPR (v), 0)) == COMPONENT_REF
	  && TYPE_REF_P (TREE_TYPE (TREE_OPERAND (TREE_OPERAND
						     (DECL_VALUE_EXPR (v), 0),
						   1))))
	{
	  /* 'return p': the value of the captured P.  */
	  field = TREE_OPERAND (TREE_OPERAND (DECL_VALUE_EXPR (v), 0), 1);
	  deref = 1;
	}
      else if (v && returns_pointer && REFERENCE_REF_P (v)
	       && is_capture_proxy (TREE_OPERAND (v, 0))
	       && TYPE_REF_P (TREE_TYPE (TREE_OPERAND (v, 0)))
	       && (TREE_CODE (DECL_VALUE_EXPR (TREE_OPERAND (v, 0)))
		   == COMPONENT_REF)
	       && TYPE_REF_P (TREE_TYPE (TREE_OPERAND
					 (DECL_VALUE_EXPR (TREE_OPERAND (v, 0)),
					  1))))
	{
	  /* The same, read through the proxy, a reference.  */
	  field = TREE_OPERAND (DECL_VALUE_EXPR (TREE_OPERAND (v, 0)), 1);
	  deref = 1;
	}
      else if (v && TREE_CODE (v) == ADDR_EXPR)
	{
	  v = TREE_OPERAND (v, 0);
	  if (v && is_capture_proxy (v))
	    v = DECL_VALUE_EXPR (v);
	  if (v && REFERENCE_REF_P (v))
	    {
	      v = TREE_OPERAND (v, 0);
	      if (TREE_CODE (v) == COMPONENT_REF
		  && TYPE_REF_P (TREE_TYPE (TREE_OPERAND (v, 1))))
		field = TREE_OPERAND (v, 1);
	    }
	}
      /* The front end folds '&a' to the reference itself, converted to
	 a pointer ('(int *) a', A the capture proxy); 'return q'
	 instead dereferences it.  */
      else if (v && returns_pointer && is_capture_proxy (v)
	       && TYPE_REF_P (TREE_TYPE (v))
	       && TREE_CODE (DECL_VALUE_EXPR (v)) == COMPONENT_REF
	       && TYPE_REF_P (TREE_TYPE (TREE_OPERAND (DECL_VALUE_EXPR (v), 1))))
	field = TREE_OPERAND (DECL_VALUE_EXPR (v), 1);
      else if (v && TREE_CODE (v) == COMPONENT_REF
	       && TYPE_REF_P (TREE_TYPE (TREE_OPERAND (v, 1)))
	       && returns_pointer)
	field = TREE_OPERAND (v, 1);
      if (!field || (data->field && data->field != field)
	  || (data->deref >= 0 && data->deref != deref))
	{
	  data->bad = true;
	  return *tp;
	}
      data->field = field;
      data->deref = deref;
      return NULL_TREE;
    }, &data);
  if (data.bad || !data.field)
    return NULL_TREE;
  /* The captured variable's value at the call is what is returned only
     if the body changes nothing first.  */
  if (data.deref == 1
      && cp_walk_tree_without_duplicates (&body,
					  [](tree *tp, int *walk_subtrees,
					     void *) -> tree
	{
	  switch (TREE_CODE (*tp))
	    {
	    case RETURN_EXPR:
	    case LAMBDA_EXPR:
	      *walk_subtrees = 0;
	      return NULL_TREE;
	    case CALL_EXPR:
	    case AGGR_INIT_EXPR:
	    case MODIFY_EXPR:
	    case INIT_EXPR:
	    case PREINCREMENT_EXPR:
	    case POSTINCREMENT_EXPR:
	    case PREDECREMENT_EXPR:
	    case POSTDECREMENT_EXPR:
	      return *tp;
	    default:
	      return NULL_TREE;
	    }
	}, NULL))
    return NULL_TREE;

  /* The value stored into that field of CLOSURE before CALL, in its
     block.  */
  gimple_stmt_iterator gsi = gsi_for_stmt (call);
  for (gsi_prev (&gsi); !gsi_end_p (gsi); gsi_prev (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (!gimple_assign_single_p (stmt))
	{
	  if (is_gimple_call (stmt))
	    return NULL_TREE;
	  continue;
	}
      tree lhs = gimple_assign_lhs (stmt);
      if (TREE_CODE (lhs) == COMPONENT_REF
	  && TREE_OPERAND (lhs, 0) == closure
	  && TREE_OPERAND (lhs, 1) == data.field)
	{
	  tree stored = gimple_assign_rhs1 (stmt);
	  if (data.deref != 1)
	    return stored;
	  /* The value last stored to the captured variable before CALL,
	     with no call or store through a pointer in between.  */
	  if (TREE_CODE (stored) != ADDR_EXPR
	      || !VAR_P (TREE_OPERAND (stored, 0)))
	    return NULL_TREE;
	  tree var = TREE_OPERAND (stored, 0);
	  gimple_stmt_iterator g = gsi_for_stmt (call);
	  for (gsi_prev (&g); !gsi_end_p (g); gsi_prev (&g))
	    {
	      gimple *s2 = gsi_stmt (g);
	      /* What writes no memory changes nothing here.  */
	      if (!gimple_vdef (s2) || gimple_clobber_p (s2))
		continue;
	      if (is_gimple_call (s2))
		return NULL_TREE;
	      if (!gimple_assign_single_p (s2))
		return NULL_TREE;
	      tree l2 = gimple_assign_lhs (s2);
	      if (l2 == var)
		return gimple_assign_rhs1 (s2);
	      tree b2 = get_base_address (l2);
	      if (!b2 || !DECL_P (b2))
		return NULL_TREE;
	    }
	  return NULL_TREE;
	}
    }
  return NULL_TREE;
}

/* If DEF is a call to std::forward, std::move or std::as_const -- a
   reference cast spelled as a call, which at -O0 isn't folded away --
   its argument, which it returns.  As contracts.cc's
   oa_std_forward_call_p.  */

static tree
cg_std_identity_call_arg (gimple *def)
{
  gcall *call = def ? dyn_cast <gcall *> (def) : NULL;
  if (!call || gimple_call_num_args (call) != 1)
    return NULL_TREE;
  tree callee = gimple_call_fndecl (call);
  if (!callee || !DECL_NAME (callee) || !decl_in_std_namespace_p (callee)
      || !(id_equal (DECL_NAME (callee), "forward")
	   || id_equal (DECL_NAME (callee), "move")
	   || id_equal (DECL_NAME (callee), "as_const")))
    return NULL_TREE;
  return gimple_call_arg (call, 0);
}

/* Is VAL the current function's return slot: its RESULT_DECL, when the
   class it returns is returned by invisible reference (DECL_BY_REFERENCE,
   so the RESULT_DECL is the pointer to the caller's storage), or that
   RESULT_DECL's default definition?  */

static bool
cg_return_slot_p (tree val)
{
  if (TREE_CODE (val) == SSA_NAME && SSA_NAME_IS_DEFAULT_DEF (val))
    val = SSA_NAME_VAR (val);
  return (val
	  && TREE_CODE (val) == RESULT_DECL
	  && DECL_BY_REFERENCE (val)
	  && DECL_CONTEXT (val) == current_function_decl);
}

/* Is VAL provably an object address, given ESTABLISHED (this
   function's own tracked facts) and REQUIRE_CONVEYOR (the calling
   obligation's own one-way-trust requirement: true for a conveyor-
   flavored consult, false for a symbolic-flavored one, which accepts
   either provenance)?  IN_PROGRESS cycle-guards a loop-carried PHI,
   conservatively false -- see the plugin prototype's own identical
   comment on provable_object_address_p for the full rationale (SSA/PHI
   walk replaces oa_env::merge_with's own hand-rolled AND-merge).  */

static bool
cg_provable_object_address_p (tree val, hash_map<tree, cg_fact> &established,
			       bool require_conveyor,
			       hash_set<tree> &in_progress,
			       oa_unprovable_reason *reason_out = nullptr)
{
  if (val == NULL_TREE)
    return false;

  if (TREE_CODE (val) == ADDR_EXPR)
    return cg_addr_provable_p (val, established, require_conveyor,
			       in_progress, reason_out);

  /* The return slot of a function returning a class by invisible
     reference: the object being returned, constructed in place (a
     returned prvalue, or a named local under NRV).  Its storage is the
     caller's, valid for the whole body -- the AST engine sees it as the
     address of the RESULT_DECL or of the NRV local.  */
  if (cg_return_slot_p (val))
    return true;

  if (TREE_CODE (val) != SSA_NAME)
    {
      if (reason_out)
	*reason_out = OA_UNPROVABLE_NOT_ADDRESS_SHAPE;
      return false;
    }

  /* A by-value class parameter with a non-trivial copy ctor/dtor is
     ABI-lowered to an invisible reference: at this pass's point (post-
     genericization) its own PARM_DECL already has REFERENCE_TYPE, and a
     use of it is the bare default-def SSA_NAME, not an ADDR_EXPR -- so
     the ADDR_EXPR axiom above never fires for it, unlike the AST engine
     (contracts.cc's own oa_provable_p), which sees this same parameter
     pre-lowering, still by value, and matches its ADDR_EXPR-of-PARM_DECL
     axiom trivially. This is a structural fact about the parameter's own
     storage (a function's own by-value parameter is always a real,
     owned object for its whole body), not a self-trust assumption about
     what some caller proved -- so, unlike a genuine reference parameter
     a few lines below in cg_seed_self_trust, it holds unconditionally,
     regardless of DECL_DECLARED_CONVEYOR_P (fndecl).  */
  if (SSA_NAME_IS_DEFAULT_DEF (val))
    {
      tree var = SSA_NAME_VAR (val);
      if (var && TREE_CODE (var) == PARM_DECL && DECL_BY_REFERENCE (var))
	return true;
    }

  /* D4324/P2680 author correction (2026-08-19): 'this' is deliberately
     NOT an unconditional axiom here anymore -- mirrors contracts.cc's
     own identical removal from oa_provable_p (see that function's own
     comment for the full reasoning). 'this', used directly, is now a
     bare SSA_NAME falling through to the ordinary ESTABLISHED.get
     consult just below like any other self-trusted fact -- seeded
     unconditionally (for ANY function this pass ever reaches, not just
     a conveyor-declared one) by cg_seed_self_trust, mirroring that
     function's own identical widening.  */

  if (cg_fact *fact = established.get (val))
    if (!require_conveyor || fact->conveyor_established)
      return true;
  /* The same address computed again ('&st[i]' in an assert, then
     'st[i].f'): -O0 GIMPLE doesn't share the computation.  */
  if (TREE_CODE (val) == SSA_NAME && !SSA_NAME_IS_DEFAULT_DEF (val)
      && is_gimple_assign (SSA_NAME_DEF_STMT (val)))
    for (auto it : established)
      if (it.first != val && TREE_CODE (it.first) == SSA_NAME
	  && (!require_conveyor || it.second.conveyor_established)
	  && cg_same_computed_value_p (it.first, val, 0))
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
	if (!cg_provable_object_address_p (gimple_phi_arg_def (def, i),
					    established, require_conveyor,
					    in_progress, reason_out))
	  {
	    result = false;
	    break;
	  }
    }
  else if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if (code == ADDR_EXPR)
	result = cg_addr_provable_p (gimple_assign_rhs1 (def), established,
				     require_conveyor, in_progress,
				     reason_out);
      /* 'p + i' within the array P points into (p = &arr[0]).  */
      else if (code == POINTER_PLUS_EXPR)
	{
	  tree array_type;
	  cg_range_lite off;
	  if (cg_array_bounds
	      && cg_pointer_array_offset (val, def, &array_type, &off)
	      && TYPE_DOMAIN (array_type)
	      && TYPE_MAX_VALUE (TYPE_DOMAIN (array_type))
	      && TREE_CODE (TYPE_MAX_VALUE (TYPE_DOMAIN (array_type)))
		   == INTEGER_CST
	      && off.has_lo && off.lo >= 0 && off.has_hi
	      && off.hi <= wi::to_widest (TYPE_MAX_VALUE
					   (TYPE_DOMAIN (array_type))))
	    result = true;
	}
      else if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	result = cg_provable_object_address_p (gimple_assign_rhs1 (def),
						established, require_conveyor,
						in_progress, reason_out);
    }
  else if (def && is_gimple_call (def))
    {
      tree callee = gimple_call_fndecl (as_a <gcall *> (def));
      if (callee)
	{
	  /* D4324/P2680, Phase 1.5 GIMPLE port: mirrors contracts.cc's own
	     oa_provable_p operator-new axiom, but far simpler here -- by
	     this pass's point (post-gimplification), 'new T()' is just an
	     ordinary GIMPLE_CALL to operator new whose result feeds the
	     constructor call's own receiver argument directly (no
	     TARGET_EXPR/AGGR_INIT_EXPR wrapper survives gimplification, so
	     no separate receiver-correlation step is needed the way the
	     AST engine's own COMPOUND_EXPR/TARGET_EXPR recognizer needs).
	     A real, non-placement allocation's own result is axiomatically
	     a valid object address; placement new's result is only as
	     provable as the pointer its caller supplied (std_placement_
	     new_fn_p is the exact, existing structural test GCC itself
	     already uses to distinguish the standard non-replaceable
	     placement overload from every allocating one -- see that
	     function's own comment).  */
	  /* D4324/P2680: 'throw T(...)' lowers, by this pass's point, to a
	     GIMPLE_CALL to '__cxa_allocate_exception' whose result feeds the
	     thrown object's own constructor call directly, the same shape
	     as 'new T()' feeding operator new's result into T's own
	     constructor -- confirmed via direct testing: 'std::
	     __throw_bad_alloc ()' ('{ throw bad_alloc (); }', bad_alloc
	     deriving from the polymorphic std::exception) failed this
	     pass's own mandatory cdtor receiver check with no established
	     fact for the exception storage pointer, while the identical
	     AST-side test file never reaches this call shape at all (a
	     'throw' expression's own construction site isn't walked by
	     contracts.cc's mandatory item-7 scan the way an ordinary call or
	     'new'-expression is -- a separate, narrower AST-side scope
	     boundary, not something this fix widens). '__cxa_allocate_
	     exception's own result is axiomatically a valid, non-null
	     object address exactly like a real (non-placement) operator
	     new's -- both are runtime allocators handing back fresh storage
	     -- identified by name (an ordinary extern "C" library function
	     declared lazily by except.cc's own build_throw, not a GCC
	     builtin with a dedicated DECL flag the way operator new has).  */
	  if (tree arg = cg_std_identity_call_arg (def))
	    result = cg_provable_object_address_p (arg, established,
						    require_conveyor,
						    in_progress, reason_out);
	  else if (tree stored = cg_iile_returned_capture_address (as_a <gcall *> (def)))
	    result = cg_provable_object_address_p (stored, established,
						    require_conveyor,
						    in_progress, reason_out);
	  else if ((DECL_IS_OPERATOR_NEW_P (callee)
		    && !std_placement_new_fn_p (callee))
	      || (DECL_NAME (callee)
		  && id_equal (DECL_NAME (callee), "__cxa_allocate_exception")
		  && CP_DECL_CONTEXT (callee) == global_namespace
		  && DECL_EXTERN_C_P (callee)))
	    result = true;
	  else
	    result = cg_call_postcondition_guarantees_p
	      (callee, require_conveyor, !require_conveyor,
	       is_object_address_call_p);
	}
    }

  in_progress.remove (val);
  if (!result && reason_out && *reason_out == OA_UNPROVABLE_NONE)
    *reason_out = OA_UNPROVABLE_NO_FACT;
  return result;
}

/* D4324/P2680 item 7's Q2 (cone-of-evaluation restriction), the
   GIMPLE-native counterpart of contracts.cc's own oa_reference_owned_p
   -- see that function's own extensive comment for the full rule this
   ports (part of the GIMPLE/AST parity effort: see .claude/plans/
   lazy-stirring-pearl.md's Tier 3a correction). Ownership is a pure
   provenance property of VAL's own SSA def-chain, not something a
   declared precondition establishes, so (unlike is_object_address/
   nonzero/range above) there is no ESTABLISHED map to consult at all
   here -- walking the SSA def-use chain directly already gives this
   pass, for free, what contracts.cc's own m_borrowed_map/alias-tracking
   needs a separate, hand-rolled mechanism to reconstruct at the tree
   level (the "ownership-laundering" fix that function's own comment
   describes).

   VAL is owned if:
   - it's the address of a decl (VAR_DECL or PARM_DECL -- by-value,
     pointer, or reference alike) whose own DECL_CONTEXT is FUN -- a
     local this function created, or any of its own received
     parameters, including 'this' (simply the first PARM_DECL of a
     member function) -- mirroring oa_reference_owned_p's own "any
     PARM_DECL of this same function... OR 'this'" rule in one unified
     check, since DECL_CONTEXT already distinguishes "belongs to this
     function" cleanly at the GIMPLE level; OR
   - (PHI) every incoming argument is independently owned -- mirrors
     oa_reference_owned_p's own COND_EXPR "both arms" rule, generalized
     to however many arms a real CFG merge has; OR
   - (a copy/conversion assignment, or an ADDR_EXPR materialized into an
     SSA name one hop back) its own single operand is owned; OR
   - (a call result) CALLEE is DECL_DECLARED_CONVEYOR_P and every one of
     its own pointer/reference-typed arguments is independently owned
     (mirrors oa_call_result_owned_p: a conveyor call can't allocate or
     produce an address from thin air, so its own return value must
     alias one of its own inputs or be self-contained).

   - (D4324/P2680, GIMPLE/AST parity, 2026-10-12) a field of an owned
     object ('&this->field'/'&op->field', OP any parameter or local of
     THIS function, not just 'this') is itself owned -- see this
     function's own ADDR_EXPR/COMPONENT_REF case for the full
     port of oa_reference_owned_p's own identical refinement, closing
     what used to be a documented, deferred gap here: a class-typed
     (non-pointer) field could never satisfy a non-const reference
     parameter's Q2 ownership obligation at all (every real-world
     trigger is a non-const-reference *parameter*, regardless of the
     passed field's own type, which is exactly what this ADDR_EXPR/
     COMPONENT_REF shape always is -- is_object_address and ownership
     are two separate obligations, and it is specifically this second
     one, not the field's own type, that was missing this composition).
     This same fix also covers a base-class SUBOBJECT access confirmed
     via direct testing to need it for the identical reason: '*this' in
     a member of a DERIVED class, forwarded to another conveyor call's
     reference parameter, needs an implicit upcast to the base class --
     GCC represents this the same way as an ordinary field access,
     'ADDR_EXPR (COMPONENT_REF (...))', not a bare 'ADDR_EXPR (decl)' --
     so it reaches this exact same case. (A member of a class with no
     base class at all is unaffected either way -- no upcast needed, so
     'this' reaches here unwrapped.)

   Still does NOT recognize an array element of an owned array
   ('&arr[index]') as owned in its own right -- oa_reference_owned_p's
   own separate refinement for that shape remains an un-ported,
   documented follow-up. Also, unlike oa_reference_owned_p, there is no
   separate IN_PREDICATE_CONTEXT ruleset here: contract_assert isn't
   ported to GIMPLE yet either (a separate, later item in this same
   porting effort), so that distinction has nothing to apply to yet.  */

static bool
cg_provably_owned_p (tree val, function *fun, hash_set<tree> &in_progress,
		      oa_unprovable_reason *reason_out = nullptr)
{
  if (val == NULL_TREE)
    return false;

  if (TREE_CODE (val) == ADDR_EXPR)
    {
      tree base = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (val, 0));
      if (DECL_P (base) && DECL_CONTEXT (base) == fun->decl)
	return true;
      /* An element of an owned array.  */
      if (TREE_CODE (base) == ARRAY_REF)
	{
	  tree arr = get_base_address (base);
	  if (arr && DECL_P (arr) && DECL_CONTEXT (arr) == fun->decl)
	    return true;
	}
      /* D4324/P2680, GIMPLE/AST parity (found via the comprehensive
	 coverage sweep, 2026-10-12): a field of an owned object is itself
	 owned -- the exact gap this function's own leading comment
	 already documented as a deferred follow-up ("Deliberately does
	 NOT yet recognize a field of an owned object"), closed here by
	 porting oa_reference_owned_p's own identical ADDR_EXPR
	 (COMPONENT_REF (...)) case (contracts.cc) -- see that function's
	 own extensive comment for the full rationale. OBJ reached through
	 a pointer ('this->f1'/'op->f1', OBJ = INDIRECT_REF (ptr)) asks
	 about the POINTER's own value/identity (any parameter or local of
	 THIS function, by-value/pointer/reference/'this' alike -- unlike
	 the stricter, REFERENCE_TYPE-or-'this'-only gate the ordinary
	 SSA_NAME/PARM_DECL check further below enforces for a direct,
	 whole-pointee dereference substituted for a reference parameter;
	 see that check's own leading comment for why the two cases are
	 genuinely different questions that happen to share this
	 function's own entry point). OBJ a bare DECL_P directly ('h.f1',
	 no pointer at all) asks the identical question about OBJ's own
	 address -- re-enters this exact ADDR_EXPR case above via a
	 freshly built wrapper, rather than duplicating its DECL_CONTEXT
	 check inline.  A deeper field chain ('a.b.c') and a base-class
	 subobject upcast (confirmed via direct testing to have the exact
	 same gap, by this function's own leading comment) both still fall
	 through to the conservative default below when OBJ is itself
	 another COMPONENT_REF -- imprecise but safe, matching
	 oa_reference_owned_p's own documented boundary.  */
      if (TREE_CODE (base) == COMPONENT_REF)
	{
	  tree obj = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (base, 0));
	  /* By this pass's point (post-gimplification), an ordinary pointer
	     dereference is a MEM_REF with a zero offset, not the AST
	     engine's own INDIRECT_REF -- confirmed via direct testing
	     ('op->inner' arrives as 'MEM_REF (op, 0).inner', not
	     'INDIRECT_REF (op).inner'); a non-zero offset is some other,
	     unrelated pointer-arithmetic shape this case does not attempt
	     to handle, so it falls through to the conservative default
	     below like any other unrecognized OBJ shape.  */
	  bool via_pointer = (TREE_CODE (obj) == INDIRECT_REF
			      || (TREE_CODE (obj) == MEM_REF
				  && integer_zerop (TREE_OPERAND (obj, 1))));
	  if (via_pointer)
	    {
	      tree ptr_val = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (obj, 0));
	      if (TREE_CODE (ptr_val) == SSA_NAME)
		{
		  tree var = SSA_NAME_VAR (ptr_val);
		  if (var
		      && (VAR_P (var) || TREE_CODE (var) == PARM_DECL)
		      && DECL_CONTEXT (var) == fun->decl
		      && SSA_NAME_IS_DEFAULT_DEF (ptr_val))
		    return true;
		}
	      return cg_provably_owned_p (ptr_val, fun, in_progress, reason_out);
	    }
	  if (DECL_P (obj))
	    return cg_provably_owned_p (build1 (ADDR_EXPR,
						 build_pointer_type (TREE_TYPE (obj)),
						 obj),
					 fun, in_progress, reason_out);
	}
      if (reason_out)
	*reason_out = OA_UNPROVABLE_NOT_RECEIVED;
      return false;
    }

  if (TREE_CODE (val) != SSA_NAME)
    {
      if (reason_out)
	*reason_out = OA_UNPROVABLE_NOT_RECEIVED;
      return false;
    }

  /* The function's own parameter, used directly (its own initial SSA
     value), with no ADDR_EXPR at all -- this function (see its own
     caller, cg_check_call_reference_safety) only ever asks this about a
     REFERENCE-typed *target*, i.e. always "what does this value
     designate" (a target that's itself a pointer, including 'this' used
     as an ordinary receiver, is entirely exempt from Q2 -- see this
     function's own leading comment, and cg_check_call_reference_safety's
     own REFERENCE_TYPE-only gate: this function is never even invoked
     for a pointer-typed callee target to begin with, so there is no
     "pointer passed directly as a pointer-typed callee parameter"
     ambiguity to disambiguate here the way oa_reference_owned_p's own
     ASKING_ABOUT_POINTER_VALUE parameter has to on the AST side).

     D4324/P2680 (2026-10-15): POINTER_TYPE_P granted here unconditionally
     too -- a POINTER-typed parameter's own POINTEE is owned the same way
     a REFERENCE parameter's designatum already is, mirroring contracts.cc's
     own OWNED_BY_VALUE_PARM widening (see that comment for the full
     soundness argument: direct mutation through an already-is_object_
     address-proven pointer is already unconditionally legal with zero Q2
     involvement, so this grants no new capability, only removes the need
     for an extra indirection to reach it). IS_THIS_PARAMETER kept
     alongside for clarity even though it's now implied by POINTER_TYPE_P
     ('this' is always pointer-typed) -- 'this' had its own, independently
     documented exemption first, and keeping it spelled out preserves that
     history without affecting behavior.  */
  tree var = SSA_NAME_VAR (val);
  if (var && TREE_CODE (var) == PARM_DECL && SSA_NAME_IS_DEFAULT_DEF (val)
      && (TREE_CODE (TREE_TYPE (var)) == REFERENCE_TYPE
	  || POINTER_TYPE_P (TREE_TYPE (var))
	  || is_this_parameter (var)))
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
	if (!cg_provably_owned_p (gimple_phi_arg_def (def, i), fun, in_progress,
				   reason_out))
	  {
	    result = false;
	    break;
	  }
    }
  else if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if (code == ADDR_EXPR || CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	result = cg_provably_owned_p (gimple_assign_rhs1 (def), fun, in_progress,
				       reason_out);
    }
  else if (def && is_gimple_call (def))
    {
      gcall *call = as_a <gcall *> (def);
      tree callee = gimple_call_fndecl (call);
      if (callee && DECL_DECLARED_CONVEYOR_P (callee))
	{
	  result = true;
	  unsigned nargs = gimple_call_num_args (call);
	  for (unsigned i = 0; i < nargs; ++i)
	    {
	      tree arg = gimple_call_arg (call, i);
	      tree arg_type = TREE_TYPE (arg);
	      if (!POINTER_TYPE_P (arg_type)
		  && TREE_CODE (arg_type) != REFERENCE_TYPE)
		continue;
	      if (!cg_provably_owned_p (arg, fun, in_progress, reason_out))
		{
		  result = false;
		  break;
		}
	    }
	}
    }

  in_progress.remove (val);
  if (!result && reason_out && *reason_out == OA_UNPROVABLE_NONE)
    *reason_out = OA_UNPROVABLE_NOT_RECEIVED;
  return result;
}

/* Nonzero-ness's own counterpart, same structure.  */

static bool
cg_provable_nonzero_p (tree val, hash_map<tree, cg_fact> &established_nz,
			bool require_conveyor, hash_set<tree> &in_progress)
{
  if (val == NULL_TREE)
    return false;

  if (TREE_CODE (val) == INTEGER_CST)
    return !integer_zerop (val);

  /* The address of an object, and 'this', are never null.  */
  if (TREE_CODE (val) == ADDR_EXPR)
    {
      tree base = get_base_address (TREE_OPERAND (val, 0));
      if (base && DECL_P (base))
	return true;
    }
  if (TREE_CODE (val) == SSA_NAME && SSA_NAME_IS_DEFAULT_DEF (val)
      && SSA_NAME_VAR (val) && is_this_parameter (SSA_NAME_VAR (val)))
    return true;
  if (tree v = cg_forwarded_load_value (val))
    if (cg_provable_nonzero_p (v, established_nz, require_conveyor,
			       in_progress))
      return true;

  /* A decl-keyed fact -- see cg_established_range_of's own identical
     comment just above.  */
  if (VAR_P (val) || TREE_CODE (val) == PARM_DECL)
    {
      cg_fact *fact = established_nz.get (val);
      return fact && (!require_conveyor || fact->conveyor_established);
    }

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  if (cg_fact *fact = established_nz.get (val))
    if (!require_conveyor || fact->conveyor_established)
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
	if (!cg_provable_nonzero_p (gimple_phi_arg_def (def, i),
				     established_nz, require_conveyor,
				     in_progress))
	  {
	    result = false;
	    break;
	  }
    }
  else if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	result = cg_provable_nonzero_p (gimple_assign_rhs1 (def),
					 established_nz, require_conveyor,
					 in_progress);
    }
  else if (def && is_gimple_call (def))
    {
      tree callee = gimple_call_fndecl (as_a <gcall *> (def));
      if (callee)
	result = cg_call_postcondition_guarantees_p
	  (callee, require_conveyor, !require_conveyor,
	   oa_nonzero_conjunct_p);
      if (!result)
	{
	  tree receiver = cg_resolve_conversion_receiver (val);
	  if (receiver != val)
	    result = cg_provable_nonzero_p (receiver, established_nz,
					     require_conveyor, in_progress);
	}
    }

  in_progress.remove (val);
  return result;
}

/* FUN/DECL packed together for the visit_store/visit_addr callbacks
   below, both of which need FUN (to re-scan for a capture struct's own
   further uses) alongside DECL (the candidate parameter itself).  */

struct cg_conversion_safety_ctx { function *fun; tree decl; };

/* visit_store callback for cg_decl_safe_for_conversion_tracking_p below
   -- any direct store to DATA's own candidate decl at all disqualifies
   it, regardless of shape.  */

static bool
cg_store_disqualifies_p (gimple *, tree base, tree, void *data)
{
  return base == ((cg_conversion_safety_ctx *) data)->decl;
}

/* True if STMT is 'SOME_STRUCT.field = &T', where the field being
   assigned is named "__args" -- the exact, stable shape build_contract_
   check (contracts.cc) emits to hand a per-call capture struct's own
   address to an assertion_context object, for its own (read-only)
   thunk to later re-evaluate the condition through.  */

static bool
cg_stores_addr_into_args_field_p (gimple *stmt, tree t)
{
  if (!gimple_assign_single_p (stmt))
    return false;
  tree rhs = gimple_assign_rhs1 (stmt);
  if (TREE_CODE (rhs) != ADDR_EXPR || TREE_OPERAND (rhs, 0) != t)
    return false;
  tree lhs = gimple_assign_lhs (stmt);
  if (TREE_CODE (lhs) != COMPONENT_REF)
    return false;
  tree field = TREE_OPERAND (lhs, 1);
  return TREE_CODE (field) == FIELD_DECL && DECL_NAME (field)
	 && id_equal (DECL_NAME (field), "__args");
}

/* True if every address-of T anywhere in FUN is exactly the safe
   cg_stores_addr_into_args_field_p shape above -- i.e. T's own address
   never escapes anywhere else at all.  Used by cg_addr_disqualifies_p
   below to validate the *capture struct* (T) a candidate decl's own
   address was stored into, one level removed.  */

static bool
cg_addr_only_feeds_args_field_p (function *fun, tree t)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (gimple_assign_single_p (stmt))
	  {
	    tree rhs = gimple_assign_rhs1 (stmt);
	    if (TREE_CODE (rhs) == ADDR_EXPR && TREE_OPERAND (rhs, 0) == t
		&& !cg_stores_addr_into_args_field_p (stmt, t))
	      return false;
	  }
	/* Any other statement kind referencing T's own address at all
	   (a call argument, a GIMPLE_ASM operand, ...) is conservatively
	   treated as an escape -- only the one recognized capture shape
	   above is ever trusted.  walk_stmt_load_store_addr_ops's own
	   visit_addr also covers the plain-assignment case just handled,
	   redundantly but harmlessly (STMT already failed the check
	   above, or T doesn't appear in it at all).  */
	/* A symbolic contract's marker takes the capture struct's address
	   directly, and does nothing with it (IFN_CONTRACT_MARK).  */
	if (gimple_call_internal_p (stmt, IFN_CONTRACT_MARK))
	  continue;
	if (walk_stmt_load_store_addr_ops (stmt, t, NULL, NULL,
					    [] (gimple *s, tree base, tree,
						void *data) -> bool
					    {
					      tree tt = (tree) data;
					      return base == tt
						     && !cg_stores_addr_into_args_field_p (s, tt);
					    }))
	  return false;
      }
  return true;
}

/* visit_addr callback for cg_decl_safe_for_conversion_tracking_p below
   -- an address-of DATA (packed with FUN into a cg_conversion_safety_ctx,
   passed as DATA) is safe when it is either (a) the receiver argument
   of a call whose own corresponding formal parameter's pointee type is
   TYPE_READONLY (a genuine const-qualified accessor call, which by the
   language's own rules cannot write through that pointer), or (b)
   stored into a field of a local capture-struct temporary whose own
   address, in turn, only ever flows into an assertion_context's own
   "__args" field (cg_addr_only_feeds_args_field_p) -- the shape DATA's
   own address takes when it's merely referenced by this precondition's
   *own* runtime-check machinery, trusted here for the same reason
   self-trust already trusts the precondition's own truth axiomatically:
   this capture exists to evaluate THIS SAME precondition, not arbitrary
   code, and by the language's own rules never writes back through it.
   Anything else disqualifies DATA.  */

static bool
cg_addr_disqualifies_p (gimple *stmt, tree base, tree op, void *data)
{
  cg_conversion_safety_ctx *ctx = (cg_conversion_safety_ctx *) data;
  if (base != ctx->decl)
    return false;

  if (is_gimple_call (stmt))
    {
      gcall *call = as_a <gcall *> (stmt);
      tree callee = gimple_call_fndecl (call);
      tree formal = callee ? DECL_ARGUMENTS (callee) : NULL_TREE;
      unsigned nargs = gimple_call_num_args (call);
      for (unsigned i = 0; i < nargs; ++i, formal = formal ? DECL_CHAIN (formal) : NULL_TREE)
	if (gimple_call_arg (call, i) == op)
	  return !(formal && POINTER_TYPE_P (TREE_TYPE (formal))
		   && TYPE_READONLY (TREE_TYPE (TREE_TYPE (formal))));
      return true;
    }

  if (gimple_assign_single_p (stmt))
    {
      tree lhs = gimple_assign_lhs (stmt);
      if (TREE_CODE (lhs) == COMPONENT_REF)
	{
	  tree capture_struct = TREE_OPERAND (lhs, 0);
	  if (DECL_P (capture_struct)
	      && cg_addr_only_feeds_args_field_p (ctx->fun, capture_struct))
	    return false;
	}
    }

  return true;
}

/* True if DECL (a candidate address-taken PARM_DECL of FUN -- never
   promoted to SSA form because calling its own conversion operator
   requires taking its address; see this file's own top-of-plan
   analysis in ~/gimple-contract-analysis.md on the ABI split between
   value-passed and reference-passed class types) is never written to
   anywhere in FUN's body, so a fact self-trust establishes for it from
   FUN's own precondition remains valid everywhere in FUN, the same
   order-independent way an SSA-keyed fact already is (see cg_seed_
   self_trust's own use of this, and cg_established_range_of/cg_
   provable_nonzero_p/cg_get_relational's own decl-keyed leaf lookup,
   which never re-derives this -- a decl-keyed fact is only ever seeded
   after this check has already passed).

   Deliberately NOT built on the general alias oracle (stmt_may_
   clobber_ref_p and friends): verified empirically (a scratch GCC
   plugin, not part of this file) that, this early in the pipeline
   (right after into-SSA, before any IPA/modref summary is available),
   it conservatively reports "may clobber" for essentially every call
   statement following the address-taking one -- including the very
   conversion-operator call this analysis exists to validate, and
   unrelated calls that don't even receive DECL's address -- making it
   useless as a precision signal here. A purpose-built, syntactic check
   instead, via walk_stmt_load_store_addr_ops: DECL is safe iff it is
   never the target of a direct store, and every address-of it is
   either passed to a call whose own corresponding formal parameter's
   pointee type is const-qualified, or captured by this precondition's
   own runtime-check machinery (cg_addr_disqualifies_p's own second
   case) -- found necessary by direct testing: *every* function with an
   actively-checked contract unconditionally takes the address of each
   referenced-by-value parameter to build that contract's own capture
   struct, entirely independent of any conversion-operator call, so
   without this second case this whole analysis would never actually
   fire for any real, checked contract.

   A class type still passed by value at all (needing this whole
   analysis) is, by the ABI's own "trivial for the purposes of calls"
   rule, always trivially destructible -- so no destructor call ever
   appears here to worry about; a type with a non-trivial destructor
   (or copy/move constructor) is passed by invisible reference instead,
   which already gets an ordinary SSA name and needs none of this (see
   cg_resolve_conversion_receiver's own callers, which reach a plain
   SSA_NAME receiver directly in that case).  */

static bool
cg_decl_safe_for_conversion_tracking_p (function *fun, tree decl)
{
  cg_conversion_safety_ctx ctx = { fun, decl };
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      if (walk_stmt_load_store_addr_ops (gsi_stmt (gsi), &ctx,
					  NULL, cg_store_disqualifies_p,
					  cg_addr_disqualifies_p))
	return false;
  return true;
}

/* The key to use in a self-trust fact map for PARM: its own SSA
   default-def if it has one, or -- when PARM is address-taken solely
   for provably-safe const-qualified accessor calls (cg_decl_safe_for_
   conversion_tracking_p) -- PARM itself, decl-keyed (raw tree-pointer
   keys mean an SSA name and a decl can never collide, so both key
   kinds share the same map without ambiguity). NULL_TREE if neither
   applies, in which case no fact should be seeded for PARM at all --
   the same silently-conservative behavior this file already had before
   either key kind existed.  */

static tree
cg_self_trust_key (function *fun, tree parm)
{
  tree ssa = ssa_default_def (fun, parm);
  if (ssa)
    return ssa;
  if (TREE_ADDRESSABLE (parm) && cg_decl_safe_for_conversion_tracking_p (fun, parm))
    return parm;
  return NULL_TREE;
}

/* Seed ESTABLISHED/ESTABLISHED_NZ from FNDECL's own declared
   precondition -- self-trust. Gated per contract on its own cached
   flavor matching an *enabled* flag: a conveyor-active contract's own
   self-trust is only recorded if -fcontract-conveyor-proofs-gimple is
   on, a symbolic-active one only if -fcontract-symbolic-proofs-gimple
   is on -- independent gates, since a real contract could in principle
   be active for both.  */

static void
cg_seed_self_trust (function *fun, hash_map<tree, cg_fact> &established,
		     hash_map<tree, cg_fact> &established_nz,
		     hash_map<tree, cg_range_lite> &established_range,
		     hash_map<tree, cg_rel_fact> &established_rel,
		     hash_map<tree, cg_call_rel_fact> &established_call_rel,
		     hash_map<cg_field_key_hash, cg_call_call_rel_fact>
		       &established_call_call_rel,
		     hash_map<tree, cg_type_bound_fact> &established_type_bound)
{
  tree fndecl = fun->decl;
  for (tree as = get_fn_contract_specifiers (fndecl); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      bool conveyor_active = oa_contract_conveyor_active_cached_p (contract);
      bool symbolic_active = oa_contract_symbolic_active_cached_p (contract);
      bool conveyor_enabled = conveyor_active && flag_contract_conveyor_proofs_gimple;
      bool symbolic_enabled = symbolic_active && flag_contract_symbolic_proofs_gimple;
      if (!conveyor_enabled && !symbolic_enabled)
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree arg;
	  hash_map<tree, cg_fact> *target;
	  if (is_object_address_call_p (*conjuncts[i], &arg))
	    {
	      STRIP_ANY_LOCATION_WRAPPER (arg);
	      target = &established;
	    }
	  else if (oa_nonzero_conjunct_p (*conjuncts[i], &arg))
	    target = &established_nz;
	  else
	    continue;

	  /* D4324/P2680: is_object_address_call_p/oa_nonzero_conjunct_p
	     return CALL_EXPR_ARG's own raw operand, unstripped -- a
	     reference parameter's own 'is_object_address(&v)' conjunct
	     arrives with 'v' wrapped in a value-preserving NOP_EXPR/
	     CONVERT_EXPR (const-qualification, the same shape contracts.cc's
	     own oa_substitute_call_arg already strips for the identical
	     reason), which the bare 'TREE_CODE (arg) != PARM_DECL' check
	     just below silently declined without this -- confirmed via a
	     real test (d4324-gimple-conveyor-vector-index-margin.C): every
	     one of its ordinary functions' own 'pre<>(is_object_address
	     (&v))' seeded nothing at all, so every accessor call on 'v'
	     inside the function body failed its own Q1 obligation.  */
	  while (TREE_CODE (arg) == NON_LVALUE_EXPR || TREE_CODE (arg) == NOP_EXPR
		 || TREE_CODE (arg) == CONVERT_EXPR || TREE_CODE (arg) == VIEW_CONVERT_EXPR)
	    arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
	  /* '&*r': the shape a function template's instantiation gives
	     '&r' for a reference parameter R, the address R holds.  */
	  if (TREE_CODE (arg) == ADDR_EXPR
	      && TREE_CODE (TREE_OPERAND (arg, 0)) == INDIRECT_REF)
	    arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (TREE_OPERAND (arg, 0),
							   0));

	  if (TREE_CODE (arg) != PARM_DECL)
	    continue;
	  tree key = cg_self_trust_key (fun, arg);
	  if (!key)
	    continue;

	  /* CONVEYOR_ENABLED/SYMBOLIC_ENABLED aren't mutually exclusive,
	     so a contract active (and enabled) for both flavors at once
	     still only ever produces ONE fact, correctly tagged
	     conveyor_established for the one-way trust rule -- there is
	     no meaningful "established twice, differently" case here.  */
	  target->put (key, { conveyor_enabled });
	}

      /* A relational conjunct against another of the SAME function's
	 own parameters (e.g. 'pre<ctrl>(x < q)') -- trust it
	 unconditionally for the rest of this function's own body,
	 mirroring contracts.cc's own oa_establish_shared_substrate_
	 self_trust exactly.  Neither PARAM nor OTHER is ever resolved to
	 a value -- see oa_match_comparison_against_param's own comment.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rel_other;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_param (*conjuncts[i], &rel_param,
						   &rel_code, &rel_other))
	    continue;
	  tree key_param = cg_self_trust_key (fun, rel_param);
	  tree key_other = cg_self_trust_key (fun, rel_other);
	  if (key_param && key_other)
	    established_rel.put (key_param, { rel_code, key_other,
					       cg_range_lite_exact (0),
					       conveyor_enabled });
	}

      /* The call analogue of the relational loop just above (e.g.
	 'pre<ctrl>(i < v.size ())').  RHS_CALLEE is a FUNCTION_DECL, not
	 a value -- no self-trust key needed for it, only for the two
	 decl-valued sides (PARAM/RHS_RECEIVER).  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rhs_receiver, rhs_callee;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_call (*conjuncts[i], &rel_param,
						  &rel_code, &rhs_receiver,
						  &rhs_callee,
						  /*allow_symbolic_accessor=*/
						    !conveyor_enabled))
	    continue;
	  tree key_param = cg_self_trust_key (fun, rel_param);
	  tree key_receiver = cg_self_trust_key (fun, rhs_receiver);
	  if (key_param && key_receiver)
	    established_call_rel.put (key_param, { rel_code, key_receiver,
						    rhs_callee,
						    cg_range_lite_exact (0),
						    conveyor_enabled });
	}

      /* Item 8's overflow check: a type-bound-witness conjunct (e.g.
	 'pre<ctrl>(i < v.size ())', 'pre<ctrl>(i > 0)') -- see cg_type_
	 bound_fact's own comment. Unlike the two loops just above, this
	 one is not mutually exclusive with either -- the SAME conjunct
	 (e.g. 'i < v.size ()') can, and typically does, ALSO satisfy
	 oa_match_comparison_against_call above, and both facts are wanted
	 side by side (mirrors contracts.cc's own oa_refine_single_
	 comparison, which establishes its own type-bound witness
	 unconditionally, before, and independently of, its own mutually-
	 exclusive relational-shape blocks).  No conveyor_established tag
	 here either, matching the range-conjunct loop just below: item 8
	 is a mandatory UB-freedom obligation, not itself flavor-gated the
	 way a one-way-trust FACT is (only whether this precondition is
	 CONVEYOR_ENABLED or SYMBOLIC_ENABLED at all gates entry to this
	 whole PRECONDITION_P branch, already checked above).  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree tb_decl;
	  tree_code tb_code;
	  if (!oa_match_type_bounded_comparison (*conjuncts[i], &tb_decl, &tb_code))
	    continue;
	  tree key = cg_self_trust_key (fun, tb_decl);
	  if (!key)
	    continue;
	  cg_type_bound_fact &fact = established_type_bound.get_or_insert (key);
	  fact.has_upper_witness |= (tb_code == LT_EXPR || tb_code == LE_EXPR);
	  fact.has_lower_witness |= (tb_code == GT_EXPR || tb_code == GE_EXPR);
	}

      /* The call-vs-call analogue of the call-relational loop just
	 above (e.g. 'pre<ctrl>(v.size () < w.size ())'). Unlike that loop
	 (keyed on a single SSA name/decl), this shape's own key is
	 itself a call, so LHS_RECEIVER needs a self-trust key the same
	 way RHS_RECEIVER does -- both decl-valued sides get one, neither
	 callee needs one (a FUNCTION_DECL, not a value).  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree lhs_receiver, lhs_callee, rhs_receiver, rhs_callee;
	  tree_code call_code;
	  if (!oa_match_call_against_call (*conjuncts[i], &lhs_receiver,
					     &lhs_callee, &call_code,
					     &rhs_receiver, &rhs_callee,
					     /*allow_symbolic_accessor=*/
					       !conveyor_enabled))
	    continue;
	  tree key_lhs_receiver = cg_self_trust_key (fun, lhs_receiver);
	  tree key_rhs_receiver = cg_self_trust_key (fun, rhs_receiver);
	  if (key_lhs_receiver && key_rhs_receiver)
	    established_call_call_rel.put ({key_lhs_receiver, lhs_callee},
					     { call_code, key_rhs_receiver,
					       rhs_callee, conveyor_enabled });
	}

      /* Range conjuncts need their own pass: several conjuncts can name
	 the SAME param ('x >= 20 && x < 100'), so they must be grouped
	 and combined (cg_extract_conjunct_range) rather than handled one
	 at a time like the two boolean facts above.  No conveyor_
	 established tag is recorded here -- see this file's own top
	 comment for why ranges carry no one-way-trust provenance.  */
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
	  cg_range_lite range;
	  if (!cg_extract_conjunct_range (conjuncts, params[p], &range))
	    continue;
	  tree key = cg_self_trust_key (fun, params[p]);
	  if (key)
	    established_range.put (key, range);
	}
    }

  /* D4324/P2680 item 7's Q1 axiom, mirroring contracts.cc's own
     oa_resolve_object_address_in_function_1 (the AST engine's identical
     "every reference-typed parameter of a CONVEYOR-DECLARED function...
     is itself provably is_object_address" seeding) -- without this,
     FNDECL's own reference parameters, forwarded unchanged to another
     conveyor call, would be flagged by cg_check_call_reference_safety's
     own Q1 check as unprovable, even though a reference parameter's own
     value is already a validated address by construction. This is the
     assumption side of the implicit precondition item 7's own call-site
     check enforces as the proof side -- sound only because DECL_
     DECLARED_CONVEYOR_P (FNDECL) is exactly what makes that check
     require every caller of *this* function to have proven it first.

     D4324/P2680 soundness fix (2026-08-21, see ~/soundness-fixes-for-
     conveyors.md): a prior "P2680 author correction" (faaa6e6a921f,
     2026-08-19) widened this loop to unconditional (every function this
     pass ever reaches, not just a conveyor-declared one), mirroring an
     identical widening in contracts.cc, reasoning that a bound
     reference/'this' is valid for its own entire lifetime regardless of
     whether the enclosing function is conveyor-declared. That conflates
     "valid once bound" with "was ever validly bound by every possible
     caller" -- a non-conveyor function imposes no obligation on its own
     callers, so it has no basis to assume the latter about its own
     parameters. Confirmed by direct testing: a non-conveyor function
     forwarding its own reference parameters into a nested conveyor call,
     with no precondition of its own, compiled clean under the widening.
     Reverted to the original DECL_DECLARED_CONVEYOR_P gate -- see
     contracts.cc's own identical comment for the two motivating cases
     the widening was trying to fix, and why each needs an explicit
     precondition on the forwarding/dereferencing function instead.

     'this' is consulted via the same ordinary self-trust path as a
     reference parameter (see cg_provable_object_address_p's own comment
     for why it is deliberately not a hardcoded axiom); the NEW
     restriction faaa6e6a921f actually added on top of this seed -- a
     member conveyor call's own receiver must be proven by its CALLER --
     lives entirely at the call site instead (cg_check_call_reference_
     safety's own is_this_parameter handling, below) and is correct,
     unaffected by this revert: this seeding only ever makes a function
     trust its OWN parameters *within its own body*.  */
  if (DECL_DECLARED_CONVEYOR_P (fndecl))
    for (tree parm = DECL_ARGUMENTS (fndecl); parm; parm = DECL_CHAIN (parm))
      if (TREE_CODE (TREE_TYPE (parm)) == REFERENCE_TYPE || is_this_parameter (parm))
	{
	  tree key = cg_self_trust_key (fun, parm);
	  if (key)
	    established.put (key, { /*conveyor_established=*/true });
	}

  /* D4324/P2680, ctor/dtor 'this' soundness correction: mirrors
     contracts.cc's own identical widening in oa_resolve_object_address_
     in_function_1 -- a bare, non-conveyor constructor/destructor has no
     contract specifiers of its own for the loop above (or the DECL_
     DECLARED_CONVEYOR_P-gated block just above) to ever find, so 'this'
     never got seeded here at all. Unlike that general reference-
     parameter seeding (correctly reverted to a DECL_DECLARED_CONVEYOR_P
     gate, per this function's own comment above, since a non-conveyor
     function imposes no obligation on its own callers), a constructor
     or destructor is structurally different: EVERY caller of a cdtor
     now owes a real, unconditional proof obligation of its own for the
     receiver (cg_check_call's own identical addition, mirroring
     contracts.cc's oa_handle_call_precondition_obligation), so seeding
     'this' as self-trusted here, unconditionally, reopens no loophole.
     Confirmed via direct testing: -fcontract-conveyor-proofs-gimple
     silently accepted an explicit destructor call / 'delete' / placement
     new, each on a wholly unproven pointer, before this addition (the
     AST engine's own repro battery, recompiled with the GIMPLE flavor)
     -- the exact same soundness hole contracts.cc's own fix closes,
     confirmed to exist in this pass too, independently, since neither
     pass shares any state with the other.  */
  if (DECL_CONSTRUCTOR_P (fndecl) || DECL_DESTRUCTOR_P (fndecl))
    if (tree this_parm = DECL_ARGUMENTS (fndecl))
      {
	tree key = cg_self_trust_key (fun, this_parm);
	if (key)
	  established.put (key, { /*conveyor_established=*/true });
      }

  /* A defaulted copy or move member's implicit precondition on its
     source (contracts.cc, oa_defaulted_copy_source_parm).  */
  tree src_parm;
  if (oa_defaulted_copy_source_parm_public (fndecl, &src_parm))
    if (tree key = cg_self_trust_key (fun, src_parm))
      established.put (key, { /*conveyor_established=*/true });
}

/* Item 6 for relational facts: does CALL's own callee have a
   postcondition relating its own return value to one of its OTHER
   parameters (e.g. 'post<ctrl>(r: r < q)', via the shared oa_match_
   result_relation)?  If so, CODE_OUT/RHS_OUT/CONVEYOR_OUT describe the
   relation oriented against CALL's own substituted argument for that
   other parameter -- e.g. for 'y = make_val (x, q);' with make_val's
   postcondition above, this returns (LT_EXPR, q's own SSA value at
   THIS call, conveyor_active). Mirrors contracts.cc's own
   oa_establish_relational_from_call, but as a query (this file's own
   established_rel map is populated once per function by self-trust
   only, never eagerly at each call site the way oa_env::relational_map
   is -- see cg_get_relational immediately below, which calls this
   lazily instead).  */

static bool
cg_call_postcondition_relation_p (gcall *call, tree_code *code_out,
				   tree *rhs_out, bool *conveyor_out)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return false;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      bool conveyor_active = oa_contract_conveyor_active_cached_p (contract);
      bool symbolic_active = oa_contract_symbolic_active_cached_p (contract);
      if (!conveyor_active && !symbolic_active)
	continue;
      tree result_id = POSTCONDITION_IDENTIFIER (contract);
      if (!result_id || (!VAR_P (result_id) && TREE_CODE (result_id) != PARM_DECL))
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
	  if (!cg_find_param_position (callee, other_param, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;

	  *code_out = code;
	  *rhs_out = cg_resolve_call_argument (call, argno);
	  *conveyor_out = conveyor_active;
	  return true;
	}
    }
  return false;
}

/* The GIMPLE-native analogue of contracts.cc's own oa_shift_arithmetic_
   no_wrap_ok_p -- see that function's own comment for the full
   soundness rationale, including why only ONE direction's witness is
   required for an unsigned ARITHMETIC_TYPE (both operands already
   guaranteed nonnegative, so only the direction that can actually
   overflow/underflow needs a bound).  */

static bool
cg_shift_arithmetic_no_wrap_ok_p (tree_code code, tree arithmetic_type,
				    const cg_range_lite &base_range,
				    const cg_range_lite &shift)
{
  if (!INTEGRAL_TYPE_P (arithmetic_type))
    return false;
  if (TYPE_OVERFLOW_UNDEFINED (arithmetic_type))
    return true;

  widest_int type_min = wi::to_widest (TYPE_MIN_VALUE (arithmetic_type));
  widest_int type_max = wi::to_widest (TYPE_MAX_VALUE (arithmetic_type));
  bool unsigned_type = TYPE_UNSIGNED (arithmetic_type);

  if (code == PLUS_EXPR)
    {
      if (!base_range.has_hi || !shift.has_hi
	  || base_range.hi + shift.hi > type_max)
	return false;
      return unsigned_type
	     || (base_range.has_lo && shift.has_lo
		 && base_range.lo + shift.lo >= type_min);
    }
  else /* MINUS_EXPR: BASE is always the minuend (see cg_get_relational's
	  own comment).  */
    {
      if (!base_range.has_lo || !shift.has_hi
	  || base_range.lo - shift.hi < type_min)
	return false;
      return unsigned_type
	     || (base_range.has_hi && shift.has_lo
		 && base_range.hi - shift.lo <= type_max);
    }
}

/* VAL's own established relational fact, if any -- tries, in order: a
   self-trusted fact in ESTABLISHED_REL (keyed exactly like ESTABLISHED/
   ESTABLISHED_NZ elsewhere in this file); a copy/conversion one hop
   back (the same "int y = make_val();" gimplifies to a temporary
   shape cg_established_range_of already handles); and item 6 via a
   GIMPLE_CALL def-stmt (cg_call_postcondition_relation_p immediately
   above).  The recursive-resolution counterpart of oa_get_relational
   in contracts.cc, which instead relies on oa_env::relational_map
   already having been eagerly populated at each assignment site --
   this file's own established_rel is only ever populated once, by
   self-trust, so item 6 must be resolved lazily here instead.  */

static bool
cg_get_relational (tree val, hash_map<tree, cg_rel_fact> &established_rel,
		    hash_map<tree, cg_rel_fact> &scalar_rel_cache,
		    hash_map<tree, cg_range_lite> &established_range,
		    hash_map<tree, cg_range_lite> &scalar_range_cache,
		    gimple_ranger *ranger, bool require_conveyor,
		    bool require_symbolic, tree_code *code_out, tree *rhs_out,
		    cg_range_lite *offset_out, bool *conveyor_out)
{
  if (val == NULL_TREE)
    return false;

  /* A decl-keyed fact -- see cg_established_range_of's own identical
     comment.  A plain decl is a leaf: no SSA def-stmt to walk.  */
  if (VAR_P (val) || TREE_CODE (val) == PARM_DECL)
    {
      cg_rel_fact *fact = established_rel.get (val);
      if (!fact)
	return false;
      *code_out = fact->code;
      *rhs_out = fact->rhs;
      *offset_out = fact->offset;
      *conveyor_out = fact->conveyor_established;
      return true;
    }

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  if (cg_rel_fact *fact = established_rel.get (val))
    {
      *code_out = fact->code;
      *rhs_out = fact->rhs;
      *offset_out = fact->offset;
      *conveyor_out = fact->conveyor_established;
      return true;
    }

  /* D4324 Commit 3: SCALAR_REL_CACHE -- a branch-derived fact,
     established by cg_predicate_facts_walk's own dominator-tree pass
     (cg_refine_edge_into) and flattened here for this, the *other*,
     simple pass to see -- see cg_predicate_facts_walk's own comment on
     why that flattening is sound. Checked here, before the SSA def-stmt
     walk below, mirroring cg_established_range_of's own identical
     ordering for SCALAR_RANGE_CACHE.  */
  if (cg_rel_fact *fact = scalar_rel_cache.get (val))
    {
      *code_out = fact->code;
      *rhs_out = fact->rhs;
      *offset_out = fact->offset;
      *conveyor_out = fact->conveyor_established;
      return true;
    }

  gimple *def = SSA_NAME_DEF_STMT (val);
  if (def && is_gimple_call (def))
    {
      if (cg_call_postcondition_relation_p (as_a <gcall *> (def), code_out,
					     rhs_out, conveyor_out))
	{
	  *offset_out = cg_range_lite_exact (0);
	  return true;
	}
      /* A class-typed operand reached via its own implicit conversion
	 operator -- recurse into the receiver.  */
      tree receiver = cg_resolve_conversion_receiver (val);
      if (receiver != val)
	return cg_get_relational (receiver, established_rel, scalar_rel_cache,
				   established_range, scalar_range_cache,
				   ranger, require_conveyor, require_symbolic,
				   code_out, rhs_out, offset_out, conveyor_out);
      return false;
    }
  if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	return cg_get_relational (gimple_assign_rhs1 (def), established_rel,
				   scalar_rel_cache, established_range,
				   scalar_range_cache, ranger, require_conveyor,
				   require_symbolic, code_out, rhs_out,
				   offset_out, conveyor_out);
      /* D4324 Commit 2 (generalized to a variable shift in Commit 4):
	 'base +/- k' shifts a copy of BASE's own established fact by
	 accumulating K into *OFFSET_OUT, mirroring contracts.cc's own
	 oa_get_relational exactly, including the Commit 4 fallback to
	 the *range* mechanism (cg_established_range_of, which -- via its
	 own gimple_ranger fallback -- derives a range for practically
	 any SSA arithmetic chain, broader coverage than the AST side's
	 own oa_get_range gets) when K isn't itself a literal. Unlike the
	 AST-level version, GIMPLE is already flat 3-address form, so the
	 two operands come directly from gimple_assign_rhs1/2 rather than
	 TREE_OPERAND on a PLUS_EXPR node.

	 Same TYPE_OVERFLOW_UNDEFINED-or-independently-bounded gate as
	 contracts.cc's own oa_get_relational (see oa_shift_arithmetic_
	 no_wrap_ok_p's own comment) -- checked against VAL's own type,
	 the SSA name being defined by this PLUS_EXPR/MINUS_EXPR
	 (equivalent to the AST side's TREE_TYPE of the arithmetic node
	 itself, since VAL's type IS that node's result type). The
	 independent numeric-bound rescue queries BASE's own range with
	 DEF as the program point (cg_established_range_of's own AT_STMT),
	 the sharpest context-sensitive query available here -- see item
	 8's own overflow-check discovery, a few thousand lines below,
	 for why AT_STMT matters (a whole-function query can miss a bound
	 only true at this exact point).  */
      if (code == PLUS_EXPR || code == MINUS_EXPR)
	{
	  if (!INTEGRAL_TYPE_P (TREE_TYPE (val)))
	    return false;
	  tree op0 = gimple_assign_rhs1 (def);
	  tree op1 = gimple_assign_rhs2 (def);

	  tree base = NULL_TREE;
	  cg_range_lite shift;
	  if (cg_get_relational (op0, established_rel, scalar_rel_cache,
				  established_range, scalar_range_cache,
				  ranger, require_conveyor, require_symbolic,
				  code_out, rhs_out, offset_out, conveyor_out))
	    {
	      base = op0;
	      if (TREE_CODE (op1) == INTEGER_CST)
		shift = cg_range_lite_exact (wi::to_widest (op1));
	      else if (!cg_established_range_of (op1, established_range,
						  scalar_range_cache, ranger,
						  require_conveyor,
						  require_symbolic, &shift))
		return false;
	    }
	  else if (code == PLUS_EXPR
		   && cg_get_relational (op1, established_rel, scalar_rel_cache,
					  established_range, scalar_range_cache,
					  ranger, require_conveyor,
					  require_symbolic, code_out, rhs_out,
					  offset_out, conveyor_out))
	    {
	      base = op1;
	      if (TREE_CODE (op0) == INTEGER_CST)
		shift = cg_range_lite_exact (wi::to_widest (op0));
	      else if (!cg_established_range_of (op0, established_range,
						  scalar_range_cache, ranger,
						  require_conveyor,
						  require_symbolic, &shift))
		return false;
	    }
	  else
	    return false;

	  cg_range_lite base_num_range;
	  if (!cg_shift_arithmetic_no_wrap_ok_p
		(code, TREE_TYPE (val),
		 cg_established_range_of (base, established_range,
					    scalar_range_cache, ranger,
					    require_conveyor, require_symbolic,
					    &base_num_range, def)
		   ? base_num_range : cg_range_lite (), shift))
	    return false;

	  if (code == MINUS_EXPR)
	    cg_range_lite_negate (shift);
	  cg_range_lite_accumulate (*offset_out, shift);
	  return true;
	}
    }

  return false;
}

/* The call analogue of cg_get_relational immediately above, for
   established_call_rel/cg_call_rel_fact instead. No item-6 (GIMPLE_
   CALL postcondition) fallback here -- this shape's postcondition-side
   composition is out of scope for this pass, matching contracts.cc's
   own oa_get_call_relational, which has none either.  */

static bool
cg_get_call_relational (tree val, hash_map<tree, cg_call_rel_fact> &established_call_rel,
			  hash_map<tree, cg_call_rel_fact> &scalar_call_rel_cache,
			  hash_map<tree, cg_range_lite> &established_range,
			  hash_map<tree, cg_range_lite> &scalar_range_cache,
			  gimple_ranger *ranger, bool require_conveyor,
			  bool require_symbolic, tree_code *code_out,
			  tree *rhs_receiver_out, tree *rhs_callee_out,
			  cg_range_lite *offset_out, bool *conveyor_out)
{
  if (val == NULL_TREE)
    return false;

  if (VAR_P (val) || TREE_CODE (val) == PARM_DECL)
    {
      cg_call_rel_fact *fact = established_call_rel.get (val);
      if (!fact)
	return false;
      *code_out = fact->code;
      *rhs_receiver_out = fact->rhs_receiver;
      *rhs_callee_out = fact->rhs_callee;
      *offset_out = fact->offset;
      *conveyor_out = fact->conveyor_established;
      return true;
    }

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  if (cg_call_rel_fact *fact = established_call_rel.get (val))
    {
      *code_out = fact->code;
      *rhs_receiver_out = fact->rhs_receiver;
      *rhs_callee_out = fact->rhs_callee;
      *offset_out = fact->offset;
      *conveyor_out = fact->conveyor_established;
      return true;
    }

  /* D4324 Commit 3: same SCALAR_CALL_REL_CACHE fallback as cg_get_
     relational's own identical addition just above.  */
  if (cg_call_rel_fact *fact = scalar_call_rel_cache.get (val))
    {
      *code_out = fact->code;
      *rhs_receiver_out = fact->rhs_receiver;
      *rhs_callee_out = fact->rhs_callee;
      *offset_out = fact->offset;
      *conveyor_out = fact->conveyor_established;
      return true;
    }

  gimple *def = SSA_NAME_DEF_STMT (val);
  if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	return cg_get_call_relational (gimple_assign_rhs1 (def),
					established_call_rel, scalar_call_rel_cache,
					established_range, scalar_range_cache,
					ranger, require_conveyor, require_symbolic,
					code_out, rhs_receiver_out, rhs_callee_out,
					offset_out, conveyor_out);
      /* D4324 Commit 2 (generalized to a variable shift in Commit 4):
	 same transfer as cg_get_relational's own identical addition
	 just above, including its own identical no-wrap gate (see
	 cg_shift_arithmetic_no_wrap_ok_p's own comment).  */
      if (code == PLUS_EXPR || code == MINUS_EXPR)
	{
	  if (!INTEGRAL_TYPE_P (TREE_TYPE (val)))
	    return false;
	  tree op0 = gimple_assign_rhs1 (def);
	  tree op1 = gimple_assign_rhs2 (def);

	  tree base = NULL_TREE;
	  cg_range_lite shift;
	  if (cg_get_call_relational (op0, established_call_rel,
				       scalar_call_rel_cache, established_range,
				       scalar_range_cache, ranger,
				       require_conveyor, require_symbolic,
				       code_out, rhs_receiver_out, rhs_callee_out,
				       offset_out, conveyor_out))
	    {
	      base = op0;
	      if (TREE_CODE (op1) == INTEGER_CST)
		shift = cg_range_lite_exact (wi::to_widest (op1));
	      else if (!cg_established_range_of (op1, established_range,
						  scalar_range_cache, ranger,
						  require_conveyor,
						  require_symbolic, &shift))
		return false;
	    }
	  else if (code == PLUS_EXPR
		   && cg_get_call_relational (op1, established_call_rel,
					       scalar_call_rel_cache,
					       established_range,
					       scalar_range_cache, ranger,
					       require_conveyor, require_symbolic,
					       code_out, rhs_receiver_out,
					       rhs_callee_out, offset_out,
					       conveyor_out))
	    {
	      base = op1;
	      if (TREE_CODE (op0) == INTEGER_CST)
		shift = cg_range_lite_exact (wi::to_widest (op0));
	      else if (!cg_established_range_of (op0, established_range,
						  scalar_range_cache, ranger,
						  require_conveyor,
						  require_symbolic, &shift))
		return false;
	    }
	  else
	    return false;

	  cg_range_lite base_num_range;
	  if (!cg_established_range_of (base, established_range,
					scalar_range_cache, ranger,
					require_conveyor, require_symbolic,
					&base_num_range, def))
	    base_num_range = cg_range_lite ();
	  /* The fact itself bounds BASE too, by the accessor's type, as in
	     the AST engine (oa_get_call_relational): '(idx - OFFSET) <
	     v.size ()' makes idx at most SIZE_MAX + OFFSET - 1.  */
	  tree result_type = (*rhs_callee_out
			      ? TREE_TYPE (TREE_TYPE (*rhs_callee_out))
			      : NULL_TREE);
	  if (result_type && INTEGRAL_TYPE_P (result_type))
	    {
	      widest_int rmin = wi::to_widest (TYPE_MIN_VALUE (result_type));
	      widest_int rmax = wi::to_widest (TYPE_MAX_VALUE (result_type));
	      const cg_range_lite &off = *offset_out;
	      tree_code fc = *code_out;
	      if ((fc == LT_EXPR || fc == LE_EXPR || fc == EQ_EXPR)
		  && off.has_hi)
		{
		  widest_int hi = rmax + off.hi - (fc == LT_EXPR ? 1 : 0);
		  if (!base_num_range.has_hi || hi < base_num_range.hi)
		    base_num_range.has_hi = true, base_num_range.hi = hi;
		}
	      if ((fc == GT_EXPR || fc == GE_EXPR || fc == EQ_EXPR)
		  && off.has_lo)
		{
		  widest_int lo = rmin + off.lo + (fc == GT_EXPR ? 1 : 0);
		  if (!base_num_range.has_lo || lo > base_num_range.lo)
		    base_num_range.has_lo = true, base_num_range.lo = lo;
		}
	    }
	  if (!cg_shift_arithmetic_no_wrap_ok_p (code, TREE_TYPE (val),
						 base_num_range, shift))
	    return false;

	  if (code == MINUS_EXPR)
	    cg_range_lite_negate (shift);
	  cg_range_lite_accumulate (*offset_out, shift);
	  return true;
	}
    }

  return false;
}

/* The call-vs-call analogue of cg_get_call_relational immediately
   above, for established_call_call_rel/cg_call_call_rel_fact instead --
   keyed on the pair (VAL, LHS_CALLEE) rather than a single SSA name/
   decl, mirroring contracts.cc's own oa_call_call_relational_fact. No
   postcondition-side composition here either, same reason as that
   function.  */

static bool
cg_get_call_call_relational (tree val, tree lhs_callee,
			       hash_map<cg_field_key_hash, cg_call_call_rel_fact>
				 &established_call_call_rel,
			       tree_code *code_out, tree *rhs_receiver_out,
			       tree *rhs_callee_out, bool *conveyor_out)
{
  if (val == NULL_TREE)
    return false;

  if (VAR_P (val) || TREE_CODE (val) == PARM_DECL)
    {
      cg_call_call_rel_fact *fact
	= established_call_call_rel.get ({val, lhs_callee});
      if (!fact)
	return false;
      *code_out = fact->code;
      *rhs_receiver_out = fact->rhs_receiver;
      *rhs_callee_out = fact->rhs_callee;
      *conveyor_out = fact->conveyor_established;
      return true;
    }

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  if (cg_call_call_rel_fact *fact
	= established_call_call_rel.get ({val, lhs_callee}))
    {
      *code_out = fact->code;
      *rhs_receiver_out = fact->rhs_receiver;
      *rhs_callee_out = fact->rhs_callee;
      *conveyor_out = fact->conveyor_established;
      return true;
    }

  gimple *def = SSA_NAME_DEF_STMT (val);
  if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	return cg_get_call_call_relational (gimple_assign_rhs1 (def), lhs_callee,
					      established_call_call_rel, code_out,
					      rhs_receiver_out, rhs_callee_out,
					      conveyor_out);
    }

  return false;
}

/* D4324 Commit 2: mirrors contracts.cc's own oa_offset_compatible_
   with_code exactly -- see that function's own comment for the
   reasoning (an established '(param - offset) CODE rhs' does not
   entail 'param CODE rhs' unconditionally once OFFSET != 0).  */

static bool
cg_offset_compatible_with_code (const cg_range_lite &offset,
				 tree_code required_code)
{
  switch (required_code)
    {
    case LT_EXPR:
    case LE_EXPR:
      return offset.has_hi && offset.hi <= 0;
    case GT_EXPR:
    case GE_EXPR:
      return offset.has_lo && offset.lo >= 0;
    case EQ_EXPR:
      return offset.has_lo && offset.has_hi && offset.lo == 0
	     && offset.hi == 0;
    default:
      return false;
    }
}

/* Mirrors contracts.cc's own oa_call_relational_contradicts_p exactly
   -- see that function's own comment for the full reasoning and the D =
   PARAM - RECEIVER.CALLEE () reduction this performs. Complements oa_
   relational_code_implies (only ever proves TRUE); this proves FALSE
   directly from a symbolic call-relational fact whose receiver/callee
   were never independently pinned to an absolute number -- the only
   other route to FALSE, cg_check_call_range_relational's own range-vs-
   range fallback, needs exactly that pinning and so can't fire here.  */

static bool
cg_call_relational_contradicts_p (tree_code established_code,
				    const cg_range_lite &offset,
				    tree_code required_code)
{
  cg_range_lite est_d;
  switch (established_code)
    {
    case LT_EXPR:
    case LE_EXPR:
      if (!offset.has_hi)
	return false;
      cg_tighten_range_bound (est_d, established_code, offset.hi);
      break;
    case GT_EXPR:
    case GE_EXPR:
      if (!offset.has_lo)
	return false;
      cg_tighten_range_bound (est_d, established_code, offset.lo);
      break;
    case EQ_EXPR:
      if (!offset.has_lo || !offset.has_hi)
	return false;
      est_d.has_lo = est_d.has_hi = true;
      est_d.lo = offset.lo;
      est_d.hi = offset.hi;
      break;
    default:
      return false;
    }

  cg_range_lite req_d;
  switch (required_code)
    {
    case LT_EXPR:
    case LE_EXPR:
    case GT_EXPR:
    case GE_EXPR:
    case EQ_EXPR:
      cg_tighten_range_bound (req_d, required_code, 0);
      break;
    default:
      return false;
    }

  if (est_d.has_hi && req_d.has_lo && est_d.hi < req_d.lo)
    return true;
  if (req_d.has_hi && est_d.has_lo && req_d.hi < est_d.lo)
    return true;
  return false;
}

/* Does FUN itself warrant cg_check_call_reference_safety being run for
   its own calls at all? Mirrors the two most reliable triggers of
   contracts.cc's own oa_function_needs_walk_p (DECL_DECLARED_CONVEYOR_P
   and a non-empty get_fn_contract_specifiers) -- deliberately does NOT
   attempt that function's third trigger, DECL_MIGHT_NEED_OA_SCAN_P
   ("this function calls some conveyor function"), since direct testing
   found the AST engine itself does NOT reliably act on that trigger in
   practice (a plain, uncontracted, non-conveyor function calling a
   conveyor function's reference parameter with an unprovable argument
   was empirically NOT flagged by contracts.cc, even though that trigger
   reads as if it should be) -- matching that observed real scope, not
   the trigger's own literal description, avoids re-introducing the
   exact libstdc++-internals regression this whole gate exists to
   prevent (see cg_check_call_reference_safety's own comment).  */


/* D4324/P2680 items 7 (Q1's implicit is_object_address) and Q2
   (ownership), the GIMPLE-native counterpart of contracts.cc's own
   oa_handle_call_precondition_obligation -- see that function's own
   trailing comment (from its "RECONSIDERED" pass) for the exact rule
   this ports, and cg_provably_owned_p's own comment for what's
   deliberately not yet covered.

   Unlike every other check in this file, this one is NOT gated behind
   CHECK_AS_CONVEYOR/CHECK_AS_SYMBOLIC or any per-contract activity at
   all: every reference-typed parameter of a DECL_DECLARED_CONVEYOR_P
   callee implicitly carries this obligation, triggered by parameter
   TYPE alone, regardless of whether the callee has written any
   precondition mentioning it -- exactly mirroring the AST engine's own
   unconditional-per-conveyor-callee scope. (This is still bounded by
   this whole pass's own gate(), unlike the AST engine's always-on
   mandatory pass -- a separate, known, pre-existing limitation of the
   GIMPLE pass as a whole, not something this specific check tries to
   fix.)

   Deliberately REFERENCE-ONLY for both Q1 and Q2 for an ORDINARY
   pointer parameter, matching contracts.cc exactly: a plain pointer
   gets neither. A non-const reference additionally requires Q2
   (ownership); a const reference only ever needs Q1.

   P2680 author correction (2026-08-19): the implicit 'this' receiver of
   a member conveyor call is handled separately, just below this loop's
   own REFERENCE_TYPE filter -- unlike an ordinary pointer parameter,
   'this' now gets Q1 (mirroring contracts.cc's own identical
   is_this_parameter block in oa_handle_call_precondition_obligation),
   no longer trusted as an axiom purely because it's the callee's own
   'this'. Still gets NO Q2, for the same reason contracts.cc's own
   comment gives: 'this' is never re-lent in a way that extends the cone
   of evaluation, and a conveyor callee can never invalidate its own
   'this' either way (see cg_provably_owned_p's own identical is_this_
   parameter exemption, unaffected by this correction). No AGGR_INIT_
   EXPR-style exclusion is needed here the way the AST engine's own port
   needs one for a constructor call's own meaningless argno-0 placeholder
   -- GIMPLE has no AGGR_INIT_EXPR at all (a constructor call is already
   just an ordinary GIMPLE_CALL by this pass point, its own 'this'
   argument the REAL target address, not a placeholder), and the
   overwhelmingly common case (constructing a named local/member) is
   already covered by cg_provable_object_address_p's own pre-existing
   'ADDR_EXPR is always trivially provable' axiom regardless.

   Run in every function the pass checks -- the ones the AST engine
   walks (oa_function_needs_walk_p) -- whoever the caller is, as the AST
   engine does: an earlier version ran it in every function of the
   translation unit, flagging calls the AST engine never looks at, and a
   later one only in a conveyor or contract-bearing caller, missing the
   obligations of an ordinary caller the AST engine checks.  */

static tree cg_display_argument (tree, tree);

static void
cg_check_call_reference_safety (gcall *call, function *fun,
				 hash_map<tree, cg_fact> &established)
{
  auto at = make_temp_override (cg_prover_at, call);
  tree callee = gimple_call_fndecl (call);
  if (!callee || !DECL_DECLARED_CONVEYOR_P (callee))
    return;

  unsigned argno = 0;
  for (tree parm = DECL_ARGUMENTS (callee); parm;
       parm = DECL_CHAIN (parm), ++argno)
    {
      if (is_this_parameter (parm))
	{
	  if (argno < gimple_call_num_args (call))
	    {
	      tree substituted = cg_resolve_call_argument (call, argno);
	      hash_set<tree> in_progress;
	      oa_unprovable_reason reason = OA_UNPROVABLE_NONE;
	      if (!cg_provable_object_address_p (substituted, established,
						  /*require_conveyor=*/true,
						  in_progress, &reason))
		{
		  error_at (gimple_location (call),
			    "cannot prove %<is_object_address%> for %qE, "
			    "required by the precondition of %qD",
			    substituted, callee);
		  if (const char *why = oa_unprovable_reason_text (reason))
		    inform (gimple_location (call), "%s", _(why));
		}
	    }
	  continue;
	}
      if (TREE_CODE (TREE_TYPE (parm)) != REFERENCE_TYPE)
	continue;
      if (argno >= gimple_call_num_args (call))
	continue;

      tree substituted = cg_resolve_call_argument (call, argno);
      hash_set<tree> in_progress;
      oa_unprovable_reason reason = OA_UNPROVABLE_NONE;
      if (!cg_provable_object_address_p (substituted, established,
					  /*require_conveyor=*/true, in_progress,
					  &reason))
	{
	  error_at (gimple_location (call),
		    "cannot prove %<is_object_address%> for %qE, "
		    "required by the precondition of %qD",
		    cg_display_argument (substituted, parm), callee);
	  if (const char *why = oa_unprovable_reason_text (reason))
	    inform (gimple_location (call), "%s", _(why));
	  continue;
	}

      bool is_const_ref = TYPE_READONLY (TREE_TYPE (TREE_TYPE (parm)));
      /* Q2 is the rule of a conveyor caller or a predicate, which no
	 runtime check covers, as in the AST engine
	 (oa_handle_call_precondition_obligation): an ordinary caller's
	 aliasing is ordinary.  */
      bool q2_applies
	= (DECL_DECLARED_CONVEYOR_P (fun->decl)
	   || CONTRACT_HELPER (fun->decl) == ldf_contract_predicate_helper);
      if (!is_const_ref && q2_applies)
	{
	  hash_set<tree> owned_in_progress;
	  oa_unprovable_reason own_reason = OA_UNPROVABLE_NONE;
	  /* A predicate's evaluation owns only its own locals (an IILE's,
	     a temporary): not what its function received.  */
	  tree owner;
	  bool owned;
	  if (predicate_core_function_contract (fun->decl, &owner))
	    {
	      tree base = (TREE_CODE (substituted) == ADDR_EXPR
			   ? get_base_address (TREE_OPERAND (substituted, 0))
			   : NULL_TREE);
	      owned = (base && VAR_P (base) && DECL_CONTEXT (base) == fun->decl);
	      own_reason = OA_UNPROVABLE_PREDICATE_CONTEXT;
	    }
	  else
	    owned = cg_provably_owned_p (substituted, fun, owned_in_progress,
					 &own_reason);
	  if (!owned)
	    {
	      error_at (gimple_location (call),
			"argument %qE is not owned by the calling function, "
			"so it may not be passed as the non-const reference "
			"parameter %qD of %qD", substituted, parm, callee);
	      if (const char *why = oa_unprovable_reason_text (own_reason))
		inform (gimple_location (call), "%s", _(why));
	    }
	}
    }
}

/* D4324/P2680 item 8's mandatory UB-freedom scans, GIMPLE-native
   counterparts of contracts.cc's own oa_scan_div_mod_in_expr/oa_scan_
   overflow_in_expr/oa_scan_array_bounds_in_expr (part of the GIMPLE/AST
   parity effort: see .claude/plans/lazy-stirring-pearl.md's Tier 3a
   correction). Like item 8 at the AST level, this is gated PURELY on
   the enclosing function itself being DECL_DECLARED_CONVEYOR_P -- see
   this file's own top comment and contracts.cc's own identical gate at
   every oa_scan_item8_in_expr call site ("if (current_function_decl &&
   DECL_DECLARED_CONVEYOR_P (current_function_decl))") -- unlike item
   7/Q2's own gate (the caller check in cg_check_call_reference_safety),
   there is no "or has its own declared contract" alternative trigger
   here, matching the AST engine's own narrower, unambiguous scope for
   this specific mandatory scan (confirmed by direct reading of every
   call site: item 8 never uses oa_function_needs_walk_p's broader
   trigger set at all).

   Div/mod only, in this first increment (Increment 1 of this porting
   piece) -- overflow and array-bounds/pointer-arithmetic are their own,
   separate follow-ups (the latter needs a different mechanism than a
   direct port, per this same plan file's own note: array indexing
   syntax leaves no trace by GIMPLE time, already lowered to ordinary
   pointer arithmetic indistinguishable from a user's own).  */

/* True if VAL, an operand at STMT, can't be zero for a reason the
   function-wide facts don't capture: the result of a call whose callee's
   flavor-matching postcondition guarantees a range excluding zero (e.g.
   'r > 0'), or a value whose range at STMT, as computed by RANGER from the
   conditions guarding STMT, excludes zero (e.g. under 'if (x != 0)').
   The ranger is asked directly: cg_established_range_of collapses a
   multi-pair range such as that of 'x != 0' to a single interval, which
   includes zero again.  */

static bool
cg_excludes_zero_at_p (tree val, gimple *stmt, gimple_ranger *ranger,
		       bool require_conveyor)
{
  if (val == NULL_TREE || !INTEGRAL_TYPE_P (TREE_TYPE (val)))
    return false;
  /* A load from a variable in memory: what was stored there.  */
  if (tree v = cg_forwarded_load_value (val))
    if (cg_excludes_zero_at_p (v, stmt, ranger, require_conveyor))
      return true;
  if (TREE_CODE (val) == SSA_NAME)
    {
      /* Through plain copies (a call result typically reaches its use
	 through one) and conversions that can't change the value.  */
      tree src = val;
      gimple *def = SSA_NAME_DEF_STMT (src);
      while (def && is_gimple_assign (def)
	     && TREE_CODE (gimple_assign_rhs1 (def)) == SSA_NAME
	     && (gimple_assign_rhs_code (def) == SSA_NAME
		 || (CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (def))
		     && types_compatible_p (TREE_TYPE (src),
					    TREE_TYPE (gimple_assign_rhs1
						       (def))))))
	{
	  src = gimple_assign_rhs1 (def);
	  def = SSA_NAME_DEF_STMT (src);
	}
      if (def && is_gimple_call (def))
	if (tree callee = gimple_call_fndecl (as_a <gcall *> (def)))
	  {
	    cg_range_lite r;
	    if (cg_call_postcondition_range_p (callee, require_conveyor,
					       !require_conveyor, &r)
		&& ((r.has_lo && r.lo > 0) || (r.has_hi && r.hi < 0)))
	      return true;
	  }
    }
  cg_trust trust = cg_trusted_bounds (val, ranger);
  if (ranger && (TREE_CODE (val) == SSA_NAME || TREE_CODE (val) == INTEGER_CST)
      && trust.lo && trust.hi)
    {
      int_range_max vr;
      if (ranger->range_of_expr (vr, val, stmt) && !vr.undefined_p ()
	  && !vr.contains_p (wi::zero (TYPE_PRECISION (TREE_TYPE (val)))))
	return true;
    }
  return false;
}

/* Is VAL, used at STMT's own program point, provably nonzero? Tries
   (in order) the same fact-based check item 7 already has
   (cg_provable_nonzero_p) and then, as a supplementary source
   (mirroring oa_provably_nonzero_p's own "Increment E1" range-fact
   fallback), VAL's own established/ranger-derived numeric range *at
   STMT* -- via cg_established_range_of's own AT_STMT parameter, so a
   self-trusted/postcondition-derived fact is tried first, falling back
   to RANGER's context-sensitive query only if no such fact exists.
   Context-sensitivity matters here specifically because a plain
   'ranger->range_of_expr (vr, val)' with no STMT argument answers a
   different, coarser question -- VAL's range across every use in the
   whole function, not specifically the range a branch-derived
   refinement establishes right before this one particular division;
   confirmed by direct testing that 'if (b > 0) return a / b;' was
   wrongly rejected without STMT.  */

static bool
cg_provably_nonzero_for_ub_p (tree val, gimple *stmt,
				hash_map<tree, cg_fact> &established_nz,
				hash_map<tree, cg_range_lite> &established_range,
				hash_map<tree, cg_range_lite> &scalar_range_cache,
				gimple_ranger *ranger)
{
  hash_set<tree> in_progress;
  if (cg_provable_nonzero_p (val, established_nz, /*require_conveyor=*/true,
			      in_progress))
    return true;
  if (cg_excludes_zero_at_p (val, stmt, ranger, /*require_conveyor=*/true))
    return true;

  cg_range_lite r;
  if (cg_established_range_of (val, established_range, scalar_range_cache,
				 ranger, /*require_conveyor=*/true,
				 /*require_symbolic=*/false, &r, stmt))
    {
      if (r.has_lo && r.lo > 0)
	return true;
      if (r.has_hi && r.hi < 0)
	return true;
    }
  return false;
}

/* Set while analyzing conveyor code -- a conveyor function's body, or a
   conveyor contract's predicate core: a call
   from it to a conveyor callee must establish the callee's own
   preconditions, as a mandatory error rather than a proof warning, and
   regardless of -fcontract-conveyor-proofs.  The callee's body is only
   verified free of undefined behavior assuming its preconditions hold.
   The AST engine's oa_callee_pre_mandatory.  */

static bool cg_callee_pre_mandatory;

/* Set while analyzing a never_proven_conveyor contract's predicate core
   for CG_CALLEE_PRE_MANDATORY's obligations alone: such a predicate is
   otherwise exempt from the conveyor checks (cg_predicate_item8_p), but
   a call it makes still mustn't break its conveyor callee's
   preconditions.  Its is_object_address obligations stay exempt, as the
   AST engine's oa_item8_callee_pre_only.  */

static bool cg_callee_pre_only;

/* error_at for a call's precondition obligation.  A mandatory one in a
   conveyor(auto) specialization being deduced only means it isn't
   conveyor: record it instead, as cg_conveyor_error_at does.  */

static void ATTRIBUTE_GCC_DIAG (2,3)
cg_obligation_error_at (location_t loc, const char *gmsgid, ...)
	       {
		 if (cg_callee_pre_mandatory
		     && conveyor_auto_note_violation_in (cfun ? cfun->decl
							 : current_function_decl))
		   return;
  auto_diagnostic_group d;
  va_list ap;
  va_start (ap, gmsgid);
  emit_diagnostic_valist (diagnostics::kind::error, loc,
			  diagnostics::option_id (), gmsgid, &ap);
  va_end (ap);
}

/* error_at for a violation of the conveyor rules found in the body of the
   function being analyzed (item 8 UB checks).  While that function is a
   conveyor(auto) specialization being deduced, the violation only means
   it isn't conveyor: record it instead, like the AST engine's
   oa_conveyor_error_at.  */

static void ATTRIBUTE_GCC_DIAG (2,3)
cg_conveyor_error_at (location_t loc, const char *gmsgid, ...)
{
  if (conveyor_auto_note_violation_in (cfun ? cfun->decl
				       : current_function_decl))
    return;
  auto_diagnostic_group d;
  va_list ap;
  va_start (ap, gmsgid);
  emit_diagnostic_valist (diagnostics::kind::error, loc,
			  diagnostics::option_id (), gmsgid, &ap);
  va_end (ap);
}


/* A tree to name SSA value VAL by in a diagnostic, as the AST engine
   would: the variable, parameter or field it was read from, or the
   arithmetic it was computed by, rather than an anonymous temporary
   ('<unknown>').  VAL itself if nothing better is found.  */

static tree cg_capture_name (tree);

static tree
cg_display_value (tree val, int depth = 0)
{
  if (TREE_CODE (val) != SSA_NAME || depth > 4)
    return val;
  if (tree var = SSA_NAME_VAR (val))
    if (DECL_P (var) && DECL_NAME (var)
	&& (!DECL_ARTIFICIAL (var) || is_this_parameter (var)))
      return var;
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (!def || !is_gimple_assign (def))
    return val;
  tree_code code = gimple_assign_rhs_code (def);
  tree rhs1 = gimple_assign_rhs1 (def);
  if (code == SSA_NAME || CONVERT_EXPR_CODE_P (code))
    {
      tree inner = cg_display_value (rhs1, depth + 1);
      if (inner == rhs1)
	return val;
      /* An integer conversion that changes the value's sign or width is
	 one the source spells (implicitly or not): shown, as the AST
	 engine shows its own '(size_type)idx'.  */
      tree from = TREE_TYPE (rhs1), to = TREE_TYPE (val);
      if (CONVERT_EXPR_CODE_P (code)
	  && INTEGRAL_TYPE_P (from) && INTEGRAL_TYPE_P (to)
	  && (TYPE_UNSIGNED (from) != TYPE_UNSIGNED (to)
	      || TYPE_PRECISION (from) != TYPE_PRECISION (to)))
	return build1 (NOP_EXPR, to, inner);
      return inner;
    }
  if (gimple_assign_single_p (def))
    {
      if ((VAR_P (rhs1) || TREE_CODE (rhs1) == PARM_DECL)
	  && !DECL_ARTIFICIAL (rhs1) && DECL_NAME (rhs1))
	return rhs1;
      /* '&x', '&p->f'.  */
      if (TREE_CODE (rhs1) == ADDR_EXPR)
	{
	  tree ref = TREE_OPERAND (rhs1, 0);
	  if (DECL_P (ref) && DECL_NAME (ref) && !DECL_ARTIFICIAL (ref))
	    return rhs1;
	  if (TREE_CODE (ref) == COMPONENT_REF
	      && TREE_CODE (TREE_OPERAND (ref, 0)) == MEM_REF
	      && integer_zerop (TREE_OPERAND (TREE_OPERAND (ref, 0), 1))
	      && TREE_CODE (TREE_OPERAND (TREE_OPERAND (ref, 0), 0)) == SSA_NAME)
	    {
	      tree ptr = cg_display_value (TREE_OPERAND (TREE_OPERAND (ref, 0),
							  0), depth + 1);
	      if (DECL_P (ptr) && POINTER_TYPE_P (TREE_TYPE (ptr)))
		return build1 (ADDR_EXPR, TREE_TYPE (rhs1),
			       build3 (COMPONENT_REF, TREE_TYPE (ref),
				       build1 (INDIRECT_REF,
					       TREE_TYPE (TREE_TYPE (ptr)), ptr),
				       TREE_OPERAND (ref, 1), NULL_TREE));
	    }
	}
      /* A lambda's capture: by value, the field's load; by reference, a
	 load through the pointer the field holds.  Named as the captured
	 variable.  */
      if (tree name = cg_capture_name (rhs1))
	return name;
      if (TREE_CODE (rhs1) == COMPONENT_REF)
	{
	  tree obj = TREE_OPERAND (rhs1, 0);
	  if (TREE_CODE (obj) == MEM_REF && integer_zerop (TREE_OPERAND (obj, 1))
	      && TREE_CODE (TREE_OPERAND (obj, 0)) == SSA_NAME)
	    {
	      tree ptr = cg_display_value (TREE_OPERAND (obj, 0), depth + 1);
	      if (!DECL_P (ptr))
		return val;
	      tree base = (TYPE_REF_P (TREE_TYPE (ptr))
			   ? convert_from_reference (ptr)
			   : build1 (INDIRECT_REF, TREE_TYPE (TREE_TYPE (ptr)),
				     ptr));
	      return build3 (COMPONENT_REF, TREE_TYPE (rhs1), base,
			     TREE_OPERAND (rhs1, 1), NULL_TREE);
	    }
	  if (DECL_P (obj) && !DECL_ARTIFICIAL (obj))
	    return rhs1;
	}
      /* 'arr[k]'.  */
      if (TREE_CODE (rhs1) == ARRAY_REF && DECL_P (TREE_OPERAND (rhs1, 0))
	  && !DECL_ARTIFICIAL (TREE_OPERAND (rhs1, 0)))
	return rhs1;
      /* '*p', or a reference's referent.  */
      if (TREE_CODE (rhs1) == MEM_REF && integer_zerop (TREE_OPERAND (rhs1, 1))
	  && TREE_CODE (TREE_OPERAND (rhs1, 0)) == SSA_NAME)
	{
	  tree ptr = cg_display_value (TREE_OPERAND (rhs1, 0), depth + 1);
	  if (DECL_P (ptr) && TYPE_REF_P (TREE_TYPE (ptr)))
	    return convert_from_reference (ptr);
	  if (DECL_P (ptr))
	    return build1 (INDIRECT_REF, TREE_TYPE (rhs1), ptr);
	}
      return val;
    }
  if (TREE_CODE_CLASS (code) == tcc_binary)
    return build2 (code, TREE_TYPE (val), cg_display_value (rhs1, depth + 1),
		   cg_display_value (gimple_assign_rhs2 (def), depth + 1));
  return val;
}

/* If REF is a load of a lambda's capture -- its closure field, for a
   capture by value, or through it, for one by reference -- the captured
   variable's name.  */

static tree
cg_capture_name (tree ref)
{
  bool deref = false;
  if (TREE_CODE (ref) == MEM_REF && integer_zerop (TREE_OPERAND (ref, 1))
      && TREE_CODE (TREE_OPERAND (ref, 0)) == SSA_NAME)
    {
      gimple *d = SSA_NAME_DEF_STMT (TREE_OPERAND (ref, 0));
      if (!d || !gimple_assign_single_p (d))
	return NULL_TREE;
      ref = gimple_assign_rhs1 (d);
      deref = true;
    }
  if (TREE_CODE (ref) != COMPONENT_REF)
    return NULL_TREE;
  tree field = TREE_OPERAND (ref, 1);
  tree obj = TREE_OPERAND (ref, 0);
  if (TREE_CODE (TREE_CODE (obj) == MEM_REF ? TREE_TYPE (obj)
		 : TREE_TYPE (obj)) != RECORD_TYPE
      || !LAMBDA_TYPE_P (TREE_TYPE (obj))
      || deref != TYPE_REF_P (TREE_TYPE (field))
      || !DECL_NAME (field))
    return NULL_TREE;
  const char *n = IDENTIFIER_POINTER (DECL_NAME (field));
  if (n[0] == '_' && n[1] == '_' && n[2])
    return get_identifier (n + 2);
  return NULL_TREE;
}

/* How to name ARG, the value passed for parameter PARM, in a
   diagnostic: for a reference parameter, the object the reference binds
   to ('* p' for a pointer P, 'a' for '&a'), as the AST engine names it;
   otherwise cg_display_value.  */

static tree
cg_display_argument (tree arg, tree parm)
{
  if (parm && TYPE_REF_P (TREE_TYPE (parm)) && POINTER_TYPE_P (TREE_TYPE (arg)))
    {
      if (TREE_CODE (arg) == SSA_NAME
	  && gimple_assign_single_p (SSA_NAME_DEF_STMT (arg))
	  && (TREE_CODE (gimple_assign_rhs1 (SSA_NAME_DEF_STMT (arg)))
	      == ADDR_EXPR))
	arg = gimple_assign_rhs1 (SSA_NAME_DEF_STMT (arg));
      if (TREE_CODE (arg) == ADDR_EXPR)
	{
	  tree ref = TREE_OPERAND (arg, 0);
	  /* 'p->v' rather than the MEM_REF it is in GIMPLE.  */
	  if (TREE_CODE (ref) == COMPONENT_REF
	      && TREE_CODE (TREE_OPERAND (ref, 0)) == MEM_REF
	      && integer_zerop (TREE_OPERAND (TREE_OPERAND (ref, 0), 1)))
	    {
	      tree ptr = cg_display_value (TREE_OPERAND (TREE_OPERAND (ref, 0),
							  0));
	      if (DECL_P (ptr))
		return build3 (COMPONENT_REF, TREE_TYPE (ref),
			       build1 (INDIRECT_REF, TREE_TYPE (TREE_TYPE (ptr)),
				       ptr),
			       TREE_OPERAND (ref, 1), NULL_TREE);
	    }
	  return ref;
	}
      tree ptr = cg_display_value (arg);
      return build1 (INDIRECT_REF, TREE_TYPE (TREE_TYPE (arg)), ptr);
    }
  return cg_display_value (arg);
}

/* Check every div/mod operation in one GIMPLE_ASSIGN statement's own
   RHS, erroring on any whose divisor isn't provably nonzero. Only ever
   called for a DECL_DECLARED_CONVEYOR_P function's own body (see this
   section's own leading comment) -- checked by the caller, not here,
   matching oa_scan_div_mod_in_expr's own identical division of
   responsibility.

   A GIMPLE_ASSIGN's own RHS is already a single, non-recursive
   operation (SSA form has no nested sub-expressions the way the AST's
   own arbitrary-depth cp_walk_tree needs to descend into) -- so, unlike
   oa_scan_div_mod_in_expr's own tree walk, this needs no recursion at
   all, just a single rhs-code check per statement; the FOR_EACH_BB_FN
   loop that calls this once per statement already provides the
   "every div/mod anywhere in the function" coverage a recursive walk
   would otherwise need to provide itself.  */

static void
cg_check_div_mod_ub (gimple *stmt, hash_map<tree, cg_fact> &established_nz,
		       hash_map<tree, cg_range_lite> &established_range,
		       hash_map<tree, cg_range_lite> &scalar_range_cache,
		       gimple_ranger *ranger)
{
  if (!is_gimple_assign (stmt))
    return;
  enum tree_code code = gimple_assign_rhs_code (stmt);
  if (code != TRUNC_DIV_EXPR && code != TRUNC_MOD_EXPR)
    return;

  tree divisor = gimple_assign_rhs2 (stmt);
  if (!cg_provably_nonzero_for_ub_p (divisor, stmt, established_nz,
				      established_range, scalar_range_cache,
				      ranger))
    cg_conveyor_error_at (gimple_location (stmt),
			  "divisor %qE not provably nonzero in a "
			  "conveyor function",
			  cg_display_value (divisor));
}

/* D4324, item 8's overflow check, GIMPLE side: is a shift of exactly 1
   (INCREASING true for 'x + 1', false for 'x - 1') provably safe for X
   via the type-bound-witness route: does X have an established cg_type_
   bound_fact (see that struct's own comment) with the matching
   direction's own witness set?

   VAL is stripped to its own canonical, pre-copy/conversion form first
   (cg_type_bound_get, immediately below) rather than the lookup itself
   walking the chain -- mirrors oa_provably_safe_unit_shift_p's own
   identical ordering (it strips X via oa_strip_to_relational_operand
   *before* consulting env.type_bound_get's own flat, pointer-identity-
   keyed lookup, rather than teaching that lookup to walk chains
   itself).  */

static bool
cg_type_bound_get (tree val, hash_map<tree, cg_type_bound_fact> &established_type_bound,
		     cg_type_bound_fact *out)
{
  if (val == NULL_TREE)
    return false;

  if (VAR_P (val) || TREE_CODE (val) == PARM_DECL)
    {
      cg_type_bound_fact *found = established_type_bound.get (val);
      if (!found)
	return false;
      *out = *found;
      return true;
    }

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  if (cg_type_bound_fact *found = established_type_bound.get (val))
    {
      *out = *found;
      return true;
    }

  gimple *def = SSA_NAME_DEF_STMT (val);
  if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	return cg_type_bound_get (gimple_assign_rhs1 (def), established_type_bound,
				    out);
    }
  return false;
}

static bool
cg_provably_safe_unit_shift_p (tree x, bool increasing,
				 hash_map<tree, cg_type_bound_fact> &established_type_bound)
{
  cg_type_bound_fact fact;
  if (!cg_type_bound_get (x, established_type_bound, &fact))
    return false;
  return increasing ? fact.has_upper_witness : fact.has_lower_witness;
}

/* D4324/P2680 item 8's overflow check, GIMPLE side -- the counterpart of
   oa_scan_overflow_in_expr. "Increment 1" of this piece: the NUMERIC
   route for NEGATE_EXPR and binary PLUS_EXPR/MINUS_EXPR/MULT_EXPR (all
   ordinary GIMPLE_ASSIGN rhs codes), plus the type-bound-witness rescue
   above for a literal +/-1 shift specifically -- see cg_provably_safe_
   unit_shift_p's own comment for its scope relative to AST's.

   No separate INCREMENT/DECREMENT_EXPR case is needed here the way
   oa_scan_overflow_in_expr has one: by the time this pass runs, GIMPLE
   has already lowered 'x++'/'x--' to an ordinary PLUS_EXPR/MINUS_EXPR
   GIMPLE_ASSIGN with a literal +/-1 operand (confirmed by direct
   testing) -- so the general binary-arithmetic case below already
   covers it.  */

static void
cg_check_overflow_ub (gimple *stmt,
			hash_map<tree, cg_type_bound_fact> &established_type_bound,
			hash_map<tree, cg_range_lite> &established_range,
			hash_map<tree, cg_range_lite> &scalar_range_cache,
			gimple_ranger *ranger)
{
  if (!is_gimple_assign (stmt))
    return;
  enum tree_code code = gimple_assign_rhs_code (stmt);
  tree lhs = gimple_assign_lhs (stmt);
  tree type = TREE_TYPE (lhs);

  if (code == NEGATE_EXPR)
    {
      if (!INTEGRAL_TYPE_P (type) || !TYPE_OVERFLOW_UNDEFINED (type))
	return;
      tree op = gimple_assign_rhs1 (stmt);
      cg_range_lite r;
      bool safe = cg_established_range_of (op, established_range,
					     scalar_range_cache, ranger,
					     /*require_conveyor=*/true,
					     /*require_symbolic=*/false, &r,
					     stmt)
		  && r.has_lo
		  && r.lo > wi::to_widest (TYPE_MIN_VALUE (type));
      if (!safe)
	cg_conveyor_error_at (gimple_location (stmt),
			      "negation of %qE not provably "
			      "free of overflow in a conveyor function", op);
      return;
    }

  if (code != PLUS_EXPR && code != MINUS_EXPR && code != MULT_EXPR)
    return;
  if (!INTEGRAL_TYPE_P (type) || !TYPE_OVERFLOW_UNDEFINED (type))
    return;

  tree op0 = gimple_assign_rhs1 (stmt);
  tree op1 = gimple_assign_rhs2 (stmt);
  widest_int type_min = wi::to_widest (TYPE_MIN_VALUE (type));
  widest_int type_max = wi::to_widest (TYPE_MAX_VALUE (type));

  /* A literal shift of exactly 1 ('x + 1'/'1 + x'/'x - 1') gets first
     refusal via the type-bound-witness rescue, mirroring oa_scan_
     overflow_in_expr's own identical ordering, before falling to the
     general numeric-only route below.  */
  tree var_side = NULL_TREE;
  bool increasing = false;
  if (code == PLUS_EXPR || code == MINUS_EXPR)
    {
      if (code == PLUS_EXPR && integer_onep (op1))
	{ var_side = op0; increasing = true; }
      else if (code == PLUS_EXPR && integer_onep (op0))
	{ var_side = op1; increasing = true; }
      else if (code == PLUS_EXPR && integer_minus_onep (op1))
	{ var_side = op0; increasing = false; }
      else if (code == PLUS_EXPR && integer_minus_onep (op0))
	{ var_side = op1; increasing = false; }
      else if (code == MINUS_EXPR && integer_onep (op1))
	{ var_side = op0; increasing = false; }
      if (var_side
	  && cg_provably_safe_unit_shift_p (var_side, increasing,
					      established_type_bound))
	return;
    }

  cg_range_lite a, b;
  bool have_a = cg_established_range_of (op0, established_range,
					   scalar_range_cache, ranger,
					   /*require_conveyor=*/true,
					   /*require_symbolic=*/false, &a, stmt);
  bool have_b = cg_established_range_of (op1, established_range,
					   scalar_range_cache, ranger,
					   /*require_conveyor=*/true,
					   /*require_symbolic=*/false, &b, stmt);

  bool safe = false;
  /* Each direction on its own: adding something non-negative can't go
     below the type's minimum, whatever the other operand ('++x' with
     only 'x < 100' known).  */
  if (code == PLUS_EXPR)
    {
      bool hi_ok = ((have_a && have_b && a.has_hi && b.has_hi
		     && a.hi + b.hi <= type_max)
		    || (have_a && a.has_hi && a.hi <= 0)
		    || (have_b && b.has_hi && b.hi <= 0));
      bool lo_ok = ((have_a && have_b && a.has_lo && b.has_lo
		     && a.lo + b.lo >= type_min)
		    || (have_a && a.has_lo && a.lo >= 0)
		    || (have_b && b.has_lo && b.lo >= 0));
      safe = hi_ok && lo_ok;
    }
  else if (code == MINUS_EXPR)
    {
      bool hi_ok = ((have_a && have_b && a.has_hi && b.has_lo
		     && a.hi - b.lo <= type_max)
		    || (have_b && b.has_lo && b.lo >= 0));
      bool lo_ok = ((have_a && have_b && a.has_lo && b.has_hi
		     && a.lo - b.hi >= type_min)
		    || (have_b && b.has_hi && b.hi <= 0));
      safe = hi_ok && lo_ok;
    }
  else /* MULT_EXPR: cg_range_lite_multiply, mirroring oa_range_multiply
	  (contracts.cc) exactly -- see that function's own comment for
	  the corner-product rationale; overflow-safety needs both sides
	  fully bounded, so HAS_LO/HAS_HI are checked explicitly rather
	  than relying on the return value alone (true whenever either
	  side was derived, for a caller wanting a partial result -- not
	  this one).  */
    {
      bool has_lo = false, has_hi = false;
      widest_int lo, hi;
      if (have_a && have_b
	  && cg_range_lite_multiply (a, b, &has_lo, &lo, &has_hi, &hi)
	  && has_lo && has_hi)
	safe = lo >= type_min && hi <= type_max;
    }

  if (safe)
    return;
  /* '++x' or 'x += 1' is the AST engine's "increment of x": a unit shift
     stored back into the variable it read.  */
  tree target = NULL_TREE;
  if (var_side && TREE_CODE (var_side) == SSA_NAME)
    {
      /* '++*p', '++r' for a reference r: the value is loaded from a
	 location and the result stored back to it.  */
      gimple *load = SSA_NAME_DEF_STMT (var_side);
      /* A post-increment copies the old value first.  */
      while (load && gimple_assign_ssa_name_copy_p (load))
	load = SSA_NAME_DEF_STMT (gimple_assign_rhs1 (load));
      use_operand_p use;
      gimple *use_stmt;
      if (load && gimple_assign_single_p (load)
	  && !is_gimple_reg (gimple_assign_rhs1 (load))
	  && TREE_CODE (lhs) == SSA_NAME
	  && single_imm_use (lhs, &use, &use_stmt)
	  && gimple_assign_single_p (use_stmt)
	  && operand_equal_p (gimple_assign_lhs (use_stmt),
			      gimple_assign_rhs1 (load), 0))
	target = cg_display_value (var_side);
    }
  if (!target && var_side && TREE_CODE (var_side) == SSA_NAME)
    {
      tree from = cg_display_value (var_side);
      if (DECL_P (from))
	{
	  if (TREE_CODE (lhs) == SSA_NAME && SSA_NAME_VAR (lhs) == from)
	    target = from;
	  else if (TREE_CODE (lhs) == SSA_NAME)
	    {
	      use_operand_p use;
	      gimple *use_stmt;
	      if (single_imm_use (lhs, &use, &use_stmt)
		  && gimple_assign_single_p (use_stmt)
		  && gimple_assign_lhs (use_stmt) == from)
		target = from;
	    }
	}
    }
  if (target)
    cg_conveyor_error_at (gimple_location (stmt), increasing
			  ? G_("increment of %qE not provably free of "
			       "overflow in a conveyor function")
			  : G_("decrement of %qE not provably free of "
			       "overflow in a conveyor function"), target);
  else
    cg_conveyor_error_at (gimple_location (stmt),
			  "result of %qE not provably free of "
			  "overflow in a conveyor function",
			  build2 (code, type, cg_display_value (op0),
				  cg_display_value (op1)));
}

/* D4324/P2680 item 8's array-bounds/pointer-arithmetic check, GIMPLE
   side -- the counterpart of oa_scan_array_bounds_in_expr, but scoped
   much narrower: "Increment 1" (deliberately named the same way item
   8's other two checks were, per this file's own established pattern of
   incremental growth) covers ONLY pointer-dereference provability --
   the ARRAY_TYPE/tracked-array-offset-POINTER_TYPE cases and the
   POINTER_PLUS_EXPR pointer-arithmetic-*formation* check are NOT ported
   (see below for why), so this is closer to just AST's own INDIRECT_REF
   "Increment W2" fallback (a dereferenced pointer with no tracked
   array-offset fact needs is_object_address provability instead) than
   to the full function.

   Why the rest isn't ported: AST's ARRAY_TYPE/POINTER_TYPE cases both
   depend on a pointer's own TRACKED ARRAY-OFFSET FACT (oa_range_fact's
   own BASE field -- "this pointer denotes some offset into array X,
   with a numeric range for exactly which offset") -- a whole fact KIND
   this file's own cg_range_lite has never had at all (no BASE field,
   confirmed by that struct's own definition) -- adding it is a new
   piece of infrastructure in its own right (establishment from an
   array-decl-to-pointer assignment, consult/composition through
   POINTER_PLUS_EXPR, etc.), not a mechanical port, and is deferred as
   its own future increment. Confirmed empirically (this file's own
   Stage 4c comment, near cg_field_slot_identity) that by this pass
   point array indexing has ALREADY been lowered to a MEM_REF with a
   byte offset -- 'arr[i]' and a hand-written '*(p + i)' are
   indistinguishable tree shapes here, so without that tracked-offset
   fact there is nothing further to check beyond the base pointer's own
   provability, which is exactly this increment's own scope.

   A GIMPLE memory reference reaching this pass point is either a bare
   MEM_REF, or a MEM_REF wrapped in one or more COMPONENT_REFs (a field
   access through a pointer, 'p->a.b' -- confirmed by the same Stage 4c
   comment: 'COMPONENT_REF (MEM_REF (base, byte_offset), field)').
   CG_EXTRACT_DEREF_BASE peels through any such COMPONENT_REF wrapping
   to find the underlying MEM_REF's own base pointer -- this is a
   deliberately unconditional peel regardless of the MEM_REF's own byte
   offset (a nonzero offset, e.g. a field access or an already-folded
   array slot, still requires the SAME base pointer to be provably
   valid; it just isn't itself bounds-checked any further here, matching
   this increment's own stated scope).  */

static bool
cg_extract_deref_base (tree ref, tree *base_out)
{
  if (ref == NULL_TREE)
    return false;
  while (TREE_CODE (ref) == COMPONENT_REF)
    ref = TREE_OPERAND (ref, 0);
  if (TREE_CODE (ref) != MEM_REF)
    return false;
  *base_out = TREE_OPERAND (ref, 0);
  return true;
}

/* Item 8's array bounds, GIMPLE side -- the counterpart of the AST
   engine's oa_scan_array_bounds_in_expr: an index into a fixed-size
   array must be provably in [0, N), a pointer formed into one ('&arr[i]',
   'p + n') in [0, N], and a dereference of such a pointer in [0, N).  A
   pointer is followed back to the array it points into
   (cg_pointer_array_offset), its offset counted in elements.

   The facts these consult, set for the duration of one function's
   check.  */


static bool cg_value_range_at (tree, gimple *, hash_map<tree, cg_range_lite> &,
			       hash_map<tree, cg_range_lite> &, gimple_ranger *,
			       bool, cg_range_lite *);

/* If VAL is a loop counter -- a PHI of an initial value and of itself
   plus a nonzero constant -- the bound its start and direction give it:
   never below the initial value when counting up, never above it when
   counting down.  Wrapping past the type's end would be signed overflow,
   which item 8 rejects on its own; a type whose overflow wraps gets no
   bound (an unsigned counter wraps to zero).  The initial value's range
   is taken from the maps given, as cg_value_range_at does.  */

static bool
cg_induction_variable_bound_1 (tree val,
			       hash_map<tree, cg_range_lite> &established_range,
			       hash_map<tree, cg_range_lite> &scalar_range_cache,
			       gimple_ranger *ranger, cg_range_lite *out)
{
  gphi *phi = dyn_cast <gphi *> (SSA_NAME_DEF_STMT (val));
  if (!phi || gimple_phi_num_args (phi) != 2
      || !INTEGRAL_TYPE_P (TREE_TYPE (val))
      || !TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (val)))
    return false;
  /* The initial value's range may be another loop's counter's.  */
  static unsigned depth;
  if (depth >= 8)
    return false;
  auto deeper = make_temp_override (depth, depth + 1);
  for (int step_arg = 0; step_arg < 2; ++step_arg)
    {
      tree next = gimple_phi_arg_def (phi, step_arg);
      tree init = gimple_phi_arg_def (phi, 1 - step_arg);
      if (TREE_CODE (next) != SSA_NAME)
	continue;
      gimple *inc = SSA_NAME_DEF_STMT (next);
      if (!inc || !is_gimple_assign (inc)
	  || (gimple_assign_rhs_code (inc) != PLUS_EXPR
	      && gimple_assign_rhs_code (inc) != MINUS_EXPR)
	  || gimple_assign_rhs1 (inc) != val
	  || TREE_CODE (gimple_assign_rhs2 (inc)) != INTEGER_CST)
	continue;
      widest_int step = wi::to_widest (gimple_assign_rhs2 (inc));
      if (gimple_assign_rhs_code (inc) == MINUS_EXPR)
	step = -step;
      if (step == 0)
	return false;
      cg_range_lite start;
      if (TREE_CODE (init) == INTEGER_CST)
	{
	  start.has_lo = start.has_hi = true;
	  start.lo = start.hi = wi::to_widest (init);
	}
      else if (!(TREE_CODE (init) == SSA_NAME
		 && cg_value_range_at (init, phi, established_range,
				       scalar_range_cache, ranger,
				       /*require_conveyor=*/true, &start)))
	return false;
      if (step > 0 && start.has_lo)
	{
	  out->has_lo = true;
	  out->lo = start.lo;
	  return true;
	}
      if (step < 0 && start.has_hi)
	{
	  out->has_hi = true;
	  out->hi = start.hi;
	  return true;
	}
      return false;
    }
  return false;
}

static bool
cg_induction_variable_bound (tree val, cg_range_lite *out)
{
  return cg_induction_variable_bound_1 (val,
					*cg_array_bounds->established_range,
					*cg_array_bounds->scalar_range_cache,
					cg_array_bounds->ranger, out);
}

/* The range of integer VAL at STMT, in *OUT.  */

static bool
cg_bounds_value_range (tree val, gimple *stmt, cg_range_lite *out)
{
  if (TREE_CODE (val) == INTEGER_CST)
    {
      out->has_lo = out->has_hi = true;
      out->lo = out->hi = wi::to_widest (val);
      return true;
    }
  if (!cg_array_bounds || TREE_CODE (val) != SSA_NAME
      || !INTEGRAL_TYPE_P (TREE_TYPE (val)))
    return false;
  bool any = cg_value_range_at (val, stmt, *cg_array_bounds->established_range,
				*cg_array_bounds->scalar_range_cache,
				cg_array_bounds->ranger,
				/*require_conveyor=*/true, out);
  /* A loop counter: the ranger, without loop information here, knows
     the bound the loop's condition gives it but not the one its start
     and direction do.  */
  cg_range_lite iv;
  if (cg_induction_variable_bound (val, &iv))
    {
      if (iv.has_lo && (!any || !out->has_lo || iv.lo > out->lo))
	out->has_lo = true, out->lo = iv.lo;
      if (iv.has_hi && (!any || !out->has_hi || iv.hi < out->hi))
	out->has_hi = true, out->hi = iv.hi;
      any = true;
    }
  return any && out->has_lo && out->has_hi;
}

/* BYTES, a POINTER_PLUS_EXPR's offset, as a range of elements of
   ELT_SIZE bytes, at STMT.  The offset is a sizetype; its signed meaning
   is recovered from what it was computed from: a constant, or an index
   converted to sizetype and scaled by the element size (and possibly
   negated, for 'p - i').  */

static bool
cg_element_offset_range (tree bytes, const widest_int &elt_size, gimple *stmt,
			 cg_range_lite *out)
{
  if (elt_size <= 0)
    return false;
  if (TREE_CODE (bytes) == INTEGER_CST)
    {
      widest_int v = widest_int::from (wi::to_wide (bytes), SIGNED);
      if (wi::smod_trunc (v, elt_size) != 0)
	return false;
      out->has_lo = out->has_hi = true;
      out->lo = out->hi = wi::sdiv_trunc (v, elt_size);
      return true;
    }
  if (TREE_CODE (bytes) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (bytes);
  if (!def || !is_gimple_assign (def))
    return false;
  tree_code code = gimple_assign_rhs_code (def);
  if (code == NEGATE_EXPR)
    {
      cg_range_lite r;
      if (!cg_element_offset_range (gimple_assign_rhs1 (def), elt_size, stmt,
				    &r))
	return false;
      out->has_lo = out->has_hi = true;
      out->lo = -r.hi;
      out->hi = -r.lo;
      return true;
    }
  tree index = NULL_TREE;
  if (code == MULT_EXPR
      && TREE_CODE (gimple_assign_rhs2 (def)) == INTEGER_CST
      && wi::to_widest (gimple_assign_rhs2 (def)) == elt_size)
    index = gimple_assign_rhs1 (def);
  else if (elt_size == 1)
    index = bytes;
  if (!index)
    return false;
  /* The index before its conversion to sizetype.  */
  if (TREE_CODE (index) == SSA_NAME)
    if (gimple *cv = SSA_NAME_DEF_STMT (index))
      if (is_gimple_assign (cv)
	  && CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (cv))
	  && INTEGRAL_TYPE_P (TREE_TYPE (gimple_assign_rhs1 (cv))))
	index = gimple_assign_rhs1 (cv);
  if (TREE_CODE (index) == INTEGER_CST)
    {
      out->has_lo = out->has_hi = true;
      out->lo = out->hi = widest_int::from (wi::to_wide (index),
					    TYPE_SIGN (TREE_TYPE (index)));
      return true;
    }
  return cg_bounds_value_range (index, stmt, out);
}

/* If pointer VAL points into a fixed-size array, set *ARRAY_TYPE to the
   array's type and *OFF to the range of VAL's offset into it, in
   elements, and return true.  */

static bool
cg_pointer_array_offset (tree val, gimple *at, tree *array_type,
			 cg_range_lite *off, int depth)
{
  if (depth > 8)
    return false;
  if (TREE_CODE (val) == ADDR_EXPR)
    {
      tree op = TREE_OPERAND (val, 0);
      if (TREE_CODE (TREE_TYPE (op)) == ARRAY_TYPE)
	{
	  *array_type = TREE_TYPE (op);
	  off->has_lo = off->has_hi = true;
	  off->lo = off->hi = 0;
	  return true;
	}
      if (TREE_CODE (op) == ARRAY_REF
	  && TREE_CODE (TREE_TYPE (TREE_OPERAND (op, 0))) == ARRAY_TYPE)
	{
	  *array_type = TREE_TYPE (TREE_OPERAND (op, 0));
	  return cg_bounds_value_range (TREE_OPERAND (op, 1), at, off);
	}
      return false;
    }
  if (TREE_CODE (val) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (gphi *phi = dyn_cast <gphi *> (def))
    {
      tree type = NULL_TREE;
      cg_range_lite u;
      for (unsigned i = 0; i < gimple_phi_num_args (phi); ++i)
	{
	  tree t;
	  cg_range_lite r;
	  if (!cg_pointer_array_offset (gimple_phi_arg_def (phi, i), phi, &t,
					&r, depth + 1)
	      || (type && !types_compatible_p (t, type)))
	    return false;
	  if (!type)
	    u = r;
	  else
	    {
	      if (r.lo < u.lo)
		u.lo = r.lo;
	      if (r.hi > u.hi)
		u.hi = r.hi;
	    }
	  type = t;
	}
      if (!type)
	return false;
      *array_type = type;
      *off = u;
      return true;
    }
  if (!def || !is_gimple_assign (def))
    return false;
  tree_code code = gimple_assign_rhs_code (def);
  tree rhs1 = gimple_assign_rhs1 (def);
  if (code == ADDR_EXPR || code == SSA_NAME || CONVERT_EXPR_CODE_P (code))
    return cg_pointer_array_offset (rhs1, def, array_type, off, depth + 1);
  if (code == POINTER_PLUS_EXPR)
    {
      cg_range_lite base, delta;
      tree t;
      if (!cg_pointer_array_offset (rhs1, def, &t, &base, depth + 1))
	return false;
      tree elt = TREE_TYPE (t);
      if (!TYPE_SIZE_UNIT (elt) || TREE_CODE (TYPE_SIZE_UNIT (elt)) != INTEGER_CST
	  || !cg_element_offset_range (gimple_assign_rhs2 (def),
				       wi::to_widest (TYPE_SIZE_UNIT (elt)),
				       def, &delta))
	return false;
      *array_type = t;
      off->has_lo = off->has_hi = true;
      off->lo = base.lo + delta.lo;
      off->hi = base.hi + delta.hi;
      return true;
    }
  return false;
}

/* Is offset range OFF, into an array of type ARRAY_TYPE, within it --
   [0, N) for an access, [0, N] for forming a pointer (one past the end
   is formable)?  0 if provably, 2 if provably not -- a single offset, or
   a range wholly outside -- and 1 otherwise.  A range here can be the
   ranger's, which may just be the type's, so a range merely reaching
   outside isn't taken as an out-of-bounds offset.  */

static int
cg_offset_in_bounds (const cg_range_lite &off, tree array_type, bool formation)
{
  tree dom = TYPE_DOMAIN (array_type);
  tree max = dom ? TYPE_MAX_VALUE (dom) : NULL_TREE;
  if (!off.has_lo || !off.has_hi || !max || TREE_CODE (max) != INTEGER_CST)
    return 1;
  widest_int limit = wi::to_widest (max) + (formation ? 1 : 0);
  if (off.lo >= 0 && off.hi <= limit)
    return 0;
  if (off.lo == off.hi || off.hi < 0 || off.lo > limit)
    return 2;
  return 1;
}

/* Report an index, formation or dereference (WHAT, naming DIAG_EXPR if
   not null) at STMT according to VERDICT (cg_offset_in_bounds).  */

static void
cg_report_bounds (gimple *stmt, int verdict, const char *what, tree diag_expr)
{
  if (verdict == 0)
    return;
  location_t loc = gimple_location (stmt);
  if (verdict == 1)
    {
      if (diag_expr)
	cg_conveyor_error_at (loc, "%s %qE not provably in-bounds in a "
			      "conveyor function", what, diag_expr);
      else
	cg_conveyor_error_at (loc, "%s not provably in-bounds in a "
			      "conveyor function", what);
    }
  else if (diag_expr)
    cg_conveyor_error_at (loc, "%s %qE out of bounds in a conveyor function",
			  what, diag_expr);
  else
    cg_conveyor_error_at (loc, "%s out of bounds in a conveyor function",
			  what);
}

/* Check the array indexes in operand *TP of STMT, and the pointers it
   forms into arrays; INSIDE_ADDR while under an ADDR_EXPR, where an
   ARRAY_REF only forms a pointer.  */

static void
cg_check_array_refs_in (gimple *stmt, tree t, bool inside_addr)
{
  if (!t)
    return;
  if (TREE_CODE (t) == ADDR_EXPR)
    {
      cg_check_array_refs_in (stmt, TREE_OPERAND (t, 0), true);
      return;
    }
  if (handled_component_p (t))
    {
      if (TREE_CODE (t) == ARRAY_REF
	  && TREE_CODE (TREE_TYPE (TREE_OPERAND (t, 0))) == ARRAY_TYPE)
	{
	  tree index = TREE_OPERAND (t, 1);
	  cg_range_lite r;
	  int verdict = (cg_bounds_value_range (index, stmt, &r)
			 ? cg_offset_in_bounds (r, TREE_TYPE (TREE_OPERAND (t, 0)),
						/*formation=*/inside_addr)
			 : 1);
	  /* '&arr[k]' is an index in the source; 'arr + 3', folded to
	     '&arr[3]', pointer arithmetic.  */
	  bool arith = inside_addr && TREE_CODE (index) == INTEGER_CST;
	  cg_report_bounds (stmt, verdict,
			    arith ? "pointer arithmetic" : "array index",
			    arith ? NULL_TREE : cg_display_value (index));
	}
      /* An outer reference only forms a pointer if it is the whole
	 operand of the ADDR_EXPR; an inner one is an access path.  */
      cg_check_array_refs_in (stmt, TREE_OPERAND (t, 0), inside_addr);
    }
}

/* Item 8's array bounds for STMT: indexes, pointer formation
   ('p + n'), and dereferences of pointers into arrays.  */

static void
cg_check_array_bounds_ub (gimple *stmt)
{
  if (is_gimple_assign (stmt))
    {
      if (gimple_assign_rhs_code (stmt) == POINTER_PLUS_EXPR)
	{
	  tree array_type;
	  cg_range_lite off;
	  /* Only a pointer already known to point into an array: an
	     arbitrary pointer's arithmetic isn't tracked (as in the AST
	     engine), only its dereference is checked.  */
	  cg_range_lite base;
	  if (cg_pointer_array_offset (gimple_assign_rhs1 (stmt), stmt,
				       &array_type, &base))
	    cg_report_bounds (stmt,
			      cg_pointer_array_offset (gimple_assign_lhs (stmt),
						       stmt, &array_type, &off)
			      ? cg_offset_in_bounds (off, array_type,
						     /*formation=*/true)
			      : 1,
			      "pointer arithmetic", NULL_TREE);
	  return;
	}
      cg_check_array_refs_in (stmt, gimple_assign_lhs (stmt), false);
      for (unsigned i = 1; i < gimple_num_ops (stmt); ++i)
	cg_check_array_refs_in (stmt, gimple_op (stmt, i), false);
    }
  else if (gcall *call = dyn_cast <gcall *> (stmt))
    {
      cg_check_array_refs_in (stmt, gimple_call_lhs (call), false);
      for (unsigned i = 0; i < gimple_call_num_args (call); ++i)
	cg_check_array_refs_in (stmt, gimple_call_arg (call, i), false);
    }
}

/* Check CANDIDATE (a GIMPLE_ASSIGN's own LHS or RHS1, or a GIMPLE_CALL's
   own LHS) for a pointer dereference needing is_object_address
   provability -- see this section's own leading comment.  */

static void
cg_check_one_dereference_candidate (gimple *stmt, tree candidate,
				      hash_map<tree, cg_fact> &established)
{
  tree base;
  if (!cg_extract_deref_base (candidate, &base))
    return;
  if (TREE_CODE (base) != SSA_NAME)
    return;
  /* An object's vtable pointer, loaded for a virtual call: the vtable
     is valid whenever the object is, whose own validity the access to
     its vtable pointer field already needed.  */
  gimple *bdef = SSA_NAME_DEF_STMT (base);
  if (bdef && gimple_assign_single_p (bdef)
      && TREE_CODE (gimple_assign_rhs1 (bdef)) == COMPONENT_REF
      && DECL_VIRTUAL_P (TREE_OPERAND (gimple_assign_rhs1 (bdef), 1)))
    return;
  /* POINTER_TYPE, or a REFERENCE-typed PARAMETER specifically --
     mirroring oa_scan_array_bounds_in_expr's own identical condition
     (contracts.cc) after its own 2026-08-26 fix (Ville: "a conveyor
     contract_assert's predicate requires object validity when the
     predicate... uses a reference"). Any OTHER REFERENCE_TYPE operand
     (a local reference/capture binding) is still excluded: it's
     guaranteed valid for its own entire lifetime by the language
     itself, and any unsafe binding would already have been caught by
     this same mandatory pass at its own point of initialization -- see
     that function's own comment for the lambda-by-reference-capture
     case that motivated the original, narrower exclusion. A reference
     PARAMETER is different: its binding comes from an external,
     unverified caller, exactly the case Q1/is_object_address exists to
     catch. SSA_NAME_VAR (base) recovers the original PARM_DECL for an
     ordinary, unmodified parameter read (its own default-def SSA
     name); guarded against NULL for an anonymous temporary with no
     source-level variable at all.  */
  bool is_ref_parm = TREE_CODE (TREE_TYPE (base)) == REFERENCE_TYPE
		      && SSA_NAME_VAR (base)
		      && TREE_CODE (SSA_NAME_VAR (base)) == PARM_DECL;
  if (TREE_CODE (TREE_TYPE (base)) != POINTER_TYPE && !is_ref_parm)
    return;

  /* A pointer into an array must stay within it.  */
  tree array_type;
  cg_range_lite off;
  if (cg_pointer_array_offset (base, stmt, &array_type, &off))
    {
      if (TREE_CODE (candidate) == MEM_REF
	  && TREE_CODE (TREE_OPERAND (candidate, 1)) == INTEGER_CST)
	{
	  /* A constant byte offset in the MEM_REF itself.  */
	  cg_range_lite delta;
	  tree elt = TREE_TYPE (array_type);
	  if (TYPE_SIZE_UNIT (elt) && TREE_CODE (TYPE_SIZE_UNIT (elt)) == INTEGER_CST
	      && cg_element_offset_range (TREE_OPERAND (candidate, 1),
					  wi::to_widest (TYPE_SIZE_UNIT (elt)),
					  stmt, &delta))
	    {
	      off.lo += delta.lo;
	      off.hi += delta.hi;
	    }
	}
      cg_report_bounds (stmt, cg_offset_in_bounds (off, array_type, false),
			"pointer dereference", NULL_TREE);
      return;
    }

  hash_set<tree> in_progress;
  if (!cg_provable_object_address_p (base, established, /*require_conveyor=*/true,
				       in_progress))
    cg_conveyor_error_at (gimple_location (stmt),
			  "pointer dereference of %qE not "
			  "provably valid in a conveyor function",
			  cg_display_value (base));
}

static void
cg_check_dereference_ub (gimple *stmt, hash_map<tree, cg_fact> &established)
{
  /* A member or element access through a pointer whose address alone
     is taken ('&p->v'): forming it is a dereference too, as in the AST
     engine.  */
  auto address_operand = [&] (tree op)
    {
      if (op && TREE_CODE (op) == ADDR_EXPR
	  && (TREE_CODE (TREE_OPERAND (op, 0)) == COMPONENT_REF
	      || TREE_CODE (TREE_OPERAND (op, 0)) == ARRAY_REF))
	cg_check_one_dereference_candidate (stmt, TREE_OPERAND (op, 0),
					    established);
    };
  if (is_gimple_assign (stmt))
    {
      cg_check_one_dereference_candidate (stmt, gimple_assign_lhs (stmt),
					    established);
      cg_check_one_dereference_candidate (stmt, gimple_assign_rhs1 (stmt),
					    established);
      address_operand (gimple_assign_rhs1 (stmt));
    }
  else if (is_gimple_call (stmt))
    {
      cg_check_one_dereference_candidate (stmt, gimple_call_lhs (stmt),
					    established);
      for (unsigned i = 0; i < gimple_call_num_args (stmt); ++i)
	address_operand (gimple_call_arg (stmt, i));
    }
}


/* A contract_assert conjunct's verdict against the facts at the assert
   (CG_VERDICT_NONE: not a shape this check decides).  */

enum cg_verdict { CG_VERDICT_NONE, CG_VERDICT_TRUE, CG_VERDICT_FALSE,
		  CG_VERDICT_UNKNOWN };

/* Does every value in R satisfy 'value CODE C' (true), none (false), or
   neither (unknown)?  */

static cg_verdict
cg_range_comparison_verdict (const cg_range_lite &r, tree_code code,
			     const widest_int &c)
{
  switch (code)
    {
    case GT_EXPR:
      if (r.has_lo && r.lo > c) return CG_VERDICT_TRUE;
      if (r.has_hi && r.hi <= c) return CG_VERDICT_FALSE;
      break;
    case GE_EXPR:
      if (r.has_lo && r.lo >= c) return CG_VERDICT_TRUE;
      if (r.has_hi && r.hi < c) return CG_VERDICT_FALSE;
      break;
    case LT_EXPR:
      if (r.has_hi && r.hi < c) return CG_VERDICT_TRUE;
      if (r.has_lo && r.lo >= c) return CG_VERDICT_FALSE;
      break;
    case LE_EXPR:
      if (r.has_hi && r.hi <= c) return CG_VERDICT_TRUE;
      if (r.has_lo && r.lo > c) return CG_VERDICT_FALSE;
      break;
    case EQ_EXPR:
      if (r.has_lo && r.has_hi && r.lo == c && r.hi == c)
	return CG_VERDICT_TRUE;
      if ((r.has_lo && r.lo > c) || (r.has_hi && r.hi < c))
	return CG_VERDICT_FALSE;
      break;
    default:
      break;
    }
  return CG_VERDICT_UNKNOWN;
}

/* Whether no value in range HAVE lies in range NEED.  */

static bool
cg_ranges_disjoint_p (const cg_range_lite &have, const cg_range_lite &need)
{
  return ((need.has_hi && have.has_lo && have.lo > need.hi)
	  || (need.has_lo && have.has_hi && have.hi < need.lo));
}

/* The same for 'value != 0'.  */

static cg_verdict
cg_range_nonzero_verdict (const cg_range_lite &r)
{
  if ((r.has_lo && r.lo > 0) || (r.has_hi && r.hi < 0))
    return CG_VERDICT_TRUE;
  if (r.has_lo && r.has_hi && r.lo == 0 && r.hi == 0)
    return CG_VERDICT_FALSE;
  return CG_VERDICT_UNKNOWN;
}

/* The values satisfying 'x CODE C', C a REAL_CST, as a NaN-free range of
   TYPE in *OUT.  False for a comparison that doesn't give one.  */

static bool
cg_frange_for_comparison (tree type, tree_code code, tree c, frange *out)
{
  REAL_VALUE_TYPE pinf, ninf, v = TREE_REAL_CST (c), lo, hi;
  real_inf (&pinf);
  real_inf (&ninf, true);
  format_helper fmt (REAL_MODE_FORMAT (TYPE_MODE (type)));
  switch (code)
    {
    case GE_EXPR: lo = v; hi = pinf; break;
    case GT_EXPR: real_nextafter (&lo, fmt, &v, &pinf); hi = pinf; break;
    case LE_EXPR: lo = ninf; hi = v; break;
    case LT_EXPR: lo = ninf; real_nextafter (&hi, fmt, &v, &ninf); break;
    case EQ_EXPR: lo = hi = v; break;
    default: return false;
    }
  if (real_isnan (&v))
    return false;
  frange r (type, lo, hi);
  r.clear_nan ();
  *out = r;
  return true;
}
/* Describe floating-point range R in BUF, as "in [LO, HI]".  */

static char *
cg_frange_text (const frange &r, char *buf, size_t n)
{
  if (r.undefined_p () || r.known_isnan ())
    {
      snprintf (buf, n, "%s", r.undefined_p () ? "impossible" : "NaN");
      return buf;
    }
  char lo[40], hi[40];
  real_to_decimal (lo, &r.lower_bound (), sizeof lo, 0, 1);
  real_to_decimal (hi, &r.upper_bound (), sizeof hi, 0, 1);
  snprintf (buf, n, "in [%s, %s]", lo, hi);
  return buf;
}

/* Does established floating-point range HAVE satisfy required range
   NEED: true if every value of HAVE is in NEED (so none is a NaN), false
   if none can be, unknown otherwise.  */

static cg_verdict
cg_frange_requirement_verdict (const frange &have, const frange &need)
{
  if (have.undefined_p ())
    return CG_VERDICT_UNKNOWN;
  if (have.known_isnan ())
    return CG_VERDICT_FALSE;
  frange both = have;
  both.intersect (need);
  if (both.undefined_p ())
    return CG_VERDICT_FALSE;
  if (!have.maybe_isnan ()
      && !real_less (&have.lower_bound (), &need.lower_bound ())
      && !real_less (&need.upper_bound (), &have.upper_bound ()))
    return CG_VERDICT_TRUE;
  return CG_VERDICT_UNKNOWN;
}

/* Whether the GIMPLE engine makes the proof checks (the ones beyond the
   mandatory obligations) for CONTRACT, a precondition of CALLEE, in the
   conveyor flavor if CONVEYOR, else the symbolic one.  Under
   -fcontract-analysis-engine= they follow the AST engine's own gate: the
   -fcontract-conveyor-proofs/-fcontract-symbolic-proofs flag, or a
   control object that forces analysis (analyzed_X, proven_X).  With the
   older -fcontract-*-proofs-gimple flags -- alone, or given explicitly
   with the engine option, which otherwise only implies them -- those
   flags themselves turn them on.  */

static bool
cg_proof_checks_p (tree contract, tree callee, bool conveyor)
{
  if (flag_contract_analysis_engine == 0)
    return true;
  if (conveyor
      ? OPTION_SET_P (flag_contract_conveyor_proofs_gimple)
	&& flag_contract_conveyor_proofs_gimple
      : OPTION_SET_P (flag_contract_symbolic_proofs_gimple)
	&& flag_contract_symbolic_proofs_gimple)
    return true;
  bool never_proven, conveyor_forced, conveyor_strict, symbolic_forced,
    symbolic_strict;
  if (!oa_contract_self_check_mode_public (contract, callee, &never_proven,
					   &conveyor_forced, &conveyor_strict,
					   &symbolic_forced, &symbolic_strict))
    return false;
  return (conveyor
	  ? flag_contract_conveyor_proofs || conveyor_forced
	  : flag_contract_symbolic_proofs || symbolic_forced);
}

/* CALL's own callee, checked against ESTABLISHED/ESTABLISHED_NZ as
   they stand right before CALL -- the consult side. Each of the
   callee's own preconditions is only checked against the flag whose
   own flavor it matches (a conveyor-active precondition is only an
   obligation to discharge when -fcontract-conveyor-proofs-gimple is
   on, and its own consult REQUIRE_CONVEYORs the one-way-trust rule; a
   symbolic-active one is only checked when -fcontract-symbolic-proofs-
   gimple is on, and accepts either provenance).  */

static bool cg_conveyor_proofs_p ();
static bool cg_symbolic_proofs_p ();
static bool cg_frange_of (tree, gimple *, gimple_ranger *,
			  hash_map<tree, frange> *, frange &, int = 0);

static void
cg_check_call (gcall *call, hash_map<tree, cg_fact> &established,
		hash_map<tree, cg_fact> &established_nz,
		hash_map<tree, cg_range_lite> &established_range,
		hash_map<tree, cg_rel_fact> &established_rel,
		hash_map<cg_field_key_hash, cg_call_call_rel_fact>
		  &established_call_call_rel,
		hash_map<tree, cg_range_lite> &scalar_range_cache,
		gimple_ranger *ranger,
		hash_set<gimple *> *call_relational_verdict,
		hash_map<tree, frange> *established_frange = NULL)
{
  auto at = make_temp_override (cg_prover_at, call);
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  /* The callee parameters whose is_object_address or nonzero obligation
     has been reported for CALL already: several preconditions can
     require the same one (an explicit one besides the implicit one a
     conveyor function's reference parameter gets, say), and each
     unprovable obligation should be reported once.  */
  hash_set<tree> reported_oa_parms, reported_nz_parms;
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      bool conveyor_active = oa_contract_conveyor_active_cached_p (contract);
      bool symbolic_active = oa_contract_symbolic_active_cached_p (contract);
      /* See cg_callee_pre_mandatory.  */
      bool mandatory = (cg_callee_pre_mandatory && conveyor_active
			&& DECL_DECLARED_CONVEYOR_P (callee));
      if (cg_callee_pre_only && !mandatory)
	continue;
      bool check_as_conveyor = (mandatory
				|| (conveyor_active
				    && flag_contract_conveyor_proofs_gimple));
      bool check_as_symbolic = (!cg_callee_pre_only && symbolic_active
				&& flag_contract_symbolic_proofs_gimple);
      if (!check_as_conveyor && !check_as_symbolic)
	continue;
      /* A contract enabled for both flavors at once is checked as
	 conveyor (the stricter, one-way-trust-requiring direction) --
	 discharging that also discharges the weaker symbolic-side
	 obligation, so there is nothing further to check twice.  */
      bool require_conveyor = check_as_conveyor;

      /* D4324: strict (proven_conveyor/proven_symbolic) vs lenient
	 (analyzed_conveyor/analyzed_symbolic, or plain is_conveyor/is_
	 symbolic under the ordinary command-line flag) -- mirrors
	 contracts.cc's own 'strict' (see oa_handle_call_conveyor_proof_
	 obligation/oa_call_conveyor_obligation_status): an OA_UNKNOWN
	 result is escalated from a warning to a hard error. Computed per-
	 CONTRACT here (unlike the built-in engine's own OR-across-all-of-
	 the-callee's-preconditions scope), which is at least as precise.  */
      bool strict = mandatory
		    || (check_as_conveyor
			&& oa_contract_conveyor_strict_cached_p (contract))
		    || (check_as_symbolic
			&& oa_contract_symbolic_strict_cached_p (contract));
      /* Under -fcontract-analysis-engine=, a conveyor precondition's
	 is_object_address conjunct is a mandatory obligation, as in the
	 AST engine (oa_handle_call_precondition_obligation) -- except in a
	 never-proven predicate (cg_callee_pre_only).  Everything else here
	 is a proof check (cg_proof_checks_p), unless MANDATORY.  */
      bool mandatory_oa = (flag_contract_analysis_engine != 0
			   && check_as_conveyor && !cg_callee_pre_only);
      bool proofs = (mandatory
		     || (check_as_conveyor
			 && cg_proof_checks_p (contract, callee, true))
		     || (check_as_symbolic
			 && cg_proof_checks_p (contract, callee, false)));

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
	  if (is_oa && cg_callee_pre_only)
	    continue;
	  if (!proofs && !(is_oa && mandatory_oa))
	    continue;

	  /* '&r' for a reference parameter R (an explicit or synthesized
	     precondition) arrives as a conversion of R.  A conveyor
	     callee's reference parameters and 'this' are cg_check_call_
	     reference_safety's to check.  */
	  while (TREE_CODE (arg) == NON_LVALUE_EXPR || CONVERT_EXPR_P (arg)
		 || TREE_CODE (arg) == VIEW_CONVERT_EXPR)
	    arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
	  if (TREE_CODE (arg) != PARM_DECL)
	    continue;
	  if ((TYPE_REF_P (TREE_TYPE (arg)) || is_this_parameter (arg))
	      && DECL_DECLARED_CONVEYOR_P (callee))
	    continue;

	  unsigned argno;
	  if (!cg_find_param_position (callee, arg, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;

	  tree substituted = (is_oa ? cg_resolve_call_argument (call, argno)
			      : cg_resolve_value_argument (call, argno));
	  /* Not through a conversion operator: the pointer it returns
	     (its postcondition says what it is), not the object it is
	     called on.  */
	  if (is_oa && !POINTER_TYPE_P (TREE_TYPE (substituted)))
	    substituted = gimple_call_arg (call, argno);
	  hash_set<tree> in_progress;
	  if (is_oa)
	    {
	      oa_unprovable_reason reason = OA_UNPROVABLE_NONE;
	      if (cg_provable_object_address_p (substituted, established,
						 require_conveyor, in_progress,
						 &reason))
		continue; /* Proven true: silently discharged.  */
	      if (reported_oa_parms.add (arg))
		continue; /* Already reported for this call.  */
	      if (mandatory_oa)
		cg_conveyor_error_at (gimple_location (call),
				      "cannot prove %<is_object_address%> for "
				      "%qE, required by the precondition of %qD",
				      cg_display_argument (substituted, arg),
				      callee);
	      else if (strict)
		cg_obligation_error_at (gimple_location (call),
					 "cannot prove %<is_object_address%> for %qE, as "
					 "required by the precondition of %qD",
					 substituted, callee);
	      else
		warning_at (gimple_location (call), 0,
			    "cannot verify %<is_object_address%> for %qE, as "
			    "required by the precondition of %qD",
			    substituted, callee);
	      if (const char *why = oa_unprovable_reason_text (reason))
		inform (gimple_location (call), "%s", _(why));
	    }
	  else
	    {
	      /* D4324: a *literal* substituted argument's value is always
		 fully known, and known-zero is exactly what the
		 precondition rules out -- sharpened past "cannot verify"
		 all the way to a hard, unconditional "provably violates"
		 error for that one case, mirroring contracts.cc's own
		 identical literal-argument sharpening in its nonzero-
		 conjunct branch (oa_handle_call_conveyor_proof_obligation).  */
	      tree stripped_nz = STRIP_ANY_LOCATION_WRAPPER (substituted);
	      if (TREE_CODE (stripped_nz) == INTEGER_CST)
		{
		  if (!integer_zerop (stripped_nz))
		    continue; /* Proven true: silently discharged.  */
		  cg_obligation_error_at (gimple_location (call),
					   "argument %qE provably violates the "
					   "precondition of %qD", substituted, callee);
		  inform (gimple_location (call),
			  "%qE is established to be exactly zero, but the "
			  "precondition requires it to be nonzero",
			  substituted);
		  continue;
		}
	      if (cg_provable_nonzero_p (substituted, established_nz,
					 require_conveyor, in_progress)
		  || cg_excludes_zero_at_p (substituted, call, ranger,
					    require_conveyor))
		continue; /* Proven true: silently discharged.  */
	      if (reported_nz_parms.add (arg))
		continue; /* Already reported for this call.  */
	      /* An anonymous SSA name prints as '<unknown>'; name the call
		 it is the result of instead.  */
	      tree result_of = NULL_TREE;
	      if (TREE_CODE (substituted) == SSA_NAME
		  && !SSA_NAME_VAR (substituted))
		{
		  tree src = substituted;
		  gimple *def = SSA_NAME_DEF_STMT (src);
		  while (def && gimple_assign_ssa_name_copy_p (def))
		    {
		      src = gimple_assign_rhs1 (def);
		      def = SSA_NAME_DEF_STMT (src);
		    }
		  if (def && is_gimple_call (def))
		    result_of = gimple_call_fndecl (as_a <gcall *> (def));
		  /* As the call: 'any (x)'.  */
		  if (result_of)
		    {
		      gcall *dc = as_a <gcall *> (def);
		      auto_vec<tree, 8> args;
		      for (unsigned k = 0; k < gimple_call_num_args (dc); ++k)
			args.safe_push (cg_display_value (gimple_call_arg (dc, k)));
		      substituted
			= build_call_expr_loc_array (UNKNOWN_LOCATION, result_of,
						     args.length (),
						     args.address ());
		    }
		}
	      if (strict)
		cg_obligation_error_at (gimple_location (call),
					 "cannot prove that %qE is nonzero, as required by "
					 "the precondition of %qD", substituted, callee);
	      else
		warning_at (gimple_location (call), 0,
			    "cannot verify that %qE is nonzero, as required by "
			    "the precondition of %qD", substituted, callee);
	    }
	}

      /* Relational obligations against another of the callee's own
	 parameters (e.g. 'pre<ctrl>(x < q)') -- both PARM_DECLs
	 substituted positionally, mirroring contracts.cc's own
	 oa_handle_call_conveyor_proof_obligation/oa_handle_call_
	 symbolic_precondition_obligation.

	 Unlike the plain relational loop's own literal-vs-literal and
	 numeric range-vs-range cases below (both branch-independent, so
	 safe to keep here), the "explicit fact vs REL_CODE" check for a
	 *branch*-derived fact does not live here at all -- it runs from
	 cg_predicate_facts_walk's own final pass instead, against
	 dominator-correct STATE.rel (see cg_check_call_relational_fact's
	 own comment for why: this loop has no dominator-tree awareness of
	 its own, and the flattened, function-wide cache that would
	 otherwise be the only thing available here can hold two genuinely
	 conflicting facts, from different arms of the same branch, under
	 the same key). A *self-trust*-derived fact is different: it is
	 never branch-dependent, so ESTABLISHED_REL alone -- with no
	 flattened-cache fallback (a fresh, empty map, so a genuinely
	 branch-derived fact is never picked up here by accident) -- is
	 sound to consult directly.  */
      if (!proofs)
	continue;

      /* A floating-point parameter compared with a constant
	 ('pre (x <= 100.0)'): the argument's range -- a literal, or what
	 the ranger and the established facts give for an SSA value --
	 against the range the conjunct requires.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree parm, const_val;
	  tree_code code;
	  frange need;
	  if (!oa_match_simple_comparison (*conjuncts[i], &parm, &code,
					   &const_val)
	      || TREE_CODE (const_val) != REAL_CST
	      || TREE_CODE (parm) != PARM_DECL
	      || !SCALAR_FLOAT_TYPE_P (TREE_TYPE (parm))
	      || !cg_frange_for_comparison (TREE_TYPE (parm), code, const_val,
					    &need))
	    continue;
	  unsigned argno;
	  if (!cg_find_param_position (callee, parm, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree arg = cg_resolve_value_argument (call, argno);
	  if (!SCALAR_FLOAT_TYPE_P (TREE_TYPE (arg)))
	    continue;
	  frange have (TREE_TYPE (arg));
	  if (TREE_CODE (arg) == REAL_CST)
	    {
	      have = frange (arg, arg);
	      if (!real_isnan (TREE_REAL_CST_PTR (arg)))
		have.clear_nan ();
	    }
	  else if (TREE_CODE (arg) == SSA_NAME)
	    {
	      /* Folded from its operands' facts, too ('t.m_value - 1e3').  */
	      if (!cg_frange_of (arg, call, ranger, established_frange, have))
		have.set_varying (TREE_TYPE (arg));
	    }
	  else
	    continue;
	  cg_verdict v = cg_frange_requirement_verdict (have, need);
	  if (v == CG_VERDICT_TRUE)
	    continue;
	  if (v == CG_VERDICT_FALSE)
	    {
	      char have_text[96], need_text[96];
	      cg_obligation_error_at (gimple_location (call),
				       "argument %qE provably violates the precondition of %qD",
				       cg_display_value (arg), callee);
	      inform (gimple_location (call),
		      "%qE is established %s, but the precondition requires "
		      "it %s", cg_display_value (arg),
		      cg_frange_text (have, have_text, sizeof have_text),
		      cg_frange_text (need, need_text, sizeof need_text));
	      continue;
	    }
	  if (strict)
	    cg_obligation_error_at (gimple_location (call),
				     "cannot prove that %qE satisfies the precondition of %qD",
				     cg_display_value (arg), callee);
	  else
	    warning_at (gimple_location (call), 0,
			"cannot verify that %qE satisfies the precondition of "
			"%qD", cg_display_value (arg), callee);
	  if (const char *why
		= oa_unprovable_reason_text (OA_UNPROVABLE_RANGE_PARTIAL))
	    inform (gimple_location (call), "%s", _(why));
	}
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rel_other;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_param (*conjuncts[i], &rel_param,
						   &rel_code, &rel_other))
	    continue;

	  unsigned param_argno, other_argno;
	  if (!cg_find_param_position (callee, rel_param, &param_argno)
	      || !cg_find_param_position (callee, rel_other, &other_argno)
	      || param_argno >= gimple_call_num_args (call)
	      || other_argno >= gimple_call_num_args (call))
	    continue;

	  tree sub_param = cg_resolve_value_argument (call, param_argno);
	  tree sub_other = cg_resolve_value_argument (call, other_argno);

	  /* Both sides are ordinary compile-time literals at this call
	     site -- plain constant folding, not resolving any
	     parameter's own opaque meaning (see oa_relational_literal_
	     holds's own comment in contracts.cc).  */
	  if (TREE_CODE (sub_param) == INTEGER_CST
	      && TREE_CODE (sub_other) == INTEGER_CST)
	    {
	      if (oa_relational_literal_holds (rel_code, sub_param, sub_other))
		continue; /* Proven true: silently discharged.  */
	      /* D4324: both sides are fully known literals, so this is a
		 genuine, provable violation, not merely "unknown" -- fixed
		 from an (inconsistent) warning_at to match this loop's own
		 OA_RANGE_DISJOINT sibling case below, and contracts.cc's own
		 error_at for the identical shape.  */
	      cg_obligation_error_at (gimple_location (call),
				       "argument %qE provably violates the precondition "
				       "of %qD", cg_display_value (sub_param), callee);
	      inform (gimple_location (call),
		      "%qE is established exactly %wd, but the precondition "
		      "requires it to be %s %qE", cg_display_value (sub_param),
		      wi::to_wide (sub_param).to_shwi (),
		      op_symbol_code (rel_code), sub_other);
	      continue;
	    }

	  /* SUB_OTHER is already resolved through the same conversion-
	     operator/copy-construction lookthrough (cg_resolve_call_
	     argument, above) that cg_get_relational applies to SUB_PARAM
	     internally, so comparing it directly against FACT_RHS below
	     is safe -- e.g. two class-typed parameters each converted to
	     int for a plain-int callee's own precondition ('check (x, q)'
	     where x/q are wrap-typed): without this, SUB_OTHER would
	     arrive as q's own converted SSA result, not q itself, and
	     never match FACT_RHS (q, as self-trust originally recorded
	     it).  */
	  oa_unprovable_reason reason = OA_UNPROVABLE_NO_FACT;
	  {
	    hash_map<tree, cg_rel_fact> empty_rel_cache;
	    tree_code fact_code;
	    tree fact_rhs;
	    cg_range_lite fact_offset;
	    bool fact_conveyor_established;
	    if (cg_get_relational (sub_param, established_rel, empty_rel_cache,
				     established_range, scalar_range_cache, ranger,
				     check_as_conveyor, check_as_symbolic,
				     &fact_code, &fact_rhs, &fact_offset,
				     &fact_conveyor_established))
	      {
		if (fact_rhs != sub_other)
		  reason = OA_UNPROVABLE_WRONG_IDENTITY;
		else if (require_conveyor && !fact_conveyor_established)
		  reason = OA_UNPROVABLE_WEAKER_PROVENANCE;
		else if (oa_relational_code_implies (fact_code, rel_code)
			 && cg_offset_compatible_with_code (fact_offset, rel_code))
		  continue; /* Proven true: silently discharged.  */
		else
		  reason = OA_UNPROVABLE_RANGE_PARTIAL;
	      }
	  }

	  /* Bounds-proving demo (see .claude/plans/lazy-stirring-pearl.md):
	     no explicit linked fact -- but both sides' own independently-
	     tracked scalar ranges might still settle this numerically,
	     mirroring contracts.cc's own oa_env_check_relational_fact_1
	     range-vs-range fallback. Both sides are plain scalars here
	     (unlike the call analogue just below), so this pass's own
	     established_range/scalar_range_cache/ranger are already
	     enough -- no architectural barrier the way call-range facts
	     have (see cg_predicate_facts_walk's own comment on that).  */
	  {
	    cg_range_lite param_range, other_range;
	    if (cg_established_range_of (sub_param, established_range,
					   scalar_range_cache, ranger,
					   check_as_conveyor, check_as_symbolic,
					   &param_range, call)
		&& cg_established_range_of (sub_other, established_range,
					      scalar_range_cache, ranger,
					      check_as_conveyor, check_as_symbolic,
					      &other_range, call))
	      {
		enum oa_range_subsumption_result r
		  = cg_range_pair_relation (param_range, rel_code, other_range);
		if (r == OA_RANGE_SUBSUMED)
		  continue; /* Proven true: silently discharged.  */
		if (r == OA_RANGE_DISJOINT)
		  {
		    char established_buf[128];
		    cg_range_lite_text (param_range, established_buf,
					 sizeof (established_buf));
		    cg_obligation_error_at (gimple_location (call),
					     "argument %qE provably violates the "
					     "precondition of %qD", cg_display_value (sub_param), callee);
		    inform (gimple_location (call),
			    "%qE is established %s, but the precondition "
			    "requires it to be %s %qE", cg_display_value (sub_param),
			    established_buf, op_symbol_code (rel_code),
			    sub_other);
		    continue;
		  }
		if (reason == OA_UNPROVABLE_NO_FACT)
		  reason = OA_UNPROVABLE_RANGE_PARTIAL;
	      }
	  }

	  /* Bounds-proving demo: this exact shape ("param vs param") also
	     has its own, separate branch-derived explicit-fact check, but
	     it can only run from cg_predicate_facts_walk's own final pass
	     (see cg_check_call_relational_fact's own comment for why) -- if
	     that pass already reached a definitive verdict for this call,
	     don't also warn "cannot verify" here for what's already been
	     resolved.  */
	  if (call_relational_verdict->contains (call))
	    continue;

	  if (strict)
	    cg_obligation_error_at (gimple_location (call),
				     "cannot prove that %qE satisfies the "
				     "precondition of %qD", cg_display_value (sub_param), callee);
	  else
	    warning_at (gimple_location (call), 0,
			"cannot verify that %qE satisfies the "
			"precondition of %qD", cg_display_value (sub_param), callee);
	  if (const char *why = oa_unprovable_reason_text (reason))
	    inform (gimple_location (call), "%s", _(why));
	}

      /* The call analogue of the relational loop just above (e.g.
	 'pre<ctrl>(i < v.size ())').  RHS_CALLEE is compared directly by
	 identity (a FUNCTION_DECL, not a value to substitute); RHS_
	 RECEIVER, like REL_PARAM, is one of CALLEE's own PARM_DECLs,
	 substituted positionally the same way.

	 Unlike the plain relational loop just above, this shape's own
	 "explicit fact vs REL_CODE" check for a *branch*-derived fact does
	 not live here at all -- it runs from cg_predicate_facts_walk's own
	 final pass instead, against dominator-correct STATE.call_rel (see
	 cg_check_call_relational_fact's own comment for why: this loop has
	 no dominator-tree awareness of its own, and the flattened,
	 function-wide cache that would otherwise be the only thing
	 available here can hold two genuinely conflicting facts, from
	 different arms of the same branch, under the same key).

	 A *self-trust*-derived fact (the caller's own declared
	 precondition, e.g. GET_CHECKED's own 'pre<ctrl>(i < s.size ())' in
	 d4324-gimple-conveyor-call-relational-basic.C) is different: it is
	 never branch-dependent (true unconditionally, for the whole
	 function), so ESTABLISHED_CALL_REL alone -- with no flattened-
	 cache fallback (a fresh, empty map, so a genuinely branch-derived
	 fact is never picked up here by accident) -- is sound to consult
	 directly, and it is cg_predicate_facts_walk's *only* route to
	 seeing that fact at all: cg_seed_predicate_self_trust seeds
	 STATE's own .pred/.field/.call, never .rel/.call_rel.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rhs_receiver, rhs_callee;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_call (*conjuncts[i], &rel_param,
						  &rel_code, &rhs_receiver,
						  &rhs_callee,
						  /*allow_symbolic_accessor=*/
						    !require_conveyor))
	    continue;

	  unsigned param_argno, receiver_argno;
	  if (!cg_find_param_position (callee, rel_param, &param_argno)
	      || !cg_find_param_position (callee, rhs_receiver, &receiver_argno)
	      || param_argno >= gimple_call_num_args (call)
	      || receiver_argno >= gimple_call_num_args (call))
	    continue;

	  tree sub_param = cg_resolve_value_argument (call, param_argno);
	  tree sub_receiver = cg_resolve_value_argument (call, receiver_argno);

	  hash_map<tree, cg_call_rel_fact> empty_call_rel_cache;
	  hash_map<tree, cg_range_lite> empty_scalar_range_cache;

	  tree_code fact_code;
	  tree fact_rhs_receiver, fact_rhs_callee;
	  cg_range_lite fact_offset;
	  bool fact_conveyor_established;
	  /* Not ESTABLISHED_CALL_REL: a precondition's fact lasts only
	     until the receiver changes, so it is
	     cg_check_call_relational_fact's to consult, in the
	     dominator walk's state (cg_seed_predicate_self_trust).  */
	  hash_map<tree, cg_call_rel_fact> no_call_rel;
	  bool have_fact
	    = cg_get_call_relational (sub_param, no_call_rel,
				       empty_call_rel_cache, established_range,
				       empty_scalar_range_cache, ranger,
				       check_as_conveyor, check_as_symbolic,
				       &fact_code, &fact_rhs_receiver,
				       &fact_rhs_callee, &fact_offset,
				       &fact_conveyor_established);
	  bool same_accessor
	    = have_fact && fact_rhs_callee == rhs_callee
	      && (oa_strip_to_receiver_identity (fact_rhs_receiver)
		  == oa_strip_to_receiver_identity (sub_receiver));
	  if (same_accessor
	      && oa_relational_code_implies (fact_code, rel_code)
	      && cg_offset_compatible_with_code (fact_offset, rel_code)
	      && (!require_conveyor || fact_conveyor_established))
	    continue; /* Proven true: silently discharged.  */

	  /* Bounds-proving demo: this exact shape ("param vs call") also
	     has its own, separate range-vs-range fallback and its own
	     branch-derived explicit-fact check, but both can only run from
	     cg_predicate_facts_walk's own final pass (see cg_check_call_
	     range_relational's own comment, and cg_check_call_relational_
	     fact's own comment, for why) -- if that pass already reached a
	     definitive verdict for this call (a silent proof, or its own
	     hard "provably violates" error), don't also warn "cannot
	     verify" here for what's already been resolved one way or the
	     other.  */
	  if (call_relational_verdict->contains (call))
	    continue;

	  if (strict)
	    cg_obligation_error_at (gimple_location (call),
				     "cannot prove that %qE satisfies the "
				     "precondition of %qD", cg_display_value (sub_param), callee);
	  else
	    warning_at (gimple_location (call), 0,
			"cannot verify that %qE satisfies the "
			"precondition of %qD", cg_display_value (sub_param), callee);
	  /* NOTE: unlike contracts.cc's own oa_env_check_call_relational_
	     fact_1 (which can consult oa_env::unprovable_reason_hint_get to
	     enrich a bare "no fact" with why a branch-derived margin fact
	     failed to establish, via a hint oa_refine_single_comparison
	     records directly into the same, single, shared oa_env), this
	     function runs in a separate, later pass from cg_predicate_
	     facts_walk's own dominator-scoped fixed point (see this pass's
	     own two-phase structure, pass_contracts_gimple::execute) --
	     any such hint recorded into a per-block cg_dom_fact_state
	     during that earlier walk would already be out of scope by the
	     time this later, independent pass runs, unless cg_predicate_
	     facts_walk were extended to flatten and export it the same way
	     it already does for SCALAR_RANGE_CACHE_OUT. Not done here -- a
	     known, GIMPLE-specific asymmetry, left as a documented gap
	     rather than chased in this pass; NO_FACT is still strictly
	     more precise than nothing for every other case below.  */
	  oa_unprovable_reason reason;
	  int *hint = cg_unprovable_hints ? cg_unprovable_hints->get (sub_param)
					  : NULL;
	  if (!have_fact)
	    reason = hint ? (oa_unprovable_reason) *hint : OA_UNPROVABLE_NO_FACT;
	  else if (!same_accessor)
	    reason = OA_UNPROVABLE_WRONG_IDENTITY;
	  else if (require_conveyor && !fact_conveyor_established)
	    reason = OA_UNPROVABLE_WEAKER_PROVENANCE;
	  else
	    reason = OA_UNPROVABLE_RANGE_PARTIAL;
	  if (const char *why = oa_unprovable_reason_text (reason))
	    inform (gimple_location (call), "%s", _(why));
	}

      /* The call-vs-call analogue of the call-relational loop just
	 above (e.g. 'pre<ctrl>(v.size () < w.size ())'). Both receivers
	 are one of CALLEE's own PARM_DECLs, substituted positionally the
	 same way; both callees are compared directly by identity.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree lhs_receiver, lhs_callee, rhs_receiver, rhs_callee;
	  tree_code call_code;
	  if (!oa_match_call_against_call (*conjuncts[i], &lhs_receiver,
					     &lhs_callee, &call_code,
					     &rhs_receiver, &rhs_callee,
					     /*allow_symbolic_accessor=*/
					       !require_conveyor))
	    continue;

	  unsigned lhs_argno, rhs_argno;
	  if (!cg_find_param_position (callee, lhs_receiver, &lhs_argno)
	      || !cg_find_param_position (callee, rhs_receiver, &rhs_argno)
	      || lhs_argno >= gimple_call_num_args (call)
	      || rhs_argno >= gimple_call_num_args (call))
	    continue;

	  tree sub_lhs_receiver = cg_resolve_value_argument (call, lhs_argno);
	  tree sub_rhs_receiver = cg_resolve_value_argument (call, rhs_argno);

	  tree_code fact_code;
	  tree fact_rhs_receiver, fact_rhs_callee;
	  bool fact_conveyor_established;
	  if (cg_get_call_call_relational (sub_lhs_receiver, lhs_callee,
					     established_call_call_rel,
					     &fact_code, &fact_rhs_receiver,
					     &fact_rhs_callee,
					     &fact_conveyor_established)
	      && oa_relational_code_implies (fact_code, call_code)
	      && (!require_conveyor || fact_conveyor_established)
	      && fact_rhs_callee == rhs_callee
	      && (oa_strip_to_receiver_identity (fact_rhs_receiver)
		  == oa_strip_to_receiver_identity (sub_rhs_receiver)))
	    continue; /* Proven true: silently discharged.  */
	  /* By a branch: cg_check_call_relational_fact.  */
	  if (cg_proven_call_call_conjuncts
	      && cg_proven_call_call_conjuncts->contains ({call, *conjuncts[i]}))
	    continue;

	  if (strict)
	    cg_obligation_error_at (gimple_location (call),
				     "cannot prove that %qD called on %qE satisfies the "
				     "precondition of %qD", lhs_callee, sub_lhs_receiver,
				     callee);
	  else
	    warning_at (gimple_location (call), 0,
			"cannot verify that %qD called on %qE satisfies the "
			"precondition of %qD", lhs_callee, sub_lhs_receiver,
			callee);
	}

      /* Range obligations: same per-param grouping as
	 cg_seed_self_trust's own range handling, since one
	 precondition can constrain several distinct parameters,
	 each via more than one conjunct.  */
      auto_vec<tree> params;
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree param, const_val;
	  tree_code code;
	  if (oa_match_simple_comparison (*conjuncts[i], &param, &code,
					   &const_val)
	      && TREE_CODE (param) == PARM_DECL && !params.contains (param))
	    params.safe_push (param);
	}
      for (unsigned p = 0; p < params.length (); ++p)
	{
	  cg_range_lite required;
	  if (!cg_extract_conjunct_range (conjuncts, params[p], &required))
	    continue;

	  unsigned argno;
	  if (!cg_find_param_position (callee, params[p], &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = cg_resolve_value_argument (call, argno);

	  cg_range_lite established_r;
	  bool have_range
	    = cg_established_range_of (substituted, established_range,
					scalar_range_cache, ranger,
					check_as_conveyor,
					check_as_symbolic, &established_r,
					call);
	  if (have_range
	      && (!required.has_lo
		  || (established_r.has_lo && established_r.lo >= required.lo))
	      && (!required.has_hi
		  || (established_r.has_hi && established_r.hi <= required.hi)))
	    continue; /* Proven true: silently discharged.  */
	  if (have_range && cg_ranges_disjoint_p (established_r, required))
	    {
	      char have_text[64], need_text[64];
	      cg_obligation_error_at (gimple_location (call),
				       "argument %qE provably violates the precondition of %qD",
				       cg_display_value (substituted), callee);
	      inform (gimple_location (call),
		      "%qE is established %s, but the precondition requires "
		      "it %s", cg_display_value (substituted),
		      cg_range_lite_text (established_r, have_text,
					  sizeof have_text),
		      cg_range_lite_text (required, need_text,
					  sizeof need_text));
	      continue;
	    }

	  if (strict)
	    cg_obligation_error_at (gimple_location (call),
				     "cannot prove that %qE satisfies the precondition "
				     "of %qD", cg_display_argument (substituted, params[p]),
				     callee);
	  else
	    warning_at (gimple_location (call), 0,
			"cannot verify that %qE satisfies the precondition "
			"of %qD", cg_display_argument (substituted, params[p]),
			callee);
	  /* See oa_unprovable_reason's own comment: cg_established_range_of
	     tries several independent sources internally and collapses all
	     of their own failures into one bare "false" -- UNRESOLVED_
	     OPERAND is as specific as this consult can get without
	     threading a reason through that function and each of its own
	     sources individually (not done here, per .claude/plans/
	     lazy-stirring-pearl.md's own Phase 4 scope).  */
	  if (const char *why = oa_unprovable_reason_text
		(have_range ? OA_UNPROVABLE_RANGE_PARTIAL
			    : OA_UNPROVABLE_NO_FACT))
	    inform (gimple_location (call), "%s", _(why));
	}
    }

  /* D4324/P2680, ctor/dtor 'this' soundness correction: mirrors
     contracts.cc's own oa_handle_call_precondition_obligation, and this
     pass's own identical cg_seed_self_trust addition just above -- EVERY
     constructor/destructor's own receiver needs a REAL, unconditional
     is_object_address obligation at each call site, regardless of
     whether CALLEE has any contract specifier of its own for the loop
     above to have found.  Simpler here than the AST engine's own
     equivalent: by this pass's point (post-gimplification),
     AGGR_INIT_EXPR/TARGET_EXPR are long gone -- 'new T()', placement
     new, 'delete', an explicit destructor call, and ordinary automatic-
     duration construction/destruction are ALL just an ordinary
     GIMPLE_CALL to the cdtor with the receiver pointer as arg 0, so no
     separate AGGR_INIT_EXPR-receiver-correlation step is needed at all.
     REQUIRE_CONVEYOR is unconditionally true: this obligation has no
     CONTRACT of its own to derive a flavor/strictness from the way the
     loop above does, and is_object_address is fundamentally a conveyor-
     flavored notion (oa_contract_conveyor_active_p's own primary
     domain) -- matches contracts.cc's own is_object_address handling,
     which likewise never accepts a merely-symbolic-trusted fact here.
     Confirmed via direct testing: -fcontract-conveyor-proofs-gimple
     silently accepted an explicit destructor call / 'delete' / placement
     new, each on a wholly unproven pointer, before this addition (the
     same repro battery contracts.cc's own fix was verified against).  */
  /* A defaulted copy or move member's implicit is_object_address of its
     source (contracts.cc, oa_defaulted_copy_source_parm): mandatory, as
     an explicit conveyor precondition's is_object_address is.  */
  tree src_parm;
  unsigned src_argno;
  if ((flag_contract_analysis_engine != 0
       || flag_contract_conveyor_proofs_gimple)
      && oa_defaulted_copy_source_parm_public (callee, &src_parm)
      && cg_find_param_position (callee, src_parm, &src_argno)
      && src_argno < gimple_call_num_args (call))
    {
      tree substituted = cg_resolve_value_argument (call, src_argno);
      hash_set<tree> in_progress;
      oa_unprovable_reason reason = OA_UNPROVABLE_NONE;
      if (!cg_provable_object_address_p (substituted, established,
					  /*require_conveyor=*/true,
					  in_progress, &reason))
	{
	  cg_conveyor_error_at (gimple_location (call),
				"cannot prove %<is_object_address%> for %qE, "
				"required by the implicit precondition of %qD",
				cg_display_argument (substituted, src_parm),
				callee);
	  if (const char *why = oa_unprovable_reason_text (reason))
	    inform (gimple_location (call), "%s", _(why));
	}
    }

  /* Under -fcontract-analysis-engine=, gated as the AST engine gates it:
     on the proofs flags, not on the engine's own (always set).  */
  bool cdtor_receiver_check
    = (flag_contract_analysis_engine != 0
       ? cg_conveyor_proofs_p () || cg_symbolic_proofs_p ()
       : (flag_contract_conveyor_proofs_gimple
	  || flag_contract_symbolic_proofs_gimple));
  if (cdtor_receiver_check
      && gimple_call_num_args (call) >= 1
      && (DECL_CONSTRUCTOR_P (callee) || DECL_DESTRUCTOR_P (callee)))
    {
      tree receiver = gimple_call_arg (call, 0);
      hash_set<tree> in_progress;
      oa_unprovable_reason reason = OA_UNPROVABLE_NONE;
      if (!cg_provable_object_address_p (receiver, established,
					  /*require_conveyor=*/true,
					  in_progress, &reason))
	{
	  cg_obligation_error_at (gimple_location (call),
				   "cannot prove %<is_object_address%> for %qE, as "
				   "required by the precondition of %qD", receiver, callee);
	  if (const char *why = oa_unprovable_reason_text (reason))
	    inform (gimple_location (call), "%s", _(why));
	}
    }
}

/* Two persistent-object fact shapes, ported from the validated plugin
   prototype's own dominator-tree dataflow (see that file's own
   extensive top comment on predicate_dom_walker for the full
   rationale): named predicates ('is_opened(this)') and ptr->field
   ranges ('this->count in [0, N)'). Unlike the value-based facts
   above, these ARE one-way-trust-tagged (cg_pred_fact/cg_field_fact's
   own CONVEYOR_ESTABLISHED), matching contracts.cc's own
   oa_predicate_fact/oa_contract_field_range_fact -- both of those
   carry the tag in the real engine, unlike oa_range_fact (see this
   file's own top comment on why general ranges are the one exception).
   Self-trust/establish are gated on flavor-enabled exactly like
   cg_seed_self_trust/cg_check_call; invalidation is unconditional
   (a safety measure, not a trust concern, so it never depends on
   which -fcontract-*-proofs-gimple flags happen to be on).  */

struct cg_pred_fact { tree pred_fn; bool polarity; bool conveyor_established; };
struct cg_field_fact
{
  cg_range_lite range;
  bool conveyor_established;
  /* The field is nonzero, a fact a range can't state for a signed
     field: from a contract_assert's 'obj.field != 0'.  */
  bool nonzero = false;
  /* For a floating-point field, its range.  */
  bool has_float = false;
  /* For a pointer in memory: it holds an object address.  */
  bool object_address = false;
  frange float_range = frange ();
};

/* Whether A and B, two facts about the same location, are the same
   floating-point fact.  */

static bool
cg_field_fact_float_equal (const cg_field_fact &a, const cg_field_fact &b)
{
  return (a.has_float == b.has_float
	  && (!a.has_float || a.float_range == b.float_range));
}

/* cg_field_key_hash itself is defined earlier in this file (alongside
   cg_call_call_rel_fact), which needs it before this point.  */

struct cg_dom_fact_state
{
  hash_map<tree, cg_pred_fact> pred;
  hash_map<cg_field_key_hash, cg_field_fact> field;
  /* Stage 4d: the GIMPLE analogue of contracts.cc's own m_field_alias_
     target -- "the tracked pointer identity this pointer/reference-
     typed field slot currently holds", same (identity, FIELD_DECL) key
     shape as .field above, reusing the same cg_field_key_hash.  */
  hash_map<cg_field_key_hash, tree> field_alias;
  /* The GIMPLE analogue of contracts.cc's own m_contract_call_range_map
     -- a call to a DECL_DECLARED_CONVEYOR_P accessor used in a
     comparison (e.g. 'i < v.size ()'), keyed by (receiver identity,
     FUNCTION_DECL) rather than (identity, FIELD_DECL). cg_field_fact/
     cg_field_key_hash are already fully generic over any two tree
     pointers/a plain range, so reused as-is, exactly as the AST engine
     reuses oa_contract_field_range_fact/oa_field_key_hash for the same
     reason.  */
  hash_map<cg_field_key_hash, cg_field_fact> call;
  /* D4324 Commit 3: branch-derived relational facts (param-vs-param,
     param-vs-call), keyed on a plain SSA name -- the exact same key
     type established_rel/established_call_rel (the *simple*, self-
     trust-only pass's own maps) already use, so a fixed-point-converged
     entry here can later be flattened into a cache of that identical
     type (see cg_predicate_facts_walk's own comment on why that's
     sound for these two shapes specifically, unlike call-vs-call, which
     deliberately has no counterpart field here at all).  */
  hash_map<tree, cg_rel_fact> rel;
  hash_map<tree, cg_call_rel_fact> call_rel;
  /* 'v.size () < w.size ()' from a branch: keyed on V's identity and
     the accessor, also recorded the other way round under W's.  */
  hash_map<cg_field_key_hash, cg_call_call_rel_fact> call_call;
};

/* An SSA_NAME's own identity is itself; '&decl' resolves to DECL
   directly; a bare VAR_DECL/PARM_DECL used directly is its own
   identity too (closing a pre-existing asymmetry with the AST engine's
   own oa_object_identity_decl, which already accepts this shape) --
   see the plugin's own identical gimple_object_identity for the full
   rationale (unifying plain-object and pointer receivers).

   Also looks through VAL's own implicit conversion operator
   (cg_resolve_conversion_receiver) first -- always safe here,
   including for establish/invalidate callers, not just consult ones:
   a conversion operator always refers to the *same* underlying object,
   unlike a by-value copy (which IS only safe to look through at
   consult call sites -- see cg_consult_persistent_facts's own use of
   cg_resolve_call_argument instead of a bare gimple_call_arg).  */

/* Pointer-aliasing fix (see contracts.cc's own oa_env::alias_find, the
   AST-engine analogue of this same fix, for the full soundness
   rationale): a bare pointer-typed SSA_NAME used to be its own
   identity unconditionally, so 'q_2 = p_1;' left q_2 and p_1 as two
   different identities even though they hold the same value -- the
   same "Rule 2 invalidates the wrong decl" bug the AST engine had.
   Fixed by chasing VAL's own def-stmt through a plain copy/conversion
   or a PHI, mirroring cg_provable_object_address_p's own identical
   SSA_NAME/GIMPLE_PHI chasing just above (same IN_PROGRESS cycle
   guard for a loop-carried PHI). Unlike the AST engine, this needs no
   separate explicit merge step at all across a conditional branch's
   own join: SSA_NAMEs are immutable, so resolution is just recomputed
   fresh from each one's own single definition every time it's needed,
   and "a PHI's own identity only if every incoming argument agrees"
   is exactly the sound, AND-across-arms answer a branch-only alias
   needs -- falls back to VAL itself, not NULL_TREE, whenever the
   chase fails or arguments disagree, preserving this function's own
   prior behavior for every already-working, non-aliasing case.  */

static tree
cg_gimple_object_identity_1 (tree val, hash_set<tree> &in_progress)
{
  if (val == NULL_TREE)
    return NULL_TREE;
  val = cg_resolve_conversion_receiver (val);
  if (TREE_CODE (val) == ADDR_EXPR)
    {
      tree op = TREE_OPERAND (val, 0);
      if (DECL_P (op) && (VAR_P (op) || TREE_CODE (op) == PARM_DECL))
	return op;
      return NULL_TREE;
    }
  if (TREE_CODE (val) == SSA_NAME && POINTER_TYPE_P (TREE_TYPE (val)))
    {
      if (in_progress.contains (val))
	return val;
      in_progress.add (val);
      gimple *def = SSA_NAME_DEF_STMT (val);
      if (def && gimple_code (def) == GIMPLE_PHI)
	{
	  tree shared = NULL_TREE;
	  unsigned n = gimple_phi_num_args (def);
	  for (unsigned i = 0; i < n; ++i)
	    {
	      tree arg_identity
		= cg_gimple_object_identity_1 (gimple_phi_arg_def (def, i),
						in_progress);
	      if (!arg_identity || (shared && shared != arg_identity))
		return val;
	      shared = arg_identity;
	    }
	  return shared ? shared : val;
	}
      if (def && is_gimple_assign (def))
	{
	  enum tree_code code = gimple_assign_rhs_code (def);
	  if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	    {
	      tree resolved
		= cg_gimple_object_identity_1 (gimple_assign_rhs1 (def),
						in_progress);
	      return resolved ? resolved : val;
	    }
	}
      if (tree arg = cg_std_identity_call_arg (def))
	{
	  tree resolved = cg_gimple_object_identity_1 (arg, in_progress);
	  return resolved ? resolved : val;
	}
      return val;
    }
  if (VAR_P (val) || TREE_CODE (val) == PARM_DECL)
    return val;
  return NULL_TREE;
}

static tree
cg_gimple_object_identity (tree val)
{
  hash_set<tree> in_progress;
  return cg_gimple_object_identity_1 (val, in_progress);
}

/* The SSA value scalar VAL is a copy of ('r_5 = _4'), for a fact
   about the value itself, e.g. a predicate a call's postcondition
   established about the value it returned.  */

static tree
cg_scalar_value_origin (tree val)
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

/* The array named by array slot REF ('arr[k]'): an array variable
   itself whose address is never taken -- not one reached through a
   pointer, nor one a pointer could reach, whose slots a store through
   that pointer could change unseen.  */

static tree
cg_array_slot_base_identity (tree ref)
{
  tree base = TREE_OPERAND (ref, 0);
  if (VAR_P (base) && !TREE_ADDRESSABLE (base)
      && TREE_CODE (TREE_TYPE (base)) == ARRAY_TYPE)
    return base;
  return NULL_TREE;
}

/* Does pointer ARG, of object IDENTITY (cg_gimple_object_identity's),
   point to an object no fact can name: one loaded from memory or
   returned by a call, or either of two objects (a PHI)?  The address of
   a decl, including a return slot being constructed, and a parameter's
   incoming value are named.  */

static bool
cg_unnamed_pointer_identity_p (tree arg, tree identity)
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
      return (TREE_CODE (rhs) != ADDR_EXPR
	      && (handled_component_p (rhs) || TREE_CODE (rhs) == MEM_REF
		  || VAR_P (rhs) || TREE_CODE (rhs) == PARM_DECL));
    }
  return false;
}

/* The object IDENTITY may have changed: drop every fact in STATE that
   compares against the result of an accessor called on it.  */

static void
cg_call_rel_invalidate_receiver (cg_dom_fact_state &state, tree identity)
{
  auto_vec<tree> to_remove;
  for (auto it : state.call_rel)
    if (cg_gimple_object_identity (it.second.rhs_receiver) == identity)
      to_remove.safe_push (it.first);
  for (unsigned i = 0; i < to_remove.length (); ++i)
    state.call_rel.remove (to_remove[i]);
  auto_vec<std::pair<tree, tree>> cc_remove;
  for (auto it : state.call_call)
    if (it.first.first == identity
	|| cg_gimple_object_identity (it.second.rhs_receiver) == identity)
      cc_remove.safe_push (it.first);
  for (auto k : cc_remove)
    state.call_call.remove (k);
}

/* Stage 4c: the GIMPLE analogue of contracts.cc's own Rule 1
   COMPONENT_REF write-detection block -- confirmed via tree-shape
   investigation (-fdump-tree-ssa-raw) that a direct field write is
   COMPONENT_REF (MEM_REF (base, byte_offset), field) for a pointer
   base at this exact pass point (MEM_REF stands in for INDIRECT_REF,
   which doesn't exist at the GIMPLE level at all), or plain COMPONENT_
   REF (base, field) directly for a non-pointer, in-place object base.

   LHS may also be a GIMPLE_CALL's own LHS directly, not just a
   GIMPLE_ASSIGN's: a class/aggregate-typed field written from a call's
   own return value under mandatory copy elision ('p->b = makebig();')
   is a GIMPLE_CALL whose own LHS *is* the COMPONENT_REF (confirmed via
   the same tree-shape investigation, 'gimple_call <makebig, p_2(D)->
   b>') -- neither gimple_assign_single_p (requires is_gimple_assign,
   false for a GIMPLE_CALL) nor the call-args-only invalidation this
   file already had (which only ever inspects gimple_call_arg, never
   gimple_call_lhs) could see this shape, found and fixed by the same
   design review that caught the dom-walker's own confluence bug. A
   scalar-typed field write from a call ('p->f = foo();') does not have
   this problem: GCC routes it through a temporary and a separate,
   ordinary GIMPLE_ASSIGN, confirmed via the same dump.

   A non-zero MEM_REF offset (a nested field access folded into one
   node, or an array-slot access -- array indexing has no syntactic
   trace left at this pass point at all, see this file's own top-level
   scope notes for why array-slot aliasing is GIMPLE-engine out of
   scope entirely) is deliberately left unresolved, correctly declining
   rather than mis-resolving.  */

static tree cg_field_slot_identity (tree val, cg_dom_fact_state &state);
static void cg_invalidate_parameter_alias_group
  (tree identity, function *fun, hash_map<cg_field_key_hash, tree> &field_object_cache,
   cg_dom_fact_state &state);
static tree cg_field_object_identity (tree val,
				       hash_map<cg_field_key_hash, tree> &cache);

/* Stage 5: mirrors contracts.cc's own oa_env::field_object_predicate_
   invalidate_all -- sweeps every cached '&BASE->field' synthetic key
   and drops whatever state.pred fact was recorded under it, the same
   whole-object granularity every other sweep in this file already
   uses.  */

static void
cg_field_object_predicate_invalidate_all (tree base,
					   hash_map<cg_field_key_hash, tree> &cache,
					   cg_dom_fact_state &state)
{
  for (auto it : cache)
    if (it.first.first == base)
      state.pred.remove (it.second);
}

/* The key under which cg_dom_fact_state::field holds the facts about
   the value of scalar DECL itself, for a scalar in memory.  */

static inline std::pair<tree, tree>
cg_scalar_memory_fact_key (tree decl)
{
  return { decl, NULL_TREE };
}

/* A store to LHS ends the facts about a scalar in memory's own value
   (cg_scalar_memory_fact_key).  */

static tree cg_contract_check_call_p (gcall *, tree *, tree *, tree *);

/* The fact key of the object '*P' designates, for a scalar a reference
   parameter refers to: {P's identity, no field}, like a scalar in
   memory's (cg_scalar_memory_fact_key).  */

static bool
cg_referent_key (tree ref, std::pair<tree, tree> *key)
{
  if (TREE_CODE (ref) != MEM_REF || !integer_zerop (TREE_OPERAND (ref, 1))
      || TREE_CODE (TREE_OPERAND (ref, 0)) != SSA_NAME)
    return false;
  tree identity = cg_gimple_object_identity (TREE_OPERAND (ref, 0));
  if (!identity || TREE_CODE (identity) != SSA_NAME)
    return false;
  *key = { identity, NULL_TREE };
  return true;
}

/* End every fact about a referent (cg_referent_key): a store through a
   pointer, or a call, may change any of them through an alias.  */

static void
cg_end_referent_facts (cg_dom_fact_state &state)
{
  auto_vec<std::pair<tree, tree>> to_remove;
  for (auto it : state.field)
    if (it.first.second == NULL_TREE && TREE_CODE (it.first.first) == SSA_NAME)
      to_remove.safe_push (it.first);
  for (auto k : to_remove)
    state.field.remove (k);
}

static void
cg_process_scalar_write (tree lhs, cg_dom_fact_state &state)
{
  if (VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL)
    state.field.remove (cg_scalar_memory_fact_key (lhs));
  /* A store through a pointer ('*x = ...', 'p->f = ...').  */
  else if (TREE_CODE (get_base_address (lhs)) == MEM_REF)
    cg_end_referent_facts (state);
}

static void
cg_process_field_write (tree lhs, tree rhs, function *fun,
			 hash_map<cg_field_key_hash, tree> &field_object_cache,
			 cg_dom_fact_state &state)
{
  /* A pointer stored to an array slot 'arr[k]': what the slot now
     aliases, keyed {array, K} in state.field_alias -- K a (shared)
     INTEGER_CST, never confused with a FIELD_DECL.  A store at an
     unknown index may have replaced any slot.  */
  if (TREE_CODE (lhs) == ARRAY_REF)
    {
      /* A named predicate about the array could depend on any element,
	 as on any field of an object -- whether or not its slots are
	 tracked (an array whose address is taken isn't).  */
      if (tree arr = get_base_address (lhs))
	if (DECL_P (arr) || TREE_CODE (arr) == MEM_REF)
	  if (tree whole = cg_gimple_object_identity
			     (TREE_CODE (arr) == MEM_REF
			      ? TREE_OPERAND (arr, 0) : arr))
	    state.pred.remove (whole);
      tree arr_identity = cg_array_slot_base_identity (lhs);
      if (!arr_identity)
	return;
      tree index = TREE_OPERAND (lhs, 1);
      if (TREE_CODE (index) != INTEGER_CST)
	{
	  auto_vec<std::pair<tree, tree>> to_remove;
	  for (auto it : state.field_alias)
	    if (it.first.first == arr_identity
		&& TREE_CODE (it.first.second) == INTEGER_CST)
	      to_remove.safe_push (it.first);
	  for (auto k : to_remove)
	    state.field_alias.remove (k);
	  return;
	}
      if (!POINTER_TYPE_P (TREE_TYPE (lhs)))
	return;
      tree rhs_identity = rhs ? cg_field_slot_identity (rhs, state) : NULL_TREE;
      if (!rhs_identity && rhs)
	rhs_identity = cg_gimple_object_identity (rhs);
      if (rhs_identity)
	state.field_alias.put ({arr_identity, index}, rhs_identity);
      else
	state.field_alias.remove ({arr_identity, index});
      return;
    }
  /* The whole object replaced ('f = io_facility ();', '*p = x'):
     nothing known about it holds.  */
  if (AGGREGATE_TYPE_P (TREE_TYPE (lhs))
      && (VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL
	  || (TREE_CODE (lhs) == MEM_REF
	      && integer_zerop (TREE_OPERAND (lhs, 1)))))
    {
      tree identity = cg_gimple_object_identity (TREE_CODE (lhs) == MEM_REF
						 ? TREE_OPERAND (lhs, 0)
						 : lhs);
      if (!identity)
	return;
      state.pred.remove (identity);
      auto_vec<std::pair<tree, tree>> drop;
      for (auto it : state.field)
	if (it.first.first == identity)
	  drop.safe_push (it.first);
      for (auto k : drop)
	state.field.remove (k);
      drop.truncate (0);
      for (auto it : state.field_alias)
	if (it.first.first == identity)
	  drop.safe_push (it.first);
      for (auto k : drop)
	state.field_alias.remove (k);
      cg_call_rel_invalidate_receiver (state, identity);
      for (auto it : field_object_cache)
	if (it.first.first == identity)
	  state.pred.remove (it.second);
      return;
    }
  if (TREE_CODE (lhs) != COMPONENT_REF)
    return;
  tree field = TREE_OPERAND (lhs, 1);
  tree obj = TREE_OPERAND (lhs, 0);
  tree obj_expr = TREE_CODE (obj) == MEM_REF && integer_zerop (TREE_OPERAND (obj, 1))
    ? TREE_OPERAND (obj, 0) : obj;
  tree identity = cg_gimple_object_identity (obj_expr);
  if (!identity)
    return;
  /* Whole-object granularity for the predicate map, narrower per-field
     granularity for the range map -- mirrors contracts.cc's own Rule 1
     COMPONENT_REF block exactly, including this plan's own Stage 4a
     fix (a named predicate is opaque, could depend on any field, so
     ANY field write invalidates it wholesale).  */
  state.pred.remove (identity);
  state.field.remove ({identity, field});
  cg_call_rel_invalidate_receiver (state, identity);
  /* Stage 5: this replaces the FIELD sub-object of IDENTITY directly,
     so whatever predicate fact was tracked about '&identity->field'
     itself must be dropped too -- narrower single-slot form, mirroring
     contracts.cc's own field-write branch exactly (unlike a whole-
     object reassignment, which has no GIMPLE analogue reaching this
     function at all: cg_process_field_write only ever sees a
     COMPONENT_REF LHS, the narrower shape).  */
  tree *fo_key = field_object_cache.get ({identity, field});
  if (fo_key)
    state.pred.remove (*fo_key);
  /* Stage 4e: a parameter-alias-group sweep for this same whole-object
     effect only -- explicitly NOT extended to sweep siblings' own
     field-slot facts (field_alias, just below), matching the AST
     engine's own already-disclosed scope cut for exact parity between
     the two engines' residual gaps.  */
  cg_invalidate_parameter_alias_group (identity, fun, field_object_cache, state);
  /* Stage 4d: FIELD is a pointer/reference-typed slot -- record what
     it now aliases (RHS resolved via either resolver, so a field-to-
     field copy works too), or drop any stale alias if RHS doesn't
     resolve to anything recognizable. Gated on FIELD's own type,
     mirroring contracts.cc's own field-type gate -- a scalar field
     write must not populate this map at all. RHS is NULL_TREE for the
     aggregate-return-elision GIMPLE_CALL shape above, which can never
     itself be a pointer/reference value anyway (elision only ever
     applies to class-typed returns), so this gate already excludes
     that shape without any special-casing needed here.  */
  if (POINTER_TYPE_P (TREE_TYPE (field)) || TREE_CODE (TREE_TYPE (field)) == REFERENCE_TYPE)
    {
      /* cg_field_slot_identity must be tried FIRST, not as a fallback:
	 unlike the AST engine's own oa_object_identity_decl (which
	 genuinely declines, returning false, for anything it doesn't
	 recognize), cg_gimple_object_identity always "succeeds" for any
	 pointer/reference-typed SSA value, falling back to the value
	 itself when it can't chase any further -- for RHS being the SSA
	 temporary a field load was materialized into ('_1 = h2->ptr;'
	 before 'h->ptr = _1;'), that fallback returns '_1' itself
	 (never established as anything, since it's a fresh temp), which
	 would silently pre-empt ever trying the more specific resolver
	 at all if tried second. Found via direct testing: the most
	 basic field-alias repro produced no diagnostic at all until this
	 ordering was corrected.  */
      tree rhs_identity = rhs ? cg_field_slot_identity (rhs, state) : NULL_TREE;
      if (!rhs_identity && rhs)
	rhs_identity = cg_gimple_object_identity (rhs);
      if (rhs_identity)
	state.field_alias.put ({identity, field}, rhs_identity);
      else
	state.field_alias.remove ({identity, field});
    }
}

/* Stage 4d: the GIMPLE analogue of contracts.cc's own oa_field_slot_
   identity -- a new, separate resolver, never folded into cg_gimple_
   object_identity itself (that function's own true/false-via-NULL
   return is not used as a control-flow discriminator anywhere the way
   contracts.cc's oa_object_identity_decl's is, but keeping the two
   resolvers separate mirrors the AST engine's own design and keeps
   this new map's own read side textually next to its own write side
   above). Recognizes a COMPONENT_REF (with the same MEM_REF zero-
   offset unwrap as cg_process_field_write above) whose field is
   pointer/reference typed, resolves the base via cg_gimple_object_
   identity, and looks up state.field_alias.  */

static tree
cg_field_slot_identity (tree val, cg_dom_fact_state &state)
{
  /* Unlike the AST engine (where 'h->ptr' can appear directly as a
     call argument expression), a GIMPLE call argument must itself be
     an SSA value -- confirmed via a raw dump that 'mutate (h->ptr);'
     lowers to '_1 = h_3(D)->ptr; mutate (_1);', so VAL here is the
     temporary '_1', not the COMPONENT_REF itself. Chase through VAL's
     own def-stmt once (a plain load, never a PHI -- a load out of
     memory has no "which arm" to merge, so no cycle-guard is needed
     the way cg_gimple_object_identity_1's own PHI-chasing needs one)
     to reach the actual COMPONENT_REF, if that's what defined it.  */
  if (TREE_CODE (val) == SSA_NAME)
    {
      gimple *def = SSA_NAME_DEF_STMT (val);
      if (!def || !gimple_assign_single_p (def))
	return NULL_TREE;
      val = gimple_assign_rhs1 (def);
    }
  /* A pointer loaded from array slot 'arr[k]' (cg_process_field_write).  */
  if (TREE_CODE (val) == ARRAY_REF
      && TREE_CODE (TREE_OPERAND (val, 1)) == INTEGER_CST
      && POINTER_TYPE_P (TREE_TYPE (val)))
    {
      tree arr_identity = cg_array_slot_base_identity (val);
      if (!arr_identity)
	return NULL_TREE;
      tree *target = state.field_alias.get ({arr_identity,
					     TREE_OPERAND (val, 1)});
      return target ? *target : NULL_TREE;
    }
  if (TREE_CODE (val) != COMPONENT_REF)
    return NULL_TREE;
  tree field = TREE_OPERAND (val, 1);
  if (!(POINTER_TYPE_P (TREE_TYPE (field)) || TREE_CODE (TREE_TYPE (field)) == REFERENCE_TYPE))
    return NULL_TREE;
  tree obj = TREE_OPERAND (val, 0);
  tree obj_expr = TREE_CODE (obj) == MEM_REF && integer_zerop (TREE_OPERAND (obj, 1))
    ? TREE_OPERAND (obj, 0) : obj;
  tree base_identity = cg_gimple_object_identity (obj_expr);
  if (!base_identity)
    return NULL_TREE;
  tree *target = state.field_alias.get ({base_identity, field});
  return target ? *target : NULL_TREE;
}

/* Stage 5: the GIMPLE analogue of contracts.cc's own oa_field_object_
   identity -- '&base->field' (of any type, most usefully a non-
   pointer, embedded sub-object) names a fixed, permanent sub-object,
   resolved to a synthesized, stable placeholder tree per (base_
   identity, FIELD_DECL) pair, cached in CACHE and plugged into the
   *existing* state.pred map exactly like the AST engine plugs it into
   m_predicate_fact_map -- no new cg_dom_fact_state member needed.

   Re-verified directly (not assumed to carry over from the AST engine
   unchanged) that a raw dump of 'mutate (&h->f);' shows the call
   argument always lowers through an SSA temporary first ('_1 =
   &h_2(D)->f; mutate (_1);'), the identical shape cg_field_slot_
   identity's own SSA-chase already needed for 'h->ptr' -- so the same
   chase is required here too, before the ADDR_EXPR check.

   Unlike the AST side's oa_env::field_object_identity_key, CACHE here
   needs no shared-vs-per-branch-copy design question at all:
   cg_predicate_facts_walk's own fixed-point walk has no forking oa_env
   analogue to begin with (state.* is threaded by reference throughout
   one single, function-scoped walk), so a single, ordinary local
   hash_map declared once in cg_predicate_facts_walk itself and passed
   by reference wherever needed is already exactly as "shared" as the
   AST side's pointer-based design has to work to be.  */

static tree
cg_field_object_identity (tree val, hash_map<cg_field_key_hash, tree> &cache)
{
  if (TREE_CODE (val) == SSA_NAME)
    {
      gimple *def = SSA_NAME_DEF_STMT (val);
      if (!def || !gimple_assign_single_p (def))
	return NULL_TREE;
      val = gimple_assign_rhs1 (def);
    }
  if (TREE_CODE (val) != ADDR_EXPR)
    return NULL_TREE;
  tree comp = TREE_OPERAND (val, 0);
  if (TREE_CODE (comp) != COMPONENT_REF)
    return NULL_TREE;
  tree field = TREE_OPERAND (comp, 1);
  if (TREE_CODE (field) != FIELD_DECL)
    return NULL_TREE;
  tree obj = TREE_OPERAND (comp, 0);
  tree obj_expr = TREE_CODE (obj) == MEM_REF && integer_zerop (TREE_OPERAND (obj, 1))
    ? TREE_OPERAND (obj, 0) : obj;
  tree base_identity = cg_gimple_object_identity (obj_expr);
  if (!base_identity)
    return NULL_TREE;
  tree *existing = cache.get ({base_identity, field});
  if (existing)
    return *existing;
  tree key = build_decl (UNKNOWN_LOCATION, VAR_DECL, NULL_TREE, ptr_type_node);
  cache.put ({base_identity, field}, key);
  return key;
}

/* A ptr->field range conjunct group, exactly like contracts.cc's own
   oa_symbolic_field_group, built from the exported oa_match_field_
   range_comparison/oa_strip_symbolic_ptr_expr_public primitives
   directly (see the plugin's own identical collect_field_range_groups
   for why: contracts.cc's own PRECONDITION_P/oa_contract_fact_
   tracking_active_p-gated iteration was found unreliable at GIMPLE-pass
   time, the same reliability gap Section 10 of ~/gimple-contract-
   analysis.md fixed for flavor checks specifically).  */

struct cg_field_group_lite { tree field; tree ptr_expr; cg_range_lite range; };

static void
cg_collect_field_range_groups (tree cond, vec<cg_field_group_lite> *out)
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

      cg_field_group_lite *found = NULL;
      for (unsigned j = 0; j < out->length () && !found; ++j)
	if ((*out)[j].field == field && (*out)[j].ptr_expr == ptr_expr)
	  found = &(*out)[j];
      if (!found)
	{
	  cg_field_group_lite g;
	  g.field = field;
	  g.ptr_expr = ptr_expr;
	  out->safe_push (g);
	  found = &out->last ();
	}
      cg_tighten_range_bound (found->range, code, wi::to_widest (const_val));
    }
}

/* If ARG, an is_object_address argument in a contract condition, is a
   pointer field of the object a parameter or local designates
   ('this->_M_this', 'p->next', 'obj.ptr'), return that FIELD_DECL and set
   *BASE to the parameter or local.  */

static tree
cg_object_address_field (tree arg, tree *base)
{
  arg = STRIP_ANY_LOCATION_WRAPPER (arg);
  while (TREE_CODE (arg) == NON_LVALUE_EXPR || CONVERT_EXPR_P (arg)
	 || TREE_CODE (arg) == VIEW_CONVERT_EXPR)
    arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
  /* A lambda's capture ('this', or '&r' for a capture by reference): the
     closure's field holding the captured pointer or reference, which
     each use of the capture loads anew.  */
  if (TREE_CODE (arg) == ADDR_EXPR || REFERENCE_REF_P (arg))
    {
      tree inner = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
      if (REFERENCE_REF_P (inner))
	inner = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (inner, 0));
      if (is_capture_proxy (inner) && TYPE_REF_P (TREE_TYPE (inner)))
	arg = inner;
    }
  if (is_capture_proxy (arg))
    {
      arg = DECL_VALUE_EXPR (arg);
      if (REFERENCE_REF_P (arg))
	arg = TREE_OPERAND (arg, 0);
    }
  if (TREE_CODE (arg) != COMPONENT_REF
      || !POINTER_TYPE_P (TREE_TYPE (arg))
      || TREE_CODE (TREE_OPERAND (arg, 1)) != FIELD_DECL)
    return NULL_TREE;
  tree obj = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
  if (TREE_CODE (obj) == INDIRECT_REF)
    obj = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (obj, 0));
  while (TREE_CODE (obj) == NON_LVALUE_EXPR || CONVERT_EXPR_P (obj))
    obj = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (obj, 0));
  if (!VAR_P (obj) && TREE_CODE (obj) != PARM_DECL)
    return NULL_TREE;
  *base = obj;
  return TREE_OPERAND (arg, 1);
}

/* The floating-point sibling of cg_collect_field_range_groups: each
   'ptr->field CODE C' conjunct of COND with C a REAL_CST, grouped by
   (pointer, field) and intersected.  PTR_EXPR is a PARM_DECL, or with
   ALLOW_VAR also a VAR_DECL.  */

struct cg_float_field_group { tree field; tree ptr_expr; frange range; };

static void
cg_collect_field_float_groups (tree cond, vec<cg_float_field_group> *out,
			       bool allow_var = false)
{
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  /* 'std::isfinite (p->f)': as 'p->f >= -max && p->f <= max'.  */
  auto_vec<tree> finite_bounds;
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree lo, hi;
      if (oa_isfinite_bounds_public (*conjuncts[i], &lo, &hi))
	{
	  finite_bounds.safe_push (lo);
	  finite_bounds.safe_push (hi);
	}
    }
  for (tree &b : finite_bounds)
    conjuncts.safe_push (&b);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree field, ptr_expr, const_val;
      tree_code code;
      if (!oa_match_field_range_comparison (*conjuncts[i], &field, &ptr_expr,
					     &code, &const_val)
	  || TREE_CODE (const_val) != REAL_CST
	  || !SCALAR_FLOAT_TYPE_P (TREE_TYPE (field)))
	continue;
      ptr_expr = oa_strip_symbolic_ptr_expr_public (ptr_expr);
      if (TREE_CODE (ptr_expr) != PARM_DECL
	  && !(allow_var && VAR_P (ptr_expr)))
	continue;
      frange r;
      if (!cg_frange_for_comparison (TREE_TYPE (field), code, const_val, &r))
	continue;
      cg_float_field_group *found = NULL;
      for (unsigned j = 0; j < out->length () && !found; ++j)
	if ((*out)[j].field == field && (*out)[j].ptr_expr == ptr_expr)
	  found = &(*out)[j];
      if (found)
	found->range.intersect (r);
      else
	out->safe_push ({ field, ptr_expr, r });
    }
}

/* Add floating-point range R to the fact KEY has in STATE.  */

static void
cg_add_float_field_fact (cg_dom_fact_state &state,
			 std::pair<tree, tree> key, const frange &r,
			 bool conveyor_established)
{
  cg_field_fact *existing = state.field.get (key);
  cg_field_fact fact = existing ? *existing
		       : cg_field_fact { {}, conveyor_established };
  if (fact.has_float)
    fact.float_range.intersect (r);
  else
    {
      fact.has_float = true;
      fact.float_range = r;
    }
  state.field.put (key, fact);
}


/* The call-range analogue of cg_field_group_lite/cg_collect_field_
   range_groups immediately above, built from the exported oa_match_
   call_range_comparison primitive -- a call to a DECL_DECLARED_
   CONVEYOR_P accessor (e.g. 'i < v.size ()') rather than a ptr->field
   access.  RECEIVER_EXPR, like PTR_EXPR above, is only recognized when
   it's already a bare PARM_DECL (including 'this') -- the shape needed
   to resolve a default SSA def / substitute a call argument by
   position; anything else is out of scope for this shape, same
   restriction the AST engine's own establishment/consult sites apply.  */

struct cg_call_group_lite { tree callee; tree receiver_expr; cg_range_lite range; };

static void
cg_collect_call_range_groups (tree cond, vec<cg_call_group_lite> *out,
			       bool allow_symbolic_accessor)
{
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree receiver_expr, callee, const_val;
      tree_code code;
      if (!oa_match_call_range_comparison (*conjuncts[i], &receiver_expr,
					    &callee, &code, &const_val,
					    allow_symbolic_accessor)
	  || TREE_CODE (const_val) != INTEGER_CST)
	continue;
      receiver_expr = oa_strip_symbolic_ptr_expr_public (receiver_expr);
      if (TREE_CODE (receiver_expr) != PARM_DECL)
	continue;

      cg_call_group_lite *found = NULL;
      for (unsigned j = 0; j < out->length () && !found; ++j)
	if ((*out)[j].callee == callee
	    && (*out)[j].receiver_expr == receiver_expr)
	  found = &(*out)[j];
      if (!found)
	{
	  cg_call_group_lite g;
	  g.callee = callee;
	  g.receiver_expr = receiver_expr;
	  out->safe_push (g);
	  found = &out->last ();
	}
      cg_tighten_range_bound (found->range, code, wi::to_widest (const_val));
    }
}

/* Bounds-proving demo (see .claude/plans/lazy-stirring-pearl.md, Part 3):
   the parametric analogue of cg_collect_call_range_groups above, for a
   postcondition's own call-range conjunct whose "other side" is another
   parameter of the postcondition-owning function (e.g. 'post<>(size ()
   == n)'), not a literal -- mirrors contracts.cc's own oa_collect_
   contract_call_ranges_parametric. CALLEE/CALL let each such conjunct's
   own parameter be positionally substituted through to this specific
   call site's own argument (cg_find_param_position/gimple_call_arg, the
   same positional-substitution convention cg_establish_persistent_facts_
   for_call's own callers already use for RECEIVER_EXPR two lines below),
   then resolved to a range via cg_established_range_of. Deliberately
   passes empty ESTABLISHED_RANGE/SCALAR_RANGE_CACHE maps and a NULL
   ranger: this walk (cg_predicate_facts_walk) has none of its own to
   offer here (unlike the *other*, simple linear pass), so this only ever
   resolves a substituted argument that's already an exact literal at this
   call site (cg_established_range_of's own first, no-lookup-needed case)
   -- e.g. 'v.resize (5)' -- not a fully general tracked-variable argument,
   which would need a real ranger threaded in, separate, not-yet-needed
   work. Folds into the SAME OUT groups cg_collect_call_range_groups
   itself populates (the caller runs both, into one vector), via
   cg_tighten_range_bound_from_range instead of cg_tighten_range_bound.  */

static void
cg_collect_call_range_groups_parametric (tree cond, tree callee, gcall *call,
					   vec<cg_call_group_lite> *out)
{
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree receiver_expr, call_callee, other;
      tree_code code;
      if (!oa_match_call_range_comparison (*conjuncts[i], &receiver_expr,
					     &call_callee, &code, &other,
					     /*allow_symbolic_accessor=*/false)
	  || TREE_CODE (other) != PARM_DECL)
	continue;

      unsigned argno;
      if (!cg_find_param_position (callee, other, &argno)
	  || argno >= gimple_call_num_args (call))
	continue;
      tree substituted_other = gimple_call_arg (call, argno);

      hash_map<tree, cg_range_lite> empty_established_range;
      hash_map<tree, cg_range_lite> empty_scalar_range_cache;
      cg_range_lite other_range;
      if (!cg_established_range_of (substituted_other, empty_established_range,
				      empty_scalar_range_cache, NULL, false,
				      false, &other_range))
	continue;

      receiver_expr = oa_strip_symbolic_ptr_expr_public (receiver_expr);
      if (TREE_CODE (receiver_expr) != PARM_DECL)
	continue;

      cg_call_group_lite *found = NULL;
      for (unsigned j = 0; j < out->length () && !found; ++j)
	if ((*out)[j].callee == call_callee
	    && (*out)[j].receiver_expr == receiver_expr)
	  found = &(*out)[j];
      if (!found)
	{
	  cg_call_group_lite g;
	  g.callee = call_callee;
	  g.receiver_expr = receiver_expr;
	  out->safe_push (g);
	  found = &out->last ();
	}
      cg_tighten_range_bound_from_range (found->range, code, other_range);
    }
}

/* Seed SEED from FNDECL's own declared precondition -- self-trust, the
   persistent-fact analogue of cg_seed_self_trust.  */

static void
cg_seed_predicate_self_trust (function *fun, cg_dom_fact_state &seed)
{
  tree fndecl = fun->decl;

  /* A predicate core function of a precondition: what the preceding
     preconditions of its function say of the fields its parameters
     point to ('is_object_address (h->ptr)'), checked before it is.  Its
     parameters start with copies of the function's own.  */
  tree owner;
  if (tree self = predicate_core_function_contract (fndecl, &owner))
    if (owner && PRECONDITION_P (self))
      for (tree as = get_fn_contract_specifiers (owner); as;
	   as = TREE_CHAIN (as))
	{
	  tree contract = CONTRACT_STATEMENT (as);
	  if (contract == self
	      || contract_predicate_core_function (contract) == fndecl
	      || (TREE_CODE (contract) == TREE_CODE (self)
		  && EXPR_LOCATION (contract) == EXPR_LOCATION (self)))
	    break;
	  if (!PRECONDITION_P (contract)
	      || !oa_contract_conveyor_active_public (contract, owner))
	    continue;
	  tree cond = CONTRACT_CONDITION (contract);
	  if (cond == NULL_TREE || cond == error_mark_node)
	    continue;
	  auto_vec<tree *> conjuncts;
	  oa_collect_conjuncts_public (&cond, &conjuncts);
	  for (unsigned i = 0; i < conjuncts.length (); ++i)
	    {
	      tree oa_arg, base;
	      if (!is_object_address_call_p (*conjuncts[i], &oa_arg))
		continue;
	      tree field = cg_object_address_field (oa_arg, &base);
	      if (!field || TREE_CODE (base) != PARM_DECL)
		continue;
	      /* OWNER's parameter -> FNDECL's at the same position.  */
	      tree c = DECL_ARGUMENTS (fndecl), q = DECL_ARGUMENTS (owner);
	      for (; q && c && q != base; q = DECL_CHAIN (q), c = DECL_CHAIN (c))
		;
	      if (!q || !c)
		continue;
	      if (tree ssa = ssa_default_def (fun, c))
		{
		  cg_field_fact *existing = seed.field.get ({ssa, field});
		  cg_field_fact fact = existing ? *existing
				       : cg_field_fact { {}, true };
		  fact.object_address = true;
		  seed.field.put ({ssa, field}, fact);
		}
	    }
	}

  for (tree as = get_fn_contract_specifiers (fndecl); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      bool conveyor_active = oa_contract_conveyor_active_cached_p (contract);
      bool symbolic_active = oa_contract_symbolic_active_cached_p (contract);
      bool conveyor_enabled = conveyor_active && flag_contract_conveyor_proofs_gimple;
      bool symbolic_enabled = symbolic_active && flag_contract_symbolic_proofs_gimple;
      if (!conveyor_enabled && !symbolic_enabled)
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
	    seed.pred.put (ssa, { pred_fn, !negated, conveyor_enabled });
	}

      auto_vec<cg_field_group_lite> field_groups;
      cg_collect_field_range_groups (cond, &field_groups);
      for (unsigned g = 0; g < field_groups.length (); ++g)
	{
	  tree ssa = ssa_default_def (fun, field_groups[g].ptr_expr);
	  if (ssa)
	    seed.field.put ({ssa, field_groups[g].field},
			     { field_groups[g].range, conveyor_enabled });
	}
      auto_vec<cg_float_field_group> float_groups;
      cg_collect_field_float_groups (cond, &float_groups);
      for (unsigned g = 0; g < float_groups.length (); ++g)
	if (tree ssa = ssa_default_def (fun, float_groups[g].ptr_expr))
	  cg_add_float_field_fact (seed, {ssa, float_groups[g].field},
				   float_groups[g].range, conveyor_enabled);

      /* 'is_object_address (this->ptr)': the field holds an object's
	 address on entry.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree oa_arg, base;
	  if (!is_object_address_call_p (*conjuncts[i], &oa_arg))
	    continue;
	  tree field = cg_object_address_field (oa_arg, &base);
	  if (!field || TREE_CODE (base) != PARM_DECL)
	    continue;
	  if (tree ssa = ssa_default_def (fun, base))
	    {
	      cg_field_fact *existing = seed.field.get ({ssa, field});
	      cg_field_fact fact = existing ? *existing
				   : cg_field_fact { {}, conveyor_enabled };
	      fact.object_address = true;
	      seed.field.put ({ssa, field}, fact);
	    }
	}

      auto_vec<cg_call_group_lite> call_groups;
      cg_collect_call_range_groups (cond, &call_groups,
				     /*allow_symbolic_accessor=*/!conveyor_enabled);
      for (unsigned g = 0; g < call_groups.length (); ++g)
	{
	  tree ssa = ssa_default_def (fun, call_groups[g].receiver_expr);
	  if (ssa)
	    seed.call.put ({ssa, call_groups[g].callee},
			    { call_groups[g].range, conveyor_enabled });
	}

      /* 'x < 100' for a reference parameter X: what X refers to, until a
	 store through a pointer or a call (cg_end_referent_facts).  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree decl, const_val;
	  tree_code code;
	  if (!oa_match_simple_comparison (*conjuncts[i], &decl, &code,
					   &const_val)
	      || TREE_CODE (const_val) != INTEGER_CST
	      || TREE_CODE (decl) != PARM_DECL
	      || !TYPE_REF_P (TREE_TYPE (decl))
	      || !INTEGRAL_TYPE_P (TREE_TYPE (TREE_TYPE (decl))))
	    continue;
	  tree ssa = ssa_default_def (fun, decl);
	  if (!ssa)
	    continue;
	  std::pair<tree, tree> key = { ssa, NULL_TREE };
	  cg_field_fact *existing = seed.field.get (key);
	  cg_field_fact fact = existing ? *existing
			       : cg_field_fact { {}, conveyor_enabled };
	  cg_tighten_range_bound (fact.range, code, wi::to_widest (const_val));
	  seed.field.put (key, fact);
	}

      /* 'i < v.size ()': holds on entry, until V changes
	 (cg_call_rel_invalidate_receiver).  So does 'i - v.size () > 10',
	 '(i - 10) > v.size ()', when the subtraction can't wrap.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rhs_receiver, rhs_callee;
	  tree_code rel_code;
	  widest_int offset = 0;
	  tree arithmetic_type = NULL_TREE;
	  if (!oa_match_comparison_against_call (*conjuncts[i], &rel_param,
						  &rel_code, &rhs_receiver,
						  &rhs_callee,
						  /*allow_symbolic_accessor=*/
						    !conveyor_enabled)
	      && !(oa_match_shifted_comparison_against_call_text
		     (*conjuncts[i], &rel_param, &rel_code, &rhs_receiver,
		      &rhs_callee, &offset,
		      /*allow_symbolic_accessor=*/!conveyor_enabled,
		      &arithmetic_type)
		   && arithmetic_type
		   && INTEGRAL_TYPE_P (arithmetic_type)
		   && TYPE_OVERFLOW_UNDEFINED (arithmetic_type)))
	    continue;
	  tree key_param = cg_self_trust_key (fun, rel_param);
	  tree key_receiver = cg_self_trust_key (fun, rhs_receiver);
	  if (key_param && key_receiver)
	    seed.call_rel.put (key_param, { rel_code, key_receiver, rhs_callee,
					    cg_range_lite_exact (offset),
					    conveyor_enabled });
	}
    }
}

/* The function's established floating-point facts, for
   cg_float_comparison_verdict's folding (set by the pass's execute).  */
static hash_map<tree, frange> *cg_established_frange_for_verdicts;

/* The range of E, an integer expression in the text of CALL's callee's
   precondition -- constants, its parameters (CALL's arguments), fields
   they point to (STATE's facts) and arithmetic of them ('v - d') -- in
   *OUT.  As the AST engine's composition, each bound is known only if
   composing it gives a representable value.  */

static bool
cg_requirement_int_range (tree e, gcall *call, tree callee,
			  cg_dom_fact_state &state,
			  hash_map<cg_field_key_hash, tree> &field_object_cache,
			  bool require_conveyor, cg_range_lite *out,
			  unsigned depth = 0)
{
  e = STRIP_ANY_LOCATION_WRAPPER (e);
  if (depth > 8 || !INTEGRAL_TYPE_P (TREE_TYPE (e)))
    return false;
  auto arg_of = [&] (tree parm) -> tree
    {
      unsigned argno;
      if (!cg_find_param_position (callee, parm, &argno)
	  || argno >= gimple_call_num_args (call))
	return NULL_TREE;
      return cg_resolve_value_argument (call, argno);
    };
  switch (TREE_CODE (e))
    {
    case INTEGER_CST:
      *out = cg_range_lite_exact (wi::to_widest (e));
      return true;
    case NON_LVALUE_EXPR:
      return cg_requirement_int_range (TREE_OPERAND (e, 0), call, callee,
				       state, field_object_cache,
				       require_conveyor, out, depth + 1);
    CASE_CONVERT:
      if (!INTEGRAL_TYPE_P (TREE_TYPE (TREE_OPERAND (e, 0)))
	  || !oa_integral_conversion_value_preserving_p
		(TREE_TYPE (TREE_OPERAND (e, 0)), TREE_TYPE (e)))
	return false;
      return cg_requirement_int_range (TREE_OPERAND (e, 0), call, callee,
				       state, field_object_cache,
				       require_conveyor, out, depth + 1);
    case PARM_DECL:
      {
	tree arg = arg_of (e);
	if (!arg)
	  return false;
	if (TREE_CODE (arg) == INTEGER_CST)
	  {
	    *out = cg_range_lite_exact (wi::to_widest (arg));
	    return true;
	  }
	return (cg_array_bounds && TREE_CODE (arg) == SSA_NAME
		&& cg_value_range_at (arg, call,
				      *cg_array_bounds->established_range,
				      *cg_array_bounds->scalar_range_cache,
				      cg_array_bounds->ranger,
				      require_conveyor, out));
      }
    case COMPONENT_REF:
      {
	tree obj = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (e, 0));
	if (TREE_CODE (obj) != INDIRECT_REF)
	  return false;
	tree base = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (obj, 0));
	while (CONVERT_EXPR_P (base) || TREE_CODE (base) == NON_LVALUE_EXPR)
	  base = TREE_OPERAND (base, 0);
	if (TREE_CODE (base) != PARM_DECL)
	  return false;
	tree arg = arg_of (base);
	if (!arg)
	  return false;
	tree identity = cg_field_slot_identity (arg, state);
	if (!identity)
	  identity = cg_field_object_identity (arg, field_object_cache);
	if (!identity)
	  identity = cg_gimple_object_identity (arg);
	cg_field_fact *fact
	  = identity ? state.field.get ({identity, TREE_OPERAND (e, 1)}) : NULL;
	if (!fact || (require_conveyor && !fact->conveyor_established)
	    || (!fact->range.has_lo && !fact->range.has_hi))
	  return false;
	*out = fact->range;
	return true;
      }
    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case NEGATE_EXPR:
      {
	cg_range_lite a, b = cg_range_lite_exact (0);
	if (!cg_requirement_int_range (TREE_OPERAND (e, 0), call, callee, state,
				       field_object_cache, require_conveyor,
				       &a, depth + 1)
	    || (TREE_CODE (e) != NEGATE_EXPR
		&& !cg_requirement_int_range (TREE_OPERAND (e, 1), call, callee,
					      state, field_object_cache,
					      require_conveyor, &b, depth + 1)))
	  return false;
	cg_range_lite r;
	switch (TREE_CODE (e))
	  {
	  case PLUS_EXPR:
	    r.has_lo = a.has_lo && b.has_lo, r.lo = a.lo + b.lo;
	    r.has_hi = a.has_hi && b.has_hi, r.hi = a.hi + b.hi;
	    break;
	  case MINUS_EXPR:
	    r.has_lo = a.has_lo && b.has_hi, r.lo = a.lo - b.hi;
	    r.has_hi = a.has_hi && b.has_lo, r.hi = a.hi - b.lo;
	    break;
	  case NEGATE_EXPR:
	    r.has_lo = a.has_hi, r.lo = -a.hi;
	    r.has_hi = a.has_lo, r.hi = -a.lo;
	    break;
	  default:
	    if (!(a.has_lo && a.has_hi && b.has_lo && b.has_hi))
	      return false;
	    {
	      widest_int p[4] = { a.lo * b.lo, a.lo * b.hi, a.hi * b.lo,
				  a.hi * b.hi };
	      r.lo = r.hi = p[0];
	      for (auto &v : p)
		r.lo = wi::smin (r.lo, v), r.hi = wi::smax (r.hi, v);
	      r.has_lo = r.has_hi = true;
	    }
	    break;
	  }
	widest_int tmin = wi::to_widest (TYPE_MIN_VALUE (TREE_TYPE (e)));
	widest_int tmax = wi::to_widest (TYPE_MAX_VALUE (TREE_TYPE (e)));
	r.has_lo &= r.lo >= tmin && r.lo <= tmax;
	r.has_hi &= r.hi >= tmin && r.hi <= tmax;
	*out = r;
	return r.has_lo || r.has_hi;
      }
    default:
      return false;
    }
}

/* The floating-point counterpart of cg_requirement_int_range.  */

static bool
cg_requirement_float_range (tree e, gcall *call, tree callee,
			    cg_dom_fact_state &state,
			    hash_map<cg_field_key_hash, tree> &field_object_cache,
			    bool require_conveyor, frange &out,
			    unsigned depth = 0)
{
  e = STRIP_ANY_LOCATION_WRAPPER (e);
  tree type = TREE_TYPE (e);
  if (depth > 8 || !SCALAR_FLOAT_TYPE_P (type))
    return false;
  switch (TREE_CODE (e))
    {
    case REAL_CST:
      out = frange (e, e);
      if (!real_isnan (TREE_REAL_CST_PTR (e)))
	out.clear_nan ();
      return true;
    case NON_LVALUE_EXPR:
      return cg_requirement_float_range (TREE_OPERAND (e, 0), call, callee,
					 state, field_object_cache,
					 require_conveyor, out, depth + 1);
    case PARM_DECL:
      {
	unsigned argno;
	if (!cg_find_param_position (callee, e, &argno)
	    || argno >= gimple_call_num_args (call))
	  return false;
	tree arg = cg_resolve_value_argument (call, argno);
	if (TREE_CODE (arg) == REAL_CST)
	  return cg_requirement_float_range (arg, call, callee, state,
					     field_object_cache,
					     require_conveyor, out, depth + 1);
	out = frange (type);
	return (cg_array_bounds && TREE_CODE (arg) == SSA_NAME
		&& cg_frange_of (arg, call, cg_array_bounds->ranger,
				 cg_established_frange_for_verdicts, out)
		&& !out.varying_p ());
      }
    case COMPONENT_REF:
      {
	tree obj = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (e, 0));
	if (TREE_CODE (obj) != INDIRECT_REF)
	  return false;
	tree base = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (obj, 0));
	while (CONVERT_EXPR_P (base) || TREE_CODE (base) == NON_LVALUE_EXPR)
	  base = TREE_OPERAND (base, 0);
	unsigned argno;
	if (TREE_CODE (base) != PARM_DECL
	    || !cg_find_param_position (callee, base, &argno)
	    || argno >= gimple_call_num_args (call))
	  return false;
	tree arg = cg_resolve_value_argument (call, argno);
	tree identity = cg_field_slot_identity (arg, state);
	if (!identity)
	  identity = cg_field_object_identity (arg, field_object_cache);
	if (!identity)
	  identity = cg_gimple_object_identity (arg);
	cg_field_fact *fact
	  = identity ? state.field.get ({identity, TREE_OPERAND (e, 1)}) : NULL;
	if (!fact || !fact->has_float
	    || (require_conveyor && !fact->conveyor_established))
	  return false;
	out = fact->float_range;
	return true;
      }
    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case RDIV_EXPR:
      {
	frange a (type), b (type);
	if (!cg_requirement_float_range (TREE_OPERAND (e, 0), call, callee,
					 state, field_object_cache,
					 require_conveyor, a, depth + 1)
	    || !cg_requirement_float_range (TREE_OPERAND (e, 1), call, callee,
					    state, field_object_cache,
					    require_conveyor, b, depth + 1))
	  return false;
	range_op_handler op (TREE_CODE (e));
	frange r (type);
	if (!op || !op.fold_range (r, type, a, b) || r.undefined_p ())
	  return false;
	out = r;
	return !out.varying_p ();
      }
    default:
      return false;
    }
}

/* Whether E, a side of a comparison in a precondition, is arithmetic
   the shape-specific consults don't take apart ('v - d', 'a * b').  */

static bool
cg_compound_requirement_side_p (tree e)
{
  e = STRIP_ANY_LOCATION_WRAPPER (e);
  while (CONVERT_EXPR_P (e) || TREE_CODE (e) == NON_LVALUE_EXPR)
    e = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (e, 0));
  switch (TREE_CODE (e))
    {
    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case RDIV_EXPR:
    case NEGATE_EXPR:
      return true;
    default:
      return false;
    }
}

/* A conjunct of CALL's callee's precondition comparing arithmetic
   ('m_value - delta >= 0.0'), which no shape-specific consult checks:
   decided from what each side's range is at CALL, or else reported as
   not verified -- never passed over.  STRICT is a proven_* contract.  */

static void
cg_check_compound_requirement (tree conjunct, gcall *call, tree callee,
			       cg_dom_fact_state &state,
			       hash_map<cg_field_key_hash, tree>
				 &field_object_cache,
			       bool require_conveyor, bool strict)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
  tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR && code != NE_EXPR)
    return;
  tree lhs = TREE_OPERAND (c, 0), rhs = TREE_OPERAND (c, 1);
  if (!cg_compound_requirement_side_p (lhs)
      && !cg_compound_requirement_side_p (rhs))
    return;
  /* 'idx - v.size () > 10' is cg_check_call_relational_fact's.  */
  {
    tree param, receiver, accessor;
    tree_code shifted_code;
    widest_int offset;
    if (oa_match_shifted_comparison_against_call_text
	  (conjunct, &param, &shifted_code, &receiver, &accessor, &offset,
	   /*allow_symbolic_accessor=*/!require_conveyor))
      return;
  }

  /* The side shown: the conjunct's arithmetic, its parameters replaced
     by the arguments.  */
  auto shown = [&] (tree e) -> tree
    {
      tree d = unshare_expr (STRIP_ANY_LOCATION_WRAPPER (e));
      struct data_t { gcall *call; tree callee; };
      data_t data = { call, callee };
      cp_walk_tree (&d, [](tree *tp, int *, void *d_) -> tree
	{
	  data_t *dd = (data_t *) d_;
	  unsigned argno;
	  if (TREE_CODE (*tp) == PARM_DECL
	      && cg_find_param_position (dd->callee, *tp, &argno)
	      && argno < gimple_call_num_args (dd->call))
	    *tp = cg_display_value (cg_resolve_value_argument (dd->call,
							       argno));
	  return NULL_TREE;
	}, &data, NULL);
      return d;
    };
  tree side = cg_compound_requirement_side_p (lhs) ? lhs : rhs;

  cg_verdict v = CG_VERDICT_UNKNOWN;
  char have[96] = "", need[96] = "";
  if (SCALAR_FLOAT_TYPE_P (TREE_TYPE (lhs)))
    {
      frange a (TREE_TYPE (lhs)), b (TREE_TYPE (rhs));
      tree cst = TREE_CODE (STRIP_ANY_LOCATION_WRAPPER (rhs)) == REAL_CST
		 ? STRIP_ANY_LOCATION_WRAPPER (rhs) : NULL_TREE;
      frange want (TREE_TYPE (lhs));
      if (cst
	  && cg_requirement_float_range (lhs, call, callee, state,
					 field_object_cache, require_conveyor, a)
	  && cg_frange_for_comparison (TREE_TYPE (lhs), code, cst, &want))
	{
	  v = cg_frange_requirement_verdict (a, want);
	  cg_frange_text (a, have, sizeof have);
	  char cbuf[40];
	  real_to_decimal (cbuf, TREE_REAL_CST_PTR (cst), sizeof cbuf, 0, 1);
	  snprintf (need, sizeof need, "to be %s %s", op_symbol_code (code),
		    cbuf);
	}
    }
  else if (INTEGRAL_TYPE_P (TREE_TYPE (lhs)))
    {
      cg_range_lite a, b;
      if (cg_requirement_int_range (lhs, call, callee, state,
				    field_object_cache, require_conveyor, &a)
	  && cg_requirement_int_range (rhs, call, callee, state,
				       field_object_cache, require_conveyor,
				       &b))
	{
	  if (b.has_lo && b.has_hi && b.lo == b.hi && code != NE_EXPR)
	    v = cg_range_comparison_verdict (a, code, b.lo);
	  else if (code != NE_EXPR && code != EQ_EXPR)
	    {
	      enum oa_range_subsumption_result r
		= cg_range_pair_relation (a, code, b);
	      v = (r == OA_RANGE_SUBSUMED ? CG_VERDICT_TRUE
		   : r == OA_RANGE_DISJOINT ? CG_VERDICT_FALSE
		   : CG_VERDICT_UNKNOWN);
	    }
	  cg_range_lite_text (a, have, sizeof have);
	  if (b.has_lo && b.has_hi && b.lo == b.hi)
	    snprintf (need, sizeof need, "to be %s %lld", op_symbol_code (code),
		      (long long) b.lo.to_shwi ());
	  else
	    {
	      char rb[64];
	      cg_range_lite_text (b, rb, sizeof rb);
	      snprintf (need, sizeof need, "to be %s a value %s",
			op_symbol_code (code), rb);
	    }
	}
    }
  if (v == CG_VERDICT_TRUE)
    return;
  location_t loc = gimple_location (call);
  if (v == CG_VERDICT_FALSE)
    {
      auto_diagnostic_group d;
      cg_obligation_error_at (loc, "argument %qE provably violates the precondition of %qD",
			       shown (side), callee);
      inform (loc, "%qE is established %s, but the precondition requires "
	      "it %s", shown (side), have, need);
      return;
    }
  auto_diagnostic_group d;
  if (strict)
    cg_obligation_error_at (loc, "cannot prove that %qE satisfies the precondition of %qD",
			     shown (side), callee);
  else
    warning_at (loc, 0, "cannot verify that %qE satisfies the precondition "
		"of %qD", shown (side), callee);
  inform (loc, "%s", _(oa_unprovable_reason_text
		       (have[0] ? OA_UNPROVABLE_RANGE_PARTIAL
				: OA_UNPROVABLE_NO_FACT)));
}

/* CALL's own callee's declared precondition, checked against STATE as
   it stands right before CALL -- the consult side, for both facts,
   the persistent-fact analogue of cg_check_call.  */

static cg_verdict cg_range_comparison_verdict (const cg_range_lite &,
					       tree_code, const widest_int &);

static void
cg_consult_persistent_facts (gcall *call,
			      hash_map<cg_field_key_hash, tree> &field_object_cache,
			      cg_dom_fact_state &state)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      bool conveyor_active = oa_contract_conveyor_active_cached_p (contract);
      bool symbolic_active = oa_contract_symbolic_active_cached_p (contract);
      /* See cg_check_call's own identical 'mandatory'.  */
      bool mandatory = (cg_callee_pre_mandatory && conveyor_active
			&& DECL_DECLARED_CONVEYOR_P (callee));
      if (cg_callee_pre_only && !mandatory)
	continue;
      bool check_as_conveyor = (mandatory
				|| (conveyor_active
				    && flag_contract_conveyor_proofs_gimple
				    && cg_proof_checks_p (contract, callee,
							  true)));
      bool check_as_symbolic = (!cg_callee_pre_only && symbolic_active
				&& flag_contract_symbolic_proofs_gimple
				&& cg_proof_checks_p (contract, callee, false));
      if (!check_as_conveyor && !check_as_symbolic)
	continue;
      bool require_conveyor = check_as_conveyor;

      /* D4324: see cg_check_call's own identical 'strict' computation.  */
      bool strict = mandatory
		    || (check_as_conveyor
			&& oa_contract_conveyor_strict_cached_p (contract))
		    || (check_as_symbolic
			&& oa_contract_symbolic_strict_cached_p (contract));

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	cg_check_compound_requirement (*conjuncts[i], call, callee, state,
				       field_object_cache, require_conveyor,
				       strict);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree pred_fn, arg_decl;
	  bool negated;
	  if (!oa_match_predicate_conjunct (*conjuncts[i], &pred_fn, &arg_decl,
					     &negated))
	    continue;

	  unsigned argno;
	  if (!cg_find_param_position (callee, arg_decl, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  /* Consult-only copy-construction lookthrough (cg_resolve_call_
	     argument, not a bare gimple_call_arg) -- sound here
	     specifically because this checks a REQUIREMENT against an
	     already-established fact, and a copy has the same state as
	     its source at the moment of copying; see contracts.cc's own
	     identical reasoning for oa_handle_call_symbolic_precondition_
	     obligation's predicate block.  cg_establish_persistent_facts_
	     for_call/cg_invalidate_persistent_facts_for_call_args
	     deliberately do NOT do this.  */
	  tree substituted = cg_resolve_value_argument (call, argno);
	  /* Ordering matters, same reason cg_field_slot_identity must be
	     tried before cg_gimple_object_identity: that resolver always
	     "succeeds" for a pointer-typed SSA value with an unrecognized
	     def-stmt, falling back to the value itself (confirmed this
	     includes an ADDR_EXPR def-stmt, e.g. '_1 = &h->f;'), which
	     would silently pre-empt this resolver if tried after it.  */
	  tree identity = cg_field_slot_identity (substituted, state);
	  if (!identity)
	    identity = cg_field_object_identity (substituted, field_object_cache);
	  if (!identity)
	    identity = cg_gimple_object_identity (substituted);
	  /* A scalar value: the predicate's fact about it, e.g. from the
	     postcondition of the call that returned it.  */
	  if (!identity && TREE_CODE (substituted) == SSA_NAME)
	    identity = cg_scalar_value_origin (substituted);

	  bool required = !negated;
	  cg_pred_fact *fact = identity ? state.pred.get (identity) : NULL;
	  /* No fact about the argument: PRED_FN's postcondition may define
	     its result as a comparison of its parameter with a constant
	     ('post (r: r == (v > 0))'), decided by the argument's range,
	     as the AST engine's oa_predicate_result_from_declared_relation
	     does.  */
	  tree_code rel_code;
	  tree rel_const;
	  if ((!fact || fact->pred_fn != pred_fn)
	      && oa_predicate_defining_relation_public (pred_fn,
							require_conveyor,
							&rel_code, &rel_const))
	    {
	      cg_range_lite r;
	      bool have = false;
	      if (TREE_CODE (substituted) == INTEGER_CST)
		{
		  r.has_lo = r.has_hi = true;
		  r.lo = r.hi = wi::to_widest (substituted);
		  have = true;
		}
	      else if (TREE_CODE (substituted) == SSA_NAME
		       && INTEGRAL_TYPE_P (TREE_TYPE (substituted))
		       && cg_array_bounds)
		have = cg_value_range_at (substituted, call,
					  *cg_array_bounds->established_range,
					  *cg_array_bounds->scalar_range_cache,
					  cg_array_bounds->ranger,
					  require_conveyor, &r);
	      cg_verdict v = (have
			      ? cg_range_comparison_verdict
				  (r, rel_code, wi::to_widest (rel_const))
			      : CG_VERDICT_UNKNOWN);
	      if (v == CG_VERDICT_TRUE || v == CG_VERDICT_FALSE)
		{
		  bool value = v == CG_VERDICT_TRUE;
		  if (value == required)
		    continue; /* Proven true: silently discharged.  */
		  cg_obligation_error_at (gimple_location (call),
					   "argument %qE provably violates the precondition "
					   "of %qD: %qD (%qE) is established %s, but the "
					   "precondition requires it to be %s",
					   cg_display_value (substituted), callee, pred_fn,
					   cg_display_value (substituted),
					   value ? "true" : "false",
					   required ? "true" : "false");
		  continue;
		}
	    }
	  if (fact && fact->pred_fn == pred_fn
	      && (!require_conveyor || fact->conveyor_established))
	    {
	      if (fact->polarity == required)
		continue; /* Proven true: silently discharged.  */
	      /* D4324: PRED_FN is established at the *opposite* polarity --
		 a genuine, provable contradiction, not merely "unknown" --
		 mirroring contracts.cc's own OA_PROVEN_FALSE tier for this
		 same shape (oa_env_predicate_result's own comment).  */
	      cg_obligation_error_at (gimple_location (call),
				       "argument %qE provably violates the precondition of "
				       "%qD: %qD (%qE) is established %s, but the "
				       "precondition requires it to be %s",
				       cg_display_value (substituted), callee, pred_fn,
				       cg_display_value (substituted),
				       fact->polarity ? "true" : "false",
				       required ? "true" : "false");
	      continue;
	    }

	  if (strict)
	    cg_obligation_error_at (gimple_location (call),
				     "cannot prove that %qD (%qE) holds, as required by "
				     "the precondition of %qD", pred_fn,
				     cg_display_value (substituted), callee);
	  else
	    warning_at (gimple_location (call), 0,
			"cannot verify that %qD (%qE) holds, as required by "
			"the precondition of %qD", pred_fn,
			cg_display_value (substituted), callee);
	  oa_unprovable_reason reason
	    = !fact ? OA_UNPROVABLE_NO_FACT
	      : fact->pred_fn != pred_fn ? OA_UNPROVABLE_WRONG_IDENTITY
	      : OA_UNPROVABLE_WEAKER_PROVENANCE;
	  if (const char *why = oa_unprovable_reason_text (reason))
	    inform (gimple_location (call), "%s", _(why));
	}

      auto_vec<cg_field_group_lite> field_groups;
      cg_collect_field_range_groups (cond, &field_groups);
      for (unsigned g = 0; g < field_groups.length (); ++g)
	{
	  unsigned argno;
	  if (!cg_find_param_position (callee, field_groups[g].ptr_expr, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  /* Consult-only copy-construction lookthrough -- see this
	     function's own predicate block above.  */
	  tree substituted = cg_resolve_value_argument (call, argno);
	  tree identity = cg_field_slot_identity (substituted, state);
	  if (!identity)
	    identity = cg_field_object_identity (substituted, field_object_cache);
	  if (!identity)
	    identity = cg_gimple_object_identity (substituted);
	  cg_range_lite &required = field_groups[g].range;

	  cg_field_fact *established
	    = identity ? state.field.get ({identity, field_groups[g].field}) : NULL;
	  if (established
	      && (!require_conveyor || established->conveyor_established)
	      && (!required.has_lo
		  || (established->range.has_lo
		      && established->range.lo >= required.lo))
	      && (!required.has_hi
		  || (established->range.has_hi
		      && established->range.hi <= required.hi)))
	    continue; /* Proven true: silently discharged.  */
	  if (established
	      && (!require_conveyor || established->conveyor_established)
	      && cg_ranges_disjoint_p (established->range, required))
	    {
	      char have_text[64], need_text[64];
	      cg_obligation_error_at (gimple_location (call),
				       "argument %qE provably violates the precondition of %qD",
				       substituted, callee);
	      inform (gimple_location (call),
		      "field %qD of %qE is established %s, but the "
		      "precondition requires it %s",
		      field_groups[g].field, substituted,
		      cg_range_lite_text (established->range, have_text,
					  sizeof have_text),
		      cg_range_lite_text (required, need_text,
					  sizeof need_text));
	      continue;
	    }

	  if (strict)
	    cg_obligation_error_at (gimple_location (call),
				     "cannot prove that field %qD of %qE satisfies the "
				     "precondition of %qD",
				     field_groups[g].field, substituted, callee);
	  else
	    warning_at (gimple_location (call), 0,
			"cannot verify that field %qD of %qE satisfies the "
			"precondition of %qD",
			field_groups[g].field, substituted, callee);
	  oa_unprovable_reason reason
	    = !established ? OA_UNPROVABLE_NO_FACT
	      : (require_conveyor && !established->conveyor_established)
		? OA_UNPROVABLE_WEAKER_PROVENANCE
		: OA_UNPROVABLE_RANGE_PARTIAL;
	  if (const char *why = oa_unprovable_reason_text (reason))
	    inform (gimple_location (call), "%s", _(why));
	}

      auto_vec<cg_float_field_group> float_groups;
      cg_collect_field_float_groups (cond, &float_groups);
      for (unsigned g = 0; g < float_groups.length (); ++g)
	{
	  unsigned argno;
	  if (!cg_find_param_position (callee, float_groups[g].ptr_expr, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = cg_resolve_value_argument (call, argno);
	  tree identity = cg_field_slot_identity (substituted, state);
	  if (!identity)
	    identity = cg_field_object_identity (substituted, field_object_cache);
	  if (!identity)
	    identity = cg_gimple_object_identity (substituted);
	  cg_field_fact *established
	    = identity ? state.field.get ({identity, float_groups[g].field}) : NULL;
	  bool usable = (established && established->has_float
			 && (!require_conveyor
			     || established->conveyor_established));
	  cg_verdict v = (usable
			  ? cg_frange_requirement_verdict
			      (established->float_range, float_groups[g].range)
			  : CG_VERDICT_UNKNOWN);
	  if (v == CG_VERDICT_TRUE)
	    continue; /* Proven true: silently discharged.  */
	  if (v == CG_VERDICT_FALSE)
	    {
	      char have[96], need[96];
	      cg_obligation_error_at (gimple_location (call),
				       "argument %qE provably violates the precondition of %qD",
				       substituted, callee);
	      inform (gimple_location (call),
		      "field %qD of %qE is established %s, but the precondition "
		      "requires it %s", float_groups[g].field, substituted,
		      cg_frange_text (established->float_range, have,
				      sizeof have),
		      cg_frange_text (float_groups[g].range, need, sizeof need));
	      continue;
	    }
	  if (strict)
	    cg_obligation_error_at (gimple_location (call),
				     "cannot prove that field %qD of %qE satisfies the "
				     "precondition of %qD",
				     float_groups[g].field, substituted, callee);
	  else
	    warning_at (gimple_location (call), 0,
			"cannot verify that field %qD of %qE satisfies the "
			"precondition of %qD",
			float_groups[g].field, substituted, callee);
	  oa_unprovable_reason reason
	    = !usable ? OA_UNPROVABLE_NO_FACT : OA_UNPROVABLE_RANGE_PARTIAL;
	  if (const char *why = oa_unprovable_reason_text (reason))
	    inform (gimple_location (call), "%s", _(why));
	}

      auto_vec<cg_call_group_lite> call_groups;
      cg_collect_call_range_groups (cond, &call_groups,
				     /*allow_symbolic_accessor=*/!require_conveyor);
      for (unsigned g = 0; g < call_groups.length (); ++g)
	{
	  unsigned argno;
	  if (!cg_find_param_position (callee, call_groups[g].receiver_expr,
					&argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = cg_resolve_value_argument (call, argno);
	  tree identity = cg_field_slot_identity (substituted, state);
	  if (!identity)
	    identity = cg_field_object_identity (substituted, field_object_cache);
	  if (!identity)
	    identity = cg_gimple_object_identity (substituted);
	  cg_range_lite &required = call_groups[g].range;

	  cg_field_fact *established
	    = identity ? state.call.get ({identity, call_groups[g].callee}) : NULL;
	  if (established
	      && (!require_conveyor || established->conveyor_established)
	      && (!required.has_lo
		  || (established->range.has_lo
		      && established->range.lo >= required.lo))
	      && (!required.has_hi
		  || (established->range.has_hi
		      && established->range.hi <= required.hi)))
	    continue; /* Proven true: silently discharged.  */
	  if (established
	      && (!require_conveyor || established->conveyor_established)
	      && cg_ranges_disjoint_p (established->range, required))
	    {
	      char have_text[64], need_text[64];
	      cg_obligation_error_at (gimple_location (call),
				       "argument %qE provably violates the precondition of %qD: "
				       "%qD is established outside the required range",
				       cg_display_value (substituted), callee,
				       call_groups[g].callee);
	      inform (gimple_location (call),
		      "%qD is established %s, but the precondition requires "
		      "it to be %s", call_groups[g].callee,
		      cg_range_lite_text (established->range, have_text,
					  sizeof have_text),
		      cg_range_lite_text (required, need_text,
					  sizeof need_text));
	      continue;
	    }

	  if (strict)
	    cg_obligation_error_at (gimple_location (call),
				     "cannot prove that %qD called on %qE satisfies the "
				     "precondition of %qD",
				     call_groups[g].callee, substituted, callee);
	  else
	    warning_at (gimple_location (call), 0,
			"cannot verify that %qD called on %qE satisfies the "
			"precondition of %qD",
			call_groups[g].callee, substituted, callee);
	  oa_unprovable_reason reason
	    = !established ? OA_UNPROVABLE_NO_FACT
	      : (require_conveyor && !established->conveyor_established)
		? OA_UNPROVABLE_WEAKER_PROVENANCE
		: OA_UNPROVABLE_RANGE_PARTIAL;
	  if (const char *why = oa_unprovable_reason_text (reason))
	    inform (gimple_location (call), "%s", _(why));
	}
    }
}

/* Stage 4e: the GIMPLE analogue of contracts.cc's own Stage 3
   (oa_could_alias_as_parameters/oa_invalidate_parameter_alias_group)
   -- two of a function's own distinct parameters are never treated as
   potentially the same object, even though 'f(p, p)' is ordinary,
   legal C++ unless a parameter is __restrict-qualified. Conservative
   by default, matching the AST engine's own already-user-approved
   scope decision: any two same/compatible-typed, non-__restrict
   parameters are always a potential-alias group.

   Key representation difference from the AST port: an identity
   flowing through state.pred/.field/.field_alias for a pointer
   parameter used by value (the common case) is that parameter's own
   *default-def SSA name* (ssa_default_def (fun, parm)), not the bare
   PARM_DECL -- confirmed by cg_gimple_object_identity_1's own SSA_NAME
   branch (a default def's own SSA_NAME_DEF_STMT is GIMPLE_NOP, which
   matches neither its GIMPLE_PHI nor is_gimple_assign cases, so it
   falls through to 'return val'), and by cg_seed_predicate_self_trust's
   own existing 'ssa_default_def (fun, arg_decl)' lookup when seeding
   these same maps. If a parameter's own address is ever taken, it's
   kept out of SSA entirely and referenced as a bare PARM_DECL instead
   (cg_gimple_object_identity_1's own last, VAR_P||PARM_DECL branch) --
   so IDENTITY here must accept *either* representation.  */

static tree
cg_identity_as_parm (tree identity)
{
  if (TREE_CODE (identity) == PARM_DECL)
    return identity;
  if (TREE_CODE (identity) == SSA_NAME && SSA_NAME_IS_DEFAULT_DEF (identity)
      && TREE_CODE (SSA_NAME_VAR (identity)) == PARM_DECL)
    return SSA_NAME_VAR (identity);
  return NULL_TREE;
}

/* IDENTITY is guarded to be a genuine parameter of FUN before doing
   anything, so sweeping a local variable's own invalidation (the
   overwhelmingly common case) is an immediate, cheap no-op -- mirrors
   contracts.cc's own oa_invalidate_parameter_alias_group identically.
   Explicitly scoped to whole-object invalidation only, matching the
   AST engine's own already-disclosed scope cut: a parameter that is
   itself a pointer to a struct, mutated through a narrower field-slot
   write, does not sweep its alias-group siblings' own field-slot
   facts (only cg_process_field_write's own state.pred.remove effect
   gets this treatment, not its state.field_alias recording).

   FUN is threaded explicitly rather than reading ambient cfun --
   confirmed by grep this file uses bare cfun nowhere at all, instead
   threading function *fun explicitly through every call in this
   subsystem (pass_contracts_gimple::execute, cg_seed_predicate_self_
   trust, cg_predicate_facts_walk); cfun would happen to be numerically
   correct here too (the pass manager sets it before execute and it
   stays set for the whole call tree underneath), but there's no reason
   to introduce this file's first bare-cfun use when fun is already
   available to thread through one more parameter.  */

static void
cg_invalidate_parameter_alias_group (tree identity, function *fun,
				      hash_map<cg_field_key_hash, tree> &field_object_cache,
				      cg_dom_fact_state &state)
{
  tree parm = cg_identity_as_parm (identity);
  if (!parm || DECL_CONTEXT (parm) != fun->decl)
    return;
  for (tree sib = DECL_ARGUMENTS (fun->decl); sib; sib = DECL_CHAIN (sib))
    {
      if (!oa_could_alias_as_parameters_public (parm, sib))
	continue;
      /* SIB's own identity as it would appear in state.*, mirroring
	 cg_seed_predicate_self_trust's own identical lookup -- prefer
	 the default-def SSA name, matching every already-established
	 entry's own key shape; fall back to the bare decl only for the
	 address-taken case, matching cg_gimple_object_identity_1's own
	 fallback.  */
      tree sib_identity = ssa_default_def (fun, sib);
      if (!sib_identity)
	sib_identity = sib;
      state.pred.remove (sib_identity);
      /* PARM's own are its caller's to end.  */
      if (sib != parm)
	cg_call_rel_invalidate_receiver (state, sib_identity);
      cg_field_object_predicate_invalidate_all (sib_identity, field_object_cache, state);
      auto_vec<std::pair<tree, tree>> to_remove;
      for (auto it : state.field)
	if (it.first.first == sib_identity)
	  to_remove.safe_push (it.first);
      for (unsigned j = 0; j < to_remove.length (); ++j)
	state.field.remove (to_remove[j]);
      to_remove.truncate (0);
      for (auto it : state.field_alias)
	if (it.first.first == sib_identity)
	  to_remove.safe_push (it.first);
      for (unsigned j = 0; j < to_remove.length (); ++j)
	state.field_alias.remove (to_remove[j]);
      to_remove.truncate (0);
      for (auto it : state.call)
	if (it.first.first == sib_identity)
	  to_remove.safe_push (it.first);
      for (unsigned j = 0; j < to_remove.length (); ++j)
	state.call.remove (to_remove[j]);
    }
}

/* A tracked object's fact must be invalidated by *any* call taking its
   address or receiving it as a bare pointer, whether or not that call
   has any contracts of its own at all -- unconditional, matching
   contracts.cc's own oa_invalidate_symbolic_facts_for_call_args, and
   the plugin's own identical invalidate_persistent_facts_for_call_args.
   Drops every tracked field for the same identity too (whole-object
   granularity).

   Except for a call *to* a conversion operator itself: cg_gimple_
   object_identity now looks through such a call to reach the wrapped
   object's own identity (cg_resolve_conversion_receiver), so a call
   through 'ref.operator T()' has that same 'ref' as its own implicit-
   object argument -- without this guard, every consult of a fact
   reached via a wrapper's conversion operator would invalidate that
   exact fact via the very call used to reach it, in program order
   before the consult even runs.  Mirrors contracts.cc's own oa_call_
   is_conversion_operator_call precedent (a conversion operator is
   already trusted as a same-object, non-mutating pass-through
   everywhere else in this pass).  */

static void cg_invalidate_persistent_facts_for_call_arg
  (tree, function *, hash_map<cg_field_key_hash, tree> &, cg_dom_fact_state &);

static void
cg_invalidate_persistent_facts_for_call_args (gcall *call, function *fun,
					       hash_map<cg_field_key_hash, tree> &field_object_cache,
					       cg_dom_fact_state &state)
{
  tree call_callee = gimple_call_fndecl (call);
  if (call_callee && DECL_CONV_FN_P (call_callee))
    return;
  /* Any call may change what a reference refers to through an alias --
     but not a contract's check, whose condition changes nothing.  */
  {
    tree sv, cd, dc;
    /* A builtin handed no pointer (__builtin_observable_checkpoint)
       can't reach one either.  */
    bool pointer_free_builtin = gimple_call_builtin_p (call, BUILT_IN_NORMAL);
    for (unsigned k = 0; pointer_free_builtin
			 && k < gimple_call_num_args (call); ++k)
      if (POINTER_TYPE_P (TREE_TYPE (gimple_call_arg (call, k))))
	pointer_free_builtin = false;
    if (!cg_contract_check_call_p (call, &sv, &cd, &dc)
	&& !gimple_call_internal_p (call) && !pointer_free_builtin
	&& !(gimple_call_flags (call) & (ECF_CONST | ECF_PURE)))
      cg_end_referent_facts (state);
  }
  /* Placement new only returns the pointer it is given.  */
  if (call_callee && std_placement_new_fn_p (call_callee))
    return;
  unsigned n = gimple_call_num_args (call);
  tree parm_types = (gimple_call_fntype (call)
		     ? TYPE_ARG_TYPES (gimple_call_fntype (call)) : NULL_TREE);
  for (unsigned i = 0; i < n;
       ++i, parm_types = parm_types ? TREE_CHAIN (parm_types) : NULL_TREE)
    {
      tree arg = gimple_call_arg (call, i);
      /* A scalar in memory bound to a const reference keeps its value
	 facts, as in the AST engine.  */
      cg_field_fact kept;
      bool keep = false;
      if (TREE_CODE (arg) == ADDR_EXPR && DECL_P (TREE_OPERAND (arg, 0))
	  && parm_types && TYPE_REF_P (TREE_VALUE (parm_types))
	  && TYPE_READONLY (TREE_TYPE (TREE_VALUE (parm_types))))
	if (cg_field_fact *f = state.field.get
	      (cg_scalar_memory_fact_key (TREE_OPERAND (arg, 0))))
	  {
	    kept = *f;
	    keep = true;
	  }
      cg_invalidate_persistent_facts_for_call_arg (arg, fun,
						   field_object_cache, state);
      /* An accessor's result on an object the callee can't change
	 through ARG (a reference or pointer to const) stays put.  */
      if (POINTER_TYPE_P (TREE_TYPE (arg))
	  && !(parm_types && POINTER_TYPE_P (TREE_VALUE (parm_types))
	       && TYPE_READONLY (TREE_TYPE (TREE_VALUE (parm_types)))))
	if (tree identity = cg_gimple_object_identity (arg))
	  cg_call_rel_invalidate_receiver (state, identity);
      /* A pointer or reference to non-const whose object can't be named
	 (loaded from memory, returned by a call): the callee may change
	 any object whose address has escaped, as in the AST engine
	 (oa_env::invalidate_all_object_state).  */
      if (POINTER_TYPE_P (TREE_TYPE (arg))
	  && !TYPE_READONLY (TREE_TYPE (TREE_TYPE (arg)))
	  && !FUNC_OR_METHOD_TYPE_P (TREE_TYPE (TREE_TYPE (arg)))
	  && TREE_CODE (arg) != INTEGER_CST
	  && !(parm_types && POINTER_TYPE_P (TREE_VALUE (parm_types))
	       && TYPE_READONLY (TREE_TYPE (TREE_VALUE (parm_types)))))
	{
	  tree identity = cg_field_slot_identity (arg, state);
	  if (!identity)
	    identity = cg_field_object_identity (arg, field_object_cache);
	  if (!identity)
	    identity = cg_gimple_object_identity (arg);
	  if (cg_unnamed_pointer_identity_p (arg, identity))
	    {
	      state.pred.empty ();
	      /* A non-mutable lambda's closure is not its body's to change,
		 and the body doesn't name it to the callee: its captures
		 stay what they were, as the AST engine keeps them.  */
	      tree closure = NULL_TREE;
	      if (LAMBDA_FUNCTION_P (fun->decl) && DECL_ARGUMENTS (fun->decl)
		  && POINTER_TYPE_P (TREE_TYPE (DECL_ARGUMENTS (fun->decl)))
		  && TYPE_READONLY (TREE_TYPE (TREE_TYPE
					       (DECL_ARGUMENTS (fun->decl)))))
		if (tree ssa = ssa_default_def (fun, DECL_ARGUMENTS (fun->decl)))
		  closure = cg_gimple_object_identity (ssa);
	      if (closure && closure != identity)
		{
		  auto_vec<std::pair<tree, tree>> drop;
		  for (auto it : state.field)
		    if (it.first.first != closure)
		      drop.safe_push (it.first);
		  for (auto k : drop)
		    state.field.remove (k);
		}
	      else
		state.field.empty ();
	      state.field_alias.empty ();
	      state.call.empty ();
	      state.call_rel.empty ();
	      state.call_call.empty ();
	      keep = false;
	    }
	}
      if (keep)
	state.field.put (cg_scalar_memory_fact_key (TREE_OPERAND (arg, 0)),
			 kept);
    }
}

/* The body of cg_invalidate_persistent_facts_for_call_args, for one
   argument ARG of CALL.  */

static void
cg_invalidate_persistent_facts_for_call_arg (tree arg, function *fun,
					     hash_map<cg_field_key_hash, tree> &field_object_cache,
					     cg_dom_fact_state &state)
{
  /* An aggregate passed by value is a copy the callee can't change the
     caller's object through.  */
  if (AGGREGATE_TYPE_P (TREE_TYPE (arg)))
    return;
  /* Ordering: cg_field_slot_identity, then cg_field_object_identity,
     then cg_gimple_object_identity last -- see cg_consult_persistent_
     facts's own identical comment for why the fallback resolver must
     stay last.  */
  tree identity = cg_field_slot_identity (arg, state);
  if (!identity)
    identity = cg_field_object_identity (arg, field_object_cache);
  if (!identity)
    identity = cg_gimple_object_identity (arg);
  if (!identity)
    {
      /* A scalar handed to a call (a file descriptor to 'close'): a
	 predicate about it may no longer hold, as in the AST engine.  */
      if (TREE_CODE (arg) == SSA_NAME)
	state.pred.remove (cg_scalar_value_origin (arg));
      return;
    }
  state.pred.remove (identity);
  cg_field_object_predicate_invalidate_all (identity, field_object_cache, state);

  auto_vec<std::pair<tree, tree>> to_remove;
  for (auto it : state.field)
    if (it.first.first == identity)
      to_remove.safe_push (it.first);
  for (unsigned j = 0; j < to_remove.length (); ++j)
    state.field.remove (to_remove[j]);

  to_remove.truncate (0);
  for (auto it : state.field_alias)
    if (it.first.first == identity)
      to_remove.safe_push (it.first);
  for (unsigned j = 0; j < to_remove.length (); ++j)
    state.field_alias.remove (to_remove[j]);

  to_remove.truncate (0);
  for (auto it : state.call)
    if (it.first.first == identity)
      to_remove.safe_push (it.first);
  for (unsigned j = 0; j < to_remove.length (); ++j)
    state.call.remove (to_remove[j]);

  cg_invalidate_parameter_alias_group (identity, fun, field_object_cache, state);
}

/* CALL's own callee's declared postcondition establishing a fact about
   one of CALLEE's own (persistent, non-return-value) parameters -- the
   persistent-fact analogue of item 6, gated on flavor-enabled exactly
   like cg_seed_predicate_self_trust.  Order relative to invalidation
   matters, matching oa_scan_calls_in_expr's own "invalidate then
   establish" discipline: this call's own postcondition must win over
   its own (necessarily stale-by-then) invalidation of the same
   identity.  */

static void
cg_establish_persistent_facts_for_call (gcall *call,
					 hash_map<cg_field_key_hash, tree> &field_object_cache,
					 cg_dom_fact_state &state)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      bool conveyor_active = oa_contract_conveyor_active_cached_p (contract);
      bool symbolic_active = oa_contract_symbolic_active_cached_p (contract);
      bool conveyor_enabled = conveyor_active && flag_contract_conveyor_proofs_gimple;
      bool symbolic_enabled = symbolic_active && flag_contract_symbolic_proofs_gimple;
      if (!conveyor_enabled && !symbolic_enabled)
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

	  /* 'post (r: check_it (r))': about the value CALL returns.  */
	  if (arg_decl == POSTCONDITION_IDENTIFIER (contract))
	    {
	      tree lhs = gimple_call_lhs (call);
	      if (lhs && TREE_CODE (lhs) == SSA_NAME)
		state.pred.put (lhs, { pred_fn, !negated, conveyor_enabled });
	      continue;
	    }
	  unsigned argno;
	  if (!cg_find_param_position (callee, arg_decl, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);
	  tree identity = cg_field_slot_identity (substituted, state);
	  if (!identity)
	    identity = cg_field_object_identity (substituted, field_object_cache);
	  if (!identity)
	    identity = cg_gimple_object_identity (substituted);
	  if (identity)
	    state.pred.put (identity, { pred_fn, !negated, conveyor_enabled });
	}

      auto_vec<cg_field_group_lite> field_groups;
      cg_collect_field_range_groups (cond, &field_groups);
      for (unsigned g = 0; g < field_groups.length (); ++g)
	{
	  unsigned argno;
	  if (!cg_find_param_position (callee, field_groups[g].ptr_expr, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);
	  tree identity = cg_field_slot_identity (substituted, state);
	  if (!identity)
	    identity = cg_field_object_identity (substituted, field_object_cache);
	  if (!identity)
	    identity = cg_gimple_object_identity (substituted);
	  if (identity)
	    state.field.put ({identity, field_groups[g].field},
			      { field_groups[g].range, conveyor_enabled });
	}
      auto_vec<cg_float_field_group> float_groups;
      cg_collect_field_float_groups (cond, &float_groups);
      for (unsigned g = 0; g < float_groups.length (); ++g)
	{
	  unsigned argno;
	  if (!cg_find_param_position (callee, float_groups[g].ptr_expr, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);
	  tree identity = cg_field_slot_identity (substituted, state);
	  if (!identity)
	    identity = cg_field_object_identity (substituted, field_object_cache);
	  if (!identity)
	    identity = cg_gimple_object_identity (substituted);
	  if (identity)
	    cg_add_float_field_fact (state, {identity, float_groups[g].field},
				     float_groups[g].range, conveyor_enabled);
	}

      auto_vec<cg_call_group_lite> call_groups;
      cg_collect_call_range_groups (cond, &call_groups,
				     /*allow_symbolic_accessor=*/!conveyor_enabled);
      /* Bounds-proving demo, Part 3: a parametric call-range conjunct
	 (e.g. 'post<>(size () == n)') folds into the same CALL_GROUPS,
	 resolved through this specific call site's own argument for N.  */
      cg_collect_call_range_groups_parametric (cond, callee, call, &call_groups);
      for (unsigned g = 0; g < call_groups.length (); ++g)
	{
	  unsigned argno;
	  if (!cg_find_param_position (callee, call_groups[g].receiver_expr,
					&argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = gimple_call_arg (call, argno);
	  tree identity = cg_field_slot_identity (substituted, state);
	  if (!identity)
	    identity = cg_field_object_identity (substituted, field_object_cache);
	  if (!identity)
	    identity = cg_gimple_object_identity (substituted);
	  if (identity)
	    state.call.put ({identity, call_groups[g].callee},
			     { call_groups[g].range, conveyor_enabled });
	}
    }
}

static tree cg_contract_check_call_p (gcall *, tree *, tree *, tree *);
static tree cg_contract_check_decl_value (gcall *, tree, tree, tree);

/* Verdicts the dominator-ordered fact walk reaches for an assert's
   conjuncts about memory (fields, named predicates, scalars in memory),
   which the check pass reports along with the rest
   (cg_check_contract_assert).  TEXT holds what was established, for a
   false one.  */

struct cg_assert_memory_verdicts
{
  static const unsigned max_conjuncts = 32;
  cg_verdict verdict[max_conjuncts][2];	/* [i][0] conveyor, [i][1] symbolic.  */
  char text[max_conjuncts][96];
};

typedef hash_map<gimple *, cg_assert_memory_verdicts *> cg_assert_memory_verdict_map;

static cg_verdict cg_frange_comparison_verdict (const frange &, tree_code,
						tree, char *, size_t);

/* Is DECL a scalar in memory (its address is taken) whose value the
   walk can follow: every use of its address is a direct call argument
   or the capture struct of a contract check, so it changes only through
   a store to it or a call it is passed to, both of which end its facts
   (cg_scalar_memory_fact_key).  */

static bool
cg_scalar_memory_tracking_p (function *fun, tree decl)
{
  if ((!VAR_P (decl) && TREE_CODE (decl) != PARM_DECL)
      || !TREE_ADDRESSABLE (decl) || !SCALAR_TYPE_P (TREE_TYPE (decl))
      || is_global_var (decl) || DECL_CONTEXT (decl) != fun->decl)
    return false;
  struct ctx { tree decl; bool ok; };
  ctx c = { decl, true };
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (is_gimple_call (stmt))
	  {
	    /* A direct argument is fine; anything else isn't.  */
	    bool other_use = false;
	    for (unsigned i = 0; i < gimple_call_num_args (stmt); ++i)
	      {
		tree arg = gimple_call_arg (stmt, i);
		if (TREE_CODE (arg) == ADDR_EXPR
		    && TREE_OPERAND (arg, 0) == decl)
		  continue;
		if (walk_tree_without_duplicates
		      (&arg, [] (tree *tp, int *, void *d) -> tree
			 {
			   return (TREE_CODE (*tp) == ADDR_EXPR
				   && TREE_OPERAND (*tp, 0) == (tree) d)
				  ? *tp : NULL_TREE;
			 }, decl))
		  other_use = true;
	      }
	    tree lhs = gimple_call_lhs (stmt);
	    if (lhs && TREE_CODE (lhs) != VAR_DECL && TREE_CODE (lhs) != SSA_NAME
		&& walk_tree_without_duplicates
		     (&lhs, [] (tree *tp, int *, void *d) -> tree
			{
			  return (TREE_CODE (*tp) == ADDR_EXPR
				  && TREE_OPERAND (*tp, 0) == (tree) d)
				 ? *tp : NULL_TREE;
			}, decl))
	      other_use = true;
	    if (other_use)
	      return false;
	    continue;
	  }
	if (cg_stores_addr_into_args_field_p (stmt, decl))
	  continue;
	/* A contract check's capture struct field, which its predicate
	   only reads through.  */
	if (gimple_assign_single_p (stmt)
	    && TREE_CODE (gimple_assign_rhs1 (stmt)) == ADDR_EXPR
	    && TREE_OPERAND (gimple_assign_rhs1 (stmt), 0) == decl
	    && TREE_CODE (gimple_assign_lhs (stmt)) == COMPONENT_REF)
	  {
	    tree cond, decls;
	    tree base = TREE_OPERAND (gimple_assign_lhs (stmt), 0);
	    if (VAR_P (base)
		&& contract_check_arg_struct_info (TREE_TYPE (base), &cond,
						   &decls))
	      continue;
	  }
	walk_stmt_load_store_addr_ops
	  (stmt, &c, NULL, NULL,
	   [] (gimple *, tree op, tree, void *d) -> bool
	     {
	       ctx *c = (ctx *) d;
	       if (op == c->decl)
		 c->ok = false;
	       return false;
	     });
	if (!c.ok)
	  return false;
      }
  return true;
}

/* The object identity of the object DECL designates in a contract
   condition at contract check CALL (STRUCT_VAR and DECLS as from
   cg_contract_check_call_p): what a pointer or reference (including
   'this') points to, or a local aggregate itself.  */

static tree
cg_contract_check_object_identity (gcall *call, tree struct_var, tree decls,
				   tree decl)
{
  decl = STRIP_ANY_LOCATION_WRAPPER (decl);
  if (!VAR_P (decl) && TREE_CODE (decl) != PARM_DECL)
    return NULL_TREE;
  tree type = TREE_TYPE (decl);
  if (TYPE_REF_P (type) || POINTER_TYPE_P (type))
    {
      tree val = cg_contract_check_decl_value (call, struct_var, decls, decl);
      /* A const parameter of this function the check doesn't capture
	 ('this', or a lambda's closure, reached through a capture's
	 field): its incoming value.  */
      if (!val && TREE_CODE (decl) == PARM_DECL && TREE_READONLY (decl)
	  && DECL_CONTEXT (decl) == current_function_decl)
	val = ssa_default_def (cfun, decl);
      return val && TREE_CODE (val) == SSA_NAME
	     ? cg_gimple_object_identity (val) : NULL_TREE;
    }
  /* A local aggregate, or one passed by value -- in a constructor's or
     destructor's clone, the clone's own copy of the parameter the
     condition names (the function it is cloned from's).  */
  if (RECORD_OR_UNION_TYPE_P (type))
    {
      if (TREE_CODE (decl) == PARM_DECL
	  && DECL_CONTEXT (decl) != current_function_decl
	  && DECL_CLONED_FUNCTION_P (current_function_decl)
	  && DECL_CLONED_FUNCTION (current_function_decl) == DECL_CONTEXT (decl))
	{
	  tree c = FUNCTION_FIRST_USER_PARM (current_function_decl);
	  tree o = FUNCTION_FIRST_USER_PARM (DECL_CONTEXT (decl));
	  for (; c && o; c = DECL_CHAIN (c), o = DECL_CHAIN (o))
	    if (o == decl)
	      return cg_gimple_object_identity (c);
	  return NULL_TREE;
	}
      return cg_gimple_object_identity (decl);
    }
  return NULL_TREE;
}

/* The memory facts a conveyor or symbolic contract_assert check CALL
   establishes in STATE: the field ranges and nonzero fields its
   condition states, and its named-predicate facts -- the
   contract_assert counterpart of cg_establish_persistent_facts_for_call.
   Its facts about SSA values are cg_assert_fact_scope's.  */

static void
cg_establish_assert_facts (gcall *call, cg_dom_fact_state &state)
{
  tree struct_var, cond, decls;
  tree contract = cg_contract_check_call_p (call, &struct_var, &cond, &decls);
  if (!contract || TREE_CODE (contract) != ASSERTION_STMT
      || cond == NULL_TREE || cond == error_mark_node)
    return;
  tree owner = DECL_ORIGIN (current_function_decl);
  bool conveyor_enabled = (flag_contract_conveyor_proofs_gimple
			   && oa_contract_conveyor_active_public (contract,
								  owner));
  bool symbolic_enabled = (flag_contract_symbolic_proofs_gimple
			   && oa_contract_symbolic_active_public (contract,
								  owner));
  if (!conveyor_enabled && !symbolic_enabled)
    return;

  auto identity_of = [&] (tree decl)
    {
      return cg_contract_check_object_identity (call, struct_var, decls,
						 decl);
    };

  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree c = *conjuncts[i];
      tree pred_fn, arg_decl;
      bool negated;
      if (oa_match_predicate_conjunct (c, &pred_fn, &arg_decl, &negated))
	{
	  if (tree identity = identity_of (arg_decl))
	    state.pred.put (identity, { pred_fn, !negated, conveyor_enabled });
	  continue;
	}

      /* 'x < v.size ()': holds until V changes, as a precondition's
	 does (cg_seed_predicate_self_trust).  */
      tree rel_decl, recv, accessor;
      tree_code rel_code;
      if (oa_match_comparison_against_call (c, &rel_decl, &rel_code, &recv,
					     &accessor,
					     /*allow_symbolic_accessor=*/
					       !conveyor_enabled))
	{
	  tree r = recv;
	  while (r && (TREE_CODE (r) == INDIRECT_REF || CONVERT_EXPR_P (r)
		       || TREE_CODE (r) == NON_LVALUE_EXPR
		       || TREE_CODE (r) == VIEW_CONVERT_EXPR
		       || TREE_CODE (r) == ADDR_EXPR))
	    r = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (r, 0));
	  tree va = cg_contract_check_decl_value (call, struct_var, decls,
						  rel_decl);
	  tree vr = (r && (VAR_P (r) || TREE_CODE (r) == PARM_DECL)
		     ? cg_contract_check_decl_value (call, struct_var, decls, r)
		     : NULL_TREE);
	  if (va && vr && TREE_CODE (va) == SSA_NAME
	      && INTEGRAL_TYPE_P (TREE_TYPE (va)))
	    state.call_rel.put (va, { rel_code, vr, accessor,
				      cg_range_lite_exact (0),
				      conveyor_enabled });
	  continue;
	}

      tree obj, field;
      if (oa_field_nonzero_conjunct_public (c, &obj, &field))
	{
	  obj = STRIP_ANY_LOCATION_WRAPPER (obj);
	  if (TREE_CODE (obj) == INDIRECT_REF)
	    obj = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (obj, 0));
	  if (tree identity = identity_of (obj))
	    {
	      cg_field_fact *existing = state.field.get ({identity, field});
	      cg_field_fact fact = existing ? *existing
				   : cg_field_fact { {}, conveyor_enabled };
	      fact.nonzero = true;
	      state.field.put ({identity, field}, fact);
	    }
	  continue;
	}

      /* A scalar in memory: its nonzero-ness or range, until it's
	 stored to or passed to a call.  */
      tree decl, const_val;
      tree_code code;
      auto memory_scalar_p = [&] (tree d)
	{
	  d = STRIP_ANY_LOCATION_WRAPPER (d);
	  return ((VAR_P (d) || TREE_CODE (d) == PARM_DECL)
		  && !cg_contract_check_decl_value (call, struct_var, decls, d)
		  && cg_scalar_memory_tracking_p (cfun, d));
	};
      tree oa_arg;
      if (is_object_address_call_p (c, &oa_arg))
	{
	  oa_arg = STRIP_ANY_LOCATION_WRAPPER (oa_arg);
	  while (TREE_CODE (oa_arg) == NON_LVALUE_EXPR || CONVERT_EXPR_P (oa_arg))
	    oa_arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (oa_arg, 0));
	  tree fbase;
	  if (tree field = cg_object_address_field (oa_arg, &fbase))
	    {
	      if (tree identity = identity_of (fbase))
		{
		  cg_field_fact *existing = state.field.get ({identity, field});
		  cg_field_fact fact = existing ? *existing
				       : cg_field_fact { {}, conveyor_enabled };
		  fact.object_address = true;
		  state.field.put ({identity, field}, fact);
		}
	    }
	  else if (POINTER_TYPE_P (TREE_TYPE (oa_arg)) && memory_scalar_p (oa_arg))
	    {
	      cg_field_fact *existing
		= state.field.get (cg_scalar_memory_fact_key (oa_arg));
	      cg_field_fact fact = existing ? *existing
				   : cg_field_fact { {}, conveyor_enabled };
	      fact.object_address = true;
	      state.field.put (cg_scalar_memory_fact_key (oa_arg), fact);
	    }
	  continue;
	}
      if (oa_nonzero_conjunct_p (c, &decl))
	{
	  if (memory_scalar_p (decl))
	    {
	      decl = STRIP_ANY_LOCATION_WRAPPER (decl);
	      cg_field_fact *existing
		= state.field.get (cg_scalar_memory_fact_key (decl));
	      cg_field_fact fact = existing ? *existing
				   : cg_field_fact { {}, conveyor_enabled };
	      fact.nonzero = true;
	      state.field.put (cg_scalar_memory_fact_key (decl), fact);
	    }
	  continue;
	}
      if (oa_match_simple_comparison_var_public (c, &decl, &code, &const_val)
	  && TREE_CODE (const_val) == INTEGER_CST)
	{
	  if (memory_scalar_p (decl))
	    {
	      decl = STRIP_ANY_LOCATION_WRAPPER (decl);
	      cg_field_fact *existing
		= state.field.get (cg_scalar_memory_fact_key (decl));
	      cg_field_fact fact = existing ? *existing
				   : cg_field_fact { {}, conveyor_enabled };
	      cg_tighten_range_bound (fact.range, code,
				      wi::to_widest (const_val));
	      state.field.put (cg_scalar_memory_fact_key (decl), fact);
	    }
	  continue;
	}

      tree ptr_expr;
      if (oa_match_field_range_comparison (c, &field, &ptr_expr, &code,
					   &const_val)
	  && TREE_CODE (const_val) == REAL_CST)
	{
	  frange r;
	  ptr_expr = oa_strip_symbolic_ptr_expr_public (ptr_expr);
	  if (cg_frange_for_comparison (TREE_TYPE (field), code, const_val, &r))
	    if (tree identity = identity_of (ptr_expr))
	      cg_add_float_field_fact (state, {identity, field}, r,
				       conveyor_enabled);
	  continue;
	}
      if (oa_match_field_range_comparison (c, &field, &ptr_expr, &code,
					   &const_val)
	  && TREE_CODE (const_val) == INTEGER_CST)
	{
	  ptr_expr = oa_strip_symbolic_ptr_expr_public (ptr_expr);
	  if (tree identity = identity_of (ptr_expr))
	    {
	      cg_field_fact *existing = state.field.get ({identity, field});
	      cg_field_fact fact = existing ? *existing
				   : cg_field_fact { {}, conveyor_enabled };
	      cg_tighten_range_bound (fact.range, code,
				      wi::to_widest (const_val));
	      state.field.put ({identity, field}, fact);
	    }
	}
    }

  /* 'v.size () > 3': the accessor's result on V, until V may change
     (cg_call_rel_invalidate_receiver), as a precondition's self-trust
     seeds it.  */
  auto_vec<cg_call_group_lite> call_groups;
  cg_collect_call_range_groups (cond, &call_groups,
				/*allow_symbolic_accessor=*/!conveyor_enabled);
  for (unsigned g = 0; g < call_groups.length (); ++g)
    {
      tree r = call_groups[g].receiver_expr;
      while (r && (TREE_CODE (r) == INDIRECT_REF || CONVERT_EXPR_P (r)
		   || TREE_CODE (r) == NON_LVALUE_EXPR
		   || TREE_CODE (r) == VIEW_CONVERT_EXPR
		   || TREE_CODE (r) == ADDR_EXPR))
	r = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (r, 0));
      tree identity = r ? identity_of (r) : NULL_TREE;
      if (!identity)
	continue;
      {
	cg_field_fact *existing
	  = state.call.get ({identity, call_groups[g].callee});
	cg_field_fact fact = existing ? *existing
			     : cg_field_fact { {}, conveyor_enabled };
	cg_range_lite &r = fact.range;
	const cg_range_lite &add = call_groups[g].range;
	if (add.has_lo && (!r.has_lo || add.lo > r.lo))
	  r.has_lo = true, r.lo = add.lo;
	if (add.has_hi && (!r.has_hi || add.hi < r.hi))
	  r.has_hi = true, r.hi = add.hi;
	state.call.put ({identity, call_groups[g].callee}, fact);
      }
    }
}

/* What STATE says, at contract_assert or postcondition check CALL, about
   each of its conjuncts that is about memory -- before an assert
   establishes them (cg_establish_assert_facts).  */

static bool
cg_compute_assert_memory_verdicts (gcall *call, cg_dom_fact_state &state,
				   cg_assert_memory_verdicts *v)
{
  tree struct_var, cond, decls;
  tree contract = cg_contract_check_call_p (call, &struct_var, &cond, &decls);
  if (!contract
      || (TREE_CODE (contract) != ASSERTION_STMT && !POSTCONDITION_P (contract))
      || cond == NULL_TREE || cond == error_mark_node)
    return false;

  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  memset (v, 0, sizeof *v);
  for (unsigned i = 0;
       i < conjuncts.length () && i < cg_assert_memory_verdicts::max_conjuncts;
       ++i)
    {
      tree c = *conjuncts[i];
      cg_field_fact *fact = NULL;
      bool nonzero_conj = false;
      {
	tree oa_arg;
	if (is_object_address_call_p (c, &oa_arg))
	  {
	    /* A pointer field, or a lambda's capture of a pointer or a
	       reference: what the walk knows the field holds.  */
	    tree fbase;
	    if (tree field = cg_object_address_field (oa_arg, &fbase))
	      {
		tree identity = cg_contract_check_object_identity
				  (call, struct_var, decls, fbase);
		cg_field_fact *f
		  = identity ? state.field.get ({identity, field}) : NULL;
		for (int flavor = 0; flavor < 2; ++flavor)
		  v->verdict[i][flavor]
		    = (f && f->object_address
		       && (flavor == 1 || f->conveyor_established)
		       ? CG_VERDICT_TRUE : CG_VERDICT_UNKNOWN);
		continue;
	      }
	    oa_arg = STRIP_ANY_LOCATION_WRAPPER (oa_arg);
	    while (TREE_CODE (oa_arg) == NON_LVALUE_EXPR || CONVERT_EXPR_P (oa_arg))
	      oa_arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (oa_arg, 0));
	    if ((VAR_P (oa_arg) || TREE_CODE (oa_arg) == PARM_DECL)
		&& POINTER_TYPE_P (TREE_TYPE (oa_arg))
		&& !cg_contract_check_decl_value (call, struct_var, decls, oa_arg)
		&& cg_scalar_memory_tracking_p (cfun, oa_arg))
	      {
		cg_field_fact *f
		  = state.field.get (cg_scalar_memory_fact_key (oa_arg));
		for (int flavor = 0; flavor < 2; ++flavor)
		  v->verdict[i][flavor]
		    = (f && f->object_address
		       && (flavor == 1 || f->conveyor_established)
		       ? CG_VERDICT_TRUE : CG_VERDICT_UNKNOWN);
	      }
	    continue;
	  }
      }
      tree_code code = ERROR_MARK;
      widest_int val = 0;
      tree pred_fn, arg_decl, obj, field, decl, ptr_expr, const_val;
      bool negated;
      /* A comparison with an accessor's result ('x < v.size ()'),
	 decided by a fact relating them in STATE, e.g. a
	 precondition's, unless the receiver has changed since.  */
      tree recv, accessor;
      bool conveyor_shape = false;
      if (cg_array_bounds
	  && ((conveyor_shape
		 = oa_match_comparison_against_call (c, &decl, &code, &recv,
						     &accessor,
						     /*allow_symbolic_accessor=*/
						       false))
	      || oa_match_comparison_against_call (c, &decl, &code, &recv,
						   &accessor,
						   /*allow_symbolic_accessor=*/
						     true)))
	{
	  tree r = recv;
	  while (r && (TREE_CODE (r) == INDIRECT_REF || CONVERT_EXPR_P (r)
		       || TREE_CODE (r) == NON_LVALUE_EXPR
		       || TREE_CODE (r) == VIEW_CONVERT_EXPR
		       || TREE_CODE (r) == ADDR_EXPR))
	    r = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (r, 0));
	  tree va = cg_contract_check_decl_value (call, struct_var, decls, decl);
	  tree vr = (r && (VAR_P (r) || TREE_CODE (r) == PARM_DECL)
		     ? cg_contract_check_decl_value (call, struct_var, decls, r)
		     : NULL_TREE);
	  if (!va || !vr || !INTEGRAL_TYPE_P (TREE_TYPE (va)))
	    continue;
	  for (int flavor = 0; flavor < 2; ++flavor)
	    {
	      bool require_conveyor = flavor == 0;
	      v->verdict[i][flavor] = CG_VERDICT_UNKNOWN;
	      if (require_conveyor && !conveyor_shape)
		continue;
	      hash_map<tree, cg_call_rel_fact> empty_call_rel_cache;
	      tree_code fact_code;
	      tree fact_receiver, fact_callee;
	      cg_range_lite fact_offset;
	      bool fact_conveyor;
	      if (!cg_get_call_relational (va, state.call_rel,
					   empty_call_rel_cache,
					   *cg_array_bounds->established_range,
					   *cg_array_bounds->scalar_range_cache,
					   cg_array_bounds->ranger,
					   require_conveyor, !require_conveyor,
					   &fact_code, &fact_receiver,
					   &fact_callee, &fact_offset,
					   &fact_conveyor)
		  || fact_callee != accessor
		  || (cg_gimple_object_identity (fact_receiver)
		      != cg_gimple_object_identity (vr))
		  || (require_conveyor && !fact_conveyor))
		continue;
	      if (oa_relational_code_implies (fact_code, code)
		  && cg_offset_compatible_with_code (fact_offset, code))
		v->verdict[i][flavor] = CG_VERDICT_TRUE;
	      else if (cg_call_relational_contradicts_p (fact_code, fact_offset,
							 code))
		{
		  v->verdict[i][flavor] = CG_VERDICT_FALSE;
		  snprintf (v->text[i], sizeof v->text[i],
			    "%s %s the result of %s",
			    (DECL_NAME (decl)
			     ? IDENTIFIER_POINTER (DECL_NAME (decl))
			     : "the value"),
			    op_symbol_code (fact_code),
			    IDENTIFIER_POINTER (DECL_NAME (accessor)));
		}
	    }
	  continue;
	}
      if (oa_match_predicate_conjunct (c, &pred_fn, &arg_decl, &negated))
	{
	  tree identity = cg_contract_check_object_identity (call, struct_var,
							     decls, arg_decl);
	  if (!identity)
	    continue;
	  for (int flavor = 0; flavor < 2; ++flavor)
	    {
	      cg_pred_fact *p = state.pred.get (identity);
	      if (!p || p->pred_fn != pred_fn
		  || (flavor == 0 && !p->conveyor_established))
		v->verdict[i][flavor] = CG_VERDICT_UNKNOWN;
	      else
		v->verdict[i][flavor] = (p->polarity == !negated
					 ? CG_VERDICT_TRUE : CG_VERDICT_FALSE);
	    }
	  snprintf (v->text[i], sizeof v->text[i], "%s",
		    negated ? "the predicate holds" : "the predicate doesn't hold");
	  continue;
	}
      if (oa_field_nonzero_conjunct_public (c, &obj, &field))
	{
	  obj = STRIP_ANY_LOCATION_WRAPPER (obj);
	  if (TREE_CODE (obj) == INDIRECT_REF)
	    obj = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (obj, 0));
	  tree identity = cg_contract_check_object_identity (call, struct_var,
							     decls, obj);
	  if (!identity)
	    continue;
	  fact = state.field.get ({identity, field});
	  nonzero_conj = true;
	}
      else if (oa_nonzero_conjunct_p (c, &decl)
	       && !cg_contract_check_decl_value (call, struct_var, decls,
						 STRIP_ANY_LOCATION_WRAPPER (decl))
	       && cg_scalar_memory_tracking_p (cfun,
					       STRIP_ANY_LOCATION_WRAPPER (decl)))
	{
	  fact = state.field.get
	    (cg_scalar_memory_fact_key (STRIP_ANY_LOCATION_WRAPPER (decl)));
	  nonzero_conj = true;
	}
      else if (oa_match_simple_comparison_var_public (c, &decl, &code,
						      &const_val)
	       && TREE_CODE (const_val) == INTEGER_CST
	       && !cg_contract_check_decl_value (call, struct_var, decls,
						 STRIP_ANY_LOCATION_WRAPPER (decl))
	       && cg_scalar_memory_tracking_p (cfun,
					       STRIP_ANY_LOCATION_WRAPPER (decl)))
	{
	  fact = state.field.get
	    (cg_scalar_memory_fact_key (STRIP_ANY_LOCATION_WRAPPER (decl)));
	  val = wi::to_widest (const_val);
	}
      else if (oa_match_field_range_comparison (c, &field, &ptr_expr, &code,
						&const_val)
	       && TREE_CODE (const_val) == REAL_CST)
	{
	  ptr_expr = oa_strip_symbolic_ptr_expr_public (ptr_expr);
	  tree identity = cg_contract_check_object_identity (call, struct_var,
							     decls, ptr_expr);
	  if (!identity)
	    continue;
	  fact = state.field.get ({identity, field});
	  for (int flavor = 0; flavor < 2; ++flavor)
	    v->verdict[i][flavor]
	      = (fact && fact->has_float
		 && (flavor == 1 || fact->conveyor_established)
		 ? cg_frange_comparison_verdict (fact->float_range, code,
						 const_val, v->text[i],
						 sizeof v->text[i])
		 : CG_VERDICT_UNKNOWN);
	  continue;
	}
      else if (oa_match_field_range_comparison (c, &field, &ptr_expr, &code,
						&const_val)
	       && TREE_CODE (const_val) == INTEGER_CST)
	{
	  ptr_expr = oa_strip_symbolic_ptr_expr_public (ptr_expr);
	  tree identity = cg_contract_check_object_identity (call, struct_var,
							     decls, ptr_expr);
	  if (!identity)
	    continue;
	  fact = state.field.get ({identity, field});
	  val = wi::to_widest (const_val);
	}
      else
	continue;

      for (int flavor = 0; flavor < 2; ++flavor)
	{
	  cg_verdict r = CG_VERDICT_UNKNOWN;
	  if (fact && (flavor == 1 || fact->conveyor_established))
	    {
	      if (nonzero_conj)
		r = fact->nonzero ? CG_VERDICT_TRUE
				  : cg_range_nonzero_verdict (fact->range);
	      else
		r = cg_range_comparison_verdict (fact->range, code, val);
	    }
	  v->verdict[i][flavor] = r;
	}
      if (fact)
	cg_range_lite_text (fact->range, v->text[i], sizeof v->text[i]);
    }
  return true;
}

/* Record the verdicts in VERDICTS, for the check pass.  */

static void
cg_record_assert_memory_verdicts (gcall *call, cg_dom_fact_state &state,
				  cg_assert_memory_verdict_map *verdicts)
{
  if (!verdicts)
    return;
  cg_assert_memory_verdicts *v = XNEW (cg_assert_memory_verdicts);
  if (!cg_compute_assert_memory_verdicts (call, state, v))
    {
      XDELETE (v);
      return;
    }
  if (cg_assert_memory_verdicts **old = verdicts->get (call))
    XDELETE (*old);
  verdicts->put (call, v);
}

/* True if contract_assert check CALL, checked (not never_proven), has a
   conjunct about memory that STATE shows to be false: like the AST
   engine, such an assert establishes nothing.  */

static bool
cg_assert_memory_conjunct_false_p (gcall *call, cg_dom_fact_state &state)
{
  cg_assert_memory_verdicts v;
  if (!cg_compute_assert_memory_verdicts (call, state, &v))
    return false;
  tree struct_var, cond, decls;
  tree contract = cg_contract_check_call_p (call, &struct_var, &cond, &decls);
  bool never_proven, cf, cs, sf, ss;
  if (!oa_contract_self_check_mode_public (contract,
					   DECL_ORIGIN (current_function_decl),
					   &never_proven, &cf, &cs, &sf, &ss)
      || never_proven)
    return false;
  for (unsigned i = 0; i < cg_assert_memory_verdicts::max_conjuncts; ++i)
    if (v.verdict[i][0] == CG_VERDICT_FALSE
	|| v.verdict[i][1] == CG_VERDICT_FALSE)
      return true;
  return false;
}

/* STMT loads a field, or a scalar in memory, into an SSA name: give the
   loaded value the facts STATE holds for it -- its range, in SCALAR_RANGE_CACHE, and
   whether it is nonzero, in ESTABLISHED_NZ.  An SSA value never
   changes, so the facts hold wherever it is used.  */

static void
cg_record_field_load_facts (gimple *stmt, cg_dom_fact_state &state,
			    hash_map<tree, cg_range_lite> &scalar_range_cache,
			    hash_map<tree, cg_fact> &established_nz,
			    hash_map<tree, frange> *established_frange,
			    hash_map<tree, cg_fact> *established_oa)
{
  if (!gimple_assign_single_p (stmt))
    return;
  tree lhs = gimple_assign_lhs (stmt);
  tree rhs = gimple_assign_rhs1 (stmt);
  if (TREE_CODE (lhs) == SSA_NAME && POINTER_TYPE_P (TREE_TYPE (lhs))
      && established_oa)
    {
      cg_field_fact *fact = NULL;
      if (VAR_P (rhs) || TREE_CODE (rhs) == PARM_DECL)
	fact = state.field.get (cg_scalar_memory_fact_key (rhs));
      else if (TREE_CODE (rhs) == COMPONENT_REF)
	{
	  tree obj = TREE_OPERAND (rhs, 0);
	  tree obj_expr = (TREE_CODE (obj) == MEM_REF
			   && integer_zerop (TREE_OPERAND (obj, 1))
			   ? TREE_OPERAND (obj, 0) : obj);
	  if (tree identity = cg_gimple_object_identity (obj_expr))
	    fact = state.field.get ({identity, TREE_OPERAND (rhs, 1)});
	}
      if (fact && fact->object_address)
	established_oa->put (lhs, { fact->conveyor_established });
      return;
    }
  if (TREE_CODE (lhs) != SSA_NAME
      || (!INTEGRAL_TYPE_P (TREE_TYPE (lhs))
	  && !SCALAR_FLOAT_TYPE_P (TREE_TYPE (lhs))))
    return;
  cg_field_fact *fact;
  std::pair<tree, tree> rkey;
  if (VAR_P (rhs) || TREE_CODE (rhs) == PARM_DECL)
    fact = state.field.get (cg_scalar_memory_fact_key (rhs));
  else if (cg_referent_key (rhs, &rkey))
    {
      fact = state.field.get (rkey);
    }
  else if (TREE_CODE (rhs) == COMPONENT_REF)
    {
      tree field = TREE_OPERAND (rhs, 1);
      tree obj = TREE_OPERAND (rhs, 0);
      tree obj_expr = (TREE_CODE (obj) == MEM_REF
		       && integer_zerop (TREE_OPERAND (obj, 1))
		       ? TREE_OPERAND (obj, 0) : obj);
      tree identity = cg_gimple_object_identity (obj_expr);
      if (!identity)
	return;
      fact = state.field.get ({identity, field});
    }
  else
    return;
  if (!fact)
    return;
  if (fact->has_float && established_frange)
    {
      bool existed;
      frange &slot = established_frange->get_or_insert (lhs, &existed);
      if (existed)
	slot.intersect (fact->float_range);
      else
	slot = fact->float_range;
      return;
    }
  if (fact->nonzero)
    established_nz.put (lhs, { fact->conveyor_established });
  if (fact->range.has_lo || fact->range.has_hi)
    {
      cg_range_lite merged = fact->range;
      if (cg_range_lite *old = scalar_range_cache.get (lhs))
	{
	  if (old->has_lo && (!merged.has_lo || old->lo > merged.lo))
	    merged.has_lo = true, merged.lo = old->lo;
	  if (old->has_hi && (!merged.has_hi || old->hi < merged.hi))
	    merged.has_hi = true, merged.hi = old->hi;
	}
      scalar_range_cache.put (lhs, merged);
    }
}

static bool cg_value_range_at (tree, gimple *, hash_map<tree, cg_range_lite> &,
			       hash_map<tree, cg_range_lite> &, gimple_ranger *,
			       bool, cg_range_lite *);

/* The floating-point range of VAL at STMT in R: the ranger's, narrowed
   by what is established for it (ESTABLISHED_FRANGE), and for an
   arithmetic result folded from its operands' ranges -- the ranger alone
   knows nothing of a precondition's or a field's facts about them.  */

static bool
cg_frange_of (tree val, gimple *stmt, gimple_ranger *ranger,
	      hash_map<tree, frange> *established_frange, frange &r,
	      int depth)
{
  tree type = TREE_TYPE (val);
  if (TREE_CODE (val) == REAL_CST)
    {
      r = frange (val, val);
      if (!real_isnan (TREE_REAL_CST_PTR (val)))
	r.clear_nan ();
      return true;
    }
  if (TREE_CODE (val) != SSA_NAME || !SCALAR_FLOAT_TYPE_P (type))
    return false;
  r.set_varying (type);
  frange q (type);
  if (ranger->range_of_expr (q, val, stmt) && !q.undefined_p ())
    r = q;
  if (established_frange)
    if (frange *known = established_frange->get (val))
      r.intersect (*known);
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (depth < 6 && def && is_gimple_assign (def))
    {
      tree_code code = gimple_assign_rhs_code (def);
      if (code == PLUS_EXPR || code == MINUS_EXPR || code == MULT_EXPR
	  || code == RDIV_EXPR)
	{
	  frange a (type), b (type);
	  range_op_handler handler (code);
	  if (handler
	      && cg_frange_of (gimple_assign_rhs1 (def), def, ranger,
			       established_frange, a, depth + 1)
	      && cg_frange_of (gimple_assign_rhs2 (def), def, ranger,
			       established_frange, b, depth + 1))
	    {
	      frange res (type);
	      if (handler.fold_range (res, type, a, b) && !res.undefined_p ())
		r.intersect (res);
	    }
	}
      else if (code == SSA_NAME || code == NOP_EXPR)
	{
	  frange a (type);
	  if (cg_frange_of (gimple_assign_rhs1 (def), def, ranger,
			    established_frange, a, depth + 1)
	      && types_compatible_p (TREE_TYPE (gimple_assign_rhs1 (def)), type))
	    r.intersect (a);
	}
    }
  return !r.undefined_p ();
}

/* STMT stores an integral value to a field or a scalar in memory: the
   location now holds that value's range and nonzero-ness, which a
   later postcondition or load can rely on -- like the AST engine, which
   establishes a field's range from what is written to it.  Runs after
   the store has ended the location's old facts.  */

static void
cg_record_write_facts (gimple *stmt, cg_dom_fact_state &state,
		       hash_map<tree, cg_range_lite> &established_range,
		       hash_map<tree, cg_range_lite> &scalar_range_cache,
		       gimple_ranger *ranger,
		       hash_map<tree, cg_fact> &established_nz,
		       hash_map<tree, frange> *established_frange,
		       hash_map<tree, cg_fact> *established_oa)
{
  if (!gimple_assign_single_p (stmt) || !ranger)
    return;
  tree lhs = gimple_assign_lhs (stmt);
  tree rhs = gimple_assign_rhs1 (stmt);

  /* A pointer in memory, or a pointer field, now holds an object
     address if what is stored is one.  */
  if (POINTER_TYPE_P (TREE_TYPE (lhs)) && TREE_CODE (lhs) == COMPONENT_REF)
    {
      tree obj = TREE_OPERAND (lhs, 0);
      tree obj_expr = (TREE_CODE (obj) == MEM_REF
		       && integer_zerop (TREE_OPERAND (obj, 1))
		       ? TREE_OPERAND (obj, 0) : obj);
      tree identity = cg_gimple_object_identity (obj_expr);
      bool oa = TREE_CODE (rhs) == ADDR_EXPR;
      if (!oa && TREE_CODE (rhs) == SSA_NAME && established_oa)
	{
	  hash_set<tree> in_progress;
	  oa = cg_provable_object_address_p (rhs, *established_oa,
					     /*require_conveyor=*/true,
					     in_progress);
	}
      if (identity && oa)
	{
	  std::pair<tree, tree> fkey = { identity, TREE_OPERAND (lhs, 1) };
	  cg_field_fact fact = { {}, /*conveyor_established=*/true };
	  fact.object_address = true;
	  state.field.put (fkey, fact);
	}
      return;
    }
  if (POINTER_TYPE_P (TREE_TYPE (lhs))
      && (VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL)
      && cg_scalar_memory_tracking_p (cfun, lhs))
    {
      bool oa = TREE_CODE (rhs) == ADDR_EXPR;
      if (!oa && TREE_CODE (rhs) == SSA_NAME && established_oa)
	{
	  hash_set<tree> in_progress;
	  oa = cg_provable_object_address_p (rhs, *established_oa,
					     /*require_conveyor=*/true,
					     in_progress);
	}
      if (oa)
	{
	  cg_field_fact fact = { {}, /*conveyor_established=*/true };
	  fact.object_address = true;
	  state.field.put (cg_scalar_memory_fact_key (lhs), fact);
	}
      return;
    }
  bool is_float = SCALAR_FLOAT_TYPE_P (TREE_TYPE (lhs));
  if ((!INTEGRAL_TYPE_P (TREE_TYPE (lhs)) && !is_float)
      || (TREE_CODE (rhs) != SSA_NAME && TREE_CODE (rhs) != INTEGER_CST
	  && TREE_CODE (rhs) != REAL_CST))
    return;

  std::pair<tree, tree> key;
  if (TREE_CODE (lhs) == COMPONENT_REF)
    {
      tree obj = TREE_OPERAND (lhs, 0);
      tree obj_expr = (TREE_CODE (obj) == MEM_REF
		       && integer_zerop (TREE_OPERAND (obj, 1))
		       ? TREE_OPERAND (obj, 0) : obj);
      tree identity = cg_gimple_object_identity (obj_expr);
      if (!identity)
	return;
      key = { identity, TREE_OPERAND (lhs, 1) };
    }
  else if ((VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL)
	   && cg_scalar_memory_tracking_p (cfun, lhs))
    key = cg_scalar_memory_fact_key (lhs);
  else if (!cg_referent_key (lhs, &key))
    return;

  cg_field_fact fact = { {}, /*conveyor_established=*/true };
  if (is_float)
    {
      frange r (TREE_TYPE (lhs));
      if (!cg_frange_of (rhs, stmt, ranger, established_frange, r)
	  || r.varying_p ())
	return;
      fact.has_float = true;
      fact.float_range = r;
      state.field.put (key, fact);
      return;
    }
  if (TREE_CODE (rhs) == INTEGER_CST)
    {
      fact.range.has_lo = fact.range.has_hi = true;
      fact.range.lo = fact.range.hi = wi::to_widest (rhs);
    }
  else
    {
      cg_value_range_at (rhs, stmt, established_range, scalar_range_cache,
			 ranger, /*require_conveyor=*/true, &fact.range);
      hash_set<tree> in_progress;
      fact.nonzero = cg_provable_nonzero_p (rhs, established_nz,
					    /*require_conveyor=*/true,
					    in_progress);
    }
  if (fact.nonzero || fact.range.has_lo || fact.range.has_hi)
    state.field.put (key, fact);
}

/* The forward dataflow walk itself.

   An earlier design used a dom_walker subclass, computing each block's
   own inherited state purely from its immediate dominator's own output
   -- rejected after direct testing confirmed this is unsound, not just
   imprecise: for an ordinary if/else with no early return, the join
   block is a *sibling* of both arms in the dominator tree (all three
   are direct children of the header), never a descendant of either, so
   a dominator-tree walk gives no ordering guarantee between an arm and
   the join at all. A fact invalidated on only one arm silently survived
   past the join, confirmed via a real compile ('f.open(); if (cond)
   unrelated (&f); f.read();' -- pre<>(is_opened(this)) -- wrongly
   raised no diagnostic at all).

   A second draft replaced this with a single reverse-postorder (RPO)
   forward pass, merging (agreement-based AND) only over predecessors
   already processed in RPO order and treating an unprocessed
   predecessor (necessarily a loop back edge, the only kind of edge RPO
   doesn't order first) as contributing nothing. Also rejected, by the
   same review discipline this whole plan uses throughout: a single
   pass never revisits a loop header after its own body has actually
   been processed, so an invalidation that happens on *every* real
   execution path through a loop body is never observed by code after
   the loop -- the same class of unsoundness as the dom-walker bug,
   just relocated from "one arm of an if" to "inside any loop".

   The actual fix: genuine fixed-point iteration over the RPO order,
   mirroring tree-ssa-pre.cc's own identical shape (that file's own
   'do { ... changed = ...; } while (changed);' around the same
   pre_and_rev_post_order_compute call) -- monotonic (agreement-merge
   only ever removes facts; a call's own postcondition establishment is
   a deterministic, IN-state-independent effect, re-derived identically
   every pass), so repeatedly recomputing every block until nothing
   changes is guaranteed to terminate, bounded by the total number of
   distinct fact entries that could ever be removed.  */

/* hash_map has no usable copy-assignment operator (its own class
   comment: "copy-constructible but not assignable", backed by
   hash_table's own private, unimplemented operator=) -- spelled out
   explicitly instead, exactly mirroring contracts.cc's own oa_env::
   assign (same comment, same empty()-then-loop-put() shape).  An
   earlier draft used 'dst = src;' directly and would not have
   compiled.  */

static void
cg_dom_fact_state_assign (cg_dom_fact_state *dst, const cg_dom_fact_state &src)
{
  dst->pred.empty ();
  for (auto it : src.pred)
    dst->pred.put (it.first, it.second);
  dst->field.empty ();
  for (auto it : src.field)
    dst->field.put (it.first, it.second);
  dst->field_alias.empty ();
  for (auto it : src.field_alias)
    dst->field_alias.put (it.first, it.second);
  dst->call.empty ();
  for (auto it : src.call)
    dst->call.put (it.first, it.second);
  dst->rel.empty ();
  for (auto it : src.rel)
    dst->rel.put (it.first, it.second);
  dst->call_rel.empty ();
  for (auto it : src.call_rel)
    dst->call_rel.put (it.first, it.second);
  dst->call_call.empty ();
  for (auto it : src.call_call)
    dst->call_call.put (it.first, it.second);
}

/* Agreement-based, mirroring contracts.cc's own oa_env::predicate_
   fact_merge_with/field_alias_merge_with (never a union -- an entry
   survives only if both sides have it and fully agree).  */

static void
cg_dom_fact_state_merge (cg_dom_fact_state *dst, cg_dom_fact_state &src,
			  bool unite_fields = false)
{
  auto_vec<tree> pred_remove;
  for (auto it : dst->pred)
    {
      const cg_pred_fact *ov = src.pred.get (it.first);
      if (!ov || ov->pred_fn != it.second.pred_fn
	  || ov->polarity != it.second.polarity
	  || ov->conveyor_established != it.second.conveyor_established)
	pred_remove.safe_push (it.first);
    }
  for (tree t : pred_remove)
    dst->pred.remove (t);

  auto_vec<std::pair<tree, tree>> field_remove;
  /* At a join no loop runs back into (UNITE_FIELDS), a field's facts are
     what either path's say: the union of its ranges ('if (v > 100.0) v =
     100.0;'), which can't keep growing there.  */
  if (unite_fields)
    for (auto it : dst->field)
      {
	const cg_field_fact *ov = src.field.get (it.first);
	if (!ov)
	  continue;
	cg_field_fact &f = it.second;
	f.range.has_lo = f.range.has_lo && ov->range.has_lo;
	f.range.has_hi = f.range.has_hi && ov->range.has_hi;
	if (f.range.has_lo && ov->range.lo < f.range.lo)
	  f.range.lo = ov->range.lo;
	if (f.range.has_hi && ov->range.hi > f.range.hi)
	  f.range.hi = ov->range.hi;
	if (f.has_float && ov->has_float)
	  f.float_range.union_ (ov->float_range);
	else
	  f.has_float = false;
	f.nonzero = f.nonzero && ov->nonzero;
	f.object_address = f.object_address && ov->object_address;
	f.conveyor_established
	  = f.conveyor_established && ov->conveyor_established;
	dst->field.put (it.first, f);
      }
  for (auto it : dst->field)
    {
      const cg_field_fact *ov = src.field.get (it.first);
      if (unite_fields)
	{
	  if (!ov)
	    field_remove.safe_push (it.first);
	  continue;
	}
      if (!ov || ov->range.has_lo != it.second.range.has_lo
	  || ov->range.has_hi != it.second.range.has_hi
	  || (it.second.range.has_lo && ov->range.lo != it.second.range.lo)
	  || (it.second.range.has_hi && ov->range.hi != it.second.range.hi)
	  || ov->conveyor_established != it.second.conveyor_established
	  || ov->nonzero != it.second.nonzero
	  || !cg_field_fact_float_equal (*ov, it.second)
	  || ov->object_address != it.second.object_address)
	field_remove.safe_push (it.first);
    }
  for (auto k : field_remove)
    dst->field.remove (k);

  auto_vec<std::pair<tree, tree>> field_alias_remove;
  for (auto it : dst->field_alias)
    {
      tree *ov = src.field_alias.get (it.first);
      if (!ov || *ov != it.second)
	field_alias_remove.safe_push (it.first);
    }
  for (auto k : field_alias_remove)
    dst->field_alias.remove (k);

  auto_vec<std::pair<tree, tree>> call_remove;
  for (auto it : dst->call)
    {
      const cg_field_fact *ov = src.call.get (it.first);
      if (!ov || ov->range.has_lo != it.second.range.has_lo
	  || ov->range.has_hi != it.second.range.has_hi
	  || (it.second.range.has_lo && ov->range.lo != it.second.range.lo)
	  || (it.second.range.has_hi && ov->range.hi != it.second.range.hi)
	  || ov->conveyor_established != it.second.conveyor_established)
	call_remove.safe_push (it.first);
    }
  for (auto k : call_remove)
    dst->call.remove (k);

  auto_vec<tree> rel_remove;
  for (auto it : dst->rel)
    {
      const cg_rel_fact *ov = src.rel.get (it.first);
      if (!ov || ov->code != it.second.code || ov->rhs != it.second.rhs
	  || !cg_range_lite_equal (ov->offset, it.second.offset)
	  || ov->conveyor_established != it.second.conveyor_established)
	rel_remove.safe_push (it.first);
    }
  for (auto t : rel_remove)
    dst->rel.remove (t);

  auto_vec<tree> call_rel_remove;
  for (auto it : dst->call_rel)
    {
      const cg_call_rel_fact *ov = src.call_rel.get (it.first);
      if (!ov || ov->code != it.second.code
	  || ov->rhs_receiver != it.second.rhs_receiver
	  || ov->rhs_callee != it.second.rhs_callee
	  || !cg_range_lite_equal (ov->offset, it.second.offset)
	  || ov->conveyor_established != it.second.conveyor_established)
	call_rel_remove.safe_push (it.first);
    }
  for (auto t : call_rel_remove)
    dst->call_rel.remove (t);

  auto_vec<std::pair<tree, tree>> call_call_remove;
  for (auto it : dst->call_call)
    {
      const cg_call_call_rel_fact *ov = src.call_call.get (it.first);
      if (!ov || ov->code != it.second.code
	  || ov->rhs_receiver != it.second.rhs_receiver
	  || ov->rhs_callee != it.second.rhs_callee
	  || ov->conveyor_established != it.second.conveyor_established)
	call_call_remove.safe_push (it.first);
    }
  for (auto t : call_call_remove)
    dst->call_call.remove (t);
}

/* The fixed-point loop's own "has anything changed since the last time
   we computed this block's own OUT state" check -- same size, then
   every one of A's own keys found equal in B (which, combined with the
   size check, also rules out B having some extra key A doesn't).  */

static bool
cg_dom_fact_state_equal (cg_dom_fact_state &a, cg_dom_fact_state &b)
{
  if (a.pred.elements () != b.pred.elements ()
      || a.field.elements () != b.field.elements ()
      || a.field_alias.elements () != b.field_alias.elements ()
      || a.call.elements () != b.call.elements ()
      || a.rel.elements () != b.rel.elements ()
      || a.call_rel.elements () != b.call_rel.elements ()
      || a.call_call.elements () != b.call_call.elements ())
    return false;
  for (auto it : a.pred)
    {
      const cg_pred_fact *ov = b.pred.get (it.first);
      if (!ov || ov->pred_fn != it.second.pred_fn
	  || ov->polarity != it.second.polarity
	  || ov->conveyor_established != it.second.conveyor_established)
	return false;
    }
  for (auto it : a.field)
    {
      const cg_field_fact *ov = b.field.get (it.first);
      if (!ov || ov->range.has_lo != it.second.range.has_lo
	  || ov->range.has_hi != it.second.range.has_hi
	  || (it.second.range.has_lo && ov->range.lo != it.second.range.lo)
	  || (it.second.range.has_hi && ov->range.hi != it.second.range.hi)
	  || ov->conveyor_established != it.second.conveyor_established
	  || ov->nonzero != it.second.nonzero
	  || !cg_field_fact_float_equal (*ov, it.second)
	  || ov->object_address != it.second.object_address)
	return false;
    }
  for (auto it : a.field_alias)
    {
      tree *ov = b.field_alias.get (it.first);
      if (!ov || *ov != it.second)
	return false;
    }
  for (auto it : a.call)
    {
      const cg_field_fact *ov = b.call.get (it.first);
      if (!ov || ov->range.has_lo != it.second.range.has_lo
	  || ov->range.has_hi != it.second.range.has_hi
	  || (it.second.range.has_lo && ov->range.lo != it.second.range.lo)
	  || (it.second.range.has_hi && ov->range.hi != it.second.range.hi)
	  || ov->conveyor_established != it.second.conveyor_established)
	return false;
    }
  for (auto it : a.rel)
    {
      const cg_rel_fact *ov = b.rel.get (it.first);
      if (!ov || ov->code != it.second.code || ov->rhs != it.second.rhs
	  || !cg_range_lite_equal (ov->offset, it.second.offset)
	  || ov->conveyor_established != it.second.conveyor_established)
	return false;
    }
  for (auto it : a.call_rel)
    {
      const cg_call_rel_fact *ov = b.call_rel.get (it.first);
      if (!ov || ov->code != it.second.code
	  || ov->rhs_receiver != it.second.rhs_receiver
	  || ov->rhs_callee != it.second.rhs_callee
	  || !cg_range_lite_equal (ov->offset, it.second.offset)
	  || ov->conveyor_established != it.second.conveyor_established)
	return false;
    }
  for (auto it : a.call_call)
    {
      const cg_call_call_rel_fact *ov = b.call_call.get (it.first);
      if (!ov || ov->code != it.second.code
	  || ov->rhs_receiver != it.second.rhs_receiver
	  || ov->rhs_callee != it.second.rhs_callee
	  || ov->conveyor_established != it.second.conveyor_established)
	return false;
    }
  return true;
}

/* New infrastructure, not a mirror of an existing mechanism: unlike
   every other fact in this file (self-trust from a declared contract,
   or a callee's declared postcondition at a call site), field/call-
   range facts have never been derivable from an ORDINARY runtime
   branch condition here (e.g. 'if (h->count < 5)' / 'if (i < v.size
   ())' in ordinary, uncontracted code) -- there is no GIMPLE_COND/edge
   handling anywhere else in this file. Ordinary bare-decl/SSA ranges
   get this for free from GCC's own general gimple_ranger (already
   consulted elsewhere in this file), which is why this gap was never
   visible until field/call-range facts existed at all. VAL, if it's an
   SSA_NAME whose def-stmt is a field load ('x = h->count') or a call
   to a DECL_DECLARED_CONVEYOR_P accessor ('x = v.size ()'), is
   recognized as such -- the GIMPLE-native analogue of contracts.cc's
   own oa_symbolic_comparison_conjunct_shape/oa_call_range_conjunct_
   shape, which instead operate on a still-AST contract-condition tree
   (a contract specifier keeps its original AST form even after the
   surrounding function body is gimplified, which is why those two
   never needed a GIMPLE-native counterpart before now -- an ordinary
   if-condition in the function body has no such luxury).  */

static bool
cg_cond_operand_shape (tree val, bool *is_call, tree *field_out, tree *base_out,
			 tree *callee_out, tree *receiver_out)
{
  if (TREE_CODE (val) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (!def)
    return false;

  /* D4324: a call used directly in an 'if' condition's own boolean
     comparison (e.g. 'if (s.size () > 3)') gimplifies with one more
     level of indirection than a field load in the same position (e.g.
     'if (s.count > 3)') -- confirmed via direct SSA-dump comparison:
     the field case's own comparison operand is the field-load SSA name
     itself ('_1 = s->count; if (_1 > 3)'), but the call case's is a
     PLAIN COPY of the call's result ('_26 = S::size (s); _1 = _26; ...
     if (_1 > 3)'), not the call statement itself. Left unchased, VAL's
     own def-stmt here is that copy assignment, never a GIMPLE_CALL nor
     a field load, so this always declined the call shape entirely --
     confirmed via a real test (d4324-gimple-conveyor-call-range-
     ifcond.C's own checked_call) where the field analogue (checked_
     field) already worked. Chase exactly one such copy hop, matching
     this file's own established "single hop, not a general SSA copy-
     chase" discipline (see cg_refine_edge_into's own zero-test unwrap
     just above this function's caller).  */
  if (is_gimple_assign (def) && gimple_assign_single_p (def)
      && TREE_CODE (gimple_assign_rhs1 (def)) == SSA_NAME)
    {
      val = gimple_assign_rhs1 (def);
      def = SSA_NAME_DEF_STMT (val);
      if (!def)
	return false;
    }

  if (is_gimple_call (def))
    {
      gcall *call = as_a <gcall *> (def);
      tree callee = gimple_call_fndecl (call);
      if (!callee || !DECL_OBJECT_MEMBER_FUNCTION_P (callee)
	  || gimple_call_num_args (call) != 1)
	return false;
      maybe_instantiate_conveyor (callee);
      if (!DECL_DECLARED_CONVEYOR_P (callee))
	return false;
      *is_call = true;
      *callee_out = callee;
      *receiver_out = gimple_call_arg (call, 0);
      return true;
    }

  if (gimple_assign_single_p (def))
    {
      tree rhs = gimple_assign_rhs1 (def);
      if (TREE_CODE (rhs) != COMPONENT_REF)
	return false;
      tree field = TREE_OPERAND (rhs, 1);
      if (TREE_CODE (field) != FIELD_DECL)
	return false;
      tree obj = TREE_OPERAND (rhs, 0);
      tree obj_expr
	= TREE_CODE (obj) == MEM_REF && integer_zerop (TREE_OPERAND (obj, 1))
	  ? TREE_OPERAND (obj, 0) : obj;
      *is_call = false;
      *field_out = field;
      *base_out = obj_expr;
      return true;
    }

  return false;
}

/* D4324 Commit 3: is VAL a bare formal parameter's own, still-unmodified
   SSA value -- the GIMPLE-native analogue of contracts.cc's own
   oa_underlying_param_operand (a bare PARM_DECL, no field/array-slot
   resolution)? Deliberately narrower than cg_self_trust_key: no
   TREE_ADDRESSABLE fallback for a not-SSA-tracked parameter, since an
   operand actually appearing in a GIMPLE_COND is virtually always an
   SSA name already (it's a value being compared, not an address-taken
   aggregate) -- matching this file's own "safe, occasionally
   conservative" discipline rather than chasing full generality.

   Strips one hop of an integer-promoting CONVERT_EXPR/NOP_EXPR first
   (mirroring cg_get_call_relational's own identical CONVERT_EXPR_CODE_P
   unwrap a few hundred lines above, and contracts.cc's own oa_strip_to_
   relational_operand on the AST side): comparing a signed int parameter
   against an unsigned std::size_t-returning accessor (e.g. 'idx <
   v.size ()', idx an int) gimplifies with idx's own read materialized
   into a separate 'widened = (size_t) idx;' assignment first, so the
   GIMPLE_COND's own operand is WIDENED's own SSA name, not idx's --
   found via direct testing against the real std::vector (whose size()
   returns size_t, unlike this pass's own earlier, int-returning test-
   only fixtures, which never exercised this at all before now: an
   int-vs-int comparison needs no such cast, so this gap was invisible
   until a real, unsigned-returning accessor was used).

   ALLOW_VAR_DECL (default false, preserving every existing caller's
   behavior byte-for-byte): also accept a bare VAR_DECL's own SSA value,
   not just a PARM_DECL's -- found via a user-reported Godbolt link
   (https://godbolt.org/z/nxE9xKdnj) that a LOCAL variable used as a
   guard's own subject (e.g. 'int idx = get_index (); if (idx <
   test.size ()) return test[idx];') failed to establish a fact at all,
   the GIMPLE-side counterpart of the identical gap already fixed in
   contracts.cc's own oa_match_comparison_against_call (see that
   function's own comment for the full rationale: every caller of
   cg_cond_is_bare_param is, unlike that AST-side function, already
   exclusively the "within-function, branch-derived fact" family --
   cg_match_shifted_comparison_against_call and cg_refine_relational_
   edge_into -- so there is no separate "declared contract text,
   substituted at a call site" caller here needing the stricter default
   preserved; every one of this function's own 4 call sites passes true).

   Deliberately NOT requiring SSA_NAME_IS_DEFAULT_DEF for the VAR_DECL
   case, unlike the PARM_DECL one: that requirement exists specifically
   to pick out a REASSIGNABLE parameter's one canonical (incoming-
   argument) SSA version for a caller that substitutes it elsewhere: a
   local variable has no such ambiguity to resolve at all -- its own SSA
   name is already uniquely tied to its own one defining statement, and
   the established fact is keyed on that exact SSA name here, not on
   the decl, so SSA's own uniqueness guarantee alone is already sound
   (a genuine reassignment produces a provably different SSA name, so a
   later consult against the wrong version simply, safely finds nothing,
   never a false match).  */

static bool
cg_cond_is_bare_param (tree val, tree *out = NULL, bool allow_var_decl = false)
{
  if (TREE_CODE (val) == SSA_NAME && !SSA_NAME_IS_DEFAULT_DEF (val))
    {
      gimple *def = SSA_NAME_DEF_STMT (val);
      if (def && is_gimple_assign (def)
	  && CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (def)))
	val = gimple_assign_rhs1 (def);
    }
  if (TREE_CODE (val) != SSA_NAME)
    return false;
  bool ok;
  if (SSA_NAME_IS_DEFAULT_DEF (val))
    ok = TREE_CODE (SSA_NAME_VAR (val)) == PARM_DECL;
  else
    ok = allow_var_decl && SSA_NAME_VAR (val) && VAR_P (SSA_NAME_VAR (val));
  if (ok && out)
    *out = val;
  return ok;
}

/* D4324 (see .claude/plans/lazy-stirring-pearl.md, Part 4): the GIMPLE-
   native analogue of contracts.cc's own oa_match_shifted_comparison_
   against_call -- is OTHER (an operand that already failed cg_cond_
   operand_shape directly, i.e. it's not itself a bare call/field load)
   an SSA name whose own def-stmt is 'CALL () - PARAM' or 'PARAM - CALL
   ()' (a MINUS_EXPR combining a bare-param operand, cg_cond_is_bare_
   param, and a call-shaped one, cg_cond_operand_shape with IS_CALL
   true)? If so, PARAM_OUT/RHS_RECEIVER_OUT/RHS_CALLEE_OUT describe it,
   and NEGATE_OUT tells the caller whether solving for PARAM negates the
   comparison direction (true for 'CALL () - PARAM', since that divides
   by -1; false for 'PARAM - CALL ()') -- the caller applies that flip
   to CODE itself, alongside its own existing FLIPPED/ASSERTED_TRUE
   flips (all three are the same self-inverse LT<->GT/LE<->GE swap, so
   applying them in any order gives the same result), and negates the
   literal the same way to get OFFSET. See that AST-side function's own
   comment for the full algebra table this mirrors.

   No PLUS_EXPR counterpart: 'v.size () - idx' and 'idx - v.size ()' are
   both written with a literal MINUS_EXPR in the source (matching the
   AST-side recognizer's own identical restriction) -- a PLUS_EXPR here
   would be a structurally different expression ('v.size () + idx'),
   not a mirror image of this same shape.  */

/* The GIMPLE-native analogue of contracts.cc's own oa_shifted_operand_
   conversion_ok_p -- true unconditionally when the conversion can't
   change PARAM's value regardless of what it holds (the fast, common
   path), otherwise true only when PARAM's own NATIVE range (its
   pre-conversion type) is provably entirely non-negative, which makes
   the conversion exact for its actual value (e.g. an 'idx >= 0'
   conjunct established elsewhere).

   Deliberately queries PARAM's own range, not OPERAND's (the already-
   converted value) -- unlike contracts.cc's own oa_get_range,
   cg_established_range_of's own gimple_ranger fallback essentially
   always succeeds for a converted, unsigned-typed SSA name, typically
   with nothing more than that type's own full range (e.g. [0,
   2^64-1]), which conveys no real sign information at all; checking
   "did a range lookup on OPERAND succeed" would therefore be far too
   permissive (confirmed via direct testing: it accepted a completely
   unconstrained PARAM). Checking PARAM's own range for an explicit
   lo >= 0 is robust against that same fallback: an unconstrained
   PARAM's own native-type range has a negative lower bound (e.g.
   INT_MIN), so this correctly declines instead.

   AT_STMT (the GIMPLE_COND this shifted comparison came from) is
   required, not optional: without it, cg_established_range_of's own
   ranger query answers a *whole-function* question ("what could PARAM
   be at any call site"), which for a plain parameter is always
   unconstrained/varying regardless of any 'idx >= 0' check dominating
   this specific point -- confirmed via direct testing (an 'idx >= 0'
   guard had no effect at all until this was added), matching item 8's
   own identical discovery for its div/mod check.  */

static bool
cg_shifted_operand_conversion_ok_p (tree operand, tree param,
				      hash_map<tree, cg_range_lite> &established_range,
				      gimple_ranger *ranger, bool require_conveyor,
				      bool require_symbolic, gimple *at_stmt,
				      oa_unprovable_reason *reason_out = nullptr)
{
  if (oa_integral_conversion_value_preserving_p (TREE_TYPE (param),
						   TREE_TYPE (operand)))
    return true;
  if (TYPE_UNSIGNED (TREE_TYPE (param)) || !TYPE_UNSIGNED (TREE_TYPE (operand)))
    {
      if (reason_out)
	*reason_out = OA_UNPROVABLE_SIGN_AMBIGUOUS;
      return false;
    }
  hash_map<tree, cg_range_lite> empty_scalar_range_cache;
  cg_range_lite param_range;
  if (cg_established_range_of (param, established_range,
				 empty_scalar_range_cache, ranger,
				 require_conveyor, require_symbolic,
				 &param_range, at_stmt)
      && param_range.has_lo && param_range.lo >= 0)
    return true;
  if (reason_out)
    *reason_out = OA_UNPROVABLE_SIGN_AMBIGUOUS;
  return false;
}

static bool
cg_match_shifted_comparison_against_call (tree other,
					    hash_map<tree, cg_range_lite> &established_range,
					    gimple_ranger *ranger,
					    bool require_conveyor, bool require_symbolic,
					    gimple *at_stmt,
					    tree *param_out,
					    tree *rhs_receiver_out,
					    tree *rhs_callee_out,
					    bool *negate_out,
					    tree *arithmetic_type_out,
					    oa_unprovable_reason *reason_out = nullptr)
{
  if (TREE_CODE (other) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (other);
  if (!def || !is_gimple_assign (def)
      || gimple_assign_rhs_code (def) != MINUS_EXPR)
    return false;

  tree op0 = gimple_assign_rhs1 (def);
  tree op1 = gimple_assign_rhs2 (def);

  bool is_call;
  tree field, base, callee, receiver, param;
  if (cg_cond_is_bare_param (op1, &param, /*allow_var_decl=*/true)
      && cg_shifted_operand_conversion_ok_p (op1, param, established_range,
					       ranger, require_conveyor,
					       require_symbolic, at_stmt,
					       reason_out)
      && cg_cond_operand_shape (op0, &is_call, &field, &base, &callee,
				 &receiver)
      && is_call)
    {
      *param_out = param;
      *rhs_receiver_out = receiver;
      *rhs_callee_out = callee;
      *negate_out = true;
      *arithmetic_type_out = TREE_TYPE (other);
      return true;
    }
  if (cg_cond_is_bare_param (op0, &param, /*allow_var_decl=*/true)
      && cg_shifted_operand_conversion_ok_p (op0, param, established_range,
					       ranger, require_conveyor,
					       require_symbolic, at_stmt,
					       reason_out)
      && cg_cond_operand_shape (op1, &is_call, &field, &base, &callee,
				 &receiver)
      && is_call)
    {
      *param_out = param;
      *rhs_receiver_out = receiver;
      *rhs_callee_out = callee;
      *negate_out = false;
      *arithmetic_type_out = TREE_TYPE (other);
      return true;
    }
  return false;
}

/* The GIMPLE-native analogue of contracts.cc's own oa_shifted_
   comparison_no_wrap_ok_p -- see that function's own comment for the
   full soundness rationale (an unsigned MINUS_EXPR can wrap regardless
   of PARAM's own type, so the algebraic "solve for PARAM" step needs an
   independently-established, exact, offset-0 companion fact ruling that
   out). Consults STATE.CALL_REL directly (the same dominator-scoped map
   the caller, cg_refine_edge_into, is about to populate), not
   ESTABLISHED_CALL_REL -- GIMPLE only ever establishes this shifted
   shape from a branch condition, never from self-trust (no PLUS_EXPR
   counterpart's own comment, a few hundred lines above), so there is
   only the one map to check.  */

static bool
cg_shifted_comparison_no_wrap_ok_p (hash_map<tree, cg_call_rel_fact> &call_rel,
				      tree param, tree receiver, tree callee,
				      tree arithmetic_type, bool param_is_minuend,
				      oa_unprovable_reason *reason_out = nullptr)
{
  if (!INTEGRAL_TYPE_P (arithmetic_type) || TYPE_OVERFLOW_UNDEFINED (arithmetic_type))
    return true;

  cg_call_rel_fact *fact = call_rel.get (param);
  if (!fact)
    {
      if (reason_out)
	*reason_out = OA_UNPROVABLE_NO_WRAP_COMPANION;
      return false;
    }
  if (fact->rhs_callee != callee
      || (oa_strip_to_receiver_identity (fact->rhs_receiver)
	  != oa_strip_to_receiver_identity (receiver)))
    {
      if (reason_out)
	*reason_out = OA_UNPROVABLE_WRONG_IDENTITY;
      return false;
    }
  if (!cg_range_lite_equal (fact->offset, cg_range_lite_exact (0)))
    {
      if (reason_out)
	*reason_out = OA_UNPROVABLE_NO_WRAP_COMPANION;
      return false;
    }

  tree_code required = param_is_minuend ? GE_EXPR : LE_EXPR;
  if (oa_relational_code_implies (fact->code, required))
    return true;
  if (reason_out)
    *reason_out = OA_UNPROVABLE_NO_WRAP_COMPANION;
  return false;
}

/* D4324 Commit 3: the relational analogue of cg_refine_edge_into's own
   field/call-range handling immediately below, for when NEITHER side
   of the (already one-hop-unwrapped) comparison is a literal -- tried
   specifically because that's exactly the shape neither of that
   function's own two blocks ever matches. Only two of the three
   relational shapes (param-vs-param, param-vs-call): see cg_dom_fact_
   state's own comment on .rel/.call_rel for why call-vs-call
   deliberately has no counterpart here (its own fact is keyed on an
   object's identity, not an SSA name, so it can't be safely flattened
   into scalar_rel_cache/scalar_call_rel_cache the way these two are --
   see cg_predicate_facts_walk's own comment on that cache).  */

static void
cg_refine_relational_edge_into (tree lhs, tree rhs, tree_code code,
				  bool asserted_true, cg_dom_fact_state &state)
{
  /* Canonicalize LHS/RHS to the bare param's own default-def SSA name
     when either is one -- see cg_cond_is_bare_param's own comment on
     why a signed-vs-unsigned comparison (e.g. against a real, size_t-
     returning accessor) needs this unwrap, and why every established
     fact must be keyed on that same canonical name a later consult
     will look up again, not on a one-off cast temporary.  */
  tree lhs_param, rhs_param;
  bool lhs_is_param = cg_cond_is_bare_param (lhs, &lhs_param,
					       /*allow_var_decl=*/true);
  bool rhs_is_param = cg_cond_is_bare_param (rhs, &rhs_param,
					       /*allow_var_decl=*/true);
  if (lhs_is_param)
    lhs = lhs_param;
  if (rhs_is_param)
    rhs = rhs_param;

  if (lhs_is_param && rhs_is_param)
    {
      tree_code rel_code = code;
      if (!asserted_true)
	switch (rel_code)
	  {
	  case LT_EXPR: rel_code = GE_EXPR; break;
	  case LE_EXPR: rel_code = GT_EXPR; break;
	  case GT_EXPR: rel_code = LE_EXPR; break;
	  case GE_EXPR: rel_code = LT_EXPR; break;
	  default: return; /* NOT(lhs == rhs) -- skip.  */
	  }
      state.rel.put (lhs, { rel_code, rhs, cg_range_lite_exact (0), true });
      return;
    }

  if (!lhs_is_param && !rhs_is_param)
    {
      /* 'v.size () < w.size ()'.  */
      bool l_is_call, r_is_call;
      tree l_field, l_base, l_callee, l_receiver;
      tree r_field, r_base, r_callee, r_receiver;
      if (!cg_cond_operand_shape (lhs, &l_is_call, &l_field, &l_base,
				  &l_callee, &l_receiver)
	  || !l_is_call
	  || !cg_cond_operand_shape (rhs, &r_is_call, &r_field, &r_base,
				     &r_callee, &r_receiver)
	  || !r_is_call)
	return;
      tree l_identity = cg_gimple_object_identity (l_receiver);
      tree r_identity = cg_gimple_object_identity (r_receiver);
      if (!l_identity || !r_identity)
	return;
      tree_code cc_code = code;
      if (!asserted_true)
	switch (cc_code)
	  {
	  case LT_EXPR: cc_code = GE_EXPR; break;
	  case LE_EXPR: cc_code = GT_EXPR; break;
	  case GT_EXPR: cc_code = LE_EXPR; break;
	  case GE_EXPR: cc_code = LT_EXPR; break;
	  default: return;
	  }
      else if (cc_code == NE_EXPR)
	return;
      tree_code swapped = swap_tree_comparison (cc_code);
      state.call_call.put ({l_identity, l_callee},
			   { cc_code, r_receiver, r_callee, true });
      state.call_call.put ({r_identity, r_callee},
			   { swapped, l_receiver, l_callee, true });
      return;
    }

  tree param_side = lhs_is_param ? lhs : rhs;
  tree call_side = lhs_is_param ? rhs : lhs;

  bool is_call;
  tree field, base, callee, receiver;
  if (!cg_cond_operand_shape (call_side, &is_call, &field, &base, &callee,
			       &receiver)
      || !is_call)
    return;

  tree_code rel_code = code;
  if (!lhs_is_param)
    switch (rel_code)
      {
      case LT_EXPR: rel_code = GT_EXPR; break;
      case LE_EXPR: rel_code = GE_EXPR; break;
      case GT_EXPR: rel_code = LT_EXPR; break;
      case GE_EXPR: rel_code = LE_EXPR; break;
      default: break;
      }
  if (!asserted_true)
    switch (rel_code)
      {
      case LT_EXPR: rel_code = GE_EXPR; break;
      case LE_EXPR: rel_code = GT_EXPR; break;
      case GT_EXPR: rel_code = LE_EXPR; break;
      case GE_EXPR: rel_code = LT_EXPR; break;
      default: return;
      }
  state.call_rel.put (param_side, { rel_code, receiver, callee,
				      cg_range_lite_exact (0), true });
}

/* If E is a true/false edge of a GIMPLE_COND matching the field/call-
   range shape above, tighten the corresponding fact in STATE (already
   a copy of the predecessor's own OUT state -- see this function's
   only caller, cg_compute_in_state) the same way an established
   contract-derived fact is tightened elsewhere in this file. A fact
   created this way (from a real, executed branch, not from any
   contract) is tagged conveyor_established = true when brand new,
   mirroring contracts.cc's own oa_refine_single_comparison exactly --
   see that function's own comment for the full rationale. No
   relational (param-vs-call) counterpart here: that shape is only ever
   established from a declared precondition's own self-trust, never
   from an ordinary branch, on the AST side either (contracts.cc's own
   oa_establish_shared_substrate_self_trust, never oa_refine_single_
   comparison) -- this mirrors that same scope exactly.  */

/* Is E a true/false edge of a GIMPLE_COND that merely tests some SSA
   name against integer zero -- the boolean-temp shape this whole
   file's edge refinement relies on (see cg_refine_edge_into's own
   comment on why 'if (v.size () > 3)' lowers this way)? If so,
   *TESTED_OUT is that SSA name and *WANT_NONZERO_OUT is whether taking
   E requires it to be nonzero (a '_2 == 0' test flips which edge means
   "the tested value is nonzero", unlike a '_2 != 0' test). Returns
   false for any other shape, including E not being a true/false edge
   at all, in which case neither output is touched.

   Factored out of cg_refine_edge_into's own former inline version of
   this same check so it and cg_bool_phi_source_edges' own caller
   immediately below can never drift apart on this extraction --
   mirrors this file's own stated rationale for factoring out cg_
   compute_in_state's IN-state computation a bit further down ("so the
   two can never drift apart").  */

static bool
cg_gimple_cond_zero_test (edge e, tree *tested_out, bool *want_nonzero_out)
{
  if (!(e->flags & (EDGE_TRUE_VALUE | EDGE_FALSE_VALUE)))
    return false;
  gimple *stmt = gsi_stmt (gsi_last_bb (e->src));
  if (!stmt || gimple_code (stmt) != GIMPLE_COND)
    return false;
  gcond *cond = as_a <gcond *> (stmt);

  tree_code code = gimple_cond_code (cond);
  tree lhs = gimple_cond_lhs (cond);
  tree rhs = gimple_cond_rhs (cond);
  if ((code != NE_EXPR && code != EQ_EXPR)
      || TREE_CODE (rhs) != INTEGER_CST || !integer_zerop (rhs)
      || TREE_CODE (lhs) != SSA_NAME)
    return false;

  bool want_nonzero = (e->flags & EDGE_TRUE_VALUE) != 0;
  if (code == EQ_EXPR)
    want_nonzero = !want_nonzero;
  *tested_out = lhs;
  *want_nonzero_out = want_nonzero;
  return true;
}

/* TESTED is known (via cg_gimple_cond_zero_test) to be compared against
   integer zero on some GIMPLE_COND edge; WANT_NONZERO is the polarity
   that edge requires. Chases TESTED through a chain of plain 'x = y;'
   copies -- exactly the shape the gimplifier's own short-circuit
   '&&'/'||' lowering produces (a final straight copy, e.g. 'retval.0 =
   iftmp.1;', on top of the PHI that actually carries the two arms' own
   1/0 constants -- confirmed by direct testing of 2-conjunct,
   3-conjunct, mixed '&&'/'||', and negated compound conditions: every
   one of them collapses to exactly this single flat PHI-of-constants
   shape, regardless of how deeply nested the original boolean
   expression was, so this is not a partial recognizer for the
   compound-condition problem, it's a complete one) -- until it lands
   on a GIMPLE_PHI, or declines (returns false) at the first
   unrecognized shape.

   Once a PHI is found, EVERY argument must be an INTEGER_CST -- if
   even one is not (e.g. a non-constant value merged in from some
   genuinely different computation, not a short-circuit artifact), this
   declines entirely: it is meant to precisely characterize the
   short-circuit lowering shape, not speculate about any other PHI.
   For each argument whose zero/nonzero-ness matches WANT_NONZERO, the
   incoming edge is pushed onto EDGES_OUT. Returns true (EDGES_OUT
   possibly empty, e.g. an always-false conjunct) once a qualifying PHI
   is found at all.  */

static bool
cg_bool_phi_source_edges (tree tested, bool want_nonzero,
			    vec<edge> *edges_out)
{
  hash_set<tree> seen;
  while (true)
    {
      if (seen.add (tested))
	return false; /* Cycle guard; not expected on a copy chain.  */
      if (TREE_CODE (tested) != SSA_NAME || virtual_operand_p (tested))
	return false;
      if (SSA_NAME_IS_DEFAULT_DEF (tested))
	return false;
      gimple *def = SSA_NAME_DEF_STMT (tested);
      if (!def)
	return false;

      if (gimple_code (def) == GIMPLE_PHI)
	{
	  gphi *phi = as_a <gphi *> (def);
	  unsigned n = gimple_phi_num_args (phi);
	  for (unsigned i = 0; i < n; ++i)
	    {
	      /* Each argument is itself an SSA name (e.g. 'iftmp.1_14'),
		 not a raw INTEGER_CST embedded directly in the PHI --
		 confirmed by direct testing (this recognizer never
		 matched at all until this hop was added): the constant
		 only appears one hop further back, at that SSA name's
		 own single, trivial def ('iftmp.1_14 = 1;') in its own
		 incoming block. Resolve that one hop here; anything
		 else (not an SSA name at all, or an SSA name whose own
		 def isn't a bare constant assignment) declines, same
		 discipline as the outer copy-chase above.  */
	      tree arg = gimple_phi_arg_def (phi, i);
	      if (TREE_CODE (arg) == SSA_NAME && !virtual_operand_p (arg)
		  && !SSA_NAME_IS_DEFAULT_DEF (arg))
		{
		  gimple *arg_def = SSA_NAME_DEF_STMT (arg);
		  if (arg_def && gimple_assign_single_p (arg_def)
		      && TREE_CODE (gimple_assign_rhs1 (arg_def)) == INTEGER_CST)
		    arg = gimple_assign_rhs1 (arg_def);
		}
	      if (TREE_CODE (arg) != INTEGER_CST)
		return false;
	      if (integer_zerop (arg) == !want_nonzero)
		edges_out->safe_push (gimple_phi_arg_edge (phi, i));
	    }
	  return true;
	}

      if (is_gimple_assign (def) && gimple_assign_rhs_code (def) == SSA_NAME)
	{
	  tested = gimple_assign_rhs1 (def);
	  continue;
	}
      return false;
    }
}

static void
cg_refine_edge_into (edge e, cg_dom_fact_state &state,
		      hash_map<tree, cg_range_lite> &established_range,
		      gimple_ranger *ranger, bool require_conveyor,
		      bool require_symbolic)
{
  if (!(e->flags & (EDGE_TRUE_VALUE | EDGE_FALSE_VALUE)))
    return;
  gimple *stmt = gsi_stmt (gsi_last_bb (e->src));
  if (!stmt || gimple_code (stmt) != GIMPLE_COND)
    return;
  gcond *cond = as_a <gcond *> (stmt);

  tree_code code = gimple_cond_code (cond);
  tree lhs = gimple_cond_lhs (cond);
  tree rhs = gimple_cond_rhs (cond);
  bool asserted_true = (e->flags & EDGE_TRUE_VALUE) != 0;

  /* 'if (v.size () > 3)' does not gimplify into a GIMPLE_COND that
     directly embeds the '>' comparison -- confirmed by direct testing
     (this refinement never fired at all until this was added): the
     comparison is computed into a separate boolean temporary first
     ('_1 = v.size (); _2 = _1 > 3;'), and the GIMPLE_COND itself only
     ever tests that temporary against zero ('if (_2 != 0)').  Unwrap
     that one indirection here: trace the temporary's own def-stmt for
     the real comparison, folding its own polarity into ASSERTED_TRUE
     (a '_2 == 0' test flips which edge means "the real comparison held
     true", unlike a '_2 != 0' test).  Only one hop is unwrapped, not a
     general SSA copy-chase -- this exact shape (a single, freshly
     computed boolean temp feeding the branch) is what's actually
     produced here; anything deeper is left unrecognized, the same
     "safe, just occasionally conservative" discipline used throughout
     this whole engine.  */
  tree zero_tested;
  bool zero_want_nonzero;
  if (cg_gimple_cond_zero_test (e, &zero_tested, &zero_want_nonzero))
    {
      gimple *def = SSA_NAME_DEF_STMT (zero_tested);
      if (!def || !is_gimple_assign (def))
	return;
      tree_code def_code = gimple_assign_rhs_code (def);
      if (def_code != LT_EXPR && def_code != LE_EXPR && def_code != GT_EXPR
	  && def_code != GE_EXPR && def_code != EQ_EXPR)
	return;
      asserted_true = zero_want_nonzero;
      code = def_code;
      lhs = gimple_assign_rhs1 (def);
      rhs = gimple_assign_rhs2 (def);
    }

  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return;

  /* A floating-point field compared with a constant ('this->m_value >
     100.0'): its range on each edge -- on the false one, under IEEE, a
     NaN too.  */
  if (TREE_CODE (rhs) == REAL_CST || TREE_CODE (lhs) == REAL_CST)
    {
      bool fl = TREE_CODE (lhs) == REAL_CST;
      tree fo = fl ? rhs : lhs, fc = fl ? lhs : rhs;
      bool fis_call;
      tree ffield, fbase, fcallee, freceiver;
      if (!cg_cond_operand_shape (fo, &fis_call, &ffield, &fbase, &fcallee,
				  &freceiver)
	  || fis_call)
	return;
      tree_code fcode = fl ? swap_tree_comparison (code) : code;
      bool nan = false;
      if (!asserted_true)
	{
	  switch (fcode)
	    {
	    case LT_EXPR: fcode = GE_EXPR; break;
	    case LE_EXPR: fcode = GT_EXPR; break;
	    case GT_EXPR: fcode = LE_EXPR; break;
	    case GE_EXPR: fcode = LT_EXPR; break;
	    default: return;
	    }
	  nan = HONOR_NANS (TREE_TYPE (fc));
	}
      frange r;
      if (!cg_frange_for_comparison (TREE_TYPE (fc), fcode, fc, &r))
	return;
      if (nan)
	r.update_nan ();
      if (tree identity = cg_gimple_object_identity (fbase))
	cg_add_float_field_fact (state, {identity, ffield}, r, true);
      return;
    }

  tree other, const_val;
  bool flipped;
  if (TREE_CODE (rhs) == INTEGER_CST)
    other = lhs, const_val = rhs, flipped = false;
  else if (TREE_CODE (lhs) == INTEGER_CST)
    other = rhs, const_val = lhs, flipped = true;
  else
    {
      cg_refine_relational_edge_into (lhs, rhs, code, asserted_true, state);
      return;
    }

  bool is_call;
  tree field, base, callee, receiver;
  bool shifted = false;
  tree shifted_param = NULL_TREE;
  bool shifted_negate = false;
  tree shifted_arithmetic_type = NULL_TREE;
  if (!cg_cond_operand_shape (other, &is_call, &field, &base, &callee, &receiver))
    {
      /* D4324 Part 4: OTHER isn't itself a bare call/field load, but it
	 may be 'CALL () - PARAM'/'PARAM - CALL ()' -- see cg_match_
	 shifted_comparison_against_call's own comment for the shape and
	 algebra this recognizes, feeding the *existing* state.call_rel
	 establishment path a nonzero OFFSET instead of state.call/
	 state.field's own always-zero-offset range refinement.  */
      if (!cg_match_shifted_comparison_against_call (other, established_range,
							ranger, require_conveyor,
							require_symbolic, stmt,
							&shifted_param,
							&receiver, &callee,
							&shifted_negate,
							&shifted_arithmetic_type))
	return;
      /* See cg_shifted_comparison_no_wrap_ok_p's own comment: an
	 unsigned MINUS_EXPR can wrap regardless of PARAM's own type,
	 so this needs an independently-established, exact companion
	 fact ruling that out before the algebraic solve above can be
	 trusted. SHIFTED_NEGATE true means 'CALL () - PARAM' (PARAM is
	 the subtrahend, so PARAM_IS_MINUEND is false); false means
	 'PARAM - CALL ()' (PARAM is the minuend).  */
      oa_unprovable_reason why = OA_UNPROVABLE_NONE;
      if (!cg_shifted_comparison_no_wrap_ok_p (state.call_rel, shifted_param,
						 receiver, callee,
						 shifted_arithmetic_type,
						 /*param_is_minuend=*/!shifted_negate,
						 &why))
	{
	  cg_note_unprovable_hint (shifted_param, why);
	  return;
	}
      shifted = true;
      if (shifted_negate)
	switch (code)
	  {
	  case LT_EXPR: code = GT_EXPR; break;
	  case LE_EXPR: code = GE_EXPR; break;
	  case GT_EXPR: code = LT_EXPR; break;
	  case GE_EXPR: code = LE_EXPR; break;
	  default: break;
	  }
    }

  if (flipped)
    switch (code)
      {
      case LT_EXPR: code = GT_EXPR; break;
      case LE_EXPR: code = GE_EXPR; break;
      case GT_EXPR: code = LT_EXPR; break;
      case GE_EXPR: code = LE_EXPR; break;
      default: break;
      }
  if (!asserted_true)
    switch (code)
      {
      case LT_EXPR: code = GE_EXPR; break;
      case LE_EXPR: code = GT_EXPR; break;
      case GT_EXPR: code = LE_EXPR; break;
      case GE_EXPR: code = LT_EXPR; break;
      default: return; /* NOT(x == val) -- skip.  */
      }

  if (shifted)
    {
      widest_int offset = wi::to_widest (const_val);
      if (shifted_negate)
	offset = -offset;
      state.call_rel.put (shifted_param,
			   { code, receiver, callee,
			     cg_range_lite_exact (offset), true });
      return;
    }

  if (is_call)
    {
      tree identity = cg_gimple_object_identity (receiver);
      if (!identity)
	return;
      cg_field_fact *existing = state.call.get ({identity, callee});
      cg_range_lite refined = existing ? existing->range : cg_range_lite ();
      bool conveyor_established = existing ? existing->conveyor_established : true;
      cg_tighten_range_bound (refined, code, wi::to_widest (const_val));
      state.call.put ({identity, callee}, { refined, conveyor_established });
    }
  else
    {
      tree identity = cg_gimple_object_identity (base);
      if (!identity)
	return;
      cg_field_fact *existing = state.field.get ({identity, field});
      cg_range_lite refined = existing ? existing->range : cg_range_lite ();
      bool conveyor_established = existing ? existing->conveyor_established : true;
      cg_tighten_range_bound (refined, code, wi::to_widest (const_val));
      state.field.put ({identity, field}, { refined, conveyor_established });
    }
}

/* IN_STATE is computed identically by both the fixed-point loop below
   (which must recompute it, possibly several times, for every block)
   and the final diagnostic pass (which needs the very same computation
   done once more, from the now-stable BLOCK_OUT) -- factored out so
   the two can never drift apart.  */

/* Does a back edge enter BB (a predecessor BB dominates)?  */

static bool
cg_loop_header_p (basic_block bb)
{
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->preds)
    if (dominated_by_p (CDI_DOMINATORS, e->src, bb))
      return true;
  return false;
}

static void
cg_compute_in_state (basic_block bb,
		      hash_map<basic_block, cg_dom_fact_state *> &block_out,
		      cg_dom_fact_state &seed, cg_dom_fact_state *in_state,
		      hash_map<tree, cg_range_lite> &established_range,
		      gimple_ranger *ranger, bool require_conveyor,
		      bool require_symbolic)
{
  bool first = true;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      /* A predecessor with no entry yet is only possible on this loop's
	 very first iteration (a not-yet-reached forward successor of a
	 not-yet-processed RPO position is impossible by RPO's own
	 ordering guarantee) or is ENTRY itself (categorically excluded
	 from the RPO array) -- by the second outer iteration, every
	 predecessor, including loop back edges, has *some* entry, even
	 if still stale relative to what this same pass will go on to
	 recompute for it later in program order; that staleness is
	 exactly what keeps CHANGED true until the whole thing settles.  */
      cg_dom_fact_state **pred_out = block_out.get (e->src);
      if (!pred_out)
	continue;
      /* Refine a copy of the predecessor's own OUT state along this
	 specific edge (cg_refine_edge_into is a no-op unless E is a
	 true/false edge of a recognized field/call-range GIMPLE_COND),
	 so a fact this refinement adds is visible only down the branch
	 it's actually sound for, not merged in from every predecessor
	 indiscriminately.  */
      cg_dom_fact_state refined;

      /* E->src's own OUT state (**pred_out, about to become the basis
	 for REFINED below) is already the agreement-merge of ALL of
	 E->src's own predecessors -- for a short-circuit '&&'/'||'
	 condition's own boolean-materialize-then-retest join block
	 (see cg_bool_phi_source_edges' own comment for the exact shape
	 and why it's the general one for this problem, not a 2-conjunct
	 special case), that merge has ALREADY discarded exactly the
	 fact this edge needs: only ONE (or a specific few, for '||') of
	 those predecessors is actually reachable when this edge's own
	 condition holds, and that predecessor's own, still-precise OUT
	 state is what should seed REFINED instead of E->src's own
	 already-lossy one.

	 Sound to substitute rather than merely supplement: each
	 qualifying edge's own source block is a direct predecessor of
	 E->src (gimple_phi_arg_edge returns exactly that), and the
	 gimplifier's own short-circuit lowering never introduces a back
	 edge inside that local diamond, so every qualifying source
	 strictly precedes E->src in this walk's own RPO order. Since
	 BLOCK_OUT entries are created in that same RPO order and only
	 ever updated in place, never removed, a qualifying source's own
	 entry is guaranteed already present whenever E->src's own entry
	 (PRED_OUT, just checked above) is -- so the "not yet available"
	 fallback below can't actually trigger for this shape in
	 practice; it exists purely as a defensive no-op, not because
	 facts could otherwise grow between fixed-point iterations
	 (which would break this whole walk's own monotonic-decrease
	 termination argument, were it ever reachable).  */
      tree tested;
      bool want_nonzero;
      auto_vec<edge> qualifying_edges;
      bool used_qualifying = false;
      if (cg_gimple_cond_zero_test (e, &tested, &want_nonzero)
	  && cg_bool_phi_source_edges (tested, want_nonzero, &qualifying_edges))
	{
	  for (unsigned i = 0; i < qualifying_edges.length (); ++i)
	    {
	      cg_dom_fact_state **q_out
		= block_out.get (qualifying_edges[i]->src);
	      if (!q_out)
		continue;
	      if (!used_qualifying)
		{
		  cg_dom_fact_state_assign (&refined, **q_out);
		  used_qualifying = true;
		}
	      else
		cg_dom_fact_state_merge (&refined, **q_out);
	    }
	}
      if (!used_qualifying)
	cg_dom_fact_state_assign (&refined, **pred_out);
      else if (cg_short_circuit_sources && single_pred_p (bb))
	{
	  auto_vec<edge> **slot = &cg_short_circuit_sources->get_or_insert (bb);
	  if (!*slot)
	    *slot = new auto_vec<edge> ();
	  (*slot)->truncate (0);
	  for (edge qe : qualifying_edges)
	    (*slot)->safe_push (qe);
	}

      cg_refine_edge_into (e, refined, established_range, ranger,
			    require_conveyor, require_symbolic);
      if (first)
	{
	  cg_dom_fact_state_assign (in_state, refined);
	  first = false;
	}
      else
	cg_dom_fact_state_merge (in_state, refined,
				 /*unite_fields=*/!cg_loop_header_p (bb));
    }
  if (first)  /* ENTRY itself, or no predecessor processed yet at all.  */
    cg_dom_fact_state_assign (in_state, seed);
}

/* GIMPLE analogue of contracts.cc's own postcondition-side call-range
   composition (oa_call_postcondition_range_p's own extension, see that
   function's comment for the full rationale and the composition math
   this mirrors exactly): if CALL's callee has a postcondition relating
   its own return value to a call-range-eligible accessor call on one of
   its own parameters (e.g. 'post<ctrl>(r: r < this->size ())'), and
   STATE already has an established call-range fact for the substituted
   receiver, derive a concrete range for CALL's own result and record it
   in SCALAR_RANGE_CACHE, keyed on CALL's own LHS.

   Structurally new plumbing, not a mirror of an existing GIMPLE
   mechanism: field/call-range facts (STATE.call) live only in this
   file's own dominator-tree fixed-point walk (branch-sensitive
   tracking, needed for the GIMPLE_COND edge refinement above), while
   the scalar-range facts a literal-bounded postcondition already
   produces (cg_established_range_of/cg_call_postcondition_range_p)
   live in pass_contracts_gimple::execute's own separate, simple linear
   pass -- the two never previously shared state. SCALAR_RANGE_CACHE
   bridges them: computed once here, using this (now-stable, final-pass)
   dominator-tracked STATE, and consulted from the *other*, simple pass
   by cg_established_range_of (see pass_contracts_gimple::execute's own
   comment on why it now runs cg_predicate_facts_walk first). Sound to
   share as a single, flat, function-wide cache despite living outside
   cg_dom_fact_state's own per-block machinery: unlike a mutable
   object's field/call state, an SSA name's own value is permanent for
   its whole lifetime once defined (the same reasoning that already
   lets established_range itself be a single, non-per-block map), and
   this is only ever computed once, in this walk's final, fully-
   converged pass -- never during the fixed-point computation above,
   where STATE.call could still be mid-convergence.

   Called from this same call's own consult step, *before* its own
   argument invalidation (cg_invalidate_persistent_facts_for_call_args)
   -- unlike contracts.cc's own equivalent composition, which runs from
   inside oa_get_range, itself reached only *after* the assignment's own
   RHS call has already had its own arguments invalidated (a real,
   documented limitation there: see oa_call_postcondition_range_p's own
   comment). Ordering it this way here avoids that same self-
   invalidation entirely for the GIMPLE side: this call's own receiver
   argument's own established facts are still exactly as the *caller*
   left them when this composition runs.  */

static void
cg_compose_call_result_range (gcall *call, cg_dom_fact_state &state,
				hash_map<tree, cg_range_lite> &scalar_range_cache)
{
  tree lhs = gimple_call_lhs (call);
  if (!lhs)
    return;
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      if (!oa_contract_conveyor_active_cached_p (contract))
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
	  tree_code rcode;
	  tree rhs_receiver, rhs_callee;
	  if (!oa_match_result_call_relation (*conjuncts[i], result_id, &rcode,
					       &rhs_receiver, &rhs_callee,
					       /*allow_symbolic_accessor=*/false)
	      || TREE_CODE (rhs_receiver) != PARM_DECL)
	    continue;

	  unsigned argno;
	  if (!cg_find_param_position (callee, rhs_receiver, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;
	  tree substituted = cg_resolve_call_argument (call, argno);
	  tree identity = cg_gimple_object_identity (substituted);
	  if (!identity)
	    continue;

	  cg_field_fact *established = state.call.get ({identity, rhs_callee});
	  if (!established)
	    continue;

	  cg_range_lite &derived = established->range;
	  cg_range_lite refined;
	  if (cg_range_lite *existing = scalar_range_cache.get (lhs))
	    refined = *existing;

	  switch (rcode)
	    {
	    case LT_EXPR:
	      if (derived.has_hi
		  && (!refined.has_hi || derived.hi - 1 < refined.hi))
		{ refined.has_hi = true; refined.hi = derived.hi - 1; }
	      break;
	    case LE_EXPR:
	      if (derived.has_hi && (!refined.has_hi || derived.hi < refined.hi))
		{ refined.has_hi = true; refined.hi = derived.hi; }
	      break;
	    case GT_EXPR:
	      if (derived.has_lo
		  && (!refined.has_lo || derived.lo + 1 > refined.lo))
		{ refined.has_lo = true; refined.lo = derived.lo + 1; }
	      break;
	    case GE_EXPR:
	      if (derived.has_lo && (!refined.has_lo || derived.lo > refined.lo))
		{ refined.has_lo = true; refined.lo = derived.lo; }
	      break;
	    case EQ_EXPR:
	      if (derived.has_lo && (!refined.has_lo || derived.lo > refined.lo))
		{ refined.has_lo = true; refined.lo = derived.lo; }
	      if (derived.has_hi && (!refined.has_hi || derived.hi < refined.hi))
		{ refined.has_hi = true; refined.hi = derived.hi; }
	      break;
	    default:
	      break;
	    }
	  if (refined.has_lo || refined.has_hi)
	    scalar_range_cache.put (lhs, refined);
	}
    }
}

/* Bounds-proving demo (see .claude/plans/lazy-stirring-pearl.md, Part 2):
   the "param vs call" relational range-vs-range fallback, for the one
   consult path that genuinely cannot live in cg_check_call -- unlike the
   param-vs-param case (both sides plain scalars, handled directly inside
   cg_check_call itself with no architectural issue), this one needs
   STATE.call, which is dominator-tracked and deliberately never
   flattened into a function-wide cache the way relational facts are (see
   cg_predicate_facts_walk's own comment on .rel/.call_rel, immediately
   below, for exactly why -- object-identity-keyed facts can be
   invalidated by a later mutation, so only the dominator-correct STATE
   at this exact call is safe to consult, never a global snapshot).  So
   this runs from cg_predicate_facts_walk's own final pass instead, where
   STATE is already correct for CALL specifically. RANGER is
   pass_contracts_gimple::execute's own single, function-wide instance,
   created before cg_predicate_facts_walk's own call and passed straight
   through -- giving this walk a second, separate ranger instance of its
   own was tried first and reverted, found via direct testing to make
   this whole pass's own diagnostics genuinely non-deterministic between
   identical runs of the same compilation (see execute's own comment on
   why one shared instance avoids that).

   Only ever handles the "param vs call" shape itself; it does not
   attempt the "explicit linked fact" check cg_check_call's own
   cg_get_call_relational-based block already does (self-trust and
   branch-derived facts are unaffected by this addition). When this
   reaches a definitive verdict (SUBSUMED or DISJOINT), CALL is recorded
   in VERDICT_OUT so cg_check_call's own later, separate pass over the
   same call knows not to *also* warn "cannot verify" for a conjunct this
   pass already resolved one way or the other -- see cg_check_call's own
   use of VERDICT_OUT. A DISJOINT verdict reports the same hard
   "provably violates" error contracts.cc's own oa_handle_call_conveyor_
   proof_obligation already does for this outcome, right here (this is
   the only place with the data to detect it at all).

   Deliberately keyed per-CALL, not per-(CALL, conjunct): a callee with
   more than one "param vs call" conjunct on the same call site (rare,
   untested) could have a second, independently-unprovable conjunct's own
   "cannot verify" warning suppressed merely because a first conjunct on
   the same call was resolved here -- an accepted, documented imprecision
   (a warning could be missed), never a soundness gap (a genuine
   violation this function itself finds is always reported here
   directly, unconditionally, regardless of any other conjunct on the
   same call).  */

static void
cg_check_call_range_relational (gcall *call,
				  hash_map<cg_field_key_hash, tree> &field_object_cache,
				  cg_dom_fact_state &state,
				  gimple_ranger *ranger,
				  hash_set<gimple *> *verdict_out)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      bool conveyor_active = oa_contract_conveyor_active_cached_p (contract);
      bool symbolic_active = oa_contract_symbolic_active_cached_p (contract);
      /* See cg_check_call's own identical 'mandatory'.  */
      bool mandatory = (cg_callee_pre_mandatory && conveyor_active
			&& DECL_DECLARED_CONVEYOR_P (callee));
      if (cg_callee_pre_only && !mandatory)
	continue;
      bool check_as_conveyor = (mandatory
				|| (conveyor_active
				    && flag_contract_conveyor_proofs_gimple
				    && cg_proof_checks_p (contract, callee,
							  true)));
      bool check_as_symbolic = (!cg_callee_pre_only && symbolic_active
				&& flag_contract_symbolic_proofs_gimple
				&& cg_proof_checks_p (contract, callee, false));
      if (!check_as_conveyor && !check_as_symbolic)
	continue;
      bool require_conveyor = check_as_conveyor;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rhs_receiver, rhs_callee;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_call (*conjuncts[i], &rel_param,
						  &rel_code, &rhs_receiver,
						  &rhs_callee,
						  /*allow_symbolic_accessor=*/
						    !require_conveyor))
	    continue;

	  unsigned param_argno, receiver_argno;
	  if (!cg_find_param_position (callee, rel_param, &param_argno)
	      || !cg_find_param_position (callee, rhs_receiver, &receiver_argno)
	      || param_argno >= gimple_call_num_args (call)
	      || receiver_argno >= gimple_call_num_args (call))
	    continue;

	  tree sub_param = cg_resolve_value_argument (call, param_argno);
	  tree sub_receiver = cg_resolve_value_argument (call, receiver_argno);

	  hash_map<tree, cg_range_lite> empty_established_range;
	  hash_map<tree, cg_range_lite> empty_scalar_range_cache;
	  cg_range_lite param_range;
	  if (!cg_established_range_of (sub_param, empty_established_range,
					  empty_scalar_range_cache, ranger,
					  check_as_conveyor, check_as_symbolic,
					  &param_range))
	    continue;

	  tree identity = cg_field_slot_identity (sub_receiver, state);
	  if (!identity)
	    identity = cg_field_object_identity (sub_receiver, field_object_cache);
	  if (!identity)
	    identity = cg_gimple_object_identity (sub_receiver);
	  if (!identity)
	    continue;
	  cg_field_fact *callee_fact = state.call.get ({identity, rhs_callee});
	  if (!callee_fact
	      || (require_conveyor && !callee_fact->conveyor_established))
	    continue;

	  enum oa_range_subsumption_result r
	    = cg_range_pair_relation (param_range, rel_code, callee_fact->range);
	  if (r == OA_RANGE_SUBSUMED)
	    verdict_out->add (call);
	  else if (r == OA_RANGE_DISJOINT)
	    {
	      verdict_out->add (call);
	      char established_buf[128];
	      cg_range_lite_text (param_range, established_buf,
				   sizeof (established_buf));
	      cg_obligation_error_at (gimple_location (call),
				       "argument %qE provably violates the precondition "
				       "of %qD", cg_display_value (sub_param), callee);
	      inform (gimple_location (call),
		      "%qE is established %s, but the precondition requires "
		      "it to be %s the result of %qD",
		      cg_display_value (sub_param), established_buf,
		      op_symbol_code (rel_code), rhs_callee);
	    }
	}
    }
}

/* Bounds-proving demo, contradiction tier: the "param vs call" analogue
   of cg_check_call_range_relational immediately above, but for the
   "explicit fact vs REL_CODE" shape (both proving TRUE, and, via
   contracts.cc's own oa_call_relational_contradicts_p technique, proving
   FALSE) instead of a range-vs-range fallback. Needs to run here,
   against dominator-correct STATE.call_rel directly, for the exact same
   architectural reason: cg_check_call has no dominator-tree awareness of
   its own, and the only other source it could consult -- SCALAR_CALL_
   REL_CACHE, cg_predicate_facts_walk's own function-wide flattening of
   every block's own .call_rel -- is deliberately unsound for this
   purpose (see that flattening's own comment): a PARM_DECL/SSA name
   compared in an if-condition can carry different, even De Morgan-
   negated, facts across the then and else arms, and the flattened
   cache's own per-key entry, once two blocks disagree, reflects
   whichever block happened to be visited last (hash_map iteration order
   depends on block pointer addresses, hence non-deterministic run to
   run) -- not necessarily the one valid at this specific call site.

   This isn't merely an imprecision risk. It was first found as a false
   POSITIVE (a call inside an if's then-arm, using a shape that should
   only ever reach "cannot verify," intermittently got a false, hard
   "provably violates" once a contradiction check first tried to consult
   the flattened cache directly from cg_check_call) -- but the same
   mechanism, in the other direction, is a false NEGATIVE: a genuinely
   ambiguous call (unprovable either way) can intermittently be picked up
   as *silently proven safe* by the "implies REL_CODE" direction instead,
   if the wrong branch's fact happens to imply it. Proving unprovable
   code safe is exactly as unsound as erroring on provably-safe code, so
   both directions of this "explicit fact" check belong here, never in
   cg_check_call.

   STATE.call_rel is passed as cg_get_call_relational's own "established"
   argument, with a *fresh, empty* map passed as its "scalar cache
   fallback" argument -- deliberately never falling through to the
   ambiguous, function-wide flattened cache. If the dominator-correct
   state doesn't have the fact directly (including via cg_get_call_
   relational's own PLUS_EXPR/MINUS_EXPR shift chase), this simply
   declines, exactly as if the check hadn't run at all, rather than reach
   for approximate data. Same reasoning for a non-constant shift amount's
   own range lookup: the empty range maps below mean such a shift is
   never resolved here -- an accepted, narrow limitation (every shift in
   this bounds-proving demo so far is a compile-time literal).

   Either a proof or a contradiction records CALL in VERDICT_OUT so cg_
   check_call's own later, separate pass over the same call -- which
   still runs its own, self-trust-only "explicit fact" attempt, safe
   because self-trust facts are function-wide and never branch-dependent
   -- knows not to also warn "cannot verify" for a conjunct this pass
   already resolved one way or the other, mirroring cg_check_call_range_
   relational's own identical use of VERDICT_OUT.  */

static void
cg_check_call_relational_fact (gcall *call,
				  cg_dom_fact_state &state,
				  hash_map<tree, cg_range_lite> &established_range,
				  gimple_ranger *ranger,
				  hash_set<gimple *> *verdict_out)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      bool conveyor_active = oa_contract_conveyor_active_cached_p (contract);
      bool symbolic_active = oa_contract_symbolic_active_cached_p (contract);
      /* See cg_check_call's own identical 'mandatory'.  */
      bool mandatory = (cg_callee_pre_mandatory && conveyor_active
			&& DECL_DECLARED_CONVEYOR_P (callee));
      if (cg_callee_pre_only && !mandatory)
	continue;
      bool check_as_conveyor = (mandatory
				|| (conveyor_active
				    && flag_contract_conveyor_proofs_gimple
				    && cg_proof_checks_p (contract, callee,
							  true)));
      bool check_as_symbolic = (!cg_callee_pre_only && symbolic_active
				&& flag_contract_symbolic_proofs_gimple
				&& cg_proof_checks_p (contract, callee, false));
      if (!check_as_conveyor && !check_as_symbolic)
	continue;
      bool require_conveyor = check_as_conveyor;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rhs_receiver, rhs_callee;
	  tree_code rel_code;
	  /* Also 'idx - v.size () > 10': '(idx - REQUIRED_OFFSET) CODE
	     v.size ()'.  */
	  widest_int required_offset = 0;
	  if (!oa_match_comparison_against_call (*conjuncts[i], &rel_param,
						  &rel_code, &rhs_receiver,
						  &rhs_callee,
						  /*allow_symbolic_accessor=*/
						    !require_conveyor)
	      && !oa_match_shifted_comparison_against_call_text
		    (*conjuncts[i], &rel_param, &rel_code, &rhs_receiver,
		     &rhs_callee, &required_offset,
		     /*allow_symbolic_accessor=*/!require_conveyor))
	    continue;

	  unsigned param_argno, receiver_argno;
	  if (!cg_find_param_position (callee, rel_param, &param_argno)
	      || !cg_find_param_position (callee, rhs_receiver, &receiver_argno)
	      || param_argno >= gimple_call_num_args (call)
	      || receiver_argno >= gimple_call_num_args (call))
	    continue;

	  tree sub_param = cg_resolve_value_argument (call, param_argno);
	  tree sub_receiver = cg_resolve_value_argument (call, receiver_argno);

	  hash_map<tree, cg_call_rel_fact> empty_call_rel_cache;
	  hash_map<tree, cg_range_lite> empty_scalar_range_cache;

	  tree_code fact_code;
	  tree fact_rhs_receiver, fact_rhs_callee;
	  cg_range_lite fact_offset;
	  bool fact_conveyor_established;
	  bool have_fact
	    = cg_get_call_relational (sub_param, state.call_rel,
				       empty_call_rel_cache, established_range,
				       empty_scalar_range_cache, ranger,
				       check_as_conveyor, check_as_symbolic,
				       &fact_code, &fact_rhs_receiver,
				       &fact_rhs_callee, &fact_offset,
				       &fact_conveyor_established);
	  if (!have_fact)
	    continue;
	  if (fact_rhs_callee != rhs_callee
	      || (oa_strip_to_receiver_identity (fact_rhs_receiver)
		  != oa_strip_to_receiver_identity (sub_receiver)))
	    {
	      cg_note_unprovable_hint (sub_param, OA_UNPROVABLE_WRONG_IDENTITY);
	      continue;
	    }
	  if (require_conveyor && !fact_conveyor_established)
	    {
	      cg_note_unprovable_hint (sub_param,
				       OA_UNPROVABLE_WEAKER_PROVENANCE);
	      continue;
	    }

	  /* The fact '(idx - A) CODE call' against the requirement
	     '(idx - K) REL_CODE call': in terms of 'idx - K', the fact's
	     offset is A - K.  */
	  cg_range_lite rel_offset = fact_offset;
	  if (rel_offset.has_lo)
	    rel_offset.lo -= required_offset;
	  if (rel_offset.has_hi)
	    rel_offset.hi -= required_offset;
	  if (oa_relational_code_implies (fact_code, rel_code)
	      && cg_offset_compatible_with_code (rel_offset, rel_code))
	    {
	      verdict_out->add (call);
	      continue; /* Proven true: silently discharged.  */
	    }

	  if (cg_call_relational_contradicts_p (fact_code, rel_offset, rel_code))
	    {
	      verdict_out->add (call);
	      cg_obligation_error_at (gimple_location (call),
				       "argument %qE provably violates the precondition "
				       "of %qD", cg_display_value (sub_param), callee);
	      char established_buf[128];
	      if (fact_offset.has_lo && fact_offset.has_hi
		  && fact_offset.lo == fact_offset.hi && fact_offset.lo == 0)
		snprintf (established_buf, sizeof (established_buf),
			  "%s the call's result", op_symbol_code (fact_code));
	      else if (fact_offset.has_lo && fact_offset.has_hi
		       && fact_offset.lo == fact_offset.hi)
		snprintf (established_buf, sizeof (established_buf),
			  "%s the call's result plus " HOST_WIDE_INT_PRINT_DEC,
			  op_symbol_code (fact_code), fact_offset.lo.to_shwi ());
	      else
		{
		  char off_buf[64];
		  cg_range_lite_text (fact_offset, off_buf, sizeof (off_buf));
		  snprintf (established_buf, sizeof (established_buf),
			    "%s the call's result plus an offset %s",
			    op_symbol_code (fact_code), off_buf);
		}
	      inform (gimple_location (call),
		      "%qE is established %s, but the precondition requires "
		      "it to be %s the result of %qD",
		      cg_display_value (sub_param), established_buf,
		      op_symbol_code (rel_code), rhs_callee);
	    }
	  else
	    cg_note_unprovable_hint (sub_param, OA_UNPROVABLE_RANGE_PARTIAL);
	}

      /* 'v.size () < w.size ()' required, against what branches have
	 established (self-trust is cg_check_call's).  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree lhs_receiver, lhs_callee, rhs_receiver, rhs_callee;
	  tree_code call_code;
	  if (!oa_match_call_against_call (*conjuncts[i], &lhs_receiver,
					     &lhs_callee, &call_code,
					     &rhs_receiver, &rhs_callee,
					     /*allow_symbolic_accessor=*/
					       !require_conveyor))
	    continue;
	  unsigned lhs_argno, rhs_argno;
	  if (!cg_find_param_position (callee, lhs_receiver, &lhs_argno)
	      || !cg_find_param_position (callee, rhs_receiver, &rhs_argno)
	      || lhs_argno >= gimple_call_num_args (call)
	      || rhs_argno >= gimple_call_num_args (call))
	    continue;
	  tree l_identity
	    = cg_gimple_object_identity (cg_resolve_value_argument (call,
								  lhs_argno));
	  tree r_identity
	    = cg_gimple_object_identity (cg_resolve_value_argument (call,
								  rhs_argno));
	  if (!l_identity || !r_identity)
	    continue;
	  const cg_call_call_rel_fact *fact
	    = state.call_call.get ({l_identity, lhs_callee});
	  if (fact
	      && fact->rhs_callee == rhs_callee
	      && cg_gimple_object_identity (fact->rhs_receiver) == r_identity
	      && oa_relational_code_implies (fact->code, call_code)
	      && cg_proven_call_call_conjuncts)
	    {
	      cg_proven_call_call_conjuncts->add ({call, *conjuncts[i]});
	      continue;
	    }
	  /* Each side's own range ('a.size () == 100' after a call whose
	     postcondition says so).  */
	  const cg_field_fact *lr = state.call.get ({l_identity, lhs_callee});
	  const cg_field_fact *rr = state.call.get ({r_identity, rhs_callee});
	  if (!lr || !rr
	      || (require_conveyor
		  && (!lr->conveyor_established || !rr->conveyor_established)))
	    continue;
	  enum oa_range_subsumption_result r
	    = cg_range_pair_relation (lr->range, call_code, rr->range);
	  if (r == OA_RANGE_PARTIAL || !cg_proven_call_call_conjuncts)
	    continue;
	  cg_proven_call_call_conjuncts->add ({call, *conjuncts[i]});
	  if (r == OA_RANGE_DISJOINT)
	    {
	      tree l_arg = cg_resolve_value_argument (call, lhs_argno);
	      tree r_arg = cg_resolve_value_argument (call, rhs_argno);
	      char established_buf[128];
	      cg_range_lite_text (lr->range, established_buf,
				   sizeof (established_buf));
	      cg_obligation_error_at (gimple_location (call),
				       "argument %qE provably violates the precondition "
				       "of %qD", cg_display_argument (l_arg, lhs_receiver),
				       callee);
	      inform (gimple_location (call),
		      "%qD called on %qE is established %s, but the "
		      "precondition requires it to be %s %qD called on %qE",
		      lhs_callee, cg_display_argument (l_arg, lhs_receiver),
		      established_buf, op_symbol_code (call_code), rhs_callee,
		      cg_display_argument (r_arg, rhs_receiver));
	    }
	}

      /* The plain param-vs-param analogue of the call-relational loop
	 just above (e.g. 'pre<ctrl>(x < q)'), same architectural reason:
	 cg_check_call's own attempt at this shape reads SCALAR_REL_CACHE,
	 cg_predicate_facts_walk's own flattened union of every block's
	 own .rel -- found via direct testing (d4324-gimple-conveyor-
	 relational-ifcond.C, intermittently, run to run) to have exactly
	 the same false-negative failure mode already fixed for .call_rel
	 above: a function whose own if-condition establishes 'x < q' in
	 the then-arm (and, via De Morgan, 'x >= q' in the else-arm) can
	 have the *else* arm's own negated fact win the flattening's own
	 non-deterministic last-write-wins union, so a call inside the
	 then-arm -- where 'x < q' provably holds -- occasionally, wrongly
	 fails to prove it and falls through to "cannot verify". Unlike
	 .call_rel, this shape has no contradiction check on either engine
	 (oa_match_comparison_against_param's own AST-side consumer only
	 ever proves true too), so only that one direction is needed here.
	 STATE.rel is dominator-correct for CALL specifically, exactly like
	 STATE.call_rel above, for the same reason.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rel_other;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_param (*conjuncts[i], &rel_param,
						   &rel_code, &rel_other))
	    continue;

	  unsigned param_argno, other_argno;
	  if (!cg_find_param_position (callee, rel_param, &param_argno)
	      || !cg_find_param_position (callee, rel_other, &other_argno)
	      || param_argno >= gimple_call_num_args (call)
	      || other_argno >= gimple_call_num_args (call))
	    continue;

	  tree sub_param = cg_resolve_value_argument (call, param_argno);
	  tree sub_other = cg_resolve_value_argument (call, other_argno);

	  if (TREE_CODE (sub_param) == INTEGER_CST && TREE_CODE (sub_other) == INTEGER_CST)
	    continue; /* cg_check_call's own literal-vs-literal case handles this.  */

	  hash_map<tree, cg_rel_fact> empty_rel_cache;
	  hash_map<tree, cg_range_lite> empty_scalar_range_cache2;

	  tree_code fact_code;
	  tree fact_rhs;
	  cg_range_lite fact_offset;
	  bool fact_conveyor_established;
	  if (cg_get_relational (sub_param, state.rel, empty_rel_cache,
				   established_range, empty_scalar_range_cache2,
				   ranger, check_as_conveyor, check_as_symbolic,
				   &fact_code, &fact_rhs, &fact_offset,
				   &fact_conveyor_established)
	      && oa_relational_code_implies (fact_code, rel_code)
	      && cg_offset_compatible_with_code (fact_offset, rel_code)
	      && (!require_conveyor || fact_conveyor_established)
	      && fact_rhs == sub_other)
	    verdict_out->add (call); /* Proven true: silently discharged.  */
	}
    }
}

/* CALL stores its result in a pointer variable in memory: whether that
   result is an object's address, as the prover would find for an SSA
   result -- here, an immediately invoked lambda returning a capture's
   address (cg_iile_returned_capture_address).  */

static void
cg_record_call_result_facts (gcall *call, cg_dom_fact_state &state,
			     hash_map<tree, cg_fact> *established_oa)
{
  tree lhs = gimple_call_lhs (call);
  if (!lhs || !established_oa || !(VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL)
      || !POINTER_TYPE_P (TREE_TYPE (lhs))
      || !cg_scalar_memory_tracking_p (cfun, lhs))
    return;
  tree stored = cg_iile_returned_capture_address (call);
  if (!stored)
    return;
  hash_set<tree> in_progress;
  if (!cg_provable_object_address_p (stored, *established_oa,
				     /*require_conveyor=*/true, in_progress))
    return;
  cg_field_key_hash::value_type key = cg_scalar_memory_fact_key (lhs);
  cg_field_fact *existing = state.field.get (key);
  cg_field_fact fact = existing ? *existing : cg_field_fact { {}, true };
  fact.object_address = true;
  state.field.put (key, fact);
}

static bool
cg_predicate_facts_walk (function *fun, hash_map<tree, cg_range_lite> *scalar_range_cache_out,
			   hash_map<tree, cg_range_lite> &established_range,
			   hash_set<gimple *> *call_relational_verdict_out,
			   gimple_ranger *ranger,
			   hash_map<tree, cg_fact> &established_nz,
			   cg_assert_memory_verdict_map *assert_memory_verdicts,
			   hash_map<tree, frange> *established_frange,
			   hash_map<tree, cg_fact> *established_oa)
{
  int *rpo = XNEWVEC (int, n_basic_blocks_for_fn (fun));
  int rpo_num = pre_and_rev_post_order_compute (NULL, rpo, false);

  hash_map<basic_block, cg_dom_fact_state *> block_out;
  cg_dom_fact_state seed;
  cg_seed_predicate_self_trust (fun, seed);
  /* Stage 5: shared across BOTH phases below (the fixed-point
     computation and the final consult-only re-derivation) -- a key
     synthesized for a given (base, field) pair during the fixed-point
     phase is referenced from BLOCK_OUT's own saved cg_dom_fact_state
     entries, so the final phase must resolve the identical (base,
     field) pair to that same key, not a freshly synthesized one, or
     every fact carried forward from BLOCK_OUT would become permanently
     unmatchable.  */
  hash_map<cg_field_key_hash, tree> field_object_cache;

  /* Fixed-point computation, deliberately WITHOUT consulting (cg_
     consult_persistent_facts is the one call among the three that has
     an externally-visible side effect, warning_at) -- a block may be
     recomputed several times before BLOCK_OUT stabilizes, and a
     warning must be reported exactly once, not once per recomputation.
     Only the two state-mutating effects (invalidate, establish) are
     needed to reach a stable fixed point at all.  */
  bool changed = true;
  while (changed)
    {
      changed = false;
      for (int i = 0; i < rpo_num; ++i)
	{
	  basic_block bb = BASIC_BLOCK_FOR_FN (fun, rpo[i]);
	  cg_dom_fact_state in_state;
	  /* REQUIRE_CONVEYOR/REQUIRE_SYMBOLIC both false: this
	     range lookup is for a plain branch-derived/self-trust
	     numeric fact (e.g. 'idx >= 0'), never a postcondition-
	     derived one -- cg_call_postcondition_range_p's own
	     "!require_conveyor && !require_symbolic" skip means
	     this correctly never trusts a postcondition fact here,
	     which is the right, conservative behavior: nothing
	     about this lookup should depend on which proof flavor
	     happens to be active.  */
	  cg_compute_in_state (bb, block_out, seed, &in_state,
				established_range, ranger,
				/*require_conveyor=*/false,
				/*require_symbolic=*/false);

	  cg_dom_fact_state out_state;
	  cg_dom_fact_state_assign (&out_state, in_state);
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	       gsi_next (&gsi))
	    {
	      gimple *stmt = gsi_stmt (gsi);
	      if (gimple_assign_single_p (stmt))
		{
		  cg_record_field_load_facts (stmt, out_state,
					      *scalar_range_cache_out,
					      established_nz,
					      established_frange,
					      established_oa);
		  cg_process_field_write (gimple_assign_lhs (stmt),
					   gimple_assign_rhs1 (stmt), fun,
					   field_object_cache, out_state);
		  cg_process_scalar_write (gimple_assign_lhs (stmt), out_state);
		  cg_record_write_facts (stmt, out_state, established_range,
					 *scalar_range_cache_out, ranger,
					 established_nz, established_frange,
					 established_oa);
		}
	      else if (is_gimple_call (stmt))
		{
		  gcall *call = as_a <gcall *> (stmt);
		  /* Statement-internal ordering: a call's own LHS field
		     write (the RVO/NRVO shape above) is narrower and more
		     specific than this same call's own argument-based
		     invalidation/postcondition-establishment, so it runs
		     first -- mirrors this file's own existing "narrowest,
		     most specific effect first" discipline (cg_establish_
		     persistent_facts_for_call's own comment: a call's own
		     postcondition must win over its own, necessarily
		     stale-by-then, argument invalidation).  */
		  if (gimple_call_lhs (call))
		    {
		      cg_process_field_write (gimple_call_lhs (call), NULL_TREE,
					       fun, field_object_cache, out_state);
		      cg_process_scalar_write (gimple_call_lhs (call), out_state);
		    }
		  cg_invalidate_persistent_facts_for_call_args
		    (call, fun, field_object_cache, out_state);
		  cg_establish_persistent_facts_for_call (call, field_object_cache,
							   out_state);
		  cg_record_call_result_facts (call, out_state, established_oa);
		  if (!cg_assert_memory_conjunct_false_p (call, out_state))
		    cg_establish_assert_facts (call, out_state);
		}
	    }

	  cg_dom_fact_state **existing = block_out.get (bb);
	  if (!existing || !cg_dom_fact_state_equal (**existing, out_state))
	    {
	      if (!existing)
		block_out.put (bb, new cg_dom_fact_state ());
	      cg_dom_fact_state_assign (*block_out.get (bb), out_state);
	      changed = true;
	    }
	}
    }

  /* D4324 Commit 3, since superseded: neither .rel nor .call_rel is
     flattened into a function-wide cache any more. A PARM_DECL/SSA name
     compared in an if-condition can genuinely carry two different, even
     De Morgan-negated, facts across the then and else arms of the very
     same branch -- unlike a plain SSA name defined once by an ordinary
     assignment, whose value (and so whose fact) truly is permanent from
     that point on. Flattening a union of every block's own entries into
     one function-wide map necessarily picks whichever block's entry
     happens to be visited last for a key two blocks disagree on
     (hash_map iteration order depends on block pointer addresses, hence
     non-deterministic run to run), which is not necessarily the one
     valid at any given consult site. Found via direct testing to be a
     genuine, two-directional soundness gap, not mere imprecision, for
     *both* maps' own former consumer in cg_check_call (which has no
     dominator-tree awareness of its own): first for .call_rel, as a
     false hard "provably violates" (wrong branch's fact flatly
     contradicted the required code) and a false *silent proof* of a
     genuinely unprovable call (wrong branch's fact happened to imply
     the required code); then, found the same way once this comment's
     own earlier version flagged .rel as "not yet confirmed... worth the
     same scrutiny if one ever turns up" -- one did
     (d4324-gimple-conveyor-relational-ifcond.C, intermittently) -- the
     same false-silent-proof direction for .rel too (.rel has no
     contradiction check on either engine, so only that one direction
     applies to it). Both shapes' own "explicit fact vs REL_CODE" checks
     now run only from this function's own final pass below, against
     dominator-correct STATE.rel/STATE.call_rel directly (see cg_check_
     call_relational_fact's own comment) -- no flattening of either map
     needed at all any more, since a per-block dominator-tracked map is
     exactly the right level of precision already.  */

  /* Final pass, over the now-stable BLOCK_OUT: re-derive each block's
     own IN state one more time and run the full per-statement dispatch,
     including consult this time, so every "cannot verify" diagnostic
     is reported exactly once, against final, fully-converged facts.  */
  for (int i = 0; i < rpo_num; ++i)
    {
      basic_block bb = BASIC_BLOCK_FOR_FN (fun, rpo[i]);
      cg_dom_fact_state state;
      cg_compute_in_state (bb, block_out, seed, &state, established_range,
			    ranger, /*require_conveyor=*/false,
			    /*require_symbolic=*/false);
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  if (gimple_assign_single_p (stmt))
	    {
	      cg_record_field_load_facts (stmt, state, *scalar_range_cache_out,
					  established_nz, established_frange,
					  established_oa);
	      cg_process_field_write (gimple_assign_lhs (stmt),
				       gimple_assign_rhs1 (stmt), fun,
				       field_object_cache, state);
	      cg_process_scalar_write (gimple_assign_lhs (stmt), state);
	      cg_record_write_facts (stmt, state, established_range,
				     *scalar_range_cache_out, ranger,
				     established_nz, established_frange,
				     established_oa);
	    }
	  else if (is_gimple_call (stmt))
	    {
	      gcall *call = as_a <gcall *> (stmt);
	      cg_consult_persistent_facts (call, field_object_cache, state);
	      /* Bounds-proving demo, Part 2: STATE is dominator-correct for
		 CALL exactly here, before any of this same call's own
		 invalidation below -- see cg_check_call_range_relational's
		 own comment for why this specific check can't live in
		 cg_check_call instead.  */
	      cg_check_call_range_relational (call, field_object_cache, state,
						ranger, call_relational_verdict_out);
	      /* Same STATE-dominator-correctness requirement as the range-
		 vs-range check just above -- see cg_check_call_relational_
		 fact's own comment for why the explicit-fact check (both
		 proving true and proving a contradiction) can't live in
		 cg_check_call either.  */
	      cg_check_call_relational_fact (call, state, established_range,
					       ranger, call_relational_verdict_out);
	      /* Before this same call's own argument invalidation just
		 below, so it sees the receiver's facts exactly as the
		 caller left them -- see cg_compose_call_result_range's
		 own comment.  */
	      cg_compose_call_result_range (call, state, *scalar_range_cache_out);
	      if (gimple_call_lhs (call))
		{
		  cg_process_field_write (gimple_call_lhs (call), NULL_TREE, fun,
					   field_object_cache, state);
		  cg_process_scalar_write (gimple_call_lhs (call), state);
		}
	      cg_invalidate_persistent_facts_for_call_args (call, fun,
							     field_object_cache, state);
	      cg_establish_persistent_facts_for_call (call, field_object_cache, state);
	      cg_record_call_result_facts (call, state, established_oa);
	      cg_record_assert_memory_verdicts (call, state,
						assert_memory_verdicts);
	      if (!cg_assert_memory_conjunct_false_p (call, state))
		cg_establish_assert_facts (call, state);
	    }
	}
    }

  for (auto it : block_out)
    delete it.second;
  XDELETEVEC (rpo);
  return true;
}

/* contract_assert as a source of facts.  A conveyor or symbolic
   contract_assert establishes its condition for the statements it
   dominates, as the AST engine's walk does for the statements after it.
   The facts recorded here are the ones keyed on an SSA value -- whether
   an address is an object address, whether a scalar is nonzero, and a
   scalar's range -- which, unlike a fact about memory, nothing later in
   the function can invalidate.  They are added to the function-wide
   fact maps while the checking walk is inside the asserting statement's
   dominator subtree (cg_assert_fact_scope), so every existing consult
   sees them exactly where they hold.

   A check site (build_contract_check) hands the control object's
   operator() the address of an assertion_context whose __args field
   points to a capture struct; field I of that struct holds the address
   (or, for a reference, the value) of the Ith decl that contracts.cc's
   contract_check_arg_struct_info records for the struct's type, along
   with the condition as written.  */

/* The value stored to LHS (a COMPONENT_REF or a decl) by the nearest
   preceding single assignment in AT's own block, or NULL_TREE.  */

static tree
cg_value_stored_before (gimple *at, tree lhs)
{
  gimple_stmt_iterator gsi = gsi_for_stmt (at);
  for (gsi_prev (&gsi); !gsi_end_p (gsi); gsi_prev (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (gimple_assign_single_p (stmt)
	  && operand_equal_p (gimple_assign_lhs (stmt), lhs, 0))
	return gimple_assign_rhs1 (stmt);
    }
  return NULL_TREE;
}

/* If CALL is a contract check's call to its control object's
   operator(), return the contract and set *STRUCT_VAR_OUT to the
   check's capture struct variable, *COND_OUT to its condition as
   written, and *DECLS_OUT to the decls its fields capture (see
   contract_check_arg_struct_info).  */

static tree
cg_contract_check_call_p (gcall *call, tree *struct_var_out, tree *cond_out,
			  tree *decls_out)
{
  /* A symbolic contract's marker: its capture struct directly.  */
  if (gimple_call_internal_p (call, IFN_CONTRACT_MARK))
    {
      tree arg = gimple_call_arg (call, 0);
      if (TREE_CODE (arg) != ADDR_EXPR || !VAR_P (TREE_OPERAND (arg, 0)))
	return NULL_TREE;
      tree struct_var = TREE_OPERAND (arg, 0);
      tree contract = contract_check_arg_struct_info (TREE_TYPE (struct_var),
						      cond_out, decls_out);
      if (contract)
	*struct_var_out = struct_var;
      return contract;
    }
  for (unsigned i = 0; i < gimple_call_num_args (call); ++i)
    {
      tree arg = gimple_call_arg (call, i);
      if (TREE_CODE (arg) != ADDR_EXPR || !VAR_P (TREE_OPERAND (arg, 0)))
	continue;
      tree ctx = TREE_OPERAND (arg, 0);
      /* The latest store of an address to CTX's '__args' field.  */
      gimple_stmt_iterator gsi = gsi_for_stmt (call);
      for (gsi_prev (&gsi); !gsi_end_p (gsi); gsi_prev (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  if (!gimple_assign_single_p (stmt))
	    continue;
	  tree lhs = gimple_assign_lhs (stmt);
	  if (TREE_CODE (lhs) != COMPONENT_REF
	      || TREE_OPERAND (lhs, 0) != ctx)
	    continue;
	  tree field = TREE_OPERAND (lhs, 1);
	  if (!DECL_NAME (field) || !id_equal (DECL_NAME (field), "__args"))
	    continue;
	  tree stored = gimple_assign_rhs1 (stmt);
	  if (TREE_CODE (stored) != ADDR_EXPR
	      || !VAR_P (TREE_OPERAND (stored, 0)))
	    break;
	  tree struct_var = TREE_OPERAND (stored, 0);
	  tree contract
	    = contract_check_arg_struct_info (TREE_TYPE (struct_var),
					      cond_out, decls_out);
	  if (!contract)
	    break;
	  *struct_var_out = struct_var;
	  return contract;
	}
    }
  return NULL_TREE;
}

/* The value DECL had at contract check CALL, as captured in STRUCT_VAR
   (DECLS as from cg_contract_check_call_p): for a reference, the
   address it is bound to; otherwise its scalar value, when that is an
   SSA name or a constant.  NULL_TREE if DECL isn't captured or its
   value isn't one of those -- e.g. it lives in memory.  */

static tree
cg_contract_check_decl_value (gcall *call, tree struct_var, tree decls,
			      tree decl)
{
  /* A postcondition's result identifier names the result, which its
     check captures last (DECL_RESULT).  */
  tree contract = TREE_VEC_ELT (decls, 0);
  if (POSTCONDITION_P (contract)
      && decl == POSTCONDITION_IDENTIFIER (contract)
      && TREE_VEC_LENGTH (decls) > 2)
    decl = TREE_VEC_ELT (decls, TREE_VEC_LENGTH (decls) - 1);
  tree field = TYPE_FIELDS (TREE_TYPE (struct_var));
  for (int i = 2; i < TREE_VEC_LENGTH (decls) && field;
       ++i, field = DECL_CHAIN (field))
    {
      if (TREE_VEC_ELT (decls, i) != decl)
	continue;
      tree ref = build3 (COMPONENT_REF, TREE_TYPE (field), struct_var, field,
			 NULL_TREE);
      tree stored = cg_value_stored_before (call, ref);
      if (!stored)
	return NULL_TREE;
      if (TYPE_REF_P (TREE_TYPE (decl)) && !DECL_BY_REFERENCE (decl))
	return TREE_CODE (stored) == SSA_NAME ? stored : NULL_TREE;
      if (TREE_CODE (stored) != ADDR_EXPR)
	return NULL_TREE;
      tree slot = TREE_OPERAND (stored, 0);
      /* DECL's own storage: its value is in memory.  */
      if (slot == decl || !VAR_P (slot))
	return NULL_TREE;
      tree val = cg_value_stored_before (call, slot);
      if (val && (TREE_CODE (val) == SSA_NAME || TREE_CODE (val) == INTEGER_CST))
	return val;
      return NULL_TREE;
    }
  return NULL_TREE;
}

/* The facts contract_assert checks add to the fact maps while the
   checking walk is inside their dominator subtrees, with what each one
   replaced so leaving the subtree can restore it.  */

class cg_assert_fact_scope
{
public:
  cg_assert_fact_scope (hash_map<tree, cg_fact> &established,
			hash_map<tree, cg_fact> &established_nz,
			hash_map<tree, cg_range_lite> &established_range,
			hash_map<tree, cg_type_bound_fact> &established_type_bound,
			hash_map<tree, frange> &established_frange)
    : m_established (established), m_established_nz (established_nz),
      m_established_range (established_range),
      m_established_type_bound (established_type_bound),
      m_established_frange (established_frange)
  {}

  unsigned mark () const { return m_undo.length (); }
  void restore (unsigned mark);
  void establish (gcall *call);

private:
  enum which { OA, NZ, RANGE, TYPE_BOUND, FRANGE };
  struct undo
  {
    which map;
    tree key;
    bool had;
    cg_fact fact;
    cg_range_lite range;
    cg_type_bound_fact type_bound;
    frange float_range;
  };

  void put_fact (which map, tree key, cg_fact fact);
  void put_range (tree key, const cg_range_lite &range);
  void put_type_bound (tree key, const cg_type_bound_fact &tb);
  void put_frange (tree key, const frange &r);

  hash_map<tree, cg_fact> &m_established;
  hash_map<tree, cg_fact> &m_established_nz;
  hash_map<tree, cg_range_lite> &m_established_range;
  hash_map<tree, cg_type_bound_fact> &m_established_type_bound;
  hash_map<tree, frange> &m_established_frange;
  auto_vec<undo> m_undo;
};

void
cg_assert_fact_scope::put_frange (tree key, const frange &r)
{
  undo u = { FRANGE, key, false, {}, {}, {}, frange () };
  frange merged = r;
  if (frange *old = m_established_frange.get (key))
    {
      u.had = true;
      u.float_range = *old;
      merged.intersect (*old);
    }
  m_undo.safe_push (u);
  m_established_frange.put (key, merged);
}

void
cg_assert_fact_scope::put_fact (which map, tree key, cg_fact fact)
{
  hash_map<tree, cg_fact> &m = map == OA ? m_established : m_established_nz;
  undo u = { map, key, false, {}, {}, {}, frange () };
  if (cg_fact *old = m.get (key))
    {
      /* A conveyor-established fact stays one.  */
      if (old->conveyor_established || !fact.conveyor_established)
	return;
      u.had = true;
      u.fact = *old;
    }
  m_undo.safe_push (u);
  m.put (key, fact);
}

void
cg_assert_fact_scope::put_range (tree key, const cg_range_lite &range)
{
  undo u = { RANGE, key, false, {}, {}, {}, frange () };
  cg_range_lite merged = range;
  if (cg_range_lite *old = m_established_range.get (key))
    {
      u.had = true;
      u.range = *old;
      if (old->has_lo && (!merged.has_lo || old->lo > merged.lo))
	merged.has_lo = true, merged.lo = old->lo;
      if (old->has_hi && (!merged.has_hi || old->hi < merged.hi))
	merged.has_hi = true, merged.hi = old->hi;
    }
  m_undo.safe_push (u);
  m_established_range.put (key, merged);
}

void
cg_assert_fact_scope::put_type_bound (tree key, const cg_type_bound_fact &tb)
{
  undo u = { TYPE_BOUND, key, false, {}, {}, {}, frange () };
  cg_type_bound_fact merged = tb;
  if (cg_type_bound_fact *old = m_established_type_bound.get (key))
    {
      u.had = true;
      u.type_bound = *old;
      merged.has_upper_witness |= old->has_upper_witness;
      merged.has_lower_witness |= old->has_lower_witness;
    }
  m_undo.safe_push (u);
  m_established_type_bound.put (key, merged);
}

void
cg_assert_fact_scope::restore (unsigned mark)
{
  while (m_undo.length () > mark)
    {
      undo u = m_undo.last ();
      m_undo.pop ();
      switch (u.map)
	{
	case OA:
	case NZ:
	  {
	    hash_map<tree, cg_fact> &m
	      = u.map == OA ? m_established : m_established_nz;
	    if (u.had)
	      m.put (u.key, u.fact);
	    else
	      m.remove (u.key);
	    break;
	  }
	case RANGE:
	  if (u.had)
	    m_established_range.put (u.key, u.range);
	  else
	    m_established_range.remove (u.key);
	  break;
	case TYPE_BOUND:
	  if (u.had)
	    m_established_type_bound.put (u.key, u.type_bound);
	  else
	    m_established_type_bound.remove (u.key);
	  break;
	case FRANGE:
	  if (u.had)
	    m_established_frange.put (u.key, u.float_range);
	  else
	    m_established_frange.remove (u.key);
	  break;
	}
    }
}

/* If CALL is a conveyor or symbolic contract_assert's check, establish
   the facts its condition states about SSA values -- the same shapes
   cg_seed_self_trust seeds from a precondition.  */

/* Does SSA value V hold the value expression E of a contract condition
   would at contract check CALL (STRUCT_VAR and DECLS as from
   cg_contract_check_call_p): computed alike from the values its
   variables have there?  Only conversions, arithmetic and address
   computations.  */

static bool
cg_expr_computes_p (tree e, tree v, gcall *call, tree struct_var, tree decls,
		    int depth)
{
  e = STRIP_ANY_LOCATION_WRAPPER (e);
  if (depth > 10)
    return false;
  if (TREE_CODE (e) == INTEGER_CST)
    return (TREE_CODE (v) == INTEGER_CST
	    && wi::to_widest (e) == wi::to_widest (v));
  gimple *def = TREE_CODE (v) == SSA_NAME ? SSA_NAME_DEF_STMT (v) : NULL;
  tree_code dcode = (def && is_gimple_assign (def)
		     ? gimple_assign_rhs_code (def) : ERROR_MARK);
  /* A conversion or copy on the GIMPLE side.  */
  if ((CONVERT_EXPR_CODE_P (dcode) || dcode == SSA_NAME)
      && cg_expr_computes_p (e, gimple_assign_rhs1 (def), call, struct_var,
			     decls, depth + 1))
    return true;
  if (VAR_P (e) || TREE_CODE (e) == PARM_DECL)
    {
      tree cap = cg_contract_check_decl_value (call, struct_var, decls, e);
      return cap && cg_same_computed_value_p (cap, v, 0);
    }
  if (CONVERT_EXPR_P (e) || TREE_CODE (e) == NON_LVALUE_EXPR)
    return cg_expr_computes_p (TREE_OPERAND (e, 0), v, call, struct_var,
			       decls, depth + 1);
  if (TREE_CODE (e) == ADDR_EXPR
      && TREE_CODE (TREE_OPERAND (e, 0)) == INDIRECT_REF)
    return cg_expr_computes_p (TREE_OPERAND (TREE_OPERAND (e, 0), 0), v,
			       call, struct_var, decls, depth + 1);
  tree_code code = TREE_CODE (e);
  if ((code == POINTER_PLUS_EXPR || code == PLUS_EXPR || code == MINUS_EXPR
       || code == MULT_EXPR)
      && dcode == code)
    {
      tree a = gimple_assign_rhs1 (def), b = gimple_assign_rhs2 (def);
      if (cg_expr_computes_p (TREE_OPERAND (e, 0), a, call, struct_var,
			      decls, depth + 1)
	  && cg_expr_computes_p (TREE_OPERAND (e, 1), b, call, struct_var,
				 decls, depth + 1))
	return true;
      if ((code == PLUS_EXPR || code == MULT_EXPR)
	  && cg_expr_computes_p (TREE_OPERAND (e, 0), b, call, struct_var,
				 decls, depth + 1)
	  && cg_expr_computes_p (TREE_OPERAND (e, 1), a, call, struct_var,
				 decls, depth + 1))
	return true;
    }
  return false;
}

void
cg_assert_fact_scope::establish (gcall *call)
{
  tree struct_var, cond, decls;
  tree contract = cg_contract_check_call_p (call, &struct_var, &cond, &decls);
  if (!contract || TREE_CODE (contract) != ASSERTION_STMT)
    return;
  /* The flavor cache only holds function contracts.  */
  tree owner = DECL_ORIGIN (current_function_decl);
  bool conveyor_enabled = (flag_contract_conveyor_proofs_gimple
			   && oa_contract_conveyor_active_public (contract,
								  owner));
  bool symbolic_enabled = (flag_contract_symbolic_proofs_gimple
			   && oa_contract_symbolic_active_public (contract,
								  owner));
  if (!conveyor_enabled && !symbolic_enabled)
    return;
  if (cond == NULL_TREE || cond == error_mark_node)
    return;

  auto value_of = [&] (tree decl) -> tree
    {
      decl = STRIP_ANY_LOCATION_WRAPPER (decl);
      if (!VAR_P (decl) && TREE_CODE (decl) != PARM_DECL)
	return NULL_TREE;
      return cg_contract_check_decl_value (call, struct_var, decls, decl);
    };
  auto scalar_decl_p = [] (tree decl)
    {
      return !TYPE_REF_P (TREE_TYPE (decl)) || DECL_BY_REFERENCE (decl);
    };

  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  auto_vec<tree> range_decls;
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree c = *conjuncts[i];
      tree arg;
      if (is_object_address_call_p (c, &arg))
	{
	  arg = STRIP_ANY_LOCATION_WRAPPER (arg);
	  while (TREE_CODE (arg) == NON_LVALUE_EXPR || CONVERT_EXPR_P (arg)
		 || TREE_CODE (arg) == VIEW_CONVERT_EXPR)
	    arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
	  /* '&r' for a reference R is R itself; 'p' for a pointer P.  */
	  if ((VAR_P (arg) || TREE_CODE (arg) == PARM_DECL)
	      && (TYPE_REF_P (TREE_TYPE (arg))
		  || POINTER_TYPE_P (TREE_TYPE (arg))))
	    {
	      if (tree key = value_of (arg))
		if (TREE_CODE (key) == SSA_NAME)
		  put_fact (OA, key, { conveyor_enabled });
	    }
	  /* An address computed from variables ('&st[i]'): every value
	     the function computes alike from the same values -- -O0
	     GIMPLE doesn't share the computation with a later 'st[i].f'.  */
	  else if (TREE_CODE (arg) == ADDR_EXPR
		   || TREE_CODE (arg) == POINTER_PLUS_EXPR)
	    {
	      unsigned k;
	      tree name;
	      FOR_EACH_SSA_NAME (k, name, cfun)
		if (POINTER_TYPE_P (TREE_TYPE (name))
		    && cg_expr_computes_p (arg, name, call, struct_var, decls,
					   0))
		  put_fact (OA, name, { conveyor_enabled });
	    }
	  continue;
	}
      tree decl;
      if (oa_nonzero_conjunct_p (c, &decl))
	{
	  if (scalar_decl_p (decl))
	    if (tree key = value_of (decl))
	      if (TREE_CODE (key) == SSA_NAME)
		put_fact (NZ, key, { conveyor_enabled });
	  continue;
	}
      tree_code code;
      tree const_val;
      if (oa_match_simple_comparison_var_public (c, &decl, &code, &const_val)
	  && TREE_CODE (const_val) == INTEGER_CST
	  && scalar_decl_p (decl) && !range_decls.contains (decl))
	range_decls.safe_push (decl);
      if (oa_match_simple_comparison_var_public (c, &decl, &code, &const_val)
	  && TREE_CODE (const_val) == REAL_CST && scalar_decl_p (decl))
	if (tree key = value_of (decl))
	  {
	    frange r;
	    if (TREE_CODE (key) == SSA_NAME
		&& SCALAR_FLOAT_TYPE_P (TREE_TYPE (key))
		&& cg_frange_for_comparison (TREE_TYPE (key), code, const_val,
					     &r))
	      put_frange (key, r);
	  }
      if (oa_match_type_bounded_comparison (c, &decl, &code)
	  && scalar_decl_p (decl))
	if (tree key = value_of (decl))
	  if (TREE_CODE (key) == SSA_NAME)
	    {
	      cg_type_bound_fact tb;
	      tb.has_upper_witness = (code == LT_EXPR || code == LE_EXPR);
	      tb.has_lower_witness = (code == GT_EXPR || code == GE_EXPR);
	      put_type_bound (key, tb);
	    }
    }

  for (unsigned d = 0; d < range_decls.length (); ++d)
    {
      tree key = value_of (range_decls[d]);
      if (!key || TREE_CODE (key) != SSA_NAME)
	continue;
      cg_range_lite range;
      bool any = false;
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree decl, const_val;
	  tree_code code;
	  if (oa_match_simple_comparison_var_public (*conjuncts[i], &decl,
						     &code, &const_val)
	      && decl == range_decls[d] && TREE_CODE (const_val) == INTEGER_CST)
	    {
	      cg_tighten_range_bound (range, code, wi::to_widest (const_val));
	      any = true;
	    }
	}
      if (any)
	put_range (key, range);
    }
}

/* Seed ESTABLISHED_FRANGE with what FUN's own enabled preconditions
   say about its floating-point parameters ('pre (x >= 0.0)'), the
   floating-point counterpart of cg_seed_self_trust's ranges.  */

static void
cg_seed_float_self_trust (function *fun,
			  hash_map<tree, frange> &established_frange)
{
  for (tree as = get_fn_contract_specifiers (fun->decl); as;
       as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      if (!((oa_contract_conveyor_active_cached_p (contract)
	     && flag_contract_conveyor_proofs_gimple)
	    || (oa_contract_symbolic_active_cached_p (contract)
		&& flag_contract_symbolic_proofs_gimple)))
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;
      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree parm, const_val;
	  tree_code code;
	  frange r;
	  /* 'std::isfinite (x)': neither infinite nor a NaN.  */
	  if (oa_isfinite_conjunct_public (*conjuncts[i], &parm)
	      && TREE_CODE (parm) == PARM_DECL)
	    {
	      tree type = TREE_TYPE (parm);
	      REAL_VALUE_TYPE max = real_max_representable (type);
	      REAL_VALUE_TYPE min = real_min_representable (type);
	      r = frange (type, min, max);
	      r.clear_nan ();
	    }
	  else if (!oa_match_simple_comparison (*conjuncts[i], &parm, &code,
						&const_val)
		   || TREE_CODE (const_val) != REAL_CST
		   || TREE_CODE (parm) != PARM_DECL
		   || !SCALAR_FLOAT_TYPE_P (TREE_TYPE (parm))
		   || !cg_frange_for_comparison (TREE_TYPE (parm), code,
						 const_val, &r))
	    continue;
	  tree ssa = ssa_default_def (fun, parm);
	  if (!ssa)
	    continue;
	  bool existed;
	  frange &slot = established_frange.get_or_insert (ssa, &existed);
	  if (existed)
	    slot.intersect (r);
	  else
	    slot = r;
	}
    }
}

/* Whether the GIMPLE engine checks a contract's own condition (a
   contract_assert's, or a postcondition's at the function's exit): as
   for the AST engine, only under -fcontract-conveyor-proofs /
   -fcontract-symbolic-proofs, or for a control object that forces
   analysis.  The older -fcontract-*-proofs-gimple flags never included
   these checks and don't turn them on.  */

static bool
cg_conveyor_proofs_p ()
{
  return flag_contract_conveyor_proofs;
}

static bool
cg_symbolic_proofs_p ()
{
  return flag_contract_symbolic_proofs;
}

/* cg_range_lite_text for R, a range of a value of TYPE, leaving out a
   bound that is only TYPE's own limit.  */

static char *
cg_range_lite_text_for (cg_range_lite r, tree type, char *buf, size_t n)
{
  if (INTEGRAL_TYPE_P (type))
    {
      signop sgn = TYPE_SIGN (type);
      if (r.has_lo
	  && r.lo == widest_int::from (wi::min_value (TYPE_PRECISION (type),
						       sgn), sgn))
	r.has_lo = false;
      if (r.has_hi
	  && r.hi == widest_int::from (wi::max_value (TYPE_PRECISION (type),
						       sgn), sgn))
	r.has_hi = false;
    }
  return cg_range_lite_text (r, buf, n);
}

/* The range of SSA value VAL at STMT: the facts established for it,
   narrowed by what the ranger knows there.  */

static bool
cg_value_range_at (tree val, gimple *stmt,
		   hash_map<tree, cg_range_lite> &established_range,
		   hash_map<tree, cg_range_lite> &scalar_range_cache,
		   gimple_ranger *ranger, bool require_conveyor,
		   cg_range_lite *out)
{
  if (!INTEGRAL_TYPE_P (TREE_TYPE (val)))
    return false;
  bool any = cg_established_range_of (val, established_range,
				      scalar_range_cache, ranger,
				      require_conveyor, !require_conveyor,
				      out, stmt);
  /* The ranger's, and that of the same value loaded earlier.  */
  auto narrow = [&] (const cg_range_lite &r)
    {
      if (!any)
	{
	  *out = r;
	  any = true;
	  return;
	}
      if (r.has_lo && (!out->has_lo || r.lo > out->lo))
	out->has_lo = true, out->lo = r.lo;
      if (r.has_hi && (!out->has_hi || r.hi < out->hi))
	out->has_hi = true, out->hi = r.hi;
    };
  cg_range_lite r;
  if (TREE_CODE (val) == SSA_NAME
      && cg_trusted_ranger_range (val, stmt, ranger, true,
				  TYPE_SIGN (TREE_TYPE (val)), &r))
    narrow (r);
  auto_vec<tree> twins;
  cg_dominating_load_twins (val, stmt, &twins);
  for (tree twin : twins)
    if (cg_trusted_ranger_range (twin, stmt, ranger, false,
				 TYPE_SIGN (TREE_TYPE (twin)), &r))
      narrow (r);
  return any;
}

/* The verdict on 'x CODE C' (C a REAL_CST) for x in floating-point range
   FR; TEXT describes the range.  A value that may be a NaN makes every
   comparison false, so it only ever rules a comparison out.  */

static cg_verdict
cg_frange_comparison_verdict (const frange &fr, tree_code code, tree c,
			      char *text, size_t n)
{
  if (fr.undefined_p () || fr.known_isnan ())
    return CG_VERDICT_UNKNOWN;
  const REAL_VALUE_TYPE &lo = fr.lower_bound ();
  const REAL_VALUE_TYPE &hi = fr.upper_bound ();
  const REAL_VALUE_TYPE &cv = TREE_REAL_CST (c);
  bool nan = fr.maybe_isnan ();
  char lo_buf[40], hi_buf[40];
  real_to_decimal (lo_buf, &lo, sizeof lo_buf, 0, 1);
  real_to_decimal (hi_buf, &hi, sizeof hi_buf, 0, 1);
  snprintf (text, n, "in [%s, %s]", lo_buf, hi_buf);
  bool lt_c_lo = real_less (&cv, &lo);		/* c < lo */
  bool le_c_lo = !real_less (&lo, &cv);		/* c <= lo */
  bool lt_hi_c = real_less (&hi, &cv);		/* hi < c */
  bool le_hi_c = !real_less (&cv, &hi);		/* hi <= c */
  switch (code)
    {
    case GT_EXPR:
      if (!nan && lt_c_lo) return CG_VERDICT_TRUE;
      if (le_hi_c) return CG_VERDICT_FALSE;
      break;
    case GE_EXPR:
      if (!nan && le_c_lo) return CG_VERDICT_TRUE;
      if (lt_hi_c) return CG_VERDICT_FALSE;
      break;
    case LT_EXPR:
      if (!nan && lt_hi_c) return CG_VERDICT_TRUE;
      if (le_c_lo) return CG_VERDICT_FALSE;
      break;
    case LE_EXPR:
      if (!nan && le_hi_c) return CG_VERDICT_TRUE;
      if (lt_c_lo) return CG_VERDICT_FALSE;
      break;
    case EQ_EXPR:
      if (!nan && le_c_lo && le_hi_c) return CG_VERDICT_TRUE;
      if (lt_c_lo || lt_hi_c) return CG_VERDICT_FALSE;
      break;
    default:
      break;
    }
  return CG_VERDICT_UNKNOWN;
}

/* The same for SSA value VAL at STMT: the ranger's range of it there,
   narrowed by what is established for it (KNOWN, if any).  */


static cg_verdict
cg_float_comparison_verdict (tree val, gimple *stmt, gimple_ranger *ranger,
			     const frange *known, tree_code code, tree c,
			     char *text, size_t n)
{
  frange fr (TREE_TYPE (val));
  /* Folded from its operands' facts too ('x * 2.0').  */
  if (!cg_frange_of (val, stmt, ranger, cg_established_frange_for_verdicts,
		     fr))
    fr.set_varying (TREE_TYPE (val));
  if (known)
    fr.intersect (*known);
  return cg_frange_comparison_verdict (fr, code, c, text, n);
}

/* If CALL is a conveyor or symbolic contract_assert's check, check its
   condition against the facts at it, like the AST engine's
   oa_handle_assertion_stmt: an is_object_address conjunct must be
   provable unless the control object is never_proven; under the proofs
   flags (or analyzed_* / proven_*), a provably false conjunct is an
   error and an unproven one a warning (an error for proven_*).  The
   conjuncts about memory were decided by the fact walk
   (MEMORY_VERDICTS); the ones about SSA values are decided here.
   False if the check failed, so the assert establishes nothing, as in
   the AST engine.  */

static bool
cg_check_contract_assert (gcall *call, hash_map<tree, cg_fact> &established,
			  hash_map<tree, cg_fact> &established_nz,
			  hash_map<tree, cg_range_lite> &established_range,
			  hash_map<tree, cg_rel_fact> &established_rel,
			  hash_map<tree, cg_range_lite> &scalar_range_cache,
			  gimple_ranger *ranger,
			  cg_assert_memory_verdict_map &memory_verdicts,
			  hash_map<tree, frange> &established_frange)
{
  tree struct_var, cond, decls;
  tree contract = cg_contract_check_call_p (call, &struct_var, &cond, &decls);
  bool is_post = POSTCONDITION_P (contract ? contract : error_mark_node);
  if (!contract || (TREE_CODE (contract) != ASSERTION_STMT && !is_post)
      || cond == NULL_TREE || cond == error_mark_node)
    return true;
  tree owner = DECL_ORIGIN (current_function_decl);
  bool conveyor_ok = (flag_contract_conveyor_proofs_gimple
		      && oa_contract_conveyor_active_public (contract, owner));
  bool symbolic_ok = (flag_contract_symbolic_proofs_gimple
		      && oa_contract_symbolic_active_public (contract, owner));
  if (!conveyor_ok && !symbolic_ok)
    return true;
  bool never_proven, conveyor_forced, conveyor_strict, symbolic_forced,
    symbolic_strict;
  if (!oa_contract_self_check_mode_public (contract, owner, &never_proven,
					   &conveyor_forced, &conveyor_strict,
					   &symbolic_forced, &symbolic_strict)
      || never_proven)
    return true;

  bool ok = true;
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  auto loc_of = [&] (tree c)
    {
      location_t loc = EXPR_LOCATION (c);
      return loc != UNKNOWN_LOCATION ? loc : gimple_location (call);
    };
  auto value_of = [&] (tree decl) -> tree
    {
      decl = STRIP_ANY_LOCATION_WRAPPER (decl);
      if (!VAR_P (decl) && TREE_CODE (decl) != PARM_DECL)
	return NULL_TREE;
      return cg_contract_check_decl_value (call, struct_var, decls, decl);
    };
  /* The floating-point value of DECL, likewise.  */
  auto float_value_of = [&] (tree decl) -> tree
    {
      decl = STRIP_ANY_LOCATION_WRAPPER (decl);
      if (TYPE_REF_P (TREE_TYPE (decl)) && !DECL_BY_REFERENCE (decl))
	return NULL_TREE;
      tree val = value_of (decl);
      return (val && TREE_CODE (val) == SSA_NAME
	      && SCALAR_FLOAT_TYPE_P (TREE_TYPE (val))) ? val : NULL_TREE;
    };
  /* The value of scalar DECL itself -- not of a reference, whose
     captured value is the address it is bound to.  */
  auto scalar_value_of = [&] (tree decl) -> tree
    {
      decl = STRIP_ANY_LOCATION_WRAPPER (decl);
      if (TYPE_REF_P (TREE_TYPE (decl)) && !DECL_BY_REFERENCE (decl))
	return NULL_TREE;
      tree val = value_of (decl);
      return val && INTEGRAL_TYPE_P (TREE_TYPE (val)) ? val : NULL_TREE;
    };

  /* is_object_address: mandatory.  */
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree arg;
      if (!is_object_address_call_p (*conjuncts[i], &arg))
	continue;
      tree a = STRIP_ANY_LOCATION_WRAPPER (arg);
      while (TREE_CODE (a) == NON_LVALUE_EXPR || CONVERT_EXPR_P (a)
	     || TREE_CODE (a) == VIEW_CONVERT_EXPR)
	a = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (a, 0));
      /* A pointer field, or a lambda's capture: what the walk found
	 the field holds (cg_compute_assert_memory_verdicts).  */
      tree fbase;
      if (cg_object_address_field (arg, &fbase))
	{
	  cg_assert_memory_verdicts **mv = memory_verdicts.get (call);
	  int flavor = conveyor_ok ? 0 : 1;
	  if (mv && i < cg_assert_memory_verdicts::max_conjuncts
	      && (*mv)->verdict[i][flavor] == CG_VERDICT_TRUE)
	    continue;
	  location_t loc = loc_of (*conjuncts[i]);
	  ok = false;
	  cg_conveyor_error_at (loc, "cannot prove %<is_object_address%> for "
				"%qE", arg);
	  if (const char *why = oa_unprovable_reason_text
		(OA_UNPROVABLE_NO_FACT))
	    inform (loc, "%s", _(why));
	  continue;
	}
      if (!(VAR_P (a) || TREE_CODE (a) == PARM_DECL)
	  || !(TYPE_REF_P (TREE_TYPE (a)) || POINTER_TYPE_P (TREE_TYPE (a))))
	continue;
      tree val = value_of (a);
      oa_unprovable_reason reason = OA_UNPROVABLE_NONE;
      if (!val || TREE_CODE (val) != SSA_NAME)
	{
	  /* A pointer in memory: what the fact walk found
	     (cg_compute_assert_memory_verdicts).  */
	  cg_assert_memory_verdicts **mv = memory_verdicts.get (call);
	  int flavor = conveyor_ok ? 0 : 1;
	  if (!mv || i >= cg_assert_memory_verdicts::max_conjuncts
	      || (*mv)->verdict[i][flavor] == CG_VERDICT_NONE
	      || (*mv)->verdict[i][flavor] == CG_VERDICT_TRUE)
	    continue;
	  reason = OA_UNPROVABLE_NO_FACT;
	}
      else if (cg_provable_object_address_at_p (val, call, established,
						 /*require_conveyor=*/conveyor_ok,
						 &reason))
	continue;
      location_t loc = loc_of (*conjuncts[i]);
      ok = false;
      cg_conveyor_error_at (loc, "cannot prove %<is_object_address%> for %qE",
			    arg);
      if (const char *why = oa_unprovable_reason_text (reason))
	inform (loc, "%s", _(why));
      inform (DECL_SOURCE_LOCATION (a), "declared here");
    }

  cg_assert_memory_verdicts **mem = memory_verdicts.get (call);
  for (int flavor = 0; flavor < 2; ++flavor)
    {
      bool require_conveyor = flavor == 0;
      bool analysis = (require_conveyor
		       ? conveyor_ok && (cg_conveyor_proofs_p () || conveyor_forced)
		       : symbolic_ok && (cg_symbolic_proofs_p () || symbolic_forced));
      if (!analysis)
	continue;
      bool strict = require_conveyor ? conveyor_strict : symbolic_strict;
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree c = *conjuncts[i];
	  tree arg;
	  if (is_object_address_call_p (c, &arg))
	    continue;
	  /* A postcondition's call of a function declared 'symbolic' (no
	     body to check it against) is a trusted axiom of the symbolic
	     check, as in the AST engine (oa_handle_postcondition_stmt) --
	     but not a contract_assert's.  */
	  {
	    tree pred_fn, pred_arg;
	    bool pred_negated;
	    if (is_post && !require_conveyor
		&& oa_match_predicate_conjunct (c, &pred_fn, &pred_arg,
						&pred_negated)
		&& DECL_DECLARED_SYMBOLIC_P (pred_fn))
	      continue;
	  }
	  cg_verdict verdict = CG_VERDICT_NONE;
	  char text[96] = "";
	  oa_unprovable_reason reason = OA_UNPROVABLE_NO_FACT;
	  tree decl, const_val;
	  tree_code code;
	  if (oa_nonzero_conjunct_p (c, &decl))
	    if (tree val = scalar_value_of (decl))
	      {
		hash_set<tree> in_progress;
		cg_range_lite r;
		bool have_range
		  = cg_value_range_at (val, call, established_range,
				       scalar_range_cache, ranger,
				       require_conveyor, &r);
		if (cg_provable_nonzero_p (val, established_nz,
					   require_conveyor, in_progress))
		  verdict = CG_VERDICT_TRUE;
		else if (have_range)
		  {
		    verdict = cg_range_nonzero_verdict (r);
		    cg_range_lite_text_for (r, TREE_TYPE (val), text, sizeof text);
		    reason = OA_UNPROVABLE_RANGE_PARTIAL;
		  }
		else
		  verdict = CG_VERDICT_UNKNOWN;
	      }
	  if (verdict == CG_VERDICT_NONE
	      && oa_match_simple_comparison_var_public (c, &decl, &code,
							&const_val)
	      && TREE_CODE (const_val) == INTEGER_CST)
	    if (tree val = scalar_value_of (decl))
	      {
		cg_range_lite r;
		if (cg_value_range_at (val, call, established_range,
				       scalar_range_cache, ranger,
				       require_conveyor, &r))
		  {
		    verdict = cg_range_comparison_verdict
				(r, code, wi::to_widest (const_val));
		    cg_range_lite_text_for (r, TREE_TYPE (val), text, sizeof text);
		    reason = OA_UNPROVABLE_RANGE_PARTIAL;
		  }
		else
		  verdict = CG_VERDICT_UNKNOWN;
	      }
	  /* A comparison between two values: a relational fact linking
	     them (e.g. the precondition 'x < q' of a function returning
	     x, for 'post (r: r < q)'), else their two ranges.  */
	  tree other;
	  if (verdict == CG_VERDICT_NONE
	      && oa_match_comparison_against_param (c, &decl, &code, &other,
						    /*allow_var_decl=*/true))
	    {
	      tree va = scalar_value_of (decl);
	      tree vb = scalar_value_of (other);
	      if (va && vb)
		{
		  verdict = CG_VERDICT_UNKNOWN;
		  hash_map<tree, cg_rel_fact> empty_rel_cache;
		  tree_code fact_code;
		  tree fact_rhs;
		  cg_range_lite fact_offset;
		  bool fact_conveyor;
		  /* VA's fact about VB, or VB's about VA, turned around.  */
		  for (int side = 0; side < 2 && verdict != CG_VERDICT_TRUE;
		       ++side)
		    {
		      tree from = side ? vb : va, to = side ? va : vb;
		      tree_code need = side ? swap_tree_comparison (code) : code;
		      if (cg_get_relational (from, established_rel,
					     empty_rel_cache, established_range,
					     scalar_range_cache, ranger,
					     require_conveyor, !require_conveyor,
					     &fact_code, &fact_rhs,
					     &fact_offset, &fact_conveyor)
			  && fact_rhs == to
			  && (!require_conveyor || fact_conveyor)
			  && oa_relational_code_implies (fact_code, need)
			  && cg_offset_compatible_with_code (fact_offset, need))
			verdict = CG_VERDICT_TRUE;
		    }
		  cg_range_lite ra, rb;
		  if (verdict != CG_VERDICT_TRUE
		      && cg_value_range_at (va, call, established_range,
					    scalar_range_cache, ranger,
					    require_conveyor, &ra)
		      && cg_value_range_at (vb, call, established_range,
					    scalar_range_cache, ranger,
					    require_conveyor, &rb))
		    {
		      enum oa_range_subsumption_result r
			= cg_range_pair_relation (ra, code, rb);
		      if (r == OA_RANGE_SUBSUMED)
			verdict = CG_VERDICT_TRUE;
		      else if (r == OA_RANGE_DISJOINT)
			{
			  verdict = CG_VERDICT_FALSE;
			  char ta[44], tb[44];
			  cg_range_lite_text_for (ra, TREE_TYPE (va), ta, sizeof ta);
			  cg_range_lite_text_for (rb, TREE_TYPE (vb), tb, sizeof tb);
			  snprintf (text, sizeof text, "%s and %s", ta, tb);
			}
		      else
			reason = OA_UNPROVABLE_RANGE_PARTIAL;
		    }
		}
	    }
	  /* 'check_it (r)' with check_it's postcondition defining its
	     result as a comparison of its parameter with a constant
	     ('post (r: r == (v > 0))'): decided by the argument's range,
	     unless the walk knows the predicate's value
	     (cg_compute_assert_memory_verdicts).  */
	  tree pred_fn, pred_arg;
	  bool pred_negated;
	  tree_code rel_code;
	  tree rel_const;
	  if (verdict == CG_VERDICT_NONE
	      && !(mem && i < cg_assert_memory_verdicts::max_conjuncts
		   && (*mem)->verdict[i][flavor] != CG_VERDICT_NONE
		   && (*mem)->verdict[i][flavor] != CG_VERDICT_UNKNOWN)
	      && oa_match_predicate_conjunct (c, &pred_fn, &pred_arg,
					      &pred_negated)
	      && oa_predicate_defining_relation_public (pred_fn,
							require_conveyor,
							&rel_code, &rel_const))
	    if (tree val = scalar_value_of (pred_arg))
	      {
		cg_range_lite r;
		if (cg_value_range_at (val, call, established_range,
				       scalar_range_cache, ranger,
				       require_conveyor, &r))
		  {
		    cg_verdict v = cg_range_comparison_verdict
				     (r, rel_code, wi::to_widest (rel_const));
		    if (v == CG_VERDICT_TRUE || v == CG_VERDICT_FALSE)
		      {
			verdict = ((v == CG_VERDICT_TRUE) != pred_negated
				   ? CG_VERDICT_TRUE : CG_VERDICT_FALSE);
			cg_range_lite_text_for (r, TREE_TYPE (val), text,
						sizeof text);
		      }
		  }
	      }
	  /* A floating-point comparison with a constant, decided from the
	     ranger's floating-point range at the check.  */
	  if (verdict == CG_VERDICT_NONE
	      && oa_match_simple_comparison_var_public (c, &decl, &code,
							&const_val)
	      && TREE_CODE (const_val) == REAL_CST)
	    if (tree val = float_value_of (decl))
	      {
		verdict = cg_float_comparison_verdict
			    (val, call, ranger, established_frange.get (val),
			     code, const_val, text, sizeof text);
		reason = OA_UNPROVABLE_RANGE_PARTIAL;
	      }
	  if (verdict == CG_VERDICT_NONE && mem
	      && i < cg_assert_memory_verdicts::max_conjuncts
	      && (*mem)->verdict[i][flavor] != CG_VERDICT_NONE)
	    {
	      verdict = (*mem)->verdict[i][flavor];
	      snprintf (text, sizeof text, "%s", (*mem)->text[i]);
	    }
	  if (verdict == CG_VERDICT_NONE)
	    verdict = CG_VERDICT_UNKNOWN;

	  location_t loc = loc_of (c);
	  switch (verdict)
	    {
	    case CG_VERDICT_FALSE:
	      ok = false;
	      if (is_post)
		error_at (loc, "postcondition condition %qE is provably false", c);
	      else
		error_at (loc, "%<contract_assert%> condition %qE is provably false",
			  c);
	      inform (loc, "established %s", text);
	      break;
	    case CG_VERDICT_UNKNOWN:
	      if (strict)
		{
		  ok = false;
		  if (is_post)
		    error_at (loc, "cannot prove postcondition condition %qE", c);
		  else
		    error_at (loc, "cannot prove %<contract_assert%> condition %qE",
			      c);
		}
	      else if (is_post)
		warning_at (loc, 0, "cannot verify postcondition condition %qE", c);
	      else
		warning_at (loc, 0, "cannot verify %<contract_assert%> condition %qE",
			    c);
	      if (const char *why = oa_unprovable_reason_text (reason))
		inform (loc, "%s", _(why));
	      break;
	    default:
	      break;
	    }
	}
    }
  return ok;
}

/* Immediately invoked lambdas ('[&] { ... } ()'): what the caller knew
   about each capture at the one call, for checking the lambda's body
   against it, as the AST engine walks such a body in place
   (oa_resolve_iile_call).  A capture by reference: the captured
   variable's value; by value: the copy's.  */

struct cg_capture_fact
{
  tree field;
  bool by_ref;
  cg_range_lite range;
};

struct cg_iile_info
{
  bool conveyor_caller = false;
  bool invalid = false;
  auto_vec<cg_capture_fact> facts;
};

static hash_map<tree, cg_iile_info *> *cg_iile_infos;

static vec<tree> cg_eager_check_deferred_until_eof_iile;

/* Is local closure object CLOSURE of FUN used only by stores to its
   fields, clobbers, and as the object of the call CALL?  */

static bool
cg_closure_only_invoked_p (function *fun, tree closure, gcall *call)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (stmt == call || gimple_clobber_p (stmt)
	    || gimple_call_internal_p (stmt, IFN_DEFERRED_INIT))
	  continue;
	if (gimple_assign_single_p (stmt)
	    && TREE_CODE (gimple_assign_lhs (stmt)) == COMPONENT_REF
	    && TREE_OPERAND (gimple_assign_lhs (stmt), 0) == closure)
	  {
	    tree rhs = gimple_assign_rhs1 (stmt);
	    if (walk_tree (&rhs, [](tree *tp, int *, void *d) -> tree
			     { return *tp == (tree) d ? *tp : NULL_TREE; },
			   closure, NULL))
	      return false;
	    continue;
	  }
	for (unsigned i = 0; i < gimple_num_ops (stmt); ++i)
	  {
	    tree op = gimple_op (stmt, i);
	    if (op && walk_tree (&op, [](tree *tp, int *, void *d) -> tree
				   { return *tp == (tree) d ? *tp : NULL_TREE; },
				 closure, NULL))
	      return false;
	  }
      }
  return true;
}

/* The value variable X has at CALL, from its last store before CALL in
   CALL's block, if no call or store through a pointer comes between.  */

static tree
cg_local_value_before (gcall *call, tree x)
{
  gimple_stmt_iterator gsi = gsi_for_stmt (call);
  for (gsi_prev (&gsi); !gsi_end_p (gsi); gsi_prev (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_clobber_p (stmt))
	continue;
      if (gimple_call_internal_p (stmt, IFN_DEFERRED_INIT))
	{
	  if (gimple_call_lhs (stmt) == x)
	    return NULL_TREE;
	  continue;
	}
      if (!gimple_assign_single_p (stmt))
	{
	  if (is_gimple_call (stmt) || gimple_code (stmt) == GIMPLE_ASM)
	    return NULL_TREE;
	  continue;
	}
      tree lhs = gimple_assign_lhs (stmt);
      if (lhs == x)
	return gimple_assign_rhs1 (stmt);
      if (TREE_CODE (lhs) == SSA_NAME || DECL_P (lhs)
	  || (TREE_CODE (lhs) == COMPONENT_REF
	      && DECL_P (TREE_OPERAND (lhs, 0))))
	continue;
      return NULL_TREE;
    }
  return NULL_TREE;
}

/* Record, at CALL in FUN, what the caller knows about the captures of
   the lambda CALL immediately invokes, if it does.  */

static void
cg_record_iile_site_facts (gcall *call, function *fun,
			   hash_map<tree, cg_range_lite> &established_range,
			   hash_map<tree, cg_range_lite> &scalar_range_cache,
			   gimple_ranger *ranger)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee || !LAMBDA_FUNCTION_P (callee) || gimple_call_num_args (call) < 1)
    return;
  tree closure_addr = gimple_call_arg (call, 0);
  if (TREE_CODE (closure_addr) != ADDR_EXPR
      || !VAR_P (TREE_OPERAND (closure_addr, 0)))
    return;
  tree closure = TREE_OPERAND (closure_addr, 0);
  if (!cg_iile_infos)
    cg_iile_infos = new hash_map<tree, cg_iile_info *>;
  bool existed;
  cg_iile_info *&info = cg_iile_infos->get_or_insert (callee, &existed);
  if (existed)
    {
      /* More than one call: not one caller's facts.  */
      info->invalid = true;
      return;
    }
  info = new cg_iile_info;
  info->conveyor_caller = DECL_DECLARED_CONVEYOR_P (fun->decl);
  if (!cg_closure_only_invoked_p (fun, closure, call))
    {
      info->invalid = true;
      return;
    }
  auto range_of = [&] (tree v, cg_range_lite *r) -> bool
    {
      if (TREE_CODE (v) == INTEGER_CST)
	{
	  r->has_lo = r->has_hi = true;
	  r->lo = r->hi = wi::to_widest (v);
	  return true;
	}
      return (TREE_CODE (v) == SSA_NAME && INTEGRAL_TYPE_P (TREE_TYPE (v))
	      && cg_value_range_at (v, call, established_range,
				    scalar_range_cache, ranger,
				    /*require_conveyor=*/true, r));
    };
  gimple_stmt_iterator gsi = gsi_for_stmt (call);
  for (gsi_prev (&gsi); !gsi_end_p (gsi); gsi_prev (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (!gimple_assign_single_p (stmt))
	continue;
      tree lhs = gimple_assign_lhs (stmt);
      if (TREE_CODE (lhs) != COMPONENT_REF || TREE_OPERAND (lhs, 0) != closure)
	continue;
      tree field = TREE_OPERAND (lhs, 1);
      tree stored = gimple_assign_rhs1 (stmt);
      cg_capture_fact f = { field, TYPE_REF_P (TREE_TYPE (field)), {} };
      tree val = stored;
      if (f.by_ref)
	{
	  if (TREE_CODE (stored) != ADDR_EXPR
	      || !VAR_P (TREE_OPERAND (stored, 0))
	      || !INTEGRAL_TYPE_P (TREE_TYPE (TREE_OPERAND (stored, 0))))
	    continue;
	  val = cg_local_value_before (call, TREE_OPERAND (stored, 0));
	  if (!val)
	    continue;
	}
      if (range_of (val, &f.range))
	info->facts.safe_push (f);
    }
  /* The body is checked at the end of the file, after its callers.  */
  cg_eager_check_deferred_until_eof_iile.safe_push (callee);
}

/* Seed lambda FUN's loads of its captures with what its one caller knew
   about them (cg_record_iile_site_facts), when its body neither writes
   them nor passes them on.  */

static void
cg_seed_iile_capture_facts (function *fun,
			    hash_map<tree, cg_fact> &established,
			    hash_map<tree, cg_fact> &established_nz,
			    hash_map<tree, cg_range_lite> &established_range)
{
  cg_iile_info **infop = cg_iile_infos ? cg_iile_infos->get (fun->decl) : NULL;
  if (!infop || (*infop)->invalid)
    return;
  tree closure_parm = DECL_ARGUMENTS (fun->decl);
  tree cl = closure_parm ? ssa_default_def (fun, closure_parm) : NULL_TREE;
  if (!cl)
    return;
  /* The closure: the caller's local object.  */
  established.put (cl, { /*conveyor_established=*/true });
  for (cg_capture_fact &f : (*infop)->facts)
    {
      /* The loads of the capture: the field's (by value) or, through the
	 pointer the field holds, the object's (by reference).  */
      auto_vec<tree> loads;
      bool ok = true;
      basic_block bb;
      FOR_EACH_BB_FN (bb, fun)
	for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	     ok && !gsi_end_p (gsi); gsi_next (&gsi))
	  {
	    gimple *stmt = gsi_stmt (gsi);
	    if (!gimple_assign_single_p (stmt))
	      continue;
	    tree lhs = gimple_assign_lhs (stmt), rhs = gimple_assign_rhs1 (stmt);
	    auto field_of_closure = [&] (tree ref)
	      {
		return (TREE_CODE (ref) == COMPONENT_REF
			&& TREE_OPERAND (ref, 1) == f.field
			&& TREE_CODE (TREE_OPERAND (ref, 0)) == MEM_REF
			&& TREE_OPERAND (TREE_OPERAND (ref, 0), 0) == cl);
	      };
	    if (field_of_closure (lhs))
	      ok = false;
	    else if (field_of_closure (rhs) && TREE_CODE (lhs) == SSA_NAME)
	      {
		if (!f.by_ref)
		  {
		    loads.safe_push (lhs);
		    continue;
		  }
		/* Every use of the pointer must be a load through it.  */
		imm_use_iterator iter;
		use_operand_p use_p;
		FOR_EACH_IMM_USE_FAST (use_p, iter, lhs)
		  {
		    gimple *use = USE_STMT (use_p);
		    if (is_gimple_debug (use))
		      continue;
		    tree urhs = (gimple_assign_single_p (use)
				 ? gimple_assign_rhs1 (use) : NULL_TREE);
		    if (urhs && TREE_CODE (urhs) == MEM_REF
			&& TREE_OPERAND (urhs, 0) == lhs
			&& integer_zerop (TREE_OPERAND (urhs, 1))
			&& TREE_CODE (gimple_assign_lhs (use)) == SSA_NAME)
		      loads.safe_push (gimple_assign_lhs (use));
		    else
		      ok = false;
		  }
	      }
	  }
      if (!ok)
	continue;
      for (tree t : loads)
	{
	  established_range.put (t, f.range);
	  if ((f.range.has_lo && f.range.lo > 0)
	      || (f.range.has_hi && f.range.hi < 0))
	    established_nz.put (t, { true });
	}
    }
}

/* What the checks of a contract's own function found about each value a
   contract check passes its predicate core function, merged over every
   check of the contract: the core function's parameter I receives the
   value captured in the check's field I.  */

struct cg_site_fact
{
  bool has_range = false;
  cg_range_lite range;
  bool nonzero = false;
  bool object_address = false;
  bool has_float = false;
  frange float_range = frange ();
};

static hash_map<tree, vec<cg_site_fact> *> *cg_predicate_site_facts;

/* Predicate core functions whose precondition is checked at its own
   function's entry.  What holds there comes from the function's
   self-trust -- that precondition itself among it -- so the core is
   seeded from the function's other preconditions instead
   (cg_seed_predicate_from_preconditions), as the entry check sees any
   argument.  */

static hash_set<tree> *cg_predicate_entry_checked;

/* Record, at contract check CALL, the facts about each value it
   captures for its predicate core function.  */

static void
cg_record_predicate_site_facts (gcall *call,
				hash_map<tree, cg_fact> &established,
				hash_map<tree, cg_fact> &established_nz,
				hash_map<tree, cg_range_lite> &established_range,
				hash_map<tree, cg_range_lite> &scalar_range_cache,
				gimple_ranger *ranger,
				hash_map<tree, frange> &established_frange)
{
  tree struct_var, cond, decls;
  tree contract = cg_contract_check_call_p (call, &struct_var, &cond, &decls);
  if (!contract)
    return;
  tree core = contract_predicate_core_function (contract);
  if (!core)
    return;
  tree owner = NULL_TREE;
  if (PRECONDITION_P (contract)
      && predicate_core_function_contract (core, &owner) && owner
      && (cfun->decl == owner
	  || (DECL_CLONED_FUNCTION_P (cfun->decl)
	      && DECL_CLONED_FUNCTION (cfun->decl) == owner)))
    {
      if (!cg_predicate_entry_checked)
	cg_predicate_entry_checked = new hash_set<tree>;
      cg_predicate_entry_checked->add (core);
      return;
    }
  if (!cg_predicate_site_facts)
    cg_predicate_site_facts = new hash_map<tree, vec<cg_site_fact> *>;
  bool existed;
  vec<cg_site_fact> *&facts = cg_predicate_site_facts->get_or_insert (core,
								       &existed);
  unsigned n = TREE_VEC_LENGTH (decls) - 2;
  auto_vec<cg_site_fact> here;
  for (unsigned i = 0; i < n; ++i)
    {
      cg_site_fact f;
      tree decl = TREE_VEC_ELT (decls, i + 2);
      tree val = cg_contract_check_decl_value (call, struct_var, decls, decl);
      if (val && TREE_CODE (val) == SSA_NAME)
	{
	  tree type = TREE_TYPE (val);
	  if (INTEGRAL_TYPE_P (type))
	    {
	      f.has_range = cg_value_range_at (val, call, established_range,
					       scalar_range_cache, ranger,
					       /*require_conveyor=*/true,
					       &f.range);
	      hash_set<tree> ip;
	      /* Also an anti-range the ranger knows ('n != 0 ? n : 1').  */
	      f.nonzero = (cg_provable_nonzero_p (val, established_nz, true, ip)
			   || (f.has_range
			       && cg_range_nonzero_verdict (f.range)
				  == CG_VERDICT_TRUE)
			   || cg_excludes_zero_at_p (val, call, ranger, true));
	    }
	  else if (POINTER_TYPE_P (type))
	    f.object_address
	      = cg_provable_object_address_at_p (val, call, established,
						 /*require_conveyor=*/true,
						 NULL);
	  else if (SCALAR_FLOAT_TYPE_P (type))
	    {
	      frange fr (type);
	      if (ranger->range_of_expr (fr, val, call))
		{
		  if (frange *known = established_frange.get (val))
		    fr.intersect (*known);
		  f.has_float = !fr.undefined_p () && !fr.varying_p ();
		  f.float_range = fr;
		}
	    }
	}
      here.safe_push (f);
    }
  if (!existed)
    {
      facts = new vec<cg_site_fact> ();
      facts->safe_splice (here);
      return;
    }
  /* Another check of the same contract (a constructor's clones): what
     holds at both.  */
  for (unsigned i = 0; i < here.length () && i < facts->length (); ++i)
    {
      cg_site_fact &a = (*facts)[i];
      const cg_site_fact &b = here[i];
      a.nonzero &= b.nonzero;
      a.object_address &= b.object_address;
      if (a.has_range && b.has_range)
	{
	  if (!b.range.has_lo || (a.range.has_lo && b.range.lo < a.range.lo))
	    a.range.has_lo = b.range.has_lo, a.range.lo = b.range.lo;
	  if (!b.range.has_hi || (a.range.has_hi && b.range.hi > a.range.hi))
	    a.range.has_hi = b.range.has_hi, a.range.hi = b.range.hi;
	}
      else
	a.has_range = false;
      if (a.has_float && b.has_float)
	a.float_range.union_ (b.float_range);
      else
	a.has_float = false;
    }
}

/* A contract never checked in this translation unit (its function has
   no definition here) has no recorded facts: seed core function FUN's
   parameters, which start with copies of its function's own, with what
   that function's other preconditions say about them, as the AST engine
   validates such a contract against the function's entry
   (oa_seed_entry_env) -- e.g. the implicit is_object_address
   precondition of a reference parameter.  */

static void
cg_seed_predicate_from_preconditions (function *fun,
				      hash_map<tree, cg_fact> &established,
				      hash_map<tree, cg_fact> &established_nz,
				      hash_map<tree, cg_range_lite> &established_range)
{
  tree owner;
  tree self = predicate_core_function_contract (fun->decl, &owner);
  if (!self || !owner)
    return;
  /* OWNER's parameter -> FUN's parameter at the same position.  */
  auto core_parm = [&] (tree parm) -> tree
    {
      tree c = DECL_ARGUMENTS (fun->decl);
      for (tree p = DECL_ARGUMENTS (owner); p && c;
	   p = DECL_CHAIN (p), c = DECL_CHAIN (c))
	if (p == parm)
	  return cg_self_trust_key (fun, c);
      return NULL_TREE;
    };
  for (tree as = get_fn_contract_specifiers (owner); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract)
	  || !oa_contract_conveyor_active_public (contract, owner))
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;
      /* SELF may be a copy of the specifier's contract (copy_node keeps
	 the location).  Mistaking another contract for SELF only trusts
	 less.  */
      bool is_self = (contract == self
		      || contract_predicate_core_function (contract)
			 == fun->decl
		      || (TREE_CODE (contract) == TREE_CODE (self)
			  && EXPR_LOCATION (contract) == EXPR_LOCATION (self)));
      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree arg, const_val;
	  tree_code code;
	  /* The contract's own conditions other than is_object_address are
	     what is being validated; its is_object_address conjuncts are
	     trusted, as the AST engine trusts a precondition's own.  */
	  if (is_self && !is_object_address_call_p (*conjuncts[i], &arg))
	    continue;
	  if (is_object_address_call_p (*conjuncts[i], &arg))
	    {
	      arg = STRIP_ANY_LOCATION_WRAPPER (arg);
	      while (TREE_CODE (arg) == NON_LVALUE_EXPR || CONVERT_EXPR_P (arg)
		     || TREE_CODE (arg) == VIEW_CONVERT_EXPR)
		arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
	      if (TREE_CODE (arg) == PARM_DECL)
		if (tree key = core_parm (arg))
		  established.put (key, { true });
	    }
	  else if (oa_nonzero_conjunct_p (*conjuncts[i], &arg))
	    {
	      if (TREE_CODE (arg) == PARM_DECL)
		if (tree key = core_parm (arg))
		  established_nz.put (key, { true });
	    }
	  else if (oa_match_simple_comparison (*conjuncts[i], &arg, &code,
					       &const_val)
		   && TREE_CODE (const_val) == INTEGER_CST)
	    if (tree key = core_parm (arg))
	      {
		cg_range_lite &r = established_range.get_or_insert (key);
		cg_tighten_range_bound (r, code, wi::to_widest (const_val));
	      }
	}
    }
}

/* Seed predicate core function FUN's parameters with what the checks of
   its contract recorded about the values passed to them.  */

static void
cg_seed_predicate_site_facts (function *fun,
			      hash_map<tree, cg_fact> &established,
			      hash_map<tree, cg_fact> &established_nz,
			      hash_map<tree, cg_range_lite> &established_range,
			      hash_map<tree, frange> &established_frange)
{
  vec<cg_site_fact> **facts
    = cg_predicate_site_facts ? cg_predicate_site_facts->get (fun->decl) : NULL;
  if (!facts
      || (cg_predicate_entry_checked
	  && cg_predicate_entry_checked->contains (fun->decl)))
    {
      cg_seed_predicate_from_preconditions (fun, established, established_nz,
					    established_range);
      return;
    }
  unsigned i = 0;
  for (tree parm = DECL_ARGUMENTS (fun->decl); parm && i < (*facts)->length ();
       parm = DECL_CHAIN (parm), ++i)
    {
      tree ssa = ssa_default_def (fun, parm);
      if (!ssa)
	continue;
      const cg_site_fact &f = (**facts)[i];
      if (f.has_range)
	established_range.put (ssa, f.range);
      if (f.nonzero)
	established_nz.put (ssa, { true });
      if (f.object_address || TYPE_REF_P (TREE_TYPE (parm)))
	{
	  /* A reference parameter binds the object the check passed the
	     address of.  */
	  if (f.object_address)
	    established.put (ssa, { true });
	}
      if (f.has_float)
	established_frange.put (ssa, f.float_range);
    }
}

/* Whether predicate core function FN's body gets item 8's checks: its
   contract is conveyor-flavored and checked, not never_proven, as in the
   AST engine's oa_scan_item8_in_expr calls.  */

static bool
cg_predicate_item8_p (tree fn)
{
  tree owner;
  tree contract = predicate_core_function_contract (fn, &owner);
  if (!contract || !flag_contract_conveyor_proofs_gimple
      || !oa_contract_conveyor_active_public (contract, owner))
    return false;
  bool never_proven, cf, cs, sf, ss;
  return (oa_contract_self_check_mode_public (contract, owner, &never_proven,
					      &cf, &cs, &sf, &ss)
	  && !never_proven);
}

/* Whether predicate core function FN belongs to a checked, conveyor-
   flavored never_proven contract: exempt from item 8, but still checked
   for its calls' callee preconditions (cg_callee_pre_only).  */

static bool
cg_predicate_never_proven_conveyor_p (tree fn)
{
  tree owner;
  tree contract = predicate_core_function_contract (fn, &owner);
  if (!contract || !flag_contract_conveyor_proofs_gimple
      || !oa_contract_conveyor_active_public (contract, owner))
    return false;
  bool never_proven, cf, cs, sf, ss;
  return (oa_contract_self_check_mode_public (contract, owner, &never_proven,
					      &cf, &cs, &sf, &ss)
	  && never_proven);
}

/* The object STORE_LHS (a store's destination in a predicate core
   function) names, in source terms, for a diagnostic: the parameter or
   field it reaches through a reference parameter, else STORE_LHS.  */

static tree
cg_display_store_target (tree lhs)
{
  if (TREE_CODE (lhs) == MEM_REF && integer_zerop (TREE_OPERAND (lhs, 1))
      && TREE_CODE (TREE_OPERAND (lhs, 0)) == SSA_NAME)
    {
      tree ptr = cg_display_value (TREE_OPERAND (lhs, 0));
      if (DECL_P (ptr) && TYPE_REF_P (TREE_TYPE (ptr)))
	return convert_from_reference (ptr);
      if (DECL_P (ptr))
	return build1 (INDIRECT_REF, TREE_TYPE (lhs), ptr);
    }
  if (TREE_CODE (lhs) == COMPONENT_REF)
    {
      tree base = cg_display_store_target (TREE_OPERAND (lhs, 0));
      if (base != TREE_OPERAND (lhs, 0))
	return build3 (COMPONENT_REF, TREE_TYPE (lhs), base,
		       TREE_OPERAND (lhs, 1), NULL_TREE);
    }
  return lhs;
}

/* FUN is a helper function.  If it is the predicate core function of a
   conveyor-flavored contract -- the function evaluating its condition --
   reject a store to anything the evaluation didn't create itself, like
   the AST engine's oa_scan_predicate_mutation_ownership_in_expr: the
   core function's parameters refer to the guarded function's own
   objects, so a store through one, or to a global, mutates something
   that outlives the check.  */

static void
cg_check_predicate_mutation (function *fun)
{
  tree owner;
  tree contract = predicate_core_function_contract (fun->decl, &owner);
  if (!contract || !flag_contract_conveyor_proofs_gimple
      || !oa_contract_conveyor_active_public (contract, owner))
    return;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	tree lhs = (is_gimple_assign (stmt) ? gimple_assign_lhs (stmt)
		    : is_gimple_call (stmt) ? gimple_call_lhs (stmt)
		    : NULL_TREE);
	if (!lhs)
	  continue;
	/* A by-value capture arrives as a parameter of its own: a new
	   value for it is an assignment to the guarded function's
	   variable, in the condition as written.  */
	if (TREE_CODE (lhs) == SSA_NAME)
	  {
	    tree var = SSA_NAME_VAR (lhs);
	    if (!var || TREE_CODE (var) != PARM_DECL
		|| DECL_CONTEXT (var) != fun->decl)
	      continue;
	  }
	else if (!gimple_vdef (stmt) || gimple_clobber_p (stmt))
	  continue;
	tree base = (TREE_CODE (lhs) == SSA_NAME ? NULL_TREE
		     : get_base_address (lhs));
	/* A local of the evaluation itself (a temporary, an IILE's own
	   variable) is its own to write.  */
	if (base && VAR_P (base) && !is_global_var (base)
	    && DECL_CONTEXT (base) == fun->decl)
	  continue;

	/* 'x += 1', '++x': the stored value is the same location +/- 1.  */
	const char *what = G_("assignment to %qE not permitted in a conveyor "
			      "predicate or assert condition");
	gimple *shift = NULL;
	if (TREE_CODE (lhs) == SSA_NAME)
	  shift = stmt;
	else if (gimple_assign_single_p (stmt)
		 && TREE_CODE (gimple_assign_rhs1 (stmt)) == SSA_NAME)
	  shift = SSA_NAME_DEF_STMT (gimple_assign_rhs1 (stmt));
	if (shift)
	  {
	    gimple *def = shift;
	    if (def && is_gimple_assign (def)
		&& (gimple_assign_rhs_code (def) == PLUS_EXPR
		    || gimple_assign_rhs_code (def) == MINUS_EXPR)
		&& TREE_CODE (gimple_assign_rhs1 (def)) == SSA_NAME
		&& (integer_onep (gimple_assign_rhs2 (def))
		    || integer_minus_onep (gimple_assign_rhs2 (def))))
	      {
		tree from = gimple_assign_rhs1 (def);
		gimple *load = SSA_NAME_DEF_STMT (from);
		/* A post-increment copies the old value first.  */
		while (load && gimple_assign_ssa_name_copy_p (load))
		  {
		    from = gimple_assign_rhs1 (load);
		    load = SSA_NAME_DEF_STMT (from);
		  }
		if ((TREE_CODE (lhs) == SSA_NAME
		     && SSA_NAME_VAR (from) == SSA_NAME_VAR (lhs))
		    || (load && gimple_assign_single_p (load)
			&& operand_equal_p (gimple_assign_rhs1 (load), lhs, 0)))
		  {
		    bool up = ((gimple_assign_rhs_code (def) == PLUS_EXPR)
			       == integer_onep (gimple_assign_rhs2 (def)));
		    what = up
		      ? G_("increment of %qE not permitted in a conveyor "
			   "predicate or assert condition")
		      : G_("decrement of %qE not permitted in a conveyor "
			   "predicate or assert condition");
		  }
	      }
	  }
	auto_diagnostic_group d;
	error_at (gimple_location (stmt), what,
		  TREE_CODE (lhs) == SSA_NAME ? SSA_NAME_VAR (lhs)
		  : cg_display_store_target (lhs));
	inform (gimple_location (stmt), "%s",
		_(oa_unprovable_reason_text (OA_UNPROVABLE_PREDICATE_CONTEXT)));
      }
}

/* The symbolic contracts' markers (IFN_CONTRACT_MARK) in FUN have
   served their purpose once FUN is checked: remove them, the stores
   filling their capture structs, and the temporaries the gimplifier
   made to take the address of a value for those stores when nothing
   else uses them -- leaving FUN as it would be without the markers.  */

static void
cg_remove_contract_marks (function *fun)
{
  hash_set<tree> structs, temps;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      if (gimple_call_internal_p (gsi_stmt (gsi), IFN_CONTRACT_MARK))
	{
	  tree arg = gimple_call_arg (gsi_stmt (gsi), 0);
	  if (TREE_CODE (arg) == ADDR_EXPR && VAR_P (TREE_OPERAND (arg, 0)))
	    structs.add (TREE_OPERAND (arg, 0));
	}
  if (structs.is_empty ())
    return;

  auto remove = [&] (gimple_stmt_iterator *gsi)
    {
      gimple *stmt = gsi_stmt (*gsi);
      if (gimple_in_ssa_p (fun))
	unlink_stmt_vdef (stmt);
      gsi_remove (gsi, true);
      if (gimple_in_ssa_p (fun))
	release_defs (stmt);
    };
  /* Whether a statement mentions VAR anywhere.  */
  auto mentions = [] (gimple *stmt, tree var)
    {
      for (unsigned i = 0; i < gimple_num_ops (stmt); ++i)
	if (tree op = gimple_op (stmt, i))
	  if (walk_tree_without_duplicates (&op, [](tree *tp, int *,
						    void *v) -> tree
		{ return *tp == (tree) v ? *tp : NULL_TREE; }, var))
	    return true;
      return false;
    };

  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);)
      {
	gimple *stmt = gsi_stmt (gsi);
	if (gimple_call_internal_p (stmt, IFN_CONTRACT_MARK))
	  {
	    remove (&gsi);
	    continue;
	  }
	tree base = (gimple_assign_single_p (stmt)
		     || gimple_clobber_p (stmt)
		     ? get_base_address (gimple_assign_lhs (stmt))
		     : gimple_call_internal_p (stmt, IFN_DEFERRED_INIT)
		     ? get_base_address (gimple_call_lhs (stmt)) : NULL_TREE);
	if (base && structs.contains (base))
	  {
	    tree rhs = (is_gimple_assign (stmt) ? gimple_assign_rhs1 (stmt)
			: NULL_TREE);
	    if (rhs && TREE_CODE (rhs) == ADDR_EXPR
		&& VAR_P (TREE_OPERAND (rhs, 0))
		&& DECL_ARTIFICIAL (TREE_OPERAND (rhs, 0)))
	      temps.add (TREE_OPERAND (rhs, 0));
	    remove (&gsi);
	    continue;
	  }
	gsi_next (&gsi);
      }

  /* A temporary only the removed stores took the address of: its own
     stores and clobbers are dead too.  */
  for (tree temp : temps)
    {
      bool used = false;
      FOR_EACH_BB_FN (bb, fun)
	for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	     !used && !gsi_end_p (gsi); gsi_next (&gsi))
	  {
	    gimple *stmt = gsi_stmt (gsi);
	    bool def_of_temp = (((gimple_assign_single_p (stmt)
				  || gimple_clobber_p (stmt))
				 && gimple_assign_lhs (stmt) == temp)
				|| (gimple_call_internal_p (stmt,
							    IFN_DEFERRED_INIT)
				    && gimple_call_lhs (stmt) == temp));
	    if (!def_of_temp && mentions (stmt, temp))
	      used = true;
	  }
      if (used)
	continue;
      FOR_EACH_BB_FN (bb, fun)
	for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);)
	  {
	    gimple *stmt = gsi_stmt (gsi);
	    if (((gimple_assign_single_p (stmt) || gimple_clobber_p (stmt))
		 && gimple_assign_lhs (stmt) == temp)
		|| (gimple_call_internal_p (stmt, IFN_DEFERRED_INIT)
		    && gimple_call_lhs (stmt) == temp))
	      remove (&gsi);
	    else
	      gsi_next (&gsi);
	  }
    }
}

namespace {

const pass_data pass_data_contracts_gimple =
{
  GIMPLE_PASS,			/* type */
  "cgimple",			/* name */
  OPTGROUP_NONE,		/* optinfo_flags */
  TV_NONE,			/* tv_id */
  PROP_ssa,			/* properties_required */
  0,				/* properties_provided */
  0,				/* properties_destroyed */
  0,				/* todo_flags_start */
  0,				/* todo_flags_finish */
};

class pass_contracts_gimple : public gimple_opt_pass
{
public:
  pass_contracts_gimple (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_contracts_gimple, ctxt)
  {}

  bool gate (function *) final override
  {
    return flag_contract_control_objects
	   && (flag_contract_conveyor_proofs_gimple
	       || flag_contract_symbolic_proofs_gimple);
  }

  unsigned int execute (function *) final override;
};

unsigned int
pass_contracts_gimple::execute (function *fun)
{
  /* D4324/P2680: mirrors contracts.cc's oa_resolve_object_address_in_
     function_1 -- a generated "pred"/"thunk"/consteval-thunk helper
     function (see ldf_contract_predicate_helper's own comment) is a
     mechanical, verbatim re-embedding of some OTHER, already-guarded
     function's own contract condition, over that helper's OWN
     reference-typed parameters (aliasing back to the guarded
     function's own locals) with none of the guarded function's own
     self-trust ever seeded for them. Without this exemption, e.g. a
     synthesized 'pre<>(is_object_address(this))' on a callee read by
     cg_check_call's own precondition-text consult fails to verify for
     such a helper's own aliasing parameter, even though the ORIGINAL,
     guarded function (whose body this helper mechanically copies) has
     already had this exact condition resolved against ITS OWN,
     correctly-seeded parameters.  */
  bool predicate_core = false;
  bool never_proven_core = false;
  if (CONTRACT_HELPER (fun->decl) != ldf_contract_none)
    {
      cg_check_predicate_mutation (fun);
      /* A conveyor contract's condition gets item 8's checks too,
	 against the facts at its checks (cg_record_predicate_site_facts).
	 A never-proven one gets only its calls' callee preconditions
	 checked, under the engine option (cg_callee_pre_only).  */
      if (cg_predicate_item8_p (fun->decl))
	predicate_core = true;
      else if (cg_predicate_never_proven_conveyor_p (fun->decl))
	predicate_core = never_proven_core = true;
      else
	return 0;
    }

  /* D4324/P2680, GIMPLE/front-end instantiation-ordering fix (found via
     the comprehensive coverage sweep, 2026-10-12): an ordinary,
     non-template caller gets its own GIMPLE contracts pass run
     EAGERLY, the moment its own body is gimplified (this file's own
     eager-checking architecture, contracts_gimple_eager_check_
     function) -- well before GCC's own, ordinary deferred-template-
     instantiation schedule would otherwise instantiate a template
     member function it calls (e.g. std::vector<int>::size ()/
     operator[] ()), which normally waits until instantiate_pending_
     templates, run from c_parse_final_cleanups right at the very end
     of the whole translation unit. Every one of this file's own *_
     active_cached_p consults (oa_contract_conveyor_active_cached_p/
     oa_contract_symbolic_active_cached_p, used throughout this pass)
     reads a cache that is only ever populated once, at the callee's
     own finish_function time (oa_cache_contract_flavors, contracts.cc)
     -- so consulting it for a callee that has not yet been
     instantiated at all silently answers "no contract is active" (the
     cache simply has no entry for it yet), not "unknown": every one of
     that callee's own declared obligations is then silently skipped
     at this call site, with no diagnostic at all. Confirmed via direct
     testing: std::vector<int>::operator[]'s own synthesized '_N <
     size ()' precondition (_GLIBCXX_PRECONDITION_ASSERTIONS) was being
     silently treated as inactive at every call site in an eagerly-
     checked, non-template caller, purely because its own template
     instantiation (and hence its own contract-flavor cache entry) had
     not happened yet at that point in the TU -- confirmed, within a
     single gdb session (to keep the comparison address-valid, unlike
     an earlier, invalid cross-process comparison this investigation
     first made and then corrected), that cg_check_call's own
     "CONSULTED ... conveyor_active=0" for this exact contract tree
     pointer is reached strictly BEFORE oa_cache_contract_flavors's own
     "ADDED_CONVEYOR" event for that identical pointer later instantiates
     the same specialization.

     Fixed by forcing every callee this function's own body actually
     calls to be fully instantiated -- hence to have already run its
     own finish_function/oa_cache_contract_flavors -- before any of the
     checks below (cg_seed_self_trust, cg_predicate_facts_walk, the
     FOR_EACH_BB_FN loop's own cg_check_call) ever consult that cache.
     maybe_instantiate_decl (decl2.cc) is the existing, general-purpose
     "instantiate this template specialization now, if it needs it and
     hasn't already" helper (already used the same way by, e.g.,
     build_over_call for an ordinary call's own callee, typeck.cc) --
     a safe, cheap no-op for a callee that is not a template
     specialization, or is already instantiated, so applying it
     unconditionally to every call in this function's own body is
     simpler and safer than trying to predict which of them might
     matter.  */
  basic_block instantiate_bb;
  FOR_EACH_BB_FN (instantiate_bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (instantiate_bb);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (!is_gimple_call (stmt))
	  continue;
	if (tree callee = gimple_call_fndecl (as_a <gcall *> (stmt)))
	  maybe_instantiate_decl (callee);
      }

  /* Named-predicate and field/call-range facts get their own, separate
     fixed-point RPO walk (see cg_predicate_facts_walk's own comment)
     rather than folding into the FOR_EACH_BB_FN loop below: that loop's
     own fact shapes are consulted using a single, function-wide
     ESTABLISHED set/map (correct for them, since a backward SSA walk
     needs no block-order-sensitive state at all), whereas these are
     inherently per-program-point and need genuine per-block dataflow.

     Run *first*, not after: SCALAR_RANGE_CACHE is this walk's own
     output (see cg_compose_call_result_range's own comment) -- a
     postcondition relating a return value to a call-range-eligible
     accessor (needing this walk's own STATE.call) is resolved here and
     fed into the loop below's own cg_established_range_of, the same
     "look up this SSA name's own established range" query an ordinary
     literal-bounded postcondition's own item 6 already answers from
     ESTABLISHED_RANGE alone.  */
  hash_map<tree, cg_range_lite> scalar_range_cache;
  /* Bounds-proving demo, Part 2: every call site where cg_predicate_
     facts_walk's own new range-vs-range fallback (cg_check_call_range_
     relational) already reached a definitive verdict for a "param vs
     call" conjunct -- cg_check_call's own, separate pass over the same
     calls below must not also warn "cannot verify" for one of those
     (see cg_check_call_range_relational's own comment for why this
     check can't simply live inside cg_check_call to begin with, and for
     the known, accepted per-call (not per-conjunct) imprecision).  */
  hash_set<gimple *> call_relational_verdict;
  hash_map<tree, int> unprovable_hints;
  auto hints_ovr = make_temp_override (cg_unprovable_hints, &unprovable_hints);
  hash_map<tree, cg_trust> trusted_bounds_cache;
  auto trusted_bounds_ovr = make_temp_override (cg_trusted_bounds_cache,
						&trusted_bounds_cache);
  hash_set<cg_call_conjunct_hash> proven_call_call_conjuncts;
  auto proven_cc_ovr = make_temp_override (cg_proven_call_call_conjuncts,
					   &proven_call_call_conjuncts);
  /* Bounds-proving demo, Part 2: created here, before cg_predicate_facts_
     walk's own call, rather than in each of the two passes separately --
     found via direct testing that giving cg_predicate_facts_walk its own,
     second, sequential (not nested) gimple_ranger instance over the same
     function made this whole pass's own diagnostics non-deterministic
     between identical runs of the same compilation (see cg_check_call_
     range_relational's own comment). One ranger, shared by both passes,
     disabled once at the very end, avoids that entirely.  */
  /* Seeded before cg_predicate_facts_walk's own call just below (rather
     than after, as an earlier version of this function had it) so
     ESTABLISHED_RANGE -- self-trust only, e.g. a declared precondition's
     own 'k <= 0' -- can be threaded into that walk's own final pass,
     needed there to resolve a shift amount that isn't itself a literal
     (cg_get_call_relational's own PLUS_EXPR/MINUS_EXPR fallback to cg_
     established_range_of) when the shift comes from a self-trust fact
     rather than a branch-derived one. cg_seed_self_trust needs neither
     dominance info nor a ranger, so reordering it earlier is safe.  */
  hash_map<tree, cg_fact> established;
  hash_map<tree, cg_fact> established_nz;
  hash_map<tree, cg_range_lite> established_range;
  hash_map<tree, cg_rel_fact> established_rel;
  hash_map<tree, cg_call_rel_fact> established_call_rel;
  hash_map<cg_field_key_hash, cg_call_call_rel_fact> established_call_call_rel;
  hash_map<tree, cg_type_bound_fact> established_type_bound;
  cg_seed_self_trust (fun, established, established_nz, established_range,
		       established_rel, established_call_rel,
		       established_call_call_rel, established_type_bound);

  calculate_dominance_info (CDI_DOMINATORS);
  gimple_ranger *ranger = enable_ranger (fun, false);

  cg_assert_memory_verdict_map assert_memory_verdicts;
  hash_map<basic_block, auto_vec<edge> *> short_circuit_sources;
  cg_short_circuit_sources = &short_circuit_sources;
  cg_array_bounds_ctx array_bounds_ctx
    = { &established_range, &scalar_range_cache, ranger };
  cg_array_bounds = &array_bounds_ctx;
  /* Floating-point ranges established for SSA values: by a
     precondition of this function, by a load of a field with one, or
     (while the check below is in its dominator subtree) by a
     contract_assert.  */
  hash_map<tree, frange> established_frange;
  cg_seed_float_self_trust (fun, established_frange);
  auto frange_ovr = make_temp_override (cg_established_frange_for_verdicts,
					&established_frange);
  if (LAMBDA_FUNCTION_P (fun->decl))
    cg_seed_iile_capture_facts (fun, established, established_nz,
				established_range);
  if (predicate_core)
    cg_seed_predicate_site_facts (fun, established, established_nz,
				  established_range, established_frange);

  cg_iile_info **iile = (cg_iile_infos && LAMBDA_FUNCTION_P (fun->decl)
			  ? cg_iile_infos->get (fun->decl) : NULL);
  bool conveyor_code = (DECL_DECLARED_CONVEYOR_P (fun->decl) || predicate_core
			|| (iile && !(*iile)->invalid
			    && (*iile)->conveyor_caller));
  /* See cg_callee_pre_mandatory and cg_callee_pre_only; set before the
     facts walk, which also checks calls' preconditions
     (cg_consult_persistent_facts).  */
  auto mandatory = make_temp_override (cg_callee_pre_mandatory,
				       conveyor_code
				       && (flag_contract_analysis_engine != 0
					   || flag_contract_conveyor_proofs_gimple));
  auto pre_only = make_temp_override (cg_callee_pre_only, never_proven_core);

  cg_predicate_facts_walk (fun, &scalar_range_cache,
			     established_range, &call_relational_verdict,
			     ranger,
			     established_nz, &assert_memory_verdicts,
			     &established_frange, &established);

  /* A conveyor callee's implicit reference-parameter and receiver
     obligations are the callee's, whoever calls it, as in the AST engine
     (Q2 is gated on the caller inside cg_check_call_reference_safety).  */
  bool check_reference_safety = !never_proven_core;
  bool check_item8_ub = conveyor_code && !never_proven_core;

  /* Walk the dominator tree, so a contract_assert's facts are in the
     fact maps exactly while its dominator subtree is being checked (see
     cg_assert_fact_scope).  Blocks the dominator tree from the entry
     doesn't reach are checked afterwards, with no assertion facts.  */
  cg_assert_fact_scope assert_facts (established, established_nz,
				     established_range,
				     established_type_bound,
				     established_frange);
  auto check_block = [&] (basic_block bb)
    {
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  if (check_item8_ub)
	    {
	      cg_check_div_mod_ub (stmt, established_nz, established_range,
				    scalar_range_cache, ranger);
	      /* A predicate's overflow is the AST engine's to report under
		 the engine option: the front end has folded some of the
		 condition's arithmetic away before this function exists
		 (contracts.cc, oa_predicate_text_scan).  */
	      if (!(predicate_core && flag_contract_analysis_engine != 0))
		cg_check_overflow_ub (stmt, established_type_bound,
				      established_range, scalar_range_cache,
				      ranger);
	      cg_check_dereference_ub (stmt, established);
	      cg_check_array_bounds_ub (stmt);
	    }
	  if (is_gimple_call (stmt))
	    {
	      gcall *call = as_a <gcall *> (stmt);
	      if (check_reference_safety)
		cg_check_call_reference_safety (call, fun, established);
	      cg_check_call (call, established, established_nz,
			     established_range, established_rel,
			     established_call_call_rel,
			     scalar_range_cache, ranger,
			     &call_relational_verdict, &established_frange);
	      cg_record_iile_site_facts (call, fun, established_range,
					 scalar_range_cache, ranger);
	      cg_record_predicate_site_facts (call, established, established_nz,
					      established_range,
					      scalar_range_cache, ranger,
					      established_frange);
	      if (cg_check_contract_assert (call, established, established_nz,
					    established_range, established_rel,
					    scalar_range_cache, ranger,
					    assert_memory_verdicts,
					    established_frange))
		assert_facts.establish (call);
	    }
	}
    };

  auto_sbitmap visited (last_basic_block_for_fn (fun));
  bitmap_clear (visited);
  struct walk_item { basic_block bb; bool leave; unsigned mark; };
  auto_vec<walk_item> stack;
  stack.safe_push ({ ENTRY_BLOCK_PTR_FOR_FN (fun), false, 0 });
  while (!stack.is_empty ())
    {
      walk_item item = stack.pop ();
      if (item.leave)
	{
	  assert_facts.restore (item.mark);
	  continue;
	}
      stack.safe_push ({ item.bb, true, assert_facts.mark () });
      bitmap_set_bit (visited, item.bb->index);
      if (item.bb != ENTRY_BLOCK_PTR_FOR_FN (fun)
	  && item.bb != EXIT_BLOCK_PTR_FOR_FN (fun))
	check_block (item.bb);
      for (basic_block son = first_dom_son (CDI_DOMINATORS, item.bb); son;
	   son = next_dom_son (CDI_DOMINATORS, son))
	stack.safe_push ({ son, false, 0 });
    }
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    if (!bitmap_bit_p (visited, bb->index))
      check_block (bb);

  for (auto it : assert_memory_verdicts)
    XDELETE (it.second);
  for (auto it : short_circuit_sources)
    delete it.second;
  cg_short_circuit_sources = NULL;
  cg_array_bounds = NULL;
  disable_ranger (fun);

  cg_remove_contract_marks (fun);
  return 0;
}

} // anon namespace

gimple_opt_pass *
make_pass_contracts_gimple (gcc::context *ctxt)
{
  return new pass_contracts_gimple (ctxt);
}

/* D4324/P2680: run pass_contracts_gimple on FNDECL's own body right
   now, eagerly, rather than waiting for the normal end-of-compilation
   pipeline -- called from expand_or_defer_fn (semantics.cc) once
   FNDECL's body is otherwise complete, the exact choke point and exact
   rationale [[project_eager_profile_checking_fix]] already used for
   the std::init/std::invalidation profile checkers: symbol_table::
   compile (cgraphunit.cc) gates SSA construction and every GIMPLE
   pass -- pass_build_ssa_passes's own "bool gate () { return !seen_
   error () && !in_lto_p; }" (passes.cc) -- on the WHOLE translation
   unit being free of front-end errors. Confirmed directly: a function
   with a genuinely unguarded, out-of-bounds vector access silently
   stopped being reported at all under -fcontract-conveyor-proofs-
   gimple, purely because some OTHER, unrelated function anywhere else
   in the same file had its own front-end error (e.g. an unprovable
   is_object_address obligation) -- the exact same whole-TU
   suppression already fixed once for the other two checkers.

   Unlike those two, this pass cannot simply skip every DECL_DECLARED_
   CONSTEXPR_P function the way profiles_eager_check_function_1 does:
   several existing GIMPLE conveyor tests (e.g. d4324-gimple-
   conversion-*-ok.C) rely on a constexpr conversion operator's own
   body being checked here, and conveyor/symbolic proofs are about
   ordinary runtime verification, not something constexpr code is
   exempt from the way memory-safety profiles are.

   A constexpr function specifically cannot be analyzed THIS early,
   though, for a second, independent reason beyond the DECL_SAVED_TREE
   clobbering an earlier version of this fix addressed by a save/
   restore (removed again below, once this deferral made it
   unnecessary): confirmed directly, via gdb, that even a CORRECT
   diagnostic emitted here for a constexpr function (e.g. "cannot
   prove is_object_address" for std::to_string's own call into
   basic_string's constructor) can itself trigger cp_finish_decl's own
   ordinary maybe_constant_init attempt on some unrelated local
   variable's initializer elsewhere in the same constexpr function,
   which ICEs deep inside cxx_eval_call_expression (a tree-check
   failure recovering a constructor's own 'this' binding) -- but ONLY
   when reached this early (mid-parse, at_eof == 0). The IDENTICAL
   diagnostic, for the IDENTICAL reachable call, reached instead at
   genuine end-of-TU (restoring the old register_pass_info splice
   temporarily, as a controlled experiment) reports cleanly with no
   crash at all -- conclusively ruling this out as a pre-existing,
   latent bug independent of timing, and confirming it is specifically
   the mid-parse timing itself that is unsafe, not anything about this
   pass's own logic. Rather than chase down why cp_finish_decl's own
   constant-folding machinery misbehaves only at this one, never-
   before-exercised timing (a different subsystem entirely), a
   constexpr function is instead deferred to the one point already
   known to be safe: genuine end-of-TU (at_eof == 3,
   c_parse_final_cleanups, decl2.cc) -- exactly where the OLD pipeline
   always ran this pass for every function, constexpr or not, and
   exactly the point cxx_eval_call_expression's own "at_eof >= 3"
   assert (constexpr.cc) already documents as the one it expects
   DECL_SAVED_TREE-clobbering work like this to happen at.

   A THIRD hazard, found afterward, shares this exact DECL_SAVED_TREE
   root cause but strikes ordinary (non-constexpr) comdat/inline
   functions instead: see the deferral site's own comment below
   (contracts_gimple_eager_check_function_1) for the full account --
   gimplifying such a function here can leave its own vague-linkage
   decision permanently stuck "assume external", silently dropping its
   body at link time despite genuine callers.  */

static bool cg_eager_check_active;
static vec<tree> cg_eager_check_pending;

/* Mirrors profiles_eager_check_deferred_until_complete's own comment:
   a lambda's own call operator can reach expand_or_defer_fn before its
   closure type's own finish_struct has actually completed, since
   finish_lambda_function runs before the enclosing cp_parser_lambda_
   expression calls finish_struct on the closure type itself.  */
static vec<tree> cg_eager_check_deferred_until_complete;

/* DECL_DECLARED_CONSTEXPR_P functions, deferred until genuine end-of-
   TU -- see the big comment above for why.  Drained by contracts_
   gimple_eager_check_drain_at_eof, called from c_parse_final_cleanups
   (decl2.cc) right after at_eof is set to 3.  */
static vec<tree> cg_eager_check_deferred_until_eof;

/* Predicate helpers, of which the core functions are checked at the end
   of the translation unit (see contracts_gimple_eager_check_function).  */
static vec<tree> cg_eager_check_predicate_helpers;

static gimple_opt_pass *cg_eager_build_ssa_pass;
static gimple_opt_pass *cg_eager_contracts_pass;

static void contracts_gimple_eager_check_function_1 (tree fndecl,
						      bool is_eof_drain = false);

/* Called from finish_struct (class.cc) the moment TYPE is fully
   complete -- retries any FUNCTION_DECL whose own eager check was
   deferred above because its DECL_CONTEXT wasn't ready yet.  */

void
contracts_gimple_eager_check_type_complete (tree)
{
  if (cg_eager_check_deferred_until_complete.is_empty ())
    return;
  auto_vec<tree> retry;
  retry.safe_splice (cg_eager_check_deferred_until_complete);
  cg_eager_check_deferred_until_complete.truncate (0);
  for (tree fndecl : retry)
    contracts_gimple_eager_check_function (fndecl);
}

void
contracts_gimple_eager_check_function (tree fndecl)
{
  /* Cheap, unconditional no-op for the overwhelmingly common case:
     neither GIMPLE-pass engine is active at all.  Mirrors pass_
     contracts_gimple::gate exactly, so this never does GIMPLE-level
     work the pass itself wouldn't have done anyway.  */
  if (!flag_contract_control_objects
      || !(flag_contract_conveyor_proofs_gimple
	   || flag_contract_symbolic_proofs_gimple))
    return;

  /* D4324/P2680: a generated "pred"/"thunk"/consteval-thunk helper
     (ldf_contract_predicate_helper, see its own comment) is never
     itself diagnosed -- pass_contracts_gimple::execute's own first
     statement already skips it unconditionally once reached normally.
     Skipping it HERE too is not just redundant-but-harmless: a helper
     can be built and finish_function'd (reaching expand_or_defer_fn,
     hence here) WHILE an entirely unrelated, OUTER contract check is
     still being built on the C++ stack (build_contract_check's own
     CC_BIND, a freshly allocated BIND_EXPR not yet attached to any GC
     root) -- confirmed directly: eagerly gimplifying/analyzing such a
     helper here triggered a GC collection as a side effect, collecting
     that still-unrooted, in-progress CC_BIND out from under its own
     caller (an ICE, "tree check: expected bind_expr, have ggc_freed",
     build_predicate_arg_struct_var). The exact same general hazard
     profiles_eager_check_function's own comment calls "recursive
     re-entrancy" -- just manifesting as a collected stack-local tree
     rather than a blown C++ stack -- and the active/pending queue
     below cannot help here, since we are not re-entering THIS
     function's own active call at all: finish_function (which builds
     and completes a helper like this one) runs *before*
     expand_or_defer_fn is ever reached for the OUTER function whose
     own body is still mid-genericize.  */
  if (CONTRACT_HELPER (fndecl) != ldf_contract_none)
    {
      /* A predicate core function is checked for mutation
	 (cg_check_predicate_mutation) -- at the end of the translation
	 unit, the safe point the comment above calls for.  Which helpers
	 are core functions is only known once they're built, so all are
	 queued.  */
      if (flag_contract_conveyor_proofs_gimple
	  && CONTRACT_HELPER (fndecl) == ldf_contract_predicate_helper
	  && !cg_eager_check_predicate_helpers.contains (fndecl))
	cg_eager_check_predicate_helpers.safe_push (fndecl);
      return;
    }

  /* An immediate function is never genericized (finish_function) or
     emitted, so there is no GIMPLE body to analyze: gimplifying its
     C++ statement trees ICEs.  Every call to it is a constant
     evaluation, which rejects core undefined behavior itself.  */
  if (DECL_IMMEDIATE_FUNCTION_P (fndecl))
    return;

  /* The gate the AST engine already applies before its own oa_walk_
     stmt (oa_function_needs_walk_p, contracts.cc, exported for exactly
     this reuse): a function with no contract of its own, no call to
     anything with an active precondition, and that is not itself
     conveyor/symbolic, is one neither engine has any reason to
     analyze. This is not merely an optimization here -- it is what
     keeps this eager hook's own blast radius matched to the AST
     engine's own, long-established scope (see [[project_gimple_ast_
     parity_direction]]), rather than every ordinary, uninvolved
     function whose body simply happens to finish parsing while either
     GIMPLE-pass flag is on. Confirmed empirically this is also what a
     genuine, reproducible stack overflow needed: cgraph_node::analyze
     (below) can itself trigger further template instantiation as a
     side effect of resolving a function's own types, and running that
     -- plus a full SSA build and this pass -- for library-internal
     helpers (e.g. std::to_string/stoi, pulled in transitively by a
     bare '#include <vector>', never called by the user's own code at
     all) added enough extra, needless C++ call-stack depth, nested
     inside the front end's own already-deep template-instantiation
     recursion during ordinary parsing, to exhaust the stack on code
     that this gate alone is enough to skip entirely -- cheaply (an
     O(1) bitfield read, no tree walk of any kind, so this gate itself
     can never be the one doing the stack-exhausting).  */
  if (!oa_function_needs_walk_p (fndecl))
    return;

  if (cg_eager_check_active)
    {
      cg_eager_check_pending.safe_push (fndecl);
      return;
    }

  cg_eager_check_active = true;
  contracts_gimple_eager_check_function_1 (fndecl);
  while (!cg_eager_check_pending.is_empty ())
    contracts_gimple_eager_check_function_1 (cg_eager_check_pending.pop ());
  cg_eager_check_active = false;
}

static void
contracts_gimple_eager_check_function_1 (tree fndecl, bool is_eof_drain)
{
  /* A body deserialized from an imported module's BMI never went
     through this translation unit's own expand_or_defer_fn the first
     time around.  */
  if (DECL_LANG_SPECIFIC (fndecl) && DECL_MODULE_IMPORT_P (fndecl))
    return;

  if (!DECL_STRUCT_FUNCTION (fndecl))
    return;

  tree ctx = DECL_CONTEXT (fndecl);
  if (ctx && RECORD_OR_UNION_TYPE_P (ctx) && !COMPLETE_TYPE_P (ctx))
    {
      cg_eager_check_deferred_until_complete.safe_push (fndecl);
      return;
    }

  cgraph_node *node = cgraph_node::get_create (fndecl);
  if (node->analyzed)
    /* Already processed (e.g. reached a second time through a clone,
       or a path that calls expand_or_defer_fn more than once for the
       same decl).  */
    return;

  /* See the big comment above contracts_gimple_eager_check_function's
     own vec declarations for the constexpr-specific hazard (a
     cxx_eval_call_expression crash) this deferral was first added for.
     A SECOND, independent hazard shares the identical root cause and
     the identical fix: gimplify_function_tree (called from node->
     analyze () below) unconditionally clears DECL_SAVED_TREE
     (gimplify.cc) the moment this eager hook analyzes a function --
     but c_parse_final_cleanups's own deferred_fns sweep (decl2.cc,
     "we lie to the back end, pretending that some functions are not
     defined when they really are... but we must stop lying when the
     functions are referenced" -- the one place that ever calls
     import_export_decl to turn a vague-linkage function's own
     TENTATIVE 'assume DECL_EXTERNAL, decide later' linkage, set by
     tentative_decl_linkage at ordinary parse time for every comdat/
     inline function, into the FINAL decision) silently skips any
     entry whose own DECL_SAVED_TREE is already gone ("if (!DECL_
     SAVED_TREE (decl)) continue;"). Skipped means DECL_EXTERNAL stays
     stuck at its tentative 'true' forever, and the function's body is
     silently never emitted -- confirmed directly via gdb: a real -ok
     test's own io_facility::open()/read()/is_opened(), each genuinely
     called from dot_receiver/pointer_receiver/main, each never once
     reached import_export_decl at all (conditional breakpoint on its
     own decl never fired), linking with "undefined reference" despite
     the calls being real. The baseline (same file, no GIMPLE-pass flag
     at all, so this hook never runs) was confirmed clean. This is not
     specific to constexpr -- it is specific to ANY function whose
     DECL_SAVED_TREE this hook clears before c_parse_final_cleanups's
     own deferred_fns sweep (which, like the constexpr hazard's own
     cxx_eval_call_expression, only runs safely at genuine end-of-TU)
     gets to see it, so the SAME deferral is extended to cover it:
     vague_linkage_p (decl2.cc) is the exact predicate tentative_decl_
     linkage itself already uses to decide "this needs the deferred_
     fns treatment" -- the natural, precise match for "needs this same
     deferral", covering every comdat/weak/inline function (io_
     facility::open() included) in addition to constexpr ones (the two
     sets overlap but neither contains the other).  IS_EOF_DRAIN is
     true only when contracts_gimple_eager_check_drain_at_eof is the
     one calling this, i.e. we ARE already at that safe point (c_parse_
     final_cleanups's own deferred_fns sweep, earlier in that same
     function, has already run) -- process it for real rather than
     deferring it right back to the same queue.  */
  if ((DECL_DECLARED_CONSTEXPR_P (fndecl) || vague_linkage_p (fndecl))
      && !is_eof_drain)
    {
      cg_eager_check_deferred_until_eof.safe_push (fndecl);
      return;
    }

  /* Mirrors cgraph_node::add_new_function's own FINISHED-state
     handling (cgraphunit.cc) -- see profiles_eager_check_function_1's
     own identical comment for why that function isn't called
     directly.  */
  node->definition = true;
  node->analyze ();
  push_cfun (DECL_STRUCT_FUNCTION (fndecl));
  gimple_register_cfg_hooks ();
  bitmap_obstack_initialize (NULL);

  /* Build SSA directly, not via the early-optimization pipeline --
     see profiles_eager_check_function_1's own comment (the early-
     inline hazard) for why.  This pass needs nothing beyond PROP_ssa
     (see pass_data_contracts_gimple above).  */
  if (!gimple_in_ssa_p (DECL_STRUCT_FUNCTION (fndecl)))
    {
      if (!cg_eager_build_ssa_pass)
	cg_eager_build_ssa_pass = make_pass_build_ssa (g);
      execute_pass_list (cfun, cg_eager_build_ssa_pass);
    }

  if (!cg_eager_contracts_pass)
    cg_eager_contracts_pass = make_pass_contracts_gimple (g);
  execute_pass_list (cfun, cg_eager_contracts_pass);

  bitmap_obstack_release (NULL);
  pop_cfun ();
}

/* Called from c_parse_final_cleanups (decl2.cc) right after at_eof is
   set to 3 -- the one point already known to be safe (see the big
   comment above) for analyzing every DECL_DECLARED_CONSTEXPR_P
   function deferred above. Loops over both queues until both are
   empty, not just cg_eager_check_deferred_until_eof alone: processing
   one deferred function can itself reach expand_or_defer_fn for
   another (ordinary re-entrancy, same as the normal eager path), which
   routes back through the public entry point and so may push onto
   EITHER queue while this is draining.  */

void
contracts_gimple_eager_check_drain_at_eof (void)
{
  cg_eager_check_active = true;
  do
    {
      while (!cg_eager_check_deferred_until_eof.is_empty ()
	     || !cg_eager_check_pending.is_empty ())
	{
	  if (!cg_eager_check_deferred_until_eof.is_empty ())
	    contracts_gimple_eager_check_function_1
	      (cg_eager_check_deferred_until_eof.pop (), /*is_eof_drain=*/true);
	  else
	    contracts_gimple_eager_check_function_1 (cg_eager_check_pending.pop ());
	}
      /* The bodies of lambdas invoked immediately: after their callers
	 (cg_record_iile_site_facts).  */
      while (!cg_eager_check_deferred_until_eof_iile.is_empty ())
	contracts_gimple_eager_check_function_1
	  (cg_eager_check_deferred_until_eof_iile.pop (), /*is_eof_drain=*/true);
      /* The predicate core functions last: checking one uses what the
	 checks of its contract's own function recorded about the values
	 it is called with (cg_record_predicate_site_facts).  */
      auto_vec<tree> helpers;
      for (tree fn : cg_eager_check_predicate_helpers)
	helpers.safe_push (fn);
      cg_eager_check_predicate_helpers.truncate (0);
      static hash_set<tree> checked_cores;
      for (tree fn : helpers)
	{
	  tree owner;
	  if (predicate_core_function_contract (fn, &owner)
	      && !checked_cores.add (fn))
	    contracts_gimple_eager_check_function_1 (fn, /*is_eof_drain=*/true);
	}
    }
  while (!cg_eager_check_deferred_until_eof.is_empty ()
	 || !cg_eager_check_pending.is_empty ()
	 || !cg_eager_check_deferred_until_eof_iile.is_empty ()
	 || !cg_eager_check_predicate_helpers.is_empty ());
  cg_eager_check_active = false;
}
