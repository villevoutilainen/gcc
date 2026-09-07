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
   (indirectly-dispatched) call IS classified, exactly as a directly-
   resolved one is: gimple_call_fndecl returns NULL_TREE for these
   (there is no single, statically-known callee), but the call's own
   OBJ_TYPE_REF still names the receiver's static type and the vtable
   slot being dispatched through, which is enough on its own --
   [class.virtual]'s own override rules require every valid override
   at a given slot to share the exact same cv-qualification as the
   function that first introduced it, so the DECLARED constness at
   that fixed, statically-known slot is invariant across every
   possible dynamic target, and is all this checker needs: it never
   looks at a callee's definition for a direct call either, only its
   declaration.  See ip_virtual_call_declared_target.

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
#include "sbitmap.h"

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

/* Forward "reaching definitions" dataflow for VAR alone -- which of
   VAR's own defining statements (ip_defines_var_p) could still be the
   value VAR holds at a given program point.  The classical technique
   Definite Assignment Analysis is itself built from, scoped to one
   tracked variable at a time (the same granularity every other query
   in this file already operates at).  Replaces this file's former
   single-answer ip_nearest_write_before (a same-block scan, then an
   immediate-dominator-chain walk) -- confirmed, not assumed, to be a
   genuine false negative on a diamond (two branches each establishing
   a different binding, merging before a use): neither arm block is an
   ancestor of the merge block in the dominator tree, so that walk
   found nothing there and silently gave up, rather than correctly
   reporting BOTH arms' definitions as simultaneously live.  This
   dataflow reports exactly that set, however large, the same way
   init-profile-gimple.cc's own block-level fixed-point DAA (added to
   fix the identical dominance-only bug for [[uninit]] locals) already
   does for its own, simpler single-boolean question -- see this
   project's own invalidation-profile-spec.html for the full account
   of why the single-answer technique was unsound here specifically,
   not merely less precise.

   Each of VAR's own defining statements is given a small integer
   index (DEFS, discovered by one linear walk before the fixed-point
   loop runs) and tracked as one bit of a small, per-block sbitmap --
   ordinary reaching-definitions machinery, the same representation
   GCC's own optimizer passes use for this exact kind of problem.
   Monotonic and guaranteed to terminate for the same reason
   ip_compute_owner_reach_info's own comment already gives for its
   analogous loop: a finite, fixed number of blocks and definitions,
   with each block's own "does it define VAR, and which definition is
   its own last one" fact fixed for the whole computation -- only the
   incoming (unioned) set can still change, and it can only grow.  */

struct ip_var_reach_info
{
  /* DEFS[i] is VAR's i-th own defining statement.  block_in[bb]/
     block_out[bb] are DEFS.length()-bit sets: bit i set means DEFS[i]
     could still be the value VAR holds at the start/end of that
     block.  Owns its own per-block sbitmaps (freed in the
     destructor); DEFS itself is just a lookup table, not owned
     resources.  */
  auto_vec<gimple *> defs;
  auto_vec<sbitmap> block_in;
  auto_vec<sbitmap> block_out;

  ~ip_var_reach_info ()
  {
    for (unsigned i = 0; i < block_in.length (); ++i)
      sbitmap_free (block_in[i]);
    for (unsigned i = 0; i < block_out.length (); ++i)
      sbitmap_free (block_out[i]);
  }
};

static void
ip_compute_var_reach_info (function *fun, tree var, ip_var_reach_info *info)
{
  unsigned n = last_basic_block_for_fn (fun);
  auto_vec<int> own_def_index;
  own_def_index.safe_grow (n);

  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    {
      own_def_index[bb->index] = -1;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	if (ip_defines_var_p (gsi_stmt (gsi), var))
	  {
	    gimple *stmt = gsi_stmt (gsi);
	    int idx = -1;
	    for (unsigned i = 0; i < info->defs.length (); ++i)
	      if (info->defs[i] == stmt)
		{
		  idx = (int) i;
		  break;
		}
	    if (idx < 0)
	      {
		idx = (int) info->defs.length ();
		info->defs.safe_push (stmt);
	      }
	    own_def_index[bb->index] = idx;
	  }
    }

  unsigned k = info->defs.length ();
  info->block_in.safe_grow (n);
  info->block_out.safe_grow (n);
  for (unsigned i = 0; i < n; ++i)
    {
      info->block_in[i] = sbitmap_alloc (MAX (k, 1u));
      info->block_out[i] = sbitmap_alloc (MAX (k, 1u));
      bitmap_clear (info->block_in[i]);
      bitmap_clear (info->block_out[i]);
    }
  if (k == 0)
    return;

  bool changed = true;
  while (changed)
    {
      changed = false;
      FOR_EACH_BB_FN (bb, fun)
	{
	  auto_sbitmap in (k);
	  bitmap_clear (in);
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    bitmap_ior (in, in, info->block_out[e->src->index]);
	  if (!bitmap_equal_p (in, info->block_in[bb->index]))
	    {
	      bitmap_copy (info->block_in[bb->index], in);
	      changed = true;
	    }

	  auto_sbitmap out (k);
	  if (own_def_index[bb->index] >= 0)
	    {
	      bitmap_clear (out);
	      bitmap_set_bit (out, own_def_index[bb->index]);
	    }
	  else
	    bitmap_copy (out, info->block_in[bb->index]);
	  if (!bitmap_equal_p (out, info->block_out[bb->index]))
	    {
	      bitmap_copy (info->block_out[bb->index], out);
	      changed = true;
	    }
	}
    }
}

/* The set of VAR's own defining statements that could still be live
   immediately before STMT -- same-block backward scan first (a
   single basic block never has an internal diamond, so this is
   always exact and unambiguous when it finds anything), falling back
   to INFO's own block_in set for STMT's block otherwise.  Pushes each
   live definition's own gimple* onto *OUT (cleared first).  An empty
   *OUT means "no definition reaches here at all" (the same meaning
   the former ip_nearest_write_before's own NULL had); more than one
   element means a diamond -- multiple, genuinely different
   definitions are simultaneously live, which the former technique
   could not represent.  */

static void
ip_var_reaching_defs_before_stmt (ip_var_reach_info *info, tree var,
				   gimple *stmt, auto_vec<gimple *> *out)
{
  out->truncate (0);
  basic_block bb = gimple_bb (stmt);
  for (gimple_stmt_iterator gsi = gsi_for_stmt (stmt); !gsi_end_p (gsi);)
    {
      gsi_prev (&gsi);
      if (gsi_end_p (gsi))
	break;
      gimple *s = gsi_stmt (gsi);
      if (ip_defines_var_p (s, var))
	{
	  out->safe_push (s);
	  return;
	}
    }
  if ((unsigned) bb->index >= info->block_in.length ())
    return;
  sbitmap in = info->block_in[bb->index];
  unsigned bit;
  sbitmap_iterator sbi;
  EXECUTE_IF_SET_IN_BITMAP (in, 0, bit, sbi)
    out->safe_push (info->defs[bit]);
}

/* Resolve DECL's own reaching definition(s) immediately before POINT
   via ip_var_reach_info, computed fresh here for DECL (a lightweight,
   on-demand computation -- this is only ever called from a handful of
   shallow, depth-guarded recursive hops, never from a hot loop).
   Returns the unique reaching definition when there is exactly one
   (full precision, matching every case the former technique already
   handled correctly); returns NULL when there are zero, OR when there
   is more than one -- a diamond reached while resolving an internal
   copy/pointer-arithmetic hop, not at the top-level use itself.  That
   second case is a deliberate, narrower residual scope limit, not a
   silent regression: it conservatively declines to establish a
   binding at all, the same safe fallback this file already takes
   whenever it can't prove something, one hop deeper than this
   change's primary target (see this project's own plan notes for why
   full precision through an arbitrarily deep alias chain is left as
   a possible, separate follow-up).

   Deliberately keeps its own original two-argument signature (no
   FUNCTION * parameter) and consults the global cfun instead: this is
   called from several places, including deep inside the mutually
   recursive pointer/container escape-analysis group
   (ip_var_contents_escape_locally_p and friends, further down), none
   of which otherwise need a FUNCTION * of their own -- threading one
   through that whole group just for this one leaf call would be a
   much larger, unrelated-looking diff for no real benefit.  cfun is
   safe to rely on here specifically: profiles_eager_check_function_1
   (profiles.cc) brackets this entire pass's execution, this function
   included, in push_cfun (DECL_STRUCT_FUNCTION (fndecl)) / pop_cfun
   (), so cfun is always the exact same function this whole checker is
   currently examining -- confirmed by reading that bracketing, not
   assumed.  */

static gimple *
ip_nearest_write_before (tree decl, gimple *point)
{
  ip_var_reach_info info;
  ip_compute_var_reach_info (cfun, decl, &info);
  auto_vec<gimple *> reaching;
  ip_var_reaching_defs_before_stmt (&info, decl, point, &reaching);
  return reaching.length () == 1 ? reaching[0] : NULL;
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
   to that receiver (ip_receiver_decl) -- including a virtually-
   dispatched accessor (e.g. a polymorphic 'data()'), resolved via
   ip_virtual_call_declared_target the same way ip_collect_mutations
   resolves a virtual mutating call, since a covariant-return override
   cannot change whether the return type has this pointer/reference/
   class shape, only how derived the pointee is; a plain copy, or pointer
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

/* If CALL is an indirect (OBJ_TYPE_REF) dispatch through a class-typed
   receiver, return the FUNCTION_DECL declared, in the receiver's own
   STATIC type, at the vtable slot the call's own token names -- or
   NULL_TREE if CALL is not such a dispatch, or the slot can't be
   resolved this way (no BINFO, e.g. an incomplete type).

   Used by both ip_binding_established_by (a virtually-dispatched
   accessor, e.g. a polymorphic 'data()', must still be able to
   establish a binding to its receiver) and ip_collect_mutations (a
   virtually-dispatched non-const call must still be classified as
   mutating) -- the same underlying gap, closed once, here.

   Reuses the exact same OBJ_TYPE_REF_OBJECT/OBJ_TYPE_REF_TOKEN
   extraction ip_owner_deleting_dtor_dispatch_p already established for
   the unrelated owner-consumption checker, generalized from "match one
   specific known candidate function's own DECL_VINDEX" to "search the
   static type's own BINFO_VIRTUALS for whichever entry's DECL_VINDEX
   matches" -- needed here because, unlike that destructor-specific
   caller, the candidate function isn't known in advance.

   The result is safe to use only for reading a DECLARED, override-
   invariant property off it, never for reasoning about what the call's
   own definition does.  [class.virtual] requires every valid override
   at a given vtable slot to share the identical parameter-type-list
   and cv-qualification of the function that first introduced that slot
   (a mismatch there means a DIFFERENT, hiding function with its own
   separate slot, not an override reachable via this token at all) --
   so the constness of whichever declaration this function returns is
   guaranteed identical to whatever the call actually dispatches to at
   runtime, regardless of dynamic type; likewise its return type's
   POINTER_TYPE/REFERENCE_TYPE/RECORD_TYPE/UNION_TYPE shape, since a
   covariant-return override may narrow to a more-derived pointee but
   cannot change that shape.  [[not_invalidating]], by contrast, is a
   library-only marker with no such language-enforced override
   consistency -- callers must not trust it when FNDECL was found this
   way (see ip_collect_mutations's own comment for how it avoids doing
   so).  */

static tree
ip_virtual_call_declared_target (gcall *call)
{
  tree fn = gimple_call_fn (call);
  if (!fn || TREE_CODE (fn) != OBJ_TYPE_REF)
    return NULL_TREE;
  tree token = OBJ_TYPE_REF_TOKEN (fn);
  if (!token || TREE_CODE (token) != INTEGER_CST)
    return NULL_TREE;
  tree obj = OBJ_TYPE_REF_OBJECT (fn);
  if (!obj
      || (TREE_CODE (TREE_TYPE (obj)) != POINTER_TYPE
	  && TREE_CODE (TREE_TYPE (obj)) != REFERENCE_TYPE))
    return NULL_TREE;
  tree type = TREE_TYPE (TREE_TYPE (obj));
  if (!CLASS_TYPE_P (type) || !TYPE_BINFO (type))
    return NULL_TREE;
  for (tree bv = BINFO_VIRTUALS (TYPE_BINFO (type)); bv; bv = TREE_CHAIN (bv))
    {
      tree bv_fn = BV_FN (bv);
      if (bv_fn && DECL_VINDEX (bv_fn)
	  && TREE_CODE (DECL_VINDEX (bv_fn)) == INTEGER_CST
	  && tree_int_cst_equal (DECL_VINDEX (bv_fn), token))
	return bv_fn;
    }
  return NULL_TREE;
}

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
      if (!fndecl)
	fndecl = ip_virtual_call_declared_target (call);
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

   A directly-unresolvable (virtual/indirect) CALL is handled too:
   gimple_call_fndecl returns NULL_TREE for those, so FNDECL below
   falls back to ip_virtual_call_declared_target.  When that fallback
   is what found FNDECL (VIA_VIRTUAL_DISPATCH), [[not_invalidating]] is
   deliberately NOT honored, on either source above, even if the
   resolved declaration carries it: unlike constness, nothing enforces
   that every override of a [[not_invalidating]]-marked virtual
   function is itself [[not_invalidating]], so trusting a marking found
   only via the receiver's STATIC type -- which may not be the type
   whose override actually executes -- would be unsound, not merely
   imprecise.  Every other property this function reads off FNDECL
   (constness, parameter types) is override-invariant by
   [class.virtual] itself and is trusted unconditionally either way.  */

static void
ip_collect_mutations (gcall *call, vec<ip_mutation> *out)
{
  bool via_virtual_dispatch = false;
  tree fndecl = gimple_call_fndecl (call);
  if (!fndecl)
    {
      fndecl = ip_virtual_call_declared_target (call);
      via_virtual_dispatch = fndecl != NULL_TREE;
    }
  if (!fndecl)
    return;

  if (DECL_IOBJ_MEMBER_FUNCTION_P (fndecl) && !DECL_CONSTRUCTOR_P (fndecl)
      && !DECL_CONST_MEMFUNC_P (fndecl)
      && (via_virtual_dispatch || !profiles_not_invalidating_p (fndecl))
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
      if (!via_virtual_dispatch
	  && profiles_not_invalidating_at_position_p (fndecl, i + 1))
	continue;
      if (tree decl = ip_receiver_decl (gimple_call_arg (call, i)))
	out->safe_push ({ decl, pointee });
    }
}

/* One trackable operand read, recorded during the initial statement
   walk in ip_check_function and checked afterward by ip_check_var_
   uses, one distinct tracked variable at a time.  */

struct ip_use
{
  gimple *stmt;
  tree var;
};

/* If STMT is one of MUTATING_CALLS whose own recorded (decl, type)
   pair (MUTATED_DECLS/MUTATED_TYPES, same index) isn't provably
   distinct from/unrelated to BOUND_DECL, return that mutated decl;
   else NULL_TREE.  The exact safety test Rule #0/#1 has always used,
   factored out so both the block-transfer function and the query
   function below share one definition instead of two copies that
   could drift apart.  */

static tree
ip_first_relevant_mutation (gimple *stmt, tree bound_decl,
			     const vec<gimple *> &mutating_calls,
			     const vec<tree> &mutated_decls,
			     const vec<tree> &mutated_types)
{
  for (unsigned i = 0; i < mutating_calls.length (); ++i)
    if (mutating_calls[i] == stmt)
      {
	tree mutated_decl = mutated_decls[i];
	bool safe = (mutated_decl != bound_decl
		     && (ip_decls_provably_distinct_objects_p (mutated_decl,
								bound_decl)
			 || ip_types_provably_unrelated_p (mutated_types[i],
							    TREE_TYPE (bound_decl))));
	if (!safe)
	  return mutated_decl;
      }
  return NULL_TREE;
}

/* Forward, monotonic, OR-across-predecessors dataflow (GEN-only, no
   kill -- once possibly mutated since this specific binding, stays
   possibly mutated for the rest of its relevance; the same "ever"
   shape ip_compute_owner_ever_owned_info uses, not the GEN/KILL shape
   ip_compute_owner_reach_info uses): "may BOUND_DECL have been
   mutated since BINDING_STMT, on some path reaching this point".
   Replaces this file's former ip_use_after_mutation_p, which used
   dominated_by_p -- a UNIVERSAL "happens on every path" test -- to
   answer what should be an EXISTENTIAL "could this happen on some
   path" question: backwards for a default-deny profile, and a second,
   independent diamond defect from ip_nearest_write_before's own (a
   mutation present on only one arm between a binding and a use used
   to fail that universal dominance test and be silently treated as
   irrelevant).  Seeding at BINDING_STMT rather than function entry is
   the one difference from ip_owner_block_transfer's own shape; see
   ip_mutated_since_block_transfer's own ACTIVE gate below for how a
   specific mid-block starting statement is handled without needing a
   separate same-block special case (mirrors how ip_owner_block_
   transfer's own per-statement GEN scan already handles "becomes true
   starting at a specific statement, not before" for its own ordinary,
   non-parameter case).  */

/* A second, simpler, purely monotonic dataflow (boolean OR-forward,
   GEN-only, no kill): "is this program point definitely reachable via
   a path that has already passed through BINDING_STMT".  Needed
   because a plain per-block "bb == binding_stmt's own block" test is
   NOT the same question and gets it wrong for any block that is a
   sibling of, or otherwise unrelated to, binding_stmt's own block --
   confirmed directly (not assumed) via a real diamond: with a two-arm
   branch, one arm establishing a binding for VAR and the other
   mutating a COMPLETELY UNRELATED container, an earlier draft of this
   file's own ip_mutated_since_block_transfer treated the unrelated
   arm as unconditionally "already past the binding" (since it isn't
   binding_stmt's own block) and scanned it for mutations regardless
   of whether it is even reachable from the binding at all -- which,
   for a container genuinely unrelated to the tracked binding, still
   produced the right answer by luck (ip_decls_provably_distinct_
   objects_p correctly ruled it out), but for a PREDECESSOR block of
   binding_stmt's own block containing a mutation of THE SAME decl
   BEFORE the binding (e.g. 'v1.push_back(...); p = v1.data();' as two
   separate statements in two separate blocks) it wrongly counted a
   mutation that had already happened before the binding even started
   as if it happened after -- a real, confirmed false positive this
   dataflow exists specifically to close.  Every block other than
   binding_stmt's own is a plain OR-passthrough (its own "reached"
   state is exactly whatever reaches it from its predecessors, no GEN
   of its own); binding_stmt's own block is the one and only place
   reachability can newly become true, from function-internal flow
   alone, partway through processing that one block.  */

struct ip_reached_from_info
{
  auto_vec<bool> block_in;
  auto_vec<bool> block_out;
};

static void
ip_compute_reached_from_info (function *fun, gimple *binding_stmt,
			       ip_reached_from_info *info)
{
  unsigned n = last_basic_block_for_fn (fun);
  info->block_in.safe_grow_cleared (n);
  info->block_out.safe_grow_cleared (n);
  basic_block binding_bb = gimple_bb (binding_stmt);

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
	  /* Reachability becomes true somewhere inside binding_bb itself
	     regardless of IN (that block's own exit is always reached,
	     since it contains binding_stmt), and simply propagates
	     forward (OR) everywhere else -- the exact statement-level
	     position within binding_bb is handled separately, by the
	     ACTIVE gates in ip_mutated_since_block_transfer/ip_mutated_
	     since_before_stmt_p below, which both already know to treat
	     binding_bb as special.  */
	  bool out = info->block_in[bb->index] || (bb == binding_bb);
	  if (out != info->block_out[bb->index])
	    {
	      info->block_out[bb->index] = out;
	      changed = true;
	    }
	}
    }
}

/* Forward, monotonic, OR-across-predecessors dataflow (GEN-only, no
   kill -- once possibly mutated since this specific binding, stays
   possibly mutated for the rest of its relevance; the same "ever"
   shape ip_compute_owner_ever_owned_info uses, not the GEN/KILL shape
   ip_compute_owner_reach_info uses): "may BOUND_DECL have been
   mutated since BINDING_STMT, on some path reaching this point, given
   that point is actually reachable from the binding at all"
   (ip_reached_from_info above is what answers that last, necessary
   qualifier).  Replaces this file's former ip_use_after_mutation_p,
   which used dominated_by_p -- a UNIVERSAL "happens on every path"
   test -- to answer what should be an EXISTENTIAL "could this happen
   on some path" question: backwards for a default-deny profile, and a
   second, independent diamond defect from ip_nearest_write_before's
   own (a mutation present on only one arm between a binding and a use
   used to fail that universal dominance test and be silently treated
   as irrelevant).  */

struct ip_mutated_since_info
{
  /* Indexed by basic_block->index.  NULL_TREE = provably not yet
     mutated on any path reaching this point; otherwise, one concrete
     mutated decl that broke the proof (arbitrary but stable choice
     when more than one candidate exists -- soundness only needs SOME
     witness for the diagnostic, not the first one in program order).  */
  auto_vec<tree> block_in;
  auto_vec<tree> block_out;
};

/* REACHED_AT_ENTRY is ip_reached_from_info's own block_in for BB --
   whether BB's own start is already known-reachable from BINDING_STMT
   (true for every block strictly downstream of binding_stmt's own
   block, false for binding_stmt's own block itself on the common,
   non-looping path, and for any block not reachable from the binding
   at all).  Statements before ACTIVE becomes true are always
   irrelevant, whether that's because they textually precede
   BINDING_STMT within its own block, or because BB isn't reachable
   from the binding yet at all -- both cases collapse to the same
   ACTIVE-starts-false handling here.  */

static tree
ip_mutated_since_block_transfer (basic_block bb, tree in, gimple *binding_stmt,
				  bool reached_at_entry, tree bound_decl,
				  const vec<gimple *> &mutating_calls,
				  const vec<tree> &mutated_decls,
				  const vec<tree> &mutated_types)
{
  tree state = in;
  bool active = reached_at_entry;
  bool own_block = (gimple_bb (binding_stmt) == bb);
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (!active)
	{
	  if (own_block && stmt == binding_stmt)
	    active = true;
	  continue;
	}
      if (!state)
	state = ip_first_relevant_mutation (stmt, bound_decl, mutating_calls,
					     mutated_decls, mutated_types);
    }
  return state;
}

static void
ip_compute_mutated_since_info (function *fun, gimple *binding_stmt,
				tree bound_decl,
				const vec<gimple *> &mutating_calls,
				const vec<tree> &mutated_decls,
				const vec<tree> &mutated_types,
				ip_mutated_since_info *info)
{
  ip_reached_from_info reached;
  ip_compute_reached_from_info (fun, binding_stmt, &reached);

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
	  tree in = NULL_TREE;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    if (info->block_out[e->src->index])
	      {
		in = info->block_out[e->src->index];
		break;
	      }
	  if (in != info->block_in[bb->index])
	    {
	      info->block_in[bb->index] = in;
	      changed = true;
	    }

	  tree out = ip_mutated_since_block_transfer (bb, info->block_in[bb->index],
						       binding_stmt,
						       reached.block_in[bb->index],
						       bound_decl, mutating_calls,
						       mutated_decls,
						       mutated_types);
	  if (out != info->block_out[bb->index])
	    {
	      info->block_out[bb->index] = out;
	      changed = true;
	    }
	}
    }
}

/* Same shape as ip_owner_unconsumed_before_stmt_p's own query
   function: start from INFO's own block-level fact for POINT's block,
   then re-run the identical per-statement scan ip_mutated_since_
   block_transfer already uses, but stopping at POINT instead of
   running to the block's end.  Returns the same "witness" mutated
   decl ip_mutated_since_block_transfer would (or NULL_TREE if none),
   directly usable in the caller's own diagnostic.  Recomputes ip_
   reached_from_info fresh (cheap, and this is only ever called a
   handful of times per binding, not from a hot loop) rather than
   threading it through from ip_compute_mutated_since_info -- keeps
   this function's own signature matching its former shape.  */

static tree
ip_mutated_since_before_stmt_p (function *fun, ip_mutated_since_info *info,
				 gimple *binding_stmt, tree bound_decl,
				 const vec<gimple *> &mutating_calls,
				 const vec<tree> &mutated_decls,
				 const vec<tree> &mutated_types,
				 gimple *point)
{
  basic_block bb = gimple_bb (point);
  ip_reached_from_info reached;
  ip_compute_reached_from_info (fun, binding_stmt, &reached);

  tree state = info->block_in[bb->index];
  bool active = reached.block_in[bb->index];
  bool own_block = (gimple_bb (binding_stmt) == bb);
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == point)
	break;
      if (!active)
	{
	  if (own_block && stmt == binding_stmt)
	    active = true;
	  continue;
	}
      if (!state)
	state = ip_first_relevant_mutation (stmt, bound_decl, mutating_calls,
					     mutated_decls, mutated_types);
    }
  return state;
}

/* Rule #0/#1's own top-level driver for one tracked variable VAR:
   USES holds every collected read of VAR (and any other variable --
   filtered to VAR's own entries here) against MUTATING_CALLS/
   MUTATED_DECLS/MUTATED_TYPES, the whole function's own flat list of
   candidate mutations.

   Computed once per VAR, not once per (use, mutation) pair the way
   this file used to: a reaching-definitions dataflow for VAR itself
   (ip_compute_var_reach_info) finds every statement that could still
   be defining VAR at each use -- possibly more than one at a genuine
   diamond -- and, for each such candidate binding, a fresh "may have
   been mutated since" dataflow (ip_compute_mutated_since_info) is
   computed once and then simply queried at every use it could reach.
   A binding whose own ip_binding_established_by can't resolve a
   container at all is skipped (nothing to check it against), the
   same as before.  */

static void
ip_check_var_uses (function *fun, tree var, const vec<ip_use> &uses,
		    const vec<gimple *> &mutating_calls,
		    const vec<tree> &mutated_decls,
		    const vec<tree> &mutated_types, tree enclosing_fndecl)
{
  ip_var_reach_info reach;
  ip_compute_var_reach_info (fun, var, &reach);

  unsigned k = reach.defs.length ();
  auto_vec<tree> bound_decls;
  auto_vec<ip_mutated_since_info *> mutated_infos;
  bound_decls.safe_grow (k);
  mutated_infos.safe_grow (k);
  for (unsigned d = 0; d < k; ++d)
    {
      tree bound_decl = ip_binding_established_by (reach.defs[d]);
      bound_decls[d] = bound_decl;
      if (!bound_decl)
	{
	  mutated_infos[d] = NULL;
	  continue;
	}
      mutated_infos[d] = new ip_mutated_since_info ();
      ip_compute_mutated_since_info (fun, reach.defs[d], bound_decl,
				      mutating_calls, mutated_decls,
				      mutated_types, mutated_infos[d]);
    }

  for (unsigned u = 0; u < uses.length (); ++u)
    {
      if (uses[u].var != var)
	continue;
      gimple *use_stmt = uses[u].stmt;
      auto_vec<gimple *> reaching;
      ip_var_reaching_defs_before_stmt (&reach, var, use_stmt, &reaching);
      for (unsigned r = 0; r < reaching.length (); ++r)
	{
	  unsigned idx = k;
	  for (unsigned d = 0; d < k; ++d)
	    if (reach.defs[d] == reaching[r])
	      {
		idx = d;
		break;
	      }
	  if (idx == k || !bound_decls[idx] || use_stmt == reaching[r])
	    continue;

	  tree culprit
	    = ip_mutated_since_before_stmt_p (fun, mutated_infos[idx],
					       reach.defs[idx],
					       bound_decls[idx], mutating_calls,
					       mutated_decls, mutated_types,
					       use_stmt);
	  if (!culprit)
	    continue;
	  if (!profiles_diagnostic_exempt_p (gimple_location (use_stmt),
					      enclosing_fndecl, "std::invalidation"))
	    error_at (gimple_location (use_stmt),
		      "use of a value bound to %qD, potentially invalidated "
		      "by an earlier mutation of %qD, not permitted under the "
		      "%<std::invalidation%> profile", bound_decls[idx], culprit);
	  break;
	}
    }

  for (unsigned d = 0; d < k; ++d)
    delete mutated_infos[d];
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

/* True if CALL is itself a fresh owner-flavored source: a 'new T'
   allocation, or a call to a function whose own return is marked
   [[owning_ptr]]/[[owner]].  Factored out so ip_arg_owner_flavored_p_1
   (RHS-flavor recognition for local binding establishment, P3446R0
   S7.6.2) and ip_owner_resolve_origin (multi-hop value-provenance
   resolution, further down) test the exact same thing and can't drift
   apart.  */

static bool
ip_owner_fresh_source_call_p (gcall *call)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return false;
  /* DECL_IS_OPERATOR_NEW_P is a plain FUNCTION_DECL property (a
     distinct field, unrelated to the CALL_FROM_NEW_OR_DELETE_P
     call-site flag that aliases CALL_FROM_THUNK_P/CALL_ALLOCA_FOR_
     VAR_P elsewhere -- see this project's own note on that), so no
     risk of misreading an unrelated call here.  */
  return profiles_owning_ptr_p (callee) || DECL_IS_OPERATOR_NEW_P (callee);
}

/* True if ARG (a call-argument, plain-assignment RHS, or return-value
   expression) is, provably, a FRESH owner-flavored source: a
   'new T' allocation, or a call to an owner-returning function --
   never a bare reference to some OTHER, already-established owner
   declaration (see ip_owner_resolve_origin below for why a plain copy
   of an existing owner value must NOT, by itself, start a second,
   independent tracked binding on its destination).

   Deliberately stops the chase the instant it reaches an SSA_NAME
   with a home variable (SSA_NAME_VAR), rather than continuing to
   chase THROUGH that variable's own reaching definition the way
   ip_owner_resolve_origin's multi-hop walk deliberately does: in SSA
   form, every version of a NAMED variable has its own well-defined
   SSA_NAME_DEF_STMT too, exactly like a pure anonymous temp does, so
   naively chasing through it here (as an earlier version of this
   function did) would keep walking straight through an existing,
   already-tracked variable's OWN history and misclassify a plain
   'y = x;' (x already owner-marked and already tracked) as ALSO
   being a fresh capture for y -- confirmed as a real, reproducible
   false positive, not a hypothetical one.  Only a genuinely anonymous
   SSA temp (no home var -- the shape a 'new'-expression's own result,
   or an owner-returning call's own result, is held in before being
   copied into a named destination, including any number of
   compiler-introduced anonymous-to-anonymous copies an EH cleanup
   region's own lowering may insert along the way) is chased through
   at all.

   Direct structural port of init-profile-gimple.cc's own
   ip_arg_uninit_flavored_p_1 for the SSA-copy/PHI-chasing shape only;
   see that function's own comment for the full rationale.  Drops the
   ADDR_EXPR branch entirely: [[ref_to_uninit]] tracks a POINTEE's
   state reached through '&var', but [[owning_ptr]]/[[owner]] tracks
   the pointer VALUE itself, which is never itself accessed via
   '&owner_var' in the relevant sense here.  */

static bool
ip_arg_owner_flavored_p_1 (tree arg, int depth)
{
  if (depth > 16)
    return false; /* Defensive recursion guard, as in the uninit
		      checker's own identical guard -- only a loop-carried
		      PHI (below) could even threaten to cycle.  */
  if (TREE_CODE (arg) != SSA_NAME)
    return false; /* A bare VAR_DECL/PARM_DECL (memory-resident, not
		      SSA-registered): reading a named variable's
		      current value is never itself a fresh source.  */
  if (SSA_NAME_VAR (arg))
    return false; /* A NAMED variable's own current value -- a read
		      of an existing, already-established binding,
		      never itself a fresh capture, regardless of how
		      THAT variable's own value first came to be (its
		      own freshness was already evaluated, once, at the
		      point IT was established).  */
  {
    gimple *def = SSA_NAME_DEF_STMT (arg); /* Only reached for a pure
						anonymous temp now.  */
    if (def && is_gimple_assign (def) && gimple_assign_single_p (def))
      return ip_arg_owner_flavored_p_1 (gimple_assign_rhs1 (def), depth + 1);
    if (def && gimple_code (def) == GIMPLE_CALL)
      return ip_owner_fresh_source_call_p (as_a<gcall *> (def));
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
    return false;
  }
}

/* One shared classification of what a value's provenance resolves to
   -- see ip_owner_resolve_origin below.  */

struct ip_owner_origin
{
  /* The canonical VAR_DECL/PARM_DECL this value's provenance traces
     back to, through any number of plain, single-operand copies --
     NULL_TREE if unresolvable.  */
  tree decl;
  /* True iff DECL is itself a genuine owner origin: an [[owner]]
     -marked PARM_DECL (owned unconditionally from function entry), or
     a local whose own establishing statement is a fresh new-
     expression or owner-returning call.  False for anything else (an
     unmarked parameter, a local with no further resolvable
     establishing write, or DECL == NULL_TREE).  */
  bool genuine;
};

/* Resolve T (a call-argument, assignment RHS, return-value, or
   similar operand expression) back to the canonical declaration its
   value provenance ultimately traces to, chasing through as many
   plain, single-operand copy assignments as needed -- not just the
   first named variable found, the way an earlier, one-hop-only
   version of this function did.  POINT is the statement T is being
   read at (needed by ip_resolve_defining_stmt's own CFG-position-
   aware reaching-write lookup, used uniformly here for both a pure
   anonymous SSA temporary and a real, memory-resident named
   variable).

   Reuses ip_resolve_defining_stmt, this file's own general-purpose
   "find the statement that most recently wrote this value" utility
   (built for, and already proven correct by, the Rule #0/#1 diamond-
   reassignment DAA fix -- a stateless utility, safe to reuse here
   without coupling owner-consumption's own state to Rule #0/#1's,
   matching this file's existing precedent of sharing ip_use_decl/
   ip_deref_base_decl across both subsystems).

   Deliberately does not resolve through a PHI merge (a pure-anonymous
   SSA temp whose own def is a GIMPLE_PHI is not itself a plain,
   single-operand assignment, so the walk stops there rather than
   descending into each incoming arm) -- the same "documented,
   permitted incompleteness" this file's own mechanical spec already
   accepts for pointer-arithmetic chain resolution (S5.4): resolve
   precisely through an unambiguous, straight-line chain of copies,
   and conservatively give up at a genuine merge, rather than
   attempting general points-to reasoning.  ip_nearest_write_before's
   own single-reaching-definition contract gives the same treatment to
   a diamond reached while resolving a NAMED variable's own nearest
   write.

   Also deliberately does not chase PAST a hop whose own destination
   is itself [[owner]]-declared -- that destination is a genuine hand-
   off boundary, paired exactly with ip_owner_reassigned_into_owner_
   var_p/CE5's own identical gate and with ip_owner_gen_lhs_decl's own
   matching GEN condition (both keyed on profiles_owning_ptr_p of the
   assignment's LHS): the value's tracked IDENTITY changes there, from
   whatever it traced back to before, to this destination's own new,
   independent binding.  Confirmed as a real, reproducible regression
   when this stop was missing: with the walk chasing straight through
   an owner-declared intermediate the same way it chases through an
   unmarked one, '[[owner]] int *y = x; f (y);' resolved 'y' in 'f
   (y)' all the way back to x's own (already CE5-consumed) binding
   instead of stopping at y's own, separately GEN'd one -- reporting x
   as "consumed again" for what CE5 and ip_owner_gen_lhs_decl already
   correctly modeled as one single, paired hand-off.  */

static ip_owner_origin
ip_owner_resolve_origin (tree t, gimple *point, int depth = 0)
{
  if (depth > 16)
    return { NULL_TREE, false };

  gimple *def_stmt = ip_resolve_defining_stmt (t, point);

  if (def_stmt && is_gimple_assign (def_stmt) && gimple_assign_single_p (def_stmt))
    {
      tree hop_lhs = ip_trackable_decl (gimple_assign_lhs (def_stmt));
      if (hop_lhs && profiles_owning_ptr_p (hop_lhs))
	return { hop_lhs, true };
      ip_owner_origin inner
	= ip_owner_resolve_origin (gimple_assign_rhs1 (def_stmt), def_stmt,
				    depth + 1);
      if (inner.decl)
	return inner;
      if (!inner.genuine)
	return { NULL_TREE, false };
      /* The chain bottomed out at a fresh source with no named decl of
	 its own yet (an anonymous-to-anonymous copy chain -- confirmed
	 to occur for real: an SSA temp crossing a try/finally region
	 boundary, the implicit EH cleanup a 'new'-expression's own
	 lowering wraps its store in, gets an extra anonymous copy of
	 this shape).  T's own named variable, if it has one, is the
	 first name to capture it, and becomes the canonical origin --
	 but genuineness itself must keep propagating regardless of
	 whether T happens to have one: an intermediate anonymous hop
	 (T itself also unnamed) is not a reason to give up, only a
	 reason to keep VAR as NULL_TREE one level further up.  */
      tree var = ip_trackable_decl (t);
      return { var, true };
    }

  if (def_stmt && gimple_code (def_stmt) == GIMPLE_CALL
      && ip_owner_fresh_source_call_p (as_a<gcall *> (def_stmt)))
    {
      tree var = ip_trackable_decl (t);
      return { var, true }; /* VAR may be NULL_TREE for a pure
				anonymous temp -- correctly "genuine, but
				not yet named"; the caller one level up
				(if any) is what names it.  */
    }

  tree var = ip_trackable_decl (t);
  if (!var)
    return { NULL_TREE, false };
  if (TREE_CODE (var) == PARM_DECL)
    return { var, profiles_owning_ptr_p (var) }; /* Owned from entry
						      iff marked -- an
						      unmarked parameter
						      is not a valid
						      owner origin.  */
  return { var, false }; /* A named local with no further resolvable
			     reaching write that's fresh -- not
			     genuine (DEF_STMT may be NULL entirely,
			     e.g. an address-taken var with no reaching
			     write found at all, or some other,
			     non-owner-related statement).  */
}

/* The DECL-only view of ip_owner_resolve_origin, used throughout
   CE1-CE6 consuming-event recognition, LP3's double-consumption
   check, LP4's read-after-consumption check, and same-call-arg-
   aliasing -- genuineness doesn't matter at any of those call sites,
   since a consuming/read check only ever compares the resolved decl
   against a V that ip_check_owner_binding's own driver is ALREADY
   iterating because it is a genuinely established binding; a
   resolution landing on anything else is simply never matched by any
   live driver iteration and has no effect.  The .genuine bit is used
   only by the three reverse-direction flavor checks further down.  */

static tree
ip_owner_resolve_underlying_decl (tree t, gimple *point)
{
  return ip_owner_resolve_origin (t, point).decl;
}

/* P3446R0/P4296R0 Phase 7a: for a direct call, check every argument at
   an owner-*accepting* parameter position -- ownership genuinely
   claimed by the callee.  Deliberately one-directional: an owner-
   flavored value handed to a plain, non-owner-accepting parameter is
   an ordinary, harmless "peek" (the destination isn't claiming
   ownership, so there's nothing to be inconsistent about -- the
   original binding stays responsible and stays checked by the
   definite-consumption layer below), not checked here at all.  The
   direction that DOES matter: the argument's own value provenance
   (ip_owner_resolve_origin, the same multi-hop chase every consuming-
   event check here already uses) must trace to a GENUINE owner
   origin -- an [[owner]]-marked parameter, or a fresh 'new'/owner-
   returning-call capture, however many plain, unmarked copies it
   passed through on the way here.  If it doesn't, the callee will
   eventually try to delete something this checker never saw allocated
   -- unsound, regardless of how many hops away the actual allocation
   (if any) might be.  */

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
      if (!profiles_owning_ptr_at_position_p (callee, i + 1))
	continue;
      tree arg = gimple_call_arg (stmt, i);
      if (ip_owner_arg_null_pointer_p (arg))
	continue;
      if (ip_owner_resolve_origin (arg, stmt).genuine)
	continue;
      if (profiles_diagnostic_exempt_p (gimple_location (stmt),
					enclosing_fndecl, "std::invalidation"))
	continue;
      error_at (gimple_location (stmt),
		"argument %u to %qD must be marked %<[[owner]]%>, matching "
		"its %<[[owner]]%> parameter, under the "
		"%<std::invalidation%> profile", i + 1, callee);
    }

  /* The RETURN-value counterpart, for a call whose result is assigned
     DIRECTLY into a named pointer (a GCC-recognized builtin's own
     lowering, or any other callee GCC chooses not to route through an
     anonymous SSA temporary) -- see init-profile-gimple.cc's own
     identical block for why this shape, though rare, is real and not
     dead code.  No multi-hop resolution needed here: the call's own
     result IS the value, with nothing yet to chase through -- just
     ip_owner_fresh_source_call_p's own genuineness test directly,
     the SAME test ip_owner_resolve_origin itself uses one level up
     (NOT a bare profiles_owning_ptr_p (callee) check: that alone
     would misclassify a direct-LHS 'new T' allocation, since operator
     new is never itself [[owner]]-marked).  */
  tree lhs = gimple_call_lhs (stmt);
  tree lhs_var = lhs ? ip_trackable_decl (lhs) : NULL_TREE;
  if (lhs_var && TREE_CODE (TREE_TYPE (lhs_var)) == POINTER_TYPE
      && profiles_owning_ptr_p (lhs_var)
      && !ip_owner_fresh_source_call_p (as_a<gcall *> (stmt))
      && !profiles_diagnostic_exempt_p (gimple_location (stmt),
					 enclosing_fndecl, "std::invalidation"))
    error_at (gimple_location (stmt),
	      "assigning a pointer not marked %<[[owner]]%> into a "
	      "pointer marked %<[[owner]]%>, under the "
	      "%<std::invalidation%> profile");
}

/* The RETURN-statement counterpart (P3446R0/P4296R0 Phase 7a): a
   function declared [[owner]] on its own return must only ever return
   a value whose provenance traces to a genuine owner origin -- same
   one-directional reasoning as the call-argument check above, applied
   to the return position.  Subsumes what used to be a special "trust
   a direct pass-through of an already-owner-declared parameter"
   exemption: ip_owner_resolve_origin already recognizes that shape
   (and any number of plain, unmarked hops in front of it) as genuine,
   with no separate carve-out needed.  NOTE this check's own
   interaction with the definite-consumption checker further below:
   'T* f([[owner]] T* p) { return p; }' with f's own return NOT
   [[owner]]-marked isn't reached by this function at all (the forward
   direction isn't checked here), but must still be flagged by the
   consumption checker as a genuine leak -- the caller now silently
   owns p with no marker saying so.  */

static void
ip_check_owner_return_flavor_consistency (gimple *stmt, tree enclosing_fndecl)
{
  if (gimple_code (stmt) != GIMPLE_RETURN)
    return;
  if (!profiles_owning_ptr_p (enclosing_fndecl))
    return;
  tree retval = gimple_return_retval (as_a<greturn *> (stmt));
  if (!retval || ip_owner_arg_null_pointer_p (retval))
    return;
  if (ip_owner_resolve_origin (retval, stmt).genuine)
    return;
  if (profiles_diagnostic_exempt_p (gimple_location (stmt),
				     enclosing_fndecl, "std::invalidation"))
    return;
  error_at (gimple_location (stmt),
	    "returning a pointer not marked %<[[owner]]%> from a function "
	    "marked %<[[owner]]%>, under the %<std::invalidation%> "
	    "profile");
}

/* The plain-assignment counterpart -- same one-directional reasoning
   as the two checks above, applied to an ordinary 'dst = src;'
   between a named pointer variable/parameter and any source
   expression.  Covers a declaration's own initializer and a cast for
   free, with no special-casing: 'T* q = p;' written as an initializer
   and 'q = p;' written as a later, separate assignment produce the
   identical GIMPLE_ASSIGN statement shape (confirmed via direct
   -fdump-tree-gimple reading), so there is no "is this an initializer"
   distinction to make at this level in the first place; a cast
   ('(T*) src') is handled the same way, since gimple_assign_rhs1
   returns the actual operand regardless of whether the assignment's
   own rhs_code is a bare copy or a NOP_EXPR/CONVERT_EXPR wrapping
   it.  */

static void
ip_check_owner_assign_flavor_consistency (gimple *stmt, tree enclosing_fndecl)
{
  if (!is_gimple_assign (stmt) || !gimple_assign_single_p (stmt))
    return;
  tree lhs = gimple_assign_lhs (stmt);
  if (TREE_CODE (TREE_TYPE (lhs)) != POINTER_TYPE)
    return;
  tree lhs_var = ip_trackable_decl (lhs);
  if (!lhs_var || !profiles_owning_ptr_p (lhs_var))
    return;
  tree rhs = gimple_assign_rhs1 (stmt);
  if (ip_owner_arg_null_pointer_p (rhs))
    return;
  if (ip_owner_resolve_origin (rhs, stmt).genuine)
    return;
  if (profiles_diagnostic_exempt_p (gimple_location (stmt),
				     enclosing_fndecl, "std::invalidation"))
    return;
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
      tree decl_i = ip_owner_resolve_underlying_decl (arg_i, call);
      if (!decl_i)
	continue;

      for (unsigned j = i + 1; j < nargs; ++j)
	{
	  if (!profiles_owning_ptr_at_position_p (callee, j + 1))
	    continue;
	  tree arg_j = gimple_call_arg (call, j);
	  if (ip_owner_arg_null_pointer_p (arg_j))
	    continue;
	  if (ip_owner_resolve_underlying_decl (arg_j, call) != decl_i)
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
  return ip_owner_resolve_underlying_decl (gimple_call_arg (call, 0), call) == v;
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
  if (ip_owner_resolve_underlying_decl (OBJ_TYPE_REF_OBJECT (fn), call) != v)
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

/* True if CALL is a DIRECT (non-virtual, statically-resolved) call to
   V's own destructor, with V as the "this" argument -- the shape
   EVERY delete-expression's own lowering produces for a non-
   polymorphic type (confirmed via -fdump-tree-gimple: 'delete p;'
   always lowers to 'S::~S (p); operator delete (p, size);', in that
   order, exactly like ip_owner_deleting_dtor_dispatch_p's own comment
   already documents for the polymorphic case).  This destructor call
   is part of the SAME delete-expression as the operator-delete call
   that follows it -- it is the consuming event's own internal
   machinery, not a separate, later use of V -- so it needs the exact
   same exemption ip_owner_delete_call_p already gets from its own
   caller below, and for the same reason: without it, the multi-hop
   resolver (ip_owner_resolve_underlying_decl) correctly tracing V's
   "this" argument back through whatever anonymous SSA copy the
   destructor call's own argument-evaluation happens to introduce
   would see V as already spent by the time this statement runs (the
   guard condition ip_owner_delete_guard_cond_p recognizes ALREADY
   killed MAYBE-UNCONSUMED one statement earlier, in the predecessor
   block) and leak point 4 would misfire on the destructor call
   itself -- confirmed as a real, reproducible false positive against
   d4324-profiles-invalidation-implicit-dtor-ok.C, not a hypothetical
   one.  Unconditional, matching ip_check_owner_call_flavor_
   consistency's own DECL_DESTRUCTOR_P exemption: the Negative
   Baseline already unconditionally bans every OTHER (explicit,
   user-written) destructor call, so any destructor call this checker
   ever sees is necessarily one of GCC's own implicitly-generated
   end-of-lifetime calls, never a user-observable "read."  */

static bool
ip_owner_direct_dtor_call_p (gcall *call, tree v)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee || !DECL_DESTRUCTOR_P (callee))
    return false;
  if (gimple_call_num_args (call) < 1)
    return false;
  return ip_owner_resolve_underlying_decl (gimple_call_arg (call, 0), call) == v;
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
	&& ip_owner_resolve_underlying_decl (gimple_call_arg (call, i), call) == v)
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
  return ip_owner_resolve_underlying_decl (gimple_assign_rhs1 (stmt), stmt) == v;
}

/* True if STMT hands V's ownership off to ANOTHER, independently
   tracked, owner-marked local/parameter via a plain copy -- e.g.
   '[[owner]] int *y = x;' -- exactly like storing into an owner-
   marked field (ip_owner_stored_into_field_p just above), just
   var-to-var instead of var-to-field: the pointer's value is now
   y's to consume, so this counts as consuming V just as much as any
   other recognized hand-off does, IMMEDIATELY at this statement --
   not merely "eventually, if y is itself later consumed somewhere."

   This is NOT redundant with ip_owner_resolve_origin's own multi-hop
   walk, despite both existing to solve "does a value handed to
   another name stay tracked": the multi-hop walk resolves a LATER
   consuming/reading event BACKWARD to whichever binding it ultimately
   traces to (so a later 'delete y;' correctly consumes x's own
   binding even without this function existing at all) -- but it says
   nothing about V's OWN state in the WINDOW between the hand-off and
   that later event.  Confirmed as a real, reproducible regression
   when this function was removed on the theory that the multi-hop
   walk alone was sufficient: 'y = x; int v = *x; delete y;' (x AND y
   both [[owner]]-declared) must flag the read of x, immediately after
   the hand-off and well before 'delete y;' ever runs -- without this
   function, x's own dataflow has nothing to GEN a "consumed" state
   from until 'delete y;' itself, so the read at 'int v = *x;' is
   missed entirely.  Required so a KNOWN ALIAS of V is tracked from
   the exact point of hand-off: without this, 'y = x; delete y; delete
   x;' (both marked [[owner]]) would also read as two independent,
   both-satisfied obligations -- a real double-free that would
   otherwise slip past every check (not a flavor mismatch, since that
   layer is now one-directional and unconcerned with this shape; not
   an unmarked delete, since y IS owner-marked; and not a same-decl
   double-consumption, since x and y are different decls) -- rather
   than x being correctly seen as already spent by the time 'delete
   x;' is reached.  Copying V into a NON-owner-marked local is a
   separate case, handled entirely by the multi-hop walk instead (no
   immediate hand-off happens there -- the destination isn't claiming
   ownership at all, so nothing should be GEN'd at that statement; see
   ip_arg_owner_flavored_p_1's own comment).  */

static bool
ip_owner_reassigned_into_owner_var_p (gimple *stmt, tree v)
{
  if (!is_gimple_assign (stmt) || !gimple_assign_single_p (stmt))
    return false;
  tree lhs = ip_trackable_decl (gimple_assign_lhs (stmt));
  if (!lhs || lhs == v || TREE_CODE (TREE_TYPE (lhs)) != POINTER_TYPE
      || !profiles_owning_ptr_p (lhs))
    return false;
  return ip_owner_resolve_underlying_decl (gimple_assign_rhs1 (stmt), stmt) == v;
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
  if (ip_owner_resolve_underlying_decl (ptr_operand, stmt) != v)
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
   recomputed per statement).  A std::owner_consumed (V) call only
   counts here when its own result is actually captured (gimple_call_
   lhs != NULL) -- a discarded 'std::owner_consumed (x);' statement
   asserts nothing was really done with the value, so it must NOT be
   trusted as a real hand-off (confirmed via -fdump-tree-gimple: a
   discarded call's return value is never materialized into an LHS
   at all, so this is a precise, not approximate, test).  */

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
	  && gimple_call_lhs (call) != NULL_TREE
	  && (ip_owner_resolve_underlying_decl (gimple_call_arg (call, 0), call)
	      == v))
	return true;
      return false;
    }
  if (gimple_code (stmt) == GIMPLE_RETURN)
    {
      if (!fn_return_is_owner)
	return false;
      tree retval = gimple_return_retval (as_a<greturn *> (stmt));
      return retval && ip_owner_resolve_underlying_decl (retval, stmt) == v;
    }
  return ip_owner_stored_into_field_p (stmt, v)
	 || ip_owner_reassigned_into_owner_var_p (stmt, v);
}

/* True if STMT reads V in a way this checker treats as dangerous once
   V has already been consumed -- either a genuine dereference of V
   (reads the POINTEE directly: '*v', 'v->m', 'v[i]', or a write
   THROUGH it, '*v = ...;', which needs V to be valid just as much),
   or handing V's own value to a call (any argument position, or a
   RETURN in a non-owner-marked function) -- both lose this checker's
   own visibility into what happens to the value next (the callee, or
   the caller after return, might dereference it; "never read a
   callee's definition" means we cannot know).  Deliberately NOT
   flagged: a plain, same-function value-copy of V into another
   ordinary variable ('tmp = v;', no dereference).  Copying a pointer
   BIT PATTERN around is not itself unsafe -- only dereferencing it,
   or losing visibility into it, is -- and treating a bare copy as
   "a read" was tried first and produces a real false positive: a
   delete-expression's own implicit null-guard temp-load ('D.1 = v;'
   feeding 'if (D.1 != 0) ...') is exactly this shape, confirmed via
   the mandatory suite catching it immediately.

   The dereference base and the call-argument/return operand are both
   resolved through ip_owner_resolve_underlying_decl's own multi-hop
   chase, not just a single SSA_NAME_VAR unwrap -- so a LATER
   dereference through an untracked plain copy ('tmp = v; ... *tmp;')
   IS caught here, resolving 'tmp' back to V, exactly like every
   consuming-event check (CE1-CE6) already does.  This used to be a
   documented, narrower residual gap (a plain copy was only tracked
   through when its destination also happened to be owner-marked);
   generalizing the shared resolver closes it for reads the same way
   it closes it for consuming events.

   Callers are expected to check ip_owner_consuming_stmt_p FIRST and
   skip this entirely when it's true (checked at whole-statement
   granularity, matching ip_owner_consuming_stmt_p's own granularity
   -- a single statement that both consumes V via one argument and
   reads it via a different, aliased argument is classified as
   consuming only; a documented, narrower residual scope limit, not
   fixed here).  GIMPLE_COND operands are not scanned, matching Rule
   #0/#1's own use-collection, which likewise never inspects a
   condition's own operands.  */

static bool
ip_owner_stmt_reads_decl_p (gimple *stmt, tree v)
{
  if (gcall *call = dyn_cast<gcall *> (stmt))
    {
      for (unsigned i = 0; i < gimple_call_num_args (call); ++i)
	if (ip_owner_resolve_underlying_decl (gimple_call_arg (call, i), call)
	    == v)
	  return true;
      return false;
    }
  if (is_gimple_assign (stmt) && gimple_assign_single_p (stmt))
    {
      tree rhs_base = ip_deref_base_decl (gimple_assign_rhs1 (stmt));
      if (rhs_base && ip_owner_resolve_underlying_decl (rhs_base, stmt) == v)
	return true;
      tree lhs_base = ip_deref_base_decl (gimple_assign_lhs (stmt));
      return lhs_base && ip_owner_resolve_underlying_decl (lhs_base, stmt) == v;
    }
  if (gimple_code (stmt) == GIMPLE_RETURN)
    {
      tree retval = gimple_return_retval (as_a<greturn *> (stmt));
      return retval && ip_owner_resolve_underlying_decl (retval, stmt) == v;
    }
  return false;
}

/* If STMT establishes a fresh, independently-tracked binding for some
   trackable local VAR_DECL, return that VAR_DECL; else NULL_TREE.
   This is how a local variable "becomes owned" -- distinct from a
   PARM_DECL, which is owned unconditionally from function entry
   instead.  Two, independent reasons a plain assignment establishes
   one:

   - The RHS is itself a FRESH owner-flavored source (ip_arg_owner_
     flavored_p: a 'new'-expression, or a call to an owner-returning
     function) -- a genuinely new obligation with nowhere else to
     attach, so it starts tracking on whatever LOCAL variable first
     captures it, regardless of that variable's own [[owner]] marking
     (P3446R0/P4296R0's own point: 'int *q = new int (42);' must be
     tracked and flagged if forgotten, even though 'q' itself carries
     no attribute at all).

   - The DESTINATION is itself explicitly [[owner]]-declared, and the
     RHS traces to SOME already-tracked value ('[[owner]] int *y =
     x;') -- a genuine hand-off, paired exactly with ip_owner_
     reassigned_into_owner_var_p/CE5's own identical gate
     (profiles_owning_ptr_p on the destination): CE5 recognizes this
     same statement as consuming the SOURCE's binding immediately, and
     this is what starts tracking the DESTINATION's own, independent
     replacement binding, so a further use under y's own name (e.g. a
     later 'f (y);') is checked against y, not incorrectly re-resolved
     all the way back to x's already-spent binding by the multi-hop
     walk (ip_owner_resolve_origin) and double-counted as consuming x
     a second time -- confirmed as a real, reproducible regression
     when this half was missing: with only the "fresh source" half of
     this function, 'y = x; f (y);' saw 'f (y)' resolve straight
     through to x's own binding (since y had no binding of its own to
     stop at), reporting x as "consumed again" for what is really one
     single, correctly-paired hand-off.

     Deliberately NOT triggered by a plain copy into a destination
     that is NOT itself [[owner]]-declared ('int *q = p;', p already
     tracked) -- see ip_arg_owner_flavored_p_1's own comment for why:
     that shape is an ordinary, harmless "peek", not a hand-off, and
     must not start any tracking of its own.

   A direct-LHS call ('lhs = owner_returning_fn (...);' -- the same
   rare but real GCC-recognized-builtin-shaped direct-assignment case
   ip_check_owner_call_flavor_consistency's own direct-LHS block
   exists for) is handled by the second branch below, fresh-source
   only (a call's own direct-LHS shape has no destination-owner-
   declared counterpart to pair with here, since CE5 only ever
   recognizes a plain, single-operand copy).  */

static tree
ip_owner_gen_lhs_decl (gimple *stmt)
{
  if (is_gimple_assign (stmt) && gimple_assign_single_p (stmt))
    {
      tree d = ip_trackable_decl (gimple_assign_lhs (stmt));
      if (!d || TREE_CODE (TREE_TYPE (d)) != POINTER_TYPE)
	return NULL_TREE;
      tree rhs = gimple_assign_rhs1 (stmt);
      if (ip_arg_owner_flavored_p (rhs))
	return d;
      /* The destination-owner-declared half (see this function's own
	 comment) additionally requires RHS to resolve to SOME decl at
	 all -- excludes 'p = nullptr;' and similar (a reassignment
	 discarding ownership, or clearing an already-consumed
	 parameter, is not a hand-off of anything and must not start a
	 second, spurious tracked instance of P as if it were a fresh
	 local; confirmed as a real regression otherwise, against
	 d4324-profiles-invalidation-owner-reassigned-after-consume-
	 ok.C's own 'delete p; p = nullptr;').  Genuineness itself is
	 NOT required here (an ungenuine-but-resolvable RHS, e.g. an
	 arbitrary untracked parameter, still starts tracking on D --
	 ip_check_owner_assign_flavor_consistency's own reverse-
	 direction check is what separately flags the assignment itself
	 as invalid; this function only decides whether a binding
	 exists to check at all).  */
      if (profiles_owning_ptr_p (d) && !ip_owner_arg_null_pointer_p (rhs)
	  && ip_owner_resolve_underlying_decl (rhs, stmt) != NULL_TREE)
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

/* A second, simpler, PURELY-MONOTONIC dataflow (GEN-only, never
   killed, OR-across-predecessors): "has DECL's binding EVER become
   owned reaching this point" -- function entry for a parameter, any
   gen event for a local.  Needed to disambiguate ip_owner_unconsumed_
   before_stmt_p's own FALSE result, which otherwise conflates two
   different reasons: "already consumed" (a genuine double-
   consumption) vs. simply "not yet owned here at all", which for a
   LOCAL binding can be true at an EARLIER-in-program-order statement
   that happens to look, syntactically, exactly like a consuming event
   for the SAME decl name -- confirmed empirically: checking 'delete
   p; p = g (); delete p;' as the is_parameter=false run tracking the
   binding that starts at 'p = g ()' would otherwise misread the
   FIRST, entirely unrelated delete (of the original PARAMETER's own
   value, nothing to do with this run's own binding, which does not
   exist yet at that point) as "already consumed" and falsely flag it.
   Only ip_owner_ever_owned_before_stmt_p's own combination with
   ip_owner_unconsumed_before_stmt_p -- ever-owned-but-not-currently-
   unconsumed -- unambiguously means "already consumed".  */

struct ip_owner_ever_owned_info
{
  auto_vec<bool> block_in;
  auto_vec<bool> block_out;
};

static void
ip_compute_owner_ever_owned_info (function *fun, tree decl, bool is_parameter,
				   basic_block entry_succ,
				   ip_owner_ever_owned_info *info)
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

	  bool out = info->block_in[bb->index] || (is_parameter && bb == entry_succ);
	  if (!out && !is_parameter)
	    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
		 gsi_next (&gsi))
	      if (ip_owner_gen_lhs_decl (gsi_stmt (gsi)) == decl)
		{
		  out = true;
		  break;
		}
	  if (out != info->block_out[bb->index])
	    {
	      info->block_out[bb->index] = out;
	      changed = true;
	    }
	}
    }
}

static bool
ip_owner_ever_owned_before_stmt_p (gimple *point, tree decl, bool is_parameter,
				    basic_block entry_succ,
				    const ip_owner_ever_owned_info &info)
{
  basic_block bb = gimple_bb (point);
  if (info.block_in[bb->index] || (is_parameter && bb == entry_succ))
    return true;
  if (is_parameter)
    return false;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == point)
	break;
      if (ip_owner_gen_lhs_decl (stmt) == decl)
	return true;
    }
  return false;
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
  ip_owner_ever_owned_info ever_owned_info;
  ip_compute_owner_ever_owned_info (fun, decl, is_parameter, entry_succ,
				     &ever_owned_info);

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
  bool decl_reassigned = false;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (!ip_defines_var_p (stmt, decl))
	  continue;
	if (!is_parameter && ip_owner_gen_lhs_decl (stmt) == decl)
	  continue;
	decl_reassigned = true;
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

  /* Leak point 3: DECL reaches a SECOND consuming event -- e.g. 'int
     f (int *p [[owner]]); int g (int *p [[owner]]); h (f (x), g
     (x));' -- with no intervening reassignment (that shape is leak
     point 2's own territory: a fresh gen event resets state to true,
     so a later consume of the NEW value is correctly not flagged
     here).  Reuses ip_owner_unconsumed_before_stmt_p's own dataflow
     query completely unchanged, just at every consuming-event
     statement instead of only at reassignments: if it's FALSE right
     before a NEW consuming event, that event's own value was already
     given away on EVERY path reaching it, not merely possibly so
     (the same "MAY be unconsumed" fact leak point 1 checks
     existentially at exit is being checked here for its negation,
     universally, at a narrower point) -- this is what keeps this
     check from firing on a merely CONDITIONALLY-already-consumed
     value ('if (c) f (x); g (x);' is NOT flagged: on the c-false
     path g (x) is the legitimate first consumption, so state is
     still "may be unconsumed" reaching it).  A raw delete/deleting-
     destructor-dispatch CALL is deliberately skipped here: it is
     always paired with, and dominated by, its own null-guard COND
     (ip_owner_delete_guard_cond_p), which independently already
     matches ip_owner_consuming_stmt_p for the exact same logical
     delete-expression -- checking both would flag the same delete
     twice, once at the guard and once, redundantly and always
     falsely (state is already false there BECAUSE the guard just
     consumed it), at the call itself.

     Skipped entirely when IS_PARAMETER and DECL_REASSIGNED: a
     PARM_DECL's own state, unlike a local's, is never re-armed by a
     reassignment (ip_owner_block_transfer's GEN branch is deliberately
     is_parameter-exclusive -- a reassigned parameter's new value is,
     by design, tracked as its own separate is_parameter=false binding
     instead, see ip_check_owner_consumption's own comment), so once
     the ORIGINAL parameter value is consumed, this run's own state
     never becomes true again for the rest of the function -- making
     EVERY later consuming-shaped statement touching the same DECL
     name look, to this is_parameter=true run alone, like a repeat
     consumption of the (long since fully accounted for) original
     value, even though it is legitimately consuming whatever DECL
     holds *now*.  Confirmed empirically: 'delete p; p = g (); delete
     p;' -- fully legitimate, the second delete consumes g()'s own
     result -- otherwise false-positived on that second delete.  Leak
     point 2 above already independently proves the original value
     itself was safely consumed before any reassignment; the separate
     is_parameter=false run this same reassignment seeds (its own GEN
     event correctly re-arms ITS OWN state, so IT does not have this
     problem) independently re-checks the new value from there on.  */
  /* Leak point 4: DECL is READ -- not consumed again, merely used as
     an ordinary operand (a call argument at a non-owner-sink
     position, an assignment's RHS, a dereference, a return operand
     of a non-owner-marked return) -- at a point where it has already
     been fully consumed on every path reaching it.  Whatever DECL was
     handed to may have destroyed the object it denoted; the value is
     not merely "no longer owned by DECL", it is not safe to read at
     all (confirmed directly: 'delete p; int x = *p;' compiled with no
     diagnostic whatsoever before this leak point existed).  Shares
     the exact same "already spent" test leak point 3 uses --
     ever_owned_before_stmt_p true, unconsumed_before_stmt_p false --
     just applied to a read instead of a second consuming event; no
     new dataflow, purely a new consumer of the two analyses already
     computed above.  A statement is checked as EITHER a leak-point-3
     candidate OR a leak-point-4 candidate, never both (the early
     `continue` after the consuming-event branch below is what
     guarantees this) -- the statement that itself performs a second
     consumption is not also, redundantly, "a read of an already-spent
     value" in this checker's own model.

     Checked at whole-STATEMENT granularity (ip_owner_stmt_reads_decl_p
     's own comment): a single statement that both consumes DECL via
     one argument and reads it via a different, aliased argument is
     classified as consuming only, a documented residual scope limit,
     not fixed here.  */
  if (is_parameter && decl_reassigned)
    return;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (gcall *call = dyn_cast<gcall *> (stmt))
	  if (ip_owner_delete_call_p (call, decl)
	      || ip_owner_deleting_dtor_dispatch_p (call, decl)
	      || ip_owner_direct_dtor_call_p (call, decl))
	    continue;
	/* ever_owned_before_stmt_p is the disambiguating half: without
	   it, a LOCAL binding's own check would also match an earlier,
	   entirely unrelated, syntactically-identical-looking consuming
	   or reading statement that happens to precede this binding's
	   own gen event in program order (state is "false" there too,
	   but for the mundane reason that this run's own binding does
	   not exist yet, not because anything was already consumed) --
	   see ip_owner_ever_owned_before_stmt_p's own comment.  */
	bool already_spent
	  = ip_owner_ever_owned_before_stmt_p (stmt, decl, is_parameter,
						entry_succ, ever_owned_info)
	    && !ip_owner_unconsumed_before_stmt_p (stmt, decl, is_parameter,
						    fn_return_is_owner,
						    entry_succ, info);
	if (ip_owner_consuming_stmt_p (stmt, decl, fn_return_is_owner))
	  {
	    if (already_spent
		&& !profiles_diagnostic_exempt_p (gimple_location (stmt),
						   fun->decl, "std::invalidation"))
	      error_at (gimple_location (stmt),
			"%qD is consumed again here, after already being "
			"consumed on every path reaching this point, under the "
			"%<std::invalidation%> profile", decl);
	    continue;
	  }
	if (already_spent && ip_owner_stmt_reads_decl_p (stmt, decl)
	    && !profiles_diagnostic_exempt_p (gimple_location (stmt),
					       fun->decl, "std::invalidation"))
	  error_at (gimple_location (stmt),
		    "%qD is read here, after already being consumed on "
		    "every path reaching this point, under the "
		    "%<std::invalidation%> profile", decl);
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

  /* Only the escape-checking machinery below (ip_check_return_escape
     and its own ip_collect_component_writes_before/ip_resolve_nrv_var
     helpers) still needs GCC's dominator tree -- the mutation-
     ordering check just above no longer does, now that it's built
     entirely on ip_compute_var_reach_info/ip_compute_mutated_since_
     info's own explicit fixed-point dataflow instead.  */
  bool dominance_computed = false;
  if (!returns_to_check.is_empty () && !dom_info_available_p (CDI_DOMINATORS))
    {
      calculate_dominance_info (CDI_DOMINATORS);
      dominance_computed = true;
    }

  if (!mutating_calls.is_empty () && !uses.is_empty ())
    {
      auto_vec<tree> seen_vars;
      for (unsigned i = 0; i < uses.length (); ++i)
	{
	  tree var = uses[i].var;
	  bool already = false;
	  for (unsigned j = 0; j < seen_vars.length (); ++j)
	    if (seen_vars[j] == var)
	      {
		already = true;
		break;
	      }
	  if (already)
	    continue;
	  seen_vars.safe_push (var);
	  ip_check_var_uses (fun, var, uses, mutating_calls, mutated_decls,
			      mutated_types, fun->decl);
	}
    }

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
