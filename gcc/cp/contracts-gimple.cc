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
   slightly coarser, stand-in for the full value set.  */

static bool
cg_established_range_of (tree val, hash_map<tree, cg_range_lite> &established_range,
			  hash_map<tree, cg_range_lite> &scalar_range_cache,
			  gimple_ranger *ranger, bool require_conveyor,
			  bool require_symbolic, cg_range_lite *out)
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
				       require_conveyor, require_symbolic, out))
	return true;
    }
  else if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if ((CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	  && cg_established_range_of (gimple_assign_rhs1 (def), established_range,
				       scalar_range_cache, ranger, require_conveyor,
				       require_symbolic, out))
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
		       &established_call_call_rel)
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
						  &rhs_callee))
	    continue;
	  tree key_param = cg_self_trust_key (fun, rel_param);
	  tree key_receiver = cg_self_trust_key (fun, rhs_receiver);
	  if (key_param && key_receiver)
	    established_call_rel.put (key_param, { rel_code, key_receiver,
						    rhs_callee,
						    cg_range_lite_exact (0),
						    conveyor_enabled });
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
					     &rhs_receiver, &rhs_callee))
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
	 TREE_OPERAND on a PLUS_EXPR node.  */
      if (code == PLUS_EXPR || code == MINUS_EXPR)
	{
	  tree op0 = gimple_assign_rhs1 (def);
	  tree op1 = gimple_assign_rhs2 (def);

	  cg_range_lite shift;
	  if (cg_get_relational (op0, established_rel, scalar_rel_cache,
				  established_range, scalar_range_cache,
				  ranger, require_conveyor, require_symbolic,
				  code_out, rhs_out, offset_out, conveyor_out))
	    {
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
	 just above.  */
      if (code == PLUS_EXPR || code == MINUS_EXPR)
	{
	  tree op0 = gimple_assign_rhs1 (def);
	  tree op1 = gimple_assign_rhs2 (def);

	  cg_range_lite shift;
	  if (cg_get_call_relational (op0, established_call_rel,
				       scalar_call_rel_cache, established_range,
				       scalar_range_cache, ranger,
				       require_conveyor, require_symbolic,
				       code_out, rhs_receiver_out, rhs_callee_out,
				       offset_out, conveyor_out))
	    {
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
		hash_map<tree, cg_rel_fact> &scalar_rel_cache,
		hash_map<tree, cg_call_rel_fact> &scalar_call_rel_cache,
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
	      warning_at (gimple_location (call), 0,
			  "cannot verify %<is_object_address%> for %qE, as "
			  "required by the precondition of %qD",
			  substituted, callee);
	    }
	  else
	    {
	      if (cg_provable_nonzero_p (substituted, established_nz,
					 require_conveyor, in_progress))
		continue; /* Proven true: silently discharged.  */
	      warning_at (gimple_location (call), 0,
			  "cannot verify that %qE is nonzero, as required by "
			  "the precondition of %qD", substituted, callee);
	    }
	}

      /* Relational obligations against another of the callee's own
	 parameters (e.g. 'pre<ctrl>(x < q)') -- both PARM_DECLs
	 substituted positionally, mirroring contracts.cc's own
	 oa_handle_call_conveyor_proof_obligation/oa_handle_call_
	 symbolic_precondition_obligation.  */
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
	      warning_at (gimple_location (call), 0,
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
	  tree_code fact_code;
	  tree fact_rhs;
	  cg_range_lite fact_offset;
	  bool fact_conveyor_established;
	  if (cg_get_relational (sub_param, established_rel, scalar_rel_cache,
				  established_range, scalar_range_cache, ranger,
				  check_as_conveyor, check_as_symbolic,
				  &fact_code, &fact_rhs, &fact_offset,
				  &fact_conveyor_established)
	      && oa_relational_code_implies (fact_code, rel_code)
	      && cg_offset_compatible_with_code (fact_offset, rel_code)
	      && (!require_conveyor || fact_conveyor_established)
	      && fact_rhs == sub_other)
	    continue; /* Proven true: silently discharged.  */

	  warning_at (gimple_location (call), 0,
		      "cannot verify that %qE satisfies the "
		      "precondition of %qD", sub_param, callee);
	}

      /* The call analogue of the relational loop just above (e.g.
	 'pre<ctrl>(i < v.size ())').  RHS_CALLEE is compared directly by
	 identity (a FUNCTION_DECL, not a value to substitute); RHS_
	 RECEIVER, like REL_PARAM, is one of CALLEE's own PARM_DECLs,
	 substituted positionally the same way.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rhs_receiver, rhs_callee;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_call (*conjuncts[i], &rel_param,
						  &rel_code, &rhs_receiver,
						  &rhs_callee))
	    continue;

	  unsigned param_argno, receiver_argno;
	  if (!cg_find_param_position (callee, rel_param, &param_argno)
	      || !cg_find_param_position (callee, rhs_receiver, &receiver_argno)
	      || param_argno >= gimple_call_num_args (call)
	      || receiver_argno >= gimple_call_num_args (call))
	    continue;

	  tree sub_param = cg_resolve_call_argument (call, param_argno);
	  tree sub_receiver = cg_resolve_call_argument (call, receiver_argno);

	  tree_code fact_code;
	  tree fact_rhs_receiver, fact_rhs_callee;
	  cg_range_lite fact_offset;
	  bool fact_conveyor_established;
	  if (cg_get_call_relational (sub_param, established_call_rel,
				       scalar_call_rel_cache, established_range,
				       scalar_range_cache, ranger,
				       check_as_conveyor, check_as_symbolic,
				       &fact_code, &fact_rhs_receiver,
				       &fact_rhs_callee, &fact_offset,
				       &fact_conveyor_established)
	      && oa_relational_code_implies (fact_code, rel_code)
	      && cg_offset_compatible_with_code (fact_offset, rel_code)
	      && (!require_conveyor || fact_conveyor_established)
	      && fact_rhs_callee == rhs_callee
	      && fact_rhs_receiver == sub_receiver)
	    continue; /* Proven true: silently discharged.  */

	  warning_at (gimple_location (call), 0,
		      "cannot verify that %qE satisfies the "
		      "precondition of %qD", sub_param, callee);
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
					     &rhs_receiver, &rhs_callee))
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
	  if (cg_established_range_of (substituted, established_range,
					scalar_range_cache, ranger,
					check_as_conveyor,
					check_as_symbolic, &established_r)
	      && (!required.has_lo
		  || (established_r.has_lo && established_r.lo >= required.lo))
	      && (!required.has_hi
		  || (established_r.has_hi && established_r.hi <= required.hi)))
	    continue; /* Proven true: silently discharged.  */

	  warning_at (gimple_location (call), 0,
		      "cannot verify that %qE satisfies the precondition "
		      "of %qD", substituted, callee);
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
cg_collect_call_range_groups (tree cond, vec<cg_call_group_lite> *out)
{
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts_public (&cond, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree receiver_expr, callee, const_val;
      tree_code code;
      if (!oa_match_call_range_comparison (*conjuncts[i], &receiver_expr,
					    &callee, &code, &const_val)
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
      cg_collect_call_range_groups (cond, &call_groups);
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
	  if (fact && fact->pred_fn == pred_fn && fact->polarity == required
	      && (!require_conveyor || fact->conveyor_established))
	    continue; /* Proven true: silently discharged.  */

	  warning_at (gimple_location (call), 0,
		      "cannot verify that %qD (%qE) holds, as required by "
		      "the precondition of %qD", pred_fn, substituted, callee);
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

	  warning_at (gimple_location (call), 0,
		      "cannot verify that field %qD of %qE satisfies the "
		      "precondition of %qD",
		      field_groups[g].field, substituted, callee);
	}

      auto_vec<cg_call_group_lite> call_groups;
      cg_collect_call_range_groups (cond, &call_groups);
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

	  warning_at (gimple_location (call), 0,
		      "cannot verify that %qD called on %qE satisfies the "
		      "precondition of %qD",
		      call_groups[g].callee, substituted, callee);
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
      cg_collect_call_range_groups (cond, &call_groups);
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
   conservative" discipline rather than chasing full generality.  */

static bool
cg_cond_is_bare_param (tree val)
{
  return TREE_CODE (val) == SSA_NAME
	 && SSA_NAME_IS_DEFAULT_DEF (val)
	 && TREE_CODE (SSA_NAME_VAR (val)) == PARM_DECL;
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
  if (cg_cond_is_bare_param (lhs) && cg_cond_is_bare_param (rhs))
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

  bool lhs_is_param = cg_cond_is_bare_param (lhs);
  bool rhs_is_param = cg_cond_is_bare_param (rhs);
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

static void
cg_refine_edge_into (edge e, cg_dom_fact_state &state)
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
  if ((code == NE_EXPR || code == EQ_EXPR)
      && TREE_CODE (rhs) == INTEGER_CST && integer_zerop (rhs)
      && TREE_CODE (lhs) == SSA_NAME)
    {
      gimple *def = SSA_NAME_DEF_STMT (lhs);
      if (!def || !is_gimple_assign (def))
	return;
      tree_code def_code = gimple_assign_rhs_code (def);
      if (def_code != LT_EXPR && def_code != LE_EXPR && def_code != GT_EXPR
	  && def_code != GE_EXPR && def_code != EQ_EXPR)
	return;
      if (code == EQ_EXPR)
	asserted_true = !asserted_true;
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
  if (!cg_cond_operand_shape (other, &is_call, &field, &base, &callee, &receiver))
    return;

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
		      cg_dom_fact_state &seed, cg_dom_fact_state *in_state)
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
      cg_dom_fact_state_assign (&refined, **pred_out);
      cg_refine_edge_into (e, refined);
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
					       &rhs_receiver, &rhs_callee)
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

static bool
cg_predicate_facts_walk (function *fun, hash_map<tree, cg_range_lite> *scalar_range_cache_out,
			   hash_map<tree, cg_rel_fact> *scalar_rel_cache_out,
			   hash_map<tree, cg_call_rel_fact> *scalar_call_rel_cache_out)
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
	  cg_compute_in_state (bb, block_out, seed, &in_state);

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

  /* D4324 Commit 3: flatten every block's own, now-fully-converged
     .rel/.call_rel entries into SCALAR_REL_CACHE_OUT/SCALAR_CALL_REL_
     CACHE_OUT -- the relational analogue of SCALAR_RANGE_CACHE (see
     cg_compose_call_result_range's own comment on that one), letting
     the *other*, simple linear pass's own cg_get_relational/cg_get_
     call_relational (used by cg_check_call, which has no dominator-
     tree awareness of its own) see a branch-derived fact, not just a
     self-trust-established one. Sound to flatten into one, function-
     wide, non-per-block map for exactly the same reason SCALAR_RANGE_
     CACHE already is: both maps here are keyed on a plain SSA name, and
     an SSA name's own value is permanent once defined, so a fact
     established for it by whichever branch dominates its later uses
     remains valid at every one of them -- unlike .field/.call
     (object-identity-keyed, where a later mutation can invalidate an
     earlier fact), which is exactly why call-vs-call has no flattened
     cache of its own here (see cg_dom_fact_state's own comment). Each
     SSA name is defined in exactly one place, so iterating every
     block's own final OUT-state and unioning their .rel/.call_rel
     entries can never see two genuinely conflicting facts for the same
     key.  */
  for (auto it : block_out)
    {
      for (auto rel_it : it.second->rel)
	scalar_rel_cache_out->put (rel_it.first, rel_it.second);
      for (auto call_rel_it : it.second->call_rel)
	scalar_call_rel_cache_out->put (call_rel_it.first, call_rel_it.second);
    }

  /* Final pass, over the now-stable BLOCK_OUT: re-derive each block's
     own IN state one more time and run the full per-statement dispatch,
     including consult this time, so every "cannot verify" diagnostic
     is reported exactly once, against final, fully-converged facts.  */
  for (int i = 0; i < rpo_num; ++i)
    {
      basic_block bb = BASIC_BLOCK_FOR_FN (fun, rpo[i]);
      cg_dom_fact_state state;
      cg_compute_in_state (bb, block_out, seed, &state);
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
  /* D4324 Commit 3: SCALAR_REL_CACHE/SCALAR_CALL_REL_CACHE -- the
     relational analogue of SCALAR_RANGE_CACHE just above, letting a
     branch-derived param-vs-param/param-vs-call fact (cg_predicate_
     facts_walk's own cg_refine_edge_into, dominator-tracked) be seen by
     this function's own simple, self-trust-only established_rel/
     established_call_rel below -- see cg_predicate_facts_walk's own
     comment on why flattening is sound for these two shapes.  */
  hash_map<tree, cg_rel_fact> scalar_rel_cache;
  hash_map<tree, cg_call_rel_fact> scalar_call_rel_cache;
  cg_predicate_facts_walk (fun, &scalar_range_cache, &scalar_rel_cache,
			     &scalar_call_rel_cache);

  hash_map<tree, cg_fact> established;
  hash_map<tree, cg_fact> established_nz;
  hash_map<tree, cg_range_lite> established_range;
  hash_map<tree, cg_rel_fact> established_rel;
  hash_map<tree, cg_call_rel_fact> established_call_rel;
  hash_map<cg_field_key_hash, cg_call_call_rel_fact> established_call_call_rel;
  cg_seed_self_trust (fun, established, established_nz, established_range,
		       established_rel, established_call_rel,
		       established_call_call_rel);

  calculate_dominance_info (CDI_DOMINATORS);
  gimple_ranger *ranger = enable_ranger (fun, false);

  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (is_gimple_call (stmt))
	  cg_check_call (as_a <gcall *> (stmt), established, established_nz,
			 established_range, established_rel,
			 established_call_rel, established_call_call_rel,
			 scalar_range_cache, scalar_rel_cache,
			 scalar_call_rel_cache, ranger);
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
