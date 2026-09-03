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
   - A [[uninit]] local whose address is taken anywhere in the
     function is not SSA-registered (is_gimple_reg is false for it),
     so this pass cannot analyze it at all -- rather than silently
     accepting an unverifiable annotation, that is a hard error: an
     unverified [[uninit]] would silently break the profile's whole
     guarantee.
   - The in_progress cycle guard, like cg_provable_object_address_p's
     own, does not remove a node once visited within a single query,
     so a shared ancestor reached via two different non-looping merge
     paths (nested diamonds, not a loop) can be conservatively
     re-treated as unproven on its second visit. This can only ever
     produce a false positive (an extra, spurious diagnostic on
     genuinely-always-initialized code), never a false negative -- the
     same accepted, explicitly-sanctioned "local flow analysis, some
     false positives" tradeoff both P4222 and P3446 state outright.  */

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

static unsigned int
ip_check_function (function *fun)
{
  unsigned i;
  tree var;
  FOR_EACH_LOCAL_DECL (fun, i, var)
    {
      if (!VAR_P (var) || !lookup_attribute ("uninit", DECL_ATTRIBUTES (var)))
	continue;

      if (!is_gimple_reg (var))
	{
	  error_at (DECL_SOURCE_LOCATION (var),
		    "cannot verify %<[[uninit]]%> on %qD under the "
		    "%<std::init%> profile: its address is taken, which "
		    "this checker cannot yet analyze", var);
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
