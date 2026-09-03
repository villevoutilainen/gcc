/* P4222 Initialization profile, GIMPLE/SSA-level checker.

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

/* Phase 2, local-scalars-only slice: proves that a local scalar
   variable marked [[uninit]] (tree.cc's handle_uninit_attribute) is
   never read before a real assignment has reached that read on every
   possible control-flow path -- real, sound Definite Assignment
   Analysis, done at the GIMPLE/SSA level, deliberately more precise
   than P4222's own "no complex flow analysis" letter (see the
   profiles plan's Phase 2 notes: real DAA is itself local and sound,
   so this is a permitted strengthening, not a deviation).

   Registered unconditionally from init_profiles (profiles.cc) as a
   real gimple_opt_pass right after "ssa", the same
   register_pass-from-front-end-init approach D4324's own experimental
   GIMPLE engine uses (see contracts-gimple.cc's own top-of-file
   comment) -- gate () below is what actually makes this a no-op
   whenever the std::init profile isn't enforced, so there's no
   separate command-line flag the way that engine has: profiles are
   enabled from source (profiles::enforce), not the command line, and
   -- because Increment 1's placement restriction requires
   profiles::enforce to appear before any declaration in the
   translation unit -- profiles_enforced_p's answer is already
   final by the time any function in the TU reaches this pass.

   Proof kernel (ip_definitely_assigned_p below) is directly modeled
   on contracts-gimple.cc's own cg_provable_object_address_p: same
   default-def base case, same AND-across-all-PHI-arms recursion for a
   GIMPLE_PHI def statement (the sound CFG-join real DAA needs), same
   in_progress cycle guard for loop-carried PHIs, conservatively
   treated as "not yet assigned" -- see that function's own comment
   for why this shape is correct. Unlike that function, this one has
   no established-facts map to consult: an SSA name for a [[uninit]]
   local is definitely assigned if and only if it is reachable from a
   real (non-PHI, non-default-def) defining statement on every
   incoming path -- there is nothing else to trust it against.

   Known, deliberate scope limits for this increment (documented, not
   silent):
   - Only SCALAR_TYPE_P locals are marked [[uninit]] at all (see
     tree.cc's own comment) -- class types and arrays are later work
     (profiles plan Phase 4).
   - The in_progress cycle guard, like cg_provable_object_address_p's
     own, does not remove a node once visited within a single query,
     so a shared ancestor reached via two different non-looping merge
     paths (nested diamonds, not a loop) can be conservatively
     re-treated as unproven on its second visit. This can only ever
     produce a false positive (an extra, spurious diagnostic on
     genuinely-always-initialized code), never a false negative -- the
     same accepted, explicitly-sanctioned "local flow analysis, some
     false positives" tradeoff both P4222 and P3446 state outright.

   Phase 3 additions (ip_check_address_taken_var, ip_check_call_
   flavor_consistency): P4222's [[ref_to_uninit]]/[[must_init]]
   attributes (S4.3, S6.2, S9.3-9.5, tree.cc's own handle_ref_to_
   uninit_attribute/handle_must_init_attribute) extend the guarantee
   to pointer flavor-consistency and, specifically, to a [[uninit]]
   local whose address IS taken -- Phase 2's original blanket "address
   taken means unverifiable, hard error" rule is superseded by a
   narrower one: still a hard error for any address-of occurrence that
   isn't a direct write or a recognized [[must_init]] call argument,
   but real, sound, CFG-dominance-based Definite Assignment Analysis
   for the pattern P4222 exists to make legal (T x [[uninit]];
   initialize(&x); use(x);) -- see ip_check_address_taken_var's own
   comment for why this is a distinct proof kernel (CFG/basic-block
   dominance, not SSA/PHI) rather than an extension of ip_definitely_
   assigned_p: VAR isn't is_gimple_reg once addressed, so it has no
   SSA names for that kernel to reason about at all. Call-argument
   pointer-flavor consistency (ip_check_call_flavor_consistency) does
   NOT attempt to propagate flavor through arbitrary pointer
   expressions (arithmetic, PHI merges) -- see ip_arg_uninit_
   flavored_p's own comment for why that's real points-to reasoning,
   deliberately left to the profiles plan's Phase 7 (P3446/P4296
   invalidation profile) rather than duplicated here.  */

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

/* True if SSA_NAME is provably reached, on every incoming control-flow
   path, by a real assignment -- i.e. is definitely assigned, in the
   classic Definite Assignment Analysis sense.  See this file's own
   top comment for the shape and its accepted imprecision.  */

static bool
ip_definitely_assigned_p (tree ssa_name, hash_set<tree> &in_progress)
{
  if (SSA_NAME_IS_DEFAULT_DEF (ssa_name))
    return false;

  if (in_progress.contains (ssa_name))
    return false;
  in_progress.add (ssa_name);

  gimple *def = SSA_NAME_DEF_STMT (ssa_name);
  if (def && gimple_code (def) == GIMPLE_PHI)
    {
      unsigned n = gimple_phi_num_args (def);
      for (unsigned i = 0; i < n; ++i)
	{
	  tree arg = gimple_phi_arg_def (def, i);
	  if (TREE_CODE (arg) != SSA_NAME)
	    continue;
	  if (!ip_definitely_assigned_p (arg, in_progress))
	    return false;
	}
      return true;
    }

  /* Under C++26 erroneous-behavior initialization (P2795), a scalar
     local declared without an initializer (and without
     [[indeterminate]]) is gimplified with an explicit, always-
     executed '.DEFERRED_INIT (...)' call that gives it a well-defined
     placeholder value -- see gimplify.cc's own gimple_add_init_for_
     auto_var and tree.h's DECL_STRUCT_FUNCTION-adjacent comment for
     the mechanism. That is precisely the case [[uninit]] exists to
     require an explicit annotation for: from this profile's point of
     view, a '.DEFERRED_INIT'-defined SSA name is exactly as
     "not really assigned by user code" as a default-def one, not a
     real write, even though it IS a real GIMPLE definition to the
     rest of the compiler.  Recognized unconditionally (not gated on
     any C++26-specific flag) since a future dialect could enable this
     lowering by default without any other observable difference this
     pass would otherwise key off of.  */
  if (def && gimple_call_internal_p (def, IFN_DEFERRED_INIT))
    return false;

  /* Any other real defining statement (an ordinary assignment, or a
     call whose result is stored directly into this SSA name) means an
     actual write reached this point.  */
  return true;
}

/* True if STMT is a real, source-level use of its operands -- as
   opposed to a GIMPLE_PHI, whose "uses" are just control-flow-merge
   bookkeeping already accounted for by ip_definitely_assigned_p's own
   recursion into PHI arguments, not a read a programmer could
   observe.  */

static bool
ip_real_use_stmt_p (gimple *stmt)
{
  return gimple_code (stmt) != GIMPLE_PHI;
}

/* P4222 Phase 3: bookkeeping for ip_check_address_taken_var's own
   direct scan of every statement in a function, for a single
   address-taken [[uninit]] local VAR.  Populated by ip_scan_stmt_for_
   var, consumed by ip_check_address_taken_var itself.  */

struct ip_addr_taken_scan
{
  tree var;
  auto_vec<gimple *> init_stmts;
  auto_vec<gimple *> read_stmts;
  bool other_addr_of;
};

/* Record, in S, how STMT relates to S->var: a direct read, a direct
   write (an "initializing event", same as P4222 S4.6's "for a
   built-in type, [writing an uninitialized object is] simply a
   write-to"), an address-of occurrence recognized as a [[must_init]]
   call argument (also an initializing event, S6.2), or any other
   address-of occurrence (unverifiable, S->other_addr_of).

   Written directly per statement kind rather than via the existing
   walk_stmt_load_store_addr_ops utility (gimple-walk.h): that utility
   does not treat a non-single-rhs assignment (e.g. 'x = var + 1') or a
   GIMPLE_COND's operands as a load at all (see its own source for the
   exact branch shape), which would silently miss real reads of VAR --
   exactly the kind of gap this project's own standing "no silent
   soundness gaps" practice exists to catch.  GIMPLE_ASM operands are
   NOT scanned (a real, narrow, documented gap: VAR used as an asm
   operand goes undetected by this pass) -- rare enough for this
   increment to defer rather than duplicate parse_output_constraint's
   own machinery here.  VAR can never appear as a GIMPLE_PHI argument
   (PHI operands are always SSA names or invariants; VAR is a memory
   decl precisely because its address is taken), so PHIs need no
   handling either.  */

/* The underlying VAR_DECL/PARM_DECL of T, whether T is that decl
   directly (a memory-based, non-is_gimple_reg variable) or an
   SSA_NAME for a register-based one -- used to find "what is this
   assignment's target flavored as" regardless of which shape the
   pointer variable being assigned into happens to be.  */

static tree
ip_underlying_var (tree t)
{
  if (TREE_CODE (t) == SSA_NAME)
    return SSA_NAME_VAR (t);
  if (VAR_P (t) || TREE_CODE (t) == PARM_DECL)
    return t;
  return NULL_TREE;
}

/* True if STMT is 'var = tmp;' where TMP's own reaching definition is
   a '.DEFERRED_INIT (...)' call -- the same C++26 erroneous-behavior
   placeholder ip_definitely_assigned_p's own comment already accounts
   for on the SSA/register side, but reaching VAR here through an
   ordinary copy into memory instead of directly, since VAR itself is
   not is_gimple_reg.  Not a real initializing event: see that
   function's own comment for the full rationale, which applies
   identically here.  */

static bool
ip_stmt_is_deferred_init_copy_p (gimple *stmt)
{
  tree rhs1 = gimple_assign_rhs1 (stmt);
  return TREE_CODE (rhs1) == SSA_NAME
	 && gimple_call_internal_p (SSA_NAME_DEF_STMT (rhs1), IFN_DEFERRED_INIT);
}

static void
ip_scan_stmt_for_var (gimple *stmt, ip_addr_taken_scan *s)
{
  tree var = s->var;

  if (is_gimple_assign (stmt))
    {
      tree lhs = gimple_assign_lhs (stmt);
      tree rhs[3] = { gimple_assign_rhs1 (stmt), gimple_assign_rhs2 (stmt),
		      gimple_assign_rhs3 (stmt) };
      for (tree r : rhs)
	{
	  if (!r)
	    continue;
	  if (r == var)
	    s->read_stmts.safe_push (stmt);
	  else if (TREE_CODE (r) == ADDR_EXPR && TREE_OPERAND (r, 0) == var)
	    {
	      /* Legitimate (not an error, not an initializing event for
		 VAR) if the assignment target is itself a pointer
		 variable/parameter marked [[ref_to_uninit]] or
		 [[must_init]] -- exactly the shape decl.cc's own
		 declaration-time check already validates for 'T* p
		 [[ref_to_uninit]] = &var;' (P4222 S4.3); this is that
		 same check's GIMPLE-level counterpart for any OTHER
		 assignment of &var into such a pointer, not just its
		 initializer.  */
	      tree lhs_var = ip_underlying_var (lhs);
	      if (!(lhs_var && TREE_CODE (TREE_TYPE (lhs_var)) == POINTER_TYPE
		    && profiles_uninit_pointee_p (lhs_var)))
		s->other_addr_of = true;
	    }
	}
      if (lhs == var && !ip_stmt_is_deferred_init_copy_p (stmt))
	s->init_stmts.safe_push (stmt);
    }
  else if (gimple_code (stmt) == GIMPLE_COND)
    {
      if (gimple_cond_lhs (stmt) == var || gimple_cond_rhs (stmt) == var)
	s->read_stmts.safe_push (stmt);
    }
  else if (gimple_code (stmt) == GIMPLE_CALL)
    {
      tree callee = gimple_call_fndecl (stmt);
      unsigned nargs = gimple_call_num_args (stmt);
      for (unsigned i = 0; i < nargs; ++i)
	{
	  tree arg = gimple_call_arg (stmt, i);
	  if (arg == var)
	    s->read_stmts.safe_push (stmt);
	  else if (TREE_CODE (arg) == ADDR_EXPR
		   && TREE_OPERAND (arg, 0) == var)
	    {
	      if (callee
		  && profiles_uninit_flavor_at_position_p (callee, i + 1,
							    /*must_init_only=*/true))
		/* [[must_init]]: an initializing event -- VAR is
		   guaranteed initialized after this call returns.  */
		s->init_stmts.safe_push (stmt);
	      else if (callee
		       && profiles_uninit_flavor_at_position_p (
			    callee, i + 1, /*must_init_only=*/false))
		/* Plain [[ref_to_uninit]] (not [[must_init]]): a
		   legitimate address-of occurrence -- the callee is
		   declared to accept VAR's [[uninit]] state as-is, but
		   makes no promise about it afterward, so this is
		   neither an error nor an initializing event.
		   ip_check_call_flavor_consistency separately verifies
		   VAR is actually [[uninit]] here, matching the
		   parameter's flavor; nothing more to do.  */
		;
	      else
		s->other_addr_of = true;
	    }
	}
      if (gimple_call_lhs (stmt) == var
	  && !gimple_call_internal_p (stmt, IFN_DEFERRED_INIT))
	s->init_stmts.safe_push (stmt);
    }
  else if (greturn *ret = dyn_cast <greturn *> (stmt))
    {
      tree val = gimple_return_retval (ret);
      if (val == var)
	s->read_stmts.safe_push (stmt);
      else if (val && TREE_CODE (val) == ADDR_EXPR
	       && TREE_OPERAND (val, 0) == var)
	s->other_addr_of = true;
    }
}

/* True if READ_STMT is provably reached, on every path from the
   function's entry, by at least one statement in INIT_STMTS -- either
   a strictly dominating statement in a different basic block, or an
   earlier statement in the very same block (plain basic-block
   dominance doesn't order two statements within one block, so same-
   block pairs need an explicit straight-line scan).  */

static bool
ip_read_dominated_by_init_p (gimple *read_stmt, vec<gimple *> &init_stmts)
{
  basic_block read_bb = gimple_bb (read_stmt);
  for (unsigned i = 0; i < init_stmts.length (); ++i)
    {
      gimple *init_stmt = init_stmts[i];
      basic_block init_bb = gimple_bb (init_stmt);
      if (init_bb == read_bb)
	{
	  for (gimple_stmt_iterator gsi = gsi_start_bb (read_bb);
	       !gsi_end_p (gsi); gsi_next (&gsi))
	    {
	      gimple *s = gsi_stmt (gsi);
	      if (s == read_stmt)
		break;
	      if (s == init_stmt)
		return true;
	    }
	}
      else if (dominated_by_p (CDI_DOMINATORS, read_bb, init_bb))
	return true;
    }
  return false;
}

/* P4222 Phase 3: the address-taken counterpart of ip_definitely_
   assigned_p's SSA-based DAA, for a [[uninit]] local whose address is
   taken -- the exact case Phase 2 could only reject outright (see
   this file's own earlier, now-superseded top comment).  VAR is not
   is_gimple_reg, so it has no SSA names to reason about; this instead
   walks the function directly (ip_scan_stmt_for_var) and applies the
   same "must be reached by a real write on every path" rule via CFG
   dominance rather than PHI recursion -- structurally the same DAA
   guarantee, over basic blocks instead of SSA names.

   Only a direct write to VAR or a call passing &VAR to a [[must_init]]
   parameter counts as an initializing event (P4222 S4.6, S6.2); any
   OTHER address-of occurrence of VAR (passed to an ordinary or merely
   [[ref_to_uninit]]-but-not-[[must_init]] parameter, stored into
   another variable, etc.) is still unverifiable and a hard error, same
   as Phase 2's original blanket rule -- this narrows that rule to
   exactly the one pattern P4222 itself provides a way to verify,
   rather than widening it past what's actually sound.  */

static void
ip_check_address_taken_var (function *fun, tree var, bool *dominance_computed)
{
  ip_addr_taken_scan scan;
  scan.var = var;
  scan.other_addr_of = false;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      ip_scan_stmt_for_var (gsi_stmt (gsi), &scan);

  if (scan.other_addr_of)
    {
      error_at (DECL_SOURCE_LOCATION (var),
		"cannot verify %<[[uninit]]%> on %qD under the "
		"%<std::init%> profile: its address is taken outside a "
		"recognized %<[[must_init]]%> call, which this checker "
		"cannot yet analyze", var);
      return;
    }

  if (scan.init_stmts.is_empty ())
    {
      for (gimple *read_stmt : scan.read_stmts)
	error_at (gimple_location (read_stmt),
		  "%qD read before it is definitely assigned, under the "
		  "%<std::init%> profile", var);
      return;
    }

  if (!*dominance_computed)
    {
      calculate_dominance_info (CDI_DOMINATORS);
      *dominance_computed = true;
    }

  for (gimple *read_stmt : scan.read_stmts)
    if (!ip_read_dominated_by_init_p (read_stmt, scan.init_stmts))
      error_at (gimple_location (read_stmt),
		"%qD read before it is definitely assigned, under the "
		"%<std::init%> profile", var);
}

/* P4222 Phase 3, S4.3/S9.4: true if ARG (a call-argument expression)
   is "uninit-flavored" -- either the address of a local marked
   [[uninit]], or a direct pass-through of a pointer variable itself
   marked [[ref_to_uninit]] or [[must_init]] (profiles_uninit_
   pointee_p covers both, S9.3: "[[must_init]] implies [[ref_to_
   uninit]]").  Conservatively false for anything else (arithmetic on
   pointers, a PHI-merged pointer, a cast chain, etc.), matching
   P4222's own default rule ("by default, a pointer... is considered
   to point to memory initialized to some type", S4.3) -- propagating
   flavor through arbitrary pointer expressions is real points-to
   reasoning, which is what P3446/P4296's own invalidation-profile
   psets exist for (profiles plan Phase 7), not duplicated here.  */

static bool
ip_arg_uninit_flavored_p (tree arg)
{
  if (TREE_CODE (arg) == ADDR_EXPR)
    {
      tree operand = TREE_OPERAND (arg, 0);
      return VAR_P (operand)
	     && lookup_attribute ("uninit", DECL_ATTRIBUTES (operand)) != NULL_TREE;
    }
  if (TREE_CODE (arg) == SSA_NAME)
    {
      tree var = SSA_NAME_VAR (arg);
      if (var
	  && (VAR_P (var) || TREE_CODE (var) == PARM_DECL)
	  && TREE_CODE (TREE_TYPE (var)) == POINTER_TYPE)
	return profiles_uninit_pointee_p (var);
    }
  return false;
}

/* P4222 Phase 3, S4.3/S9.4: for a direct call, check that every
   pointer argument's uninit-flavor (ip_arg_uninit_flavored_p) matches
   its corresponding parameter's ([[ref_to_uninit]]/[[must_init]] via
   profiles_uninit_flavor_at_position_p) -- in both directions, exactly
   the "a pointer or reference to something initialized can't be
   passed to a [[ref_to_uninit]]" / "...to an [[uninit]] can be passed
   only to a [[ref_to_uninit]]" pair of rules (S4.2).  Runs
   unconditionally over every call in an enforced function, independent
   of whether either side is tied to a [[uninit]] local -- flavor
   consistency is a property of the pointer TYPES/declarations
   involved, not just of locals this pass otherwise tracks.

   Queries flavor by ARGUMENT POSITION (profiles_uninit_flavor_at_
   position_p), not by walking DECL_ARGUMENTS (callee): the latter is
   NULL whenever CALLEE is only declared, never defined, in this
   translation unit (see that function's own comment for the full
   reason) -- exactly the common case of calling a function declared
   in a header, which this check needs to work for, not just for
   same-TU-defined callees.  No separate "is this a pointer parameter"
   check is needed either: a non-pointer position is simply never
   recorded by that marker, so param_flavor is false for it same as
   any unflavored position, and ip_arg_uninit_flavored_p is equally
   false for a non-pointer argument -- both sides agree with no
   mismatch, without needing to know the parameter's type at all.  */

static void
ip_check_call_flavor_consistency (gimple *stmt)
{
  if (gimple_code (stmt) != GIMPLE_CALL)
    return;
  tree callee = gimple_call_fndecl (stmt);
  if (!callee)
    return;

  unsigned nargs = gimple_call_num_args (stmt);
  for (unsigned i = 0; i < nargs; ++i)
    {
      bool param_flavor
	= profiles_uninit_flavor_at_position_p (callee, i + 1,
						 /*must_init_only=*/false);
      bool arg_flavor = ip_arg_uninit_flavored_p (gimple_call_arg (stmt, i));

      if (param_flavor && !arg_flavor)
	error_at (gimple_location (stmt),
		  "argument %u to %qD must refer to %<[[uninit]]%> memory, "
		  "matching its %<[[ref_to_uninit]]%> parameter, under the "
		  "%<std::init%> profile", i + 1, callee);
      else if (!param_flavor && arg_flavor)
	error_at (gimple_location (stmt),
		  "argument %u to %qD refers to %<[[uninit]]%> memory but "
		  "its parameter is not marked %<[[ref_to_uninit]]%>, under "
		  "the %<std::init%> profile", i + 1, callee);
    }
}

static unsigned int
ip_check_function (function *fun)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      ip_check_call_flavor_consistency (gsi_stmt (gsi));

  bool dominance_computed = false;
  unsigned i;
  tree var;
  FOR_EACH_LOCAL_DECL (fun, i, var)
    {
      if (!VAR_P (var) || !lookup_attribute ("uninit", DECL_ATTRIBUTES (var)))
	continue;

      if (!is_gimple_reg (var))
	{
	  ip_check_address_taken_var (fun, var, &dominance_computed);
	  continue;
	}

      /* Walk every SSA name in the function and filter by
	 SSA_NAME_VAR: there is no single starting point (like a
	 default-def) guaranteed to exist for VAR to reconstruct the
	 rest of its SSA names from -- under C++26 erroneous-behavior
	 initialization (see ip_definitely_assigned_p's own comment),
	 VAR's very first GIMPLE definition can be a '.DEFERRED_INIT'
	 call, in which case VAR is never read in an actually-
	 undefined state and so never gets a default-def SSA name at
	 all.  FOR_EACH_SSA_NAME visits every SSA name the renamer
	 created for this function exactly once regardless, so this
	 is simpler and correct either way.  */
      imm_use_iterator imm_iter;
      use_operand_p use_p;
      tree name;
      unsigned si;
      FOR_EACH_SSA_NAME (si, name, fun)
	{
	  if (SSA_NAME_VAR (name) != var)
	    continue;

	  FOR_EACH_IMM_USE_FAST (use_p, imm_iter, name)
	    {
	      gimple *use_stmt = USE_STMT (use_p);
	      if (!ip_real_use_stmt_p (use_stmt))
		continue;

	      hash_set<tree> in_progress;
	      if (!ip_definitely_assigned_p (name, in_progress))
		error_at (gimple_location (use_stmt),
			  "%qD read before it is definitely assigned, "
			  "under the %<std::init%> profile", var);
	    }
	}
    }

  if (dominance_computed)
    free_dominance_info (CDI_DOMINATORS);

  return 0;
}

namespace {

const pass_data pass_data_init_profile_gimple =
{
  GIMPLE_PASS,
  "init_profile",
  OPTGROUP_NONE,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_init_profile_gimple : public gimple_opt_pass
{
public:
  pass_init_profile_gimple (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_init_profile_gimple, ctxt)
  {}

  bool gate (function *) final override
  {
    return profiles_enforced_p ("std::init");
  }

  unsigned int execute (function *fun) final override
  {
    return ip_check_function (fun);
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_init_profile_gimple (gcc::context *ctxt)
{
  return new pass_init_profile_gimple (ctxt);
}
