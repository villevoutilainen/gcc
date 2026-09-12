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
   whenever the std::init profile isn't active at all (neither
   enforced nor merely warned, profiles_active_p, profiles.cc): a
   profile can be enabled from source (profiles::enforce) or, non-
   intrusively, from the command line (-fprofiles-enforce=/-fprofiles-
   warning=), and -- because Increment 1's placement restriction
   requires profiles::enforce to appear before any declaration in the
   translation unit, and both command-line flags are processed before
   parsing even begins -- profiles_active_p's answer is already
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
   invalidation profile) rather than duplicated here.

   Phase 4 addition (arrays, P4222 S1.3/S4.9/S5.5): decl.cc's own
   declaration-time check now also requires [[uninit]] for a local
   array of scalar element type left without an initializer.  An array
   is never is_gimple_reg (AGGREGATE_TYPE_P), so it always goes
   through ip_check_address_taken_var like any other address-taken
   [[uninit]] local -- array-to-pointer decay ('&arr' passed to a
   [[must_init]]/[[ref_to_uninit]] bulk-initialization function like
   uninitialized_fill()) is already recognized for free by that same
   machinery, since it's the identical ADDR_EXPR-of-the-variable shape
   as '&scalar_var'.  An element access (ip_array_ref_base, checked in
   ip_scan_stmt_for_var alongside the existing read/write/address-of
   cases) is NOT banned outright -- it's folded into the same
   dominance check as an ordinary scalar read: P4222's own S4.9
   example ('uninitialized_fill(a2,10); int x = a2[0]; // OK') shows
   random access is fine once a recognized bulk-initialization call
   has been proven to dominate it, and only unverifiable *before* that
   point, matching S1.3's "random access... must be banned" in spirit
   (banning the ad-hoc, unverifiable case) without banning ordinary use
   of an already-initialized array.  An array element is never itself
   an init_stmts entry (raw arrays have no whole-array assignment
   syntax to begin with), so this can only ever be checked against
   some other must_init call, never mistaken for one.  */

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
  auto_vec<gimple *> other_addr_of_stmts;
  bool member_access;
  location_t member_access_loc;
};

/* Record STMT_LOC as the location of the first unverifiable occurrence
   found for a scan flag, if one hasn't already been recorded -- shared
   by ip_addr_taken_scan/ip_local_member_scan/ip_member_scan's own
   "escape" flags below, so each family's "cannot verify" diagnostic can
   point at the actual offending statement via a followup inform(),
   not just (as before) the [[uninit]] declaration itself.  First
   occurrence found wins, matching this file's general practice of
   reporting the earliest problem rather than the last one seen.  */

/* Unlike ip_record_first_loc just below (still used for cases where
   only a single representative occurrence is ever reported, e.g.
   ip_addr_taken_scan's own member_access), every unverifiable
   occurrence recorded into ip_addr_taken_scan/ip_local_member_scan/
   ip_member_scan's own "escape" vectors is simply pushed as-is (plain
   vec::safe_push, no helper needed) -- each occurrence gets its own,
   independent diagnostic UNLESS it turns out to be dominated by an
   initializing event for the same variable (see ip_check_address_
   taken_var's own reach-info check below): a variable can be escaped
   unverifiably more than once in the same function, and each is its
   own separate, independently annotatable/suppressible/curable
   problem, not a repeat of the same one.  */

static inline void
ip_record_first_loc (bool *flag, location_t *loc, location_t stmt_loc)
{
  if (!*flag)
    *loc = stmt_loc;
  *flag = true;
}

/* P4222 Phase 4, S1.3/S5.5: the ultimate base of (possibly nested,
   for a multi-dimensional array) ARRAY_REFs in T, or T itself if T is
   not an ARRAY_REF at all.  */

static tree
ip_array_ref_base (tree t)
{
  while (t && TREE_CODE (t) == ARRAY_REF)
    t = TREE_OPERAND (t, 0);
  return t;
}

/* P4222 Phase 4e (S5.4): the ultimate base of (possibly nested, for a
   'var.a.b'-style chain) COMPONENT_REFs in T, or T itself if T is not
   a COMPONENT_REF at all.  Used only to detect that VAR (now
   potentially a non-union class-type local with a trivial default
   constructor, since ip_scalar_or_scalar_array_p's own Phase 4e
   extension) had one of its own members accessed directly -- this
   pass has no per-member DAA for an arbitrary local the way
   ip_check_constructor_member has for 'this' inside a constructor
   (Phase 4d), so any such access is honestly declined
   (ip_addr_taken_scan's own member_access flag), never silently
   passed as if it had been verified.  */

static tree
ip_component_ref_base (tree t)
{
  while (t && TREE_CODE (t) == COMPONENT_REF)
    t = TREE_OPERAND (t, 0);
  return t;
}

/* True if PTR's own value is traceable through a straight-line chain
   of plain single-operand copies all the way back to a literal
   'ADDR_EXPR (var)' -- i.e. PTR is provably '&var', not merely some
   pointer that MIGHT be. Pointer arithmetic, a PHI-merged pointer, or
   a pointer read back out of some OTHER variable all conservatively
   decline (return false), matching this file's default-decline stance
   elsewhere for anything requiring real points-to reasoning (e.g.
   ip_resolve_underlying_decl further down, for an unrelated purpose).
   Shared by ip_mem_ref_targets_var_p below (PTR is a dereference's own
   base operand) and ip_scan_stmt_for_var's std::construct_at
   recognition (PTR is passed directly as a call argument, no
   dereference involved at all).  */

static bool
ip_ptr_traces_to_var_p (tree ptr, tree var)
{
  if (TREE_CODE (ptr) == ADDR_EXPR)
    return TREE_OPERAND (ptr, 0) == var;
  while (TREE_CODE (ptr) == SSA_NAME)
    {
      gimple *def = SSA_NAME_DEF_STMT (ptr);
      if (!def || !is_gimple_assign (def) || !gimple_assign_single_p (def))
	return false;
      tree rhs = gimple_assign_rhs1 (def);
      if (TREE_CODE (rhs) == ADDR_EXPR)
	return TREE_OPERAND (rhs, 0) == var;
      ptr = rhs;
    }
  return false;
}

/* True if T is a MEM_REF (a raw pointer's own dereference, '*p') whose
   base pointer's reaching definition traces back to '&VAR' -- an
   access through a pointer this checker can prove points at VAR is
   exactly as much an access of VAR as naming VAR directly, whether T
   is being READ (needs the same DAA proof VAR itself would: dominated
   by an init_stmts entry, or VAR was never [[uninit]] to begin with)
   or WRITTEN (IS itself an init_stmts entry for VAR, exactly like
   'var = ...;' directly -- unlike an ARRAY_REF write, which only ever
   covers one element, never the whole object, a MEM_REF write through
   a pointer proven to equal '&var' covers the ENTIRE object, the same
   as a direct scalar write does, so there is no "partial coverage"
   reason to withhold it the way ip_array_ref_base's own callers
   deliberately do for x[i]). Confirmed via direct testing the read
   case was a real, silent gap: 'int x [[uninit]]; int* p
   [[ref_to_uninit]] = &x; return *p;' compiled clean, since a
   dereference's operand is an opaque SSA_NAME to a purely syntactic
   per-statement scan like this one -- nothing about the *shape* '*p'
   contains 'x' as a literal subtree, unlike '&x' or 'x.field' or
   'x[i]', which this file's existing ARRAY_REF/COMPONENT_REF/direct-
   equality checks already catch. Deliberately narrow: only a
   ZERO-offset MEM_REF is considered (VAR is a scalar, so any other
   offset reads/writes memory merely NEAR var, not var itself).  */

static bool
ip_mem_ref_targets_var_p (tree t, tree var)
{
  if (TREE_CODE (t) != MEM_REF || !integer_zerop (TREE_OPERAND (t, 1)))
    return false;
  return ip_ptr_traces_to_var_p (TREE_OPERAND (t, 0), var);
}

/* True if CALL is a call to std::construct_at -- recognized by name
   (decl_in_std_namespace_p + id_equal, the same pattern invalidation-
   profile-gimple.cc's own no_dangling/now_valid escape hatches use),
   not by any attribute on its own declared signature. construct_at's
   real signature ('template<class T, class..
   . Args> constexpr T* construct_at(T* p, Args&&... args);') cannot be
   given [[ref_to_uninit]]/[[must_init]] on its own 'p' parameter the
   way a purpose-built escape hatch like now_init can: unlike now_init,
   construct_at is called constantly with an ORDINARY, unflavored
   pointer (freshly-allocated storage, or storage being re-constructed
   over), so requiring every caller's pointer to itself be flavored
   would reject nearly all real, legitimate usage. See this function's
   two call sites (ip_check_call_flavor_consistency, ip_scan_stmt_for_
   var) for what's special-cased instead.  */

static bool
ip_construct_at_call_p (gcall *call)
{
  tree fndecl = gimple_call_fndecl (call);
  return fndecl && decl_in_std_namespace_p (fndecl)
	 && id_equal (DECL_NAME (fndecl), "construct_at");
}

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
	  else if (TREE_CODE (r) == ARRAY_REF && ip_array_ref_base (r) == var)
	    /* An element access on an [[uninit]] array (P4222 S5.5) is
	       checked exactly like a scalar read -- dominated by an
	       initializing [[must_init]] call (the array's own
	       elements are never individually "written" the way a
	       scalar is, so this can never itself become an
	       init_stmts entry, only ever something ip_read_dominated_
	       by_init_p checks against those) -- not treated as its
	       own separate "banned outright" category: P4222's own
	       S4.9 example ('uninitialized_fill(a2,10); int x =
	       a2[0]; // OK') shows random access is fine once the
	       whole array is provably initialized, only *before* that
	       point is it unverifiable.  */
	    s->read_stmts.safe_push (stmt);
	  else if (TREE_CODE (r) == COMPONENT_REF
		   && ip_component_ref_base (r) == var)
	    ip_record_first_loc (&s->member_access, &s->member_access_loc,
				 gimple_location (stmt));
	  else if (TREE_CODE (r) == MEM_REF && ip_mem_ref_targets_var_p (r, var))
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
		s->other_addr_of_stmts.safe_push (stmt);
	    }
	  else if (TREE_CODE (r) == ADDR_EXPR
		   && TREE_CODE (TREE_OPERAND (r, 0)) == COMPONENT_REF
		   && ip_component_ref_base (TREE_OPERAND (r, 0)) == var)
	    /* '&var.field' -- unlike a direct read/write of var.field
	       just above, this was previously invisible to this scan
	       entirely (no case matched it at all): neither the
	       COMPONENT_REF case above (R here is an ADDR_EXPR, not a
	       COMPONENT_REF) nor the ADDR_EXPR-of-VAR case just above
	       (TREE_OPERAND (R, 0) is a COMPONENT_REF, not VAR itself).
	       MEMBER_ACCESS is not just informational -- it's what gates
	       whether ip_check_local_aggregate_member ever runs at all
	       for ANY field of VAR, so a field whose only access anywhere
	       in the function was '&var.field' silently skipped all per-
	       field escape/read verification for the whole aggregate, not
	       just that one field (confirmed directly: 'X x [[uninit]];
	       take_ptr (&x.a);' alone compiled clean). Treat '&var.field'
	       as its own trigger for MEMBER_ACCESS, exactly like a direct
	       read/write -- ip_scan_stmt_for_local_member's own ADDR_EXPR/
	       COMPONENT_REF handling already correctly verifies (or
	       cures) the occurrence itself once this actually runs.  */
	    ip_record_first_loc (&s->member_access, &s->member_access_loc,
				 gimple_location (stmt));
	}
      if (lhs == var && !ip_stmt_is_deferred_init_copy_p (stmt))
	s->init_stmts.safe_push (stmt);
      else if (TREE_CODE (lhs) == ARRAY_REF && ip_array_ref_base (lhs) == var)
	s->read_stmts.safe_push (stmt);
      else if (TREE_CODE (lhs) == COMPONENT_REF
	       && ip_component_ref_base (lhs) == var)
	ip_record_first_loc (&s->member_access, &s->member_access_loc,
			     gimple_location (stmt));
      else if (TREE_CODE (lhs) == MEM_REF && ip_mem_ref_targets_var_p (lhs, var)
	       && !ip_stmt_is_deferred_init_copy_p (stmt))
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
	  /* std::construct_at(p, ...)'s own first argument, whether
	     passed as a literal '&x' or as some pointer P provably
	     traceable back to it (ip_ptr_traces_to_var_p), is an
	     initializing event for VAR -- recognized by name
	     (ip_construct_at_call_p), not by any attribute on
	     construct_at's own signature, and deliberately does NOT
	     touch P's own flavor (see ip_check_call_flavor_consistency's
	     own matching special case): only the RETURN value of
	     construct_at is meant to be trusted afterward, exactly like
	     now_init's own by-value pass-through above, but recognized
	     here purely so the argument itself is treated as having
	     initialized VAR, without requiring construct_at's real,
	     unmodified declaration to carry [[must_init]] at all.  */
	  if (i == 0 && callee && ip_construct_at_call_p (as_a<gcall *> (stmt))
	      && ip_ptr_traces_to_var_p (arg, var))
	    s->init_stmts.safe_push (stmt);
	  else if (arg == var)
	    s->read_stmts.safe_push (stmt);
	  else if (TREE_CODE (arg) == ARRAY_REF
		   && ip_array_ref_base (arg) == var)
	    s->read_stmts.safe_push (stmt);
	  else if (TREE_CODE (arg) == COMPONENT_REF
		   && ip_component_ref_base (arg) == var)
	    ip_record_first_loc (&s->member_access, &s->member_access_loc,
				 gimple_location (stmt));
	  else if (TREE_CODE (arg) == MEM_REF && ip_mem_ref_targets_var_p (arg, var))
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
		s->other_addr_of_stmts.safe_push (stmt);
	    }
	  else if (TREE_CODE (arg) == ADDR_EXPR
		   && TREE_CODE (TREE_OPERAND (arg, 0)) == COMPONENT_REF
		   && ip_component_ref_base (TREE_OPERAND (arg, 0)) == var)
	    /* '&var.field' as a call argument -- see the identical case
	       in the assignment-rhs loop above for the full rationale;
	       same gap, same fix.  */
	    ip_record_first_loc (&s->member_access, &s->member_access_loc,
				 gimple_location (stmt));
	}
      tree call_lhs = gimple_call_lhs (stmt);
      if (call_lhs == var && !gimple_call_internal_p (stmt, IFN_DEFERRED_INIT))
	s->init_stmts.safe_push (stmt);
      else if (call_lhs && TREE_CODE (call_lhs) == ARRAY_REF
	       && ip_array_ref_base (call_lhs) == var)
	s->read_stmts.safe_push (stmt);
      else if (call_lhs && TREE_CODE (call_lhs) == COMPONENT_REF
	       && ip_component_ref_base (call_lhs) == var)
	ip_record_first_loc (&s->member_access, &s->member_access_loc,
			     gimple_location (stmt));
      else if (call_lhs && TREE_CODE (call_lhs) == MEM_REF
	       && ip_mem_ref_targets_var_p (call_lhs, var))
	s->init_stmts.safe_push (stmt);
    }
  else if (greturn *ret = dyn_cast <greturn *> (stmt))
    {
      tree val = gimple_return_retval (ret);
      if (val == var)
	s->read_stmts.safe_push (stmt);
      else if (val && TREE_CODE (val) == ARRAY_REF
	       && ip_array_ref_base (val) == var)
	s->read_stmts.safe_push (stmt);
      else if (val && TREE_CODE (val) == COMPONENT_REF
	       && ip_component_ref_base (val) == var)
	ip_record_first_loc (&s->member_access, &s->member_access_loc,
			     gimple_location (stmt));
      else if (val && TREE_CODE (val) == MEM_REF
	       && ip_mem_ref_targets_var_p (val, var))
	s->read_stmts.safe_push (stmt);
      else if (val && TREE_CODE (val) == ADDR_EXPR
	       && TREE_OPERAND (val, 0) == var)
	s->other_addr_of_stmts.safe_push (stmt);
      else if (val && TREE_CODE (val) == ADDR_EXPR
	       && TREE_CODE (TREE_OPERAND (val, 0)) == COMPONENT_REF
	       && ip_component_ref_base (TREE_OPERAND (val, 0)) == var)
	/* 'return &var.field;' -- see the identical case in the
	   assignment-rhs loop above for the full rationale; same gap,
	   same fix.  */
	ip_record_first_loc (&s->member_access, &s->member_access_loc,
			     gimple_location (stmt));
    }
}

/* Found and fixed 2026-09-04: plain CFG dominance ("does some SINGLE
   statement in INIT_STMTS dominate this point") cannot express "these
   SEVERAL statements, none of which individually dominates the merge
   point, jointly cover every path" -- the textbook-safe 'if (c) x = 1;
   else x = 2; use (x);' shape, where each branch's own write reaches
   the merge point but neither dominates it alone. Confirmed via
   direct testing: this incorrectly rejected that exact shape, for
   both an address-taken [[uninit]] local (ip_check_address_taken_var)
   and (by the same reused helper) a constructor's own member
   (ip_check_constructor_member) -- previously unnoticed only because
   no existing test exercised "both branches write a NON-register
   [[uninit]] variable/member"; the SSA/PHI-based scalar path
   (ip_definitely_assigned_p) has always handled this shape correctly,
   since PHI nodes make CFG merges explicit.

   Replaced with a real forward "must reach" dataflow analysis instead
   -- the same shape classic definite-assignment analysis uses: one
   boolean per basic block, meet-by-AND across predecessors (a block
   is "reached" on entry only if EVERY predecessor is already
   reached), updated to true the moment a block contains a statement
   in INIT_STMTS, iterated to a fixed point.  Monotonic (a block's own
   state only ever goes from unreached to reached, never back) and
   bounded (finitely many blocks), so a worklist that keeps re-
   visiting until nothing changes is guaranteed to terminate, in at
   most O(blocks) full passes.  */

struct ip_reach_info
{
  /* Indexed by basic_block->index (this covers the ENTRY block's own
     index too, whose slot is simply never marked reached, exactly
     modeling "nothing has happened yet at function entry").  TRUE if
     every path from the function's entry to the START (block_in) or
     END (block_out) of that block already passes through at least
     one statement in the INIT_STMTS the info was computed for.  */
  auto_vec<bool> block_in;
  auto_vec<bool> block_out;
};

static void
ip_compute_reach_info (function *fun, vec<gimple *> &init_stmts,
			ip_reach_info *info)
{
  unsigned n = last_basic_block_for_fn (fun);
  info->block_in.safe_grow_cleared (n);
  info->block_out.safe_grow_cleared (n);

  auto_vec<bool> block_has_init (n);
  block_has_init.safe_grow_cleared (n);
  for (unsigned i = 0; i < init_stmts.length (); ++i)
    block_has_init[gimple_bb (init_stmts[i])->index] = true;

  bool changed = true;
  while (changed)
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, fun)
	{
	  bool in = EDGE_COUNT (bb->preds) > 0;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    if (!info->block_out[e->src->index])
	      {
		in = false;
		break;
	      }
	  if (in && !info->block_in[bb->index])
	    {
	      info->block_in[bb->index] = true;
	      changed = true;
	    }

	  bool out = info->block_in[bb->index] || block_has_init[bb->index];
	  if (out && !info->block_out[bb->index])
	    {
	      info->block_out[bb->index] = true;
	      changed = true;
	    }
	}
    }
}

/* True if READ_STMT is provably reached, on every path from the
   function's entry, by at least one statement in INIT_STMTS -- either
   the block containing READ_STMT was already fully "reached" on
   entry (INFO's own dataflow answer), or an earlier statement in the
   very same block establishes it (block-level dataflow only tracks
   block BOUNDARIES, not ordering within one block, so a same-block
   pair still needs an explicit straight-line scan, exactly as
   before).  */

static bool
ip_read_dominated_by_init_p (gimple *read_stmt, vec<gimple *> &init_stmts,
			      const ip_reach_info &info)
{
  basic_block read_bb = gimple_bb (read_stmt);
  if (info.block_in[read_bb->index])
    return true;

  for (unsigned i = 0; i < init_stmts.length (); ++i)
    {
      gimple *init_stmt = init_stmts[i];
      if (gimple_bb (init_stmt) != read_bb)
	continue;
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
  return false;
}

/* P4222 Phase 4f: bookkeeping for ip_check_local_aggregate_member's
   own scan, for a single FIELD of a [[uninit]]-marked local aggregate
   VAR (not 'this' inside a constructor -- ip_member_scan's own job,
   Phase 4d, defined further down). Matches 'var.field' directly:
   unlike a constructor's 'this->field', a local aggregate's own
   members are never accessed through a pointer dereference.  */

struct ip_local_member_scan
{
  tree var;
  tree field;
  auto_vec<gimple *> init_stmts;
  auto_vec<gimple *> read_stmts;
  auto_vec<gimple *> other_escape_stmts;
};

/* True if T is exactly 'var.field' -- a COMPONENT_REF selecting FIELD
   directly out of VAR (a VAR_DECL, not a pointer to one).  */

static bool
ip_component_ref_of_var_field_p (tree t, tree var, tree field)
{
  if (TREE_CODE (t) != COMPONENT_REF || TREE_OPERAND (t, 1) != field)
    return false;
  return ip_underlying_var (TREE_OPERAND (t, 0)) == var;
}

/* True if DEST is itself a pointer variable/parameter declared
   [[ref_to_uninit]]/[[must_init]] -- the single-hop test that decides
   whether an assignment 'dest = &var.field;'/'dest = &this->field;' is
   a legitimate formation of a flavored pointer, or an unverifiable
   escape.  Identical in spirit to ip_scan_stmt_for_var's own ADDR_EXPR-
   of-VAR case just above, and intentionally as simple: a [[ref_to_
   uninit]]-declared pointer pointing at still-[[uninit]] memory is, by
   definition, exactly flavor-consistent (§3) the moment it's formed --
   nothing more needs verifying, regardless of how (or whether) the
   pointer is subsequently used.  An earlier version of this check
   instead walked every downstream SSA use of the resulting pointer,
   requiring each one to independently look like a safe consumption (a
   flavored call argument, or a copy into another flavored variable) --
   solving a problem this simpler, whole-object-matching test doesn't
   have, while also rejecting a declared-flavored pointer that simply
   went unused, and separately accepting an UNflavored intermediate
   pointer merely because of what it was later passed to (deferring
   the real problem -- an unflavored variable holding a still-uninit
   address -- to an unrelated later statement instead of catching it
   where it's formed).  */

static bool
ip_addr_of_field_dest_flavored_p (tree dest)
{
  tree dest_var = ip_underlying_var (dest);
  return dest_var && TREE_CODE (TREE_TYPE (dest_var)) == POINTER_TYPE
	 && profiles_uninit_pointee_p (dest_var);
}

/* Extends ip_addr_of_field_dest_flavored_p for one confirmed, narrow
   GIMPLE-lowering wrinkle: '&this->field''s address, when about to be
   consumed immediately as a call argument, is never inlined directly
   at the point of use -- GCC's own gimplifier always routes it through
   an anonymous SSA temporary first ('_1 = &this->p; initialize (_1);',
   confirmed via -fdump-tree-gimple), even though the equivalent local-
   aggregate call, 'initialize (&x.a);', inlines the ADDR_EXPR directly
   with no temporary at all, and even 'this->field' itself, when
   assigned to a real named variable ('int* p = &this->a;'), also
   inlines directly with no temporary. Such a temporary has no source-
   level declaration of its own to check a flavor against, so the only
   way to judge whether *this* address-of occurrence is safe is to look
   at the single place the temporary's value is actually used: safe
   exactly when that's a call argument at a parameter position declared
   [[must_init]] (an initializing event, recorded into INIT_STMTS) or
   plain [[ref_to_uninit]] (neutral); a temporary used more than once,
   zero times, or any other way cannot be vouched for. This is a
   bounded, single hop through a compiler-only intermediate -- not a
   general walk over a real variable's downstream uses, which is what
   an earlier, more permissive version of this whole check did for
   every DEST regardless of whether it was a genuine source-level
   variable, and got both directions wrong: rejecting an unused-but-
   flavored real pointer, and separately accepting an UNflavored real
   pointer purely because of what it was later passed to (see git
   history for the full rationale of that simplification).  */

static bool
ip_addr_of_field_dest_ok_p (tree dest, auto_vec<gimple *> *init_stmts)
{
  if (ip_addr_of_field_dest_flavored_p (dest))
    return true;

  if (TREE_CODE (dest) != SSA_NAME || SSA_NAME_VAR (dest) != NULL_TREE)
    return false;

  imm_use_iterator imm_iter;
  use_operand_p use_p;
  gimple *only_use = NULL;
  unsigned use_count = 0;
  FOR_EACH_IMM_USE_FAST (use_p, imm_iter, dest)
    {
      only_use = USE_STMT (use_p);
      if (++use_count > 1)
	return false;
    }
  if (use_count != 1 || gimple_code (only_use) != GIMPLE_CALL)
    return false;

  tree callee = gimple_call_fndecl (only_use);
  unsigned nargs = gimple_call_num_args (only_use);
  for (unsigned i = 0; i < nargs; ++i)
    if (gimple_call_arg (only_use, i) == dest)
      {
	if (callee
	    && profiles_uninit_flavor_at_position_p (callee, i + 1,
						      /*must_init_only=*/true))
	  {
	    init_stmts->safe_push (only_use);
	    return true;
	  }
	if (callee
	    && profiles_uninit_flavor_at_position_p (callee, i + 1,
						      /*must_init_only=*/false))
	  return true;
	return false;
      }
  return false;
}

/* The local-aggregate-member counterpart of ip_scan_stmt_for_member
   (defined further down, for 'this->field'), matching 'var.field'
   instead.  Same per-slot shape: assign rhs/lhs, cond operands, call
   args/lhs, return retval.  */

static void
ip_scan_stmt_for_local_member (gimple *stmt, ip_local_member_scan *s)
{
  tree var = s->var;
  tree field = s->field;

  if (is_gimple_assign (stmt))
    {
      tree lhs = gimple_assign_lhs (stmt);
      tree rhs[3] = { gimple_assign_rhs1 (stmt), gimple_assign_rhs2 (stmt),
		      gimple_assign_rhs3 (stmt) };
      for (tree r : rhs)
	{
	  if (!r)
	    continue;
	  if (ip_component_ref_of_var_field_p (r, var, field))
	    s->read_stmts.safe_push (stmt);
	  else if (TREE_CODE (r) == ADDR_EXPR
		   && ip_component_ref_of_var_field_p (TREE_OPERAND (r, 0),
							var, field))
	    {
	      if (!ip_addr_of_field_dest_ok_p (lhs, &s->init_stmts))
		s->other_escape_stmts.safe_push (stmt);
	    }
	}
      if (ip_component_ref_of_var_field_p (lhs, var, field)
	  && !ip_stmt_is_deferred_init_copy_p (stmt))
	s->init_stmts.safe_push (stmt);
    }
  else if (gimple_code (stmt) == GIMPLE_COND)
    {
      if (ip_component_ref_of_var_field_p (gimple_cond_lhs (stmt), var, field)
	  || ip_component_ref_of_var_field_p (gimple_cond_rhs (stmt), var,
					       field))
	s->read_stmts.safe_push (stmt);
    }
  else if (gimple_code (stmt) == GIMPLE_CALL)
    {
      tree callee = gimple_call_fndecl (stmt);
      unsigned nargs = gimple_call_num_args (stmt);
      for (unsigned i = 0; i < nargs; ++i)
	{
	  tree arg = gimple_call_arg (stmt, i);
	  if (ip_component_ref_of_var_field_p (arg, var, field))
	    s->read_stmts.safe_push (stmt);
	  else if (TREE_CODE (arg) == ADDR_EXPR
		   && ip_component_ref_of_var_field_p (TREE_OPERAND (arg, 0),
							var, field))
	    {
	      if (callee
		  && profiles_uninit_flavor_at_position_p (callee, i + 1,
							    /*must_init_only=*/true))
		s->init_stmts.safe_push (stmt);
	      else if (callee
		       && profiles_uninit_flavor_at_position_p (
			    callee, i + 1, /*must_init_only=*/false))
		;
	      else
		s->other_escape_stmts.safe_push (stmt);
	    }
	}
      tree call_lhs = gimple_call_lhs (stmt);
      if (call_lhs
	  && ip_component_ref_of_var_field_p (call_lhs, var, field)
	  && !gimple_call_internal_p (stmt, IFN_DEFERRED_INIT))
	s->init_stmts.safe_push (stmt);
    }
  else if (greturn *ret = dyn_cast <greturn *> (stmt))
    {
      tree val = gimple_return_retval (ret);
      if (val && ip_component_ref_of_var_field_p (val, var, field))
	s->read_stmts.safe_push (stmt);
      else if (val && TREE_CODE (val) == ADDR_EXPR
	       && ip_component_ref_of_var_field_p (TREE_OPERAND (val, 0), var,
						    field))
	s->other_escape_stmts.safe_push (stmt);
    }
}

/* Bookkeeping for ip_compute_field_reach_table below: one FIELD's own
   init_stmts (OUTER_INIT_STMTS already folded in, exactly like
   ip_check_local_aggregate_member's own internal scan) and the reach
   info computed from them.  */

struct ip_field_reach_entry
{
  tree field;
  auto_vec<gimple *> init_stmts;
  ip_reach_info info;
};

/* A whole-object write to a local aggregate VAR (e.g. 'var = f();') is
   never the only way every field ends up provably initialized: GCC's
   own gimplifier decomposes a simple aggregate assignment like
   'var = {1, 2};' directly into per-field stores ('var.a = 1; var.b =
   2;') before this pass ever sees a single whole-object GIMPLE_ASSIGN
   (confirmed directly via -fdump-tree-gimple) -- so a set of per-field
   writes that happens to cover every field is semantically equivalent
   to a whole-object write, even though ip_scan_stmt_for_var's own IE-1
   check can never see it as one.  This computes, for every non-
   artificial FIELD_DECL of VAR's RECORD_TYPE, that field's own reach
   info (the identical per-field scan ip_check_local_aggregate_member
   already runs internally, duplicated here rather than threading out-
   parameters through that existing, separately-tested function -- this
   is a static-analysis pass, not hot codegen, so the extra scan is an
   acceptable, low-risk tradeoff).  OUT_TABLE's entries are heap-
   allocated; the caller owns them (see ip_free_field_reach_table).  */

static void
ip_compute_field_reach_table (function *fun, tree var,
			       vec<gimple *> &outer_init_stmts,
			       vec<ip_field_reach_entry *> *out_table)
{
  for (tree field = TYPE_FIELDS (TREE_TYPE (var)); field;
       field = DECL_CHAIN (field))
    {
      if (TREE_CODE (field) != FIELD_DECL || DECL_ARTIFICIAL (field))
	continue;

      ip_local_member_scan scan;
      scan.var = var;
      scan.field = field;

      basic_block bb;
      FOR_EACH_BB_FN (bb, fun)
	for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	     gsi_next (&gsi))
	  ip_scan_stmt_for_local_member (gsi_stmt (gsi), &scan);

      ip_field_reach_entry *e = new ip_field_reach_entry ();
      e->field = field;
      for (gimple *stmt : scan.init_stmts)
	e->init_stmts.safe_push (stmt);
      for (gimple *stmt : outer_init_stmts)
	e->init_stmts.safe_push (stmt);
      ip_compute_reach_info (fun, e->init_stmts, &e->info);
      out_table->safe_push (e);
    }
}

static void
ip_free_field_reach_table (vec<ip_field_reach_entry *> &table)
{
  for (ip_field_reach_entry *e : table)
    delete e;
}

/* True iff AT_STMT is dominated by EVERY entry in TABLE -- every field
   of the aggregate TABLE was computed for is individually, provably
   initialized by this point, the "per-field writes add up to a whole-
   object write" fact ip_compute_field_reach_table exists to establish.
   False (never vacuously true) for an empty TABLE -- this is only ever
   called where at least one field genuinely needs checking.  */

static bool
ip_all_fields_dominate_p (gimple *at_stmt, vec<ip_field_reach_entry *> &table)
{
  if (table.is_empty ())
    return false;
  for (ip_field_reach_entry *e : table)
    if (!ip_read_dominated_by_init_p (at_stmt, e->init_stmts, e->info))
      return false;
  return true;
}

/* P4222 Phase 4f: the ordinary-local counterpart of ip_check_
   constructor_member (Phase 4d, defined further down, for
   'this->field' inside a constructor) -- real per-FIELD CFG-
   dominance-based DAA for a single member of a [[uninit]]-marked
   local aggregate VAR, so a local can genuinely be declared
   uninitialized and filled in field-by-field (in whichever order,
   across however many statements) before being read, the same way a
   plain [[uninit]] scalar already can.

   Unlike ip_check_constructor_member, there is no "every RETURN
   dominated by init" requirement here: that exists specifically
   because returning from a constructor exposes the object to callers,
   who could then read any member with no further visibility into how
   it was built.  An ordinary local's own scope simply ending is not
   an analogous exposure point -- a field never read anywhere in this
   function needs no write at all, exactly like a plain [[uninit]]
   scalar that's never read.

   OUTER_INIT_STMTS are ip_check_address_taken_var's own whole-object
   init events for VAR (e.g. a whole-object assignment 'var = f();')
   -- those legitimately establish every field's value at once too, so
   a field read is safe if dominated by EITHER its own direct write or
   one of these.  */

static void
ip_check_local_aggregate_member (function *fun, tree var, tree field,
				  vec<gimple *> &outer_init_stmts)
{
  ip_local_member_scan scan;
  scan.var = var;
  scan.field = field;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      ip_scan_stmt_for_local_member (gsi_stmt (gsi), &scan);

  /* OUTER_INIT_STMTS folded in before computing reach info, so a
     whole-object init event cures an unverifiable FIELD escape the
     same way it already cures a FIELD read (see this function's own
     top comment) -- [[uninit]] is not permanent for a field either.  */
  for (gimple *init_stmt : outer_init_stmts)
    scan.init_stmts.safe_push (init_stmt);

  ip_reach_info info;
  ip_compute_reach_info (fun, scan.init_stmts, &info);

  if (!scan.other_escape_stmts.is_empty ())
    {
      /* Anchored at each surviving escape site, not at VAR's own
	 declaration -- see ip_check_address_taken_var's own identical,
	 more detailed comment on this exact point (same rationale
	 applies here, including reporting every occurrence, not just
	 the first, and curing an occurrence dominated by an
	 initializing event rather than diagnosing it).  */
      for (gimple *escape_stmt : scan.other_escape_stmts)
	{
	  if (ip_read_dominated_by_init_p (escape_stmt, scan.init_stmts,
					   info))
	    continue;
	  location_t loc = gimple_location (escape_stmt);
	  if (!profiles_diagnostic_exempt_p (loc, fun->decl, "std::init"))
	    {
	      profiles_diagnostic_at (loc, "std::init",
			"address of %<[[uninit]]%> member %qD of %qD is "
			"taken here before it is provably initialized, "
			"under the %<std::init%> profile", field, var);
	      inform (DECL_SOURCE_LOCATION (var),
		      "%qD is declared %<[[uninit]]%> here", var);
	    }
	}
    }

  if (scan.read_stmts.is_empty ())
    return;

  for (gimple *read_stmt : scan.read_stmts)
    if (!ip_read_dominated_by_init_p (read_stmt, scan.init_stmts, info)
	&& !profiles_diagnostic_exempt_p (gimple_location (read_stmt),
					  fun->decl, "std::init"))
      profiles_diagnostic_at (gimple_location (read_stmt), "std::init",
		"member %qD of %qD read before it is definitely assigned, "
		"under the %<std::init%> profile", field, var);
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
ip_check_address_taken_var (function *fun, tree var)
{
  ip_addr_taken_scan scan;
  scan.var = var;
  scan.member_access = false;
  scan.member_access_loc = UNKNOWN_LOCATION;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      ip_scan_stmt_for_var (gsi_stmt (gsi), &scan);

  /* Computed unconditionally, up front, so both the escape check just
     below and the read check further down can share it: [[uninit]]
     is not a permanent property of VAR's declaration -- a write
     (IE-1), a call passing &VAR to a [[must_init]] parameter (IE-2),
     a write through a provably-traced pointer (IE-3), or construct_at
     (IE-4) all cure it, for an address-escape occurrence exactly the
     same way they already do for a read.  */
  ip_reach_info info;
  ip_compute_reach_info (fun, scan.init_stmts, &info);

  /* A local aggregate whose fields were each written individually (e.g.
     'var = {1, 2};', which GCC's own gimplifier decomposes into per-
     field stores before this pass ever sees a single whole-object
     assignment -- confirmed directly via -fdump-tree-gimple) never
     shows up in SCAN.INIT_STMTS above, even once every field has
     provably been covered.  Computed once, up front, only when actually
     needed (member-level access exists, on a genuine RECORD_TYPE), and
     reused for every escape occurrence below.  */
  auto_vec<ip_field_reach_entry *> field_reach;
  bool have_field_reach
    = scan.member_access && TREE_CODE (TREE_TYPE (var)) == RECORD_TYPE;
  if (have_field_reach)
    ip_compute_field_reach_table (fun, var, scan.init_stmts, &field_reach);

  if (!scan.other_addr_of_stmts.is_empty ())
    {
      /* Anchored at each surviving escape site itself (where the
	 address is actually taken), not at VAR's own declaration: this
	 is what every other diagnostic in this checker already does
	 (e.g. the "read before it is definitely assigned" case just
	 below), and anchoring here specifically matters for
	 [[profiles::suppress]] -- its dominion is the statement/
	 declaration it appertains to, so a suppress attribute placed on
	 the actual offending statement could never reach a diagnostic
	 anchored elsewhere, confirmed as a real, reported limitation of
	 the old anchor point.  VAR's own declaration is still surfaced,
	 via the note below, for context.  Every non-cured occurrence is
	 its own independent diagnostic, not just the first one found --
	 VAR can be escaped unverifiably more than once in the same
	 function.  An occurrence dominated by an initializing event --
	 either a genuine whole-object one, or every one of VAR's fields
	 individually -- is silently skipped, not diagnosed -- cured,
	 same as a read past that point would be.  */
      for (gimple *escape_stmt : scan.other_addr_of_stmts)
	{
	  if (ip_read_dominated_by_init_p (escape_stmt, scan.init_stmts,
					   info))
	    continue;
	  if (have_field_reach
	      && ip_all_fields_dominate_p (escape_stmt, field_reach))
	    continue;
	  location_t loc = gimple_location (escape_stmt);
	  if (!profiles_diagnostic_exempt_p (loc, fun->decl, "std::init"))
	    {
	      profiles_diagnostic_at (loc, "std::init",
			"address of %<[[uninit]]%> variable %qD is taken "
			"here before it is provably initialized, under the "
			"%<std::init%> profile", var);
	      inform (DECL_SOURCE_LOCATION (var),
		      "%qD is declared %<[[uninit]]%> here", var);
	    }
	}
    }

  if (have_field_reach)
    ip_free_field_reach_table (field_reach);

  /* P4222 Phase 4e (S5.4): VAR can now be a non-union class-type
     local with a trivial default constructor (ip_scalar_or_scalar_
     array_p's own extension, decl.cc), not just a scalar or array.
     For a genuine aggregate (RECORD_TYPE), Phase 4f's own per-FIELD
     DAA (ip_check_local_aggregate_member, above) applies real CFG-
     dominance-based verification to each member independently -- so
     a local can be declared [[uninit]] and filled in field-by-field
     before being read, the same way a plain [[uninit]] scalar
     already can.  Anything else with a member-access on it (a union,
     say -- those have their own separate story, d4324-profiles-
     union-member-bad.C) is still honestly declined rather than
     silently accepted.  */
  if (scan.member_access)
    {
      if (TREE_CODE (TREE_TYPE (var)) == RECORD_TYPE)
	{
	  for (tree field = TYPE_FIELDS (TREE_TYPE (var)); field;
	       field = DECL_CHAIN (field))
	    {
	      if (TREE_CODE (field) != FIELD_DECL || DECL_ARTIFICIAL (field))
		continue;
	      ip_check_local_aggregate_member (fun, var, field, scan.init_stmts);
	    }
	  return;
	}
      if (!profiles_diagnostic_exempt_p (DECL_SOURCE_LOCATION (var),
					 fun->decl, "std::init"))
	{
	  profiles_diagnostic_at (DECL_SOURCE_LOCATION (var), "std::init",
		    "cannot verify %<[[uninit]]%> on %qD under the "
		    "%<std::init%> profile: member-level access on this "
		    "aggregate is not yet analyzed", var);
	  inform (scan.member_access_loc,
		  "member access on %qD occurs here", var);
	}
      return;
    }

  /* INFO was already computed above, shared with the escape check --
     valid even when scan.init_stmts is empty (everything simply comes
     back "not dominated," same as the old dedicated fast path).  */
  for (gimple *read_stmt : scan.read_stmts)
    if (!ip_read_dominated_by_init_p (read_stmt, scan.init_stmts, info)
	&& !profiles_diagnostic_exempt_p (gimple_location (read_stmt),
					  fun->decl, "std::init"))
      profiles_diagnostic_at (gimple_location (read_stmt), "std::init",
		"%qD read before it is definitely assigned, under the "
		"%<std::init%> profile", var);
}

/* P4222 Phase 4d (S5.1-S5.3): bookkeeping for ip_check_constructor_
   member's own scan, for a single [[uninit]]/[[ref_to_uninit]]-marked
   FIELD of the class THIS_PARM constructs, within one constructor's
   body.  Structurally the member-access counterpart of ip_addr_taken_
   scan/ip_scan_stmt_for_var -- kept as its own, purpose-built scan
   rather than genericizing that one over "what counts as the
   target": the two targets (a whole variable vs. one member accessed
   through *this) differ enough in per-slot matching detail that
   sharing code seemed likelier to introduce subtle bugs between the
   two than to prevent them.  */

struct ip_member_scan
{
  tree this_parm;
  tree field;
  auto_vec<gimple *> init_stmts;
  auto_vec<gimple *> read_stmts;
  auto_vec<gimple *> other_escape_stmts;
};

/* True if T is exactly 'this->FIELD' (or an SSA-copy-of-THIS_PARM's
   equivalent) -- a COMPONENT_REF selecting FIELD out of a MEM_REF (the
   canonical GIMPLE dereference node) or INDIRECT_REF of THIS_PARM at
   offset 0.  Confirmed against real GIMPLE output (a constructor's own
   'this_4(D)->p = ...' / '_1 = this_4(D)->p;' shapes), not assumed.  */

static bool
ip_component_ref_of_this_field_p (tree t, tree this_parm, tree field)
{
  if (TREE_CODE (t) != COMPONENT_REF || TREE_OPERAND (t, 1) != field)
    return false;
  tree base = TREE_OPERAND (t, 0);
  tree ptr;
  if (TREE_CODE (base) == MEM_REF && integer_zerop (TREE_OPERAND (base, 1)))
    ptr = TREE_OPERAND (base, 0);
  else if (TREE_CODE (base) == INDIRECT_REF)
    ptr = TREE_OPERAND (base, 0);
  else
    return false;
  return ip_underlying_var (ptr) == this_parm;
}

/* The member-access counterpart of ip_scan_stmt_for_var -- same
   per-slot shape (assign rhs/lhs, cond operands, call args/lhs,
   return retval), same must_init/ref_to_uninit call recognition, just
   matching 'this->field' via ip_component_ref_of_this_field_p instead
   of a bare VAR_DECL.  */

static void
ip_scan_stmt_for_member (gimple *stmt, ip_member_scan *s)
{
  tree this_parm = s->this_parm;
  tree field = s->field;

  if (is_gimple_assign (stmt))
    {
      tree lhs = gimple_assign_lhs (stmt);
      tree rhs[3] = { gimple_assign_rhs1 (stmt), gimple_assign_rhs2 (stmt),
		      gimple_assign_rhs3 (stmt) };
      for (tree r : rhs)
	{
	  if (!r)
	    continue;
	  if (ip_component_ref_of_this_field_p (r, this_parm, field))
	    s->read_stmts.safe_push (stmt);
	  else if (TREE_CODE (r) == ADDR_EXPR
		   && ip_component_ref_of_this_field_p (TREE_OPERAND (r, 0),
							 this_parm, field))
	    {
	      if (!ip_addr_of_field_dest_ok_p (lhs, &s->init_stmts))
		s->other_escape_stmts.safe_push (stmt);
	    }
	}
      if (ip_component_ref_of_this_field_p (lhs, this_parm, field)
	  && !ip_stmt_is_deferred_init_copy_p (stmt))
	s->init_stmts.safe_push (stmt);
    }
  else if (gimple_code (stmt) == GIMPLE_COND)
    {
      if (ip_component_ref_of_this_field_p (gimple_cond_lhs (stmt), this_parm,
					     field)
	  || ip_component_ref_of_this_field_p (gimple_cond_rhs (stmt),
						this_parm, field))
	s->read_stmts.safe_push (stmt);
    }
  else if (gimple_code (stmt) == GIMPLE_CALL)
    {
      tree callee = gimple_call_fndecl (stmt);
      unsigned nargs = gimple_call_num_args (stmt);
      for (unsigned i = 0; i < nargs; ++i)
	{
	  tree arg = gimple_call_arg (stmt, i);
	  if (ip_component_ref_of_this_field_p (arg, this_parm, field))
	    s->read_stmts.safe_push (stmt);
	  else if (TREE_CODE (arg) == ADDR_EXPR
		   && ip_component_ref_of_this_field_p (TREE_OPERAND (arg, 0),
							 this_parm, field))
	    {
	      if (callee
		  && profiles_uninit_flavor_at_position_p (callee, i + 1,
							    /*must_init_only=*/true))
		s->init_stmts.safe_push (stmt);
	      else if (callee
		       && profiles_uninit_flavor_at_position_p (
			    callee, i + 1, /*must_init_only=*/false))
		;
	      else
		s->other_escape_stmts.safe_push (stmt);
	    }
	}
      tree call_lhs = gimple_call_lhs (stmt);
      if (call_lhs
	  && ip_component_ref_of_this_field_p (call_lhs, this_parm, field)
	  && !gimple_call_internal_p (stmt, IFN_DEFERRED_INIT))
	s->init_stmts.safe_push (stmt);
    }
  else if (greturn *ret = dyn_cast <greturn *> (stmt))
    {
      tree val = gimple_return_retval (ret);
      if (val && ip_component_ref_of_this_field_p (val, this_parm, field))
	s->read_stmts.safe_push (stmt);
      else if (val && TREE_CODE (val) == ADDR_EXPR
	       && ip_component_ref_of_this_field_p (TREE_OPERAND (val, 0),
						     this_parm, field))
	s->other_escape_stmts.safe_push (stmt);
    }
}

/* True if every path reaching the END of BB is provably preceded by
   at least one statement in the INIT_STMTS INFO was computed for --
   the "is this exit point safe" counterpart of
   ip_read_dominated_by_init_p (which asks "is this read safe").
   INFO's own block_out already answers exactly this (a same-block
   init_stmt is unconditionally sufficient there too: a basic block's
   own terminating statement -- the only kind that can lead to another
   block -- is always last, so any init_stmt sharing that block
   necessarily precedes it, matching block_out's own definition).  */

static bool
ip_block_dominated_by_init_p (basic_block bb, const ip_reach_info &info)
{
  return info.block_out[bb->index];
}

/* P4222 Phase 4d (S5.1-S5.3): the constructor-body counterpart of
   ip_check_address_taken_var, for a single [[uninit]]/[[ref_to_uninit]]-
   marked FIELD of the class THIS_PARM constructs.  Phase 4b's own
   front-end check (init.cc) already confirmed FIELD isn't covered by
   the member-initializer-list or an NSDMI (that's exactly why it's
   still marked [[uninit]]/[[ref_to_uninit]] and reaches this function
   at all).

   A member marked [[ref_to_uninit]]/[[must_init]] is a pointer/
   reference whose POINTEE may be uninitialized -- the pointer VALUE
   itself still has to exist before the object is exposed to callers,
   exactly like any other member; only what it points to is exempted.
   For such a FIELD, every ordinary RETURN point of the constructor
   must be dominated by an initializing write, the "OTHER half of
   S5.1's guarantee" this function's own read-before-write check
   below only covers in-body reads for.  EH-unwind edges (EDGE_EH) are
   excluded: a constructor that exits via an exception never actually
   brings the object into existence from a caller's perspective, so
   nothing is "exposed" on that path.

   A member marked literally [[uninit]] (the object itself, not a
   pointer to one, is uninitialized) has no such requirement: the
   entire point of [[uninit]] is "no promise is made here, not even by
   the constructor -- verify it later, at whatever point something
   actually reads it" (confirmed directly, 2026-09-04, after an
   earlier attempt in this same session to draw exactly this
   distinction was itself reverted based on a since-corrected reading
   of two now-fixed sibling tests, member-body-daa-ok.C/-bad.C, that
   had encoded the OPPOSITE, wrong assumption). A read-before-write
   within the SAME constructor body is still flagged regardless of
   which kind of FIELD it is (the loop just below): that read is
   something this pass CAN see and prove unsound, unlike a
   member's eventual use from outside the constructor entirely, which
   this function has no visibility into and doesn't attempt to check
   (see ip_arg_uninit_flavored_p's own comment on that being future,
   cross-function work).  */

static void
ip_check_constructor_member (function *fun, tree this_parm, tree field)
{
  ip_member_scan scan;
  scan.this_parm = this_parm;
  scan.field = field;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      ip_scan_stmt_for_member (gsi_stmt (gsi), &scan);

  /* Computed up front so the escape check, the read check, and the
     exit-edge check further down all share it.  */
  ip_reach_info info;
  ip_compute_reach_info (fun, scan.init_stmts, &info);

  if (!scan.other_escape_stmts.is_empty ())
    {
      /* Anchored at each surviving escape site, not at the enclosing
	 constructor's own declaration (which was the previous anchor
	 here -- even less useful than a sibling variable's own
	 declaration would have been) -- see ip_check_address_taken_
	 var's own identical, more detailed comment on this exact
	 point, including reporting every occurrence, not just the
	 first, and curing an occurrence dominated by an initializing
	 event rather than diagnosing it.  */
      for (gimple *escape_stmt : scan.other_escape_stmts)
	{
	  if (ip_read_dominated_by_init_p (escape_stmt, scan.init_stmts,
					   info))
	    continue;
	  location_t loc = gimple_location (escape_stmt);
	  if (!profiles_diagnostic_exempt_p (loc, fun->decl, "std::init"))
	    {
	      profiles_diagnostic_at (loc, "std::init",
			"address of %<[[uninit]]%> member %qD is taken "
			"here before it is provably initialized, under "
			"the %<std::init%> profile", field);
	      inform (DECL_SOURCE_LOCATION (field),
		      "%qD is declared %<[[uninit]]%> here", field);
	    }
	}
    }

  for (gimple *read_stmt : scan.read_stmts)
    if (!ip_read_dominated_by_init_p (read_stmt, scan.init_stmts, info)
	&& !profiles_diagnostic_exempt_p (gimple_location (read_stmt),
					  fun->decl, "std::init"))
      profiles_diagnostic_at (gimple_location (read_stmt), "std::init",
		"member %qD read before it is definitely assigned, under "
		"the %<std::init%> profile", field);

  /* A field marked literally [[uninit]] (as opposed to a pointer/
     reference merely marked [[ref_to_uninit]]/[[must_init]], whose
     own pointer VALUE still needs to exist) is exempt from ever
     needing a write inside the constructor at all -- see this
     function's own comment above.  */
  if (lookup_attribute ("uninit", DECL_ATTRIBUTES (field)))
    return;

  bool exit_ok = true;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR_FOR_FN (fun)->preds)
    {
      if (e->flags & EDGE_EH)
	continue;
      if (!ip_block_dominated_by_init_p (e->src, info))
	{
	  exit_ok = false;
	  break;
	}
    }
  if (!exit_ok
      && !profiles_diagnostic_exempt_p (DECL_SOURCE_LOCATION (fun->decl),
					fun->decl, "std::init"))
    profiles_diagnostic_at (DECL_SOURCE_LOCATION (fun->decl), "std::init",
	      "constructor may leave member %qD, marked %<[[uninit]]%>, "
	      "not definitely assigned before %<*this%> is exposed, under "
	      "the %<std::init%> profile", field);
}

/* P4222 Phase 3/4d, S4.3/S9.4: true if ARG (a call-argument
   expression) is "uninit-flavored" -- deliberately one uniform model,
   not two coincidentally-similar ones:

   - The address of a local or member marked [[uninit]] (a
     COMPONENT_REF's own FIELD_DECL operand, for a member -- gated on
     FIELD's own attribute OR the containing variable's, since P4222
     S5.3's [[uninit]] members are exactly as valid an argument to a
     [[must_init]]/[[ref_to_uninit]] parameter as an [[uninit]] local,
     and a field is just as subject to uninit-tracking when the WHOLE
     aggregate is [[uninit]]-declared, S2.3, as when it individually
     carries the attribute) is flavored *only if this exact occurrence
     is not itself already provably initialized* -- checked via ip_
     currently_uninit_p, the same CFG-dominance reach-info §2's own DAA
     and §2.5's address-escape check already use.  [[uninit]] is not a
     permanent property of a declaration; neither is being flavored.
   - A direct pass-through of a pointer variable/member itself
     PERMANENTLY marked [[ref_to_uninit]] or [[must_init]]
     (profiles_uninit_pointee_p covers both, S9.3: "[[must_init]]
     implies [[ref_to_uninit]]") is flavored unconditionally -- this
     one part genuinely is a fact of the declaration's own type, not a
     DAA question (a pointer declared [[ref_to_uninit]] doesn't stop
     being so just because what it currently points to happens to be
     initialized).

   Because the recursive SSA-copy chase below reaches the first rule
   regardless of how many plain-copy hops separate ARG from the literal
   '&E' (e.g. 'q <- p <- &x'), both Assign-/Call-arg-/Return-flavor-
   consistency already get the exact same DAA proof a direct '&E' at
   that exact position would -- no cast or escape-hatch call is needed
   in either case if the proof succeeds; if it can't even be attempted
   (a parameter, a global, an unprovable merge), that failure to prove
   is the reason a diagnostic (or an escape hatch) is still required,
   not a special case.  Conservatively false for anything the chase
   can't resolve to either rule (arithmetic on pointers, a genuinely
   mixed PHI merge, a cast chain through an unrelated variable, etc.),
   matching P4222's own default rule ("by default, a pointer... is
   considered to point to memory initialized to some type", S4.3) --
   propagating flavor through arbitrary pointer expressions beyond what
   this straight-line/PHI chase already does is real points-to
   reasoning, which is what P3446/P4296's own invalidation-profile
   psets exist for (profiles plan Phase 7), not duplicated here.  */

/* std::now_uninit -- the Initialization profile's manual, unproven
   "treat this value as [[ref_to_uninit]]-flavored regardless of its
   own declared flavor" assertion -- used to need its own by-name
   recognition function here (removed): unlike std::construct_at just
   above, what now_uninit asserts genuinely IS tied to a single
   position (its own return value), so it can just carry
   [[ref_to_uninit]] on that return directly (<utility>'s own
   definition), the same "void* [[ref_to_uninit]] malloc(size_t);"
   shape any other flavored-returning function already uses. Every
   call site below that used to special-case now_uninit by name now
   falls straight through to the ordinary profiles_uninit_pointee_p
   (callee) check, unconditionally correct with no special case at
   all -- confirmed empirically (a differently-named copy of now_
   uninit's own template, carrying the same attribute, produces
   identical diagnostics through the general path alone).  */

/* [[uninit]] is not a permanent property of a declaration -- a write
   (IE-1), a call passing &E to a [[must_init]] parameter (IE-2), a
   write through a provably-traced pointer (IE-3), or construct_at
   (IE-4) all cure it, for flavor-consistency purposes exactly the same
   way they already cure a read (ip_check_address_taken_var and
   friends) or an address-escape occurrence (same functions, extended
   above).  This cache lets ip_arg_uninit_flavored_p_1's own ADDR_EXPR
   branch ask "is BASE (or BASE's FIELD) still [[uninit]] AT_STMT" via
   the same dominance machinery, instead of a bare, position-
   independent DECL_ATTRIBUTES lookup -- computed lazily, once per
   (BASE, FIELD) pair actually queried within one function, and reused
   for the rest of that function's own flavor-consistency pass (built
   fresh in ip_check_function, since a function's own CFG/statements
   don't change mid-pass).  A linear scan is deliberately used instead
   of a hash map: the number of distinct [[uninit]] entities actually
   queried within one function is always small in practice, and a
   plain vec of heap-allocated entries avoids any complication from
   auto_vec-of-auto_vec copy semantics.  */

struct ip_flavor_reach_entry
{
  tree base;
  tree field; /* NULL_TREE for a plain variable, not a member.  */
  auto_vec<gimple *> init_stmts;
  ip_reach_info info;
};

struct ip_flavor_reach_cache
{
  auto_vec<ip_flavor_reach_entry *> entries;
  ~ip_flavor_reach_cache ()
  {
    for (ip_flavor_reach_entry *e : entries)
      delete e;
  }
};

static ip_flavor_reach_entry *
ip_get_flavor_reach_entry (function *fun, ip_flavor_reach_cache *cache,
			    tree base, tree field)
{
  for (ip_flavor_reach_entry *e : cache->entries)
    if (e->base == base && e->field == field)
      return e;

  ip_flavor_reach_entry *e = new ip_flavor_reach_entry ();
  e->base = base;
  e->field = field;

  basic_block bb;
  if (field == NULL_TREE)
    {
      /* Reuse ip_scan_stmt_for_var purely to collect BASE's own
	 init_stmts -- the exact same IE-1..4 recognition already used
	 for BASE's own DAA/address-escape checking, just re-run here
	 (lazily, once) for flavor-consistency's own benefit.  */
      ip_addr_taken_scan scan;
      scan.var = base;
      scan.member_access = false;
      scan.member_access_loc = UNKNOWN_LOCATION;
      FOR_EACH_BB_FN (bb, fun)
	for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	     gsi_next (&gsi))
	  ip_scan_stmt_for_var (gsi_stmt (gsi), &scan);
      for (gimple *stmt : scan.init_stmts)
	e->init_stmts.safe_push (stmt);
    }
  else
    {
      ip_local_member_scan scan;
      scan.var = base;
      scan.field = field;
      FOR_EACH_BB_FN (bb, fun)
	for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	     gsi_next (&gsi))
	  ip_scan_stmt_for_local_member (gsi_stmt (gsi), &scan);
      for (gimple *stmt : scan.init_stmts)
	e->init_stmts.safe_push (stmt);
    }

  ip_compute_reach_info (fun, e->init_stmts, &e->info);
  cache->entries.safe_push (e);
  return e;
}

/* True if BASE (or BASE's FIELD, when FIELD is not NULL_TREE) is still
   [[uninit]] AT_STMT -- i.e. AT_STMT is NOT dominated by an
   initializing event for it.  */

static bool
ip_currently_uninit_p (function *fun, ip_flavor_reach_cache *cache,
			gimple *at_stmt, tree base, tree field)
{
  ip_flavor_reach_entry *e
    = ip_get_flavor_reach_entry (fun, cache, base, field);
  if (ip_read_dominated_by_init_p (at_stmt, e->init_stmts, e->info))
    return false;
  /* Same fallback as ip_check_address_taken_var's own escape check,
     for the identical reason: a whole-object BASE that was instead
     initialized field-by-field (e.g. 'base = {1, 2};', decomposed by
     the gimplifier into per-field stores before this pass ever sees a
     single whole-object assignment) never shows up in E's own
     init_stmts, even once every field has provably been covered.
     Only meaningful for a whole-object query (FIELD == NULL_TREE) on
     a genuine RECORD_TYPE -- a per-field query already has its own,
     correct field-level init_stmts via E itself.  */
  if (field == NULL_TREE && TREE_CODE (TREE_TYPE (base)) == RECORD_TYPE)
    {
      auto_vec<ip_field_reach_entry *> field_reach;
      ip_compute_field_reach_table (fun, base, e->init_stmts, &field_reach);
      bool cured = ip_all_fields_dominate_p (at_stmt, field_reach);
      ip_free_field_reach_table (field_reach);
      if (cured)
	return false;
    }
  return true;
}

static bool ip_arg_uninit_flavored_p_1 (tree arg, int depth, function *fun,
					 gimple *at_stmt,
					 ip_flavor_reach_cache *cache);
static bool ip_arg_null_pointer_p (tree arg);

static bool
ip_arg_uninit_flavored_p (tree arg, function *fun, gimple *at_stmt,
			   ip_flavor_reach_cache *cache)
{
  return ip_arg_uninit_flavored_p_1 (arg, 0, fun, at_stmt, cache);
}

static bool
ip_arg_uninit_flavored_p_1 (tree arg, int depth, function *fun,
			     gimple *at_stmt, ip_flavor_reach_cache *cache)
{
  if (depth > 16)
    return false; /* Defensive recursion guard; never expected to trigger
		      in practice (see the GIMPLE_PHI case below for the
		      only way a cycle could even arise).  */
  if (TREE_CODE (arg) == ADDR_EXPR)
    {
      tree operand = TREE_OPERAND (arg, 0);
      if (VAR_P (operand))
	return lookup_attribute ("uninit", DECL_ATTRIBUTES (operand)) != NULL_TREE
	       && ip_currently_uninit_p (fun, cache, at_stmt, operand,
					 NULL_TREE);
      if (TREE_CODE (operand) == COMPONENT_REF)
	{
	  tree field = TREE_OPERAND (operand, 1);
	  tree base = ip_underlying_var (TREE_OPERAND (operand, 0));
	  /* Gate on FIELD *or* BASE carrying [[uninit]] -- not FIELD
	     alone.  A field is just as much subject to uninit-tracking
	     when the whole containing aggregate is [[uninit]]-declared
	     (every field starts uninitialized, S2.3) as when the field
	     itself individually carries the attribute; checking only
	     FIELD's own attribute wrongly treated 'int* p
	     [[ref_to_uninit]] = &x.a;' (x itself [[uninit]], a not
	     individually marked) as a flavor mismatch -- FIELD_MARKED
	     was false, so this returned false (ordinary) even though
	     x.a was genuinely still uninit, the opposite of what BASE's
	     own escape-checking (ip_check_local_aggregate_member)
	     already correctly concludes for the exact same field.  */
	  bool field_marked
	    = lookup_attribute ("uninit", DECL_ATTRIBUTES (field)) != NULL_TREE;
	  bool base_marked
	    = base && lookup_attribute ("uninit", DECL_ATTRIBUTES (base)) != NULL_TREE;
	  if (!field_marked && !base_marked)
	    return false;
	  /* BASE unresolved (an expression this pass can't trace to a
	     concrete local, e.g. through an opaque function call's
	     result): fall back to the old, conservative "declaration
	     alone decides" answer -- no dominance info to consult.  */
	  if (!base)
	    return true;
	  return ip_currently_uninit_p (fun, cache, at_stmt, base, field);
	}
      return false;
    }
  if (TREE_CODE (arg) == SSA_NAME)
    {
      /* If ARG's own reaching definition is itself a single-operand
	 assignment -- '&local_var'/'&this->field' (an ADDR_EXPR GIMPLE
	 never inlines directly at the point of use, confirmed by direct
	 testing), or a plain copy of another value into an anonymous
	 SSA temporary (confirmed by direct testing: a GLOBAL pointer
	 variable's read is first copied into one, e.g. 'src.0_1 = src;'
	 followed by 'take (src.0_1);' -- the call never sees 'src'
	 itself at all) -- recurse into whatever THAT assignment's own
	 RHS is, rather than examining ARG itself.  Well-founded (no
	 cycle-guard needed): a GIMPLE_ASSIGN's own reaching def is
	 always a strictly earlier statement, and this branch only ever
	 fires for an is_gimple_assign def, never a GIMPLE_PHI, so there
	 is no loop this recursion could run around.  */
      gimple *def = SSA_NAME_DEF_STMT (arg);
      if (def && is_gimple_assign (def) && gimple_assign_single_p (def))
	return ip_arg_uninit_flavored_p_1 (gimple_assign_rhs1 (def), depth + 1,
					    fun, at_stmt, cache);
      /* Or, if ARG's own reaching definition is a GIMPLE_CALL, ARG is
	 that call's own return value -- flavored exactly when CALLEE's
	 own return is (P4222 S4.3's "void* [[ref_to_uninit]]
	 malloc(size_t);"-shaped case, handle_ref_to_uninit_attribute's
	 FUNCTION_DECL branch, tree.cc). Confirmed via direct testing
	 this is the COMMON shape for 'p = flavored_fn ();' once SSA
	 construction has run: the call's own result is NOT assigned
	 directly into 'p' but into an anonymous SSA temporary first
	 ('_2 = flavored_fn (); p = _2;'), so without this branch, the
	 second, plain-copy statement's own RHS ('_2') would fall through
	 to the SSA_NAME_VAR check below and find nothing (an anonymous
	 temp has none), silently treating a flavored return as
	 unflavored.  No new predicate needed: profiles_uninit_pointee_p
	 already does nothing but a bare DECL_ATTRIBUTES lookup, which
	 works identically for a FUNCTION_DECL as for any other decl
	 kind -- including std::now_uninit itself, which carries
	 [[ref_to_uninit]] on its own return for exactly this reason
	 (<utility>'s own definition), needing no special case here.  */
      if (def && gimple_code (def) == GIMPLE_CALL)
	{
	  gcall *call = as_a<gcall *> (def);
	  tree callee = gimple_call_fndecl (call);
	  if (callee)
	    return profiles_uninit_pointee_p (callee);
	  return false;
	}
      /* Or, if ARG's own reaching definition is a GIMPLE_PHI -- the
	 shape multiple 'return expr;' statements in the same function
	 take once the compiler unifies them into one canonical exit
	 block (confirmed via direct -fdump-tree-ssa reading: 'if (cond)
	 return w.get(); return nullptr;' becomes a single '# _1 = PHI
	 <_9(3), _4(4)>; return _1;', not two separate GIMPLE_RETURNs) --
	 ARG is flavored iff EVERY incoming edge is either a null pointer
	 constant (ip_arg_null_pointer_p; compatible with either flavor,
	 so it never disqualifies) or itself flavored: one non-null,
	 unflavored edge means the merged value could genuinely hold an
	 ordinary, non-uninit-referring pointer on that path, which is
	 exactly what must NOT be treated as flavored (matches this
	 file's own "conservatively false/default-unflavored" philosophy
	 -- see this function's own top comment).  DEPTH is threaded
	 through (unlike the straight-line cases above) because a PHI can
	 be loop-carried and reach its own def again through a back
	 edge -- the one way this recursion could actually cycle.  */
      if (def && gimple_code (def) == GIMPLE_PHI)
	{
	  gphi *phi = as_a<gphi *> (def);
	  for (unsigned i = 0; i < gimple_phi_num_args (phi); ++i)
	    {
	      tree phi_arg = gimple_phi_arg_def (phi, i);
	      if (ip_arg_null_pointer_p (phi_arg))
		continue;
	      if (!ip_arg_uninit_flavored_p_1 (phi_arg, depth + 1, fun, at_stmt,
						cache))
		return false;
	    }
	  return true;
	}

      tree var = SSA_NAME_VAR (arg);
      if (var
	  && (VAR_P (var) || TREE_CODE (var) == PARM_DECL)
	  && TREE_CODE (TREE_TYPE (var)) == POINTER_TYPE)
	return profiles_uninit_pointee_p (var);
      return false;
    }
  /* A bare COMPONENT_REF (a field read BY VALUE, e.g. 'this->elem' or
     'mem.elem') -- the shape a [[ref_to_uninit]]-flavored pointer FIELD's
     read produces once the SSA_NAME branch above has peeled off its
     anonymous load temporary ('_1 = this_2(D)->elem; now_init (_1);').
     Checks the innermost FIELD_DECL's own [[ref_to_uninit]]/[[must_init]]
     attribute directly, exactly like the VAR_DECL/PARM_DECL fallback just
     below does for a plain variable -- this is the field's own declared
     POINTER FLAVOR, unrelated to the ADDR_EXPR (COMPONENT_REF) case above
     (which answers a different question: whether the OBJECT is still
     [[uninit]], DAA-based via ip_currently_uninit_p). Works for any
     field-chain depth ('this->mem.elem') for free, with no recursion:
     COMPONENT_REF's operand 1 is always the innermost field regardless of
     how deeply operand 0 itself nests ('a.b.c' is COMPONENT_REF
     (COMPONENT_REF (a, b), c) -- operand 1 is always 'c').  */
  if (TREE_CODE (arg) == COMPONENT_REF)
    {
      tree field = TREE_OPERAND (arg, 1);
      if (TREE_CODE (TREE_TYPE (field)) == POINTER_TYPE)
	return profiles_uninit_pointee_p (field);
      return false;
    }
  /* A bare VAR_DECL/PARM_DECL (not wrapped in an SSA_NAME at all) --
     the shape a memory-resident pointer variable's read produces, e.g.
     a global/namespace-scope variable (never is_gimple_reg regardless
     of its own scalar-ness, since its value can be observed/modified
     from outside this function's own CFG) or a local whose address is
     taken elsewhere in the function.  Confirmed directly: a [[ref_to_
     uninit]]-marked GLOBAL pointer passed as a call argument was
     silently treated as unflavored before this branch existed, purely
     because it happens to reach this function as 'src' itself rather
     than as 'SSA_NAME_VAR (src_N)'.  */
  if ((VAR_P (arg) || TREE_CODE (arg) == PARM_DECL)
      && TREE_CODE (TREE_TYPE (arg)) == POINTER_TYPE)
    return profiles_uninit_pointee_p (arg);
  return false;
}

/* True if ARG (a call-argument or plain-assignment RHS expression) is,
   provably, a null pointer constant -- possibly reached through the
   same SSA-copy-chasing ip_arg_uninit_flavored_p above already
   performs (confirmed via -fdump-tree-gimple: 'nullptr' itself lowers
   to a plain zero INTEGER_CST of pointer type, '0B', and a local
   'int* q = nullptr;' later passed as 'q' reaches this function as an
   SSA_NAME whose own reaching definition is exactly such a constant).
   A null pointer refers to no object at all, so it is compatible with
   EITHER flavor: it is not itself [[ref_to_uninit]]-flavored (there is
   no uninitialized memory it refers to), but it is also not the kind
   of "a pointer to already-initialized memory" value flavor
   consistency exists to keep out of a [[ref_to_uninit]] destination --
   there is no memory of any kind being smuggled in.  Used by both
   ip_check_call_flavor_consistency and ip_check_assign_flavor_
   consistency below as an early exemption from their own mismatch
   check, in either direction: 'take_uninit (nullptr);' and 'int* p
   [[ref_to_uninit]] = nullptr;' must both be accepted regardless of
   the destination's own flavor.  */

static bool
ip_arg_null_pointer_p (tree arg)
{
  if (TREE_CODE (arg) == INTEGER_CST)
    return integer_zerop (arg);
  if (TREE_CODE (arg) == SSA_NAME)
    {
      gimple *def = SSA_NAME_DEF_STMT (arg);
      if (def && is_gimple_assign (def) && gimple_assign_single_p (def))
	return ip_arg_null_pointer_p (gimple_assign_rhs1 (def));
    }
  return false;
}

/* True if ARG (a call-argument, plain-assignment RHS, or return-value
   expression, after STRIP_NOPS to see through an intervening cast) is
   a direct '&E'/'&E.field' address-of expression -- as opposed to a
   pre-existing pointer value (an SSA_NAME, however many hops of plain
   copies it's chased through) merely being passed along.  Used by the
   three flavor-consistency checks below to skip their own "refers to
   [[uninit]] memory but its parameter/destination is not marked
   [[ref_to_uninit]]" diagnostic for exactly this shape: the address-
   escape family (ip_check_address_taken_var/ip_check_local_aggregate_
   member/ip_check_constructor_member) already, unconditionally covers
   every such occurrence, with -- since both sides now share ip_
   currently_uninit_p/ip_all_fields_dominate_p -- a PROVABLY identical
   firing condition for this one shape: same entity, same "is it
   dominated by an initializing event" test, same anchor location.
   Reporting both would be pure duplication, not two independent
   findings.  This is NOT true for a pointer VALUE flowing in from
   elsewhere (a declared-[[ref_to_uninit]] local copied into an
   unflavored one, 'q = p;') -- the escape family only ever looks at
   literal address-of expressions, so it has nothing to say about that
   shape, and flavor-consistency remains the sole, necessary catch for
   it.  Deliberately a simple, cheap, top-level-only syntactic test --
   no need to thread anything through ip_arg_uninit_flavored_p_1's own
   recursion: whichever rule fired to make arg_flavor true is exactly
   as identifiable to the caller from ARG's own top-level shape here as
   it would be from inside that recursion, without needing to expose
   which internal rule matched.  */

static bool
ip_arg_is_direct_addr_expr_p (tree arg)
{
  STRIP_NOPS (arg);
  return TREE_CODE (arg) == ADDR_EXPR;
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
ip_check_call_flavor_consistency (gimple *stmt, tree enclosing_fndecl,
				   function *fun, ip_flavor_reach_cache *cache)
{
  if (gimple_code (stmt) != GIMPLE_CALL)
    return;
  tree callee = gimple_call_fndecl (stmt);
  if (!callee)
    return;

  unsigned nargs = gimple_call_num_args (stmt);
  for (unsigned i = 0; i < nargs; ++i)
    {
      tree arg = gimple_call_arg (stmt, i);
      if (ip_arg_null_pointer_p (arg))
	continue;
      /* std::construct_at's own first argument is deliberately exempt
	 from this check in EITHER direction: its real signature has no
	 [[ref_to_uninit]]/[[must_init]] of its own (see ip_construct_
	 at_call_p's own comment for why it can't), so a flavored
	 argument there is not a mismatch to report -- ip_scan_stmt_for_
	 var separately recognizes this exact call as initializing
	 whatever that argument traces back to.  */
      if (i == 0 && ip_construct_at_call_p (as_a<gcall *> (stmt)))
	continue;
      bool param_flavor
	= profiles_uninit_flavor_at_position_p (callee, i + 1,
						 /*must_init_only=*/false);
      bool arg_flavor = ip_arg_uninit_flavored_p (arg, fun, stmt, cache);

      if (profiles_diagnostic_exempt_p (gimple_location (stmt),
					enclosing_fndecl, "std::init"))
	continue;
      if (param_flavor && !arg_flavor)
	profiles_diagnostic_at (gimple_location (stmt), "std::init",
		  "argument %u to %qD must refer to %<[[uninit]]%> memory, "
		  "matching its %<[[ref_to_uninit]]%> parameter, under the "
		  "%<std::init%> profile", i + 1, callee);
      /* !param_flavor && arg_flavor, for a direct '&E'/'&E.field'
	 argument, is skipped here: ip_check_address_taken_var and
	 friends already, unconditionally, diagnose every such
	 occurrence -- see ip_arg_is_direct_addr_expr_p's own comment
	 for why the two are now provably testing the same condition
	 for this one shape, not just usually agreeing.  */
      else if (!param_flavor && arg_flavor
	       && !ip_arg_is_direct_addr_expr_p (arg))
	profiles_diagnostic_at (gimple_location (stmt), "std::init",
		  "argument %u to %qD refers to %<[[uninit]]%> memory but "
		  "its parameter is not marked %<[[ref_to_uninit]]%>, under "
		  "the %<std::init%> profile", i + 1, callee);
    }

  /* The RETURN-value counterpart of the argument loop above (P4222
     S4.3's "void* [[ref_to_uninit]] malloc(size_t);"-shaped case): when
     this call's result is assigned DIRECTLY into a named pointer (the
     call's own gimple_call_lhs, not a separate later statement), that
     destination's own flavor must match CALLEE's declared return
     flavor, bidirectionally, exactly like an argument/parameter pair.

     This was tried once before and REMOVED as apparently-unreachable
     dead code: for every explicitly-user-defined function tested (a
     toy 'my_malloc', reused/single-assign/discarded-result variants),
     a call's result is ALWAYS copied through an anonymous SSA temporary
     first ('_2 = fn (); dst = _2;'), which ip_check_assign_flavor_
     consistency alone already catches (ip_underlying_var returns
     NULL_TREE for the temp here, so LHS_VAR below is NULL and this
     block is a no-op for that far more common shape -- no double
     diagnostic). It turned out to be reachable after all: confirmed
     via direct -fdump-tree-ssa reading that for a function GCC
     recognizes as an actual BUILTIN by name+signature (e.g. a real
     'extern "C" void* malloc(size_t);' matching the true libc
     signature), the gimplifier emits the call's result DIRECTLY into
     the named destination ('p_3 = malloc (4);', no temp) -- exactly
     the shape this block exists for.  */
  tree lhs = gimple_call_lhs (stmt);
  tree lhs_var = lhs ? ip_underlying_var (lhs) : NULL_TREE;
  if (lhs_var && TREE_CODE (TREE_TYPE (lhs_var)) == POINTER_TYPE)
    {
      bool dst_flavor = profiles_uninit_pointee_p (lhs_var);
      /* std::now_uninit's own call is itself exactly this
	 direct-LHS shape (it's an always-inline template, so its own
	 call is never routed through an intermediate SSA temp either --
	 confirmed via -fdump-tree-ssa: 'p = std::now_uninit<void*>
	 (_1);' directly) -- correctly caught here with no special case,
	 since its own return carries [[ref_to_uninit]] directly
	 (<utility>'s own definition), same as any other flavored-
	 returning function CALLEE_FLAVOR already checks below.  */
      bool callee_flavor = profiles_uninit_pointee_p (callee);
      if (dst_flavor != callee_flavor
	  && !profiles_diagnostic_exempt_p (gimple_location (stmt),
					     enclosing_fndecl, "std::init"))
	{
	  if (callee_flavor)
	    profiles_diagnostic_at (gimple_location (stmt), "std::init",
		      "assigning a pointer marked %<[[ref_to_uninit]]%> into "
		      "a pointer not marked %<[[ref_to_uninit]]%>, under the "
		      "%<std::init%> profile");
	  else
	    profiles_diagnostic_at (gimple_location (stmt), "std::init",
		      "assigning a pointer not marked %<[[ref_to_uninit]]%> "
		      "into a pointer marked %<[[ref_to_uninit]]%>, under "
		      "the %<std::init%> profile");
	}
    }
}

/* Resolve T down to whatever VAR_DECL/PARM_DECL it ultimately traces
   back to through a chain of plain single-operand copies (the shape
   'return __p;' actually takes even for a single-statement function:
   confirmed via -fdump-tree-ssa that it still assigns __p into an
   anonymous SSA temporary first, '_2 = __p_1(D); return _2;', so a
   bare SSA_NAME_VAR lookup on the return value alone finds nothing).
   Used only by ip_check_return_flavor_consistency below, to recognize
   when a return value IS one of the enclosing function's own
   parameters, as opposed to ip_arg_uninit_flavored_p's own similar-
   looking recursion, which answers a different question (is this
   expression flavored) and must not be reused here for that reason --
   see that function's own call site below for exactly why.  */

static tree
ip_resolve_underlying_decl (tree t)
{
  if (TREE_CODE (t) == SSA_NAME)
    {
      tree var = SSA_NAME_VAR (t);
      if (var)
	return var;
      gimple *def = SSA_NAME_DEF_STMT (t);
      if (def && is_gimple_assign (def) && gimple_assign_single_p (def))
	return ip_resolve_underlying_decl (gimple_assign_rhs1 (def));
      return NULL_TREE;
    }
  if (VAR_P (t) || TREE_CODE (t) == PARM_DECL)
    return t;
  return NULL_TREE;
}

/* The RETURN-statement counterpart of the checks above (P4222 S4.3):
   a function declared [[ref_to_uninit]] on its own return must only
   ever return a flavored value, and conversely, an unflavored function
   must never return one, bidirectionally -- 'return malloc (n);' inside
   a '[[ref_to_uninit]] void* my_malloc (size_t);'-declared function,
   with an ordinary, unflavored 'malloc', was previously silently
   accepted: nothing examined a function's own GIMPLE_RETURN statements
   against its own declared flavor at all, only how CALLERS treat the
   result (ip_check_call_flavor_consistency/ip_check_assign_flavor_
   consistency above). No explicit "is the function's own return type a
   pointer" guard is needed: a non-pointer return makes ip_arg_uninit_
   flavored_p false, and profiles_uninit_pointee_p on a never-attributed
   function is false too, so both sides already agree with no mismatch
   -- the same reasoning ip_check_call_flavor_consistency's own comment
   already gives for skipping an analogous "is this a pointer parameter"
   guard.

   Confirmed via direct testing this needs one exemption: std::now_init
   ('_Tp* now_init(_Tp* __p [[must_init]]) { return __p; }') was flagged
   -- __p's own flavor makes ip_arg_uninit_flavored_p true, but now_init
   itself is correctly NOT marked [[ref_to_uninit]] on its own return
   (its entire purpose is a POSTCONDITION about __p itself, not a
   description of now_init's own return flavor; std::escape_uninit's
   near-identical shape has the same property). When a return value
   resolves directly to one of the ENCLOSING function's own parameters
   that already carries an explicit [[ref_to_uninit]]/[[must_init]]
   declaration, that parameter's own attribute is trusted completely
   and the function's own return-flavor is not second-guessed against
   it -- a direct, unmodified pass-through of an already-declared-
   flavored parameter is exactly the shape this profile's own escape-
   hatch functions are built from, not a mismatch to flag.  */

static void
ip_check_return_flavor_consistency (gimple *stmt, tree enclosing_fndecl,
				     function *fun,
				     ip_flavor_reach_cache *cache)
{
  if (gimple_code (stmt) != GIMPLE_RETURN)
    return;
  tree retval = gimple_return_retval (as_a<greturn *> (stmt));
  if (!retval || ip_arg_null_pointer_p (retval))
    return;
  tree retval_decl = ip_resolve_underlying_decl (retval);
  if (retval_decl && TREE_CODE (retval_decl) == PARM_DECL
      && profiles_uninit_pointee_p (retval_decl))
    return;
  bool fn_flavor = profiles_uninit_pointee_p (enclosing_fndecl);
  bool retval_flavor = ip_arg_uninit_flavored_p (retval, fun, stmt, cache);
  if (fn_flavor == retval_flavor)
    return;
  /* !fn_flavor && retval_flavor, for a direct '&E'/'&E.field' return
     value, is skipped here -- see ip_arg_is_direct_addr_expr_p's own
     comment: the address-escape family already, unconditionally,
     diagnoses every such occurrence, with a provably identical firing
     condition for this shape.  */
  if (!fn_flavor && retval_flavor && ip_arg_is_direct_addr_expr_p (retval))
    return;
  if (profiles_diagnostic_exempt_p (gimple_location (stmt),
				     enclosing_fndecl, "std::init"))
    return;
  if (retval_flavor)
    profiles_diagnostic_at (gimple_location (stmt), "std::init",
	      "returning a pointer marked %<[[ref_to_uninit]]%> from a "
	      "function not itself marked %<[[ref_to_uninit]]%>, under the "
	      "%<std::init%> profile");
  else
    profiles_diagnostic_at (gimple_location (stmt), "std::init",
	      "returning a pointer not marked %<[[ref_to_uninit]]%> from a "
	      "function marked %<[[ref_to_uninit]]%>, under the "
	      "%<std::init%> profile");
}

/* The plain-assignment counterpart of ip_check_call_flavor_
   consistency above, same bidirectional mismatch rule (S4.2), applied
   to an ordinary 'dst = src;' between two named pointer variables/
   parameters instead of a call argument/parameter pair. Covers a
   declaration's own initializer for free, with no special-casing:
   confirmed via direct -fdump-tree-gimple reading that 'T* q = p;'
   written as an initializer and 'q = p;' written as a later, separate
   assignment produce the identical GIMPLE_ASSIGN statement shape, so
   there is no "is this an initializer" distinction to make at this
   level in the first place. Also covers a cast ('(T*) src') exactly
   like a plain copy, for the same reason ip_arg_uninit_flavored_p
   already does: gimple_assign_rhs1 returns the actual operand
   regardless of whether the assignment's own rhs_code is a bare copy
   or a NOP_EXPR/CONVERT_EXPR wrapping it.

   Without this, a [[ref_to_uninit]]-flavored pointer's flavor could be
   silently discarded by one intervening copy into an unmarked pointer
   variable -- nothing would then distinguish that unmarked copy from
   an ordinary, definitely-safe pointer, so it could go on to be passed
   anywhere (including to a plain, unflavored parameter) with zero
   further checking, defeating the whole point of requiring the
   flavor to be explicitly declared in the first place.  */

static void
ip_check_assign_flavor_consistency (gimple *stmt, tree enclosing_fndecl,
				     function *fun,
				     ip_flavor_reach_cache *cache)
{
  if (!is_gimple_assign (stmt) || !gimple_assign_single_p (stmt))
    return;
  tree lhs = gimple_assign_lhs (stmt);
  if (TREE_CODE (TREE_TYPE (lhs)) != POINTER_TYPE)
    return;
  tree lhs_var = ip_underlying_var (lhs);
  if (!lhs_var)
    return;
  tree rhs = gimple_assign_rhs1 (stmt);
  if (ip_arg_null_pointer_p (rhs))
    return;

  bool dst_flavor = profiles_uninit_pointee_p (lhs_var);
  bool src_flavor = ip_arg_uninit_flavored_p (rhs, fun, stmt, cache);

  if (dst_flavor == src_flavor)
    return;
  /* !dst_flavor && src_flavor, for a direct '&E'/'&E.field' RHS, is
     skipped here -- see ip_arg_is_direct_addr_expr_p's own comment:
     the address-escape family already, unconditionally, diagnoses
     every such occurrence, with a provably identical firing condition
     for this shape.  */
  if (!dst_flavor && src_flavor && ip_arg_is_direct_addr_expr_p (rhs))
    return;
  if (profiles_diagnostic_exempt_p (gimple_location (stmt),
				     enclosing_fndecl, "std::init"))
    return;
  if (src_flavor)
    profiles_diagnostic_at (gimple_location (stmt), "std::init",
	      "assigning a pointer marked %<[[ref_to_uninit]]%> into a "
	      "pointer not marked %<[[ref_to_uninit]]%>, under the "
	      "%<std::init%> profile");
  else
    profiles_diagnostic_at (gimple_location (stmt), "std::init",
	      "assigning a pointer not marked %<[[ref_to_uninit]]%> into a "
	      "pointer marked %<[[ref_to_uninit]]%>, under the %<std::init%> "
	      "profile");
}

static unsigned int
ip_check_function (function *fun)
{
  /* One cache per function, shared across every statement's own
     flavor-consistency check -- a given [[uninit]] variable/field's
     own init_stmts/reach info don't change mid-pass, so this is
     computed at most once per (base, field) pair actually queried,
     not once per query.  */
  ip_flavor_reach_cache flavor_cache;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	ip_check_call_flavor_consistency (gsi_stmt (gsi), fun->decl, fun,
					   &flavor_cache);
	ip_check_assign_flavor_consistency (gsi_stmt (gsi), fun->decl, fun,
					     &flavor_cache);
	ip_check_return_flavor_consistency (gsi_stmt (gsi), fun->decl, fun,
					     &flavor_cache);
      }

  unsigned i;
  tree var;
  FOR_EACH_LOCAL_DECL (fun, i, var)
    {
      if (!VAR_P (var) || !lookup_attribute ("uninit", DECL_ATTRIBUTES (var)))
	continue;

      if (!is_gimple_reg (var))
	{
	  ip_check_address_taken_var (fun, var);
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
	      if (!ip_definitely_assigned_p (name, in_progress)
		  && !profiles_diagnostic_exempt_p (gimple_location (use_stmt),
						    fun->decl, "std::init"))
		profiles_diagnostic_at (gimple_location (use_stmt), "std::init",
			  "%qD read before it is definitely assigned, "
			  "under the %<std::init%> profile", var);
	    }
	}
    }

  /* P4222 Phase 4d (S5.1-S5.3): for a constructor specifically, check
     every [[uninit]]/[[ref_to_uninit]]-marked member of the class it
     constructs -- 'this' is always DECL_ARGUMENTS (fun->decl) itself
     (the first entry grokfndecl inserts for any non-static member
     function), safe to read directly here (unlike a CALLEE's
     DECL_ARGUMENTS elsewhere in this file, FUN is the function this
     pass is currently compiling, which always has a real body).  */
  if (DECL_CONSTRUCTOR_P (fun->decl))
    {
      tree this_parm = DECL_ARGUMENTS (fun->decl);
      tree class_type = TREE_TYPE (TREE_TYPE (this_parm));
      for (tree field = TYPE_FIELDS (class_type); field;
	   field = DECL_CHAIN (field))
	{
	  if (TREE_CODE (field) != FIELD_DECL || DECL_ARTIFICIAL (field))
	    continue;
	  if (!lookup_attribute ("uninit", DECL_ATTRIBUTES (field))
	      && !profiles_uninit_pointee_p (field))
	    continue;
	  ip_check_constructor_member (fun, this_parm, field);
	}
    }

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
    return profiles_active_p ("std::init");
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
