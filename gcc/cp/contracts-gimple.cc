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

   Scope so far: is_object_address, nonzero-ness, and general numeric
   ranges (self-trust, call-site consult, and item 6's postcondition-
   return-value guarantee for all three) -- the two persistent-object
   fact shapes (named predicates, ptr->field ranges) are not yet
   ported from the validated plugin prototype. IILE recursion remains
   permanently out of scope, per the same instruction that applied to
   the plugin prototype.

   One-way trust applies to is_object_address/nonzero (see cg_fact
   below) but deliberately NOT to general ranges: mirroring
   contracts.cc's own m_contract_scalar_range_map (consulted
   identically by both -fcontract-conveyor-proofs and -fcontract-
   symbolic-proofs, with no conveyor_established tag at all, unlike
   oa_predicate_fact/oa_contract_field_range_fact), a range fact here
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
#include "is-a.h"
#include "tree-dfa.h"
#include "tree-pass.h"
#include "context.h"
#include "diagnostic.h"
#include "stringpool.h"
#include "gimple-range.h"

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
    }

  in_progress.remove (val);
  return result;
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
		     hash_map<tree, cg_range_lite> &established_range)
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
	  tree ssa = ssa_default_def (fun, arg);
	  if (!ssa)
	    continue;

	  /* CONVEYOR_ENABLED/SYMBOLIC_ENABLED aren't mutually exclusive,
	     so a contract active (and enabled) for both flavors at once
	     still only ever produces ONE fact, correctly tagged
	     conveyor_established for the one-way trust rule -- there is
	     no meaningful "established twice, differently" case here.  */
	  target->put (ssa, { conveyor_enabled });
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
	  tree ssa = ssa_default_def (fun, params[p]);
	  if (ssa)
	    established_range.put (ssa, range);
	}
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

	  tree substituted = gimple_call_arg (call, argno);
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
	  tree substituted = gimple_call_arg (call, argno);

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
  cg_seed_self_trust (fun, established, established_nz, established_range);

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
			 established_range, ranger);
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
