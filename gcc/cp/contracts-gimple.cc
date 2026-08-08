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
#include "domwalk.h"
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

struct cg_rel_fact { tree_code code; tree rhs; bool conveyor_established; };

/* A numeric-interval fact -- see this file's own top comment for why,
   unlike cg_fact, this carries no conveyor_established provenance tag
   (mirroring contracts.cc's own m_contract_scalar_range_map).  */

struct cg_range_lite
{
  bool has_lo = false, has_hi = false;
  widest_int lo = 0, hi = 0;
};

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
      return false;
    }

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  if (cg_range_lite *found = established_range.get (val))
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
	  && cg_established_range_of (receiver, established_range, ranger,
				       require_conveyor, require_symbolic, out))
	return true;
    }
  else if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if ((CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	  && cg_established_range_of (gimple_assign_rhs1 (def), established_range,
				       ranger, require_conveyor, require_symbolic,
				       out))
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
		     hash_map<tree, cg_rel_fact> &established_rel)
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
	    established_rel.put (key_param, { rel_code, key_other, conveyor_enabled });
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
		    tree_code *code_out, tree *rhs_out, bool *conveyor_out)
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
      *conveyor_out = fact->conveyor_established;
      return true;
    }

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  if (cg_rel_fact *fact = established_rel.get (val))
    {
      *code_out = fact->code;
      *rhs_out = fact->rhs;
      *conveyor_out = fact->conveyor_established;
      return true;
    }

  gimple *def = SSA_NAME_DEF_STMT (val);
  if (def && is_gimple_call (def))
    {
      if (cg_call_postcondition_relation_p (as_a <gcall *> (def), code_out,
					     rhs_out, conveyor_out))
	return true;
      /* A class-typed operand reached via its own implicit conversion
	 operator -- recurse into the receiver.  */
      tree receiver = cg_resolve_conversion_receiver (val);
      if (receiver != val)
	return cg_get_relational (receiver, established_rel, code_out,
				   rhs_out, conveyor_out);
      return false;
    }
  if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	return cg_get_relational (gimple_assign_rhs1 (def), established_rel,
				   code_out, rhs_out, conveyor_out);
    }

  return false;
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
	  bool fact_conveyor_established;
	  if (cg_get_relational (sub_param, established_rel, &fact_code,
				  &fact_rhs, &fact_conveyor_established)
	      && oa_relational_code_implies (fact_code, rel_code)
	      && (!require_conveyor || fact_conveyor_established)
	      && fact_rhs == sub_other)
	    continue; /* Proven true: silently discharged.  */

	  warning_at (gimple_location (call), 0,
		      "cannot verify that %qE satisfies the "
		      "precondition of %qD", sub_param, callee);
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
					ranger, check_as_conveyor,
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
typedef pair_hash<nofree_ptr_hash<tree_node>, nofree_ptr_hash<tree_node>>
  cg_field_key_hash;

struct cg_dom_fact_state
{
  hash_map<tree, cg_pred_fact> pred;
  hash_map<cg_field_key_hash, cg_field_fact> field;
};

/* An SSA_NAME's own identity is itself; '&decl' resolves to DECL
   directly -- see the plugin's own identical gimple_object_identity
   for the full rationale (unifying plain-object and pointer
   receivers).  */

static tree
cg_gimple_object_identity (tree val)
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
    }
}

/* CALL's own callee's declared precondition, checked against STATE as
   it stands right before CALL -- the consult side, for both facts,
   the persistent-fact analogue of cg_check_call.  */

static void
cg_consult_persistent_facts (gcall *call, cg_dom_fact_state &state)
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
	  tree substituted = gimple_call_arg (call, argno);
	  tree identity = cg_gimple_object_identity (substituted);

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
	  tree substituted = gimple_call_arg (call, argno);
	  tree identity = cg_gimple_object_identity (substituted);
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
    }
}

/* A tracked object's fact must be invalidated by *any* call taking its
   address or receiving it as a bare pointer, whether or not that call
   has any contracts of its own at all -- unconditional, matching
   contracts.cc's own oa_invalidate_symbolic_facts_for_call_args, and
   the plugin's own identical invalidate_persistent_facts_for_call_args.
   Drops every tracked field for the same identity too (whole-object
   granularity).  */

static void
cg_invalidate_persistent_facts_for_call_args (gcall *call,
					       cg_dom_fact_state &state)
{
  unsigned n = gimple_call_num_args (call);
  for (unsigned i = 0; i < n; ++i)
    {
      tree identity = cg_gimple_object_identity (gimple_call_arg (call, i));
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
   persistent-fact analogue of item 6, gated on flavor-enabled exactly
   like cg_seed_predicate_self_trust.  Order relative to invalidation
   matters, matching oa_scan_calls_in_expr's own "invalidate then
   establish" discipline: this call's own postcondition must win over
   its own (necessarily stale-by-then) invalidation of the same
   identity.  */

static void
cg_establish_persistent_facts_for_call (gcall *call, cg_dom_fact_state &state)
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
	  tree identity = cg_gimple_object_identity (substituted);
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
	  tree identity = cg_gimple_object_identity (substituted);
	  if (identity)
	    state.field.put ({identity, field_groups[g].field},
			      { field_groups[g].range, conveyor_enabled });
	}
    }
}

/* The dominator-preorder walk itself -- see the plugin's own identical
   predicate_dom_walker for the full rationale (a fact is available at
   block B iff established somewhere that dominates B with no
   invalidation on the path, exactly what processing each block from
   its immediate dominator's already-computed exit state gives for
   free).  */

class cg_predicate_dom_walker : public dom_walker
{
public:
  cg_predicate_dom_walker (cg_dom_fact_state *seed_)
    : dom_walker (CDI_DOMINATORS), seed (seed_)
  {}

  ~cg_predicate_dom_walker ()
  {
    for (auto it : block_out)
      delete it.second;
  }

  edge before_dom_children (basic_block) final override;

  cg_dom_fact_state *seed;
  hash_map<basic_block, cg_dom_fact_state *> block_out;
};

edge
cg_predicate_dom_walker::before_dom_children (basic_block bb)
{
  cg_dom_fact_state *state = new cg_dom_fact_state ();

  basic_block idom = get_immediate_dominator (CDI_DOMINATORS, bb);
  const cg_dom_fact_state *from = seed;
  if (idom)
    {
      cg_dom_fact_state **parent = block_out.get (idom);
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
      cg_consult_persistent_facts (call, *state);
      cg_invalidate_persistent_facts_for_call_args (call, *state);
      cg_establish_persistent_facts_for_call (call, *state);
    }

  block_out.put (bb, state);
  return NULL;
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
  hash_map<tree, cg_fact> established;
  hash_map<tree, cg_fact> established_nz;
  hash_map<tree, cg_range_lite> established_range;
  hash_map<tree, cg_rel_fact> established_rel;
  cg_seed_self_trust (fun, established, established_nz, established_range,
		       established_rel);

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
			 established_range, established_rel, ranger);
      }

  disable_ranger (fun);

  /* Named-predicate and field-range facts get their own, separate
     dominator-tree-based walk (see cg_predicate_dom_walker's own
     comment) rather than folding into the FOR_EACH_BB_FN loop above:
     that loop's own three fact shapes are consulted using a single,
     function-wide ESTABLISHED set/map (correct for them, since a
     backward SSA walk needs no block-order-sensitive state at all),
     whereas these two are inherently per-program-point and need the
     dominator walk's own per-block state threading.  */
  cg_dom_fact_state pred_seed;
  cg_seed_predicate_self_trust (fun, pred_seed);
  cg_predicate_dom_walker walker (&pred_seed);
  walker.walk (ENTRY_BLOCK_PTR_FOR_FN (fun));

  return 0;
}

} // anon namespace

gimple_opt_pass *
make_pass_contracts_gimple (gcc::context *ctxt)
{
  return new pass_contracts_gimple (ctxt);
}
