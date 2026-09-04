/* P3446R0 Invalidation profile, refined by P4296R0's "Default-Deny +
   Provable-Whitelist" strategy -- GIMPLE-level checker for Phase 7b's
   Positive Rules #0 and #1.

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

/* Phase 7a (decl2.cc/init.cc/call.cc/decl.cc/typeck.cc) already
   implements P4296R0's own "Negative Baseline" for the constructs it
   scoped there (delete of a non-owning pointer, placement new,
   explicit destructor calls, user-defined allocators, reinterpret_cast
   to pointer, and an unconditional ban on every pointer dereference).
   This file adds the OTHER half of the Negative Baseline P4296R0
   describes -- "ANY pointer is invalidated by mutation of ANY
   container" -- together with the first two "Positive Rules" (S7.6)
   that narrow it back down, since shipping the Negative Baseline half
   here without any Positive Rule at all would make every container
   iterator use flag, which is not a useful increment on its own.

   Rule #0 ("Unrelated Types don't Interact", S7.6.1): mutating an
   object of one class can never invalidate a value belonging to a
   class from a genuinely different, unrelated family.  Implemented in
   ip_types_provably_unrelated_p below via each type's underlying class
   TEMPLATE (not the literal field-layout recursion the paper's own
   S7.6.1 describes) -- a deliberately narrower, but easier to verify
   and equally sound, stand-in: two different class templates (or two
   unrelated, non-template classes) can be proven not to alias; this
   increment does NOT attempt to additionally distinguish two
   different instantiations of the SAME template (e.g. two distinct
   vector<int>s) the way the paper's own field-recursion or origin/cset
   machinery could -- a known, documented scope limit, not a
   soundness gap (the answer for that case is simply "not provably
   unrelated", the same conservative default as before this file
   existed).

   Rule #1 ("Patently Independent Containers don't Interact", S7.6.2):
   needs a "proven binding" step first -- establishing that a given
   iterator/handle value is, at a given use, reached from one single
   defining statement whose own effect associates it with one
   container declaration -- before Rule #0 even has two concrete types
   to compare.

   IMPORTANT, discovered while implementing this (not assumed): a
   class-typed local like an iterator is NEVER represented as an
   SSA_NAME in this compiler -- is_gimple_reg (gimple-expr.cc) is
   explicitly "true if T is a NON-AGGREGATE register variable", so an
   iterator stays a plain memory-resident VAR_DECL through this early
   point in the pipeline (confirmed via a direct -fdump-tree-ssa-
   details reading, not guessed) even when never address-taken and
   never split by SRA.  An earlier draft of this file assumed the
   contracts-gimple.cc-style SSA/PHI proof shape (AND-across-all-
   incoming-arms for a GIMPLE_PHI def) could be reused as-is the way
   init-profile-gimple.cc's own scalar DAA proof kernel does; that
   draft found precisely zero RECORD_TYPE SSA names in any test
   function and was abandoned before being relied on. What follows
   instead is ip_nearest_write_before/ip_binding_established_by: a
   dominance-based "nearest reaching write" query directly over the
   VAR_DECL (same-block backward scan, then a walk up the immediate-
   dominator chain), mirroring the CFG-dominance technique init-
   profile-gimple.cc's own address-taken-variable proof kernel
   (ip_read_dominated_by_init_p) already uses for exactly this reason
   -- a variable that is not is_gimple_reg needs CFG/dominance
   reasoning, not SSA/PHI reasoning.  Known, deliberate scope limit:
   this walks the *immediate-dominator chain* for the nearest ancestor
   block containing a write, which is not full reaching-definitions
   dataflow -- a diamond-shaped reassignment (two different branches
   each writing the tracked variable with a different binding, merging
   before the use) is not soundly resolved by this technique and would
   be treated as "no reaching write visible via a single dominator-
   chain walk" rather than correctly recognized as "provably
   conflicting" or "provably identical" -- P4296R0's own explicitly-
   stated preference for local, not fully general, flow analysis is
   the standing justification for not building a complete reaching-
   definitions solver here.

   "Container-returning member call" is itself resolved structurally,
   not via any hardcoded standard-library name: ip_shares_template_
   argument_p checks whether the call's return type and its receiver
   type are both specializations of some class template sharing at
   least one common type template argument -- modeled directly on how
   every standard container actually declares its iterator types (e.g.
   libstdc++'s bits/stl_list.h: "typedef _List_iterator<_Tp> iterator;"
   -- _List_iterator<_Tp> shares _Tp with list<_Tp> itself, confirmed
   by reading that header, not assumed; an earlier design draft tried
   testing the return type's TYPE_CONTEXT for being a literal nested
   class of the receiver, which does NOT hold for libstdc++'s actual
   iterator classes and was abandoned before being relied on).

   "Mutating call" is resolved structurally too, gated on P3446R0's own
   annotation for this exact question: [[not_invalidating]] (tree.cc's
   handle_not_invalidating_attribute) marks a non-const member function
   as NOT invalidating -- the default, absent that annotation, is
   "assumed invalidating" for any non-const member call whatsoever
   (P3446R0 S4's own stated default), which is why an accessor like
   begin()/end() needs the annotation to avoid being treated as
   mutating merely because its non-const overload exists to return a
   mutable iterator.  Deliberately NOT implemented here: classifying a
   plain (non-member) function call that takes a container by
   non-const reference as mutating too (P4296R0's own broader statement
   of this rule) -- this increment only reaches far enough to make its
   own worked example (see the testsuite's own d4324-profiles-
   invalidation-rule0-rule1-*.C) provably safe or provably flagged, not
   the full breadth of P4296R0's Negative Baseline; a virtual
   (indirectly-dispatched) mutating call is likewise not classified as
   mutating for the same reason (gimple_call_fndecl returns NULL_TREE
   for those) -- both are known, documented scope limits for this
   increment, not silent gaps.

   Diagnostic ordering ("did this mutation happen before this read")
   uses plain CFG dominance plus a same-block statement scan, the same
   ip_read_dominated_by_init_p technique init-profile-gimple.cc already
   uses for its own DAA proof.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "function.h"
#include "basic-block.h"
#include "cp-tree.h"
#include "profiles.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-expr.h"
#include "internal-fn.h"
#include "is-a.h"
#include "ssa.h"
#include "tree-dfa.h"
#include "tree-pass.h"
#include "context.h"
#include "diagnostic.h"
#include "attribs.h"
#include "cfg.h"
#include "dominance.h"

/* Rule #0.  Returns the TEMPLATE_DECL TYPE is a specialization of, or
   TYPE itself if it is not a class-template specialization -- the
   structural stand-in this checker uses for "which family of
   container/handle does this type belong to".  */

static tree
ip_class_template_decl (tree type)
{
  type = TYPE_MAIN_VARIANT (type);
  if (!CLASS_TYPE_P (type))
    return type;
  tree info = CLASSTYPE_TEMPLATE_INFO (type);
  return info ? TI_TEMPLATE (info) : type;
}

/* Rule #0 (P4296R0 S7.6.1): true if this checker can find no
   relationship between TYPE_A and TYPE_B that could make mutating an
   object of one possibly affect an object of the other.  Same type,
   inheritance, or the same underlying class template are all treated
   as "related" (declined to the conservative "not provably
   unrelated" answer, per this file's own top comment); two class
   types from genuinely different templates, with neither derived
   from the other, are the one case proven safe here.  */

static bool
ip_types_provably_unrelated_p (tree type_a, tree type_b)
{
  if (TREE_CODE (type_a) == REFERENCE_TYPE)
    type_a = TREE_TYPE (type_a);
  if (TREE_CODE (type_b) == REFERENCE_TYPE)
    type_b = TREE_TYPE (type_b);
  type_a = TYPE_MAIN_VARIANT (type_a);
  type_b = TYPE_MAIN_VARIANT (type_b);
  if (type_a == type_b)
    return false;
  if (!CLASS_TYPE_P (type_a) || !CLASS_TYPE_P (type_b))
    return false;
  if (DERIVED_FROM_P (type_a, type_b) || DERIVED_FROM_P (type_b, type_a))
    return false;
  return ip_class_template_decl (type_a) != ip_class_template_decl (type_b);
}

/* Rule #1 support.  True if RETURN_TYPE and RECEIVER_TYPE are both
   class-template specializations sharing at least one common type
   template argument -- see this file's own top comment for why this,
   not a nested-class/TYPE_CONTEXT check, is the structural stand-in
   for "this method's return value is an iterator/handle associated
   with its receiver".  */

static bool
ip_shares_template_argument_p (tree return_type, tree receiver_type)
{
  return_type = TYPE_MAIN_VARIANT (return_type);
  receiver_type = TYPE_MAIN_VARIANT (receiver_type);
  if (!CLASS_TYPE_P (return_type) || !CLASS_TYPE_P (receiver_type))
    return false;
  tree info_a = CLASSTYPE_TEMPLATE_INFO (return_type);
  tree info_b = CLASSTYPE_TEMPLATE_INFO (receiver_type);
  if (!info_a || !info_b)
    return false;

  tree args_a = INNERMOST_TEMPLATE_ARGS (TI_ARGS (info_a));
  tree args_b = INNERMOST_TEMPLATE_ARGS (TI_ARGS (info_b));
  for (int i = 0; i < TREE_VEC_LENGTH (args_a); ++i)
    {
      tree ai = TREE_VEC_ELT (args_a, i);
      if (!TYPE_P (ai))
	continue;
      ai = TYPE_MAIN_VARIANT (ai);
      for (int j = 0; j < TREE_VEC_LENGTH (args_b); ++j)
	{
	  tree bj = TREE_VEC_ELT (args_b, j);
	  if (TYPE_P (bj) && ai == TYPE_MAIN_VARIANT (bj))
	    return true;
	}
    }
  return false;
}

/* Resolve RECEIVER -- a call's "this" argument at the GIMPLE level --
   back to the single DECL it addresses: a direct address of a
   VAR_DECL/PARM_DECL, or (for a reference parameter, already a
   pointer at this level) the parameter itself, read directly (no
   SSA_NAME wrapper, per this file's own top comment) or via its
   default-def SSA name.  Anything else (a computed address, a heap
   pointer, a field access) resolves to NULL_TREE -- this checker only
   tracks bindings to a single, nameable declaration, the safe default
   under this profile's own "default deny" stance.  */

static tree
ip_receiver_decl (tree receiver)
{
  if (TREE_CODE (receiver) == PARM_DECL)
    return receiver;
  if (TREE_CODE (receiver) == SSA_NAME)
    {
      if (!SSA_NAME_IS_DEFAULT_DEF (receiver))
	return NULL_TREE;
      tree var = SSA_NAME_VAR (receiver);
      return var && TREE_CODE (var) == PARM_DECL ? var : NULL_TREE;
    }
  if (TREE_CODE (receiver) == ADDR_EXPR)
    {
      tree base = TREE_OPERAND (receiver, 0);
      return (VAR_P (base) || TREE_CODE (base) == PARM_DECL) ? base : NULL_TREE;
    }
  return NULL_TREE;
}

/* True if STMT is a definition (write) of VAR -- a GIMPLE_CALL or
   GIMPLE_ASSIGN whose own LHS is exactly VAR.  */

static bool
ip_defines_var_p (gimple *stmt, tree var)
{
  tree lhs = NULL_TREE;
  if (gimple_code (stmt) == GIMPLE_CALL)
    lhs = gimple_call_lhs (as_a<gcall *> (stmt));
  else if (is_gimple_assign (stmt))
    lhs = gimple_assign_lhs (stmt);
  return lhs == var;
}

/* The nearest definition of VAR that provably reaches POINT (a
   statement in the same function) -- an ordinary backward scan within
   POINT's own basic block, else the nearest immediate-dominator-chain
   ancestor block containing any definition of VAR (its own last such
   definition).  See this file's own top comment for the known,
   deliberate scope limit this implies (a diamond-shaped reassignment
   is not soundly resolved).  Returns NULL_TREE if no such definition
   is found at all (VAR is default-constructed or otherwise never
   written before POINT on the path this technique can see).  */

static gimple *
ip_nearest_write_before (tree var, gimple *point)
{
  basic_block bb = gimple_bb (point);
  for (gimple_stmt_iterator gsi = gsi_for_stmt (point); !gsi_end_p (gsi);)
    {
      gsi_prev (&gsi);
      if (gsi_end_p (gsi))
	break;
      gimple *s = gsi_stmt (gsi);
      if (ip_defines_var_p (s, var))
	return s;
    }

  for (basic_block d = get_immediate_dominator (CDI_DOMINATORS, bb); d;
       d = get_immediate_dominator (CDI_DOMINATORS, d))
    {
      gimple *last = NULL;
      for (gimple_stmt_iterator gsi = gsi_start_bb (d); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	if (ip_defines_var_p (gsi_stmt (gsi), var))
	  last = gsi_stmt (gsi);
      if (last)
	return last;
    }
  return NULL;
}

/* The container declaration DEF_STMT's own effect binds its LHS to
   (P4296R0 S7.6.2's "proven binding"), or NULL_TREE if this checker
   cannot establish one: a container-returning member call
   (ip_shares_template_argument_p) binds to its receiver
   (ip_receiver_decl); a plain copy from another class-typed
   declaration inherits whatever binding the nearest reaching write to
   THAT declaration, as of DEF_STMT's own position, itself establishes
   -- recursing via ip_nearest_write_before, which is always called on
   a strictly earlier statement than its caller's own DEF_STMT, so
   this recursion is well-founded (no cycle-guard is needed the way
   contracts-gimple.cc's PHI recursion needs one: there is no PHI node
   here to create a cycle through).  */

static tree
ip_binding_established_by (gimple *def_stmt)
{
  if (gimple_code (def_stmt) == GIMPLE_CALL)
    {
      gcall *call = as_a<gcall *> (def_stmt);
      tree fndecl = gimple_call_fndecl (call);
      tree lhs = gimple_call_lhs (call);
      if (!fndecl || !lhs || !DECL_IOBJ_MEMBER_FUNCTION_P (fndecl)
	  || gimple_call_num_args (call) < 1)
	return NULL_TREE;
      tree this_ptr_type = TREE_VALUE (TYPE_ARG_TYPES (TREE_TYPE (fndecl)));
      tree receiver_type = TREE_TYPE (this_ptr_type);
      if (!ip_shares_template_argument_p (TREE_TYPE (lhs), receiver_type))
	return NULL_TREE;
      return ip_receiver_decl (gimple_call_arg (call, 0));
    }
  if (is_gimple_assign (def_stmt) && gimple_assign_single_p (def_stmt))
    {
      tree rhs = gimple_assign_rhs1 (def_stmt);
      if (TREE_CODE (rhs) != VAR_DECL && TREE_CODE (rhs) != PARM_DECL)
	return NULL_TREE;
      gimple *reaching = ip_nearest_write_before (rhs, def_stmt);
      return reaching ? ip_binding_established_by (reaching) : NULL_TREE;
    }
  return NULL_TREE;
}

/* True if CALL is a "mutating operation" this checker treats as
   capable of invalidating other values bound to its receiver: a
   non-const member-function call, not marked [[not_invalidating]],
   whose receiver resolves to a single, nameable DECL.  See this
   file's own top comment for what this deliberately does not yet
   cover.  On success, sets *MUTATED_DECL and *MUTATED_TYPE.  */

static bool
ip_mutating_call_p (gcall *call, tree *mutated_decl, tree *mutated_type)
{
  tree fndecl = gimple_call_fndecl (call);
  if (!fndecl || !DECL_IOBJ_MEMBER_FUNCTION_P (fndecl)
      || DECL_CONST_MEMFUNC_P (fndecl)
      || profiles_not_invalidating_p (fndecl)
      || gimple_call_num_args (call) < 1)
    return false;
  tree decl = ip_receiver_decl (gimple_call_arg (call, 0));
  if (!decl)
    return false;
  tree this_ptr_type = TREE_VALUE (TYPE_ARG_TYPES (TREE_TYPE (fndecl)));
  *mutated_decl = decl;
  *mutated_type = TREE_TYPE (this_ptr_type);
  return true;
}

/* True if MUTATING_STMT is guaranteed to have already executed by the
   time USE_STMT runs, on every path that reaches USE_STMT -- plain
   basic-block dominance across blocks, an explicit forward scan for a
   same-block pair (mirroring ip_read_dominated_by_init_p's own
   technique in init-profile-gimple.cc, with the roles of "the
   established fact" and "the read" reversed).  */

static bool
ip_use_after_mutation_p (gimple *mutating_stmt, gimple *use_stmt)
{
  if (mutating_stmt == use_stmt)
    return false;
  basic_block m_bb = gimple_bb (mutating_stmt);
  basic_block u_bb = gimple_bb (use_stmt);
  if (m_bb == u_bb)
    {
      for (gimple_stmt_iterator gsi = gsi_start_bb (m_bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gimple *s = gsi_stmt (gsi);
	  if (s == mutating_stmt)
	    return true;
	  if (s == use_stmt)
	    return false;
	}
      return false;
    }
  return dominated_by_p (CDI_DOMINATORS, u_bb, m_bb);
}

/* True if VAR (an operand of USE_STMT) is a class/union-typed
   VAR_DECL or PARM_DECL worth checking at all -- excludes the LHS of
   USE_STMT's own definition (that is a write, not a read) and
   anything not RECORD_TYPE/UNION_TYPE (this checker only reasons
   about class-typed iterator/handle-shaped values; a raw pointer is
   already wholly covered by Phase 7a's blanket dereference ban).  */

static bool
ip_trackable_operand_p (tree var)
{
  if (TREE_CODE (var) != VAR_DECL && TREE_CODE (var) != PARM_DECL)
    return false;
  tree type = TREE_TYPE (var);
  return TREE_CODE (type) == RECORD_TYPE || TREE_CODE (type) == UNION_TYPE;
}

/* Check every trackable operand VAR is USE_STMT (a call argument, or
   an ordinary copy's RHS) against every mutating call in MUTATING_
   CALLS/MUTATED_DECLS/MUTATED_TYPES that provably precedes it,
   emitting a diagnostic (unless header-exempted) for the first one
   Rule #0/#1 cannot clear.  */

static void
ip_check_operand_uses (gimple *use_stmt, tree var,
			vec<gimple *> &mutating_calls,
			vec<tree> &mutated_decls, vec<tree> &mutated_types)
{
  gimple *reaching = ip_nearest_write_before (var, use_stmt);
  if (!reaching)
    return;
  tree bound_decl = ip_binding_established_by (reaching);
  if (!bound_decl)
    return;

  for (unsigned i = 0; i < mutating_calls.length (); ++i)
    {
      gimple *m = mutating_calls[i];
      if (use_stmt == m || !ip_use_after_mutation_p (m, use_stmt))
	continue;

      tree mutated_decl = mutated_decls[i];
      bool safe = (mutated_decl != bound_decl
		   && ip_types_provably_unrelated_p (mutated_types[i],
						      TREE_TYPE (bound_decl)));
      if (safe)
	continue;
      if (!profiles_header_exempt_p (gimple_location (use_stmt),
				      "std::invalidation"))
	error_at (gimple_location (use_stmt),
		  "use of a value bound to %qD, potentially invalidated "
		  "by an earlier mutation of %qD, not permitted under the "
		  "%<std::invalidation%> profile", bound_decl, mutated_decl);
      break;
    }
}

/* One trackable operand read, recorded during the initial statement
   walk in ip_check_function and checked afterward once dominance
   info is available.  */

struct ip_use
{
  gimple *stmt;
  tree var;
};

/* Main per-function check.  Collects every mutating call (as defined
   by ip_mutating_call_p) once, then walks every statement's operands
   looking for a trackable class-typed VAR_DECL/PARM_DECL read
   (ip_check_operand_uses does the actual Rule #0/#1 work per use).  */

static unsigned int
ip_check_function (function *fun)
{
  auto_vec<gimple *> mutating_calls;
  auto_vec<tree> mutated_decls;
  auto_vec<tree> mutated_types;
  auto_vec<ip_use> uses;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (gcall *call = dyn_cast<gcall *> (stmt))
	  {
	    tree decl, type;
	    if (ip_mutating_call_p (call, &decl, &type))
	      {
		mutating_calls.safe_push (call);
		mutated_decls.safe_push (decl);
		mutated_types.safe_push (type);
	      }
	    for (unsigned i = 0; i < gimple_call_num_args (call); ++i)
	      {
		tree arg = gimple_call_arg (call, i);
		if (ip_trackable_operand_p (arg))
		  uses.safe_push ({ stmt, arg });
	      }
	  }
	else if (is_gimple_assign (stmt) && gimple_assign_single_p (stmt))
	  {
	    tree rhs = gimple_assign_rhs1 (stmt);
	    if (ip_trackable_operand_p (rhs))
	      uses.safe_push ({ stmt, rhs });
	  }
      }

  if (mutating_calls.is_empty () || uses.is_empty ())
    return 0;

  bool dominance_computed = false;
  if (!dom_info_available_p (CDI_DOMINATORS))
    {
      calculate_dominance_info (CDI_DOMINATORS);
      dominance_computed = true;
    }

  for (unsigned i = 0; i < uses.length (); ++i)
    ip_check_operand_uses (uses[i].stmt, uses[i].var, mutating_calls,
			    mutated_decls, mutated_types);

  if (dominance_computed)
    free_dominance_info (CDI_DOMINATORS);

  return 0;
}

namespace {

const pass_data pass_data_invalidation_profile_gimple =
{
  GIMPLE_PASS,
  "invalidation_profile",
  OPTGROUP_NONE,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_invalidation_profile_gimple : public gimple_opt_pass
{
public:
  pass_invalidation_profile_gimple (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_invalidation_profile_gimple, ctxt)
  {}

  bool gate (function *) final override
  {
    return profiles_enforced_p ("std::invalidation");
  }

  unsigned int execute (function *fun) final override
  {
    return ip_check_function (fun);
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_invalidation_profile_gimple (gcc::context *ctxt)
{
  return new pass_invalidation_profile_gimple (ctxt);
}
