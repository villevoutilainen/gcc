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
   established fact's own RHS).  */

static tree
cg_resolve_conversion_receiver (tree val)
{
  if (val == NULL_TREE || TREE_CODE (val) != SSA_NAME)
    return val;
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (!def || !is_gimple_call (def))
    return val;
  gcall *call = as_a <gcall *> (def);
  tree callee = gimple_call_fndecl (call);
  if (!callee || !DECL_CONV_FN_P (callee) || gimple_call_num_args (call) != 1)
    return val;
  tree receiver = gimple_call_arg (call, 0);
  if (TREE_CODE (receiver) == ADDR_EXPR)
    receiver = TREE_OPERAND (receiver, 0);
  return receiver;
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
			  gimple *at_stmt = NULL)
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
    }

  if (ranger)
    {
      int_range_max vr;
      if (ranger->range_of_expr (vr, val, at_stmt) && !vr.undefined_p ()
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
	  if (arg == result_id)
	    return true;
	}
    }
  return false;
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
			       hash_set<tree> &in_progress)
{
  if (val == NULL_TREE)
    return false;

  if (TREE_CODE (val) == ADDR_EXPR)
    return true;

  if (TREE_CODE (val) != SSA_NAME)
    return false;

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
					    in_progress))
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
	result = cg_provable_object_address_p (gimple_assign_rhs1 (def),
						established, require_conveyor,
						in_progress);
    }
  else if (def && is_gimple_call (def))
    {
      tree callee = gimple_call_fndecl (as_a <gcall *> (def));
      if (callee)
	result = cg_call_postcondition_guarantees_p
	  (callee, require_conveyor, !require_conveyor,
	   is_object_address_call_p);
    }

  in_progress.remove (val);
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

   Deliberately does NOT yet recognize a field of an owned object
   ('&this->field') or an array element of an owned array
   ('&arr[index]') as owned in their own right -- oa_reference_owned_p
   itself gained these as later, separate refinements (see its own
   comment), and porting them here is a documented, deferred follow-up,
   not attempted in this first GIMPLE port. Also, unlike oa_reference_
   owned_p, there is no separate IN_PREDICATE_CONTEXT ruleset here:
   contract_assert isn't ported to GIMPLE yet either (a separate, later
   item in this same porting effort), so that distinction has nothing
   to apply to yet.

   Confirmed by direct testing that this same "field of an owned
   object" gap also covers a base-class SUBOBJECT access: '*this' in a
   member of a DERIVED class, forwarded to another conveyor call's
   reference parameter, needs an implicit upcast to the base class --
   GCC represents this the same way as an ordinary field access
   ('ADDR_EXPR (COMPONENT_REF (...)))', not a bare 'ADDR_EXPR (decl)'
   -- so it falls into this function's own conservative default (NOT
   owned) today, even though it should be, by the exact same "'this'
   never extends the cone" reasoning as the non-inheritance case just
   above. A member of a class with no base class at all is unaffected
   (no upcast needed, so 'this' reaches here unwrapped).  */

static bool
cg_provably_owned_p (tree val, function *fun, hash_set<tree> &in_progress)
{
  if (val == NULL_TREE)
    return false;

  if (TREE_CODE (val) == ADDR_EXPR)
    {
      tree base = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (val, 0));
      return DECL_P (base) && DECL_CONTEXT (base) == fun->decl;
    }

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  /* The function's own parameter, used directly (its own initial SSA
     value), with no ADDR_EXPR at all -- this function (see its own
     caller, cg_check_call_reference_safety) only ever asks this about a
     REFERENCE-typed *target*, i.e. always "what does this value
     designate," never "is this pointer's own value owned" (a target
     that's itself a pointer, including 'this' used as an ordinary
     receiver, is entirely exempt from Q2 -- see this function's own
     leading comment). Mirrors oa_reference_owned_p's own ASKING_ABOUT_
     POINTER_VALUE disambiguation: a REFERENCE-typed parameter used
     directly IS "received as my own" (accept_ref_param's own shape) --
     but a POINTER-typed parameter's bare value, asked about this way,
     means its own POINTEE is being bound to the reference target
     (reject_ptr_param_dereference's own shape: P's own storage is this
     function's private copy, but *P is still the caller's own,
     unrelated object) -- NOT owned, unless it's specifically 'this'
     (re-lending 'this' further, even dereferenced, never extends the
     cone: no new party gains access, it's still the exact object this
     function was already given).  */
  tree var = SSA_NAME_VAR (val);
  if (var && TREE_CODE (var) == PARM_DECL && SSA_NAME_IS_DEFAULT_DEF (val)
      && (TREE_CODE (TREE_TYPE (var)) == REFERENCE_TYPE
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
	if (!cg_provably_owned_p (gimple_phi_arg_def (def, i), fun, in_progress))
	  {
	    result = false;
	    break;
	  }
    }
  else if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if (code == ADDR_EXPR || CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	result = cg_provably_owned_p (gimple_assign_rhs1 (def), fun, in_progress);
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
	      if (!cg_provably_owned_p (arg, fun, in_progress))
		{
		  result = false;
		  break;
		}
	    }
	}
    }

  in_progress.remove (val);
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
     "every reference-typed parameter... is itself provably is_object_
     address" seeding) -- without this, FNDECL's own reference
     parameters, forwarded unchanged to another conveyor call, would be
     flagged by cg_check_call_reference_safety's own Q1 check as
     unprovable, even though a reference parameter's own value is
     already a validated address by construction (whatever proved it
     valid for THIS function's own call is exactly why it's valid to
     forward).

     P2680 author correction (2026-08-19): widened from DECL_DECLARED_
     CONVEYOR_P (FNDECL)-gated to unconditional (every function this
     pass ever reaches), mirroring the identical widening in contracts.cc
     -- a non-conveyor-declared function with its own contract
     specifiers (cg_function_might_need_reference_safety_walk_p's own
     broader trigger, which the call-site CONSULT side already uses) can
     just as easily forward its own reference parameter, or 'this', to a
     nested conveyor call from its own precondition/postcondition text;
     the original DECL_DECLARED_CONVEYOR_P gate here was narrower than
     that consult-side trigger, a real, independently-confirmed gap
     (contracts.cc's own identical comment has the concrete repro). Sound
     for the same reason as always: a bound reference is guaranteed
     valid for its own entire lifetime by the language itself, and
     'this' is trusted for the ENTIRE duration of the function whose
     'this' it is -- neither depends on that function being conveyor-
     declared.

     'this' gets the exact same treatment now too, not the unconditional
     cg_provable_object_address_p axiom it used to be (see that
     function's own comment) -- both are now ordinary self-trusted
     ESTABLISHED facts. The NEW restriction this correction actually adds
     -- a member conveyor call's own receiver must be proven by its
     CALLER -- lives entirely at the call site instead (cg_check_call_
     reference_safety's own is_this_parameter handling, below), unaffected
     by this seeding: this only ever makes a function trust its OWN
     parameters *within its own body*.  */
  for (tree parm = DECL_ARGUMENTS (fndecl); parm; parm = DECL_CHAIN (parm))
    if (TREE_CODE (TREE_TYPE (parm)) == REFERENCE_TYPE || is_this_parameter (parm))
      {
	tree key = cg_self_trust_key (fun, parm);
	if (key)
	  established.put (key, { /*conveyor_established=*/true });
      }
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

static bool
cg_function_might_need_reference_safety_walk_p (function *fun)
{
  tree fndecl = fun->decl;
  return DECL_DECLARED_CONVEYOR_P (fndecl)
	 || get_fn_contract_specifiers (fndecl) != NULL_TREE;
}

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

   Only ever invoked (see this function's own caller in pass_contracts_
   gimple::execute) when cg_function_might_need_reference_safety_walk_p
   says the CALLING function itself warrants it -- see that function's
   own comment for why this scoping is load-bearing, not optional: an
   earlier, unconditional version of this check (run for every call in
   every function in the TU, matching how cg_check_call's own existing
   checks already ran) surfaced a real regression deep in libstdc++
   internals (std::vector<int> constructing __new_allocator's own
   _GLIBCXX_CONVEYOR-tagged copy constructor from a plain, uncontracted,
   non-conveyor helper function) -- confirmed, by direct testing, that
   contracts.cc's own AST engine does NOT flag this same call either,
   even though its own item 7/Q2 check is *also* unconditional on any
   proof flag (only gated on flag_contract_control_objects): the AST
   engine's own oa_function_needs_walk_p never triggers the walk for
   that specific internal calling function to begin with. Matching that
   same calling-function-level gate here keeps this port at genuine
   parity with the AST engine's own real, observed scope -- not
   *stricter* than it.  */

static void
cg_check_call_reference_safety (gcall *call, function *fun,
				 hash_map<tree, cg_fact> &established)
{
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
	      if (!cg_provable_object_address_p (substituted, established,
						  /*require_conveyor=*/true,
						  in_progress))
		error_at (gimple_location (call),
			  "cannot prove %<is_object_address%> for %qE, "
			  "implicitly required by the receiver of %qD",
			  substituted, callee);
	    }
	  continue;
	}
      if (TREE_CODE (TREE_TYPE (parm)) != REFERENCE_TYPE)
	continue;
      if (argno >= gimple_call_num_args (call))
	continue;

      tree substituted = cg_resolve_call_argument (call, argno);
      hash_set<tree> in_progress;
      if (!cg_provable_object_address_p (substituted, established,
					  /*require_conveyor=*/true, in_progress))
	{
	  error_at (gimple_location (call),
		    "cannot prove %<is_object_address%> for %qE, "
		    "required by the precondition of %qD", substituted, callee);
	  continue;
	}

      bool is_const_ref = TYPE_READONLY (TREE_TYPE (TREE_TYPE (parm)));
      if (!is_const_ref)
	{
	  hash_set<tree> owned_in_progress;
	  if (!cg_provably_owned_p (substituted, fun, owned_in_progress))
	    error_at (gimple_location (call),
		      "argument %qE is not owned by the calling function, "
		      "so it may not be passed as the non-const reference "
		      "parameter %qD of %qD", substituted, parm, callee);
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
   7/Q2's own gate (cg_function_might_need_reference_safety_walk_p),
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
    error_at (gimple_location (stmt),
	      "divisor %qE not provably nonzero in a conveyor function",
	      divisor);
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
	error_at (gimple_location (stmt), "negation of %qE not provably "
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
  if (code == PLUS_EXPR || code == MINUS_EXPR)
    {
      tree var_side = NULL_TREE;
      bool increasing = false;
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
  if (code == PLUS_EXPR)
    {
      bool hi_ok = have_a && have_b && a.has_hi && b.has_hi
		   && a.hi + b.hi <= type_max;
      bool lo_ok = have_a && have_b && a.has_lo && b.has_lo
		   && a.lo + b.lo >= type_min;
      safe = hi_ok && lo_ok;
    }
  else if (code == MINUS_EXPR)
    {
      bool hi_ok = have_a && have_b && a.has_hi && b.has_lo
		   && a.hi - b.lo <= type_max;
      bool lo_ok = have_a && have_b && a.has_lo && b.has_hi
		   && a.lo - b.hi >= type_min;
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

  if (!safe)
    error_at (gimple_location (stmt), "result of %qE not provably free of "
	      "overflow in a conveyor function", lhs);
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
  /* POINTER_TYPE only, excluding REFERENCE_TYPE -- a bound reference is
     guaranteed valid for its own entire lifetime by the language itself,
     never itself the unprovable-UB case this check exists for, mirroring
     oa_scan_array_bounds_in_expr's own identical exclusion (see that
     function's own comment for the lambda-by-reference-capture case
     that motivated it).  */
  if (TREE_CODE (TREE_TYPE (base)) != POINTER_TYPE)
    return;

  hash_set<tree> in_progress;
  if (!cg_provable_object_address_p (base, established, /*require_conveyor=*/true,
				       in_progress))
    error_at (gimple_location (stmt), "pointer dereference of %qE not "
	      "provably valid in a conveyor function", base);
}

static void
cg_check_dereference_ub (gimple *stmt, hash_map<tree, cg_fact> &established)
{
  if (is_gimple_assign (stmt))
    {
      cg_check_one_dereference_candidate (stmt, gimple_assign_lhs (stmt),
					    established);
      cg_check_one_dereference_candidate (stmt, gimple_assign_rhs1 (stmt),
					    established);
    }
  else if (is_gimple_call (stmt))
    cg_check_one_dereference_candidate (stmt, gimple_call_lhs (stmt), established);
}

/* CALL's own callee, checked against ESTABLISHED/ESTABLISHED_NZ as
   they stand right before CALL -- the consult side. Each of the
   callee's own preconditions is only checked against the flag whose
   own flavor it matches (a conveyor-active precondition is only an
   obligation to discharge when -fcontract-conveyor-proofs-gimple is
   on, and its own consult REQUIRE_CONVEYORs the one-way-trust rule; a
   symbolic-active one is only checked when -fcontract-symbolic-proofs-
   gimple is on, and accepts either provenance).  */

static void
cg_check_call (gcall *call, hash_map<tree, cg_fact> &established,
		hash_map<tree, cg_fact> &established_nz,
		hash_map<tree, cg_range_lite> &established_range,
		hash_map<tree, cg_rel_fact> &established_rel,
		hash_map<tree, cg_call_rel_fact> &established_call_rel,
		hash_map<cg_field_key_hash, cg_call_call_rel_fact>
		  &established_call_call_rel,
		hash_map<tree, cg_range_lite> &scalar_range_cache,
		gimple_ranger *ranger,
		hash_set<gimple *> *call_relational_verdict)
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
      bool check_as_conveyor = conveyor_active && flag_contract_conveyor_proofs_gimple;
      bool check_as_symbolic = symbolic_active && flag_contract_symbolic_proofs_gimple;
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
      bool strict = (check_as_conveyor
		     && oa_contract_conveyor_strict_cached_p (contract))
		    || (check_as_symbolic
			&& oa_contract_symbolic_strict_cached_p (contract));

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
	  if (!cg_find_param_position (callee, arg, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;

	  tree substituted = cg_resolve_call_argument (call, argno);
	  hash_set<tree> in_progress;
	  if (is_oa)
	    {
	      if (cg_provable_object_address_p (substituted, established,
						 require_conveyor, in_progress))
		continue; /* Proven true: silently discharged.  */
	      if (strict)
		error_at (gimple_location (call),
			  "cannot prove %<is_object_address%> for %qE, as "
			  "required by the precondition of %qD",
			  substituted, callee);
	      else
		warning_at (gimple_location (call), 0,
			    "cannot verify %<is_object_address%> for %qE, as "
			    "required by the precondition of %qD",
			    substituted, callee);
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
		  error_at (gimple_location (call),
			    "argument %qE provably violates the "
			    "precondition of %qD", substituted, callee);
		  continue;
		}
	      if (cg_provable_nonzero_p (substituted, established_nz,
					 require_conveyor, in_progress))
		continue; /* Proven true: silently discharged.  */
	      if (strict)
		error_at (gimple_location (call),
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

	  tree sub_param = cg_resolve_call_argument (call, param_argno);
	  tree sub_other = cg_resolve_call_argument (call, other_argno);

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
	      error_at (gimple_location (call),
			"argument %qE provably violates the precondition "
			"of %qD", sub_param, callee);
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
					   &param_range)
		&& cg_established_range_of (sub_other, established_range,
					      scalar_range_cache, ranger,
					      check_as_conveyor, check_as_symbolic,
					      &other_range))
	      {
		enum oa_range_subsumption_result r
		  = cg_range_pair_relation (param_range, rel_code, other_range);
		if (r == OA_RANGE_SUBSUMED)
		  continue; /* Proven true: silently discharged.  */
		if (r == OA_RANGE_DISJOINT)
		  {
		    error_at (gimple_location (call),
			      "argument %qE provably violates the "
			      "precondition of %qD", sub_param, callee);
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
	    error_at (gimple_location (call),
		      "cannot prove that %qE satisfies the "
		      "precondition of %qD", sub_param, callee);
	  else
	    warning_at (gimple_location (call), 0,
			"cannot verify that %qE satisfies the "
			"precondition of %qD", sub_param, callee);
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

	  tree sub_param = cg_resolve_call_argument (call, param_argno);
	  tree sub_receiver = cg_resolve_call_argument (call, receiver_argno);

	  hash_map<tree, cg_call_rel_fact> empty_call_rel_cache;
	  hash_map<tree, cg_range_lite> empty_scalar_range_cache;

	  tree_code fact_code;
	  tree fact_rhs_receiver, fact_rhs_callee;
	  cg_range_lite fact_offset;
	  bool fact_conveyor_established;
	  bool have_fact
	    = cg_get_call_relational (sub_param, established_call_rel,
				       empty_call_rel_cache, established_range,
				       empty_scalar_range_cache, ranger,
				       check_as_conveyor, check_as_symbolic,
				       &fact_code, &fact_rhs_receiver,
				       &fact_rhs_callee, &fact_offset,
				       &fact_conveyor_established);
	  bool same_accessor
	    = have_fact && fact_rhs_callee == rhs_callee
	      && fact_rhs_receiver == sub_receiver;
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
	    error_at (gimple_location (call),
		      "cannot prove that %qE satisfies the "
		      "precondition of %qD", sub_param, callee);
	  else
	    warning_at (gimple_location (call), 0,
			"cannot verify that %qE satisfies the "
			"precondition of %qD", sub_param, callee);
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
	  if (!have_fact)
	    reason = OA_UNPROVABLE_NO_FACT;
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

	  tree sub_lhs_receiver = cg_resolve_call_argument (call, lhs_argno);
	  tree sub_rhs_receiver = cg_resolve_call_argument (call, rhs_argno);

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
	      && fact_rhs_receiver == sub_rhs_receiver)
	    continue; /* Proven true: silently discharged.  */

	  if (strict)
	    error_at (gimple_location (call),
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
	  tree substituted = cg_resolve_call_argument (call, argno);

	  cg_range_lite established_r;
	  bool have_range
	    = cg_established_range_of (substituted, established_range,
					scalar_range_cache, ranger,
					check_as_conveyor,
					check_as_symbolic, &established_r);
	  if (have_range
	      && (!required.has_lo
		  || (established_r.has_lo && established_r.lo >= required.lo))
	      && (!required.has_hi
		  || (established_r.has_hi && established_r.hi <= required.hi)))
	    continue; /* Proven true: silently discharged.  */

	  if (strict)
	    error_at (gimple_location (call),
		      "cannot prove that %qE satisfies the precondition "
		      "of %qD", substituted, callee);
	  else
	    warning_at (gimple_location (call), 0,
			"cannot verify that %qE satisfies the precondition "
			"of %qD", substituted, callee);
	  /* See oa_unprovable_reason's own comment: cg_established_range_of
	     tries several independent sources internally and collapses all
	     of their own failures into one bare "false" -- UNRESOLVED_
	     OPERAND is as specific as this consult can get without
	     threading a reason through that function and each of its own
	     sources individually (not done here, per .claude/plans/
	     lazy-stirring-pearl.md's own Phase 4 scope).  */
	  if (const char *why = oa_unprovable_reason_text
		(have_range ? OA_UNPROVABLE_RANGE_PARTIAL
			    : OA_UNPROVABLE_UNRESOLVED_OPERAND))
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
struct cg_field_fact { cg_range_lite range; bool conveyor_established; };
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

static void
cg_process_field_write (tree lhs, tree rhs, function *fun,
			 hash_map<cg_field_key_hash, tree> &field_object_cache,
			 cg_dom_fact_state &state)
{
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
    }
}

/* CALL's own callee's declared precondition, checked against STATE as
   it stands right before CALL -- the consult side, for both facts,
   the persistent-fact analogue of cg_check_call.  */

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
      bool check_as_conveyor = conveyor_active && flag_contract_conveyor_proofs_gimple;
      bool check_as_symbolic = symbolic_active && flag_contract_symbolic_proofs_gimple;
      if (!check_as_conveyor && !check_as_symbolic)
	continue;
      bool require_conveyor = check_as_conveyor;

      /* D4324: see cg_check_call's own identical 'strict' computation.  */
      bool strict = (check_as_conveyor
		     && oa_contract_conveyor_strict_cached_p (contract))
		    || (check_as_symbolic
			&& oa_contract_symbolic_strict_cached_p (contract));

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
	  tree substituted = cg_resolve_call_argument (call, argno);
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

	  bool required = !negated;
	  cg_pred_fact *fact = identity ? state.pred.get (identity) : NULL;
	  if (fact && fact->pred_fn == pred_fn
	      && (!require_conveyor || fact->conveyor_established))
	    {
	      if (fact->polarity == required)
		continue; /* Proven true: silently discharged.  */
	      /* D4324: PRED_FN is established at the *opposite* polarity --
		 a genuine, provable contradiction, not merely "unknown" --
		 mirroring contracts.cc's own OA_PROVEN_FALSE tier for this
		 same shape (oa_env_predicate_result's own comment).  */
	      error_at (gimple_location (call),
			"argument %qE provably violates the precondition of "
			"%qD: %qD (%qE) is established %s, but the "
			"precondition requires it to be %s",
			substituted, callee, pred_fn, substituted,
			fact->polarity ? "true" : "false",
			required ? "true" : "false");
	      continue;
	    }

	  if (strict)
	    error_at (gimple_location (call),
		      "cannot prove that %qD (%qE) holds, as required by "
		      "the precondition of %qD", pred_fn, substituted, callee);
	  else
	    warning_at (gimple_location (call), 0,
			"cannot verify that %qD (%qE) holds, as required by "
			"the precondition of %qD", pred_fn, substituted, callee);
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
	  tree substituted = cg_resolve_call_argument (call, argno);
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

	  if (strict)
	    error_at (gimple_location (call),
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
	  tree substituted = cg_resolve_call_argument (call, argno);
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

	  if (strict)
	    error_at (gimple_location (call),
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

static void
cg_invalidate_persistent_facts_for_call_args (gcall *call, function *fun,
					       hash_map<cg_field_key_hash, tree> &field_object_cache,
					       cg_dom_fact_state &state)
{
  tree call_callee = gimple_call_fndecl (call);
  if (call_callee && DECL_CONV_FN_P (call_callee))
    return;
  unsigned n = gimple_call_num_args (call);
  for (unsigned i = 0; i < n; ++i)
    {
      tree arg = gimple_call_arg (call, i);
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
	continue;
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
}

/* Agreement-based, mirroring contracts.cc's own oa_env::predicate_
   fact_merge_with/field_alias_merge_with (never a union -- an entry
   survives only if both sides have it and fully agree).  */

static void
cg_dom_fact_state_merge (cg_dom_fact_state *dst, cg_dom_fact_state &src)
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
  for (auto it : dst->field)
    {
      const cg_field_fact *ov = src.field.get (it.first);
      if (!ov || ov->range.has_lo != it.second.range.has_lo
	  || ov->range.has_hi != it.second.range.has_hi
	  || (it.second.range.has_lo && ov->range.lo != it.second.range.lo)
	  || (it.second.range.has_hi && ov->range.hi != it.second.range.hi)
	  || ov->conveyor_established != it.second.conveyor_established)
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
      || a.call_rel.elements () != b.call_rel.elements ())
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
	  || ov->conveyor_established != it.second.conveyor_established)
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
   until a real, unsigned-returning accessor was used).  */

static bool
cg_cond_is_bare_param (tree val, tree *out = NULL)
{
  if (TREE_CODE (val) == SSA_NAME && !SSA_NAME_IS_DEFAULT_DEF (val))
    {
      gimple *def = SSA_NAME_DEF_STMT (val);
      if (def && is_gimple_assign (def)
	  && CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (def)))
	val = gimple_assign_rhs1 (def);
    }
  bool ok = TREE_CODE (val) == SSA_NAME
	    && SSA_NAME_IS_DEFAULT_DEF (val)
	    && TREE_CODE (SSA_NAME_VAR (val)) == PARM_DECL;
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
  if (cg_cond_is_bare_param (op1, &param)
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
  if (cg_cond_is_bare_param (op0, &param)
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
  if (fact->rhs_callee != callee || fact->rhs_receiver != receiver)
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
  bool lhs_is_param = cg_cond_is_bare_param (lhs, &lhs_param);
  bool rhs_is_param = cg_cond_is_bare_param (rhs, &rhs_param);
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
    return;

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
      if (!cg_shifted_comparison_no_wrap_ok_p (state.call_rel, shifted_param,
						 receiver, callee,
						 shifted_arithmetic_type,
						 /*param_is_minuend=*/!shifted_negate))
	return;
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

      cg_refine_edge_into (e, refined, established_range, ranger,
			    require_conveyor, require_symbolic);
      if (first)
	{
	  cg_dom_fact_state_assign (in_state, refined);
	  first = false;
	}
      else
	cg_dom_fact_state_merge (in_state, refined);
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
      bool check_as_conveyor = conveyor_active && flag_contract_conveyor_proofs_gimple;
      bool check_as_symbolic = symbolic_active && flag_contract_symbolic_proofs_gimple;
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

	  tree sub_param = cg_resolve_call_argument (call, param_argno);
	  tree sub_receiver = cg_resolve_call_argument (call, receiver_argno);

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
	      error_at (gimple_location (call),
			"argument %qE provably violates the precondition "
			"of %qD", sub_param, callee);
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
      bool check_as_conveyor = conveyor_active && flag_contract_conveyor_proofs_gimple;
      bool check_as_symbolic = symbolic_active && flag_contract_symbolic_proofs_gimple;
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

	  tree sub_param = cg_resolve_call_argument (call, param_argno);
	  tree sub_receiver = cg_resolve_call_argument (call, receiver_argno);

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
	  if (!have_fact || fact_rhs_callee != rhs_callee
	      || fact_rhs_receiver != sub_receiver
	      || (require_conveyor && !fact_conveyor_established))
	    continue;

	  if (oa_relational_code_implies (fact_code, rel_code)
	      && cg_offset_compatible_with_code (fact_offset, rel_code))
	    {
	      verdict_out->add (call);
	      continue; /* Proven true: silently discharged.  */
	    }

	  if (cg_call_relational_contradicts_p (fact_code, fact_offset, rel_code))
	    {
	      verdict_out->add (call);
	      error_at (gimple_location (call),
			"argument %qE provably violates the precondition "
			"of %qD", sub_param, callee);
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

	  tree sub_param = cg_resolve_call_argument (call, param_argno);
	  tree sub_other = cg_resolve_call_argument (call, other_argno);

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

static bool
cg_predicate_facts_walk (function *fun, hash_map<tree, cg_range_lite> *scalar_range_cache_out,
			   hash_map<tree, cg_range_lite> &established_range,
			   hash_set<gimple *> *call_relational_verdict_out,
			   gimple_ranger *ranger)
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
		cg_process_field_write (gimple_assign_lhs (stmt),
					 gimple_assign_rhs1 (stmt), fun,
					 field_object_cache, out_state);
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
		    cg_process_field_write (gimple_call_lhs (call), NULL_TREE,
					     fun, field_object_cache, out_state);
		  cg_invalidate_persistent_facts_for_call_args
		    (call, fun, field_object_cache, out_state);
		  cg_establish_persistent_facts_for_call (call, field_object_cache,
							   out_state);
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
	    cg_process_field_write (gimple_assign_lhs (stmt),
				     gimple_assign_rhs1 (stmt), fun,
				     field_object_cache, state);
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
		cg_process_field_write (gimple_call_lhs (call), NULL_TREE, fun,
					 field_object_cache, state);
	      cg_invalidate_persistent_facts_for_call_args (call, fun,
							     field_object_cache, state);
	      cg_establish_persistent_facts_for_call (call, field_object_cache, state);
	    }
	}
    }

  for (auto it : block_out)
    delete it.second;
  XDELETEVEC (rpo);
  return true;
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

  cg_predicate_facts_walk (fun, &scalar_range_cache,
			     established_range, &call_relational_verdict,
			     ranger);

  bool check_reference_safety
    = cg_function_might_need_reference_safety_walk_p (fun);
  bool check_item8_ub = DECL_DECLARED_CONVEYOR_P (fun->decl);

  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (check_item8_ub)
	  {
	    cg_check_div_mod_ub (stmt, established_nz, established_range,
				  scalar_range_cache, ranger);
	    cg_check_overflow_ub (stmt, established_type_bound,
				   established_range, scalar_range_cache, ranger);
	    cg_check_dereference_ub (stmt, established);
	  }
	if (is_gimple_call (stmt))
	  {
	    gcall *call = as_a <gcall *> (stmt);
	    if (check_reference_safety)
	      cg_check_call_reference_safety (call, fun, established);
	    cg_check_call (call, established, established_nz,
			   established_range, established_rel,
			   established_call_rel, established_call_call_rel,
			   scalar_range_cache, ranger,
			   &call_relational_verdict);
	  }
      }

  disable_ranger (fun);

  return 0;
}

} // anon namespace

gimple_opt_pass *
make_pass_contracts_gimple (gcc::context *ctxt)
{
  return new pass_contracts_gimple (ctxt);
}
