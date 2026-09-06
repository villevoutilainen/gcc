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
   mutable iterator.  ip_collect_mutations also classifies a plain
   (non-member) function call as mutating any argument bound to a
   reference-to-non-const or pointer-to-non-const class-typed
   parameter, not marked [[not_invalidating]] AT THAT PARAMETER
   POSITION (the CppCon 2026 "Profiles" talk's own slide 53:
   "a function is assumed to invalidate a non-const argument")
   -- a distinct annotation position from the member-function case
   above, tracked via a synthesized "profiles_not_invalidating_flavor"
   marker (decl.cc's grokfndecl, profiles.cc's own profiles_not_
   invalidating_at_position_p), the same reason [[must_init]]/
   [[ref_to_uninit]] need their own analogous marker.  A virtual
   (indirectly-dispatched) mutating call is not classified as mutating
   either way, since gimple_call_fndecl returns NULL_TREE for those --
   a known, documented scope limit for this increment, not a silent
   gap.

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
#include "hash-set.h"

/* True if VAR (an operand of USE_STMT) is a class/union-typed or
   raw-pointer-typed VAR_DECL or PARM_DECL worth checking at all --
   excludes the LHS of USE_STMT's own definition (that is a write, not
   a read) and anything not RECORD_TYPE/UNION_TYPE/POINTER_TYPE.
   POINTER_TYPE was added alongside RECORD_TYPE/UNION_TYPE once
   Phase 7a's own blanket dereference ban (typeck.cc's cp_build_
   indirect_ref_1) was removed in favor of this file's own mutation
   tracking covering a raw pointer the same way it already covers a
   class-typed iterator/handle.  */

static bool
ip_trackable_operand_p (tree var)
{
  if (TREE_CODE (var) != VAR_DECL && TREE_CODE (var) != PARM_DECL)
    return false;
  tree type = TREE_TYPE (var);
  return TREE_CODE (type) == RECORD_TYPE || TREE_CODE (type) == UNION_TYPE
	 || TREE_CODE (type) == POINTER_TYPE;
}

/* If T is, or (through SSA_NAME_VAR) resolves to, a trackable
   VAR_DECL/PARM_DECL (ip_trackable_operand_p), return that decl; else
   NULL_TREE.  Needed because a raw pointer local, unlike a class-typed
   one, almost always IS an SSA_NAME by this point (is_gimple_reg is
   true for any non-aggregate register variable, see this file's own
   top comment) -- every place below that used to compare a GIMPLE
   operand directly against a plain VAR_DECL/PARM_DECL now goes through
   this first, so a pointer's own SSA versioning doesn't hide it from
   that comparison.  */

static tree
ip_trackable_decl (tree t)
{
  if (!t)
    return NULL_TREE;
  if (TREE_CODE (t) == SSA_NAME)
    t = SSA_NAME_VAR (t);
  return (t && ip_trackable_operand_p (t)) ? t : NULL_TREE;
}

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

/* True if DECL_A and DECL_B (already confirmed != each other) are
   provably distinct OBJECTS, independent of what type they happen to
   share -- true whenever BOTH are ordinary local/static/global
   variables (VAR_DECL, never PARM_DECL): each such declaration gets
   its own storage, entirely independent of every other declaration,
   so two DIFFERENT VAR_DECLs can never be the same object regardless
   of type (barring non-standard linker-level aliasing tricks this
   checker, like the rest of this project, does not attempt to defend
   against).  This is a DIFFERENT, and strictly more direct, question
   than ip_types_provably_unrelated_p's own template-family comparison
   answers: that one exists specifically because a REFERENCE/POINTER
   PARAMETER's own underlying object identity is supplied by the
   CALLER and so genuinely could be the same object behind two
   different parameter names in the SAME call (e.g.
   'f(vector<int> &a, vector<int> &b)' called as 'f(vi, vi)') -- a
   concern that simply does not apply to two locals this function
   itself declared, which can never alias each other purely by being
   declared.  ip_receiver_decl only ever returns a PARM_DECL directly,
   or (via its own ADDR_EXPR branch) the VAR_DECL an address-of
   expression names -- always the ultimate object itself, never a
   reference variable -- so no separate REFERENCE_TYPE exclusion is
   needed on top of VAR_P here.  A PARM_DECL on either side keeps the
   conservative "not proven" default from ip_types_provably_
   unrelated_p alone, since its own identity is not under this
   function's control.  */

static bool
ip_decls_provably_distinct_objects_p (tree decl_a, tree decl_b)
{
  return VAR_P (decl_a) && VAR_P (decl_b);
}

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

/* Rule #1 support: true if RETURN_TYPE is a shape that could possibly
   reference/alias its receiver's own state -- a pointer, a reference,
   or an object of class type (RECORD_TYPE/UNION_TYPE).  This is
   deliberately NOT trying to structurally prove "this really is an
   iterator/handle associated with its receiver" (an earlier version of
   this function attempted exactly that, via template-argument-sharing
   comparisons -- abandoned after direct testing showed it fails for
   libstdc++'s own real iterators: __normal_iterator<_Iterator,
   _Container>, e.g. vector<int>::iterator = __normal_iterator<int*,
   vector<int>>, is parameterized on the CONTAINER TYPE ITSELF as its
   second argument, not a shared element type the way the toy
   iterator<T>/ToyListIterator<T> templates used elsewhere in this
   project's own tests are -- silently failing to establish ANY binding
   for real standard-library iterators is a far worse failure mode than
   over-approximating).  The general, deliberately broad assumption
   this checker takes instead: ANY pointer, reference, or class-typed
   return value from a non-const-receiver member call is assumed
   POSSIBLY bound to the receiver, the same "default deny" stance
   P4296R0 already takes everywhere else in this file -- a scalar
   return (int, bool, an enum, ...) cannot reference anything and is
   the only shape excluded.  Provably narrowing this back down for
   specific, structurally-recognizable safe shapes (the same way Rule
   #0/#1 already narrow the blanket "assumed invalidating" default back
   down elsewhere) is future work, not attempted here.  */

static bool
ip_call_result_may_reference_receiver_p (tree return_type)
{
  return TREE_CODE (return_type) == POINTER_TYPE
	 || TREE_CODE (return_type) == REFERENCE_TYPE
	 || TREE_CODE (return_type) == RECORD_TYPE
	 || TREE_CODE (return_type) == UNION_TYPE;
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

/* True if CALL is a call to a std::-namespace function named NAME --
   shared name-based recognition for this profile's manual escape
   hatches (std::no_dangling, std::now_valid), which have no distinct
   attribute of their own to key off (unlike std::init's [[must_init]]/
   [[ref_to_uninit]]-parameter-attribute pair) since what they assert
   isn't associated with any one parameter position.  */

static bool
ip_std_call_named_p (gcall *call, const char *name)
{
  tree fndecl = gimple_call_fndecl (call);
  if (!fndecl || !decl_in_std_namespace_p (fndecl))
    return false;
  tree id = DECL_NAME (fndecl);
  return id && id_equal (id, name);
}

/* True if CALL is a call to std::now_valid -- the invalidation
   profile's manual "forcibly (re)validate this object" assertion (see
   <utility>'s own definition): recognized by ip_defines_var_p below as
   a fresh (re-)establishment of its own argument's binding, and by
   ip_binding_established_by further down as inheriting whatever that
   argument was already bound to as of just before this call.  */

static bool
ip_now_valid_call_p (gcall *call)
{
  return ip_std_call_named_p (call, "now_valid");
}

/* True if STMT is a definition (write) of VAR -- a GIMPLE_CALL or
   GIMPLE_ASSIGN whose own LHS is exactly VAR (through ip_trackable_
   decl's own SSA_NAME_VAR unwrap, since a raw pointer's LHS is
   normally an SSA name, not VAR itself, post-SSA), a constructor
   call whose own "this" (first) argument is &VAR (a constructor
   returns void and writes through its first argument instead of an
   ordinary LHS), or a std::now_valid call whose own argument is VAR
   (recognized via its ARGUMENT, not its call-LHS: a reference-
   returning call's LHS, if any, is a temporary holding the returned
   reference, never VAR itself -- confirmed this is the only shape
   that lets a manual revalidation of VAR register as a fresh write to
   VAR without also needing 'VAR = std::now_valid (VAR);' at every call
   site, since ip_check_operand_uses only ever asks "what is the
   nearest write to VAR", never "what did this specific statement
   assign to its LHS").  */

static bool
ip_defines_var_p (gimple *stmt, tree var)
{
  if (gimple_code (stmt) == GIMPLE_CALL)
    {
      gcall *call = as_a<gcall *> (stmt);
      if (ip_trackable_decl (gimple_call_lhs (call)) == var)
	return true;
      tree fndecl = gimple_call_fndecl (call);
      if (fndecl && DECL_CONSTRUCTOR_P (fndecl) && gimple_call_num_args (call) >= 1)
	{
	  tree this_arg = gimple_call_arg (call, 0);
	  return TREE_CODE (this_arg) == ADDR_EXPR
		 && TREE_OPERAND (this_arg, 0) == var;
	}
      if (ip_now_valid_call_p (call) && gimple_call_num_args (call) >= 1)
	return ip_receiver_decl (gimple_call_arg (call, 0)) == var;
      return false;
    }
  if (is_gimple_assign (stmt))
    return ip_trackable_decl (gimple_assign_lhs (stmt)) == var;
  return false;
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

/* ---- Pointer/container escape-from-scope analysis (CppCon 2026
   "Profiles" talk, slides 45-49) ----

   A function must not return a pointer to one of its own locals, nor
   a container (a class/struct that might hold pointers) built from
   one -- and, since this checker never reads a callee's own body
   (the same standing rule the rest of this project follows), a
   method call on a container that was itself built from a pointer to
   a local is conservatively flagged too, even though the specific
   field the call's result depends on is unknown (the WidgetFactory/
   Logger example, slides 47-48) -- unless wrapped in
   std::no_dangling(), a manual, unproven assertion (<utility>'s own
   definition) exactly analogous to std::now_init() for the
   initialization profile.

   ip_escapes_locally_p/ip_call_escapes_locally_p/ip_var_contents_
   escape_locally_p are mutually recursive over: (a) SSA copies/PHIs,
   handled the ordinary way; (b) a class-typed local's own nearest
   reaching write (ip_nearest_write_before, the same technique Rule #1
   above uses); (c) a call's own arguments -- with one necessary
   special case: the RECEIVER of an ordinary (non-constructor) member
   call is not itself flagged merely for being a local variable's
   address (calling a method on a local object is completely
   ordinary), but its own CONTENTS are recursed into instead, since
   those are what could actually hold a risky pointer.  A constructor
   call's own "this" argument is skipped outright (it names the
   object being initialized, not an incoming value).  Anything this
   analysis cannot trace at all is conservatively treated as
   escaping -- default-deny, the same stance as every other check in
   this file.  */

static bool ip_escapes_locally_p (tree expr, gimple *point, int depth);
static bool ip_var_contents_escape_locally_p (tree var, gimple *point,
					       int depth);

/* True if DECL has automatic storage duration in the CURRENT
   function -- the only kind of variable whose address cannot safely
   escape it (CppCon 2026 talk, slide 38: "for an object on the stack
   or for a static object, the owner is itself" -- but stack self-
   ownership ends when the function returns).  is_global_var already
   answers true for a function-local 'static', which is exactly the
   "static object" case that must NOT be treated as escaping.  */

static bool
ip_local_var_p (tree decl)
{
  return VAR_P (decl) && !is_global_var (decl);
}

/* True if CALL is a call to std::no_dangling -- the invalidation
   profile's manual, unproven "this doesn't dangle" assertion (see
   <utility>'s own definition).  */

static bool
ip_no_dangling_call_p (gcall *call)
{
  return ip_std_call_named_p (call, "no_dangling");
}

/* True if any of CALL's arguments resolves to something that would
   dangle if a value derived from it escaped the current function --
   shared by both "is this call's own return value unsafe" and "was
   this local variable's contents built from anything unsafe" (see
   this section's own top comment for the receiver/constructor special
   cases).  FNDECL is CALL's callee, already known non-NULL by every
   caller.  */

static bool
ip_call_args_escape_locally_p (gcall *call, tree fndecl, int depth)
{
  bool is_ctor = DECL_CONSTRUCTOR_P (fndecl);
  bool is_member = DECL_IOBJ_MEMBER_FUNCTION_P (fndecl)
		   && gimple_call_num_args (call) >= 1;

  for (unsigned i = 0; i < gimple_call_num_args (call); ++i)
    {
      if (i == 0 && is_ctor)
	continue; /* The object being initialized, not an incoming value.  */

      tree arg = gimple_call_arg (call, i);
      if (i == 0 && is_member && !is_ctor)
	{
	  tree stripped = arg;
	  STRIP_NOPS (stripped);
	  if (TREE_CODE (stripped) == ADDR_EXPR
	      && VAR_P (TREE_OPERAND (stripped, 0)))
	    {
	      if (ip_var_contents_escape_locally_p
		    (TREE_OPERAND (stripped, 0), call, depth + 1))
		return true;
	      continue;
	    }
	}
      if (ip_escapes_locally_p (arg, call, depth + 1))
	return true;
    }
  return false;
}

/* True if CALL's own return value would dangle if returned/stored
   past the current function's end.  */

static bool
ip_call_escapes_locally_p (gcall *call, int depth)
{
  if (depth > 16)
    return true; /* Defensive recursion guard; never expected to trigger.  */
  if (ip_no_dangling_call_p (call))
    return false;
  tree fndecl = gimple_call_fndecl (call);
  if (!fndecl)
    return true; /* Indirectly-dispatched call: can't see its arguments
		    at all -- conservative default-deny, the same known,
		    documented scope limit as Rule #0/#1's own.  */
  return ip_call_args_escape_locally_p (call, fndecl, depth);
}

/* Collect, into *OUT, the RHS of every "VAR.field = rhs"-shaped
   assignment reaching POINT -- the field-by-field aggregate-
   initialization counterpart of ip_nearest_write_before's own
   whole-object write search, needed because a class-typed return
   value or local is very often populated field-by-field (e.g. brace
   initialization, "Widget{}") rather than via one single whole-object
   call or copy ip_defines_var_p can see.  Same same-block-then-
   dominator-chain technique as ip_nearest_write_before, but collects
   every matching write found rather than stopping at the first,
   since more than one field may need checking.  */

static void
ip_collect_component_writes_before (tree var, gimple *point, vec<tree> *out)
{
  basic_block bb = gimple_bb (point);
  for (gimple_stmt_iterator gsi = gsi_for_stmt (point); !gsi_end_p (gsi);)
    {
      gsi_prev (&gsi);
      if (gsi_end_p (gsi))
	break;
      gimple *s = gsi_stmt (gsi);
      if (is_gimple_assign (s) && gimple_assign_single_p (s))
	{
	  tree lhs = gimple_assign_lhs (s);
	  if (TREE_CODE (lhs) == COMPONENT_REF && TREE_OPERAND (lhs, 0) == var)
	    out->safe_push (gimple_assign_rhs1 (s));
	}
    }
  for (basic_block d = get_immediate_dominator (CDI_DOMINATORS, bb); d;
       d = get_immediate_dominator (CDI_DOMINATORS, d))
    for (gimple_stmt_iterator gsi = gsi_start_bb (d); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *s = gsi_stmt (gsi);
	if (is_gimple_assign (s) && gimple_assign_single_p (s))
	  {
	    tree lhs = gimple_assign_lhs (s);
	    if (TREE_CODE (lhs) == COMPONENT_REF && TREE_OPERAND (lhs, 0) == var)
	      out->safe_push (gimple_assign_rhs1 (s));
	  }
      }
}

/* True if TYPE could, structurally, hold a pointer/reference
   anywhere within it -- P3446R0's own definition of "container"
   (S6.2: "any class/struct with a raw pointer or another container
   within"), checked recursively through embedded class-typed fields,
   with VISITED guarding against infinite recursion through a
   self-referential or mutually-recursive type.  A class with no such
   field at all (VISITED's own base case, and the common case for a
   small "handle" class in this checker's own worked examples) simply
   cannot leak a dangling pointer no matter how it was built, so there
   is nothing for ip_var_contents_escape_locally_p to trace -- this is
   what actually distinguishes "provably fine, nothing to check" from
   "unprovable, conservatively dangles".  */

static bool
ip_type_may_hold_pointer_p (tree type, hash_set<tree> *visited)
{
  type = TYPE_MAIN_VARIANT (type);
  if (TREE_CODE (type) == POINTER_TYPE || TREE_CODE (type) == REFERENCE_TYPE)
    return true;
  if (TREE_CODE (type) == ARRAY_TYPE)
    return ip_type_may_hold_pointer_p (TREE_TYPE (type), visited);
  if (TREE_CODE (type) != RECORD_TYPE && TREE_CODE (type) != UNION_TYPE)
    return false;
  if (visited->add (type))
    return false; /* Already visited (or currently being visited): no NEW
		      pointer-shaped field found via this cycle.  */
  for (tree f = TYPE_FIELDS (type); f; f = TREE_CHAIN (f))
    if (TREE_CODE (f) == FIELD_DECL
	&& ip_type_may_hold_pointer_p (TREE_TYPE (f), visited))
      return true;
  return false;
}

/* True if VAR (a class-typed local -- never itself an SSA name, see
   this file's own top comment) was, as of its nearest reaching write
   before POINT, built from anything that would dangle if it escaped.  */

static bool
ip_var_contents_escape_locally_p (tree var, gimple *point, int depth)
{
  if (depth > 16)
    return true;
  {
    hash_set<tree> visited;
    if (!ip_type_may_hold_pointer_p (TREE_TYPE (var), &visited))
      return false; /* Structurally cannot hold a pointer -- nothing to trace.  */
  }
  gimple *reaching = ip_nearest_write_before (var, point);
  if (reaching)
    {
      if (gimple_code (reaching) == GIMPLE_CALL)
	{
	  gcall *call = as_a<gcall *> (reaching);
	  if (ip_no_dangling_call_p (call))
	    return false;
	  tree fndecl = gimple_call_fndecl (call);
	  if (!fndecl)
	    return true;
	  return ip_call_args_escape_locally_p (call, fndecl, depth + 1);
	}
      if (is_gimple_assign (reaching) && gimple_assign_single_p (reaching))
	return ip_escapes_locally_p (gimple_assign_rhs1 (reaching), reaching,
				      depth + 1);
      return true;
    }

  auto_vec<tree> field_values;
  ip_collect_component_writes_before (var, point, &field_values);
  if (field_values.is_empty ())
    return true; /* Truly nothing found -- conservative default-deny.  */
  for (unsigned i = 0; i < field_values.length (); ++i)
    if (ip_escapes_locally_p (field_values[i], point, depth + 1))
      return true;
  return false;
}

/* True if EXPR, evaluated at POINT, would dangle if it (or a value
   derived from it) escaped the current function by being returned.  */

static bool
ip_escapes_locally_p (tree expr, gimple *point, int depth)
{
  if (depth > 16)
    return true;
  STRIP_NOPS (expr);

  if (CONSTANT_CLASS_P (expr))
    return false; /* A literal (e.g. a null-pointer constant) never dangles.  */

  if (TREE_CODE (expr) == CONSTRUCTOR)
    {
      /* Whole-object zero/aggregate initialization ("Widget{}"): check
	 each initialized element's own value (an empty CONSTRUCTOR, the
	 common case, trivially has none).  */
      unsigned HOST_WIDE_INT i;
      tree val;
      FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (expr), i, val)
	if (val && ip_escapes_locally_p (val, point, depth + 1))
	  return true;
      return false;
    }

  if (TREE_CODE (expr) == ADDR_EXPR)
    {
      tree base = TREE_OPERAND (expr, 0);
      return VAR_P (base) && ip_local_var_p (base);
    }
  if (TREE_CODE (expr) == POINTER_PLUS_EXPR)
    /* Pointer arithmetic ('result + n', the common
       '++result'-in-a-loop shape libstdc++'s own
       __uninitialized_copy_a returns -- confirmed directly by reading
       its source, bits/stl_uninitialized.h) never changes WHETHER the
       pointer traces back to a local, only where within the same
       storage it points -- so this inherits its base operand's own
       answer exactly, ignoring the offset (operand 1) entirely.  */
    return ip_escapes_locally_p (TREE_OPERAND (expr, 0), point, depth + 1);
  if (TREE_CODE (expr) == PARM_DECL)
    return false;
  if (TREE_CODE (expr) == SSA_NAME)
    {
      if (SSA_NAME_IS_DEFAULT_DEF (expr))
	return false; /* A parameter's own default-def; never &local.  */
      gimple *def = SSA_NAME_DEF_STMT (expr);
      if (gimple_code (def) == GIMPLE_PHI)
	{
	  gphi *phi = as_a<gphi *> (def);
	  for (unsigned i = 0; i < gimple_phi_num_args (phi); ++i)
	    if (ip_escapes_locally_p (gimple_phi_arg_def (phi, i), point,
				       depth + 1))
	      return true;
	  return false;
	}
      if (is_gimple_assign (def) && gimple_assign_single_p (def))
	return ip_escapes_locally_p (gimple_assign_rhs1 (def), point, depth + 1);
      if (gimple_code (def) == GIMPLE_CALL)
	return ip_call_escapes_locally_p (as_a<gcall *> (def), depth + 1);
      return false; /* Some other computed value -- not itself a pointer.  */
    }
  if (VAR_P (expr))
    {
      if (is_global_var (expr))
	return false;
      return ip_var_contents_escape_locally_p (expr, point, depth + 1);
    }
  return true; /* Unrecognized shape: conservative default-deny.  */
}

/* True if S is a "VAR ={v} {CLOBBER(...)}" end-of-storage marker for
   a local VAR_DECL of TYPE (ignoring top-level qualifiers).  */

static bool
ip_clobber_of_type_p (gimple *s, tree type)
{
  if (!is_gimple_assign (s) || !gimple_clobber_p (s))
    return false;
  tree lhs = gimple_assign_lhs (s);
  return VAR_P (lhs)
	 && same_type_ignoring_top_level_qualifiers_p (TREE_TYPE (lhs), type);
}

/* RETVAL is a bare RESULT_DECL ("<retval>") -- Named Return Value
   optimization has elided the copy from some local variable entirely
   (there is no "<retval> = result;" statement anywhere to trace via
   ip_nearest_write_before, confirmed via a direct -fdump-tree-ssa-
   details reading, not assumed).  NRV requires there be exactly one
   eligible local candidate, so the local VAR_DECL whose own end-of-
   storage clobber is the nearest one preceding POINT is, in practice,
   that variable: every local's storage ends with exactly such a
   clobber at scope exit, and it is the last thing to happen before
   the return for the one NRV actually elided.  Returns NULL_TREE if
   no such clobber can be found at all (the safe, honestly-inconclusive
   answer -- ip_escapes_locally_p's own final "unrecognized shape"
   fallback still applies to the bare RESULT_DECL in that case).  */

static tree
ip_resolve_nrv_var (tree retval, gimple *point)
{
  basic_block bb = gimple_bb (point);
  for (gimple_stmt_iterator gsi = gsi_for_stmt (point); !gsi_end_p (gsi);)
    {
      gsi_prev (&gsi);
      if (gsi_end_p (gsi))
	break;
      gimple *s = gsi_stmt (gsi);
      if (ip_clobber_of_type_p (s, TREE_TYPE (retval)))
	return gimple_assign_lhs (s);
    }
  for (basic_block d = get_immediate_dominator (CDI_DOMINATORS, bb); d;
       d = get_immediate_dominator (CDI_DOMINATORS, d))
    for (gimple_stmt_iterator gsi = gsi_start_bb (d); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      if (ip_clobber_of_type_p (gsi_stmt (gsi), TREE_TYPE (retval)))
	return gimple_assign_lhs (gsi_stmt (gsi));
  return NULL_TREE;
}

/* Check a single RETURN_STMT (a GIMPLE_RETURN whose return type this
   file's caller has already confirmed is worth checking): emit a
   diagnostic, unless header-exempted, if the returned value would
   dangle per ip_escapes_locally_p.

   Known, discovered (not assumed) limitation: a return statement that
   directly and unconditionally returns "&local" with no other use is
   already replaced with a null-pointer constant during
   gimplification itself, well before any GIMPLE pass -- including
   this one -- ever runs; that exact shape is already diagnosed by
   GCC's own pre-existing -Wreturn-local-addr warning instead
   (confirmed via a direct -fdump-tree-ssa-details reading, not
   assumed: the SSA dump for such a function already shows "_N = 0B;"
   at the very first GIMPLE dump point).  Every other shape this
   checker cares about -- a conditional return, a pointer threaded
   through an intermediate variable/call, and critically the
   container-escape case (ip_var_contents_escape_locally_p) -- is
   unaffected and still reaches this function with the real
   expression intact.  */

static void
ip_check_return_escape (gimple *return_stmt, tree enclosing_fndecl)
{
  tree retval = gimple_return_retval (as_a<greturn *> (return_stmt));
  if (!retval)
    return;
  if (TREE_CODE (retval) == RESULT_DECL)
    if (tree nrv_var = ip_resolve_nrv_var (retval, return_stmt))
      retval = nrv_var;
  if (!ip_escapes_locally_p (retval, return_stmt, 0))
    return;
  if (profiles_diagnostic_exempt_p (gimple_location (return_stmt),
				    enclosing_fndecl, "std::invalidation"))
    return;
  error_at (gimple_location (return_stmt),
	    "returning a pointer or container that may hold a pointer "
	    "to a local, not permitted under the %<std::invalidation%> "
	    "profile (wrap in %<std::no_dangling%> if this is provably "
	    "safe)");
}

/* Resolve RHS -- either the whole RHS of a single-copy assignment, or
   a POINTER_PLUS_EXPR's own base operand (the offset itself never
   matters: 'base + n' traces back to whatever 'base' does, just at a
   different position within the same storage) -- to whichever
   statement actually defines it, or NULL if that can't be done: a
   pure anonymous SSA temporary (no home VAR_DECL at all -- e.g. a raw
   pointer's own 'begin()'/'data()' return value, confirmed via direct
   -fdump-tree-ssa reading: unlike a class-typed return, which
   mandatory copy elision constructs directly into the named local, a
   POINTER return's value commonly lives in a plain SSA temporary the
   gimplifier never names) resolves directly via its own SSA_NAME_
   DEF_STMT, since SSA form already gives the unique reaching
   definition with no CFG walk needed or even possible; anything else
   ip_trackable_decl resolves to a real VAR_DECL/PARM_DECL (through its
   own SSA_NAME_VAR unwrap) is looked up via ip_nearest_write_before
   instead, since a real declaration needs the same CFG-dominance walk
   every other tracked binding does.  Shared by ip_binding_established_
   by and ip_originating_call below, which differ only in what they do
   with the statement this resolves to.  */

static gimple *
ip_resolve_defining_stmt (tree rhs, gimple *point)
{
  if (TREE_CODE (rhs) == SSA_NAME && !SSA_NAME_VAR (rhs))
    return SSA_NAME_DEF_STMT (rhs);
  tree decl = ip_trackable_decl (rhs);
  return decl ? ip_nearest_write_before (decl, point) : NULL;
}

/* The container declaration DEF_STMT's own effect binds its LHS to
   (P4296R0 S7.6.2's "proven binding"), or NULL_TREE if this checker
   cannot establish one: a std::now_valid call inherits whatever
   binding its own argument was ALREADY bound to, as of the nearest
   write to that argument strictly before this call (deliberately
   re-deriving the SAME, unchanged binding -- the point of this branch
   isn't to change what the argument is bound to, only to let
   ip_check_operand_uses's own caller-side ip_nearest_write_before see
   THIS call as the argument's own current establishing statement, so
   only a mutation strictly after it counts against future uses); a
   member call whose return value could possibly reference its
   receiver's own state (ip_call_result_may_reference_receiver_p) binds
   to that receiver (ip_receiver_decl); a plain copy, or pointer
   arithmetic on one ('base + n', the common 'vec.data() + n' shape --
   confirmed directly: without this, the pointer-arithmetic assignment
   this lowers to is neither a GIMPLE_CALL nor a single-operand copy, so
   binding establishment gave up immediately and never even recursed
   into 'vec.data()' itself), inherits whatever binding the nearest
   reaching write to the base declaration, as of DEF_STMT's own
   position, itself establishes -- recursing via ip_resolve_defining_
   stmt/ip_nearest_write_before, which are always called on a strictly
   earlier statement than this function's own DEF_STMT, so this
   recursion is well-founded (no cycle-guard is needed the way
   contracts-gimple.cc's PHI recursion needs one: there is no PHI node
   here to create a cycle through).  */

static tree
ip_binding_established_by (gimple *def_stmt)
{
  if (gimple_code (def_stmt) == GIMPLE_CALL)
    {
      gcall *call = as_a<gcall *> (def_stmt);
      if (ip_now_valid_call_p (call) && gimple_call_num_args (call) >= 1)
	{
	  tree var = ip_receiver_decl (gimple_call_arg (call, 0));
	  if (!var)
	    return NULL_TREE;
	  gimple *reaching = ip_nearest_write_before (var, def_stmt);
	  return reaching ? ip_binding_established_by (reaching) : NULL_TREE;
	}
      tree fndecl = gimple_call_fndecl (call);
      tree lhs = gimple_call_lhs (call);
      if (!fndecl || !lhs || !DECL_IOBJ_MEMBER_FUNCTION_P (fndecl)
	  || gimple_call_num_args (call) < 1)
	return NULL_TREE;
      if (!ip_call_result_may_reference_receiver_p (TREE_TYPE (lhs)))
	return NULL_TREE;
      return ip_receiver_decl (gimple_call_arg (call, 0));
    }
  if (is_gimple_assign (def_stmt)
      && gimple_assign_rhs_code (def_stmt) == POINTER_PLUS_EXPR)
    {
      gimple *reaching = ip_resolve_defining_stmt (gimple_assign_rhs1 (def_stmt),
						    def_stmt);
      return reaching ? ip_binding_established_by (reaching) : NULL_TREE;
    }
  if (is_gimple_assign (def_stmt) && gimple_assign_single_p (def_stmt))
    {
      gimple *reaching = ip_resolve_defining_stmt (gimple_assign_rhs1 (def_stmt),
						    def_stmt);
      return reaching ? ip_binding_established_by (reaching) : NULL_TREE;
    }
  return NULL_TREE;
}

/* The same chain of copies/pointer-arithmetic/anonymous-SSA-temp hops
   ip_binding_established_by walks (via the identical ip_resolve_
   defining_stmt helper), but returning the ultimate GIMPLE_CALL it
   traces back to (or NULL if that walk wouldn't establish a binding at
   all) instead of what that call binds its result to.  Used so a
   value's own establishing call is never also counted as a mutation
   that invalidates that SAME value: a call to a non-const, unannotated
   accessor like 'begin()' is, by ip_collect_mutations's own "assumed
   invalidating" default, itself a mutation of its receiver -- but the
   fresh iterator/pointer it just returned cannot have been invalidated
   by that same call's own side effect, only a DIFFERENT, earlier-bound
   value could be.  Mirrors ip_binding_established_by's own recursive
   structure exactly, for the same reason (no cycle-guard needed: no
   PHI node here to create a cycle through).  */

static gimple *
ip_originating_call (gimple *def_stmt)
{
  if (gimple_code (def_stmt) == GIMPLE_CALL)
    return def_stmt;
  if (is_gimple_assign (def_stmt)
      && (gimple_assign_rhs_code (def_stmt) == POINTER_PLUS_EXPR
	  || gimple_assign_single_p (def_stmt)))
    {
      gimple *reaching = ip_resolve_defining_stmt (gimple_assign_rhs1 (def_stmt),
						    def_stmt);
      return reaching ? ip_originating_call (reaching) : NULL;
    }
  return NULL;
}

/* One (DECL, TYPE) pair CALL is judged to mutate -- see
   ip_collect_mutations's own comment for how these are found.  */

struct ip_mutation
{
  tree decl;
  tree type;
};

/* Collect into *OUT every (decl, type) pair CALL is a "mutating
   operation" for -- capable of invalidating other values bound to
   that decl.  Two independent sources, per the CppCon 2026 "Profiles"
   talk's own slide 53 ("Invalidation profile summary"):

   - A non-const member-function call, not marked [[not_invalidating]]
     on the function itself, whose receiver resolves to a single,
     nameable DECL ("A non-const function is assumed to invalidate").
     A constructor call is excluded from this source specifically
     (DECL_CONSTRUCTOR_P): it cannot invalidate anything bound to its
     own receiver, since nothing could have been bound to an object
     before that object is even constructed -- ip_defines_var_p
     already treats a constructor call as a WRITE/definition of its
     receiver for exactly this reason, not an ordinary mutating call.

   - Any function (member or free) call with an argument bound to a
     parameter of reference-to-non-const or pointer-to-non-const class
     type, not marked [[not_invalidating]] AT THAT PARAMETER POSITION,
     whose argument itself resolves to a single, nameable DECL ("A
     function is assumed to invalidate a non-const argument" -- the
     free-function half slide 43's own vector<int>& arg2 example
     shows, distinct from the member-function case above, which is
     checked on the function itself, not per-parameter).  A single
     call can mutate more than one such argument, hence a vector of
     results rather than a single pair.

   See this file's own top comment for what this deliberately does
   not yet cover (indirectly-dispatched calls: gimple_call_fndecl
   returns NULL_TREE for those, so neither source above ever fires).  */

static void
ip_collect_mutations (gcall *call, vec<ip_mutation> *out)
{
  tree fndecl = gimple_call_fndecl (call);
  if (!fndecl)
    return;

  if (DECL_IOBJ_MEMBER_FUNCTION_P (fndecl) && !DECL_CONSTRUCTOR_P (fndecl)
      && !DECL_CONST_MEMFUNC_P (fndecl)
      && !profiles_not_invalidating_p (fndecl)
      && gimple_call_num_args (call) >= 1)
    if (tree decl = ip_receiver_decl (gimple_call_arg (call, 0)))
      {
	tree this_ptr_type = TREE_VALUE (TYPE_ARG_TYPES (TREE_TYPE (fndecl)));
	out->safe_push ({ decl, TREE_TYPE (this_ptr_type) });
      }

  tree arg_type = TYPE_ARG_TYPES (TREE_TYPE (fndecl));
  if (DECL_IOBJ_MEMBER_FUNCTION_P (fndecl) && arg_type)
    arg_type = TREE_CHAIN (arg_type); /* Skip the already-handled 'this'.  */
  unsigned first_arg = DECL_IOBJ_MEMBER_FUNCTION_P (fndecl) ? 1 : 0;
  for (unsigned i = first_arg;
       i < gimple_call_num_args (call) && arg_type
       && TREE_VALUE (arg_type) != void_type_node;
       ++i, arg_type = TREE_CHAIN (arg_type))
    {
      tree param_type = TREE_VALUE (arg_type);
      tree pointee = (TREE_CODE (param_type) == REFERENCE_TYPE
		      || TREE_CODE (param_type) == POINTER_TYPE)
		     ? TREE_TYPE (param_type) : NULL_TREE;
      if (!pointee || !CLASS_TYPE_P (pointee) || TYPE_READONLY (pointee))
	continue;
      if (profiles_not_invalidating_at_position_p (fndecl, i + 1))
	continue;
      if (tree decl = ip_receiver_decl (gimple_call_arg (call, i)))
	out->safe_push ({ decl, pointee });
    }
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


/* Check every trackable operand VAR is USE_STMT (a call argument, or
   an ordinary copy's RHS) against every mutating call in MUTATING_
   CALLS/MUTATED_DECLS/MUTATED_TYPES that provably occurs strictly
   between VAR's own binding's establishment (REACHING, below) and
   USE_STMT, emitting a diagnostic (unless header-exempted) for the
   first one Rule #0/#1 cannot clear.  A mutation that precedes
   REACHING is irrelevant: the binding didn't even exist yet when it
   happened, so it cannot be what invalidates THIS binding -- e.g.
   'mutate(v); v = other; auto p = v.data() + 1; use(*p);' must stay
   clean, since both mutations of 'v' happened before 'p' was ever
   (re-)established.  */

static void
ip_check_operand_uses (gimple *use_stmt, tree var,
			vec<gimple *> &mutating_calls,
			vec<tree> &mutated_decls, vec<tree> &mutated_types,
			tree enclosing_fndecl)
{
  gimple *reaching = ip_nearest_write_before (var, use_stmt);
  if (!reaching)
    return;
  tree bound_decl = ip_binding_established_by (reaching);
  if (!bound_decl)
    return;
  gimple *origin = ip_originating_call (reaching);

  for (unsigned i = 0; i < mutating_calls.length (); ++i)
    {
      gimple *m = mutating_calls[i];
      if (use_stmt == m || m == origin
	  || !ip_use_after_mutation_p (m, use_stmt)
	  || !ip_use_after_mutation_p (reaching, m))
	continue;

      tree mutated_decl = mutated_decls[i];
      bool safe = (mutated_decl != bound_decl
		   && (ip_decls_provably_distinct_objects_p (mutated_decl,
							      bound_decl)
		       || ip_types_provably_unrelated_p (mutated_types[i],
							  TREE_TYPE (bound_decl))));
      if (safe)
	continue;
      if (!profiles_diagnostic_exempt_p (gimple_location (use_stmt),
					 enclosing_fndecl, "std::invalidation"))
	error_at (gimple_location (use_stmt),
		  "use of a value bound to %qD, potentially invalidated "
		  "by an earlier mutation of %qD, not permitted under the "
		  "%<std::invalidation%> profile", bound_decl, mutated_decl);
      break;
    }
}

/* If T is a MEM_REF/INDIRECT_REF/ARRAY_REF based on a trackable raw
   pointer -- a raw pointer's own built-in dereference ('*p'/'p->m'/
   'p[i]') -- return that pointer's decl; else NULL_TREE.  A
   POINTER_PLUS_EXPR base (the common '_1 = p_2 + i_3; MEM[_1]' shape
   a computed-index 'p[i]' lowers to) is unwrapped one level first.
   Split out from ip_use_decl below so an assignment's own LHS can be
   checked with JUST this, not that function's full set of shapes:
   writing through a dereference ('*p = ...;') still reads p's own
   value (to know where to write), but the bare trackable variable
   itself, as a plain assignment's LHS ('q = ...;'), is being WRITTEN,
   not read, and must not be treated as a use of q.  */

static tree
ip_deref_base_decl (tree t)
{
  if (TREE_CODE (t) != MEM_REF && TREE_CODE (t) != INDIRECT_REF
      && TREE_CODE (t) != ARRAY_REF)
    return NULL_TREE;
  tree base = TREE_OPERAND (t, 0);
  if (TREE_CODE (base) == POINTER_PLUS_EXPR)
    base = TREE_OPERAND (base, 0);
  tree decl = ip_trackable_decl (base);
  return (decl && TREE_CODE (TREE_TYPE (decl)) == POINTER_TYPE)
	 ? decl : NULL_TREE;
}

/* If T (a call argument, or an assignment's RHS) is a "read" of some
   trackable VAR_DECL/PARM_DECL (ip_trackable_operand_p), in one of
   three shapes this checker recognizes as such a read, return that
   decl; else NULL_TREE.

     - T itself (through ip_trackable_decl's own SSA_NAME unwrap):
       passed by value/reference to another function, e.g.
       'can_process (iter)'.

     - ADDR_EXPR of one: the implicit "this" a class-typed value's own
       member-function call takes its receiver by -- '*p'/'p.operator*
       ()', 'p->m'/'p.operator-> ()->m', '++p'/'p.operator++ ()' are
       all, at the GIMPLE level, a call whose first argument is &p,
       not p directly.  This is ip_receiver_decl's own resolution
       (already used for a MUTATING call's receiver) applied to an
       ordinary, non-mutating USE of the same shape instead -- this is
       exactly the gap that let a class-typed iterator's own
       dereference through unchecked before this was added.

     - MEM_REF/INDIRECT_REF/ARRAY_REF based on one (ip_deref_base_decl):
       a raw pointer's own built-in dereference, which (unlike a
       class-typed value's operator overloads above) never goes
       through a function call at all.  Not used for an assignment's
       own LHS -- see ip_deref_base_decl's own comment for why.  */

static tree
ip_use_decl (tree t)
{
  tree direct = ip_trackable_decl (t);
  if (direct)
    return direct;
  if (TREE_CODE (t) == ADDR_EXPR)
    return ip_trackable_decl (TREE_OPERAND (t, 0));
  return ip_deref_base_decl (t);
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
   by ip_collect_mutations) once, then walks every statement's
   operands looking for a trackable read (ip_use_decl) of a class-
   typed or raw-pointer-typed VAR_DECL/PARM_DECL (ip_check_operand_
   uses does the actual Rule #0/#1 work per use).  */

/* True if TYPE is worth running the escape-from-scope check
   (ip_check_return_escape) against at all: a pointer/reference, or a
   class/union that might itself hold one ("container", P3446R0's own
   broad sense) -- a plain scalar return can never dangle.  */

static bool
ip_escape_checkable_type_p (tree type)
{
  switch (TREE_CODE (type))
    {
    case POINTER_TYPE:
    case REFERENCE_TYPE:
    case RECORD_TYPE:
    case UNION_TYPE:
      return true;
    default:
      return false;
    }
}

/* -------------------------------------------------------------------
   P3446R0/P4296R0 Phase 7a: owner-consumption checking.

   Two independent layers, both keyed off [[owning_ptr]]/[[owner]]
   (profiles_owning_ptr_p, profiles_owning_ptr_at_position_p --
   profiles.cc), added alongside this file's existing Rule #0/#1
   dangling-pointer machinery, which they share no state with:

   1. Flavor consistency (bidirectional mismatch checks: assignment,
      call argument/parameter, return) -- a direct structural port of
      init-profile-gimple.cc's own three [[ref_to_uninit]] consistency
      checks (ip_check_call/assign/return_flavor_consistency), same
      pattern, substituting the owner flavor throughout. No shared
      header exists between the two GIMPLE-checker files, so this is a
      genuine from-scratch port, not a call-through.

   2. Definite-consumption dataflow (ip_check_owner_consumption and its
      helpers, further below): the actual leak checker -- an
      [[owner]] parameter, or the captured result of a call to an
      owner-returning function, must be deleted, passed to another
      owner-accepting sink (parameter, return, or field), or handed to
      std::owner_consumed, on EVERY path before the function exits or
      the binding is reassigned.
   ------------------------------------------------------------------- */

/* True if ARG (a call-argument or plain-assignment RHS expression) is,
   provably, a null pointer constant -- same technique and same
   rationale as init-profile-gimple.cc's own ip_arg_null_pointer_p (a
   null pointer refers to no object, so it is compatible with either
   owner-flavor, in either direction).  */

static bool
ip_owner_arg_null_pointer_p (tree arg)
{
  if (TREE_CODE (arg) == INTEGER_CST)
    return integer_zerop (arg);
  if (TREE_CODE (arg) == SSA_NAME)
    {
      gimple *def = SSA_NAME_DEF_STMT (arg);
      if (def && is_gimple_assign (def) && gimple_assign_single_p (def))
	return ip_owner_arg_null_pointer_p (gimple_assign_rhs1 (def));
    }
  return false;
}

static bool ip_arg_owner_flavored_p_1 (tree arg, int depth);

static bool
ip_arg_owner_flavored_p (tree arg)
{
  return ip_arg_owner_flavored_p_1 (arg, 0);
}

/* True if ARG (a call-argument, plain-assignment RHS, or return-value
   expression) is, provably, owner-flavored -- direct structural port
   of init-profile-gimple.cc's own ip_arg_uninit_flavored_p_1, see that
   function's own comment for the full rationale of each branch below.
   Substitutes profiles_owning_ptr_p for profiles_uninit_pointee_p
   throughout, and drops the ADDR_EXPR branch entirely: [[ref_to_
   uninit]] tracks a POINTEE's state reached through '&var', but
   [[owning_ptr]]/[[owner]] tracks the pointer VALUE itself, which is
   never itself accessed via '&owner_var' in the relevant sense here.  */

static bool
ip_arg_owner_flavored_p_1 (tree arg, int depth)
{
  if (depth > 16)
    return false; /* Defensive recursion guard, as in the uninit
		      checker's own identical guard -- only a loop-carried
		      PHI (below) could even threaten to cycle.  */
  if (TREE_CODE (arg) == SSA_NAME)
    {
      gimple *def = SSA_NAME_DEF_STMT (arg);
      if (def && is_gimple_assign (def) && gimple_assign_single_p (def))
	return ip_arg_owner_flavored_p_1 (gimple_assign_rhs1 (def), depth + 1);
      if (def && gimple_code (def) == GIMPLE_CALL)
	{
	  tree callee = gimple_call_fndecl (as_a<gcall *> (def));
	  return callee && profiles_owning_ptr_p (callee);
	}
      if (def && gimple_code (def) == GIMPLE_PHI)
	{
	  gphi *phi = as_a<gphi *> (def);
	  for (unsigned i = 0; i < gimple_phi_num_args (phi); ++i)
	    {
	      tree phi_arg = gimple_phi_arg_def (phi, i);
	      if (ip_owner_arg_null_pointer_p (phi_arg))
		continue;
	      if (!ip_arg_owner_flavored_p_1 (phi_arg, depth + 1))
		return false;
	    }
	  return true;
	}
      tree var = SSA_NAME_VAR (arg);
      if (var
	  && (VAR_P (var) || TREE_CODE (var) == PARM_DECL)
	  && TREE_CODE (TREE_TYPE (var)) == POINTER_TYPE)
	return profiles_owning_ptr_p (var);
      return false;
    }
  if ((VAR_P (arg) || TREE_CODE (arg) == PARM_DECL)
      && TREE_CODE (TREE_TYPE (arg)) == POINTER_TYPE)
    return profiles_owning_ptr_p (arg);
  return false;
}

/* Resolve T down to whatever VAR_DECL/PARM_DECL it ultimately traces
   back to through a chain of plain single-operand copies -- direct
   port of init-profile-gimple.cc's own ip_resolve_underlying_decl (see
   that function's own comment: a call's result, or a return statement's
   own operand, is never assigned/used directly, always through an
   anonymous SSA temporary first).  Used both by the return-flavor
   check below (to recognize a direct pass-through of an already-
   owner-declared parameter) and by the definite-consumption checker
   further down (to test whether a call argument/return/field-RHS
   traces back to a SPECIFIC tracked binding).  */

static tree
ip_owner_resolve_underlying_decl (tree t)
{
  if (TREE_CODE (t) == SSA_NAME)
    {
      tree var = SSA_NAME_VAR (t);
      if (var)
	return var;
      gimple *def = SSA_NAME_DEF_STMT (t);
      if (def && is_gimple_assign (def) && gimple_assign_single_p (def))
	return ip_owner_resolve_underlying_decl (gimple_assign_rhs1 (def));
      return NULL_TREE;
    }
  if (VAR_P (t) || TREE_CODE (t) == PARM_DECL)
    return t;
  return NULL_TREE;
}

/* P3446R0/P4296R0 Phase 7a: for a direct call, check that every
   pointer argument's owner-flavor (ip_arg_owner_flavored_p) matches
   its corresponding parameter's (profiles_owning_ptr_at_position_p),
   bidirectionally -- direct structural port of init-profile-gimple.cc's
   own ip_check_call_flavor_consistency; see that function's own
   comment for why this queries by ARGUMENT POSITION rather than
   walking DECL_ARGUMENTS (callee), and why no separate "is this a
   pointer parameter/argument" guard is needed.  No construct_at-style
   exemption (irrelevant to ownership) and no "force owner-flavored"
   override on the direct-LHS case below (unlike now_uninit's own
   override in the sibling file): std::owner_consumed (see further
   down) is a CONSUMING, not FLAVORING, escape hatch -- it asserts an
   owner value has been handed off, not that some other, unattributed
   value should retroactively be treated as owner-flavored -- so no
   analogous override is needed or correct here.  */

static bool ip_owner_delete_call_shape_p (gcall *call);

static void
ip_check_owner_call_flavor_consistency (gimple *stmt, tree enclosing_fndecl)
{
  if (gimple_code (stmt) != GIMPLE_CALL)
    return;
  tree callee = gimple_call_fndecl (stmt);
  if (!callee)
    return;
  /* A destructor's own "this" is never a real ownership-transfer
     parameter, regardless of whether the object happens to be reached
     through an [[owner]] pointer -- confirmed empirically (-fdump-
     tree-ssa) that EVEN a non-virtual delete-expression's own lowering
     calls the destructor directly, 'S::~S (_3); operator delete (_3,
     size);', with the SAME traced pointer as the destructor's own
     first ("this") argument; without this exemption, deleting any
     [[owner]] pointer to a class with a non-trivial destructor would
     falsely report "this" itself as flavor-mismatched.  */
  if (DECL_DESTRUCTOR_P (callee))
    return;
  /* operator delete's own real declaration is an ordinary, unattributed
     system function -- deleting an [[owner]] pointer is the entire
     point of the attribute, not a flavor mismatch to report against
     operator delete's own (necessarily unflavored) parameter.  See
     ip_owner_delete_call_shape_p's own comment further down.  Likewise
     std::owner_consumed's own real signature has no [[owner]] of its
     own to match (same reasoning as construct_at's identical exemption
     in the sibling init-profile-gimple.cc: it's a generic, never-
     attributed identity template meant to accept exactly this kind of
     argument, matching no_dangling/now_uninit's own shape) -- ip_check_
     owner_binding's own consuming-event scan is what recognizes this
     call as consuming its argument; this check must not separately,
     incorrectly reject the very call that's meant to make the leak
     checker happy.  */
  if (ip_owner_delete_call_shape_p (as_a<gcall *> (stmt))
      || ip_std_call_named_p (as_a<gcall *> (stmt), "owner_consumed"))
    return;

  unsigned nargs = gimple_call_num_args (stmt);
  for (unsigned i = 0; i < nargs; ++i)
    {
      tree arg = gimple_call_arg (stmt, i);
      if (ip_owner_arg_null_pointer_p (arg))
	continue;
      bool param_flavor = profiles_owning_ptr_at_position_p (callee, i + 1);
      bool arg_flavor = ip_arg_owner_flavored_p (arg);

      if (profiles_diagnostic_exempt_p (gimple_location (stmt),
					enclosing_fndecl, "std::invalidation"))
	continue;
      if (param_flavor && !arg_flavor)
	error_at (gimple_location (stmt),
		  "argument %u to %qD must be marked %<[[owner]]%>, matching "
		  "its %<[[owner]]%> parameter, under the "
		  "%<std::invalidation%> profile", i + 1, callee);
      else if (!param_flavor && arg_flavor)
	error_at (gimple_location (stmt),
		  "argument %u to %qD is marked %<[[owner]]%> but its "
		  "parameter is not marked %<[[owner]]%>, under the "
		  "%<std::invalidation%> profile", i + 1, callee);
    }

  /* The RETURN-value counterpart, for a call whose result is assigned
     DIRECTLY into a named pointer (a GCC-recognized builtin's own
     lowering, or any other callee GCC chooses not to route through an
     anonymous SSA temporary) -- see init-profile-gimple.cc's own
     identical block for why this shape, though rare, is real and not
     dead code.  */
  tree lhs = gimple_call_lhs (stmt);
  tree lhs_var = lhs ? ip_trackable_decl (lhs) : NULL_TREE;
  if (lhs_var && TREE_CODE (TREE_TYPE (lhs_var)) == POINTER_TYPE)
    {
      bool dst_flavor = profiles_owning_ptr_p (lhs_var);
      bool callee_flavor = profiles_owning_ptr_p (callee);
      if (dst_flavor != callee_flavor
	  && !profiles_diagnostic_exempt_p (gimple_location (stmt),
					     enclosing_fndecl,
					     "std::invalidation"))
	{
	  if (callee_flavor)
	    error_at (gimple_location (stmt),
		      "assigning a pointer marked %<[[owner]]%> into a "
		      "pointer not marked %<[[owner]]%>, under the "
		      "%<std::invalidation%> profile");
	  else
	    error_at (gimple_location (stmt),
		      "assigning a pointer not marked %<[[owner]]%> into a "
		      "pointer marked %<[[owner]]%>, under the "
		      "%<std::invalidation%> profile");
	}
    }
}

/* The RETURN-statement counterpart (P3446R0/P4296R0 Phase 7a): a
   function declared [[owner]] on its own return must only ever return
   an owner-flavored value, and conversely, an unflavored function must
   never return one -- direct structural port of init-profile-gimple.cc's
   own ip_check_return_flavor_consistency, including its "trust a
   direct pass-through of an already-owner-declared parameter" exemption
   (see that function's own comment for the full rationale).  NOTE this
   exemption's own interaction with the definite-consumption checker
   further below: 'T* f([[owner]] T* p) { return p; }' with f's own
   return NOT [[owner]]-marked passes THIS check (an exempt pass-
   through) but must still be flagged by the consumption checker as a
   genuine leak -- the caller now silently owns p with no marker saying
   so.  */

static void
ip_check_owner_return_flavor_consistency (gimple *stmt, tree enclosing_fndecl)
{
  if (gimple_code (stmt) != GIMPLE_RETURN)
    return;
  tree retval = gimple_return_retval (as_a<greturn *> (stmt));
  if (!retval || ip_owner_arg_null_pointer_p (retval))
    return;
  tree retval_decl = ip_owner_resolve_underlying_decl (retval);
  if (retval_decl && TREE_CODE (retval_decl) == PARM_DECL
      && profiles_owning_ptr_p (retval_decl))
    return;
  bool fn_flavor = profiles_owning_ptr_p (enclosing_fndecl);
  bool retval_flavor = ip_arg_owner_flavored_p (retval);
  if (fn_flavor == retval_flavor)
    return;
  if (profiles_diagnostic_exempt_p (gimple_location (stmt),
				     enclosing_fndecl, "std::invalidation"))
    return;
  if (retval_flavor)
    error_at (gimple_location (stmt),
	      "returning a pointer marked %<[[owner]]%> from a function not "
	      "itself marked %<[[owner]]%>, under the %<std::invalidation%> "
	      "profile");
  else
    error_at (gimple_location (stmt),
	      "returning a pointer not marked %<[[owner]]%> from a function "
	      "marked %<[[owner]]%>, under the %<std::invalidation%> "
	      "profile");
}

/* The plain-assignment counterpart -- direct structural port of
   init-profile-gimple.cc's own ip_check_assign_flavor_consistency; see
   that function's own comment for why this covers a declaration's own
   initializer and a cast for free, with no special-casing.  */

static void
ip_check_owner_assign_flavor_consistency (gimple *stmt, tree enclosing_fndecl)
{
  if (!is_gimple_assign (stmt) || !gimple_assign_single_p (stmt))
    return;
  tree lhs = gimple_assign_lhs (stmt);
  if (TREE_CODE (TREE_TYPE (lhs)) != POINTER_TYPE)
    return;
  tree lhs_var = ip_trackable_decl (lhs);
  if (!lhs_var)
    return;
  tree rhs = gimple_assign_rhs1 (stmt);
  if (ip_owner_arg_null_pointer_p (rhs))
    return;

  bool dst_flavor = profiles_owning_ptr_p (lhs_var);
  bool src_flavor = ip_arg_owner_flavored_p (rhs);

  if (dst_flavor == src_flavor)
    return;
  if (profiles_diagnostic_exempt_p (gimple_location (stmt),
				     enclosing_fndecl, "std::invalidation"))
    return;
  if (src_flavor)
    error_at (gimple_location (stmt),
	      "assigning a pointer marked %<[[owner]]%> into a pointer not "
	      "marked %<[[owner]]%>, under the %<std::invalidation%> "
	      "profile");
  else
    error_at (gimple_location (stmt),
	      "assigning a pointer not marked %<[[owner]]%> into a pointer "
	      "marked %<[[owner]]%>, under the %<std::invalidation%> "
	      "profile");
}

/* P3446R0/P4296R0 Phase 7a: a single call passing the SAME [[owner]]
   value to two DIFFERENT owner-accepting parameters is a real hazard
   the definite-consumption layer alone can't see: 'void f (T *p
   [[owner]], T *q [[owner]]);' declares two INDEPENDENT ownership
   obligations, so a callee that (reasonably) assumes p and q never
   alias and deletes each separately double-frees when called as 'f
   (ptr, ptr)' -- and the consumption checker itself would see nothing
   wrong, since ptr genuinely DOES reach an owner-sink argument
   position (whichever ip_owner_passed_to_sink_p's own loop happens to
   find first) and gets marked consumed, with no separate check that
   OTHER owner-sink positions in that same call aren't the identical
   value.  Checked by resolving (ip_owner_resolve_underlying_decl,
   the same plain-copy-chain trace every other check here already
   uses) every owner-marked argument position and comparing for exact,
   syntactic identity -- deliberately NOT the more speculative "could
   these alias" reasoning ip_decls_provably_distinct_objects_p answers
   elsewhere in this file (a different question, about two syntactically
   DIFFERENT decls); here the two arguments resolve to the literal SAME
   decl, no speculation needed.  A null argument at multiple owner-sink
   positions is exempt, same as everywhere else in this checker: null
   represents no object at all, so aliasing is moot.  */

static void
ip_check_owner_call_arg_aliasing (gimple *stmt, tree enclosing_fndecl)
{
  if (gimple_code (stmt) != GIMPLE_CALL)
    return;
  gcall *call = as_a<gcall *> (stmt);
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  unsigned nargs = gimple_call_num_args (call);
  for (unsigned i = 0; i < nargs; ++i)
    {
      if (!profiles_owning_ptr_at_position_p (callee, i + 1))
	continue;
      tree arg_i = gimple_call_arg (call, i);
      if (ip_owner_arg_null_pointer_p (arg_i))
	continue;
      tree decl_i = ip_owner_resolve_underlying_decl (arg_i);
      if (!decl_i)
	continue;

      for (unsigned j = i + 1; j < nargs; ++j)
	{
	  if (!profiles_owning_ptr_at_position_p (callee, j + 1))
	    continue;
	  tree arg_j = gimple_call_arg (call, j);
	  if (ip_owner_arg_null_pointer_p (arg_j))
	    continue;
	  if (ip_owner_resolve_underlying_decl (arg_j) != decl_i)
	    continue;
	  if (profiles_diagnostic_exempt_p (gimple_location (stmt),
					     enclosing_fndecl,
					     "std::invalidation"))
	    continue;
	  error_at (gimple_location (stmt),
		    "the same %<[[owner]]%> pointer %qD passed to two "
		    "different owner-accepting parameters (%u and %u) of "
		    "%qD, under the %<std::invalidation%> profile",
		    decl_i, i + 1, j + 1, callee);
	}
    }
}

/* -------------------------------------------------------------------
   Definite-consumption dataflow: the actual leak checker.  An
   [[owner]]/[[owning_ptr]] PARM_DECL, or a local VAR_DECL that
   receives an owner-flavored value somewhere in the function, must be
   DELETED, or handed to another owner-accepting sink -- a call
   argument at an owner-marked parameter position, this function's own
   [[owner]]-marked return, an [[owner]]-marked field, or std::owner_
   consumed (the manual "handed to something this checker can't see
   into, e.g. a std::unique_ptr's constructor" escape hatch) -- on
   EVERY path before the function exits or the binding is reassigned.

   For a delete-expression on a POLYMORPHIC type, the actual
   deallocation happens inside the deleting destructor's own
   synthesized clone (a separate function), reached here via an
   indirect/virtual call -- confirmed via -fdump-tree-ssa: 'delete p;'
   lowers to a null check followed by an indirect OBJ_TYPE_REF call to
   the destructor, with NO directly-visible operator-delete call at
   this call site at all (that call happens inside the deleting
   destructor's own body instead).  Rather than trying to look inside
   that separate function -- which would violate this project's
   "never read a callee's definition" boundary (see e.g. handle_must_
   init_attribute's own comment, tree.cc) -- ip_owner_deleting_dtor_
   dispatch_p further down recognizes this shape directly, resolved
   entirely from V's own known static type (no shared-infrastructure
   changes needed; see that function's own comment for why an earlier
   attempt to thread a new flag through gimplification was reverted).  */

static bool
ip_owner_delete_call_shape_p (gcall *call)
{
  if (!gimple_call_from_new_or_delete (call))
    return false;
  tree fndecl = gimple_call_fndecl (call);
  return fndecl && DECL_IS_OPERATOR_DELETE_P (fndecl);
}

/* True if CALL is a lowered 'delete V;' specifically -- ip_owner_
   delete_call_shape_p above, plus tracing the deleted argument back
   to V through the same plain-copy chain every other flavor check
   here already chases.  */

static bool
ip_owner_delete_call_p (gcall *call, tree v)
{
  if (!ip_owner_delete_call_shape_p (call))
    return false;
  if (gimple_call_num_args (call) < 1)
    return false;
  return ip_owner_resolve_underlying_decl (gimple_call_arg (call, 0)) == v;
}

/* True if CALL is an indirect (vtable) dispatch to V's own DELETING
   destructor, for V's known STATIC type -- the shape a delete-
   expression on a POLYMORPHIC type lowers to (confirmed via -fdump-
   tree-ssa: no directly-resolvable operator-delete call is visible at
   the delete-expression's own call site at all; the real deallocation
   happens inside the deleting destructor's own body, a separate
   function this checker does not, and per this project's "never read
   a callee's definition" boundary should not, look inside).

   Resolved entirely from already-known, already-resolved front-end
   data -- V's own static pointee type -- with NO need to touch shared
   compiler infrastructure (an earlier attempt to thread a new flag
   through gimplification, marking build_delete's own deleting-
   destructor call the same way build_op_delete_call marks a direct
   operator-delete call, was reverted: CALL_FROM_NEW_OR_DELETE_P,
   CALL_FROM_THUNK_P, and CALL_ALLOCA_FOR_VAR_P all alias the exact
   same tree_base bit (tree.h), disambiguated only by the ORIGINAL
   code's own fndecl-based dispatch -- setting it unconditionally
   corrupted THUNK call information for unrelated non-trivial-
   parameter-passing thunks, an ICE confirmed via the full contracts
   suite, not a hypothetical risk).

   Instead: CALL's own callee, if an OBJ_TYPE_REF, carries the
   dispatched-through object (OBJ_TYPE_REF_OBJECT, traced back to V
   the same way every other argument here is) and the vtable slot
   TOKEN being dispatched (OBJ_TYPE_REF_TOKEN). V's own static pointee
   type's destructor (CLASSTYPE_DESTRUCTOR) has a "deleting destructor"
   clone (DECL_DELETING_DESTRUCTOR_P, found via FOR_EACH_CLONE -- the
   same clone build_delete, init.cc, asks build_dtor_call for when
   deleting a polymorphic object) whose own DECL_VINDEX is exactly the
   vtable slot IT occupies. If CALL's own token matches THAT slot, this
   call provably invokes -- for whatever V's DYNAMIC type turns out to
   be at runtime, since virtual dispatch preserves "which special
   member this slot is" across every override, not just V's own static
   type -- the deleting destructor, i.e. this delete-expression's own
   complete deallocation, exactly like a directly-visible operator-
   delete call would.  */

static bool
ip_owner_deleting_dtor_dispatch_p (gcall *call, tree v)
{
  tree fn = gimple_call_fn (call);
  if (!fn || TREE_CODE (fn) != OBJ_TYPE_REF)
    return false;
  if (ip_owner_resolve_underlying_decl (OBJ_TYPE_REF_OBJECT (fn)) != v)
    return false;
  tree token = OBJ_TYPE_REF_TOKEN (fn);
  if (!token || TREE_CODE (token) != INTEGER_CST)
    return false;

  tree ptr_type = TREE_TYPE (v);
  if (TREE_CODE (ptr_type) != POINTER_TYPE)
    return false;
  tree type = TREE_TYPE (ptr_type);
  if (!CLASS_TYPE_P (type))
    return false;
  tree dtor = CLASSTYPE_DESTRUCTOR (type);
  if (!dtor)
    return false;

  tree clone;
  FOR_EACH_CLONE (clone, dtor)
    {
      if (!DECL_DELETING_DESTRUCTOR_P (clone))
	continue;
      tree vindex = DECL_VINDEX (clone);
      return vindex && TREE_CODE (vindex) == INTEGER_CST
	     && tree_int_cst_equal (vindex, token);
    }
  return false;
}

/* True if CALL passes V as an argument at a position the callee's own
   corresponding parameter marks [[owner]]/[[owning_ptr]] -- ownership
   transferred to the callee.  */

static bool
ip_owner_passed_to_sink_p (gcall *call, tree v)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return false;
  unsigned nargs = gimple_call_num_args (call);
  for (unsigned i = 0; i < nargs; ++i)
    if (profiles_owning_ptr_at_position_p (callee, i + 1)
	&& ip_owner_resolve_underlying_decl (gimple_call_arg (call, i)) == v)
      return true;
  return false;
}

/* True if STMT stores V into an [[owner]]-marked field ('obj.field =
   V;'/'obj->field = V;') -- ownership transferred to the containing
   object.  Deliberately does NOT itself track that field's own
   eventual destruction (a separate, harder, whole-class-lifetime
   question) -- a field is a consuming SINK only, never itself a
   tracked source.  */

static bool
ip_owner_stored_into_field_p (gimple *stmt, tree v)
{
  if (!is_gimple_assign (stmt) || !gimple_assign_single_p (stmt))
    return false;
  tree lhs = gimple_assign_lhs (stmt);
  if (TREE_CODE (lhs) != COMPONENT_REF || !profiles_owning_ptr_p (lhs))
    return false;
  return ip_owner_resolve_underlying_decl (gimple_assign_rhs1 (stmt)) == v;
}

/* True if CALL is a call to std::owner_consumed -- the invalidation
   profile's manual, unproven "this owner value has been properly
   handed off for cleanup by some means this checker cannot itself
   see (e.g. construction of a std::unique_ptr from it)" assertion
   (see <utility>'s own definition), recognized the same way ip_now_
   valid_call_p/ip_no_dangling_call_p recognize their own escape
   hatches.  */

static bool
ip_owner_consumed_call_p (gcall *call)
{
  return ip_std_call_named_p (call, "owner_consumed");
}

/* True if STMT is a delete-expression's own implicit null-guard --
   'if (V != 0) goto ...; else goto ...;' -- where the edge taken when
   V is NOT null leads (the exact shape build_delete's own lowering
   always produces, confirmed via -fdump-tree-ssa: deleting a null
   pointer is defined to be a no-op, so a delete-expression is ALWAYS
   preceded by exactly this null check) to a block that itself deletes
   V.  Recognized as a consuming event IN ITS OWN RIGHT, not just via
   the delete call buried in the guarded block: without this, the
   OTHER edge out of this same COND (the "V was null" path, which
   never reaches the delete call at all) would be misread as "V is
   still owned and unconsumed" and wrongly flagged as a leak -- a null
   [[owner]] value represents nothing owned, not an owned value that
   escaped consumption, so BOTH outcomes of this check must count as
   settling V's fate, not just the one that happens to reach the
   actual operator-delete call.  */

static bool
ip_owner_delete_guard_cond_p (gimple *stmt, tree v)
{
  if (gimple_code (stmt) != GIMPLE_COND)
    return false;
  gcond *cond = as_a<gcond *> (stmt);
  tree_code code = gimple_cond_code (cond);
  if (code != NE_EXPR && code != EQ_EXPR)
    return false;
  tree lhs = gimple_cond_lhs (cond);
  tree rhs = gimple_cond_rhs (cond);
  tree ptr_operand;
  if (integer_zerop (rhs))
    ptr_operand = lhs;
  else if (integer_zerop (lhs))
    ptr_operand = rhs;
  else
    return false;
  if (ip_owner_resolve_underlying_decl (ptr_operand) != v)
    return false;

  basic_block bb = gimple_bb (stmt);
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      bool non_null_edge = (code == NE_EXPR)
	? (e->flags & EDGE_TRUE_VALUE) != 0
	: (e->flags & EDGE_FALSE_VALUE) != 0;
      if (!non_null_edge)
	continue;
      for (gimple_stmt_iterator gsi = gsi_start_bb (e->dest);
	   !gsi_end_p (gsi); gsi_next (&gsi))
	if (gcall *call = dyn_cast<gcall *> (gsi_stmt (gsi)))
	  if (ip_owner_delete_call_p (call, v)
	      || ip_owner_deleting_dtor_dispatch_p (call, v))
	    return true;
    }
  return false;
}

/* True if STMT is a consuming event for the tracked owner value V, in
   the function whose own return-flavor is FN_RETURN_IS_OWNER
   (profiles_owning_ptr_p (fun->decl), passed in rather than
   recomputed per statement).  */

static bool
ip_owner_consuming_stmt_p (gimple *stmt, tree v, bool fn_return_is_owner)
{
  if (ip_owner_delete_guard_cond_p (stmt, v))
    return true;
  if (gcall *call = dyn_cast<gcall *> (stmt))
    {
      if (ip_owner_delete_call_p (call, v))
	return true;
      if (ip_owner_deleting_dtor_dispatch_p (call, v))
	return true;
      if (ip_owner_passed_to_sink_p (call, v))
	return true;
      if (ip_owner_consumed_call_p (call) && gimple_call_num_args (call) >= 1
	  && (ip_owner_resolve_underlying_decl (gimple_call_arg (call, 0))
	      == v))
	return true;
      return false;
    }
  if (gimple_code (stmt) == GIMPLE_RETURN)
    {
      if (!fn_return_is_owner)
	return false;
      tree retval = gimple_return_retval (as_a<greturn *> (stmt));
      return retval && ip_owner_resolve_underlying_decl (retval) == v;
    }
  return ip_owner_stored_into_field_p (stmt, v);
}

/* If STMT assigns a FRESH owner-flavored value into some trackable
   local VAR_DECL (either a plain 'lhs = owner_flavored_expr;', or a
   direct-LHS call 'lhs = owner_returning_fn (...);' -- the same rare
   but real GCC-recognized-builtin-shaped direct-assignment case ip_
   check_owner_call_flavor_consistency's own direct-LHS block exists
   for), return that VAR_DECL; else NULL_TREE.  This is how a local
   variable "becomes owned" -- distinct from a PARM_DECL, which is
   owned unconditionally from function entry instead.  */

static tree
ip_owner_gen_lhs_decl (gimple *stmt)
{
  if (is_gimple_assign (stmt) && gimple_assign_single_p (stmt))
    {
      tree d = ip_trackable_decl (gimple_assign_lhs (stmt));
      if (d && TREE_CODE (TREE_TYPE (d)) == POINTER_TYPE
	  && ip_arg_owner_flavored_p (gimple_assign_rhs1 (stmt)))
	return d;
      return NULL_TREE;
    }
  if (gcall *call = dyn_cast<gcall *> (stmt))
    {
      tree d = ip_trackable_decl (gimple_call_lhs (call));
      tree callee = gimple_call_fndecl (call);
      if (d && TREE_CODE (TREE_TYPE (d)) == POINTER_TYPE
	  && callee && profiles_owning_ptr_p (callee))
	return d;
    }
  return NULL_TREE;
}

/* Forward "may still be owned and unconsumed" dataflow -- the dual of
   init-profile-gimple.cc's own "must be initialized" ip_compute_
   reach_info/ip_read_dominated_by_init_p (see that function's own
   comment for the textbook diamond-merge motivation shared by both):
   there, a MUST-property (AND-across-predecessors, monotonic GEN
   only, since a write is never "undone"); here, a MAY-property
   (OR-across-predecessors, GEN *and* KILL, since a consuming event
   really does retire the obligation -- "must eventually consume" is
   the logical negation of "may still reach exit unconsumed").  Still
   a standard monotone dataflow framework despite the kill: each
   block's own transfer function, for fixed GEN/KILL statements, is
   provably monotonic in its own input (either passthrough, or
   constant-true, or constant-false depending on the block's own
   trailing gen/kill event) -- the same reasoning "reaching
   definitions"/"available expressions" rely on everywhere in GCC's
   own optimizers -- so plain iterate-to-fixed-point over a finite
   number of boolean block states still terminates at the correct
   (least) fixed point.  */

struct ip_owner_reach_info
{
  /* Indexed by basic_block->index.  TRUE if some path from the
     binding's own start (function entry, for a parameter; DECL's
     first owner-flavored assignment, for a local) to the START
     (block_in) or END (block_out) of that block still carries an
     owned, not-yet-consumed value.  */
  auto_vec<bool> block_in;
  auto_vec<bool> block_out;
};

/* BB's own transfer function: given IN (may some predecessor path
   still be carrying an unconsumed value into this block), scan BB's
   statements in order, applying DECL's own gen (ip_owner_gen_lhs_decl)
   and consume (ip_owner_consuming_stmt_p) events as they occur, and
   return the resulting state at BB's end.  IS_PARAMETER means DECL is
   owned unconditionally from function entry -- an implicit gen event
   before ENTRY_SUCC's own first statement, rather than at any specific
   statement of DECL's own.  */

static bool
ip_owner_block_transfer (basic_block bb, bool in, tree decl,
			  bool is_parameter, bool fn_return_is_owner,
			  basic_block entry_succ)
{
  bool state = in || (is_parameter && bb == entry_succ);
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (!is_parameter && ip_owner_gen_lhs_decl (stmt) == decl)
	state = true;
      if (state && ip_owner_consuming_stmt_p (stmt, decl, fn_return_is_owner))
	state = false;
    }
  return state;
}

static void
ip_compute_owner_reach_info (function *fun, tree decl, bool is_parameter,
			      bool fn_return_is_owner,
			      basic_block entry_succ,
			      ip_owner_reach_info *info)
{
  unsigned n = last_basic_block_for_fn (fun);
  info->block_in.safe_grow_cleared (n);
  info->block_out.safe_grow_cleared (n);

  bool changed = true;
  while (changed)
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, fun)
	{
	  bool in = false;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    if (info->block_out[e->src->index])
	      {
		in = true;
		break;
	      }
	  if (in != info->block_in[bb->index])
	    {
	      info->block_in[bb->index] = in;
	      changed = true;
	    }

	  bool out = ip_owner_block_transfer (bb, info->block_in[bb->index],
					       decl, is_parameter,
					       fn_return_is_owner, entry_succ);
	  if (out != info->block_out[bb->index])
	    {
	      info->block_out[bb->index] = out;
	      changed = true;
	    }
	}
    }
}

/* True if DECL's tracked binding is still owned-and-unconsumed
   strictly BEFORE POINT executes -- INFO's own block_in, refined by an
   explicit same-block forward scan up to (not including) POINT, the
   same "block dataflow only tracks boundaries" reasoning ip_read_
   dominated_by_init_p's own comment gives.  Used only for the
   reassignment-leak check below: a write to DECL is itself a KILL
   candidate (per ip_owner_block_transfer's own gen/consume scan), but
   here we want the state strictly BEFORE that specific write, i.e.
   whether it discards an as-yet-unconsumed value.  */

static bool
ip_owner_unconsumed_before_stmt_p (gimple *point, tree decl,
				    bool is_parameter, bool fn_return_is_owner,
				    basic_block entry_succ,
				    const ip_owner_reach_info &info)
{
  basic_block bb = gimple_bb (point);
  bool state = info.block_in[bb->index] || (is_parameter && bb == entry_succ);
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == point)
	break;
      if (!is_parameter && ip_owner_gen_lhs_decl (stmt) == decl)
	state = true;
      if (state && ip_owner_consuming_stmt_p (stmt, decl, fn_return_is_owner))
	state = false;
    }
  return state;
}

/* Run the definite-consumption check for one tracked binding: DECL is
   either an [[owner]]/[[owning_ptr]] PARM_DECL of FUN (IS_PARAMETER
   true, owned from function entry), or a local VAR_DECL some statement
   in FUN assigns a fresh owner-flavored value into (IS_PARAMETER
   false; DECL's own gen statement(s) are re-discovered here via ip_
   owner_gen_lhs_decl, the same on-demand per-variable scan pattern
   init-profile-gimple.cc's own per-[[uninit]]-local checkers already
   use, rather than threading a precomputed list through).  */

static void
ip_check_owner_binding (function *fun, tree decl, bool is_parameter)
{
  bool fn_return_is_owner = profiles_owning_ptr_p (fun->decl);
  basic_block entry_succ = single_succ (ENTRY_BLOCK_PTR_FOR_FN (fun));

  ip_owner_reach_info info;
  ip_compute_owner_reach_info (fun, decl, is_parameter, fn_return_is_owner,
				entry_succ, &info);

  /* Leak point 1: some path reaches the function's own exit still
     owned-and-unconsumed.  Mirrors init-profile-gimple.cc's own
     "walk EXIT_BLOCK_PTR_FOR_FN's own preds, skip EH edges" idiom
     (ip_check_constructor_member) exactly.  One diagnostic per
     binding, anchored at DECL's own declaration -- not one per
     leaking exit edge, matching how this project's other whole-
     variable diagnostics (e.g. "cannot verify [[uninit]]") already
     anchor at the declaration rather than at every individual use.  */
  bool leaks_at_exit = false;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR_FOR_FN (fun)->preds)
    {
      if (e->flags & EDGE_EH)
	continue;
      if (info.block_out[e->src->index])
	{
	  leaks_at_exit = true;
	  break;
	}
    }
  if (leaks_at_exit
      && !profiles_diagnostic_exempt_p (DECL_SOURCE_LOCATION (decl),
					 fun->decl, "std::invalidation"))
    error_at (DECL_SOURCE_LOCATION (decl),
	      "%<[[owner]]%> pointer %qD is never deleted or passed on "
	      "before the function returns, under the "
	      "%<std::invalidation%> profile", decl);

  /* Leak point 2: DECL is reassigned (ip_defines_var_p) while its
     current value is still owned-and-unconsumed -- necessary for
     soundness, not optional: without this, 'p = g (); delete p;'
     would look "consumed" by only checking the FINAL value of p,
     silently leaking whatever p originally held.  Excludes DECL's own
     gen statement(s) (the statement that itself first establishes the
     binding is not a "reassignment" of anything).  */
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (!ip_defines_var_p (stmt, decl))
	  continue;
	if (!is_parameter && ip_owner_gen_lhs_decl (stmt) == decl)
	  continue;
	if (ip_owner_unconsumed_before_stmt_p (stmt, decl, is_parameter,
						fn_return_is_owner, entry_succ,
						info)
	    && !profiles_diagnostic_exempt_p (gimple_location (stmt),
					       fun->decl, "std::invalidation"))
	  error_at (gimple_location (stmt),
		    "%qD is reassigned here, discarding a not-yet-consumed "
		    "%<[[owner]]%> pointer, under the %<std::invalidation%> "
		    "profile", decl);
      }
}

/* Top-level driver: find every binding worth definite-consumption
   checking in FUN (its own [[owner]]/[[owning_ptr]] parameters, and
   every local VAR_DECL that receives an owner-flavored value
   somewhere), and check each independently.  A PARM_DECL that is
   ALSO later reassigned an owner-flavored value (e.g. 'void f
   ([[owner]] T *p) { p = g (); ... }') is deliberately checked as
   BOTH a parameter binding (was the ORIGINAL value consumed before
   being overwritten -- ip_check_owner_binding's own reassignment
   check) AND, independently, as a local-style binding starting at
   that same reassignment (was the NEW value ALSO eventually
   consumed) -- two genuinely independent obligations on the same
   variable name, not a redundant double-check.  */

static void
ip_check_owner_consumption (function *fun)
{
  for (tree parm = DECL_ARGUMENTS (fun->decl); parm; parm = DECL_CHAIN (parm))
    if (TREE_CODE (TREE_TYPE (parm)) == POINTER_TYPE
	&& profiles_owning_ptr_p (parm))
      ip_check_owner_binding (fun, parm, /*is_parameter=*/true);

  auto_vec<tree> local_decls;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	tree d = ip_owner_gen_lhs_decl (gsi_stmt (gsi));
	if (!d)
	  continue;
	bool seen = false;
	for (unsigned i = 0; i < local_decls.length (); ++i)
	  if (local_decls[i] == d)
	    {
	      seen = true;
	      break;
	    }
	if (!seen)
	  local_decls.safe_push (d);
      }
  for (unsigned i = 0; i < local_decls.length (); ++i)
    ip_check_owner_binding (fun, local_decls[i], /*is_parameter=*/false);
}

static unsigned int
ip_check_function (function *fun)
{
  auto_vec<gimple *> mutating_calls;
  auto_vec<tree> mutated_decls;
  auto_vec<tree> mutated_types;
  auto_vec<ip_use> uses;
  auto_vec<gimple *> returns_to_check;

  bool check_returns
    = ip_escape_checkable_type_p (TREE_TYPE (TREE_TYPE (fun->decl)));

  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	/* P3446R0/P4296R0 Phase 7a: owner-flavor consistency (bidirectional
	   mismatch checks) -- unconditional over every statement, same
	   pattern init-profile-gimple.cc's own ip_check_function uses for
	   its three [[ref_to_uninit]] counterparts.  Independent of the
	   Rule #0/#1 work below: shares no state, and must not be skipped
	   by that work's own early-exit further down.  */
	ip_check_owner_call_flavor_consistency (stmt, fun->decl);
	ip_check_owner_assign_flavor_consistency (stmt, fun->decl);
	ip_check_owner_return_flavor_consistency (stmt, fun->decl);
	ip_check_owner_call_arg_aliasing (stmt, fun->decl);

	if (gcall *call = dyn_cast<gcall *> (stmt))
	  {
	    auto_vec<ip_mutation> muts;
	    ip_collect_mutations (call, &muts);
	    for (unsigned m = 0; m < muts.length (); ++m)
	      {
		mutating_calls.safe_push (call);
		mutated_decls.safe_push (muts[m].decl);
		mutated_types.safe_push (muts[m].type);
	      }
	    /* A std::now_valid call's own argument is never an ordinary
	       read needing validation against past mutations -- the
	       whole point of calling it is to supersede whatever the
	       argument's prior binding state was, not to read through
	       it one more time under the old rules (ip_defines_var_p/
	       ip_binding_established_by above handle the WRITE side of
	       this same call; this is what keeps the READ side from
	       flagging the very call meant to fix things).  */
	    if (!ip_now_valid_call_p (call))
	      for (unsigned i = 0; i < gimple_call_num_args (call); ++i)
		{
		  tree arg = gimple_call_arg (call, i);
		  if (tree decl = ip_use_decl (arg))
		    uses.safe_push ({ stmt, decl });
		}
	  }
	else if (is_gimple_assign (stmt) && gimple_assign_single_p (stmt))
	  {
	    if (tree decl = ip_use_decl (gimple_assign_rhs1 (stmt)))
	      uses.safe_push ({ stmt, decl });
	    if (tree decl = ip_deref_base_decl (gimple_assign_lhs (stmt)))
	      uses.safe_push ({ stmt, decl });
	  }
	else if (check_returns && gimple_code (stmt) == GIMPLE_RETURN)
	  returns_to_check.safe_push (stmt);
      }

  /* P3446R0/P4296R0 Phase 7a: definite-consumption checking (the
     actual leak checker) must run regardless of whether this
     function has any Rule #0/#1-relevant mutating call/use/return at
     all -- 'void f ([[owner]] int *p) {}' has none of those, but is
     unambiguously a leak.  Deliberately NOT gated by the early-exit
     just below, which is specific to the (unrelated) dangling-pointer
     machinery.  */
  ip_check_owner_consumption (fun);

  if ((mutating_calls.is_empty () || uses.is_empty ())
      && returns_to_check.is_empty ())
    return 0;

  bool dominance_computed = false;
  if (!dom_info_available_p (CDI_DOMINATORS))
    {
      calculate_dominance_info (CDI_DOMINATORS);
      dominance_computed = true;
    }

  if (!mutating_calls.is_empty () && !uses.is_empty ())
    for (unsigned i = 0; i < uses.length (); ++i)
      ip_check_operand_uses (uses[i].stmt, uses[i].var, mutating_calls,
			      mutated_decls, mutated_types, fun->decl);

  for (unsigned i = 0; i < returns_to_check.length (); ++i)
    ip_check_return_escape (returns_to_check[i], fun->decl);

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
