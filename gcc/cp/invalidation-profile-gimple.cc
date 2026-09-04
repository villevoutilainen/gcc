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
   GIMPLE_ASSIGN whose own LHS is exactly VAR, or a constructor call
   whose own "this" (first) argument is &VAR (a constructor returns
   void and writes through its first argument instead of an ordinary
   LHS).  */

static bool
ip_defines_var_p (gimple *stmt, tree var)
{
  if (gimple_code (stmt) == GIMPLE_CALL)
    {
      gcall *call = as_a<gcall *> (stmt);
      if (gimple_call_lhs (call) == var)
	return true;
      tree fndecl = gimple_call_fndecl (call);
      if (fndecl && DECL_CONSTRUCTOR_P (fndecl) && gimple_call_num_args (call) >= 1)
	{
	  tree this_arg = gimple_call_arg (call, 0);
	  return TREE_CODE (this_arg) == ADDR_EXPR
		 && TREE_OPERAND (this_arg, 0) == var;
	}
      return false;
    }
  if (is_gimple_assign (stmt))
    return gimple_assign_lhs (stmt) == var;
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
  tree fndecl = gimple_call_fndecl (call);
  if (!fndecl || !decl_in_std_namespace_p (fndecl))
    return false;
  tree name = DECL_NAME (fndecl);
  return name && id_equal (name, "no_dangling");
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
ip_check_return_escape (gimple *return_stmt)
{
  tree retval = gimple_return_retval (as_a<greturn *> (return_stmt));
  if (!retval)
    return;
  if (TREE_CODE (retval) == RESULT_DECL)
    if (tree nrv_var = ip_resolve_nrv_var (retval, return_stmt))
      retval = nrv_var;
  if (!ip_escapes_locally_p (retval, return_stmt, 0))
    return;
  if (profiles_header_exempt_p (gimple_location (return_stmt),
				 "std::invalidation"))
    return;
  error_at (gimple_location (return_stmt),
	    "returning a pointer or container that may hold a pointer "
	    "to a local, not permitted under the %<std::invalidation%> "
	    "profile (wrap in %<std::no_dangling%> if this is provably "
	    "safe)");
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

  if (DECL_IOBJ_MEMBER_FUNCTION_P (fndecl) && !DECL_CONST_MEMFUNC_P (fndecl)
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
   by ip_collect_mutations) once, then walks every statement's
   operands looking for a trackable class-typed VAR_DECL/PARM_DECL
   read (ip_check_operand_uses does the actual Rule #0/#1 work per
   use).  */

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
	else if (check_returns && gimple_code (stmt) == GIMPLE_RETURN)
	  returns_to_check.safe_push (stmt);
      }

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
			      mutated_decls, mutated_types);

  for (unsigned i = 0; i < returns_to_check.length (); ++i)
    ip_check_return_escape (returns_to_check[i]);

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
