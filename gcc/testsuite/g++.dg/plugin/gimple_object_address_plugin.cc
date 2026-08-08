/* gimple-object-address: an experimental prototype demonstrating the
   "Option A, front-end-assisted" design from ~/gimple-contract-
   analysis.md -- doing D4324's mandatory item 6/7/8 checks (is_object_
   address, nonzero-divisor; more fact shapes to follow, see the
   analysis's own Section 8) as a GIMPLE pass, run right after SSA is
   built (before any optimization pass), instead of the existing
   AST-walk (gcc/cp/contracts.cc's own oa_* machinery, hooked at
   PLUGIN_PRE_GENERICIZE).

   This is an ADDITIONAL alternative, not a replacement: the existing
   mandatory checks (contracts.cc) keep running exactly as before, so a
   test compiled with this plugin will typically see BOTH the existing
   check's own diagnostic AND this pass's own (distinctly worded,
   "gimple-oa:"-prefixed) diagnostic for the same construct -- that
   overlap is expected and is not a bug in either engine.

   Design (see ~/gimple-contract-analysis.md, section 4, "the wrapper-
   parameter problem, and its resolution"): this pass NEVER looks at a
   contract's own outlined GIMPLE machinery (F.pre/F.post/the
   predicate-core function/the thunk) -- it reads a function's
   *declared* precondition/postcondition text directly off its own
   FUNCTION_DECL (get_fn_contract_specifiers/CONTRACT_CONDITION,
   exactly the same front-end API the existing AST-walk itself uses,
   and the exported is_object_address_call_p/oa_nonzero_conjunct_p
   recognizers, unchanged), and does the "is this argument provably
   true, right here" part with ordinary GIMPLE/SSA reasoning:
   SSA_NAME_DEF_STMT plus a recursive PHI-argument walk (a PHI's own
   "reaches from every incoming edge" is exactly the merge rule
   oa_env::merge_with hand-rolls at the AST level, but reified as a
   real node here instead of a hand-maintained env). Because contract
   *text* is read declaratively, this sidesteps both the wrapper-
   parameter correspondence problem (Section 4.1 of the analysis) and
   the fact that a `symbolic` contract's own condition may generate no
   GIMPLE at all (Section 2.2) -- neither matters, since this pass
   never needs the contract's own generated code, only its declared
   text plus the ordinary code around it.

   Every fact shape below (is_object_address, nonzero -- more to
   follow) shares this exact same three-part structure, mirroring the
   "classic" m_map/m_nz_map maps' own self-trust/consult/item-6 trio in
   contracts.cc itself:
   1. Self-trust: a function's own declared precondition seeds a fact
      onto ssa_default_def(fun, parm) at function entry.
   2. Call-site consult: a direct callee's own declared precondition is
      checked against the caller's actual gimple_call_arg, substituted
      positionally (find_param_position, the GIMPLE analogue of
      oa_substitute_call_arg).
   3. Item 6: a direct callee's own declared postcondition
      unconditionally guaranteeing the fact for its own return value is
      recognized wherever a GIMPLE_CALL's own result feeds into a later
      obligation.
   Provability itself is a single recursive SSA walk per fact shape:
   SSA_NAME_DEF_STMT dispatch on GIMPLE_PHI (AND over every incoming
   value, cycle-guarded via an IN_PROGRESS hash_set for loop-carried
   PHIs -- conservatively false, the same "must be provable, else
   unprovable" discipline used throughout), GIMPLE_ASSIGN (a trivial
   shape, or propagate through a copy/conversion), and GIMPLE_CALL
   (item 6).

   Deliberately narrow scope, kept out on purpose (see the analysis's
   own "phased plan," section 6, and its own "Section 8"/"Section 9"
   validation-results writeups for how this evolved):
   - ASSERTION_STMT (contract_assert, which has no single fixed
     get_fn_contract_specifiers-style declarative home the way pre/post
     do), and a postcondition establishing a fact about a *persistent
     parameter* (as opposed to the return value) for a later, separate
     call site, are both still out of scope for every fact shape below.
   - Only DIRECT calls (a resolved GIMPLE_CALL callee FUNCTION_DECL)
     are consulted; an indirect/virtual call gets no consult at all,
     matching the same limitation the existing AST-walk already has.
   - IILE recursion is explicitly, permanently out of scope for this
     prototype (not merely deferred) -- an immediately-invoked closure
     always reports "cannot verify" here, by design.  */

#include "gcc-plugin.h"
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "basic-block.h"
#include "cp/cp-tree.h"
#include "cp/contracts.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "is-a.h"
#include "tree-dfa.h"
#include "tree-pass.h"
#include "context.h"
#include "diagnostic.h"
#include "stringpool.h"
#include "function.h"
#include "plugin-version.h"

int plugin_is_GPL_compatible;

/* Positional correspondence between CALLEE's own PARM_DECLs and CALL's
   actual argument expressions -- the GIMPLE-level analogue of
   contracts.cc's own oa_substitute_call_arg, just keyed by
   gimple_call_arg instead of CALL_EXPR_ARG.  */

static bool
find_param_position (tree callee, tree parm, unsigned *argno_out)
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

/* Item 6's own shape, read declaratively: does CALLEE have a declared
   postcondition whose condition names is_object_address(r) for r ==
   its own POSTCONDITION_IDENTIFIER (its named result)?  If so, ANY
   successful call to CALLEE unconditionally guarantees its return
   value is an object address -- no argument substitution needed at
   all, since a postcondition's guarantee about its own return value
   holds regardless of the caller's own context.  Mirrors contracts.cc's
   own oa_call_postcondition_object_address_p exactly, but -- per this
   whole prototype's own design -- reads CALLEE's *declared* condition
   tree directly rather than anything derived from CALLEE's own,
   possibly-not-yet-processed GIMPLE body, so it works regardless of
   whichever order the pass manager happens to visit functions in (see
   ~/gimple-contract-analysis.md, Section 3's own "ordering" caveat).  */

static bool
call_postcondition_guarantees_object_address_p (tree callee)
{
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
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
	  if (!is_object_address_call_p (*conjuncts[i], &arg))
	    continue;
	  STRIP_ANY_LOCATION_WRAPPER (arg);
	  if (arg == result_id)
	    return true;
	}
    }
  return false;
}

/* Same idea, for nonzero-ness -- mirrors contracts.cc's own
   oa_call_postcondition_nonzero_p exactly, again reading CALLEE's
   *declared* postcondition text only.  */

static bool
call_postcondition_guarantees_nonzero_p (tree callee)
{
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
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
	  tree decl;
	  if (oa_nonzero_conjunct_p (*conjuncts[i], &decl) && decl == result_id)
	    return true;
	}
    }
  return false;
}

/* Is VAL (a real GIMPLE operand: an SSA_NAME, an invariant address, or
   a constant) provably an object address, given ESTABLISHED (the SSA
   names this function's own declared precondition already trusts, via
   IN_PROGRESS's own function seed_self_trust below)?  IN_PROGRESS
   guards against infinite recursion on a loop-carried PHI -- revisiting
   an SSA name already being resolved is conservatively treated as
   "not (yet) provable," the same "must be provable, else treated as
   unprovable" discipline the AST-walk uses throughout, here falling
   out for free from the cycle guard rather than a hand-written
   loop-header merge rule.  */

static bool
provable_object_address_p (tree val, hash_set<tree> &established,
			    hash_set<tree> &in_progress)
{
  if (val == NULL_TREE)
    return false;

  /* Trivial case: taking the address of any decl is definitionally an
     object address, regardless of any tracked fact -- mirrors
     oa_provable_p's own ADDR_EXPR short-circuit.  */
  if (TREE_CODE (val) == ADDR_EXPR)
    return true;

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  if (established.contains (val))
    return true;

  if (in_progress.contains (val))
    return false;

  in_progress.add (val);
  bool result = false;

  gimple *def = SSA_NAME_DEF_STMT (val);
  if (def && gimple_code (def) == GIMPLE_PHI)
    {
      /* A PHI node's own operands ARE the "every incoming value must
	 satisfy it" merge oa_env::merge_with otherwise hand-rolls --
	 reified as a real node, not a parallel data structure kept in
	 sync by hand.  */
      result = true;
      unsigned n = gimple_phi_num_args (def);
      for (unsigned i = 0; i < n; ++i)
	if (!provable_object_address_p (gimple_phi_arg_def (def, i),
					 established, in_progress))
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
	/* A plain copy or a value-preserving conversion: whatever's
	   true of the RHS is true here too.  */
	result = provable_object_address_p (gimple_assign_rhs1 (def),
					     established, in_progress);
    }
  else if (def && is_gimple_call (def))
    {
      /* Item 6: VAL is a call's own return value -- if the (direct)
	 callee's own declared postcondition unconditionally guarantees
	 it, that's a fact about VAL regardless of anything else.  Only
	 ever consults CALLEE's *declared* text (see the function above),
	 never CALLEE's own GIMPLE body -- so this works even if CALLEE
	 hasn't been visited by this pass yet at all.  */
      tree callee = gimple_call_fndecl (as_a <gcall *> (def));
      if (callee)
	result = call_postcondition_guarantees_object_address_p (callee);
    }

  in_progress.remove (val);
  return result;
}

/* Nonzero-ness's own counterpart of provable_object_address_p
   immediately above -- same three-part structure (trivial constant
   case, established-fact lookup, PHI-merge, copy/conversion
   propagation, item 6's own call-return-value guarantee), just for a
   different fact.  ESTABLISHED_NZ is a wholly separate hash_set from
   is_object_address's own ESTABLISHED (matching contracts.cc's own
   m_map/m_nz_map being two separate maps for two separate facts).  */

static bool
provable_nonzero_p (tree val, hash_set<tree> &established_nz,
		     hash_set<tree> &in_progress)
{
  if (val == NULL_TREE)
    return false;

  if (TREE_CODE (val) == INTEGER_CST)
    return !integer_zerop (val);

  if (TREE_CODE (val) != SSA_NAME)
    return false;

  if (established_nz.contains (val))
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
	if (!provable_nonzero_p (gimple_phi_arg_def (def, i),
				  established_nz, in_progress))
	  {
	    result = false;
	    break;
	  }
    }
  else if (def && is_gimple_assign (def))
    {
      enum tree_code code = gimple_assign_rhs_code (def);
      if (CONVERT_EXPR_CODE_P (code) || code == SSA_NAME)
	result = provable_nonzero_p (gimple_assign_rhs1 (def),
				      established_nz, in_progress);
    }
  else if (def && is_gimple_call (def))
    {
      tree callee = gimple_call_fndecl (as_a <gcall *> (def));
      if (callee)
	result = call_postcondition_guarantees_nonzero_p (callee);
    }

  in_progress.remove (val);
  return result;
}

/* Seed ESTABLISHED/ESTABLISHED_NZ from FNDECL's own declared
   precondition: an is_object_address(p)/'p != 0'-shaped conjunct
   naming one of FNDECL's own parameters is trusted as an axiom for the
   rest of FNDECL's own body (self-trust) -- the GIMPLE-level analogue
   of oa_handle_precondition_stmt's own fact-seeding, just keyed by SSA
   name (ssa_default_def)
   instead of a raw PARM_DECL in a hand-rolled map.  Reads FNDECL's
   *declared* condition tree directly (get_fn_contract_specifiers/
   CONTRACT_CONDITION) -- never FNDECL.pre's own outlined GIMPLE body,
   which is the whole point of this design (see this file's own top
   comment).  */

static void
seed_self_trust (function *fun, hash_set<tree> &established,
		  hash_set<tree> &established_nz)
{
  tree fndecl = fun->decl;
  for (tree as = get_fn_contract_specifiers (fndecl); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts_public (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree arg;
	  hash_set<tree> *target;
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
	  if (ssa)
	    target->add (ssa);
	}
    }
}

/* For CALL's own callee, check every is_object_address(param)/'param
   != 0'-shaped conjunct of its own declared precondition against
   CALL's own actual argument, substituted positionally
   (find_param_position) exactly the way the AST-walk's own
   oa_substitute_call_arg already does -- again, only CALLEE's
   *declared* condition is ever consulted, never CALLEE.pre's own
   outlined body.  */

static void
check_call (gcall *call, hash_set<tree> &established,
	     hash_set<tree> &established_nz)
{
  tree callee = gimple_call_fndecl (call);
  if (!callee)
    return;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
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
	  if (!find_param_position (callee, arg, &argno)
	      || argno >= gimple_call_num_args (call))
	    continue;

	  tree substituted = gimple_call_arg (call, argno);
	  hash_set<tree> in_progress;
	  if (is_oa)
	    {
	      if (provable_object_address_p (substituted, established,
					      in_progress))
		continue; /* Proven true: silently discharged.  */
	      warning_at (gimple_location (call), 0,
			  "gimple-oa: cannot verify %<is_object_address%> for "
			  "%qE, as required by the precondition of %qD",
			  substituted, callee);
	    }
	  else
	    {
	      if (provable_nonzero_p (substituted, established_nz, in_progress))
		continue; /* Proven true: silently discharged.  */
	      warning_at (gimple_location (call), 0,
			  "gimple-oa: cannot verify that %qE is nonzero, as "
			  "required by the precondition of %qD",
			  substituted, callee);
	    }
	}
    }
}

namespace {

const pass_data pass_data_gimple_object_address =
{
  GIMPLE_PASS,			/* type */
  "gimple_object_address",	/* name */
  OPTGROUP_NONE,		/* optinfo_flags */
  TV_NONE,			/* tv_id */
  PROP_ssa,			/* properties_required */
  0,				/* properties_provided */
  0,				/* properties_destroyed */
  0,				/* todo_flags_start */
  0,				/* todo_flags_finish */
};

class pass_gimple_object_address : public gimple_opt_pass
{
public:
  pass_gimple_object_address (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_gimple_object_address, ctxt)
  {}

  bool gate (function *) final override { return true; }
  unsigned int execute (function *) final override;
};

unsigned int
pass_gimple_object_address::execute (function *fun)
{
  hash_set<tree> established;
  hash_set<tree> established_nz;
  seed_self_trust (fun, established, established_nz);

  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (is_gimple_call (stmt))
	  check_call (as_a <gcall *> (stmt), established, established_nz);
      }

  return 0;
}

} // anon namespace

static gimple_opt_pass *
make_pass_gimple_object_address (gcc::context *ctxt)
{
  return new pass_gimple_object_address (ctxt);
}

int
plugin_init (struct plugin_name_args *plugin_info,
	     struct plugin_gcc_version *version)
{
  const char *plugin_name = plugin_info->base_name;

  if (!plugin_default_version_check (version, &gcc_version))
    return 1;

  struct register_pass_info pass_info;
  pass_info.pass = make_pass_gimple_object_address (g);
  pass_info.reference_pass_name = "ssa";
  pass_info.ref_pass_instance_number = 1;
  pass_info.pos_op = PASS_POS_INSERT_AFTER;

  register_callback (plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &pass_info);
  return 0;
}
