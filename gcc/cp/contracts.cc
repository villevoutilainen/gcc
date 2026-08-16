/* C++ contracts.

   Copyright (C) 2020-2026 Free Software Foundation, Inc.
   Originally by Jeff Chapman II (jchapman@lock3software.com) for proposed
   C++20 contracts.
   Rewritten for C++26 contracts by:
     Nina Ranns (dinka.ranns@googlemail.com)
     Iain Sandoe (iain@sandoe.co.uk)
     Ville Voutilainen (ville.voutilainen@gmail.com).

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "cp-tree.h"
#include "stringpool.h"
#include "diagnostic.h"
#include "options.h"
#include "contracts.h"
#include "tree.h"
#include "hash-traits.h"
#include "tree-inline.h"
#include "attribs.h"
#include "tree-iterator.h"
#include "print-tree.h"
#include "stor-layout.h"
#include "intl.h"
#include "cgraph.h"
#include "opts.h"
#include "calls.h"
#include "output.h"
#include "context.h"
#include "tree-pass.h"

/*  Design notes.

  There are three phases:
    1. Parsing and semantic checks.
       Most of the code for this is in the parser, with helpers provided here.
    2. Emitting contract assertion AST nodes into function bodies.
       This is initiated from "finish_function ()"
    3. Lowering the contract assertion AST nodes to control flow, constant
       data and calls to the violation handler.
       This is initiated from "cp_genericize ()".

  The organisation of the code in this file is intended to follow those three
  phases where possible.

  Contract Assertion State
  ========================

  contract_assert () does not require any special handling and can be
  represented directly by AST inserted in the function body.

  'pre' and 'post' function contract specifiers require most of the special
  handling, since they must be tracked across re-declarations of functions and
  there are constraints on how such specifiers may change in these cases.

  The contracts specification identifies a "first declaration" of any given
  function - which is the first encountered when parsing a given TU.
  Subsequent re-declarations may not add or change the function contract
  specifiers from any introduced on this first declaration.  It is, however,
  permitted to omit specifiers on re-declarations.

  Since the implementation of GCC's (re-)declarations is a destructive merge
  we need to keep some state on the side to determine whether the re-declaration
  rules are met.  In this current design we have chosen not to add another tree
  to each function decl but, instead, keep a map from function decl to contract
  specifier state.  In this state we record the 'first declaration' specifiers
  which are used to validate re-declaration(s) and to report the initial state
  in diagnostics.

  We need (for example) to compare
    pre ( x > 2 ) equal to
    pre ( z > 2 ) when x and z refer to the same function parameter in a
    re-declaration.

  The mechanism used to determine if two contracts are the same is to compare
  the folded trees.  This makes use of current compiler machinery, rather than
  constructing some new AST comparison scheme.  However, it does introduce an
  additional complexity in that we need to defer such comparison until parsing
  is complete - and function contract specifiers in class declarations must be
  deferred parses, since it is also permitted for specifiers to refer to class
  members.

  When we encounter a definition, the parameter names in a function decl are
  re-written to match those of the definition (thus the expected names will
  appear in debug information etc).  At this point, we also need to re-map
  any function parameter names that appear in function contract specifiers
  to agree with those of the definition - although we intend to keep the
  'first declaration' record consistent for diagnostics.

  Since we shared some code from the C++2a contracts implementation, pre and
  post specifiers are represented by chains of attributes, where the payload
  of the attribute is an AST node.  However during the parse, these are not
  inserted into the function bodies, but kept in the decl-keyed state described
  above.  A future improvement planned here is to store the specifiers using a
  tree vec instead of the attribute list.

  Emitting contract AST
  =====================

  When we reach `finish_function ()` and therefore are committed to potentially
  emitting code for an instance, we build a new variant of the function body
  with the pre-condition AST inserted before the user's function body, and the
  post condition AST (if any) linked into the function return.

  Lowering the contract assertion AST
  ===================================

  In all cases (pre, post, contract_assert) the AST node is lowered to control
  flow and (potentially) calls to the violation handler and/or termination.
  This is done during `cp_genericize ()`.  In the current implementation, the
  decision on the control flow is made on the basis of the setting of a command-
  line flag that determines a TU-wide contract evaluation semantic, which has
  the following initial set of behaviours:

    'ignore'	    : contract assertion AST is lowered to 'nothing',
		      i.e. omitted.
    'enforce'	    : contract assertion AST is lowered to a check, if this
		      fails a violation handler is called, followed by
		      std::terminate().
    'quick_enforce' : contract assertion AST is lowered to a check, if this
		      fails, std::terminate () is called.
    'observe'	    : contract assertion AST is lowered to a check, if this
		      fails, a violation handler is called, the code then
		      continues.

  In each case, the "check" might be a simple 'if' (when it is determined that
  the assertion condition does not throw) or the condition evaluation will be
  wrapped in a try-catch block that treats any exception thrown when evaluating
  the check as equivalent to a failed check.  It is noted in the violation data
  object whether a check failed because of an exception raised in evaluation.

  At present, a simple (but potentially space-inefficient) scheme is used to
  store constant data objects that represent the read-only data for the
  violation.  The exact form of this is subject to revision as it represents
  ABI that must be agreed between implementations (as of this point, that
  discussion is not yet concluded).  */

/* Contract matching.  */

bool comparing_contracts;

/* True if the contract is valid.  */

static bool
contract_valid_p (tree contract)
{
  return CONTRACT_CONDITION (contract) != error_mark_node;
}

/* True if the contract specifier is valid.  */

static bool
contract_specifier_valid_p (tree contract)
{
  return contract_valid_p (TREE_VALUE (TREE_VALUE (contract)));
}

/* Compare the contract conditions of OLD_CONTRACT and NEW_CONTRACT.
   Returns false if the conditions are equivalent, and true otherwise.  */

static bool
mismatched_contracts_p (tree old_contract, tree new_contract)
{
  /* Different kinds of contracts do not match.  */
  if (TREE_CODE (old_contract) != TREE_CODE (new_contract))
    {
      auto_diagnostic_group d;
      error_at (EXPR_LOCATION (new_contract),
		"mismatched contract specifier in declaration");
      inform (EXPR_LOCATION (old_contract), "previous contract here");
      return true;
    }

  /* A deferred contract tentatively matches.  */
  if (CONTRACT_CONDITION_DEFERRED_P (new_contract))
    return false;

  /* Compare the conditions of the contracts.  */
  tree t1 = cp_fully_fold_init (CONTRACT_CONDITION (old_contract));
  tree t2 = cp_fully_fold_init (CONTRACT_CONDITION (new_contract));

  /* Compare the contracts. */

  bool saved_comparing_contracts = comparing_contracts;
  comparing_contracts = true;
  bool matching_p = cp_tree_equal (t1, t2);
  comparing_contracts = saved_comparing_contracts;

  if (!matching_p)
    {
      auto_diagnostic_group d;
      error_at (EXPR_LOCATION (CONTRACT_CONDITION (new_contract)),
		"mismatched contract condition in declaration");
      inform (EXPR_LOCATION (CONTRACT_CONDITION (old_contract)),
	      "previous contract here");
      return true;
    }

  /* The control object (pre<A>/pre<B>) must also match across
     declarations of "the same" contract -- otherwise which control
     object actually governs a precondition/postcondition/contract_
     assert would silently depend on which declaration a given caller
     (or the definition itself) happens to see, exactly the hazard the
     condition check just above already guards against. NULL_TREE on
     both sides (-fcontract-control-objects off) trivially matches; with
     the flag on, a bare pre/post/contract_assert is already resolved to
     std::contracts::default_v before this point (see CONTRACT_CONTROL_
     OBJECT's own comment in contracts.h), so within a single
     translation unit this never compares "explicit vs absent," only
     "which object" -- mirroring the strict, symmetric matching this
     fork already requires for its own conveyor/symbolic specifiers.  */
  tree c1 = CONTRACT_CONTROL_OBJECT (old_contract);
  tree c2 = CONTRACT_CONTROL_OBJECT (new_contract);
  if ((c1 != NULL_TREE) != (c2 != NULL_TREE))
    {
      auto_diagnostic_group d;
      error_at (EXPR_LOCATION (new_contract),
		"mismatched contract control object in declaration");
      inform (EXPR_LOCATION (old_contract), "previous contract here");
      return true;
    }
  if (c1 != NULL_TREE)
    {
      tree cf1 = cp_fully_fold_init (c1);
      tree cf2 = cp_fully_fold_init (c2);

      saved_comparing_contracts = comparing_contracts;
      comparing_contracts = true;
      bool ctrl_matching_p = cp_tree_equal (cf1, cf2);
      comparing_contracts = saved_comparing_contracts;

      if (!ctrl_matching_p)
	{
	  auto_diagnostic_group d;
	  error_at (EXPR_LOCATION (new_contract),
		    "mismatched contract control object in declaration");
	  inform (EXPR_LOCATION (old_contract), "previous contract here");
	  return true;
	}
    }

  return false;
}

/* Compare the contract specifiers of OLDDECL and NEWDECL. Returns true
   if the contracts match, and false if they differ.  */

static bool
match_contract_specifiers (location_t oldloc, tree old_contracts,
			   location_t newloc, tree new_contracts)
{
  /* Contracts only match if they are both specified.  */
  if (!old_contracts || !new_contracts)
    return true;

  /* Compare each contract in turn.  */
  while (old_contracts && new_contracts)
    {
      /* If either contract is ill-formed, skip the rest of the comparison,
	 since we've already diagnosed an error.  */
      if (!contract_specifier_valid_p (new_contracts)
	  || !contract_specifier_valid_p (old_contracts))
	return false;

      if (mismatched_contracts_p (CONTRACT_STATEMENT (old_contracts),
				  CONTRACT_STATEMENT (new_contracts)))
	return false;
      old_contracts = TREE_CHAIN (old_contracts);
      new_contracts = TREE_CHAIN (new_contracts);
    }

  /* If we didn't compare all specifiers, the contracts don't match.  */
  if (old_contracts || new_contracts)
    {
      auto_diagnostic_group d;
      error_at (newloc,
		"declaration has a different number of contracts than "
		"previously declared");
      inform (oldloc,
	      new_contracts
	      ? "previous declaration with fewer contracts here"
	      : "previous declaration with more contracts here");
      return false;
    }

  return true;
}

/* -Wfunction-pointer-contract-mismatch: silent counterpart of
   mismatched_contracts_p above, comparing two CONTRACT_STATEMENTs for
   structural equality without emitting any diagnostics -- used to
   decide whether copying a function-pointer value between two decls
   that each carry their own contract should warn, not to accept/reject
   a redeclaration.  */

static bool
contracts_equal_p (tree c1, tree c2)
{
  if (TREE_CODE (c1) != TREE_CODE (c2))
    return false;

  tree t1 = cp_fully_fold_init (CONTRACT_CONDITION (c1));
  tree t2 = cp_fully_fold_init (CONTRACT_CONDITION (c2));

  bool saved_comparing_contracts = comparing_contracts;
  comparing_contracts = true;
  bool matching_p = cp_tree_equal (t1, t2);
  comparing_contracts = saved_comparing_contracts;
  if (!matching_p)
    return false;

  tree ctrl1 = CONTRACT_CONTROL_OBJECT (c1);
  tree ctrl2 = CONTRACT_CONTROL_OBJECT (c2);
  if ((ctrl1 != NULL_TREE) != (ctrl2 != NULL_TREE))
    return false;
  if (ctrl1 != NULL_TREE)
    {
      tree cf1 = cp_fully_fold_init (ctrl1);
      tree cf2 = cp_fully_fold_init (ctrl2);

      saved_comparing_contracts = comparing_contracts;
      comparing_contracts = true;
      bool ctrl_matching_p = cp_tree_equal (cf1, cf2);
      comparing_contracts = saved_comparing_contracts;
      if (!ctrl_matching_p)
	return false;
    }

  return true;
}

/* -Wfunction-pointer-contract-mismatch: silent counterpart of
   match_contract_specifiers above: true iff SPEC1 and SPEC2 (each a
   chain as returned by get_fn_contract_specifiers) carry the exact same
   sequence of contracts, with no diagnostics.  Two NULL chains are
   equal (neither decl has a contract of its own).  */

static bool
contract_specifiers_equal_p (tree spec1, tree spec2)
{
  while (spec1 && spec2)
    {
      if (!contract_specifier_valid_p (spec1)
	  || !contract_specifier_valid_p (spec2))
	return false;
      if (!contracts_equal_p (CONTRACT_STATEMENT (spec1),
			       CONTRACT_STATEMENT (spec2)))
	return false;
      spec1 = TREE_CHAIN (spec1);
      spec2 = TREE_CHAIN (spec2);
    }
  return !spec1 && !spec2;
}

/* -Wfunction-pointer-contract-mismatch: resolve EXPR down to the single
   decl whose own get_fn_contract_specifiers entry should be consulted
   when EXPR is copied into a function-pointer/reference-typed
   destination -- a bare function name or '&fn' (both collapse to the
   same ADDR_EXPR (FUNCTION_DECL) shape after decay_conversion), or a
   function-pointer-typed VAR_DECL/PARM_DECL used directly, or a
   FIELD_DECL named via a COMPONENT_REF.  NULL_TREE for anything else
   (a computed/complex expression), so the warning stays silent rather
   than guessing.  Modeled on oa_object_identity_decl's own stripping
   idiom above.  Exported (declared in contracts.h) so callers outside
   this file (typeck.cc's assignment handling, which only has the raw
   LHS expression, not an already-resolved decl) can resolve a
   destination the same way maybe_warn_fnptr_contract_mismatch resolves
   a source.  */

tree
fnptr_contract_owner (tree expr)
{
  if (expr == NULL_TREE || expr == error_mark_node)
    return NULL_TREE;

  expr = STRIP_ANY_LOCATION_WRAPPER (expr);
  while (TREE_CODE (expr) == NON_LVALUE_EXPR
	 || TREE_CODE (expr) == NOP_EXPR
	 || TREE_CODE (expr) == CONVERT_EXPR
	 || TREE_CODE (expr) == VIEW_CONVERT_EXPR)
    expr = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));

  if (TREE_CODE (expr) == ADDR_EXPR)
    expr = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));

  if (TREE_CODE (expr) == FUNCTION_DECL
      || VAR_P (expr)
      || TREE_CODE (expr) == PARM_DECL)
    return expr;

  if (TREE_CODE (expr) == COMPONENT_REF)
    return TREE_OPERAND (expr, 1);

  return NULL_TREE;
}

/* -Wfunction-pointer-contract-mismatch: warn at LOC if copying SRC_EXPR
   into DEST_DECL (a function-pointer/reference-typed VAR_DECL, PARM_
   DECL, or FIELD_DECL) would silently change which contract governs
   calls made through the destination.

   Deliberately asymmetric, matching this fork's own object-decl-
   callable semantics (a contract attached to a function-pointer object
   always governs calls made through that object, regardless of what's
   currently assigned to it -- see maybe_object_contract_check_call):

     - destination has no contract, source does: warn.  The source
       function's contract is real, but nothing will check it once
       called only through the (uncontracted) destination -- a real,
       silent loss of enforcement.
     - destination has a contract, source doesn't: silent.  This is the
       ordinary, intended idiom -- the destination's own contract keeps
       governing regardless of what gets assigned to it.
     - both have contracts, and they differ: warn.  The destination's
       contract still governs at runtime, but this is likely a mistake
       worth flagging.
     - both have the same contract, or neither has one: silent.

   No subsumption reasoning anywhere -- purely structural equality via
   contract_specifiers_equal_p, or nothing.  */

void
maybe_warn_fnptr_contract_mismatch (location_t loc, tree dest_decl,
				     tree src_expr)
{
  if (!warn_function_pointer_contract_mismatch)
    return;
  if (dest_decl == NULL_TREE || dest_decl == error_mark_node)
    return;
  if (src_expr == NULL_TREE || src_expr == error_mark_node)
    return;

  tree dest_type = TREE_TYPE (dest_decl);
  if (dest_type == NULL_TREE
      || (!TYPE_PTRFN_P (dest_type)
	  && !TYPE_REFFN_P (dest_type)
	  && !TYPE_PTRMEMFUNC_P (dest_type)))
    return;

  if (warning_suppressed_p (src_expr, OPT_Wfunction_pointer_contract_mismatch))
    return;

  tree src_owner = fnptr_contract_owner (src_expr);
  if (src_owner == NULL_TREE)
    return;

  tree dest_spec = get_fn_contract_specifiers (dest_decl);
  tree src_spec = get_fn_contract_specifiers (src_owner);

  if (dest_spec == NULL_TREE)
    {
      if (src_spec != NULL_TREE)
	warning_at (loc, OPT_Wfunction_pointer_contract_mismatch,
		    "assigning %qD, which has its own contract, to "
		    "%qD, which has none", src_owner, dest_decl);
      return;
    }

  if (src_spec == NULL_TREE)
    return;

  if (!contract_specifiers_equal_p (dest_spec, src_spec))
    warning_at (loc, OPT_Wfunction_pointer_contract_mismatch,
		"assigning %qD, whose contract differs from that of "
		"%qD", src_owner, dest_decl);
}

static bool contract_control_is_ignored (tree, contract_check_side, bool = false);
static bool contract_control_assumable (tree, contract_check_side, bool = false);
static bool contract_control_forces_client_side (tree, contract_check_side, bool = false);
static bool contract_control_forces_definition_side (tree, contract_check_side, bool = false);
static bool contract_is_inherited_p (tree);
static bool contract_inherited_runs_on_side (tree, contract_check_side);

/* CONTRACT's side, given the FNDECL whose own copy of it is currently
   being processed (the real function, or its caller-side wrapper).
   contract_check_side is declared in contracts.h since this is also
   needed by parser.cc/pt.cc, at points where a contract's condition is
   first parsed/instantiated, before any wrapper copy exists.  */

contract_check_side
contract_side_of (tree contract, tree fndecl)
{
  if (TREE_CODE (contract) == ASSERTION_STMT)
    return ccs_not_applicable;
  if (!fndecl || !DECL_LANG_SPECIFIC (fndecl))
    return ccs_definition;
  return DECL_CONTRACT_WRAPPER (fndecl) ? ccs_wrapper : ccs_definition;
}

/* Return true if CONTRACT is checked or assumed under the current build
   configuration, for the given SIDE.  */

static bool
contract_active_p (tree contract, contract_check_side side)
{
  /* D4324: a named control object decides activity by its type's
     compile-time members rather than the translation-unit semantic.  The
     assertion is active if it is not ignored (a runtime check or
     control-object dispatch runs) or if it is ignored but assumable (an
     optimizer assumption is emitted).  */
  if (tree ctrl = CONTRACT_CONTROL_OBJECT (contract))
    {
      if (!contract_control_is_ignored (ctrl, side))
	return true;
      return contract_control_assumable (ctrl, side);
    }
  return get_evaluation_semantic (contract) != CES_IGNORE;
}

/* True if CONTRACT should run its check on SIDE.  A control object naming
   force_client_side_check/force_definition_side_check overrides the
   ordinary -fcontracts-definition-check/-fcontracts-client-check policy
   for that one contract, regardless of what the command line says.  If a
   control object (erroneously) sets both, this deterministically routes
   the contract to ccs_definition only, so it is still processed exactly
   once; build_contract_check diagnoses that case.

   A contract synthesized by maybe_inherit_virtual_contract is a special
   case handled first: its side eligibility was fixed, per side, at
   inheritance time, and is the sole authority for it -- not blended
   with the (irrelevant, since it names the same control object as the
   base's own contract) force-flag/policy logic below.  */

static bool
contract_runs_on_side (tree contract, contract_check_side side)
{
  if (contract_is_inherited_p (contract))
    return contract_inherited_runs_on_side (contract, side);

  tree ctrl = CONTRACT_CONTROL_OBJECT (contract);
  bool force_client
    = ctrl && contract_control_forces_client_side (ctrl, side);
  bool force_def
    = ctrl && contract_control_forces_definition_side (ctrl, side);
  /* Check the "definition" flag first on both sides, so that when a
     control object (erroneously) sets both, the contract is routed to
     ccs_definition only rather than dropped from both sides entirely --
     see the comment above.  */
  if (side == ccs_definition)
    return force_def ? true : force_client ? false
			     : flag_contracts_definition_check;
  return force_def ? false : force_client ? true
			   : (TREE_CODE (contract) == POSTCONDITION_STMT
			      ? flag_contract_client_check > 1
			      : flag_contract_client_check > 0);
}

/* True if FNDECL has any checked or assumed contracts whose TREE_CODE is
   C that also run on SIDE.  */

static bool
has_active_contract_condition (tree fndecl, tree_code c,
				contract_check_side side)
{
  tree as = get_fn_contract_specifiers (fndecl);
  for (; as != NULL_TREE; as = TREE_CHAIN (as))
    {
      tree contract = TREE_VALUE (TREE_VALUE (as));
      if (TREE_CODE (contract) == c
	  && contract_active_p (contract, side)
	  && contract_runs_on_side (contract, side))
	return true;
    }
  return false;
}

/* True if FNDECL has any checked or assumed preconditions that run on
   SIDE.  */

static bool
has_active_preconditions (tree fndecl, contract_check_side side)
{
  return has_active_contract_condition (fndecl, PRECONDITION_STMT, side);
}

/* True if FNDECL has any checked or assumed postconditions that run on
   SIDE.  */

static bool
has_active_postconditions (tree fndecl, contract_check_side side)
{
  return has_active_contract_condition (fndecl, POSTCONDITION_STMT, side);
}

/* Return true if any contract in the CONTRACT list is checked or assumed
   under the current build configuration.  A plain "is anything active at
   all" query has no single correct side, so a PRE/POST contract is
   checked under *both* real sides (never a false negative that would
   wrongly skip real per-side processing later); an ASSERTION_STMT has
   only ccs_not_applicable to check.  */

static bool
contract_any_active_p (tree fndecl)
{
  tree as = get_fn_contract_specifiers (fndecl);
  for (; as; as = TREE_CHAIN (as))
    {
      tree contract = TREE_VALUE (TREE_VALUE (as));
      if (TREE_CODE (contract) == ASSERTION_STMT)
	{
	  if (contract_active_p (contract, ccs_not_applicable))
	    return true;
	}
      else if (contract_active_p (contract, ccs_definition)
	       || contract_active_p (contract, ccs_wrapper))
	return true;
    }
  return false;
}

/* Return true if any contract in CONTRACTS is not yet parsed.  */

bool
contract_any_deferred_p (tree contracts)
{
  for (; contracts; contracts = TREE_CHAIN (contracts))
    if (CONTRACT_CONDITION_DEFERRED_P (CONTRACT_STATEMENT (contracts)))
      return true;
  return false;
}

/* Returns true if function decl FNDECL has contracts and we need to
   process them for the purposes of either building caller or definition
   contract checks.
   This function does not take into account whether caller or definition
   side checking is enabled. Those checks will be done from the calling
   function which will be able to determine whether it is doing caller
   or definition contract handling.  */

static bool
handle_contracts_p (tree fndecl)
{
  return (flag_contracts
	  && !processing_template_decl
	  && (CONTRACT_HELPER (fndecl) == ldf_contract_none)
	  && contract_any_active_p (fndecl));
}

/* For use with the tree inliner. This preserves non-mapped local variables,
   such as postcondition result variables, during remapping.  */

static tree
retain_decl (tree decl, copy_body_data *)
{
  return decl;
}

/* Lookup a name in std::, or inject it.  */

static tree
lookup_std_type (tree name_id)
{
  tree res_type = lookup_qualified_name
    (std_node, name_id, LOOK_want::TYPE | LOOK_want::HIDDEN_FRIEND);

  if (TREE_CODE (res_type) == TYPE_DECL)
    res_type = TREE_TYPE (res_type);
  else
    {
      push_nested_namespace (std_node);
      res_type = make_class_type (RECORD_TYPE);
      create_implicit_typedef (name_id, res_type);
      DECL_SOURCE_LOCATION (TYPE_NAME (res_type)) = BUILTINS_LOCATION;
      DECL_CONTEXT (TYPE_NAME (res_type)) = current_namespace;
      pushdecl_namespace_level (TYPE_NAME (res_type), /*hidden*/true);
      pop_nested_namespace (std_node);
    }
  return res_type;
}

/* Get constract_assertion_kind of the specified contract. Used when building
  contract_violation object.  */

static contract_assertion_kind
get_contract_assertion_kind (tree contract)
{
  if (CONTRACT_ASSERTION_KIND (contract))
    {
      tree s = CONTRACT_ASSERTION_KIND (contract);
      tree i = (TREE_CODE (s) == INTEGER_CST) ? s
					      : DECL_INITIAL (STRIP_NOPS (s));
      gcc_checking_assert (!type_dependent_expression_p (s) && i);
      return (contract_assertion_kind) tree_to_uhwi (i);
    }

  switch (TREE_CODE (contract))
  {
    case ASSERTION_STMT:	return CAK_ASSERT;
    case PRECONDITION_STMT:	return CAK_PRE;
    case POSTCONDITION_STMT:	return CAK_POST;
    default: break;
  }

  gcc_unreachable ();
}

/* Get contract_evaluation_semantic of the specified contract.  */

contract_evaluation_semantic
get_evaluation_semantic (const_tree contract)
{
  if (CONTRACT_EVALUATION_SEMANTIC (contract))
    {
      tree s = CONTRACT_EVALUATION_SEMANTIC (contract);
      tree i = (TREE_CODE (s) == INTEGER_CST) ? s
					      : DECL_INITIAL (STRIP_NOPS (s));
      gcc_checking_assert (!type_dependent_expression_p (s) && i);
      switch (contract_evaluation_semantic ev =
	      (contract_evaluation_semantic) tree_to_uhwi (i))
	{
	/* This needs to be kept in step with any added semantics.  */
	case CES_IGNORE:
	case CES_OBSERVE:
	case CES_ENFORCE:
	case CES_QUICK:
	  return ev;
	default:
	  break;
	}
    }

  gcc_unreachable ();
}

/* Get location of the last contract in the CONTRACTS tree chain.  */

static location_t
get_contract_end_loc (tree contracts)
{
  tree last = NULL_TREE;
  for (tree a = contracts; a; a = TREE_CHAIN (a))
    last = a;
  gcc_checking_assert (last);
  last = CONTRACT_STATEMENT (last);
  return EXPR_LOCATION (last);
}

struct GTY(()) contract_decl
{
  tree contract_specifiers;
  location_t note_loc;
};

static GTY(()) hash_map<tree, contract_decl> *contract_decl_map;

/* Converts a contract condition to bool and ensures it has a location.  */

tree
finish_contract_condition (cp_expr condition)
{
  if (!condition || error_operand_p (condition))
    return condition;

  /* Ensure we have the condition location saved in case we later need to
     emit a conversion error during template instantiation and wouldn't
     otherwise have it.  This differs from maybe_wrap_with_location in that
     it allows wrappers on EXCEPTIONAL_CLASS_P which includes CONSTRUCTORs.  */
  if (!CAN_HAVE_LOCATION_P (condition)
      && condition.get_location () != UNKNOWN_LOCATION)
    {
      tree_code code
	= (((CONSTANT_CLASS_P (condition) && TREE_CODE (condition) != STRING_CST)
	    || (TREE_CODE (condition) == CONST_DECL && !TREE_STATIC (condition)))
	  ? NON_LVALUE_EXPR : VIEW_CONVERT_EXPR);
      condition = build1_loc (condition.get_location (), code,
			      TREE_TYPE (condition), condition);
      EXPR_LOCATION_WRAPPER_P (condition) = true;
    }

  if (type_dependent_expression_p (condition))
    return condition;

  return condition_conversion (condition);
}

/* Wrap the DECL into VIEW_CONVERT_EXPR representing const qualified version
   of the declaration.  */

tree
view_as_const (tree decl)
{
  if (decl
      && !CP_TYPE_CONST_P (TREE_TYPE (decl)))
    {
      gcc_checking_assert (!contract_const_wrapper_p (decl));
      tree ctype = TREE_TYPE (decl);
      location_t loc =
	  EXPR_P (decl) ? EXPR_LOCATION (decl) : DECL_SOURCE_LOCATION (decl);
      ctype = cp_build_qualified_type (ctype, (cp_type_quals (ctype)
					       | TYPE_QUAL_CONST));
      decl = build1 (VIEW_CONVERT_EXPR, ctype, decl);
      SET_EXPR_LOCATION (decl, loc);
      /* Mark the VCE as contract const wrapper.  */
      CONST_WRAPPER_P (decl) = true;
    }
  return decl;
}

/* True while parsing or substituting a contract condition whose control type
   opts into constification (constify == true).  D4324 makes constification
   opt-in: by default a predicate binds the same overload the function body
   would, so this is false unless a control type turns it on.  */

bool contract_condition_constify_p = false;

/* True while parsing or substituting a contract condition whose control
   type opts into D4324 conveyor-function rules (is_conveyor == true).
   Set via contract_control_is_conveyor at the same call sites as
   contract_condition_constify_p above.  */

bool contract_condition_conveyor_p = false;

/* True while D4324's own internal machinery is probing a contract
   control object's own trait members (is_ignored/constify/assumable/
   is_conveyor/is_symbolic, see contract_control_bool_member below) at
   compile time -- this call has nothing to do with whatever function
   happens to be mid-compilation (current_function_decl) at the exact
   moment it runs, so none of conveyor_restrictions_active_p's own
   restrictions, including the callee-must-be-conveyor check in
   call.cc's build_over_call/typeck.cc's cp_build_function_call_vec,
   should ever apply to it.  Found and fixed via direct testing: without
   this, evaluating a conveyor-declared function's own is_conveyor()/
   is_ignored() trait query for one of its own contracts -- while
   current_function_decl still happens to be that very function --
   silently poisoned the query's own result (the callee-must-be-conveyor
   check rejected the trait-method call itself, since a trait method is
   an ordinary, never conveyor-declared, function), corrupting
   is_conveyor()/is_ignored() to a false negative and, cascading from
   there, disabling self-trust fact-seeding for the function's own
   precondition/postcondition/assert entirely -- a real, confirmed
   regression (d4324-conveyor-divmod-precondition-fact-ok.C and several
   siblings), not a hypothetical one.  */

bool suppress_conveyor_restrictions_for_trait_query_p = false;

/* True while building a converted constant expression (an
   explicit-specifier's own operand, a non-type template argument, an
   array bound, an enumerator value, ...) -- see build_converted_
   constant_expr_internal in call.cc, the single shared helper for all
   of these. A manifestly constant expression can never have side
   effects or exhibit UB: the core language itself already requires
   this (a sub-expression that would need to invoke UB, or that isn't a
   core constant expression for any other reason, simply isn't a valid
   constant expression at all, and evaluation fails with its own,
   conveyor-independent diagnostic -- see cxx_eval_outermost_constant_
   expr in constexpr.cc). So none of conveyor_restrictions_active_p's
   restrictions have anything real left to check while evaluating one,
   regardless of whether we're otherwise inside conveyor-restricted
   code. Found and fixed via direct testing: instantiating std::pair's
   own conditionally-explicit constructors (explicit(bool-expr), where
   bool-expr calls a constexpr helper like _S_convertible()) from
   inside an ordinary, never-conveyor _Rb_tree::erase() overload -- not
   even a conveyor function itself -- tripped the callee-must-be-
   conveyor check purely because completing std::pair as a type
   happened to be needed there, exactly the same false-positive shape
   __or_/__and_'s decltype-based SFINAE hit before it (see
   cp_unevaluated_operand above), just reached through constant-
   expression evaluation instead of an unevaluated operand.  */

bool suppress_conveyor_restrictions_for_converted_constant_expr_p = false;

/* See contracts.h for the full rationale.  */
bool *conveyor_auto_probe_violation_p = nullptr;

/* True if constructs subject to the D4324 conveyor-function syntactic
   restrictions (gcc/cp/constexpr.cc's check_conveyor_function_body, and
   the point-of-construction checks alongside it) should be rejected
   right now -- either because we are in the body of a function declared
   with the 'conveyor' function-specifier, or because we are parsing/
   substituting a contract condition whose control object says
   is_conveyor() == true.  Note this deliberately does NOT cover a
   contract control object's own operator(): only the condition itself
   is ever checked against conveyor rules.  */

bool
conveyor_restrictions_active_p ()
{
  if (suppress_conveyor_restrictions_for_trait_query_p)
    return false;
  if (suppress_conveyor_restrictions_for_converted_constant_expr_p)
    return false;
  /* None of these checks exist to reject code that can never actually
     execute: they exist to prove the code that *does* run at runtime is
     UB-free. cp_unevaluated_operand covers exactly that -- decltype's,
     sizeof's, and noexcept's operands, plus the discarded branch of a
     requires-expression -- so a call/reinterpret_cast/etc. found only
     there can never introduce UB, regardless of whether its callee is
     itself conveyor-declared or well-formed by conveyor's own rules.
     Found and fixed via direct testing: <type_traits>'s own __or_/__and_
     (struct __or_ : decltype(__detail::__or_fn<_Bn...>(0)) { };) are
     pure, body-less SFINAE overloads used only to compute a base-class
     type at compile time -- the "call" to __or_fn never generates any
     code, yet was rejected here as though it were a real, executed call
     to a non-conveyor function, which made ordinary <type_traits>
     queries like is_scalar_v unusable from conveyor-restricted code for
     no actual UB-freedom reason.  */
  if (cp_unevaluated_operand)
    return false;
  if (contract_condition_conveyor_p)
    return true;
  if (current_function_decl
      && DECL_DECLARED_CONVEYOR_P (current_function_decl))
    return true;
  return false;
}

/* Constify access to DECL from within the contract condition.  */

tree
constify_contract_access (tree decl)
{
  /* D4324: constification is opt-in via the control type's constify member.
     When it is off, the access is left as-is so the predicate binds the same
     overload the function body would.  */
  if (!contract_condition_constify_p)
    return decl;

  /* We check if we have a variable, a parameter, a variable of reference type,
   * or a parameter of reference type
   */
  if (!TREE_READONLY (decl)
      && (VAR_P (decl)
	  || (TREE_CODE (decl) == PARM_DECL)
	  || (REFERENCE_REF_P (decl)
	      && (VAR_P (TREE_OPERAND (decl, 0))
		  || (TREE_CODE (TREE_OPERAND (decl, 0)) == PARM_DECL)
		  || (TREE_CODE (TREE_OPERAND (decl, 0))
		      == TEMPLATE_PARM_INDEX)))))
    decl = view_as_const (decl);

  return decl;
}

/* Indicate that PARM_DECL DECL is ODR used in a postcondition.  */

static void
set_parm_used_in_post (tree decl, bool constify = true)
{
  gcc_checking_assert (TREE_CODE (decl) == PARM_DECL);
  DECL_LANG_FLAG_4 (decl) = constify;
}

/* Test if PARM_DECL is ODR used in a postcondition.  */

static bool
parm_used_in_post_p (const_tree decl)
{
  /* Check if this parameter is odr used within a function's postcondition  */
  return ((TREE_CODE (decl) == PARM_DECL) && DECL_LANG_FLAG_4 (decl));
}

/* If declaration DECL is a PARM_DECL and it appears in a postcondition, then
   check that it is not a non-const by-value param. LOCATION is where the
   expression was found and is used for diagnostic purposes.  */

void
check_param_in_postcondition (tree decl, location_t location)
{
  if (processing_postcondition
      && TREE_CODE (decl) == PARM_DECL
      /* TREE_CODE (decl) == PARM_DECL only holds for non-reference
	 parameters.  */
      && !cp_unevaluated_operand
      /* Return value parameter has DECL_ARTIFICIAL flag set. The flag
	 presence of the flag should be sufficient to distinguish the
	 return value parameter in this context.  */
      && !(DECL_ARTIFICIAL (decl)))
    {
      set_parm_used_in_post (decl);

      if (!dependent_type_p (TREE_TYPE (decl))
	  && !CP_TYPE_CONST_P (TREE_TYPE (decl)))
	{
	  auto_diagnostic_group d;
	  error_at (location,
		    "a value parameter used in a postcondition must be const");
	  inform (DECL_SOURCE_LOCATION (decl), "parameter declared here");
	}
    }
}

/* Check if parameters used in postconditions are const qualified on
   a redeclaration that does not specify contracts or on an instantiation
   of a function template.  */

void
check_postconditions_in_redecl (tree olddecl, tree newdecl)
{
  tree contract_spec = get_fn_contract_specifiers (olddecl);
  if (!contract_spec)
    return;

  tree t1 = FUNCTION_FIRST_USER_PARM (olddecl);
  tree t2 = FUNCTION_FIRST_USER_PARM (newdecl);

  for (; t1 && t1 != void_list_node;
       t1 = TREE_CHAIN (t1), t2 = TREE_CHAIN (t2))
    {
      if (parm_used_in_post_p (t1))
	{
	  set_parm_used_in_post (t2);
	  if (!dependent_type_p (TREE_TYPE (t2))
	      && !CP_TYPE_CONST_P (TREE_TYPE (t2))
	      && !TREE_READONLY (t2))
	    {
	      auto_diagnostic_group d;
	      error_at (DECL_SOURCE_LOCATION (t2),
			"value parameter %qE used in a postcondition must be "
			"const", t2);
	      inform (DECL_SOURCE_LOCATION (olddecl),
		      "previous declaration here");
	    }
	}
    }
}

/* Map from FUNCTION_DECL to a FUNCTION_DECL for either the PRE_FN or POST_FN.
   These are used to parse contract conditions and are called inside the body
   of the guarded function.  */
static GTY(()) hash_map<tree, tree> *decl_pre_fn;
static GTY(()) hash_map<tree, tree> *decl_post_fn;

/* Given a pre or post function decl (for an outlined check function) return
   the decl for the function for which the outlined checks are being
   performed.  */
static GTY(()) hash_map<tree, tree> *orig_from_outlined;

/* Map from a CONTRACT tree (a PRECONDITION_STMT/POSTCONDITION_STMT) to its
   persistent predicate core function (see build_predicate_core_function_1).
   Populated eagerly for a virtual function's control-object contracts
   (update_late_contract), so a later contract-less override can inherit
   it (maybe_inherit_virtual_contract) by calling straight into code
   already compiled once, with the base's own access, regardless of
   whether/when the base function itself is ever genericized; populated
   lazily otherwise, exactly as before this map existed.  */
static GTY(()) hash_map<tree, tree> *contract_predicate_core_fn;

/* Caches, per CONTRACT tree, whether it's conveyor-/symbolic-active --
   see oa_cache_contract_flavors's own comment (further below, near
   oa_resolve_object_address_in_function_1) for why a *cache* is needed
   here at all: oa_contract_conveyor_active_p/oa_contract_symbolic_
   active_p do real semantic work (overload resolution + constexpr
   evaluation, via contract_control_bool_member) that is only reliable
   at front-end time, not once GIMPLE exists for a GIMPLE-pass-based
   consumer to call into later (found empirically -- see ~/gimple-
   contract-analysis.md, Sections 9.3/10).  */
static GTY(()) hash_set<tree> *oa_conveyor_active_contract_cache;
static GTY(()) hash_set<tree> *oa_symbolic_active_contract_cache;

/* Makes PRE the precondition function for FNDECL.  */

static void
set_precondition_function (tree fndecl, tree pre)
{
  gcc_assert (pre);
  hash_map_maybe_create<hm_ggc> (decl_pre_fn);
  gcc_checking_assert (!decl_pre_fn->get (fndecl));
  decl_pre_fn->put (fndecl, pre);

  hash_map_maybe_create<hm_ggc> (orig_from_outlined);
  gcc_checking_assert (!orig_from_outlined->get (pre));
  orig_from_outlined->put (pre, fndecl);
}

/* Makes POST the postcondition function for FNDECL.  */

static void
set_postcondition_function (tree fndecl, tree post)
{
  gcc_checking_assert (post);
  hash_map_maybe_create<hm_ggc> (decl_post_fn);
  gcc_checking_assert (!decl_post_fn->get (fndecl));
  decl_post_fn->put (fndecl, post);

  hash_map_maybe_create<hm_ggc> (orig_from_outlined);
  gcc_checking_assert (!orig_from_outlined->get (post));
  orig_from_outlined->put (post, fndecl);
}

/* For a given pre or post condition function, find the checked function.  */
tree
get_orig_for_outlined (tree fndecl)
{
  gcc_checking_assert (fndecl);
  tree *result = hash_map_safe_get (orig_from_outlined, fndecl);
  return result ? *result : NULL_TREE ;
}

/* Given a check function built by build_object_contract_check_function
   for a declaration-level pre<>/post<> clause on a callable-typed
   OBJECT declaration (a VAR_DECL/PARM_DECL, never a FUNCTION_DECL --
   see .claude/plans/stateless-jumping-shore.md), return that object
   decl; NULL_TREE if FNDECL isn't one of these check functions.  Lets
   get_src_loc_impl_ptr_for substitute the object's own declared name
   into a violation's reported "function name" instead of the check
   function's own compiler-synthesized one (e.g. "divide" instead of
   "__contract_post_check_1"), mirroring get_orig_for_outlined's
   identical role for an ordinary function's outlined pre/post
   checks.  */
static GTY(()) hash_map<tree, tree> *object_check_fn_orig_decl;

static tree
get_orig_objdecl_for_check_fn (tree fndecl)
{
  gcc_checking_assert (fndecl);
  tree *result = hash_map_safe_get (object_check_fn_orig_decl, fndecl);
  return result ? *result : NULL_TREE;
}

/* For a given function OLD_FN set suitable names for NEW_FN (which is an
   outlined contract check) usually by appending '.pre' or '.post'.

   For functions with special meaning names (i.e. main and cdtors) we need to
   make special provisions and therefore handle all the contracts function
   name changes here, rather than requiring a separate update to mangle.cc.

   PRE specifies if we need an identifier for a pre or post contract check.  */

static void
contracts_fixup_names (tree new_fn, tree old_fn, bool pre, bool wrapper)
{
  bool cdtor = DECL_CXX_CONSTRUCTOR_P (old_fn)
	       || DECL_CXX_DESTRUCTOR_P (old_fn);
  const char *fname = IDENTIFIER_POINTER (DECL_NAME (old_fn));
  const char *append = wrapper ? "contract_wrapper"
			       : (pre ? "pre" : "post");
  size_t len = strlen (fname);
  /* Cdtor names have a space at the end.  We need to remove that space
     when forming the new identifier.  */
  char *nn = xasprintf ("%.*s%s%s",
			cdtor ? (int)len-1 : int(len),
			fname,
			JOIN_STR,
			append);
  DECL_NAME (new_fn) = get_identifier (nn);
  free (nn);

  /* Now do the mangled version.  */
  fname = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (old_fn));
  nn = xasprintf ("%s%s%s", fname, JOIN_STR, append);
  SET_DECL_ASSEMBLER_NAME (new_fn, get_identifier (nn));
  free (nn);
}

/* Build a declaration for the pre- or postcondition of a guarded FNDECL.  */

static tree
build_contract_condition_function (tree fndecl, bool pre)
{
  if (error_operand_p (fndecl))
    return error_mark_node;

  /* Start the copy.  */
  tree fn = copy_decl (fndecl);

  /* Don't propagate declaration attributes to the checking function,
     including the original contracts.  */
  DECL_ATTRIBUTES (fn) = NULL_TREE;

  /* If requested, disable optimisation of checking functions; this can, in
     some cases, prevent UB from eliding the checks themselves.  */
  if (flag_contract_disable_optimized_checks)
    DECL_ATTRIBUTES (fn)
      = tree_cons (get_identifier ("optimize"),
		   build_tree_list (NULL_TREE, build_string (3, "-O0")),
		   NULL_TREE);

  /* Now parse and add any internal representation of these attrs to the
     decl.  */
  if (DECL_ATTRIBUTES (fn))
    cplus_decl_attributes (&fn, DECL_ATTRIBUTES (fn), 0);

  /* A possible later optimization may delete unused args to prevent extra arg
     passing.  */
  /* Handle the args list.  */
  tree arg_types = NULL_TREE;
  tree *last = &arg_types;
  for (tree arg_type = TYPE_ARG_TYPES (TREE_TYPE (fn));
      arg_type && arg_type != void_list_node;
      arg_type = TREE_CHAIN (arg_type))
    {
      if (DECL_IOBJ_MEMBER_FUNCTION_P (fndecl)
	  && TYPE_ARG_TYPES (TREE_TYPE (fn)) == arg_type)
      continue;
      *last = build_tree_list (TREE_PURPOSE (arg_type), TREE_VALUE (arg_type));
      last = &TREE_CHAIN (*last);
    }

  /* Copy the function parameters, if present.  Disable warnings for them.  */
  DECL_ARGUMENTS (fn) = NULL_TREE;
  if (DECL_ARGUMENTS (fndecl))
    {
      tree *last_a = &DECL_ARGUMENTS (fn);
      for (tree p = DECL_ARGUMENTS (fndecl); p; p = TREE_CHAIN (p))
	{
	  *last_a = copy_decl (p);
	  suppress_warning (*last_a);
	  DECL_CONTEXT (*last_a) = fn;
	  last_a = &TREE_CHAIN (*last_a);
	}
    }

  tree orig_fn_value_type = TREE_TYPE (TREE_TYPE (fn));
  if (!pre && !VOID_TYPE_P (orig_fn_value_type))
    {
      /* For post contracts that deal with a non-void function, append a
	 parameter to pass the return value.  */
      tree name = get_identifier ("__r");
      tree parm = build_lang_decl (PARM_DECL, name, orig_fn_value_type);
      DECL_CONTEXT (parm) = fn;
      DECL_ARTIFICIAL (parm) = true;
      suppress_warning (parm);
      DECL_ARGUMENTS (fn) = chainon (DECL_ARGUMENTS (fn), parm);
      *last = build_tree_list (NULL_TREE, orig_fn_value_type);
      last = &TREE_CHAIN (*last);
    }

  *last = void_list_node;

  tree adjusted_type = NULL_TREE;

  /* The handlers are void fns.  */
  if (DECL_IOBJ_MEMBER_FUNCTION_P (fndecl))
    adjusted_type = build_method_type_directly (DECL_CONTEXT (fndecl),
						void_type_node,
						arg_types);
  else
    adjusted_type = build_function_type (void_type_node, arg_types);

  /* If the original function is noexcept, build a noexcept function.  */
  if (flag_exceptions && type_noexcept_p (TREE_TYPE (fndecl)))
    adjusted_type = build_exception_variant (adjusted_type, noexcept_true_spec);

  TREE_TYPE (fn) = adjusted_type;
  DECL_RESULT (fn) = NULL_TREE; /* Let the start function code fill it in.  */

  /* The contract check functions are never a cdtor, nor virtual.  */
  DECL_CXX_DESTRUCTOR_P (fn) = DECL_CXX_CONSTRUCTOR_P (fn) = 0;
  DECL_VIRTUAL_P (fn) = false;

  /* Append .pre / .post to a usable name for the original function.  */
  contracts_fixup_names (fn, fndecl, pre, /*wrapper*/false);

  DECL_INITIAL (fn) = NULL_TREE;
  CONTRACT_HELPER (fn) = pre ? ldf_contract_pre : ldf_contract_post;
  /* We might have a pre/post for a wrapper.  */
  DECL_CONTRACT_WRAPPER (fn) = DECL_CONTRACT_WRAPPER (fndecl);

  /* Make these functions internal if we can, i.e. if the guarded function is
     not vague linkage, or if we can put them in a comdat group with the
     guarded function.  */
  if (!DECL_WEAK (fndecl) || HAVE_COMDAT_GROUP)
    {
      TREE_PUBLIC (fn) = false;
      DECL_EXTERNAL (fn) = false;
      DECL_WEAK (fn) = false;
      DECL_COMDAT (fn) = false;

      /* We may not have set the comdat group on the guarded function yet.
	 If we haven't, we'll add this to the same group in comdat_linkage
	 later.  Otherwise, add it to the same comdat group now.  */
      if (DECL_ONE_ONLY (fndecl))
	{
	  symtab_node *n = symtab_node::get (fndecl);
	  cgraph_node::get_create (fn)->add_to_same_comdat_group (n);
	}

    }

  DECL_INTERFACE_KNOWN (fn) = true;
  DECL_ARTIFICIAL (fn) = true;
  suppress_warning (fn);

  return fn;
}

/* Build the precondition checking function for FNDECL.  */

static tree
build_precondition_function (tree fndecl)
{
  contract_check_side side
    = DECL_CONTRACT_WRAPPER (fndecl) ? ccs_wrapper : ccs_definition;
  if (!has_active_preconditions (fndecl, side))
    return NULL_TREE;

  return build_contract_condition_function (fndecl, /*pre=*/true);
}

/* Build the postcondition checking function for FNDECL.  If the return
   type is undeduced, don't build the function yet.  We do that in
   apply_deduced_return_type.  */

static tree
build_postcondition_function (tree fndecl)
{
  contract_check_side side
    = DECL_CONTRACT_WRAPPER (fndecl) ? ccs_wrapper : ccs_definition;
  if (!has_active_postconditions (fndecl, side))
    return NULL_TREE;

  tree type = TREE_TYPE (TREE_TYPE (fndecl));
  if (is_auto (type))
    return NULL_TREE;

  return build_contract_condition_function (fndecl, /*pre=*/false);
}

/* If we're outlining the contract, build the functions to do the
   precondition and postcondition checks, and associate them with
   the function decl FNDECL.
 */

static void
build_contract_function_decls (tree fndecl)
{
  /* Build the pre/post functions (or not).  */
  if (!get_precondition_function (fndecl))
    if (tree pre = build_precondition_function (fndecl))
      set_precondition_function (fndecl, pre);

  if (!get_postcondition_function (fndecl))
    if (tree post = build_postcondition_function (fndecl))
      set_postcondition_function (fndecl, post);
}

/* Map from FUNCTION_DECL to a FUNCTION_DECL for contract wrapper.  */

static GTY(()) hash_map<tree, tree> *decl_wrapper_fn = nullptr;

/* Map from the function decl of a wrapper to the function that it wraps.  */

static GTY(()) hash_map<tree, tree> *decl_for_wrapper = nullptr;

/* Makes wrapper the precondition function for FNDECL.  */

static void
set_contract_wrapper_function (tree fndecl, tree wrapper)
{
  gcc_checking_assert (wrapper && fndecl);
  hash_map_maybe_create<hm_ggc> (decl_wrapper_fn);
  gcc_checking_assert (decl_wrapper_fn && !decl_wrapper_fn->get (fndecl));
  decl_wrapper_fn->put (fndecl, wrapper);

  /* We need to know the wrapped function when composing the diagnostic.  */
  hash_map_maybe_create<hm_ggc> (decl_for_wrapper);
  gcc_checking_assert (decl_for_wrapper && !decl_for_wrapper->get (wrapper));
  decl_for_wrapper->put (wrapper, fndecl);
}

/* Returns the wrapper function decl for FNDECL, or null if not set.  */

static tree
get_contract_wrapper_function (tree fndecl)
{
  gcc_checking_assert (fndecl);
  tree *result = hash_map_safe_get (decl_wrapper_fn, fndecl);
  return result ? *result : NULL_TREE;
}

/* Given a wrapper function WRAPPER, find the original function decl.  */

static tree
get_orig_func_for_wrapper (tree wrapper)
{
  gcc_checking_assert (wrapper);
  tree *result = hash_map_safe_get (decl_for_wrapper, wrapper);
  return result ? *result : NULL_TREE;
}

/* Build a declaration for the contract wrapper of a caller FNDECL.
   We're making a caller side contract check wrapper. For caller side contract
   checks, postconditions are only checked if check_post is true.
   Defer the attachment of the contracts to this function until the callee
   is non-dependent, or we get cases where the conditions can be non-dependent
   but still need tsubst-ing.  */

static tree
build_contract_wrapper_function (tree fndecl)
{
  if (error_operand_p (fndecl))
    return error_mark_node;

  /* We should not be trying to build wrappers for templates or functions that
     are still dependent.  */
  gcc_checking_assert (!processing_template_decl
		       && !TYPE_DEPENDENT_P (TREE_TYPE (fndecl)));

  location_t loc = DECL_SOURCE_LOCATION (fndecl);

  /* Fill in the names later.  */
  tree wrapdecl
    = build_lang_decl_loc (loc, FUNCTION_DECL, NULL_TREE, TREE_TYPE (fndecl));

  /* Put the wrapper in the same context as the callee.  */
  DECL_CONTEXT (wrapdecl) = DECL_CONTEXT (fndecl);

  /* This declaration is a contract wrapper function.  */
  DECL_CONTRACT_WRAPPER (wrapdecl) = true;

  contracts_fixup_names (wrapdecl, fndecl, /*pre*/false, /*wrapper*/true);

  DECL_SOURCE_LOCATION (wrapdecl) = loc;
  /* The declaration was implicitly generated by the compiler.  */
  DECL_ARTIFICIAL (wrapdecl) = true;
  /* Declaration, no definition yet.  */
  DECL_INITIAL (wrapdecl) = NULL_TREE;

  /* Let the start function code fill in the result decl.  */
  DECL_RESULT (wrapdecl) = NULL_TREE;

  /* Copy the function parameters, if present.  Suppress (e.g. unused)
     warnings on them.  */
  DECL_ARGUMENTS (wrapdecl) = NULL_TREE;
  if (tree p = DECL_ARGUMENTS (fndecl))
    {
      tree *last_a = &DECL_ARGUMENTS (wrapdecl);
      for (; p; p = TREE_CHAIN (p))
	{
	  *last_a = copy_decl (p);
	  suppress_warning (*last_a);
	  DECL_CONTEXT (*last_a) = wrapdecl;
	  last_a = &TREE_CHAIN (*last_a);
	}
    }

  /* Copy selected attributes from the original function.  */
  TREE_USED (wrapdecl) = TREE_USED (fndecl);

  /* Copy any alignment added.  */
  if (DECL_ALIGN (fndecl))
    SET_DECL_ALIGN (wrapdecl, DECL_ALIGN (fndecl));
  DECL_USER_ALIGN (wrapdecl) = DECL_USER_ALIGN (fndecl);

  /* Make this function internal.  */
  TREE_PUBLIC (wrapdecl) = false;
  DECL_EXTERNAL (wrapdecl) = false;
  DECL_WEAK (wrapdecl) = false;

  /* We know this is an internal function.  */
  DECL_INTERFACE_KNOWN (wrapdecl) = true;
  return wrapdecl;
}

static tree
get_or_create_contract_wrapper_function (tree fndecl)
{
  tree wrapdecl = get_contract_wrapper_function (fndecl);
  if (!wrapdecl)
    {
      wrapdecl = build_contract_wrapper_function (fndecl);
      set_contract_wrapper_function (fndecl, wrapdecl);
    }
  return wrapdecl;
}

void
start_function_contracts (tree fndecl)
{
  if (error_operand_p (fndecl))
    return;

  if (!handle_contracts_p (fndecl))
    return;

  /* If nothing on FNDECL is going to run on its own side (whichever side
     that is -- the wrapper's or the real definition's), do nothing.  */
  {
    contract_check_side side
      = DECL_CONTRACT_WRAPPER (fndecl) ? ccs_wrapper : ccs_definition;
    if (!has_active_preconditions (fndecl, side)
	&& !has_active_postconditions (fndecl, side))
      return;
  }

  /* Check that the postcondition result name, if any, does not shadow a
     function parameter.  */
  for (tree ca = get_fn_contract_specifiers (fndecl); ca; ca = TREE_CHAIN (ca))
    if (POSTCONDITION_P (CONTRACT_STATEMENT (ca)))
      if (tree id = POSTCONDITION_IDENTIFIER (CONTRACT_STATEMENT (ca)))
	{
	  if (id == error_mark_node)
	    {
	      CONTRACT_CONDITION (CONTRACT_STATEMENT (ca)) = error_mark_node;
	      continue;
	    }
	  tree r_name = tree_strip_any_location_wrapper (id);
	  if (TREE_CODE (id) == PARM_DECL)
	    r_name = DECL_NAME (id);
	  gcc_checking_assert (r_name && TREE_CODE (r_name) == IDENTIFIER_NODE);
	  tree seen = lookup_name (r_name);
	  if (seen
	      && TREE_CODE (seen) == PARM_DECL
	      && DECL_CONTEXT (seen) == fndecl)
	    {
		auto_diagnostic_group d;
		location_t id_l = location_wrapper_p (id)
				  ? EXPR_LOCATION (id)
				  : DECL_SOURCE_LOCATION (id);
		location_t co_l = EXPR_LOCATION (CONTRACT_STATEMENT (ca));
		if (id_l != UNKNOWN_LOCATION)
		  co_l = make_location (id_l, co_l, co_l);
		error_at (co_l, "contract postcondition result name shadows a"
			  " function parameter");
		inform (DECL_SOURCE_LOCATION (seen),
			"parameter declared here");
		POSTCONDITION_IDENTIFIER (CONTRACT_STATEMENT (ca))
		  = error_mark_node;
		CONTRACT_CONDITION (CONTRACT_STATEMENT (ca)) = error_mark_node;
	    }
	}

  /* If we are expanding contract assertions inline then no need to declare
     the outline function decls.  */
  if (!flag_contract_checks_outlined)
    return;

  /* Contracts may have just been added without a chance to parse them, though
     we still need the PRE_FN available to generate a call to it.  */
  /* Do we already have declarations generated ? */
  if (!DECL_PRE_FN (fndecl) && !DECL_POST_FN (fndecl))
    build_contract_function_decls (fndecl);
}

void
maybe_update_postconditions (tree fndecl)
{
  /* Update any postconditions and the postcondition checking function
     as needed.  If there are postconditions, we'll use those to rewrite
     return statements to check postconditions.  */
  contract_check_side side
    = DECL_CONTRACT_WRAPPER (fndecl) ? ccs_wrapper : ccs_definition;
  if (has_active_postconditions (fndecl, side))
    {
      rebuild_postconditions (fndecl);
      tree post = build_postcondition_function (fndecl);
      set_postcondition_function (fndecl, post);
    }
}

/* Build and return an argument list containing all the parameters of the
   (presumably guarded) function decl FNDECL.  This can be used to forward
   all of FNDECL arguments to a function taking the same list of arguments
   -- namely the unchecked form of FNDECL.

   We use CALL_FROM_THUNK_P instead of forward_parm for forwarding
   semantics.  */

static vec<tree, va_gc> *
build_arg_list (tree fndecl)
{
  vec<tree, va_gc> *args = make_tree_vector ();
  for (tree t = DECL_ARGUMENTS (fndecl); t; t = DECL_CHAIN (t))
    vec_safe_push (args, t);
  return args;
}

/* Build and return a thunk like call to FUNC from CALLER using the supplied
   arguments.  The call is like a thunk call in the fact that we do not
   want to create additional copies of the arguments.  We can not simply reuse
   the thunk machinery as it does more than we want.  More specifically, we
   don't want to mark the calling function as `DECL_THUNK_P` for this
   particular purpose, we only want the special treatment for the parameters
   of the call we are about to generate.  We temporarily mark the calling
   function as DECL_THUNK_P so build_call_a does the right thing.  */

static tree
build_thunk_like_call (tree func, int n, tree *argarray)
{
  bool old_decl_thunk_p = DECL_THUNK_P (current_function_decl);
  LANG_DECL_FN_CHECK (current_function_decl)->thunk_p  = true;

  tree call = build_call_a (func, n, argarray);

  /* Revert the `DECL_THUNK_P` flag.  */
  LANG_DECL_FN_CHECK (current_function_decl)->thunk_p = old_decl_thunk_p;

  /* Mark the call as a thunk call to allow for correct gimplification
   of the arguments.  */
  CALL_FROM_THUNK_P (call) = true;

  return call;
}

/* If we have a precondition function and it's valid, call it.  */

static void
add_pre_condition_fn_call (tree fndecl)
{
  /* If we're starting a guarded function with valid contracts, we need to
     insert a call to the pre function.  */
  gcc_checking_assert (DECL_PRE_FN (fndecl)
		       && DECL_PRE_FN (fndecl) != error_mark_node);

  releasing_vec args = build_arg_list (fndecl);
  tree call = build_thunk_like_call (DECL_PRE_FN (fndecl),
				     args->length (), args->address ());

  finish_expr_stmt (call);
}

/* Returns the parameter corresponding to the return value of a guarded
   function FNDECL.  Returns NULL_TREE if FNDECL has no postconditions or
   is void.  */

static tree
get_postcondition_result_parameter (tree fndecl)
{
  if (!fndecl || fndecl == error_mark_node)
    return NULL_TREE;

  if (VOID_TYPE_P (TREE_TYPE (TREE_TYPE (fndecl))))
    return NULL_TREE;

  tree post = DECL_POST_FN (fndecl);
  if (!post || post == error_mark_node)
    return NULL_TREE;

  /* The last param is the return value.  */
  return tree_last (DECL_ARGUMENTS (post));
}

/* Build and add a call to the post-condition checking function, when that
   is in use.  */

static void
add_post_condition_fn_call (tree fndecl)
{
  gcc_checking_assert (DECL_POST_FN (fndecl)
		       && DECL_POST_FN (fndecl) != error_mark_node);

  releasing_vec args = build_arg_list (fndecl);
  if (get_postcondition_result_parameter (fndecl))
    vec_safe_push (args, DECL_RESULT (fndecl));
  tree call = build_thunk_like_call (DECL_POST_FN (fndecl),
				     args->length (), args->address ());
  finish_expr_stmt (call);
}

/* Copy (possibly a sub-set of) contracts from CONTRACTS on FNDECL.  */

static tree
copy_contracts_list (tree contracts, tree fndecl,
		     contract_match_kind remap_kind = cmk_all)
{
  tree last = NULL_TREE, new_contracts = NULL_TREE;
  for (; contracts; contracts = TREE_CHAIN (contracts))
    {
      if ((remap_kind == cmk_pre
	   && (TREE_CODE (CONTRACT_STATEMENT (contracts))
	       == POSTCONDITION_STMT))
	  || (remap_kind == cmk_post
	      && (TREE_CODE (CONTRACT_STATEMENT (contracts))
		  == PRECONDITION_STMT)))
	continue;

      contract_check_side side
	= contract_side_of (CONTRACT_STATEMENT (contracts), fndecl);
      if (!contract_runs_on_side (CONTRACT_STATEMENT (contracts), side))
	continue;

      tree c = copy_node (contracts);
      TREE_VALUE (c) = build_tree_list (TREE_PURPOSE (TREE_VALUE (c)),
					copy_node (CONTRACT_STATEMENT (c)));

      copy_body_data id;
      hash_map<tree, tree> decl_map;

      memset (&id, 0, sizeof (id));

      id.src_fn = fndecl;
      id.dst_fn = fndecl;
      id.src_cfun = DECL_STRUCT_FUNCTION (fndecl);
      id.decl_map = &decl_map;

      id.copy_decl = retain_decl;

      id.transform_call_graph_edges = CB_CGE_DUPLICATE;
      id.transform_new_cfg = false;
      id.transform_return_to_modify = false;
      id.transform_parameter = true;

      /* Make sure not to unshare trees behind the front-end's back
	 since front-end specific mechanisms may rely on sharing.  */
      id.regimplify = false;
      id.do_not_unshare = true;
      id.do_not_fold = true;

      /* We're not inside any EH region.  */
      id.eh_lp_nr = 0;
      walk_tree (&CONTRACT_CONDITION (CONTRACT_STATEMENT (c)),
				      copy_tree_body_r, &id, NULL);


      CONTRACT_COMMENT (CONTRACT_STATEMENT (c))
	= copy_node (CONTRACT_COMMENT (CONTRACT_STATEMENT (c)));

      chainon (last, c);
      last = c;
      if (!new_contracts)
	new_contracts = c;
    }
  return new_contracts;
}

/* Returns a copy of FNDECL contracts. This is used when emitting a contract.
 If we were to emit the original contract tree, any folding of the contract
 condition would affect the original contract too. The original contract
 tree needs to be preserved in case it is used to apply to a different
 function (for inheritance or wrapping reasons). */

static tree
copy_contracts (tree fndecl, contract_match_kind remap_kind = cmk_all)
{
  tree contracts = get_fn_contract_specifiers (fndecl);
  return copy_contracts_list (contracts, fndecl, remap_kind);
}

/* Add the contract statement CONTRACT to the current block if valid.  */

static bool
emit_contract_statement (tree contract)
{
  /* Only add valid contracts.  */
  if (contract == error_mark_node
      || CONTRACT_CONDITION (contract) == error_mark_node)
    return false;

  if (get_evaluation_semantic (contract) == CES_INVALID)
    return false;

  add_stmt (contract);
  return true;
}

/* Generate the statement for the given contract by adding the contract
   statement to the current block. Returns the next contract in the chain.  */

static tree
emit_contract (tree contract)
{
  gcc_assert (TREE_CODE (contract) == TREE_LIST);

  emit_contract_statement (CONTRACT_STATEMENT (contract));

  return TREE_CHAIN (contract);
}

/* Add a call or a direct evaluation of the pre checks.  */

static void
apply_preconditions (tree fndecl)
{
  if (flag_contract_checks_outlined)
    add_pre_condition_fn_call (fndecl);
  else
  {
    tree contract_copy = copy_contracts (fndecl, cmk_pre);
    for (; contract_copy; contract_copy = TREE_CHAIN (contract_copy))
      emit_contract (contract_copy);
  }
}

/* Add a call or a direct evaluation of the post checks.  */

static void
apply_postconditions (tree fndecl)
{
  if (flag_contract_checks_outlined)
    add_post_condition_fn_call (fndecl);
  else
    {
      tree contract_copy = copy_contracts (fndecl, cmk_post);
      for (; contract_copy; contract_copy = TREE_CHAIN (contract_copy))
	emit_contract (contract_copy);
    }
}

/* Add contract handling to the function in FNDECL.

   When we have only pre-conditions, this simply prepends a call (or a direct
   evaluation, for cdtors) to the existing function body.

   When we have post conditions we build a try-finally block.
   If the function might throw then the handler in the try-finally is an
   EH_ELSE expression, where the post condition check is applied to the
   non-exceptional path, and an empty statement is added to the EH path.  If
   the function has a non-throwing eh spec, then the handler is simply the
   post-condition checker.  */

void
maybe_apply_function_contracts (tree fndecl)
{
  if (!handle_contracts_p (fndecl))
    /* We did nothing and the original function body statement list will be
       popped by our caller.  */
    return;

  /* If nothing on FNDECL is going to run on its own side (whichever side
     that is -- the wrapper's or the real definition's), do nothing.  */
  contract_check_side side
    = DECL_CONTRACT_WRAPPER (fndecl) ? ccs_wrapper : ccs_definition;
  bool do_pre = has_active_preconditions (fndecl, side);
  bool do_post = has_active_postconditions (fndecl, side);
  if (!do_pre && !do_post)
    return;

  /* If the function is noexcept, the user's written body will be wrapped in a
     MUST_NOT_THROW expression.  In that case we leave the MUST_NOT_THROW in
     place and do our replacement inside it.  */
  tree fnbody;
  if (TYPE_NOEXCEPT_P (TREE_TYPE (fndecl)))
    {
      tree m_n_t_expr = expr_first (DECL_SAVED_TREE (fndecl));
      gcc_checking_assert (TREE_CODE (m_n_t_expr) == MUST_NOT_THROW_EXPR);
      fnbody = TREE_OPERAND (m_n_t_expr, 0);
      TREE_OPERAND (m_n_t_expr, 0) = push_stmt_list ();
    }
  else
    {
      fnbody = DECL_SAVED_TREE (fndecl);
      DECL_SAVED_TREE (fndecl) = push_stmt_list ();
    }

  /* If we have a lambda with captures, ensure that those captures are in-
     scope for pre and post conditions.  */
  if (LAMBDA_FUNCTION_P (fndecl)
      && TREE_CODE (fnbody) == BIND_EXPR)
    {
      tree extract = BIND_EXPR_BODY (fnbody);
      BIND_EXPR_BODY (fnbody) = NULL_TREE;
      add_stmt (fnbody);
      BIND_EXPR_BODY (fnbody) = push_stmt_list ();
      fnbody = extract;
    }

  /* Now add the pre and post conditions to the existing function body.
     This copies the approach used for function try blocks.  */
  tree compound_stmt = begin_compound_stmt (0);
  current_binding_level->artificial = true;

  /* Do not add locations for the synthesised code.  */
  location_t loc = UNKNOWN_LOCATION;

  /* For other cases, we call a function to process the check.  */

  /* If we have a pre, but not a post, then just emit that and we are done.  */
  if (!do_post)
    {
      apply_preconditions (fndecl);
      add_stmt (fnbody);
      finish_compound_stmt (compound_stmt);
      return;
    }

  if (do_pre)
    /* Add a precondition call, if we have one. */
    apply_preconditions (fndecl);
  tree try_fin = build_stmt (loc, TRY_FINALLY_EXPR, fnbody, NULL_TREE);
  add_stmt (try_fin);
  TREE_OPERAND (try_fin, 1) = push_stmt_list ();
  /* If we have exceptions, and a function that might throw, then add
     an EH_ELSE clause that allows the exception to propagate upwards
     without encountering the post-condition checks.  */
  if (flag_exceptions && !type_noexcept_p (TREE_TYPE (fndecl)))
    {
      tree eh_else = build_stmt (loc, EH_ELSE_EXPR, NULL_TREE, NULL_TREE);
      add_stmt (eh_else);
      TREE_OPERAND (eh_else, 0) = push_stmt_list ();
      apply_postconditions (fndecl);
      TREE_OPERAND (eh_else, 0) = pop_stmt_list (TREE_OPERAND (eh_else, 0));
      TREE_OPERAND (eh_else, 1) = void_node;
    }
  else
    apply_postconditions (fndecl);
  TREE_OPERAND (try_fin, 1) = pop_stmt_list (TREE_OPERAND (try_fin, 1));
  finish_compound_stmt (compound_stmt);
  /* The DECL_SAVED_TREE stmt list will be popped by our caller.  */
}

/* Rewrite the condition of contract in place, so that references to SRC's
   parameters are updated to refer to DST's parameters. The postcondition
   result variable is left unchanged.

   When declarations are merged, we sometimes need to update contracts to
   refer to new parameters.

   If DUPLICATE_P is true, this is called by duplicate_decls to rewrite
   contracts in terms of a new set of parameters.  This also preserves the
   references to postcondition results, which are not replaced during
   merging.  */

static void
remap_contract (tree src, tree dst, tree contract, bool duplicate_p)
{
  copy_body_data id;
  hash_map<tree, tree> decl_map;

  memset (&id, 0, sizeof (id));
  id.src_fn = src;
  id.dst_fn = dst;
  id.src_cfun = DECL_STRUCT_FUNCTION (src);
  id.decl_map = &decl_map;

  /* If we're merging contracts, don't copy local variables.  */
  id.copy_decl = duplicate_p ? retain_decl : copy_decl_no_change;

  id.transform_call_graph_edges = CB_CGE_DUPLICATE;
  id.transform_new_cfg = false;
  id.transform_return_to_modify = false;
  id.transform_parameter = true;

  /* Make sure not to unshare trees behind the front-end's back
     since front-end specific mechanisms may rely on sharing.  */
  id.regimplify = false;
  id.do_not_unshare = true;
  id.do_not_fold = true;

  /* We're not inside any EH region.  */
  id.eh_lp_nr = 0;

  bool do_remap = false;

  /* Insert parameter remappings.  */
  gcc_checking_assert (TREE_CODE (src) == FUNCTION_DECL);
  gcc_checking_assert (TREE_CODE (dst) == FUNCTION_DECL);

  int src_num_artificial_args = num_artificial_parms_for (src);
  int dst_num_artificial_args = num_artificial_parms_for (dst);

  for (tree sp = DECL_ARGUMENTS (src), dp = DECL_ARGUMENTS (dst);
       sp || dp;
       sp = DECL_CHAIN (sp), dp = DECL_CHAIN (dp))
    {
      if (!sp && dp
	  && TREE_CODE (contract) == POSTCONDITION_STMT
	  && DECL_CHAIN (dp) == NULL_TREE)
	{
	  gcc_assert (!duplicate_p);
	  if (tree result = POSTCONDITION_IDENTIFIER (contract))
	    {
	      gcc_assert (DECL_P (result));
	      insert_decl_map (&id, result, dp);
	      do_remap = true;
	    }
	  break;
	}
      gcc_assert (sp && dp);

      if (sp == dp)
	continue;

      insert_decl_map (&id, sp, dp);
      do_remap = true;

      /* First artificial arg is *this. We want to remap that.  However, we
	 want to skip _in_charge param and __vtt_parm.  Do so now.  */
      if (src_num_artificial_args > 0)
	{
	  while (--src_num_artificial_args,src_num_artificial_args > 0)
	    sp = DECL_CHAIN (sp);
	}
      if (dst_num_artificial_args > 0)
	{
	  while (--dst_num_artificial_args,dst_num_artificial_args > 0)
	    dp = DECL_CHAIN (dp);
	}
    }

  if (!do_remap)
    return;

  walk_tree (&CONTRACT_CONDITION (contract), copy_tree_body_r, &id, NULL);
}

/* Returns a copy of SOURCE contracts where any references to SOURCE's
   PARM_DECLs have been rewritten to the corresponding PARM_DECL in DEST.  */

tree
copy_and_remap_contracts (tree dest, tree source,
			  contract_match_kind remap_kind, bool for_wrapper)
{
  tree last = NULL_TREE, contracts_copy= NULL_TREE;
  tree contracts = get_fn_contract_specifiers (source);
  for (; contracts; contracts = TREE_CHAIN (contracts))
    {
      if ((remap_kind == cmk_pre
	   && (TREE_CODE (CONTRACT_STATEMENT (contracts))
	       == POSTCONDITION_STMT))
	  || (remap_kind == cmk_post
	      && (TREE_CODE (CONTRACT_STATEMENT (contracts))
		  == PRECONDITION_STMT)))
	continue;

      /* update_contract_arguments uses this function for plain
	 redeclaration/definition argument-name bookkeeping, unrelated to
	 caller-vs-definition side selection, and must keep copying every
	 contract unconditionally; only a genuine caller-side wrapper copy
	 (define_contract_wrapper_func) filters by side here.  */
      if (for_wrapper
	  && !contract_runs_on_side (CONTRACT_STATEMENT (contracts),
				     ccs_wrapper))
	continue;

      /* The first part is copying of the legacy attribute layout - eventually
	 this will go away.  */
      tree c = copy_node (contracts);
      TREE_VALUE (c) = build_tree_list (TREE_PURPOSE (TREE_VALUE (c)),
					copy_node (CONTRACT_STATEMENT (c)));
      /* This is the copied contract statement.  */
      tree stmt = CONTRACT_STATEMENT (c);

      /* If we have an erroneous postcondition identifier, we also mark the
	 condition as invalid so only need to check that.  */
      if (CONTRACT_CONDITION (stmt) != error_mark_node)
	remap_contract (source, dest, stmt, /*duplicate_p=*/true);

      if (TREE_CODE (stmt) == POSTCONDITION_STMT)
	{
	  /* If we have a postcondition return value placeholder, then
	     ensure the copied one has the correct context.  */
	  tree var = POSTCONDITION_IDENTIFIER (stmt);
	  if (var && var != error_mark_node)
	    DECL_CONTEXT (var) = dest;
	}

      if (CONTRACT_COMMENT (stmt) != error_mark_node)
	CONTRACT_COMMENT (stmt) = copy_node (CONTRACT_COMMENT (stmt));

      chainon (last, c);
      last = c;
      if (!contracts_copy)
	contracts_copy = c;
    }

  return contracts_copy;
}

/* Set the (maybe) parsed contract specifier LIST for DECL.  */

void
set_fn_contract_specifiers (tree decl, tree list)
{
  if (!decl || error_operand_p (decl))
    return;

  bool existed = false;
  contract_decl& rd
    = hash_map_safe_get_or_insert<hm_ggc> (contract_decl_map, decl, &existed);
  if (!existed)
    {
      /* This is the first time we encountered this decl, save the location
	 for error messages.  This will ensure all error messages refer to the
	 contracts used for the function.  */
      location_t decl_loc = DECL_SOURCE_LOCATION (decl);
      location_t cont_end = decl_loc;
      if (list)
	cont_end = get_contract_end_loc (list);
      rd.note_loc = make_location (decl_loc, decl_loc, cont_end);
    }
  rd.contract_specifiers = list;
}

/* Update the entry for DECL in the map of contract specifiers with the
  contracts in LIST. */

void
update_fn_contract_specifiers (tree decl, tree list)
{
  if (!decl || error_operand_p (decl))
    return;

  bool existed = false;
  contract_decl& rd
    = hash_map_safe_get_or_insert<hm_ggc> (contract_decl_map, decl, &existed);
  gcc_checking_assert (existed);

  /* We should only get here when we parse deferred contracts.  */
  gcc_checking_assert (!contract_any_deferred_p (list));

  rd.contract_specifiers = list;
}

/* When a decl is about to be removed, then we need to release its content and
   then take it out of the map.  */

void
remove_decl_with_fn_contracts_specifiers (tree decl)
{
  if (contract_decl *p = hash_map_safe_get (contract_decl_map, decl))
    {
      p->contract_specifiers = NULL_TREE;
      contract_decl_map->remove (decl);
    }
}

/* If this function has contract specifiers, then remove them, but leave the
   function registered.  */

void
remove_fn_contract_specifiers (tree decl)
{
  if (contract_decl *p = hash_map_safe_get (contract_decl_map, decl))
    {
      p->contract_specifiers = NULL_TREE;
    }
}

/* Get the contract specifier list for this DECL if there is one.  */

tree
get_fn_contract_specifiers (tree decl)
{
  if (contract_decl *p = hash_map_safe_get (contract_decl_map, decl))
    return p->contract_specifiers;
  return NULL_TREE;
}

/* Map from a CONTRACT (PRECONDITION_STMT/POSTCONDITION_STMT) to the
   ordered TREE_LIST (TREE_VALUE of each node a decl) of the decls its
   condition treats as its own positional parameters -- either the real
   declarator's own named PARM_DECLs (the "shortcut" form, no binder
   list) or the synthetic binder decls built by make_postcondition_
   variable (a comma-separated binder list).  Populated at parse time
   (cp_parser_function_contract_specifier, parser.cc) for every eagerly
   -parsed contract, not just ones on a declaration-level callable-typed
   object clause -- see .claude/plans/stateless-jumping-shore.md -- but
   only ever consulted for that feature: an ordinary function's own
   contracts are remapped via DECL_ARGUMENTS instead (remap_contract),
   which needs no such side table.  */

static GTY(()) hash_map<tree, tree> *contract_positional_parms_map;

void
set_contract_positional_parms (tree contract, tree parms)
{
  if (!contract || contract == error_mark_node)
    return;
  hash_map_maybe_create<hm_ggc> (contract_positional_parms_map);
  contract_positional_parms_map->put (contract, parms);
}

tree
get_contract_positional_parms (tree contract)
{
  if (tree *p = hash_map_safe_get (contract_positional_parms_map, contract))
    return *p;
  return NULL_TREE;
}

/* A subroutine of duplicate_decls. Diagnose issues in the redeclaration of
   guarded functions.  */

void
check_redecl_contract (tree newdecl, tree olddecl)
{
  if (!flag_contracts)
    return;

  if (TREE_CODE (newdecl) == TEMPLATE_DECL)
    newdecl = DECL_TEMPLATE_RESULT (newdecl);
  if (TREE_CODE (olddecl) == TEMPLATE_DECL)
    olddecl = DECL_TEMPLATE_RESULT (olddecl);

  tree new_contracts = get_fn_contract_specifiers (newdecl);
  tree old_contracts = get_fn_contract_specifiers (olddecl);

  if (!old_contracts && !new_contracts)
    return;

  /* We should always be comparing with the 'first' declaration which should
   have been recorded already (if it has contract specifiers).  However
   if the new decl is trying to add contracts, that is an error and we do
   not want to create a map entry yet.  */
  contract_decl *rdp = hash_map_safe_get (contract_decl_map, olddecl);
  gcc_checking_assert(rdp || !old_contracts);

  location_t new_loc = DECL_SOURCE_LOCATION (newdecl);
  if (new_contracts && !old_contracts)
    {
      auto_diagnostic_group d;
      /* If a re-declaration has contracts, they must be the same as those
       that appear on the first declaration seen (they cannot be added).  */
      location_t cont_end = get_contract_end_loc (new_contracts);
      cont_end = make_location (new_loc, new_loc, cont_end);
      error_at (cont_end, "declaration adds contracts to %q#D", olddecl);
      inform (DECL_SOURCE_LOCATION (olddecl), "first declared here");
      return;
    }

  if (old_contracts && !new_contracts)
    /* We allow re-declarations to omit contracts declared on the initial decl.
       In fact, this is required if the conditions contain lambdas.  Check if
       all the parameters are correctly const qualified. */
    check_postconditions_in_redecl (olddecl, newdecl);
  else if (old_contracts && new_contracts
	   && !contract_any_deferred_p (old_contracts)
	   && contract_any_deferred_p (new_contracts)
	   && DECL_UNIQUE_FRIEND_P (newdecl))
    {
      /* Put the deferred contracts on the olddecl so we parse it when
	 we can.  */
      set_fn_contract_specifiers (olddecl, old_contracts);
    }
  else if (contract_any_deferred_p (old_contracts)
	   || contract_any_deferred_p (new_contracts))
    {
      /* TODO: ignore these and figure out how to process them later.  */
      /* Note that a friend declaration has deferred contracts, but the
	 declaration of the same function outside the class definition
	 doesn't.  */
    }
  else
    {
      gcc_checking_assert (old_contracts);
      location_t cont_end = get_contract_end_loc (new_contracts);
      cont_end = make_location (new_loc, new_loc, cont_end);
      /* We have two sets - they should match or we issue a diagnostic.  */
      match_contract_specifiers (rdp->note_loc, old_contracts,
				 cont_end, new_contracts);
    }

  return;
}

/* A subroutine of duplicate_decls, for VAR_DECL/PARM_DECL redeclarations
   of a callable-typed object carrying a declaration-level pre<>/post<>
   clause (see .claude/plans/stateless-jumping-shore.md) -- the object-
   declaration analogue of check_redecl_contract, following the same
   consistency rule (a later declaration may omit the clause, inheriting
   the first; must match structurally if both specify one; may never
   add one where the first had none).  Deliberately simpler than
   check_redecl_contract: there is no function body here to const-check
   parameters against (check_postconditions_in_redecl), and no template/
   friend deferral to consider, since this feature currently only
   supports the eager, non-class-body parse path.  */

void
check_redecl_object_contract (tree newdecl, tree olddecl)
{
  if (!flag_contracts)
    return;

  tree new_contracts = get_fn_contract_specifiers (newdecl);
  tree old_contracts = get_fn_contract_specifiers (olddecl);

  if (!old_contracts && !new_contracts)
    return;

  contract_decl *rdp = hash_map_safe_get (contract_decl_map, olddecl);
  gcc_checking_assert (rdp || !old_contracts);

  location_t new_loc = DECL_SOURCE_LOCATION (newdecl);
  if (new_contracts && !old_contracts)
    {
      auto_diagnostic_group d;
      /* If a re-declaration has a clause, it must be the same as the one
	 on the first declaration seen (it cannot be added).  */
      location_t cont_end = get_contract_end_loc (new_contracts);
      cont_end = make_location (new_loc, new_loc, cont_end);
      error_at (cont_end, "declaration adds a contract specifier to %q#D",
		olddecl);
      inform (DECL_SOURCE_LOCATION (olddecl), "first declared here");
      return;
    }

  if (old_contracts && !new_contracts)
    /* Re-declarations may omit a clause declared on the initial decl --
       it is inherited.  */
    return;

  gcc_checking_assert (old_contracts);
  location_t cont_end = get_contract_end_loc (new_contracts);
  cont_end = make_location (new_loc, new_loc, cont_end);
  /* We have two sets - they should match or we issue a diagnostic.  */
  match_contract_specifiers (rdp->note_loc, old_contracts, cont_end,
			     new_contracts);
}

/* Update the contracts of DEST to match the argument names from contracts
  of SRC. When we merge two declarations in duplicate_decls, we preserve the
  arguments from the new declaration, if the new declaration is a
  definition. We need to update the contracts accordingly.  */

void
update_contract_arguments (tree srcdecl, tree destdecl)
{
  tree src_contracts = get_fn_contract_specifiers (srcdecl);
  tree dest_contracts = get_fn_contract_specifiers (destdecl);

  if (!src_contracts && !dest_contracts)
    return;

  /* Check if src even has contracts. It is possible that a redeclaration
    does not have contracts. Is this is the case, first apply contracts
    to src.  */
  if (!src_contracts)
    {
      if (contract_any_deferred_p (dest_contracts))
	{
	  set_fn_contract_specifiers (srcdecl, dest_contracts);
	  /* Nothing more to do here.  */
	  return;
	}
      else
	set_fn_contract_specifiers
	  (srcdecl, copy_and_remap_contracts (srcdecl, destdecl));
    }

  /* For deferred contracts, we currently copy the tokens from the redeclaration
    onto the decl that will be preserved. This is not ideal because the
    redeclaration may have erroneous contracts.
    For non deferred contracts we currently do copy and remap, which is doing
    more than we need.  */
  if (contract_any_deferred_p (src_contracts))
    set_fn_contract_specifiers (destdecl, src_contracts);
  else
    {
      /* Temporarily rename the arguments to get the right mapping.  */
      tree tmp_arguments = DECL_ARGUMENTS (destdecl);
      DECL_ARGUMENTS (destdecl) = DECL_ARGUMENTS (srcdecl);
      set_fn_contract_specifiers (destdecl,
				  copy_and_remap_contracts (destdecl, srcdecl));
      DECL_ARGUMENTS (destdecl) = tmp_arguments;
    }
}

/* Forward-declared: full definitions are much further below (need the
   shared oa_* fact-tracking substrate's own declarations first); needed
   here by maybe_contract_wrap_call, which runs at ordinary call-
   resolution time, well before any of that substrate is otherwise
   reached.  */
static void oa_maybe_instantiate_contracts (tree fn);
static bool oa_contract_conveyor_active_p (tree contract, tree owner_fn);
static bool oa_contract_symbolic_active_p (tree contract, tree owner_fn);

/* Checks if a contract check wrapper is needed for fndecl.  DO_PRE/DO_POST
   (computed with side ccs_wrapper) already fold in both the ordinary
   -fcontracts-client-check policy and any per-contract
   force_client_side_check/force_definition_side_check override, so a
   wrapper is needed simply if either is set.  */

static bool
should_contract_wrap_call (bool do_pre, bool do_post)
{
  return do_pre || do_post;
}

/* Possibly replace call with a call to a wrapper function which
   will do the contracts check required around a CALL to FNDECL.  */

tree
maybe_contract_wrap_call (tree fndecl, tree call)
{
  /* We can be called from build_cxx_call without a known callee.  */
  if (!fndecl)
    return call;

  if (error_operand_p (fndecl) || !call || call == error_mark_node)
    return error_mark_node;

  if (!handle_contracts_p (fndecl))
    return call;

  /* D4324: mark the caller (current_function_decl) as possibly needing
     the oa_* walk if FNDECL's own precondition is conveyor- or
     symbolic-active -- a different, narrower notion than the has_
     active_preconditions/has_active_postconditions check just below
     (which cares about wrapper generation, not conveyor/symbolic
     classification), so this sits alongside that check, not instead
     of it.  oa_maybe_instantiate_contracts first, exactly like oa_
     scan_calls_in_expr/oa_function_needs_walk_p, since FNDECL's own
     contract specifiers can still be pointing at an uninstantiated
     template pattern at this, ordinary call-resolution, timing.  See
     DECL_MIGHT_NEED_OA_SCAN_P's own comment for the full list of
     touch points feeding this bit.  */
  if (current_function_decl)
    {
      oa_maybe_instantiate_contracts (fndecl);
      for (tree as = get_fn_contract_specifiers (fndecl); as; as = TREE_CHAIN (as))
	{
	  tree contract = CONTRACT_STATEMENT (as);
	  if (PRECONDITION_P (contract)
	      && (oa_contract_conveyor_active_p (contract, fndecl)
		  || oa_contract_symbolic_active_p (contract, fndecl)))
	    {
	      SET_DECL_MIGHT_NEED_OA_SCAN_P (current_function_decl);
	      break;
	    }
	}
    }

  bool do_pre = has_active_preconditions (fndecl, ccs_wrapper);
  bool do_post = has_active_postconditions (fndecl, ccs_wrapper);

  /* Check if we need a wrapper.  */
  if (!should_contract_wrap_call (do_pre, do_post))
    return call;

  /* Build the declaration of the wrapper, if we need to.  */
  tree wrapdecl = get_or_create_contract_wrapper_function (fndecl);

  unsigned nargs = call_expr_nargs (call);
  vec<tree, va_gc> *argwrap;
  vec_alloc (argwrap, nargs);

  tree arg;
  call_expr_arg_iterator iter;
  FOR_EACH_CALL_EXPR_ARG (arg, iter, call)
    argwrap->quick_push (arg);

  tree wrapcall = build_call_expr_loc_vec (DECL_SOURCE_LOCATION (wrapdecl),
					   wrapdecl, argwrap);

  return wrapcall;
}

/* Map traversal callback to define a wrapper function.
   This generates code for client-side contract check wrappers and the
   noexcept wrapper around the contract violation handler.  */

bool
define_contract_wrapper_func (const tree& fndecl, const tree& wrapdecl, void*)
{
  /* If we already built this function on a previous pass, then do nothing.  */
  if (DECL_INITIAL (wrapdecl) && DECL_INITIAL (wrapdecl) != error_mark_node)
    return true;

  gcc_checking_assert (!DECL_HAS_CONTRACTS_P (wrapdecl));
  /* Which of the original contracts (pre and/or post) actually belong on
     this wrapper -- whether under the ordinary -fcontracts-client-check
     policy or a per-contract force_client_side_check/
     force_definition_side_check override -- is decided per-contract by
     copy_and_remap_contracts's for_wrapper filter below, not here.
     For wrappers on CDTORs we need to refer to the original contracts,
     when the wrapper is around a clone.  */
  set_fn_contract_specifiers ( wrapdecl,
		      copy_and_remap_contracts (wrapdecl, DECL_ORIGIN (fndecl),
						cmk_all, /*for_wrapper=*/true));

  start_preparsed_function (wrapdecl, /*DECL_ATTRIBUTES*/NULL_TREE,
			    SF_DEFAULT | SF_PRE_PARSED);
  tree body = begin_function_body ();
  tree compound_stmt = begin_compound_stmt (BCS_FN_BODY);

  vec<tree, va_gc> * args = build_arg_list (wrapdecl);

  /* For a virtual FNDECL, the wrapper must still call through the
     vtable: the caller-side check above runs against FNDECL, the
     statically-chosen function, but the call itself needs to reach
     whatever the final overrider actually is, exactly like any
     ordinary virtual call would.  A plain call to FNDECL here would
     silently always run FNDECL's own body, never dispatching to a more
     derived override -- breaking polymorphism for every wrapped call.
     Build the same base-adjusted vtable load build_over_call uses
     (call.cc), rather than a direct call, when that matters.  */
  tree call_target = fndecl;
  if (DECL_IOBJ_MEMBER_FUNCTION_P (fndecl) && DECL_VIRTUAL_P (fndecl))
    {
      gcc_checking_assert (TREE_CODE (DECL_VINDEX (fndecl)) == INTEGER_CST);
      tree this_parm = (*args)[0];
      tree binfo = lookup_base (TREE_TYPE (TREE_TYPE (this_parm)),
				 DECL_CONTEXT (fndecl), ba_any, NULL,
				 tf_warning_or_error);
      gcc_assert (binfo && binfo != error_mark_node);
      this_parm = build_base_path (PLUS_EXPR, this_parm, binfo, 1,
				    tf_warning_or_error);
      if (TREE_SIDE_EFFECTS (this_parm))
	this_parm = save_expr (this_parm);
      (*args)[0] = this_parm;
      call_target = build_vfn_ref (this_parm, DECL_VINDEX (fndecl));
      TREE_TYPE (call_target) = build_pointer_type (TREE_TYPE (fndecl));
    }

  tree call = build_thunk_like_call (call_target, args->length (),
				      args->address ());

  finish_return_stmt (call);

  finish_compound_stmt (compound_stmt);
  finish_function_body (body);
  expand_or_defer_fn (finish_function (/*inline_p=*/false));
  return true;
}

/* If any wrapper functions have been declared, emit their definition.
   This might be called multiple times, as we instantiate functions. When
   the processing here adds more wrappers, then flag to the caller that
   possible additional instantiations should be considered.
   Once instantiations are complete, this will be called with done == true.  */

bool
emit_contract_wrapper_func (bool done)
{
  if (!decl_wrapper_fn || decl_wrapper_fn->is_empty ())
    return false;
  size_t start_elements = decl_wrapper_fn->elements ();
  decl_wrapper_fn->traverse<void *, define_contract_wrapper_func>(NULL);
  bool more = decl_wrapper_fn->elements () > start_elements;
  if (done)
    decl_wrapper_fn->empty ();
  gcc_checking_assert (!done || !more);
  return more;
}

/* If TYPE (expected to be a complete class type) has exactly one
   operator(), and it isn't a template (a member function template, or
   a generic lambda's own call operator), return its FUNCTION_DECL;
   otherwise NULL_TREE.  This resolves the one call signature a
   declaration-level pre<>/post<> clause on a class-type callable
   object (e.g. std::function<void(int)>) is parsed and checked
   against -- see .claude/plans/stateless-jumping-shore.md.  Class
   types with an overloaded or generic operator() are deliberately not
   supported by this feature at all (rather than deferring resolution
   to each call site): this keeps the existing "resolve once, at the
   declaration" architecture (built for function pointers) fully
   reusable, unchanged, for class-type callables too.

   Pure function of TYPE alone, not of any particular declaration or
   call site -- deliberately, so it can be (and is) re-run at every
   call site to verify the call actually resolved through this same
   operator(), not some other candidate (see
   maybe_object_contract_check_call's caller in semantics.cc): a class
   type can also have a conversion operator to a function pointer/
   reference, which competes with operator() in real overload
   resolution at an actual call site (build_op_call, call.cc), so
   "exactly one operator() exists" alone does not guarantee every call
   actually goes through it.  Uses the same lookup_member machinery
   build_op_call itself uses (rather than the narrower, non-inherited
   get_class_binding_direct), so this always agrees with what a real
   call site's own overload resolution would consider.  */

tree
resolve_single_call_operator (tree type)
{
  if (!type || !CLASS_TYPE_P (type))
    return NULL_TREE;
  complete_type (type);
  if (!COMPLETE_TYPE_P (type))
    return NULL_TREE;

  tree fns = lookup_member (type, call_op_identifier,
			    /*protect=*/1, /*want_type=*/false, tf_none);
  if (!fns || fns == error_mark_node || !BASELINK_P (fns))
    return NULL_TREE;

  tree found = NULL_TREE;
  for (ovl_iterator it (BASELINK_FUNCTIONS (fns)); it; ++it)
    {
      tree fn = *it;
      /* A template operator() (a member function template, or a
	 generic lambda's own call operator) -- reject the whole thing,
	 even if some other, non-template overload also exists: any
	 overloading at all is out of scope for this feature.  */
      if (TREE_CODE (fn) == TEMPLATE_DECL)
	return NULL_TREE;
      /* More than one non-template candidate -- genuinely overloaded,
	 also out of scope.  */
      if (found)
	return NULL_TREE;
      found = fn;
    }
  return found;
}

/* Build a TREE_LIST of fresh PARM_DECLs with the same names and types
   as OPERATOR_FN's own real parameters (skipping any implicit object
   parameter), matching the shape the contract-specifier-seq grammar
   expects for its own PARAMS argument (a chain, via TREE_CHAIN, of
   TREE_LIST nodes each holding a PARM_DECL in TREE_VALUE -- the same
   shape a declarator's own u.function.parameters already has) -- see
   resolve_single_call_operator and
   .claude/plans/stateless-jumping-shore.md.  OPERATOR_FN is expected
   to already be resolved by resolve_single_call_operator (non-NULL,
   non-template, the one real operator()).

   Fresh copies, rather than OPERATOR_FN's own real PARM_DECLs,
   because the caller (the new grammar hooks in
   cp_parser_init_declarator/cp_parser_parameter_declaration) needs to
   push these by name into a temporary scope for the shortcut (no
   binder list) grammar's ordinary-lookup condition-parsing to find
   them -- exactly like an ordinary function declarator's own
   parameter scope already does for the function-pointer case (see
   cp_parser_direct_declarator's begin_scope (sk_function_parms, ...)
   around its own parameter-declaration-clause parsing).  OPERATOR_FN's
   real parameters are shared, permanent decls belonging to the class's
   member function; pushing those directly into an unrelated temporary
   scope would be a needless, surprising mutation of shared state.  */

tree
build_call_operator_contract_params (tree operator_fn)
{
  tree list = NULL_TREE;
  tree *tail = &list;
  for (tree p = FUNCTION_FIRST_USER_PARM (operator_fn); p; p = DECL_CHAIN (p))
    {
      tree fresh = cp_build_parm_decl (NULL_TREE, DECL_NAME (p), TREE_TYPE (p));
      DECL_SOURCE_LOCATION (fresh) = DECL_SOURCE_LOCATION (p);
      *tail = build_tree_list (NULL_TREE, fresh);
      tail = &TREE_CHAIN (*tail);
    }
  return list;
}

/* ------------------------------------------------------------------
   Declaration-level contracts on callable-typed objects: call-site
   enforcement (see .claude/plans/stateless-jumping-shore.md and
   maybe_attach_object_contract_specifiers, decl.cc).

   A pre<ctrl>/post<ctrl> clause attached directly to a callable-typed
   OBJECT declaration (a function pointer/reference variable or
   parameter -- never a FUNCTION_DECL) is checked at every call site
   referencing that declared name, using that call's own actual
   arguments, with no dependency on what the callable currently targets
   at runtime: the real call is never rerouted (contrast with
   maybe_contract_wrap_call's wrapper-reroute mechanism for an ordinary
   function's own contracts, deliberately not reused here beyond its
   underlying dispatch, which this still relies on transitively via the
   ordinary apply_preconditions/apply_postconditions pipeline any
   function body goes through in finish_function).

   For each contracted object decl, up to two small, internal "check
   functions" are built once (and cached): one running only the pre<>
   clauses (same parameter types as the callable, void return), one
   running only the post<> clauses (same parameter types, plus one
   extra trailing parameter for the actual computed result when the
   callable's return type isn't void; same return type as the
   callable, with a body that is simply 'return' of that trailing
   parameter, so the ordinary postcondition-checking machinery -- which
   expects to check a real return statement -- runs unmodified, with no
   manual build_contract_check call needed here at all).  Both are
   built and defined immediately, not deferred the way contract
   wrappers are: this feature does not yet support templates, so there
   is no dependent-type case requiring the declaration/definition split
   the wrapper mechanism needs.

   Each call site rewrites 'fp(args...)' into a single expression that
   evaluates each argument (and the real call's own result) exactly
   once, shared between the checks and the real call: pre<>, then the
   real call, then post<>, yielding the real call's result.  ------
   ------------------------------------------------------------------ */

/* A copy_tree_body_r-style condition remap that needs no FUNCTION_DECL
   on the source side at all (unlike remap_contract, which walks
   DECL_ARGUMENTS on both sides in lockstep): DECL_MAP already holds
   the exact decl -> decl substitutions to apply.  A CONTRACT_CONDITION
   is always a plain expression (parsed via
   cp_parser_conditional_expression), never containing statements,
   blocks, or labels, so this needs none of copy_tree_body_r's
   FUNCTION_DECL-inlining machinery -- just substitute-or-copy.

   Two wrapper shapes need special handling rather than a blind copy,
   both only ever seen around one of the *parameter* positions this
   walker actually substitutes (DECL_MAP never maps the postcondition
   result placeholder at all -- see remap_object_contract's own
   comment for why that one is handled separately, entirely by
   rebuild_postconditions):

   - A D4324 constification const-wrapper (view_as_const/
     constify_contract_access, a VIEW_CONVERT_EXPR marked
     CONST_WRAPPER_P), added around a parameter reference when the
     control object opts into constify() but the declarator's own
     parameter isn't already const-qualified.  Rebuilding it via
     view_as_const on the already-substituted decl recomputes its
     const-qualified type from that decl's own (already correct) type,
     rather than blindly copying forward whatever the wrapper's type
     happened to be before substitution.

   - An ordinary location wrapper (maybe_wrap_with_location), which
     "normally has the same type as its operand, but it can have a
     different one if the type of the operand has changed" (tree.h's
     own location_wrapper_p comment) -- in practice not something the
     substituted parameter positions here trip (their types don't
     change across the substitution), but handled the same way for
     the same reason, and cheap to keep correct regardless.  */

static tree
object_contract_remap_r (tree *tp, int *walk_subtrees, void *data)
{
  hash_map<tree, tree> *decl_map = (hash_map<tree, tree> *) data;

  if (contract_const_wrapper_p (*tp))
    if (tree *n = decl_map->get (TREE_OPERAND (*tp, 0)))
      {
	*tp = view_as_const (*n);
	*walk_subtrees = 0;
	return NULL_TREE;
      }

  /* An ordinary location wrapper -- see the function comment above.  */
  if (location_wrapper_p (*tp))
    if (tree *n = decl_map->get (TREE_OPERAND (*tp, 0)))
      {
	*tp = maybe_wrap_with_location (*n, EXPR_LOCATION (*tp));
	*walk_subtrees = 0;
	return NULL_TREE;
      }

  if (tree *n = decl_map->get (*tp))
    {
      *tp = *n;
      *walk_subtrees = 0;
      return NULL_TREE;
    }

  /* Anything else that's a decl or a type is shared, not copied -- it
     isn't part of the condition's own private expression structure
     (a global, the control object, ...).  */
  if (DECL_P (*tp) || TYPE_P (*tp))
    {
      *walk_subtrees = 0;
      return NULL_TREE;
    }

  copy_tree_r (tp, walk_subtrees, NULL);
  return NULL_TREE;
}

/* Return a copy of CONTRACT (a PRECONDITION_STMT/POSTCONDITION_STMT
   attribute-list node) whose condition has been remapped from its own
   stored positional parameter decls (get_contract_positional_parms)
   onto DST_PARMS, a DECL_CHAIN of DST_FN's own fresh parameters (of
   the same length as the stored list).

   A postcondition's own result identifier (built by
   make_postcondition_variable at parse time, always auto-typed there,
   per the single existing binder mechanism this feature's binder list
   generalizes -- see cp_parser_function_contract_specifier) is
   deliberately left untouched here: any expression built using it at
   parse time (e.g. 'r == a + b') has a dependent_operator_type of its
   own, computed then from operand types that included that auto
   placeholder -- fixing only the leaf reference (whether by a plain
   substitution or by retyping a copy of it in place) leaves every
   *enclosing* expression node's own stale, still-dependent type
   unrepaired, which later chokes fold_convert in exactly the way this
   function's caller (build_object_contract_check_function) exists to
   avoid.  There is no shallow fix for that: it needs the same full
   tsubst_expr-based re-evaluation grokfndecl runs (rebuild_
   postconditions) for an ordinary function's own postcondition right
   after attaching its contract_specifiers -- which
   build_object_contract_check_function duly calls, once, right after
   this function's own parameter remapping has already replaced every
   *other* decl reference the condition might have; by that point
   DST_FN's own DECL_ARGUMENTS already match what the condition
   references, so rebuild_postconditions's local_specialization_stack
   set-up (register_local_identity over DECL_ARGUMENTS (DST_FN))
   resolves them correctly instead of falling through to its own
   "unrecognized decl" path.  */

static tree
remap_object_contract (tree contract, tree dst_parms)
{
  /* Look this up against the *original* (pre-copy) statement node:
     that's the exact tree set_contract_positional_parms was keyed on
     back in cp_parser_function_contract_specifier, before
     finish_contract_specifier ever wrapped it into the attribute-list
     shape CONTRACT is received as here.  */
  tree orig_stmt = CONTRACT_STATEMENT (contract);
  tree orig_positional_parms = get_contract_positional_parms (orig_stmt);

  tree copy = copy_node (contract);
  TREE_VALUE (copy) = build_tree_list (TREE_PURPOSE (TREE_VALUE (contract)),
					copy_node (orig_stmt));
  tree stmt = CONTRACT_STATEMENT (copy);

  hash_map<tree, tree> decl_map;
  tree dp = dst_parms;
  for (tree sp = orig_positional_parms; sp;
       sp = TREE_CHAIN (sp), dp = DECL_CHAIN (dp))
    {
      gcc_checking_assert (dp);
      decl_map.put (TREE_VALUE (sp), dp);
    }

  walk_tree (&CONTRACT_CONDITION (stmt), object_contract_remap_r, &decl_map,
	     NULL);
  return copy;
}

/* Map from a callable-typed object decl to its (cached) pre-check or
   post-check function -- see build_object_contract_check_function.  */

static GTY(()) hash_map<tree, tree> *object_pre_check_fn_map;
static GTY(()) hash_map<tree, tree> *object_post_check_fn_map;

/* Build OBJDECL's pre-check (IS_POST false) or post-check (IS_POST
   true) function: a fresh, internal FUNCTION_DECL taking OBJDECL's own
   callable parameter types (plus, for a post-check with a non-void
   return, one extra trailing parameter for the real call's actual
   result), carrying only OBJDECL's contracts of the matching kind
   (remapped onto its own fresh parameters), defined immediately with a
   trivial body ('return <the trailing result parameter, if any>;') so
   the ordinary contract-application pipeline
   (maybe_apply_function_contracts, driven automatically by
   finish_function) builds the actual check.  Returns NULL_TREE if
   OBJDECL has no contract of the requested kind.  */

static tree
build_object_contract_check_function (tree objdecl, bool is_post)
{
  /* A class-type callable's own operator() has a METHOD_TYPE (an
     implicit leading 'this' entry in TYPE_ARG_TYPES), unlike a
     function pointer's own pointee FUNCTION_TYPE -- resolve it the
     same way the declaration-time attach point did
     (resolve_single_call_operator), and skip that implicit entry via
     FUNCTION_FIRST_USER_PARMTYPE (matching contract_control_operator's
     own use of the same idiom) -- see
     .claude/plans/stateless-jumping-shore.md.  */
  tree fn_type;
  tree first_arg_type;
  if (CLASS_TYPE_P (TREE_TYPE (objdecl)))
    {
      tree operator_fn = resolve_single_call_operator (TREE_TYPE (objdecl));
      fn_type = TREE_TYPE (operator_fn);
      first_arg_type = FUNCTION_FIRST_USER_PARMTYPE (operator_fn);
    }
  else
    {
      fn_type = TREE_TYPE (TREE_TYPE (objdecl));
      first_arg_type = TYPE_ARG_TYPES (fn_type);
    }
  tree ret_type = TREE_TYPE (fn_type);
  bool has_result = is_post && !VOID_TYPE_P (ret_type);
  location_t loc = DECL_SOURCE_LOCATION (objdecl);

  /* No point building anything unless OBJDECL actually has a contract
     of this kind.  Each kept entry is its own copy_node, with its
     TREE_CHAIN explicitly reset: C's own chain slot still belongs to
     OBJDECL's full (pre-and-post) list, and reusing C or its chain
     pointer directly here would splice the *other* kind's contracts
     in right behind it.  */
  tree specs = NULL_TREE;
  tree *specs_tail = &specs;
  for (tree c = get_fn_contract_specifiers (objdecl); c; c = TREE_CHAIN (c))
    if ((TREE_CODE (CONTRACT_STATEMENT (c)) == POSTCONDITION_STMT) == is_post)
      {
	tree entry = copy_node (c);
	TREE_CHAIN (entry) = NULL_TREE;
	*specs_tail = entry;
	specs_tail = &TREE_CHAIN (entry);
      }
  if (!specs)
    return NULL_TREE;

  /* Fresh parameters matching OBJDECL's own callable signature,
     positionally, plus (for a post-check with a non-void return) one
     extra trailing parameter carrying the real call's actual result.  */
  tree parms = NULL_TREE;
  tree *parms_tail = &parms;
  tree arg_types = NULL_TREE;
  tree *at_tail = &arg_types;
  unsigned i = 0;
  for (tree t = first_arg_type; t && t != void_list_node;
       t = TREE_CHAIN (t), i++)
    {
      char namebuf[8];
      snprintf (namebuf, sizeof namebuf, "__a%u", i);
      tree ptype = TREE_VALUE (t);
      tree parm = cp_build_parm_decl (NULL_TREE, get_identifier (namebuf),
				       ptype);
      DECL_ARTIFICIAL (parm) = true;
      *parms_tail = parm;
      parms_tail = &DECL_CHAIN (parm);
      *at_tail = build_tree_list (NULL_TREE, ptype);
      at_tail = &TREE_CHAIN (*at_tail);
    }

  tree result_parm = NULL_TREE;
  if (has_result)
    {
      result_parm = cp_build_parm_decl (NULL_TREE, get_identifier ("__result"),
					 ret_type);
      DECL_ARTIFICIAL (result_parm) = true;
      *parms_tail = result_parm;
      parms_tail = &DECL_CHAIN (result_parm);
      *at_tail = build_tree_list (NULL_TREE, ret_type);
      at_tail = &TREE_CHAIN (*at_tail);
    }
  *at_tail = void_list_node;

  tree check_ret_type = is_post ? ret_type : void_type_node;
  tree check_fn_type = build_function_type (check_ret_type, arg_types);
  tree check_fn = build_lang_decl_loc (loc, FUNCTION_DECL, NULL_TREE,
				       check_fn_type);
  for (tree p = parms; p; p = DECL_CHAIN (p))
    DECL_CONTEXT (p) = check_fn;

  /* Reuse OBJDECL's own enclosing namespace/class as CHECK_FN's context
     when that's what it is -- but OBJDECL may instead be function-local
     (a PARM_DECL, whose DECL_CONTEXT is the enclosing FUNCTION_DECL, or
     a local VAR_DECL): CHECK_FN is never actually nested inside
     anything (it's synthesized once, the first time some call site
     needs it, regardless of how deeply that call site happens to be
     lexically nested -- see the file comment above), so a FUNCTION_DECL
     context here would wrongly mark it as a real nested function, which
     later stages (e.g. tree-nested.cc's unnest_nesting_tree, expecting
     the full nested-function apparatus: a static chain, and so on) are
     not prepared to cope with.  Fall back to the global namespace in
     that case.  */
  tree objdecl_context = DECL_CONTEXT (objdecl);
  if (objdecl_context && TREE_CODE (objdecl_context) == FUNCTION_DECL)
    objdecl_context = NULL_TREE;
  DECL_CONTEXT (check_fn) = objdecl_context;
  DECL_ARTIFICIAL (check_fn) = true;
  DECL_SOURCE_LOCATION (check_fn) = loc;
  DECL_ARGUMENTS (check_fn) = parms;
  DECL_RESULT (check_fn) = NULL_TREE;
  DECL_INITIAL (check_fn) = NULL_TREE;
  TREE_PUBLIC (check_fn) = false;
  DECL_EXTERNAL (check_fn) = false;
  DECL_WEAK (check_fn) = false;
  DECL_INTERFACE_KNOWN (check_fn) = true;

  /* So a violation's reported source_location can name OBJDECL itself
     (e.g. "divide") instead of CHECK_FN's own compiler-synthesized
     name -- see get_orig_objdecl_for_check_fn and its use in
     get_src_loc_impl_ptr_for.  */
  hash_map_maybe_create<hm_ggc> (object_check_fn_orig_decl);
  gcc_checking_assert (!object_check_fn_orig_decl->get (check_fn));
  object_check_fn_orig_decl->put (check_fn, objdecl);

  {
    static unsigned counter;
    char namebuf[32];
    snprintf (namebuf, sizeof namebuf, "__contract_%s_check_%u",
	      is_post ? "post" : "pre", counter++);
    DECL_NAME (check_fn) = get_identifier (namebuf);
  }

  tree remapped_specs = NULL_TREE;
  tree *remapped_tail = &remapped_specs;
  for (tree c = specs; c; c = TREE_CHAIN (c))
    {
      *remapped_tail = remap_object_contract (c, parms);
      remapped_tail = &TREE_CHAIN (*remapped_tail);
    }
  set_fn_contract_specifiers (check_fn, remapped_specs);
  /* Resolve the postcondition result placeholder (still auto-typed,
     per make_postcondition_variable) to CHECK_FN's own real return
     type, and re-evaluate every expression built using it so no
     enclosing node is left with a stale dependent_operator_type --
     exactly the same step grokfndecl runs for any ordinary function
     right after attaching its own contract_specifiers, and just as
     necessary here (see remap_object_contract's own comment for why
     the parameter remapping just above can't also cover this).  A
     no-op for the pre-check function, which never has a
     postcondition at all.  */
  rebuild_postconditions (check_fn);

  /* This whole synthesis happens mid-expression, wherever the call site
     that first needs CHECK_FN happens to be lexically (possibly itself
     nested arbitrarily deep in some other function's own body, class
     scope, etc. still being parsed) -- push_to_top_level/
     pop_from_top_level is the same general-purpose primitive
     build_predicate_core_function_1 uses for exactly this reason (see
     its own, more detailed comment): it isolates CHECK_FN's own
     start_preparsed_function/finish_function from every bit of that
     ambient, still-in-progress state (current_function_decl,
     current_class_type, the statement-list stack, ...), and restores
     it correctly afterward so parsing of whatever expression is
     actually in progress can continue.  */
  push_to_top_level ();
  start_preparsed_function (check_fn, /*attributes*/NULL_TREE,
			   SF_DEFAULT | SF_PRE_PARSED);
  tree body = begin_function_body ();
  tree compound_stmt = begin_compound_stmt (BCS_FN_BODY);
  finish_return_stmt (has_result ? result_parm : NULL_TREE);
  finish_compound_stmt (compound_stmt);
  finish_function_body (body);
  expand_or_defer_fn (finish_function (/*inline_p=*/false));
  pop_from_top_level ();

  return check_fn;
}

/* Return OBJDECL's cached pre-check (IS_POST false) or post-check
   (IS_POST true) function, building it first if this is the first
   request; NULL_TREE if OBJDECL has no contract of that kind.  */

static tree
get_or_build_object_contract_check_function (tree objdecl, bool is_post)
{
  hash_map<tree, tree> *&map
    = is_post ? object_post_check_fn_map : object_pre_check_fn_map;
  if (tree *p = hash_map_safe_get (map, objdecl))
    return *p;
  tree checkfn = build_object_contract_check_function (objdecl, is_post);
  hash_map_maybe_create<hm_ggc> (map);
  map->put (objdecl, checkfn);
  return checkfn;
}

/* If FUNCTION (a call's callee, before any decay to a pointer value --
   see cp_build_function_call_vec) is a reference to a decl carrying a
   declaration-level pre<>/post<> clause, return an expression that
   also runs those checks, using CALL's own actual arguments (ARGS,
   already fully resolved/converted), alongside the untouched real call
   CALL -- see the file comment above build_object_contract_check_
   function.  Otherwise, return CALL unchanged.

   ARG_OFFSET is how many leading arguments CALL's own CALL_EXPR has
   beyond ARGS's own count: 0 for a plain function call (the
   function-pointer/reference case), where CALL_EXPR_ARG lines up with
   ARGS directly; 1 for a class-type callable's ordinary (non-static,
   non-explicit-object) operator(), whose CALL_EXPR carries an
   implicit leading 'this' argument that ARGS -- the source-level
   argument list -- never includes at all (see finish_call_expr's own
   caller, semantics.cc, which derives this from the same
   DECL_IOBJ_MEMBER_FUNCTION_P check build_object_contract_check_
   function itself uses).  Only affects where the possibly-save_expr'd
   argument gets written back into CALL for shared evaluation --
   SAVED_ARGS, and the check functions built from them, are unaffected,
   since neither the pre-check nor the post-check has an implicit
   object parameter of its own.  */

tree
maybe_object_contract_check_call (tree function, tree call,
				  vec<tree, va_gc> *args, unsigned arg_offset)
{
  if (!flag_contracts || call == error_mark_node
      || !function || !DECL_P (function)
      || !get_fn_contract_specifiers (function))
    return call;

  tree pre_check = get_or_build_object_contract_check_function (function,
								 false);
  tree post_check = get_or_build_object_contract_check_function (function,
								  true);
  if (!pre_check && !post_check)
    return call;

  /* Evaluate each argument exactly once, reusing the same value for
     the checks and the real call -- also updating CALL's own args in
     place, so its evaluation of them is the shared one too.  */
  unsigned nargs = vec_safe_length (args);
  auto_vec<tree, 8> saved_args (nargs);
  for (unsigned i = 0; i < nargs; i++)
    {
      tree arg = (*args)[i];
      if (TREE_SIDE_EFFECTS (arg))
	arg = save_expr (arg);
      saved_args.quick_push (arg);
      if (i + arg_offset < (unsigned) call_expr_nargs (call))
	CALL_EXPR_ARG (call, i + arg_offset) = arg;
    }

  tree pre_call = NULL_TREE;
  if (pre_check)
    pre_call = build_call_a (pre_check, nargs, saved_args.address ());

  tree ret_type = TREE_TYPE (call);
  bool void_result = VOID_TYPE_P (ret_type);
  /* Force the real call to happen exactly once, right here -- between
     the pre<> and post<> checks -- and, if it has a result, let a
     later reference to it (post<>'s own extra argument, and this
     expression's own overall value) reuse that one evaluation.  */
  tree result_expr = (!void_result && post_check) ? save_expr (call) : call;

  tree post_call = NULL_TREE;
  if (post_check)
    {
      auto_vec<tree, 9> post_args (nargs + 1);
      for (unsigned i = 0; i < nargs; i++)
	post_args.quick_push (saved_args[i]);
      if (!void_result)
	post_args.quick_push (result_expr);
      post_call = build_call_a (post_check, post_args.length (),
				post_args.address ());
    }

  tree seq = result_expr;
  if (post_call)
    {
      seq = build2 (COMPOUND_EXPR, TREE_TYPE (post_call), seq, post_call);
      if (!void_result)
	seq = build2 (COMPOUND_EXPR, ret_type, seq, result_expr);
    }
  if (pre_call)
    seq = build2 (COMPOUND_EXPR, TREE_TYPE (seq), pre_call, seq);

  return seq;
}

/* Mark most of a contract as being invalid.  */

tree
invalidate_contract (tree contract)
{
  if (TREE_CODE (contract) == POSTCONDITION_STMT
      && POSTCONDITION_IDENTIFIER (contract))
    POSTCONDITION_IDENTIFIER (contract) = error_mark_node;
  CONTRACT_CONDITION (contract) = error_mark_node;
  CONTRACT_COMMENT (contract) = error_mark_node;
  return contract;
}

/* Returns an invented parameter declaration of the form 'TYPE ID' for the
   purpose of parsing the postcondition.

   We use a PARM_DECL instead of a VAR_DECL so that tsubst forces a lookup
   in local specializations when we instantiate these things later.  */

tree
make_postcondition_variable (cp_expr id, tree type)
{
  if (id == error_mark_node)
    return id;
  gcc_checking_assert (scope_chain && scope_chain->bindings
		       && scope_chain->bindings->kind == sk_contract);

  tree decl = build_lang_decl (PARM_DECL, id, type);
  DECL_ARTIFICIAL (decl) = true;
  DECL_SOURCE_LOCATION (decl) = id.get_location ();
  return pushdecl (decl);
}

/* As above, except that the type is unknown.  */

tree
make_postcondition_variable (cp_expr id)
{
  return make_postcondition_variable (id, make_auto ());
}

/* Check that the TYPE is valid for a named postcondition variable on
   function decl FNDECL. Emit a diagnostic if it is not.  Returns TRUE if
   the result is OK and false otherwise.  */

bool
check_postcondition_result (tree fndecl, tree type, location_t loc)
{
  /* Do not be confused by targetm.cxx.cdtor_return_this ();
     conceptually, cdtors have no return value.  */
  if (VOID_TYPE_P (type)
      || DECL_CONSTRUCTOR_P (fndecl)
      || DECL_DESTRUCTOR_P (fndecl))
    {
      error_at (loc,
		DECL_CONSTRUCTOR_P (fndecl)
		? G_("constructor does not return a value to test")
		: DECL_DESTRUCTOR_P (fndecl)
		? G_("destructor does not return a value to test")
		: G_("function does not return a value to test"));
      return false;
    }

  return true;
}

/* Instantiate each postcondition with the return type to finalize the
   contract specifiers on a function decl FNDECL.  */

void
rebuild_postconditions (tree fndecl)
{
  if (!fndecl || fndecl == error_mark_node)
    return;

  tree type = TREE_TYPE (TREE_TYPE (fndecl));

  /* If the return type is undeduced, defer until later.  */
  if (TREE_CODE (type) == TEMPLATE_TYPE_PARM)
    return;

  tree contract_spec = get_fn_contract_specifiers (fndecl);
  for (; contract_spec ; contract_spec = TREE_CHAIN (contract_spec))
    {
      tree contract = TREE_VALUE (TREE_VALUE (contract_spec));
      if (TREE_CODE (contract) != POSTCONDITION_STMT)
	continue;
      tree condition = CONTRACT_CONDITION (contract);
      if (!condition || condition == error_mark_node)
	continue;

      /* If any conditions are deferred, they're all deferred.  Note that
	 we don't have to instantiate postconditions in that case because
	 the type is available through the declaration.  */
      if (TREE_CODE (condition) == DEFERRED_PARSE)
	return;

      tree oldvar = POSTCONDITION_IDENTIFIER (contract);
      if (!oldvar)
	continue;

      gcc_checking_assert (!DECL_CONTEXT (oldvar)
			   || DECL_CONTEXT (oldvar) == fndecl);
      DECL_CONTEXT (oldvar) = fndecl;

      /* Check the postcondition variable.  */
      location_t loc = DECL_SOURCE_LOCATION (oldvar);
      if (!check_postcondition_result (fndecl, type, loc))
	{
	  invalidate_contract (contract);
	  continue;
	}

      /* "Instantiate" the result variable using the known type.  */
      tree newvar = copy_node (oldvar);
      TREE_TYPE (newvar) = type;

      /* Make parameters and result available for substitution.  */
      local_specialization_stack stack (lss_copy);
      for (tree t = DECL_ARGUMENTS (fndecl); t != NULL_TREE; t = TREE_CHAIN (t))
	register_local_identity (t);
      register_local_specialization (newvar, oldvar);

      begin_scope (sk_contract, fndecl);
      bool old_pc = processing_postcondition;
      processing_postcondition = true;

      /* Re-establish the constification state for this walk.  tsubst_expr
	 re-visits the VIEW_CONVERT_EXPR const-wrappers in the condition and
	 calls constify_contract_access on the remapped result variable; that
	 helper is gated on contract_condition_constify_p (D4324), which is the
	 parser-time global and is not otherwise set here.  Leaving it false
	 lets the result variable come back non-const, which sends tsubst down
	 the "not const, presumably still dependent" path where it substitutes
	 the wrapper's placeholder type with an empty argument vector and ICEs
	 (e.g. auto return with post(r: check(r))).  Matching the parser's
	 setting keeps the result const, exactly as P2900 requires.  */
      bool constify_p = flag_contract_control_objects
	? contract_control_constifies (CONTRACT_CONTROL_OBJECT (contract),
					contract_side_of (contract, fndecl))
	: true;
      auto constify_ovr
	= make_temp_override (contract_condition_constify_p, constify_p);

      bool conveyor_p = flag_contract_control_objects
	&& contract_control_conveyor_like (CONTRACT_CONTROL_OBJECT (contract),
					    contract_side_of (contract, fndecl));
      auto conveyor_ovr
	= make_temp_override (contract_condition_conveyor_p, conveyor_p);

      condition = tsubst_expr (condition, make_tree_vec (0),
			       tf_warning_or_error, fndecl);

      /* Update the contract condition and result.  */
      POSTCONDITION_IDENTIFIER (contract) = newvar;
      CONTRACT_CONDITION (contract) = finish_contract_condition (condition);
      processing_postcondition = old_pc;
      gcc_checking_assert (scope_chain && scope_chain->bindings
			   && scope_chain->bindings->kind == sk_contract);
      pop_bindings_and_leave_scope ();
    }
}

/* Make a string of the contract condition, if it is available.  */

static tree
build_comment (cp_expr condition)
{
  /* Try to get the actual source text for the condition; if that fails pretty
     print the resulting tree.  */
  char *str = get_source_text_between (global_dc->get_file_cache (),
				       condition.get_start (),
				       condition.get_finish ());
  if (!str)
    {
      const char *str = expr_to_string (condition);
      return build_string_literal (strlen (str) + 1, str);
    }

  tree t = build_string_literal (strlen (str) + 1, str);
  free (str);
  return t;
}

/* Build a contract statement.  */

tree
grok_contract (tree contract_spec, tree mode, tree result, cp_expr condition,
	       location_t loc, tree control_object /* = NULL_TREE */)
{
  if (condition == error_mark_node)
    return error_mark_node;

  if (control_object == error_mark_node)
    return error_mark_node;

  tree_code code;
  contract_assertion_kind kind = CAK_INVALID;
  if (id_equal (contract_spec, "contract_assert"))
    {
      code = ASSERTION_STMT;
      kind = CAK_ASSERT;
      /* D4324: mark the enclosing function as possibly needing the
	 oa_* walk -- see DECL_MIGHT_NEED_OA_SCAN_P's own comment (cp-
	 tree.h) for the full list of touch points feeding this bit.
	 Set here, before the DEFERRED_PARSE early-return below, since
	 this same code path is reached identically on both a deferred
	 contract_assert's first pass and its later re-parse with the
	 real condition -- idempotent either way, accumulate-only bit.
	 A precondition/postcondition's own presence is instead picked
	 up cheaply via get_fn_contract_specifiers directly, with no
	 walk needed, so this only fires for contract_assert.  */
      if (current_function_decl)
	SET_DECL_MIGHT_NEED_OA_SCAN_P (current_function_decl);
    }
  else if (id_equal (contract_spec, "pre"))
    {
      code = PRECONDITION_STMT;
      kind = CAK_PRE;
    }
  else if (id_equal (contract_spec,"post"))
    {
      code = POSTCONDITION_STMT;
      kind = CAK_POST;
    }
  else
    gcc_unreachable ();

  /* Build the contract. The condition is added later.  In the case that
     the contract is deferred, result an plain identifier, not a result
     variable.  Operand 5 holds the optional control object; postconditions
     store the result name at operand 6.  */
  tree contract;
  if (code != POSTCONDITION_STMT)
    contract = build_nt (code, mode, NULL_TREE, NULL_TREE, NULL_TREE,
			 NULL_TREE, control_object);
  else
    contract = build_nt (code, mode, NULL_TREE, NULL_TREE, NULL_TREE,
			 NULL_TREE, control_object, result);
  TREE_TYPE (contract) = void_type_node;
  SET_EXPR_LOCATION (contract, loc);

  /* Determine the assertion kind.  */
  CONTRACT_ASSERTION_KIND (contract) = build_int_cst (uint16_type_node, kind);

  /* Determine the evaluation semantic.  This is now an override, so that if
     not set we will get the default (currently enforce).  */
  CONTRACT_EVALUATION_SEMANTIC (contract)
    = build_int_cst (uint16_type_node, (uint16_t)
		     flag_contract_evaluation_semantic);

  /* If the contract is deferred, don't do anything with the condition.  */
  if (TREE_CODE (condition) == DEFERRED_PARSE)
    {
      CONTRACT_CONDITION (contract) = condition;
      return contract;
    }

  /* Generate the comment from the original condition.  */
  CONTRACT_COMMENT (contract) = build_comment (condition);

  /* The condition is converted to bool.  */
  condition = finish_contract_condition (condition);

  if (condition == error_mark_node)
    return error_mark_node;

  CONTRACT_CONDITION (contract) = condition;

  return contract;
}

/* Build the contract specifier where IDENTIFIER is one of 'pre',
   'post' or 'assert' and CONTRACT is the underlying statement.  */

tree
finish_contract_specifier (tree identifier, tree contract)
{
  if (contract == error_mark_node)
    return error_mark_node;

  tree contract_spec = build_tree_list (build_tree_list (NULL_TREE, identifier),
					build_tree_list (NULL_TREE, contract));

  /* Mark the contract as dependent if the condition is dependent.  */
  tree condition = CONTRACT_CONDITION (contract);
  if (TREE_CODE (condition) != DEFERRED_PARSE
      && value_dependent_expression_p (condition))
    ATTR_IS_DEPENDENT (contract_spec) = true;

  return contract_spec;
}

/* Update condition of a late-parsed contract and postcondition variable,
   if any.  */

void
update_late_contract (tree contract, tree result, cp_expr condition)
{
  if (TREE_CODE (contract) == POSTCONDITION_STMT)
    POSTCONDITION_IDENTIFIER (contract) = result;

  /* Generate the comment from the original condition.  */
  CONTRACT_COMMENT (contract) = build_comment (condition);

  /* The condition is converted to bool.  */
  condition = finish_contract_condition (condition);
  CONTRACT_CONDITION (contract) = condition;
}

/* Returns the precondition function for FNDECL, or null if not set.  */

tree
get_precondition_function (tree fndecl)
{
  gcc_checking_assert (fndecl);
  tree *result = hash_map_safe_get (decl_pre_fn, fndecl);
  return result ? *result : NULL_TREE;
}

/* Returns the postcondition function for FNDECL, or null if not set.  */

tree
get_postcondition_function (tree fndecl)
{
  gcc_checking_assert (fndecl);
  tree *result = hash_map_safe_get (decl_post_fn, fndecl);
  return result ? *result : NULL_TREE;
}

/* Set the PRE and POST functions for FNDECL.  Note that PRE and POST can
   be null in this case.  If so the functions are not recorded.  Used by the
   modules code.  */

void
set_contract_functions (tree fndecl, tree pre, tree post)
{
  if (pre)
    set_precondition_function (fndecl, pre);

  if (post)
    set_postcondition_function (fndecl, post);
}


/* We're compiling the pre/postcondition function CONDFN; remap any FN
   contracts that match CODE and emit them.  */

static void
remap_and_emit_conditions (tree fn, tree condfn, tree_code code)
{
  gcc_assert (code == PRECONDITION_STMT || code == POSTCONDITION_STMT);
  tree contract_spec = get_fn_contract_specifiers (fn);
  for (; contract_spec; contract_spec = TREE_CHAIN (contract_spec))
    {
      tree contract = CONTRACT_STATEMENT (contract_spec);
      if (TREE_CODE (contract) == code
	  && contract_runs_on_side (contract, contract_side_of (contract, fn)))
	{
	  contract = copy_node (contract);
	  if (CONTRACT_CONDITION (contract) != error_mark_node)
	    remap_contract (fn, condfn, contract, /*duplicate_p=*/false);
	  emit_contract_statement (contract);
	}
    }
}

/* Finish up the pre & post function definitions for a guarded FNDECL,
   and compile those functions all the way to assembler language output.  */

void
finish_function_outlined_contracts (tree fndecl)
{
  /* If the guarded func is either already decided to be ill-formed or is
     not yet complete return early.  */
  if (error_operand_p (fndecl)
      || !DECL_INITIAL (fndecl)
      || DECL_INITIAL (fndecl) == error_mark_node)
    return;

  /* If there are no contracts here, or we're building them in-line then we
     do not need to build the outlined functions.  */
  if (!handle_contracts_p (fndecl)
      || !flag_contract_checks_outlined)
    return;

  /* If nothing on FNDECL is going to run on its own side (whichever side
     that is -- the wrapper's or the real definition's), do nothing.  */
  {
    contract_check_side side
      = DECL_CONTRACT_WRAPPER (fndecl) ? ccs_wrapper : ccs_definition;
    if (!has_active_preconditions (fndecl, side)
	&& !has_active_postconditions (fndecl, side))
      return;
  }

  /* If either the pre or post functions are bad, don't bother emitting
     any contracts.  The program is already ill-formed.  */
  tree pre = DECL_PRE_FN (fndecl);
  tree post = DECL_POST_FN (fndecl);
  if (pre == error_mark_node || post == error_mark_node)
    return;

  /* We are generating code, deferred parses should be complete.  */
  tree contract_spec = get_fn_contract_specifiers (fndecl);
  gcc_checking_assert (!contract_any_deferred_p (contract_spec));

  int flags = SF_DEFAULT | SF_PRE_PARSED;

  if (pre && !DECL_INITIAL (pre))
    {
      DECL_PENDING_INLINE_P (pre) = false;
      start_preparsed_function (pre, DECL_ATTRIBUTES (pre), flags);
      remap_and_emit_conditions (fndecl, pre, PRECONDITION_STMT);
      finish_return_stmt (NULL_TREE);
      pre = finish_function (false);
      expand_or_defer_fn (pre);
    }

  if (post && !DECL_INITIAL (post))
    {
      DECL_PENDING_INLINE_P (post) = false;
      start_preparsed_function (post, DECL_ATTRIBUTES (post), flags);
      remap_and_emit_conditions (fndecl, post, POSTCONDITION_STMT);
      gcc_checking_assert (VOID_TYPE_P (TREE_TYPE (TREE_TYPE (post))));
      finish_return_stmt (NULL_TREE);
      post = finish_function (false);
      expand_or_defer_fn (post);
    }
}

/* ===== Code generation ===== */

/* Insert a BUILT_IN_OBSERVABLE_CHECKPOINT epoch marker.  */

static void
emit_builtin_observable_checkpoint ()
{
  tree fn = builtin_decl_explicit (BUILT_IN_OBSERVABLE_CHKPT);
  releasing_vec vec;
  fn = finish_call_expr (fn, &vec, false, false, tf_warning_or_error);
  finish_expr_stmt (fn);
}

/* Shared code between TU-local wrappers for the violation handler.  */

static tree
declare_one_violation_handler_wrapper (tree fn_name, tree fn_type,
				       tree p1_type, tree p2_type)
{
  location_t loc = BUILTINS_LOCATION;
  tree fn_decl = build_lang_decl_loc (loc, FUNCTION_DECL, fn_name, fn_type);
  DECL_CONTEXT (fn_decl) = FROB_CONTEXT (global_namespace);
  DECL_ARTIFICIAL (fn_decl) = true;
  DECL_INITIAL (fn_decl) = error_mark_node;
  /* Let the start function code fill in the result decl.  */
  DECL_RESULT (fn_decl) = NULL_TREE;
  /* Two args violation ref, dynamic info.  */
  tree parms = cp_build_parm_decl (fn_decl, NULL_TREE, p1_type);
  TREE_USED (parms) = true;
  DECL_READ_P (parms) = true;
  tree p2 = cp_build_parm_decl (fn_decl, NULL_TREE, p2_type);
  TREE_USED (p2) = true;
  DECL_READ_P (p2) = true;
  DECL_CHAIN (parms) = p2;
  DECL_ARGUMENTS (fn_decl) = parms;
  /* Make this function internal.  */
  TREE_PUBLIC (fn_decl) = false;
  DECL_EXTERNAL (fn_decl) = false;
  DECL_WEAK (fn_decl) = false;
  return fn_decl;
}

static GTY(()) tree tu_has_violation = NULL_TREE;
/* The P2900 exception-to-violation entry point.  Only declared/emitted in
   the default (non-D4324) mode; D4324 does not translate predicate
   exceptions into violations.  */
static GTY(()) tree tu_has_violation_exception = NULL_TREE;

static void
declare_violation_handler_wrappers ()
{
  if (tu_has_violation)
    return;

  iloc_sentinel ils (input_location);
  input_location = BUILTINS_LOCATION;
  tree v_obj_type = builtin_contract_violation_type;
  v_obj_type = cp_build_qualified_type (v_obj_type, TYPE_QUAL_CONST);
  v_obj_type = cp_build_reference_type (v_obj_type, /*rval*/false);
  tree fn_type = build_function_type_list (void_type_node, v_obj_type,
					   uint16_type_node, NULL_TREE);
  if (!flag_contract_control_objects)
    {
      tree fn_name_ex = get_identifier ("__tu_has_violation_exception");
      tu_has_violation_exception
	= declare_one_violation_handler_wrapper (fn_name_ex, fn_type, v_obj_type,
						 uint16_type_node);
    }
  tree fn_name = get_identifier ("__tu_has_violation");
  tu_has_violation
    = declare_one_violation_handler_wrapper (fn_name, fn_type, v_obj_type,
					     uint16_type_node);
}

static GTY(()) tree tu_terminate_wrapper = NULL_TREE;

/* Declare a noipa wrapper around the call to std::terminate */

static tree
declare_terminate_wrapper ()
{
  if (tu_terminate_wrapper)
    return tu_terminate_wrapper;

  iloc_sentinel ils (input_location);
  input_location = BUILTINS_LOCATION;

  tree fn_type = build_function_type_list (void_type_node, NULL_TREE);
  if (!TREE_NOTHROW (terminate_fn))
    fn_type = build_exception_variant (fn_type, noexcept_true_spec);
  tree fn_name = get_identifier ("__tu_terminate_wrapper");

  tu_terminate_wrapper
    = build_lang_decl_loc (input_location, FUNCTION_DECL, fn_name, fn_type);
  DECL_CONTEXT (tu_terminate_wrapper) = FROB_CONTEXT(global_namespace);
  DECL_ARTIFICIAL (tu_terminate_wrapper) = true;
  DECL_INITIAL (tu_terminate_wrapper) = error_mark_node;
  /* Let the start function code fill in the result decl.  */
  DECL_RESULT (tu_terminate_wrapper) = NULL_TREE;

  /* Make this function internal.  */
  TREE_PUBLIC (tu_terminate_wrapper) = false;
  DECL_EXTERNAL (tu_terminate_wrapper) = false;
  DECL_WEAK (tu_terminate_wrapper) = false;

  DECL_ATTRIBUTES (tu_terminate_wrapper)
    = tree_cons (get_identifier ("noipa"), NULL, NULL_TREE);
  cplus_decl_attributes (&tu_terminate_wrapper,
			 DECL_ATTRIBUTES (tu_terminate_wrapper), 0);
  return tu_terminate_wrapper;
}

/* Define a noipa wrapper around the call to std::terminate */

static void
build_terminate_wrapper ()
{
  /* We should not be trying to build this if we never used it.  */
  gcc_checking_assert (tu_terminate_wrapper);

  start_preparsed_function (tu_terminate_wrapper,
			    DECL_ATTRIBUTES(tu_terminate_wrapper),
			    SF_DEFAULT | SF_PRE_PARSED);
  tree body = begin_function_body ();
  tree compound_stmt = begin_compound_stmt (BCS_FN_BODY);
  finish_expr_stmt (build_call_a (terminate_fn, 0, nullptr));
  finish_return_stmt (NULL_TREE);
  finish_compound_stmt (compound_stmt);
  finish_function_body (body);
  tu_terminate_wrapper = finish_function (false);
  expand_or_defer_fn (tu_terminate_wrapper);
}

/* Lookup a name in std::contracts, or inject it.  */

static tree
lookup_std_contracts_type (tree name_id)
{
  tree id_ns = get_identifier ("contracts");
  tree ns = lookup_qualified_name (std_node, id_ns);

  tree res_type = error_mark_node;
  if (TREE_CODE (ns) == NAMESPACE_DECL)
    res_type = lookup_qualified_name
      (ns, name_id, LOOK_want::TYPE | LOOK_want::HIDDEN_FRIEND);

  if (TREE_CODE (res_type) == TYPE_DECL)
    res_type = TREE_TYPE (res_type);
  else
    {
      push_nested_namespace (std_node);
      push_namespace (id_ns, /*inline*/false);
      res_type = make_class_type (RECORD_TYPE);
      create_implicit_typedef (name_id, res_type);
      DECL_SOURCE_LOCATION (TYPE_NAME (res_type)) = BUILTINS_LOCATION;
      DECL_CONTEXT (TYPE_NAME (res_type)) = current_namespace;
      pushdecl_namespace_level (TYPE_NAME (res_type), /*hidden*/true);
      pop_namespace ();
      pop_nested_namespace (std_node);
    }
  return res_type;
}

/* Look up VAR_DECL std::contracts::default_v: the implicit control object
   used for a bare pre/post/contract_assert (no named control) when
   -fcontract-control-objects is enabled, exactly as if
   '<std::contracts::default_v>' had been written.  Unlike
   lookup_std_contracts_type, this never injects a stand-in: default_v's
   behavior comes entirely from the library, so a missing declaration is a
   hard error telling the user to include <contracts>.  */

tree
contract_default_control_object (location_t loc)
{
  tree id_ns = get_identifier ("contracts");
  tree ns = lookup_qualified_name (std_node, id_ns);

  tree res = NULL_TREE;
  if (TREE_CODE (ns) == NAMESPACE_DECL)
    res = lookup_qualified_name (ns, get_identifier ("default_v"));

  if (!res || res == error_mark_node || TREE_CODE (res) != VAR_DECL)
    {
      error_at (loc, "%<std::contracts::default_v%> has not been declared; "
		"include %<<contracts>%> before using %qs without a named "
		"control object", "-fcontract-control-objects");
      return error_mark_node;
    }
  return res;
}

/* Return handle_contract_violation (), declaring it if needed.  */

static tree
declare_handle_contract_violation ()
{
  /* We may need to declare new types, ensure they are not considered
     attached to a named module.  */
  auto module_kind_override = make_temp_override
    (module_kind, module_kind & ~(MK_PURVIEW | MK_ATTACH | MK_EXPORTING));
  tree fnname = get_identifier ("handle_contract_violation");
  tree viol_name = get_identifier ("contract_violation");
  tree l = lookup_qualified_name (global_namespace, fnname,
				  LOOK_want::HIDDEN_FRIEND);
  for (tree f: lkp_range (l))
    if (TREE_CODE (f) == FUNCTION_DECL)
	{
	  tree parms = TYPE_ARG_TYPES (TREE_TYPE (f));
	  if (remaining_arguments (parms) != 1)
	    continue;
	  tree parmtype = non_reference (TREE_VALUE (parms));
	  if (CLASS_TYPE_P (parmtype)
	      && TYPE_IDENTIFIER (parmtype) == viol_name)
	    return f;
	}

  tree violation = lookup_std_contracts_type (viol_name);
  tree fntype = NULL_TREE;
  tree v_obj_ref = cp_build_qualified_type (violation, TYPE_QUAL_CONST);
  v_obj_ref = cp_build_reference_type (v_obj_ref, /*rval*/false);
  fntype = build_function_type_list (void_type_node, v_obj_ref, NULL_TREE);

  push_nested_namespace (global_namespace);
  tree fndecl
    = build_cp_library_fn_ptr ("handle_contract_violation", fntype, ECF_COLD);
  pushdecl_namespace_level (fndecl, /*hiding*/true);
  pop_nested_namespace (global_namespace);

  /* Build the parameter(s).  */
  tree parms = cp_build_parm_decl (fndecl, NULL_TREE, v_obj_ref);
  TREE_USED (parms) = true;
  DECL_READ_P (parms) = true;
  DECL_ARGUMENTS (fndecl) = parms;
  return fndecl;
}

/* Build the call to handle_contract_violation for VIOLATION.  */

static void
build_contract_handler_call (tree violation)
{
  tree violation_fn = declare_handle_contract_violation ();
  tree call = build_call_n (violation_fn, 1, violation);
  finish_expr_stmt (call);
}

/* -fcontract-symbolic-runtime-checks ("the gem", ~/gcc-axiom-
   contracts.md): the four runtime record-store entry points, declared in
   <contracts> and defined exactly once in libstdc++'s contract26.cc.
   Unlike handle_contract_violation, these are not meant to be user-
   overridable, so there's no global_namespace fallback-declaration path
   -- a plain qualified-name lookup into std::contracts, mirroring
   contract_default_control_object's lookup of default_v, is enough; a
   missing declaration is a hard error naming the required #include,
   the same way contract_default_control_object handles a missing
   default_v.  Each of the four is looked up (and cached) at most once
   per compilation.  */

static tree
lookup_symbolic_runtime_fn (location_t loc, tree *cache, const char *name)
{
  if (*cache)
    return *cache;

  tree id_ns = get_identifier ("contracts");
  tree ns = lookup_qualified_name (std_node, id_ns);

  tree res = NULL_TREE;
  if (TREE_CODE (ns) == NAMESPACE_DECL)
    res = lookup_qualified_name (ns, get_identifier (name));

  if (!res || res == error_mark_node || TREE_CODE (res) != FUNCTION_DECL)
    {
      error_at (loc, "%<std::contracts::%s%> has not been declared; "
		"include %<<contracts>%> before using %qs", name,
		"-fcontract-symbolic-runtime-checks");
      return error_mark_node;
    }
  *cache = res;
  return res;
}

static GTY(()) tree symbolic_establish_bool_decl;
static GTY(()) tree symbolic_check_bool_decl;
static GTY(()) tree symbolic_establish_range_decl;
static GTY(()) tree symbolic_check_range_decl;

static tree
get_symbolic_establish_bool_fn (location_t loc)
{
  return lookup_symbolic_runtime_fn (loc, &symbolic_establish_bool_decl,
				      "__contracts_symbolic_establish_bool");
}

static tree
get_symbolic_check_bool_fn (location_t loc)
{
  return lookup_symbolic_runtime_fn (loc, &symbolic_check_bool_decl,
				      "__contracts_symbolic_check_bool");
}

static tree
get_symbolic_establish_range_fn (location_t loc)
{
  return lookup_symbolic_runtime_fn (loc, &symbolic_establish_range_decl,
				      "__contracts_symbolic_establish_range");
}

static tree
get_symbolic_check_range_fn (location_t loc)
{
  return lookup_symbolic_runtime_fn (loc, &symbolic_check_range_decl,
				      "__contracts_symbolic_check_range");
}

/* Return the address of a stable, comdat-folded, per-declaration opaque
   "key" VAR_DECL named NAME -- -fcontract-symbolic-runtime-checks's
   runtime identity for "which predicate/comparison is this", used as
   the KEY argument to the four runtime functions above.  Mirrors
   get_guard/mangle_guard_variable (decl2.cc/mangle.cc): a 1-byte
   artificial global, cached via get_global_binding exactly like a
   guard variable, so it's synthesized at most once per name even
   though every TU that names the same symbolic function/field
   independently asks for it.  Unlike a guard variable (whose linkage
   mirrors its guarded variable's own, via copy_linkage), this is
   *always* comdat regardless of the target's own linkage: there is no
   real definition of the target anywhere to mirror (a symbolic
   function has none at all; a field isn't a linkage-bearing entity in
   the first place), so every TU that needs this key must be able to
   synthesize an identical, foldable one independently.  */

static tree
get_symbolic_key_decl (location_t loc, tree name)
{
  tree key = get_global_binding (name);
  if (key)
    return key;

  key = build_decl (loc, VAR_DECL, name, unsigned_char_type_node);
  SET_DECL_ASSEMBLER_NAME (key, name);
  DECL_ARTIFICIAL (key) = 1;
  DECL_IGNORED_P (key) = 1;
  TREE_STATIC (key) = 1;
  TREE_USED (key) = 1;
  TREE_PUBLIC (key) = 1;
  comdat_linkage (key);
  pushdecl_top_level_and_finish (key, NULL_TREE);
  return key;
}

/* Return the runtime identity key for symbolic predicate function FN
   (an is_opened(this)-shaped conjunct's callee) -- see
   get_symbolic_key_decl.  FN has no definition anywhere (that's the
   whole point of 'symbolic'), so its own DECL_ASSEMBLER_NAME is the
   only stable, cross-TU-identical handle to derive a name from.  */

static tree
get_symbolic_predicate_key (tree fn)
{
  const char *fn_name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (fn));
  char *name_str = concat ("_ZGSyP", fn_name, NULL);
  tree key = get_symbolic_key_decl (DECL_SOURCE_LOCATION (fn),
				     get_identifier (name_str));
  free (name_str);
  return build_fold_addr_expr (key);
}

/* Return the runtime identity key for symbolic comparison conjunct
   FIELD (a ptr->field OP const-shaped conjunct's field) -- see
   get_symbolic_key_decl.  FIELD_DECLs carry no DECL_ASSEMBLER_NAME
   (they aren't linkage-bearing entities), so the name is derived from
   its containing class's own ABI-mangled name (identical across every
   TU by ODR) plus the field's own identifier (unique within that
   class).  */

static tree
get_symbolic_field_key (tree field)
{
  tree type_name = mangle_typeinfo_for_type (DECL_CONTEXT (field));
  const char *field_name = IDENTIFIER_POINTER (DECL_NAME (field));
  char *name_str = concat ("_ZGSyF", IDENTIFIER_POINTER (type_name), "_",
			    field_name, NULL);
  tree key = get_symbolic_key_decl (DECL_SOURCE_LOCATION (field),
				     get_identifier (name_str));
  free (name_str);
  return build_fold_addr_expr (key);
}

/* Return true if FNDECL is std::contracts::__d4324_invoke_violation_handler
   (declared, never defined, in libstdc++-v3/include/std/contracts).  See
   maybe_replace_d4324_violation_handler_call below.  The name is
   distinctive enough that only a light namespace sanity check is done,
   matching the analogous recognizer for __d4324_consteval_diagnose_violation
   in gcc/cp/constexpr.cc.  */

static bool
is_d4324_invoke_violation_handler (tree fndecl)
{
  if (fndecl == NULL_TREE || TREE_CODE (fndecl) != FUNCTION_DECL)
    return false;
  tree name = DECL_NAME (fndecl);
  if (name == NULL_TREE
      || !id_equal (name, "__d4324_invoke_violation_handler"))
    return false;
  tree ctx = CP_DECL_CONTEXT (fndecl);
  return (ctx != NULL_TREE && TREE_CODE (ctx) == NAMESPACE_DECL
	  && DECL_NAME (ctx) != NULL_TREE
	  && id_equal (DECL_NAME (ctx), "contracts"));
}

/* CALL_EXPR is a call to a FUNCTION_DECL FN, found by the caller
   (cp_genericize_r, gcc/cp/cp-gimplify.cc) via cp_get_callee_fndecl_nofold.
   If FN is std::contracts::__d4324_invoke_violation_handler, build the
   same contract_violation object and the same call to the real,
   user-replaceable ::handle_contract_violation that the bare
   (control-object-less) contract path already builds
   (build_contract_violation_ctor/build_contract_handler_call above) --
   but from the five arguments the caller passed directly (kind, semantic,
   mode, comment, location) instead of deriving them from a CONTRACT tree.
   Unlike build_contract_violation_ctor's inputs, these are ordinary,
   possibly non-constant runtime expressions -- a control object's
   operator() calls this with values computed however it likes, not
   necessarily compile-time constants. Returns NULL_TREE if FN doesn't
   match, so the caller falls through unchanged.

   Deliberately does nothing else: no severity decision, no termination --
   that stays entirely the calling control object's own responsibility.  */

tree
maybe_replace_d4324_violation_handler_call (tree call_expr, tree fn)
{
  if (!is_d4324_invoke_violation_handler (fn))
    return NULL_TREE;

  location_t loc = EXPR_LOCATION (call_expr);
  tree kind_arg = CALL_EXPR_ARG (call_expr, 0);
  tree semantic_arg = CALL_EXPR_ARG (call_expr, 1);
  tree mode_arg = CALL_EXPR_ARG (call_expr, 2);
  tree comment_arg = CALL_EXPR_ARG (call_expr, 3);
  tree loc_arg = CALL_EXPR_ARG (call_expr, 4);

  /* Extract the real std::source_location argument's single pointer-typed
     field (named _M_impl in libstdc++) -- the same layout-compatible,
     position-based binding used throughout this file, here used in
     reverse (reading rather than building one) from how the
     constexpr-evaluation path's build_real_source_location_value builds
     a real std::source_location value.  */
  tree loc_type = non_reference (TREE_TYPE (loc_arg));
  tree loc_field = next_aggregate_field (TYPE_FIELDS (loc_type));
  tree src_loc_impl_ptr;
  if (loc_field
      && POINTER_TYPE_P (TREE_TYPE (loc_field))
      && !next_aggregate_field (DECL_CHAIN (loc_field)))
    src_loc_impl_ptr = build3 (COMPONENT_REF, TREE_TYPE (loc_field),
			       loc_arg, loc_field, NULL_TREE);
  else
    /* Not the expected single-pointer-member shape; fall back to a null
       location rather than misinterpreting the object's layout.  */
    src_loc_impl_ptr = build_zero_cst (ptr_type_node);

  /* Must match the field order in get_contract_violation_fields.  */
  tree f0 = next_aggregate_field (TYPE_FIELDS (builtin_contract_violation_type));
  tree f1 = next_aggregate_field (DECL_CHAIN (f0));
  tree f2 = next_aggregate_field (DECL_CHAIN (f1));
  tree f3 = next_aggregate_field (DECL_CHAIN (f2));
  tree f4 = next_aggregate_field (DECL_CHAIN (f3));
  tree f5 = next_aggregate_field (DECL_CHAIN (f4));
  tree f6 = next_aggregate_field (DECL_CHAIN (f5));
  tree ctor = build_constructor_va
    (builtin_contract_violation_type, 7,
     f0, build_int_cst (uint16_type_node, 1), /* _M_version.  */
     f1, fold_convert (TREE_TYPE (f1), kind_arg),
     f2, fold_convert (TREE_TYPE (f2), semantic_arg),
     f3, fold_convert (TREE_TYPE (f3), mode_arg),
     f4, comment_arg,
     f5, fold_convert (TREE_TYPE (f5), src_loc_impl_ptr),
     f6, build_zero_cst (nullptr_type_node)); /* __vendor_ext.  */
  TREE_READONLY (ctor) = true;

  /* Build the violation object on the stack; register it, the same way
     build_contract_control_call's runtime path builds its own
     temporaries, since these fields are runtime values, not compile-time
     constants the way build_contract_violation_constant's TU-local
     static-const path requires.  */
  tree viol_var = build_decl (loc, VAR_DECL, NULL_TREE,
			      builtin_contract_violation_type);
  DECL_ARTIFICIAL (viol_var) = true;
  DECL_IGNORED_P (viol_var) = true;
  DECL_CONTEXT (viol_var) = current_function_decl;
  layout_decl (viol_var, 0);
  DECL_INITIAL (viol_var) = ctor;

  tree bind = build3 (BIND_EXPR, void_type_node, NULL_TREE, NULL_TREE,
		       NULL_TREE);
  BIND_EXPR_VARS (bind) = viol_var;
  BIND_EXPR_BODY (bind) = push_stmt_list ();
  add_decl_expr (viol_var);
  build_contract_handler_call (build_fold_addr_expr (viol_var));
  BIND_EXPR_BODY (bind) = pop_stmt_list (BIND_EXPR_BODY (bind));

  return bind;
}

/* If we have emitted any contracts in this TU that will call a violation
   handler, then emit the wrappers for the handler.  */

void
maybe_emit_violation_handler_wrappers ()
{
  /* We might need the terminate wrapper, even if we do not use the violation
     handler wrappers.  */
  if (tu_terminate_wrapper && flag_contracts_conservative_ipa)
    build_terminate_wrapper ();

  if (!tu_has_violation)
    return;

  tree terminate_wrapper = terminate_fn;
  if (flag_contracts_conservative_ipa)
    terminate_wrapper = tu_terminate_wrapper;

  /* tu_has_violation */
  start_preparsed_function (tu_has_violation, NULL_TREE,
			    SF_DEFAULT | SF_PRE_PARSED);
  tree body = begin_function_body ();
  tree compound_stmt = begin_compound_stmt (BCS_FN_BODY);
  tree v = DECL_ARGUMENTS (tu_has_violation);
  tree semantic = DECL_CHAIN (v);

  /* We are going to call the handler.  */
  build_contract_handler_call (v);

  tree if_observe = begin_if_stmt ();
  /* if (observe) return; */
  tree cond = build2 (EQ_EXPR, uint16_type_node, semantic,
		      build_int_cst (uint16_type_node, (uint16_t)CES_OBSERVE));
  finish_if_stmt_cond (cond, if_observe);
  emit_builtin_observable_checkpoint ();
  finish_then_clause (if_observe);
  begin_else_clause (if_observe);
  /* else terminate.  */
  finish_expr_stmt (build_call_a (terminate_wrapper, 0, nullptr));
  finish_else_clause (if_observe);
  finish_if_stmt (if_observe);
  finish_return_stmt (NULL_TREE);

  finish_compound_stmt (compound_stmt);
  finish_function_body (body);
  tu_has_violation = finish_function (false);
  expand_or_defer_fn (tu_has_violation);

  /* tu_has_violation_exception (P2900 only; not declared under D4324).  */
  if (tu_has_violation_exception)
    {
      start_preparsed_function (tu_has_violation_exception, NULL_TREE,
				SF_DEFAULT | SF_PRE_PARSED);
      body = begin_function_body ();
      compound_stmt = begin_compound_stmt (BCS_FN_BODY);
      v = DECL_ARGUMENTS (tu_has_violation_exception);
      semantic = DECL_CHAIN (v);
      location_t loc = DECL_SOURCE_LOCATION (tu_has_violation_exception);

      tree a_type = strip_top_quals (non_reference (TREE_TYPE (v)));
      tree v2 = build_decl (loc, VAR_DECL, NULL_TREE, a_type);
      DECL_SOURCE_LOCATION (v2) = loc;
      DECL_CONTEXT (v2) = current_function_decl;
      DECL_ARTIFICIAL (v2) = true;
      layout_decl (v2, 0);
      v2 = pushdecl (v2);
      add_decl_expr (v2);
      tree r = cp_build_init_expr (v2, convert_from_reference (v));
      finish_expr_stmt (r);
      tree memb = lookup_member (a_type, get_identifier ("_M_detection_mode"),
				 /*protect=*/1, /*want_type=*/0,
				 tf_warning_or_error);
      r = build_class_member_access_expr (v2, memb, NULL_TREE, false,
					  tf_warning_or_error);
      r = cp_build_modify_expr
	(loc, r, NOP_EXPR,
	 build_int_cst (uint16_type_node, (uint16_t)CDM_EVAL_EXCEPTION),
	 tf_warning_or_error);
      finish_expr_stmt (r);
      /* We are going to call the handler.  */
      build_contract_handler_call (v);

      if_observe = begin_if_stmt ();
      /* if (observe) return; */
      cond = build2 (EQ_EXPR, uint16_type_node, semantic,
		     build_int_cst (uint16_type_node, (uint16_t)CES_OBSERVE));
      finish_if_stmt_cond (cond, if_observe);
      emit_builtin_observable_checkpoint ();
      finish_then_clause (if_observe);
      begin_else_clause (if_observe);
      /* else terminate.  */
      finish_expr_stmt (build_call_a (terminate_wrapper, 0, nullptr));
      finish_else_clause (if_observe);
      finish_if_stmt (if_observe);
      finish_return_stmt (NULL_TREE);
      finish_compound_stmt (compound_stmt);
      finish_function_body (body);
      tu_has_violation_exception = finish_function (false);
      expand_or_defer_fn (tu_has_violation_exception);
    }
}

/* Build a layout-compatible internal version of contract_violation type.  */

static tree
get_contract_violation_fields ()
{
  tree fields = NULL_TREE;
  /* Must match <contracts>:
  class contract_violation {
    uint16_t _M_version;
    assertion_kind _M_assertion_kind;
    evaluation_semantic _M_evaluation_semantic;
    detection_mode _M_detection_mode;
    const char* _M_comment;
    void *_M_src_loc_ptr;
    __vendor_ext* _M_ext;
  };
    If this changes, also update the initializer in
    build_contract_violation.  */
  const tree types[] = { uint16_type_node,
			 uint16_type_node,
			 uint16_type_node,
			 uint16_type_node,
			 const_string_type_node,
			 ptr_type_node,
			 ptr_type_node
			};
 const char *names[] = { "_M_version",
			 "_M_assertion_kind",
			 "_M_evaluation_semantic",
			 "_M_detection_mode",
			 "_M_comment",
			 "_M_src_loc_ptr",
			 "_M_ext",
			};
  unsigned n = 0;
  for (tree type : types)
    {
      /* finish_builtin_struct wants fields chained in reverse.  */
      tree next = build_decl (BUILTINS_LOCATION, FIELD_DECL,
				  get_identifier(names[n++]), type);
      DECL_CHAIN (next) = fields;
      fields = next;
    }
 return fields;
}

/* Build a type to represent contract violation objects.  */

static tree
init_builtin_contract_violation_type ()
{
  if (builtin_contract_violation_type)
    return builtin_contract_violation_type;

  tree fields = get_contract_violation_fields ();

  iloc_sentinel ils (input_location);
  input_location = BUILTINS_LOCATION;
  builtin_contract_violation_type = make_class_type (RECORD_TYPE);
  finish_builtin_struct (builtin_contract_violation_type,
			 "__builtin_contract_violation_type", fields, NULL_TREE);
  CLASSTYPE_AS_BASE (builtin_contract_violation_type)
    = builtin_contract_violation_type;
  DECL_CONTEXT (TYPE_NAME (builtin_contract_violation_type))
    = FROB_CONTEXT (global_namespace);
  CLASSTYPE_LITERAL_P (builtin_contract_violation_type) = true;
  CLASSTYPE_LAZY_COPY_CTOR (builtin_contract_violation_type) = true;
  xref_basetypes (builtin_contract_violation_type, /*bases=*/NULL_TREE);
  DECL_CONTEXT (TYPE_NAME (builtin_contract_violation_type))
    = FROB_CONTEXT (global_namespace);
  DECL_ARTIFICIAL (TYPE_NAME (builtin_contract_violation_type)) = true;
  TYPE_ARTIFICIAL (builtin_contract_violation_type) = true;
  builtin_contract_violation_type
    = cp_build_qualified_type (builtin_contract_violation_type,
			       TYPE_QUAL_CONST);
  return builtin_contract_violation_type;
}

/* Early initialisation of types and functions we will use.  */
void
init_contracts ()
{
  init_terminate_fn ();
  init_builtin_contract_violation_type ();

  /* Register the experimental GIMPLE-pass contract-checking engine
     (-fcontract-conveyor-proofs-gimple / -fcontract-symbolic-proofs-gimple)
     directly, the same way a plugin's own PLUGIN_PASS_MANAGER_SETUP
     callback would, rather than listing it in passes.def.  passes.def is
     shared by every language driver via libbackend.a, but
     make_pass_contracts_gimple only exists in the C++ front end's own
     object file, so referencing it from passes.def would leave cc1/lto1
     with an unresolved symbol at link time.  This call site runs safely
     before the pass list is ever iterated: general_init (toplev.cc)
     constructs the pass manager before lang_hooks.init (which reaches
     here via cxx_init_decl_processing) or initialize_plugins ever run.  */
  if (flag_contract_conveyor_proofs_gimple || flag_contract_symbolic_proofs_gimple)
    {
      struct register_pass_info pass_info;
      pass_info.pass = make_pass_contracts_gimple (g);
      pass_info.reference_pass_name = "ssa";
      pass_info.ref_pass_instance_number = 1;
      pass_info.pos_op = PASS_POS_INSERT_AFTER;
      register_pass (&pass_info);
    }
}

static GTY(()) tree contracts_source_location_impl_type;

/* Build a layout-compatible internal version of source location __impl
   type.  */

static tree
get_contracts_source_location_impl_type (tree context = NULL_TREE)
{
  if (contracts_source_location_impl_type)
     return contracts_source_location_impl_type;

  /* First see if we have a declaration that we can use.  */
  tree contracts_source_location_type
    = lookup_std_type (get_identifier ("source_location"));

  if (contracts_source_location_type
      && contracts_source_location_type != error_mark_node
      && TYPE_FIELDS (contracts_source_location_type))
    {
      contracts_source_location_impl_type = get_source_location_impl_type ();
      return contracts_source_location_impl_type;
    }

  /* We do not, so build the __impl layout equivalent type, which must
     match <source_location>:
     struct __impl
      {
	  const char* _M_file_name;
	  const char* _M_function_name;
	  unsigned _M_line;
	  unsigned _M_column;
      }; */
  const tree types[] = { const_string_type_node,
			const_string_type_node,
			uint_least32_type_node,
			uint_least32_type_node };

 const char *names[] = { "_M_file_name",
			 "_M_function_name",
			 "_M_line",
			 "_M_column",
			};
  tree fields = NULL_TREE;
  unsigned n = 0;
  for (tree type : types)
  {
    /* finish_builtin_struct wants fields chained in reverse.  */
    tree next = build_decl (BUILTINS_LOCATION, FIELD_DECL,
			    get_identifier (names[n++]), type);
    DECL_CHAIN (next) = fields;
    fields = next;
  }

  iloc_sentinel ils (input_location);
  input_location = BUILTINS_LOCATION;
  contracts_source_location_impl_type = cxx_make_type (RECORD_TYPE);
  finish_builtin_struct (contracts_source_location_impl_type,
			 "__impl", fields, NULL_TREE);
  DECL_CONTEXT (TYPE_NAME (contracts_source_location_impl_type)) = context;
  DECL_ARTIFICIAL (TYPE_NAME (contracts_source_location_impl_type)) = true;
  TYPE_ARTIFICIAL (contracts_source_location_impl_type) = true;
  contracts_source_location_impl_type
    = cp_build_qualified_type (contracts_source_location_impl_type,
			       TYPE_QUAL_CONST);

  return contracts_source_location_impl_type;
}

/* FNDECL is about to be embedded (by name) into a diagnostic-facing
   source_location -- a violation message's "in function ..." text, via
   either build_source_location_impl (cp-gimplify.cc, the bare/no-
   control-object path) or build_real_source_location_value (the D4324
   control-object path, both the runtime and constexpr variants).
   FNDECL may not be the function a user would recognize: resolve it to
   whatever *should* actually be named instead, the same way already
   done here for an outlined pre/post-condition function and a caller-
   side wrapper. Also resolves a declaration-level object contract's
   own synthesized check function (build_object_contract_check_function)
   to the contracted object's own declaration (a VAR_DECL/PARM_DECL,
   e.g. "divide", not a FUNCTION_DECL at all from that point on) --
   see .claude/plans/stateless-jumping-shore.md.  */

static tree
resolve_fndecl_for_diagnostic_name (tree fndecl)
{
  /* We might be an outlined function.  */
  if (DECL_IS_PRE_FN_P (fndecl) || DECL_IS_POST_FN_P (fndecl))
    fndecl = get_orig_for_outlined (fndecl);
  /* We might be a wrapper.  */
  if (DECL_IS_WRAPPER_FN_P (fndecl))
    fndecl = get_orig_func_for_wrapper (fndecl);
  /* We might be a declaration-level object contract's own synthesized
     check function.  */
  if (tree objdecl = get_orig_objdecl_for_check_fn (fndecl))
    fndecl = objdecl;
  return fndecl;
}

static tree
get_src_loc_impl_ptr_for (location_t loc, tree fndecl)
{
  if (!contracts_source_location_impl_type)
    get_contracts_source_location_impl_type ();

  fndecl = resolve_fndecl_for_diagnostic_name (fndecl);

  gcc_checking_assert (fndecl);
  tree impl__
    = build_source_location_impl (loc, fndecl,
				  contracts_source_location_impl_type);
  tree p = build_pointer_type (contracts_source_location_impl_type);
  return build_fold_addr_expr_with_type_loc (loc, impl__, p);
}

static tree
get_src_loc_impl_ptr (location_t loc)
{
  return get_src_loc_impl_ptr_for (loc, current_function_decl);
}

/* D4324 used to represent a control object's operator() argument with a
   separate, compiler-internal mirror type here (get_assertion_context_fields/
   init_builtin_assertion_context_type), reinterpret-cast to the real
   assertion_context& at the call boundary in build_contract_control_call --
   mirroring build_contract_violation_ctor's (P2900, pre-existing)
   contract_violation mirror-type approach for the bare, no-control-object
   path. That second, independent representation of a location (alongside
   the real std::source_location build_contract_control_constexpr_check
   needs for constant evaluation) is what caused two different requested
   __impl types to collide in build_source_location_impl's (cp-gimplify.cc)
   location+fndecl-keyed cache, silently corrupting whichever path ran
   second for the same assertion. assertion_context (like
   std::source_location) is a compiler-backed library type with no base
   classes or vtable to set up, so there was never a real need for the
   mirror type here: build_contract_control_call now builds directly
   against the real assertion_context, the same low-level "CONSTRUCTOR
   against a real class's own private fields" mechanism the constexpr path
   already used (and still does, unchanged) -- see there for why that's
   safe. contract_violation's own, separate mirror type is untouched: nothing
   about the bare path ever requests a second, differently-typed location
   for the same assertion, so it never collides with anything.  */

/* Build a contract_violation layout compatible object. */

/* Constructor.  At present, this should always be constant. */

static tree
build_contract_violation_ctor (tree contract)
{
  bool can_be_const = true;
  uint16_t version = 1;
  /* Default CDM_PREDICATE_FALSE. */
  uint16_t detection_mode = CDM_PREDICATE_FALSE;

  tree assertion_kind = CONTRACT_ASSERTION_KIND (contract);
  if (!assertion_kind || really_constant_p (assertion_kind))
    {
      contract_assertion_kind kind = get_contract_assertion_kind (contract);
      assertion_kind = build_int_cst (uint16_type_node, kind);
    }
  else
    can_be_const = false;

  tree eval_semantic = CONTRACT_EVALUATION_SEMANTIC (contract);
  gcc_checking_assert (eval_semantic);
  if (!really_constant_p (eval_semantic))
    can_be_const = false;

  tree comment = CONTRACT_COMMENT (contract);
  if (comment && !really_constant_p (comment))
    can_be_const = false;

  tree std_src_loc_impl_ptr = CONTRACT_STD_SOURCE_LOC (contract);
  if (std_src_loc_impl_ptr)
    {
      std_src_loc_impl_ptr = convert_from_reference (std_src_loc_impl_ptr);
      if (!really_constant_p (std_src_loc_impl_ptr))
	can_be_const = false;
    }
  else
    std_src_loc_impl_ptr = get_src_loc_impl_ptr (EXPR_LOCATION (contract));

  /* Must match the type layout in builtin_contract_violation_type.  */
  tree f0 = next_aggregate_field (TYPE_FIELDS (builtin_contract_violation_type));
  tree f1 = next_aggregate_field (DECL_CHAIN (f0));
  tree f2 = next_aggregate_field (DECL_CHAIN (f1));
  tree f3 = next_aggregate_field (DECL_CHAIN (f2));
  tree f4 = next_aggregate_field (DECL_CHAIN (f3));
  tree f5 = next_aggregate_field (DECL_CHAIN (f4));
  tree f6 = next_aggregate_field (DECL_CHAIN (f5));
  tree ctor = build_constructor_va
    (builtin_contract_violation_type, 7,
     f0, build_int_cst (uint16_type_node, version),
     f1, assertion_kind,
     f2, eval_semantic,
     f3, build_int_cst (uint16_type_node, detection_mode),
     f4, comment,
     f5, std_src_loc_impl_ptr,
     f6, build_zero_cst (nullptr_type_node)); // __vendor_ext

  TREE_READONLY (ctor) = true;
  if (can_be_const)
    TREE_CONSTANT (ctor) = true;

  return ctor;
}

/* Build a named TU-local constant of TYPE.  */

static tree
contracts_tu_local_named_var (location_t loc, const char *name, tree type)
{
  tree var_ = build_decl (loc, VAR_DECL, NULL, type);
  DECL_NAME (var_) = generate_internal_label (name);
  TREE_PUBLIC (var_) = false;
  DECL_EXTERNAL (var_) = false;
  TREE_STATIC (var_) = true;
  /* Compiler-generated.  */
  DECL_ARTIFICIAL (var_) = true;
  TREE_CONSTANT (var_) = true;
  layout_decl (var_, 0);
  return var_;
}

/* Create a read-only violation object.  */

static tree
build_contract_violation_constant (tree ctor, tree contract)
{
  tree viol_ = contracts_tu_local_named_var
    (EXPR_LOCATION (contract), "Lcontract_violation",
     builtin_contract_violation_type);

  TREE_CONSTANT (viol_) = true;
  DECL_INITIAL (viol_) = ctor;
  varpool_node::finalize_decl (viol_);

  return viol_;
}

/* Helper to replace references to dummy this parameters with references to
   the first argument of the FUNCTION_DECL DATA.  */

static tree
remap_dummy_this_1 (tree *tp, int *, void *data)
{
  if (!is_this_parameter (*tp))
    return NULL_TREE;
  tree fn = (tree)data;
  *tp = DECL_ARGUMENTS (fn);
  return NULL_TREE;
}

/* Replace all references to dummy this parameters in EXPR with references to
   the first argument of the FUNCTION_DECL FNDECL.  */

static void
remap_dummy_this (tree fndecl, tree *expr)
{
  walk_tree (expr, remap_dummy_this_1, fndecl, NULL);
}

/* Replace uses of user's placeholder var with the actual return value.  */

struct replace_tree
{
  tree from, to;
};

static tree
remap_retval_1 (tree *here, int *do_subtree, void *d)
{
  replace_tree *data = (replace_tree *) d;

  if (*here == data->from)
    {
      *here = data->to;
      *do_subtree = 0;
    }
  else
    *do_subtree = 1;
  return NULL_TREE;
}

static void
remap_retval (tree fndecl, tree contract)
{
  struct replace_tree data;
  data.from = POSTCONDITION_IDENTIFIER (contract);
  gcc_checking_assert (DECL_RESULT (fndecl));
  data.to = DECL_RESULT (fndecl);
  walk_tree (&CONTRACT_CONDITION (contract), remap_retval_1, &data, NULL);
}

/* Map the translation-unit -fcontract-evaluation-semantic flag to the
   std::contracts::evaluation_semantic value that is passed to a control
   type's compile-time members.  CES_IGNORE/OBSERVE/ENFORCE/QUICK (see
   contracts.h) already use the same 1-based ignore/observe/enforce/
   quick_enforce encoding as evaluation_semantic, so no remapping is
   needed.  */

static unsigned
contract_evaluation_semantic_value ()
{
  return flag_contract_evaluation_semantic;
}

/* CTRL is the constant-expression naming a control OBJECT for
   pre<...>/post<...>/contract_assert<...> (including the implicit
   std::contracts::default_v substituted for a bare pre/post/contract_assert
   under -fcontract-control-objects), or NULL_TREE.  Return CTRL's type,
   used for member lookup by every caller below (some of those members may
   be non-static, in which case the caller also needs CTRL's own value --
   see contract_control_bool_member -- but the type alone is always enough
   to find the member in the first place).  */

static tree
contract_control_naming_type (tree ctrl)
{
  return ctrl ? TREE_TYPE (ctrl) : NULL_TREE;
}

/* Cached, once-per-TU: a static, internal-linkage array of
   std::contracts::group_semantic_rule holding every
   -fcontracts-group-evaluation-semantic= entry
   (contract_group_semantic_table, c-family/c-common.h), in
   command-line order -- built lazily (most TUs never use the option at
   all) and referenced, not rebuilt, from every per-assertion
   assertion_static_info value (see build_group_rules_value below).  */
static GTY(()) tree group_rule_array_decl;

/* Build (once, cached in GROUP_RULE_ARRAY_DECL above) a static array of
   RULE_TYPE (std::contracts::group_semantic_rule), one element per
   contract_group_semantic_table entry.  Each element is a plain, flat,
   2-field CONSTRUCTOR directly filling group_semantic_rule's own
   private fields -- the same raw-field-filling technique
   build_assertion_static_info_value already uses for
   assertion_static_info itself (not a real constructor call: nothing
   needs calling, since RULE_TYPE has no user-declared constructors,
   and this bypasses its fields' own access control entirely, same as
   build_real_source_location_value already does for
   std::source_location's private _M_impl).  */

static tree
get_group_rule_array_decl (location_t loc, tree rule_type)
{
  if (group_rule_array_decl)
    return group_rule_array_decl;

  tree name_field = next_aggregate_field (TYPE_FIELDS (rule_type));
  tree sem_field = next_aggregate_field (DECL_CHAIN (name_field));

  vec<constructor_elt, va_gc> *elts = NULL;
  unsigned ix;
  contract_group_semantic_entry *entry;
  FOR_EACH_VEC_ELT (contract_group_semantic_table, ix, entry)
    {
      tree name_cst = build_string_literal (strlen (entry->name) + 1,
					    entry->name);
      tree rule_ctor = build_constructor_va
	(rule_type, 2,
	 name_field, name_cst,
	 sem_field, build_int_cst (TREE_TYPE (sem_field), entry->semantic));
      CONSTRUCTOR_APPEND_ELT (elts, size_int (ix), rule_ctor);
    }

  tree array_type = build_array_type_nelts
    (rule_type, contract_group_semantic_table.length ());
  tree ctor = build_constructor (array_type, elts);
  TREE_CONSTANT (ctor) = 1;
  TREE_STATIC (ctor) = 1;

  tree decl = build_decl (loc, VAR_DECL,
			  get_identifier ("__contracts_group_rules"),
			  array_type);
  DECL_ARTIFICIAL (decl) = 1;
  DECL_IGNORED_P (decl) = 1;
  TREE_STATIC (decl) = 1;
  TREE_READONLY (decl) = 1;
  /* Internal linkage: this is this TU's own, possibly TU-specific,
     command-line configuration -- unlike get_symbolic_key_decl's
     comdat-folded keys, there is nothing to safely fold across TUs
     here (a different TU may legitimately set different rules).  */
  TREE_PUBLIC (decl) = 0;
  /* group_semantic_rules() is read at compile time (from is_ignored/
     compute_semantic), not just at runtime, so this array must itself
     be usable in a constant expression -- a plain TREE_READONLY isn't
     enough for a class-type array (only "const variables of integral
     or enumeration type" get that for free); mark it constexpr
     directly, the same way a compiler-synthesized constant temporary
     already does (build_over_call's reference-temporary handling,
     call.cc).  */
  DECL_DECLARED_CONSTEXPR_P (decl) = 1;
  DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P (decl) = 1;
  TREE_CONSTANT (decl) = 1;
  DECL_INITIAL (decl) = ctor;
  pushdecl_top_level_and_finish (decl, ctor);

  group_rule_array_decl = decl;
  return decl;
}

/* Build a group_semantic_rules_view CONSTRUCTOR of type VIEW_TYPE for
   this TU's -fcontracts-group-evaluation-semantic= table -- empty (null
   data pointer, zero size) if the option was never given (the common
   case), without ever needing a zero-length array.  */

static tree
build_group_rules_value (tree view_type)
{
  tree data_field = next_aggregate_field (TYPE_FIELDS (view_type));
  tree size_field = next_aggregate_field (DECL_CHAIN (data_field));

  if (contract_group_semantic_table.is_empty ())
    return build_constructor_va
      (view_type, 2,
       data_field, build_zero_cst (TREE_TYPE (data_field)),
       size_field, build_int_cst (TREE_TYPE (size_field), 0));

  /* TREE_TYPE (data_field) is a pointer to group_semantic_rule; its own
     TREE_TYPE is that pointee type.  */
  tree rule_type = TREE_TYPE (TREE_TYPE (data_field));
  tree array_decl = get_group_rule_array_decl (input_location, rule_type);

  /* Array-to-pointer decay: the address of the array's first element,
     not of the array object itself (which would have array, not
     pointer, type).  */
  tree first_elt = build4 (ARRAY_REF, rule_type, array_decl,
			   size_zero_node, NULL_TREE, NULL_TREE);
  tree addr = fold_convert (TREE_TYPE (data_field),
			    build_fold_addr_expr (first_elt));

  return build_constructor_va
    (view_type, 2,
     data_field, addr,
     size_field, build_int_cst (TREE_TYPE (size_field),
				contract_group_semantic_table.length ()));
}

/* Build an assertion_static_info CONSTRUCTOR of type INFO_TYPE for a
   contract being evaluated for SIDE.  Shared by contract_control_bool_member
   below (evaluating a control-object query) and the assertion_context
   builders build_contract_control_call/build_contract_control_constexpr_check
   (populating assertion_context::static_info()) -- the one place this
   value's shape is built.  is_virtual/overrides_virtual are always false
   for now: groundwork for later work on virtual-function semantics, not
   yet wired to any real detection.  */

static tree
build_assertion_static_info_value (contract_check_side side, tree info_type)
{
  tree f0 = next_aggregate_field (TYPE_FIELDS (info_type));
  tree f1 = next_aggregate_field (DECL_CHAIN (f0));
  tree f2 = next_aggregate_field (DECL_CHAIN (f1));
  tree f3 = next_aggregate_field (DECL_CHAIN (f2));
  tree f4 = next_aggregate_field (DECL_CHAIN (f3));

  /* Matches std::contracts::assertion_check_side's enumerator values
     exactly (see the library header).  */
  int side_val;
  switch (side)
    {
    case ccs_not_applicable: side_val = 0; break;
    case ccs_definition: side_val = 1; break;
    case ccs_wrapper: side_val = 2; break;
    default: gcc_unreachable ();
    }

  return build_constructor_va
    (info_type, 5,
     f0, build_int_cst (TREE_TYPE (f0), contract_evaluation_semantic_value ()),
     f1, build_int_cst (TREE_TYPE (f1), side_val),
     f2, boolean_false_node,
     f3, boolean_false_node,
     f4, build_group_rules_value (TREE_TYPE (f4)));
}

/* Constant-evaluate CTRL::NAME(info) for the current translation unit's
   evaluation_semantic and the given SIDE, where NAME is a static *or
   non-static* member function taking a single
   std::contracts::assertion_static_info parameter (e.g. is_ignored,
   constify, assumable, omit_comment, ...).  For a non-static NAME, CTRL
   itself -- the real object, exactly as parsed -- is used as the call's
   instance (not a dummy placeholder), so instance-level state (e.g. a
   label that carries its own configuration data) is genuinely visible.
   Returns 1 if it folds to a compile-time true, 0 if it folds to false,
   and -1 if CTRL has no such usable compile-time member (no member by
   that name, not callable with one assertion_static_info argument, or
   the resulting call doesn't constant-fold to a bool -- including CTRL
   itself still being dependent, e.g. while merely analyzing an
   uninstantiated template body).

   Once the member is confirmed to genuinely exist (past the checks
   above), failing to constant-fold it is a real bug in CTRL -- a throw,
   a call to a non-constexpr function, anything -- not a "this trait
   wasn't provided" default, and unless QUIET is diagnosed as a hard
   error rather than silently collapsed into the same -1 that an
   absent member returns. QUIET must stay true for callers that may
   run while some unrelated, syntactically enclosing expression is
   itself being speculatively, quietly constant-evaluated (see
   build_contract_control_constexpr_check's own callers): in that
   case a real failure here must stay silent too, exactly like any
   other quiet constexpr evaluation attempt, since the ordinary
   (non-speculative) dispatch will raise this same error for real
   if/when the trait is actually consulted outside such a context.  */

static int
contract_control_bool_member (tree ctrl, const char *name,
			       contract_check_side side, bool quiet = false)
{
  tree ctrl_expr = ctrl;
  ctrl = contract_control_naming_type (ctrl);
  if (!ctrl || !CLASS_TYPE_P (ctrl))
    return -1;
  complete_type (ctrl);
  if (!COMPLETE_TYPE_P (ctrl))
    return -1;

  tree member = lookup_member (ctrl, get_identifier (name),
			       /*protect=*/1, /*want_type=*/false, tf_none);
  if (!member || member == error_mark_node || !BASELINK_P (member))
    return -1;

  tree fn = OVL_FIRST (BASELINK_FUNCTIONS (member));
  if (!fn || TREE_CODE (fn) != FUNCTION_DECL)
    return -1;

  /* The single parameter must be std::contracts::assertion_static_info
     (by value or by reference); build the argument directly in that
     type so overload resolution matches.  A member by this name with
     some other parameter type (e.g. a plain evaluation_semantic, or
     anything else a user might mistakenly write) is exactly the "not
     callable with one assertion_static_info argument" case this
     function's own contract already promises to handle by returning
     -1 -- build_assertion_static_info_value below assumes its type
     argument is assertion_static_info's own class type unconditionally
     (TYPE_FIELDS on anything else, e.g. an enum, is an ICE, not a
     graceful failure), so that assumption must be checked here first.
     FN may be static (TREE_TYPE a FUNCTION_TYPE) or non-static (a
     METHOD_TYPE, whose TYPE_ARG_TYPES has a leading implicit 'this'
     entry) -- FUNCTION_FIRST_USER_PARMTYPE skips that implicit entry
     for non-static FN and is a no-op for static FN, so no other change
     is needed to support both uniformly.  */
  tree parm_types = FUNCTION_FIRST_USER_PARMTYPE (fn);
  if (!parm_types || parm_types == void_list_node)
    return -1;
  tree info_type = non_reference (TREE_VALUE (parm_types));
  tree real_info_type
    = lookup_std_contracts_type (get_identifier ("assertion_static_info"));
  if (!same_type_ignoring_top_level_qualifiers_p (info_type, real_info_type))
    return -1;
  tree cfg_arg = build_assertion_static_info_value (side, info_type);

  releasing_vec args;
  vec_safe_push (args, cfg_arg);
  /* Use CTRL_EXPR itself -- the real object, exactly as parsed -- as the
     call's instance, rather than a build_dummy_object placeholder, so a
     non-static NAME can read genuine instance state.  No re-derivation
     (e.g. constant-evaluating CTRL_EXPR up front to build a fresh
     stand-in object) is needed or wanted: CTRL_EXPR is already either an
     lvalue naming a real control object (the common case, e.g. a
     pre<my_label>(...) referring to a named constexpr variable) or a
     prvalue temporary control object (e.g. an inline
     pre<my_label_t{...}>(...)), and in either case it is already exactly
     the same well-formed object expression build_new_method_call handles
     for any ordinary obj.method()/Temp{...}.method() call -- ordinary
     overload resolution and, for the prvalue case, ordinary temporary
     materialization apply unchanged.  Constant-folding the *whole*
     resulting call (including evaluating CTRL_EXPR as its object
     argument) happens uniformly afterward, via maybe_constant_value
     below, exactly like evaluating any other constant-expression call.
     This also stays correct while merely analyzing an uninstantiated
     template's own body (CTRL_EXPR still dependent): build_new_method_call
     already handles a dependent instance by deferring, same as it does
     for any other member call inside a template, and the later
     maybe_constant_value simply fails to fold, giving this function's
     usual -1 "not yet known" result.

     See suppress_conveyor_restrictions_for_trait_query_p's own comment:
     this call -- and its evaluation just below, in case the trait
     method's own body itself calls something else -- must never be
     subject to conveyor_restrictions_active_p's restrictions, which
     have nothing to do with this internal, compile-time-only probe.  */
  bool saved_suppress = suppress_conveyor_restrictions_for_trait_query_p;
  suppress_conveyor_restrictions_for_trait_query_p = true;
  tree call = build_new_method_call (ctrl_expr, member, &args, NULL_TREE,
				     LOOKUP_NORMAL, NULL, tf_none);
  tree val = (call && call != error_mark_node)
    ? maybe_constant_value (call) : NULL_TREE;
  if (call && call != error_mark_node
      && (!val || TREE_CODE (val) != INTEGER_CST)
      && !quiet
      && !processing_template_decl)
    {
      /* The member genuinely exists and is callable -- past this
	 point a fold failure is a real bug in CTRL, not a "this trait
	 wasn't provided" default, and must be diagnosed instead of
	 silently collapsing to the same -1 sentinel absence uses.
	 Re-run non-quiet purely to obtain the diagnostic: same idiom
	 as cp_fold_immediate's consteval-escalation check
	 (cp-gimplify.cc).

	 The processing_template_decl guard matters independently of
	 QUIET: this same query also runs while merely analyzing an
	 uninstantiated template's own body (e.g. a control object
	 used inside __glibcxx_assert within a template function that
	 is never actually instantiated in this TU) -- GCC's
	 cp_adjust_diagnostic_info (error.cc) automatically retags any
	 error_at issued in that context as -Wtemplate-body, meant to
	 be a downgradable, re-diagnosed-for-real-at-instantiation
	 check, not an unconditional hard error the way a bare
	 error_at here would be.  Skipping the loud path entirely
	 during template-body-only analysis defers correctly to
	 whatever real, non-dependent evaluation happens once (if
	 ever) the template is actually instantiated with concrete
	 arguments, matching that intended "speculative during the
	 template body, real at instantiation" behavior instead of
	 fighting it.  */
      auto_diagnostic_group d;
      location_t loc = cp_expr_loc_or_input_loc (call);
      error_at (loc, "%qs for control object of type %qT does not "
		"produce a constant expression", name, ctrl);
      cxx_constant_value (call, tf_error);
    }
  suppress_conveyor_restrictions_for_trait_query_p = saved_suppress;
  if (!call || call == error_mark_node)
    return -1;
  if (!val || TREE_CODE (val) != INTEGER_CST)
    return -1;
  return integer_onep (val) ? 1 : 0;
}

/* If the assertion names a control type CTRL, constant-evaluate
   CTRL::is_ignored(cfg) for the current translation unit's cfg.  Returns true
   iff it folds to a compile-time true, meaning the assertion is ignored: no
   code is emitted and the predicate is never evaluated, even under an enforced
   translation-unit default.  A bare contract (no control type) or a control
   type without a usable compile-time is_ignored member yields false, so the
   existing evaluation-semantic path is used instead.  */

static bool
contract_control_is_ignored (tree ctrl, contract_check_side side, bool quiet)
{
  return contract_control_bool_member (ctrl, "is_ignored", side, quiet) == 1;
}

/* True if the control type CTRL opts into constification
   (constify(cfg) == true for the TU's evaluation_semantic).  A bare
   contract, or a control type without a usable constify getter, does not
   constify.  */

bool
contract_control_constifies (tree ctrl, contract_check_side side, bool quiet)
{
  return contract_control_bool_member (ctrl, "constify", side, quiet) == 1;
}

/* True if the control type CTRL's is_conveyor(cfg) returns true for the
   TU's evaluation_semantic, meaning the predicate itself (not CTRL's own
   operator()) must satisfy the D4324 conveyor-function syntactic
   restrictions.  A bare contract, or a control type without a usable
   is_conveyor getter, is not conveyor.  */

bool
contract_control_is_conveyor (tree ctrl, contract_check_side side, bool quiet)
{
  return contract_control_bool_member (ctrl, "is_conveyor", side, quiet) == 1;
}

/* True if the control type CTRL's is_symbolic(cfg) returns true for the
   TU's evaluation_semantic -- axiom/symbolic contracts (see
   ~/gcc-axiom-contracts.md): a pre/post written against such a control
   object has no runtime representation by default (see build_contract_
   check's own is_symbolic early-return), existing purely for static
   analysis to consult.  A bare contract, or a control type without a
   usable is_symbolic getter, is not symbolic.  */

bool
contract_control_is_symbolic (tree ctrl, contract_check_side side, bool quiet)
{
  return contract_control_bool_member (ctrl, "is_symbolic", side, quiet) == 1;
}

/* True if the control type CTRL's never_proven(cfg) returns true --
   exempts a contract_assert naming CTRL from ever being checked against
   ambient facts (oa_handle_assertion_stmt's own "check" step), regardless
   of any proofs flag or analyzed_conveyor/proven_conveyor elsewhere; it
   still establishes itself as a trusted fact for later code, same as any
   other conveyor/symbolic contract_assert (see std::never_proven_conveyor
   in <contracts> for the intended use: the library's own internal,
   not-yet-migrated defensive assertions).  */

static bool
contract_control_never_proven (tree ctrl, contract_check_side side, bool quiet = false)
{
  return contract_control_bool_member (ctrl, "never_proven", side, quiet) == 1;
}

/* True if the control type CTRL's analyzed_conveyor(cfg)/proven_
   conveyor(cfg) returns true -- each implies is_conveyor, and
   additionally forces -fcontract-conveyor-proofs-equivalent analysis on
   for any contract naming it, regardless of the command-line flag.
   analyzed_conveyor is lenient (an unprovable conjunct is a warning);
   proven_conveyor is strict (unprovable is also an error, matching WG14
   P4021R2's compile_assert() outcome table).  Symbolic siblings mirror
   both exactly.  */

static bool
contract_control_analyzed_conveyor (tree ctrl, contract_check_side side, bool quiet = false)
{
  return contract_control_bool_member (ctrl, "analyzed_conveyor", side, quiet) == 1;
}

static bool
contract_control_proven_conveyor (tree ctrl, contract_check_side side, bool quiet = false)
{
  return contract_control_bool_member (ctrl, "proven_conveyor", side, quiet) == 1;
}

static bool
contract_control_analyzed_symbolic (tree ctrl, contract_check_side side, bool quiet = false)
{
  return contract_control_bool_member (ctrl, "analyzed_symbolic", side, quiet) == 1;
}

static bool
contract_control_proven_symbolic (tree ctrl, contract_check_side side, bool quiet = false)
{
  return contract_control_bool_member (ctrl, "proven_symbolic", side, quiet) == 1;
}

/* True if CTRL is conveyor for *any* reason -- is_conveyor, or either of
   the two D4324 traits that additionally force optional analysis on
   (analyzed_conveyor/proven_conveyor, see their own comment in
   <contracts>) but are also, in every other respect, conveyor
   themselves.  The single, shared answer to "is this conveyor" that
   every direct consultation site should use instead of contract_
   control_is_conveyor alone -- every place that used to check is_
   conveyor by itself (conveyor syntactic restrictions during parsing/
   substitution, oa_contract_conveyor_active_p, build_contract_check's
   own runtime-codegen gate) goes through this, so a contract tagged
   analyzed_conveyor/proven_conveyor is conveyor everywhere
   consistently, not just for the static analysis pass.  */

bool
contract_control_conveyor_like (tree ctrl, contract_check_side side, bool quiet)
{
  return contract_control_is_conveyor (ctrl, side, quiet)
	 || contract_control_analyzed_conveyor (ctrl, side, quiet)
	 || contract_control_proven_conveyor (ctrl, side, quiet);
}

/* Symbolic mirror of contract_control_conveyor_like immediately
   above.  */

bool
contract_control_symbolic_like (tree ctrl, contract_check_side side, bool quiet)
{
  return contract_control_is_symbolic (ctrl, side, quiet)
	 || contract_control_analyzed_symbolic (ctrl, side, quiet)
	 || contract_control_proven_symbolic (ctrl, side, quiet);
}

/* True if the control type CTRL's assumable(cfg) returns true for the TU's
   evaluation_semantic, meaning an ignored predicate may be handed to the
   optimizer as an assumption.  */

static bool
contract_control_assumable (tree ctrl, contract_check_side side, bool quiet)
{
  return contract_control_bool_member (ctrl, "assumable", side, quiet) == 1;
}

/* True if the control type CTRL's omit_comment(cfg) returns true for the
   TU's evaluation_semantic, meaning it never needs the assertion's
   stringified condition text, so the compiler should not embed it at all.
   Optional: a control type without this getter (or one that returns false)
   keeps the existing behaviour of always storing it.  */

static bool
contract_control_omits_comment (tree ctrl, contract_check_side side,
				 bool quiet = false)
{
  return contract_control_bool_member (ctrl, "omit_comment", side, quiet) == 1;
}

/* True if the control type CTRL's omit_source_location(cfg) returns true
   for the TU's evaluation_semantic, meaning it never needs the assertion's
   std::source_location, so the compiler should not build/embed one at all.
   Optional, same default-false behaviour as contract_control_omits_comment.  */

static bool
contract_control_omits_source_location (tree ctrl, contract_check_side side,
					 bool quiet = false)
{
  return contract_control_bool_member (ctrl, "omit_source_location", side,
					quiet) == 1;
}

/* True if the control type CTRL's force_client_side_check(cfg) returns true
   for the TU's evaluation_semantic, meaning any contract naming it is
   checked only via the caller-side (client) wrapper mechanism, never at the
   function's own definition, regardless of
   -fcontracts-client-check/-fcontracts-definition-check.  Optional, same
   default-false behaviour as contract_control_omits_comment.  */

static bool
contract_control_forces_client_side (tree ctrl, contract_check_side side,
				      bool quiet)
{
  return contract_control_bool_member (ctrl, "force_client_side_check", side,
					quiet) == 1;
}

/* True if the control type CTRL's force_definition_side_check(cfg) returns
   true for the TU's evaluation_semantic, the mirror image of
   contract_control_forces_client_side: any contract naming it is checked
   only at the function's own definition, never via a caller-side wrapper.  */

static bool
contract_control_forces_definition_side (tree ctrl, contract_check_side side,
					  bool quiet)
{
  return contract_control_bool_member (ctrl, "force_definition_side_check",
					side, quiet) == 1;
}

/* True if the control type CTRL's inherited(cfg) returns true for SIDE,
   meaning a contract-less override of the function this contract is
   attached to should behave, on that side, as if it had explicitly
   declared the identical contract (same control object).  See
   maybe_inherit_virtual_contract.  */

static bool
contract_control_inherited (tree ctrl, contract_check_side side,
			     bool quiet = false)
{
  return contract_control_bool_member (ctrl, "inherited", side, quiet) == 1;
}

/* If the control type CTRL provides the D4324 dispatch operator
   operator()(const assertion_context&), return its FUNCTION_DECL,
   otherwise NULL_TREE.  assertion_context bundles the comment, source
   location, evaluation_semantic, and a type-erased (args, check) callback
   pair that evaluates the predicate given those args: the operator decides
   whether/when to call it, rather than the compiler always evaluating the
   predicate itself.  Every named control type must provide this operator,
   even one that's always ignored or always assumable and so never actually
   calls it -- see build_contract_check, which errors out if CTRL is
   non-NULL and this returns NULL_TREE.  A bare contract (CTRL itself
   NULL_TREE, i.e. -fcontract-control-objects off) uses the built-in
   evaluation-semantic path instead.  */

static tree
contract_control_operator (tree ctrl)
{
  ctrl = contract_control_naming_type (ctrl);
  if (!ctrl || !CLASS_TYPE_P (ctrl))
    return NULL_TREE;
  complete_type (ctrl);
  if (!COMPLETE_TYPE_P (ctrl))
    return NULL_TREE;

  tree fns = lookup_member (ctrl, call_op_identifier,
			    /*protect=*/1, /*want_type=*/false, tf_none);
  if (!fns || fns == error_mark_node || !BASELINK_P (fns))
    return NULL_TREE;

  for (ovl_iterator it (BASELINK_FUNCTIONS (fns)); it; ++it)
    {
      tree fn = *it;
      if (TREE_CODE (fn) != FUNCTION_DECL)
	continue;
      tree parms = FUNCTION_FIRST_USER_PARMTYPE (fn);
      if (!parms || parms == void_list_node)
	continue;
      if (TREE_CHAIN (parms) != void_list_node)
	continue;
      /* The one parameter should be a reference (or value) to some class
	 type -- assertion_context, by convention, though we don't depend on
	 the name: a stray unrelated single-parameter operator() is far more
	 plausible than one taking 5 parameters was, so this arity check
	 alone is weaker evidence than it used to be.  */
      tree parm_type = non_reference (TREE_VALUE (parms));
      if (!CLASS_TYPE_P (parm_type))
	continue;
      return fn;
    }
  return NULL_TREE;
}

/* Build a fresh, file-local, static bool FUNCTION_DECL that copies ORIG's
   real parameter list verbatim (including an implicit `this' for a member
   function, kept as an ordinary leading pointer parameter -- never a
   METHOD_TYPE, since its address needs to flow through a type-erased
   struct/thunk rather than member-call syntax) plus, for a postcondition, a
   trailing result parameter, and whose body is a single
   `return <condition>;'.  This "outlines" the predicate itself, so a
   control object's operator() can invoke it on demand via a matching thunk
   (build_predicate_thunk_function) instead of the compiler evaluating it
   unconditionally -- and, for a virtual ORIG, so a later contract-less
   override can invoke it too (maybe_inherit_virtual_contract), reusing
   code already compiled once, with ORIG's own access, instead of
   re-hosting the condition anywhere new.

   Called either during genericization of ORIG (from build_contract_check,
   where ORIG is always CURRENT_FUNCTION_DECL), or eagerly, right after
   CONTRACT's condition finishes parsing (update_late_contract, for a
   virtual ORIG only) -- both contexts leave ORIG's own parameters and
   CONTRACT_CONDITION fully resolved, which is all this needs.
   Synthesizes and finalizes the new function immediately:
   push_struct_function/pop_cfun save and restore cfun/current_function_decl
   around the nested synthesis, so control returns to whichever of those two
   callers invoked this exactly as it was.  */

/* walk_tree callback data for find_condition_captures.  */

struct find_condition_captures_data
{
  hash_set<tree> *exclude;
  vec<tree, va_gc> *captured;
};

static tree
find_condition_captures_r (tree *tp, int *, void *data_)
{
  auto *data = (find_condition_captures_data *) data_;
  tree t = *tp;
  /* DECL_ARTIFICIAL excludes compiler-synthesized temporaries -- most
     importantly a TARGET_EXPR's own slot (plain walk_tree, with no
     custom callback intercepting TARGET_EXPR specially, still walks
     into its operand 0, the slot, as an ordinary VAR_DECL leaf): that's
     already correctly given a fresh copy by copy_tree_body_r's own
     existing SAVE_EXPR/TARGET_EXPR handling (remap_save_expr), which
     depends on it being a plain automatic variable of ORIG, not an
     extra captured parameter of the outlined function -- capturing it
     here too would rebind the slot to a passed-in parameter instead of
     the outlined function's own local temporary storage, corrupting
     the target-expr's own semantics entirely.  A genuine, user-written
     local variable is never artificial.  */
  if ((VAR_P (t) || TREE_CODE (t) == PARM_DECL)
      && !DECL_ARTIFICIAL (t)
      && !data->exclude->contains (t))
    {
      bool already = false;
      for (unsigned i = 0; i < vec_safe_length (data->captured); ++i)
	if ((*data->captured)[i] == t)
	  {
	    already = true;
	    break;
	  }
      if (!already)
	vec_safe_push (data->captured, t);
    }
  return NULL_TREE;
}

/* Collect, in walk_tree's own stable traversal order, every VAR_DECL or
   PARM_DECL that CONDITION references other than one of ORIG's own
   parameters, ORIG's DECL_RESULT, or (if POSTCOND_ID) the postcondition
   placeholder -- all of which build_predicate_core_function_1 already
   maps some other way.  A pre/post condition can only ever reference its
   own parameters/result, so this is always empty for one of those; only
   an ASSERTION_STMT's condition -- an ordinary in-body statement, free to
   reference any local variable in scope at that point, exactly like a
   plain assert() would -- can populate it.  Called twice for the same
   CONTRACT (once building the predicate core function's own extra
   parameters, once building the caller-side argument struct that feeds
   them): both calls walk the same, unmodified CONTRACT_CONDITION tree, so
   both see the same list in the same order, safe to zip positionally.  */

static vec<tree, va_gc> *
find_condition_captures (tree condition, tree orig, tree postcond_id)
{
  hash_set<tree> exclude;
  for (tree p = DECL_ARGUMENTS (orig); p; p = DECL_CHAIN (p))
    exclude.add (p);
  if (tree result = DECL_RESULT (orig))
    exclude.add (result);
  if (postcond_id)
    exclude.add (postcond_id);

  find_condition_captures_data data;
  data.exclude = &exclude;
  data.captured = NULL;
  walk_tree (&condition, find_condition_captures_r, &data, NULL);
  return data.captured;
}

static tree
build_predicate_core_function_1 (tree contract, tree orig)
{
  location_t loc = EXPR_LOCATION (contract);
  bool postcondition = POSTCONDITION_P (contract);

  tree result_type = postcondition ? TREE_TYPE (TREE_TYPE (orig)) : NULL_TREE;
  bool has_result = postcondition && result_type
		    && !VOID_TYPE_P (result_type);
  tree postcond_id = postcondition ? POSTCONDITION_IDENTIFIER (contract)
				    : NULL_TREE;
  vec<tree, va_gc> *captures
    = find_condition_captures (CONTRACT_CONDITION (contract), orig,
				postcond_id);

  /* Parameter TYPE list: a copy of ORIG's, plus one entry per captured
     local (see find_condition_captures), plus a trailing result type for
     a postcondition with a non-void return.  */
  tree arg_types = NULL_TREE;
  tree *last_type = &arg_types;
  for (tree p = DECL_ARGUMENTS (orig); p; p = DECL_CHAIN (p))
    {
      *last_type = build_tree_list (NULL_TREE, TREE_TYPE (p));
      last_type = &TREE_CHAIN (*last_type);
    }
  for (unsigned i = 0; i < vec_safe_length (captures); ++i)
    {
      *last_type = build_tree_list (NULL_TREE, TREE_TYPE ((*captures)[i]));
      last_type = &TREE_CHAIN (*last_type);
    }
  if (has_result)
    {
      *last_type = build_tree_list (NULL_TREE, result_type);
      last_type = &TREE_CHAIN (*last_type);
    }
  *last_type = void_list_node;

  tree fn_type = build_function_type (boolean_type_node, arg_types);
  tree name = clone_function_name_numbered (orig, "pred");
  tree fn = build_lang_decl_loc (loc, FUNCTION_DECL, name, fn_type);
  DECL_CONTEXT (fn) = NULL_TREE;
  DECL_SOURCE_LOCATION (fn) = loc;
  SET_DECL_ASSEMBLER_NAME (fn, name);

  /* Copy ORIG's parameters verbatim, one fresh parameter per captured
     local, plus a trailing result parameter.  */
  tree new_args = NULL_TREE;
  tree *last_arg = &new_args;
  for (tree p = DECL_ARGUMENTS (orig); p; p = DECL_CHAIN (p))
    {
      tree np = copy_decl (p);
      DECL_CONTEXT (np) = fn;
      DECL_CHAIN (np) = NULL_TREE;
      suppress_warning (np);
      *last_arg = np;
      last_arg = &DECL_CHAIN (np);
    }
  tree new_captures = NULL_TREE;
  tree *last_capture = &new_captures;
  for (unsigned i = 0; i < vec_safe_length (captures); ++i)
    {
      /* A captured local may be a VAR_DECL (an ordinary local variable)
	 or a PARM_DECL (e.g. a lambda's own parameter, if the condition
	 references one) -- either way, DECL_ARGUMENTS (fn) must be a
	 PARM_DECL chain, so build a fresh one of the same type rather
	 than copy_decl, which would preserve a VAR_DECL's own tree code.  */
      tree cp = (*captures)[i];
      tree np = build_lang_decl (PARM_DECL, DECL_NAME (cp), TREE_TYPE (cp));
      DECL_CONTEXT (np) = fn;
      DECL_ARTIFICIAL (np) = true;
      suppress_warning (np);
      *last_arg = np;
      last_arg = &DECL_CHAIN (np);
      *last_capture = np;
      last_capture = &DECL_CHAIN (np);
    }
  tree result_parm = NULL_TREE;
  if (has_result)
    {
      result_parm = build_lang_decl (PARM_DECL, get_identifier ("__r"),
				     result_type);
      DECL_CONTEXT (result_parm) = fn;
      DECL_ARTIFICIAL (result_parm) = true;
      suppress_warning (result_parm);
      *last_arg = result_parm;
      last_arg = &DECL_CHAIN (result_parm);
    }
  DECL_ARGUMENTS (fn) = new_args;
  DECL_RESULT (fn) = NULL_TREE; /* Let start_preparsed_function fill it in.  */

  TREE_STATIC (fn) = 1;
  TREE_USED (fn) = 1;
  DECL_ARTIFICIAL (fn) = 1;
  TREE_PUBLIC (fn) = 0;
  DECL_EXTERNAL (fn) = 0;
  DECL_INTERFACE_KNOWN (fn) = 1;
  suppress_warning (fn);

  /* Remap CONTRACT's condition from ORIG's real decls (parameters, and for a
     postcondition, DECL_RESULT), plus each captured local, onto FN's fresh
     copies.  Work on a local copy of the tree pointer, not
     CONTRACT_CONDITION's own slot, so the original (already fully
     processed for ORIG) is left untouched.  */
  copy_body_data id;
  hash_map<tree, tree> decl_map;
  memset (&id, 0, sizeof (id));
  id.src_fn = orig;
  id.dst_fn = fn;
  id.src_cfun = DECL_STRUCT_FUNCTION (orig);
  id.decl_map = &decl_map;
  id.copy_decl = copy_decl_no_change;
  id.transform_call_graph_edges = CB_CGE_DUPLICATE;
  id.transform_new_cfg = false;
  id.transform_return_to_modify = false;
  id.transform_parameter = true;
  id.regimplify = false;
  id.do_not_unshare = true;
  id.do_not_fold = true;
  id.eh_lp_nr = 0;

  tree dp = new_args;
  for (tree sp = DECL_ARGUMENTS (orig); sp; sp = DECL_CHAIN (sp), dp = DECL_CHAIN (dp))
    insert_decl_map (&id, sp, dp);
  {
    tree ncp = new_captures;
    for (unsigned i = 0; i < vec_safe_length (captures); ++i, ncp = DECL_CHAIN (ncp))
      insert_decl_map (&id, (*captures)[i], ncp);
  }
  if (has_result)
    {
      insert_decl_map (&id, DECL_RESULT (orig), result_parm);
      /* CONTRACT's condition may not have had its postcondition
	 placeholder substituted with DECL_RESULT (orig) yet: that
	 substitution (remap_retval) is part of ORIG's own
	 genericization, and this function may be called (e.g. by
	 build_base_contract_expr, for a base_contract<Base>() naming
	 ORIG's class) before ORIG's own body has been genericized --
	 body genericization order between unrelated functions in a TU
	 is not otherwise something this depends on.  Map the
	 placeholder too, so either form works.  */
      if (postcond_id && postcond_id != DECL_RESULT (orig))
	insert_decl_map (&id, postcond_id, result_parm);
    }

  tree condition = CONTRACT_CONDITION (contract);
  walk_tree (&condition, copy_tree_body_r, &id, NULL);
  condition = fold_convert (boolean_type_node, condition);

  /* Synthesizing and finishing a whole new, unrelated (non-member) function
     while genericizing ORIG (which is still mid-flight on the call stack,
     possibly itself a class member with its own class scope still pushed)
     needs to temporarily act as if at global scope: push_to_top_level /
     pop_from_top_level is the front end's general-purpose primitive for
     that (it also handles the plain cfun/current_function_decl save that
     push_function_context alone provides, but additionally resets
     current_class_type/current_namespace/etc., which a bare
     push_function_context leaves untouched -- leaving them untouched here
     is what makes finish_function wrongly believe FN shares ORIG's still-
     active class scope and pop it a second, unbalanced time).
     start_preparsed_function/finish_function internally does the actual
     genericization, matching how every other synthesized function body in
     this front end is finished.  */
  push_to_top_level ();
  start_preparsed_function (fn, NULL_TREE, SF_PRE_PARSED | SF_DEFAULT);
  /* The body below is built directly as already-resolved trees rather than
     via the normal semantic-level call-building routines, so the usual
     "did we see anything that might throw" bookkeeping never runs.  Without
     this, finish_function would conclude FN can't throw and mark it
     TREE_NOTHROW, and an exception genuinely raised while evaluating the
     predicate would hit that false nothrow boundary and terminate instead
     of propagating -- exactly backwards from a control object that wants
     to let it through.  */
  cp_function_chain->can_throw = true;
  tree body = begin_function_body ();
  finish_return_stmt (condition);
  finish_function_body (body);
  fn = finish_function (/*inline_p=*/false);
  expand_or_defer_fn (fn);
  pop_from_top_level ();

  return fn;
}

/* Return CONTRACT's cached predicate core function, or NULL_TREE if none
   has been built yet.  */

static tree
get_contract_predicate_core_fn (tree contract)
{
  tree *result = hash_map_safe_get (contract_predicate_core_fn, contract);
  return result ? *result : NULL_TREE;
}

/* Return CONTRACT's predicate core function for ORIG, building and
   caching it via build_predicate_core_function_1 if this is the first
   request for it (whether that first request comes from the normal,
   genericization-time dispatch path below, or eagerly, from
   update_late_contract for a virtual ORIG) -- so at most one such
   function is ever built per contract, callable by anything that later
   needs it.  */

static tree
get_or_build_predicate_core_function (tree contract, tree orig)
{
  tree fn = get_contract_predicate_core_fn (contract);
  if (fn)
    return fn;
  fn = build_predicate_core_function_1 (contract, orig);
  hash_map_maybe_create<hm_ggc> (contract_predicate_core_fn);
  contract_predicate_core_fn->put (contract, fn);
  return fn;
}

/* A synthesized (inherited) contract stashes its bookkeeping directly in
   its own, otherwise-unused CONTRACT_STD_SOURCE_LOC operand, rather than
   in a side table keyed by the contract tree's identity: this specifier
   gets duplicated by copy_node an arbitrary number of times (for a
   caller-side wrapper, for an outlined PRE_FN/POST_FN, ...), each
   producing a distinct tree object that a side-table lookup would miss,
   while copy_node itself -- along with the remap walks in
   copy_contracts_list/copy_and_remap_contracts/remap_and_emit_conditions,
   which only ever touch CONTRACT_CONDITION and POSTCONDITION_IDENTIFIER
   -- shallow-copies every other operand, including this one, verbatim.

   The stashed value is
     (bits . (basefn . base_contract))
   where BITS (an INTEGER_CST) encodes which side(s) this specifier is
   eligible on (bit 0/1 client, bit 1/2 definition) -- the answer
   contract_runs_on_side uses in place of the normal force-flag/policy
   logic, the authoritative decision for this specifier -- and
   (BASEFN . BASE_CONTRACT) is what CONTRACT_CONDITION is still pending
   against, until resolve_inherited_contract fills it in for good (see
   its own comment for why that's deferred instead of done up front).  */

#define CONTRACT_INHERITED_CLIENT_BIT 1
#define CONTRACT_INHERITED_DEFINITION_BIT 2

/* True if CONTRACT was synthesized by maybe_inherit_virtual_contract
   (rather than written explicitly).  */

static bool
contract_is_inherited_p (tree contract)
{
  tree marker = CONTRACT_STD_SOURCE_LOC (contract);
  return marker && TREE_CODE (marker) == TREE_LIST
	 && TREE_VALUE (marker) && TREE_CODE (TREE_VALUE (marker)) == TREE_LIST
	 && TREE_PURPOSE (TREE_VALUE (marker))
	 && TREE_CODE (TREE_PURPOSE (TREE_VALUE (marker))) == FUNCTION_DECL;
}

/* True if inherited CONTRACT (contract_is_inherited_p) is eligible on
   SIDE.  */

static bool
contract_inherited_runs_on_side (tree contract, contract_check_side side)
{
  gcc_checking_assert (contract_is_inherited_p (contract));
  int bits = TREE_INT_CST_LOW (TREE_PURPOSE (CONTRACT_STD_SOURCE_LOC (contract)));
  int bit = side == ccs_definition ? CONTRACT_INHERITED_DEFINITION_BIT
				    : CONTRACT_INHERITED_CLIENT_BIT;
  return (bits & bit) != 0;
}

/* Map from an overrider FUNCTION_DECL that has received at least one
   inherited contract (maybe_inherit_virtual_contract) to the specific
   base FUNCTION_DECL it inherited from -- so that a second, distinct
   direct base independently offering an inheritable contract for the
   same override can be recognized as ambiguous rather than silently
   merged or overwritten.  */
static GTY(()) hash_map<tree, tree> *contract_inherited_from;

/* True if OVERRIDER currently has contracts that came only from
   maybe_inherit_virtual_contract, i.e. it had none of its own to begin
   with.  Distinguishes that case from OVERRIDER having genuinely
   user-written contracts, which must never be touched here.  */

static bool
contract_only_has_inherited_p (tree overrider)
{
  return hash_map_safe_get (contract_inherited_from, overrider) != NULL;
}

/* For each contract on BASEFN whose TREE_CODE is CODE (PRECONDITION_STMT
   or POSTCONDITION_STMT), require every one of their control objects to
   agree on inherited(info) for both sides; report the unanimous answer
   in CLIENT/DEFINITION.  Returns false (diagnosing the disagreement)
   if they don't agree, in which case CLIENT/DEFINITION are both left
   false, as if inheritance had been declined outright.  Returns true
   (with CLIENT/DEFINITION left false) when there are no contracts of
   this CODE on BASEFN at all -- nothing to inherit, not an error.  */

static bool
whole_set_inherited_p (tree basefn, tree_code code, tree overrider,
			bool *client, bool *definition)
{
  *client = false;
  *definition = false;
  bool seen = false;
  for (tree spec = get_fn_contract_specifiers (basefn); spec;
       spec = TREE_CHAIN (spec))
    {
      tree contract = CONTRACT_STATEMENT (spec);
      if (TREE_CODE (contract) != code)
	continue;
      tree ctrl = CONTRACT_CONTROL_OBJECT (contract);
      if (!ctrl)
	continue;
      bool this_client = contract_control_inherited (ctrl, ccs_wrapper);
      bool this_definition = contract_control_inherited (ctrl, ccs_definition);
      if (!seen)
	{
	  *client = this_client;
	  *definition = this_definition;
	  seen = true;
	}
      else if (this_client != *client || this_definition != *definition)
	{
	  auto_diagnostic_group d;
	  error_at (DECL_SOURCE_LOCATION (overrider),
		    "disagreement between inherited base contracts for %qD",
		    overrider);
	  inform (DECL_SOURCE_LOCATION (basefn),
		  "base contracts declared on %qD disagree on %<inherited%>",
		  basefn);
	  *client = false;
	  *definition = false;
	  return false;
	}
    }
  return true;
}

/* True if inherited CONTRACT (contract_is_inherited_p) hasn't had its
   condition resolved into a real call yet (see the file comment on
   synthesize_inherited_specifier), and if so, set *BASEFN and
   *BASE_CONTRACT to what it's pending against.  Once resolve_inherited_contract
   overwrites CONTRACT_CONDITION, this returns false forever after (the
   bookkeeping in CONTRACT_STD_SOURCE_LOC is left in place -- harmless,
   since side-eligibility queries still need it, but the placeholder
   CONTRACT_CONDITION it was pending against is gone).  */

static bool
contract_inherited_pending_p (tree contract, tree *basefn, tree *base_contract)
{
  if (!contract_is_inherited_p (contract)
      || CONTRACT_CONDITION (contract) != boolean_true_node)
    return false;
  tree pending = TREE_VALUE (CONTRACT_STD_SOURCE_LOC (contract));
  *basefn = TREE_PURPOSE (pending);
  *base_contract = TREE_VALUE (pending);
  return true;
}

/* Resolve CONTRACT (a pending inherited specifier, per
   contract_inherited_pending_p, currently being processed as part of
   USING_FNDECL -- CONTRACT's own real function, its caller-side wrapper,
   or its outlined PRE_FN/POST_FN) into a real condition: a call to
   BASE_CONTRACT's own (cached, built with BASEFN's own access) predicate
   core function, with USING_FNDECL's own `this' (whichever of those
   three it is -- its first parameter always has the same, real static
   pointer-to-derived-class type, regardless) adjusted to BASEFN's type,
   and USING_FNDECL's other parameters (identical types to BASEFN's,
   guaranteed by override compatibility) passed through unchanged.

   Deferred to here (called from build_contract_check, at genericization
   time) rather than resolved once when the specifier is first
   synthesized (maybe_inherit_virtual_contract, called from
   look_for_overrides_r during the *overrider's* own class completion):
   at that point the overriding class itself is not yet a complete type
   (finish_struct_1 is still running), and both lookup_base and
   build_base_path need a complete type to compute a base subobject
   offset.  By genericization time -- long after every class involved is
   complete -- this is safe, and USING_FNDECL is exactly the right
   function to build the call against no matter which of the three
   contexts is currently being processed.  */

static void
resolve_inherited_contract (tree contract, tree using_fndecl,
			    tree basefn, tree base_contract)
{
  tree core_fn = get_or_build_predicate_core_function (base_contract, basefn);

  tree this_parm = DECL_ARGUMENTS (using_fndecl);
  tree binfo = lookup_base (TREE_TYPE (TREE_TYPE (this_parm)),
			    DECL_CONTEXT (basefn), ba_any, NULL,
			    tf_warning_or_error);
  gcc_assert (binfo && binfo != error_mark_node);

  releasing_vec args;
  tree adjusted_this = build_base_path (PLUS_EXPR, this_parm, binfo, 1,
					 tf_warning_or_error);
  vec_safe_push (args, adjusted_this);
  /* USING_FNDECL's own DECL_ARGUMENTS never has a trailing result slot
     (that's specific to CORE_FN's own synthesized signature) -- just its
     ordinary parameters, all the way to the end.  */
  for (tree p = DECL_CHAIN (this_parm); p; p = DECL_CHAIN (p))
    vec_safe_push (args, p);
  /* The postcondition result placeholder, already built (and, for a
     copy made for a wrapper or outlined PRE_FN/POST_FN, already remapped
     onto that copy) as part of synthesizing this specifier.  */
  if (POSTCONDITION_P (contract))
    vec_safe_push (args, POSTCONDITION_IDENTIFIER (contract));

  CONTRACT_CONDITION (contract)
    = build_call_a (core_fn, args->length (), args->address ());
  /* CONTRACT_STD_SOURCE_LOC is deliberately left as-is: it still holds
     this specifier's side-eligibility bits (contract_inherited_runs_on_side),
     which stay relevant forever; only the now-resolved CONTRACT_CONDITION
     is what made this "pending".  */
}

/* Synthesize OVERRIDER's own copy of BASE_SPEC (one entry of BASEFN's
   contract-specifier list, as returned by get_fn_contract_specifiers),
   recording its side eligibility (CLIENT/DEFINITION) and a (BASEFN,
   BASE_CONTRACT) marker together in the otherwise-unused
   CONTRACT_STD_SOURCE_LOC slot (see the comment above
   contract_is_inherited_p for why there, not a side table).  Mirrors
   copy_contracts_list's shallow copy-and-rebuild of the specifier-list
   entry shape.  The condition itself is left as a placeholder (never
   evaluated as-is) -- resolved into a real call later, by
   resolve_inherited_contract, once OVERRIDER (or a copy made for its
   wrapper or outlined PRE_FN/POST_FN) is definitely a complete type.  */

static tree
synthesize_inherited_specifier (tree base_spec, tree basefn, tree overrider,
				 bool client, bool definition)
{
  tree base_contract = CONTRACT_STATEMENT (base_spec);
  tree result_parm = NULL_TREE;
  if (TREE_CODE (base_contract) == POSTCONDITION_STMT)
    {
      result_parm = build_lang_decl (PARM_DECL,
				      get_identifier ("__inherited_r"),
				      make_auto ());
      DECL_ARTIFICIAL (result_parm) = true;
      DECL_SOURCE_LOCATION (result_parm) = DECL_SOURCE_LOCATION (overrider);
    }

  tree entry = copy_node (base_spec);
  tree contract = copy_node (base_contract);
  TREE_VALUE (entry) = build_tree_list (TREE_PURPOSE (TREE_VALUE (base_spec)),
					contract);
  TREE_CHAIN (entry) = NULL_TREE;

  int bits = (client ? CONTRACT_INHERITED_CLIENT_BIT : 0)
	     | (definition ? CONTRACT_INHERITED_DEFINITION_BIT : 0);
  CONTRACT_CONDITION (contract) = boolean_true_node; /* Placeholder.  */
  CONTRACT_STD_SOURCE_LOC (contract)
    = build_tree_list (build_int_cst (integer_type_node, bits),
		       build_tree_list (basefn, base_contract));
  if (result_parm)
    POSTCONDITION_IDENTIFIER (contract) = result_parm;

  return entry;
}

/* OVERRIDER overrides BASEFN (gcc/cp/search.cc's look_for_overrides_r has
   just confirmed this and validated signature compatibility via
   check_final_overrider).  If OVERRIDER has no contracts of its own, and
   BASEFN's precondition-set and/or postcondition-set (each as a whole --
   see whole_set_inherited_p) grants inherited() == true for at least one
   side, synthesize OVERRIDER's own copy of each contract in that set,
   eligible on exactly the side(s) granted, and attach them to OVERRIDER.
   A second, distinct direct base independently offering an inheritable
   contract for the same override is diagnosed as ambiguous.  */

void
maybe_inherit_virtual_contract (tree overrider, tree basefn)
{
  if (!flag_contract_control_objects)
    return;
  if (DECL_HAS_CONTRACTS_P (overrider)
      && !contract_only_has_inherited_p (overrider))
    return;
  if (!DECL_HAS_CONTRACTS_P (basefn))
    return;

  bool pre_client, pre_definition, post_client, post_definition;
  whole_set_inherited_p (basefn, PRECONDITION_STMT, overrider,
			  &pre_client, &pre_definition);
  whole_set_inherited_p (basefn, POSTCONDITION_STMT, overrider,
			  &post_client, &post_definition);

  if (!pre_client && !pre_definition && !post_client && !post_definition)
    return;

  tree *prev = hash_map_safe_get (contract_inherited_from, overrider);
  if (prev && *prev != basefn)
    {
      auto_diagnostic_group d;
      error_at (DECL_SOURCE_LOCATION (overrider),
		"ambiguous inherited contract for %qD", overrider);
      inform (DECL_SOURCE_LOCATION (*prev),
	      "inherited from %qD here", *prev);
      inform (DECL_SOURCE_LOCATION (basefn),
	      "and also from %qD here", basefn);
      return;
    }

  tree last = NULL_TREE, new_specs = NULL_TREE;
  for (tree spec = get_fn_contract_specifiers (basefn); spec;
       spec = TREE_CHAIN (spec))
    {
      tree contract = CONTRACT_STATEMENT (spec);
      bool client, definition;
      if (TREE_CODE (contract) == PRECONDITION_STMT)
	client = pre_client, definition = pre_definition;
      else if (TREE_CODE (contract) == POSTCONDITION_STMT)
	client = post_client, definition = post_definition;
      else
	continue;
      if (!client && !definition)
	continue;

      tree entry = synthesize_inherited_specifier (spec, basefn, overrider,
						    client, definition);
      chainon (last, entry);
      last = entry;
      if (!new_specs)
	new_specs = entry;
    }

  hash_map_maybe_create<hm_ggc> (contract_inherited_from);
  contract_inherited_from->put (overrider, basefn);
  set_fn_contract_specifiers (overrider, new_specs);
}

/* D4324 step 3: std::contracts::base_contract<Base>() -- an explicit,
   user-written reference (inside a pre<>/post<> condition) to a named
   base class's own corresponding contract, as opposed to
   maybe_inherit_virtual_contract's automatic, all-or-nothing inheritance
   above.  Declared (never defined) as a plain function template in
   <contracts>; base_contract<Base>() is therefore ordinary,
   unmodified-grammar template-id-call syntax, recognized here purely by
   which template a CALL_EXPR's callee is a specialization of -- nothing
   in the parser needs to know about it at all.  */

/* Cached TEMPLATE_DECL for std::contracts::base_contract, looked up once
   (mirrors lookup_std_contracts_type).  */

static GTY(()) tree base_contract_template;

static tree
lookup_base_contract_template ()
{
  if (base_contract_template)
    return base_contract_template;

  tree id_ns = get_identifier ("contracts");
  tree ns = lookup_qualified_name (std_node, id_ns);
  if (TREE_CODE (ns) != NAMESPACE_DECL)
    return NULL_TREE;

  tree found = lookup_qualified_name (ns, get_identifier ("base_contract"));
  for (tree f : lkp_range (found))
    if (TREE_CODE (f) == TEMPLATE_DECL)
      {
	base_contract_template = f;
	break;
      }
  return base_contract_template;
}

/* If CALL is a call to a specialization of std::contracts::base_contract,
   return true and set *BASE_TYPE to its explicit template argument.  */

static bool
base_contract_call_p (tree call, tree *base_type)
{
  if (TREE_CODE (call) != CALL_EXPR)
    return false;
  tree tmpl = lookup_base_contract_template ();
  if (!tmpl)
    return false;
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL
      || !is_specialization_of (callee, tmpl))
    return false;
  tree args = DECL_TI_ARGS (callee);
  if (!args || TREE_VEC_LENGTH (args) < 1)
    return false;
  *base_type = TREE_VEC_ELT (args, 0);
  return true;
}

/* D4324/P2680: std::is_object_address(p) -- a compile-time-only proof
   predicate usable inside a conveyor-checked contract predicate.
   Declared (never defined) as a plain function template directly in
   namespace std (see libstdc++-v3/include/std/contracts); recognized
   here purely by which template a CALL_EXPR's callee is a
   specialization of, exactly like base_contract<Base>() above -- the
   only difference is the single argument's type is deduced, not an
   explicit template argument, so there's nothing to extract from
   DECL_TI_ARGS here.  */

/* Cached TEMPLATE_DECL for std::is_object_address (mirrors
   lookup_base_contract_template).  */

static GTY(()) tree is_object_address_template;

static tree
lookup_is_object_address_template ()
{
  if (is_object_address_template)
    return is_object_address_template;

  tree found = lookup_qualified_name (std_node,
				       get_identifier ("is_object_address"));
  for (tree f : lkp_range (found))
    if (TREE_CODE (f) == TEMPLATE_DECL)
      {
	is_object_address_template = f;
	break;
      }
  return is_object_address_template;
}

/* If CALL is a call to a specialization of std::is_object_address,
   return true and set *ARG to its (single) argument expression.  */

bool
is_object_address_call_p (tree call, tree *arg)
{
  /* A conjunct picked out of a &&-chain, or a whole precondition/
     postcondition condition consisting of nothing else, commonly
     arrives wrapped in a location wrapper (a VIEW_CONVERT_EXPR/
     NON_LVALUE_EXPR purely there to carry a location_t) and/or a
     CLEANUP_POINT_EXPR (the full-expression temporary-cleanup scope a
     condition gets wrapped in at this pre-genericize stage) -- unlike a
     call reached via cp_walk_tree, which recurses through those on its
     own, a direct TREE_CODE check here needs to strip them first.  */
  STRIP_ANY_LOCATION_WRAPPER (call);
  while (call && TREE_CODE (call) == CLEANUP_POINT_EXPR)
    {
      call = TREE_OPERAND (call, 0);
      STRIP_ANY_LOCATION_WRAPPER (call);
    }
  if (!call || TREE_CODE (call) != CALL_EXPR)
    return false;
  tree tmpl = lookup_is_object_address_template ();
  if (!tmpl)
    return false;
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL
      || !is_specialization_of (callee, tmpl))
    return false;
  if (call_expr_nargs (call) != 1)
    return false;
  *arg = CALL_EXPR_ARG (call, 0);
  return true;
}

/* D4324: is FN a specialization of std::is_object_address itself, rather
   than an arbitrary call appearing as one of its arguments? Used by the
   callee-must-be-conveyor check (call.cc's build_over_call, typeck.cc's
   cp_build_function_call_vec) to exempt it: is_object_address is a
   special, compiler-recognized identifier that is never actually invoked
   at runtime (resolve_object_address_in_function_1 always resolves or
   rejects it before genericization -- see that function's own comment),
   so requiring a 'conveyor' declaration on it would be meaningless (it
   has no body of its own for the mandatory checks to even examine) and
   would incorrectly block the one, well-established way to name an
   object's own address inside conveyor-restricted code at all.  */

bool
is_object_address_fndecl_p (tree fn)
{
  if (!fn || TREE_CODE (fn) != FUNCTION_DECL)
    return false;
  tree tmpl = lookup_is_object_address_template ();
  return tmpl && is_specialization_of (fn, tmpl);
}

/* D4324: is FN std::unreachable itself? Same recognition constexpr.cc's
   own check_conveyor_function_body_r already uses to reject a call to it
   with its own specific diagnostic. The callee-must-be-conveyor check
   exempts it for the identical reason: without this, that later,
   already-specific diagnostic would never be reached at all (this
   check's own, less specific "not declared conveyor" error would fire
   first, at the point the call is built, and replace the call with
   error_mark_node before check_conveyor_function_body's own later,
   whole-body walk ever sees it) -- confirmed via direct testing this
   regression is real, not hypothetical: d4324-conveyor-no-unreachable.C
   lost its own specific message and gained a spurious secondary "missing
   return statement" cascade before this exemption was added.  */

bool
is_std_unreachable_fndecl_p (tree fn)
{
  return fn && DECL_NAME (fn) && id_equal (DECL_NAME (fn), "unreachable")
	 && decl_in_std_namespace_p (fn);
}

/* D4324/P2680 std::is_object_address definite-assignment walker.

   Runs at finish_function-adjacent (pre-genericize) timing, alongside
   check_conveyor_function_body -- see resolve_object_address_in_function
   below for why: it must resolve every is_object_address(...) call to a
   literal `true` (or emit a hard error) before build_contract_check's
   later, genericization-time outlining (get_or_build_predicate_core_function)
   ever runs, because that outlining copies captured locals into a
   separate FUNCTION_DECL with no traceable link back to the original
   assignment history -- the only point the real provenance is visible
   at all is here, in the declaring function's own pre-genericize body.

   This first increment implements the core: recognition, the
   well-formedness gate (is_object_address only legal inside a
   conveyor, non-ignored predicate), the this/&obj base case,
   straight-line reaching-definition tracing, the if/else merge rule,
   and contract_assert (both as an obligation to discharge and as a
   fact source for later code in the same function). Not yet
   implemented: the loop-header merge rule (loops are conservatively
   treated as "invalidate anything reassigned inside"), recursing into
   immediately-invoked lambdas, precondition-of-self and
   postcondition-of-callee as fact sources, the call-site
   precondition-obligation mechanism, and folding in the stage-1
   deferred flow-sensitive restrictions -- each a follow-on increment.  */

/* Maps a VAR_DECL/PARM_DECL to whether its value, as of the walker's
   current position in a forward walk over the function body, is known
   to satisfy is_object_address.  Absence means "not known" (treated the
   same as false) -- this is a "must be provable, else treated as
   unprovable" lattice, the same discipline is_ignored/constify/
   is_conveyor already use.  */

/* D4324/P2680 item 8, Increment E1: a provable value-range fact for an
   integer-typed decl, or a pointer-typed decl's provable offset (in
   elements) into a named array -- see oa_env's own m_range_map comment
   below for why these two are unified into one representation rather
   than tracked as two separate maps.  BASE is NULL_TREE for a plain
   integer range; a VAR_DECL of ARRAY_TYPE for a pointer's tracked
   offset into that array.  HAS_LO/HAS_HI false means unbounded in that
   direction (e.g. a fact established by 'i < N' alone has no lower
   bound at all).  Bounds are widest_int (arbitrary precision, the same
   utility real value-range passes use, via the wi:: namespace already
   available everywhere in gcc/cp through coretypes.h) rather than
   HOST_WIDE_INT, so interval arithmetic never has to separately worry
   about overflow of the bound-tracking machinery itself, independent
   of the actual integer type being reasoned about.

   Default member initializers give LO/HI a defined value even when
   HAS_LO/HAS_HI are false and no bound was ever assigned -- without
   them, a default-constructed instance leaves widest_int's own
   internal storage uninitialized, which -Wmaybe-uninitialized
   correctly flags at every copy/destroy of an oa_range_fact that
   was never fully assigned (e.g. oa_collect_contract_field_ranges's
   own fresh oa_symbolic_field_group, or oa_env::deriv_merge_with's
   own early-continue path) -- LO/HI are still never meant to be
   *read* while their own HAS_* is false, this only makes copying and
   destroying a not-fully-established fact well-defined.  */

struct oa_range_fact
{
  tree base = NULL_TREE;
  bool has_lo = false, has_hi = false;
  widest_int lo = 0, hi = 0;
};

/* -fcontract-symbolic-proofs: a symbolic contract's own established
   fact -- "PRED_FN holds (POLARITY true) or its negation holds
   (POLARITY false) for this object identity", e.g. for
   'post<ctrl>(is_opened(this))', PRED_FN is the FUNCTION_DECL for
   'is_opened' and POLARITY is true.  Unlike oa_range_fact (a numeric
   interval, meaningful even partially known via has_lo/has_hi alone),
   this fact only ever means anything as a whole -- there's no partial
   "known lower bound" analogue -- so it's carried by value the same
   way, but merged by full agreement only (oa_env::predicate_fact_merge_with),
   not by combining partial information.  */

struct oa_predicate_fact
{
  tree pred_fn;
  bool polarity;
  /* True if the specific contract that established this fact was
     conveyor-active -- see oa_env_predicate_result's own REQUIRE_CONVEYOR
     parameter for why this matters: a fact a purely-axiomatic symbolic
     contract established must never satisfy a conveyor obligation (which
     is supposed to be backed by real UB-freedom verification), while the
     reverse (conveyor feeding symbolic) is fine.  */
  bool conveyor_established;
};

/* A *relational* fact between two decls -- "LHS CODE RHS holds", e.g.
   for 'pre<ctrl>(x < q)', LHS is x, CODE is LT_EXPR, RHS is q. Unlike
   oa_range_fact (a numeric interval against a literal bound), this
   never resolves either side to a value at all -- see oa_match_
   comparison_against_param's own comment for why. Structurally
   identical in every other way to oa_predicate_fact immediately above
   (whole-fact merge by full agreement, same CONVEYOR_ESTABLISHED
   one-way-trust tag, same rationale), just keyed on a second decl
   instead of a FUNCTION_DECL/polarity pair.  */

/* OFFSET (D4324 Commit 2, generalized to an interval in Commit 4): the
   fact actually holds for '(LHS - OFFSET) CODE RHS', not literally 'LHS
   CODE RHS' -- established facts always have OFFSET an exact-zero point
   interval (an actually-observed comparison, not a derived one); a
   later 'LHS2 = LHS +/- k' shifts a *copy* of the fact for LHS2 by
   accumulating k into OFFSET. OFFSET is an interval (reusing oa_range_
   fact itself, BASE always NULL_TREE here, the same way a plain scalar
   range already uses that struct without a base) rather than a single
   widest_int, so K need not itself be a compile-time constant -- it can
   come from a second tracked variable's own established range, per
   Commit 4 (a fixed K is just this interval's own degenerate has_lo &&
   has_hi && lo == hi case). Tracked this way (rather than folding the
   shift into RHS, which is symbolic for the call-relational shapes and
   so can't absorb a numeric shift) so the same representation covers
   both decl-keyed relational-fact structs uniformly. See oa_get_
   relational's own comment for the transfer function, and oa_env_check_
   relational_fact_1's own comment for why OFFSET's own bounds, not just
   CODE, must be checked at consult time.  */
struct oa_relational_fact
{
  tree_code code;
  tree rhs;
  oa_range_fact offset;
  bool conveyor_established;
};

/* The call analogue of oa_relational_fact immediately above -- "LHS
   CODE RECEIVER.CALLEE () holds", e.g. for 'pre<ctrl>(i < v.size ())',
   LHS is i, CODE is LT_EXPR, RHS is the (RECEIVER, CALLEE) pair (v,
   size) rather than a single decl RHS -- a plain oa_relational_fact's
   single 'tree rhs' has nowhere to put the second component, so this is
   a distinct struct rather than a reused one, unlike m_contract_call_
   range_map's own reuse of oa_contract_field_range_fact (that reuse
   works because a FIELD_DECL and a FUNCTION_DECL can never collide as
   the second half of the exact same composite key; this shape has no
   analogous single existing field to overload).  */

struct oa_call_relational_fact
{
  tree_code code;
  tree rhs_receiver;
  tree rhs_callee;
  /* See oa_relational_fact's own comment on OFFSET.  */
  oa_range_fact offset;
  bool conveyor_established;
};

/* The call-vs-call analogue of oa_call_relational_fact immediately
   above -- "LHS_RECEIVER.LHS_CALLEE () CODE RHS_RECEIVER.RHS_CALLEE ()
   holds", e.g. for 'pre<ctrl>(v.size () < w.size ())". Unlike oa_call_
   relational_fact (keyed on a single decl, LHS is implicit), this
   shape's own LHS is itself a call, so it can't be the map key by
   itself either -- m_call_call_relational_map (below) is keyed on the
   *pair* (lhs_receiver identity, lhs_callee) instead, via the same
   oa_field_key_hash idiom m_contract_call_range_map already uses; this
   struct is the value, holding only the RHS side (the LHS is the key).  */

/* D4324 Commit 2: unlike oa_relational_fact/oa_call_relational_fact,
   this struct deliberately does NOT gain an OFFSET field. Those two are
   keyed on a plain decl, so 'decl2 = decl +/- k' meaningfully shifts a
   copy of decl's own fact for decl2. This one is keyed on (receiver
   identity, callee) -- its own "left-hand side" is an object's call,
   not an arithmetic-shiftable scalar, so there is no assignment that
   plays the analogous role: shifting an object's own identity by an
   integer constant would be pointer arithmetic on the object, not value
   arithmetic on an index, and isn't a coherent operation on this map's
   key at all.  */
struct oa_call_call_relational_fact
{
  tree_code code;
  tree rhs_receiver;
  tree rhs_callee;
  bool conveyor_established;
};

/* D4324, item 8's overflow check (oa_scan_overflow_in_expr): "this decl
   has, at some point still in scope, been compared as less-than (or
   greater-than) an expression of an integral type no wider than its
   own". Deliberately unlike oa_relational_fact/oa_call_relational_fact,
   this does NOT record the other side's identity, value, or shape at
   all -- only whether *some* suitably-typed witness was ever seen, in
   which direction. That's a strictly weaker fact than a full relational
   fact, but it's also strictly more general on the *witness* side: the
   proof it backs ("this decl's own current value is <= wi::max_value(T)
   - 1", from the type invariant that *any* value of an integral type is
   already <= that type's own max) only ever needs the witness's type,
   never its identity, so it can be established from a bare decl, a
   CALL_EXPR ('i < v.size ()'), or any other expression shape uniformly,
   with no per-shape special-casing and no separate fact family for each
   shape a bound could be written in. See oa_match_type_bounded_
   comparison's own comment for how this gets established, and oa_
   provably_safe_unit_shift_p (near oa_scan_overflow_in_expr) for its
   only consumer.  */
struct oa_type_bound_fact
{
  bool has_upper_witness;
  bool has_lower_witness;
};

/* Composite key for m_contract_field_range_map, (object identity,
   FIELD_DECL) -- pair_hash (hash-traits.h)
   combines two ordinary pointer-hash traits, the same idiom used
   elsewhere in the compiler for a two-tree key (e.g. tree-vectorizer.h's
   own tree_cond_mask_hash).  */
typedef pair_hash<nofree_ptr_hash<tree_node>, nofree_ptr_hash<tree_node>>
  oa_field_key_hash;

/* m_contract_field_range_map's own value type -- RANGE plus the same
   CONVEYOR_ESTABLISHED provenance tag oa_predicate_fact carries, and for
   the same reason (see that struct's own comment).  */
struct oa_contract_field_range_fact
{
  oa_range_fact range;
  bool conveyor_established;
};

/* Forward-declared: oa_derivation's full definition (contract-conveyor-
   proof-provenance's own "why does this range fact hold" node, see its
   own comment further below) isn't needed here, only pointers to it --
   oa_env only ever stores/copies/merges pointers, never builds nodes
   itself (that's oa_provenance_env's job, defined alongside
   oa_derivation below oa_env).  */
struct oa_derivation;

/* Forward-declared for the same reason as oa_derivation just above:
   defined alongside its own oa_range_fact_exact/_negate/_accumulate
   siblings, much further below, but oa_env's own relational_merge_with/
   call_relational_merge_with (right here) need it before that point.  */
static bool oa_range_fact_equal (const oa_range_fact &a, const oa_range_fact &b);

class oa_env
{
public:
  bool provable_p (tree decl)
  {
    bool *v = m_map.get (decl);
    return v && *v;
  }
  void set (tree decl, bool provable) { m_map.put (decl, provable); }
  void invalidate (tree decl) { m_map.put (decl, false); }

  /* A second, independent per-decl fact -- "provably nonzero" -- for
     item 8's narrow div/mod restriction (see oa_provably_nonzero_p
     below). Kept as a wholly separate map rather than folded into the
     one above: it tracks a different property (integer-valued decls,
     not pointer provenance) and the two are never meaningfully
     conflated, but sharing this class's copy/assign/merge_with
     machinery (the exact same "every incoming value must satisfy it"
     lattice) is exactly what's wanted for it too.  */
  bool nz_provable_p (tree decl)
  {
    bool *v = m_nz_map.get (decl);
    return v && *v;
  }
  void nz_set (tree decl, bool provable) { m_nz_map.put (decl, provable); }
  void nz_invalidate (tree decl) { m_nz_map.put (decl, false); }

  /* -fcontract-symbolic-proofs: symbolic-only counterparts of m_map/
     m_nz_map above, established *only* when the specific establishing
     contract is symbolic-active (never conveyor-active) -- see
     .claude/plans/well-we-last-discussed-ethereal-duckling.md. Kept
     entirely separate from m_map/m_nz_map, rather than folded in with
     an extra provenance tag the way m_predicate_fact_map/m_contract_
     field_range_map were, because those two are read-only from the
     conveyor side (m_map/m_nz_map must never gain a fact a symbolic
     contract established): a symbolic consult falls back to m_map/
     m_nz_map itself (the allowed direction, conveyor feeding symbolic)
     at its own call site instead, so there is no shared map needing a
     tag here at all. Same "every incoming value must satisfy it"
     AND-of-booleans lattice as m_map/m_nz_map, folded into the same
     merge_with below.  */
  bool symbolic_object_address_provable_p (tree decl)
  {
    bool *v = m_symbolic_object_address_map.get (decl);
    return v && *v;
  }
  void symbolic_object_address_set (tree decl, bool provable)
  {
    m_symbolic_object_address_map.put (decl, provable);
  }
  void symbolic_object_address_invalidate (tree decl)
  {
    m_symbolic_object_address_map.put (decl, false);
  }
  bool symbolic_nz_provable_p (tree decl)
  {
    bool *v = m_symbolic_nz_map.get (decl);
    return v && *v;
  }
  void symbolic_nz_set (tree decl, bool provable)
  {
    m_symbolic_nz_map.put (decl, provable);
  }
  void symbolic_nz_invalidate (tree decl)
  {
    m_symbolic_nz_map.put (decl, false);
  }

  /* A third, independent per-decl fact -- a provable value range,
     unified across "plain integer range" and "pointer's offset into a
     named array" (see oa_range_fact's own comment) -- for item 8's
     Increment E1/E2 (symbolic range analysis for the array-bound
     rule). Unlike the two boolean maps above, absence means
     "unconstrained," and merging is by *union* of intervals, not AND
     of booleans -- a genuinely different lattice, so this needs its
     own range_merge_with rather than fitting into the existing
     merge_with's shape.  */
  bool range_get (tree decl, oa_range_fact *out)
  {
    oa_range_fact *v = m_range_map.get (decl);
    if (!v)
      return false;
    *out = *v;
    return true;
  }
  void range_set (tree decl, const oa_range_fact &fact)
  {
    m_range_map.put (decl, fact);
  }
  void range_invalidate (tree decl) { m_range_map.remove (decl); }

  /* Another independent per-decl fact -- see oa_predicate_fact's own
     comment.  A shared substrate: populated whenever a contract is
     conveyor-active *or* symbolic-active (oa_contract_fact_tracking_
     active_p), so it's always empty -- and therefore free -- unless at
     least one of -fcontract-conveyor-proofs/-fcontract-symbolic-proofs
     is active, or a plugin is driving oa_walk_function_calls.
     predicate_fact_merge_with mirrors range_merge_with's own "keep only if
     both sides agree" shape, but requires full agreement (same
     PRED_FN, same POLARITY), not partial-bound combination -- there is
     no meaningful "weaker combined fact" for two different named-
     predicate claims the way there is for two numeric intervals.
     CONVEYOR_ESTABLISHED is merged by AND, matching HAS_LO/HAS_HI's own
     "only claim what holds on every path" discipline elsewhere in this
     file: a fact conveyor-established on only one of two joining
     branches must not be treated as conveyor-established after the
     join.  */
  bool predicate_fact_get (tree decl, oa_predicate_fact *out)
  {
    oa_predicate_fact *v = m_predicate_fact_map.get (decl);
    if (!v)
      return false;
    *out = *v;
    return true;
  }
  void predicate_fact_set (tree decl, tree pred_fn, bool polarity,
			    bool conveyor_established)
  {
    oa_predicate_fact fact;
    fact.pred_fn = pred_fn;
    fact.polarity = polarity;
    fact.conveyor_established = conveyor_established;
    m_predicate_fact_map.put (decl, fact);
  }
  void predicate_fact_invalidate (tree decl) { m_predicate_fact_map.remove (decl); }
  void predicate_fact_merge_with (oa_env &other)
  {
    auto_vec<tree> to_remove;
    auto_vec<tree> to_keep;
    auto_vec<oa_predicate_fact> kept_facts;
    for (auto it : m_predicate_fact_map)
      {
	oa_predicate_fact *ov = other.m_predicate_fact_map.get (it.first);
	if (!ov || ov->pred_fn != it.second.pred_fn
	    || ov->polarity != it.second.polarity)
	  {
	    to_remove.safe_push (it.first);
	    continue;
	  }
	oa_predicate_fact merged = it.second;
	merged.conveyor_established
	  = it.second.conveyor_established && ov->conveyor_established;
	to_keep.safe_push (it.first);
	kept_facts.safe_push (merged);
      }
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_predicate_fact_map.remove (to_remove[i]);
    for (unsigned i = 0; i < to_keep.length (); ++i)
      m_predicate_fact_map.put (to_keep[i], kept_facts[i]);
  }

  /* Fixes a soundness gap in m_predicate_fact_map/m_contract_field_
     range_map's own invalidation: both are invalidated (Rule 2, see
     oa_invalidate_symbolic_facts_for_call_args) keyed on a tracked
     pointer's own syntactic decl identity, not on what it points to,
     so 'file *q = p; mutate (q);' never invalidates a fact established
     via 'p' even though q and p name the same object -- confirmed via
     direct testing to be a real, silent unsoundness for -fcontract-
     symbolic-proofs specifically (no runtime backstop there, unlike
     conveyor). M_ALIAS_TARGET[d] = target means "d currently holds the
     same pointer value as canonical decl TARGET"; TARGET itself never
     has its own entry (every write stores the fully-resolved target,
     so alias_find is always a single lookup, never a chase). Every
     other identity-resolution/establish/consult/invalidate call site
     for the two maps above canonicalizes through alias_find first, so
     'q' and 'p' share the same map key once aliased.

     Deliberately NOT a union-find/DSU: an earlier version of this fix
     used one, merged across branches by unioning edges (matching
     m_shadow_decls_merge_with's own "union, once true always true"
     semantics) -- rejected after review found it unsound. DSU's own
     union(a,b) operates on *roots*, so reassigning any variable that
     once shared history with a component permanently fuses that whole
     component with whatever the variable is reassigned to next: 'file
     *q = p1; q = p2;' would incorrectly fuse p1 and p2's own
     identities together forever, even though q never simultaneously
     aliased both. And "current alias target" isn't a monotonic
     property the way shadow existence is -- it changes on every
     reassignment of the aliasing decl itself -- so union-only merging
     also fails the branch-merge case: an alias created on only one arm
     of an 'if' would survive unconditionally past the join. A plain,
     *overwritten* hash_map with agreement-based merging (below) avoids
     both: reassigning q only ever touches q's own entry, and a branch-
     only alias is correctly dropped, not kept, at the join.  */
  tree alias_find (tree d)
  {
    tree *t = m_alias_target.get (d);
    return t ? *t : d;
  }
  void alias_set (tree d, tree target) { m_alias_target.put (d, target); }
  void alias_invalidate (tree d) { m_alias_target.remove (d); }
  /* Agreement-based, mirroring predicate_fact_merge_with exactly: an
     entry survives only if both sides have it and agree on the same
     target, otherwise it's dropped (treated as "no longer known to
     alias anything" going forward) -- never a union, per this
     structure's own comment above.  */
  void alias_merge_with (oa_env &other)
  {
    auto_vec<tree> to_remove;
    for (auto it : m_alias_target)
      {
	tree *ov = other.m_alias_target.get (it.first);
	if (!ov || *ov != it.second)
	  to_remove.safe_push (it.first);
      }
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_alias_target.remove (to_remove[i]);
  }

  /* Stage 2a: the same "current alias target" tracking as m_alias_
     target above, for a struct/class field slot (e.g. 'h.ptr') instead
     of a bare decl -- keyed on (base_identity, FIELD_DECL), the same
     composite key shape m_contract_field_range_map already uses via
     oa_field_key_hash. Unlike alias_find, field_alias_find has no
     "identity of last resort" to fall back to: a COMPONENT_REF is not
     itself an interned decl the way a VAR_DECL is (two syntactically
     identical 'h.ptr' occurrences are two different tree nodes), so
     the (base, field) pair is the only possible stable cross-statement
     key, and returning NULL_TREE (rather than some synthesized "self"
     identity) when a slot was never recorded is the only structurally
     coherent answer, not just the conservative one. See oa_field_slot_
     identity's own comment for how this map actually gets consulted --
     deliberately not folded into oa_object_identity_decl itself (see
     that comment for why: this file's own Rule 1 dispatch relies on
     that function's true/false return as a control-flow discriminator,
     which teaching it to resolve COMPONENT_REF would silently break).  */
  tree *field_alias_find (tree base, tree field)
  {
    return m_field_alias_target.get ({base, field});
  }
  void field_alias_set (tree base, tree field, tree target)
  {
    m_field_alias_target.put ({base, field}, target);
  }
  void field_alias_invalidate (tree base, tree field)
  {
    m_field_alias_target.remove ({base, field});
  }
  /* Mirrors contract_field_range_invalidate_all's own removal-by-
     first-key-component sweep exactly -- BASE's own reassignment or
     escape must drop every field-slot alias recorded for it too.  */
  void field_alias_invalidate_all (tree base)
  {
    auto_vec<std::pair<tree, tree>> to_remove;
    for (auto it : m_field_alias_target)
      if (it.first.first == base)
	to_remove.safe_push (it.first);
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_field_alias_target.remove (to_remove[i]);
  }
  /* Agreement-based, mirroring alias_merge_with exactly (never a
     union -- see that method's own comment for why).  */
  void field_alias_merge_with (oa_env &other)
  {
    auto_vec<std::pair<tree, tree>> to_remove;
    for (auto it : m_field_alias_target)
      {
	tree *ov = other.m_field_alias_target.get (it.first);
	if (!ov || *ov != it.second)
	  to_remove.safe_push (it.first);
      }
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_field_alias_target.remove (to_remove[i]);
  }

  /* Stage 2b: the same "current alias target" tracking as m_field_
     alias_target above, for a named array's own slot (e.g. 'arr[0]' or
     the semantically identical '*arr') instead of a struct field.
     Keyed on (base_identity, HOST_WIDE_INT) rather than (base_identity,
     FIELD_DECL): an array index, unlike a FIELD_DECL, isn't a small,
     finite, already-interned identity safely comparable by pointer, so
     the actual integer value is extracted (via oa_array_index_
     constant) and used as the key directly, rather than keying on the
     INTEGER_CST tree itself. int_hash<HOST_WIDE_INT, -1, -2> is the
     same precedented pattern coroutine-passes.cc's own destinations
     map already uses; the -1/-2 sentinels are never actually consulted
     here (pair_hash's own is_empty/is_deleted/mark_empty/mark_deleted
     all delegate solely to the first, tree-pointer component -- this
     is why field_alias's own base/FIELD_DECL key shape works safely
     too), so restricting this map to non-negative indices is purely
     about not bothering to track an already-UB index, not about
     sentinel collisions. Same "no identity of last resort" absence of
     a fallback as field_alias_find, for the same reason: an array-slot
     expression isn't an interned decl either. See oa_array_slot_
     identity's own comment for how this map is actually consulted --
     deliberately a new, separate resolver rather than an extension of
     oa_object_identity_decl, for the identical Rule-1-dispatch reason
     field_alias's own comment explains.  */
  tree *array_alias_find (tree base, HOST_WIDE_INT index)
  {
    return m_array_alias_target.get ({base, index});
  }
  void array_alias_set (tree base, HOST_WIDE_INT index, tree target)
  {
    m_array_alias_target.put ({base, index}, target);
  }
  void array_alias_invalidate (tree base, HOST_WIDE_INT index)
  {
    m_array_alias_target.remove ({base, index});
  }
  /* Mirrors field_alias_invalidate_all's own removal-by-first-key-
     component sweep -- also the rule an unprovable-index write itself
     uses (see the Rule 1 write-detection site's own comment): BASE's
     own reassignment/escape, or a write through it with an index that
     could be any slot, must drop every array-slot alias recorded for
     it.  */
  void array_alias_invalidate_all (tree base)
  {
    auto_vec<std::pair<tree, HOST_WIDE_INT>> to_remove;
    for (auto it : m_array_alias_target)
      if (it.first.first == base)
	to_remove.safe_push (it.first);
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_array_alias_target.remove (to_remove[i]);
  }
  /* Agreement-based, mirroring field_alias_merge_with exactly (never a
     union -- see alias_merge_with's own comment for why).  */
  void array_alias_merge_with (oa_env &other)
  {
    auto_vec<std::pair<tree, HOST_WIDE_INT>> to_remove;
    for (auto it : m_array_alias_target)
      {
	tree *ov = other.m_array_alias_target.get (it.first);
	if (!ov || *ov != it.second)
	  to_remove.safe_push (it.first);
      }
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_array_alias_target.remove (to_remove[i]);
  }

  /* Stage 5: '&h->f'/'&h.f' (the address of a struct/class member, of
     any type) names a fixed, permanent sub-object -- unlike a field's
     own *value* (what m_field_alias_target tracks), there is nothing to
     "look up" here: for a given base object, '&h.f' always refers to
     the exact same storage for its whole lifetime, the same way '&f'
     and a plain decl 'f' already share one identity (Stage 1's own
     widened scope). Since m_predicate_fact_map is keyed by a single
     tree, and two syntactically identical '&h->f' occurrences are two
     different, non-interned tree nodes, a stable, synthesized
     placeholder tree is created once per (base_identity, FIELD_DECL)
     pair and reused for every later occurrence naming the same slot --
     plugged into the *existing*, unchanged m_predicate_fact_map, not a
     new parallel map: every establish/consult/invalidate site keeps
     working unmodified once handed this key, the same shape oa_field_
     slot_identity/oa_array_slot_identity already produce for that map.

     M_FIELD_OBJECT_KEY is deliberately a *pointer*, shared (never
     deep-copied) across every oa_env instance analyzing one function --
     unlike every map above, which is copied by value at every branch
     point. If it were an ordinary, per-instance member, two sibling
     branches that each, independently, first need a key for the same
     (base, field) pair (never yet cached in their shared parent) would
     synthesize two *different* placeholder trees for the same logical
     slot, silently breaking predicate_fact_merge_with's own pointer-
     identity agreement check forever after for that field. Allocated
     once, before the first oa_env for a given function exists
     (oa_resolve_object_address_in_function_1's own local, address-
     taken hash_map), and explicitly propagated -- never freshly
     allocated -- into the only other two functions that construct a
     fresh oa_env of their own for a nested walk (oa_resolve_iile_call/
     oa_resolve_iile_range): found and fixed during this feature's own
     design review, which caught that an otherwise-uninitialized fresh
     inner_env used inside a parameterless IILE (e.g. 'g (holder *h) {
     [&]{ open_it (&h->f); }(); }') would otherwise dereference a null
     cache the first time '&h->f' is seen inside such a closure.

     No new merge function is needed: the cache itself is a pure,
     permanent, append-only mapping (a synthesized key, once created for
     a given (base, field) pair, is never removed or regenerated -- only
     the *fact* recorded under it in m_predicate_fact_map is ever
     invalidated), and predicate_fact_merge_with already works on any
     tree key regardless of its own origin, so entries reached through a
     synthetic key merge correctly for free.  */
  tree field_object_identity_key (tree base, tree field)
  {
    tree *existing = m_field_object_key->get ({base, field});
    if (existing)
      return *existing;
    tree key = build_decl (UNKNOWN_LOCATION, VAR_DECL, NULL_TREE,
			    ptr_type_node);
    m_field_object_key->put ({base, field}, key);
    return key;
  }
  void field_object_predicate_invalidate (tree base, tree field)
  {
    tree *key = m_field_object_key->get ({base, field});
    if (key)
      predicate_fact_invalidate (*key);
  }
  /* Mirrors field_alias_invalidate_all's own removal-by-first-key-
     component sweep, but against m_predicate_fact_map (via each cached
     key) rather than against this cache itself -- the cache's own
     entries are permanent (see the comment above); only the *fact*
     recorded under a given key is ever invalidated.  */
  void field_object_predicate_invalidate_all (tree base)
  {
    for (auto it : *m_field_object_key)
      if (it.first.first == base)
	predicate_fact_invalidate (it.second);
  }
  hash_map<oa_field_key_hash, tree> *field_object_key_cache ()
  {
    return m_field_object_key;
  }
  void set_field_object_key_cache (hash_map<oa_field_key_hash, tree> *cache)
  {
    m_field_object_key = cache;
  }

  /* A shared substrate, same gating and shape as m_predicate_fact_map
     above, for oa_relational_fact ("LHS CODE RHS holds") instead of a
     named-predicate call.  relational_merge_with mirrors predicate_
     fact_merge_with exactly: full agreement (same CODE, same RHS) or
     the fact is dropped at the join, and CONVEYOR_ESTABLISHED merges
     by AND.  */
  bool relational_get (tree decl, oa_relational_fact *out)
  {
    oa_relational_fact *v = m_relational_map.get (decl);
    if (!v)
      return false;
    *out = *v;
    return true;
  }
  void relational_set (tree decl, tree_code code, tree rhs,
			bool conveyor_established, oa_range_fact offset)
  {
    oa_relational_fact fact;
    fact.code = code;
    fact.rhs = rhs;
    fact.offset = offset;
    fact.conveyor_established = conveyor_established;
    m_relational_map.put (decl, fact);
  }
  void relational_invalidate (tree decl) { m_relational_map.remove (decl); }
  /* Rule 2 (see oa_predicate_fact's own invalidation callers): a fact
     about a stale value on *either* side is no longer valid, so this
     also drops every fact whose own RHS is DECL, not just an entry
     keyed on DECL itself.  */
  void relational_invalidate_involving (tree decl)
  {
    m_relational_map.remove (decl);
    auto_vec<tree> to_remove;
    for (auto it : m_relational_map)
      if (it.second.rhs == decl)
	to_remove.safe_push (it.first);
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_relational_map.remove (to_remove[i]);
  }
  void relational_merge_with (oa_env &other)
  {
    auto_vec<tree> to_remove;
    auto_vec<tree> to_keep;
    auto_vec<oa_relational_fact> kept_facts;
    for (auto it : m_relational_map)
      {
	oa_relational_fact *ov = other.m_relational_map.get (it.first);
	if (!ov || ov->code != it.second.code || ov->rhs != it.second.rhs
	    || !oa_range_fact_equal (ov->offset, it.second.offset))
	  {
	    to_remove.safe_push (it.first);
	    continue;
	  }
	oa_relational_fact merged = it.second;
	merged.conveyor_established
	  = it.second.conveyor_established && ov->conveyor_established;
	to_keep.safe_push (it.first);
	kept_facts.safe_push (merged);
      }
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_relational_map.remove (to_remove[i]);
    for (unsigned i = 0; i < to_keep.length (); ++i)
      m_relational_map.put (to_keep[i], kept_facts[i]);
  }

  /* oa_type_bound_fact's own get/set/invalidate/merge_with -- see that
     struct's own comment. No "invalidate_involving" variant, unlike
     relational_get/set's own family: the other side's identity is never
     recorded in the first place, so nothing could ever need to sweep
     this map for entries referencing a just-reassigned decl on their
     own RHS the way relational_invalidate_involving does. Merge is per-
     field AND (a witness survives a join only if both incoming paths
     independently have it), the same "union of independently-AND'd
     booleans" shape range_merge_with already uses for has_lo/has_hi,
     just without that function's own BASE-equality gate (this struct
     has no base to compare).  */
  bool type_bound_get (tree decl, oa_type_bound_fact *out)
  {
    oa_type_bound_fact *v = m_type_bound_map.get (decl);
    if (!v)
      return false;
    *out = *v;
    return true;
  }
  void type_bound_set (tree decl, bool upper, bool lower)
  {
    oa_type_bound_fact *v = m_type_bound_map.get (decl);
    oa_type_bound_fact fact = v ? *v : oa_type_bound_fact { false, false };
    fact.has_upper_witness |= upper;
    fact.has_lower_witness |= lower;
    m_type_bound_map.put (decl, fact);
  }
  void type_bound_invalidate (tree decl) { m_type_bound_map.remove (decl); }
  void type_bound_merge_with (oa_env &other)
  {
    auto_vec<tree> to_remove;
    auto_vec<tree> to_keep;
    auto_vec<oa_type_bound_fact> kept_facts;
    for (auto it : m_type_bound_map)
      {
	oa_type_bound_fact *ov = other.m_type_bound_map.get (it.first);
	oa_type_bound_fact merged;
	merged.has_upper_witness
	  = it.second.has_upper_witness && ov && ov->has_upper_witness;
	merged.has_lower_witness
	  = it.second.has_lower_witness && ov && ov->has_lower_witness;
	if (!merged.has_upper_witness && !merged.has_lower_witness)
	  to_remove.safe_push (it.first);
	else
	  {
	    to_keep.safe_push (it.first);
	    kept_facts.safe_push (merged);
	  }
      }
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_type_bound_map.remove (to_remove[i]);
    for (unsigned i = 0; i < to_keep.length (); ++i)
      m_type_bound_map.put (to_keep[i], kept_facts[i]);
  }

  /* The call-relational analogue of relational_get/set/invalidate/
     merge_with immediately above, for oa_call_relational_fact instead
     of oa_relational_fact -- see that struct's own comment for why this
     is a separate map rather than a reuse.  */
  bool call_relational_get (tree decl, oa_call_relational_fact *out)
  {
    oa_call_relational_fact *v = m_call_relational_map.get (decl);
    if (!v)
      return false;
    *out = *v;
    return true;
  }
  void call_relational_set (tree decl, tree_code code, tree rhs_receiver,
			      tree rhs_callee, bool conveyor_established,
			      oa_range_fact offset)
  {
    oa_call_relational_fact fact;
    fact.code = code;
    fact.rhs_receiver = rhs_receiver;
    fact.rhs_callee = rhs_callee;
    fact.offset = offset;
    fact.conveyor_established = conveyor_established;
    m_call_relational_map.put (decl, fact);
  }
  void call_relational_invalidate (tree decl)
  {
    m_call_relational_map.remove (decl);
  }
  /* Rule 2 (see relational_invalidate_involving's own comment): a fact
     about a stale value on *either* side is no longer valid, so this
     also drops every fact whose own RHS_RECEIVER is DECL, not just an
     entry keyed on DECL itself.  */
  void call_relational_invalidate_involving (tree decl)
  {
    m_call_relational_map.remove (decl);
    auto_vec<tree> to_remove;
    for (auto it : m_call_relational_map)
      if (it.second.rhs_receiver == decl)
	to_remove.safe_push (it.first);
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_call_relational_map.remove (to_remove[i]);
  }
  void call_relational_merge_with (oa_env &other)
  {
    auto_vec<tree> to_remove;
    auto_vec<tree> to_keep;
    auto_vec<oa_call_relational_fact> kept_facts;
    for (auto it : m_call_relational_map)
      {
	oa_call_relational_fact *ov = other.m_call_relational_map.get (it.first);
	if (!ov || ov->code != it.second.code
	    || ov->rhs_receiver != it.second.rhs_receiver
	    || ov->rhs_callee != it.second.rhs_callee
	    || !oa_range_fact_equal (ov->offset, it.second.offset))
	  {
	    to_remove.safe_push (it.first);
	    continue;
	  }
	oa_call_relational_fact merged = it.second;
	merged.conveyor_established
	  = it.second.conveyor_established && ov->conveyor_established;
	to_keep.safe_push (it.first);
	kept_facts.safe_push (merged);
      }
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_call_relational_map.remove (to_remove[i]);
    for (unsigned i = 0; i < to_keep.length (); ++i)
      m_call_relational_map.put (to_keep[i], kept_facts[i]);
  }

  /* The call-vs-call analogue of call_relational_get/set/invalidate/
     merge_with immediately above, for oa_call_call_relational_fact keyed
     on (lhs_receiver identity, lhs_callee) instead of a single decl --
     see that struct's own comment for why.  */
  bool call_call_relational_get (tree lhs_receiver, tree lhs_callee,
				    oa_call_call_relational_fact *out)
  {
    oa_call_call_relational_fact *v
      = m_call_call_relational_map.get ({lhs_receiver, lhs_callee});
    if (!v)
      return false;
    *out = *v;
    return true;
  }
  void call_call_relational_set (tree lhs_receiver, tree lhs_callee,
				    tree_code code, tree rhs_receiver,
				    tree rhs_callee, bool conveyor_established)
  {
    oa_call_call_relational_fact fact;
    fact.code = code;
    fact.rhs_receiver = rhs_receiver;
    fact.rhs_callee = rhs_callee;
    fact.conveyor_established = conveyor_established;
    m_call_call_relational_map.put ({lhs_receiver, lhs_callee}, fact);
  }
  void call_call_relational_invalidate (tree lhs_receiver, tree lhs_callee)
  {
    m_call_call_relational_map.remove ({lhs_receiver, lhs_callee});
  }
  /* Rule 2 (see call_relational_invalidate_involving's own comment): a
     fact about a stale value on *either* side is no longer valid, so
     this drops every entry whose own KEY receiver component is DECL (the
     LHS call's receiver, any callee) or whose VALUE's own RHS_RECEIVER is
     DECL (the RHS call's receiver), not just an entry keyed exactly on
     (DECL, some one callee).  */
  void call_call_relational_invalidate_involving (tree decl)
  {
    auto_vec<std::pair<tree, tree>> to_remove;
    for (auto it : m_call_call_relational_map)
      if (it.first.first == decl || it.second.rhs_receiver == decl)
	to_remove.safe_push (it.first);
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_call_call_relational_map.remove (to_remove[i]);
  }
  void call_call_relational_merge_with (oa_env &other)
  {
    auto_vec<std::pair<tree, tree>> to_remove;
    auto_vec<std::pair<tree, tree>> to_keep;
    auto_vec<oa_call_call_relational_fact> kept_facts;
    for (auto it : m_call_call_relational_map)
      {
	oa_call_call_relational_fact *ov
	  = other.m_call_call_relational_map.get (it.first);
	if (!ov || ov->code != it.second.code
	    || ov->rhs_receiver != it.second.rhs_receiver
	    || ov->rhs_callee != it.second.rhs_callee)
	  {
	    to_remove.safe_push (it.first);
	    continue;
	  }
	oa_call_call_relational_fact merged = it.second;
	merged.conveyor_established
	  = it.second.conveyor_established && ov->conveyor_established;
	to_keep.safe_push (it.first);
	kept_facts.safe_push (merged);
      }
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_call_call_relational_map.remove (to_remove[i]);
    for (unsigned i = 0; i < to_keep.length (); ++i)
      m_call_call_relational_map.put (to_keep[i], kept_facts[i]);
  }

  /* A shared substrate, same gating as m_predicate_fact_map above: a
     conveyor- or symbolic-postcondition's own established range for a
     by-value scalar (a precondition's own parameter, or a
     postcondition's own return-value binder assigned to a decl),
     tracked purely at compile time so a later call's precondition can
     be checked against it with a diagnostic -- static-prover analogue
     of Mechanism B's runtime bare-scalar range shadow (oa_call_
     symbolic_range_p / oa_precondition_symbolic_ranges, built for
     -fcontract-symbolic-runtime-checks), which stays symbolic-only
     (Mechanism B has no conveyor counterpart).  Merge is intersect-and-
     widen, exactly mirroring range_merge_with (dropping that map's own
     `base`/array-offset handling, which this fact never has).  */
  bool contract_scalar_range_get (tree decl, oa_range_fact *out)
  {
    oa_range_fact *v = m_contract_scalar_range_map.get (decl);
    if (!v)
      return false;
    *out = *v;
    return true;
  }
  void contract_scalar_range_set (tree decl, const oa_range_fact &fact)
  {
    m_contract_scalar_range_map.put (decl, fact);
  }
  void contract_scalar_range_invalidate (tree decl)
  {
    m_contract_scalar_range_map.remove (decl);
  }
  void contract_scalar_range_merge_with (oa_env &other)
  {
    auto_vec<tree> to_remove;
    auto_vec<tree> to_keep;
    auto_vec<oa_range_fact> kept_facts;
    for (auto it : m_contract_scalar_range_map)
      {
	oa_range_fact *ov = other.m_contract_scalar_range_map.get (it.first);
	if (!ov)
	  {
	    to_remove.safe_push (it.first);
	    continue;
	  }
	oa_range_fact merged;
	merged.base = NULL_TREE;
	merged.has_lo = it.second.has_lo && ov->has_lo;
	merged.has_hi = it.second.has_hi && ov->has_hi;
	if (merged.has_lo)
	  merged.lo = wi::smin (it.second.lo, ov->lo);
	if (merged.has_hi)
	  merged.hi = wi::smax (it.second.hi, ov->hi);
	to_keep.safe_push (it.first);
	kept_facts.safe_push (merged);
      }
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_contract_scalar_range_map.remove (to_remove[i]);
    for (unsigned i = 0; i < to_keep.length (); ++i)
      m_contract_scalar_range_map.put (to_keep[i], kept_facts[i]);
  }

  /* A shared substrate, same gating as m_predicate_fact_map above: a
     conveyor- or symbolic-postcondition's own established range for a
     persistent pointer's own field, keyed by (object identity, FIELD_
     DECL) rather than a single decl, since one object can have several
     independently-tracked fields -- static-prover analogue of
     Mechanism A's runtime ptr->field range shape (oa_symbolic_
     comparison_conjunct_shape, built for -fcontract-symbolic-runtime-
     checks), which stays symbolic-only (Mechanism A has no conveyor
     counterpart).  Same intersect-and-widen merge discipline as
     contract_scalar_range_merge_with, just keyed on the pair, plus the
     same CONVEYOR_ESTABLISHED provenance tag (merged by AND) oa_
     predicate_fact's own map carries, and for the same reason.  */
  bool contract_field_range_get (tree identity, tree field,
				  oa_contract_field_range_fact *out)
  {
    oa_contract_field_range_fact *v
      = m_contract_field_range_map.get ({identity, field});
    if (!v)
      return false;
    *out = *v;
    return true;
  }
  void contract_field_range_set (tree identity, tree field,
				  const oa_range_fact &range,
				  bool conveyor_established)
  {
    oa_contract_field_range_fact fact = { range, conveyor_established };
    m_contract_field_range_map.put ({identity, field}, fact);
  }
  void contract_field_range_invalidate (tree identity, tree field)
  {
    m_contract_field_range_map.remove ({identity, field});
  }
  /* Drop every tracked field for IDENTITY at once -- a reassignment of
     the whole object, or a call taking its address, invalidates all of
     its fields, not just whichever one happens to already be tracked
     (mirrors predicate_fact_invalidate's own whole-object granularity).  */
  void contract_field_range_invalidate_all (tree identity)
  {
    auto_vec<std::pair<tree, tree>> to_remove;
    for (auto it : m_contract_field_range_map)
      if (it.first.first == identity)
	to_remove.safe_push (it.first);
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_contract_field_range_map.remove (to_remove[i]);
  }
  void contract_field_range_merge_with (oa_env &other)
  {
    auto_vec<std::pair<tree, tree>> to_remove;
    auto_vec<std::pair<tree, tree>> to_keep;
    auto_vec<oa_contract_field_range_fact> kept_facts;
    for (auto it : m_contract_field_range_map)
      {
	oa_contract_field_range_fact *ov
	  = other.m_contract_field_range_map.get (it.first);
	if (!ov)
	  {
	    to_remove.safe_push (it.first);
	    continue;
	  }
	oa_contract_field_range_fact merged;
	merged.range.base = NULL_TREE;
	merged.range.has_lo = it.second.range.has_lo && ov->range.has_lo;
	merged.range.has_hi = it.second.range.has_hi && ov->range.has_hi;
	if (merged.range.has_lo)
	  merged.range.lo = wi::smin (it.second.range.lo, ov->range.lo);
	if (merged.range.has_hi)
	  merged.range.hi = wi::smax (it.second.range.hi, ov->range.hi);
	merged.conveyor_established
	  = it.second.conveyor_established && ov->conveyor_established;
	to_keep.safe_push (it.first);
	kept_facts.safe_push (merged);
      }
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_contract_field_range_map.remove (to_remove[i]);
    for (unsigned i = 0; i < to_keep.length (); ++i)
      m_contract_field_range_map.put (to_keep[i], kept_facts[i]);
  }

  /* The call-range analogue of the ptr->field range map immediately
     above -- keyed by (receiver identity, FUNCTION_DECL) rather than
     (identity, FIELD_DECL), for a call to a DECL_DECLARED_CONVEYOR_P
     accessor (e.g. 'v.size ()') used in a comparison, the same way a
     field access is.  oa_field_key_hash/oa_contract_field_range_fact are
     both already fully generic over any two tree pointers/a plain range
     -- nothing field-specific in either -- so they're reused as-is
     rather than duplicated under a new name.  Same shared substrate,
     same provenance-tag discipline, for the same reasons: see oa_
     call_range_conjunct_shape's own comment for why the accessor itself
     must be conveyor-declared (never symbolic-declared).  */
  bool contract_call_range_get (tree identity, tree callee,
				  oa_contract_field_range_fact *out)
  {
    oa_contract_field_range_fact *v
      = m_contract_call_range_map.get ({identity, callee});
    if (!v)
      return false;
    *out = *v;
    return true;
  }
  void contract_call_range_set (tree identity, tree callee,
				  const oa_range_fact &range,
				  bool conveyor_established)
  {
    oa_contract_field_range_fact fact = { range, conveyor_established };
    m_contract_call_range_map.put ({identity, callee}, fact);
  }
  void contract_call_range_invalidate (tree identity, tree callee)
  {
    m_contract_call_range_map.remove ({identity, callee});
  }
  /* Drop every tracked call-range fact for IDENTITY at once -- mirrors
     contract_field_range_invalidate_all's own whole-object granularity,
     for the same reason (a reassignment or potentially-mutating exposure
     of the whole object invalidates everything tracked about it, not
     just whichever single callee happens to already be tracked).  */
  void contract_call_range_invalidate_all (tree identity)
  {
    auto_vec<std::pair<tree, tree>> to_remove;
    for (auto it : m_contract_call_range_map)
      if (it.first.first == identity)
	to_remove.safe_push (it.first);
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_contract_call_range_map.remove (to_remove[i]);
  }
  void contract_call_range_merge_with (oa_env &other)
  {
    auto_vec<std::pair<tree, tree>> to_remove;
    auto_vec<std::pair<tree, tree>> to_keep;
    auto_vec<oa_contract_field_range_fact> kept_facts;
    for (auto it : m_contract_call_range_map)
      {
	oa_contract_field_range_fact *ov
	  = other.m_contract_call_range_map.get (it.first);
	if (!ov)
	  {
	    to_remove.safe_push (it.first);
	    continue;
	  }
	oa_contract_field_range_fact merged;
	merged.range.base = NULL_TREE;
	merged.range.has_lo = it.second.range.has_lo && ov->range.has_lo;
	merged.range.has_hi = it.second.range.has_hi && ov->range.has_hi;
	if (merged.range.has_lo)
	  merged.range.lo = wi::smin (it.second.range.lo, ov->range.lo);
	if (merged.range.has_hi)
	  merged.range.hi = wi::smax (it.second.range.hi, ov->range.hi);
	merged.conveyor_established
	  = it.second.conveyor_established && ov->conveyor_established;
	to_keep.safe_push (it.first);
	kept_facts.safe_push (merged);
      }
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_contract_call_range_map.remove (to_remove[i]);
    for (unsigned i = 0; i < to_keep.length (); ++i)
      m_contract_call_range_map.put (to_keep[i], kept_facts[i]);
  }

  /* -fcontract-conveyor-proof-provenance only: a fourth, independent
     per-decl map, "why is DECL's range fact what it is" -- pointers
     only (oa_derivation nodes themselves are owned and allocated
     elsewhere, see oa_provenance_env below), copied/assigned by value
     exactly like the three maps above so it follows an oa_env copy
     (e.g. a branch's own then_env/else_env) automatically. Always empty
     -- and therefore free -- unless -fcontract-conveyor-proof-provenance
     is active; see oa_active_provenance's own comment. Merging (unlike
     the three maps above, which have simple, context-free merge rules)
     needs to *build a new node*, so its body is defined out-of-line,
     after oa_derivation/oa_provenance_env's own definitions further
     below.  */
  oa_derivation *deriv_get (tree decl)
  {
    oa_derivation **v = m_deriv_map.get (decl);
    return v ? *v : NULL;
  }
  void deriv_set (tree decl, oa_derivation *d) { m_deriv_map.put (decl, d); }
  void deriv_invalidate (tree decl) { m_deriv_map.remove (decl); }
  void deriv_merge_with (oa_env &other, tree branch_cond);

  /* -fcontract-symbolic-runtime-checks (Mechanism B, see
     .claude/plans/stateless-jumping-shore.md): OUTERMOST_BIND is the
     current function's own top-level BIND_EXPR, captured once by
     oa_resolve_object_address_in_function_1 before the walk starts --
     a plain tree value, never forked/merged (every copy/assign of an
     oa_env just carries the same value along), since it names a
     function-wide constant, not a per-branch fact.  SHADOW_DECLS maps a
     tracked bare-scalar decl to its shadow VAR_DECL (get_or_build_
     scalar_shadow); unlike every map above, its own merge rule is a
     plain set *union*, not an intersection/agreement check -- a shadow,
     once created anywhere reachable from here, must stay visible
     everywhere reachable from here, regardless of which branch created
     it, since the shadow variable itself already exists in the
     compiled function's own top-level scope by then.  */
  tree outermost_bind () { return m_outermost_bind; }
  void set_outermost_bind (tree t) { m_outermost_bind = t; }
  tree shadow_get (tree decl)
  {
    tree *v = m_shadow_decls.get (decl);
    return v ? *v : NULL_TREE;
  }
  void shadow_set (tree decl, tree shadow_var)
  {
    m_shadow_decls.put (decl, shadow_var);
  }
  void shadow_decls_merge_with (oa_env &other)
  {
    for (auto it : other.m_shadow_decls)
      if (!m_shadow_decls.get (it.first))
	m_shadow_decls.put (it.first, it.second);
  }

  oa_env copy ()
  {
    oa_env r;
    for (auto it : m_map)
      r.m_map.put (it.first, it.second);
    for (auto it : m_nz_map)
      r.m_nz_map.put (it.first, it.second);
    for (auto it : m_symbolic_object_address_map)
      r.m_symbolic_object_address_map.put (it.first, it.second);
    for (auto it : m_symbolic_nz_map)
      r.m_symbolic_nz_map.put (it.first, it.second);
    for (auto it : m_range_map)
      r.m_range_map.put (it.first, it.second);
    for (auto it : m_deriv_map)
      r.m_deriv_map.put (it.first, it.second);
    for (auto it : m_predicate_fact_map)
      r.m_predicate_fact_map.put (it.first, it.second);
    for (auto it : m_relational_map)
      r.m_relational_map.put (it.first, it.second);
    for (auto it : m_type_bound_map)
      r.m_type_bound_map.put (it.first, it.second);
    for (auto it : m_call_relational_map)
      r.m_call_relational_map.put (it.first, it.second);
    for (auto it : m_call_call_relational_map)
      r.m_call_call_relational_map.put (it.first, it.second);
    for (auto it : m_contract_scalar_range_map)
      r.m_contract_scalar_range_map.put (it.first, it.second);
    for (auto it : m_contract_field_range_map)
      r.m_contract_field_range_map.put (it.first, it.second);
    for (auto it : m_contract_call_range_map)
      r.m_contract_call_range_map.put (it.first, it.second);
    r.m_outermost_bind = m_outermost_bind;
    for (auto it : m_shadow_decls)
      r.m_shadow_decls.put (it.first, it.second);
    for (auto it : m_alias_target)
      r.m_alias_target.put (it.first, it.second);
    for (auto it : m_field_alias_target)
      r.m_field_alias_target.put (it.first, it.second);
    for (auto it : m_array_alias_target)
      r.m_array_alias_target.put (it.first, it.second);
    r.m_field_object_key = m_field_object_key;
    return r;
  }
  /* Replace *this's contents with a copy of OTHER's (hash_map itself
     has no usable copy-assignment operator, so this is spelled out
     explicitly rather than via operator=).  */
  void assign (oa_env &other)
  {
    m_map.empty ();
    for (auto it : other.m_map)
      m_map.put (it.first, it.second);
    m_nz_map.empty ();
    for (auto it : other.m_nz_map)
      m_nz_map.put (it.first, it.second);
    m_symbolic_object_address_map.empty ();
    for (auto it : other.m_symbolic_object_address_map)
      m_symbolic_object_address_map.put (it.first, it.second);
    m_symbolic_nz_map.empty ();
    for (auto it : other.m_symbolic_nz_map)
      m_symbolic_nz_map.put (it.first, it.second);
    m_range_map.empty ();
    for (auto it : other.m_range_map)
      m_range_map.put (it.first, it.second);
    m_deriv_map.empty ();
    for (auto it : other.m_deriv_map)
      m_deriv_map.put (it.first, it.second);
    m_predicate_fact_map.empty ();
    for (auto it : other.m_predicate_fact_map)
      m_predicate_fact_map.put (it.first, it.second);
    m_relational_map.empty ();
    for (auto it : other.m_relational_map)
      m_relational_map.put (it.first, it.second);
    m_type_bound_map.empty ();
    for (auto it : other.m_type_bound_map)
      m_type_bound_map.put (it.first, it.second);
    m_call_relational_map.empty ();
    for (auto it : other.m_call_relational_map)
      m_call_relational_map.put (it.first, it.second);
    m_call_call_relational_map.empty ();
    for (auto it : other.m_call_call_relational_map)
      m_call_call_relational_map.put (it.first, it.second);
    m_contract_scalar_range_map.empty ();
    for (auto it : other.m_contract_scalar_range_map)
      m_contract_scalar_range_map.put (it.first, it.second);
    m_contract_field_range_map.empty ();
    for (auto it : other.m_contract_field_range_map)
      m_contract_field_range_map.put (it.first, it.second);
    m_contract_call_range_map.empty ();
    for (auto it : other.m_contract_call_range_map)
      m_contract_call_range_map.put (it.first, it.second);
    m_outermost_bind = other.m_outermost_bind;
    m_shadow_decls.empty ();
    for (auto it : other.m_shadow_decls)
      m_shadow_decls.put (it.first, it.second);
    m_alias_target.empty ();
    for (auto it : other.m_alias_target)
      m_alias_target.put (it.first, it.second);
    m_field_alias_target.empty ();
    for (auto it : other.m_field_alias_target)
      m_field_alias_target.put (it.first, it.second);
    m_array_alias_target.empty ();
    for (auto it : other.m_array_alias_target)
      m_array_alias_target.put (it.first, it.second);
    m_field_object_key = other.m_field_object_key;
  }
  /* Merge OTHER into *this in place: a decl remains provable only if
     provable in both (the if/else and loop-header "every incoming
     value must satisfy it" rule).  Collects invalidations separately
     from the read pass, rather than mutating m_map while iterating it.  */
  void merge_with (oa_env &other)
  {
    auto_vec<tree> to_invalidate;
    for (auto it : m_map)
      if (it.second)
	{
	  bool *ov = other.m_map.get (it.first);
	  if (!ov || !*ov)
	    to_invalidate.safe_push (it.first);
	}
    for (unsigned i = 0; i < to_invalidate.length (); ++i)
      m_map.put (to_invalidate[i], false);

    auto_vec<tree> nz_to_invalidate;
    for (auto it : m_nz_map)
      if (it.second)
	{
	  bool *ov = other.m_nz_map.get (it.first);
	  if (!ov || !*ov)
	    nz_to_invalidate.safe_push (it.first);
	}
    for (unsigned i = 0; i < nz_to_invalidate.length (); ++i)
      m_nz_map.put (nz_to_invalidate[i], false);

    auto_vec<tree> symbolic_oa_to_invalidate;
    for (auto it : m_symbolic_object_address_map)
      if (it.second)
	{
	  bool *ov = other.m_symbolic_object_address_map.get (it.first);
	  if (!ov || !*ov)
	    symbolic_oa_to_invalidate.safe_push (it.first);
	}
    for (unsigned i = 0; i < symbolic_oa_to_invalidate.length (); ++i)
      m_symbolic_object_address_map.put (symbolic_oa_to_invalidate[i], false);

    auto_vec<tree> symbolic_nz_to_invalidate;
    for (auto it : m_symbolic_nz_map)
      if (it.second)
	{
	  bool *ov = other.m_symbolic_nz_map.get (it.first);
	  if (!ov || !*ov)
	    symbolic_nz_to_invalidate.safe_push (it.first);
	}
    for (unsigned i = 0; i < symbolic_nz_to_invalidate.length (); ++i)
      m_symbolic_nz_map.put (symbolic_nz_to_invalidate[i], false);
  }

  /* Merge OTHER's range facts into *this in place, by *union* of
     intervals (the range-fact lattice's own merge rule, distinct from
     merge_with's AND-of-booleans above): a decl keeps a fact after the
     merge only if *both* sides have one, for the *same* base (differing
     bases, or a fact present on only one side, means the merged value
     could fall outside either single interval, so the merged result is
     "unconstrained" -- absence, the same "must be provable, else
     treated as unconstrained" discipline as everywhere else in this
     pass). Collects the post-merge state separately from the read
     pass, rather than mutating m_range_map while iterating it.  */
  void range_merge_with (oa_env &other)
  {
    auto_vec<tree> to_remove;
    auto_vec<tree> to_keep;
    auto_vec<oa_range_fact> kept_facts;
    for (auto it : m_range_map)
      {
	oa_range_fact *ov = other.m_range_map.get (it.first);
	if (!ov || ov->base != it.second.base)
	  {
	    to_remove.safe_push (it.first);
	    continue;
	  }
	oa_range_fact merged;
	merged.base = it.second.base;
	merged.has_lo = it.second.has_lo && ov->has_lo;
	merged.has_hi = it.second.has_hi && ov->has_hi;
	if (merged.has_lo)
	  merged.lo = wi::smin (it.second.lo, ov->lo);
	if (merged.has_hi)
	  merged.hi = wi::smax (it.second.hi, ov->hi);
	to_keep.safe_push (it.first);
	kept_facts.safe_push (merged);
      }
    for (unsigned i = 0; i < to_remove.length (); ++i)
      m_range_map.remove (to_remove[i]);
    for (unsigned i = 0; i < to_keep.length (); ++i)
      m_range_map.put (to_keep[i], kept_facts[i]);
  }

private:
  hash_map<tree, bool> m_map;
  hash_map<tree, bool> m_nz_map;
  hash_map<tree, bool> m_symbolic_object_address_map;
  hash_map<tree, bool> m_symbolic_nz_map;
  hash_map<tree, oa_range_fact> m_range_map;
  hash_map<tree, oa_derivation *> m_deriv_map;
  hash_map<tree, oa_predicate_fact> m_predicate_fact_map;
  hash_map<tree, oa_relational_fact> m_relational_map;
  hash_map<tree, oa_type_bound_fact> m_type_bound_map;
  hash_map<tree, oa_call_relational_fact> m_call_relational_map;
  hash_map<oa_field_key_hash, oa_call_call_relational_fact> m_call_call_relational_map;
  hash_map<tree, oa_range_fact> m_contract_scalar_range_map;
  hash_map<oa_field_key_hash, oa_contract_field_range_fact> m_contract_field_range_map;
  hash_map<oa_field_key_hash, oa_contract_field_range_fact> m_contract_call_range_map;
  tree m_outermost_bind = NULL_TREE;
  hash_map<tree, tree> m_shadow_decls;
  hash_map<tree, tree> m_alias_target;
  hash_map<oa_field_key_hash, tree> m_field_alias_target;
  hash_map<pair_hash<nofree_ptr_hash<tree_node>,
		      int_hash<HOST_WIDE_INT, -1, -2>>, tree> m_array_alias_target;
  /* Never owned/allocated here -- see field_object_identity_key's own
     comment for why this must be a shared pointer, not an embedded map.  */
  hash_map<oa_field_key_hash, tree> *m_field_object_key = nullptr;
};

/* An empty, no-added-members subclass, purely so a plugin (which only
   ever sees the opaque forward declaration in contracts.h) can hold an
   oa_analysis_env* without oa_env's own definition being plugin-visible
   at all -- see .claude/plans/stateless-jumping-shore.md. Never
   separately instantiated: oa_walk_function_calls always actually hands
   a plugin callback a real oa_env's address, reinterpreted, which is
   safe here since there is no vtable and not one added byte of layout
   to diverge on.  */

struct oa_analysis_env : public oa_env {};

/* -fcontract-conveyor-proof-provenance: a *why*, alongside the numeric
   *what* oa_range_fact already tracks -- see
   .claude/plans/stateless-jumping-shore.md.

   OA_DERIV_AXIOM: a leaf -- FACT came directly from asserting
   SOURCE_CONTRACT's own conjunct(s) true (a precondition/postcondition/
   contract_assert), no further derivation needed.  OA_DERIV_CALL: FACT
   is CALLEE's own combined postcondition range, substituted at a call
   -- wraps one or more OA_DERIV_AXIOM children (one per contributing
   postcondition conjunct); already fully faithful with no merging
   involved, so this node exists mainly to record which function's
   postcondition it came from for the rendered certificate's own
   comments.  OA_DERIV_IF_JOIN: FACT is the union of THEN_DERIV's and
   ELSE_DERIV's own facts under BRANCH_COND, mirroring
   oa_env::range_merge_with's own min/max join one level up.
   OA_DERIV_LOOP: FACT is the union of PRE_DERIV's own facts (the value
   entering the loop, zero iterations) and BODY_DERIV's own facts (one
   iteration-independent execution of the loop's repeated part, from the
   same invalidated re-walk oa_handle_loop's own numeric merge already
   performs) -- see .claude/plans/stateless-jumping-shore.md's own
   Context section for why this is already a sound Hoare-style loop
   invariant by construction, and why it renders as a bare disjunction
   with no branch selector at all (unlike OA_DERIV_IF_JOIN): there is no
   condition to select between "zero iterations" and "at least one," so
   none is needed to state the fact.

   Node *pointers* are freely shared/aliased across independent oa_env
   copies (see oa_env's own new m_deriv_map below) -- a derivation node
   is immutable once built, exactly like a `tree` node is shared
   throughout the rest of the compiler, so aliasing across branch copies
   is always safe.  */

enum oa_derivation_kind
{
  OA_DERIV_AXIOM, OA_DERIV_CALL, OA_DERIV_IF_JOIN, OA_DERIV_LOOP
};

struct oa_derivation
{
  oa_derivation_kind kind;
  oa_range_fact fact;
  tree source_contract;	/* OA_DERIV_AXIOM.  */
  tree callee;			/* OA_DERIV_CALL.  */
  auto_vec<oa_derivation *> children;	/* OA_DERIV_CALL: one per axiom conjunct.  */
  tree branch_cond;		/* OA_DERIV_IF_JOIN.  */
  oa_derivation *then_deriv, *else_deriv;	/* OA_DERIV_IF_JOIN.  */
  oa_derivation *pre_deriv, *body_deriv;	/* OA_DERIV_LOOP.  */
};

/* The *allocator/owner* for every oa_derivation node built during one
   function's own oa_walk_stmt walk -- deliberately NOT where the
   per-decl "which derivation does this decl currently have" mapping
   lives (that has to copy/merge across if/else branches in lockstep
   with the numeric facts, so it lives directly in oa_env itself instead
   -- see oa_env::m_deriv_map below). This class's only job is handing
   out freshly built nodes and freeing all of them together when the
   function's analysis is done, rather than making every individual hook
   manage its own nodes' lifetimes.  */

class oa_provenance_env
{
public:
  ~oa_provenance_env ()
  {
    for (unsigned i = 0; i < m_owned.length (); ++i)
      delete m_owned[i];
  }

  oa_derivation *make_axiom (const oa_range_fact &fact, tree source_contract)
  {
    oa_derivation *d = new oa_derivation ();
    d->kind = OA_DERIV_AXIOM;
    d->fact = fact;
    d->source_contract = source_contract;
    m_owned.safe_push (d);
    return d;
  }

  oa_derivation *make_call (const oa_range_fact &fact, tree callee)
  {
    oa_derivation *d = new oa_derivation ();
    d->kind = OA_DERIV_CALL;
    d->fact = fact;
    d->callee = callee;
    m_owned.safe_push (d);
    return d;
  }

  oa_derivation *make_if_join (const oa_range_fact &fact, tree branch_cond,
			       oa_derivation *then_deriv, oa_derivation *else_deriv)
  {
    oa_derivation *d = new oa_derivation ();
    d->kind = OA_DERIV_IF_JOIN;
    d->fact = fact;
    d->branch_cond = branch_cond;
    d->then_deriv = then_deriv;
    d->else_deriv = else_deriv;
    m_owned.safe_push (d);
    return d;
  }

  oa_derivation *make_loop (const oa_range_fact &fact, oa_derivation *pre_deriv,
			    oa_derivation *body_deriv)
  {
    oa_derivation *d = new oa_derivation ();
    d->kind = OA_DERIV_LOOP;
    d->fact = fact;
    d->pre_deriv = pre_deriv;
    d->body_deriv = body_deriv;
    m_owned.safe_push (d);
    return d;
  }

private:
  auto_vec<oa_derivation *> m_owned;
};

/* NULL (the default) means provenance tracking is completely inactive --
   every hook that would otherwise call into this allocator degrades to
   a no-op, so this feature has zero effect on anything unless both
   -fcontract-conveyor-proofs and -fcontract-conveyor-proof-provenance
   are on.  Set/cleared by oa_resolve_object_address_in_function_1
   around one function's own walk, mirroring oa_call_site_callback's
   own save/restore discipline.  */

static oa_provenance_env *oa_active_provenance;

/* Out-of-line (needs oa_provenance_env's full definition, and
   oa_active_provenance, both only just declared above -- see the
   in-class declaration's own comment).  A no-op entirely when
   provenance tracking is inactive.  Mirrors range_merge_with's own
   shape exactly, but a decl is only kept if the *numeric* merge
   (range_merge_with, already called by the caller before this) itself
   still has a fact for it -- if either side lacks a derivation (e.g.
   one arm's fact came from a source this feature doesn't yet build
   derivations for), the decl is simply left without one, and the
   certificate renderer falls back to a flat bare-premise assertion for
   it, exactly as if provenance tracking had never run at all.  */

void
oa_env::deriv_merge_with (oa_env &other, tree branch_cond)
{
  if (!oa_active_provenance)
    return;

  auto_vec<tree> to_remove;
  auto_vec<tree> to_keep;
  auto_vec<oa_derivation *> kept_derivs;
  for (auto it : m_deriv_map)
    {
      oa_derivation **ov = other.m_deriv_map.get (it.first);
      oa_range_fact merged_fact;
      if (!ov || !range_get (it.first, &merged_fact))
	{
	  to_remove.safe_push (it.first);
	  continue;
	}
      oa_derivation *joined
	= oa_active_provenance->make_if_join (merged_fact, branch_cond,
					       it.second, *ov);
      to_keep.safe_push (it.first);
      kept_derivs.safe_push (joined);
    }
  for (unsigned i = 0; i < to_remove.length (); ++i)
    m_deriv_map.remove (to_remove[i]);
  for (unsigned i = 0; i < to_keep.length (); ++i)
    m_deriv_map.put (to_keep[i], kept_derivs[i]);
}

/* True if CALL is a statically-resolvable, immediately-invoked closure
   call -- a CALL_EXPR whose callee is directly a lambda's operator(),
   invoked on a closure object constructed right there in the same
   expression (a TARGET_EXPR, never a named variable, parameter, or
   anything stored/passed around) -- the exact, narrow pattern item 5
   permits recursing into.  On success, *CLOSURE_OBJ is the constructed
   closure TARGET_EXPR (unused by the caller currently, but kept for
   symmetry/future use).  */

static bool
oa_iile_call_p (tree call, tree *closure_obj)
{
  if (call == NULL_TREE || call == error_mark_node
      || TREE_CODE (call) != CALL_EXPR)
    return false;
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return false;
  tree ctx = DECL_CONTEXT (callee);
  if (!ctx || TREE_CODE (ctx) != RECORD_TYPE || !LAMBDA_TYPE_P (ctx))
    return false;
  if (call_expr_nargs (call) < 1)
    return false;
  tree arg0 = CALL_EXPR_ARG (call, 0);
  if (TREE_CODE (arg0) != ADDR_EXPR)
    return false;
  tree obj = TREE_OPERAND (arg0, 0);
  if (TREE_CODE (obj) != TARGET_EXPR)
    return false;
  *closure_obj = obj;
  return true;
}

/* D4324: the same recognition oa_iile_call_p above already does,
   parameterized for build_over_call's own callee-must-be-conveyor
   check (call.cc), which runs *before* the CALL_EXPR itself is fully
   built (only a resolved FUNCTION_DECL and its own not-yet-adjusted
   object argument are available at that point, not a real call tree to
   hand oa_iile_call_p). FN is the resolved callee; OBJ_ARG is the
   corresponding not-yet-adjusted 'this' argument. Exempting this exact,
   narrow shape (never bare "any lambda operator() call") is deliberate
   and load-bearing, not just a convenience: an immediately-invoked
   closure's own body is already walked directly, with full access to
   the *caller's* own established facts (capture-proxy redirection,
   oa_resolve_iile_call), by the very same analysis that would otherwise
   need the closure to be independently 'conveyor' -- requiring the
   keyword here would only ever add a *second*, wholly separate,
   context-free top-level analysis of the same body (this file's own,
   ordinary per-function entry point that every FUNCTION_DECL gets,
   conveyor or not), which cannot see anything the caller captured by
   reference and would misdiagnose facts the caller-context walk already
   established correctly. Confirmed via direct testing: d4324-conveyor-
   divmod-iile-ok.C's own by-reference-capture-proxy-redirect scenario
   regressed to a spurious "not provably nonzero" precisely this way
   before this exemption was added.  A lambda stored in a variable and
   invoked later is NOT this shape (OBJ_ARG would name that variable,
   not a fresh TARGET_EXPR) and still needs its own 'conveyor'
   declaration like any other callable, since nothing analyzes its body
   with the caller's own context in that case.  */

bool
is_iile_operator_call_p (tree fn, tree obj_arg)
{
  if (!fn || TREE_CODE (fn) != FUNCTION_DECL)
    return false;
  tree ctx = DECL_CONTEXT (fn);
  if (!ctx || TREE_CODE (ctx) != RECORD_TYPE || !LAMBDA_TYPE_P (ctx))
    return false;
  if (!obj_arg || TREE_CODE (obj_arg) != ADDR_EXPR)
    return false;
  return TREE_CODE (TREE_OPERAND (obj_arg, 0)) == TARGET_EXPR;
}

/* Non-null only while oa_provable_p is currently resolving values
   *inside* an invoked closure's own body (set/cleared by
   oa_resolve_iile_call, defined below oa_walk_stmt since it needs to
   call it) -- points at the *enclosing* (caller's) env, consulted only
   for capture-proxy resolution (see oa_provable_p below). A single
   pointer rather than a stack: deliberately supports only one level of
   IILE nesting (recursing into an IILE found *inside* another IILE's
   own body is conservatively left unresolved, never incorrectly
   accepted) -- nested IILEs are a pathological enough case that this
   restriction is an accepted, documented limitation rather than
   something worth a general stack for in this increment.  */

static oa_env *oa_iile_outer_env;

/* Forward-declared: defined below oa_walk_stmt, since resolving an
   invoked closure's body requires calling the statement walker
   recursively.  */
static bool oa_resolve_iile_call (tree call, oa_env &env);

/* D4324/P2680 item 6: forward-declared -- the actual definitions
   (defined near oa_handle_call_precondition_obligation, item 7's dual,
   since they need oa_collect_conjuncts/oa_nonzero_conjunct_p/oa_
   refine_single_comparison/oa_contract_conveyor_active_p, all defined
   later in the file) are consulted from the tail of oa_provable_p/
   oa_provably_nonzero_p/oa_get_range respectively, as one more fact
   source alongside each function's existing IILE-recursion check: a
   callee's own non-ignored, conveyor postcondition, naming its result
   identifier in a fact-shaped conjunct, is an unconditional guarantee
   about any call's return value -- no argument substitution needed,
   unlike item 7's complementary precondition-*obligation* direction.  */
static bool oa_call_postcondition_object_address_p (tree call);
static bool oa_call_postcondition_nonzero_p (tree call);
static bool oa_call_postcondition_range_p (tree call, oa_env &env, oa_range_fact *out,
					    oa_derivation **deriv_out = NULL);

/* -fcontract-symbolic-runtime-checks (Mechanism B): gates every codegen-
   *injecting* side effect (shadow creation, establish/invalidate
   statement insertion) -- as opposed to every other global/map in this
   file, which only ever gets *read* to produce a diagnostic.  On
   whenever the flag is set, for the walk's one top-level pass, exactly
   the same shape as OA_RETURN_TRACKING (below); explicitly saved and
   cleared around oa_handle_loop's own per-reassigned-decl re-walks
   (which exist purely to compute compile-time facts, not to represent
   any one real execution of the loop) so codegen is never injected more
   than once for the same loop -- see oa_handle_loop's own comment.  */
static bool oa_symbolic_codegen_active;
static tree oa_shadow_field (tree type, unsigned index);
static tree build_real_source_location_value (location_t, tree, tree);

/* Forward-declared: full definition is much further below (near its
   own sibling oa_strip_conversion_call), but oa_object_identity_decl
   just below needs it here.  */
static tree oa_strip_conversion_operator_call (tree op);

/* -fcontract-symbolic-proofs: resolve EXPR to the one canonical decl
   identifying "the object this expression names" -- true for 'this'
   (returns the current function's own 'this' PARM_DECL), '&decl' where
   decl is a VAR_DECL/PARM_DECL (returns decl), or a bare VAR_DECL/
   PARM_DECL used directly (returns itself, for a value that's already
   a pointer). Conservatively false for anything else.

   Factored out of oa_provable_p's own identical 'this'/'&decl'
   recognition just below (same shapes, same stripping), but
   deliberately simpler: no IILE-capture-proxy redirection, no oa_env
   dependency at all -- this only ever needs a stable identity key for
   the symbolic-fact map, never a truth value, so none of
   oa_provable_p's *provability* logic applies here.  */

static bool
oa_object_identity_decl (tree expr, tree *decl_out)
{
  if (expr == NULL_TREE || expr == error_mark_node)
    return false;

  expr = STRIP_ANY_LOCATION_WRAPPER (expr);
  while (TREE_CODE (expr) == NON_LVALUE_EXPR
	 || TREE_CODE (expr) == NOP_EXPR
	 || TREE_CODE (expr) == CONVERT_EXPR
	 || TREE_CODE (expr) == VIEW_CONVERT_EXPR)
    expr = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));

  /* A class-typed operand reached via its own implicit conversion
     operator (e.g. a smart-pointer-like wrapper converting to a
     reference to the object a predicate/field-range fact is actually
     tracked against) -- always safe to look through here, for every
     caller (self-trust, establish, invalidate, consult alike), since a
     conversion operator always refers to the *same* object, unlike a
     by-value copy (see oa_strip_conversion_operator_call's own
     comment for why that distinction matters and isn't handled here).  */
  expr = oa_strip_conversion_operator_call (expr);

  if (is_this_parameter (expr))
    {
      *decl_out = expr;
      return true;
    }

  if (TREE_CODE (expr) == ADDR_EXPR)
    {
      tree op = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));
      /* A reference-typed argument is itself passed as the address of
	 whatever it's bound to -- so when that reference is produced by
	 a conversion operator (e.g. a wrapper's own 'operator T&()',
	 substituted for a callee's own reference parameter at a call
	 site), the conversion call sits *underneath* this ADDR_EXPR,
	 not at EXPR's own top level where the strip just above already
	 looked -- found via direct testing of exactly this shape.  */
      op = oa_strip_conversion_operator_call (op);
      if (DECL_P (op) && (VAR_P (op) || TREE_CODE (op) == PARM_DECL))
	{
	  *decl_out = op;
	  return true;
	}
      return false;
    }

  if (VAR_P (expr) || TREE_CODE (expr) == PARM_DECL)
    {
      *decl_out = expr;
      return true;
    }

  return false;
}

/* Stage 2a: a new, separate resolver for a struct/class field slot
   (e.g. 'h.ptr' or 'hp->ptr') that currently holds the same pointer
   value as some other tracked decl -- see oa_env::field_alias_find's
   own comment for the (base_identity, FIELD_DECL) map this consults.

   Deliberately NOT folded into oa_object_identity_decl itself, even
   though every caller of that function could, in principle, also
   benefit from resolving a COMPONENT_REF this way. oa_object_identity_
   decl's own true/false return is used as a control-flow discriminator
   at Rule 1's own reassignment dispatch (see oa_walk_stmt's INIT_EXPR/
   MODIFY_EXPR case): true means "LHS is itself a whole-object identity,
   invalidate its own facts wholesale"; false means "LHS is some other
   shape (a COMPONENT_REF field write), do the narrower per-field
   invalidation instead". Teaching oa_object_identity_decl to resolve
   'h.ptr' whenever a field alias happens to already be recorded for it
   would make an ordinary, *unrelated* later write to that same field
   wrongly take the "whole-object" branch instead of the field-specific
   one -- a real regression, found and rejected during this feature's
   own design review, not a hypothetical worry. Callers that want this
   resolution (establish/consult/Rule 2 sites) call this function as an
   explicit, separate fallback instead.

   The base of the COMPONENT_REF must be resolved via oa_object_
   identity_decl specifically (the untyped "any object has an
   identity" resolver) -- never oa_invalidation_identity_decl, which
   requires POINTER_TYPE_P/REFERENCE_TYPE on its own operand and would
   always fail for a plain, non-pointer struct base like 'h'. A leading
   INDIRECT_REF is unwrapped first (mirroring Rule 1's own existing
   COMPONENT_REF invalidation block's identical unwrap), so 'hp->ptr'
   (pointer-to-struct) resolves the same way 'h.ptr' (plain struct)
   does. The resolved base is then canonicalized through ENV's own
   alias_find *before* the field-map lookup, symmetric with how the
   write side (oa_walk_stmt's own field-alias-recording block) stores
   it -- without this, Stage 1's own pointer aliasing (e.g. 'hp' itself
   being reassigned from another pointer) and this function's own field
   aliasing wouldn't compose for a combined case like 'hp = other_hp;
   hp->ptr = p; mutate (hp->ptr);', found and fixed during the same
   review.  */

static bool
oa_field_slot_identity (tree expr, oa_env &env, tree *decl_out)
{
  expr = STRIP_ANY_LOCATION_WRAPPER (expr);
  if (TREE_CODE (expr) != COMPONENT_REF)
    return false;
  tree field = TREE_OPERAND (expr, 1);
  if (TREE_CODE (field) != FIELD_DECL
      || !(POINTER_TYPE_P (TREE_TYPE (field))
	   || TREE_CODE (TREE_TYPE (field)) == REFERENCE_TYPE))
    return false;
  tree obj = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));
  tree obj_expr = TREE_CODE (obj) == INDIRECT_REF
    ? STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (obj, 0)) : obj;
  tree base_identity;
  if (!oa_object_identity_decl (obj_expr, &base_identity))
    return false;
  base_identity = env.alias_find (base_identity);
  tree *target = env.field_alias_find (base_identity, field);
  if (!target)
    return false;
  *decl_out = *target;
  return true;
}

/* Stage 2b: recognizes 'arr[N]' (a literal ARRAY_REF) or '*arr' (an
   array decaying to a pointer and being immediately dereferenced --
   confirmed via a raw tree dump to be INDIRECT_REF (NOP_EXPR
   (ADDR_EXPR (arr))) at this stage, semantically identical to
   'arr[0]') uniformly, used by both oa_array_slot_identity below and
   the Rule 1 write-detection site so the two can never drift out of
   sync the way two independently-written copies of this logic could.

   Deliberately returns the index as a TREE, not yet resolved to a
   constant: the write site needs to distinguish "not an array-slot
   access at all" (nothing to do) from "an array-slot access with an
   unprovable index" (invalidate every tracked slot for this array,
   see oa_array_index_constant's own comment) -- collapsing both into a
   single boolean here would lose that distinction.

   Deliberately does not care whether ARR_BASE itself is a plain decl
   or something else (e.g. a COMPONENT_REF for a struct-embedded array,
   'hp2->arr[0]'/'h2.arr[0]') -- both are syntactically valid array-
   typed bases here. The caller's own subsequent oa_object_identity_
   decl call already, correctly, declines for a COMPONENT_REF base (no
   such case in that function), which is the intended, honestly
   documented "struct-embedded arrays are out of scope" outcome (see
   this feature's own plan notes: a struct-embedded array's own slots
   would need a fundamentally different, 3-tuple (struct_identity,
   FIELD_DECL, index) key, not attempted here) -- no extra code is
   needed to enforce that boundary, it falls out for free from reusing
   the same base-resolution step everywhere.  */

static bool
oa_array_slot_base (tree expr, tree *arr_base_out, tree *index_out)
{
  expr = STRIP_ANY_LOCATION_WRAPPER (expr);
  if (TREE_CODE (expr) == ARRAY_REF)
    {
      tree arr_base = TREE_OPERAND (expr, 0);
      if (TREE_CODE (TREE_TYPE (arr_base)) != ARRAY_TYPE)
	return false;
      *arr_base_out = arr_base;
      *index_out = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 1));
      return true;
    }
  if (TREE_CODE (expr) == INDIRECT_REF)
    {
      tree op = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));
      while (TREE_CODE (op) == NOP_EXPR || TREE_CODE (op) == CONVERT_EXPR
	     || TREE_CODE (op) == NON_LVALUE_EXPR
	     || TREE_CODE (op) == VIEW_CONVERT_EXPR)
	op = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (op, 0));
      if (TREE_CODE (op) != ADDR_EXPR)
	return false;
      tree arr_base = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (op, 0));
      if (TREE_CODE (TREE_TYPE (arr_base)) != ARRAY_TYPE)
	return false;
      *arr_base_out = arr_base;
      *index_out = integer_zero_node;
      return true;
    }
  return false;
}

/* True if INDEX (as returned by oa_array_slot_base) is a provable,
   non-negative compile-time constant. Non-negative is required purely
   to keep the value space simple (a negative index into a real,
   fixed-size array is already UB in the source under analysis, so
   there is nothing useful to track) -- NOT to avoid colliding with
   int_hash<HOST_WIDE_INT, -1, -2>'s own empty/deleted sentinels the
   way it might first appear: pair_hash's own is_empty/is_deleted/
   mark_empty/mark_deleted all delegate solely to the first (tree
   pointer) component, so the second component's sentinels are never
   actually consulted for slot management when used this way, and no
   such collision could occur regardless of this gate.  */

static bool
oa_array_index_constant (tree index, HOST_WIDE_INT *out)
{
  if (TREE_CODE (index) != INTEGER_CST || !tree_fits_shwi_p (index)
      || tree_int_cst_sgn (index) < 0)
    return false;
  *out = tree_to_shwi (index);
  return true;
}

/* Stage 2b: the array-element analogue of oa_field_slot_identity
   above -- same "new, separate, purely additive resolver" reasoning,
   same reason it's never folded into oa_object_identity_decl.  */

static bool
oa_array_slot_identity (tree expr, oa_env &env, tree *decl_out)
{
  expr = STRIP_ANY_LOCATION_WRAPPER (expr);
  if (!(POINTER_TYPE_P (TREE_TYPE (expr))
	|| TREE_CODE (TREE_TYPE (expr)) == REFERENCE_TYPE))
    return false;
  tree arr_base, index;
  if (!oa_array_slot_base (expr, &arr_base, &index))
    return false;
  HOST_WIDE_INT idx;
  if (!oa_array_index_constant (index, &idx))
    return false;
  tree array_identity;
  if (!oa_object_identity_decl (arr_base, &array_identity))
    return false;
  array_identity = env.alias_find (array_identity);
  tree *target = env.array_alias_find (array_identity, idx);
  if (!target)
    return false;
  *decl_out = *target;
  return true;
}

/* Stage 5: '&h->f'/'&h.f' -- the address of a struct/class member of
   ANY type (most usefully a non-pointer, embedded sub-object) -- names
   a fixed, permanent sub-object of the base, distinct in kind from
   oa_field_slot_identity above (which answers "what pointer value does
   field slot h.ptr currently hold", an alias lookup for a value that
   can change over time). There is no "alias target" to look up here;
   the (base_identity, FIELD_DECL) pair *is* the identity, permanently,
   for the object's whole lifetime -- resolved to a synthesized,
   stable, cached placeholder tree via ENV's own field_object_identity_
   key (see that method's own comment on why the cache must be shared,
   not per-branch-copied).

   Deliberately not folded into oa_object_identity_decl itself, even
   though (unlike Stage 2a's h.ptr case) '&h->f' can never appear as an
   assignment's own LHS, so there is no Rule-1-dispatch-corruption risk
   here -- oa_object_identity_decl returns a single, already-existing
   tree via its own out-param, but a (base, FIELD_DECL) pair has no
   single, natural tree representation the way a bare decl does; two
   syntactically identical '&h->f' expressions at different program
   points are different ADDR_EXPR/COMPONENT_REF tree nodes (the same
   non-interning problem oa_field_slot_identity's own map was designed
   around), so whatever is returned as "the identity" has to be some
   stable, synthesized representative instead.

   The base is resolved via oa_object_identity_decl specifically (the
   untyped "any object has an identity" resolver) -- never oa_
   invalidation_identity_decl, for the identical reason oa_field_slot_
   identity's own comment gives.  A leading INDIRECT_REF is unwrapped
   first (confirmed via raw tree dump that '&h->f' is ADDR_EXPR
   (COMPONENT_REF (INDIRECT_REF (h), f)) while '&h.f' is ADDR_EXPR
   (COMPONENT_REF (h, f)), no INDIRECT_REF at all), the same unwrap
   oa_field_slot_identity already needs for 'hp->ptr'.

   Deliberately does NOT gate on FIELD's own type (unlike oa_field_
   slot_identity, which only ever tracks pointer/reference-typed
   fields, since it's about aliasing a pointer *value*) -- '&h->f' is a
   meaningful, trackable identity for a field of any type at all, since
   the predicate is over the field's own address, not whatever value it
   currently holds.  */

static bool
oa_field_object_identity (tree expr, oa_env &env, tree *decl_out)
{
  expr = STRIP_ANY_LOCATION_WRAPPER (expr);
  if (TREE_CODE (expr) != ADDR_EXPR)
    return false;
  tree comp = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));
  if (TREE_CODE (comp) != COMPONENT_REF)
    return false;
  tree field = TREE_OPERAND (comp, 1);
  if (TREE_CODE (field) != FIELD_DECL)
    return false;
  tree obj = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (comp, 0));
  tree obj_expr = TREE_CODE (obj) == INDIRECT_REF
    ? STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (obj, 0)) : obj;
  tree base_identity;
  if (!oa_object_identity_decl (obj_expr, &base_identity))
    return false;
  base_identity = env.alias_find (base_identity);
  *decl_out = env.field_object_identity_key (base_identity, field);
  return true;
}

/* True if EXPR (evaluated in ENV) is provably an object address:
   'this'; '&obj' where obj is a parameter/variable of object type; a
   VAR_DECL/PARM_DECL whose current value ENV already knows to be
   provable; a by-reference lambda-capture proxy, resolved against the
   *enclosing* scope's env if we're currently inside an invoked
   closure's own body; or the result of a statically-resolvable,
   immediately-invoked closure call (item 5), recursed into via
   oa_resolve_iile_call.  Conservatively false for anything else (in
   particular: this does not interpret arbitrary function calls, or a
   stored/passed-around closure invoked here).  */

static bool
oa_provable_p (tree expr, oa_env &env)
{
  if (expr == NULL_TREE || expr == error_mark_node)
    return false;

  STRIP_ANY_LOCATION_WRAPPER (expr);

  /* 'return &a;' where 'a' is a by-reference lambda-capture proxy
     arrives here as NOP_EXPR/CONVERT_EXPR converting the proxy's own
     reference type directly to a pointer type -- taking the address of
     a reference is, at this representation, just reading the
     reference's own stored pointer value, with no separate ADDR_EXPR
     node the way '&plain_var' gets. This must be recognized *before*
     the generic conversion-stripping loop below (which would otherwise
     blindly strip it and lose the "this was an address-of" distinction
     entirely, treating it as if the code had read the pointee's *value*
     instead of its *address*). Only meaningful -- and only safe --
     while currently resolving values inside an invoked closure's own
     body (OA_IILE_OUTER_ENV set): the answer is exactly the same as the
     ADDR_EXPR-of-plain-decl base case below, just applied to whatever
     DECL_CAPTURED_VARIABLE names, since capturing a variable by
     reference always binds directly to that variable's own storage.  */
  if (oa_iile_outer_env
      && (TREE_CODE (expr) == NOP_EXPR || TREE_CODE (expr) == CONVERT_EXPR)
      && POINTER_TYPE_P (TREE_TYPE (expr)))
    {
      tree op = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));
      if (VAR_P (op) && TREE_CODE (TREE_TYPE (op)) == REFERENCE_TYPE
	  && is_capture_proxy (op))
	{
	  tree captured = DECL_CAPTURED_VARIABLE (op);
	  return captured && DECL_P (captured)
	    && (VAR_P (captured) || TREE_CODE (captured) == PARM_DECL);
	}
    }

  /* Reading a by-reference lambda-capture proxy's *value* directly
     (e.g. a captured pointer used as-is, '[&]{ return p; }()', as
     opposed to '&p' handled just above) arrives here as
     INDIRECT_REF(proxy) -- found empirically alongside the div/mod
     fact's own identical case (Increment E-divmod): unlike '&proxy',
     an ordinary *read* needs an explicit dereference node with no
     conversion wrapper of its own, so the VAR_P branch below never
     used to see through it at all -- silently falling through to
     "unprovable" regardless of whether the captured pointer actually
     was, rather than via the capture-proxy redirect that branch
     already contains.  Strip it here, before the generic conversion-
     stripping loop (which has nothing that would remove it).  */
  if (oa_iile_outer_env && TREE_CODE (expr) == INDIRECT_REF)
    {
      tree op = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));
      if (VAR_P (op) && TREE_CODE (TREE_TYPE (op)) == REFERENCE_TYPE
	  && is_capture_proxy (op))
	expr = op;
    }

  /* D4324, Increment V: VIEW_CONVERT_EXPR is how a contract condition's
     access to a decl is wrapped to present it as const-qualified (see
     the comment above the VIEW_CONVERT_EXPR-building code near line
     570) -- decl-identity-preserving, so safe (and necessary) to strip
     through here exactly like the ordinary conversion wrappers, found
     via direct testing that a const-qualified parameter's own fact
     otherwise silently failed to be recognized in a contract
     condition.  */
  while (TREE_CODE (expr) == NON_LVALUE_EXPR
	 || TREE_CODE (expr) == NOP_EXPR
	 || TREE_CODE (expr) == CONVERT_EXPR
	 || TREE_CODE (expr) == VIEW_CONVERT_EXPR)
    expr = TREE_OPERAND (expr, 0);

  if (is_this_parameter (expr))
    return true;

  if (TREE_CODE (expr) == ADDR_EXPR)
    {
      tree op = TREE_OPERAND (expr, 0);
      return DECL_P (op) && (VAR_P (op) || TREE_CODE (op) == PARM_DECL);
    }

  if (VAR_P (expr) || TREE_CODE (expr) == PARM_DECL)
    {
      /* A by-reference lambda-capture proxy whose *value* (not address,
	 handled above) is read directly -- e.g. a captured *pointer*
	 variable used as-is, '[&]{ return p; }()' -- is just an alias
	 for the outer variable it captures. Only meaningful -- and only
	 safe -- while currently resolving values inside an invoked
	 closure's own body (OA_IILE_OUTER_ENV set): redirect to whatever
	 the *enclosing* scope's env already knows about the real
	 captured variable, since for an *immediately*-invoked closure
	 nothing in the enclosing scope can have changed between capture
	 and invocation.  */
      if (oa_iile_outer_env && is_capture_proxy (expr))
	{
	  tree captured = DECL_CAPTURED_VARIABLE (expr);
	  if (captured)
	    return oa_provable_p (captured, *oa_iile_outer_env);
	}
      return env.provable_p (expr);
    }

  /* Recurse into a statically-resolvable, immediately-invoked closure
     (item 5) -- but only one level deep (see oa_iile_outer_env above):
     an IILE found while already resolving another IILE's own body is
     conservatively left unprovable rather than followed further.  */
  tree closure_obj;
  if (!oa_iile_outer_env && oa_iile_call_p (expr, &closure_obj))
    return oa_resolve_iile_call (expr, env);

  /* Item 6: an ordinary (non-IILE) call whose callee's own non-ignored,
     conveyor postcondition unconditionally guarantees its return value
     is an object address.  */
  if (TREE_CODE (expr) == CALL_EXPR
      && oa_call_postcondition_object_address_p (expr))
    return true;

  return false;
}

/* D4324/P2680 item 8: true if EXPR is provably nonzero -- a literal
   nonzero integer constant, an integer-typed VAR_DECL/PARM_DECL ENV's
   second ("nonzero") fact map already knows to be provable (fed by the
   narrow version's two sources -- a literal, or a decl straight-line-
   assigned from one -- plus, as of Increment E-divmod, the loop-header
   merge rule and contract_assert/precondition/postcondition fact
   sources, exactly mirroring is_object_address's own sources), or (also
   Increment E-divmod) a by-reference lambda-capture proxy for such a
   decl, resolved against the *enclosing* scope's env exactly the way
   oa_provable_p's own capture-proxy redirect works.

   This function itself still does not recurse into a statically-
   resolvable, immediately-invoked closure *call* directly the way
   oa_provable_p does (item 5) -- doing so here would need its own
   parallel return-path tracking mechanism for nonzero-ness
   specifically. In practice, though, Increment E4's extension of the
   supplementary range-fact check just below now covers the common
   case anyway: 'int n = [&]{ return 5; }();' ends up with a range
   fact for 'n' (via oa_get_range's own IILE recursion, oa_resolve_
   iile_range), which the range check below then recognizes as
   excluding zero -- so this residual gap only remains for a case
   where a range fact isn't establishable at all (e.g. the closure
   returns something oa_get_range doesn't recognize) but nonzero-ness
   specifically still might be. Anything not recognized here is
   conservatively left unprovable, the same "must be provable, else
   treated as unprovable" discipline used throughout this pass.  */

/* Forward-declared: oa_provably_nonzero_p also consults a range fact
   (Increment E1) as a supplementary source, defined below it.  */
static bool oa_get_range (tree expr, oa_env &env, oa_range_fact *out);

/* Forward-declared: oa_get_range recurses into a statically-
   resolvable, immediately-invoked closure (Increment E4) via this,
   defined near its sibling oa_resolve_iile_call further down (after
   oa_walk_stmt itself, which both need to call).  */
static bool oa_resolve_iile_range (tree call, oa_env &env, oa_range_fact *out);

/* If OP, after stripping the same ordinary, decl-identity-preserving
   wrappers used throughout this pass (NON_LVALUE_EXPR/NOP_EXPR/
   CONVERT_EXPR/VIEW_CONVERT_EXPR -- see oa_get_range's own comment on
   why these are safe to strip), is a call through an implicit, single-
   argument conversion operator (DECL_CONV_FN_P) -- e.g. 'q.operator
   int()' or '(ptr->count).operator int()', the shape a class-typed
   operand gets wrapped in wherever it participates in a scalar
   comparison, arithmetic, or nonzero-ness context via a user-defined
   conversion rather than already being of scalar type -- return its
   own receiver object expression (ordinary-stripped the same way, with
   one ADDR_EXPR peeled off, since a non-static member function's
   implicit object argument is always passed by address), whatever
   shape that turns out to be (a decl, a field access, ...).  Otherwise
   return OP itself, ordinary-stripped only, unchanged.

   Also looks through a TARGET_EXPR whose own initializer is a plain
   copy -- passing a class-typed decl BY VALUE to another function (e.g.
   'need_small (q)' substituting a class-typed q for an int parameter,
   or a relational obligation's own call-argument substitution)
   materializes the copy as exactly this shape, found via direct testing
   that a relational fact for a class-typed parameter otherwise silently
   failed to be recognized at any *cross-call* consult site (self-trust
   seeding, which stays within one function's own AST, never goes
   through this materialization at all -- only a call's own substituted
   argument does).

   A TARGET_EXPR's own initializer can also be a full constructor call
   (AGGR_INIT_EXPR) instead of a plain copy -- the shape a NON-trivially-
   copyable class type's own by-value forwarding takes (found via direct
   testing of 'int g (wrap q) { return f (q); }' where wrap has a user-
   provided copy constructor: 'f (&TARGET_EXPR<D.NNNN, AGGR_INIT_EXPR
   (wrap::wrap(const wrap&), D.NNNN, ..., q)>)', not the plain-copy
   TARGET_EXPR a trivially-copyable type gets). Recognized specifically
   when that call is to a copy or move constructor (DECL_COPY_
   CONSTRUCTOR_P/DECL_MOVE_CONSTRUCTOR_P) -- its *last* argument is then
   the receiver's own single source value (found via direct testing
   that an extra, compiler-internal leading argument can precede it, so
   the argument *count* isn't reliably one, only the source's *position*
   is reliably last) -- and otherwise left unresolved (a converting
   constructor building a genuinely new object from unrelated arguments
   has no "source" to unwrap to, the same non-goal as an ordinary
   AGGR_INIT_EXPR construction of a fresh rvalue like 'wrap (2)').

   Purely structural: never inspects the conversion's own return value
   or the receiver's type, only *which* underlying operand a scalar-
   context expression actually refers to -- callers that need a
   specific resulting shape (a bare decl, a COMPONENT_REF, ...) check
   that themselves, the same way they already check the un-converted
   case.  Shared by every fact-shape recognizer that used to require an
   operand to already be of scalar type (oa_provably_nonzero_p, oa_get_
   range, oa_nonzero_conjunct_p, oa_predicate_conjunct_shape, oa_match_
   simple_comparison, oa_match_result_relation, oa_symbolic_comparison_
   conjunct_shape, oa_underlying_param_operand, oa_get_relational,
   oa_strip_to_relational_operand) so a class type with a conversion
   operator is recognized uniformly everywhere a bare scalar-typed
   operand already was, not just in whichever one shape prompted the
   fix.  */

/* The conversion-operator-only half of oa_strip_conversion_call below
   (ordinary-wrapper strip + the CALL_EXPR/DECL_CONV_FN_P receiver
   extraction), deliberately NOT looking through a TARGET_EXPR/AGGR_
   INIT_EXPR copy-construction materialization the way the full
   function does -- a standalone twin, not a shared-code refactor of
   the existing, already-tested function, so as to carry zero risk of
   changing its behavior.

   Needed because, unlike the value-fact layer (a copy has the
   *identical value* to its source, so copy-construction lookthrough is
   sound for every purpose there), the named-predicate/ptr->field-range
   *identity* layer tracks an object's own *mutable state*: a callee
   that receives a byvalue COPY and asserts something about it tells us
   nothing about the caller's own original object, so establishing or
   invalidating a fact via a copy's own source would be unsound.
   Conversion-operator lookthrough has no such problem (a wrapper
   converting to a reference is always the *same* object), so it's
   safe to apply unconditionally, including to establish/invalidate --
   see oa_object_identity_decl's own use of this function, versus its
   own callers that additionally apply the full oa_strip_conversion_
   call for their own consult-only call sites.  */

static tree
oa_strip_conversion_operator_call (tree op)
{
  while (TREE_CODE (op) == NON_LVALUE_EXPR || TREE_CODE (op) == NOP_EXPR
	 || TREE_CODE (op) == CONVERT_EXPR || TREE_CODE (op) == VIEW_CONVERT_EXPR)
    op = TREE_OPERAND (op, 0);

  if (TREE_CODE (op) != CALL_EXPR || call_expr_nargs (op) != 1)
    return op;
  tree fn = CALL_EXPR_FN (op);
  if (fn == NULL_TREE || TREE_CODE (fn) != ADDR_EXPR)
    return op;
  tree fndecl = TREE_OPERAND (fn, 0);
  if (TREE_CODE (fndecl) != FUNCTION_DECL || !DECL_CONV_FN_P (fndecl))
    return op;

  tree object = oa_strip_conversion_operator_call (CALL_EXPR_ARG (op, 0));
  if (TREE_CODE (object) == ADDR_EXPR)
    object = TREE_OPERAND (object, 0);
  return object;
}

static tree
oa_strip_conversion_call (tree op)
{
  while (TREE_CODE (op) == NON_LVALUE_EXPR || TREE_CODE (op) == NOP_EXPR
	 || TREE_CODE (op) == CONVERT_EXPR || TREE_CODE (op) == VIEW_CONVERT_EXPR)
    op = TREE_OPERAND (op, 0);

  /* A non-trivially-copyable by-value argument is passed as the address
     of a materialized temporary even at this pre-genericize stage
     (found via direct testing -- matches the GIMPLE-level "invisible
     reference" ABI convention, already visible this early), so the
     TARGET_EXPR below can arrive wrapped in one extra ADDR_EXPR; peel
     it, the same way a conversion-operator call's own receiver is
     peeled just below.  */
  if (TREE_CODE (op) == ADDR_EXPR && TREE_CODE (TREE_OPERAND (op, 0)) == TARGET_EXPR)
    op = TREE_OPERAND (op, 0);

  if (TREE_CODE (op) == TARGET_EXPR)
    {
      tree init = TREE_OPERAND (op, 1);
      if (init != NULL_TREE && TREE_CODE (init) != AGGR_INIT_EXPR)
	return oa_strip_conversion_call (init);
      if (init != NULL_TREE && TREE_CODE (init) == AGGR_INIT_EXPR)
	{
	  tree fn = AGGR_INIT_EXPR_FN (init);
	  if (fn != NULL_TREE && TREE_CODE (fn) == ADDR_EXPR)
	    {
	      tree fndecl = TREE_OPERAND (fn, 0);
	      int nargs = aggr_init_expr_nargs (init);
	      if (fndecl != NULL_TREE && TREE_CODE (fndecl) == FUNCTION_DECL
		  && (DECL_COPY_CONSTRUCTOR_P (fndecl)
		      || DECL_MOVE_CONSTRUCTOR_P (fndecl))
		  && nargs >= 1)
		{
		  /* A copy/move constructor has exactly one user-visible
		     parameter (the source reference), always last in
		     AGGR_INIT_EXPR's own argument list -- found via direct
		     testing that an extra, leading compiler-internal
		     argument (an unrelated CONVERT_EXPR of a VOID_CST) can
		     precede it here, so the *count* isn't reliably 1, only
		     the *position* of the real source is reliably last.
		     That source argument is itself an ADDR_EXPR when it's a
		     reference (the same by-reference representation the
		     conversion-operator receiver below is unwrapped from)
		     -- peel it too before recursing, so the result is the
		     bare decl, not its address.  */
		  tree source
		    = oa_strip_conversion_call (AGGR_INIT_EXPR_ARG (init, nargs - 1));
		  if (TREE_CODE (source) == ADDR_EXPR)
		    source = TREE_OPERAND (source, 0);
		  return source;
		}
	    }
	}
      return op;
    }

  if (TREE_CODE (op) != CALL_EXPR || call_expr_nargs (op) != 1)
    return op;
  tree fn = CALL_EXPR_FN (op);
  if (fn == NULL_TREE || TREE_CODE (fn) != ADDR_EXPR)
    return op;
  tree fndecl = TREE_OPERAND (fn, 0);
  if (TREE_CODE (fndecl) != FUNCTION_DECL || !DECL_CONV_FN_P (fndecl))
    return op;

  tree object = oa_strip_conversion_call (CALL_EXPR_ARG (op, 0));
  if (TREE_CODE (object) == ADDR_EXPR)
    object = TREE_OPERAND (object, 0);
  return object;
}

static bool
oa_provably_nonzero_p (tree expr, oa_env &env)
{
  if (expr == NULL_TREE || expr == error_mark_node)
    return false;

  STRIP_ANY_LOCATION_WRAPPER (expr);

  /* Reading a by-reference lambda-capture proxy's *value* (as opposed
     to its address, which oa_provable_p's own capture-proxy comment
     explains at length) arrives here as INDIRECT_REF(proxy) directly --
     found empirically (via debug_tree, the same method used throughout
     this pass): unlike '&proxy' (a bare NOP_EXPR/CONVERT_EXPR
     reference-to-pointer conversion, no ADDR_EXPR node), an ordinary
     *read* of a reference-typed proxy needs an explicit dereference
     node, with no intervening conversion wrapper of its own. Strip it
     before the generic conversion-stripping loop below, which
     otherwise has nothing that would recognize or remove it.  */
  if (oa_iile_outer_env && TREE_CODE (expr) == INDIRECT_REF)
    {
      tree op = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));
      if (VAR_P (op) && TREE_CODE (TREE_TYPE (op)) == REFERENCE_TYPE
	  && is_capture_proxy (op))
	expr = op;
    }

  /* D4324, Increment V: VIEW_CONVERT_EXPR is how a contract condition's
     access to a decl is wrapped to present it as const-qualified (see
     the comment above the VIEW_CONVERT_EXPR-building code near line
     570) -- decl-identity-preserving, so safe (and necessary) to strip
     through here exactly like the ordinary conversion wrappers, found
     via direct testing that a const-qualified parameter's own fact
     otherwise silently failed to be recognized in a contract
     condition.  */
  while (TREE_CODE (expr) == NON_LVALUE_EXPR
	 || TREE_CODE (expr) == NOP_EXPR
	 || TREE_CODE (expr) == CONVERT_EXPR
	 || TREE_CODE (expr) == VIEW_CONVERT_EXPR)
    expr = TREE_OPERAND (expr, 0);

  /* A class-typed operand reached via an implicit conversion operator
     (e.g. 'w != 0' where w's type has 'operator int() const') --
     recognize the same underlying decl a bare scalar-typed operand
     already would.  */
  expr = oa_strip_conversion_call (expr);

  if (TREE_CODE (expr) == INTEGER_CST)
    return !integer_zerop (expr);

  if (VAR_P (expr) || TREE_CODE (expr) == PARM_DECL)
    {
      if (oa_iile_outer_env && is_capture_proxy (expr))
	{
	  tree captured = DECL_CAPTURED_VARIABLE (expr);
	  if (captured)
	    return oa_provably_nonzero_p (captured, *oa_iile_outer_env);
	}
      if (env.nz_provable_p (expr))
	return true;
      /* Increment E1: a provable value range that excludes zero
	 entirely (e.g. established by a preceding 'if (n > 0)' guard)
	 is also sufficient, supplementing the narrow nz-fact map above
	 rather than replacing it.  */
      oa_range_fact fact;
      if (env.range_get (expr, &fact) && fact.base == NULL_TREE
	  && ((fact.has_lo && fact.lo > 0) || (fact.has_hi && fact.hi < 0)))
	return true;
      return false;
    }

  /* Item 6: an ordinary call whose callee's own non-ignored, conveyor
     postcondition has a bare 'r != 0'/'0 != r' conjunct naming its
     result identifier. A comparison-based guarantee (e.g. 'r > 0') is
     already covered above, via oa_get_range's own item-6 fallback
     (below) feeding the range-fact check just above -- this only
     covers the literal-!= shape that isn't representable as a single
     interval.  */
  if (TREE_CODE (expr) == CALL_EXPR && oa_call_postcondition_nonzero_p (expr))
    return true;

  return false;
}


/* D4324/P2680 item 8, Increment E1: determine EXPR's provable value
   range (or a pointer's provable offset into a named array, once
   Increment E2 starts populating that side of the fact -- this
   function doesn't yet special-case pointer arithmetic itself, only
   plain integer ranges, per E1's own scope), writing it to *OUT and
   returning true if any fact is known. Mirrors oa_provable_p/oa_
   provably_nonzero_p's shape exactly (capture-proxy redirect via
   OA_IILE_OUTER_ENV, including the same INDIRECT_REF unwrap for
   reading a captured value directly), plus straight-line propagation
   through a constant addition/subtraction ('i + 1', with 'i' itself
   range-tracked) -- deliberately only a *constant* shift, not general
   interval-plus-interval arithmetic (needed for e.g. a pointer
   incremented by another range-tracked amount, which is Increment E2's
   concern, not this one's). Does not recurse into a statically-
   resolvable immediately-invoked closure the way oa_provable_p does
   (item 5) -- an even narrower residual gap than oa_provably_nonzero_p
   already has, noted for a future increment rather than this one.  */

static bool
oa_get_range (tree expr, oa_env &env, oa_range_fact *out)
{
  if (expr == NULL_TREE || expr == error_mark_node)
    return false;

  STRIP_ANY_LOCATION_WRAPPER (expr);

  if (oa_iile_outer_env && TREE_CODE (expr) == INDIRECT_REF)
    {
      tree op = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));
      if (VAR_P (op) && TREE_CODE (TREE_TYPE (op)) == REFERENCE_TYPE
	  && is_capture_proxy (op))
	expr = op;
    }

  /* D4324, Increment V: VIEW_CONVERT_EXPR is how a contract condition's
     access to a decl is wrapped to present it as const-qualified (see
     the comment above the VIEW_CONVERT_EXPR-building code near line
     570) -- decl-identity-preserving, so safe (and necessary) to strip
     through here exactly like the ordinary conversion wrappers, found
     via direct testing that a const-qualified parameter's own fact
     otherwise silently failed to be recognized in a contract
     condition.  */
  /* Also strips CLEANUP_POINT_EXPR (the full-expression temporary-
     cleanup scope a compound-assignment operator's own RHS call can
     arrive wrapped in -- confirmed via direct testing: 'x += v.size ()'
     reached here with the CALL_EXPR wrapped this way, while the
     equivalent spelled-out 'x = x + v.size ()' did not, silently
     defeating the CALL_EXPR case below's own oa_call_postcondition_
     range_p purely due to that wrapping).  */
  while (TREE_CODE (expr) == NON_LVALUE_EXPR
	 || TREE_CODE (expr) == NOP_EXPR
	 || TREE_CODE (expr) == CONVERT_EXPR
	 || TREE_CODE (expr) == VIEW_CONVERT_EXPR
	 || TREE_CODE (expr) == CLEANUP_POINT_EXPR)
    expr = TREE_OPERAND (expr, 0);

  /* A class-typed operand reached via an implicit conversion operator
     (e.g. 'q.operator int()') -- recognize the same underlying decl a
     bare scalar-typed operand already would, so its range fact (if
     any) is found the same way.  */
  expr = oa_strip_conversion_call (expr);

  if (TREE_CODE (expr) == INTEGER_CST)
    {
      out->base = NULL_TREE;
      out->has_lo = out->has_hi = true;
      out->lo = out->hi = wi::to_widest (expr);
      return true;
    }

  if (VAR_P (expr) || TREE_CODE (expr) == PARM_DECL)
    {
      if (oa_iile_outer_env && is_capture_proxy (expr))
	{
	  tree captured = DECL_CAPTURED_VARIABLE (expr);
	  if (captured)
	    return oa_get_range (captured, *oa_iile_outer_env, out);
	}
      return env.range_get (expr, out);
    }

  if (TREE_CODE (expr) == PLUS_EXPR || TREE_CODE (expr) == MINUS_EXPR)
    {
      tree op0 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));
      tree op1 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 1));

      oa_range_fact base_fact;
      widest_int k;
      if (TREE_CODE (op1) == INTEGER_CST && oa_get_range (op0, env, &base_fact))
	k = wi::to_widest (op1);
      else if (TREE_CODE (expr) == PLUS_EXPR && TREE_CODE (op0) == INTEGER_CST
	       && oa_get_range (op1, env, &base_fact))
	k = wi::to_widest (op0);
      else
	/* '<constant> - decl' negates the whole range rather than
	   shifting it -- not a simple shift, left unrecognized.  */
	return false;

      if (base_fact.base != NULL_TREE)
	/* A pointer's own array-offset shifting through plain PLUS_EXPR/
	   MINUS_EXPR doesn't happen at this stage -- see POINTER_PLUS_EXPR
	   below, confirmed empirically to be what pointer arithmetic
	   actually lowers to here, unlike plain integer addition.  */
	return false;

      if (TREE_CODE (expr) == MINUS_EXPR)
	k = -k;

      out->base = NULL_TREE;
      out->has_lo = base_fact.has_lo;
      out->has_hi = base_fact.has_hi;
      if (out->has_lo)
	out->lo = base_fact.lo + k;
      if (out->has_hi)
	out->hi = base_fact.hi + k;
      return true;
    }

  /* Increment E2: '&arr[index]', forming a pointer's initial tracked
     offset into a named array -- confirmed empirically to be a plain
     ADDR_EXPR(ARRAY_REF(arr, index)) at this stage, the same shape
     oa_scan_array_bounds_in_expr's own ARRAY_TYPE-base case already
     recognizes for direct array-element access; here it's the
     address-of form specifically, establishing a fact for a pointer
     variable rather than validating an immediate access.  */
  if (TREE_CODE (expr) == ADDR_EXPR)
    {
      tree op = TREE_OPERAND (expr, 0);
      if (TREE_CODE (op) == ARRAY_REF)
	{
	  /* ARR reaches here as a VIEW_CONVERT_EXPR whenever the array's
	     own element type needed a const-qualified view for this
	     access (the same wrapping this function's own top-of-function
	     comment already documents finding for a plain scalar decl,
	     confirmed via direct testing to affect a reassignment's own
	     RHS -- e.g. 'p = &arr[k];' after an earlier 'const int*
	     p = arr;' -- even though the very same expression reaches
	     here unwrapped when it's a declaration's own initializer
	     instead) -- strip it (and the same ordinary conversion
	     wrappers this function already strips off EXPR itself above)
	     before the VAR_P check below, or a reassigned pointer's
	     array-offset fact silently, incorrectly fails to establish.  */
	  tree arr = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (op, 0));
	  while (TREE_CODE (arr) == NON_LVALUE_EXPR || TREE_CODE (arr) == NOP_EXPR
		 || TREE_CODE (arr) == CONVERT_EXPR
		 || TREE_CODE (arr) == VIEW_CONVERT_EXPR)
	    arr = TREE_OPERAND (arr, 0);
	  tree index = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (op, 1));
	  if (VAR_P (arr) && TREE_CODE (TREE_TYPE (arr)) == ARRAY_TYPE)
	    {
	      oa_range_fact idx_fact;
	      if (!oa_get_range (index, env, &idx_fact) || idx_fact.base != NULL_TREE)
		return false;
	      out->base = arr;
	      out->has_lo = idx_fact.has_lo;
	      out->has_hi = idx_fact.has_hi;
	      out->lo = idx_fact.lo;
	      out->hi = idx_fact.hi;
	      return true;
	    }
	}
      return false;
    }

  /* Increment E2: pointer arithmetic ('p + n'/'p - n') -- confirmed
     empirically to already be POINTER_PLUS_EXPR at this pre-genericize
     stage (unlike plain integer addition, which stays PLUS_EXPR), with
     the offset operand *always* addition (subtraction folds the
     negated byte count directly into the constant, via ordinary
     two's-complement wraparound in the offset's own sizetype) and
     *already scaled to bytes* rather than elements -- both found only
     by direct inspection, not guessable from the plain-integer case's
     own shape. wi::to_offset (not to_widest) is required to read the
     byte constant as the signed value it actually represents: sizetype
     is nominally unsigned, so to_widest would zero-extend a
     wraparound-encoded negative offset into a huge positive number
     instead of recovering the intended negative byte count.

     Increment W: a *variable* offset ('p + i', as opposed to a literal
     constant like 'p + 2') arrives, confirmed empirically the same
     way, as NOP_EXPR(MULT_EXPR(i, elt_size)) -- the scaling
     multiplication by the pointee's own size, done before the literal
     case's own constant-folding ever gets a chance to collapse it into
     a single byte count, wrapped in a widening conversion to the
     pointer-difference type. Handled by recognizing this shape and
     using I's own tracked range (already in *elements*, not bytes, so
     no further division is needed) instead of dividing a byte
     constant.  */
  if (TREE_CODE (expr) == POINTER_PLUS_EXPR)
    {
      tree ptr = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));
      tree byte_off = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 1));

      oa_range_fact ptr_fact;
      if (!oa_get_range (ptr, env, &ptr_fact) || ptr_fact.base == NULL_TREE)
	return false;

      tree pointee = TREE_TYPE (TREE_TYPE (expr));
      tree elt_size_tree = TYPE_SIZE_UNIT (pointee);
      if (!elt_size_tree || TREE_CODE (elt_size_tree) != INTEGER_CST)
	return false;
      widest_int elt_size = wi::to_widest (elt_size_tree);
      if (elt_size == 0)
	return false;

      oa_range_fact elt_off;
      if (TREE_CODE (byte_off) == INTEGER_CST)
	{
	  widest_int byte_k = widest_int::from (wi::to_offset (byte_off),
						 SIGNED);
	  widest_int rem;
	  widest_int elt_k = wi::divmod_trunc (byte_k, elt_size, SIGNED, &rem);
	  if (rem != 0)
	    /* Not an exact multiple of the pointee's size -- e.g. a
	       reinterpreted or otherwise non-array-normalized offset.
	       Conservatively unprovable rather than guessing.  */
	    return false;
	  elt_off.base = NULL_TREE;
	  elt_off.has_lo = elt_off.has_hi = true;
	  elt_off.lo = elt_off.hi = elt_k;
	}
      else
	{
	  tree inner = byte_off;
	  while (TREE_CODE (inner) == NOP_EXPR || TREE_CODE (inner) == CONVERT_EXPR)
	    inner = TREE_OPERAND (inner, 0);
	  if (TREE_CODE (inner) != MULT_EXPR)
	    return false;
	  tree mop0 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (inner, 0));
	  tree mop1 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (inner, 1));
	  tree index_expr;
	  if (TREE_CODE (mop1) == INTEGER_CST
	      && wi::to_widest (mop1) == elt_size)
	    index_expr = mop0;
	  else if (TREE_CODE (mop0) == INTEGER_CST
		   && wi::to_widest (mop0) == elt_size)
	    index_expr = mop1;
	  else
	    /* Not a plain 'index * elt_size' scaling -- conservatively
	       unprovable rather than guessing.  */
	    return false;
	  if (!oa_get_range (index_expr, env, &elt_off)
	      || elt_off.base != NULL_TREE)
	    return false;
	}

      out->base = ptr_fact.base;
      out->has_lo = ptr_fact.has_lo && elt_off.has_lo;
      out->has_hi = ptr_fact.has_hi && elt_off.has_hi;
      if (out->has_lo)
	out->lo = ptr_fact.lo + elt_off.lo;
      if (out->has_hi)
	out->hi = ptr_fact.hi + elt_off.hi;
      return true;
    }

  /* Increment E4: recurse into a statically-resolvable, immediately-
     invoked closure (item 5), the same restriction oa_provable_p/oa_
     provably_nonzero_p already apply -- one level deep only (see
     oa_iile_outer_env's own comment).  */
  tree closure_obj;
  if (!oa_iile_outer_env && oa_iile_call_p (expr, &closure_obj))
    return oa_resolve_iile_range (expr, env, out);

  /* Item 6: an ordinary call whose callee's own non-ignored, conveyor
     postcondition(s) imply a value range for its result identifier.  */
  if (TREE_CODE (expr) == CALL_EXPR && oa_call_postcondition_range_p (expr, env, out))
    return true;

  return false;
}

/* Strip EXPR the same way oa_get_relational below does, without
   consulting any fact -- used to normalize both sides of a relational
   obligation's own substituted call-site arguments down to a bare
   decl before comparing them for identity (oa_relational_fact's own
   RHS is always recorded as a bare decl by oa_establish_shared_
   substrate_self_trust, so the caller-side argument must be reduced
   the same way before matching).  Also looks through a class-typed
   operand's own implicit conversion operator and/or by-value copy
   materialization (oa_strip_conversion_call) -- a relational
   obligation's own substituted call argument goes through exactly
   that shape whenever the parameter it substitutes is class-typed,
   not just a bare scalar decl.  */

static tree
oa_strip_to_relational_operand (tree expr)
{
  if (expr == NULL_TREE)
    return NULL_TREE;
  STRIP_ANY_LOCATION_WRAPPER (expr);
  return oa_strip_conversion_call (expr);
}

/* D4324 Commit 4: an exact-point oa_range_fact for VAL -- a relational
   fact's own OFFSET at establishment time (always exactly 0: an
   actually-observed comparison, never yet shifted) or a literal
   constant shift amount, represented as a degenerate interval so the
   same OFFSET field covers both a fixed and a variable shift.  */

static oa_range_fact
oa_range_fact_exact (widest_int val)
{
  oa_range_fact r;
  r.base = NULL_TREE;
  r.has_lo = r.has_hi = true;
  r.lo = r.hi = val;
  return r;
}

/* D4324 Commit 4: negate an interval in place (swap and negate lo/hi) --
   MINUS_EXPR's own shift is subtracted, so its interval must be negated
   before accumulating, the same sign-flip oa_get_range's own scalar K
   already gets for MINUS_EXPR, generalized to a whole interval.  */

static void
oa_range_fact_negate (oa_range_fact &r)
{
  bool old_has_lo = r.has_lo, old_has_hi = r.has_hi;
  widest_int old_lo = r.lo, old_hi = r.hi;
  r.has_lo = old_has_hi;
  r.has_hi = old_has_lo;
  if (r.has_lo)
    r.lo = -old_hi;
  if (r.has_hi)
    r.hi = -old_lo;
}

/* D4324 Commit 4: accumulate SHIFT into ACC via ordinary interval
   addition (ACC.lo/hi each widen by SHIFT's own lo/hi) -- unknown
   (has_lo/has_hi false) on either side is contagious, same convention
   oa_range_fact uses everywhere else.  */

static void
oa_range_fact_accumulate (oa_range_fact &acc, const oa_range_fact &shift)
{
  acc.has_lo = acc.has_lo && shift.has_lo;
  acc.has_hi = acc.has_hi && shift.has_hi;
  if (acc.has_lo)
    acc.lo += shift.lo;
  if (acc.has_hi)
    acc.hi += shift.hi;
}

/* D4324 Commit 4: do A and B (both interpreted as oa_relational_fact/
   oa_call_relational_fact's own OFFSET field) represent the exact same
   interval? Used by relational_merge_with/call_relational_merge_with,
   which need full agreement across a branch merge, the same as CODE/
   RHS.  */

static bool
oa_range_fact_equal (const oa_range_fact &a, const oa_range_fact &b)
{
  return a.has_lo == b.has_lo && a.has_hi == b.has_hi
	 && (!a.has_lo || a.lo == b.lo) && (!a.has_hi || a.hi == b.hi);
}

/* EXPR's own established *relational* fact, if any -- the oa_
   relational_fact analogue of oa_get_range immediately above, sharing
   its exact wrapper-stripping discipline (a contract condition's own
   access to a decl is wrapped the same way regardless of which fact
   shape is being consulted).  Unlike oa_get_range, still no IILE/item-6
   handling (a relational fact is only ever recorded directly against a
   bare decl by oa_establish_shared_substrate_self_trust, never derived
   from a callee's own postcondition -- see this plan's own explicit
   non-goal on postcondition-established relational facts), but D4324
   Commit 2 adds the same PLUS_EXPR/MINUS_EXPR-by-constant handling
   oa_get_range already has: 'base +/- k' (K a literal) shifts a copy of
   BASE's own established fact by accumulating K into OUT->offset,
   mirroring oa_get_range's own identical arithmetic exactly (same
   restriction: only a literal-constant shift, not two tracked
   quantities combined -- Commit 4 covers that case separately). See
   oa_env_check_relational_fact_1's own comment for why OFFSET's sign
   must be checked at consult time, since it's no longer always 0.  */

static bool
oa_get_relational (tree expr, oa_env &env, oa_relational_fact *out)
{
  if (expr == NULL_TREE || expr == error_mark_node)
    return false;

  tree e = oa_strip_to_relational_operand (expr);
  if (VAR_P (e) || TREE_CODE (e) == PARM_DECL)
    return env.relational_get (e, out);

  if (TREE_CODE (e) == PLUS_EXPR || TREE_CODE (e) == MINUS_EXPR)
    {
      tree op0 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (e, 0));
      tree op1 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (e, 1));

      oa_range_fact shift;
      if (oa_get_relational (op0, env, out))
	{
	  /* D4324 Commit 4: OP1 (the shift amount) need not itself be a
	     literal -- a second tracked variable's own established range
	     works too, using its interval directly instead of a single
	     exact point (Commit 2's own original, narrower restriction).  */
	  if (TREE_CODE (op1) == INTEGER_CST)
	    shift = oa_range_fact_exact (wi::to_widest (op1));
	  else if (!oa_get_range (op1, env, &shift) || shift.base != NULL_TREE)
	    return false;
	}
      else if (TREE_CODE (e) == PLUS_EXPR && oa_get_relational (op1, env, out))
	{
	  if (TREE_CODE (op0) == INTEGER_CST)
	    shift = oa_range_fact_exact (wi::to_widest (op0));
	  else if (!oa_get_range (op0, env, &shift) || shift.base != NULL_TREE)
	    return false;
	}
      else
	/* '<constant> - decl' negates rather than shifts -- not a simple
	   shift, left unrecognized, same as oa_get_range.  */
	return false;

      if (TREE_CODE (e) == MINUS_EXPR)
	oa_range_fact_negate (shift);
      oa_range_fact_accumulate (out->offset, shift);
      return true;
    }

  return false;
}

/* The call analogue of oa_get_relational immediately above, for
   oa_call_relational_fact/oa_env::call_relational_get instead.  */

static bool
oa_get_call_relational (tree expr, oa_env &env, oa_call_relational_fact *out)
{
  if (expr == NULL_TREE || expr == error_mark_node)
    return false;

  tree e = oa_strip_to_relational_operand (expr);
  if (VAR_P (e) || TREE_CODE (e) == PARM_DECL)
    return env.call_relational_get (e, out);

  if (TREE_CODE (e) == PLUS_EXPR || TREE_CODE (e) == MINUS_EXPR)
    {
      tree op0 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (e, 0));
      tree op1 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (e, 1));

      oa_range_fact shift;
      if (oa_get_call_relational (op0, env, out))
	{
	  if (TREE_CODE (op1) == INTEGER_CST)
	    shift = oa_range_fact_exact (wi::to_widest (op1));
	  else if (!oa_get_range (op1, env, &shift) || shift.base != NULL_TREE)
	    return false;
	}
      else if (TREE_CODE (e) == PLUS_EXPR
	       && oa_get_call_relational (op1, env, out))
	{
	  if (TREE_CODE (op0) == INTEGER_CST)
	    shift = oa_range_fact_exact (wi::to_widest (op0));
	  else if (!oa_get_range (op0, env, &shift) || shift.base != NULL_TREE)
	    return false;
	}
      else
	return false;

      if (TREE_CODE (e) == MINUS_EXPR)
	oa_range_fact_negate (shift);
      oa_range_fact_accumulate (out->offset, shift);
      return true;
    }

  return false;
}

/* Intersect REFINED with the bound implied by "decl CODE val" (CODE
   already normalized to read left-to-right as "decl CODE val", any
   asserted-false negation already applied by the caller) -- the shared
   core of oa_refine_single_comparison below, factored out so
   -fcontract-conveyor-proofs's own combined-range precondition check
   (oa_handle_call_conveyor_proof_obligation) can fold several
   precondition conjuncts on the same parameter into one required
   interval the same way a postcondition's own conjuncts already get
   folded into one established interval (oa_call_postcondition_range_p).
   Only ever tightens (intersects), never widens -- same discipline as
   the rest of this pass.  */

static void
oa_tighten_range_bound (oa_range_fact &refined, tree_code code, widest_int val)
{
  switch (code)
    {
    case LT_EXPR:
      if (!refined.has_hi || refined.hi > val - 1)
	{ refined.has_hi = true; refined.hi = val - 1; }
      break;
    case LE_EXPR:
      if (!refined.has_hi || refined.hi > val)
	{ refined.has_hi = true; refined.hi = val; }
      break;
    case GT_EXPR:
      if (!refined.has_lo || refined.lo < val + 1)
	{ refined.has_lo = true; refined.lo = val + 1; }
      break;
    case GE_EXPR:
      if (!refined.has_lo || refined.lo < val)
	{ refined.has_lo = true; refined.lo = val; }
      break;
    case EQ_EXPR:
      refined.has_lo = refined.has_hi = true;
      refined.lo = refined.hi = val;
      break;
    default:
      break;
    }
}

/* D4324/P2680 item 8, Increment E1: refine a single top-level
   comparison CONJUNCT ('<', '<=', '>', '>=', '=='; '!=' isn't usefully
   representable as a single interval and is left alone) between a
   range-trackable decl and a constant-or-exactly-known-range
   expression, folding the implied bound into ENV -- ASSERTED_TRUE
   selects whether CONJUNCT is being assumed true (the then-branch) or
   false (its logical negation, the else-branch of a single, non-
   compound condition only -- see oa_refine_range_for_condition below
   for why a compound '&&' condition's else-branch is never refined at
   all). Only ever *tightens* an existing fact (intersects with what's
   already known), never widens it. Silently does nothing for any
   unrecognized shape -- always safe, just occasionally conservative,
   the discipline used throughout this whole pass.  */

/* Forward-declared: full definitions are much further below (this
   pass's own ptr->field/call-range shape recognizers), needed here by
   oa_refine_single_comparison to recognize those two shapes directly.  */
static bool oa_symbolic_comparison_conjunct_shape
  (tree conjunct, tree *field_out, tree *ptr_expr_out, tree_code *code_out,
   tree *const_val_out);
static tree oa_strip_symbolic_ptr_expr (tree ptr_expr);
static bool oa_call_range_conjunct_shape
  (tree conjunct, tree *receiver_out, tree *callee_out, tree_code *code_out,
   tree *const_val_out, bool allow_symbolic_accessor);
static bool oa_match_shifted_comparison_against_call
  (tree conjunct, tree *param_out, tree_code *code_out,
   tree *rhs_receiver_out, tree *rhs_callee_out, widest_int *offset_out,
   bool allow_symbolic_accessor);
static bool oa_match_type_bounded_comparison
  (tree conjunct, tree *decl_out, tree_code *code_out);

/* Forward-declared: full definition is much further below (needs the
   VAR_DECL-admitting sibling matchers, defined near the exported
   matchers they mirror); needed here by oa_handle_assertion_stmt to
   check a contract_assert's own condition against already-established
   ambient facts before trusting it (see that function's own comment,
   and this helper's own comment further below, for the full
   rationale).  */
static oa_proof_result oa_check_assertion_conjunct_against_env
  (tree conjunct, oa_env &env, bool require_conveyor);

/* The last-resort fallback inside oa_refine_single_comparison below:
   "DECL OP <exactly-known point>" refined into ENV's own plain, untagged
   range map (m_range_map) -- factored out into its own function because
   it's also needed by oa_handle_precondition_stmt's own SYMBOLIC_OK
   block (see that call site's own comment): unlike the ptr->field/call-
   range/relational/call-relational blocks earlier in oa_refine_single_
   comparison (each hardcoding CONVEYOR_ESTABLISHED true, or gated on
   ALLOW_SYMBOLIC_ACCESSOR false, because they're the "if-condition
   establishment, real executed code" shapes), a plain range fact carries
   no provenance tag at all -- it's trusted uniformly everywhere once
   established, exactly like an ordinary 'if (i < N)' branch's own range
   refinement. So this one piece is safe to reuse verbatim for a
   symbolic-only precondition's own self-trust, while the rest of
   oa_refine_single_comparison is not (calling the whole function again
   there would re-establish the ptr->field/call-range/relational/call-
   relational facts oa_establish_shared_substrate_self_trust already
   established correctly, but with the wrong CONVEYOR_ESTABLISHED tag and
   the wrong accessor gate).  */

static void
oa_refine_scalar_range_only (tree conjunct, oa_env &env, bool asserted_true)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  enum tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return;

  tree op0_scalar = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
  tree op1_scalar = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 1));
  tree op0 = oa_strip_conversion_call (op0_scalar);
  tree op1 = oa_strip_conversion_call (op1_scalar);

  tree decl0 = (TREE_CODE (op0) == INIT_EXPR || TREE_CODE (op0) == MODIFY_EXPR)
    ? STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (op0, 0)) : op0;
  tree decl1 = (TREE_CODE (op1) == INIT_EXPR || TREE_CODE (op1) == MODIFY_EXPR)
    ? STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (op1, 0)) : op1;

  tree decl, other;
  bool flipped;
  if ((VAR_P (decl0) || TREE_CODE (decl0) == PARM_DECL)
      && INTEGRAL_TYPE_P (TREE_TYPE (op0_scalar)))
    decl = decl0, other = op1, flipped = false;
  else if ((VAR_P (decl1) || TREE_CODE (decl1) == PARM_DECL)
	   && INTEGRAL_TYPE_P (TREE_TYPE (op1_scalar)))
    decl = decl1, other = op0, flipped = true;
  else
    return;

  oa_range_fact other_fact;
  if (!oa_get_range (other, env, &other_fact) || other_fact.base != NULL_TREE
      || !other_fact.has_lo || !other_fact.has_hi
      || other_fact.lo != other_fact.hi)
    return;
  widest_int val = other_fact.lo;

  if (flipped)
    switch (code)
      {
      case LT_EXPR: code = GT_EXPR; break;
      case LE_EXPR: code = GE_EXPR; break;
      case GT_EXPR: code = LT_EXPR; break;
      case GE_EXPR: code = LE_EXPR; break;
      default: break;
      }

  if (!asserted_true)
    switch (code)
      {
      case LT_EXPR: code = GE_EXPR; break;
      case LE_EXPR: code = GT_EXPR; break;
      case GT_EXPR: code = LE_EXPR; break;
      case GE_EXPR: code = LT_EXPR; break;
      default: return; /* NOT(decl == val) is decl != val -- skip.  */
      }

  oa_range_fact refined;
  if (!env.range_get (decl, &refined))
    {
      refined.base = NULL_TREE;
      refined.has_lo = refined.has_hi = false;
    }

  oa_tighten_range_bound (refined, code, val);
  env.range_set (decl, refined);
}

static void
oa_refine_single_comparison (tree conjunct, oa_env &env, bool asserted_true)
{
  /* D4324, item 8's overflow check: establish a type-bound witness (see
     oa_type_bound_fact's own comment) independently of, and before, the
     mutually-exclusive-by-construction blocks below -- unlike those,
     this doesn't return on a match, since a conjunct recognized here
     can also, separately, be recognized by one of them (e.g. 'i < n'
     with i a PARM_DECL matches both this and oa_match_comparison_
     against_param below), and both facts are wanted side by side, not
     just whichever one happens to run first.  */
  {
    tree decl;
    tree_code code;
    if (oa_match_type_bounded_comparison (conjunct, &decl, &code))
      {
	if (!asserted_true)
	  switch (code)
	    {
	    case LT_EXPR: code = GE_EXPR; break;
	    case LE_EXPR: code = GT_EXPR; break;
	    case GT_EXPR: code = LE_EXPR; break;
	    case GE_EXPR: code = LT_EXPR; break;
	    default: gcc_unreachable ();
	    }
	env.type_bound_set (decl, /*upper=*/code == LT_EXPR || code == LE_EXPR,
			      /*lower=*/code == GT_EXPR || code == GE_EXPR);
      }
  }

  /* D4324: a ptr->field or call-range conjunct in an ordinary runtime
     'if'/ternary condition (e.g. 'if (ptr->count < N)' / 'if (i < v.size
     ())') refines the same shared substrate a declared contract already
     establishes into (m_contract_field_range_map/m_contract_call_range_
     map) -- previously these two shapes were only ever established from
     a declared pre<>/post<>, never from ordinary control flow, unlike a
     bare decl (handled below). Tried first, before the bare-decl-
     specific parsing below (which requires one side to already be a
     PARM_DECL/VAR_DECL and would never match either shape anyway).  A
     fact established this way is tagged conveyor_established = true
     unconditionally when it's brand new: it comes from a real,
     actually-executed comparison, not from any contract's own control
     object, so it's exactly as trustworthy as a real conveyor contract's
     own established fact -- conveyor's own strict consult can use it
     freely. Tightening an *existing* fact instead keeps whatever
     provenance that fact already had (never upgrades a symbolic-only
     fact to conveyor-established merely because it was also refined by
     a real comparison).  */
  {
    tree field, ptr_expr, field_const;
    tree_code field_code;
    if (oa_symbolic_comparison_conjunct_shape (conjunct, &field, &ptr_expr,
						&field_code, &field_const)
	&& TREE_CODE (field_const) == INTEGER_CST)
      {
	ptr_expr = oa_strip_symbolic_ptr_expr (ptr_expr);
	tree identity;
	if (oa_object_identity_decl (ptr_expr, &identity)
	    || oa_field_slot_identity (ptr_expr, env, &identity)
	    || oa_array_slot_identity (ptr_expr, env, &identity)
	    || oa_field_object_identity (ptr_expr, env, &identity))
	  {
	    identity = env.alias_find (identity);
	    if (!asserted_true)
	      switch (field_code)
		{
		case LT_EXPR: field_code = GE_EXPR; break;
		case LE_EXPR: field_code = GT_EXPR; break;
		case GT_EXPR: field_code = LE_EXPR; break;
		case GE_EXPR: field_code = LT_EXPR; break;
		default: return; /* NOT(field == val) -- skip.  */
		}
	    oa_contract_field_range_fact established;
	    oa_range_fact refined;
	    bool conveyor_established = true;
	    if (env.contract_field_range_get (identity, field, &established))
	      {
		refined = established.range;
		conveyor_established = established.conveyor_established;
	      }
	    else
	      {
		refined.base = NULL_TREE;
		refined.has_lo = refined.has_hi = false;
	      }
	    oa_tighten_range_bound (refined, field_code,
				     wi::to_widest (field_const));
	    env.contract_field_range_set (identity, field, refined,
					   conveyor_established);
	  }
	return;
      }
  }

  {
    tree receiver_expr, callee, call_const;
    tree_code call_code;
    if (oa_call_range_conjunct_shape (conjunct, &receiver_expr, &callee,
				       &call_code, &call_const,
				       /*allow_symbolic_accessor=*/false)
	&& TREE_CODE (call_const) == INTEGER_CST)
      {
	receiver_expr = oa_strip_symbolic_ptr_expr (receiver_expr);
	tree identity;
	if (oa_object_identity_decl (receiver_expr, &identity)
	    || oa_field_slot_identity (receiver_expr, env, &identity)
	    || oa_array_slot_identity (receiver_expr, env, &identity)
	    || oa_field_object_identity (receiver_expr, env, &identity))
	  {
	    identity = env.alias_find (identity);
	    if (!asserted_true)
	      switch (call_code)
		{
		case LT_EXPR: call_code = GE_EXPR; break;
		case LE_EXPR: call_code = GT_EXPR; break;
		case GT_EXPR: call_code = LE_EXPR; break;
		case GE_EXPR: call_code = LT_EXPR; break;
		default: return; /* NOT(call == val) -- skip.  */
		}
	    oa_contract_field_range_fact established;
	    oa_range_fact refined;
	    bool conveyor_established = true;
	    if (env.contract_call_range_get (identity, callee, &established))
	      {
		refined = established.range;
		conveyor_established = established.conveyor_established;
	      }
	    else
	      {
		refined.base = NULL_TREE;
		refined.has_lo = refined.has_hi = false;
	      }
	    oa_tighten_range_bound (refined, call_code,
				     wi::to_widest (call_const));
	    env.contract_call_range_set (identity, callee, refined,
					  conveyor_established);
	  }
	return;
      }
  }

  /* D4324 Commit 3: the same if-condition establishment just above,
     generalized to the three relational shapes -- previously these
     were only ever established from a declared pre<>, never from
     ordinary control flow (see oa_env_check_relational_fact_1's own
     comment on why 'i < v.size ()' proven this way is exactly as
     trustworthy as a real conveyor contract's own fact). Overwrite, not
     tighten, unlike the two numeric blocks above: there's no numeric
     range to combine for a symbolic RHS, the same way self-trust
     establishment (contracts.cc's own precondition-establishing loop)
     already just overwrites. Tried in this order because each matcher
     is mutually exclusive with the other two by construction (bare-
     param-vs-bare-param, bare-param-vs-call, call-vs-call can never
     all match the same conjunct), so relative order among these three
     doesn't affect correctness.  */
  {
    tree param, other;
    tree_code rel_code;
    if (oa_match_comparison_against_param (conjunct, &param, &rel_code, &other))
      {
	if (!asserted_true)
	  switch (rel_code)
	    {
	    case LT_EXPR: rel_code = GE_EXPR; break;
	    case LE_EXPR: rel_code = GT_EXPR; break;
	    case GT_EXPR: rel_code = LE_EXPR; break;
	    case GE_EXPR: rel_code = LT_EXPR; break;
	    default: return; /* NOT(param == other) -- skip.  */
	    }
	env.relational_set (param, rel_code, other, /*conveyor_established=*/true,
			      oa_range_fact_exact (0));
	return;
      }
  }

  {
    tree param, rhs_receiver, rhs_callee;
    tree_code rel_code;
    if (oa_match_comparison_against_call (conjunct, &param, &rel_code,
					    &rhs_receiver, &rhs_callee,
					    /*allow_symbolic_accessor=*/false))
      {
	if (!asserted_true)
	  switch (rel_code)
	    {
	    case LT_EXPR: rel_code = GE_EXPR; break;
	    case LE_EXPR: rel_code = GT_EXPR; break;
	    case GT_EXPR: rel_code = LE_EXPR; break;
	    case GE_EXPR: rel_code = LT_EXPR; break;
	    default: return;
	    }
	env.call_relational_set (param, rel_code, rhs_receiver, rhs_callee,
				   /*conveyor_established=*/true,
				   oa_range_fact_exact (0));
	return;
      }
  }

  /* D4324 Part 4: 'RECEIVER.ACCESSOR () - PARAM OP <literal>' (or its
     mirror 'PARAM - RECEIVER.ACCESSOR () OP <literal>') -- a genuinely
     fresh observation feeding the same call_relational_set path as the
     bare "PARAM OP CALL ()" block just above, but with a nonzero OFFSET
     (see oa_match_shifted_comparison_against_call's own comment for the
     algebra). Tried after that block (mutually exclusive with it and
     the other two above by construction -- this one requires a MINUS_
     EXPR on one side, which none of the other three shapes have).  */
  {
    tree param, rhs_receiver, rhs_callee;
    tree_code rel_code;
    widest_int offset;
    if (oa_match_shifted_comparison_against_call (conjunct, &param, &rel_code,
						     &rhs_receiver, &rhs_callee,
						     &offset,
						     /*allow_symbolic_accessor=*/false))
      {
	if (!asserted_true)
	  switch (rel_code)
	    {
	    case LT_EXPR: rel_code = GE_EXPR; break;
	    case LE_EXPR: rel_code = GT_EXPR; break;
	    case GT_EXPR: rel_code = LE_EXPR; break;
	    case GE_EXPR: rel_code = LT_EXPR; break;
	    default: return;
	    }
	env.call_relational_set (param, rel_code, rhs_receiver, rhs_callee,
				   /*conveyor_established=*/true,
				   oa_range_fact_exact (offset));
	return;
      }
  }

  {
    tree lhs_receiver, lhs_callee, rhs_receiver, rhs_callee;
    tree_code call_code;
    if (oa_match_call_against_call (conjunct, &lhs_receiver, &lhs_callee,
				      &call_code, &rhs_receiver, &rhs_callee,
				      /*allow_symbolic_accessor=*/false))
      {
	/* Only the three-function chain self-trust establishment already
	   uses (contracts.cc:12889's own establishment loop), not the
	   four-function chain the consult side needs -- matching that
	   loop's own scope exactly, the same asymmetry the pre-existing
	   field-range/call-range establishment-vs-consult pair already
	   has.  */
	tree identity;
	if (oa_object_identity_decl (lhs_receiver, &identity)
	    || oa_field_slot_identity (lhs_receiver, env, &identity)
	    || oa_array_slot_identity (lhs_receiver, env, &identity))
	  {
	    identity = env.alias_find (identity);
	    if (!asserted_true)
	      switch (call_code)
		{
		case LT_EXPR: call_code = GE_EXPR; break;
		case LE_EXPR: call_code = GT_EXPR; break;
		case GT_EXPR: call_code = LE_EXPR; break;
		case GE_EXPR: call_code = LT_EXPR; break;
		default: return;
		}
	    env.call_call_relational_set (identity, lhs_callee, call_code,
					    rhs_receiver, rhs_callee,
					    /*conveyor_established=*/true);
	  }
	return;
      }
  }

  oa_refine_scalar_range_only (conjunct, env, asserted_true);
}

/* D4324/P2680: closes the "assignment-in-condition" gap left open when
   the IF_STMT/COND_EXPR condition-operand gap was first fixed
   (item 7) -- an assignment written directly inside an if/ternary
   condition (e.g. 'if ((i = compute()) > 0)') previously never updated
   the assigned decl's tracked facts (is_object_address-provability,
   "provably nonzero," or -- since Increment E -- its value/array-offset
   range) at all, since the condition is deliberately *not* dispatched
   through the full oa_walk_stmt switch (which would re-trigger CALL_
   EXPR/INIT_EXPR's own internal call/div-mod/array-bounds scans a
   second time -- the exact double-scan/double-report bug found and
   fixed while first closing this same gap for item 7).

   Deliberately narrow: only ever recognizes a *single* assignment that
   either *is* COND itself, or is directly nested as the operand of a
   comparison/negation wrapping it ('(i = compute()) > 0', '(p = f())
   != nullptr', '!(p = f())'). Since Increment K, COND is one top-level
   '&&' conjunct at a time (oa_process_condition calls this once per
   conjunct, not once on the whole condition) -- this correctly extends
   coverage to an assignment nested in *any* conjunct of a '&&'-chain
   ('a && (i = compute()) > 0'), while still never looking inside a
   '||', where whether the assignment actually executes at all depends
   on short-circuit evaluation of an earlier operand (oa_collect_
   conjuncts itself never decomposes '||', so a '||'-nested conjunct
   reaches this function, if at all, only as one opaque, undecomposed
   unit that won't match either recognized shape). Blindly tracking a
   fact from an assignment that might not have run would be unsound;
   this restriction avoids that question entirely by simply not
   looking there. Reuses the *tracking* logic oa_walk_stmt's own
   INIT_EXPR/MODIFY_EXPR case already has (oa_provable_p/oa_provably_
   nonzero_p/oa_get_range), not its scanning logic (already run
   separately by oa_process_condition, per conjunct, in evaluation
   order).  */

static void
oa_track_condition_assignment (tree cond, oa_env &env)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (cond);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR || TREE_CODE (c) == NON_LVALUE_EXPR
	 || TREE_CODE (c) == NOP_EXPR || TREE_CODE (c) == CONVERT_EXPR
	 || TREE_CODE (c) == TRUTH_NOT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  tree assign = NULL_TREE;
  if (TREE_CODE (c) == INIT_EXPR || TREE_CODE (c) == MODIFY_EXPR)
    assign = c;
  else if (TREE_CODE (c) == EQ_EXPR || TREE_CODE (c) == NE_EXPR
	   || TREE_CODE (c) == LT_EXPR || TREE_CODE (c) == LE_EXPR
	   || TREE_CODE (c) == GT_EXPR || TREE_CODE (c) == GE_EXPR)
    {
      tree op0 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
      tree op1 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 1));
      if (TREE_CODE (op0) == INIT_EXPR || TREE_CODE (op0) == MODIFY_EXPR)
	assign = op0;
      else if (TREE_CODE (op1) == INIT_EXPR || TREE_CODE (op1) == MODIFY_EXPR)
	assign = op1;
    }
  if (!assign)
    return;

  tree lhs = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (assign, 0));
  tree rhs = TREE_OPERAND (assign, 1);
  if (!(VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL))
    return;

  if (POINTER_TYPE_P (TREE_TYPE (lhs)))
    {
      env.set (lhs, oa_provable_p (rhs, env));
      oa_range_fact fact;
      if (oa_get_range (rhs, env, &fact))
	env.range_set (lhs, fact);
      else
	env.range_invalidate (lhs);
    }
  else if (INTEGRAL_TYPE_P (TREE_TYPE (lhs)))
    {
      env.nz_set (lhs, oa_provably_nonzero_p (rhs, env));
      oa_range_fact fact;
      if (oa_get_range (rhs, env, &fact))
	env.range_set (lhs, fact);
      else
	env.range_invalidate (lhs);
      /* D4324 Commit 2: same relational-fact derive-or-invalidate
	 treatment as oa_walk_stmt's own INIT_EXPR/MODIFY_EXPR case (see
	 that case's own comment on the PLUS_EXPR/MINUS_EXPR transfer) --
	 unlike that case, nothing already invalidated LHS's own stale
	 relational facts before this function runs, so both the derive
	 and the invalidate-on-no-match belong here directly, including
	 invalidating the *other* shape's map whenever one of them
	 successfully resolves (a fact is only ever one shape at a
	 time).  */
      oa_relational_fact rel_fact;
      oa_call_relational_fact call_rel_fact;
      if (oa_get_relational (rhs, env, &rel_fact))
	{
	  env.relational_set (lhs, rel_fact.code, rel_fact.rhs,
				rel_fact.conveyor_established, rel_fact.offset);
	  env.call_relational_invalidate_involving (lhs);
	}
      else if (oa_get_call_relational (rhs, env, &call_rel_fact))
	{
	  env.call_relational_set (lhs, call_rel_fact.code,
				     call_rel_fact.rhs_receiver,
				     call_rel_fact.rhs_callee,
				     call_rel_fact.conveyor_established,
				     call_rel_fact.offset);
	  env.relational_invalidate_involving (lhs);
	}
      else
	{
	  env.relational_invalidate_involving (lhs);
	  env.call_relational_invalidate_involving (lhs);
	}
    }
}

/* D4324, item 8: whether oa_scan_div_mod_in_expr/oa_scan_array_bounds_
   in_expr/oa_scan_overflow_in_expr (all three, below and further down)
   should actually call error_at right now. Default true; saved,
   cleared, and restored around each of oa_handle_loop's own
   "invalidated re-walk" fixed-point passes (mirroring how OA_RETURN_
   TRACKING/OA_SYMBOLIC_CODEGEN_ACTIVE are already saved/cleared/
   restored around those exact same calls, for the exact same reason:
   those re-walks exist purely to compute a compile-time fact by
   re-walking the loop's own repeated part with one decl artificially
   invalidated -- they do not represent any real execution of the loop,
   so nothing about them should be user-visible).

   Found via direct testing, while fixing oa_collect_loop_targets's own
   pre-existing gap (see that function's own comment) to correctly
   recognize a loop counter written as '++i' rather than 'i = i + 1' as
   a reassignment target: making that recognition correct meant a
   decl's own item-8 violation, reachable from the loop's repeated
   part, is now walked once for the real diagnostic pass *and* once per
   invalidated re-walk (one per distinct reassigned decl, across both
   the boolean-provability and range fixed-point loops) -- each with no
   suppression of its own, so the exact same error_at call fired once
   per walk, with no deduplication, printing the identical diagnostic
   two to five times over for what should be a single, real violation.
   This was already latently true before that fix too (confirmed via
   direct testing with two plainly '='-reassigned decls, no '++'/'--'
   involved at all) -- simply never triggered by any existing test,
   since the "excess errors" mismatch this produces would have failed
   any dg-error-asserting test that happened to combine a genuinely
   unprovable item-8 violation with more than one reassigned decl in
   the same loop.  */

static bool oa_diagnostics_active = true;

/* D4324/P2680 item 8, narrow version: check every TRUNC_DIV_EXPR/
   TRUNC_MOD_EXPR div/mod operation within *EXPR (an arbitrary
   sub-expression -- a RETURN_EXPR's value or an INIT_EXPR/MODIFY_EXPR's
   RHS, the same two hook points oa_scan_calls_in_expr uses for item 7),
   erroring on any whose divisor isn't provably nonzero (ENV, per
   oa_provably_nonzero_p above). Only meaningful within a function
   actually declared with the 'conveyor' keyword -- checked by the
   caller, not here.  */

static void
oa_scan_div_mod_in_expr (tree *expr, oa_env &env)
{
  if (!oa_diagnostics_active)
    return;
  cp_walk_tree (expr, [](tree *tp, int *, void *data_) -> tree
    {
      oa_env *e = (oa_env *) data_;
      tree t = *tp;
      if (t == NULL_TREE || t == error_mark_node
	  || (TREE_CODE (t) != TRUNC_DIV_EXPR && TREE_CODE (t) != TRUNC_MOD_EXPR))
	return NULL_TREE;
      tree divisor = TREE_OPERAND (t, 1);
      if (!oa_provably_nonzero_p (divisor, *e))
	error_at (EXPR_LOCATION (t), "divisor %qE not provably nonzero in "
		  "a conveyor function", divisor);
      return NULL_TREE;
    }, &env, NULL);
}

/* D4324/P2680 item 8: get INDEX's value as an oa_range_fact -- either a
   literal constant (the exact point) or (Increment E2) a range-tracked
   expression, requiring a plain integer range (no array base of its
   own; an index that's itself a tracked pointer-into-array offset
   would be a nonsensical index expression).  */

static bool
oa_index_range (tree index, oa_env &env, oa_range_fact *out)
{
  if (TREE_CODE (index) == INTEGER_CST)
    {
      out->base = NULL_TREE;
      out->has_lo = out->has_hi = true;
      out->lo = out->hi = wi::to_widest (index);
      return true;
    }
  return oa_get_range (index, env, out) && out->base == NULL_TREE;
}

/* D4324/P2680 item 8: check that TOTAL (an already-computed offset
   range relative to ARRAY_TYPE's own start, in elements) is fully
   within [0, N) of ARRAY_TYPE's declared bound, erroring at T
   (labelled WHAT in the diagnostic) if not -- either because the
   bound itself isn't staticaly known, TOTAL is not fully bounded in
   both directions, or the bounded range exceeds the array either
   below zero or beyond its last valid index.  */

static void
oa_check_offset_in_bounds (tree t, tree array_type, const oa_range_fact &total,
			    const char *what, tree diag_expr,
			    bool allow_one_past_end = false)
{
  tree max = TYPE_DOMAIN (array_type) ? TYPE_MAX_VALUE (TYPE_DOMAIN (array_type))
				       : NULL_TREE;
  if (!total.has_lo || !total.has_hi || !max || TREE_CODE (max) != INTEGER_CST)
    {
      if (diag_expr)
	error_at (EXPR_LOCATION (t), "%s %qE not provably in-bounds in a "
		  "conveyor function", what, diag_expr);
      else
	error_at (EXPR_LOCATION (t), "%s not provably in-bounds in a "
		  "conveyor function", what);
      return;
    }
  /* D4324/P2680 item 8, Increment W: forming a one-past-the-end pointer
     (offset == N, for an N-element array) is well-defined -- only
     *dereferencing* it isn't -- so the pointer-arithmetic-formation
     check (ALLOW_ONE_PAST_END) allows an upper bound of N, one past
     the array's own last valid index (MAX_W, itself N-1).  Every other
     caller (ARRAY_REF/INDIRECT_REF, i.e. actual access) keeps the
     strict N-1 bound.  */
  widest_int max_w = wi::to_widest (max);
  widest_int hi_limit = allow_one_past_end ? max_w + 1 : max_w;
  if (total.lo < 0 || total.hi > hi_limit)
    {
      if (diag_expr)
	error_at (EXPR_LOCATION (t), "%s %qE out of bounds in a "
		  "conveyor function", what, diag_expr);
      else
	error_at (EXPR_LOCATION (t), "%s out of bounds in a conveyor "
		  "function", what);
    }
}

/* D4324/P2680 item 8, the pointer-arithmetic array-bound rule: check
   every ARRAY_REF and INDIRECT_REF within *EXPR.

   An ARRAY_REF (subscript syntax, 'arr[i]'/'p[i]') is *always*
   validated, whether its base is a directly-named fixed-size
   ARRAY_TYPE (the narrow version's original scope: the index must be
   a compile-time constant, or, as of Increment E2, a range-tracked
   expression, fully within [0, N)) or a POINTER_TYPE whose own value
   is itself tracked (Increment E2) as an offset into a named array
   (oa_get_range) -- combined with the subscript's own index range via
   ordinary interval addition, then checked the same way. Subscript
   syntax unambiguously signals "this is array access," so an
   unprovable case is always an error, never silently skipped.

   An INDIRECT_REF ('*p', no subscript syntax) is different: when P
   already carries a tracked array-offset fact (Increment E2), "was
   this dereference formed via array-related pointer arithmetic, and
   is the offset still in range" is checked exactly like ARRAY_REF
   above. Otherwise (Increment W2, closing a real UB-freedom gap: a
   conveyor function could freely dereference an arbitrary, unproven
   pointer, with none of "no unprovable UB" actually enforced), P must
   instead be provably an object address in its own right (oa_provable_p
   -- 'this', '&local', or an is_object_address(p)-trusted parameter):
   dereferencing a single proven object needs no further bound, but a
   pointer that's neither array-offset-tracked nor is_object_address-
   proven is unprovable UB and is now an error, the same as an
   unprovable ARRAY_REF index above. Note this does NOT extend to the
   POINTER_PLUS_EXPR case below (pointer arithmetic *formation*, 'p+n'
   with no dereference): is_object_address(p) proves only that P alone
   denotes a valid object, nothing about whether P+N is itself a
   formable pointer, so applying the same fallback there would be
   unsound, not just incomplete -- deliberately left unaddressed here,
   a disclosed, narrower follow-on gap.

   Only meaningful within a function actually declared with the
   'conveyor' keyword -- checked by the caller, not here.  */

static void
oa_scan_array_bounds_in_expr (tree *expr, oa_env &env)
{
  if (!oa_diagnostics_active)
    return;
  cp_walk_tree (expr, [](tree *tp, int *, void *data_) -> tree
    {
      oa_env *e = (oa_env *) data_;
      tree t = *tp;
      if (t == NULL_TREE || t == error_mark_node)
	return NULL_TREE;

      if (TREE_CODE (t) == ARRAY_REF)
	{
	  tree base = TREE_OPERAND (t, 0);
	  tree base_type = TREE_TYPE (base);
	  tree index = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (t, 1));

	  if (TREE_CODE (base_type) == ARRAY_TYPE)
	    {
	      oa_range_fact idx_fact;
	      if (!oa_index_range (index, *e, &idx_fact))
		{
		  error_at (EXPR_LOCATION (t), "array index %qE not "
			    "provably in-bounds in a conveyor function",
			    index);
		  return NULL_TREE;
		}
	      oa_check_offset_in_bounds (t, base_type, idx_fact, "array index", index);
	    }
	  else if (POINTER_TYPE_P (base_type))
	    {
	      oa_range_fact base_fact;
	      oa_range_fact idx_fact;
	      if (!oa_get_range (base, *e, &base_fact) || base_fact.base == NULL_TREE
		  || !oa_index_range (index, *e, &idx_fact))
		{
		  error_at (EXPR_LOCATION (t), "array index %qE not "
			    "provably in-bounds in a conveyor function",
			    index);
		  return NULL_TREE;
		}
	      oa_range_fact total;
	      total.base = NULL_TREE;
	      total.has_lo = base_fact.has_lo && idx_fact.has_lo;
	      total.has_hi = base_fact.has_hi && idx_fact.has_hi;
	      if (total.has_lo)
		total.lo = base_fact.lo + idx_fact.lo;
	      if (total.has_hi)
		total.hi = base_fact.hi + idx_fact.hi;
	      oa_check_offset_in_bounds (t, TREE_TYPE (base_fact.base), total,
					 "array index", index);
	    }
	  return NULL_TREE;
	}

      if (TREE_CODE (t) == INDIRECT_REF)
	{
	  tree base = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (t, 0));
	  if (!POINTER_TYPE_P (TREE_TYPE (base)))
	    return NULL_TREE;
	  oa_range_fact base_fact;
	  if (oa_get_range (base, *e, &base_fact) && base_fact.base != NULL_TREE)
	    {
	      oa_check_offset_in_bounds (t, TREE_TYPE (base_fact.base), base_fact,
					 "pointer dereference", NULL_TREE);
	      return NULL_TREE;
	    }
	  /* A REFERENCE_TYPE'd operand (POINTER_TYPE_P above matches both
	     POINTER_TYPE and REFERENCE_TYPE) reaching here needs no
	     is_object_address fallback below: unlike a raw pointer, a
	     bound reference is guaranteed valid for its own entire
	     lifetime by the language itself -- there is no "null"/
	     "reseated" reference, so reading one is never itself the
	     unprovable-UB case this fallback exists for. Confirmed via
	     direct testing this matters in practice, not just in theory:
	     without this exclusion, reading an ordinary by-reference
	     lambda-capture proxy (itself a REFERENCE_TYPE local, e.g.
	     '[&]() { return b; }' reading a by-reference-captured 'b')
	     was wrongly flagged as an unprovable "pointer" dereference.  */
	  if (TREE_CODE (TREE_TYPE (base)) != POINTER_TYPE)
	    return NULL_TREE;
	  /* No tracked array-offset fact -- fall back to is_object_address
	     provability (Increment W2): dereferencing a pointer proven to
	     denote a single valid object (this/&var/an is_object_address-
	     trusted parameter) needs no further bound check; anything
	     else is unprovable UB.  See this function's own header
	     comment for why this does not extend to POINTER_PLUS_EXPR.  */
	  if (!oa_provable_p (base, *e))
	    /* The implicit INDIRECT_REF inside 'p->field' (as opposed to
	       an explicit '*p') is compiler-synthesized with no location
	       of its own -- confirmed via direct testing that plain
	       EXPR_LOCATION (t) silently falls back to UNKNOWN_LOCATION
	       for it, printing a location-free diagnostic; EXPR_LOC_OR_LOC
	       falls back to INPUT_LOCATION the same way this file's
	       neighboring diagnostics already do for a similar reason.  */
	    error_at (EXPR_LOC_OR_LOC (t, input_location), "pointer "
		      "dereference of %qE not provably valid in a "
		      "conveyor function", base);
	  return NULL_TREE;
	}

      /* D4324/P2680 item 8, Increment W: pointer-arithmetic-*formation*
	 checking -- validate 'p + n'/'p - n' itself, independent of
	 whether the result is ever dereferenced, per the "one-past-the-
	 end is formable but not dereferencable" rule (the strict N-1
	 upper bound above is for actual access; ALLOW_ONE_PAST_END below
	 relaxes it by one for mere formation). Only applies when the
	 base pointer already carries a tracked array-offset fact (an
	 ordinary pointer with no such fact is untouched, exactly like
	 the INDIRECT_REF case above); reuses oa_get_range's own
	 POINTER_PLUS_EXPR handling directly (already computes the
	 resulting shifted range) rather than re-deriving the byte-to-
	 element arithmetic here.  */
      if (TREE_CODE (t) == POINTER_PLUS_EXPR)
	{
	  oa_range_fact base_fact;
	  tree ptr = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (t, 0));
	  if (!oa_get_range (ptr, *e, &base_fact) || base_fact.base == NULL_TREE)
	    return NULL_TREE;
	  oa_range_fact result_fact;
	  if (!oa_get_range (t, *e, &result_fact))
	    {
	      error_at (EXPR_LOCATION (t), "pointer arithmetic not provably "
			"in-bounds in a conveyor function");
	      return NULL_TREE;
	    }
	  oa_check_offset_in_bounds (t, TREE_TYPE (result_fact.base), result_fact,
				     "pointer arithmetic", NULL_TREE,
				     /*allow_one_past_end=*/true);
	  return NULL_TREE;
	}

      return NULL_TREE;
    }, &env, NULL);
}

/* D4324, item 8's overflow check: is a shift of exactly 1 (INCREASING
   true for '++x'/'x++'/'x + 1', false for '--x'/'x--'/'x - 1')
   provably safe for X? Two independent routes, either sufficient:

   1. Numeric: X's own established range (oa_get_range, the same lookup
      div/mod/array-bounds already use) already proves it -- for
      INCREASING, X's own upper bound is at least one below TYPE's own
      max; for decreasing, X's own lower bound is at least one above
      TYPE's own min.
   2. Type-bound witness (see oa_type_bound_fact's own comment): X has,
      at some point still in scope, been compared less-than (for
      INCREASING) or greater-than (for decreasing) *something* of a
      no-wider integral type -- sufficient by the type invariant alone,
      no numeric fact about that something ever needed.

   Neither route succeeding means this is genuinely unprovable, not
   merely "this particular check didn't think to look" -- the caller
   errors in that case, matching this pass's own "must be provable, else
   treated as unprovable" discipline throughout.  */

static bool
oa_provably_safe_unit_shift_p (tree x, bool increasing, oa_env &env)
{
  /* X, as handed in from oa_scan_overflow_in_expr, is TREE_OPERAND of a
     raw INCREMENT/DECREMENT_EXPR node -- often a bare decl, but not
     always: a by-value parameter's own access can
     arrive wrapped in a value-preserving NOP_EXPR/VIEW_CONVERT_EXPR
     (see oa_underlying_param_operand's own comment for the same shape
     and why), which env.type_bound_get's own raw, pointer-identity-
     keyed hash_map lookup would never match against the *canonical*
     decl the witness was actually established under. Strip to the same
     canonical form oa_underlying_param_operand itself resolves to
     before either lookup, so both consult the same key establishment
     used (found via direct testing: without this, a by-value
     parameter's own wrapped access silently never found its own type-
     bound witness, always falling through to "unprovable").  */
  x = oa_strip_to_relational_operand (x);
  tree type = TREE_TYPE (x);
  oa_range_fact fact;
  if (oa_get_range (x, env, &fact))
    {
      if (increasing && fact.has_hi
	  && fact.hi + 1 <= wi::to_widest (TYPE_MAX_VALUE (type)))
	return true;
      if (!increasing && fact.has_lo
	  && fact.lo - 1 >= wi::to_widest (TYPE_MIN_VALUE (type)))
	return true;
    }
  oa_type_bound_fact witness;
  if (env.type_bound_get (x, &witness))
    {
      if (increasing && witness.has_upper_witness)
	return true;
      if (!increasing && witness.has_lower_witness)
	return true;
    }
  return false;
}

/* D4324/P2680 item 8: the third mandatory UB-freedom scan, alongside
   oa_scan_div_mod_in_expr/oa_scan_array_bounds_in_expr immediately
   above -- signed-integer-overflow-capable operators. Covers the unary
   shift-by-one operators (INCREMENT/DECREMENT, pre and post) -- the
   concrete motivating case (see the plan's own Context section:
   'pre<conveyor_assert_v>(x++ < 2048)') -- NEGATE_EXPR ('-x'), and
   general binary PLUS_EXPR/MINUS_EXPR/MULT_EXPR ('a + b', 'x + 1',
   etc.), each with their own proof strategy (see each case's own
   comment below).

   Only ever applies to INTEGRAL_TYPE_P operands for which overflow is
   actually undefined behavior at all (TYPE_OVERFLOW_UNDEFINED, GCC's
   own existing test -- correctly excludes unsigned types, and respects
   -fwrapv/-ftrapv, rather than this scan reimplementing that logic).
   Pointer increment/arithmetic is a different UB category entirely,
   already oa_scan_array_bounds_in_expr's own POINTER_PLUS_EXPR territory
   (disjoint tree codes from this scan's own, so there is no double-
   checking or gap between the two).  */

static void
oa_scan_overflow_in_expr (tree *expr, oa_env &env)
{
  if (!oa_diagnostics_active)
    return;
  cp_walk_tree (expr, [](tree *tp, int *, void *data_) -> tree
    {
      oa_env *e = (oa_env *) data_;
      tree t = *tp;
      if (t == NULL_TREE || t == error_mark_node)
	return NULL_TREE;

      enum tree_code code = TREE_CODE (t);

      if (code == PREINCREMENT_EXPR || code == POSTINCREMENT_EXPR
	  || code == PREDECREMENT_EXPR || code == POSTDECREMENT_EXPR)
	{
	  tree x = TREE_OPERAND (t, 0);
	  /* See oa_provably_safe_unit_shift_p's own comment on why a
	     by-value parameter's own wrapped access needs stripping to
	     its canonical form before any fact lookup -- applies to this
	     type gate too, though const-qualification alone would not
	     actually change either answer.  */
	  tree stripped = oa_strip_to_relational_operand (x);
	  if (!INTEGRAL_TYPE_P (TREE_TYPE (stripped))
	      || !TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (stripped)))
	    return NULL_TREE;
	  bool increasing
	    = code == PREINCREMENT_EXPR || code == POSTINCREMENT_EXPR;
	  if (!oa_provably_safe_unit_shift_p (x, increasing, *e))
	    error_at (EXPR_LOCATION (t), increasing
		      ? G_("increment of %qE not provably free of overflow "
			   "in a conveyor function")
		      : G_("decrement of %qE not provably free of overflow "
			   "in a conveyor function"), x);
	  return NULL_TREE;
	}

      /* NEGATE_EXPR ('-x'): overflows iff x == TYPE_MIN (negating any
	 other value stays representable), so safe iff x's own established
	 range excludes TYPE_MIN -- i.e. a strict lower bound greater than
	 TYPE_MIN (TYPE_MIN itself must be excluded; TYPE_MIN + 1 negates
	 fine, giving TYPE_MAX). Numeric route only, no type-bound-witness
	 rescue: unlike the unit-shift case above, nothing in this project
	 has ever needed one for negation specifically (no "loop guard"-
	 style pattern motivates one the way it does for '++'/'--').

	 Previously disclosed as a deliberately deferred gap: an earlier
	 attempt found this genuinely rejected already-shipped library code
	 (libstdc++-v3/libsupc++/compare's own 'operator<=>' for
	 std::strong_ordering negated its own tag value, whose actual legal
	 range -- {-1, 0, 1} -- had no established range fact this scan
	 could see). Fixed at the source instead of narrowing the check:
	 that call site now uses __cmp_cat::__saturating_negate, which is
	 provably safe by construction (saturates instead of negating
	 TYPE_MIN outright) rather than relying on an implicit range this
	 scan could never have seen.  */
      if (code == NEGATE_EXPR)
	{
	  tree x = TREE_OPERAND (t, 0);
	  if (!INTEGRAL_TYPE_P (TREE_TYPE (x))
	      || !TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (x)))
	    return NULL_TREE;
	  oa_range_fact fact;
	  bool safe = oa_get_range (x, *e, &fact)
		      && fact.has_lo
		      && fact.lo > wi::to_widest (TYPE_MIN_VALUE (TREE_TYPE (x)));
	  if (!safe)
	    error_at (EXPR_LOCATION (t), "negation of %qE not provably free "
		      "of overflow in a conveyor function", x);
	  return NULL_TREE;
	}

      /* Binary PLUS_EXPR/MINUS_EXPR/MULT_EXPR ('x + 1', 'a + b', etc.): a
	 literal shift of exactly 1 gets first refusal via the same unit-
	 shift rescue '++x'/'--x' use (numeric range OR type-bound witness,
	 see the dedicated comment just below this one, right before that
	 rescue's own call); everything else -- both operands variable, or
	 a literal shift by anything other than 1 -- falls to a numeric-
	 only route (oa_get_range on both operands, interval arithmetic in
	 widest_int -- wide enough that this check's own arithmetic can't
	 itself overflow, the same type OA_RANGE_FACT's own LO/HI already
	 use). An *unconstrained* 'x + 1' (no witness, no range at all on
	 x -- see d4324-conveyor-ok-baseline.C's own history, the single
	 most foundational "ordinary conveyor function" test in the suite)
	 is still, correctly, unprovable by either route: same as the rest
	 of item 8, unprovable is always an error, never silently skipped;
	 any fallout this surfaces against real code (library or test) is
	 fixed on its own merits (an added precondition/range fact, or a
	 narrower scan if a whole category turns out unfixable), not
	 avoided by narrowing the check preemptively.  */
      if (code == PLUS_EXPR || code == MINUS_EXPR || code == MULT_EXPR)
	{
	  tree op0 = TREE_OPERAND (t, 0);
	  tree op1 = TREE_OPERAND (t, 1);
	  if (!INTEGRAL_TYPE_P (TREE_TYPE (t))
	      || !TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (t)))
	    return NULL_TREE;
	  tree type = TREE_TYPE (t);
	  widest_int type_min = wi::to_widest (TYPE_MIN_VALUE (type));
	  widest_int type_max = wi::to_widest (TYPE_MAX_VALUE (type));

	  /* A literal shift of exactly 1 ('x + 1'/'1 + x'/'x - 1') gets the
	     same rescue as '++x'/'--x' before falling to the general,
	     numeric-only route below: the type-bound witness route (sound
	     only for a shift of exactly 1, see oa_provably_safe_unit_shift_
	     p's own header) can prove this even when neither operand has a
	     full numeric range -- e.g. 'pre<>(i < v.size ())' then 'i + 1'
	     has only a witness on i, never a numeric bound on v.size ()'s
	     own return value, which the general route below can't use at
	     all.  This is exactly the part-3 shape the design doc calls out
	     as needing "first refusal" before the general fallback; it was
	     originally paired with '++x' in the unit-shift scan itself but
	     pulled from there (see d4324-conveyor-ok-baseline.C's history)
	     since binary arithmetic wasn't yet handled *at all* at that
	     point -- an unconstrained 'x + 1' with no witness or range
	     either was, correctly, still unprovable regardless, so nothing
	     about restoring it here reopens that regression.  */
	  if (code == PLUS_EXPR || code == MINUS_EXPR)
	    {
	      /* 'x - 1' is already constant-folded to 'x + -1' (a PLUS_EXPR
		 with a literal -1 operand) well before this scan ever runs
		 -- found via direct testing (d4324-conveyor-call-relational-
		 arithmetic-shift.C's own 'i - 1' arrived here as PLUS_EXPR,
		 not MINUS_EXPR, so a check for MINUS_EXPR alone silently
		 never matched it). The MINUS_EXPR/integer_onep case is kept
		 anyway for whatever un-folded shape might still reach here
		 from a path that doesn't constant-fold first.  */
	      tree var_side = NULL_TREE;
	      bool increasing = false;
	      if (code == PLUS_EXPR && integer_onep (op1))
		{ var_side = op0; increasing = true; }
	      else if (code == PLUS_EXPR && integer_onep (op0))
		{ var_side = op1; increasing = true; }
	      else if (code == PLUS_EXPR && integer_minus_onep (op1))
		{ var_side = op0; increasing = false; }
	      else if (code == PLUS_EXPR && integer_minus_onep (op0))
		{ var_side = op1; increasing = false; }
	      else if (code == MINUS_EXPR && integer_onep (op1))
		{ var_side = op0; increasing = false; }
	      if (var_side
		  && oa_provably_safe_unit_shift_p (var_side, increasing, *e))
		return NULL_TREE;
	    }

	  oa_range_fact a;
	  oa_range_fact b;
	  bool have_a = oa_get_range (op0, *e, &a);
	  bool have_b = oa_get_range (op1, *e, &b);

	  bool safe = false;
	  if (code == PLUS_EXPR)
	    {
	      bool hi_ok = have_a && have_b && a.has_hi && b.has_hi
			   && a.hi + b.hi <= type_max;
	      bool lo_ok = have_a && have_b && a.has_lo && b.has_lo
			   && a.lo + b.lo >= type_min;
	      safe = hi_ok && lo_ok;
	    }
	  else if (code == MINUS_EXPR)
	    {
	      bool hi_ok = have_a && have_b && a.has_hi && b.has_lo
			   && a.hi - b.lo <= type_max;
	      bool lo_ok = have_a && have_b && a.has_lo && b.has_hi
			   && a.lo - b.hi >= type_min;
	      safe = hi_ok && lo_ok;
	    }
	  else /* MULT_EXPR: needs a fully two-sided range on both operands
		  -- a one-sided bound isn't enough to bound a product the
		  way it is for a sum/difference (e.g. a huge positive upper
		  bound on one side combined with no lower bound at all
		  leaves the product's own lower extreme totally open).
		  Standard interval multiplication: the result's own two
		  extremes are always among the four corner products.  */
	    {
	      if (have_a && have_b && a.has_lo && a.has_hi && b.has_lo && b.has_hi)
		{
		  widest_int corner0 = a.lo * b.lo;
		  widest_int corner1 = a.lo * b.hi;
		  widest_int corner2 = a.hi * b.lo;
		  widest_int corner3 = a.hi * b.hi;
		  widest_int lo = wi::smin (wi::smin (corner0, corner1),
					      wi::smin (corner2, corner3));
		  widest_int hi = wi::smax (wi::smax (corner0, corner1),
					      wi::smax (corner2, corner3));
		  safe = lo >= type_min && hi <= type_max;
		}
	    }

	  if (!safe)
	    error_at (EXPR_LOCATION (t), "result of %qE not provably free of "
		      "overflow in a conveyor function", t);
	  return NULL_TREE;
	}

      return NULL_TREE;
    }, &env, NULL);
}

/* Walk COND (an arbitrary boolean expression, e.g. a contract's
   condition) looking for std::is_object_address(...) calls; resolve
   each, replacing it in place with boolean_true_node if provable (or,
   when TRUST is set, unconditionally -- see the comment on
   oa_handle_own_precondition below for why a precondition's own
   is_object_address is trusted as an axiom here rather than proven
   against ENV). Returns false (having already diagnosed) if any call
   was found unprovable, or was found outside a conveyor/symbolic/
   non-ignored context (CONVEYOR_OK and SYMBOLIC_OK both false means
   this whole COND is not itself inside a conveyor- or symbolic-checked
   predicate, so any is_object_address found here at all is a well-
   formedness error, provable or not). The prove-vs-trust behavior
   below is unaffected by which of the two made it allowed: a symbolic-
   only post/assert still only proves via ENV's own m_map (i.e. whatever
   a conveyor-established fact already put there -- the allowed
   direction, see .claude/plans/well-we-last-discussed-ethereal-
   duckling.md), never trusted outright the way a precondition's own is.  */

static bool
oa_resolve_condition (tree *cond, oa_env &env, bool conveyor_ok,
		      bool symbolic_ok = false, bool trust = false)
{
  bool ok = true;

  struct walk_data
  {
    oa_env *env; bool conveyor_ok; bool symbolic_ok; bool trust; bool *ok;
  };
  walk_data data = { &env, conveyor_ok, symbolic_ok, trust, &ok };

  cp_walk_tree (cond, [](tree *tp, int *, void *data_) -> tree
    {
      walk_data *d = (walk_data *) data_;
      tree arg;
      if (!is_object_address_call_p (*tp, &arg))
	return NULL_TREE;

      if (!d->conveyor_ok && !d->symbolic_ok)
	{
	  error_at (EXPR_LOCATION (*tp),
		    "%<std::is_object_address%> may only be used inside "
		    "a conveyor- or symbolic-checked predicate");
	  *d->ok = false;
	  /* Replace with a harmless leaf, same reason as the success path
	     below: *TP may be a CALL_EXPR wrapped in a CLEANUP_POINT_EXPR/
	     location wrapper, and is_object_address_call_p matches through
	     such wrappers by stripping a *local copy* -- it doesn't alter
	     *TP itself. Left unchanged, walk_tree would next recurse into
	     *TP's own operand (the unwrapped call), re-invoke this same
	     callback on it, match again, and report the same error a
	     second time.  */
	  *tp = error_mark_node;
	  return NULL_TREE;
	}

      if (!d->trust && !oa_provable_p (arg, *d->env))
	{
	  error_at (EXPR_LOCATION (*tp),
		    "cannot prove %<is_object_address%> for %qE", arg);
	  if (DECL_P (STRIP_ANY_LOCATION_WRAPPER (arg)))
	    inform (DECL_SOURCE_LOCATION (STRIP_ANY_LOCATION_WRAPPER (arg)),
		    "declared here");
	  *d->ok = false;
	  *tp = error_mark_node;
	  return NULL_TREE;
	}

      *tp = boolean_true_node;
      /* Don't recurse into what we just replaced.  */
      return NULL_TREE;
    }, &data, NULL);

  return ok;
}

/* Decompose *COND at top-level && (either spelling), collecting the
   address of each conjunct into CONJUNCTS -- a condition with no
   top-level && is a single conjunct of itself.  Used to find which
   specific is_object_address(E) conjunct(s) of a precondition to seed
   as facts, without needing to guess at E's identity from an arbitrary
   position inside a larger boolean expression.

   Increment K: the classification strips a CLEANUP_POINT_EXPR/location
   wrapper on a *local copy* first (never mutating *COND itself, which
   downstream consumers -- oa_refine_single_comparison et al. -- still
   need to see in its original, wrapped form and unwrap themselves) --
   found necessary because a condition containing an embedded
   assignment ('k >= 0 && k < 5 && (p = &arr[k]) != nullptr') arrives
   with the *entire* condition wrapped in one CLEANUP_POINT_EXPR (the
   assignment's own full-expression temporary-cleanup scope), which
   previously defeated decomposition entirely: the top-level code was
   CLEANUP_POINT_EXPR, never TRUTH_ANDIF_EXPR/TRUTH_AND_EXPR, so the
   whole condition was treated as a single opaque "conjunct" no matter
   how many real && operands it contained.

   D4324/P2680: also strips NOP_EXPR/CONVERT_EXPR/VIEW_CONVERT_EXPR on
   that same local copy, for the same reason -- found via oa_scan_item8_
   in_expr's own new callers reaching SWITCH_STMT_COND, whose own '&&'
   condition arrives as 'CLEANUP_POINT_EXPR (CONVERT_EXPR (TRUTH_ANDIF_
   EXPR (...)))' (the extra CONVERT_EXPR converts the '&&' to the
   switch's own integral selector type, a conversion an ordinary 'if'
   condition never needs, which is why oa_process_condition's own,
   longer-standing use of this function never exposed the gap).  */

static void
oa_collect_conjuncts (tree *cond, vec<tree *> *conjuncts)
{
  tree c = *cond;
  STRIP_ANY_LOCATION_WRAPPER (c);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR
	 || TREE_CODE (c) == NOP_EXPR
	 || TREE_CODE (c) == CONVERT_EXPR
	 || TREE_CODE (c) == VIEW_CONVERT_EXPR)
    {
      c = TREE_OPERAND (c, 0);
      STRIP_ANY_LOCATION_WRAPPER (c);
    }
  if (c && (TREE_CODE (c) == TRUTH_ANDIF_EXPR
	    || TREE_CODE (c) == TRUTH_AND_EXPR))
    {
      oa_collect_conjuncts (&TREE_OPERAND (c, 0), conjuncts);
      oa_collect_conjuncts (&TREE_OPERAND (c, 1), conjuncts);
      return;
    }
  conjuncts->safe_push (cond);
}

/* D4324/P2680 item 8, Increment J: the De Morgan's-dual sibling of
   oa_collect_conjuncts above, decomposing *COND at top-level ||
   (either spelling) instead of && -- a condition with no top-level ||
   is a single disjunct of itself. Used only by oa_process_condition's
   else-branch refinement (Increment K): negating a top-level || gives
   a plain conjunction of negated disjuncts (De Morgan's), each
   refinable the same way an ordinary && conjunct chain already is.
   Strips a CLEANUP_POINT_EXPR/location wrapper on a local copy before
   classifying, for the same reason oa_collect_conjuncts does.  */

static void
oa_collect_disjuncts (tree *cond, vec<tree *> *disjuncts)
{
  tree c = *cond;
  STRIP_ANY_LOCATION_WRAPPER (c);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    {
      c = TREE_OPERAND (c, 0);
      STRIP_ANY_LOCATION_WRAPPER (c);
    }
  if (c && (TREE_CODE (c) == TRUTH_ORIF_EXPR
	    || TREE_CODE (c) == TRUTH_OR_EXPR))
    {
      oa_collect_disjuncts (&TREE_OPERAND (c, 0), disjuncts);
      oa_collect_disjuncts (&TREE_OPERAND (c, 1), disjuncts);
      return;
    }
  disjuncts->safe_push (cond);
}

/* D4324/P2680 item 8, Increment E-divmod: true if CONJUNCT is of the
   form 'E != 0' or '0 != E' (either operand order), with *DECL_OUT set
   to E -- the fact-seeding counterpart of is_object_address_call_p,
   used the same way by oa_handle_precondition_stmt/oa_handle_
   assertion_stmt/oa_handle_postcondition_stmt to recognize a
   nonzero-ness conjunct worth folding into the nz-fact map. Only a
   direct decl reference is recognized as E (matching oa_provably_
   nonzero_p's own scope); '==' is deliberately not handled the
   symmetric way is_object_address's gate is, since 'E == 0' doesn't
   establish nonzero-ness at all.  */

bool
oa_nonzero_conjunct_p (tree conjunct, tree *decl_out)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
  if (TREE_CODE (c) != NE_EXPR)
    return false;

  tree op0 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
  tree op1 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 1));
  /* D4324/P2680, Increment V: strip NOP_EXPR/CONVERT_EXPR wrappers before
     the VAR_P/PARM_DECL check below, mirroring oa_provably_nonzero_p's
     own stripping loop -- found via direct testing that a const-
     qualified parameter (needing an implicit conversion wrapper for the
     comparison) was never recognized as an nz-conjunct at all, silently
     defeating precondition/contract_assert fact-seeding for exactly the
     common 'pre(m != 0)' shape whenever m is declared const (as every
     by-value non-reference postcondition parameter must be).  */
  while (TREE_CODE (op0) == NOP_EXPR || TREE_CODE (op0) == CONVERT_EXPR
	 || TREE_CODE (op0) == NON_LVALUE_EXPR
	 || TREE_CODE (op0) == VIEW_CONVERT_EXPR)
    op0 = TREE_OPERAND (op0, 0);
  while (TREE_CODE (op1) == NOP_EXPR || TREE_CODE (op1) == CONVERT_EXPR
	 || TREE_CODE (op1) == NON_LVALUE_EXPR
	 || TREE_CODE (op1) == VIEW_CONVERT_EXPR)
    op1 = TREE_OPERAND (op1, 0);
  /* Also look through a class-typed operand's own implicit conversion
     operator (e.g. 'w != 0' where w's type has 'operator int() const'),
     the same lookthrough oa_provably_nonzero_p itself now applies.  */
  op0 = oa_strip_conversion_call (op0);
  op1 = oa_strip_conversion_call (op1);
  tree decl, zero;
  if (TREE_CODE (op1) == INTEGER_CST)
    decl = op0, zero = op1;
  else if (TREE_CODE (op0) == INTEGER_CST)
    decl = op1, zero = op0;
  else
    return false;

  if (!integer_zerop (zero) || !(VAR_P (decl) || TREE_CODE (decl) == PARM_DECL))
    return false;

  *decl_out = decl;
  return true;
}

/* True if CONTRACT (an ASSERTION_STMT/PRECONDITION_STMT/POSTCONDITION_STMT)
   is both conveyor and statically non-ignored -- the explicit
   well-formedness gate every is_object_address consultation site must
   check, per the comment on oa_resolve_condition above: never assumed
   structurally from context.  OWNER_FN is the FUNCTION_DECL that
   CONTRACT belongs to (needed by contract_side_of to tell a wrapper
   from the real definition); defaults to CURRENT_FUNCTION_DECL, correct
   for every consultation site that checks a contract belonging to the
   function whose own body is currently being walked (all of them,
   except the call-site precondition-obligation check in
   oa_handle_call_precondition_obligation below, which explicitly
   passes the *callee*).  */

static bool
oa_contract_conveyor_active_p (tree contract, tree owner_fn = NULL_TREE)
{
  tree ctrl = CONTRACT_CONTROL_OBJECT (contract);
  if (!ctrl || !flag_contract_control_objects)
    return false;
  if (!owner_fn)
    owner_fn = current_function_decl;
  contract_check_side side = contract_side_of (contract, owner_fn);
  if (!contract_control_conveyor_like (ctrl, side))
    return false;
  return !contract_control_is_ignored (ctrl, side);
}

/* -fcontract-symbolic-proofs: does CONTRACT (a PRECONDITION_STMT/
   POSTCONDITION_STMT belonging to OWNER_FN) currently have an active
   (non-ignored) symbolic control object?  Mirrors oa_contract_conveyor_
   active_p immediately above exactly, keyed on is_symbolic instead of
   is_conveyor.  */

static bool
oa_contract_symbolic_active_p (tree contract, tree owner_fn = NULL_TREE)
{
  tree ctrl = CONTRACT_CONTROL_OBJECT (contract);
  if (!ctrl || !flag_contract_control_objects)
    return false;
  if (!owner_fn)
    owner_fn = current_function_decl;
  contract_check_side side = contract_side_of (contract, owner_fn);
  if (!contract_control_symbolic_like (ctrl, side))
    return false;
  return !contract_control_is_ignored (ctrl, side);
}

/* True if CONTRACT's own analysis should run regardless of flag_contract_
   conveyor_proofs -- i.e. its control object is analyzed_conveyor or
   proven_conveyor (see those traits' own comment in <contracts>).  Does
   not itself imply conveyor-active; callers already gate on oa_contract_
   conveyor_active_p separately (which now also recognizes both of these
   traits as conveyor).  Symbolic mirror immediately below.  */

static bool
oa_contract_conveyor_analysis_forced_p (tree contract, tree owner_fn = NULL_TREE)
{
  tree ctrl = CONTRACT_CONTROL_OBJECT (contract);
  if (!ctrl || !flag_contract_control_objects)
    return false;
  if (!owner_fn)
    owner_fn = current_function_decl;
  contract_check_side side = contract_side_of (contract, owner_fn);
  return contract_control_analyzed_conveyor (ctrl, side)
	 || contract_control_proven_conveyor (ctrl, side);
}

static bool
oa_contract_symbolic_analysis_forced_p (tree contract, tree owner_fn = NULL_TREE)
{
  tree ctrl = CONTRACT_CONTROL_OBJECT (contract);
  if (!ctrl || !flag_contract_control_objects)
    return false;
  if (!owner_fn)
    owner_fn = current_function_decl;
  contract_check_side side = contract_side_of (contract, owner_fn);
  return contract_control_analyzed_symbolic (ctrl, side)
	 || contract_control_proven_symbolic (ctrl, side);
}

/* True if CONTRACT's control object is proven_conveyor specifically (not
   just analyzed_conveyor): an unproven (OA_UNKNOWN) result for this
   contract is *also* an error, not merely a warning.  Symbolic mirror
   immediately below.  */

static bool
oa_contract_conveyor_strict_p (tree contract, tree owner_fn = NULL_TREE)
{
  tree ctrl = CONTRACT_CONTROL_OBJECT (contract);
  if (!ctrl || !flag_contract_control_objects)
    return false;
  if (!owner_fn)
    owner_fn = current_function_decl;
  contract_check_side side = contract_side_of (contract, owner_fn);
  return contract_control_proven_conveyor (ctrl, side);
}

static bool
oa_contract_symbolic_strict_p (tree contract, tree owner_fn = NULL_TREE)
{
  tree ctrl = CONTRACT_CONTROL_OBJECT (contract);
  if (!ctrl || !flag_contract_control_objects)
    return false;
  if (!owner_fn)
    owner_fn = current_function_decl;
  contract_check_side side = contract_side_of (contract, owner_fn);
  return contract_control_proven_symbolic (ctrl, side);
}

/* Is CONTRACT active for the shared fact-tracking substrate (m_predicate_
   fact_map/m_contract_scalar_range_map/m_contract_field_range_map) --
   i.e. conveyor-active *or* symbolic-active?  These maps are a single,
   domain-neutral substrate shared by both -fcontract-conveyor-proofs and
   -fcontract-symbolic-proofs (the same relationship m_range_map already
   has with the mandatory UB-freedom pass and -fcontract-conveyor-proofs):
   the map doesn't care which contract flavor established a fact, only
   whether *some* fact-tracking-active contract did. Used to gate
   establishment/invalidation (bookkeeping); each feature's own
   *diagnostics* stay gated on its own flag alone (oa_contract_conveyor_
   active_p / oa_contract_symbolic_active_p individually), never this
   combined predicate -- see .claude/plans/well-we-last-discussed-ethereal-
   duckling.md.  */

static bool
oa_contract_fact_tracking_active_p (tree contract, tree owner_fn = NULL_TREE)
{
  return oa_contract_symbolic_active_p (contract, owner_fn)
	 || oa_contract_conveyor_active_p (contract, owner_fn);
}

/* Discharge the call-site precondition-obligation mechanism (item 7):
   the complement of a postcondition being a trusted fact for the caller
   (item 6) -- here, a *precondition's* is_object_address(E) conjunct
   is instead a proof *obligation* the caller must satisfy at each call,
   using its own argument expression substituted positionally for the
   callee's corresponding parameter (DECL_ARGUMENTS/CALL_EXPR_ARG). This
   is what actually earns the "trust" oa_handle_precondition_stmt grants
   a function's own precondition when checking its own body -- every
   caller found here is required to independently prove it.

   Always performed, regardless of whether the runtime enforcement for
   CALLEE's precondition actually executes client-side or definition-
   side (that policy, -fcontracts-client-check et al., only controls
   *where* the runtime check runs; this is a separate, always-active
   compile-time proof requirement -- see the "single unified hook" note
   in the plan). Unprovable here is a hard error at the call site
   itself, not at CALLEE's own definition.

   Only CALLEE's precondition *text* is ever consulted -- never its
   body -- so this stays a purely local, per-call substitution-and-prove
   step with no interprocedural body-walking involved.  Only a bare
   parameter reference (not a general expression built from one or more
   parameters) is supported for E, via direct positional substitution;
   anything more general is conservatively left unproven (silently, not
   an error -- the call-site obligation mechanism just doesn't help in
   that case, exactly like any other "must be provable, else treated as
   unprovable" discipline elsewhere in this pass, except here the
   consequence is simply "no obligation discharged", since the *error*
   for failing to discharge one only fires when a matching bare
   parameter reference *is* found and its substituted argument isn't
   provable).  */

/* Scan CALL's own callee for any conveyor-active contract that forces
   -fcontract-conveyor-proofs-equivalent analysis on regardless of the
   command-line flag (analyzed_conveyor/proven_conveyor), and/or is
   strict (proven_conveyor specifically) -- both OR'd across every such
   contract the callee has, since whether to analyze a call at all is
   an all-or-nothing decision per call site, not selectively per
   contract (matching how oa_contract_fact_tracking_active_p already
   treats conveyor/symbolic activity as a shared, non-selective
   substrate).

   FORCED_OUT considers *both* preconditions and postconditions: a
   postcondition tagged analyzed_conveyor/proven_conveyor must still
   force the shared bookkeeping (fact establishment from that very
   postcondition, in oa_scan_calls_in_expr) on for this call, regardless
   of the flag -- found by direct testing, not assumed: a postcondition-
   only proven_symbolic scenario silently stayed "cannot prove" without
   this, because postcondition establishment never happened without
   flag_contract_symbolic_proofs, even though the precondition
   discharging it upstream was checked correctly regardless.  STRICT_OUT
   stays scoped to preconditions only, since strictness only ever
   affects the precondition-obligation-discharge diagnostics below --
   there is no analogous "unknown" outcome for postcondition
   establishment itself (it either establishes a fact or doesn't; there
   is nothing to warn or error about at the establishing call site).

   FORCED_OUT/STRICT_OUT are only ever set to true, never reset, and
   either may be NULL if the caller only wants the other.  Symbolic
   mirror immediately below.  */

static void
oa_call_conveyor_obligation_status (tree call, bool *forced_out, bool *strict_out)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return;
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!oa_contract_conveyor_active_p (contract, callee))
	continue;
      if (forced_out && oa_contract_conveyor_analysis_forced_p (contract, callee))
	*forced_out = true;
      if (strict_out && PRECONDITION_P (contract)
	  && oa_contract_conveyor_strict_p (contract, callee))
	*strict_out = true;
    }
}

static void
oa_call_symbolic_obligation_status (tree call, bool *forced_out, bool *strict_out)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return;
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!oa_contract_symbolic_active_p (contract, callee))
	continue;
      if (forced_out && oa_contract_symbolic_analysis_forced_p (contract, callee))
	*forced_out = true;
      if (strict_out && PRECONDITION_P (contract)
	  && oa_contract_symbolic_strict_p (contract, callee))
	*strict_out = true;
    }
}

static void
oa_handle_call_precondition_obligation (tree call, oa_env &env)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      if (!oa_contract_conveyor_active_p (contract, callee))
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree arg;
	  if (!is_object_address_call_p (*conjuncts[i], &arg))
	    continue;
	  arg = STRIP_ANY_LOCATION_WRAPPER (arg);

	  /* Positional correspondence between CALLEE's own PARM_DECLs and
	     CALL's actual argument expressions (member-function in-charge/
	     VTT artificial parameters aren't specially reconciled here --
	     out of scope for this increment, matching the plan's free-
	     function-oriented item 7 examples).  */
	  tree substituted = NULL_TREE;
	  unsigned argno = 0;
	  for (tree p = DECL_ARGUMENTS (callee); p; p = DECL_CHAIN (p), ++argno)
	    if (p == arg)
	      {
		if (argno < (unsigned) call_expr_nargs (call))
		  substituted = CALL_EXPR_ARG (call, argno);
		break;
	      }
	  if (!substituted)
	    continue;

	  if (!oa_provable_p (substituted, env))
	    {
	      error_at (EXPR_LOCATION (call),
			"cannot prove %<is_object_address%> for %qE, "
			"required by the precondition of %qD", substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	    }
	}
    }
}

/* Forward-declared: defined later, alongside its public, plugin-facing
   wrapper oa_env_check_comparison; oa_handle_call_conveyor_proof_obligation
   below needs it before that point in the file.  */
static oa_proof_result oa_env_check_comparison_1
  (oa_env &env, tree expr, tree_code cmp, tree const_val);

/* Forward-declared: defined later, right after oa_env_check_comparison_1;
   oa_handle_call_conveyor_proof_obligation below needs it before that
   point in the file.  */
static oa_proof_result oa_env_check_range_subsumption
  (oa_env &env, tree expr, oa_range_fact &req);

/* Forward-declared: defined later, right after oa_call_postcondition_
   range_p; oa_handle_call_conveyor_proof_obligation below needs it
   before that point in the file.  */
static oa_derivation *oa_get_range_derivation (tree expr, oa_env &env);

/* -fcontract-conveyor-proofs: recognize CONJUNCT as "pred_fn (decl)" or
   its negation "!pred_fn (decl)" -- a call to some ordinary FUNCTION_DECL
   with exactly one argument, itself a bare PARM_DECL/VAR_DECL (never a
   general expression -- same "bare decl only" scope limit
   oa_match_simple_comparison already has).  Fills PRED_FN_OUT/
   ARG_DECL_OUT/NEGATED_OUT.  Used both for a precondition's own conjunct
   ("check_it (x)", ARG_DECL_OUT = the callee's own parameter x) and a
   postcondition's ("!check_it (r)", ARG_DECL_OUT = POSTCONDITION_IDENTIFIER,
   NEGATED_OUT = true) -- see oa_handle_call_conveyor_proof_obligation's own
   use of this below for how the two get connected, including how a
   mismatched polarity between the two is a genuine, provable
   contradiction (see oa_env_predicate_result's own comment).

   PRED_FN (e.g.@: "check_it") can be a conveyor-declared function whose
   own definition is never visible here (declared only, defined in some
   other TU) -- none of this ever needs to evaluate or even see PRED_FN's
   body.  The connection this establishes is purely syntactic (the same
   predicate function, named identically, applied to identical-by-
   construction values across a call boundary), which is exactly the
   trust a conveyor-declared predicate is supposed to license.

   Excludes an is_object_address(E)-shaped conjunct explicitly: syntactically
   it's indistinguishable from an ordinary single-arg predicate call (a
   FUNCTION_DECL callee, one PARM_DECL/VAR_DECL argument), but it has its
   own, entirely separate dedicated mechanism throughout this file
   (is_object_address_call_p, the classic/symbolic-only maps, item 6's
   own postcondition-return-value shortcut) with its own diagnostics --
   letting it through here as if it were a named predicate would send it
   through the wrong consult path (looking for a same-named "predicate
   fact" that establishment never stores under that name) instead of the
   right one, producing a spurious "cannot verify" even when the
   dedicated mechanism could prove it immediately.  Found via direct
   testing, once Increment (see .claude/plans/well-we-last-discussed-
   ethereal-duckling.md) first allowed is_object_address inside a
   symbolic (as opposed to only conveyor) contract at all -- conveyor's
   own oa_handle_call_conveyor_proof_obligation already had its own
   explicit 'continue' for this before ever reaching this function, from
   the very first increment that added it; every symbolic-side caller
   needs the same exclusion, so it belongs here once, centrally, rather
   than repeated at each call site.  */

static bool
oa_predicate_conjunct_shape (tree conjunct, tree *pred_fn_out,
			     tree *arg_decl_out, bool *negated_out)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  bool negated = false;
  if (TREE_CODE (c) == TRUTH_NOT_EXPR)
    {
      negated = true;
      c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
      while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
	c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
    }

  if (TREE_CODE (c) != CALL_EXPR || call_expr_nargs (c) != 1)
    return false;

  tree oa_arg;
  if (is_object_address_call_p (c, &oa_arg))
    return false;

  tree fn = cp_get_callee_fndecl_nofold (c);
  if (!fn || TREE_CODE (fn) != FUNCTION_DECL)
    return false;

  tree arg = STRIP_ANY_LOCATION_WRAPPER (CALL_EXPR_ARG (c, 0));
  /* -fcontract-symbolic-proofs: an argument that's already a pointer
     (e.g. 'this' passed to a predicate expecting the same pointer type)
     can still arrive wrapped in a plain conversion node (found via
     direct testing with 'is_opened (this)': a NOP_EXPR, not the
     VIEW_CONVERT_EXPR the const-qualification comment elsewhere in this
     file warns about, but the same class of "decl-identity-preserving
     wrapper" concern) -- strip it here the same way oa_provable_p's own
     generic conversion-stripping loop already does, so 'this' (and
     other pointer-typed arguments reached the same way) aren't silently
     rejected as "not a bare decl".  */
  while (TREE_CODE (arg) == NON_LVALUE_EXPR
	 || TREE_CODE (arg) == NOP_EXPR
	 || TREE_CODE (arg) == CONVERT_EXPR
	 || TREE_CODE (arg) == VIEW_CONVERT_EXPR)
    arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
  /* A class-typed argument reached via its own implicit conversion
     operator (e.g. a wrapper convertible to the reference/pointer type
     a predicate function actually expects) -- same lookthrough as
     every other fact-shape recognizer in this pass.  */
  arg = oa_strip_conversion_call (arg);
  if (TREE_CODE (arg) != PARM_DECL && !VAR_P (arg))
    return false;

  *pred_fn_out = fn;
  *arg_decl_out = arg;
  *negated_out = negated;
  return true;
}

/* Shared consult helper for both -fcontract-conveyor-proofs's and
   -fcontract-symbolic-proofs's own predicate obligations (and the
   exported oa_env_check_predicate_fact a plugin uses for the same job):
   is SUBSTITUTED (the caller's actual argument expression) established
   by ENV's current facts to have PRED_FN hold at REQUIRED_POLARITY,
   established at the opposite polarity, or unknown?  Resolves
   SUBSTITUTED to its own object identity (oa_object_identity_decl) and
   consults the shared, real, cross-statement-tracked m_predicate_fact_
   map.

   This replaces the older oa_predicate_check_inner_call, which was
   purely syntactic and single-hop: it only ever recognized the case
   where SUBSTITUTED was itself a direct call ("consume (produce ())"),
   re-deriving the fact from that inner call's own postcondition text on
   the spot, with no memory of anything established earlier in the
   function.  Consulting the real map instead means an object whose
   identity persists across statements (e.g. 'f.open(); f.read();') is
   now provable too, not just the direct-nested-call shape -- see
   .claude/plans/well-we-last-discussed-ethereal-duckling.md.

   REQUIRE_CONVEYOR: the trust relationship between the two flavors is
   one-way, not a wall in both directions -- a conveyor-established fact
   (backed by real UB-freedom verification) is trustworthy enough for
   symbolic's own, purely-axiomatic checks to rely on, but a symbolic-
   established fact (never verified, trusted outright) must never
   satisfy a *conveyor* obligation, which would silently weaken the
   guarantee conveyor is supposed to provide.  Conveyor's own callers
   (oa_handle_call_conveyor_proof_obligation) pass true; symbolic's own
   (oa_handle_call_symbolic_precondition_obligation) pass false, since
   accepting a fact regardless of which flavor established it is
   correct for that direction.  */

static oa_proof_result
oa_env_predicate_result (oa_env &env, tree substituted, tree pred_fn,
			  bool required_polarity, bool require_conveyor)
{
  /* Consult-only copy-construction lookthrough -- see oa_handle_call_
     symbolic_precondition_obligation's own predicate block for why
     it's sound specifically at a consult site.  Shared by both the
     built-in engine's own conveyor predicate obligation and the
     plugin-facing oa_env_check_predicate_fact below, so both benefit
     from a single fix here.  */
  substituted = oa_strip_conversion_call (substituted);
  tree identity;
  if (!oa_object_identity_decl (substituted, &identity)
      && !oa_field_slot_identity (substituted, env, &identity)
      && !oa_array_slot_identity (substituted, env, &identity)
      && !oa_field_object_identity (substituted, env, &identity))
    return OA_UNKNOWN;
  identity = env.alias_find (identity);
  oa_predicate_fact fact;
  bool found = env.predicate_fact_get (identity, &fact);
  if (!found || fact.pred_fn != pred_fn
      || (require_conveyor && !fact.conveyor_established))
    return OA_UNKNOWN;
  return (fact.polarity == required_polarity) ? OA_PROVEN_TRUE : OA_PROVEN_FALSE;
}

/* Mirrors oa_call_symbolic_range_p (Mechanism B's own bare-scalar
   establishment recognizer, defined further below) exactly, but for a
   named-predicate fact about CALL's callee's own return value -- e.g. a
   postcondition "post<ctrl> (r: check_it (r))" -- rather than a numeric
   range.  This closes a return-value-predicate gap oa_handle_call_
   symbolic_postcondition_establishment's own parameter-only
   substitution (oa_substitute_call_arg) can never reach: that
   function's ARG_DECL must be one of the callee's own PARM_DECLs (so
   'is_opened (this)'-style object-identity postconditions work), but a
   postcondition's own POSTCONDITION_IDENTIFIER never is one -- there is
   no positional argument to substitute for "the value this function is
   about to return."  Establishing this instead at the assignment site
   (mirroring exactly where Mechanism B's own oa_call_symbolic_range_p
   is consulted, in oa_walk_stmt's INIT_EXPR/MODIFY_EXPR case) sidesteps
   that: PRED_FN_OUT/POLARITY_OUT are keyed by the *assignment's own
   LHS* there, not by any substitution through CALL's own arguments.
   Gated by oa_contract_fact_tracking_active_p, the same shared
   substrate every other establishment site here uses.  CONVEYOR_
   ESTABLISHED_OUT records whether the *matching* postcondition was
   itself conveyor-active, for the caller to tag the fact it stores
   (see oa_predicate_fact's own comment on why this matters).  */

static bool
oa_call_symbolic_predicate_p (tree call, tree *pred_fn_out, bool *polarity_out,
			       bool *conveyor_established_out)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return false;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      if (!oa_contract_fact_tracking_active_p (contract, callee))
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree pred_fn, arg_decl;
	  bool negated;
	  if (!oa_predicate_conjunct_shape (*conjuncts[i], &pred_fn, &arg_decl,
					    &negated)
	      || arg_decl != POSTCONDITION_IDENTIFIER (contract))
	    continue;

	  *pred_fn_out = pred_fn;
	  *polarity_out = !negated;
	  *conveyor_established_out = oa_contract_conveyor_active_p (contract, callee);
	  return true;
	}
    }
  return false;
}

/* -fdump-contract-proofs: a lazily-opened dump file, one SMT-LIB2
   verification-condition certificate per concluded conveyor-proof
   obligation (OA_PROVEN_TRUE or OA_PROVEN_FALSE only -- there is no
   proof to certify for OA_UNKNOWN).  See
   .claude/plans/stateless-jumping-shore.md.  Opened once and appended to
   directly, the same shape -fdump-go-spec's own flag_dump_go_spec use
   (godump.cc/toplev.cc) already has -- overkill to route this through
   GCC's own per-pass dump-file manager for one flat, ad hoc text
   stream.  */

static FILE *oa_proof_dump_file;

/* QF_UFLIA (not QF_LIA/QF_UF separately) so a single "(set-logic ...)"
   covers both the range-subsumption (linear-arithmetic) and predicate-
   chaining (uninterpreted-constant) certificates that may both be
   appended to the same file.  Each individual obligation's own
   declarations/assertions are wrapped in its own "(push 1)"/"(pop 1)"
   (oa_emit_range_certificate/oa_emit_predicate_certificate below), so
   certificate blocks never collide on a repeated constant name (e.g.
   two obligations about different calls to the same predicate) -- the
   whole file remains one single, valid SMT-LIB2 script check-sat can be
   run against block by block, or in one solver session covering every
   obligation at once.

   "(set-logic ...)" itself must appear only *once* in the whole file,
   though, and this file can be shared across more than one compiler
   invocation appending to the same path (e.g. a build compiling many
   translation units all with the same -fdump-contract-proofs=path) --
   each such invocation opens its own fresh FILE* in its own process, so
   a plain "write it once per process" guard isn't enough by itself. Use
   the file's on-disk size instead: only an invocation that finds the
   file genuinely empty (the very first writer) emits the header;
   every later invocation appending to an already-populated file leaves
   it alone, keeping exactly one "(set-logic ...)" in the file no matter
   how many separate compilations contributed to it.  */

static FILE *
oa_get_proof_dump_file ()
{
  if (!oa_proof_dump_file && flag_dump_contract_proofs)
    {
      oa_proof_dump_file = fopen (flag_dump_contract_proofs, "a");
      if (oa_proof_dump_file)
	{
	  fseek (oa_proof_dump_file, 0, SEEK_END);
	  if (ftell (oa_proof_dump_file) == 0)
	    fprintf (oa_proof_dump_file, "(set-logic QF_UFLIA)\n\n");
	}
    }
  return oa_proof_dump_file;
}

/* Print every known bound of BOUNDS as an SMT-LIB2 assertion on the
   free constant "v" to FILE -- one (assert ...) per side that's
   actually constrained (has_lo/has_hi), nothing for a side that isn't.
   Shared between the "establish the premises" and "assert the goal"
   roles in oa_emit_range_certificate below -- both are just "print
   this range's own bounds," the only difference is which range and
   whether the result gets negated (handled by the caller).  */

static void
oa_print_range_assertions (FILE *file, oa_range_fact &bounds)
{
  if (bounds.has_lo)
    fprintf (file, "(assert (>= v " HOST_WIDE_INT_PRINT_DEC "))\n",
	     bounds.lo.to_shwi ());
  if (bounds.has_hi)
    fprintf (file, "(assert (<= v " HOST_WIDE_INT_PRINT_DEC "))\n",
	     bounds.hi.to_shwi ());
}

/* -fcontract-conveyor-proof-provenance: format FACT as a single inline
   SMT-LIB2 boolean expression on "v" into BUF (a plain fixed buffer --
   these expressions are always tiny, and this file otherwise avoids any
   std::string dependency question by just not introducing one).  Used
   as an implication's consequent by oa_emit_derivation_premises's own
   OA_DERIV_IF_JOIN case below.  */

static void
oa_range_fact_inline_expr (oa_range_fact &fact, char *buf, size_t buflen)
{
  if (!fact.has_lo && !fact.has_hi)
    snprintf (buf, buflen, "true");
  else if (fact.has_lo && fact.has_hi)
    snprintf (buf, buflen,
	      "(and (>= v " HOST_WIDE_INT_PRINT_DEC ") (<= v "
	      HOST_WIDE_INT_PRINT_DEC "))",
	      fact.lo.to_shwi (), fact.hi.to_shwi ());
  else if (fact.has_lo)
    snprintf (buf, buflen, "(>= v " HOST_WIDE_INT_PRINT_DEC ")",
	      fact.lo.to_shwi ());
  else
    snprintf (buf, buflen, "(<= v " HOST_WIDE_INT_PRINT_DEC ")",
	      fact.hi.to_shwi ());
}

/* -fcontract-conveyor-proof-provenance: emit DERIV's own bounds on "v"
   to FILE as premises, in place of oa_print_range_assertions's own flat
   rendering -- OA_DERIV_AXIOM/OA_DERIV_CALL are exactly that same flat
   case (already fully faithful, see oa_derivation's own comment).
   OA_DERIV_IF_JOIN is a genuine case split: a fresh branch-condition
   constant (BRANCH_COUNTER supplies distinct names across a whole
   certificate) and one implication per arm. Only one level of case-
   split provenance is unpacked this way -- each arm's own bounds are
   rendered directly from its already-merged FACT (oa_range_fact_inline_
   expr) rather than recursing into a *nested* IF_JOIN inside that arm;
   a deeper if/else chain than that falls back to its own outer FACT,
   exactly like any other source this increment doesn't build
   derivations for (IILE) -- safe, just less faithful, matching the
   plan's own documented scope.  OA_DERIV_LOOP is simpler than
   OA_DERIV_IF_JOIN -- no branch selector needed at all, just a bare
   disjunction of PRE_DERIV's own bounds (zero iterations) and
   BODY_DERIV's own bounds (one iteration-independent execution of the
   repeated part) -- see oa_derivation's own comment for why this is
   already a sound loop-invariant argument without needing an
   implication tied to any condition.  Same one-level-only scope limit
   as OA_DERIV_IF_JOIN.  */

static void
oa_emit_derivation_premises (FILE *file, oa_derivation *deriv,
			      unsigned *branch_counter)
{
  if (deriv->kind == OA_DERIV_IF_JOIN)
    {
      unsigned n = (*branch_counter)++;
      char then_buf[128], else_buf[128];
      oa_range_fact_inline_expr (deriv->then_deriv->fact, then_buf,
				 sizeof (then_buf));
      oa_range_fact_inline_expr (deriv->else_deriv->fact, else_buf,
				 sizeof (else_buf));
      fprintf (file, "(declare-const branch_%u Bool)\n", n);
      fprintf (file, "(assert (=> branch_%u %s))\n", n, then_buf);
      fprintf (file, "(assert (=> (not branch_%u) %s))\n", n, else_buf);
      return;
    }
  if (deriv->kind == OA_DERIV_LOOP)
    {
      char pre_buf[128], body_buf[128];
      oa_range_fact_inline_expr (deriv->pre_deriv->fact, pre_buf,
				 sizeof (pre_buf));
      oa_range_fact_inline_expr (deriv->body_deriv->fact, body_buf,
				 sizeof (body_buf));
      fprintf (file, "(assert (or %s %s))\n", pre_buf, body_buf);
      return;
    }
  oa_print_range_assertions (file, deriv->fact);
}

/* Emit one QF_LIA SMT-LIB2 verification condition certifying the range-
   subsumption/disjointness obligation just concluded at LOC for CALLEE:
   ESTABLISHED is the caller's own already-established interval (the
   axioms -- e.g. from an earlier call's postcondition), REQUIRED is the
   callee's own combined precondition interval (the goal).
   PROVEN_FALSE selects which of the two symmetric refutations applies
   (see the plan's worked examples): OA_PROVEN_TRUE asserts ESTABLISHED
   plus the *negation* of REQUIRED (refuting "a value satisfying
   ESTABLISHED could violate REQUIRED"); OA_PROVEN_FALSE asserts
   ESTABLISHED plus REQUIRED directly, unnegated (refuting "a value
   satisfying ESTABLISHED could ever satisfy REQUIRED at all"). Either
   way, an independent SMT solver reporting "unsat" on the emitted query
   confirms the same conclusion this compiler already reached, without
   having to trust this compiler's own reasoning.

   ESTABLISHED_DERIV, when non-NULL (-fcontract-conveyor-proof-
   provenance active and a derivation was actually recorded for this
   obligation's argument), is rendered instead of the flat ESTABLISHED
   bare premise -- see oa_emit_derivation_premises above.  */

static void
oa_emit_range_certificate (location_t loc, tree callee,
			    oa_range_fact &established, oa_range_fact &required,
			    bool proven_false, oa_derivation *established_deriv)
{
  FILE *file = oa_get_proof_dump_file ();
  if (!file)
    return;

  expanded_location xloc = expand_location (loc);
  fprintf (file, "; obligation: call to %s at %s:%d\n",
	   IDENTIFIER_POINTER (DECL_NAME (callee)), xloc.file, xloc.line);
  fprintf (file, "(push 1)\n");
  fprintf (file, "(declare-const v Int)\n");
  if (established_deriv)
    {
      unsigned branch_counter = 0;
      oa_emit_derivation_premises (file, established_deriv, &branch_counter);
    }
  else
    oa_print_range_assertions (file, established);

  if (!proven_false)
    {
      /* OA_PROVEN_TRUE: refute the negation of REQUIRED.  */
      fprintf (file, "(assert (not (and");
      if (required.has_lo)
	fprintf (file, " (>= v " HOST_WIDE_INT_PRINT_DEC ")",
		 required.lo.to_shwi ());
      if (required.has_hi)
	fprintf (file, " (<= v " HOST_WIDE_INT_PRINT_DEC ")",
		 required.hi.to_shwi ());
      fprintf (file, ")))\n");
    }
  else
    {
      /* OA_PROVEN_FALSE: refute REQUIRED directly.  */
      fprintf (file, "(assert (and");
      if (required.has_lo)
	fprintf (file, " (>= v " HOST_WIDE_INT_PRINT_DEC ")",
		 required.lo.to_shwi ());
      if (required.has_hi)
	fprintf (file, " (<= v " HOST_WIDE_INT_PRINT_DEC ")",
		 required.hi.to_shwi ());
      fprintf (file, "))\n");
    }
  fprintf (file, "(check-sat)\n; expect: unsat\n(pop 1)\n\n");
  fflush (file);
}

/* Emit one SMT-LIB2 verification condition (within the file-wide
   QF_UFLIA logic, see oa_get_proof_dump_file's own comment) certifying the
   predicate-chaining obligation just concluded at LOC for CALLEE, about
   PRED_FN: ESTABLISHED_VALUE is the truth value an earlier call's
   postcondition guarantees for "pred_fn applied to this value";
   REQUIRED_VALUE is the truth value CALLEE's own precondition requires
   for the very same value.  A single 0-ary Bool constant models
   "pred_fn applied to this value" (sound here precisely because both
   sides name the identical value -- see oa_env_predicate_result's own
   comment on why this never needs to inspect PRED_FN's actual
   definition).

   PROVEN_FALSE selects which of the two symmetric refutations applies,
   exactly mirroring oa_emit_range_certificate: since ESTABLISHED_VALUE
   fixes the constant to one definite value, OA_PROVEN_TRUE (reached
   when ESTABLISHED_VALUE == REQUIRED_VALUE) asserts the *negation* of
   REQUIRED_VALUE (refuting "the fixed value could differ from what's
   required"); OA_PROVEN_FALSE (reached when they differ) asserts
   REQUIRED_VALUE directly, unnegated (refuting "the fixed value could
   ever equal what's required").  Either way, unsat confirms the
   conclusion independently.  */

static void
oa_emit_predicate_certificate (location_t loc, tree callee, tree pred_fn,
				bool established_value, bool required_value,
				bool proven_false)
{
  FILE *file = oa_get_proof_dump_file ();
  if (!file)
    return;

  expanded_location xloc = expand_location (loc);
  const char *name = IDENTIFIER_POINTER (DECL_NAME (pred_fn));
  fprintf (file, "; obligation: call to %s at %s:%d\n",
	   IDENTIFIER_POINTER (DECL_NAME (callee)), xloc.file, xloc.line);
  fprintf (file, "(push 1)\n");
  fprintf (file, "(declare-const %s Bool)\n", name);
  fprintf (file, "(assert %s%s%s)\n",
	   established_value ? "" : "(not ", name, established_value ? "" : ")");

  /* Goal assertion: negated (opposite literal of REQUIRED_VALUE) unless
     PROVEN_FALSE, in which case it's asserted directly.  */
  bool assert_true = proven_false ? required_value : !required_value;
  fprintf (file, "(assert %s%s%s)\n",
	   assert_true ? "" : "(not ", name, assert_true ? "" : ")");

  fprintf (file, "(check-sat)\n; expect: unsat\n(pop 1)\n\n");
  fflush (file);
}

/* -fcontract-conveyor-proofs: the built-in counterpart to
   oa_handle_call_precondition_obligation above, extending call-site
   precondition-obligation checking beyond std::is_object_address to
   general comparison conjuncts (oa_match_simple_comparison /
   oa_range_subsumption_result), named-predicate obligations
   (oa_predicate_conjunct_shape / oa_env_predicate_result), and
   ptr->field range obligations (oa_handle_call_conveyor_field_range_
   obligation, defined below once oa_collect_contract_field_ranges/
   oa_range_subsumption exist) -- see .claude/plans/well-we-last-
   discussed-ethereal-duckling.md.  The predicate and field-range checks
   consult the same real, cross-statement-tracked substrate -fcontract-
   symbolic-proofs uses for its own obligations (m_predicate_fact_map/
   m_contract_field_range_map are shared, not symbolic-exclusive; see
   oa_contract_fact_tracking_active_p) -- this closes what was
   previously a real gap: conveyor's own predicate/field-range checking
   used to be purely syntactic and single-hop only, unlike its numeric-
   range checking below, which was always backed by the real m_range_map
   engine.  Only active when flag_contract_conveyor_proofs is set
   (checked by the caller in oa_scan_calls_in_expr); every existing
   mandatory diagnostic above is completely unaffected.  A conjunct
   already recognized by is_object_address_call_p is skipped here --
   that one is already mandatorily handled above, not this function's
   obligation.

   Comparison conjuncts are handled in two passes rather than checked
   independently as each is found: pass 1 folds every comparison
   conjunct that constrains the *same* parameter into one combined
   required range (oa_tighten_range_bound, the same tightening
   oa_refine_single_comparison itself uses -- mirroring how
   oa_call_postcondition_range_p already combines a postcondition's own
   conjuncts into one established range), and pass 2 checks that
   combined range, as a whole, against the argument's own established
   range via oa_range_subsumption_result.  This matters for two
   reasons: a precondition like "x > 10 && x < 200" would otherwise be
   checked as two entirely independent single-sided comparisons, which
   can under-report a genuine subset/disjoint relationship the combined
   interval actually has, and -- even when the per-conjunct verdicts
   happen to agree with the combined one -- independent per-conjunct
   checking can emit multiple redundant diagnostics for what is really
   one obligation on one value.  Predicate-shaped conjuncts aren't
   ranges at all, so they're still handled per-conjunct, unchanged.  */

/* Forward-declared: full definition is below oa_handle_call_symbolic_
   precondition_obligation, once oa_collect_contract_field_ranges/
   oa_range_subsumption exist -- oa_handle_call_conveyor_proof_obligation
   here needs it before that point in the file.  */
static void oa_handle_call_conveyor_field_range_obligation
  (tree call, oa_env &env);

/* Forward-declared for the same reason as oa_handle_call_conveyor_
   field_range_obligation immediately above: the call-range analogue.  */
static void oa_handle_call_conveyor_call_range_obligation
  (tree call, oa_env &env);

/* Forward-declared for the same reason: defined just below this
   function, but needed here to substitute a relational obligation's
   own two parameters at this specific call site.  */
static tree oa_substitute_call_arg (tree callee, tree call, tree param);

/* Forward-declared: full definition is much further below, near
   oa_env_check_comparison_1 (its own non-relational sibling), but both
   oa_handle_call_conveyor_proof_obligation here and oa_handle_call_
   symbolic_precondition_obligation need it before that point in the
   file.  */
static oa_proof_result oa_env_check_relational_fact_1
  (oa_env &env, tree substituted_param, tree_code required_code,
   tree substituted_other, bool require_conveyor);

/* Forward-declared for the same reason as oa_env_check_relational_
   fact_1 immediately above: the call analogue.  */
static oa_proof_result oa_env_check_call_relational_fact_1
  (oa_env &env, tree substituted_param, tree_code required_code,
   tree substituted_rhs_receiver, tree substituted_rhs_callee,
   bool require_conveyor);

/* Forward-declared for the same reason as oa_env_check_relational_
   fact_1 above: the call-vs-call analogue.  */
static oa_proof_result oa_env_check_call_call_relational_fact_1
  (oa_env &env, tree substituted_lhs_receiver, tree substituted_lhs_callee,
   tree_code required_code, tree substituted_rhs_receiver,
   tree substituted_rhs_callee, bool require_conveyor);

/* True if an established relational fact of code ESTABLISHED is
   strong enough to satisfy a required comparison of code REQUIRED --
   e.g. an established '<' satisfies a required '<=' (a stricter fact
   subsumes a weaker obligation), and '==' satisfies both '<=' and
   '>='; nothing else is implied (an established '<=' does NOT satisfy
   a required '<', since equality remains possible).  Exported (not
   static) so contracts-gimple.cc's own built-in GIMPLE-pass engine can
   reuse the exact same rule rather than a separately-maintained copy.  */

bool
oa_relational_code_implies (tree_code established, tree_code required)
{
  if (established == required)
    return true;
  switch (established)
    {
    case LT_EXPR: return required == LE_EXPR;
    case GT_EXPR: return required == GE_EXPR;
    case EQ_EXPR: return required == LE_EXPR || required == GE_EXPR;
    default: return false;
    }
}

/* Does "A CODE B" hold, where A and B are both ordinary compile-time
   INTEGER_CST literals?  Plain constant folding of two concrete
   numbers actually present at a specific call site -- not resolving
   any parameter's own opaque meaning (contrast oa_get_relational,
   which never inspects a value at all), so this is unconditionally
   safe to evaluate directly, the same way oa_match_simple_comparison's
   own literal-bound checking already is.  Exported for the same
   reason as oa_relational_code_implies immediately above.  */

bool
oa_relational_literal_holds (tree_code code, tree a, tree b)
{
  widest_int wa = wi::to_widest (a);
  widest_int wb = wi::to_widest (b);
  switch (code)
    {
    case LT_EXPR: return wa < wb;
    case LE_EXPR: return wa <= wb;
    case GT_EXPR: return wa > wb;
    case GE_EXPR: return wa >= wb;
    case EQ_EXPR: return wa == wb;
    default: return false;
    }
}

static void
oa_handle_call_conveyor_proof_obligation (tree call, oa_env &env)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return;

  /* Strict (proven_conveyor) vs lenient (analyzed_conveyor, or plain
     is_conveyor under the ordinary command-line flag) -- OR'd across
     every one of the callee's own conveyor-active preconditions, since
     this whole function processes them all together, not selectively
     (see oa_call_conveyor_obligation_status's own comment).  */
  bool strict = false;
  oa_call_conveyor_obligation_status (call, NULL, &strict);

  auto_vec<tree> range_parms;
  auto_vec<oa_range_fact> range_facts;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      if (!oa_contract_conveyor_active_p (contract, callee))
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree already_arg;
	  if (is_object_address_call_p (*conjuncts[i], &already_arg))
	    continue;

	  tree param, const_val;
	  tree_code code;
	  if (oa_match_simple_comparison (*conjuncts[i], &param, &code,
					  &const_val))
	    {
	      unsigned idx;
	      for (idx = 0; idx < range_parms.length (); ++idx)
		if (range_parms[idx] == param)
		  break;
	      if (idx == range_parms.length ())
		{
		  range_parms.safe_push (param);
		  oa_range_fact fresh;
		  fresh.base = NULL_TREE;
		  fresh.has_lo = fresh.has_hi = false;
		  range_facts.safe_push (fresh);
		}
	      oa_tighten_range_bound (range_facts[idx], code,
				      wi::to_widest (const_val));
	      continue;
	    }

	  tree rel_param, rel_other;
	  tree_code rel_code;
	  if (oa_match_comparison_against_param (*conjuncts[i], &rel_param,
						  &rel_code, &rel_other))
	    {
	      tree sub_param = oa_substitute_call_arg (callee, call, rel_param);
	      tree sub_other = oa_substitute_call_arg (callee, call, rel_other);
	      oa_proof_result rel_pr
		= oa_env_check_relational_fact_1 (env, sub_param, rel_code, sub_other,
					   /*require_conveyor=*/true);
	      switch (rel_pr)
		{
		case OA_PROVEN_TRUE:
		  break; /* Silently discharged.  */
		case OA_PROVEN_FALSE:
		  error_at (EXPR_LOCATION (call),
			    "argument %qE provably violates the precondition "
			    "of %qD", sub_param, callee);
		  inform (DECL_SOURCE_LOCATION (callee), "declared here");
		  break;
		case OA_UNKNOWN:
		  if (strict)
		    error_at (EXPR_LOCATION (call),
			      "cannot prove that %qE satisfies the "
			      "precondition of %qD",
			      sub_param ? sub_param : rel_param, callee);
		  else
		    warning_at (EXPR_LOCATION (call), 0,
				"cannot verify that %qE satisfies the "
				"precondition of %qD",
				sub_param ? sub_param : rel_param, callee);
		  inform (DECL_SOURCE_LOCATION (callee), "declared here");
		  break;
		}
	      continue;
	    }

	  {
	    tree rel_param2, rhs_receiver, rhs_callee;
	    tree_code rel_code2;
	    if (oa_match_comparison_against_call (*conjuncts[i], &rel_param2,
						   &rel_code2, &rhs_receiver,
						   &rhs_callee,
						   /*allow_symbolic_accessor=*/
						     false)
		&& TREE_CODE (rhs_receiver) == PARM_DECL)
	      {
		tree sub_param = oa_substitute_call_arg (callee, call, rel_param2);
		tree sub_receiver
		  = oa_substitute_call_arg (callee, call, rhs_receiver);
		oa_proof_result rel_pr
		  = oa_env_check_call_relational_fact_1 (env, sub_param, rel_code2,
							  sub_receiver, rhs_callee,
							  /*require_conveyor=*/true);
		switch (rel_pr)
		  {
		  case OA_PROVEN_TRUE:
		    break; /* Silently discharged.  */
		  case OA_PROVEN_FALSE:
		    error_at (EXPR_LOCATION (call),
			      "argument %qE provably violates the precondition "
			      "of %qD", sub_param, callee);
		    inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    break;
		  case OA_UNKNOWN:
		    if (strict)
		      error_at (EXPR_LOCATION (call),
				"cannot prove that %qE satisfies the "
				"precondition of %qD",
				sub_param ? sub_param : rel_param2, callee);
		    else
		      warning_at (EXPR_LOCATION (call), 0,
				  "cannot verify that %qE satisfies the "
				  "precondition of %qD",
				  sub_param ? sub_param : rel_param2, callee);
		    inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    break;
		  }
		continue;
	      }
	  }

	  {
	    tree lhs_receiver, lhs_callee, rhs_receiver, rhs_callee;
	    tree_code call_code;
	    if (oa_match_call_against_call (*conjuncts[i], &lhs_receiver,
					      &lhs_callee, &call_code,
					      &rhs_receiver, &rhs_callee,
					      /*allow_symbolic_accessor=*/false)
		&& TREE_CODE (lhs_receiver) == PARM_DECL
		&& TREE_CODE (rhs_receiver) == PARM_DECL)
	      {
		tree sub_lhs_receiver
		  = oa_strip_conversion_call
		      (oa_substitute_call_arg (callee, call, lhs_receiver));
		tree sub_rhs_receiver
		  = oa_strip_conversion_call
		      (oa_substitute_call_arg (callee, call, rhs_receiver));
		oa_proof_result rel_pr
		  = oa_env_check_call_call_relational_fact_1
		      (env, sub_lhs_receiver, lhs_callee, call_code,
		       sub_rhs_receiver, rhs_callee, /*require_conveyor=*/true);
		switch (rel_pr)
		  {
		  case OA_PROVEN_TRUE:
		    break; /* Silently discharged.  */
		  case OA_PROVEN_FALSE:
		    error_at (EXPR_LOCATION (call),
			      "argument %qE provably violates the precondition "
			      "of %qD", sub_lhs_receiver, callee);
		    inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    break;
		  case OA_UNKNOWN:
		    if (strict)
		      error_at (EXPR_LOCATION (call),
				"cannot prove that %qD called on %qE satisfies "
				"the precondition of %qD", lhs_callee,
				sub_lhs_receiver ? sub_lhs_receiver : lhs_receiver,
				callee);
		    else
		      warning_at (EXPR_LOCATION (call), 0,
				  "cannot verify that %qD called on %qE satisfies "
				  "the precondition of %qD", lhs_callee,
				  sub_lhs_receiver ? sub_lhs_receiver : lhs_receiver,
				  callee);
		    inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    break;
		  }
		continue;
	      }
	  }

	  /* A nonzero-shaped conjunct ('param != 0'/'0 != param') -- the
	     motivating case is std::saturating_div's own declared
	     precondition (see bits/sat_arith.h/_GLIBCXX_PRECONDITION_
	     NONZERO_DIVISOR), which oa_match_simple_comparison/oa_match_
	     comparison_against_param can never match at all (NE_EXPR isn't
	     among either matcher's own recognized comparison codes -- a
	     range or a relational fact both model a contiguous interval,
	     which "everything except one point" isn't), silently leaving
	     this precondition shape completely unchecked at any call site
	     before this. Mirrors oa_handle_call_symbolic_precondition_
	     obligation's own, pre-existing nz-conjunct branch further down
	     in this file -- same oa_nonzero_conjunct_p matcher, same oa_
	     provably_nonzero_p consult -- except for the literal-argument
	     case just below, which that symbolic-side branch doesn't need
	     (see its own comment on why nonzero-ness "has no provably
	     false case of their own"): a *literal* substituted argument's
	     value is always fully known, and known-zero is exactly what
	     the precondition rules out, so this is sharpened past "cannot
	     verify" all the way to "provably violates" for that one case,
	     matching the range/relational branches' own PROVEN_FALSE
	     handling above.  */
	  tree nz_arg;
	  if (oa_nonzero_conjunct_p (*conjuncts[i], &nz_arg))
	    {
	      tree substituted = oa_substitute_call_arg (callee, call, nz_arg);
	      if (!substituted)
		continue;
	      substituted = oa_strip_conversion_call (substituted);
	      tree stripped = STRIP_ANY_LOCATION_WRAPPER (substituted);
	      if (TREE_CODE (stripped) == INTEGER_CST)
		{
		  if (!integer_zerop (stripped))
		    continue; /* Proven true: silently discharged.  */
		  error_at (EXPR_LOCATION (call),
			    "argument %qE provably violates the precondition "
			    "of %qD", substituted, callee);
		  inform (DECL_SOURCE_LOCATION (callee), "declared here");
		  continue;
		}
	      if (oa_provably_nonzero_p (substituted, env))
		continue; /* Proven true: silently discharged.  */
	      if (strict)
		error_at (EXPR_LOCATION (call),
			  "cannot prove that %qE is nonzero, as required by "
			  "the precondition of %qD", substituted, callee);
	      else
		warning_at (EXPR_LOCATION (call), 0,
			    "cannot verify that %qE is nonzero, as required by "
			    "the precondition of %qD", substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      continue;
	    }

	  tree pred_fn, arg_decl;
	  bool negated;
	  if (!oa_predicate_conjunct_shape (*conjuncts[i], &pred_fn, &arg_decl,
					    &negated))
	    continue;

	  tree matched_parm = NULL_TREE;
	  unsigned argno = 0;
	  for (tree p = DECL_ARGUMENTS (callee); p; p = DECL_CHAIN (p), ++argno)
	    if (p == arg_decl)
	      {
		matched_parm = p;
		break;
	      }
	  if (!matched_parm || argno >= (unsigned) call_expr_nargs (call))
	    continue;

	  tree substituted = CALL_EXPR_ARG (call, argno);
	  oa_proof_result pr
	    = oa_env_predicate_result (env, substituted, pred_fn, !negated,
				       /*require_conveyor=*/true);
	  switch (pr)
	    {
	    case OA_PROVEN_TRUE:
	      /* Same polarity as the precondition's own requirement --
		 see oa_env_predicate_result's own comment.  */
	      if (flag_dump_contract_proofs)
		oa_emit_predicate_certificate (EXPR_LOCATION (call), callee,
						pred_fn, /*established_value=*/!negated,
						/*required_value=*/!negated,
						/*proven_false=*/false);
	      break;
	    case OA_PROVEN_FALSE:
	      error_at (EXPR_LOCATION (call),
			"argument %qE provably violates the precondition of "
			"%qD: %qD (%qE) is established %s, but the "
			"precondition requires it to be %s",
			substituted, callee, pred_fn, substituted,
			negated ? "true" : "false", negated ? "false" : "true");
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      /* Opposite polarity -- established_value is therefore
		 NEGATED (not !negated) here.  */
	      if (flag_dump_contract_proofs)
		oa_emit_predicate_certificate (EXPR_LOCATION (call), callee,
						pred_fn, /*established_value=*/negated,
						/*required_value=*/!negated,
						/*proven_false=*/true);
	      break;
	    case OA_UNKNOWN:
	      if (strict)
		error_at (EXPR_LOCATION (call),
			  "cannot prove that %qD (%qE) holds, as required by "
			  "the precondition of %qD", pred_fn, substituted, callee);
	      else
		warning_at (EXPR_LOCATION (call), 0,
			    "cannot verify that %qD (%qE) holds, as required by "
			    "the precondition of %qD", pred_fn, substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    }
	}
    }

  for (unsigned idx = 0; idx < range_parms.length (); ++idx)
    {
      tree param = range_parms[idx];

      tree substituted = NULL_TREE;
      unsigned argno = 0;
      for (tree p = DECL_ARGUMENTS (callee); p; p = DECL_CHAIN (p), ++argno)
	if (p == param)
	  {
	    if (argno < (unsigned) call_expr_nargs (call))
	      substituted = CALL_EXPR_ARG (call, argno);
	    break;
	  }
      if (!substituted)
	continue;

      oa_proof_result r
	= oa_env_check_range_subsumption (env, substituted, range_facts[idx]);
      switch (r)
	{
	case OA_PROVEN_TRUE:
	  if (flag_dump_contract_proofs)
	    {
	      oa_range_fact established;
	      if (oa_get_range (substituted, env, &established))
		oa_emit_range_certificate (EXPR_LOCATION (call), callee,
					   established, range_facts[idx],
					   /*proven_false=*/false,
					   oa_get_range_derivation (substituted, env));
	    }
	  break;
	case OA_PROVEN_FALSE:
	  error_at (EXPR_LOCATION (call),
		    "argument %qE provably violates the precondition "
		    "of %qD", substituted, callee);
	  inform (DECL_SOURCE_LOCATION (callee), "declared here");
	  if (flag_dump_contract_proofs)
	    {
	      oa_range_fact established;
	      if (oa_get_range (substituted, env, &established))
		oa_emit_range_certificate (EXPR_LOCATION (call), callee,
					   established, range_facts[idx],
					   /*proven_false=*/true,
					   oa_get_range_derivation (substituted, env));
	    }
	  break;
	case OA_UNKNOWN:
	  if (strict)
	    error_at (EXPR_LOCATION (call),
		      "cannot prove that %qE satisfies the "
		      "precondition of %qD", substituted, callee);
	  else
	    warning_at (EXPR_LOCATION (call), 0,
			"cannot verify that %qE satisfies the "
			"precondition of %qD", substituted, callee);
	  inform (DECL_SOURCE_LOCATION (callee), "declared here");
	  break;
	}
    }

  oa_handle_call_conveyor_field_range_obligation (call, env);
  oa_handle_call_conveyor_call_range_obligation (call, env);
}

/* -fcontract-symbolic-proofs: positionally substitute PARAM (one of
   CALLEE's own PARM_DECLs -- including its implicit 'this', which is
   just an ordinary leading PARM_DECL here, no special-casing needed)
   to CALL's actual argument expression -- the same DECL_ARGUMENTS-to-
   CALL_EXPR_ARG correspondence used throughout this file's other call-
   site checks.  Returns NULL_TREE if PARAM isn't actually one of
   CALLEE's own parameters, or CALL doesn't supply that many
   arguments.  */

static tree
oa_substitute_call_arg (tree callee, tree call, tree param)
{
  unsigned argno = 0;
  for (tree p = DECL_ARGUMENTS (callee); p; p = DECL_CHAIN (p), ++argno)
    if (p == param)
      return argno < (unsigned) call_expr_nargs (call)
	? CALL_EXPR_ARG (call, argno) : NULL_TREE;
  return NULL_TREE;
}

/* Forward-declared: full definition (and the runtime-checking-only
   oa_symbolic_action/oa_collect_symbolic_actions this shares its single-
   conjunct recognition with) is much further below, where -fcontract-
   symbolic-runtime-checks's own codegen lives; oa_collect_symbolic_
   field_ranges just below needs it here too, for the static prover's own,
   differently-scoped use (see that function's own comment for why it
   isn't simply oa_collect_symbolic_actions itself).  */
static bool oa_symbolic_comparison_conjunct_shape
  (tree conjunct, tree *field_out, tree *ptr_expr_out, tree_code *code_out,
   tree *const_val_out);

/* Forward-declared for the same reason as oa_symbolic_comparison_
   conjunct_shape immediately above: the call-range analogue, full
   definition much further below, needed here by oa_collect_contract_
   call_ranges.  */
static bool oa_call_range_conjunct_shape
  (tree conjunct, tree *receiver_out, tree *callee_out, tree_code *code_out,
   tree *const_val_out, bool allow_symbolic_accessor);

/* Forward-declared for the same reason: needed by oa_match_result_
   call_relation below, full definition much further below (alongside
   oa_call_range_conjunct_shape, which shares it).  */
static bool oa_underlying_call_range_operand
  (tree op, tree *receiver_out, tree *callee_out, bool allow_symbolic_accessor);

/* Forward-declared: full definition is below oa_match_result_relation
   (whose call-shaped analogue this is), but oa_call_postcondition_
   range_p above that point in the file needs it here.  Exported (not
   static), like oa_match_result_relation itself, so contracts-gimple.cc
   can reuse it for its own postcondition-range composition.  */
bool oa_match_result_call_relation
  (tree conjunct, tree result_id, tree_code *code_out,
   tree *rhs_receiver_out, tree *rhs_callee_out, bool allow_symbolic_accessor);

/* Forward-declared: full definition is much further below (it needs
   oa_match_result_call_relation immediately above), but oa_walk_stmt's
   own INIT_EXPR/MODIFY_EXPR case needs it here, well before that
   point.  */
static bool oa_compose_call_result_range (tree lhs, tree rhs, oa_env &env);

/* -fcontract-symbolic-proofs: one (FIELD, PTR_EXPR) pair's own combined
   range, as written in a single contract's own condition -- e.g.
   'this->count >= 0 && this->count < 100' combines into one RANGE for
   (count, this).  PTR_EXPR and FIELD still reference the *contract
   owner's own* decls (this, parameters), unresolved to any caller-side
   identity yet -- that substitution is each caller's own job (establish
   substitutes via the callee's DECL_ARGUMENTS, consult likewise), exactly
   mirroring how oa_predicate_conjunct_shape's own ARG_DECL_OUT is handled
   by its two callers.  */

struct oa_symbolic_field_group
{
  tree field;
  tree ptr_expr;
  oa_range_fact range;
};

/* -fcontract-symbolic-proofs: like oa_collect_symbolic_actions (defined
   much further below, for -fcontract-symbolic-runtime-checks's own
   codegen), but scoped to just the ptr->field comparison shape, and
   silently *skipping* (not error_at-ing on) any conjunct that doesn't
   match it -- unlike that function, which must recognize every conjunct
   of a condition it's given (since it always runs under -fcontract-
   symbolic-runtime-checks, where every conjunct genuinely does need
   codegen), this one runs whenever just -fcontract-symbolic-proofs is
   active, where a predicate-call conjunct (oa_predicate_conjunct_shape's
   own job, called separately) or a bare-scalar conjunct (oa_precondition_
   symbolic_ranges/oa_call_symbolic_range_p's own job) appearing in the
   same condition is simply somebody else's shape to recognize, not a
   failure here.  Groups multiple conjuncts on the same (FIELD, PTR_EXPR)
   pair into one combined range via oa_tighten_range_bound (the same
   widest_int/inclusive-hi representation oa_range_fact itself already
   uses), rather than oa_symbolic_action's own separate long long/half-
   open one (which exists only to feed runtime codegen's struct-literal
   building, of no use to this static-only path).  */

/* PTR_EXPR, as extracted by oa_symbolic_comparison_conjunct_shape from a
   contract *condition*, is presented wrapped for const-qualified access
   (see the comment above the VIEW_CONVERT_EXPR-building code near line
   570, and oa_get_range's own identical stripping) -- typically a
   NOP_EXPR around the real PARM_DECL (including 'this'), confirmed by
   direct testing (a bare 'TREE_CODE (ptr_expr) != PARM_DECL' check
   otherwise silently rejected every use, the same failure mode oa_get_
   range's own comment describes for a different field).  Strip that
   down to the real decl before resolving identity.  */

static tree
oa_strip_symbolic_ptr_expr (tree ptr_expr)
{
  ptr_expr = STRIP_ANY_LOCATION_WRAPPER (ptr_expr);
  while (TREE_CODE (ptr_expr) == NON_LVALUE_EXPR
	 || TREE_CODE (ptr_expr) == NOP_EXPR
	 || TREE_CODE (ptr_expr) == CONVERT_EXPR
	 || TREE_CODE (ptr_expr) == VIEW_CONVERT_EXPR)
    ptr_expr = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (ptr_expr, 0));
  return ptr_expr;
}

static void
oa_collect_contract_field_ranges (tree condition,
				   vec<oa_symbolic_field_group> *out)
{
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts (&condition, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree field, ptr_expr, const_val;
      tree_code code;
      if (!oa_symbolic_comparison_conjunct_shape (*conjuncts[i], &field,
						   &ptr_expr, &code, &const_val)
	  || TREE_CODE (const_val) != INTEGER_CST)
	continue;

      oa_symbolic_field_group *found = NULL;
      for (unsigned j = 0; j < out->length () && !found; ++j)
	if ((*out)[j].field == field
	    && cp_tree_equal ((*out)[j].ptr_expr, ptr_expr))
	  found = &(*out)[j];
      if (!found)
	{
	  oa_symbolic_field_group g;
	  g.field = field;
	  g.ptr_expr = ptr_expr;
	  g.range.base = NULL_TREE;
	  g.range.has_lo = false;
	  g.range.has_hi = false;
	  out->safe_push (g);
	  found = &out->last ();
	}
      oa_tighten_range_bound (found->range, code, wi::to_widest (const_val));
    }
}

/* The call-range analogue of oa_symbolic_field_group/oa_collect_
   contract_field_ranges immediately above -- one (RECEIVER_EXPR,
   CALLEE) pair's own combined range, as written in a single contract's
   own condition -- e.g. 'i < this->size ()' combines into one RANGE for
   (this, size).  Same grouping discipline: multiple conjuncts on the
   same pair combine via oa_tighten_range_bound; RECEIVER_EXPR is
   compared structurally (cp_tree_equal), CALLEE by decl identity,
   mirroring the field case's (FIELD identity, PTR_EXPR structural)
   split exactly.  */

struct oa_symbolic_call_group
{
  tree callee;
  tree receiver_expr;
  oa_range_fact range;
};

static void
oa_collect_contract_call_ranges (tree condition,
				  vec<oa_symbolic_call_group> *out,
				  bool allow_symbolic_accessor)
{
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts (&condition, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree receiver_expr, callee, const_val;
      tree_code code;
      if (!oa_call_range_conjunct_shape (*conjuncts[i], &receiver_expr,
					  &callee, &code, &const_val,
					  allow_symbolic_accessor)
	  || TREE_CODE (const_val) != INTEGER_CST)
	continue;
      /* RECEIVER_EXPR, like PTR_EXPR in the field case, is presented
	 wrapped for const-qualified access (typically a NOP_EXPR around
	 the real PARM_DECL, including 'this') -- confirmed by direct
	 testing (a bare 'TREE_CODE (receiver_expr) != PARM_DECL' check
	 otherwise silently rejected every use). Stripped once here,
	 unlike the field case (which leaves this to each individual
	 consumer, oa_strip_symbolic_ptr_expr) -- there is exactly one
	 place that builds an oa_symbolic_call_group, so stripping here
	 once removes the chance of a consumer forgetting to.  */
      receiver_expr = oa_strip_symbolic_ptr_expr (receiver_expr);

      oa_symbolic_call_group *found = NULL;
      for (unsigned j = 0; j < out->length () && !found; ++j)
	if ((*out)[j].callee == callee
	    && cp_tree_equal ((*out)[j].receiver_expr, receiver_expr))
	  found = &(*out)[j];
      if (!found)
	{
	  oa_symbolic_call_group g;
	  g.callee = callee;
	  g.receiver_expr = receiver_expr;
	  g.range.base = NULL_TREE;
	  g.range.has_lo = false;
	  g.range.has_hi = false;
	  out->safe_push (g);
	  found = &out->last ();
	}
      oa_tighten_range_bound (found->range, code, wi::to_widest (const_val));
    }
}

/* The range-vs-range analogue of oa_tighten_range_bound above: folds
   OTHER's own worst-case bound (not a single literal point) into REFINED,
   oriented per CODE -- used only by oa_collect_contract_call_ranges_
   parametric below, where the "other side" of a call-range conjunct is
   itself a range (a substituted parameter's own established range), not
   an already-known literal.  */

static void
oa_tighten_range_bound_from_range (oa_range_fact &refined, tree_code code,
				     const oa_range_fact &other)
{
  switch (code)
    {
    case LT_EXPR:
      if (other.has_hi && (!refined.has_hi || refined.hi > other.hi - 1))
	{ refined.has_hi = true; refined.hi = other.hi - 1; }
      break;
    case LE_EXPR:
      if (other.has_hi && (!refined.has_hi || refined.hi > other.hi))
	{ refined.has_hi = true; refined.hi = other.hi; }
      break;
    case GT_EXPR:
      if (other.has_lo && (!refined.has_lo || refined.lo < other.lo + 1))
	{ refined.has_lo = true; refined.lo = other.lo + 1; }
      break;
    case GE_EXPR:
      if (other.has_lo && (!refined.has_lo || refined.lo < other.lo))
	{ refined.has_lo = true; refined.lo = other.lo; }
      break;
    case EQ_EXPR:
      if (other.has_lo && (!refined.has_lo || refined.lo < other.lo))
	{ refined.has_lo = true; refined.lo = other.lo; }
      if (other.has_hi && (!refined.has_hi || refined.hi > other.hi))
	{ refined.has_hi = true; refined.hi = other.hi; }
      break;
    default:
      break;
    }
}

/* Bounds-proving demo (see .claude/plans/lazy-stirring-pearl.md, Part 3):
   the parametric analogue of oa_collect_contract_call_ranges above, for a
   postcondition's own call-range conjunct whose "other side" is another
   parameter of the postcondition-owning function (e.g. 'post<>(size ()
   == n)'), not a literal. CALLEE/CALL/ENV let each such conjunct's own
   parameter be positionally substituted through to this specific call
   site's own argument (oa_substitute_call_arg) and then resolved to a
   range (oa_get_range) -- e.g. 'v.resize (5)' substitutes N to the
   literal 5, giving an exact-point range, exactly equivalent to today's
   literal-postcondition case; 'v.resize (m)' for some other tracked
   variable M works too, if M's own range is known at this call site.
   Deliberately separate from oa_collect_contract_call_ranges itself (kept
   a pure, substitution-free shape collector, with no call site to
   substitute through at all when called from self-trust establishment or
   precondition-side consult): only a postcondition being established at
   an actual call site can ever resolve a parametric bound this way. Folds
   into the SAME OUT groups oa_collect_contract_call_ranges itself
   populates (the caller runs both, into one vector), via oa_tighten_
   range_bound_from_range instead of oa_tighten_range_bound.  */

static void
oa_collect_contract_call_ranges_parametric (tree condition, tree callee,
					      tree call, oa_env &env,
					      vec<oa_symbolic_call_group> *out)
{
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts (&condition, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree receiver_expr, call_callee, other;
      tree_code code;
      if (!oa_call_range_conjunct_shape (*conjuncts[i], &receiver_expr,
					   &call_callee, &code, &other,
					   /*allow_symbolic_accessor=*/false)
	  || TREE_CODE (other) != PARM_DECL)
	continue;
      tree substituted_other = oa_substitute_call_arg (callee, call, other);
      oa_range_fact other_range;
      if (!substituted_other || !oa_get_range (substituted_other, env, &other_range))
	continue;

      receiver_expr = oa_strip_symbolic_ptr_expr (receiver_expr);
      oa_symbolic_call_group *found = NULL;
      for (unsigned j = 0; j < out->length () && !found; ++j)
	if ((*out)[j].callee == call_callee
	    && cp_tree_equal ((*out)[j].receiver_expr, receiver_expr))
	  found = &(*out)[j];
      if (!found)
	{
	  oa_symbolic_call_group g;
	  g.callee = call_callee;
	  g.receiver_expr = receiver_expr;
	  g.range.base = NULL_TREE;
	  g.range.has_lo = false;
	  g.range.has_hi = false;
	  out->safe_push (g);
	  found = &out->last ();
	}
      oa_tighten_range_bound_from_range (found->range, code, other_range);
    }
}

/* -fcontract-symbolic-proofs: three-way outcome shared by both new
   range-based consult loops below (the bare-scalar one in oa_handle_
   call_symbolic_precondition_obligation's sibling, and the ptr->field one
   in oa_handle_call_symbolic_precondition_obligation itself) -- unlike
   the predicate shape's own strict true/false (a symbolic boolean fact
   either matches the required polarity or its exact negation), two
   *ranges* can also merely partially overlap: ESTABLISHED might satisfy
   REQUIRED for some but not all of its own possible values, which is
   neither a proof of correctness nor a proof of violation, just "cannot
   verify" -- the same "must be provable, else treated as unconstrained"
   discipline used everywhere else in this file, extended to have a
   distinct provably-false case only when the two ranges cannot possibly
   overlap at all. The outcome enum itself now lives in contracts.h (see
   its own comment there): contracts-gimple.cc's own range-based consult
   needs it too, since the bounds-proving demo.  */

static oa_range_subsumption_result
oa_range_subsumption (const oa_range_fact &established,
		       const oa_range_fact &required)
{
  bool subsumed
    = (!required.has_lo
       || (established.has_lo && established.lo >= required.lo))
      && (!required.has_hi
	  || (established.has_hi && established.hi <= required.hi));
  if (subsumed)
    return OA_RANGE_SUBSUMED;

  bool disjoint
    = (established.has_hi && required.has_lo && established.hi < required.lo)
      || (established.has_lo && required.has_hi
	  && established.lo > required.hi);
  return disjoint ? OA_RANGE_DISJOINT : OA_RANGE_PARTIAL;
}

/* Bounds-proving demo (see .claude/plans/lazy-stirring-pearl.md): unlike
   oa_range_subsumption immediately above (one established range against
   one required [lo,hi] bound), this compares two independently-tracked
   ranges related by an arbitrary comparison CODE -- e.g. a plain local's
   own scalar range against a receiver's own established call-range fact,
   never explicitly linked to each other by any if-condition or self-trust
   (that's oa_env_check_call_relational_fact_1's own existing "linked
   fact" check, tried first; this is the fallback for when no such link
   was ever established, only two separate facts that might still settle
   the question numerically). SUBSUMED requires A's own worst-case bound
   in the direction that could break the relation to still satisfy it
   against B's own worst-case bound; DISJOINT requires the mirror-image
   worst case to already violate it unconditionally -- the same "use the
   interval's own worst case" discipline oa_offset_compatible_with_code
   already applies to a single interval, extended to two.  */

static oa_range_subsumption_result
oa_range_pair_relation (const oa_range_fact &a, tree_code code,
			  const oa_range_fact &b)
{
  switch (code)
    {
    case LT_EXPR:
      if (a.has_hi && b.has_lo && a.hi < b.lo) return OA_RANGE_SUBSUMED;
      if (a.has_lo && b.has_hi && a.lo >= b.hi) return OA_RANGE_DISJOINT;
      return OA_RANGE_PARTIAL;
    case LE_EXPR:
      if (a.has_hi && b.has_lo && a.hi <= b.lo) return OA_RANGE_SUBSUMED;
      if (a.has_lo && b.has_hi && a.lo > b.hi) return OA_RANGE_DISJOINT;
      return OA_RANGE_PARTIAL;
    case GT_EXPR:
      if (a.has_lo && b.has_hi && a.lo > b.hi) return OA_RANGE_SUBSUMED;
      if (a.has_hi && b.has_lo && a.hi <= b.lo) return OA_RANGE_DISJOINT;
      return OA_RANGE_PARTIAL;
    case GE_EXPR:
      if (a.has_lo && b.has_hi && a.lo >= b.hi) return OA_RANGE_SUBSUMED;
      if (a.has_hi && b.has_lo && a.hi < b.lo) return OA_RANGE_DISJOINT;
      return OA_RANGE_PARTIAL;
    case EQ_EXPR:
      if (a.has_lo && a.has_hi && b.has_lo && b.has_hi
	  && a.lo == a.hi && b.lo == b.hi && a.lo == b.lo)
	return OA_RANGE_SUBSUMED;
      if ((a.has_hi && b.has_lo && a.hi < b.lo)
	  || (a.has_lo && b.has_hi && a.lo > b.hi))
	return OA_RANGE_DISJOINT;
      return OA_RANGE_PARTIAL;
    default:
      return OA_RANGE_PARTIAL;
    }
}

/* For each of CALL's callee's own active (conveyor- or symbolic-,
   non-ignored) postconditions, record the fact it establishes -- e.g.
   'post<ctrl>(is_opened(this))' called as 'f.open()' records "is_opened
   holds for f" in ENV, keyed by f's own object identity
   (oa_object_identity_decl).  Symmetric complement of oa_handle_call_
   symbolic_precondition_obligation and oa_handle_call_conveyor_proof_
   obligation below (each of those *consumes* a fact this one
   *produces*) -- see .claude/plans/well-we-last-discussed-ethereal-
   duckling.md.  Also establishes any ptr->field range facts the same
   postcondition's condition names (oa_collect_contract_field_ranges),
   the static-prover analogue of Mechanism A's own runtime
   establishment, and, for a symbolic-active postcondition specifically,
   any is_object_address(this)/E != 0-shaped conjunct naming one of
   CALLEE's own persistent parameters, into the symbolic-only m_symbolic_
   object_address_map/m_symbolic_nz_map -- see those maps' own comment.  */

static void
oa_handle_call_symbolic_postcondition_establishment (tree call, oa_env &env)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      if (!oa_contract_fact_tracking_active_p (contract, callee))
	continue;
      /* Which flavor *this specific* postcondition is -- recorded
	 alongside every fact it establishes, so a conveyor-only consumer
	 (oa_env_predicate_result/oa_env_check_field_range_fact with
	 REQUIRE_CONVEYOR true) can refuse a fact that was only ever
	 backed by a symbolic contract's own, unverified trust.  See
	 oa_predicate_fact's own comment.  */
      bool conveyor_established = oa_contract_conveyor_active_p (contract, callee);
      /* Unlike the shared-substrate maps above/below, is_object_address/
	 nonzero-ness has its own, symbolic-only maps (m_symbolic_object_
	 address_map/m_symbolic_nz_map) rather than a provenance tag on a
	 shared one -- see those maps' own comment on oa_env. Only a
	 symbolic-active postcondition ever writes to them: a conveyor-
	 active one's own is_object_address/nonzero conjuncts are never
	 checked as a caller-side obligation at all (oa_handle_call_
	 conveyor_proof_obligation explicitly skips is_object_address, and
	 has no nonzero-conjunct handling either -- these exist only to
	 seed conveyor's own UB-freedom analysis of the callee's own body,
	 not as a fact for callers), so establishing into a *conveyor* map
	 here would have no consumer and only add confusion.  */
      bool symbolic_established = oa_contract_symbolic_active_p (contract, callee);

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree pred_fn, arg_decl;
	  bool negated;
	  if (!oa_predicate_conjunct_shape (*conjuncts[i], &pred_fn, &arg_decl,
					    &negated))
	    continue;

	  tree substituted = oa_substitute_call_arg (callee, call, arg_decl);
	  if (!substituted)
	    continue;

	  tree identity;
	  if (!oa_object_identity_decl (substituted, &identity)
	      && !oa_field_slot_identity (substituted, env, &identity)
	      && !oa_array_slot_identity (substituted, env, &identity)
	      && !oa_field_object_identity (substituted, env, &identity))
	    continue;
	  identity = env.alias_find (identity);

	  env.predicate_fact_set (identity, pred_fn, !negated, conveyor_established);
	}

      if (symbolic_established)
	for (unsigned i = 0; i < conjuncts.length (); ++i)
	  {
	    tree arg;
	    if (is_object_address_call_p (*conjuncts[i], &arg))
	      {
		tree substituted
		  = oa_substitute_call_arg (callee, call,
					     STRIP_ANY_LOCATION_WRAPPER (arg));
		tree identity;
		if (substituted && oa_object_identity_decl (substituted, &identity))
		  env.symbolic_object_address_set (identity, true);
	      }
	    else if (oa_nonzero_conjunct_p (*conjuncts[i], &arg))
	      {
		tree substituted = oa_substitute_call_arg (callee, call, arg);
		tree identity;
		if (substituted && oa_object_identity_decl (substituted, &identity))
		  env.symbolic_nz_set (identity, true);
	      }
	  }

	      auto_vec<oa_symbolic_field_group> field_groups;
	      oa_collect_contract_field_ranges (cond, &field_groups);
	      for (unsigned i = 0; i < field_groups.length (); ++i)
		{
		  tree ptr_expr
		    = oa_strip_symbolic_ptr_expr (field_groups[i].ptr_expr);
		  if (TREE_CODE (ptr_expr) != PARM_DECL)
		    continue;
		  tree substituted = oa_substitute_call_arg (callee, call, ptr_expr);
		  if (!substituted)
		    continue;
		  tree identity;
		  if (!oa_object_identity_decl (substituted, &identity)
		      && !oa_field_slot_identity (substituted, env, &identity)
		      && !oa_array_slot_identity (substituted, env, &identity)
		      && !oa_field_object_identity (substituted, env, &identity))
		    continue;
		  identity = env.alias_find (identity);
		  env.contract_field_range_set (identity, field_groups[i].field,
						field_groups[i].range,
						conveyor_established);
		}

	      /* The call-range analogue of the field-range block just
		 above: CALLEE's own postcondition may also name a call to
		 a conveyor-declared accessor (e.g. 'post<ctrl>(this->
		 count () < 100)'), establishing a fact for the caller's own
		 receiver the same way.  */
	      auto_vec<oa_symbolic_call_group> call_groups;
	      oa_collect_contract_call_ranges (cond, &call_groups,
						/*allow_symbolic_accessor=*/
						  !conveyor_established);
	      /* Bounds-proving demo, Part 3: a parametric call-range
		 conjunct (e.g. 'post<>(size () == n)') folds into the same
		 CALL_GROUPS, resolved through this specific call site's own
		 argument for N.  */
	      oa_collect_contract_call_ranges_parametric (cond, callee, call,
							    env, &call_groups);
	      for (unsigned i = 0; i < call_groups.length (); ++i)
		{
		  if (TREE_CODE (call_groups[i].receiver_expr) != PARM_DECL)
		    continue;
		  tree substituted
		    = oa_substitute_call_arg (callee, call,
					       call_groups[i].receiver_expr);
		  if (!substituted)
		    continue;
		  tree identity;
		  if (!oa_object_identity_decl (substituted, &identity)
		      && !oa_field_slot_identity (substituted, env, &identity)
		      && !oa_array_slot_identity (substituted, env, &identity)
		      && !oa_field_object_identity (substituted, env, &identity))
		    continue;
		  identity = env.alias_find (identity);
		  env.contract_call_range_set (identity, call_groups[i].callee,
						call_groups[i].range,
						conveyor_established);
		}
    }
}

/* -fcontract-symbolic-proofs: for each of CALL's callee's own active
   (non-ignored) symbolic preconditions, check that ENV already has a
   matching fact for the substituted argument's own object identity --
   established by an earlier call's own symbolic postcondition
   (oa_handle_call_symbolic_postcondition_establishment above), and not
   invalidated since (see this function's own callers in oa_scan_
   calls_in_expr for the invalidation rules).  Also checks is_object_
   address/nonzero-shaped conjuncts, each falling back to whatever
   conveyor has already established (the allowed direction) before
   consulting its own symbolic-only maps -- see the comment on that
   loop, further below.  */

static void
oa_handle_call_symbolic_precondition_obligation (tree call, oa_env &env)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return;

  /* See oa_handle_call_conveyor_proof_obligation's own identical
     comment -- symbolic mirror.  */
  bool strict = false;
  oa_call_symbolic_obligation_status (call, NULL, &strict);

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      if (!oa_contract_symbolic_active_p (contract, callee))
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree pred_fn, arg_decl;
	  bool negated;
	  if (!oa_predicate_conjunct_shape (*conjuncts[i], &pred_fn, &arg_decl,
					    &negated))
	    continue;

	  tree substituted = oa_substitute_call_arg (callee, call, arg_decl);
	  if (!substituted)
	    continue;
	  /* Consult-only: also look through a by-value copy-construction
	     materialization (oa_object_identity_decl's own internal strip
	     only ever handles the always-safe conversion-operator case) --
	     sound here specifically because this is checking a REQUIREMENT
	     against an already-established fact, and a copy has the same
	     state as its source at the moment of copying.  See this file's
	     own plan notes on why establish/invalidate must not do this.  */
	  substituted = oa_strip_conversion_call (substituted);

	  tree identity;
	  if (!oa_object_identity_decl (substituted, &identity)
	      && !oa_field_slot_identity (substituted, env, &identity)
	      && !oa_array_slot_identity (substituted, env, &identity)
	      && !oa_field_object_identity (substituted, env, &identity))
	    continue;
	  identity = env.alias_find (identity);

	  bool required = !negated;
	  oa_predicate_fact fact;
	  if (!env.predicate_fact_get (identity, &fact) || fact.pred_fn != pred_fn)
	    {
	      if (strict)
		error_at (EXPR_LOCATION (call),
			  "cannot prove that %qD (%qE) holds, as required by "
			  "the precondition of %qD", pred_fn, substituted, callee);
	      else
		warning_at (EXPR_LOCATION (call), 0,
			    "cannot verify that %qD (%qE) holds, as required by "
			    "the precondition of %qD", pred_fn, substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      continue;
	    }

	  if (fact.polarity == required)
	    continue; /* Proven true: silently discharged.  */

	  error_at (EXPR_LOCATION (call),
		    "argument %qE provably violates the precondition of "
		    "%qD: %qD (%qE) is established %s, but the "
		    "precondition requires it to be %s",
		    substituted, callee, pred_fn, substituted,
		    fact.polarity ? "true" : "false", required ? "true" : "false");
	  inform (DECL_SOURCE_LOCATION (callee), "declared here");
	}

      /* A relational conjunct against another of the callee's own
	 parameters -- unlike the predicate/field-range shapes above,
	 there is no established provenance requirement here: a
	 conveyor-established relational fact is trustworthy enough for
	 this symbolic obligation to rely on (the allowed direction --
	 see oa_relational_fact's own comment), so this consult never
	 checks CONVEYOR_ESTABLISHED at all, unlike the conveyor side's
	 own consult in oa_handle_call_conveyor_proof_obligation.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rel_other;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_param (*conjuncts[i], &rel_param,
						   &rel_code, &rel_other))
	    continue;

	  tree sub_param = oa_substitute_call_arg (callee, call, rel_param);
	  tree sub_other = oa_substitute_call_arg (callee, call, rel_other);
	  oa_proof_result rel_pr
	    = oa_env_check_relational_fact_1 (env, sub_param, rel_code, sub_other,
					/*require_conveyor=*/false);
	  switch (rel_pr)
	    {
	    case OA_PROVEN_TRUE:
	      break; /* Silently discharged.  */
	    case OA_PROVEN_FALSE:
	      error_at (EXPR_LOCATION (call),
			"argument %qE provably violates the precondition "
			"of %qD", sub_param, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    case OA_UNKNOWN:
	      if (strict)
		error_at (EXPR_LOCATION (call),
			  "cannot prove that %qE satisfies the "
			  "precondition of %qD",
			  sub_param ? sub_param : rel_param, callee);
	      else
		warning_at (EXPR_LOCATION (call), 0,
			    "cannot verify that %qE satisfies the "
			    "precondition of %qD",
			    sub_param ? sub_param : rel_param, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    }
	}

      /* The call analogue of the relational loop just above -- same
	 allowed-direction discipline (no CONVEYOR_ESTABLISHED check).  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree rel_param, rhs_receiver, rhs_callee;
	  tree_code rel_code;
	  if (!oa_match_comparison_against_call (*conjuncts[i], &rel_param,
						  &rel_code, &rhs_receiver,
						  &rhs_callee,
						  /*allow_symbolic_accessor=*/true)
	      || TREE_CODE (rhs_receiver) != PARM_DECL)
	    continue;

	  tree sub_param = oa_substitute_call_arg (callee, call, rel_param);
	  tree sub_receiver = oa_substitute_call_arg (callee, call, rhs_receiver);
	  oa_proof_result rel_pr
	    = oa_env_check_call_relational_fact_1 (env, sub_param, rel_code,
						    sub_receiver, rhs_callee,
						    /*require_conveyor=*/false);
	  switch (rel_pr)
	    {
	    case OA_PROVEN_TRUE:
	      break; /* Silently discharged.  */
	    case OA_PROVEN_FALSE:
	      error_at (EXPR_LOCATION (call),
			"argument %qE provably violates the precondition "
			"of %qD", sub_param, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    case OA_UNKNOWN:
	      if (strict)
		error_at (EXPR_LOCATION (call),
			  "cannot prove that %qE satisfies the "
			  "precondition of %qD",
			  sub_param ? sub_param : rel_param, callee);
	      else
		warning_at (EXPR_LOCATION (call), 0,
			    "cannot verify that %qE satisfies the "
			    "precondition of %qD",
			    sub_param ? sub_param : rel_param, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    }
	}

      /* The call-vs-call analogue of the relational loop just above --
	 same allowed-direction discipline (no CONVEYOR_ESTABLISHED check).  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree lhs_receiver, lhs_callee, rhs_receiver, rhs_callee;
	  tree_code call_code;
	  if (!oa_match_call_against_call (*conjuncts[i], &lhs_receiver,
					     &lhs_callee, &call_code,
					     &rhs_receiver, &rhs_callee,
					     /*allow_symbolic_accessor=*/true)
	      || TREE_CODE (lhs_receiver) != PARM_DECL
	      || TREE_CODE (rhs_receiver) != PARM_DECL)
	    continue;

	  tree sub_lhs_receiver
	    = oa_strip_conversion_call
		(oa_substitute_call_arg (callee, call, lhs_receiver));
	  tree sub_rhs_receiver
	    = oa_strip_conversion_call
		(oa_substitute_call_arg (callee, call, rhs_receiver));
	  oa_proof_result rel_pr
	    = oa_env_check_call_call_relational_fact_1
		(env, sub_lhs_receiver, lhs_callee, call_code,
		 sub_rhs_receiver, rhs_callee, /*require_conveyor=*/false);
	  switch (rel_pr)
	    {
	    case OA_PROVEN_TRUE:
	      break; /* Silently discharged.  */
	    case OA_PROVEN_FALSE:
	      error_at (EXPR_LOCATION (call),
			"argument %qE provably violates the precondition "
			"of %qD", sub_lhs_receiver, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    case OA_UNKNOWN:
	      if (strict)
		error_at (EXPR_LOCATION (call),
			  "cannot prove that %qD called on %qE satisfies "
			  "the precondition of %qD", lhs_callee,
			  sub_lhs_receiver ? sub_lhs_receiver : lhs_receiver,
			  callee);
	      else
		warning_at (EXPR_LOCATION (call), 0,
			    "cannot verify that %qD called on %qE satisfies "
			    "the precondition of %qD", lhs_callee,
			    sub_lhs_receiver ? sub_lhs_receiver : lhs_receiver,
			    callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      break;
	    }
	}

      /* is_object_address/nonzero-shaped conjuncts: unlike the
	 predicate/field-range shapes above, these have no "provably
	 false" case of their own (see is_object_address_call_p/
	 oa_nonzero_conjunct_p's own callers elsewhere in this file --
	 neither ever tracks "provably NOT an object address/zero"), so
	 the only two outcomes are proven-true or cannot-verify.  Each
	 checks, in order: CALL's own conveyor fallback (oa_provable_p/
	 oa_provably_nonzero_p, which read m_map/m_nz_map/m_range_map --
	 the allowed direction, see oa_predicate_fact's own comment),
	 then the symbolic-only maps this same pass populates (m_symbolic_
	 object_address_map/m_symbolic_nz_map, see their own comment on
	 oa_env), then, for nonzero only, an m_contract_scalar_range_map
	 fact for the same decl that provably excludes zero -- the
	 symbolic-only bare-scalar range shape (Mechanism B's own static-
	 prover analogue), checked here rather than folded into oa_
	 provably_nonzero_p itself since that function is shared with
	 conveyor's own, unrelated checking and must not gain a symbolic-
	 only fact source.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree arg;
	  if (is_object_address_call_p (*conjuncts[i], &arg))
	    {
	      tree substituted
		= oa_substitute_call_arg (callee, call,
					   STRIP_ANY_LOCATION_WRAPPER (arg));
	      if (!substituted)
		continue;
	      if (oa_provable_p (substituted, env))
		continue; /* Proven true: silently discharged.  */
	      tree identity;
	      if (oa_object_identity_decl (substituted, &identity)
		  && env.symbolic_object_address_provable_p (identity))
		continue; /* Proven true: silently discharged.  */
	      if (strict)
		error_at (EXPR_LOCATION (call),
			  "cannot prove %<is_object_address%> for %qE, as "
			  "required by the precondition of %qD",
			  substituted, callee);
	      else
		warning_at (EXPR_LOCATION (call), 0,
			    "cannot verify %<is_object_address%> for %qE, as "
			    "required by the precondition of %qD",
			    substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	    }
	  else if (oa_nonzero_conjunct_p (*conjuncts[i], &arg))
	    {
	      tree substituted = oa_substitute_call_arg (callee, call, arg);
	      if (!substituted)
		continue;
	      if (oa_provably_nonzero_p (substituted, env))
		continue; /* Proven true: silently discharged.  */
	      /* Nonzero-ness is a value fact (copy-invariant, unlike
		 identity), so the fallback symbolic_nz_provable_p check
		 just below may as well see through a by-value copy too --
		 harmless if oa_provably_nonzero_p (just above, which
		 already does this internally) already succeeded.  */
	      substituted = oa_strip_conversion_call (substituted);
	      tree identity;
	      if (oa_object_identity_decl (substituted, &identity)
		  && env.symbolic_nz_provable_p (identity))
		continue; /* Proven true: silently discharged.  */
	      tree stripped = STRIP_ANY_LOCATION_WRAPPER (substituted);
	      oa_range_fact scalar_range;
	      if ((VAR_P (stripped) || TREE_CODE (stripped) == PARM_DECL)
		  && env.contract_scalar_range_get (stripped, &scalar_range)
		  && ((scalar_range.has_lo && scalar_range.lo > 0)
		      || (scalar_range.has_hi && scalar_range.hi < 0)))
		continue; /* Proven true: silently discharged.  */
	      if (strict)
		error_at (EXPR_LOCATION (call),
			  "cannot prove that %qE is nonzero, as required by "
			  "the precondition of %qD", substituted, callee);
	      else
		warning_at (EXPR_LOCATION (call), 0,
			    "cannot verify that %qE is nonzero, as required by "
			    "the precondition of %qD", substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	    }
	}

	      auto_vec<oa_symbolic_field_group> field_groups;
	      oa_collect_contract_field_ranges (cond, &field_groups);
	      for (unsigned i = 0; i < field_groups.length (); ++i)
		{
		  tree ptr_expr
		    = oa_strip_symbolic_ptr_expr (field_groups[i].ptr_expr);
		  if (TREE_CODE (ptr_expr) != PARM_DECL)
		    continue;
		  tree substituted = oa_substitute_call_arg (callee, call, ptr_expr);
		  if (!substituted)
		    continue;
		  /* Consult-only copy-construction lookthrough -- see this
		     function's own predicate block above for why it's sound
		     specifically here (a copy has the same field state as
		     its source at the moment of copying).  */
		  substituted = oa_strip_conversion_call (substituted);
		  tree identity;
		  if (!oa_object_identity_decl (substituted, &identity)
		      && !oa_field_slot_identity (substituted, env, &identity)
		      && !oa_array_slot_identity (substituted, env, &identity)
		      && !oa_field_object_identity (substituted, env, &identity))
		    continue;
		  identity = env.alias_find (identity);

		  oa_contract_field_range_fact established;
		  if (!env.contract_field_range_get (identity, field_groups[i].field,
						      &established))
		    {
		      if (strict)
			error_at (EXPR_LOCATION (call),
				  "cannot prove that field %qD of %qE satisfies "
				  "the precondition of %qD", field_groups[i].field,
				  substituted, callee);
		      else
			warning_at (EXPR_LOCATION (call), 0,
				    "cannot verify that field %qD of %qE satisfies "
				    "the precondition of %qD", field_groups[i].field,
				    substituted, callee);
		      inform (DECL_SOURCE_LOCATION (callee), "declared here");
		      continue;
		    }
		  /* Symbolic's own consult: any established fact satisfies
		     it, whichever flavor established it -- see oa_predicate_
		     fact's own comment on the one-way trust direction.  */

		  oa_range_subsumption_result r
		    = oa_range_subsumption (established.range, field_groups[i].range);
		  if (r == OA_RANGE_SUBSUMED)
		    continue; /* Proven true: silently discharged.  */
		  if (r == OA_RANGE_DISJOINT)
		    {
		      error_at (EXPR_LOCATION (call),
				"argument %qE provably violates the precondition "
				"of %qD: %qD is established outside the required "
				"range", substituted, callee, field_groups[i].field);
		      inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    }
		  else if (strict)
		    {
		      error_at (EXPR_LOCATION (call),
				"cannot prove that field %qD of %qE satisfies "
				"the precondition of %qD", field_groups[i].field,
				substituted, callee);
		      inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    }
		  else
		    {
		      warning_at (EXPR_LOCATION (call), 0,
				  "cannot verify that field %qD of %qE satisfies "
				  "the precondition of %qD", field_groups[i].field,
				  substituted, callee);
		      inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    }
		}

	      /* The call-range analogue of the field-range consult loop
		 just above: symbolic's own consult accepts an established
		 fact regardless of provenance, whichever flavor of
		 contract established it -- unlike conveyor's own consult
		 (oa_handle_call_conveyor_call_range_obligation below),
		 which requires conveyor_established specifically.  */
	      auto_vec<oa_symbolic_call_group> call_groups;
	      oa_collect_contract_call_ranges (cond, &call_groups,
						/*allow_symbolic_accessor=*/true);
	      for (unsigned i = 0; i < call_groups.length (); ++i)
		{
		  if (TREE_CODE (call_groups[i].receiver_expr) != PARM_DECL)
		    continue;
		  tree substituted
		    = oa_substitute_call_arg (callee, call,
					       call_groups[i].receiver_expr);
		  if (!substituted)
		    continue;
		  substituted = oa_strip_conversion_call (substituted);
		  tree identity;
		  if (!oa_object_identity_decl (substituted, &identity)
		      && !oa_field_slot_identity (substituted, env, &identity)
		      && !oa_array_slot_identity (substituted, env, &identity)
		      && !oa_field_object_identity (substituted, env, &identity))
		    continue;
		  identity = env.alias_find (identity);

		  oa_contract_field_range_fact established;
		  if (!env.contract_call_range_get (identity, call_groups[i].callee,
						     &established))
		    {
		      if (strict)
			error_at (EXPR_LOCATION (call),
				  "cannot prove that %qD called on %qE satisfies "
				  "the precondition of %qD", call_groups[i].callee,
				  substituted, callee);
		      else
			warning_at (EXPR_LOCATION (call), 0,
				    "cannot verify that %qD called on %qE satisfies "
				    "the precondition of %qD", call_groups[i].callee,
				    substituted, callee);
		      inform (DECL_SOURCE_LOCATION (callee), "declared here");
		      continue;
		    }

		  oa_range_subsumption_result r
		    = oa_range_subsumption (established.range, call_groups[i].range);
		  if (r == OA_RANGE_SUBSUMED)
		    continue; /* Proven true: silently discharged.  */
		  if (r == OA_RANGE_DISJOINT)
		    {
		      error_at (EXPR_LOCATION (call),
				"argument %qE provably violates the precondition "
				"of %qD: %qD is established outside the required "
				"range", substituted, callee, call_groups[i].callee);
		      inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    }
		  else if (strict)
		    {
		      error_at (EXPR_LOCATION (call),
				"cannot prove that %qD called on %qE satisfies "
				"the precondition of %qD", call_groups[i].callee,
				substituted, callee);
		      inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    }
		  else
		    {
		      warning_at (EXPR_LOCATION (call), 0,
				  "cannot verify that %qD called on %qE satisfies "
				  "the precondition of %qD", call_groups[i].callee,
				  substituted, callee);
		      inform (DECL_SOURCE_LOCATION (callee), "declared here");
		    }
		}
    }
}

/* -fcontract-conveyor-proofs: the ptr->field range consult side of
   oa_handle_call_conveyor_proof_obligation above, forward-declared there
   because oa_symbolic_field_group/oa_collect_contract_field_ranges/
   oa_range_subsumption aren't defined until further down this file,
   below this point -- closes conveyor-proofs' own field-range gap (a
   conveyor contract's 'this->count >= 0 && this->count < 100'-style
   conjunct previously got no scrutiny at all from -fcontract-conveyor-
   proofs, since m_range_map only ever tracks bare decls, never a
   pointer's own field).  Identical three-way subsumed/disjoint/partial
   consult logic to oa_handle_call_symbolic_precondition_obligation's own
   field-range loop just above, gated on oa_contract_conveyor_active_p
   instead of oa_contract_symbolic_active_p -- the underlying map
   (m_contract_field_range_map) is the same shared substrate either
   way.  */

static void
oa_handle_call_conveyor_field_range_obligation (tree call, oa_env &env)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return;

  /* See oa_handle_call_conveyor_proof_obligation's own identical
     comment.  */
  bool strict = false;
  oa_call_conveyor_obligation_status (call, NULL, &strict);

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      if (!oa_contract_conveyor_active_p (contract, callee))
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<oa_symbolic_field_group> field_groups;
      oa_collect_contract_field_ranges (cond, &field_groups);
      for (unsigned i = 0; i < field_groups.length (); ++i)
	{
	  tree ptr_expr = oa_strip_symbolic_ptr_expr (field_groups[i].ptr_expr);
	  if (TREE_CODE (ptr_expr) != PARM_DECL)
	    continue;
	  tree substituted = oa_substitute_call_arg (callee, call, ptr_expr);
	  if (!substituted)
	    continue;
	  /* Consult-only copy-construction lookthrough -- see oa_handle_
	     call_symbolic_precondition_obligation's own identical field-
	     range block for why it's sound specifically here.  */
	  substituted = oa_strip_conversion_call (substituted);
	  tree identity;
	  if (!oa_object_identity_decl (substituted, &identity)
	      && !oa_field_slot_identity (substituted, env, &identity)
	      && !oa_array_slot_identity (substituted, env, &identity)
	      && !oa_field_object_identity (substituted, env, &identity))
	    continue;
	  identity = env.alias_find (identity);

	  oa_contract_field_range_fact established;
	  /* Conveyor's own consult: a fact backed only by a symbolic
	     contract's own, unverified trust must not satisfy a conveyor
	     obligation -- see oa_predicate_fact's own comment.  Treated
	     identically to "no fact found at all".  */
	  if (!env.contract_field_range_get (identity, field_groups[i].field,
					      &established)
	      || !established.conveyor_established)
	    {
	      if (strict)
		error_at (EXPR_LOCATION (call),
			  "cannot prove that field %qD of %qE satisfies "
			  "the precondition of %qD", field_groups[i].field,
			  substituted, callee);
	      else
		warning_at (EXPR_LOCATION (call), 0,
			    "cannot verify that field %qD of %qE satisfies "
			    "the precondition of %qD", field_groups[i].field,
			    substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      continue;
	    }

	  oa_range_subsumption_result r
	    = oa_range_subsumption (established.range, field_groups[i].range);
	  if (r == OA_RANGE_SUBSUMED)
	    continue; /* Proven true: silently discharged.  */
	  if (r == OA_RANGE_DISJOINT)
	    {
	      error_at (EXPR_LOCATION (call),
			"argument %qE provably violates the precondition "
			"of %qD: %qD is established outside the required "
			"range", substituted, callee, field_groups[i].field);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	    }
	  else if (strict)
	    {
	      error_at (EXPR_LOCATION (call),
			"cannot prove that field %qD of %qE satisfies "
			"the precondition of %qD", field_groups[i].field,
			substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	    }
	  else
	    {
	      warning_at (EXPR_LOCATION (call), 0,
			  "cannot verify that field %qD of %qE satisfies "
			  "the precondition of %qD", field_groups[i].field,
			  substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	    }
	}
    }
}

/* -fcontract-conveyor-proofs: the call-range consult side of oa_handle_
   call_conveyor_proof_obligation, forward-declared there for the same
   reason as oa_handle_call_conveyor_field_range_obligation immediately
   above -- closes conveyor-proofs' own call-range gap (a conveyor
   contract's 'n < this->size ()'-style conjunct, naming a call to a
   DECL_DECLARED_CONVEYOR_P accessor rather than a ptr->field access,
   previously got no scrutiny at all).  Identical three-way subsumed/
   disjoint/partial consult logic to oa_handle_call_conveyor_field_range_
   obligation, over m_contract_call_range_map instead of m_contract_
   field_range_map -- requires conveyor_established, exactly like that
   function: a call-range fact backed only by a symbolic contract's own,
   unverified trust must not satisfy a conveyor obligation.  */

static void
oa_handle_call_conveyor_call_range_obligation (tree call, oa_env &env)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return;

  /* See oa_handle_call_conveyor_proof_obligation's own identical
     comment.  */
  bool strict = false;
  oa_call_conveyor_obligation_status (call, NULL, &strict);

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      if (!oa_contract_conveyor_active_p (contract, callee))
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<oa_symbolic_call_group> call_groups;
      oa_collect_contract_call_ranges (cond, &call_groups,
					/*allow_symbolic_accessor=*/false);
      for (unsigned i = 0; i < call_groups.length (); ++i)
	{
	  if (TREE_CODE (call_groups[i].receiver_expr) != PARM_DECL)
	    continue;
	  tree substituted
	    = oa_substitute_call_arg (callee, call, call_groups[i].receiver_expr);
	  if (!substituted)
	    continue;
	  substituted = oa_strip_conversion_call (substituted);
	  tree identity;
	  if (!oa_object_identity_decl (substituted, &identity)
	      && !oa_field_slot_identity (substituted, env, &identity)
	      && !oa_array_slot_identity (substituted, env, &identity)
	      && !oa_field_object_identity (substituted, env, &identity))
	    continue;
	  identity = env.alias_find (identity);

	  oa_contract_field_range_fact established;
	  /* Conveyor's own consult: a fact backed only by a symbolic
	     contract's own, unverified trust must not satisfy a conveyor
	     obligation -- see oa_predicate_fact's own comment.  Treated
	     identically to "no fact found at all".  */
	  if (!env.contract_call_range_get (identity, call_groups[i].callee,
					     &established)
	      || !established.conveyor_established)
	    {
	      if (strict)
		error_at (EXPR_LOCATION (call),
			  "cannot prove that %qD called on %qE satisfies "
			  "the precondition of %qD", call_groups[i].callee,
			  substituted, callee);
	      else
		warning_at (EXPR_LOCATION (call), 0,
			    "cannot verify that %qD called on %qE satisfies "
			    "the precondition of %qD", call_groups[i].callee,
			    substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      continue;
	    }

	  oa_range_subsumption_result r
	    = oa_range_subsumption (established.range, call_groups[i].range);
	  if (r == OA_RANGE_SUBSUMED)
	    continue; /* Proven true: silently discharged.  */
	  if (r == OA_RANGE_DISJOINT)
	    {
	      error_at (EXPR_LOCATION (call),
			"argument %qE provably violates the precondition "
			"of %qD: %qD is established outside the required "
			"range", substituted, callee, call_groups[i].callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	    }
	  else if (strict)
	    {
	      error_at (EXPR_LOCATION (call),
			"cannot prove that %qD called on %qE satisfies "
			"the precondition of %qD", call_groups[i].callee,
			substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	    }
	  else
	    {
	      warning_at (EXPR_LOCATION (call), 0,
			  "cannot verify that %qD called on %qE satisfies "
			  "the precondition of %qD", call_groups[i].callee,
			  substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	    }
	}
    }
}

/* oa_object_identity_decl's own bare-"VAR_DECL/PARM_DECL used
   directly" branch is documented as being "for a value that's already
   a pointer" but doesn't actually check that -- harmless everywhere
   else that function is used (establish/consult sites want ANY decl,
   pointer or not, to be its own identity; reassignment invalidation
   likewise wants any type), but wrong specifically for *this* call-
   argument-invalidation question: passing a bare, non-pointer scalar
   *by value* (e.g. 'consume (r)' where r is int) can never let the
   callee alias or mutate anything the caller can observe, so it must
   not invalidate r's own predicate fact -- only a genuine address-of
   (or 'this', or a bare *pointer* passed by value, which still lets
   the callee reach the same pointee) is a real aliasing concern. This
   mirrors oa_invalidate_symbolic_scalar_range_for_call_args's own
   "not oa_object_identity_decl, this/ADDR_EXPR-of-decl only" carve-out
   for exactly the same reason, generalized to also accept a bare
   pointer decl (which that function's own scalar-only domain never
   needs to).  */

/* True if CALL is a call to a single-argument implicit conversion
   operator (DECL_CONV_FN_P) -- the exact shape oa_strip_conversion_
   operator_call/oa_strip_conversion_call already treat as a
   transparent, identity-preserving pass-through to the *same* object
   (see that function's own comment), never a genuine, opaque call
   that could mutate anything.

   Every "any call taking this decl's address invalidates it" rule
   below must skip such a call entirely: once identity resolution
   learns to see *through* a wrapper's own conversion operator to
   reach the wrapped object's identity, calling that same conversion
   operator necessarily takes the wrapper's own address for its
   implicit object argument -- and without this guard, invalidation
   would immediately undo, within the very same statement, whatever
   fact establish/consult just reached through that identical call.
   Found via direct testing: a postcondition establishing a named-
   predicate fact for a conversion-reached identity was silently wiped
   out before the very next statement's precondition could consult it,
   traced to this exact interaction.  Mirrors oa_strip_conversion_call's
   own "no need to check constness -- a conversion operator is already
   trusted as a same-object pass-through everywhere else in this pass"
   precedent, so this isn't a new soundness claim, just consistency
   with one already made.  */

static bool
oa_call_is_conversion_operator_call (tree call)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  return callee != NULL_TREE && DECL_CONV_FN_P (callee);
}

static bool
oa_invalidation_identity_decl (tree expr, tree *decl_out)
{
  if (expr == NULL_TREE || expr == error_mark_node)
    return false;

  expr = STRIP_ANY_LOCATION_WRAPPER (expr);
  while (TREE_CODE (expr) == NON_LVALUE_EXPR
	 || TREE_CODE (expr) == NOP_EXPR
	 || TREE_CODE (expr) == CONVERT_EXPR
	 || TREE_CODE (expr) == VIEW_CONVERT_EXPR)
    expr = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));

  if (is_this_parameter (expr))
    {
      *decl_out = expr;
      return true;
    }

  if (TREE_CODE (expr) == ADDR_EXPR)
    {
      tree op = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));
      if (DECL_P (op) && (VAR_P (op) || TREE_CODE (op) == PARM_DECL))
	{
	  *decl_out = op;
	  return true;
	}
      return false;
    }

  /* A bare REFERENCE_TYPE decl reaches here, with no ADDR_EXPR wrapper
     at all, for exactly '&some_reference' (taking a reference's own
     address needs no extra address-of node -- the reference already
     internally represents one) -- found via direct testing that
     'mutate_via_alias (&r)' for 'file &r = *p;' never invalidated
     anything at all before this, the same "real aliasing concern" as
     a bare pointer passed by value, just reached one layer differently.  */
  if ((VAR_P (expr) || TREE_CODE (expr) == PARM_DECL)
      && (POINTER_TYPE_P (TREE_TYPE (expr))
	  || TREE_CODE (TREE_TYPE (expr)) == REFERENCE_TYPE))
    {
      *decl_out = expr;
      return true;
    }

  return false;
}

/* Stage 3: two of a function's own distinct parameters (or 'this' and
   a parameter) are never treated as potentially the same object,
   anywhere in this file -- yet nothing in standard C++ prevents a
   caller from passing the same object through two different pointer/
   reference parameters ('f(p, p)' for 'f(file *a, file *b)' is
   completely ordinary, legal code) unless a parameter is __restrict-
   qualified. This is a real, confirmed soundness gap of the identical
   shape Stages 1/2a/2b each fixed for a different syntactic source of
   aliasing -- just never assigned from one another within the body,
   so none of those stages' own alias-tracking (which only ever links
   decls *observed* to be assigned from each other) can see it:

     void f (file *a, file *b) {
       open_it (a);              // establishes is_opened(a)
       mutate_via_alias (b);      // invalidates b's own identity only
       use_it (a);                 // wrongly proven if called as f(p, p)
     }

   Deliberately conservative by default, a scope decision made
   explicitly with the user rather than assumed: any two same/
   compatible-typed, non-__restrict parameters are *always* treated as
   a potential-alias group, no opt-in annotation -- this closes the gap
   universally but does increase "cannot verify" diagnostics for the
   common, actually-non-aliasing case; __restrict is the existing,
   standard way to opt back out.

   Fully intraprocedural, no per-call-site reanalysis, no new alias
   map: only *invalidation* propagates across a potential-alias group;
   *establishing* a fact via one parameter must never propagate to
   another (unsound in the other direction -- the two parameters might
   genuinely be different objects, so proving is_opened(a) can never
   imply is_opened(b)).

   Explicitly scoped to the whole-object invalidation sites only (this
   function's own callers, Rule 1's whole-object reassignment branch,
   and oa_handle_loop's two per-reassigned-decl sites) -- a parameter
   that is itself a pointer to a struct/array, mutated through a
   narrower field/array-slot write (Stage 2a/2b's own single-slot
   invalidate calls), does NOT get this treatment. See .claude/plans/
   well-we-last-discussed-ethereal-duckling.md for why this and the
   nested-IILE-closure gap (a parameterized IILE's own aliasing
   parameters never get swept, since oa_resolve_iile_call never
   redirects current_function_decl to the closure's own operator())
   are both deliberately disclosed, bounded omissions rather than
   attempted here.  */

static bool
oa_could_alias_as_parameters (tree a, tree b)
{
  if (a == b || TREE_CODE (a) != PARM_DECL || TREE_CODE (b) != PARM_DECL)
    return false;
  tree ta = TREE_TYPE (a), tb = TREE_TYPE (b);
  if (!((POINTER_TYPE_P (ta) || TREE_CODE (ta) == REFERENCE_TYPE)
	&& (POINTER_TYPE_P (tb) || TREE_CODE (tb) == REFERENCE_TYPE)))
    return false;
  if (TYPE_RESTRICT (ta) || TYPE_RESTRICT (tb))
    return false;
  tree pa = TREE_TYPE (ta), pb = TREE_TYPE (tb);
  /* 'void*' can legitimately alias any object type (a common C-API
     parameter shape) -- found during this stage's own design review:
     a 'void*' parameter's own pointee is never TYPE_MAIN_VARIANT-equal
     to any concrete type, so a plain type-match comparison alone would
     never group it with anything. (A reference operand can never have
     a void referent at all, so this never spuriously fires for the
     REFERENCE_TYPE side.)  */
  if (VOID_TYPE_P (pa) || VOID_TYPE_P (pb))
    return true;
  return TYPE_MAIN_VARIANT (pa) == TYPE_MAIN_VARIANT (pb);
}

/* Public wrapper, the same "thin, non-static, same name + _public
   suffix" shape as this file's own dozen-plus other GIMPLE-engine-
   facing exports (oa_collect_conjuncts_public, oa_strip_symbolic_ptr_
   expr_public, etc.) -- oa_could_alias_as_parameters itself needs no
   change at all to be reused by contracts-gimple.cc's own Stage 4e:
   it only ever inspects two PARM_DECLs' own types (TYPE_RESTRICT,
   TYPE_MAIN_VARIANT, VOID_TYPE_P), nothing AST-specific.  */

bool
oa_could_alias_as_parameters_public (tree a, tree b)
{
  return oa_could_alias_as_parameters (a, b);
}

/* IDENTITY is guarded to be a genuine parameter of the currently-
   analyzed function before doing anything, so invalidating a local
   variable's own identity (the overwhelmingly common case) is an
   immediate, cheap no-op: this is specifically about two parameters
   potentially being the *same caller-supplied object*, not about a
   local coincidentally sharing a parameter's type. 'this' needs no
   special-casing -- it is spliced in as the head of the real
   DECL_ARGUMENTS chain for every non-static member function (confirmed
   via method.cc's own build_this_parm, and by direct tree-dump
   inspection), so the loop below already enumerates it.  */

static void
oa_invalidate_parameter_alias_group (tree identity, oa_env &env)
{
  if (!current_function_decl || TREE_CODE (identity) != PARM_DECL
      || DECL_CONTEXT (identity) != current_function_decl)
    return;
  for (tree parm = DECL_ARGUMENTS (current_function_decl); parm;
       parm = DECL_CHAIN (parm))
    {
      if (!oa_could_alias_as_parameters (identity, parm))
	continue;
      env.predicate_fact_invalidate (parm);
      env.contract_field_range_invalidate_all (parm);
      env.contract_call_range_invalidate_all (parm);
      env.field_alias_invalidate_all (parm);
      env.array_alias_invalidate_all (parm);
      env.field_object_predicate_invalidate_all (parm);
    }
}

/* Shared-substrate invalidation rule 2 (see the plan's own
   "Invalidation" design section): a tracked object's fact must be
   invalidated by *any* call taking its address, not just the one
   recognized as re-establishing it -- the analysis has no way to know
   an arbitrary, uncontracted function didn't change that object's
   logical state.  Invalidating unconditionally here and letting oa_
   handle_call_symbolic_postcondition_establishment run *after* this in
   oa_scan_calls_in_expr (so it cleanly overwrites whatever this just
   invalidated) is simpler than trying to detect and skip the
   re-establishing case specially.  Also drops every tracked ptr->field
   range fact for the same identity (contract_field_range_invalidate_all)
   -- a whole-object invalidation, same reasoning, same granularity as
   the predicate fact just above.  Uses oa_invalidation_identity_decl,
   not oa_object_identity_decl -- see that function's own comment.  */

static void
oa_invalidate_symbolic_facts_for_call_args (tree call, oa_env &env)
{
  if (oa_call_is_conversion_operator_call (call))
    return;
  int nargs = call_expr_nargs (call);
  for (int i = 0; i < nargs; ++i)
    {
      tree identity;
      if (oa_invalidation_identity_decl (CALL_EXPR_ARG (call, i), &identity)
	  || oa_field_slot_identity (CALL_EXPR_ARG (call, i), env, &identity)
	  || oa_array_slot_identity (CALL_EXPR_ARG (call, i), env, &identity)
	  || oa_field_object_identity (CALL_EXPR_ARG (call, i), env, &identity))
	{
	  identity = env.alias_find (identity);
	  env.predicate_fact_invalidate (identity);
	  env.contract_field_range_invalidate_all (identity);
	  env.contract_call_range_invalidate_all (identity);
	  env.field_alias_invalidate_all (identity);
	  env.array_alias_invalidate_all (identity);
	  env.field_object_predicate_invalidate_all (identity);
	  oa_invalidate_parameter_alias_group (identity, env);
	}
    }
}

/* -fcontract-symbolic-proofs: static-prover analogue of Mechanism B's
   own runtime invalidation (oa_invalidate_scalar_shadow_for_call_args,
   below) for a bare scalar's own compile-time tracked range -- same
   "not oa_object_identity_decl, `this`/`ADDR_EXPR`-of-decl only" logic
   (passing a plain scalar *by value* never aliases it, so it must not
   invalidate its own range fact; only a genuine address-of does), just
   updating ENV's own map directly rather than emitting code.  */

static void
oa_invalidate_symbolic_scalar_range_for_call_args (tree call, oa_env &env)
{
  if (oa_call_is_conversion_operator_call (call))
    return;
  int nargs = call_expr_nargs (call);
  for (int i = 0; i < nargs; ++i)
    {
      tree arg = STRIP_ANY_LOCATION_WRAPPER (CALL_EXPR_ARG (call, i));
      while (TREE_CODE (arg) == NON_LVALUE_EXPR || TREE_CODE (arg) == NOP_EXPR
	     || TREE_CODE (arg) == CONVERT_EXPR
	     || TREE_CODE (arg) == VIEW_CONVERT_EXPR)
	arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
      tree identity = NULL_TREE;
      if (is_this_parameter (arg))
	identity = arg;
      else if (TREE_CODE (arg) == ADDR_EXPR)
	{
	  tree op = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
	  if (DECL_P (op) && (VAR_P (op) || TREE_CODE (op) == PARM_DECL))
	    identity = op;
	}
      if (identity)
	env.contract_scalar_range_invalidate (identity);
    }
}


/* -fcontract-symbolic-runtime-checks (Mechanism B): the exact same
   "any call taking this decl's address invalidates it" rule as
   oa_invalidate_symbolic_facts_for_call_args above, for a bare
   scalar's own shadow instead of an object-identity symbolic fact --
   a call taking '&y' has no way to promise it didn't change y's value,
   so any established range fact for y can no longer be trusted.
   Unlike that function (which mutates ENV's own compile-time map
   directly), this one only *emits real code* -- it appends a
   'shadow.is_valid = false;' assignment to *EXTRA (force-appended, a
   STATEMENT_LIST built up across possibly several calls in the same
   expression) for each argument whose decl already has a registered
   shadow; the caller is responsible for splicing *EXTRA into its own
   statement, exactly like the reassignment case in the INIT_EXPR/
   MODIFY_EXPR walker above does.  Never creates a shadow that doesn't
   already exist, matching that same "never establish just to
   invalidate" discipline.  */

static void
oa_invalidate_scalar_shadow_for_call_args (tree call, oa_env &env, tree *extra)
{
  if (!oa_symbolic_codegen_active)
    return;
  if (oa_call_is_conversion_operator_call (call))
    return;
  int nargs = call_expr_nargs (call);
  for (int i = 0; i < nargs; ++i)
    {
      /* Deliberately *not* oa_object_identity_decl here: that helper's
	 own trailing "a bare VAR_DECL/PARM_DECL used directly" branch
	 exists for Mechanism A's own domain, where the tracked decl is
	 always pointer-typed, so passing the pointer *value* itself
	 already amounts to passing an address. A Mechanism B shadow
	 tracks a plain integer scalar -- passing 'y' *by value* (e.g.
	 'printf("%d", y)') gives the callee no way to alias or modify
	 the caller's own y at all, and must not invalidate its shadow;
	 only a genuine address-of ('&y') or 'this' should.  */
      tree arg = STRIP_ANY_LOCATION_WRAPPER (CALL_EXPR_ARG (call, i));
      while (TREE_CODE (arg) == NON_LVALUE_EXPR || TREE_CODE (arg) == NOP_EXPR
	     || TREE_CODE (arg) == CONVERT_EXPR
	     || TREE_CODE (arg) == VIEW_CONVERT_EXPR)
	arg = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
      tree identity = NULL_TREE;
      if (is_this_parameter (arg))
	identity = arg;
      else if (TREE_CODE (arg) == ADDR_EXPR)
	{
	  tree op = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (arg, 0));
	  if (DECL_P (op) && (VAR_P (op) || TREE_CODE (op) == PARM_DECL))
	    identity = op;
	}
      if (!identity)
	continue;
      tree shadow = env.shadow_get (identity);
      if (!shadow)
	continue;
      tree type = TREE_TYPE (shadow);
      tree lhs_ref = build3 (COMPONENT_REF, TREE_TYPE (oa_shadow_field (type, 0)),
			      shadow, oa_shadow_field (type, 0), NULL_TREE);
      tree a = build2 (MODIFY_EXPR, TREE_TYPE (lhs_ref), lhs_ref,
			boolean_false_node);
      TREE_SIDE_EFFECTS (a) = 1;
      append_to_statement_list_force (a, extra);
    }
}

/* D4324/P2680 item 6: the complementary direction from item 7 above --
   a callee's own non-ignored, conveyor *postcondition* is a trusted
   fact about *any* call's return value, not a per-call obligation the
   caller must discharge. Unlike item 7, no argument substitution is
   needed at all: a postcondition's guarantee about its own named
   result identifier (POSTCONDITION_IDENTIFIER, the same accessor
   oa_handle_postcondition_stmt already uses for a function's own
   postcondition) holds unconditionally for every successful call, so
   this only ever consults CALL's callee declaration, never CALL's own
   arguments or the caller's ENV.  Three near-identical small
   functions, one per fact map, mirroring how oa_provable_p/oa_
   provably_nonzero_p/oa_get_range are themselves three separate
   functions rather than one combined multi-output one.  */

static bool
oa_call_postcondition_object_address_p (tree call)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return false;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      if (!oa_contract_conveyor_active_p (contract, callee))
	continue;
      tree result_id = POSTCONDITION_IDENTIFIER (contract);
      if (!result_id || (!VAR_P (result_id) && TREE_CODE (result_id) != PARM_DECL))
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree arg;
	  if (is_object_address_call_p (*conjuncts[i], &arg))
	    {
	      STRIP_ANY_LOCATION_WRAPPER (arg);
	      if (arg == result_id)
		return true;
	    }
	}
    }
  return false;
}

/* Same idea, for item 8's "provably nonzero" fact -- a bare
   'r != 0'/'0 != r' conjunct only. A comparison-based exclusion of
   zero (e.g. 'r > 0') needs no separate handling here: it is already
   covered by oa_provably_nonzero_p's own existing range-fact
   supplementary check, once oa_call_postcondition_range_p below is
   consulted from oa_get_range -- exactly why oa_nonzero_conjunct_p and
   oa_refine_single_comparison are two separate mechanisms everywhere
   else in this pass.  */

static bool
oa_call_postcondition_nonzero_p (tree call)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return false;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      if (!oa_contract_conveyor_active_p (contract, callee))
	continue;
      tree result_id = POSTCONDITION_IDENTIFIER (contract);
      if (!result_id || (!VAR_P (result_id) && TREE_CODE (result_id) != PARM_DECL))
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree decl;
	  if (oa_nonzero_conjunct_p (*conjuncts[i], &decl) && decl == result_id)
	    return true;
	}
    }
  return false;
}

/* Same idea, for item 8's value-range fact -- applies every matching
   postcondition's conjuncts via oa_refine_single_comparison ("trusted
   true," exactly as a precondition/contract_assert conjunct already is
   for the current function's own body, Increment E4) into one scratch,
   otherwise-empty ENV keyed at the postcondition's own result
   identifier, then reads back whatever fact accumulated there. A
   second postcondition naming a *different* result identifier is
   conservatively skipped rather than risking a wrong merge under one
   key -- a deliberate simplification for the rare case of more than
   one named postcondition; in practice a function has at most one.  */

/* DERIV_OUT, when non-NULL and -fcontract-conveyor-proof-provenance is
   active, receives an OA_DERIV_CALL node wrapping one OA_DERIV_AXIOM per
   contributing postcondition contract (in practice at most one, per
   this function's own comment above) -- a thin, purely additional
   recording of exactly what's already being computed below, changing no
   computation itself.  A second call to this same function purely to
   fetch DERIV_OUT (see oa_get_range_derivation further below) is
   deliberately allowed to redundantly recompute the fact: this is a
   cheap, side-effect-free query over contract specifiers, and only ever
   happens when provenance tracking is explicitly requested.  */

static bool
oa_call_postcondition_range_p (tree call, oa_env &env, oa_range_fact *out,
				oa_derivation **deriv_out)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return false;

  tree result_id = NULL_TREE;
  oa_env scratch;
  bool any = false;
  tree last_contract = NULL_TREE;
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      if (!oa_contract_conveyor_active_p (contract, callee))
	continue;
      tree rid = POSTCONDITION_IDENTIFIER (contract);
      if (!rid || (!VAR_P (rid) && TREE_CODE (rid) != PARM_DECL))
	continue;
      if (!result_id)
	result_id = rid;
      else if (rid != result_id)
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      any = true;
      last_contract = contract;
      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	oa_refine_single_comparison (*conjuncts[i], scratch, /*asserted_true=*/true);

      /* 'post<ctrl>(r: r < this->size ())' -- RESULT_ID related to a
	 call rather than a literal, so there's nothing for the plain
	 refinement above to resolve on its own (this->size () is not a
	 known point). Compose with the *caller's* own already-
	 established call-range fact for the substituted receiver
	 instead: if the caller already knows 'v.size ()' is in
	 [lo, hi], and the postcondition says 'r < this->size ()', the
	 call's own result is therefore in (-inf, hi - 1].  Only ever
	 tightens SCRATCH's existing result_id fact (never overwrites
	 it), so a literal-bounded conjunct on the same result_id
	 combines correctly with this one, same as everywhere else in
	 this file.

	 Ordering hazard, found via direct testing: CALL is itself the
	 statement that exposes its own receiver, so if this function is
	 reached lazily -- via oa_get_range, itself only ever called
	 *after* oa_scan_calls_in_expr has already invalidated this same
	 call's own exposed arguments (INIT_EXPR/MODIFY_EXPR's own
	 handling) -- any fact the caller established about that same
	 receiver *earlier* is already gone by the time this composition
	 runs (confirmed: this alone never fires for 'if (v.size () > 3)
	 { int y = f (v); ... }'). Not a bug in the composition itself:
	 it's the same "a call's own exposure of its receiver invalidates
	 facts about that receiver, unconditionally, before this same
	 call's own effects are computed" rule already applied throughout
	 this file -- this is simply the first item-6 shape whose own
	 composition needs a fact about one of the *current* call's own
	 arguments (the literal and param-relation shapes above never
	 needed one). Fixed not by reordering oa_scan_calls_in_expr's own
	 shared, foundational invalidate/compute steps (which every other
	 feature in this file also depends on), but by also calling this
	 same composition *eagerly*, from oa_walk_stmt's own INIT_EXPR/
	 MODIFY_EXPR case, before that same invalidation runs -- see oa_
	 compose_call_result_range below, and contracts-gimple.cc's own
	 cg_compose_call_result_range, which fixes the identical ordering
	 problem the identical way on the GIMPLE side. This lazy path
	 (oa_get_range/here) remains as the correct fallback for every
	 context that isn't a direct 'lhs = call(...)' assignment (e.g. a
	 call's return value used directly in a larger expression), where
	 there is no "eager, pre-invalidation" moment to hook into to
	 begin with, and for the case oa_compose_call_result_range's own
	 comment describes: a fact established by something *other* than
	 CALL's own exposure of the same receiver.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree_code rcode;
	  tree rhs_receiver, rhs_callee;
	  if (!oa_match_result_call_relation (*conjuncts[i], result_id, &rcode,
					       &rhs_receiver, &rhs_callee,
					       /*allow_symbolic_accessor=*/false)
	      || TREE_CODE (rhs_receiver) != PARM_DECL)
	    continue;
	  tree sub_receiver = oa_substitute_call_arg (callee, call, rhs_receiver);
	  if (!sub_receiver)
	    continue;
	  sub_receiver = oa_strip_conversion_call (sub_receiver);
	  tree identity;
	  if (!oa_object_identity_decl (sub_receiver, &identity)
	      && !oa_field_slot_identity (sub_receiver, env, &identity)
	      && !oa_array_slot_identity (sub_receiver, env, &identity)
	      && !oa_field_object_identity (sub_receiver, env, &identity))
	    continue;
	  identity = env.alias_find (identity);
	  oa_contract_field_range_fact established;
	  if (!env.contract_call_range_get (identity, rhs_callee, &established))
	    continue;

	  oa_range_fact &derived = established.range;
	  oa_range_fact refined;
	  if (!scratch.range_get (result_id, &refined))
	    {
	      refined.base = NULL_TREE;
	      refined.has_lo = refined.has_hi = false;
	    }
	  switch (rcode)
	    {
	    case LT_EXPR:
	      if (derived.has_hi
		  && (!refined.has_hi || derived.hi - 1 < refined.hi))
		{ refined.has_hi = true; refined.hi = derived.hi - 1; }
	      break;
	    case LE_EXPR:
	      if (derived.has_hi && (!refined.has_hi || derived.hi < refined.hi))
		{ refined.has_hi = true; refined.hi = derived.hi; }
	      break;
	    case GT_EXPR:
	      if (derived.has_lo
		  && (!refined.has_lo || derived.lo + 1 > refined.lo))
		{ refined.has_lo = true; refined.lo = derived.lo + 1; }
	      break;
	    case GE_EXPR:
	      if (derived.has_lo && (!refined.has_lo || derived.lo > refined.lo))
		{ refined.has_lo = true; refined.lo = derived.lo; }
	      break;
	    case EQ_EXPR:
	      if (derived.has_lo && (!refined.has_lo || derived.lo > refined.lo))
		{ refined.has_lo = true; refined.lo = derived.lo; }
	      if (derived.has_hi && (!refined.has_hi || derived.hi < refined.hi))
		{ refined.has_hi = true; refined.hi = derived.hi; }
	      break;
	    default:
	      break;
	    }
	  if (refined.has_lo || refined.has_hi)
	    scratch.range_set (result_id, refined);
	}
    }
  if (!any || !result_id)
    return false;
  if (!scratch.range_get (result_id, out))
    return false;

  if (deriv_out && oa_active_provenance)
    {
      oa_derivation *axiom
	= oa_active_provenance->make_axiom (*out, last_contract);
      oa_derivation *call_deriv = oa_active_provenance->make_call (*out, callee);
      call_deriv->children.safe_push (axiom);
      *deriv_out = call_deriv;
    }
  return true;
}

/* -fcontract-symbolic-runtime-checks (Mechanism B, see
   .claude/plans/stateless-jumping-shore.md): CALL's own bare-scalar
   counterpart to oa_call_postcondition_range_p above -- same shape
   entirely (a postcondition whose own POSTCONDITION_IDENTIFIER is a
   bare VAR_DECL/PARM_DECL, refined via oa_refine_single_comparison into
   a scratch env), except gated on oa_contract_symbolic_active_p instead
   of oa_contract_conveyor_active_p.  Kept as its own, separate function
   rather than parameterizing the one above over which "is this active"
   predicate to use: conveyor and symbolic are two deliberately
   independent axes elsewhere in this file (see e.g.
   oa_predicate_conjunct_shape's own shared-but-unparameterized use by
   both), and entangling them behind one shared knob here would be the
   first place that stopped being true.  No provenance/derivation
   output -- -fcontract-conveyor-proof-provenance is a conveyor-only
   concept, orthogonal to this.  */

static bool
oa_call_symbolic_range_p (tree call, oa_range_fact *out)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return false;

  tree result_id = NULL_TREE;
  oa_env scratch;
  bool any = false;
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      if (!oa_contract_symbolic_active_p (contract, callee))
	continue;
      tree rid = POSTCONDITION_IDENTIFIER (contract);
      if (!rid || (!VAR_P (rid) && TREE_CODE (rid) != PARM_DECL))
	continue;
      if (!result_id)
	result_id = rid;
      else if (rid != result_id)
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      any = true;
      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	oa_refine_single_comparison (*conjuncts[i], scratch, /*asserted_true=*/true);
    }
  if (!any || !result_id)
    return false;
  return scratch.range_get (result_id, out);
}

/* -fcontract-symbolic-runtime-checks (Mechanism B): one (CONTRACT,
   PARAM, RANGE) match found by oa_precondition_symbolic_ranges below --
   CONTRACT is needed by the caller to find CTRL/build the dispatch,
   PARAM to positionally find the actual argument at the call site.  */

struct oa_symbolic_precondition_match
{
  tree contract;
  tree param;
  oa_range_fact range;
};

/* -fcontract-symbolic-runtime-checks (Mechanism B): the consult-side
   counterpart of oa_call_symbolic_range_p above -- CALLEE's own
   precondition(s) (not postconditions), each comparing one or more of
   CALLEE's *own parameters* directly (not its return-value binder), the
   bare-scalar shape Mechanism A's ptr->field-only comparison recognizer
   (oa_symbolic_comparison_conjunct_shape) deliberately excludes.  Unlike
   oa_call_symbolic_range_p, which combines every conjunct into one
   accumulator keyed by a single, already-known identifier
   (POSTCONDITION_IDENTIFIER), a precondition may compare several
   different parameters across its own conjuncts, and a callee may have
   more than one symbolic-active precondition contract at all (multiple
   control-object specifiers) -- so this collects every distinct bare
   PARM_DECL named by *any* conjunct of *every* symbolic-active
   precondition, appending one match per (contract, param) pair found,
   rather than stopping at the first.  Each contract's own conjuncts are
   refined into a single fresh SCRATCH env shared by every parameter that
   contract names, exactly as oa_call_symbolic_range_p does for its own
   single identifier.  */

static void
oa_precondition_symbolic_ranges (tree callee,
				  vec<oa_symbolic_precondition_match> *out)
{
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      if (!oa_contract_symbolic_active_p (contract, callee))
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts (&cond, &conjuncts);
      auto_vec<tree> params;
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree p, const_val;
	  tree_code code;
	  if (oa_match_simple_comparison (*conjuncts[i], &p, &code, &const_val)
	      && TREE_CODE (p) == PARM_DECL
	      && !params.contains (p))
	    params.safe_push (p);
	}
      if (params.is_empty ())
	continue;

      oa_env scratch;
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	oa_refine_single_comparison (*conjuncts[i], scratch, /*asserted_true=*/true);

      for (unsigned i = 0; i < params.length (); ++i)
	{
	  oa_range_fact range;
	  if (!scratch.range_get (params[i], &range))
	    continue;
	  oa_symbolic_precondition_match match
	    = { contract, params[i], range };
	  out->safe_push (match);
	}
    }
}

/* -fcontract-symbolic-proofs: the bare-scalar consult side, sibling of
   oa_handle_call_symbolic_precondition_obligation above (which only
   handles the predicate and ptr->field shapes) -- for each of CALL's
   callee's own symbolic-active preconditions comparing one of its own
   by-value parameters (oa_precondition_symbolic_ranges, already built
   for Mechanism B's runtime dispatch and reused here as-is), check
   whether ENV already has a compile-time established range for the
   substituted argument, with the same three-way subsumed/disjoint/
   partial outcome as the ptr->field consult above. Falls back to the
   general-purpose m_range_map (conveyor's own numeric checking's own
   map, populated by ordinary dataflow throughout the caller's body, not
   just by a callee's postcondition) when m_contract_scalar_range_map
   has nothing for this decl -- the same allowed conveyor-feeds-symbolic
   direction as is_object_address/nonzero's own new fallback just above,
   giving this check the same reach conveyor's own numeric checking
   already has. Only a plain integer range, never a pointer's own
   offset-into-array range (BASE null), is a meaningful fallback here --
   this obligation is about a *by-value scalar* argument, not a pointer,
   mirroring oa_provably_nonzero_p's own identical BASE-null guard on its
   own m_range_map fallback.  */

static void
oa_handle_call_symbolic_scalar_precondition_obligation (tree call, oa_env &env)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return;

  auto_vec<oa_symbolic_precondition_match> matches;
  oa_precondition_symbolic_ranges (callee, &matches);
  for (unsigned m = 0; m < matches.length (); ++m)
    {
      tree contract = matches[m].contract;
      tree param = matches[m].param;
      oa_range_fact &required = matches[m].range;

      tree substituted = oa_substitute_call_arg (callee, call, param);
      if (!substituted)
	continue;
      /* Full lookthrough (conversion operator and copy-construction)
	 -- this is a value fact like oa_get_range's own domain, not an
	 identity one, so unlike the predicate/field-range layer there's
	 no establish/invalidate-vs-consult asymmetry to worry about; a
	 class-typed argument reached via a conversion operator, or
	 forwarded by value through a real copy/move-constructor call,
	 must resolve the same way oa_get_range itself already does.  */
      tree arg_decl = oa_strip_conversion_call (STRIP_ANY_LOCATION_WRAPPER (substituted));
      if (!VAR_P (arg_decl) && TREE_CODE (arg_decl) != PARM_DECL)
	continue;

      bool strict = oa_contract_symbolic_strict_p (contract, callee);

      oa_range_fact established;
      if (!env.contract_scalar_range_get (arg_decl, &established))
	{
	  oa_range_fact fallback;
	  if (!env.range_get (arg_decl, &fallback) || fallback.base != NULL_TREE)
	    {
	      if (strict)
		error_at (EXPR_LOCATION (call),
			  "cannot prove that %qE satisfies the precondition "
			  "of %qD", substituted, callee);
	      else
		warning_at (EXPR_LOCATION (call), 0,
			    "cannot verify that %qE satisfies the precondition "
			    "of %qD", substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	      continue;
	    }
	  established = fallback;
	}

      oa_range_subsumption_result r = oa_range_subsumption (established, required);
      if (r == OA_RANGE_SUBSUMED)
	continue; /* Proven true: silently discharged.  */
      if (r == OA_RANGE_DISJOINT)
	{
	  error_at (EXPR_LOCATION (call),
		    "argument %qE provably violates the precondition of %qD",
		    substituted, callee);
	  inform (DECL_SOURCE_LOCATION (callee), "declared here");
	}
      else if (strict)
	{
	  error_at (EXPR_LOCATION (call),
		    "cannot prove that %qE satisfies the precondition "
		    "of %qD", substituted, callee);
	  inform (DECL_SOURCE_LOCATION (callee), "declared here");
	}
      else
	{
	  warning_at (EXPR_LOCATION (call), 0,
		      "cannot verify that %qE satisfies the precondition "
		      "of %qD", substituted, callee);
	  inform (DECL_SOURCE_LOCATION (callee), "declared here");
	}
    }
}

/* Thin wrapper over oa_contract_symbolic_active_p, for a plugin.  */

bool
oa_contract_symbolic_active_public (tree contract, tree owner_fn)
{
  return oa_contract_symbolic_active_p (contract, owner_fn);
}

/* Thin wrapper over oa_predicate_conjunct_shape, for a plugin -- both
   built-in checkers already share this shape-recognizer internally, so
   a plugin needn't reimplement it (previously duplicated locally inside
   conveyor_proof_plugin.cc; that duplication is removed now that this
   export exists -- see .claude/plans/well-we-last-discussed-ethereal-
   duckling.md).  */

bool
oa_match_predicate_conjunct (tree conjunct, tree *pred_fn_out,
			      tree *arg_decl_out, bool *negated_out)
{
  return oa_predicate_conjunct_shape (conjunct, pred_fn_out, arg_decl_out,
				       negated_out);
}

/* Thin wrappers over oa_symbolic_comparison_conjunct_shape/oa_strip_
   symbolic_ptr_expr, for a plugin that needs the ptr->field comparison
   shape directly (see their own declarations in contracts.h for why:
   oa_precondition_field_range_obligations's own PRECONDITION_P/
   oa_contract_fact_tracking_active_p-gated iteration doesn't cover
   every use a plugin might have for this shape, e.g. collecting the
   POSTCONDITION_P side too).  */

bool
oa_match_field_range_comparison (tree conjunct, tree *field_out,
				  tree *ptr_expr_out, tree_code *code_out,
				  tree *const_val_out)
{
  return oa_symbolic_comparison_conjunct_shape (conjunct, field_out,
						 ptr_expr_out, code_out,
						 const_val_out);
}

tree
oa_strip_symbolic_ptr_expr_public (tree ptr_expr)
{
  return oa_strip_symbolic_ptr_expr (ptr_expr);
}

/* Public, plugin-facing wrapper over oa_env_predicate_result.  ENV's
   dynamic type is always really oa_env (the same reinterpret_cast idiom
   oa_env_check_comparison already uses further below).  REQUIRE_CONVEYOR:
   see oa_env_predicate_result's own comment -- the conveyor plugin
   passes true, the symbolic plugin passes false.  */

oa_proof_result
oa_env_check_predicate_fact (oa_analysis_env *env, tree obj_expr, tree pred_fn,
			      bool required_polarity, bool require_conveyor)
{
  return oa_env_predicate_result (*reinterpret_cast<oa_env *> (env), obj_expr,
				   pred_fn, required_polarity, require_conveyor);
}

/* Build a plain-tree-bounds REQUIRED oa_range_fact from a plugin's own
   has_lo/lo/has_hi/hi arguments, for the two range-fact query wrappers
   below.  */

static oa_range_fact
oa_range_fact_from_bounds (bool has_lo, tree lo, bool has_hi, tree hi)
{
  oa_range_fact required;
  required.base = NULL_TREE;
  required.has_lo = has_lo;
  required.has_hi = has_hi;
  if (has_lo)
    required.lo = wi::to_widest (lo);
  if (has_hi)
    required.hi = wi::to_widest (hi);
  return required;
}

/* Public, plugin-facing wrapper: does ENV's own contract-established
   range for EXPR (m_contract_scalar_range_map) subsume, is disjoint
   from, or only partially overlap the required [lo,hi]?  EXPR (a
   plugin's own substituted call argument, never stripped by the
   plugin itself) can arrive wrapped in a plain conversion node (a
   by-value int argument found wrapped in a VIEW_CONVERT_EXPR by direct
   testing), or reach a class-typed decl via its own conversion
   operator, or arrive as a by-value copy-construction materialization
   -- oa_strip_conversion_call handles all three (consult-only lookthrough
   is sound here: a scalar range, like nonzero-ness, is copy-invariant),
   so the bare decl m_contract_scalar_range_map is actually keyed on
   isn't silently missed as "no fact".  */

oa_proof_result
oa_env_check_scalar_range_fact (oa_analysis_env *env, tree expr, bool has_lo,
				 tree lo, bool has_hi, tree hi)
{
  oa_env &e = *reinterpret_cast<oa_env *> (env);
  expr = oa_strip_conversion_call (STRIP_ANY_LOCATION_WRAPPER (expr));
  oa_range_fact established;
  if (!e.contract_scalar_range_get (expr, &established))
    return OA_UNKNOWN;
  oa_range_fact required = oa_range_fact_from_bounds (has_lo, lo, has_hi, hi);
  switch (oa_range_subsumption (established, required))
    {
    case OA_RANGE_SUBSUMED: return OA_PROVEN_TRUE;
    case OA_RANGE_DISJOINT: return OA_PROVEN_FALSE;
    default: return OA_UNKNOWN;
    }
}

/* Same, for FIELD of the object identified by BASE_EXPR
   (m_contract_field_range_map).  REQUIRE_CONVEYOR: see oa_env_check_
   predicate_fact's own comment -- the conveyor plugin passes true, the
   symbolic plugin passes false.  */

oa_proof_result
oa_env_check_field_range_fact (oa_analysis_env *env, tree base_expr,
				tree field, bool has_lo, tree lo, bool has_hi,
				tree hi, bool require_conveyor)
{
  oa_env &e = *reinterpret_cast<oa_env *> (env);
  /* Consult-only copy-construction lookthrough -- see oa_handle_call_
     symbolic_precondition_obligation's own field-range block for why
     it's sound specifically at a consult site.  */
  base_expr = oa_strip_conversion_call (base_expr);
  tree identity;
  if (!oa_object_identity_decl (base_expr, &identity)
      && !oa_field_slot_identity (base_expr, e, &identity)
      && !oa_array_slot_identity (base_expr, e, &identity)
      && !oa_field_object_identity (base_expr, e, &identity))
    return OA_UNKNOWN;
  identity = e.alias_find (identity);
  oa_contract_field_range_fact established;
  if (!e.contract_field_range_get (identity, field, &established)
      || (require_conveyor && !established.conveyor_established))
    return OA_UNKNOWN;
  oa_range_fact required = oa_range_fact_from_bounds (has_lo, lo, has_hi, hi);
  switch (oa_range_subsumption (established.range, required))
    {
    case OA_RANGE_SUBSUMED: return OA_PROVEN_TRUE;
    case OA_RANGE_DISJOINT: return OA_PROVEN_FALSE;
    default: return OA_UNKNOWN;
    }
}

/* The call-range analogue of oa_env_check_field_range_fact immediately
   above, for a call to CALLEE_FN (a DECL_DECLARED_CONVEYOR_P accessor)
   on the object identified by RECEIVER_EXPR (m_contract_call_range_map)
   rather than a ptr->field access.  REQUIRE_CONVEYOR: see oa_env_check_
   predicate_fact's own comment -- the conveyor plugin passes true, the
   symbolic plugin passes false.  */

oa_proof_result
oa_env_check_call_range_fact (oa_analysis_env *env, tree receiver_expr,
				tree callee_fn, bool has_lo, tree lo,
				bool has_hi, tree hi, bool require_conveyor)
{
  oa_env &e = *reinterpret_cast<oa_env *> (env);
  /* Consult-only copy-construction lookthrough -- see oa_handle_call_
     symbolic_precondition_obligation's own call-range block for why
     it's sound specifically at a consult site.  */
  receiver_expr = oa_strip_conversion_call (receiver_expr);
  tree identity;
  if (!oa_object_identity_decl (receiver_expr, &identity)
      && !oa_field_slot_identity (receiver_expr, e, &identity)
      && !oa_array_slot_identity (receiver_expr, e, &identity)
      && !oa_field_object_identity (receiver_expr, e, &identity))
    return OA_UNKNOWN;
  identity = e.alias_find (identity);
  oa_contract_field_range_fact established;
  if (!e.contract_call_range_get (identity, callee_fn, &established)
      || (require_conveyor && !established.conveyor_established))
    return OA_UNKNOWN;
  oa_range_fact required = oa_range_fact_from_bounds (has_lo, lo, has_hi, hi);
  switch (oa_range_subsumption (established.range, required))
    {
    case OA_RANGE_SUBSUMED: return OA_PROVEN_TRUE;
    case OA_RANGE_DISJOINT: return OA_PROVEN_FALSE;
    default: return OA_UNKNOWN;
    }
}

/* Public, plugin-facing wrapper over oa_precondition_symbolic_ranges --
   hides oa_range_fact/oa_symbolic_precondition_match behind plain tree
   bounds, invoking CALLBACK once per (contract, param) match found.  */

void
oa_precondition_scalar_range_obligations
  (tree callee,
   void (*callback) (tree, tree, bool, tree, bool, tree, void *),
   void *data)
{
  auto_vec<oa_symbolic_precondition_match> matches;
  oa_precondition_symbolic_ranges (callee, &matches);
  for (unsigned i = 0; i < matches.length (); ++i)
    {
      oa_range_fact &r = matches[i].range;
      tree lo = r.has_lo
	? wide_int_to_tree (long_long_integer_type_node, r.lo) : NULL_TREE;
      tree hi = r.has_hi
	? wide_int_to_tree (long_long_integer_type_node, r.hi) : NULL_TREE;
      callback (matches[i].contract, matches[i].param, r.has_lo, lo,
		r.has_hi, hi, data);
    }
}

/* Same, for the ptr->field shape -- iterates CALLEE's own active
   preconditions directly (oa_collect_contract_field_ranges has no per-
   callee driver of its own the way oa_precondition_symbolic_ranges does
   for the bare-scalar shape), gated on oa_contract_fact_tracking_
   active_p (conveyor- or symbolic-active) -- unlike oa_precondition_
   scalar_range_obligations, which stays symbolic-only (m_contract_
   scalar_range_map has no conveyor-side gap to close, see oa_call_
   symbolic_range_p's own comment), this export is genuinely shared:
   both conveyor_proof_plugin.cc and symbolic_proof_plugin.cc use it.
   Since CALLEE could carry preconditions of *both* flavors, each
   caller filters the CONTRACT a match came from by its own flavor
   (oa_contract_conveyor_active_public / oa_contract_symbolic_active_
   public) before treating a match as its own obligation to check --
   -fcontract-conveyor-proofs's own built-in field-range checking,
   oa_handle_call_conveyor_field_range_obligation above, does its own
   independent, conveyor-gated iteration instead of going through this
   export, so it needs no such filter.  BASE_PARM is CALLEE's own
   PARM_DECL (including 'this'); the caller is responsible for
   positional substitution to its own call site's actual argument,
   exactly as for oa_precondition_scalar_range_obligations's own
   PARAM.  */

void
oa_precondition_field_range_obligations
  (tree callee,
   void (*callback) (tree, tree, tree, bool, tree, bool, tree, void *),
   void *data)
{
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      if (!oa_contract_fact_tracking_active_p (contract, callee))
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<oa_symbolic_field_group> field_groups;
      oa_collect_contract_field_ranges (cond, &field_groups);
      for (unsigned i = 0; i < field_groups.length (); ++i)
	{
	  tree ptr_expr = oa_strip_symbolic_ptr_expr (field_groups[i].ptr_expr);
	  if (TREE_CODE (ptr_expr) != PARM_DECL)
	    continue;
	  oa_range_fact &r = field_groups[i].range;
	  tree lo = r.has_lo
	    ? wide_int_to_tree (long_long_integer_type_node, r.lo) : NULL_TREE;
	  tree hi = r.has_hi
	    ? wide_int_to_tree (long_long_integer_type_node, r.hi) : NULL_TREE;
	  callback (contract, field_groups[i].field, ptr_expr, r.has_lo, lo,
		    r.has_hi, hi, data);
	}
    }
}

/* The call-range analogue of oa_precondition_field_range_obligations
   immediately above: CALLBACK is invoked once per (contract, callee_fn,
   receiver_parm, required [lo,hi]) match, for a call to a DECL_
   DECLARED_CONVEYOR_P accessor (e.g. 'n < this->size ()') rather than a
   ptr->field access, named in one of CALLEE's own active preconditions.
   Same shared-substrate, both-flavors-possible discipline: the caller
   filters matches to its own flavor exactly as for the field-range
   export.  RECEIVER_PARM is CALLEE's own PARM_DECL (including 'this');
   the caller is responsible for positional substitution to its own call
   site's actual argument.  */

void
oa_precondition_call_range_obligations
  (tree callee,
   void (*callback) (tree, tree, tree, bool, tree, bool, tree, void *),
   void *data)
{
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!PRECONDITION_P (contract))
	continue;
      if (!oa_contract_fact_tracking_active_p (contract, callee))
	continue;

      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      /* This export serves both conveyor's and symbolic's own plugin-
	 side consumers, filtering matches to its own flavor via CONTRACT
	 itself once handed to CALLBACK (see the comment above) -- so the
	 shape's own accessor gate must widen exactly when CONTRACT itself
	 is symbolic-, not conveyor-, active, the same per-contract flavor
	 rule used everywhere else in this file.  */
      auto_vec<oa_symbolic_call_group> call_groups;
      oa_collect_contract_call_ranges
	(cond, &call_groups,
	 /*allow_symbolic_accessor=*/
	   !oa_contract_conveyor_active_p (contract, callee));
      for (unsigned i = 0; i < call_groups.length (); ++i)
	{
	  if (TREE_CODE (call_groups[i].receiver_expr) != PARM_DECL)
	    continue;
	  oa_range_fact &r = call_groups[i].range;
	  tree lo = r.has_lo
	    ? wide_int_to_tree (long_long_integer_type_node, r.lo) : NULL_TREE;
	  tree hi = r.has_hi
	    ? wide_int_to_tree (long_long_integer_type_node, r.hi) : NULL_TREE;
	  callback (contract, call_groups[i].callee, call_groups[i].receiver_expr,
		    r.has_lo, lo, r.has_hi, hi, data);
	}
    }
}

/* -fcontract-symbolic-runtime-checks (Mechanism B): the shared shadow
   RECORD_TYPE every tracked bare-scalar decl's own shadow VAR_DECL
   uses -- built once (mirrors build_predicate_arg_struct_type's own
   "build once, reuse" shape further below) and cached, since every
   shadow needs the exact same five fields regardless of which decl it
   tracks: IS_VALID (has a fact been established here, and not since
   invalidated), HAS_LO/LO and HAS_HI/HI (the bound itself). LO and HI
   are both *inclusive* -- unlike Mechanism A's own, unrelated
   established/required range payload (deliberately half-open, see
   oa_collect_symbolic_actions), Mechanism B's own establish
   (oa_call_symbolic_range_p) and consult (oa_precondition_symbolic_
   range_p) sides both derive their ranges from the pre-existing
   oa_range_fact/oa_refine_single_comparison/oa_tighten_range_bound
   machinery already used throughout this file for -fcontract-conveyor-
   proofs, whose own HI is inclusive (see oa_tighten_range_bound's own
   LE_EXPR/LT_EXPR cases) -- so the shadow keeps that value unchanged,
   rather than introducing an unnecessary, error-prone +-1 conversion
   at either end.  */

static GTY(()) tree oa_symbolic_shadow_type_cache;

static tree
oa_symbolic_shadow_type ()
{
  if (oa_symbolic_shadow_type_cache)
    return oa_symbolic_shadow_type_cache;

  static const char *const names[5]
    = { "is_valid", "has_lo", "lo", "has_hi", "hi" };
  tree type = make_node (RECORD_TYPE);
  tree fields = NULL_TREE;
  tree *last = &fields;
  for (unsigned i = 0; i < 5; ++i)
    {
      tree field_type = (i == 2 || i == 4)
	? long_long_integer_type_node : boolean_type_node;
      tree field = build_decl (BUILTINS_LOCATION, FIELD_DECL,
				get_identifier (names[i]), field_type);
      DECL_CONTEXT (field) = type;
      *last = field;
      last = &DECL_CHAIN (field);
    }
  TYPE_FIELDS (type) = fields;
  layout_type (type);

  oa_symbolic_shadow_type_cache = type;
  return type;
}

/* Return the INDEXth (0-based) field of the shadow RECORD_TYPE, in the
   fixed order oa_symbolic_shadow_type above builds them.  */

static tree
oa_shadow_field (tree type, unsigned index)
{
  tree f = TYPE_FIELDS (type);
  for (unsigned i = 0; i < index; ++i)
    f = DECL_CHAIN (f);
  return f;
}

/* Return DECL's shadow VAR_DECL, creating it if this is the first time
   it's needed anywhere in the current function -- see the plan's own
   "shadow placement" note for why it always lives in ENV's
   OUTERMOST_BIND (the current function's own top-level scope) rather
   than wherever the walk happens to be right now: DECL (and any later
   consult of its shadow) may live in an enclosing scope even when the
   *first* establishing assignment the walk encounters is nested inside
   an if/switch/loop. Declines (returns NULL_TREE) if Mechanism B isn't
   active for this walk, or if OUTERMOST_BIND was never captured (see
   oa_resolve_object_address_in_function_1's own defensive check).

   Inserted via tsi_link_before, not the push_stmt_list/add_decl_expr
   machinery build_contract_check's own codegen uses: that machinery
   assumes an active genericization-time parsing context that doesn't
   exist during oa_walk_stmt's own, earlier, pre-genericize walk;
   tsi_link_before instead splices a new statement directly into an
   existing STATEMENT_LIST without depending on any such ambient state,
   and without disturbing any *other* iterator already in progress over
   the same list (in particular, the very tsi_start/tsi_next loop in
   oa_walk_stmt's own STATEMENT_LIST case, several frames up the call
   stack when this runs from a nested statement).  */

static tree
get_or_build_scalar_shadow (tree decl, oa_env &env)
{
  tree existing = env.shadow_get (decl);
  if (existing)
    return existing;

  if (!oa_symbolic_codegen_active)
    return NULL_TREE;
  tree outer = env.outermost_bind ();
  if (!outer)
    return NULL_TREE;

  location_t loc = DECL_SOURCE_LOCATION (decl);
  tree type = oa_symbolic_shadow_type ();
  tree shadow = build_decl (loc, VAR_DECL, NULL_TREE, type);
  DECL_ARTIFICIAL (shadow) = 1;
  DECL_IGNORED_P (shadow) = 1;
  TREE_USED (shadow) = 1;
  DECL_CONTEXT (shadow) = current_function_decl;
  /* Value-initialize: an empty CONSTRUCTOR zero-initializes every
     field, so IS_VALID starts false (no fact established yet) without
     needing a separate explicit assignment.  */
  DECL_INITIAL (shadow) = build_constructor (type, NULL);
  layout_decl (shadow, 0);

  DECL_CHAIN (shadow) = BIND_EXPR_VARS (outer);
  BIND_EXPR_VARS (outer) = shadow;

  /* Prepend a DECL_EXPR for SHADOW ahead of whatever OUTER's body
     already contains (a bare single statement or an existing
     STATEMENT_LIST -- append_to_statement_list_force handles both
     uniformly, allocating/converting to a STATEMENT_LIST as needed).
     This builds a brand new STATEMENT_LIST tree object rather than
     mutating the existing one in place, so it can never disturb any
     tree_stmt_iterator some other, already-in-progress stack frame
     might hold into the *old* body (in particular, the very
     tsi_start/tsi_next loop in oa_walk_stmt's own STATEMENT_LIST case,
     several frames up the call stack when this runs from a nested
     statement) -- that iterator simply keeps walking the old, otherwise
     still-intact tree object to completion, unaware BIND_EXPR_BODY now
     points elsewhere.  Uses the _force variant (not the plain, side-
     effect-sniffing append_to_statement_list) since a raw, hand-built
     DECL_EXPR/existing body might not have TREE_SIDE_EFFECTS set the
     way semantically-constructed trees normally would, and this must
     never be silently dropped.  */
  tree new_body = alloc_stmt_list ();
  append_to_statement_list_force (build_stmt (loc, DECL_EXPR, shadow), &new_body);
  append_to_statement_list_force (BIND_EXPR_BODY (outer), &new_body);
  BIND_EXPR_BODY (outer) = new_body;

  env.shadow_set (decl, shadow);
  return shadow;
}

/* -fcontract-symbolic-runtime-checks (Mechanism B): cache, keyed by the
   precondition CONTRACT's own identity (mirrors contract_predicate_
   core_fn's own cache), of a small, purpose-built thunk function
   'bool (void *p)' that casts P to a pointer to the shared shadow
   RECORD_TYPE and directly evaluates whether it satisfies REQUIRED --
   no argument-struct packing/unpacking at all, unlike build_predicate_
   thunk_function's own general-purpose thunk: there is nothing to defer
   or unpack here, since the whole comparison is just reading P's own
   fields against bounds that are already compile-time constants (REQUIRED
   itself, extracted once from CONTRACT's own condition by the caller).
   Built once per CONTRACT, then reused at every call site that needs to
   check it -- each such call site only needs to supply its own shadow's
   address as P.  */
static GTY(()) hash_map<tree, tree> *symbolic_scalar_thunk_cache;

static tree
get_or_build_scalar_precondition_thunk (tree contract, tree callee,
					 oa_range_fact &required)
{
  if (tree *cached = hash_map_safe_get (symbolic_scalar_thunk_cache, contract))
    return *cached;

  location_t loc = EXPR_LOCATION (contract);
  tree shadow_type = oa_symbolic_shadow_type ();
  tree shadow_ptr_type = build_pointer_type (shadow_type);
  tree void_ptr_type = build_pointer_type (void_type_node);
  tree arg_types = tree_cons (NULL_TREE, void_ptr_type, void_list_node);
  tree fn_type = build_function_type (boolean_type_node, arg_types);

  tree name = clone_function_name_numbered (callee, "symthunk");
  tree fn = build_lang_decl_loc (loc, FUNCTION_DECL, name, fn_type);
  DECL_CONTEXT (fn) = NULL_TREE;
  DECL_SOURCE_LOCATION (fn) = loc;
  SET_DECL_ASSEMBLER_NAME (fn, name);

  tree parm = build_lang_decl (PARM_DECL, get_identifier ("p"), void_ptr_type);
  DECL_CONTEXT (parm) = fn;
  DECL_ARGUMENTS (fn) = parm;
  DECL_RESULT (fn) = NULL_TREE; /* Let start_preparsed_function fill it in.  */

  TREE_STATIC (fn) = 1;
  TREE_USED (fn) = 1;
  DECL_ARTIFICIAL (fn) = 1;
  TREE_PUBLIC (fn) = 0;
  DECL_EXTERNAL (fn) = 0;
  DECL_INTERFACE_KNOWN (fn) = 1;
  suppress_warning (fn);

  tree cast = build1 (NOP_EXPR, shadow_ptr_type, parm);
  tree deref = build_simple_mem_ref (cast);

  auto field_val = [&] (unsigned idx)
    {
      tree field = oa_shadow_field (shadow_type, idx);
      return build3 (COMPONENT_REF, TREE_TYPE (field), deref, field, NULL_TREE);
    };

  /* IS_VALID && (!has_lo-required || (shadow.has_lo && shadow.lo >=
     need_lo)) && (!has_hi-required || (shadow.has_hi && shadow.hi <=
     need_hi)) -- both LO and HI are inclusive on both sides (see
     oa_symbolic_shadow_type's own comment for why no conversion is
     needed here).  */
  tree cond = field_val (0);
  if (required.has_lo)
    {
      tree need_lo = wide_int_to_tree (long_long_integer_type_node, required.lo);
      tree ok = build2 (TRUTH_ANDIF_EXPR, boolean_type_node, field_val (1),
			 build2 (GE_EXPR, boolean_type_node, field_val (2), need_lo));
      cond = build2 (TRUTH_ANDIF_EXPR, boolean_type_node, cond, ok);
    }
  if (required.has_hi)
    {
      tree need_hi = wide_int_to_tree (long_long_integer_type_node, required.hi);
      tree ok = build2 (TRUTH_ANDIF_EXPR, boolean_type_node, field_val (3),
			 build2 (LE_EXPR, boolean_type_node, field_val (4), need_hi));
      cond = build2 (TRUTH_ANDIF_EXPR, boolean_type_node, cond, ok);
    }

  push_to_top_level ();
  start_preparsed_function (fn, NULL_TREE, SF_PRE_PARSED | SF_DEFAULT);
  cp_function_chain->can_throw = true;
  tree body = begin_function_body ();
  finish_return_stmt (cond);
  finish_function_body (body);
  fn = finish_function (/*inline_p=*/false);
  expand_or_defer_fn (fn);
  pop_from_top_level ();

  hash_map_maybe_create<hm_ggc> (symbolic_scalar_thunk_cache);
  symbolic_scalar_thunk_cache->put (contract, fn);
  return fn;
}

/* -fcontract-symbolic-runtime-checks (Mechanism B): build a self-
   contained BIND_EXPR dispatching CONTRACT's own control object CTRL/OP
   with THUNK_FN/ARGS_PTR -- mirrors build_contract_control_constexpr_
   check's own construction technique exactly (direct tree manipulation:
   a real CONSTRUCTOR, DECL_INITIAL, a manually-chained BIND_EXPR_VARS
   list, a hand-built stmt_list), *not* build_contract_control_call's
   runtime-path technique (which depends on an active push_stmt_list/
   add_decl_expr parsing context that does not exist here: this runs
   from oa_walk_stmt's own pre-genericize walk, not from genericization
   proper the way build_contract_check's own dispatch does). ARGS_PTR
   is the caller's own shadow variable's address; THUNK_FN is get_or_
   build_scalar_precondition_thunk's result -- unlike the constexpr
   path's own dummy args_ptr (nothing for its thunk to read), this one
   is real and meaningfully read by THUNK_FN.  Uses CURRENT_FUNCTION_
   DECL (the caller, since this always runs while oa_walk_stmt is
   walking the caller's own body) for both CONTRACT_SIDE_OF -- giving
   exactly the caller/client-side semantics wanted here -- and the
   assertion_context's own source-location field.  */

static tree
oa_build_symbolic_scalar_check_bind (tree contract, tree ctrl, tree op,
				      tree thunk_fn, tree args_ptr)
{
  location_t loc = EXPR_LOCATION (contract);
  contract_check_side side = contract_side_of (contract, current_function_decl);
  tree t_ctx = TREE_VALUE (FUNCTION_FIRST_USER_PARMTYPE (op));
  tree ctx_type = non_reference (t_ctx);

  tree comment = contract_control_omits_comment (ctrl, side)
    ? NULL_TREE : CONTRACT_COMMENT (contract);
  if (!comment)
    comment = build_string_literal ("");

  tree check_fn = build_addr_func (thunk_fn, tf_warning_or_error);
  mark_used (thunk_fn);

  tree f0 = next_aggregate_field (TYPE_FIELDS (ctx_type));
  tree f1 = next_aggregate_field (DECL_CHAIN (f0));
  tree f2 = next_aggregate_field (DECL_CHAIN (f1));
  tree f3 = next_aggregate_field (DECL_CHAIN (f2));
  tree f4 = next_aggregate_field (DECL_CHAIN (f3));
  tree f5 = next_aggregate_field (DECL_CHAIN (f4));
  tree f6 = next_aggregate_field (DECL_CHAIN (f5));
  tree ctor = build_constructor_va
    (ctx_type, 7,
     f0, comment,
     f1, (contract_control_omits_source_location (ctrl, side)
	  ? build_constructor (TREE_TYPE (f1), NULL)
	  : build_real_source_location_value
	      (loc, TREE_TYPE (f1),
	       resolve_fndecl_for_diagnostic_name (current_function_decl))),
     f2, build_int_cst (TREE_TYPE (f2), contract_evaluation_semantic_value ()),
     f3, build_int_cst (TREE_TYPE (f3), get_contract_assertion_kind (contract)),
     f4, build_assertion_static_info_value (side, TREE_TYPE (f4)),
     f5, fold_convert (TREE_TYPE (f5), args_ptr),
     f6, fold_convert (TREE_TYPE (f6), check_fn));

  tree ctx_var = build_decl (loc, VAR_DECL, NULL_TREE, ctx_type);
  DECL_ARTIFICIAL (ctx_var) = true;
  DECL_INITIAL (ctx_var) = ctor;
  layout_decl (ctx_var, 0);

  tree ctrl_type = TREE_TYPE (ctrl);
  tree ctrl_init = cxx_constant_value (ctrl);

  tree ctrl_var = build_decl (loc, VAR_DECL, NULL_TREE, ctrl_type);
  DECL_ARTIFICIAL (ctrl_var) = true;
  DECL_INITIAL (ctrl_var) = ctrl_init;
  layout_decl (ctrl_var, 0);

  tree this_arg = build_fold_addr_expr (ctrl_var);
  tree this_type = TREE_TYPE (DECL_ARGUMENTS (op));
  this_arg = fold_convert (this_type, this_arg);

  tree fn_addr = build_addr_func (op, tf_warning_or_error);
  tree fntype = TREE_TYPE (TREE_TYPE (fn_addr));
  tree result_type = TREE_TYPE (fntype);

  tree ctx_arg = fold_convert (t_ctx, build_fold_addr_expr (ctx_var));

  tree args[2] = { this_arg, ctx_arg };
  mark_used (op);
  tree call = build_call_array_loc (loc, result_type, fn_addr, 2, args);

  DECL_CHAIN (ctrl_var) = ctx_var;
  DECL_CHAIN (ctx_var) = NULL_TREE;

  tree bind = build3 (BIND_EXPR, void_type_node, ctrl_var, NULL_TREE, NULL_TREE);
  tree stmt_list = alloc_stmt_list ();
  append_to_statement_list_force (build_stmt (loc, DECL_EXPR, ctrl_var), &stmt_list);
  append_to_statement_list_force (build_stmt (loc, DECL_EXPR, ctx_var), &stmt_list);
  append_to_statement_list_force (call, &stmt_list);
  BIND_EXPR_BODY (bind) = stmt_list;

  return bind;
}

/* -fcontract-symbolic-runtime-checks (Mechanism B): the consult side's
   own per-call-site obligation check, wired into oa_scan_calls_in_expr
   alongside the existing symbolic/conveyor handlers -- for CALL's
   callee, for *every* symbolic-active precondition/parameter match
   oa_precondition_symbolic_ranges finds (a callee may have more than one
   symbolic-active precondition, and any one of them may itself compare
   more than one of its own parameters -- each gets its own independent
   check here now, rather than only ever the first), positionally
   substitute to find the *actual argument expression* at this call site
   (the same DECL_ARGUMENTS-to-CALL_EXPR_ARG pattern used throughout
   this file), and append a fully self-contained dispatch
   (oa_build_symbolic_scalar_check_bind) to *EXTRA for each, force-
   appended exactly like every other Mechanism B codegen this walk
   injects.  */

static void
oa_handle_call_symbolic_scalar_obligation (tree call, oa_env &env, tree *extra)
{
  if (!oa_symbolic_codegen_active)
    return;
  tree callee = cp_get_callee_fndecl_nofold (call);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return;

  auto_vec<oa_symbolic_precondition_match> matches;
  oa_precondition_symbolic_ranges (callee, &matches);

  for (unsigned m = 0; m < matches.length (); ++m)
    {
      tree contract = matches[m].contract;
      tree param = matches[m].param;
      oa_range_fact required = matches[m].range;

      tree substituted = oa_substitute_call_arg (callee, call, param);
      if (!substituted)
	continue;
      /* Full lookthrough, same reasoning as this function's static-
	 consult sibling (oa_handle_call_symbolic_scalar_precondition_
	 obligation) just above -- ARG_DECL is only ever used below as a
	 lookup key into ENV's own shadow map, never spliced into
	 generated code as an expression, so resolving it further can
	 only let more legitimate shadows be found, never change what
	 code gets emitted.  */
      tree arg_decl = oa_strip_conversion_call (STRIP_ANY_LOCATION_WRAPPER (substituted));
      if (!VAR_P (arg_decl) && TREE_CODE (arg_decl) != PARM_DECL)
	continue;

      tree ctrl = CONTRACT_CONTROL_OBJECT (contract);
      tree control_op = contract_control_operator (ctrl);
      if (!control_op)
	continue;
      tree thunk_fn = get_or_build_scalar_precondition_thunk (contract, callee,
							       required);

      tree shadow = env.shadow_get (arg_decl);
      tree args_ptr;
      tree dummy_wrap = NULL_TREE;
      location_t loc = EXPR_LOCATION (call);
      if (shadow)
	args_ptr = fold_convert (ptr_type_node, build_fold_addr_expr (shadow));
      else
	{
	  /* No shadow at all for this argument -- correctly "never
	     established," not "nothing to check": dispatch anyway, using a
	     throwaway, zero-initialized (IS_VALID false) temporary in place
	     of a real shadow, so the control object still gets invoked and
	     correctly sees the check fail -- matching Mechanism A's own "no
	     record means the check fails" semantics (a value that was never
	     proven safe must not silently pass just because this walk never
	     saw it established anywhere reachable from here).  Declared in
	     its own small BIND_EXPR (DUMMY_WRAP), wrapping the real check
	     dispatch as its body, so it only needs to stay in scope for
	     exactly as long as that dispatch's own use of its address does.  */
	  tree type = oa_symbolic_shadow_type ();
	  tree dummy = build_decl (loc, VAR_DECL, NULL_TREE, type);
	  DECL_ARTIFICIAL (dummy) = 1;
	  DECL_IGNORED_P (dummy) = 1;
	  DECL_CONTEXT (dummy) = current_function_decl;
	  DECL_INITIAL (dummy) = build_constructor (type, NULL);
	  layout_decl (dummy, 0);

	  dummy_wrap = build3 (BIND_EXPR, void_type_node, dummy, NULL_TREE, NULL_TREE);
	  tree stmt_list = alloc_stmt_list ();
	  append_to_statement_list_force (build_stmt (loc, DECL_EXPR, dummy),
					   &stmt_list);
	  BIND_EXPR_BODY (dummy_wrap) = stmt_list;
	  args_ptr = fold_convert (ptr_type_node, build_fold_addr_expr (dummy));
	}

      tree bind = oa_build_symbolic_scalar_check_bind (contract, ctrl, control_op,
							thunk_fn, args_ptr);
      if (dummy_wrap)
	{
	  append_to_statement_list_force (bind, &BIND_EXPR_BODY (dummy_wrap));
	  bind = dummy_wrap;
	}
      append_to_statement_list_force (bind, extra);
    }
}

/* -fcontract-conveyor-proof-provenance: EXPR's own derivation, if one
   can be built -- a decl simply propagates forward whatever derivation
   it already has (an ordinary copy, 'int s = r;', doesn't lose
   provenance); a call defers to oa_call_postcondition_range_p above.
   Anything else (array offsets, IILE recursion, ...) returns NULL --
   out of scope for this increment (see the plan's own "deliberately out
   of scope" section) -- the certificate renderer's own fallback to a
   flat bare-premise assertion handles that safely.  Always NULL when
   provenance tracking is inactive.  */

static oa_derivation *
oa_get_range_derivation (tree expr, oa_env &env)
{
  if (!oa_active_provenance)
    return NULL;

  expr = STRIP_ANY_LOCATION_WRAPPER (expr);
  if (VAR_P (expr) || TREE_CODE (expr) == PARM_DECL)
    return env.deriv_get (expr);

  if (TREE_CODE (expr) == CALL_EXPR)
    {
      oa_range_fact fact;
      oa_derivation *deriv = NULL;
      oa_call_postcondition_range_p (expr, env, &fact, &deriv);
      return deriv;
    }

  return NULL;
}

/* A standalone plugin's own call-site fact-tracing (see
   .claude/plans/stateless-jumping-shore.md and oa_walk_function_calls
   further below) needs to observe every call site oa_scan_calls_in_expr
   already finds, with the environment as it stands at that exact point
   -- set/cleared by oa_walk_function_calls around a single function's
   walk, the same save/restore discipline as OA_RETURN_TRACKING uses
   elsewhere in this file.  NULL (the default) means "no plugin callback
   active," so resolve_object_address_in_function's own, unrelated
   callers are completely unaffected.  */

static void (*oa_call_site_callback) (tree, tree, oa_env *, void *);
static void *oa_call_site_callback_data;

/* D4324 (see .claude/plans/lazy-stirring-pearl.md, Part 4): a callee-
   must-satisfy-its-own-precondition check (oa_handle_call_conveyor_
   proof_obligation and its symbolic/plain siblings, all reached from
   oa_scan_calls_in_expr's own shared per-call dispatch just below) can
   run before a template callee's own BODY -- and therefore its own
   contract specifiers' *substitution* (tsubst_contract_specifiers,
   only ever run as part of regenerate_decl_from_template during body
   instantiation, pt.cc) -- has happened: only the callee's signature is
   needed to resolve the call itself, so GCC's ordinary deferred-
   instantiation model can (and, found via direct testing with a
   template member function's own pre<>(), reliably does) leave the
   callee's own get_fn_contract_specifiers still pointing at the
   *pattern*'s raw, unsubstituted, still-template-dependent condition
   tree at the exact point this pass reads it -- every shape recognizer
   below silently fails to match a dependent-typed condition, so the
   whole obligation is (silently, wrongly) treated as if the callee had
   no precondition at all, rather than "cannot verify."  Mirrors maybe_
   instantiate_conveyor's own, narrower fix for the exact same "callee-
   must-be-conveyor can run before a template body is instantiated"
   problem (build_over_call's own check, pt.cc's own comment on that
   function) -- this is the analogue for *reading a callee's contract
   condition* rather than *reading its conveyor bit*, needed at every
   one of this pass's own call-site dispatch points instead of just
   build_over_call's single one, hence living here (the single, shared
   per-call entry point) rather than duplicated at each handler.  A no-
   op for anything that isn't an as-yet-body-uninstantiated template
   specialization (instantiate_decl's own DECL_TEMPLATE_INSTANTIATED
   check, mirrored here first to avoid DECL_TI_TEMPLATE on a decl with
   no template info at all, makes a redundant call harmless otherwise).  */

static void
oa_maybe_instantiate_contracts (tree fn)
{
  if (fn == NULL_TREE || fn == error_mark_node || TREE_CODE (fn) != FUNCTION_DECL)
    return;
  if (DECL_CLONED_FUNCTION_P (fn))
    fn = DECL_CLONED_FUNCTION (fn);
  if (!DECL_TEMPLATE_INFO (fn) || DECL_TEMPLATE_INSTANTIATED (fn))
    return;
  instantiate_decl (fn, /*defer_ok=*/false, /*expl_inst_class_mem_p=*/false);
}

/* If EXPR (or any subexpression) calls a function whose own precondition
   is conveyor- or symbolic-active, mark FNDECL (via DECL_MIGHT_NEED_OA_
   SCAN_P) as possibly needing the oa_* walk -- one of the touch points
   feeding that bit (see its own comment in cp-tree.h for the full list),
   this one specifically for a call reached only via an already-resolved
   expression being spliced somewhere new rather than through ordinary
   call resolution (build_cxx_call's own touch point, maybe_contract_
   wrap_call): a default argument's own resolved expression reused at
   an omitting call site (convert_default_arg, call.cc), and a member's
   own resolved NSDMI reused at a constructor that needs it (get_nsdmi,
   init.cc). Both splice an already-fully-resolved tree into a new
   context without re-running build_cxx_call for it, so this is the
   only place either FNDECL ever gets a chance to be marked for such a
   call. oa_maybe_instantiate_contracts first, exactly like oa_scan_
   calls_in_expr/oa_function_needs_walk_p, so a template callee's own
   contracts are never mis-read as absent.  */

void
oa_mark_fn_if_expr_calls_active_contract (tree fndecl, tree expr)
{
  if (fndecl == NULL_TREE || fndecl == error_mark_node || expr == NULL_TREE
      || expr == error_mark_node)
    return;
  cp_walk_tree (&expr, [](tree *tp, int *, void *data) -> tree
    {
      tree t = *tp;
      if (t == NULL_TREE || t == error_mark_node || TREE_CODE (t) != CALL_EXPR)
	return NULL_TREE;
      tree callee = cp_get_callee_fndecl_nofold (t);
      if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
	return NULL_TREE;
      oa_maybe_instantiate_contracts (callee);
      for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
	{
	  tree contract = CONTRACT_STATEMENT (as);
	  if (PRECONDITION_P (contract)
	      && (oa_contract_conveyor_active_p (contract, callee)
		  || oa_contract_symbolic_active_p (contract, callee)))
	    {
	      SET_DECL_MIGHT_NEED_OA_SCAN_P ((tree) data);
	      return t;
	    }
	}
      return NULL_TREE;
    }, fndecl, NULL);
}

/* Scan *EXPR (an arbitrary expression, not necessarily a full
   statement -- e.g. a RETURN_EXPR's value or an INIT_EXPR/MODIFY_EXPR's
   RHS) for every CALL_EXPR it contains, including nested calls within
   argument expressions, and discharge each one's call-site precondition
   obligation against ENV.  A bare is_object_address(...) call found
   here is deliberately skipped -- it isn't an ordinary call needing
   this treatment, and any illegitimate use of it reaching this point is
   still separately caught by oa_walk_stmt's own default-fallback
   stray-use scan.  */

/* EXTRA, when non-NULL, is -fcontract-symbolic-runtime-checks
   (Mechanism B)'s own consult-side accumulator: any call found here
   whose callee has a symbolic-active precondition on a bare parameter
   (oa_handle_call_symbolic_scalar_obligation) gets its runtime check
   appended there, for the caller to splice into its own statement the
   same way every other Mechanism B codegen this walk injects already
   is. Defaults to NULL (skip that check entirely) for every call site
   that doesn't have a natural "splice a new statement in right here"
   position to offer -- a real, narrow limitation (see oa_handle_call_
   symbolic_scalar_obligation's own comment), not a silent one.

   INVALIDATE_EXTRA, when non-NULL, is Mechanism B's own invalidation
   accumulator: any call found here (including one nested arbitrarily
   deep inside another, e.g. the inner 'modify(&y)' in
   'foo(modify(&y), 5)' -- this cp_walk_tree already reaches every
   CALL_EXPR regardless of nesting depth, unlike a shallow check of the
   expression's own top-level shape) whose own arguments take the
   address of an already-shadowed bare scalar gets that shadow
   invalidated there (oa_invalidate_scalar_shadow_for_call_args).
   Defaults to NULL for call sites with no splice point to offer, same
   as EXTRA above.  */

static void
oa_scan_calls_in_expr (tree *expr, oa_env &env, tree *extra = NULL,
			tree *invalidate_extra = NULL)
{
  struct oa_scan_calls_data { oa_env *env; tree *extra; tree *invalidate_extra; };
  oa_scan_calls_data data = { &env, extra, invalidate_extra };
  cp_walk_tree (expr, [](tree *tp, int *, void *data_) -> tree
    {
      oa_scan_calls_data *d = (oa_scan_calls_data *) data_;
      oa_env *e = d->env;
      tree t = *tp;
      if (t == NULL_TREE || t == error_mark_node || TREE_CODE (t) != CALL_EXPR)
	return NULL_TREE;
      tree arg;
      if (is_object_address_call_p (t, &arg))
	return NULL_TREE;
      oa_maybe_instantiate_contracts (cp_get_callee_fndecl_nofold (t));
      oa_handle_call_precondition_obligation (t, *e);
      /* analyzed_conveyor/proven_conveyor (resp. symbolic) on any one of
	 the callee's own preconditions forces this call's own obligation
	 discharge on regardless of the command-line flag -- see those
	 traits' own comment in <contracts>.  */
      bool conveyor_forced = false;
      oa_call_conveyor_obligation_status (t, &conveyor_forced, NULL);
      if (flag_contract_conveyor_proofs || conveyor_forced)
	oa_handle_call_conveyor_proof_obligation (t, *e);
      bool symbolic_forced = false;
      oa_call_symbolic_obligation_status (t, &symbolic_forced, NULL);
      if (flag_contract_symbolic_proofs || symbolic_forced)
	{
	  oa_handle_call_symbolic_precondition_obligation (t, *e);
	  oa_handle_call_symbolic_scalar_precondition_obligation (t, *e);
	}
      /* A plugin's own consult runs here, at the same relative position
	 as the built-in diagnostics just above -- *before* this call's
	 own arguments get invalidated/re-established below.  A method
	 call's own receiver (e.g. 'f.write_data()', implicitly passing
	 '&f') is itself one of this call's own arguments, so consulting
	 after invalidation would see the fact this very call is about to
	 wipe (and possibly re-establish from its own postcondition)
	 before the plugin ever got to read it -- exactly the ordering
	 the built-in's own consult calls above already avoid by running
	 first.  */
      if (oa_call_site_callback)
	{
	  tree callee = cp_get_callee_fndecl_nofold (t);
	  if (callee && TREE_CODE (callee) == FUNCTION_DECL)
	    oa_call_site_callback (t, callee, e, oa_call_site_callback_data);
	}
      /* Bookkeeping (establish/invalidate) for the shared predicate/
	 field-range substrate runs whenever *either* built-in feature or
	 a plugin driving oa_walk_function_calls might consult it --
	 unlike the diagnostics above, which stay specific to each flag's
	 own feature (see oa_contract_fact_tracking_active_p's own
	 comment).  */
      if (flag_contract_symbolic_proofs || flag_contract_conveyor_proofs
	  || oa_call_site_callback || conveyor_forced || symbolic_forced)
	{
	  oa_invalidate_symbolic_facts_for_call_args (t, *e);
	  oa_invalidate_symbolic_scalar_range_for_call_args (t, *e);
	  oa_handle_call_symbolic_postcondition_establishment (t, *e);
	}
      if (d->extra)
	oa_handle_call_symbolic_scalar_obligation (t, *e, d->extra);
      if (d->invalidate_extra && oa_symbolic_codegen_active)
	oa_invalidate_scalar_shadow_for_call_args (t, *e, d->invalidate_extra);
      return NULL_TREE;
    }, &data, NULL);
}

/* Scan *EXPR for a stray std::is_object_address(...) call -- one
   reached somewhere this pass doesn't recognize as a legitimate
   contract construct's own condition (ASSERTION_STMT/PRECONDITION_STMT/
   POSTCONDITION_STMT), which is always an error: it has no definition
   and could never be evaluated at runtime, so it can never be
   legally present anywhere else.  Used both by oa_walk_stmt's own
   default fallback (over an entire unhandled node) and, narrower, by
   the IF_STMT/COND_EXPR condition-operand handling (over just the
   condition, not the whole if-statement).  */

static void
oa_scan_stray_is_object_address (tree *expr)
{
  tree found = cp_walk_tree (expr, [](tree *tp, int *, void *) -> tree
    {
      tree arg;
      if (is_object_address_call_p (*tp, &arg))
	return *tp;
      return NULL_TREE;
    }, NULL, NULL);
  if (found)
    error_at (EXPR_LOCATION (found), "%<std::is_object_address%> may "
	      "only be used directly inside a conveyor- or symbolic-checked "
	      "%<contract_assert%>, %<pre%>, or %<post%> condition");
}

/* Scan *EXPR for a stray call to a function declared 'symbolic'
   (axiom contracts, see ~/gcc-axiom-contracts.md) -- one reached
   somewhere this pass doesn't recognize as a legitimate contract
   construct's own condition, mirroring oa_scan_stray_is_object_address
   above exactly: a symbolic function has no definition (enforced at
   declaration time, decl.cc's grokfndecl) and could never be evaluated
   at runtime, so calling it anywhere but directly inside a contract
   condition is always an error, not merely an unresolved external at
   link time.

   This only ever inspects CALL_EXPRs actually present in the pre-
   genericize *executable* body/condition tree oa_walk_stmt walks --
   an unevaluated operand (decltype, a requires-expression's own
   requirement, sizeof, noexcept) is fully resolved away during parsing/
   constraint-checking and leaves no residual CALL_EXPR in that tree at
   all, so `decltype (is_opened (p))` or `requires { is_opened (p); }`
   never reaches this scan and is correctly never flagged -- exactly the
   same reason std::is_object_address itself already coexists with
   unevaluated uses.  */

static void
oa_scan_stray_symbolic_call (tree *expr)
{
  tree found = cp_walk_tree (expr, [](tree *tp, int *, void *) -> tree
    {
      tree t = *tp;
      if (t == NULL_TREE || t == error_mark_node || TREE_CODE (t) != CALL_EXPR)
	return NULL_TREE;
      tree callee = cp_get_callee_fndecl_nofold (t);
      if (callee && TREE_CODE (callee) == FUNCTION_DECL
	  && DECL_DECLARED_SYMBOLIC_P (callee))
	return t;
      return NULL_TREE;
    }, NULL, NULL);
  if (found)
    {
      tree callee = cp_get_callee_fndecl_nofold (found);
      error_at (EXPR_LOCATION (found), "%qD, declared %<symbolic%>, may "
		"only be used directly inside a %<contract_assert%>, "
		"%<pre%>, or %<post%> condition", callee);
    }
}

/* Shared substrate self-trust: both oa_handle_precondition_stmt and
   oa_handle_assertion_stmt call this once their own condition has
   already resolved is_object_address and is being trusted as
   unconditionally true -- the shared-substrate analogue of the is_
   object_address/nonzero/range self-trust those two functions already
   do for the classic three maps (m_map/m_nz_map/m_range_map), extended
   to the three newer maps (m_predicate_fact_map/m_contract_scalar_
   range_map/m_contract_field_range_map) that today are only ever
   established from a *callee's* postcondition at a call site
   (oa_handle_call_symbolic_postcondition_establishment). A function's
   own pre<ctrl>(...)/contract_assert<ctrl>(...) is just as good a
   source of these facts for its own later code as a callee's
   postcondition already is for its caller -- e.g. 'pre<ctrl>(is_opened
   (this))' should let the rest of this same function's body use
   is_opened(this) as an established fact, exactly like a *caller* of
   this function already can after this precondition is proven.

   Deliberately gated on TRACKING_OK (oa_contract_fact_tracking_
   active_p: conveyor- or symbolic-active) rather than the classic
   facts' own CONVEYOR_OK: this is general contract-obligation-fact
   tracking, not UB-freedom, the same distinction that already keeps
   item 8's own div/mod/array-bound scanning conveyor-scoped while
   -fcontract-conveyor-proofs's own range-subsumption consult is not
   (see .claude/plans/well-we-last-discussed-ethereal-duckling.md and
   the "Conveyor UB-freedom scope" project note) -- so a symbolic-only
   precondition/assert gets this shared-substrate self-trust even
   though it gets none of the *classic* m_map/m_nz_map self-trust just
   below (those stay conveyor-only forever; a symbolic-only precondition/
   assert's own is_object_address/nonzero self-trust instead goes to the
   newer, symbolic-only m_symbolic_object_address_map/m_symbolic_nz_map,
   in oa_handle_precondition_stmt/oa_handle_assertion_stmt's own
   SYMBOLIC_OK branch, right alongside this call).

   Only ever matches a conjunct against the function's own PARM_DECLs/
   'this' (via oa_predicate_conjunct_shape's own VAR_P||PARM_DECL scope
   and oa_object_identity_decl's own this/&decl/bare-decl resolution),
   the same "trust the condition's own literal text, no interprocedural
   reasoning" discipline the classic-maps blocks already use.

   CONVEYOR_OK records whether *this specific* contract is conveyor-
   active, independent of TRACKING_OK -- a symbolic-only precondition/
   assert still establishes these facts (TRACKING_OK), but they get
   tagged as not conveyor_established, so a conveyor consumer later
   refuses to rely on them (see oa_predicate_fact's own comment).  */

static void
oa_establish_shared_substrate_self_trust (tree cond, oa_env &env,
					   bool tracking_ok, bool conveyor_ok)
{
  if (!tracking_ok)
    return;

  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts (&cond, &conjuncts);

  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree pred_fn, arg_decl;
      bool negated;
      if (!oa_predicate_conjunct_shape (*conjuncts[i], &pred_fn, &arg_decl,
					&negated))
	continue;
      tree identity;
      if (oa_object_identity_decl (arg_decl, &identity)
	  || oa_field_slot_identity (arg_decl, env, &identity)
	  || oa_array_slot_identity (arg_decl, env, &identity))
	env.predicate_fact_set (env.alias_find (identity), pred_fn, !negated,
				 conveyor_ok);
    }

  /* A relational conjunct against another of the SAME function's own
     parameters (e.g. 'pre<ctrl>(x < q)') -- trust it unconditionally
     for the rest of this function's own body, the same "trust your
     own precondition" principle already applied to every other fact
     shape here. See oa_match_comparison_against_param's own comment
     for why neither PARAM nor OTHER is ever resolved to a value.  */
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree param, other;
      tree_code code;
      if (oa_match_comparison_against_param (*conjuncts[i], &param, &code,
					      &other))
	env.relational_set (param, code, other, conveyor_ok, oa_range_fact_exact (0));
    }

  /* The call analogue of the relational block just above (e.g.
     'pre<ctrl>(i < v.size ())') -- trust it unconditionally for the
     rest of this function's own body, same principle.  */
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree param, rhs_receiver, rhs_callee;
      tree_code code;
      if (oa_match_comparison_against_call (*conjuncts[i], &param, &code,
					     &rhs_receiver, &rhs_callee,
					     /*allow_symbolic_accessor=*/
					       !conveyor_ok))
	env.call_relational_set (param, code, rhs_receiver, rhs_callee,
				  conveyor_ok, oa_range_fact_exact (0));
    }

  /* The call-vs-call analogue of the two relational blocks above (e.g.
     'pre<ctrl>(v.size () < w.size ())') -- trust it unconditionally for
     the rest of this function's own body, same principle. Unlike those
     two (each keyed on a bare decl), this shape's own key is itself a
     call, so LHS_RECEIVER needs the same identity resolution the call-
     range-vs-literal block below applies to its own receiver (it's
     stored in a (receiver identity, callee)-keyed map, not a bare-decl-
     keyed one); the RHS side is stored raw and compared structurally at
     consult time instead, exactly like the call-relational block
     above's own RHS_RECEIVER.  */
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree lhs_receiver, lhs_callee, rhs_receiver, rhs_callee;
      tree_code code;
      if (!oa_match_call_against_call (*conjuncts[i], &lhs_receiver,
					 &lhs_callee, &code, &rhs_receiver,
					 &rhs_callee,
					 /*allow_symbolic_accessor=*/!conveyor_ok))
	continue;
      tree identity;
      if (!oa_object_identity_decl (lhs_receiver, &identity)
	  && !oa_field_slot_identity (lhs_receiver, env, &identity)
	  && !oa_array_slot_identity (lhs_receiver, env, &identity))
	continue;
      identity = env.alias_find (identity);
      env.call_call_relational_set (identity, lhs_callee, code, rhs_receiver,
				      rhs_callee, conveyor_ok);
    }

  auto_vec<tree> params;
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree param, const_val;
      tree_code code;
      if (oa_match_simple_comparison (*conjuncts[i], &param, &code, &const_val)
	  && TREE_CODE (param) == PARM_DECL && !params.contains (param))
	params.safe_push (param);
    }
  if (!params.is_empty ())
    {
      oa_env scratch;
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	oa_refine_single_comparison (*conjuncts[i], scratch,
				     /*asserted_true=*/true);
      for (unsigned i = 0; i < params.length (); ++i)
	{
	  oa_range_fact range;
	  if (scratch.range_get (params[i], &range))
	    env.contract_scalar_range_set (params[i], range);
	}
    }

  auto_vec<oa_symbolic_field_group> field_groups;
  oa_collect_contract_field_ranges (cond, &field_groups);
  for (unsigned i = 0; i < field_groups.length (); ++i)
    {
      tree ptr_expr = oa_strip_symbolic_ptr_expr (field_groups[i].ptr_expr);
      tree identity;
      if (!oa_object_identity_decl (ptr_expr, &identity)
	  && !oa_field_slot_identity (ptr_expr, env, &identity)
	  && !oa_array_slot_identity (ptr_expr, env, &identity))
	continue;
      identity = env.alias_find (identity);
      env.contract_field_range_set (identity, field_groups[i].field,
				     field_groups[i].range, conveyor_ok);
    }

  /* The call-range analogue of the field-range block just above: trust
     a call-range conjunct naming this function's own precondition (e.g.
     'pre<ctrl>(n < this->size ())') for the rest of this function's own
     body, the same "trust your own precondition" principle.  */
  auto_vec<oa_symbolic_call_group> call_groups;
  oa_collect_contract_call_ranges (cond, &call_groups,
				    /*allow_symbolic_accessor=*/!conveyor_ok);
  for (unsigned i = 0; i < call_groups.length (); ++i)
    {
      tree identity;
      if (!oa_object_identity_decl (call_groups[i].receiver_expr, &identity)
	  && !oa_field_slot_identity (call_groups[i].receiver_expr, env,
				      &identity)
	  && !oa_array_slot_identity (call_groups[i].receiver_expr, env,
				      &identity))
	continue;
      identity = env.alias_find (identity);
      env.contract_call_range_set (identity, call_groups[i].callee,
				    call_groups[i].range, conveyor_ok);
    }
}

/* Handle one PRECONDITION_STMT encountered during the body walk: both
   a resolution point (so is_object_address never reaches
   genericization unresolved -- it has no definition and could never
   link) and a fact source, seeding ENV with any top-level &&-conjunct
   that names is_object_address(E) for the rest of the function body.

   CONTRACT here is the actual node embedded in the function's own
   body (added by apply_preconditions/copy_contracts), not the
   pristine one reachable via get_fn_contract_specifiers -- the two are
   separate tree copies (copy_contracts makes a fresh copy for the body
   at maybe_apply_function_contracts time, well before this pass ever
   runs), so resolution must happen on the embedded copy directly, or
   the unresolved is_object_address call would still be sitting in the
   body that genericization/outlining actually sees.

   Unlike contract_assert/postcondition, a precondition's own
   is_object_address is TRUSTED here (oa_resolve_condition's TRUST mode)
   rather than proven against ENV: per the paper's Q4.7 answer, a
   precondition's is_object_address is proven at *each call site*, using
   the caller's own argument expression (item 7 in the plan -- not yet
   implemented) -- the callee has no way to prove anything about its own
   parameter's provenance internally.  Trusting it here, in the
   declaring function's own body, is exactly the same "assume your own
   non-ignored precondition holds" model this branch already uses for
   ordinary boolean preconditions via ignored-and-assumable/IFN_ASSUME;
   item 7 is what will eventually make that trust actually be earned by
   every caller, not a soundness gap introduced by doing this half
   first.

   Also a shared-substrate fact source (oa_establish_shared_substrate_
   self_trust, own comment above): a named-predicate/scalar-range/
   field-range conjunct here is trusted for the rest of this function's
   body too, gated independently on TRACKING_OK rather than CONVEYOR_OK
   -- unlike the is_object_address/nonzero/range facts below, this
   half also fires for a symbolic-only precondition.  */

/* Forward-declared: full definition is much further below, right after
   oa_process_condition (whose own per-conjunct discipline it factors
   out the item-8-relevant slice of), but this function and
   oa_handle_assertion_stmt below both need it here, ahead of that
   definition.  */
static void oa_scan_item8_in_expr (tree *expr, oa_env &env);

/* Forward-declared: mutually recursive with oa_scan_item8_in_expr just
   above (a CALL_EXPR/COND_EXPR conjunct recurses back into it) and with
   oa_process_condition (defined between the two) -- see its own
   definition, right before oa_process_condition, for why this always
   terminates despite the mutual recursion.  */
static void oa_scan_item8_conjunct (tree *conjunct, oa_env &env);

static void
oa_handle_precondition_stmt (tree contract, oa_env &env)
{
  bool conveyor_ok = oa_contract_conveyor_active_p (contract);
  bool symbolic_ok = oa_contract_symbolic_active_p (contract);
  bool tracking_ok = oa_contract_fact_tracking_active_p (contract);
  tree cond = CONTRACT_CONDITION (contract);
  if (cond == NULL_TREE || cond == error_mark_node)
    return;

  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts (&cond, &conjuncts);

  /* D4324/P2680, Increment V: the narrow item-8 dataflow checks (div/mod
     nonzero-divisor, fixed-size-array-bound, overflow) are conveyor-scoped
     the same way the point-of-construction checks are, but unlike those,
     they previously only ever fired inside a function actually declared
     'conveyor' -- never for an is_conveyor contract condition living
     inside an otherwise-ordinary function.  oa_scan_item8_in_expr gives
     this the same left-to-right, per-'&&'-conjunct refinement discipline
     oa_process_condition's own "Increment K" already gives ordinary
     if/loop conditions -- see its own comment for why a flat, unrefined
     scan of every conjunct (what used to be here) missed real cases like
     'pre<conveyor_assert_v>(x < 100000 && x++ < 2048)'.  */
  if (conveyor_ok)
    oa_scan_item8_in_expr (&cond, env);

  auto_vec<tree> facts;
  auto_vec<tree> nz_facts;
  if (conveyor_ok || symbolic_ok)
    for (unsigned i = 0; i < conjuncts.length (); ++i)
      {
	tree arg;
	if (is_object_address_call_p (*conjuncts[i], &arg))
	  {
	    /* A 'T* const' parameter's own read arrives wrapped in a
	       NOP_EXPR (dropping the top-level const so it reads as a
	       plain 'T*' rvalue) -- confirmed via direct testing that,
	       left unstripped, the self-trust loop below's VAR_P/
	       PARM_DECL check silently never fires for it, so
	       is_object_address(t) established nothing at all for the
	       ordinary, common 'T* const' parameter style, exactly
	       mirroring the analogous VIEW_CONVERT_EXPR-stripping fix
	       oa_provable_p/oa_get_range already needed for the same
	       reason.  */
	    arg = STRIP_ANY_LOCATION_WRAPPER (arg);
	    while (TREE_CODE (arg) == NON_LVALUE_EXPR || TREE_CODE (arg) == NOP_EXPR
		   || TREE_CODE (arg) == CONVERT_EXPR
		   || TREE_CODE (arg) == VIEW_CONVERT_EXPR)
	      arg = TREE_OPERAND (arg, 0);
	    facts.safe_push (arg);
	  }
	else if (oa_nonzero_conjunct_p (*conjuncts[i], &arg))
	  nz_facts.safe_push (arg);
      }

  if (!oa_resolve_condition (&cond, env, conveyor_ok, symbolic_ok, /*trust=*/true))
    {
      CONTRACT_CONDITION (contract) = error_mark_node;
      return;
    }
  CONTRACT_CONDITION (contract) = cond;

  oa_establish_shared_substrate_self_trust (cond, env, tracking_ok, conveyor_ok);

  /* CONVEYOR_OK writes into env's own m_map/m_nz_map, SYMBOLIC_OK into
     the symbolic-only maps -- independently, since a control object
     could in principle be both (see oa_predicate_fact's own comment on
     why the two axes are never entangled behind one shared knob).  A
     symbolic-only precondition's own is_object_address/nonzero fact
     must never reach m_map/m_nz_map itself: those feed conveyor's own,
     stronger obligations, and a symbolic fact is only ever trusted, not
     verified (see .claude/plans/well-we-last-discussed-ethereal-
     duckling.md).  */
  if (conveyor_ok)
    {
      for (unsigned i = 0; i < facts.length (); ++i)
	{
	  tree e = facts[i];
	  if (VAR_P (e) || TREE_CODE (e) == PARM_DECL)
	    env.set (e, true);
	}
      for (unsigned i = 0; i < nz_facts.length (); ++i)
	env.nz_set (nz_facts[i], true);
      /* Increment E4: a comparison-shaped conjunct ('i < N', etc.) is
	 trusted the same way an is_object_address(E)/E != 0 conjunct
	 already is above -- reusing oa_refine_single_comparison directly,
	 since "trusted true" for a precondition conjunct is exactly the
	 same thing as a then-branch refinement.  Silently does nothing
	 for any conjunct shape it doesn't recognize (already covered
	 above, or neither), so it's safe to call unconditionally over
	 every conjunct.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	oa_refine_single_comparison (*conjuncts[i], env, /*asserted_true=*/true);
    }
  if (symbolic_ok)
    {
      for (unsigned i = 0; i < facts.length (); ++i)
	{
	  tree e = facts[i];
	  if (VAR_P (e) || TREE_CODE (e) == PARM_DECL)
	    env.symbolic_object_address_set (e, true);
	}
      for (unsigned i = 0; i < nz_facts.length (); ++i)
	env.symbolic_nz_set (nz_facts[i], true);
      /* A plain scalar-range conjunct ('k <= 0') carries no provenance
	 tag at all (unlike is_object_address/nonzero-ness just above, or
	 the ptr->field/call-range/relational/call-relational shapes
	 oa_establish_shared_substrate_self_trust already handled for both
	 flavors) -- it's trusted uniformly once established, so a
	 symbolic-only precondition's own conjunct seeds ENV's real range
	 map exactly like a conveyor one does above.  Found via direct
	 testing: without this, a symbolic-only precondition's own 'k <=
	 0' was invisible to oa_get_range/oa_get_call_relational's own
	 arithmetic-shift fallback for a *later* statement in the same
	 body ('int j = i + k;'), even though the exact same shape works
	 for a conveyor-active precondition.  oa_refine_scalar_range_only
	 is oa_refine_single_comparison's own last-resort fallback,
	 factored out for reuse here -- see its own comment for why only
	 this one piece, not the whole function, is safe to reuse.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	oa_refine_scalar_range_only (*conjuncts[i], env, /*asserted_true=*/true);
    }
}

/* Forward-declared: the statement walker recurses into itself, and into
   the postcondition handler defined below it (which needs the return-
   tracking globals the walker itself accumulates).  */
static void oa_walk_stmt (tree *stmt, oa_env &env);
static void oa_handle_postcondition_stmt (tree contract, oa_env &env);

/* Forward-declared: full definition is much further below (needs
   oa_match_result_relation/oa_underlying_param_operand, defined near
   oa_match_comparison_against_param), but oa_walk_stmt's own DECL_EXPR/
   MODIFY_EXPR handling needs it here, at an assignment whose RHS is a
   call.  */
static void oa_establish_relational_from_call (tree lhs, tree rhs, oa_env &env);

/* Forward-declared: Increment N -- oa_handle_loop (defined next)
   reuses this for its own condition, defined much later in the file
   (right before oa_stmt_terminates_p), the same way IF_STMT/COND_EXPR/
   SWITCH_STMT already do -- see oa_handle_loop's own comment on why.  */
static void oa_process_condition (tree cond, oa_env &env,
				   oa_env *then_env_out, oa_env *else_env_out);

/* Accumulates, across every RETURN_EXPR the walk encounters, whether
   the *returned value itself* is provably an object address on *every*
   return path -- since a function's postcondition is a single,
   physically shared condition checked identically regardless of which
   return statement was taken, is_object_address(<its named result>)
   can only be trusted using whatever holds on *all* return paths (the
   same "every incoming value must satisfy it" merge rule as if/else and
   loops, just merged across every exit point instead of two branches).
   A single accumulated bool suffices here (rather than a full oa_env,
   as if/else and loops need) because the postcondition's named result
   identifier is a synthetic binding to "whatever was returned," not an
   ordinary local variable this pass otherwise tracks -- there is
   nothing else to look up. -1 (not yet reset) / 0 (tracking, currently
   false) / 1 (tracking, currently true); tracking-vs-not is
   distinguished by OA_RETURN_TRACKING below, which also doubles as
   "does a function with an active postcondition need this at all."
   Set/cleared by resolve_object_address_in_function around a single
   function's walk (and, likewise, saved/restored around a nested
   oa_resolve_iile_call walk below); not re-entrant beyond that explicit
   save/restore discipline.  */

static bool oa_return_tracking;
static bool oa_return_all_provable;
static bool oa_return_seen;

/* Increment E4: the exact same accumulator shape, for a range fact
   instead of is_object_address-provability -- merged by *union* of
   intervals across every return path (the range-fact lattice's own
   merge rule, same as everywhere else it's used) rather than AND of
   booleans.  Used by oa_resolve_iile_range below, the range-fact
   counterpart of oa_resolve_iile_call.  */

static bool oa_return_range_tracking;
static bool oa_return_range_has_fact;
static bool oa_return_range_seen;
static oa_range_fact oa_return_range_fact;

/* Resolve CALL (already confirmed by oa_iile_call_p) by walking the
   invoked closure's own operator() body: provable only if the returned
   value is provable on *every* return path (the same merge discipline
   oa_handle_postcondition_stmt already uses for a function's own named
   return value), reusing the very same OA_RETURN_TRACKING/
   OA_RETURN_ALL_PROVABLE/OA_RETURN_SEEN globals for exactly that
   purpose -- saved and restored around this nested walk, since it must
   not disturb the *enclosing* walk's own in-progress return tracking
   (relevant if the IILE itself appears inside ENV's own function's
   return-value expression, e.g. 'return is_object_address([&]{...}());').
   ENV (the enclosing/caller's env at the point of the call) is recorded
   in OA_IILE_OUTER_ENV for the duration, consulted by oa_provable_p
   whenever the closure body reads a by-reference capture-proxy.  */

static bool
oa_resolve_iile_call (tree call, oa_env &env)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  tree body = DECL_SAVED_TREE (callee);
  if (body == NULL_TREE || body == error_mark_node)
    return false;

  oa_env *saved_outer_env = oa_iile_outer_env;
  bool saved_tracking = oa_return_tracking;
  bool saved_all_provable = oa_return_all_provable;
  bool saved_seen = oa_return_seen;
  /* Suppress the sibling range-fact accumulator (Increment E4) during
     this walk -- it must not be conflated with the boolean one being
     computed here, in case this call happens to be nested inside an
     already-in-progress oa_resolve_iile_range (a pathological,
     currently out-of-scope case, but the save/restore costs nothing
     and keeps the two mechanisms cleanly independent regardless).  */
  bool saved_range_tracking = oa_return_range_tracking;
  oa_return_range_tracking = false;

  oa_iile_outer_env = &env;
  oa_return_tracking = true;
  oa_return_all_provable = false;
  oa_return_seen = false;

  oa_env inner_env;
  /* Stage 5: propagate the caller's own already-valid cache -- a fresh,
     default-constructed inner_env's own cache pointer is null, a real
     crash/UB path for '&h->f' first resolved inside this closure's own
     body (see oa_env::field_object_identity_key's own comment).  */
  inner_env.set_field_object_key_cache (env.field_object_key_cache ());
  oa_walk_stmt (&body, inner_env);

  bool result = oa_return_seen && oa_return_all_provable;

  oa_iile_outer_env = saved_outer_env;
  oa_return_tracking = saved_tracking;
  oa_return_all_provable = saved_all_provable;
  oa_return_seen = saved_seen;
  oa_return_range_tracking = saved_range_tracking;

  return result;
}

/* Increment E4: the range-fact counterpart of oa_resolve_iile_call
   above -- same shape entirely, just accumulating OA_RETURN_RANGE_*
   (union of intervals) instead of the boolean OA_RETURN_*.  Writes the
   merged fact to *OUT and returns true only if every return path
   concluded with an actual, mutually-compatible range fact.  */

static bool
oa_resolve_iile_range (tree call, oa_env &env, oa_range_fact *out)
{
  tree callee = cp_get_callee_fndecl_nofold (call);
  tree body = DECL_SAVED_TREE (callee);
  if (body == NULL_TREE || body == error_mark_node)
    return false;

  oa_env *saved_outer_env = oa_iile_outer_env;
  bool saved_range_tracking = oa_return_range_tracking;
  bool saved_range_has_fact = oa_return_range_has_fact;
  bool saved_range_seen = oa_return_range_seen;
  oa_range_fact saved_range_fact = oa_return_range_fact;
  /* Suppress the sibling boolean accumulator during this walk, for the
     same reason oa_resolve_iile_call suppresses this one.  */
  bool saved_tracking = oa_return_tracking;
  oa_return_tracking = false;

  oa_iile_outer_env = &env;
  oa_return_range_tracking = true;
  oa_return_range_has_fact = false;
  oa_return_range_seen = false;

  oa_env inner_env;
  /* Stage 5: see oa_resolve_iile_call's own identical propagation.  */
  inner_env.set_field_object_key_cache (env.field_object_key_cache ());
  oa_walk_stmt (&body, inner_env);

  bool result = oa_return_range_seen && oa_return_range_has_fact;
  if (result)
    *out = oa_return_range_fact;

  oa_iile_outer_env = saved_outer_env;
  oa_return_range_tracking = saved_range_tracking;
  oa_return_range_has_fact = saved_range_has_fact;
  oa_return_range_seen = saved_range_seen;
  oa_return_range_fact = saved_range_fact;
  oa_return_tracking = saved_tracking;

  return result;
}

/* Handle one ASSERTION_STMT (contract_assert) encountered during the
   walk: resolve its condition using ENV (erroring if it names
   is_object_address outside a conveyor/non-ignored control object, or
   if unprovable), then -- if it's conveyor and provable -- fold any
   top-level &&-conjunct that was exactly is_object_address(E) into ENV
   as an established fact for the rest of the function (the
   contract_assert-as-fact-source escape hatch for the loop/IILE cases
   a later increment will add).

   Also a shared-substrate fact source (oa_establish_shared_substrate_
   self_trust, own comment above oa_handle_precondition_stmt): gated
   independently on TRACKING_OK, so a symbolic-only contract_assert
   establishes a named-predicate/scalar-range/field-range fact for
   later code too, even though it gets none of the is_object_address/
   nonzero/range treatment below.  */

static void
oa_handle_assertion_stmt (tree stmt, oa_env &env)
{
  bool conveyor_ok = oa_contract_conveyor_active_p (stmt);
  bool symbolic_ok = oa_contract_symbolic_active_p (stmt);
  bool tracking_ok = oa_contract_fact_tracking_active_p (stmt);
  tree cond = CONTRACT_CONDITION (stmt);
  if (cond == NULL_TREE || cond == error_mark_node)
    return;

  /* Find top-level &&-conjuncts that are exactly is_object_address(E),
     capturing E *before* resolution replaces the call with
     boolean_true_node below -- these are what get folded into ENV as
     established facts for later code (the contract_assert-as-fact-
     source mechanism), once we know the whole condition resolved
     successfully.  */
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts (&cond, &conjuncts);

  /* D4324/P2680, Increment V: see the identical comment in
     oa_handle_precondition_stmt -- same narrow item-8 dataflow checks,
     now given the same left-to-right, per-conjunct refinement via
     oa_scan_item8_in_expr.  */
  if (conveyor_ok)
    oa_scan_item8_in_expr (&cond, env);

  auto_vec<tree> facts;
  auto_vec<tree> nz_facts;
  if (conveyor_ok || symbolic_ok)
    for (unsigned i = 0; i < conjuncts.length (); ++i)
      {
	tree arg;
	if (is_object_address_call_p (*conjuncts[i], &arg))
	  {
	    /* See oa_handle_precondition_stmt's own identical fix and
	       comment: a 'T* const' parameter's read needs the same
	       NOP_EXPR/CONVERT_EXPR/VIEW_CONVERT_EXPR stripping before
	       the self-trust loop below's VAR_P/PARM_DECL check, or the
	       fact silently never gets established.  */
	    arg = STRIP_ANY_LOCATION_WRAPPER (arg);
	    while (TREE_CODE (arg) == NON_LVALUE_EXPR || TREE_CODE (arg) == NOP_EXPR
		   || TREE_CODE (arg) == CONVERT_EXPR
		   || TREE_CODE (arg) == VIEW_CONVERT_EXPR)
	      arg = TREE_OPERAND (arg, 0);
	    facts.safe_push (arg);
	  }
	else if (oa_nonzero_conjunct_p (*conjuncts[i], &arg))
	  nz_facts.safe_push (arg);
      }

  /* D4324: unlike every other fact-establishing step in this function,
     this one *checks* first -- a contract_assert's own condition is
     the one place in the whole conveyor/symbolic proof pass that both
     consumes ambient facts (like a precondition) and, once checked,
     re-establishes itself as a fact for subsequent code (like a
     postcondition) -- see oa_check_assertion_conjunct_against_env's
     own comment for why this is achievable here (no callee/call site,
     so no substitution needed) where the equivalent fix for an actual
     postcondition is not (a postcondition is checked once, at a single
     shared exit point downstream of every return, not per-program-point
     during the walk -- out of scope here).

     never_proven exempts this contract_assert from this whole step,
     unconditionally, regardless of any flag or analyzed_conveyor/
     proven_conveyor -- ANY_CONJUNCT_PROVEN_FALSE simply never gets set,
     so the fact collection/establishment above and below proceeds
     exactly as it would for any other conveyor/symbolic contract_assert
     (see std::never_proven_conveyor in <contracts>: this still
     establishes itself as a trusted fact, it just never gets checked or
     diagnosed itself).

     Otherwise, gated on the proofs flags *or* analyzed_conveyor/
     proven_conveyor forcing analysis on regardless of the flag: an
     ordinary compile with neither flag (and no forcing trait) sees no
     behavior change at all. Matches the call-obligation family's own
     three-way outcome (contracts.cc's own relational/scalar-range
     call-obligation checks, e.g. ~line 10950): a provable conjunct is
     silently discharged, a provably false one is always a hard error,
     and a merely-unprovable (OA_UNKNOWN) one is a warning by default --
     unless this specific contract is proven_conveyor/proven_symbolic
     (strict), in which case unproven is *also* a hard error, matching
     WG14 P4021R2's compile_assert() outcome table exactly.  */
  bool any_conjunct_proven_false = false;
  if (!contract_control_never_proven (CONTRACT_CONTROL_OBJECT (stmt),
				       contract_side_of (stmt, current_function_decl)))
    {
      bool conveyor_analysis = conveyor_ok
	&& (flag_contract_conveyor_proofs
	    || oa_contract_conveyor_analysis_forced_p (stmt));
      bool conveyor_strict = oa_contract_conveyor_strict_p (stmt);
      if (conveyor_analysis)
	for (unsigned i = 0; i < conjuncts.length (); ++i)
	  switch (oa_check_assertion_conjunct_against_env (*conjuncts[i], env,
							     /*require_conveyor=*/true))
	    {
	    case OA_PROVEN_TRUE:
	      break;
	    case OA_PROVEN_FALSE:
	      error_at (EXPR_LOCATION (*conjuncts[i]),
			"%<contract_assert%> condition %qE is provably false",
			*conjuncts[i]);
	      any_conjunct_proven_false = true;
	      break;
	    case OA_UNKNOWN:
	      if (conveyor_strict)
		{
		  error_at (EXPR_LOCATION (*conjuncts[i]),
			    "cannot prove %<contract_assert%> condition %qE",
			    *conjuncts[i]);
		  any_conjunct_proven_false = true;
		}
	      else
		warning_at (EXPR_LOCATION (*conjuncts[i]), 0,
			    "cannot verify %<contract_assert%> condition %qE",
			    *conjuncts[i]);
	      break;
	    }

      bool symbolic_analysis = symbolic_ok
	&& (flag_contract_symbolic_proofs
	    || oa_contract_symbolic_analysis_forced_p (stmt));
      bool symbolic_strict = oa_contract_symbolic_strict_p (stmt);
      if (symbolic_analysis)
	for (unsigned i = 0; i < conjuncts.length (); ++i)
	  switch (oa_check_assertion_conjunct_against_env (*conjuncts[i], env,
							     /*require_conveyor=*/false))
	    {
	    case OA_PROVEN_TRUE:
	      break;
	    case OA_PROVEN_FALSE:
	      error_at (EXPR_LOCATION (*conjuncts[i]),
			"%<contract_assert%> condition %qE is provably false",
			*conjuncts[i]);
	      any_conjunct_proven_false = true;
	      break;
	    case OA_UNKNOWN:
	      if (symbolic_strict)
		{
		  error_at (EXPR_LOCATION (*conjuncts[i]),
			    "cannot prove %<contract_assert%> condition %qE",
			    *conjuncts[i]);
		  any_conjunct_proven_false = true;
		}
	      else
		warning_at (EXPR_LOCATION (*conjuncts[i]), 0,
			    "cannot verify %<contract_assert%> condition %qE",
			    *conjuncts[i]);
	      break;
	    }
    }

  if (!oa_resolve_condition (&cond, env, conveyor_ok, symbolic_ok))
    {
      CONTRACT_CONDITION (stmt) = error_mark_node;
      return;
    }
  CONTRACT_CONDITION (stmt) = cond;

  /* A conjunct just proven false above must not also be established as
     a trusted fact below -- doing so would inject a known-contradictory
     "fact" into ENV for the rest of the function, silently corrupting
     later analysis (e.g. suppressing a genuinely separate, later
     violation's own diagnostic) on top of the error already reported.
     The whole condition is false regardless of which conjunct did it
     (it's a conjunction), so nothing from it should be trusted onward.  */
  if (any_conjunct_proven_false)
    return;

  oa_establish_shared_substrate_self_trust (cond, env, tracking_ok, conveyor_ok);

  /* See the identical CONVEYOR_OK/SYMBOLIC_OK split in oa_handle_
     precondition_stmt above, and its own comment on why the two axes
     stay independent rather than sharing one knob.  */
  if (conveyor_ok)
    {
      for (unsigned i = 0; i < facts.length (); ++i)
	{
	  tree e = facts[i];
	  if (VAR_P (e) || TREE_CODE (e) == PARM_DECL)
	    env.set (e, true);
	}
      for (unsigned i = 0; i < nz_facts.length (); ++i)
	env.nz_set (nz_facts[i], true);
      /* Increment E4: the same comparison-shaped-conjunct fact-seeding
	 as oa_handle_precondition_stmt above, applied here too -- a
	 preceding, conveyor, non-ignored contract_assert's own
	 comparison conjunct establishes a usable range fact for later
	 code, the same escape hatch already used for is_object_address/
	 nonzero-ness.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	oa_refine_single_comparison (*conjuncts[i], env, /*asserted_true=*/true);
    }
  if (symbolic_ok)
    {
      for (unsigned i = 0; i < facts.length (); ++i)
	{
	  tree e = facts[i];
	  if (VAR_P (e) || TREE_CODE (e) == PARM_DECL)
	    env.symbolic_object_address_set (e, true);
	}
      for (unsigned i = 0; i < nz_facts.length (); ++i)
	env.symbolic_nz_set (nz_facts[i], true);
      /* See the identical fix and comment in oa_handle_precondition_stmt
	 above: a plain scalar-range conjunct carries no provenance tag,
	 so it's safe (and, per that comment, necessary) to seed it for a
	 symbolic-only contract_assert too, not just a conveyor one.  */
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	oa_refine_scalar_range_only (*conjuncts[i], env, /*asserted_true=*/true);
    }
}

/* Collect, into PTR_OUT/NZ_OUT (each deduplicated), every pointer-typed
   (respectively integer-typed) VAR_DECL/PARM_DECL that is the target of
   a plain INIT_EXPR/MODIFY_EXPR assignment, *or* the operand of a
   PRE/POSTINCREMENT_EXPR/PRE/POSTDECREMENT_EXPR, anywhere within *STMT
   -- a plain syntactic scan, independent of provability, used only to
   determine which decls the loop-header merge rule (item 4, and its
   div/mod-nonzero-fact counterpart, Increment E-divmod) needs to
   consider at all. A decl freshly declared *inside* the loop body (via
   DECL_EXPR) is deliberately not specially recognized here: such a decl
   doesn't persist across iterations, so it needs no pre-loop/post-loop
   merge -- any INIT_EXPR that happens to represent its own initial
   declaration-with-initializer looks identical to an ordinary
   assignment to this syntactic scan, but including it in an OUT vec is
   harmless (at worst a dead, never-looked-up entry ends up in the
   enclosing env after the loop, since the decl itself is out of scope
   there).

   The increment/decrement case was a genuine, pre-existing gap (found
   via direct testing while adding item 8's overflow scan, but not
   specific to it -- the div/mod and array-bounds scans were equally
   affected): a `for (int i = 0; i < n; ++i)` loop's own counter, almost
   always written this way rather than as `i = i + 1`, was never added
   to either OUT vec at all, so the loop-header merge rule below never
   ran for it -- its own pre-loop fact (e.g. the `[0, 0]` range from its
   own initializer) simply persisted, stale and unwidened, through the
   diagnostic-producing pass that scans the loop's *entire* repeated
   part, wrongly treating `i` as if it could never be anything other
   than its initial value anywhere in the loop body/condition/increment-
   expression.  */

struct oa_loop_target_data { vec<tree> *ptr_out; vec<tree> *nz_out; };

static void
oa_collect_loop_targets (tree *stmt, vec<tree> *ptr_out, vec<tree> *nz_out)
{
  oa_loop_target_data data = { ptr_out, nz_out };
  cp_walk_tree (stmt, [](tree *tp, int *, void *data_) -> tree
    {
      oa_loop_target_data *d = (oa_loop_target_data *) data_;
      tree t = *tp;
      if (t == NULL_TREE || t == error_mark_node)
	return NULL_TREE;
      tree target;
      switch (TREE_CODE (t))
	{
	case INIT_EXPR:
	case MODIFY_EXPR:
	  target = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (t, 0));
	  break;
	case PREINCREMENT_EXPR:
	case POSTINCREMENT_EXPR:
	case PREDECREMENT_EXPR:
	case POSTDECREMENT_EXPR:
	  target = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (t, 0));
	  break;
	default:
	  return NULL_TREE;
	}
      if (!(VAR_P (target) || TREE_CODE (target) == PARM_DECL))
	return NULL_TREE;
      if (POINTER_TYPE_P (TREE_TYPE (target)) && !d->ptr_out->contains (target))
	d->ptr_out->safe_push (target);
      else if (INTEGRAL_TYPE_P (TREE_TYPE (target))
	       && !d->nz_out->contains (target))
	d->nz_out->safe_push (target);
      return NULL_TREE;
    }, &data, NULL);
}

/* Handle one loop (FOR_STMT/WHILE_STMT/DO_STMT), implementing the
   loop-header merge rule (item 4): PARTS is the sequence of tree slots
   that execute on *every* iteration, in execution order (condition-prep,
   condition, body, increment-expression -- whichever apply; a loop's
   one-time init-statement, e.g. FOR_INIT_STMT, is walked by the caller
   *before* calling this, with ordinary straight-line semantics, since it
   only ever runs once).

   Two passes over the combined repeated part:

   1. A single "diagnostic" pass, using a plain copy of the incoming ENV
      (no artificial invalidation) -- this is the one pass that performs
      real, honest evaluation: it's what actually resolves/diagnoses any
      is_object_address use or contract_assert reached inside the loop
      (a nested ASSERTION_STMT is handled exactly like anywhere else),
      and the only pass allowed to affect the OA_RETURN_TRACKING globals
      if the loop body contains a return statement. Resolving a
      contract's condition mutates it in place to a literal
      boolean_true_node/error_mark_node, so any *later* re-walk of the
      same shared subtree (pass 2 below) is naturally idempotent on it --
      no duplicate diagnostics.

   2. For each pointer decl the loop's repeated part ever reassigns
      (oa_collect_loop_targets, a plain syntactic scan): re-walk the
      *same* repeated part once more, in a fresh copy of the pre-loop ENV
      with that one decl pre-invalidated -- this is exactly what enforces
      "every reassignment's RHS must be provable without referring back
      to the decl's own prior value" (the plan's item 4 restriction)
      without needing genuine fixpoint iteration: if the decl's own
      (invalidated) value is what a reassignment's RHS depends on, that
      RHS correctly fails to resolve as provable; if a *different*,
      provable reassignment already ran earlier in the very same pass
      (ordinary straight-line/if-else sequencing within one iteration,
      not a dependency on a *previous* iteration), that's legitimate and
      is correctly picked up. The decl is provable after the loop only if
      this pass concludes it provable *and* it was already provable
      before the loop even started (covering zero-iteration execution).
      OA_RETURN_TRACKING is suppressed during these synthetic re-walks
      (they use a deliberately perturbed, hypothetical environment, not
      real program-order semantics, and must not corrupt real
      return-value tracking established by pass 1).

   All outcomes from pass 2 are collected and only applied to the real
   ENV after every reassigned decl has been independently checked (each
   check must start fresh from the real pre-loop ENV, not from another
   decl's already-updated result).  */

static void
oa_handle_loop (tree *cond_prep, tree *cond, tree *body, tree *expr,
		oa_env &env)
{
  auto_vec<tree *> parts;
  if (cond_prep && *cond_prep) parts.safe_push (cond_prep);
  if (cond && *cond) parts.safe_push (cond);
  if (body && *body) parts.safe_push (body);
  if (expr && *expr) parts.safe_push (expr);

  /* Increment E3: the loop's own condition refines the env used for
     body+expr -- entering the body at all means the condition held.
     Applied inside walk_parts itself, so every invocation (the
     diagnostic pass below, and every per-decl invalidated re-walk
     further down) sees it uniformly. Mirrors the same then-branch-only
     refinement IF_STMT/COND_EXPR/SWITCH_STMT already apply, just
     without a parallel else-branch -- a loop body is only ever entered
     when the condition is true, there is no "else" here at all.

     Increment N: the condition part is handled via oa_process_condition
     (not a plain oa_walk_stmt call) -- previously calling oa_walk_stmt
     directly on the condition meant a call/div-mod/array-ref reached
     as a *sub-expression* (not the condition's own top-level code, the
     only shape oa_walk_stmt's own CALL_EXPR case would catch) got no
     item-7/item-8 scanning at all, the exact same "condition gets no
     scanning" gap already closed for IF_STMT/COND_EXPR (item 7) and
     SWITCH_STMT (Increment M). oa_process_condition never dispatches
     the condition through oa_walk_stmt's own switch internally either,
     for the same double-scan reason those two never did -- so
     replacing the call here (rather than calling both) avoids
     reintroducing that bug. ELSE_ENV is computed but discarded (a
     loop's condition has no "else" side to use it for) -- a harmless,
     compile-time-only inefficiency, not worth a separate then-only
     variant of oa_process_condition just to avoid it.  */
  auto walk_parts = [&] (oa_env &e)
    {
      for (unsigned i = 0; i < parts.length (); ++i)
	{
	  if (cond && parts[i] == cond)
	    {
	      oa_env then_env, else_env;
	      oa_process_condition (*cond, e, &then_env, &else_env);
	      e.assign (then_env);
	    }
	  else
	    oa_walk_stmt (parts[i], e);
	}
    };

  oa_env scratch = env.copy ();
  walk_parts (scratch);
  /* -fcontract-symbolic-runtime-checks (Mechanism B): SCRATCH is where
     any shadow the loop body itself establishes actually gets created
     (get_or_build_scalar_shadow registers it via whichever env
     reference reaches it, which throughout this one, real pass is
     SCRATCH, not ENV) -- merge its existence back into ENV now, or
     code after the loop could never find a shadow the loop body
     itself just created.  A plain union, same as every other
     shadow_decls_merge_with call site.  */
  env.shadow_decls_merge_with (scratch);

  auto_vec<tree> reassigned, reassigned_nz;
  for (unsigned i = 0; i < parts.length (); ++i)
    oa_collect_loop_targets (parts[i], &reassigned, &reassigned_nz);

  auto_vec<tree> result_decls;
  auto_vec<bool> result_provable;
  for (unsigned i = 0; i < reassigned.length (); ++i)
    {
      tree d = reassigned[i];
      bool pre_ok = env.provable_p (d);

      oa_env checkenv = env.copy ();
      checkenv.invalidate (d);

      bool saved_tracking = oa_return_tracking;
      oa_return_tracking = false;
      /* -fcontract-symbolic-runtime-checks (Mechanism B): this re-walk
	 exists purely to compute a compile-time fact (does D stay
	 provable independent of its own prior value) -- it never
	 represents any one real execution of the loop, so codegen must
	 not fire here, or the same establish/invalidate statements would
	 be injected into the tree once per reassigned decl.  See
	 oa_symbolic_codegen_active's own comment.  */
      bool saved_symbolic_codegen = oa_symbolic_codegen_active;
      oa_symbolic_codegen_active = false;
      bool saved_diagnostics_active = oa_diagnostics_active;
      oa_diagnostics_active = false;
      walk_parts (checkenv);
      oa_diagnostics_active = saved_diagnostics_active;
      oa_symbolic_codegen_active = saved_symbolic_codegen;
      oa_return_tracking = saved_tracking;

      result_decls.safe_push (d);
      result_provable.safe_push (pre_ok && checkenv.provable_p (d));
    }

  for (unsigned i = 0; i < result_decls.length (); ++i)
    env.set (result_decls[i], result_provable[i]);

  /* Increment E-divmod: the exact same merge rule, applied to the
     "provably nonzero" fact map instead of the "is_object_address"
     one -- a reassigned integer decl is nonzero-provable after the
     loop only if every reassignment is independently nonzero-provable
     without depending on the decl's own prior value, and the pre-loop
     value was already nonzero-provable (covers zero-iteration
     execution).  */
  auto_vec<tree> nz_result_decls;
  auto_vec<bool> nz_result_provable;
  for (unsigned i = 0; i < reassigned_nz.length (); ++i)
    {
      tree d = reassigned_nz[i];
      bool pre_ok = env.nz_provable_p (d);

      oa_env checkenv = env.copy ();
      checkenv.nz_invalidate (d);

      bool saved_tracking = oa_return_tracking;
      oa_return_tracking = false;
      bool saved_symbolic_codegen = oa_symbolic_codegen_active;
      oa_symbolic_codegen_active = false;
      bool saved_diagnostics_active = oa_diagnostics_active;
      oa_diagnostics_active = false;
      walk_parts (checkenv);
      oa_diagnostics_active = saved_diagnostics_active;
      oa_symbolic_codegen_active = saved_symbolic_codegen;
      oa_return_tracking = saved_tracking;

      nz_result_decls.safe_push (d);
      nz_result_provable.safe_push (pre_ok && checkenv.nz_provable_p (d));
    }

  for (unsigned i = 0; i < nz_result_decls.length (); ++i)
    env.nz_set (nz_result_decls[i], nz_result_provable[i]);

  /* Increment E3: the same merge rule again, generalized from the
     boolean AND-merge above to interval *union* -- a decl (pointer or
     integer; both fact kinds share the one range map, so this covers
     both a plain integer's value range and a pointer's array-offset
     range together) reassigned in the loop's repeated part gets a
     post-loop range fact only if: every reassignment is provable
     without depending on the decl's own prior value (checked the same
     way as above, invalidating the decl's *range* fact specifically
     before the re-walk); doing so still concludes with an actual range
     fact (not just "some value," which the invalidated re-walk can't
     rule out); and the pre-loop fact existed with the *same* base.
     The final fact is the union of the pre-loop range and the
     iteration-independent post-reassignment range (covering
     zero-iteration execution the same way the boolean version's "AND
     with pre_ok" does) -- e.g. 'for (...) { p = &candidates[i]; if
     (...) break; }' with a provable pre-loop value, exactly the
     realistic pattern the original plan cited for this rule. Any
     disagreement (a differing base, or either side failing to
     conclude with a fact at all) invalidates the range entirely rather
     than guessing -- note this replaces item 4's original placeholder,
     which only ever invalidated integer decls' ranges
     (REASSIGNED_NZ), never a reassigned *pointer's* array-offset fact
     at all (REASSIGNED) -- a latent gap between when Increment E2
     started populating that side of the fact and now, closed here by
     covering both.  */
  auto_vec<tree> range_targets;
  for (unsigned i = 0; i < reassigned.length (); ++i)
    range_targets.safe_push (reassigned[i]);
  for (unsigned i = 0; i < reassigned_nz.length (); ++i)
    range_targets.safe_push (reassigned_nz[i]);

  auto_vec<tree> range_result_decls;
  auto_vec<bool> range_result_has_fact;
  auto_vec<oa_range_fact> range_result_facts;
  auto_vec<oa_derivation *> range_result_derivs;
  for (unsigned i = 0; i < range_targets.length (); ++i)
    {
      tree d = range_targets[i];
      oa_range_fact pre_fact;
      bool pre_ok = env.range_get (d, &pre_fact);
      /* -fcontract-conveyor-proof-provenance: fetched from ENV before
	 CHECKENV (below) invalidates its own copy; NULL harmlessly when
	 tracking is inactive.  */
      oa_derivation *pre_deriv = env.deriv_get (d);

      oa_env checkenv = env.copy ();
      checkenv.range_invalidate (d);
      /* -fcontract-conveyor-proof-provenance: mirror range_invalidate
	 just above -- without this, a reassignment that doesn't touch D
	 on every path through the loop body (e.g. 'if (cond) d = foo();'
	 with no else) could leave CHECKENV's own derivation for D as a
	 stale carry-over from *before* this invalidated re-walk, exactly
	 the same staleness range_invalidate itself already exists to
	 prevent for the numeric fact.  A no-op when tracking is
	 inactive (the map is always empty).  */
      checkenv.deriv_invalidate (d);
      /* Same staleness concern, for the shared predicate-fact map -- a
	 no-op when tracking is inactive.  */
      checkenv.predicate_fact_invalidate (d);
      /* D's own VALUE (not whatever it aliased) may change across this
	 speculative re-walk, so any "D currently aliases TARGET" entry
	 from before it can no longer be trusted -- TARGET's own fact
	 stays valid (D's own reassignment doesn't touch TARGET's actual
	 memory), only D's own claim to still equal it is stale.  */
      checkenv.alias_invalidate (d);
      /* Same staleness concern, for a relational fact naming D on
	 either side.  */
      checkenv.relational_invalidate_involving (d);
      checkenv.call_relational_invalidate_involving (d);
      checkenv.call_call_relational_invalidate_involving (d);
      /* Same staleness concern, for the two static-only contract range
	 maps -- D may be a tracked bare scalar (m_contract_scalar_range_
	 map's own key) or a tracked pointer whose fields are tracked
	 (m_contract_field_range_map's own identity half); harmless
	 no-ops for whichever one D isn't.  */
      checkenv.contract_scalar_range_invalidate (d);
      checkenv.contract_field_range_invalidate_all (d);
      checkenv.contract_call_range_invalidate_all (d);
      checkenv.field_alias_invalidate_all (d);
      checkenv.array_alias_invalidate_all (d);
      checkenv.field_object_predicate_invalidate_all (d);
      oa_invalidate_parameter_alias_group (d, checkenv);

      bool saved_tracking = oa_return_tracking;
      oa_return_tracking = false;
      bool saved_symbolic_codegen = oa_symbolic_codegen_active;
      oa_symbolic_codegen_active = false;
      bool saved_diagnostics_active = oa_diagnostics_active;
      oa_diagnostics_active = false;
      walk_parts (checkenv);
      oa_diagnostics_active = saved_diagnostics_active;
      oa_symbolic_codegen_active = saved_symbolic_codegen;
      oa_return_tracking = saved_tracking;

      oa_range_fact post_fact;
      bool post_ok = checkenv.range_get (d, &post_fact);
      /* -fcontract-conveyor-proof-provenance: the re-walk's own
	 derivation for D, if any -- populated by the exact same
	 assignment/if-join hooks already threaded through oa_walk_stmt,
	 since CHECKENV is walked by the ordinary machinery.  */
      oa_derivation *body_deriv = checkenv.deriv_get (d);

      range_result_decls.safe_push (d);
      if (!pre_ok || !post_ok || pre_fact.base != post_fact.base)
	{
	  range_result_has_fact.safe_push (false);
	  range_result_facts.safe_push (oa_range_fact ());
	  range_result_derivs.safe_push (NULL);
	  continue;
	}
      oa_range_fact merged;
      merged.base = pre_fact.base;
      merged.has_lo = pre_fact.has_lo && post_fact.has_lo;
      merged.has_hi = pre_fact.has_hi && post_fact.has_hi;
      if (merged.has_lo)
	merged.lo = wi::smin (pre_fact.lo, post_fact.lo);
      if (merged.has_hi)
	merged.hi = wi::smax (pre_fact.hi, post_fact.hi);
      range_result_has_fact.safe_push (true);
      range_result_facts.safe_push (merged);
      /* -fcontract-conveyor-proof-provenance: OA_DERIV_LOOP only when
	 both arms actually have their own derivation recorded -- falls
	 back to a flat (no-derivation) rendering of MERGED otherwise,
	 the same fallback discipline used everywhere else in this
	 feature.  */
      if (oa_active_provenance && pre_deriv && body_deriv)
	range_result_derivs.safe_push
	  (oa_active_provenance->make_loop (merged, pre_deriv, body_deriv));
      else
	range_result_derivs.safe_push (NULL);
    }

  for (unsigned i = 0; i < range_result_decls.length (); ++i)
    {
      if (range_result_has_fact[i])
	env.range_set (range_result_decls[i], range_result_facts[i]);
      else
	env.range_invalidate (range_result_decls[i]);
      if (range_result_derivs[i])
	env.deriv_set (range_result_decls[i], range_result_derivs[i]);
      else
	env.deriv_invalidate (range_result_decls[i]);
      /* -fcontract-symbolic-proofs: a decl reassigned anywhere in the
	 loop's repeated part unconditionally loses its symbolic fact --
	 unlike a numeric range, there's no sound "loop invariant" to
	 compute for a symbolic fact (see the plan's own scope notes), so
	 this is simply always invalidated, never re-established here.  */
      env.predicate_fact_invalidate (range_result_decls[i]);
      /* Same "D's own value may have changed" treatment as the
	 speculative re-walk above.  */
      env.alias_invalidate (range_result_decls[i]);
      /* Same treatment for a relational fact naming this decl on
	 either side.  */
      env.relational_invalidate_involving (range_result_decls[i]);
      env.call_relational_invalidate_involving (range_result_decls[i]);
      env.call_call_relational_invalidate_involving (range_result_decls[i]);
      /* Same "always invalidated, never re-established" treatment for
	 the two new static-only symbolic range maps, for the same reason.  */
      env.contract_scalar_range_invalidate (range_result_decls[i]);
      env.contract_field_range_invalidate_all (range_result_decls[i]);
      env.contract_call_range_invalidate_all (range_result_decls[i]);
      env.field_alias_invalidate_all (range_result_decls[i]);
      env.array_alias_invalidate_all (range_result_decls[i]);
      env.field_object_predicate_invalidate_all (range_result_decls[i]);
      oa_invalidate_parameter_alias_group (range_result_decls[i], env);
    }
}

/* D4324/P2680: scan a single conjunct (as collected by oa_collect_
   conjuncts, from either oa_process_condition's own loop below or oa_
   scan_item8_in_expr's) for item 8's div-mod/array-bounds/overflow
   restrictions -- recursing into a CALL_EXPR's own arguments or a
   COND_EXPR's own condition/branches when the conjunct turns out to be
   one of those shapes, rather than just flat-scanning it, so a '&&'
   nested *inside* either ('take (a < 100000 && a++ < 2048)', '(a <
   100000 && a++ < 2048) ? x : y') gets the same left-to-right refinement
   a top-level '&&' already does. This closes the one gap oa_scan_item8_
   in_expr's own original comment disclosed as pre-existing and out of
   scope; recursion always proceeds onto a strictly smaller subtree
   (an argument, or a condition/branch), so it terminates the same way
   any other recursive tree walk over a finite AST does.  */

static void
oa_scan_item8_conjunct (tree *conjunct, oa_env &env)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (*conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR
	 || TREE_CODE (c) == NOP_EXPR
	 || TREE_CODE (c) == CONVERT_EXPR
	 || TREE_CODE (c) == VIEW_CONVERT_EXPR)
    {
      c = TREE_OPERAND (c, 0);
      STRIP_ANY_LOCATION_WRAPPER (c);
    }

  if (c && TREE_CODE (c) == CALL_EXPR)
    {
      /* Each argument's own evaluation is fully sequenced within itself,
	 but the relative order *between* arguments is unspecified -- so
	 every argument gets its own independent scan starting from ENV
	 as it stands *before* this call, never seeing another argument's
	 own refinement. oa_scan_item8_in_expr's own scratch-copy
	 discipline already gives us this for free: it never mutates the
	 ENV reference it's handed, so calling it repeatedly with the same
	 ENV for each argument is exactly the right semantics. Also scans
	 the callee expression itself for completeness/symmetry with the
	 arguments -- in practice this is almost always a bare FUNCTION_DECL
	 reference with nothing to find, since a genuinely *computed* callee
	 (e.g. '(cond ? f : g) (x)') is banned outright in conveyor-
	 restricted code by an entirely separate, pre-existing restriction
	 (see d4324-conveyor-callee-function-pointer-bad.C) and so can never
	 actually reach here.  */
      if (CALL_EXPR_FN (c))
	oa_scan_item8_in_expr (&CALL_EXPR_FN (c), env);
      int nargs = call_expr_nargs (c);
      for (int i = 0; i < nargs; ++i)
	oa_scan_item8_in_expr (&CALL_EXPR_ARG (c, i), env);
      return;
    }

  if (c && TREE_CODE (c) == COND_EXPR)
    {
      /* Mirrors oa_walk_stmt's own COND_EXPR case: the condition is
	 fully evaluated, in fully-specified order, before either branch,
	 so oa_process_condition's own then/else split applies completely
	 unchanged here; each branch is then scanned recursively with its
	 own correctly-refined env. oa_symbolic_codegen_active is
	 suppressed around this call specifically: oa_process_condition's
	 own oa_scan_calls_in_expr can inject real runtime-check codegen
	 under -fcontract-symbolic-runtime-checks, appropriate for a
	 genuine if/loop condition but not for a ternary reached
	 incidentally while scanning some other statement's own
	 expression -- mirrors oa_handle_loop's own save/clear/restore
	 discipline around its speculative re-walks (see oa_symbolic_
	 codegen_active's own comment).  */
      oa_env then_env, else_env;
      bool saved_codegen = oa_symbolic_codegen_active;
      oa_symbolic_codegen_active = false;
      oa_process_condition (TREE_OPERAND (c, 0), env, &then_env, &else_env);
      oa_symbolic_codegen_active = saved_codegen;
      if (TREE_OPERAND (c, 1))
	oa_scan_item8_in_expr (&TREE_OPERAND (c, 1), then_env);
      if (TREE_OPERAND (c, 2))
	oa_scan_item8_in_expr (&TREE_OPERAND (c, 2), else_env);
      return;
    }

  oa_scan_div_mod_in_expr (conjunct, env);
  oa_scan_array_bounds_in_expr (conjunct, env);
  oa_scan_overflow_in_expr (conjunct, env);
}

/* D4324/P2680 item 8, Increment K: process COND (an IF_STMT's or
   COND_EXPR's own condition operand) in true left-to-right,
   short-circuit evaluation order, replacing what used to be two
   separate phases (scan the *whole* condition eagerly, then
   separately refine THEN_ENV/ELSE_ENV from it -- Increment E1's
   oa_refine_range_for_condition, folded into this function entirely)
   with one interleaved pass.

   For each top-level '&&' conjunct (oa_collect_conjuncts, in order):
   scan it for call-site obligations/div-mod/array-bounds/stray-
   is_object_address uses and any top-level assignment
   (oa_track_condition_assignment, now applied once per conjunct
   rather than once to the whole condition -- see its own updated
   comment), using COND_ENV as refined by only the *strictly earlier*
   conjuncts; then fold this conjunct's own comparison into COND_ENV
   (oa_refine_single_comparison, asserted true) before moving on, so
   the *next* conjunct's scan sees it. This matters because reaching a
   later conjunct in a '&&'-chain genuinely does guarantee every
   earlier conjunct already evaluated true -- e.g. 'k >= 0 && k < 5 &&
   (p = &arr[k]) != nullptr' is a sound access (the bounds are
   established before '&arr[k]' ever executes), but was previously
   rejected anyway, since the array-bounds scan ran against the
   *pre*-condition ENV, before any conjunct's refinement had been
   applied at all. The mirror-image ordering ('(p = &arr[k]) !=
   nullptr && k >= 0 && k < 5') is genuinely unsound -- '&arr[k]'
   really does execute before the bounds are checked -- and correctly
   stays rejected: it is scanned using only whatever facts were
   already established *before* this whole condition, since it's the
   first conjunct, with nothing preceding it to refine from.

   *THEN_ENV_OUT is the fully-refined COND_ENV (every conjunct
   evaluated true to reach the then-branch). *ELSE_ENV_OUT starts from
   ENV itself (unrefined by any conjunct) and, only for a single
   non-compound condition, is refined by De Morgan's over its
   top-level '||' disjuncts (Increment J, unchanged, folded in here
   verbatim) -- a compound '&&' condition's else-branch is still never
   refined at all, for the same reason as always (De Morgan's gives a
   disjunction of negations there, not a single conjunction
   representable the same way).

   This is a strict generalization, not a behavior change, for any
   condition with no top-level '&&': oa_collect_conjuncts yields the
   condition itself as the sole "conjunct," scanned and refined exactly
   once, in the same relative order as before.  */

static void
oa_process_condition (tree cond, oa_env &env,
		       oa_env *then_env_out, oa_env *else_env_out)
{
  oa_env cond_env = env.copy ();
  tree cond_copy = cond;
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts (&cond_copy, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      oa_scan_calls_in_expr (conjuncts[i], cond_env);
      if (current_function_decl && DECL_DECLARED_CONVEYOR_P (current_function_decl))
	oa_scan_item8_conjunct (conjuncts[i], cond_env);
      oa_scan_stray_is_object_address (conjuncts[i]);
      oa_scan_stray_symbolic_call (conjuncts[i]);
      oa_track_condition_assignment (*conjuncts[i], cond_env);
      /* A bare 'E != 0'/'0 != E' conjunct seeds the nz-fact map
	 directly, exactly as a precondition/contract_assert conjunct
	 already does (oa_handle_precondition_stmt/oa_handle_assertion_
	 stmt) -- oa_refine_single_comparison alone doesn't handle
	 NE_EXPR at all (not representable as a single interval), so
	 without this a later conjunct's div/mod scan wouldn't see an
	 earlier 'n != 0' conjunct's own fact.  */
      tree nz_decl;
      if (oa_nonzero_conjunct_p (*conjuncts[i], &nz_decl))
	cond_env.nz_set (nz_decl, true);
      oa_refine_single_comparison (*conjuncts[i], cond_env, /*asserted_true=*/true);
    }

  then_env_out->assign (cond_env);
  else_env_out->assign (env);
  if (conjuncts.length () == 1)
    {
      auto_vec<tree *> disjuncts;
      oa_collect_disjuncts (conjuncts[0], &disjuncts);
      for (unsigned i = 0; i < disjuncts.length (); ++i)
	oa_refine_single_comparison (*disjuncts[i], *else_env_out,
				      /*asserted_true=*/false);
    }
}

/* D4324/P2680: the item-8-relevant slice of oa_process_condition's own
   per-conjunct discipline just above, factored out for every OTHER item-8
   call site in this file -- every one of them used to just scan the whole
   of EXPR (or, for precondition/assertion/postcondition, each of its own
   pre-collected top-level '&&' conjuncts) against a single, unrefined env,
   never applying what an *earlier* conjunct in the very same '&&'-chain
   establishes before scanning a *later* one. Found via a real user report
   (https://godbolt.org/z/vjfxK7Psz, https://godbolt.org/z/MWvnjP9bG):
   'pre<conveyor_assert_v>(x < 100000 && x++ < 2048)' flagged 'x++' as
   possibly overflowing even though, by '&&'s own short-circuit semantics,
   reaching the second conjunct at all already proves 'x < 100000' true --
   plainly enough to bound 'x++' -- yet nothing here ever saw that.

   '&&'/'||' short-circuit identically regardless of *why* the expression
   is being evaluated -- a condition, a return value, an initializer, a
   switch's own discriminant, anything -- so this refinement is a property
   of the language's own evaluation order, not something to special-case
   to "looks like a condition" the way this scan's other call sites used
   to. EXPR is scanned, and refined for the *next* conjunct's own scan,
   against a scratch copy -- never the caller's own real, ambient ENV --
   mirroring oa_process_condition's own COND_ENV discipline exactly: this
   function's refinement must never leak into whatever the caller does
   with its real ENV afterward (self-trust/condition-resolution logic for
   precondition/assertion/postcondition, ordinary subsequent statements
   for everything else).

   oa_collect_conjuncts only ever looks at EXPR's own *top level* (after
   stripping CLEANUP_POINT_EXPR/NOP_EXPR/CONVERT_EXPR/VIEW_CONVERT_EXPR,
   and, at a couple of call sites that need it, the hidden-result-
   temporary INIT_EXPR/MODIFY_EXPR a RETURN_EXPR's own value can arrive
   wrapped in) -- a '&&' buried one level *deeper*, inside some other
   expression shape entirely (a call argument, e.g. 'take (a < 100000 &&
   a++ < 2048)'; a ternary's own condition, e.g. '(a < 100000 && a++ <
   2048) ? x : y') is therefore never itself split into further conjuncts.
   oa_scan_item8_conjunct (called per conjunct just below) is what
   actually recurses into a CALL_EXPR/COND_EXPR conjunct's own arguments/
   condition/branches -- see its own comment for the full reasoning and
   for why this is sound despite C++'s own unspecified inter-argument
   evaluation order.  */

static void
oa_scan_item8_in_expr (tree *expr, oa_env &env)
{
  oa_env scan_env = env.copy ();
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts (expr, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      oa_scan_item8_conjunct (conjuncts[i], scan_env);
      tree nz_decl;
      if (oa_nonzero_conjunct_p (*conjuncts[i], &nz_decl))
	scan_env.nz_set (nz_decl, true);
      oa_refine_single_comparison (*conjuncts[i], scan_env, /*asserted_true=*/true);
    }
}

/* D4324/P2680, Increment L: true if COND (a loop's condition --
   WHILE_COND/DO_COND, or NULL_TREE for a FOR_STMT with none, i.e.
   'for (;;)') is a compile-time constant that is always true.
   Deliberately narrow: only a bare nonzero INTEGER_CST (confirmed via
   debug_tree to be exactly what 'while (true)'/'while (1)' already
   reduce to by this pass's timing, after stripping location wrappers/
   CLEANUP_POINT_EXPR) -- no attempt at deeper constant-folding for a
   non-literal-but-provably-true condition.  */

static bool
oa_cond_always_true_p (tree cond)
{
  if (cond == NULL_TREE)
    return true;
  tree c = cond;
  STRIP_ANY_LOCATION_WRAPPER (c);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    {
      c = TREE_OPERAND (c, 0);
      STRIP_ANY_LOCATION_WRAPPER (c);
    }
  return TREE_CODE (c) == INTEGER_CST && !integer_zerop (c);
}

/* D4324/P2680, Increment L: true if BODY (a loop's body) contains a
   BREAK_STMT belonging to *that* loop specifically -- i.e. one not
   itself nested inside a further loop or switch, whose own break
   would belong to it instead. Implemented via a cp_walk_tree scan
   (unlike everything else in this file, which hand-rolls a recursive
   switch) specifically so this is correct for any nesting shape
   without needing to enumerate every leaf statement code that could
   never embed a further statement -- the only thing genuinely special
   here is pruning descent, via the callback's own WALK_SUBTREES
   out-parameter, at a nested FOR_STMT/WHILE_STMT/DO_STMT/SWITCH_STMT's
   own body.  */

static bool
oa_loop_has_own_break_p (tree body)
{
  tree found = cp_walk_tree (&body, [](tree *tp, int *walk_subtrees, void *) -> tree
    {
      tree t = *tp;
      if (t == NULL_TREE || t == error_mark_node)
	return NULL_TREE;
      switch (TREE_CODE (t))
	{
	case BREAK_STMT:
	  return t;
	case FOR_STMT:
	case WHILE_STMT:
	case DO_STMT:
	case SWITCH_STMT:
	  *walk_subtrees = 0;
	  return NULL_TREE;
	default:
	  return NULL_TREE;
	}
    }, NULL, NULL);
  return found != NULL_TREE;
}

/* D4324/P2680: does control ever fall through past the end of STMT?
   Used by oa_walk_stmt's IF_STMT/COND_EXPR cases to decide, after
   walking both branches, whether the code following the if/else is
   only ever reachable via one particular branch (because the other
   unconditionally returns, throws, or hits a noreturn call) -- in
   which case that branch's facts should be used as-is for the merge
   point, rather than blindly ANDed/unioned with the terminating
   branch's (which never actually reaches there). Recognizes:
   RETURN_EXPR; THROW_EXPR; BREAK_STMT/CONTINUE_STMT/GOTO_EXPR (each a
   plain leaf that never falls through to whatever textually follows
   it, regardless of where control ends up); a CALL_EXPR to a function
   GCC already knows is noreturn (call_expr_flags, the same query used
   everywhere else in the compiler for this question -- picks up
   __builtin_trap, __builtin_unreachable, std::unreachable, abort, and
   any other [[noreturn]]-attributed callee uniformly, no name-matching
   needed); a nested IF_STMT/COND_EXPR where *both* arms terminate;
   a SWITCH_STMT that is provably exhaustive and provably has no break
   anywhere in it (SWITCH_STMT_ALL_CASES_P/SWITCH_STMT_NO_BREAK_P,
   directly reusing the exact flags/logic gcc/c-family/c-common.cc's
   own c_block_may_fallthru already computes and relies on for this
   same question -- see Increment L's own plan-file writeup for why
   checking only the body's last element, via the STATEMENT_LIST case
   below, is not merely conservative but exactly correct once NO_
   BREAK_P holds); a WHILE_STMT/FOR_STMT/DO_STMT with a provably-always
   -true condition and no break belonging to it (oa_cond_always_true_p/
   oa_loop_has_own_break_p above); a CLEANUP_STMT (a local variable's
   non-trivial-destructor scope-exit wrapper), matching gcc/cp/
   cp-objcp-common.cc's own cxx_block_may_fallthru exactly (see that
   case's own comment); and, recursively, a STATEMENT_LIST/BIND_EXPR's
   last real statement. Deliberately conservative for anything else
   (default: does NOT terminate, i.e. assume it might fall through) --
   safe, just occasionally missing a case, the discipline used
   throughout this pass.

   Increment O: also exported (declared in contracts.h) and reused
   directly by check_conveyor_function_body (constexpr.cc) for the
   "all exit paths return" restriction -- a non-void conveyor
   function's body never falling through its own end is exactly the
   same property this function already computes for an arbitrary
   sub-statement, just applied to the whole body.  */

bool
oa_stmt_terminates_p (tree stmt)
{
  if (stmt == NULL_TREE || stmt == error_mark_node)
    return false;

  tree t = STRIP_ANY_LOCATION_WRAPPER (stmt);
  while (true)
    {
      if (TREE_CODE (t) == CLEANUP_POINT_EXPR)
	t = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (t, 0));
      else if (TREE_CODE (t) == EXPR_STMT)
	t = STRIP_ANY_LOCATION_WRAPPER (EXPR_STMT_EXPR (t));
      else if (TREE_CODE (t) == BIND_EXPR)
	t = STRIP_ANY_LOCATION_WRAPPER (BIND_EXPR_BODY (t));
      else if (TREE_CODE (t) == MUST_NOT_THROW_EXPR)
	/* A 'noexcept' function's whole body is wrapped in one of these
	   (run the real body; if an exception propagates out of it,
	   std::terminate instead) -- found via a real regression (any
	   conveyor function declared 'noexcept' with a non-void return
	   type, confirmed to affect a plain free function exactly as
	   much as a member function, nothing template- or const-
	   specific about it) once this case was missing here: the
	   wrapper adds no new *normal*-path control flow of its own, so
	   whether the real body returns on every path is unaffected by
	   unwrapping it, the same reasoning as CLEANUP_POINT_EXPR above.  */
	t = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (t, 0));
      else
	break;
    }

  switch (TREE_CODE (t))
    {
    case STATEMENT_LIST:
      {
	tree_stmt_iterator i = tsi_last (t);
	if (tsi_end_p (i))
	  return false;
	return oa_stmt_terminates_p (tsi_stmt (i));
      }

    case RETURN_EXPR:
    case THROW_EXPR:
    case BREAK_STMT:
    case CONTINUE_STMT:
    case GOTO_EXPR:
      return true;

    case CALL_EXPR:
      return (call_expr_flags (t) & ECF_NORETURN) != 0;

    case IF_STMT:
      /* For 'if constexpr' specifically, once instantiated, IF_COND is
	 a resolved, known compile-time constant, and the branch it
	 doesn't select is never copied into the instantiated tree at all
	 (pt.cc's own tsubst_expr IF_STMT case skips substituting it) --
	 so that branch ends up looking like an ordinary empty clause
	 (which would legitimately fail to terminate on its own), even
	 though it can never actually run and so can never threaten
	 falling through regardless of its own, never-executed shape.
	 Found via a real regression: a conveyor function's if-constexpr
	 with a 'return' in *both* source branches was misdiagnosed as
	 never returning, because only one branch's statements survive
	 instantiation and the other reads as empty here. Only the
	 selected branch's own termination status matters in this case;
	 an unresolved (still-dependent, or a plain non-constexpr) 'if'
	 falls through to the ordinary same-arms-required conjunction
	 below, exactly as before.  */
      if (IF_STMT_CONSTEXPR_P (t))
	{
	  tree cond = IF_COND (t);
	  if (cond && TREE_CODE (cond) == INTEGER_CST)
	    return oa_stmt_terminates_p (integer_zerop (cond)
					  ? ELSE_CLAUSE (t) : THEN_CLAUSE (t));
	}
      return (oa_stmt_terminates_p (THEN_CLAUSE (t))
	      && oa_stmt_terminates_p (ELSE_CLAUSE (t)));

    case COND_EXPR:
      return (oa_stmt_terminates_p (TREE_OPERAND (t, 1))
	      && oa_stmt_terminates_p (TREE_OPERAND (t, 2)));

    case SWITCH_STMT:
      return (SWITCH_STMT_ALL_CASES_P (t) && SWITCH_STMT_NO_BREAK_P (t)
	      && oa_stmt_terminates_p (SWITCH_STMT_BODY (t)));

    case CLEANUP_STMT:
      /* A local variable with a non-trivial destructor wraps the rest
	 of its scope in one of these (a try/finally shape: run BODY,
	 then always run the destructor call EXPR at scope exit) --
	 found via a real regression (a pre-existing RAII test) once
	 this case was missing here. Mirrors gcc/cp/cp-objcp-common.cc's
	 own cxx_block_may_fallthru exactly (De Morgan's over its
	 "&&", since that function computes the opposite polarity --
	 "may fall through" -- from this one): if BODY itself never
	 falls through (already returns/throws/etc, regardless of what
	 EXPR does), the whole construct doesn't either -- the returned
	 value already left before EXPR's own fallthrough status could
	 matter. An EH-only cleanup (only runs on the exceptional path,
	 never the normal one) is conservatively treated as "does NOT
	 terminate" -- the same simple, always-safe choice cxx_block_
	 may_fallthru itself makes there (unconditional "may fall
	 through", never trying to reason about BODY specifically for
	 that case).  */
      if (!CLEANUP_EH_ONLY (t))
	return (oa_stmt_terminates_p (CLEANUP_BODY (t))
		|| oa_stmt_terminates_p (CLEANUP_EXPR (t)));
      return false;

    case TRY_FINALLY_EXPR:
      /* The exact same try/finally shape CLEANUP_STMT's own case just
	 above already handles (that case's own comment literally
	 describes it that way), just built via the more primitive,
	 non-C++-specific tree code -- this is precisely how a function
	 with an active postcondition gets its own body wrapped (see
	 grok_contract's own 'build_stmt (loc, TRY_FINALLY_EXPR, fnbody,
	 NULL_TREE)'), which this function's own caller,
	 check_conveyor_function_body (constexpr.cc), needs to see through
	 for its "every exit path returns" check on a non-void conveyor
	 function. Found via direct testing: a conveyor function with any
	 active postcondition and a non-void return type was unconditionally
	 misdiagnosed as never returning, regardless of its own body's
	 actual, real return statements -- this tree shape has no
	 CLEANUP_EH_ONLY-style "only runs on the exceptional path" flag to
	 check the way CLEANUP_STMT does, so both operands are always
	 unconditionally live, same OR-of-both-arms reasoning either way.  */
      return (oa_stmt_terminates_p (TREE_OPERAND (t, 0))
	      || oa_stmt_terminates_p (TREE_OPERAND (t, 1)));

    case WHILE_STMT:
      return (oa_cond_always_true_p (WHILE_COND (t))
	      && !oa_loop_has_own_break_p (WHILE_BODY (t)));

    case FOR_STMT:
      return (oa_cond_always_true_p (FOR_COND (t))
	      && !oa_loop_has_own_break_p (FOR_BODY (t)));

    case DO_STMT:
      return (oa_cond_always_true_p (DO_COND (t))
	      && !oa_loop_has_own_break_p (DO_BODY (t)));

    case TRY_BLOCK:
      /* Mirrors IF_STMT/COND_EXPR's own "every arm must terminate"
	 rule, generalized from two arms to N+1: the whole construct
	 falls through unless *both* TRY_STMTS and *every* handler do
	 not (an exception could be caught by any one handler, so all of
	 them are live "arms" here, not just the one that happens to
	 match at runtime -- the same conservative "could be any of
	 them" treatment oa_walk_stmt's own TRY_BLOCK case gives them).  */
      {
	if (!oa_stmt_terminates_p (TRY_STMTS (t)))
	  return false;
	tree handlers = TRY_HANDLERS (t);
	if (handlers == NULL_TREE)
	  return true;
	if (TREE_CODE (handlers) == STATEMENT_LIST)
	  {
	    for (tree_stmt_iterator i = tsi_start (handlers); !tsi_end_p (i);
		 tsi_next (&i))
	      {
		tree h = tsi_stmt (i);
		if (h && TREE_CODE (h) == HANDLER
		    && !oa_stmt_terminates_p (HANDLER_BODY (h)))
		  return false;
	      }
	    return true;
	  }
	if (TREE_CODE (handlers) == HANDLER)
	  return oa_stmt_terminates_p (HANDLER_BODY (handlers));
	return false;
      }

    default:
      return false;
    }
}

/* The forward statement walker: processes *STMT (an arbitrary
   statement or statement-sequence) in program order, threading ENV
   forward -- an abstract, flow-sensitive simulation of "what is
   provably known about each pointer variable's value here," used to
   resolve is_object_address as the walk reaches each contract_assert.  */

static void
oa_walk_stmt (tree *stmt, oa_env &env)
{
  tree t = *stmt;
  if (t == NULL_TREE || t == error_mark_node)
    return;

  switch (TREE_CODE (t))
    {
    case STATEMENT_LIST:
      for (tree_stmt_iterator i = tsi_start (t); !tsi_end_p (i); tsi_next (&i))
	oa_walk_stmt (tsi_stmt_ptr (i), env);
      return;

    case RETURN_EXPR:
      if (oa_return_tracking || oa_return_range_tracking)
	{
	  /* The return value: TREE_OPERAND (t, 0) is either the plain
	     value expression (void-returning path not relevant here) or
	     an INIT_EXPR/MODIFY_EXPR assigning it to a hidden result
	     temporary -- in the latter case the actual value is the
	     assignment's RHS.  */
	  tree val = TREE_OPERAND (t, 0);
	  if (val && (TREE_CODE (val) == INIT_EXPR || TREE_CODE (val) == MODIFY_EXPR))
	    val = TREE_OPERAND (val, 1);

	  if (oa_return_tracking)
	    {
	      bool this_provable = oa_provable_p (val, env);
	      if (!oa_return_seen)
		{
		  oa_return_all_provable = this_provable;
		  oa_return_seen = true;
		}
	      else
		oa_return_all_provable = oa_return_all_provable && this_provable;
	    }

	  /* Increment E4: the same "every incoming value must satisfy
	     it" merge across every return path, generalized to range
	     facts (union of intervals rather than AND of booleans) --
	     mirrors OA_RETURN_TRACKING/ALL_PROVABLE/SEEN's shape exactly,
	     just for oa_resolve_iile_range below instead of oa_provable_p/
	     oa_resolve_iile_call.  */
	  if (oa_return_range_tracking)
	    {
	      oa_range_fact this_fact;
	      bool this_ok = oa_get_range (val, env, &this_fact);
	      if (!oa_return_range_seen)
		{
		  oa_return_range_has_fact = this_ok;
		  if (this_ok)
		    oa_return_range_fact = this_fact;
		  oa_return_range_seen = true;
		}
	      else if (!oa_return_range_has_fact || !this_ok
		       || oa_return_range_fact.base != this_fact.base)
		oa_return_range_has_fact = false;
	      else
		{
		  oa_return_range_fact.has_lo
		    = oa_return_range_fact.has_lo && this_fact.has_lo;
		  oa_return_range_fact.has_hi
		    = oa_return_range_fact.has_hi && this_fact.has_hi;
		  if (oa_return_range_fact.has_lo)
		    oa_return_range_fact.lo
		      = wi::smin (oa_return_range_fact.lo, this_fact.lo);
		  if (oa_return_range_fact.has_hi)
		    oa_return_range_fact.hi
		      = wi::smax (oa_return_range_fact.hi, this_fact.hi);
		}
	    }
	}
      /* A returned value commonly flows directly from a call (e.g.
	 'return deref(p);') -- discharge any call-site precondition
	 obligation (item 7) for every call reached from here, since such
	 a call never stands alone as its own expression-statement and so
	 would otherwise never reach the CALL_EXPR case below.  */
      oa_scan_calls_in_expr (&TREE_OPERAND (t, 0), env);
      /* Item 8's narrow div/mod, array-bound, and overflow restrictions,
	 only within a function actually declared 'conveyor' -- given the
	 same left-to-right, per-'&&'-conjunct refinement as every other
	 item-8 call site (oa_scan_item8_in_expr's own comment), so
	 'return a < 100000 && a++ < 2048;' is provable the same way the
	 equivalent 'if' condition already was. Same INIT_EXPR/MODIFY_EXPR
	 unwrapping as VAL above (the hidden-result-temporary assignment
	 idiom): without it, oa_collect_conjuncts sees the *assignment* at
	 the top, not the '&&' it wraps, and never splits it into
	 conjuncts at all.  */
      if (current_function_decl && DECL_DECLARED_CONVEYOR_P (current_function_decl))
	{
	  tree *ret_val_slot = &TREE_OPERAND (t, 0);
	  if (*ret_val_slot
	      && (TREE_CODE (*ret_val_slot) == INIT_EXPR
		  || TREE_CODE (*ret_val_slot) == MODIFY_EXPR))
	    ret_val_slot = &TREE_OPERAND (*ret_val_slot, 1);
	  oa_scan_item8_in_expr (ret_val_slot, env);
	}
      /* Still scan the return value expression itself for a stray
	 is_object_address call (e.g. 'return std::is_object_address(p);'
	 directly) -- fall through to the default case's blanket scan.  */
      goto oa_default_scan;

    case TRY_FINALLY_EXPR:
      /* The postcondition machinery's own shape (maybe_apply_function_
	 contracts): operand 0 is the real function body (containing every
	 return), operand 1 is the finally/handler block -- walked here in
	 that same order, so any RETURN_EXPRs in operand 0 have already
	 updated OA_RETURN_TRACKING's accumulators by the time operand 1
	 (which is where the postcondition itself lives, see EH_ELSE_EXPR
	 below) is reached.  */
      oa_walk_stmt (&TREE_OPERAND (t, 0), env);
      oa_walk_stmt (&TREE_OPERAND (t, 1), env);
      return;

    case EH_ELSE_EXPR:
      /* Operand 0 is the non-exceptional handler (where the
	 postcondition check itself lives); operand 1 is the exceptional
	 path (a no-op void expression -- postconditions are skipped if
	 the function exits via an exception).  Walk both for uniformity;
	 there is nothing of interest in operand 1.  */
      oa_walk_stmt (&TREE_OPERAND (t, 0), env);
      oa_walk_stmt (&TREE_OPERAND (t, 1), env);
      return;

    case CLEANUP_STMT:
      /* Emitted whenever a local variable has a non-trivial destructor
	 (i.e. very commonly): CLEANUP_BODY is "the rest of this block"
	 (every statement from here to the end of the enclosing scope,
	 not just the very next one -- the whole remainder is nested
	 inside this one node, not a sibling of it), CLEANUP_EXPR is the
	 destructor call to run when leaving CLEANUP_BODY, CLEANUP_DECL
	 the variable it's for.  Found via direct testing (a temporary
	 per-statement trace of every TREE_CODE this walk visits) that,
	 with no case for this node, execution fell to the default
	 fallback below, which only scans for *stray* is_object_address/
	 symbolic misuse and never recurses into CLEANUP_BODY at all --
	 silently skipping every statement following such a declaration,
	 for every analysis in this file (establish, consult, invalidate
	 alike), not just one fact shape. Walk CLEANUP_BODY first (the
	 code that actually runs), then CLEANUP_EXPR (the destructor call
	 itself -- a bare CALL_EXPR, handled the same as any other
	 expression-statement call by the CALL_EXPR case below, so Rule 2
	 invalidation sees it taking CLEANUP_DECL's own address like any
	 other call would).  */
      oa_walk_stmt (&CLEANUP_BODY (t), env);
      oa_walk_stmt (&CLEANUP_EXPR (t), env);
      return;

    case TRY_BLOCK:
      {
	/* A real 'try { ... } catch (...) { ... }' (user-written, unlike
	   TRY_FINALLY_EXPR's own compiler-generated postcondition shape
	   above): with no case for this node, execution fell to the same
	   default fallback CLEANUP_STMT did, silently skipping the try
	   body, every handler, and anything following the whole
	   construct -- confirmed via direct testing that a call with a
	   provable, unconditional precondition violation inside a bare
	   'try { ...; } catch (...) { ... }' produced no diagnostic at
	   all, the same severity as the CLEANUP_STMT gap.

	   Soundness needs more than just "walk both parts", though: an
	   exception can occur at *any* point inside TRY_STMTS, so a
	   handler's own entry state must be the *pre-try* ENV, never
	   anything TRY_STMTS itself may have established by the time it
	   threw -- forking a copy before walking TRY_STMTS, and walking
	   every handler from a fresh copy of that same pre-try state (not
	   of whatever TRY_STMTS left behind), gives each handler exactly
	   the same "nothing established yet" view a throw from the very
	   first statement would.  Post-try/catch code is then reachable
	   via any one of: TRY_STMTS running to completion without
	   throwing, or any single handler completing -- an exception can
	   only be caught by one handler, but statically it's unknown
	   which, so all of them are live "arms" needing the same N-way
	   merge_with/range_merge_with/... SWITCH_STMT's own MERGED/
	   any_result/record() already generalizes the if/else merge to
	   (Increment M), reused here verbatim rather than duplicating
	   its own logic. TRY_HANDLERS is always a STATEMENT_LIST of
	   HANDLER nodes (built via push_stmt_list/pop_stmt_list in
	   finish_handler_sequence), each carrying its own body via
	   HANDLER_BODY.  */
	oa_env pre_try = env.copy ();
	oa_env merged;
	bool any_result = false;

	auto record = [&] (oa_env &result)
	  {
	    if (!any_result)
	      {
		merged.assign (result);
		any_result = true;
	      }
	    else
	      {
		merged.merge_with (result);
		merged.range_merge_with (result);
		merged.predicate_fact_merge_with (result);
		merged.relational_merge_with (result);
		merged.type_bound_merge_with (result);
		merged.call_relational_merge_with (result);
		merged.call_call_relational_merge_with (result);
		merged.contract_scalar_range_merge_with (result);
		merged.contract_field_range_merge_with (result);
		merged.contract_call_range_merge_with (result);
		merged.shadow_decls_merge_with (result);
		merged.alias_merge_with (result);
		merged.field_alias_merge_with (result);
		merged.array_alias_merge_with (result);
	      }
	  };

	oa_walk_stmt (&TRY_STMTS (t), env);
	if (!oa_stmt_terminates_p (TRY_STMTS (t)))
	  record (env);

	tree handlers = TRY_HANDLERS (t);
	auto walk_handler = [&] (tree *h)
	  {
	    if (*h == NULL_TREE || TREE_CODE (*h) != HANDLER)
	      return;
	    oa_env handler_env = pre_try.copy ();
	    oa_walk_stmt (&HANDLER_BODY (*h), handler_env);
	    if (!oa_stmt_terminates_p (HANDLER_BODY (*h)))
	      record (handler_env);
	  };
	if (handlers && TREE_CODE (handlers) == STATEMENT_LIST)
	  {
	    for (tree_stmt_iterator i = tsi_start (handlers); !tsi_end_p (i);
		 tsi_next (&i))
	      walk_handler (tsi_stmt_ptr (i));
	  }
	else if (handlers)
	  walk_handler (&TRY_HANDLERS (t));

	/* If nothing was ever recorded, every arm (the try body and
	   every handler alike) provably terminates, so post-try/catch
	   code is unreachable regardless -- leaving ENV as TRY_STMTS'
	   own walk left it is sound either way, mirroring SWITCH_STMT's
	   own identical "any_result stays false" case.  */
	if (any_result)
	  env.assign (merged);
	return;
      }

    case HANDLER:
      /* Defensive only: every HANDLER this pass actually reaches is
	 unwrapped directly by the TRY_BLOCK case above, which forks its
	 own pre-try ENV for it rather than delegating to a generic
	 recursive walk -- this case exists only in case some other path
	 ever feeds a bare HANDLER node here, walking its own body with
	 whatever ENV was already in hand since no better "pre-try"
	 state is available in that situation.  */
      oa_walk_stmt (&HANDLER_BODY (t), env);
      return;

    case PRECONDITION_STMT:
      oa_handle_precondition_stmt (t, env);
      return;

    case POSTCONDITION_STMT:
      oa_handle_postcondition_stmt (t, env);
      return;

    case BIND_EXPR:
      oa_walk_stmt (&BIND_EXPR_BODY (t), env);
      return;

    case DECL_EXPR:
      {
	tree decl = DECL_EXPR_DECL (t);
	bool tracked = (VAR_P (decl)
			&& (POINTER_TYPE_P (TREE_TYPE (decl))
			    || INTEGRAL_TYPE_P (TREE_TYPE (decl))));
	/* A declaration's own initializer ('int c = 10 / q;') is a
	   distinct shape from an ordinary assignment statement ('int c;
	   c = 10 / q;', reaching the INIT_EXPR/MODIFY_EXPR case below) --
	   this was previously the *only* place in the whole walk that
	   never ran the item 7/8 scans on a call/div-mod/array-ref
	   reached through it at all, found while testing Increment
	   E-divmod's IILE support (a direct-initialization local inside
	   a closure body went completely unchecked). Mirrors the exact
	   same three calls RETURN_EXPR's value and INIT_EXPR/MODIFY_
	   EXPR's RHS already use.  */
	if (tracked && DECL_INITIAL (decl))
	  {
	    oa_scan_calls_in_expr (&DECL_INITIAL (decl), env);
	    if (current_function_decl
		&& DECL_DECLARED_CONVEYOR_P (current_function_decl))
	      oa_scan_item8_in_expr (&DECL_INITIAL (decl), env);
	  }
	/* Return-value predicate establishment (see oa_call_symbolic_
	   predicate_p's own comment): the direct-initialization shape's
	   own analogue of the same block in the INIT_EXPR/MODIFY_EXPR case
	   below -- 'int r = produce ();' reaches here, never there (see
	   this case's own top comment on why the two shapes are handled
	   separately).  Any VAR_DECL, not just a POINTER_TYPE_P/INTEGRAL_
	   TYPE_P "tracked" one -- a predicate fact isn't tied to either.
	   No invalidation needed first: a freshly declared decl never had
	   a prior entry.  */
	if (VAR_P (decl) && DECL_INITIAL (decl)
	    && (flag_contract_symbolic_proofs || flag_contract_conveyor_proofs
		|| oa_call_site_callback))
	  {
	    tree stripped_init_pred = STRIP_ANY_LOCATION_WRAPPER (DECL_INITIAL (decl));
	    tree pred_fn;
	    bool polarity, pred_conveyor_established;
	    if (TREE_CODE (stripped_init_pred) == CALL_EXPR
		&& oa_call_symbolic_predicate_p (stripped_init_pred, &pred_fn,
						  &polarity, &pred_conveyor_established))
	      env.predicate_fact_set (env.alias_find (decl), pred_fn, polarity,
				       pred_conveyor_established);
	  }
	if (VAR_P (decl) && POINTER_TYPE_P (TREE_TYPE (decl)))
	  {
	    if (DECL_INITIAL (decl))
	      env.set (decl, oa_provable_p (DECL_INITIAL (decl), env));
	    else
	      env.invalidate (decl);
	    /* Increment E2's array-base+offset tracking, independent of
	       (and alongside) the is_object_address tracking above.  */
	    oa_range_fact fact;
	    if (DECL_INITIAL (decl) && oa_get_range (DECL_INITIAL (decl), env, &fact))
	      env.range_set (decl, fact);
	    else
	      env.range_invalidate (decl);
	    /* -fcontract-symbolic-proofs: a freshly declared decl never had
	       a prior entry, so this is a defensive no-op in practice --
	       kept for the same completeness/consistency reasons as the
	       is_object_address/range invalidation just above.  */
	    env.predicate_fact_invalidate (decl);
	    /* Pointer-aliasing fix, direct-initialization shape's own
	       analogue of the INIT_EXPR/MODIFY_EXPR case's identical
	       block below -- see oa_env::alias_find's own comment.
	       Whatever oa_object_identity_decl itself resolves to, not
	       just another pointer decl -- that function already treats
	       '&f' and 'f' as the same identity for is_opened(f)/is_opened
	       (&f) purposes, so a pointer *initialized* from '&f' (or
	       'this') must reach that same identity too, the same way a
	       pointer copied from another pointer does.  */
	    tree alias_rhs_identity;
	    if (DECL_INITIAL (decl)
		&& oa_object_identity_decl (DECL_INITIAL (decl), &alias_rhs_identity)
		&& alias_rhs_identity != decl)
	      env.alias_set (decl, env.alias_find (alias_rhs_identity));
	    else
	      env.alias_invalidate (decl);
	  }
	else if (VAR_P (decl) && TREE_CODE (TREE_TYPE (decl)) == REFERENCE_TYPE
		 && DECL_INITIAL (decl))
	  {
	    /* A reference can never be rebound after its own declaration
	       (unlike a pointer, there is no INIT_EXPR/MODIFY_EXPR shape
	       to also handle -- 'r = x;' assigns through the reference,
	       it never re-targets it), so this is the *only* site a
	       reference's own aliasing needs recording at.  Same "whatever
	       oa_object_identity_decl itself resolves to" scope as the
	       pointer case above (see its own comment) -- covers 'file &r
	       = *p;', 'file &r2 = r;', and 'file &r = f;' alike.  */
	    tree alias_rhs_identity;
	    if (oa_object_identity_decl (DECL_INITIAL (decl), &alias_rhs_identity)
		&& alias_rhs_identity != decl)
	      env.alias_set (decl, env.alias_find (alias_rhs_identity));
	  }
	else if (VAR_P (decl) && INTEGRAL_TYPE_P (TREE_TYPE (decl)))
	  {
	    /* Item 8's narrow "provably nonzero" tracking, parallel to
	       the pointer tracking above.  */
	    if (DECL_INITIAL (decl))
	      env.nz_set (decl, oa_provably_nonzero_p (DECL_INITIAL (decl), env));
	    else
	      env.nz_invalidate (decl);
	    /* Increment E1's value-range tracking, parallel again.  */
	    oa_range_fact fact;
	    if (DECL_INITIAL (decl) && oa_get_range (DECL_INITIAL (decl), env, &fact))
	      env.range_set (decl, fact);
	    else
	      env.range_invalidate (decl);
	    /* Item 6 for relational facts: a callee's own postcondition
	       relating its return value to one of its own OTHER
	       parameters, e.g. 'int y = f (x, q);' with f's postcondition
	       'post<ctrl>(r: r < q)' -- see oa_establish_relational_
	       from_call's own comment.  No else-invalidate here: unlike
	       range/nz (always-tracked, mandatory-adjacent facts), a
	       relational fact for a freshly-declared decl simply has no
	       prior entry to go stale.  */
	    oa_establish_relational_from_call (decl, DECL_INITIAL (decl), env);
	    /* D4324 Commit 2: same relational/call-relational derive as the
	       INIT_EXPR/MODIFY_EXPR case's own identical addition (see that
	       case's own comment on the PLUS_EXPR/MINUS_EXPR transfer) --
	       'int j = i + k;' is this direct-initialization shape, not
	       that one, so it needs its own copy of the same "try, else
	       nothing" logic. No else-invalidate here either, same reason
	       as oa_establish_relational_from_call just above (a freshly
	       declared decl has no prior entry to go stale).  */
	    if (DECL_INITIAL (decl))
	      {
		oa_relational_fact rel_fact;
		if (oa_get_relational (DECL_INITIAL (decl), env, &rel_fact))
		  env.relational_set (decl, rel_fact.code, rel_fact.rhs,
					rel_fact.conveyor_established,
					rel_fact.offset);
		else
		  {
		    oa_call_relational_fact call_rel_fact;
		    if (oa_get_call_relational (DECL_INITIAL (decl), env,
						  &call_rel_fact))
		      env.call_relational_set (decl, call_rel_fact.code,
						 call_rel_fact.rhs_receiver,
						 call_rel_fact.rhs_callee,
						 call_rel_fact.conveyor_established,
						 call_rel_fact.offset);
		  }
	      }
	    /* -fcontract-conveyor-proof-provenance: mirror the numeric
	       tracking just above, one level up -- a no-op entirely when
	       provenance tracking is inactive.  */
	    oa_derivation *deriv = DECL_INITIAL (decl)
	      ? oa_get_range_derivation (DECL_INITIAL (decl), env) : NULL;
	    if (deriv)
	      env.deriv_set (decl, deriv);
	    else
	      env.deriv_invalidate (decl);
	  }
	return;
      }

    case EXPR_STMT:
      /* Wraps an ordinary expression-statement (e.g. a bare assignment
	 like 'p = &a;') at this pre-genericize stage -- unwrap and
	 recurse on the real expression underneath, so it reaches
	 INIT_EXPR/MODIFY_EXPR below instead of silently falling through
	 the default case unprocessed.  */
      oa_walk_stmt (&EXPR_STMT_EXPR (t), env);
      return;

    case CLEANUP_POINT_EXPR:
    case MUST_NOT_THROW_EXPR:
    case CONVERT_EXPR:
    case NOP_EXPR:
      /* Transparent wrappers introduced around ordinary statements at
	 this pre-genericize stage (a full-expression's temporary cleanup
	 scope; a noexcept boundary; a discarded expression-statement's
	 value converted to void) -- none of these change what's
	 provable, so just recurse into the operand underneath.  */
      oa_walk_stmt (&TREE_OPERAND (t, 0), env);
      return;

    case INIT_EXPR:
    case MODIFY_EXPR:
      {
	tree lhs = TREE_OPERAND (t, 0);
	tree rhs = TREE_OPERAND (t, 1);
	lhs = STRIP_ANY_LOCATION_WRAPPER (lhs);
	/* -fcontract-symbolic-runtime-checks (Mechanism B): PRE_EXTRA
	   accumulates this statement's own consult-side obligation check
	   (a call reached from the RHS whose callee has a bare-parameter
	   precondition, e.g. 'int z = consumer(y);') -- spliced in
	   *before* T, matching ordinary precondition-checking semantics.
	   POST_EXTRA accumulates everything else below (address-taken
	   invalidation, establish/invalidate for LHS itself) -- spliced
	   in *after* T, since all of it reflects what T itself just did
	   or what some other call's arguments may have just changed.  */
	tree pre_extra = NULL_TREE;
	tree post_extra = NULL_TREE;
	oa_relational_fact eager_rel_fact;
	oa_call_relational_fact eager_call_rel_fact;
	/* Postcondition-side call-range composition (see oa_call_
	   postcondition_range_p's own comment on this exact ordering
	   problem, and contracts-gimple.cc's own cg_compose_call_result_
	   range, whose fix this mirrors): if RHS is a direct call whose
	   callee's own postcondition relates its return value to a call-
	   range-eligible accessor on one of its own parameters, compose
	   *now*, before oa_scan_calls_in_expr below invalidates this same
	   call's own exposed receiver. EAGER_RANGE_COMPOSED gates the
	   ordinary oa_get_range-based assignment handling much further
	   below (which runs after invalidation, and so can no longer see
	   the fact this composition needs) -- that handling's own "found
	   nothing, invalidate LHS" branch must not blindly wipe what this
	   already set, or this composition would be nullified immediately
	   after running (found via direct testing: without this, LHS's
	   own range came back exactly as if this call never happened).  */
	bool eager_range_composed = oa_compose_call_result_range (lhs, rhs, env);
	/* D4324: a self-referential reassignment ('idx = idx + 5;', from
	   'idx += 5;') has RHS mention the very same decl Rule 1 below is
	   about to invalidate -- so, exactly like EAGER_RANGE_COMPOSED just
	   above (same underlying problem, same fix shape), RHS's own
	   relational/call-relational fact must be read *now*, before that
	   invalidation, or the "shift an existing fact by a constant"
	   handling much further below (which runs after invalidation) would
	   always see nothing, silently discarding a fact that should have
	   carried over shifted -- found via direct testing of exactly this
	   shape while building the bounds-proving demo (see .claude/plans/
	   lazy-stirring-pearl.md, Part 4): 'if (v.size () - idx > 10) { idx
	   += 5; return v[idx]; }' never verified even though the shift is
	   provably still within the established margin, because idx's own
	   just-established call-relational fact was gone by the time the
	   shift lookup ran.  */
	bool eager_rel_ok = oa_get_relational (rhs, env, &eager_rel_fact);
	bool eager_call_rel_ok
	  = !eager_rel_ok && oa_get_call_relational (rhs, env, &eager_call_rel_fact);
	/* The RHS commonly flows directly from a call (e.g.
	   'int* q = deref(p);') -- discharge any call-site precondition
	   obligation (item 7) for every call reached from here, for the
	   same reason as RETURN_EXPR above.  Also passes PRE_EXTRA
	   through, so a call reached here whose callee has a Mechanism B
	   bare-parameter precondition gets its own runtime check appended
	   too, and POST_EXTRA through as oa_scan_calls_in_expr's own
	   INVALIDATE_EXTRA, so the RHS's own top-level call *and* any call
	   nested arbitrarily deep inside it (e.g. the inner 'modify(&y)'
	   in 'int z = foo(modify(&y), 5);') that takes the address of an
	   already-shadowed bare scalar invalidates that shadow -- this
	   cp_walk_tree already reaches every such call regardless of
	   nesting depth, the same reach oa_handle_call_symbolic_scalar_
	   obligation above already gets through PRE_EXTRA.  */
	oa_scan_calls_in_expr (&TREE_OPERAND (t, 1), env, &pre_extra, &post_extra);
	/* Item 8's narrow div/mod, array-bound, and overflow restrictions,
	   only within a function actually declared 'conveyor'.  */
	if (current_function_decl && DECL_DECLARED_CONVEYOR_P (current_function_decl))
	  oa_scan_item8_in_expr (&TREE_OPERAND (t, 1), env);
	/* Shared-substrate invalidation rule 1: any reassignment of a
	   tracked object's identity invalidates its predicate/field-range
	   facts, whatever the object's type -- unlike the is_object_
	   address/range tracking below, these facts can be keyed on a
	   class-typed decl (e.g. 'f = io_facility();'), which neither the
	   pointer nor the integral branch below ever reaches.  */
	if (flag_contract_symbolic_proofs || flag_contract_conveyor_proofs
	    || oa_call_site_callback)
	  {
	    tree identity;
	    if (oa_object_identity_decl (lhs, &identity))
	      {
		/* Deliberately NOT canonicalized through alias_find: this
		   branch fires for LHS's own reassignment (LHS may be a
		   pointer being repointed, not its pointee being mutated),
		   so invalidating whatever LHS's own raw key tracks is
		   correct -- if LHS currently aliases some other TARGET,
		   nothing was ever stored under LHS's own raw key to begin
		   with (establish/consult always canonicalize first), and
		   TARGET's own fact must NOT be invalidated here: LHS being
		   repointed doesn't touch TARGET's actual memory. Do still
		   drop LHS's own now-stale alias entry, if any -- LHS's own
		   value may have just changed, so it can no longer be
		   trusted to still equal whatever it aliased before this
		   assignment (see oa_env::alias_find's own comment).  */
		env.predicate_fact_invalidate (identity);
		env.type_bound_invalidate (identity);
		env.relational_invalidate_involving (identity);
		env.call_relational_invalidate_involving (identity);
		env.call_call_relational_invalidate_involving (identity);
		env.contract_field_range_invalidate_all (identity);
		env.contract_call_range_invalidate_all (identity);
		env.field_alias_invalidate_all (identity);
		env.array_alias_invalidate_all (identity);
		env.field_object_predicate_invalidate_all (identity);
		oa_invalidate_parameter_alias_group (identity, env);
		env.alias_invalidate (identity);
	      }
	    else
	      {
		/* A direct field write ('p->field = x;', LHS a COMPONENT_REF
		   through an INDIRECT_REF -- or 't.field = x;' for a plain,
		   non-pointer object, LHS a COMPONENT_REF whose own base is
		   already a bare decl with no indirection at all) reassigns
		   just that one field, not the whole object -- narrower
		   invalidation than the whole-object rule just above, using
		   the same ptr->field shape oa_symbolic_comparison_conjunct_
		   shape recognizes for a contract condition (here applied to
		   an ordinary assignment statement's own LHS instead).  */
		tree base = STRIP_ANY_LOCATION_WRAPPER (lhs);
		if (TREE_CODE (base) == COMPONENT_REF)
		  {
		    tree field = TREE_OPERAND (base, 1);
		    tree obj = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (base, 0));
		    if (TREE_CODE (field) == FIELD_DECL)
		      {
			tree obj_expr
			  = TREE_CODE (obj) == INDIRECT_REF
			    ? STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (obj, 0))
			    : obj;
			tree field_identity;
			if (oa_object_identity_decl (obj_expr, &field_identity))
			  {
			    /* Unlike the whole-object branch above, this
			       genuinely mutates the pointee's own memory (a
			       field write through OBJ_EXPR), so -- unlike a
			       pointer's own reassignment -- it must reach
			       whatever OBJ_EXPR currently aliases, not just
			       OBJ_EXPR's own raw key.  */
			    field_identity = env.alias_find (field_identity);
			    env.contract_field_range_invalidate (field_identity, field);
			    /* Stage 4a: a named predicate (e.g. 'is_opened')
			       is opaque and could depend on any field, so
			       a direct write to *any* field must invalidate
			       whole-object predicate facts about FIELD_
			       IDENTITY too -- not just the narrower field-
			       range fact above. Mirrors the sibling whole-
			       object branch above, which already does this
			       unconditionally. Found by asking, after Stage
			       3 shipped, whether any gaps remained: 'open_it
			       (p); p->opened = false; use_it (p);' (the same
			       p, no aliasing at all) wrongly verified before
			       this fix.  */
			    env.predicate_fact_invalidate (field_identity);
			    /* Stage 5: this replaces the FIELD sub-object of
			       FIELD_IDENTITY directly, so whatever predicate
			       fact was tracked about '&field_identity->field'
			       itself (a fixed address, but not a fixed value)
			       must be dropped too -- narrower than the whole-
			       object sweep above, same granularity distinction
			       contract_field_range_invalidate already draws.  */
			    env.field_object_predicate_invalidate (field_identity, field);
			    /* Stage 2a: LHS ('h.ptr' or 'hp->ptr') is a
			       pointer/reference-typed field slot -- record
			       what it now aliases (RHS resolved via either
			       resolver, so a field-to-field copy like
			       'h.ptr = other.ptr;' works too), or drop any
			       stale alias if RHS doesn't resolve to anything
			       recognizable.  Gated on the field's own type,
			       matching Stage 1's own plain-decl gating -- a
			       scalar field write ('h.count = q;') must not
			       populate this map at all.  */
			    if (POINTER_TYPE_P (TREE_TYPE (field))
				|| TREE_CODE (TREE_TYPE (field)) == REFERENCE_TYPE)
			      {
				tree alias_rhs_identity;
				if (oa_object_identity_decl (rhs, &alias_rhs_identity)
				    || oa_field_slot_identity (rhs, env,
							       &alias_rhs_identity)
				    || oa_array_slot_identity (rhs, env,
							       &alias_rhs_identity)
				    || oa_field_object_identity (rhs, env,
								  &alias_rhs_identity))
				  env.field_alias_set (field_identity, field,
							env.alias_find (alias_rhs_identity));
				else
				  env.field_alias_invalidate (field_identity, field);
			      }
			  }
		      }
		  }
		else
		  {
		    /* Stage 2b: 'arr[N] = x;' or the semantically identical
		       '*arr = x;' -- the array-element analogue of the
		       COMPONENT_REF block just above. ARR_BASE may itself be
		       a COMPONENT_REF (a struct-embedded array, 'hp2->arr[0]'/
		       'h2.arr[0]') -- oa_object_identity_decl below already,
		       correctly, declines for that shape (no COMPONENT_REF
		       case), which is the intended, documented "struct-
		       embedded arrays are out of scope" outcome; nothing
		       further is needed to enforce it here.  */
		    tree arr_base, index;
		    if (oa_array_slot_base (base, &arr_base, &index))
		      {
			tree array_identity;
			if (oa_object_identity_decl (arr_base, &array_identity))
			  {
			    array_identity = env.alias_find (array_identity);
			    /* Stage 4a: same reasoning as the field-write
			       branch above -- a named predicate could, in
			       principle, be declared to accept the array's
			       own identity directly, so any write through it
			       must invalidate whole-object predicate facts
			       about ARRAY_IDENTITY too. Deliberately NOT
			       gated on the element's own type (unlike the
			       block below): this is about ARRAY_IDENTITY's
			       own whole-object facts, independent of what
			       kind of element was written.  */
			    env.predicate_fact_invalidate (array_identity);
			    /* Gated on the *element's* own type, matching the
			       field block's own gating just above -- a scalar
			       array's writes must never touch this map at all,
			       not even to call invalidate_all on a base that
			       could never have an entry.  */
			    if (POINTER_TYPE_P (TREE_TYPE (base))
				|| TREE_CODE (TREE_TYPE (base)) == REFERENCE_TYPE)
			      {
				HOST_WIDE_INT idx;
				if (oa_array_index_constant (index, &idx))
				  {
				    tree alias_rhs_identity;
				    if (oa_object_identity_decl (rhs, &alias_rhs_identity)
					|| oa_field_slot_identity (rhs, env,
							   &alias_rhs_identity)
					|| oa_array_slot_identity (rhs, env,
							   &alias_rhs_identity)
					|| oa_field_object_identity (rhs, env,
							   &alias_rhs_identity))
				      env.array_alias_set (array_identity, idx,
							    env.alias_find (alias_rhs_identity));
				    else
				      env.array_alias_invalidate (array_identity, idx);
				  }
				else
				  /* Unprovable index -- could touch any
				     previously tracked slot of this array; see
				     oa_env::array_alias_invalidate_all's own
				     comment for why this must be a full sweep,
				     not a decline-to-update.  */
				  env.array_alias_invalidate_all (array_identity);
			      }
			  }
		      }
		  }
	      }
	  }
	/* Return-value predicate establishment (see oa_call_symbolic_
	   predicate_p's own comment): mirrors Mechanism B's own bare-
	   scalar range establishment just below exactly, one map over --
	   only from a call matching oa_call_symbolic_predicate_p, keyed on
	   the assignment's own LHS, invalidating (not establishing) on any
	   other RHS shape, so LHS never carries a stale predicate fact
	   from before this assignment.  Any VAR_DECL/PARM_DECL LHS, not
	   restricted to a numeric type -- a predicate fact isn't tied to
	   one.  Rule 1 just above already invalidated LHS unconditionally;
	   this may immediately re-establish it.  */
	if ((flag_contract_symbolic_proofs || flag_contract_conveyor_proofs
	     || oa_call_site_callback)
	    && (VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL))
	  {
	    tree stripped_rhs_pred = STRIP_ANY_LOCATION_WRAPPER (rhs);
	    tree pred_fn;
	    bool polarity, pred_conveyor_established;
	    if (TREE_CODE (stripped_rhs_pred) == CALL_EXPR
		&& oa_call_symbolic_predicate_p (stripped_rhs_pred, &pred_fn,
						  &polarity, &pred_conveyor_established))
	      env.predicate_fact_set (env.alias_find (lhs), pred_fn, polarity,
				       pred_conveyor_established);
	  }
	if ((VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL)
	    && POINTER_TYPE_P (TREE_TYPE (lhs)))
	  {
	    env.set (lhs, oa_provable_p (rhs, env));
	    /* Increment E2's array-base+offset tracking, independent of
	       (and alongside) the is_object_address tracking above.  */
	    oa_range_fact fact;
	    if (oa_get_range (rhs, env, &fact))
	      env.range_set (lhs, fact);
	    else
	      env.range_invalidate (lhs);
	    /* Pointer-aliasing fix (see oa_env::alias_find's own comment):
	       'q = p;' means Rule 2 invalidating a call argument's own
	       identity must also be able to reach whatever else currently
	       holds the same value -- record LHS as now aliasing RHS's own
	       identity, whatever oa_object_identity_decl itself resolves
	       RHS to (another pointer decl, '&f', or 'this' -- that
	       function already treats '&f' and 'f' as the same identity,
	       so a pointer *reassigned* from '&f' must reach that same
	       identity too), or drop any stale alias LHS previously had
	       when RHS resolves to nothing recognizable (e.g. 'q =
	       nullptr;' or 'q = some_call();').  */
	    tree alias_rhs_identity;
	    if (oa_object_identity_decl (rhs, &alias_rhs_identity)
		&& alias_rhs_identity != lhs)
	      env.alias_set (lhs, env.alias_find (alias_rhs_identity));
	    else
	      env.alias_invalidate (lhs);
	  }
	else if ((VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL)
		 && INTEGRAL_TYPE_P (TREE_TYPE (lhs)))
	  {
	    /* Item 8's narrow "provably nonzero" tracking, parallel to the
	       pointer tracking above.  */
	    env.nz_set (lhs, oa_provably_nonzero_p (rhs, env));
	    /* Increment E1's value-range tracking, parallel again.  EAGER_
	       RANGE_COMPOSED: don't blindly wipe what oa_compose_call_
	       result_range already set further up this same case, on the
	       same LHS, from a fact this call's own argument invalidation
	       (already run by now) has since dropped -- see that call's
	       own comment.  */
	    oa_range_fact fact;
	    if (oa_get_range (rhs, env, &fact))
	      env.range_set (lhs, fact);
	    else if (!eager_range_composed)
	      env.range_invalidate (lhs);
	    /* Item 6 for relational facts -- see oa_establish_relational_
	       from_call's own comment.  No else-invalidate needed here:
	       Rule 1, just above this whole if/else chain, already
	       unconditionally invalidated any stale relational fact for
	       LHS before this branch ever runs.  */
	    oa_establish_relational_from_call (lhs, rhs, env);
	    /* D4324 Commit 2: RHS may itself resolve to a shifted copy of
	       an already-established relational/call-relational fact for
	       some other decl (see oa_get_relational/oa_get_call_
	       relational's own comment on the PLUS_EXPR/MINUS_EXPR
	       transfer) -- same "no else-invalidate needed" reasoning as
	       oa_establish_relational_from_call just above (Rule 1 already
	       invalidated). Call-relational tried only if the plain shape
	       didn't already resolve something (a fact is only ever one
	       shape at a time). Uses EAGER_REL_OK/EAGER_CALL_REL_OK's own
	       pre-invalidation lookup (see that variable's own comment),
	       not a fresh oa_get_relational/oa_get_call_relational call
	       against ENV here -- Rule 1, above, has since invalidated
	       exactly the fact a self-referential RHS like 'idx + 5' would
	       need to find for itself.  */
	    if (eager_rel_ok)
	      env.relational_set (lhs, eager_rel_fact.code, eager_rel_fact.rhs,
				    eager_rel_fact.conveyor_established,
				    eager_rel_fact.offset);
	    else if (eager_call_rel_ok)
	      env.call_relational_set (lhs, eager_call_rel_fact.code,
					 eager_call_rel_fact.rhs_receiver,
					 eager_call_rel_fact.rhs_callee,
					 eager_call_rel_fact.conveyor_established,
					 eager_call_rel_fact.offset);
	    /* -fcontract-conveyor-proof-provenance: mirror the numeric
	       tracking just above, one level up -- a no-op entirely when
	       provenance tracking is inactive.  */
	    oa_derivation *deriv = oa_get_range_derivation (rhs, env);
	    if (deriv)
	      env.deriv_set (lhs, deriv);
	    else
	      env.deriv_invalidate (lhs);
	  }

	/* -fcontract-symbolic-runtime-checks (Mechanism B): a bare
	   scalar's own runtime-tracked range fact -- entirely independent
	   of the compile-time m_range_map handling just above (a symbolic
	   postcondition gives no compile-time guarantee at all; only an
	   actual runtime record does).  Establishing overwrites any
	   existing shadow (whether or not one already existed); an
	   ordinary reassignment that doesn't match invalidates one only if
	   it already exists -- no shadow is ever created purely to
	   invalidate it.  */
	if (oa_symbolic_codegen_active
	    && (VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL)
	    && INTEGRAL_TYPE_P (TREE_TYPE (lhs)))
	  {
	    tree stripped_rhs = STRIP_ANY_LOCATION_WRAPPER (rhs);
	    oa_range_fact sym_fact;
	    if (TREE_CODE (stripped_rhs) == CALL_EXPR
		&& oa_call_symbolic_range_p (stripped_rhs, &sym_fact))
	      {
		tree shadow = get_or_build_scalar_shadow (lhs, env);
		if (shadow)
		  {
		    tree type = TREE_TYPE (shadow);
		    location_t loc = EXPR_LOCATION (t);
		    tree lo_cst = sym_fact.has_lo
		      ? wide_int_to_tree (long_long_integer_type_node, sym_fact.lo)
		      : build_zero_cst (long_long_integer_type_node);
		    tree hi_cst = sym_fact.has_hi
		      ? wide_int_to_tree (long_long_integer_type_node, sym_fact.hi)
		      : build_zero_cst (long_long_integer_type_node);
		    auto field_ref = [&] (unsigned idx)
		      {
			return build3 (COMPONENT_REF, TREE_TYPE (oa_shadow_field (type, idx)),
				       shadow, oa_shadow_field (type, idx), NULL_TREE);
		      };
		    auto assign_field = [&] (unsigned idx, tree val)
		      {
			tree lhs_ref = field_ref (idx);
			tree a = build2 (MODIFY_EXPR, TREE_TYPE (lhs_ref), lhs_ref,
					  fold_convert (TREE_TYPE (lhs_ref), val));
			TREE_SIDE_EFFECTS (a) = 1;
			append_to_statement_list_force (a, &post_extra);
		      };
		    assign_field (0, boolean_true_node);
		    assign_field (1, sym_fact.has_lo ? boolean_true_node : boolean_false_node);
		    assign_field (2, lo_cst);
		    assign_field (3, sym_fact.has_hi ? boolean_true_node : boolean_false_node);
		    assign_field (4, hi_cst);
		    (void) loc;
		  }
	      }
	    else
	      {
		tree shadow = env.shadow_get (lhs);
		if (shadow)
		  {
		    tree type = TREE_TYPE (shadow);
		    tree lhs_ref = build3 (COMPONENT_REF, TREE_TYPE (oa_shadow_field (type, 0)),
					    shadow, oa_shadow_field (type, 0), NULL_TREE);
		    tree a = build2 (MODIFY_EXPR, TREE_TYPE (lhs_ref), lhs_ref,
				      boolean_false_node);
		    TREE_SIDE_EFFECTS (a) = 1;
		    append_to_statement_list_force (a, &post_extra);
		  }
	      }
	  }

	/* The static-prover analogue of the runtime shadow tracking just
	   above -- same establishment rule (only from a call matching
	   oa_call_symbolic_range_p, the exact same recognizer, reused as-
	   is -- still symbolic-postcondition-scoped only: unlike the
	   predicate/field-range maps, m_contract_scalar_range_map has no
	   conveyor-side gap to close, since -fcontract-conveyor-proofs's
	   own bare-scalar checking already gets full cross-statement
	   tracking from the general-purpose m_range_map), same "invalidate
	   only if a fact already exists, never establish just to
	   invalidate" discipline, just updating ENV's own compile-time map
	   directly instead of emitting code.  Entirely independent of
	   oa_symbolic_codegen_active.  The outer condition widens beyond
	   flag_contract_symbolic_proofs alone so invalidation (the "else"
	   branch below) stays correct whenever a plugin might be
	   consulting this map via oa_env_check_scalar_range_fact, even
	   though establishment itself stays symbolic-only.  */
	if ((flag_contract_symbolic_proofs || flag_contract_conveyor_proofs
	     || oa_call_site_callback)
	    && (VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL)
	    && INTEGRAL_TYPE_P (TREE_TYPE (lhs)))
	  {
	    tree stripped_rhs_static = STRIP_ANY_LOCATION_WRAPPER (rhs);
	    oa_range_fact static_sym_fact;
	    if (TREE_CODE (stripped_rhs_static) == CALL_EXPR
		&& oa_call_symbolic_range_p (stripped_rhs_static, &static_sym_fact))
	      env.contract_scalar_range_set (lhs, static_sym_fact);
	    else
	      env.contract_scalar_range_invalidate (lhs);
	  }
	if (pre_extra || post_extra)
	  {
	    tree new_list = alloc_stmt_list ();
	    append_to_statement_list_force (pre_extra, &new_list);
	    append_to_statement_list_force (t, &new_list);
	    append_to_statement_list_force (post_extra, &new_list);
	    *stmt = new_list;
	  }
	return;
      }

    case COND_EXPR:
      {
	/* Increment K: oa_process_condition scans the condition (calls/
	   div-mod/array-bounds/stray-is_object_address/assignment-
	   tracking) and refines THEN_ENV/ELSE_ENV, all in one interleaved,
	   left-to-right, per-'&&'-conjunct pass -- see its own comment for
	   why this matters (a later conjunct's scan now sees facts
	   established by earlier conjuncts within the same condition).
	   Identical treatment for both shapes (COND_EXPR/IF_STMT).  */
	oa_env then_env, else_env;
	oa_process_condition (TREE_OPERAND (t, 0), env, &then_env, &else_env);
	oa_walk_stmt (&TREE_OPERAND (t, 1), then_env);
	oa_walk_stmt (&TREE_OPERAND (t, 2), else_env);
	/* Increment H: if exactly one arm never falls through (always
	   returns/throws/hits a noreturn call), code after this
	   conditional is only ever reached via the *other* arm -- use
	   its facts as-is rather than blindly merging in the
	   terminating arm's (which the merge point never actually
	   sees).  */
	bool then_terminates = oa_stmt_terminates_p (TREE_OPERAND (t, 1));
	bool else_terminates = oa_stmt_terminates_p (TREE_OPERAND (t, 2));
	if (then_terminates && !else_terminates)
	  env.assign (else_env);
	else if (else_terminates && !then_terminates)
	  env.assign (then_env);
	else
	  {
	    then_env.merge_with (else_env);
	    then_env.range_merge_with (else_env);
	    /* -fcontract-conveyor-proof-provenance: mirror the numeric
	       merge just above, one level up -- see oa_env::deriv_merge_
	       with's own comment; a no-op entirely when provenance
	       tracking is inactive.  */
	    then_env.deriv_merge_with (else_env, TREE_OPERAND (t, 0));
	    /* -fcontract-symbolic-proofs: a symbolic fact survives the
	       merge only if both branches agree on it exactly (same
	       PRED_FN, same polarity) -- see oa_env::predicate_fact_merge_with's
	       own comment.  */
	    then_env.predicate_fact_merge_with (else_env);
	    then_env.relational_merge_with (else_env);
	    then_env.type_bound_merge_with (else_env);
	    then_env.call_relational_merge_with (else_env);
	    then_env.call_call_relational_merge_with (else_env);
	    /* -fcontract-symbolic-proofs: same intersect-and-widen merge as
	       range_merge_with, for the two new static-only symbolic range
	       maps (bare-scalar and ptr->field).  */
	    then_env.contract_scalar_range_merge_with (else_env);
	    then_env.contract_field_range_merge_with (else_env);
	    then_env.contract_call_range_merge_with (else_env);
	    /* -fcontract-symbolic-runtime-checks (Mechanism B): a shadow's
	       own *existence* is a plain set union across branches, not
	       an agreement check -- see oa_env::shadow_decls_merge_with's
	       own comment for why.  */
	    then_env.shadow_decls_merge_with (else_env);
	    /* Pointer-aliasing fix: agreement-based, same as predicate_
	       fact_merge_with -- see oa_env::alias_find's own comment.  */
	    then_env.alias_merge_with (else_env);
	    then_env.field_alias_merge_with (else_env);
	    then_env.array_alias_merge_with (else_env);
	    env.assign (then_env);
	  }
	return;
      }

    case IF_STMT:
      /* The pre-genericize cp-tree shape of an if/else (COND_EXPR above
	 is what genericization eventually lowers this to, but that
	 hasn't happened yet at this point in the pipeline).  Same
	 if/else merge rule, just via THEN_CLAUSE/ELSE_CLAUSE instead of
	 TREE_OPERAND 1/2.  */
      {
	/* The condition operand itself: not walked via a full oa_walk_stmt
	   dispatch (unlike a plain sub-statement), to avoid re-dispatching
	   into (and so double-scanning/double-reporting through) the very
	   same CALL_EXPR/INIT_EXPR/MODIFY_EXPR cases that already perform
	   their own oa_scan_calls_in_expr internally -- see oa_process_
	   condition's own comment for the explicit, interleaved scan-then-
	   refine sequencing it performs instead (Increment K), including
	   the call-site precondition-obligation check (item 7), the
	   narrow item-8 scans (conveyor functions only), the stray-
	   is_object_address well-formedness gate, and assignment-in-
	   condition tracking (oa_track_condition_assignment, narrowly
	   scoped to a top-level assignment only, never inside &&/||, see
	   its own comment for why).  */
	oa_env then_env, else_env;
	oa_process_condition (IF_COND (t), env, &then_env, &else_env);
	oa_walk_stmt (&THEN_CLAUSE (t), then_env);
	oa_walk_stmt (&ELSE_CLAUSE (t), else_env);
	/* Increment H: same reachability-aware merge as COND_EXPR above
	   -- if exactly one arm never falls through, code after the if
	   is only ever reached via the other arm.  This is what makes
	   the common early-return-guard idiom ('if (n <= 0) return 0;')
	   work: ELSE_CLAUSE is NULL_TREE (falls through trivially), so
	   ELSE_ENV alone -- carrying the else-branch's own range
	   refinement -- survives to the code after the if.  */
	bool then_terminates = oa_stmt_terminates_p (THEN_CLAUSE (t));
	bool else_terminates = oa_stmt_terminates_p (ELSE_CLAUSE (t));
	if (then_terminates && !else_terminates)
	  env.assign (else_env);
	else if (else_terminates && !then_terminates)
	  env.assign (then_env);
	else
	  {
	    then_env.merge_with (else_env);
	    then_env.range_merge_with (else_env);
	    /* -fcontract-conveyor-proof-provenance: mirror the numeric
	       merge just above, one level up -- see oa_env::deriv_merge_
	       with's own comment; a no-op entirely when provenance
	       tracking is inactive.  */
	    then_env.deriv_merge_with (else_env, IF_COND (t));
	    /* -fcontract-symbolic-proofs: same merge rule as COND_EXPR
	       above.  */
	    then_env.predicate_fact_merge_with (else_env);
	    then_env.relational_merge_with (else_env);
	    then_env.type_bound_merge_with (else_env);
	    then_env.call_relational_merge_with (else_env);
	    then_env.call_call_relational_merge_with (else_env);
	    /* -fcontract-symbolic-proofs: same as COND_EXPR above.  */
	    then_env.contract_scalar_range_merge_with (else_env);
	    then_env.contract_field_range_merge_with (else_env);
	    then_env.contract_call_range_merge_with (else_env);
	    /* -fcontract-symbolic-runtime-checks (Mechanism B): same union
	       rule as COND_EXPR above.  */
	    then_env.shadow_decls_merge_with (else_env);
	    /* Pointer-aliasing fix: same agreement-based merge as COND_EXPR
	       above -- see oa_env::alias_find's own comment.  */
	    then_env.alias_merge_with (else_env);
	    then_env.field_alias_merge_with (else_env);
	    then_env.array_alias_merge_with (else_env);
	    env.assign (then_env);
	  }
	return;
      }

    case ASSERTION_STMT:
      oa_handle_assertion_stmt (t, env);
      return;

    case CALL_EXPR:
      /* A call used as its own complete expression-statement (e.g.
	 'deref(p);', return value discarded) -- everywhere else a call
	 can appear (a RETURN_EXPR's value, an INIT_EXPR/MODIFY_EXPR's
	 RHS) is handled by an explicit oa_scan_calls_in_expr call at that
	 site instead, since this switch only ever dispatches on a node's
	 own top-level code. oa_scan_calls_in_expr's own is_object_address
	 exclusion means a bare 'std::is_object_address(x);' reaching here
	 directly (used outside any contract, i.e. not as an ASSERTION_STMT/
	 PRECONDITION_STMT/POSTCONDITION_STMT's own condition) is correctly
	 left for the default fallback's stray-use scan below instead.  */
      {
	/* -fcontract-symbolic-runtime-checks (Mechanism B): PRE_EXTRA
	   accumulates this call's own consult-side obligation check
	   (oa_handle_call_symbolic_scalar_obligation, via
	   oa_scan_calls_in_expr's own EXTRA parameter) -- spliced in
	   *before* T, matching ordinary precondition-checking semantics
	   (checked ahead of the call actually running), unlike POST_EXTRA
	   below (address-taken invalidation, via oa_scan_calls_in_expr's
	   own INVALIDATE_EXTRA parameter), which belongs *after* T, since
	   it reflects what T and any call nested inside its own arguments
	   may have just changed -- this cp_walk_tree visits T itself and
	   every call reachable from its arguments regardless of nesting
	   depth, so a case like 'foo(modify(&y), 5);' correctly invalidates
	   y's shadow from the inner modify(&y) call too, not just from a
	   shallow check of T's own top-level shape.  */
	tree pre_extra = NULL_TREE;
	tree post_extra = NULL_TREE;
	oa_scan_calls_in_expr (stmt, env, &pre_extra, &post_extra);
	if (pre_extra || post_extra)
	  {
	    tree new_list = alloc_stmt_list ();
	    append_to_statement_list_force (pre_extra, &new_list);
	    append_to_statement_list_force (t, &new_list);
	    append_to_statement_list_force (post_extra, &new_list);
	    *stmt = new_list;
	  }
      }
      /* Item 8's narrow div/mod, array-bound, and overflow restrictions
	 -- a bare call statement's own arguments (e.g. 'some_call (x++);')
	 are exactly as much this scan's business as any other expression-
	 statement's, and this CALL_EXPR case is one of only two (the
	 other being RETURN_EXPR above) that fall through to the shared
	 OA_DEFAULT_SCAN label below, so it must run this scan explicitly
	 itself, on T specifically (not *STMT, which the restructuring
	 just above may have replaced with a wrapper STATEMENT_LIST) --
	 see OA_DEFAULT_SCAN's own comment for why it does not repeat this
	 scan for either of these two cases.  */
      if (current_function_decl && DECL_DECLARED_CONVEYOR_P (current_function_decl))
	oa_scan_item8_in_expr (&t, env);
      goto oa_default_scan;

    case FOR_STMT:
      /* FOR_INIT_STMT only ever runs once, before the loop -- walk it
	 with ordinary straight-line semantics; everything else
	 (cond-prep/cond/body/increment) repeats every iteration, handled
	 by the loop-header merge rule (item 4).  */
      oa_walk_stmt (&FOR_INIT_STMT (t), env);
      oa_handle_loop (&FOR_COND_PREP (t), &FOR_COND (t), &FOR_BODY (t),
		      &FOR_EXPR (t), env);
      return;

    case WHILE_STMT:
      oa_handle_loop (&WHILE_COND_PREP (t), &WHILE_COND (t),
		      &WHILE_BODY (t), NULL, env);
      return;

    case DO_STMT:
      oa_handle_loop (NULL, &DO_COND (t), &DO_BODY (t), NULL, env);
      return;

    case SWITCH_STMT:
      {
	/* Increment M: closes the "no SWITCH_STMT case at all" gap --
	   previously a switch body got no fact-tracking whatsoever
	   (no assignment tracking, no call-site obligations, no
	   contract_assert resolution), silently falling to the default
	   fallback's stray-is_object_address-only scan.

	   The condition itself: same explicit scan-only treatment
	   IF_STMT/COND_EXPR's own condition gets (item 7's obligation
	   scan, item 8's narrow scans, the stray-use gate).  */
	oa_scan_calls_in_expr (&SWITCH_STMT_COND (t), env);
	if (current_function_decl && DECL_DECLARED_CONVEYOR_P (current_function_decl))
	  oa_scan_item8_in_expr (&SWITCH_STMT_COND (t), env);
	oa_scan_stray_is_object_address (&SWITCH_STMT_COND (t));
	oa_scan_stray_symbolic_call (&SWITCH_STMT_COND (t));

	/* The body (always a STATEMENT_LIST -- confirmed via
	   finish_switch_stmt/pop_switch, built via push_stmt_list/
	   pop_stmt_list) is walked left to right in CURRENT, reset to
	   the pre-switch ENV at every CASE_LABEL_EXPR -- a switch has
	   multiple valid entry points (each case/default label), so a
	   naive single top-to-bottom walk would be unsound: 'case 0:
	   p = &a; case 1: contract_assert<...>(is_object_address(p));'
	   (intentional fallthrough) would wrongly trust P at case 1
	   even when case 1 is entered *directly*, bypassing case 0's
	   assignment entirely. The reset means a fact is only ever
	   trusted within one label-to-label run, never assumed to
	   carry across a label boundary -- conservative but sound;
	   genuine fallthrough-dependent facts are a documented,
	   accepted precision loss.

	   MERGED accumulates every legitimate "reaches past the
	   switch" exit point (a bare break, or falling off the body's
	   true end) via the same merge_with/range_merge_with used for
	   the if/else merge (Increment H), generalized to N branches;
	   REACHABLE_AT_END tracks whether the *current* run could
	   still reach the very end of the body (reset true at each
	   label, false after a break or anything oa_stmt_terminates_p
	   already recognizes as never falling through).  */
	oa_env current = env.copy ();
	oa_env merged;
	bool any_result = false;
	bool reachable_at_end = true;

	auto record = [&] ()
	  {
	    if (!any_result)
	      {
		merged.assign (current);
		any_result = true;
	      }
	    else
	      {
		merged.merge_with (current);
		merged.range_merge_with (current);
		/* -fcontract-symbolic-proofs: same merge rule as the
		   if/else case.  */
		merged.predicate_fact_merge_with (current);
		merged.relational_merge_with (current);
		merged.type_bound_merge_with (current);
		merged.call_relational_merge_with (current);
		merged.call_call_relational_merge_with (current);
		/* -fcontract-symbolic-proofs: same as the if/else case.  */
		merged.contract_scalar_range_merge_with (current);
		merged.contract_field_range_merge_with (current);
		merged.contract_call_range_merge_with (current);
		/* -fcontract-symbolic-runtime-checks (Mechanism B): same
		   union rule as the if/else case.  */
		merged.shadow_decls_merge_with (current);
		/* Pointer-aliasing fix: same agreement-based merge as the
		   if/else case -- see oa_env::alias_find's own comment.  */
		merged.alias_merge_with (current);
		merged.field_alias_merge_with (current);
		merged.array_alias_merge_with (current);
	      }
	  };

	auto process_elem = [&] (tree *elem_ptr)
	  {
	    tree elem = *elem_ptr;
	    if (elem == NULL_TREE)
	      return;
	    tree stripped = elem;
	    STRIP_ANY_LOCATION_WRAPPER (stripped);
	    if (TREE_CODE (stripped) == CASE_LABEL_EXPR)
	      {
		current.assign (env);
		reachable_at_end = true;
		return;
	      }
	    oa_walk_stmt (elem_ptr, current);
	    if (TREE_CODE (stripped) == BREAK_STMT)
	      {
		record ();
		current.assign (env);
		reachable_at_end = false;
	      }
	    else if (oa_stmt_terminates_p (elem))
	      {
		current.assign (env);
		reachable_at_end = false;
	      }
	    else
	      reachable_at_end = true;
	  };

	tree body = SWITCH_STMT_BODY (t);
	if (body && TREE_CODE (body) == STATEMENT_LIST)
	  {
	    for (tree_stmt_iterator i = tsi_start (body); !tsi_end_p (i); tsi_next (&i))
	      process_elem (tsi_stmt_ptr (i));
	  }
	else if (body)
	  process_elem (&SWITCH_STMT_BODY (t));

	if (reachable_at_end)
	  record ();

	/* No default label (and not provably exhaustive): "no case
	   matches" is itself a legitimate way to reach post-switch
	   code, contributing the untouched pre-switch ENV.  */
	if (!SWITCH_STMT_ALL_CASES_P (t))
	  {
	    if (!any_result)
	      merged.assign (env);
	    else
	      {
		merged.merge_with (env);
		merged.range_merge_with (env);
		merged.predicate_fact_merge_with (env);
		merged.relational_merge_with (env);
		merged.type_bound_merge_with (env);
		merged.call_relational_merge_with (env);
		merged.call_call_relational_merge_with (env);
		merged.contract_scalar_range_merge_with (env);
		merged.contract_field_range_merge_with (env);
		merged.contract_call_range_merge_with (env);
		merged.shadow_decls_merge_with (env);
		merged.alias_merge_with (env);
		merged.field_alias_merge_with (env);
		merged.array_alias_merge_with (env);
	      }
	    any_result = true;
	  }

	/* If nothing was ever recorded, every run provably terminates
	   and the switch is exhaustive with no break -- exactly
	   Increment L's own oa_stmt_terminates_p (SWITCH_STMT)
	   conclusion. Post-switch code is unreachable either way, so
	   leaving ENV untouched is sound.  */
	if (any_result)
	  env.assign (merged);
	return;
      }

    case RANGE_FOR_STMT:
      /* Defensive only: an ordinary (non-template, already-instantiated)
	 function's range-for is already desugared into a plain FOR_STMT
	 by the time finish_function runs (cp_convert_range_for) -- a real
	 RANGE_FOR_STMT only survives for a dependent template body, which
	 this whole pass already skips via resolve_object_address_in_
	 function's processing_template_decl guard, so this case is not
	 expected to be reachable in practice.  Walk both operands plainly
	 rather than invoking the full loop-header merge logic, since the
	 exact per-iteration semantics here don't matter for a path that
	 should never actually execute.  */
      oa_walk_stmt (&RANGE_FOR_EXPR (t), env);
      oa_walk_stmt (&RANGE_FOR_BODY (t), env);
      return;

    default:
      /* D4324, item 8: this default case is exactly where a bare,
	 top-level expression-statement lands when it's neither an
	 assignment/return/condition nor any other specifically-handled
	 shape -- an ordinary 'x++;'/'--x;'/'a / b;'/'arr[i];' used as its
	 own complete statement, or (via oa_handle_loop's own walk_parts,
	 which passes a FOR_STMT's own FOR_EXPR through oa_walk_stmt with
	 no dedicated case of its own, exactly like BODY) a for-loop's own
	 increment-clause, e.g. the '++i' in 'for (...; ...; ++i)'. Found
	 via direct testing that neither the two pre-existing item-8 scans
	 nor the new overflow one ever reached either shape before this --
	 a real, practically significant gap for the overflow scan
	 specifically, since a bare increment/decrement statement and a
	 for-loop's own increment-clause are the two most common ways
	 '++'/'--' actually appear in real code, far more so than as a
	 sub-expression of an assignment or return.

	 T here is only ever the whole node reached by this fallback, so
	 scanning it (unlike the two sites below that jump here via GOTO
	 OA_DEFAULT_SCAN) cannot double up with any more specific, already-
	 scanned sub-expression.  */
      if (current_function_decl && DECL_DECLARED_CONVEYOR_P (current_function_decl))
	oa_scan_item8_in_expr (&t, env);
    oa_default_scan:
      /* Anything else (TRY_BLOCK, SWITCH_STMT, ordinary
	 expression statements, a RETURN_EXPR's own value expression
	 falling through from above, ...): not yet specially handled in
	 this increment -- is_object_address is only ever legitimate
	 directly inside a recognized contract construct's own condition
	 (ASSERTION_STMT and a function's own PRECONDITION_STMT/
	 POSTCONDITION_STMT above; the call-site precondition-obligation
	 mechanism for *other* functions' preconditions is a separate,
	 not-yet-wired gap -- see item 7 in the plan), so any occurrence
	 reached via this fallback, at any nesting depth, is always an
	 error: there is no "proper" resolution path for it here.
	 Unconditionally scan the whole subtree (not just T itself) so
	 nothing nested inside an unhandled construct silently passes
	 through unchecked. RETURN_EXPR and CALL_EXPR above both jump
	 directly to this label, skipping the item-8 scan just above --
	 both already ran it themselves, on their own more specific sub-
	 expression, before jumping here (see each one's own comment).  */
      oa_scan_stray_is_object_address (&t);
      oa_scan_stray_symbolic_call (&t);
      return;
    }
}

/* True if FNDECL has at least one active (conveyor, non-ignored)
   postcondition.  */

static bool
oa_has_active_postcondition (tree fndecl)
{
  for (tree as = get_fn_contract_specifiers (fndecl); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (POSTCONDITION_P (contract) && oa_contract_conveyor_active_p (contract))
	return true;
    }
  return false;
}

/* Handle one POSTCONDITION_STMT encountered during the body walk (see
   the TRY_FINALLY_EXPR/EH_ELSE_EXPR cases in oa_walk_stmt above -- by
   construction this is always reached *after* every RETURN_EXPR in the
   same function's try-block has already been walked, since the
   postcondition lives in the try-finally's handler operand).
   OA_RETURN_ALL_PROVABLE/OA_RETURN_SEEN (globals above) hold whether
   the returned value was provably an object address on *every* return
   path encountered (false/unseen if the function has no return
   statement at all -- e.g. every path throws -- which conservatively
   fails to prove anything, the same "must be provable, else treated as
   unprovable" discipline as everywhere else here). Unlike the
   precondition, this is proven (not trusted): the postcondition is
   checked at THIS function's own return, in THIS function's own body,
   so the ordinary reaching-definition rules apply, merged across every
   exit point instead of just two branches. Builds a one-entry env
   binding the postcondition's own named result identifier to that
   merged result, since that identifier is a synthetic binding to
   "whatever was returned," not an ordinary local variable
   oa_provable_p's env lookup otherwise tracks.

   CONTRACT is the actual node embedded in the body, for the same
   sharing reason explained on oa_handle_precondition_stmt above.

   D4324: "this is proven" above is currently only true for is_object_
   address claims (via OA_RETURN_ALL_PROVABLE/OA_RETURN_SEEN) -- for an
   ordinary relational/range claim (e.g. 'post<ctrl>(r: r > 0)'), this
   function -- like oa_resolve_condition it calls into -- has no
   equivalent verification at all, the same gap
   oa_check_assertion_conjunct_against_env (above, this file) closes
   for a bare contract_assert. Fixing it here is NOT the same size of
   change: a contract_assert sits at one real program point reached
   during the ordinary walk, where the already-sound per-point ENV is
   simply consulted in place; a postcondition is checked once, at a
   single shared exit point downstream of *every* return (see the
   TRY_FINALLY_EXPR/EH_ELSE_EXPR comment above), and the ENV that
   reaches it is an artifact of walk order, not a genuine multi-exit
   join. The one thing that IS correctly merged across every return
   today is OA_RETURN_RANGE_FACT (see oa_return_range_tracking's own
   comment further up) -- but it's scoped to exactly one fact about the
   return expression itself, isn't even enabled for a self-postcondition
   check today (only for oa_resolve_iile_call's own, different, IILE-body
   analysis), and has no analog for a postcondition naming an ordinary
   parameter directly. Making this sound would need new per-exit-point
   fact-merging infrastructure generalized well beyond that one
   accumulator -- a real, separate undertaking, not attempted here.  */

static void
oa_handle_postcondition_stmt (tree contract, oa_env &env)
{
  bool conveyor_ok = oa_contract_conveyor_active_p (contract);
  bool symbolic_ok = oa_contract_symbolic_active_p (contract);
  tree cond = CONTRACT_CONDITION (contract);
  if (cond == NULL_TREE || cond == error_mark_node)
    return;

  oa_env ret_env;
  tree result_id = POSTCONDITION_IDENTIFIER (contract);
  if (result_id && (VAR_P (result_id) || TREE_CODE (result_id) == PARM_DECL))
    ret_env.set (result_id, oa_return_seen && oa_return_all_provable);

  /* D4324/P2680, Increment V: the narrow item-8 dataflow checks, same as
     oa_handle_precondition_stmt/oa_handle_assertion_stmt above -- scanned
     against a copy of the real, accumulated function-body ENV (so an
     ordinary local variable referenced in the postcondition is checked
     using everything established throughout the function), additionally
     seeded with the postcondition's own named-result identifier's
     is_object_address-provability (mirroring RET_ENV above -- but not
     its nonzero-ness or range, which nothing yet seeds for a named
     result; a conjunct needing those for the result identifier itself
     is conservatively left unprovable, a documented, narrower-than-
     ideal scope, not a soundness gap). Now given the same left-to-right,
     per-conjunct refinement as every other item-8 call site via
     oa_scan_item8_in_expr (which makes its own further scratch copy of
     SCAN_ENV internally -- a copy of an already-scratch copy, harmless).  */
  if (conveyor_ok)
    {
      oa_env scan_env = env.copy ();
      if (result_id && (VAR_P (result_id) || TREE_CODE (result_id) == PARM_DECL))
	scan_env.set (result_id, oa_return_seen && oa_return_all_provable);
      oa_scan_item8_in_expr (&cond, scan_env);
    }

  if (!oa_resolve_condition (&cond, ret_env, conveyor_ok, symbolic_ok))
    {
      CONTRACT_CONDITION (contract) = error_mark_node;
      return;
    }
  CONTRACT_CONDITION (contract) = cond;
}

/* Cache FNDECL's own contracts' conveyor-/symbolic-active status now,
   at reliable front-end (pre-genericize) time, into oa_conveyor_
   active_contract_cache/oa_symbolic_active_contract_cache -- for a
   GIMPLE-pass-based consumer that needs a flavor split (unlike this
   whole file's other oa_* consumers, which only ever call oa_contract_
   conveyor_active_p/oa_contract_symbolic_active_p directly, always at
   this same reliable timing) to consult later via oa_contract_
   conveyor_active_cached_p/oa_contract_symbolic_active_cached_p
   instead of calling into the real, non-cached predicates itself.
   Runs once per function, alongside (and for the same reason as) the
   rest of this pass's own mandatory work -- see ~/gimple-contract-
   analysis.md, Sections 9.3/10, for the empirical finding motivating
   this: oa_contract_conveyor_active_p/oa_contract_symbolic_active_p do
   real semantic work (overload resolution + constexpr evaluation, via
   contract_control_bool_member/build_new_method_call/maybe_constant_
   value) that silently, incorrectly answers false once called from
   GIMPLE-pass timing instead -- direct instrumentation confirmed the
   condition tree itself was completely intact at that point, so the
   failure is specifically in that semantic-analysis machinery's own
   reliance on front-end-only context, not in anything the oa_* fact-
   tracking layer itself does.  */

static void
oa_cache_contract_flavors (tree fndecl)
{
  for (tree as = get_fn_contract_specifiers (fndecl); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (oa_contract_conveyor_active_p (contract, fndecl))
	{
	  if (!oa_conveyor_active_contract_cache)
	    oa_conveyor_active_contract_cache = hash_set<tree>::create_ggc (37);
	  oa_conveyor_active_contract_cache->add (contract);
	}
      if (oa_contract_symbolic_active_p (contract, fndecl))
	{
	  if (!oa_symbolic_active_contract_cache)
	    oa_symbolic_active_contract_cache = hash_set<tree>::create_ggc (37);
	  oa_symbolic_active_contract_cache->add (contract);
	}
    }
}

/* Cheap pre-scan: does FNDECL's own analysis potentially need the full
   oa_walk_stmt pass at all?  True whenever anything below *might* need
   proving/checking, so the only thing skipping the full walk buys is
   a function where nothing here is present at all.

   Formerly a dedicated cp_walk_tree over the whole body (looking for
   an ASSERTION_STMT or a call to an active-precondition callee);
   replaced by a direct read of DECL_MIGHT_NEED_OA_SCAN_P, set
   incrementally at the several places that information is already
   naturally produced -- see that bit's own comment (cp-tree.h) for
   the full, audited list of touch points (contract_assert parsing/
   re-instantiation, ordinary call resolution, and the two splice
   sites -- NSDMI reuse, default-argument reuse -- that reuse an
   already-resolved expression without re-running ordinary call
   resolution for it).

   Deliberately does *not* try to special-case "a pre/post/assert is
   present but not itself conveyor/symbolic-active" as still skippable:
   such a contract's own condition still needs oa_resolve_condition's
   well-formedness check (e.g. a non-active pre<> containing
   is_object_address is still an error), and replicating that check
   standalone here would just be a second, narrower copy of the real
   walk -- "any contract present at all, active or not" is the
   simplest answer that stays correct.

   The call-obligation case (a callee's own active precondition,
   discharged at the call site regardless of whether *this* function
   has any contracts of its own -- see oa_handle_call_precondition_
   obligation's own comment) is why this can't just check FNDECL's own
   contracts: gcc.dg/.../d4324-object-address-callsite-bad.C is
   exactly a contract-free caller that still must be walked because
   its callee isn't -- DECL_MIGHT_NEED_OA_SCAN_P's own call-resolution
   touch point (maybe_contract_wrap_call) covers this the same way the
   old body scan's own CALL_EXPR case did.

   DECL_CONTRACT_WRAPPER is its own, separate "always walk" trigger,
   alongside DECL_DECLARED_CONVEYOR_P: a contract-check wrapper's own
   call to the real function it wraps is built via build_thunk_like_
   call/build_call_a, bypassing build_cxx_call (and so DECL_MIGHT_
   NEED_OA_SCAN_P's own touch point there) entirely -- conservatively
   always walking any wrapper, rather than duplicating the activity
   check at that construction site too, since a wrapper's whole
   purpose is calling something whose contracts matter.  */

static bool
oa_function_needs_walk_p (tree fndecl)
{
  /* Both orthogonal to per-function contract activity -- never skip
     when either could observe/mutate this function's own walk (see
     oa_symbolic_codegen_active's/oa_call_site_callback's own
     comments).  */
  if (flag_contract_symbolic_runtime_checks || oa_call_site_callback)
    return true;

  if (get_fn_contract_specifiers (fndecl))
    return true;

  if (DECL_DECLARED_CONVEYOR_P (fndecl)
      || (DECL_LANG_SPECIFIC (fndecl) && DECL_CONTRACT_WRAPPER (fndecl)))
    return true;

  return DECL_MIGHT_NEED_OA_SCAN_P (fndecl);
}

/* Plugin-facing (and, prospectively, in-tree-GIMPLE-pass-facing)
   readers of the cache oa_cache_contract_flavors populates -- pure
   lookups, no semantic analysis, safe to call at any time after the
   owning function's own front-end processing has completed.  */

bool
oa_contract_conveyor_active_cached_p (tree contract)
{
  return oa_conveyor_active_contract_cache
	 && oa_conveyor_active_contract_cache->contains (contract);
}

bool
oa_contract_symbolic_active_cached_p (tree contract)
{
  return oa_symbolic_active_contract_cache
	 && oa_symbolic_active_contract_cache->contains (contract);
}

/* Shared body for resolve_object_address_in_function and
   oa_walk_function_calls below: both need exactly the same early exits
   and the same fresh, freshly-tracked oa_env walk over FNDECL's own
   pre-genericize body -- they differ only in whether a plugin's own
   call-site callback is armed (via oa_call_site_callback) around the
   walk, which is oa_walk_function_calls's own, sole addition.  */

static void
oa_resolve_object_address_in_function_1 (tree fndecl)
{
  if (!flag_contract_control_objects)
    return;

  oa_cache_contract_flavors (fndecl);
  /* Skip an uninstantiated template pattern, exactly like
     maybe_save_constexpr_fundef/check_conveyor_function_body -- this
     naturally re-runs at instantiation time, when finish_function runs
     again for the instantiated body with concrete types and real,
     non-dependent local variables to trace.  */
  if (processing_template_decl)
    return;

  tree body = DECL_SAVED_TREE (fndecl);
  if (body == NULL_TREE || body == error_mark_node)
    return;

  if (!oa_function_needs_walk_p (fndecl))
    {
      /* Nothing here could possibly need proving -- but a stray,
	 always-illegal is_object_address/symbolic-declared call can
	 still be anywhere in this body, and since oa_function_needs_
	 walk_p just proved there is no ASSERTION_STMT/PRECONDITION_STMT/
	 POSTCONDITION_STMT anywhere in it either, every such call found
	 here is necessarily stray (no legitimate contract condition
	 exists anywhere in this function to legally contain one).  */
      oa_scan_stray_is_object_address (&body);
      oa_scan_stray_symbolic_call (&body);
      return;
    }

  oa_env env;
  /* Stage 5: the shared, cross-branch, cross-nested-walk cache backing
     oa_env::field_object_identity_key -- allocated once here, the true
     single per-function entry point for the *outermost* env, and
     explicitly propagated (never freshly allocated) into every other
     oa_env this walk ever constructs, whether via copy()/assign() (every
     branch/loop/try fork) or explicitly (oa_resolve_iile_call/_range's
     own fresh inner_env, see their own comments). A plain stack-local
     hash_map, not heap-allocated: its address stays valid for this
     entire function's walk, including any nested IILE resolution, since
     that always happens synchronously within this same call.  */
  hash_map<oa_field_key_hash, tree> field_object_key_cache;
  env.set_field_object_key_cache (&field_object_key_cache);

  /* -fcontract-symbolic-runtime-checks (Mechanism B): every shadow
     variable get_or_build_scalar_shadow ever creates for this function
     needs one stable scope to live in, regardless of how deeply nested
     the statement that first needs it is (see the plan's own "shadow
     placement" note for why declaring it anywhere else is unsound).
     BODY at this pre-genericize stage is *not* generally wrapped in a
     BIND_EXPR at all yet (that only happens later, during real
     genericization) -- it is typically a bare STATEMENT_LIST, or that
     wrapped in a TRY_FINALLY_EXPR for a function with active
     postcondition processing (see oa_walk_stmt's own TRY_FINALLY_EXPR
     case) -- so there is no existing scope to hook into here at all.
     When the flag is on, synthesize one: wrap the whole, untouched
     original BODY in a fresh, otherwise-empty BIND_EXPR, and write it
     back to DECL_SAVED_TREE itself -- BODY here is only a local
     snapshot of that slot's value, and mutating operand slots reached
     *through* it (as every other oa_walk_stmt case already does)
     persists correctly on its own, but reassigning BODY's own top-
     level identity does not, without this explicit writeback.  An
     empty-VARS BIND_EXPR wrapping the exact same content it already
     held is completely transparent (the same as adding one extra pair
     of braces in source) until/unless a shadow is actually prepended
     to it, so this is done unconditionally whenever the flag is on,
     not only for functions later found to need one.  */
  if (flag_contract_symbolic_runtime_checks)
    {
      tree outer_bind = build3 (BIND_EXPR, void_type_node, NULL_TREE,
				 body, NULL_TREE);
      DECL_SAVED_TREE (fndecl) = outer_bind;
      body = outer_bind;
      env.set_outermost_bind (outer_bind);
    }

  /* Whether a RETURN_EXPR needs to be tracked at all is known upfront
     (a pure existence query over the pristine specifier list -- same
     conveyor/is_ignored status as the body's copy, no identity-sharing
     concern since nothing is mutated here); the actual resolution of
     the postcondition itself always happens on the body's own embedded
     copy, via the POSTCONDITION_STMT case in oa_walk_stmt below.  */
  oa_return_tracking = oa_has_active_postcondition (fndecl);
  oa_return_all_provable = false;
  oa_return_seen = false;

  /* -fcontract-symbolic-runtime-checks (Mechanism B): active for this
     one top-level walk whenever the flag is on -- saved/restored (not
     just set unconditionally false at the end) for the same reason
     OA_ACTIVE_PROVENANCE below is: this function is the sole top-level
     driver, never itself nested, but the save/restore discipline is
     used uniformly throughout this file regardless.  */
  bool saved_symbolic_codegen = oa_symbolic_codegen_active;
  oa_symbolic_codegen_active = flag_contract_symbolic_runtime_checks;

  /* -fcontract-conveyor-proof-provenance: arm the provenance side-table
     for this one function's walk, mirroring oa_walk_function_calls's own
     save/restore of oa_call_site_callback -- see oa_active_provenance's
     own comment.  Only ever non-NULL here (never nested: this function
     is the sole top-level driver, not itself reentrant), but save/
     restore defensively anyway, the same discipline used everywhere
     else in this file for this kind of cross-cutting optional state.  */
  oa_provenance_env prov_env;
  oa_provenance_env *saved_provenance = oa_active_provenance;
  oa_active_provenance
    = (flag_contract_conveyor_proofs && flag_contract_conveyor_proof_provenance)
      ? &prov_env : NULL;

  oa_walk_stmt (&body, env);

  oa_active_provenance = saved_provenance;
  oa_return_tracking = false;
  oa_symbolic_codegen_active = saved_symbolic_codegen;
}

/* Top-level entry point, called from finish_function alongside
   check_conveyor_function_body, at the same pre-genericize timing.  */

void
resolve_object_address_in_function (tree fndecl)
{
  oa_resolve_object_address_in_function_1 (fndecl);
}

/* A standalone plugin's own entry point (see
   .claude/plans/stateless-jumping-shore.md): identical walk, but with
   CALLBACK armed so it additionally observes every call site
   oa_scan_calls_in_expr finds during that walk, in program order, with
   the environment as it stands at that exact point -- including any
   facts already established by an earlier call's postcondition in the
   same straight-line sequence, since that chaining already happens
   unconditionally as part of the walk (see the INIT_EXPR/MODIFY_EXPR
   case in oa_walk_stmt).  Never invoked from anywhere in the compiler
   itself; CALLBACK is always NULL when resolve_object_address_in_function
   runs, so that mandatory pass is entirely unaffected.  */

void
oa_walk_function_calls (tree fndecl,
			 void (*callback) (tree, tree, oa_analysis_env *, void *),
			 void *data)
{
  void (*saved_callback) (tree, tree, oa_env *, void *) = oa_call_site_callback;
  void *saved_data = oa_call_site_callback_data;

  oa_call_site_callback
    = reinterpret_cast<void (*) (tree, tree, oa_env *, void *)> (callback);
  oa_call_site_callback_data = data;

  oa_resolve_object_address_in_function_1 (fndecl);

  oa_call_site_callback = saved_callback;
  oa_call_site_callback_data = saved_data;
}

/* Thin wrapper over oa_collect_conjuncts, for a plugin (see
   .claude/plans/stateless-jumping-shore.md).  */

void
oa_collect_conjuncts_public (tree *cond, vec<tree *> *out)
{
  oa_collect_conjuncts (cond, out);
}

/* Thin wrapper over oa_contract_conveyor_active_p, for a plugin.  */

bool
oa_contract_conveyor_active_public (tree contract, tree owner_fn)
{
  return oa_contract_conveyor_active_p (contract, owner_fn);
}

/* If OP is (a) a bare PARM_DECL, or (b) a call through an implicit,
   single-argument conversion operator (DECL_CONV_FN_P) whose own
   receiver is a PARM_DECL -- e.g. 'q.operator int()', the shape a
   class-typed parameter gets wrapped in wherever it's used in an
   arithmetic comparison -- return that underlying PARM_DECL;
   otherwise NULL_TREE.  Purely structural: this never inspects the
   PARM_DECL's own type or value, only *which* parameter a comparison
   operand refers to -- see oa_match_comparison_against_param's own
   comment for why that distinction matters.

   ALLOW_VAR_DECL (default false, preserving every existing caller's
   behavior byte-for-byte): also accept a bare VAR_DECL, not just a
   PARM_DECL. Only oa_match_type_bounded_comparison (below) passes
   true, for resolving a *local control-flow refinement's* own subject
   decl (e.g. a for-loop's own induction variable) -- never appropriate
   for oa_match_comparison_against_param's own callers, which parse a
   *callee's declared contract text* and need each side to be
   positionally substitutable with a caller's actual argument at a call
   site, something only ever true of a PARM_DECL.  */

static tree
oa_underlying_param_operand (tree op, bool allow_var_decl = false)
{
  /* A contract condition's own access to a by-value parameter is
     wrapped in ordinary value-preserving conversions (NOP_EXPR/
     VIEW_CONVERT_EXPR, to present it as const-qualified -- see oa_get_
     range's own identical comment) -- found by direct testing to be
     necessary as soon as the same function also has a postcondition
     naming one of its own by-value parameters (which forces this
     const-presentation view onto the precondition's own copy of the
     same parameters too, not just the postcondition's).  oa_strip_
     to_relational_operand handles both that and a class-typed
     parameter's own implicit conversion operator/by-value copy
     materialization in one shared step.  */
  op = oa_strip_to_relational_operand (op);
  if (TREE_CODE (op) == PARM_DECL || (allow_var_decl && VAR_P (op)))
    return op;
  return NULL_TREE;
}

/* Recognize CONJUNCT as "paramA OP paramB", where *both* sides are
   PARM_DECLs of the same callee (via oa_underlying_param_operand just
   above) -- e.g. 'x < q' where q is another of the callee's own
   parameters, of any type, not a literal. Deliberately does NOT try
   to resolve paramB to any compile-time value: unlike oa_match_simple_
   comparison (which requires an already-literal INTEGER_CST), this
   recognizes a genuine *relational* fact between two parameters,
   tracked and consulted symbolically by its own caller (oa_env's
   relational_get/set) the same way a named-predicate fact like
   'is_opened(this)' is tracked without ever evaluating is_opened --
   paramB's actual value is never needed, only whether the same
   relationship holds between whatever a specific call site's own
   substituted arguments turn out to be. Kept entirely separate from
   oa_match_simple_comparison, which self-trust's own range-fact
   seeding and the exported plugin API also rely on returning an
   actual literal -- broadening that function's own contract instead
   would force every existing caller to handle an unresolved "other
   parameter" result it has no way to act on.  Exported so
   contracts-gimple.cc's own built-in GIMPLE-pass engine can reuse the
   exact same shape recognition, the same way it already reuses
   is_object_address_call_p/oa_nonzero_conjunct_p.  */

bool
oa_match_comparison_against_param (tree conjunct, tree *param_out,
				    tree_code *code_out, tree *other_out)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  enum tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return false;

  tree op0 = oa_strip_to_relational_operand (TREE_OPERAND (c, 0));
  tree op1 = oa_strip_to_relational_operand (TREE_OPERAND (c, 1));

  /* Both sides go through oa_underlying_param_operand, not just one --
     a bare PARM_DECL passes through it unchanged, so this is a strict
     generalization that also covers the case where *both* sides are
     class-typed parameters each reached via their own conversion
     operator (e.g. 'x < y' with both x and y convertible-to-int).  */
  tree param, other;
  bool flipped;
  if ((param = oa_underlying_param_operand (op0))
      && (other = oa_underlying_param_operand (op1)))
    flipped = false;
  else if ((param = oa_underlying_param_operand (op1))
	   && (other = oa_underlying_param_operand (op0)))
    flipped = true;
  else
    return false;

  /* A parameter compared against itself isn't a useful relation (and
     would otherwise self-establish trivially).  */
  if (param == other)
    return false;

  if (flipped)
    switch (code)
      {
      case LT_EXPR: code = GT_EXPR; break;
      case LE_EXPR: code = GE_EXPR; break;
      case GT_EXPR: code = LT_EXPR; break;
      case GE_EXPR: code = LE_EXPR; break;
      default: break;
      }

  *param_out = param;
  *code_out = code;
  *other_out = other;
  return true;
}

/* D4324, item 8's overflow check: recognize CONJUNCT as "DECL OP other"
   or "other OP DECL", where DECL is a bare PARM_DECL or VAR_DECL (via
   oa_underlying_param_operand, with its own ALLOW_VAR_DECL argument set
   -- see that function's own comment for why only this caller passes
   true) and
   OTHER is *any* expression at all of an INTEGRAL_TYPE_P no wider than
   DECL's own type (TYPE_PRECISION/TYPE_UNSIGNED) -- deliberately never
   resolved, stripped for identity, or inspected any further. Unlike
   oa_match_comparison_against_param immediately above, OTHER need not
   be a decl, let alone a PARM_DECL: a bare comparison ('i < n'), a call
   ('i < v.size ()'), or any other expression shape are all recognized
   identically, since the only thing this needs from OTHER is its type
   -- see oa_type_bound_fact's own comment for why. CODE_OUT is oriented
   so the returned relation always reads "DECL CODE_OUT other", flipping
   analogously to oa_match_comparison_against_param's own FLIPPED
   handling when OTHER appears on the left.

   Called from oa_refine_single_comparison, mutually exclusive with the
   other matchers there by construction (a conjunct already recognized
   as "param OP param"/"param OP call ()"/etc. can still separately
   satisfy this one too -- e.g. 'i < n' matches both this and, once i is
   itself a PARM_DECL, oa_match_comparison_against_param -- so both
   facts get established side by side, which is fine: they answer
   different questions and neither invalidates the other).  */

static bool
oa_match_type_bounded_comparison (tree conjunct, tree *decl_out,
				    tree_code *code_out)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  enum tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR && code != GE_EXPR)
    return false;

  tree op0 = oa_strip_to_relational_operand (TREE_OPERAND (c, 0));
  tree op1 = oa_strip_to_relational_operand (TREE_OPERAND (c, 1));

  tree decl, other;
  bool flipped;
  if ((decl = oa_underlying_param_operand (op0, /*allow_var_decl=*/true)))
    {
      other = op1;
      flipped = false;
    }
  else if ((decl = oa_underlying_param_operand (op1, /*allow_var_decl=*/true)))
    {
      other = op0;
      flipped = true;
    }
  else
    return false;

  if (!INTEGRAL_TYPE_P (TREE_TYPE (other))
      || TYPE_PRECISION (TREE_TYPE (other)) > TYPE_PRECISION (TREE_TYPE (decl))
      || (TYPE_PRECISION (TREE_TYPE (other)) == TYPE_PRECISION (TREE_TYPE (decl))
	  && !TYPE_UNSIGNED (TREE_TYPE (decl)) && TYPE_UNSIGNED (TREE_TYPE (other))))
    return false;

  if (flipped)
    switch (code)
      {
      case LT_EXPR: code = GT_EXPR; break;
      case LE_EXPR: code = GE_EXPR; break;
      case GT_EXPR: code = LT_EXPR; break;
      case GE_EXPR: code = LE_EXPR; break;
      default: gcc_unreachable ();
      }

  *decl_out = decl;
  *code_out = code;
  return true;
}

/* Recognize CONJUNCT as "RESULT_ID OP other", where RESULT_ID is a
   postcondition's own already-known return-value binder (POSTCONDITION_
   IDENTIFIER, typically a VAR_DECL, not a PARM_DECL -- unlike oa_match_
   comparison_against_param immediately above, this takes RESULT_ID as a
   parameter rather than discovering it, since it isn't itself
   recognized by that function's own "bare PARM_DECL" check) and OTHER
   is one of the postcondition-owning function's own *parameters* (via
   oa_underlying_param_operand, shared with oa_match_comparison_against_
   param above). CODE_OUT/OTHER_OUT are oriented so the returned
   relation always reads "RESULT_ID CODE_OUT OTHER", flipping if the
   postcondition itself wrote it the other way around (e.g. 'q > r').
   The item-6 counterpart of oa_match_comparison_against_param, used by
   oa_establish_relational_from_call below to let a callee's own
   postcondition establish a relational fact for its caller's own
   assigned-to decl -- see that function's own comment. Exported (not
   static) so contracts-gimple.cc's own built-in GIMPLE-pass engine can
   reuse the exact same shape recognition for its own item-6 support.  */

bool
oa_match_result_relation (tree conjunct, tree result_id, tree_code *code_out,
			   tree *other_out)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  enum tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return false;

  /* RESULT_ID itself may be reached through its own implicit conversion
     operator too (e.g. a class-typed return value with 'operator
     int() const'), the same way the OTHER side already is via
     oa_underlying_param_operand -- oa_strip_to_relational_operand
     already applies that same lookthrough, so strip that before
     comparing against RESULT_ID by identity.  */
  tree op0 = oa_strip_to_relational_operand (TREE_OPERAND (c, 0));
  tree op1 = oa_strip_to_relational_operand (TREE_OPERAND (c, 1));

  tree other;
  bool flipped;
  if (op0 == result_id && (other = oa_underlying_param_operand (op1)))
    flipped = false;
  else if (op1 == result_id && (other = oa_underlying_param_operand (op0)))
    flipped = true;
  else
    return false;

  if (flipped)
    switch (code)
      {
      case LT_EXPR: code = GT_EXPR; break;
      case LE_EXPR: code = GE_EXPR; break;
      case GT_EXPR: code = LT_EXPR; break;
      case GE_EXPR: code = LE_EXPR; break;
      default: break;
      }

  *code_out = code;
  *other_out = other;
  return true;
}

/* The call analogue of oa_match_result_relation immediately above:
   "RESULT_ID OP RECEIVER.CALLEE ()" (e.g. 'post<ctrl>(r: r < this->
   size ())'), where CALLEE is a DECL_DECLARED_CONVEYOR_P accessor
   rather than another of the postcondition-owning function's own
   parameters.  Used by oa_call_postcondition_range_p to compose a
   concrete range for a call's own result from the *caller's* own
   already-established call-range fact for the substituted receiver --
   see that function's own comment for the composition itself.  */

bool
oa_match_result_call_relation (tree conjunct, tree result_id, tree_code *code_out,
				 tree *rhs_receiver_out, tree *rhs_callee_out,
				 bool allow_symbolic_accessor)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  enum tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return false;

  tree op0 = oa_strip_to_relational_operand (TREE_OPERAND (c, 0));
  tree op1 = oa_strip_to_relational_operand (TREE_OPERAND (c, 1));

  tree receiver, callee;
  bool flipped;
  if (op0 == result_id
      && oa_underlying_call_range_operand (op1, &receiver, &callee,
					    allow_symbolic_accessor))
    flipped = false;
  else if (op1 == result_id
	   && oa_underlying_call_range_operand (op0, &receiver, &callee,
						 allow_symbolic_accessor))
    flipped = true;
  else
    return false;

  if (flipped)
    switch (code)
      {
      case LT_EXPR: code = GT_EXPR; break;
      case LE_EXPR: code = GE_EXPR; break;
      case GT_EXPR: code = LT_EXPR; break;
      case GE_EXPR: code = LE_EXPR; break;
      default: break;
      }

  *code_out = code;
  *rhs_receiver_out = receiver;
  *rhs_callee_out = callee;
  return true;
}

/* Eager counterpart of oa_call_postcondition_range_p's own call-range
   composition block: called from oa_walk_stmt's own INIT_EXPR/
   MODIFY_EXPR case, *before* oa_scan_calls_in_expr invalidates RHS's
   own exposed arguments, so LHS's range can still be composed from a
   fact oa_scan_calls_in_expr's own invalidation step is about to drop
   -- see oa_call_postcondition_range_p's own comment for the ordering
   problem this fixes, and contracts-gimple.cc's own cg_compose_call_
   result_range, which fixes the identical problem the identical way on
   the GIMPLE side. Sets ENV's own range for LHS directly and returns
   true when it does, so the caller (the ordinary, lazy oa_get_range-
   based assignment handling further down the same case, which by then
   finds nothing through oa_get_range -- the fact is gone) knows not to
   unconditionally invalidate LHS the way it otherwise would on "found
   nothing": found via direct testing that skipping this return value
   entirely (assuming "nothing else uses this fact, so nothing can
   clobber it") is wrong -- that unconditional invalidate immediately
   wipes what this function just set, the same statement, before
   control ever leaves oa_walk_stmt's own INIT_EXPR/MODIFY_EXPR case.
   Returns false (a no-op) for every other shape RHS could have (not a
   direct call, or a call whose postcondition doesn't use this exact
   shape) -- always safe to call unconditionally.  */

static bool
oa_compose_call_result_range (tree lhs, tree rhs, oa_env &env)
{
  rhs = STRIP_ANY_LOCATION_WRAPPER (rhs);
  if (TREE_CODE (rhs) != CALL_EXPR)
    return false;
  tree callee = cp_get_callee_fndecl_nofold (rhs);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return false;
  lhs = STRIP_ANY_LOCATION_WRAPPER (lhs);
  if (!VAR_P (lhs) && TREE_CODE (lhs) != PARM_DECL)
    return false;

  bool composed = false;
  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      if (!oa_contract_conveyor_active_p (contract, callee))
	continue;
      tree result_id = POSTCONDITION_IDENTIFIER (contract);
      if (!result_id)
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree_code rcode;
	  tree rhs_receiver, rhs_callee;
	  if (!oa_match_result_call_relation (*conjuncts[i], result_id, &rcode,
					       &rhs_receiver, &rhs_callee,
					       /*allow_symbolic_accessor=*/false)
	      || TREE_CODE (rhs_receiver) != PARM_DECL)
	    continue;
	  tree sub_receiver = oa_substitute_call_arg (callee, rhs, rhs_receiver);
	  if (!sub_receiver)
	    continue;
	  sub_receiver = oa_strip_conversion_call (sub_receiver);
	  tree identity;
	  if (!oa_object_identity_decl (sub_receiver, &identity)
	      && !oa_field_slot_identity (sub_receiver, env, &identity)
	      && !oa_array_slot_identity (sub_receiver, env, &identity)
	      && !oa_field_object_identity (sub_receiver, env, &identity))
	    continue;
	  identity = env.alias_find (identity);
	  oa_contract_field_range_fact established;
	  if (!env.contract_call_range_get (identity, rhs_callee, &established))
	    continue;

	  oa_range_fact &derived = established.range;
	  oa_range_fact refined;
	  if (!env.range_get (lhs, &refined))
	    {
	      refined.base = NULL_TREE;
	      refined.has_lo = refined.has_hi = false;
	    }
	  switch (rcode)
	    {
	    case LT_EXPR:
	      if (derived.has_hi
		  && (!refined.has_hi || derived.hi - 1 < refined.hi))
		{ refined.has_hi = true; refined.hi = derived.hi - 1; }
	      break;
	    case LE_EXPR:
	      if (derived.has_hi && (!refined.has_hi || derived.hi < refined.hi))
		{ refined.has_hi = true; refined.hi = derived.hi; }
	      break;
	    case GT_EXPR:
	      if (derived.has_lo
		  && (!refined.has_lo || derived.lo + 1 > refined.lo))
		{ refined.has_lo = true; refined.lo = derived.lo + 1; }
	      break;
	    case GE_EXPR:
	      if (derived.has_lo && (!refined.has_lo || derived.lo > refined.lo))
		{ refined.has_lo = true; refined.lo = derived.lo; }
	      break;
	    case EQ_EXPR:
	      if (derived.has_lo && (!refined.has_lo || derived.lo > refined.lo))
		{ refined.has_lo = true; refined.lo = derived.lo; }
	      if (derived.has_hi && (!refined.has_hi || derived.hi < refined.hi))
		{ refined.has_hi = true; refined.hi = derived.hi; }
	      break;
	    default:
	      break;
	    }
	  if (refined.has_lo || refined.has_hi)
	    {
	      env.range_set (lhs, refined);
	      composed = true;
	    }
	}
    }
  return composed;
}

/* Item 6 for relational facts, the establish side: if RHS is (after
   ordinary wrapper stripping) a call whose callee's own postcondition
   guarantees a relation between its own return value and one of its
   OTHER parameters (e.g. 'post<ctrl>(r: r < q)'), and that other
   parameter's own positional argument at THIS call resolves to a real
   decl, establish that relation for LHS in ENV -- e.g. 'int y = f (x,
   q);' with f's own postcondition above establishes "y < q" (q being
   THIS call's own substituted argument, not necessarily f's own q).
   Deliberately narrow, unlike oa_get_range's own much broader item 6:
   RHS must literally reduce to a CALL_EXPR, no arithmetic/IILE
   composition, matching the explicit scope decision recorded in
   .claude/plans/well-we-last-discussed-ethereal-duckling.md (a
   postcondition relating the return value to a parameter is a
   materially different, harder problem than a precondition doing the
   same, and this covers only the direct, single-hop case).  A no-op
   (ENV untouched) for anything else, including when neither opt-in
   prover nor a plugin is active at all -- relational facts are never
   part of the mandatory, always-on substrate.  Gated on OA_CALL_SITE_
   CALLBACK too, not just the two built-in flags -- matching oa_handle_
   call_symbolic_postcondition_establishment's own identical gating in
   oa_scan_calls_in_expr, for the same reason: a plugin driving oa_walk_
   function_calls needs this bookkeeping done on its behalf even with
   neither -fcontract-conveyor-proofs nor -fcontract-symbolic-proofs on.  */

static void
oa_establish_relational_from_call (tree lhs, tree rhs, oa_env &env)
{
  if (!flag_contract_conveyor_proofs && !flag_contract_symbolic_proofs
      && !oa_call_site_callback)
    return;
  if (rhs == NULL_TREE)
    return;

  tree expr = STRIP_ANY_LOCATION_WRAPPER (rhs);
  if (TREE_CODE (expr) != CALL_EXPR)
    return;
  tree callee = cp_get_callee_fndecl_nofold (expr);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL)
    return;

  for (tree as = get_fn_contract_specifiers (callee); as; as = TREE_CHAIN (as))
    {
      tree contract = CONTRACT_STATEMENT (as);
      if (!POSTCONDITION_P (contract))
	continue;
      bool conveyor_active = oa_contract_conveyor_active_p (contract, callee);
      bool symbolic_active = oa_contract_symbolic_active_p (contract, callee);
      if (!conveyor_active && !symbolic_active)
	continue;
      tree result_id = POSTCONDITION_IDENTIFIER (contract);
      if (!result_id || (!VAR_P (result_id) && TREE_CODE (result_id) != PARM_DECL))
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (cond == NULL_TREE || cond == error_mark_node)
	continue;

      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	{
	  tree_code code;
	  tree other_param;
	  if (!oa_match_result_relation (*conjuncts[i], result_id, &code,
					  &other_param))
	    continue;

	  tree substituted = oa_substitute_call_arg (callee, expr, other_param);
	  if (!substituted)
	    continue;
	  tree resolved = oa_strip_to_relational_operand (substituted);
	  if (!VAR_P (resolved) && TREE_CODE (resolved) != PARM_DECL)
	    continue;

	  env.relational_set (lhs, code, resolved, conveyor_active,
			       oa_range_fact_exact (0));
	  return;
	}
    }
}

/* Recognize CONJUNCT as the shape "param OP const" (or "const OP
   param", normalized so the returned CODE_OUT always reads left-to-
   right as "param CODE_OUT const"), for a plugin's own precondition-
   obligation checking (see .claude/plans/stateless-jumping-shore.md).
   Only a bare PARM_DECL is recognized for "param" -- the same
   restriction oa_handle_call_precondition_obligation's own
   is_object_address(param) matching already has, kept consistent here
   for the general comparison case.  Shares its shape-recognition logic
   with oa_refine_single_comparison, but in "just recognize" mode: no
   env is touched, and (unlike that function) there is no "asserted_true"
   direction to negate, since this is answering "what does this conjunct
   require," not "what does asserting it true establish."  */

bool
oa_match_simple_comparison (tree conjunct, tree *param_out, tree_code *code_out,
			    tree *const_val_out)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  enum tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return false;

  /* A class-typed parameter reached via its own implicit conversion
     operator (e.g. 'q.operator int() < 5') is recognized the same way
     a bare scalar-typed parameter already is -- oa_strip_conversion_
     call is a no-op for the already-bare case, so this is a strict
     generalization.  */
  tree op0 = oa_strip_conversion_call (STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0)));
  tree op1 = oa_strip_conversion_call (STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 1)));

  tree param, const_val;
  bool flipped;
  if (TREE_CODE (op0) == PARM_DECL && TREE_CODE (op1) == INTEGER_CST)
    param = op0, const_val = op1, flipped = false;
  else if (TREE_CODE (op1) == PARM_DECL && TREE_CODE (op0) == INTEGER_CST)
    param = op1, const_val = op0, flipped = true;
  else
    return false;

  if (flipped)
    switch (code)
      {
      case LT_EXPR: code = GT_EXPR; break;
      case LE_EXPR: code = GE_EXPR; break;
      case GT_EXPR: code = LT_EXPR; break;
      case GE_EXPR: code = LE_EXPR; break;
      default: break;
      }

  *param_out = param;
  *code_out = code;
  *const_val_out = const_val;
  return true;
}

/* The three-way answer a call-site precondition-obligation check
   actually needs (see .claude/plans/stateless-jumping-shore.md): is
   EXPR, under ENV's current facts, provably CMP CONST_VAL for every
   value it could take (OA_PROVEN_TRUE), provably CMP CONST_VAL for no
   value it could take (OA_PROVEN_FALSE), or is ENV's known range for
   EXPR (if any) insufficient to conclude either (OA_UNKNOWN, which also
   covers "no range fact known for EXPR at all")?  Built on oa_get_range's
   [lo,hi] interval -- a genuinely new query direction: oa_refine_single_
   comparison only ever *establishes* facts from an asserted-true
   condition, never needing a proven-false direction before this.  A
   range fact tied to a pointer's own array-base (BASE != NULL_TREE, see
   oa_range_fact's own comment) isn't a plain numeric interval comparable
   against CONST_VAL the same way, so that case is conservatively
   OA_UNKNOWN too.

   Shared between the public, plugin-facing oa_env_check_comparison below
   and the compiler's own built-in oa_handle_call_conveyor_proof_obligation
   (-fcontract-conveyor-proofs), so both go through one implementation.  */

static oa_proof_result
oa_env_check_comparison_1 (oa_env &env, tree expr, tree_code cmp,
			    tree const_val)
{
  oa_range_fact fact;
  if (!oa_get_range (expr, env, &fact) || fact.base != NULL_TREE)
    return OA_UNKNOWN;

  widest_int val = wi::to_widest (const_val);

  switch (cmp)
    {
    case GT_EXPR:
      if (fact.has_lo && fact.lo > val) return OA_PROVEN_TRUE;
      if (fact.has_hi && fact.hi <= val) return OA_PROVEN_FALSE;
      return OA_UNKNOWN;
    case GE_EXPR:
      if (fact.has_lo && fact.lo >= val) return OA_PROVEN_TRUE;
      if (fact.has_hi && fact.hi < val) return OA_PROVEN_FALSE;
      return OA_UNKNOWN;
    case LT_EXPR:
      if (fact.has_hi && fact.hi < val) return OA_PROVEN_TRUE;
      if (fact.has_lo && fact.lo >= val) return OA_PROVEN_FALSE;
      return OA_UNKNOWN;
    case LE_EXPR:
      if (fact.has_hi && fact.hi <= val) return OA_PROVEN_TRUE;
      if (fact.has_lo && fact.lo > val) return OA_PROVEN_FALSE;
      return OA_UNKNOWN;
    case EQ_EXPR:
      if (fact.has_lo && fact.has_hi && fact.lo == val && fact.hi == val)
	return OA_PROVEN_TRUE;
      if ((fact.has_lo && fact.lo > val) || (fact.has_hi && fact.hi < val))
	return OA_PROVEN_FALSE;
      return OA_UNKNOWN;
    default:
      return OA_UNKNOWN;
    }
}

/* -fcontract-conveyor-proofs: is ARG's established interval a *subset*
   of REQ (every value ARG could take also satisfies REQ ->
   OA_PROVEN_TRUE, the callee's combined range precondition is fully
   discharged), or is it *disjoint* from REQ (no value ARG could take
   satisfies REQ -> OA_PROVEN_FALSE, a genuine, confirmed violation), or
   does neither hold (the two intervals genuinely overlap only
   partially, so whether the actual runtime value -- known only to lie
   somewhere in ARG's interval -- satisfies REQ depends on exactly which
   value that turns out to be; OA_UNKNOWN is the mathematically correct
   answer here, not just a conservative fallback: no sound analysis of
   two overlapping-but-not-nested intervals could claim more)?  REQ
   missing one side (has_lo or has_hi false) means that side is
   unconstrained -- e.g. a precondition of "x > 0" alone leaves REQ.hi
   unset, matching oa_env_check_comparison_1's own single-sided
   handling exactly (this function is a strict generalization of it: for
   a REQ built from a single conjunct, the two are equivalent).  A
   pointer/array-base fact (ARG.base != NULL_TREE) isn't a plain integer
   interval, so that case is conservatively OA_UNKNOWN too.  */

static oa_proof_result
oa_range_subsumption_result (oa_range_fact &arg, oa_range_fact &req)
{
  if (arg.base != NULL_TREE)
    return OA_UNKNOWN;

  bool subsumed
    = (!req.has_lo || (arg.has_lo && arg.lo >= req.lo))
      && (!req.has_hi || (arg.has_hi && arg.hi <= req.hi));
  if (subsumed)
    return OA_PROVEN_TRUE;

  bool disjoint
    = (req.has_hi && arg.has_lo && arg.lo > req.hi)
      || (req.has_lo && arg.has_hi && arg.hi < req.lo);
  if (disjoint)
    return OA_PROVEN_FALSE;

  return OA_UNKNOWN;
}

/* -fcontract-conveyor-proofs: EXPR's own version of the above -- looks
   up EXPR's established range via oa_get_range first (mirroring
   oa_env_check_comparison_1's own first step), then defers to
   oa_range_subsumption_result.  */

static oa_proof_result
oa_env_check_range_subsumption (oa_env &env, tree expr, oa_range_fact &req)
{
  oa_range_fact fact;
  if (!oa_get_range (expr, env, &fact))
    return OA_UNKNOWN;
  return oa_range_subsumption_result (fact, req);
}

/* The three-way answer a relational obligation's own consult needs: is
   SUBSTITUTED_PARAM provably REQUIRED_CODE SUBSTITUTED_OTHER, given
   ENV's current facts? Tries, in order: both sides already ordinary
   compile-time literals (plain constant folding, oa_relational_
   literal_holds -- OA_PROVEN_TRUE or OA_PROVEN_FALSE, never UNKNOWN,
   since two concrete numbers always decide the question outright); an
   established relational fact (oa_get_relational, which also reaches
   item 6 via oa_establish_relational_from_call's own earlier work)
   whose own code implies REQUIRED_CODE and whose own RHS matches
   SUBSTITUTED_OTHER, filtered by REQUIRE_CONVEYOR the same one-way-
   trust way every other shared-substrate fact is. Shared by both
   oa_handle_call_conveyor_proof_obligation/oa_handle_call_symbolic_
   precondition_obligation (which call this directly, keeping their own
   diagnostic emission local) and, via the plugin-facing oa_env_check_
   relational_fact below, any plugin driving oa_walk_function_calls --
   the same DRY relationship oa_env_check_range_subsumption above has
   with oa_env_check_comparison.  */

/* D4324 Commit 2 (generalized from a single widest_int to an interval
   in Commit 4): is OFFSET compatible with (i.e., doesn't invalidate) an
   established fact's entailment of REQUIRED_CODE? An established
   '(param - offset) CODE rhs' does not entail 'param CODE rhs'
   unconditionally once OFFSET isn't exactly 0 -- e.g. 'i < size ()'
   does not entail 'i + 1 < size ()' (i could equal size () - 1) -- so
   this must be checked alongside oa_relational_code_implies, not
   instead of it. A non-positive offset (the value was decremented, or
   left unchanged, since establishment) only ever tightens an upper
   bound (LT_EXPR/LE_EXPR) and loosens a lower one; a non-negative
   offset does the reverse for a lower bound (GT_EXPR/GE_EXPR); EQ_EXPR
   needs an exact, unshifted match. Checked against REQUIRED_CODE (what
   must ultimately hold), not FACT.CODE (which oa_relational_code_
   implies already independently verifies is strong enough).

   OFFSET is now an interval, not a single value (Commit 4: the shift
   amount can come from a second tracked variable's own established
   range, not just a literal) -- soundness requires using OFFSET's own
   *worst case* in the direction that could break entailment: an upper
   bound needs the interval's own upper end (HAS_HI/HI) to still be
   <= 0, a lower bound needs its own lower end (HAS_LO/LO) to still be
   >= 0, and an unknown bound in the relevant direction (HAS_HI/HAS_LO
   false) can never be proven safe.  A fixed literal K is just this
   interval's own degenerate has_lo && has_hi && lo == hi == K case, so
   this is a strict generalization of Commit 2's own plain sign check --
   nothing about that commit's own tests changes.  */

static bool
oa_offset_compatible_with_code (const oa_range_fact &offset,
				 tree_code required_code)
{
  switch (required_code)
    {
    case LT_EXPR:
    case LE_EXPR:
      return offset.has_hi && offset.hi <= 0;
    case GT_EXPR:
    case GE_EXPR:
      return offset.has_lo && offset.lo >= 0;
    case EQ_EXPR:
      return offset.has_lo && offset.has_hi && offset.lo == 0
	     && offset.hi == 0;
    default:
      return false;
    }
}

static oa_proof_result
oa_env_check_relational_fact_1 (oa_env &env, tree substituted_param,
				 tree_code required_code, tree substituted_other,
				 bool require_conveyor)
{
  tree stripped_param = oa_strip_to_relational_operand (substituted_param);
  tree stripped_other = oa_strip_to_relational_operand (substituted_other);

  if (stripped_param && stripped_other
      && TREE_CODE (stripped_param) == INTEGER_CST
      && TREE_CODE (stripped_other) == INTEGER_CST)
    return oa_relational_literal_holds (required_code, stripped_param,
					 stripped_other)
	   ? OA_PROVEN_TRUE : OA_PROVEN_FALSE;

  oa_relational_fact fact;
  if (oa_get_relational (substituted_param, env, &fact)
      && oa_relational_code_implies (fact.code, required_code)
      && oa_offset_compatible_with_code (fact.offset, required_code)
      && (!require_conveyor || fact.conveyor_established)
      && oa_strip_to_relational_operand (fact.rhs) == stripped_other)
    return OA_PROVEN_TRUE;

  /* Bounds-proving demo: no explicit linked fact -- but both sides' own
     independently-tracked scalar ranges might still settle this
     numerically (e.g. both are plain locals, never compared to each
     other by any if-condition or self-trust). Plain ranges carry no
     provenance tag at all (see oa_refine_scalar_range_only's own
     comment: trusted uniformly once established, regardless of
     REQUIRE_CONVEYOR), so no extra provenance check is needed here.  */
  oa_range_fact param_range, other_range;
  if (oa_get_range (substituted_param, env, &param_range)
      && oa_get_range (substituted_other, env, &other_range))
    {
      enum oa_range_subsumption_result r
	= oa_range_pair_relation (param_range, required_code, other_range);
      if (r == OA_RANGE_SUBSUMED)
	return OA_PROVEN_TRUE;
      if (r == OA_RANGE_DISJOINT)
	return OA_PROVEN_FALSE;
    }

  return OA_UNKNOWN;
}

/* Public, plugin-facing wrapper over oa_env_check_relational_fact_1,
   the relational-fact analogue of oa_env_check_comparison immediately
   below.  SUBSTITUTED_PARAM/SUBSTITUTED_OTHER are the plugin's own
   already-positionally-substituted call arguments (the same
   oa_substitute_call_arg-based substitution the plugin already does
   for oa_env_check_comparison); REQUIRE_CONVEYOR is the same one-way-
   trust parameter oa_env_check_predicate_fact's own plugin-facing
   wrapper already exposes.  */

oa_proof_result
oa_env_check_relational_fact (oa_analysis_env *env, tree substituted_param,
			       tree_code required_code, tree substituted_other,
			       bool require_conveyor)
{
  return oa_env_check_relational_fact_1 (*reinterpret_cast<oa_env *> (env),
					  substituted_param, required_code,
					  substituted_other, require_conveyor);
}

/* Does an established call-relational fact of code ESTABLISHED_CODE and
   shift OFFSET (see oa_call_relational_fact's own OFFSET comment: the
   fact holds for "(PARAM - OFFSET) ESTABLISHED_CODE RECEIVER.CALLEE ()")
   *contradict* REQUIRED_CODE ("PARAM REQUIRED_CODE RECEIVER.CALLEE ()",
   offset implicitly 0)? Complements oa_relational_code_implies (which
   only ever answers "does established still imply required," i.e. only
   ever proves TRUE): this answers the opposite question, needed to
   prove FALSE directly from a symbolic call-relational fact whose own
   receiver/callee were never independently pinned to an absolute
   number -- the only other route to FALSE, the range-vs-range fallback
   just below this function's own sole caller, needs exactly that and so
   can't fire here. E.g. an established 'idx > size () + 10' (GT_EXPR,
   offset +10) flatly contradicts a required 'idx < size ()' (LT_EXPR),
   regardless of what size () actually is, since size ()+10 can never be
   less than size () itself -- found missing via direct testing: a
   margin fact shifted arbitrarily far past a required upper bound still
   only ever produced "cannot verify," never "provably violates," since
   oa_relational_code_implies alone has no way to notice this.

   Reduces both sides to a single shared variable D = PARAM - RECEIVER.
   CALLEE () (so "ESTABLISHED_CODE, OFFSET" becomes "D ESTABLISHED_CODE
   OFFSET" and REQUIRED_CODE becomes "D REQUIRED_CODE 0"), converts each
   to an inclusive [lo, hi] bound via the same code-to-bound
   normalization oa_tighten_range_bound already uses for a plain decl's
   own range, and checks for empty intersection. OFFSET's own
   uncertainty (Commit 4: it may be an interval, not a single point) is
   resolved to whichever endpoint makes the established side as
   *permissive* as possible before checking for contradiction --
   OFFSET.hi for an upper-bound-defining code (LT/LE), OFFSET.lo for a
   lower-bound-defining one (GT/GE) -- so a genuinely uncertain shift
   only ever loses a real contradiction, never invents a spurious one;
   missing the needed bound (!has_hi/!has_lo) declines (returns false)
   rather than guessing, the same discipline used throughout this pass.  */

static bool
oa_call_relational_contradicts_p (tree_code established_code,
				    const oa_range_fact &offset,
				    tree_code required_code)
{
  oa_range_fact est_d;
  est_d.base = NULL_TREE;
  est_d.has_lo = est_d.has_hi = false;

  switch (established_code)
    {
    case LT_EXPR:
    case LE_EXPR:
      if (!offset.has_hi)
	return false;
      oa_tighten_range_bound (est_d, established_code, offset.hi);
      break;
    case GT_EXPR:
    case GE_EXPR:
      if (!offset.has_lo)
	return false;
      oa_tighten_range_bound (est_d, established_code, offset.lo);
      break;
    case EQ_EXPR:
      if (!offset.has_lo || !offset.has_hi)
	return false;
      est_d.has_lo = est_d.has_hi = true;
      est_d.lo = offset.lo;
      est_d.hi = offset.hi;
      break;
    default:
      return false;
    }

  oa_range_fact req_d;
  req_d.base = NULL_TREE;
  req_d.has_lo = req_d.has_hi = false;
  switch (required_code)
    {
    case LT_EXPR:
    case LE_EXPR:
    case GT_EXPR:
    case GE_EXPR:
    case EQ_EXPR:
      oa_tighten_range_bound (req_d, required_code, 0);
      break;
    default:
      return false;
    }

  /* Two inclusive [lo, hi] integer ranges are disjoint iff one's hi is
     below the other's lo (in either direction); a bound that's still
     unknown (!has_*) on either side can never itself prove
     disjointness.  */
  if (est_d.has_hi && req_d.has_lo && est_d.hi < req_d.lo)
    return true;
  if (req_d.has_hi && est_d.has_lo && req_d.hi < est_d.lo)
    return true;
  return false;
}

/* The call analogue of oa_env_check_relational_fact_1 immediately
   above, for a required relation against a call (SUBSTITUTED_RHS_
   RECEIVER.SUBSTITUTED_RHS_CALLEE ()) rather than another parameter.
   No "both sides already literal" fast path here (unlike that
   function): a call can never fold to a literal at this level, so
   there is nothing to fold.  */

static oa_proof_result
oa_env_check_call_relational_fact_1 (oa_env &env, tree substituted_param,
				       tree_code required_code,
				       tree substituted_rhs_receiver,
				       tree substituted_rhs_callee,
				       bool require_conveyor)
{
  oa_call_relational_fact fact;
  bool have_fact = oa_get_call_relational (substituted_param, env, &fact);
  bool same_accessor
    = have_fact
      && fact.rhs_callee == substituted_rhs_callee
      && (oa_strip_to_relational_operand (fact.rhs_receiver)
	  == oa_strip_to_relational_operand (substituted_rhs_receiver));

  if (have_fact && same_accessor
      && oa_relational_code_implies (fact.code, required_code)
      && oa_offset_compatible_with_code (fact.offset, required_code)
      && (!require_conveyor || fact.conveyor_established))
    return OA_PROVEN_TRUE;

  if (have_fact && same_accessor
      && (!require_conveyor || fact.conveyor_established)
      && oa_call_relational_contradicts_p (fact.code, fact.offset,
					     required_code))
    return OA_PROVEN_FALSE;

  /* Bounds-proving demo (see .claude/plans/lazy-stirring-pearl.md): no
     explicit linked fact -- but SUBSTITUTED_PARAM's own independently-
     tracked scalar range and SUBSTITUTED_RHS_RECEIVER.SUBSTITUTED_RHS_
     CALLEE ()'s own independently-established call-range fact (e.g. from
     an earlier call's own postcondition) might still settle this
     numerically, even though nothing ever explicitly linked the two
     (no if-condition, no matching self-trust).  */
  oa_range_fact param_range;
  if (oa_get_range (substituted_param, env, &param_range))
    {
      tree stripped_receiver = oa_strip_conversion_call (substituted_rhs_receiver);
      tree identity;
      if (oa_object_identity_decl (stripped_receiver, &identity)
	  || oa_field_slot_identity (stripped_receiver, env, &identity)
	  || oa_array_slot_identity (stripped_receiver, env, &identity)
	  || oa_field_object_identity (stripped_receiver, env, &identity))
	{
	  identity = env.alias_find (identity);
	  oa_contract_field_range_fact callee_fact;
	  if (env.contract_call_range_get (identity, substituted_rhs_callee,
					    &callee_fact)
	      && (!require_conveyor || callee_fact.conveyor_established))
	    {
	      enum oa_range_subsumption_result r
		= oa_range_pair_relation (param_range, required_code,
					   callee_fact.range);
	      if (r == OA_RANGE_SUBSUMED)
		return OA_PROVEN_TRUE;
	      if (r == OA_RANGE_DISJOINT)
		return OA_PROVEN_FALSE;
	    }
	}
    }

  return OA_UNKNOWN;
}

/* Public, plugin-facing wrapper over oa_env_check_call_relational_
   fact_1 immediately above, mirroring oa_env_check_relational_fact's
   own identical role.  */

oa_proof_result
oa_env_check_call_relational_fact (oa_analysis_env *env, tree substituted_param,
				     tree_code required_code,
				     tree substituted_rhs_receiver,
				     tree substituted_rhs_callee,
				     bool require_conveyor)
{
  return oa_env_check_call_relational_fact_1 (*reinterpret_cast<oa_env *> (env),
					       substituted_param, required_code,
					       substituted_rhs_receiver,
					       substituted_rhs_callee,
					       require_conveyor);
}

/* The call-vs-call analogue of oa_env_check_call_relational_fact_1
   immediately above, for a required relation between two calls
   (SUBSTITUTED_LHS_RECEIVER.SUBSTITUTED_LHS_CALLEE () REQUIRED_CODE
   SUBSTITUTED_RHS_RECEIVER.SUBSTITUTED_RHS_CALLEE ()) rather than a
   bare parameter against a call. Unlike that function, this shape's own
   key is itself a call, so SUBSTITUTED_LHS_RECEIVER needs the same
   identity resolution oa_handle_call_conveyor_call_range_obligation's
   own consult loop already applies to its own receiver, before the
   lookup can happen at all.  */

static oa_proof_result
oa_env_check_call_call_relational_fact_1 (oa_env &env,
					    tree substituted_lhs_receiver,
					    tree substituted_lhs_callee,
					    tree_code required_code,
					    tree substituted_rhs_receiver,
					    tree substituted_rhs_callee,
					    bool require_conveyor)
{
  tree stripped = oa_strip_conversion_call (substituted_lhs_receiver);
  tree identity;
  if (!oa_object_identity_decl (stripped, &identity)
      && !oa_field_slot_identity (stripped, env, &identity)
      && !oa_array_slot_identity (stripped, env, &identity)
      && !oa_field_object_identity (stripped, env, &identity))
    return OA_UNKNOWN;
  identity = env.alias_find (identity);

  oa_call_call_relational_fact fact;
  if (env.call_call_relational_get (identity, substituted_lhs_callee, &fact)
      && oa_relational_code_implies (fact.code, required_code)
      && (!require_conveyor || fact.conveyor_established)
      && fact.rhs_callee == substituted_rhs_callee
      && (oa_strip_to_relational_operand (fact.rhs_receiver)
	  == oa_strip_to_relational_operand (substituted_rhs_receiver)))
    return OA_PROVEN_TRUE;

  /* Bounds-proving demo: no explicit linked fact -- but each side's own
     independently-established call-range fact might still settle this
     numerically (mirroring oa_env_check_call_relational_fact_1's own
     identical fallback, generalized to both sides being calls).  */
  oa_contract_field_range_fact lhs_fact;
  if (env.contract_call_range_get (identity, substituted_lhs_callee, &lhs_fact)
      && (!require_conveyor || lhs_fact.conveyor_established))
    {
      tree rhs_stripped = oa_strip_conversion_call (substituted_rhs_receiver);
      tree rhs_identity;
      if (oa_object_identity_decl (rhs_stripped, &rhs_identity)
	  || oa_field_slot_identity (rhs_stripped, env, &rhs_identity)
	  || oa_array_slot_identity (rhs_stripped, env, &rhs_identity)
	  || oa_field_object_identity (rhs_stripped, env, &rhs_identity))
	{
	  rhs_identity = env.alias_find (rhs_identity);
	  oa_contract_field_range_fact rhs_fact;
	  if (env.contract_call_range_get (rhs_identity, substituted_rhs_callee,
					    &rhs_fact)
	      && (!require_conveyor || rhs_fact.conveyor_established))
	    {
	      enum oa_range_subsumption_result r
		= oa_range_pair_relation (lhs_fact.range, required_code,
					   rhs_fact.range);
	      if (r == OA_RANGE_SUBSUMED)
		return OA_PROVEN_TRUE;
	      if (r == OA_RANGE_DISJOINT)
		return OA_PROVEN_FALSE;
	    }
	}
    }

  return OA_UNKNOWN;
}

/* Public, plugin-facing wrapper over oa_env_check_call_call_relational_
   fact_1 immediately above, mirroring oa_env_check_call_relational_
   fact's own identical role.  */

oa_proof_result
oa_env_check_call_call_relational_fact (oa_analysis_env *env,
					  tree substituted_lhs_receiver,
					  tree substituted_lhs_callee,
					  tree_code required_code,
					  tree substituted_rhs_receiver,
					  tree substituted_rhs_callee,
					  bool require_conveyor)
{
  return oa_env_check_call_call_relational_fact_1
    (*reinterpret_cast<oa_env *> (env), substituted_lhs_receiver,
     substituted_lhs_callee, required_code, substituted_rhs_receiver,
     substituted_rhs_callee, require_conveyor);
}

/* Public, plugin-facing wrapper over oa_env_check_comparison_1 -- see
   .claude/plans/stateless-jumping-shore.md.  ENV's dynamic type is always
   really oa_env (oa_analysis_env is an empty subclass with no added
   layout, the same cast idiom oa_walk_function_calls already uses).  */

oa_proof_result
oa_env_check_comparison (oa_analysis_env *env, tree expr, tree_code cmp,
			 tree const_val)
{
  return oa_env_check_comparison_1 (*reinterpret_cast<oa_env *> (env),
				     expr, cmp, const_val);
}

/* -fcontract-symbolic-runtime-checks: recognize a ptr->field OP const
   (or (*ptr).field OP const, the same shape after the front end
   desugars '->') conjunct -- the "reached through a persistent
   pointer" comparison case Mechanism A covers (see
   .claude/plans/stateless-jumping-shore.md); a bare by-value local
   compared directly, with no pointer indirection at all, does not
   match this shape (that shape is Mechanism B's, handled separately by
   oa_match_simple_comparison/oa_precondition_symbolic_range_p and
   oa_call_symbolic_range_p further below). Shares its
   OP/const-operand recognition with oa_match_simple_comparison, but
   requires the non-constant operand to be a COMPONENT_REF through an
   INDIRECT_REF, extracting the FIELD_DECL and the pointer
   sub-expression underneath separately rather than requiring a bare
   PARM_DECL.  */

static bool
oa_symbolic_comparison_conjunct_shape (tree conjunct, tree *field_out,
					tree *ptr_expr_out, tree_code *code_out,
					tree *const_val_out)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return false;

  tree op0 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
  tree op1 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 1));

  tree comp, const_val;
  bool flipped;
  if (TREE_CODE (op1) == INTEGER_CST)
    comp = op0, const_val = op1, flipped = false;
  else if (TREE_CODE (op0) == INTEGER_CST)
    comp = op1, const_val = op0, flipped = true;
  else
    return false;

  /* 'ptr->count' itself may be reached via a further implicit
     conversion operator (e.g. if count's own type has 'operator
     int() const') -- oa_strip_conversion_call returns the conversion's
     receiver object regardless of its shape, so the COMPONENT_REF
     check just below still applies to whatever's underneath.  */
  comp = oa_strip_conversion_call (STRIP_ANY_LOCATION_WRAPPER (comp));
  if (TREE_CODE (comp) != COMPONENT_REF)
    return false;
  tree field = TREE_OPERAND (comp, 1);
  if (TREE_CODE (field) != FIELD_DECL)
    return false;
  tree base = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (comp, 0));
  if (TREE_CODE (base) != INDIRECT_REF)
    return false;
  tree ptr_expr = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (base, 0));

  if (flipped)
    switch (code)
      {
      case LT_EXPR: code = GT_EXPR; break;
      case LE_EXPR: code = GE_EXPR; break;
      case GT_EXPR: code = LT_EXPR; break;
      case GE_EXPR: code = LE_EXPR; break;
      default: break;
      }

  *field_out = field;
  *ptr_expr_out = ptr_expr;
  *code_out = code;
  *const_val_out = const_val;
  return true;
}

/* Recognize CONJUNCT as "RECEIVER.ACCESSOR () OP const" (or "const OP
   RECEIVER.ACCESSOR ()", normalized so CODE_OUT always reads left-to-
   right as "call CODE_OUT const"), the call-range analogue of oa_
   symbolic_comparison_conjunct_shape immediately above -- e.g.
   'i < v.size ()'.  ACCESSOR must itself be DECL_DECLARED_CONVEYOR_P
   (forcing maybe_instantiate_conveyor first, exactly as build_over_
   call's own callee check already does, so a conveyor(auto) callee's
   per-specialization answer is resolved on demand) -- never DECL_
   DECLARED_SYMBOLIC_P: a 'symbolic' function has no definition at all
   (decl.cc's grokfndecl) and can never genuinely execute, so it has no
   real value this numeric range shape could ever compare against. A
   manifestly constant-expression call (e.g. std::array<T,N>::size())
   needs no handling here at all -- it's already folded to an INTEGER_
   CST long before this pass ever sees a CALL_EXPR.  RECEIVER_OUT is
   presented the way a call's own implicit-object argument actually
   appears in the tree -- typically a bare pointer PARM_DECL with no
   ADDR_EXPR (e.g. 'this' inside operator[]'s own 'this->size ()'), or
   an ADDR_EXPR of the real object (e.g. '&v' for 'v.size ()' at an
   ordinary call site) -- both handled by conditionally peeling a
   leading ADDR_EXPR, mirroring oa_strip_conversion_operator_call's own
   identical peel.  Only a non-static member function call with no
   extra arguments is recognized (RECEIVER.ACCESSOR()); a free-function
   call is out of scope for this shape.  */

/* Shared by oa_call_range_conjunct_shape below and oa_match_comparison_
   against_call further below: is OP (one side of a top-level
   comparison, not yet stripped) a call to an accessor with no extra
   arguments (RECEIVER.ACCESSOR())? If so, RECEIVER_OUT/CALLEE_OUT
   describe it -- RECEIVER_OUT presented the way a call's own implicit-
   object argument actually appears in the tree (see oa_call_range_
   conjunct_shape's own comment on that).

   The accessor must be DECL_DECLARED_CONVEYOR_P, or, when
   ALLOW_SYMBOLIC_ACCESSOR is true, may instead be DECL_DECLARED_
   SYMBOLIC_P. Either tag is what licenses treating two textual mentions
   of RECEIVER.ACCESSOR() as denoting the same value: conveyor via
   verified purity, symbolic by fiat (it's an axiom, no real body to
   disagree with). A plain, untagged accessor gives neither guarantee
   and is never accepted, regardless of ALLOW_SYMBOLIC_ACCESSOR -- this
   analysis never walks a callee's own definition to find out what it
   actually does. ALLOW_SYMBOLIC_ACCESSOR is true only when matching a
   symbolic-flavored contract's own condition (self-trust/consult of a
   contract already known to be symbolic-active, never conveyor-active);
   a conveyor-flavored contract's condition really executes at runtime,
   and a symbolic accessor (no definition at all) could never do that,
   so conveyor's own gate never widens. See .claude/plans/lazy-stirring-
   pearl.md for the full rationale.  */

static bool
oa_underlying_call_range_operand (tree op, tree *receiver_out, tree *callee_out,
				    bool allow_symbolic_accessor)
{
  tree comp = oa_strip_conversion_call (STRIP_ANY_LOCATION_WRAPPER (op));
  if (TREE_CODE (comp) != CALL_EXPR || call_expr_nargs (comp) != 1)
    return false;

  tree callee = cp_get_callee_fndecl_nofold (comp);
  if (!callee || TREE_CODE (callee) != FUNCTION_DECL
      || !DECL_OBJECT_MEMBER_FUNCTION_P (callee))
    return false;

  maybe_instantiate_conveyor (callee);
  if (!DECL_DECLARED_CONVEYOR_P (callee)
      && !(allow_symbolic_accessor && DECL_DECLARED_SYMBOLIC_P (callee)))
    return false;

  /* The implicit-object argument is presented wrapped for const-
     qualified access, exactly like a ptr->field access's own PTR_EXPR
     (see oa_strip_symbolic_ptr_expr's own comment) -- confirmed by
     direct testing (a bare ADDR_EXPR check alone left a NOP_EXPR/
     CONVERT_EXPR wrapper in place around 'this' and around '&v' alike,
     so neither ever matched TREE_CODE (...) == PARM_DECL downstream).
     Strip that wrapping both before and after peeling a leading
     ADDR_EXPR (an ADDR_EXPR can itself be wrapped, and unwrapping it
     can expose another layer).  */
  tree receiver = oa_strip_symbolic_ptr_expr (CALL_EXPR_ARG (comp, 0));
  if (TREE_CODE (receiver) == ADDR_EXPR)
    receiver = oa_strip_symbolic_ptr_expr (TREE_OPERAND (receiver, 0));

  *receiver_out = receiver;
  *callee_out = callee;
  return true;
}

static bool
oa_call_range_conjunct_shape (tree conjunct, tree *receiver_out,
			       tree *callee_out, tree_code *code_out,
			       tree *const_val_out, bool allow_symbolic_accessor)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return false;

  tree op0 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
  tree op1 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 1));

  /* CONST_VAL_OUT's own name predates the bounds-proving demo (see
     .claude/plans/lazy-stirring-pearl.md, Part 3): it may now also be a
     bare PARM_DECL of the conjunct-owning function, not just a literal --
     e.g. a postcondition's own 'size () == n', where N is resolved
     through positional substitution (and then, if not itself a literal
     at the substituting call site, that substituted expression's own
     established range) only by a caller that actually has a call site to
     substitute through (postcondition establishment) -- self-trust/
     precondition-consult callers have no such call site and keep
     requiring an already-known INTEGER_CST exactly as before, simply by
     continuing to check for one themselves.  */
  /* A PARM_DECL "other side" read from a postcondition's own const-
     qualified evaluation context arrives wrapped the same way a
     receiver/ptr_expr already does (see oa_strip_symbolic_ptr_expr's own
     comment; confirmed via direct testing here too -- a bare
     'TREE_CODE (op1) == PARM_DECL' check otherwise silently never fired
     for a postcondition's own 'size () == m'). Strip before checking,
     but keep OP0/OP1 themselves (not the stripped form) as COMP/CONST_
     VAL_OUT's own stored value below, matching the receiver side's own
     "present it the way it actually appears" discipline for what gets
     returned, since a literal never needs this stripping anyway.  */
  tree op0_stripped = oa_strip_symbolic_ptr_expr (op0);
  tree op1_stripped = oa_strip_symbolic_ptr_expr (op1);

  tree comp, const_val;
  bool flipped;
  if (TREE_CODE (op1) == INTEGER_CST)
    comp = op0, const_val = op1, flipped = false;
  else if (TREE_CODE (op0) == INTEGER_CST)
    comp = op1, const_val = op0, flipped = true;
  else if (TREE_CODE (op1_stripped) == PARM_DECL)
    comp = op0, const_val = op1_stripped, flipped = false;
  else if (TREE_CODE (op0_stripped) == PARM_DECL)
    comp = op1, const_val = op0_stripped, flipped = true;
  else
    return false;

  tree receiver, callee;
  if (!oa_underlying_call_range_operand (comp, &receiver, &callee,
					  allow_symbolic_accessor))
    return false;

  if (flipped)
    switch (code)
      {
      case LT_EXPR: code = GT_EXPR; break;
      case LE_EXPR: code = GE_EXPR; break;
      case GT_EXPR: code = LT_EXPR; break;
      case GE_EXPR: code = LE_EXPR; break;
      default: break;
      }

  *receiver_out = receiver;
  *callee_out = callee;
  *code_out = code;
  *const_val_out = const_val;
  return true;
}

/* Recognize CONJUNCT as "PARAM OP RECEIVER.ACCESSOR ()" (or the mirror
   image, normalized so CODE_OUT always reads left-to-right as "param
   CODE_OUT call"), the call analogue of oa_match_comparison_against_
   param -- e.g. 'i < v.size ()', the shape that actually motivated this
   whole feature (oa_call_range_conjunct_shape above only recognizes a
   call compared against a *literal*, e.g. 'v.size () > 3', which is a
   different, narrower shape -- found by direct testing that the
   motivating 'n < v.size()'-style precondition doesn't match it at
   all). PARAM is recognized the same narrow way oa_match_comparison_
   against_param's own PARAM is (a bare PARM_DECL, via oa_underlying_
   param_operand) -- deliberately no field/array-slot identity
   resolution on either side, matching that function's own scope
   exactly, not oa_call_range_conjunct_shape's broader one.  */

bool
oa_match_comparison_against_call (tree conjunct, tree *param_out,
				    tree_code *code_out, tree *rhs_receiver_out,
				    tree *rhs_callee_out,
				    bool allow_symbolic_accessor)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  enum tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return false;

  tree op0 = oa_strip_to_relational_operand (TREE_OPERAND (c, 0));
  tree op1 = oa_strip_to_relational_operand (TREE_OPERAND (c, 1));

  tree param, receiver, callee;
  bool flipped;
  if ((param = oa_underlying_param_operand (op0))
      && oa_underlying_call_range_operand (op1, &receiver, &callee,
					    allow_symbolic_accessor))
    flipped = false;
  else if ((param = oa_underlying_param_operand (op1))
	   && oa_underlying_call_range_operand (op0, &receiver, &callee,
						 allow_symbolic_accessor))
    flipped = true;
  else
    return false;

  if (flipped)
    switch (code)
      {
      case LT_EXPR: code = GT_EXPR; break;
      case LE_EXPR: code = GE_EXPR; break;
      case GT_EXPR: code = LT_EXPR; break;
      case GE_EXPR: code = LE_EXPR; break;
      default: break;
      }

  *param_out = param;
  *code_out = code;
  *rhs_receiver_out = receiver;
  *rhs_callee_out = callee;
  return true;
}

/* D4324 (see .claude/plans/lazy-stirring-pearl.md, Part 4): recognize
   CONJUNCT as "RECEIVER.ACCESSOR () - PARAM OP <literal>" or its mirror
   "PARAM - RECEIVER.ACCESSOR () OP <literal>" (plus the usual literal-
   on-the-left flip of either), e.g. 'v.size () - idx < 10'. Unlike oa_
   get_relational/oa_get_call_relational's own PLUS_EXPR/MINUS_EXPR
   handling just above (which *shifts* an already-established fact for
   one operand), this recognizes a genuinely fresh, direct observation
   -- there is no pre-existing fact on PARAM here -- so it produces
   (PARAM, CODE, RECEIVER, CALLEE, OFFSET) suitable for feeding straight
   into the *existing* call_relational_set establishment path (oa_call_
   relational_fact's own OFFSET already means "the fact holds for
   (LHS - OFFSET) CODE RHS", which accommodates a nonzero value at
   establishment just as well as at shift time, so no new storage is
   needed, only this recognizer).

   Algebra (CODE_OUT/OFFSET_OUT normalized so the established fact reads
   "(PARAM - OFFSET_OUT) CODE_OUT RECEIVER.CALLEE ()"):
     CALL () - PARAM CODE L  ->  PARAM <flip CODE> CALL (), offset -L
     PARAM - CALL () CODE L  ->  PARAM CODE CALL (), offset +L
   (the first negates the comparison direction, since solving for PARAM
   divides by -1; the second doesn't -- mirroring oa_call_range_
   conjunct_shape's own negate-on-flip discipline for its own literal-
   position flip, applied here to the CALL()-vs-PARAM position instead).  */

static bool
oa_match_shifted_comparison_against_call (tree conjunct, tree *param_out,
					    tree_code *code_out,
					    tree *rhs_receiver_out,
					    tree *rhs_callee_out,
					    widest_int *offset_out,
					    bool allow_symbolic_accessor)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  enum tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return false;

  tree op0 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
  tree op1 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 1));

  tree diff, lit;
  if (TREE_CODE (op1) == INTEGER_CST)
    diff = op0, lit = op1;
  else if (TREE_CODE (op0) == INTEGER_CST)
    {
      /* Literal on the left ('L < C - P'): mirror the comparison the
	 same way oa_call_range_conjunct_shape's own flipped branch
	 does, before any of this function's own algebraic flip below.  */
      diff = op1, lit = op0;
      switch (code)
	{
	case LT_EXPR: code = GT_EXPR; break;
	case LE_EXPR: code = GE_EXPR; break;
	case GT_EXPR: code = LT_EXPR; break;
	case GE_EXPR: code = LE_EXPR; break;
	default: break;
	}
    }
  else
    return false;

  diff = oa_strip_to_relational_operand (diff);
  if (TREE_CODE (diff) != MINUS_EXPR)
    return false;

  tree lhs = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (diff, 0));
  tree rhs = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (diff, 1));

  tree param, receiver, callee;
  widest_int offset;
  if ((param = oa_underlying_param_operand (rhs))
      && oa_underlying_call_range_operand (lhs, &receiver, &callee,
					     allow_symbolic_accessor))
    {
      /* CALL () - PARAM: solving for PARAM negates the direction.  */
      switch (code)
	{
	case LT_EXPR: code = GT_EXPR; break;
	case LE_EXPR: code = GE_EXPR; break;
	case GT_EXPR: code = LT_EXPR; break;
	case GE_EXPR: code = LE_EXPR; break;
	default: break;
	}
      offset = -wi::to_widest (lit);
    }
  else if ((param = oa_underlying_param_operand (lhs))
	   && oa_underlying_call_range_operand (rhs, &receiver, &callee,
						  allow_symbolic_accessor))
    /* PARAM - CALL (): no direction change.  */
    offset = wi::to_widest (lit);
  else
    return false;

  *param_out = param;
  *code_out = code;
  *rhs_receiver_out = receiver;
  *rhs_callee_out = callee;
  *offset_out = offset;
  return true;
}

/* Thin wrapper over oa_call_range_conjunct_shape immediately above, for
   a plugin/GIMPLE-pass-based consumer that needs the call-range
   comparison shape directly -- mirrors oa_match_field_range_comparison's
   own identical role for the ptr->field shape.  */

bool
oa_match_call_range_comparison (tree conjunct, tree *receiver_out,
				  tree *callee_out, tree_code *code_out,
				  tree *const_val_out,
				  bool allow_symbolic_accessor)
{
  return oa_call_range_conjunct_shape (conjunct, receiver_out, callee_out,
					code_out, const_val_out,
					allow_symbolic_accessor);
}

/* Recognize CONJUNCT as "RECEIVER_1.CALLEE_1 () OP RECEIVER_2.CALLEE_2 ()"
   -- the call-vs-call analogue of oa_match_comparison_against_call
   immediately above (e.g. 'v.size () < w.size ()'), for two calls
   compared against each other rather than a call against a bare
   parameter. Both sides are recognized via oa_underlying_call_range_
   operand; unlike oa_match_comparison_against_call's own PARAM/CALL
   asymmetry (which forces a flip whenever PARAM is written on the
   right), neither side is privileged here, so LHS/RHS are simply OP0/
   OP1 as written, CODE unchanged -- there's nothing to canonicalize
   around.  */

bool
oa_match_call_against_call (tree conjunct, tree *lhs_receiver_out,
			      tree *lhs_callee_out, tree_code *code_out,
			      tree *rhs_receiver_out, tree *rhs_callee_out,
			      bool allow_symbolic_accessor)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  enum tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return false;

  tree op0 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
  tree op1 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 1));

  tree lhs_receiver, lhs_callee, rhs_receiver, rhs_callee;
  if (!oa_underlying_call_range_operand (op0, &lhs_receiver, &lhs_callee,
					  allow_symbolic_accessor)
      || !oa_underlying_call_range_operand (op1, &rhs_receiver, &rhs_callee,
					     allow_symbolic_accessor))
    return false;

  *lhs_receiver_out = lhs_receiver;
  *lhs_callee_out = lhs_callee;
  *code_out = code;
  *rhs_receiver_out = rhs_receiver;
  *rhs_callee_out = rhs_callee;
  return true;
}

/* D4324: VAR_DECL-admitting sibling of oa_match_simple_comparison, for
   a contract_assert's own condition -- unlike that function's own
   callers (a plugin's precondition-obligation checking, or self-trust
   fact-seeding for LATER code), there is no callee/call site here to
   positionally substitute a matched operand into, so the PARM_DECL-
   only restriction that protects oa_substitute_call_arg elsewhere
   (see oa_underlying_param_operand's own comment) doesn't apply: the
   conjunct's own operand already IS the exact decl to consult in ENV
   directly (substitution is the identity function). Otherwise
   identical logic.  */

static bool
oa_match_simple_comparison_var (tree conjunct, tree *decl_out,
				 tree_code *code_out, tree *const_val_out)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  enum tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return false;

  tree op0 = oa_strip_conversion_call (STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0)));
  tree op1 = oa_strip_conversion_call (STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 1)));

  tree decl, const_val;
  bool flipped;
  if ((VAR_P (op0) || TREE_CODE (op0) == PARM_DECL) && TREE_CODE (op1) == INTEGER_CST)
    decl = op0, const_val = op1, flipped = false;
  else if ((VAR_P (op1) || TREE_CODE (op1) == PARM_DECL) && TREE_CODE (op0) == INTEGER_CST)
    decl = op1, const_val = op0, flipped = true;
  else
    return false;

  if (flipped)
    switch (code)
      {
      case LT_EXPR: code = GT_EXPR; break;
      case LE_EXPR: code = GE_EXPR; break;
      case GT_EXPR: code = LT_EXPR; break;
      case GE_EXPR: code = LE_EXPR; break;
      default: break;
      }

  *decl_out = decl;
  *code_out = code;
  *const_val_out = const_val;
  return true;
}

/* D4324: VAR_DECL-admitting sibling of oa_match_comparison_against_param,
   for the same reason oa_match_simple_comparison_var exists (see its
   own comment) -- calls the shared oa_underlying_param_operand helper
   with ALLOW_VAR_DECL true on both sides instead of relying on its
   default.  */

static bool
oa_match_comparison_against_decl (tree conjunct, tree *decl_out,
				   tree_code *code_out, tree *other_out)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  enum tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return false;

  tree op0 = oa_strip_to_relational_operand (TREE_OPERAND (c, 0));
  tree op1 = oa_strip_to_relational_operand (TREE_OPERAND (c, 1));

  tree decl, other;
  bool flipped;
  if ((decl = oa_underlying_param_operand (op0, /*allow_var_decl=*/true))
      && (other = oa_underlying_param_operand (op1, /*allow_var_decl=*/true)))
    flipped = false;
  else if ((decl = oa_underlying_param_operand (op1, /*allow_var_decl=*/true))
	   && (other = oa_underlying_param_operand (op0, /*allow_var_decl=*/true)))
    flipped = true;
  else
    return false;

  /* A decl compared against itself isn't a useful relation.  */
  if (decl == other)
    return false;

  if (flipped)
    switch (code)
      {
      case LT_EXPR: code = GT_EXPR; break;
      case LE_EXPR: code = GE_EXPR; break;
      case GT_EXPR: code = LT_EXPR; break;
      case GE_EXPR: code = LE_EXPR; break;
      default: break;
      }

  *decl_out = decl;
  *code_out = code;
  *other_out = other;
  return true;
}

/* D4324: VAR_DECL-admitting sibling of oa_match_comparison_against_call,
   for the same reason the two matchers above exist. Only the PARAM_OUT
   side needs relaxing -- oa_underlying_call_range_operand (the
   RHS_RECEIVER_OUT side, shared with oa_match_call_against_call, which
   therefore needs no sibling of its own at all) already imposes no
   decl-kind restriction of its own.  */

static bool
oa_match_comparison_against_call_var (tree conjunct, tree *decl_out,
				       tree_code *code_out,
				       tree *rhs_receiver_out,
				       tree *rhs_callee_out,
				       bool allow_symbolic_accessor)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  enum tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return false;

  tree op0 = oa_strip_to_relational_operand (TREE_OPERAND (c, 0));
  tree op1 = oa_strip_to_relational_operand (TREE_OPERAND (c, 1));

  tree decl, receiver, callee;
  bool flipped;
  if ((decl = oa_underlying_param_operand (op0, /*allow_var_decl=*/true))
      && oa_underlying_call_range_operand (op1, &receiver, &callee,
					   allow_symbolic_accessor))
    flipped = false;
  else if ((decl = oa_underlying_param_operand (op1, /*allow_var_decl=*/true))
	   && oa_underlying_call_range_operand (op0, &receiver, &callee,
						 allow_symbolic_accessor))
    flipped = true;
  else
    return false;

  if (flipped)
    switch (code)
      {
      case LT_EXPR: code = GT_EXPR; break;
      case LE_EXPR: code = GE_EXPR; break;
      case GT_EXPR: code = LT_EXPR; break;
      case GE_EXPR: code = LE_EXPR; break;
      default: break;
      }

  *decl_out = decl;
  *code_out = code;
  *rhs_receiver_out = receiver;
  *rhs_callee_out = callee;
  return true;
}

/* D4324: check CONJUNCT (part of a contract_assert's own condition)
   against whatever ENV already has established at this program point
   -- the ambient-fact counterpart of the call-obligation family
   (oa_handle_call_conveyor_proof_obligation and siblings) used at an
   actual call site, minus their positional-substitution step: there is
   no callee/call site here, so the conjunct's own operands already ARE
   the decls to consult directly (see the three _var/_decl matchers
   above). Returns OA_PROVEN_FALSE only when some already-tracked fact
   category flatly contradicts CONJUNCT; OA_PROVEN_TRUE/OA_UNKNOWN are
   not separately actionable to the caller (oa_handle_assertion_stmt) --
   an unprovable, or merely consistent, conjunct is simply trusted
   onward exactly as it always was, this only adds the "provably false"
   diagnostic path. REQUIRE_CONVEYOR mirrors the same parameter already
   threaded through every reused *_1 helper below: true for the
   conveyor-flavored check, false for the symbolic one, exactly
   matching how the paired call-obligation functions already
   distinguish conveyor vs. symbolic mode.

   Tries each fact shape in turn, the same sequence (and mutual
   exclusivity, by construction) oa_refine_single_comparison's own
   establishing side already relies on for its ptr->field/call-range/
   relational/call-relational blocks.  */

static oa_proof_result
oa_check_assertion_conjunct_against_env (tree conjunct, oa_env &env,
					  bool require_conveyor)
{
  tree decl, other, const_val;
  tree_code code;

  if (oa_match_simple_comparison_var (conjunct, &decl, &code, &const_val))
    {
      oa_range_fact req;
      req.base = NULL_TREE;
      req.has_lo = req.has_hi = false;
      oa_tighten_range_bound (req, code, wi::to_widest (const_val));
      return oa_env_check_range_subsumption (env, decl, req);
    }

  if (oa_match_comparison_against_decl (conjunct, &decl, &code, &other))
    return oa_env_check_relational_fact_1 (env, decl, code, other,
					    require_conveyor);

  {
    tree rhs_receiver, rhs_callee;
    if (oa_match_comparison_against_call_var (conjunct, &decl, &code,
					       &rhs_receiver, &rhs_callee,
					       /*allow_symbolic_accessor=*/
						 !require_conveyor))
      return oa_env_check_call_relational_fact_1 (env, decl, code,
						   rhs_receiver, rhs_callee,
						   require_conveyor);
  }

  {
    tree lhs_receiver, lhs_callee, rhs_receiver, rhs_callee;
    if (oa_match_call_against_call (conjunct, &lhs_receiver, &lhs_callee,
				     &code, &rhs_receiver, &rhs_callee,
				     /*allow_symbolic_accessor=*/
				       !require_conveyor))
      return oa_env_check_call_call_relational_fact_1
	(env, lhs_receiver, lhs_callee, code, rhs_receiver, rhs_callee,
	 require_conveyor);
  }

  {
    tree pred_fn, arg_decl;
    bool negated;
    if (oa_predicate_conjunct_shape (conjunct, &pred_fn, &arg_decl, &negated))
      return oa_env_predicate_result (env, arg_decl, pred_fn, !negated,
				       require_conveyor);
  }

  {
    tree field, ptr_expr, field_const;
    tree_code field_code;
    if (oa_symbolic_comparison_conjunct_shape (conjunct, &field, &ptr_expr,
						&field_code, &field_const)
	&& TREE_CODE (field_const) == INTEGER_CST)
      {
	ptr_expr = oa_strip_symbolic_ptr_expr (ptr_expr);
	tree identity;
	if (oa_object_identity_decl (ptr_expr, &identity)
	    || oa_field_slot_identity (ptr_expr, env, &identity)
	    || oa_array_slot_identity (ptr_expr, env, &identity)
	    || oa_field_object_identity (ptr_expr, env, &identity))
	  {
	    identity = env.alias_find (identity);
	    oa_contract_field_range_fact established;
	    if (env.contract_field_range_get (identity, field, &established)
		&& (!require_conveyor || established.conveyor_established))
	      {
		oa_range_fact req;
		req.base = NULL_TREE;
		req.has_lo = req.has_hi = false;
		oa_tighten_range_bound (req, field_code,
					 wi::to_widest (field_const));
		return oa_range_subsumption_result (established.range, req);
	      }
	  }
	return OA_UNKNOWN;
      }
  }

  {
    tree receiver_expr, callee, call_const;
    tree_code call_code;
    if (oa_call_range_conjunct_shape (conjunct, &receiver_expr, &callee,
				       &call_code, &call_const,
				       /*allow_symbolic_accessor=*/
					 !require_conveyor)
	&& TREE_CODE (call_const) == INTEGER_CST)
      {
	receiver_expr = oa_strip_symbolic_ptr_expr (receiver_expr);
	tree identity;
	if (oa_object_identity_decl (receiver_expr, &identity)
	    || oa_field_slot_identity (receiver_expr, env, &identity)
	    || oa_array_slot_identity (receiver_expr, env, &identity)
	    || oa_field_object_identity (receiver_expr, env, &identity))
	  {
	    identity = env.alias_find (identity);
	    oa_contract_field_range_fact established;
	    if (env.contract_call_range_get (identity, callee, &established)
		&& (!require_conveyor || established.conveyor_established))
	      {
		oa_range_fact req;
		req.base = NULL_TREE;
		req.has_lo = req.has_hi = false;
		oa_tighten_range_bound (req, call_code,
					 wi::to_widest (call_const));
		return oa_range_subsumption_result (established.range, req);
	      }
	  }
	return OA_UNKNOWN;
      }
  }

  return OA_UNKNOWN;
}

/* One recognized, codegen-ready action for -fcontract-symbolic-runtime-
   checks: either a pred_fn(arg)/!pred_fn(arg) conjunct (IS_RANGE
   false), or one or more ptr->field OP const conjuncts on the same
   (FIELD, PTR_EXPR) pair, already combined into a single [lo,hi) range
   (IS_RANGE true) -- mirrors oa_range_fact's own half-open convention.
   PRED_FN/BOOL_ARG/FIELD/PTR_EXPR all still reference ORIG's real decls
   (this, parameters), unremapped -- the same decls
   build_predicate_core_function_1's own walk_tree/copy_tree_body_r
   remapping already knows how to handle once these are woven into a
   shadow CONTRACT_CONDITION, exactly as for any other conjunct.  */

struct oa_symbolic_action
{
  bool is_range;
  tree pred_fn;
  tree bool_arg;
  bool polarity;
  tree field;
  tree ptr_expr;
  bool has_lo, has_hi;
  long long lo, hi;
};

/* Break CONDITION into conjuncts (oa_collect_conjuncts) and classify
   each as a predicate-call or comparison action (above), combining
   multiple comparison conjuncts on the same (field, ptr_expr) pair into
   one range action.  Returns false (diagnosing at LOC) if any conjunct
   matches neither shape -- -fcontract-symbolic-runtime-checks requires
   every conjunct of a symbolic contract's condition to be recognized,
   rather than silently doing something unsound with the rest.  */

static bool
oa_collect_symbolic_actions (location_t loc, tree condition,
			     vec<oa_symbolic_action> *actions)
{
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts (&condition, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    {
      tree pred_fn, arg_decl;
      bool negated;
      if (oa_predicate_conjunct_shape (*conjuncts[i], &pred_fn, &arg_decl,
					&negated))
	{
	  oa_symbolic_action act = {};
	  act.is_range = false;
	  act.pred_fn = pred_fn;
	  act.bool_arg = arg_decl;
	  act.polarity = !negated;
	  actions->safe_push (act);
	  continue;
	}

      tree field, ptr_expr, const_val;
      tree_code code;
      if (oa_symbolic_comparison_conjunct_shape (*conjuncts[i], &field,
						  &ptr_expr, &code, &const_val))
	{
	  if (TREE_CODE (const_val) != INTEGER_CST)
	    {
	      error_at (loc, "non-constant bound in a comparison conjunct of "
			"a symbolic contract");
	      return false;
	    }
	  long long val = TREE_INT_CST_LOW (const_val);
	  bool has_lo = false, has_hi = false;
	  long long lo = 0, hi = 0;
	  switch (code)
	    {
	    case GE_EXPR: has_lo = true; lo = val; break;
	    case GT_EXPR: has_lo = true; lo = val + 1; break;
	    case LE_EXPR: has_hi = true; hi = val + 1; break;
	    case LT_EXPR: has_hi = true; hi = val; break;
	    case EQ_EXPR: has_lo = true; has_hi = true; lo = val; hi = val + 1; break;
	    default: gcc_unreachable ();
	    }

	  oa_symbolic_action *found = NULL;
	  for (unsigned j = 0; j < actions->length () && !found; ++j)
	    {
	      oa_symbolic_action &a = (*actions)[j];
	      if (a.is_range && a.field == field
		  && cp_tree_equal (a.ptr_expr, ptr_expr))
		found = &a;
	    }
	  if (found)
	    {
	      if (has_lo && (!found->has_lo || lo > found->lo))
		{ found->has_lo = true; found->lo = lo; }
	      if (has_hi && (!found->has_hi || hi < found->hi))
		{ found->has_hi = true; found->hi = hi; }
	    }
	  else
	    {
	      oa_symbolic_action act = {};
	      act.is_range = true;
	      act.field = field;
	      act.ptr_expr = ptr_expr;
	      act.has_lo = has_lo;
	      act.lo = lo;
	      act.has_hi = has_hi;
	      act.hi = hi;
	      actions->safe_push (act);
	    }
	  continue;
	}

      /* -fcontract-symbolic-runtime-checks (Mechanism B): a conjunct
	 comparing a *bare* decl directly (no pointer indirection at
	 all -- a precondition's own by-value parameter, or a
	 postcondition's own POSTCONDITION_IDENTIFIER binder, either of
	 which may be a VAR_DECL or a PARM_DECL, unlike oa_match_simple_
	 comparison's own PARM_DECL-only shape, built for a different
	 purpose) is neither of the two shapes Mechanism A (this
	 function) handles at all -- it is entirely Mechanism B's own
	 responsibility, established at the producing call's own call
	 site (oa_call_symbolic_range_p) and consulted at the consuming
	 call's own call site (oa_precondition_symbolic_range_p/oa_
	 handle_call_symbolic_scalar_obligation), never inside
	 CONTRACT's own owner's build_contract_check dispatch at all.
	 Silently deferred, not an error: unlike a conjunct matching
	 neither shape at all (still a hard error below), this one is a
	 recognized, supported shape -- just not this function's to
	 handle.  */
      {
	tree c = STRIP_ANY_LOCATION_WRAPPER (*conjuncts[i]);
	while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
	  c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
	if (TREE_CODE (c) == LT_EXPR || TREE_CODE (c) == LE_EXPR
	    || TREE_CODE (c) == GT_EXPR || TREE_CODE (c) == GE_EXPR
	    || TREE_CODE (c) == EQ_EXPR)
	  {
	    tree bare_op0 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
	    tree bare_op1 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 1));
	    /* A postcondition's own identifier, once remap_retval has
	       already substituted it (build_contract_check does this
	       before calling build_symbolic_runtime_check), is DECL_RESULT
	       (current_function_decl) -- a RESULT_DECL, not a VAR_DECL/
	       PARM_DECL like a bare precondition parameter is.  */
	    bool bare0 = VAR_P (bare_op0) || TREE_CODE (bare_op0) == PARM_DECL
	      || TREE_CODE (bare_op0) == RESULT_DECL;
	    bool bare1 = VAR_P (bare_op1) || TREE_CODE (bare_op1) == PARM_DECL
	      || TREE_CODE (bare_op1) == RESULT_DECL;
	    if ((bare0 && TREE_CODE (bare_op1) == INTEGER_CST)
		|| (bare1 && TREE_CODE (bare_op0) == INTEGER_CST))
	      continue;
	  }
      }

      error_at (loc, "unsupported conjunct in the condition of a symbolic "
		"contract under %<-fcontract-symbolic-runtime-checks%>: "
		"expected a symbolic predicate call or a pointer-reached "
		"field comparison");
      return false;
    }
  return true;
}

/* Validate BASE_TYPE as the target of a base_contract<BASE_TYPE>() used
   inside a contract of CANONICAL (the real, canonical FUNCTION_DECL this
   contract belongs to -- see resolve_base_contract_calls's comment for
   why this may differ from the FUNCTION_DECL currently being
   genericized): CANONICAL must be virtual; BASE_TYPE must be an
   accessible (from CANONICAL's own class, so accessibility of the
   inheritance itself is irrelevant here) unambiguous, proper (direct or
   indirect) base of CANONICAL's class; and BASE_TYPE must itself declare
   a matching override of CANONICAL (look_for_overrides_here, exactly
   the same override-signature match used to validate an ordinary
   override).  Diagnoses each failure when COMPLAIN; returns the found
   FUNCTION_DECL in BASE_TYPE, or NULL_TREE.  */

static tree
find_base_contract_target (tree base_type, tree canonical, bool complain,
			    location_t loc)
{
  if (!DECL_VIRTUAL_P (canonical))
    {
      if (complain)
	error_at (loc, "%<base_contract%> may only be used in a contract "
		  "of a virtual member function");
      return NULL_TREE;
    }

  tree derived_type = DECL_CONTEXT (canonical);

  if (!CLASS_TYPE_P (base_type))
    {
      if (complain)
	error_at (loc, "%qT is not a class type", base_type);
      return NULL_TREE;
    }

  if (same_type_ignoring_top_level_qualifiers_p (base_type, derived_type))
    {
      if (complain)
	error_at (loc, "%qT is not a base of itself", base_type);
      return NULL_TREE;
    }

  tree binfo = lookup_base (derived_type, base_type, ba_unique, NULL,
			    complain ? tf_warning_or_error : tf_none);
  if (binfo == error_mark_node)
    /* lookup_base already diagnosed the ambiguity when COMPLAIN.  */
    return NULL_TREE;
  if (!binfo)
    {
      if (complain)
	error_at (loc, "%qT is not a base of %qT", base_type, derived_type);
      return NULL_TREE;
    }

  tree target = look_for_overrides_here (base_type, canonical);
  if (!target)
    {
      if (complain)
	error_at (loc, "%qT does not declare an override of %qD",
		  base_type, canonical);
      return NULL_TREE;
    }

  return target;
}

/* Build the boolean expression base_contract<BASE_FN's class>() resolves
   to: the conjunction of all of BASE_FN's own contract specifiers whose
   TREE_CODE is KIND (matching whether CONTRACT -- the contract this
   base_contract<>() call appears inside, as part of USING_FNDECL's own
   processing -- is itself a precondition or postcondition), each called
   through a this-adjusting thunk exactly like
   resolve_inherited_contract's own argument-building.  boolean_true_node
   (vacuously true) if BASE_FN has no contracts of that kind at all.  */

static tree
build_base_contract_expr (tree base_fn, tree_code kind, tree using_fndecl,
			   tree contract)
{
  tree this_parm = DECL_ARGUMENTS (using_fndecl);
  tree binfo = lookup_base (TREE_TYPE (TREE_TYPE (this_parm)),
			    DECL_CONTEXT (base_fn), ba_any, NULL,
			    tf_warning_or_error);
  gcc_assert (binfo && binfo != error_mark_node);
  tree adjusted_this = save_expr (build_base_path (PLUS_EXPR, this_parm,
						    binfo, 1,
						    tf_warning_or_error));

  tree result = NULL_TREE;
  for (tree spec = get_fn_contract_specifiers (base_fn); spec;
       spec = TREE_CHAIN (spec))
    {
      tree base_contract = CONTRACT_STATEMENT (spec);
      if (TREE_CODE (base_contract) != kind)
	continue;

      tree core_fn = get_or_build_predicate_core_function (base_contract,
							     base_fn);
      releasing_vec args;
      vec_safe_push (args, adjusted_this);
      for (tree p = DECL_CHAIN (this_parm); p; p = DECL_CHAIN (p))
	vec_safe_push (args, p);
      if (POSTCONDITION_P (contract))
	vec_safe_push (args, POSTCONDITION_IDENTIFIER (contract));

      tree call = build_call_a (core_fn, args->length (), args->address ());
      result = result
	? build2 (TRUTH_ANDIF_EXPR, boolean_type_node, result, call)
	: call;
    }

  return result ? result : boolean_true_node;
}

/* walk_tree callback data for resolve_base_contract_calls.  */

struct base_contract_walk_data
{
  tree using_fndecl;
  tree contract;
  bool complain;
};

static tree
resolve_base_contract_r (tree *tp, int *do_subtree, void *data_)
{
  base_contract_walk_data *data = (base_contract_walk_data *) data_;
  tree base_type;
  if (!base_contract_call_p (*tp, &base_type))
    return NULL_TREE;

  location_t loc = EXPR_LOCATION (*tp);

  if (TREE_CODE (data->contract) != PRECONDITION_STMT
      && TREE_CODE (data->contract) != POSTCONDITION_STMT)
    {
      if (data->complain)
	error_at (loc, "%<base_contract%> may only be used in a "
		  "precondition or postcondition");
      *tp = error_mark_node;
      *do_subtree = 0;
      return NULL_TREE;
    }

  tree canonical = data->using_fndecl;
  if (DECL_LANG_SPECIFIC (canonical) && DECL_CONTRACT_WRAPPER (canonical))
    canonical = get_orig_func_for_wrapper (canonical);

  tree target = find_base_contract_target (base_type, canonical,
					     data->complain, loc);
  *tp = target
    ? build_base_contract_expr (target, TREE_CODE (data->contract),
				data->using_fndecl, data->contract)
    : error_mark_node;
  *do_subtree = 0;
  return NULL_TREE;
}

/* Rewrite every std::contracts::base_contract<Base>() call appearing
   anywhere in CONTRACT's condition (as currently being processed as part
   of USING_FNDECL -- CONTRACT's own real function or its caller-side
   wrapper; never its outlined PRE_FN/POST_FN, which is built directly
   from an already-fully-resolved CONTRACT_CONDITION via
   build_predicate_core_function_1's own copy_tree_body_r remapping, and
   so never calls build_contract_check/this function at all) into a
   real, this-adjusted call to the named base's own matching contract
   set.

   Deferred to here (genericization time), exactly like
   resolve_inherited_contract, for the same reason: lookup_base/
   build_base_path need a complete type.  Unlike that function, there is
   no persistent marker to maintain across copy_node duplicates -- the
   base_contract<Base>() CALL_EXPR itself already carries everything
   needed (BASE_TYPE is recoverable from its callee's own template
   arguments), so simply redoing this lookup, harmlessly, on every copy
   build_contract_check ever sees is enough.

   USING_FNDECL's own DECL_ARGUMENTS give the *current* copy's own
   parameters to forward, but its DECL_NAME/signature is not always a
   reliable proxy for the enclosing member function's own signature (an
   outlined PRE_FN/POST_FN copy's is not one at all -- moot here, since
   that copy never reaches this function; a caller-side wrapper's,
   though, is reached here, and while it does mirror the real function's
   name and parameter list, going through the existing, already-proven
   decl_for_wrapper map instead is simpler and avoids relying on that).
   contract_side_of (DECL_CONTRACT_WRAPPER) already distinguishes a
   wrapper from the real function; get_orig_func_for_wrapper already maps
   a wrapper back to it -- so CANONICAL, used only to find BASE_TYPE's
   matching override, is always the real, stable FUNCTION_DECL.

   Diagnoses failures only when USING_FNDECL is the real function, not
   its wrapper: the real function's own pass always eventually runs
   (unlike the wrapper, which is only built on demand by an actual
   client-side-eligible call site), so it is the diagnostic point of
   record -- the wrapper's own, independent re-resolution of the same
   source-level expression reuses the identical logic but stays quiet.  */

static void
resolve_base_contract_calls (tree contract, tree using_fndecl)
{
  base_contract_walk_data data;
  data.using_fndecl = using_fndecl;
  data.contract = contract;
  data.complain = !(DECL_LANG_SPECIFIC (using_fndecl)
		     && DECL_CONTRACT_WRAPPER (using_fndecl));
  walk_tree (&CONTRACT_CONDITION (contract), resolve_base_contract_r,
	     &data, NULL);
}

/* Build a fresh, file-local, static `bool NAME (void *)' thunk for CORE_FN
   (as built by build_predicate_core_function).  Its body casts its argument
   to a pointer to STRUCT_TYPE (see the struct built in build_contract_check,
   one pointer FIELD_DECL per CORE_FN parameter), dereferences each field to
   recover CORE_FN's real arguments -- except for a field backing a
   reference-typed parameter, where the stored pointer value is passed
   directly, since a reference argument is itself just that pointer value --
   calls CORE_FN, and returns its result.  This is the function whose address
   is handed to a control object's operator() as its bool(*)(void*)
   callback.  */

static tree
build_predicate_thunk_function (tree contract, tree core_fn, tree struct_type)
{
  location_t loc = EXPR_LOCATION (contract);

  tree void_ptr_type = build_pointer_type (void_type_node);
  tree arg_types = tree_cons (NULL_TREE, void_ptr_type, void_list_node);
  tree fn_type = build_function_type (boolean_type_node, arg_types);

  tree name = clone_function_name_numbered (core_fn, "thunk");
  tree fn = build_lang_decl_loc (loc, FUNCTION_DECL, name, fn_type);
  DECL_CONTEXT (fn) = NULL_TREE;
  DECL_SOURCE_LOCATION (fn) = loc;
  SET_DECL_ASSEMBLER_NAME (fn, name);

  tree parm = build_lang_decl (PARM_DECL, get_identifier ("p"), void_ptr_type);
  DECL_CONTEXT (parm) = fn;
  DECL_ARGUMENTS (fn) = parm;
  DECL_RESULT (fn) = NULL_TREE; /* Let start_preparsed_function fill it in.  */

  TREE_STATIC (fn) = 1;
  TREE_USED (fn) = 1;
  DECL_ARTIFICIAL (fn) = 1;
  TREE_PUBLIC (fn) = 0;
  DECL_EXTERNAL (fn) = 0;
  DECL_INTERFACE_KNOWN (fn) = 1;
  suppress_warning (fn);

  tree struct_ptr_type = build_pointer_type (struct_type);
  tree cast = build1 (NOP_EXPR, struct_ptr_type, parm);
  tree deref = build_simple_mem_ref (cast);

  releasing_vec call_args;
  tree field = TYPE_FIELDS (struct_type);
  for (tree cp = DECL_ARGUMENTS (core_fn); cp; cp = DECL_CHAIN (cp), field = DECL_CHAIN (field))
    {
      tree field_ref = build3 (COMPONENT_REF, TREE_TYPE (field), deref,
			       field, NULL_TREE);
      tree arg = TYPE_REF_P (TREE_TYPE (cp))
	? fold_convert (TREE_TYPE (cp), field_ref)
	: build_simple_mem_ref (field_ref);
      vec_safe_push (call_args, arg);
    }

  tree call = build_call_a (core_fn, call_args->length (), call_args->address ());

  /* See the matching comment in build_predicate_core_function: this nested
     synthesis must go through push_to_top_level/pop_from_top_level, not a
     bare push_function_context/pop_function_context.  */
  push_to_top_level ();
  start_preparsed_function (fn, NULL_TREE, SF_PRE_PARSED | SF_DEFAULT);
  /* The body below is built directly as already-resolved trees rather than
     via the normal semantic-level call-building routines, so the usual
     "did we see anything that might throw" bookkeeping never runs.  Without
     this, finish_function would conclude FN can't throw and mark it
     TREE_NOTHROW, and an exception genuinely raised while evaluating the
     predicate would hit that false nothrow boundary and terminate instead
     of propagating -- exactly backwards from a control object that wants
     to let it through.  */
  cp_function_chain->can_throw = true;
  tree body = begin_function_body ();
  finish_return_stmt (call);
  finish_function_body (body);
  fn = finish_function (/*inline_p=*/false);
  expand_or_defer_fn (fn);
  pop_from_top_level ();

  return fn;
}

/* Build a RECORD_TYPE with one pointer FIELD_DECL per CORE_FN parameter (see
   build_predicate_core_function): the Nth field's type is a pointer to the
   Nth parameter's type with any reference stripped.  Needed by both the
   thunk (to know which fields to dereference when unpacking) and the
   struct-populating code in build_contract_check, so it's built once,
   separately from either.  */

static tree
build_predicate_arg_struct_type (tree core_fn, location_t loc)
{
  tree fields = NULL_TREE;
  tree *last_field = &fields;
  for (tree p = DECL_ARGUMENTS (core_fn); p; p = DECL_CHAIN (p))
    {
      tree pointee = TYPE_REF_P (TREE_TYPE (p))
	? TREE_TYPE (TREE_TYPE (p)) : TREE_TYPE (p);
      tree field = build_decl (loc, FIELD_DECL, NULL_TREE,
			       build_pointer_type (pointee));
      *last_field = field;
      last_field = &DECL_CHAIN (field);
    }

  tree struct_type = make_node (RECORD_TYPE);
  TYPE_FIELDS (struct_type) = fields;
  for (tree f = fields; f; f = DECL_CHAIN (f))
    DECL_CONTEXT (f) = struct_type;
  layout_type (struct_type);
  return struct_type;
}

/* Declare a local variable of STRUCT_TYPE (built by
   build_predicate_arg_struct_type) in CC_BIND (registered for the
   gimplifier, exactly like the ctrl_var/loc_var temporaries
   build_contract_control_call already declares there), and populate each
   field with the address of CURRENT_FUNCTION_DECL's corresponding real
   parameter -- or, for STRUCT_TYPE's trailing field when CONTRACT is a
   postcondition, the address of DECL_RESULT (CURRENT_FUNCTION_DECL).  No
   explicit "spill to memory" step is needed: taking a parameter's (or
   DECL_RESULT's) address is ordinary C++ semantics -- a reference
   parameter's address is already the address of its referent -- and GCC's
   own gimplifier forces the addressed decl onto the stack automatically
   once the ADDR_EXPR is built.  Returns the address of the new struct
   variable.  */

static tree
build_predicate_arg_struct_var (tree contract, tree struct_type, tree cc_bind,
				location_t loc)
{
  tree struct_var = build_decl (loc, VAR_DECL, NULL_TREE, struct_type);
  DECL_ARTIFICIAL (struct_var) = true;
  DECL_IGNORED_P (struct_var) = true;
  DECL_CONTEXT (struct_var) = current_function_decl;
  layout_decl (struct_var, 0);
  DECL_CHAIN (struct_var) = BIND_EXPR_VARS (cc_bind);
  BIND_EXPR_VARS (cc_bind) = struct_var;
  add_decl_expr (struct_var);

  tree field = TYPE_FIELDS (struct_type);

  auto fill_one = [&] (tree real_val)
    {
      tree field_ref = build3 (COMPONENT_REF, TREE_TYPE (field), struct_var,
				field, NULL_TREE);
      tree addr = fold_convert (TREE_TYPE (field),
				build_fold_addr_expr (real_val));
      finish_expr_stmt (cp_build_init_expr (field_ref, addr));
      field = DECL_CHAIN (field);
    };

  for (tree real_val = DECL_ARGUMENTS (current_function_decl);
       real_val && field; real_val = DECL_CHAIN (real_val))
    fill_one (real_val);

  tree postcond_id = POSTCONDITION_P (contract)
		      ? POSTCONDITION_IDENTIFIER (contract) : NULL_TREE;
  vec<tree, va_gc> *captures
    = find_condition_captures (CONTRACT_CONDITION (contract),
				current_function_decl, postcond_id);
  for (unsigned i = 0; i < vec_safe_length (captures) && field; ++i)
    fill_one ((*captures)[i]);

  if (field)
    {
      /* The trailing field, for a postcondition's result.  */
      gcc_checking_assert (POSTCONDITION_P (contract));
      fill_one (DECL_RESULT (current_function_decl));
    }
  gcc_checking_assert (!field);

  return build_fold_addr_expr (struct_var);
}

/* Forward declaration: defined below, builds a genuine std::source_location
   CONSTRUCTOR; used here as well as by build_contract_control_constexpr_check
   further down.  Despite its "constexpr" heritage in the comment above its
   definition, it's just an ordinary constant-folded __builtin_source_location()
   call and is equally valid at genericization time as it is under constant
   evaluation.  */
static tree build_real_source_location_value (location_t, tree, tree);

/* Build the D4324 control-object dispatch call for CONTRACT inside CC_BIND
   (a BIND_EXPR whose variable chain is available for temporaries).  The
   control object's operator() returns void: returning means proceed, and a
   terminating control terminates in its own body.  ARGS_PTR is a void*
   expression pointing at the packed argument struct for this assertion (see
   build_predicate_arg_struct_type/_var) and THUNK_FN is the FUNCTION_DECL of
   the matching bool(void*) thunk (see build_predicate_thunk_function):
   together these let the control object evaluate the predicate itself, on
   its own terms, via a callback bundled into OP's single assertion_context
   parameter, instead of the compiler evaluating it eagerly.  Returns the
   call expression or error_mark_node.  */

static tree
build_contract_control_call (tree contract, tree ctrl, tree op, tree cc_bind,
			      tree args_ptr, tree thunk_fn)
{
  location_t loc = EXPR_LOCATION (contract);
  tree t_ctx = TREE_VALUE (FUNCTION_FIRST_USER_PARMTYPE (op));
  tree ctx_type = non_reference (t_ctx);
  contract_check_side side
    = contract_side_of (contract, current_function_decl);

  tree comment = contract_control_omits_comment (ctrl, side)
    ? NULL_TREE : CONTRACT_COMMENT (contract);
  if (!comment)
    /* Empty, not null: matches the "static empty string, never a null
       pointer" convention cp-gimplify.cc's build_source_location_impl
       already uses for a missing file/function name.  */
    comment = build_string_literal ("");

  tree check_fn = build_addr_func (thunk_fn, tf_warning_or_error);
  mark_used (thunk_fn);

  /* Build a genuine `const assertion_context' CONSTRUCTOR directly against
     the real class's own (private) fields -- the same mechanism
     build_contract_control_constexpr_check uses for the constexpr path, and
     just as safe here: assertion_context (like std::source_location) is a
     compiler-backed library type with no base classes or vtable to set up,
     so there's no real constructor semantics a raw CONSTRUCTOR could skip.
     Building directly against the real type -- instead of a separate
     compiler-internal mirror type, reinterpret-cast at the call boundary --
     is what makes this path and the constexpr path always agree on the
     location field's real __impl type, rather than each asking
     build_source_location_impl's (cp-gimplify.cc) location+fndecl-keyed
     cache for a different one and silently colliding.  */
  tree f0 = next_aggregate_field (TYPE_FIELDS (ctx_type));
  tree f1 = next_aggregate_field (DECL_CHAIN (f0));
  tree f2 = next_aggregate_field (DECL_CHAIN (f1));
  tree f3 = next_aggregate_field (DECL_CHAIN (f2));
  tree f4 = next_aggregate_field (DECL_CHAIN (f3));
  tree f5 = next_aggregate_field (DECL_CHAIN (f4));
  tree f6 = next_aggregate_field (DECL_CHAIN (f5));
  tree ctor = build_constructor_va
    (ctx_type, 7,
     f0, comment,
     f1, (contract_control_omits_source_location (ctrl, side)
	  ? build_constructor (TREE_TYPE (f1), NULL)
	  : build_real_source_location_value
	      (loc, TREE_TYPE (f1),
	       resolve_fndecl_for_diagnostic_name (current_function_decl))),
     f2, build_int_cst (TREE_TYPE (f2), contract_evaluation_semantic_value ()),
     f3, build_int_cst (TREE_TYPE (f3), get_contract_assertion_kind (contract)),
     f4, build_assertion_static_info_value (side, TREE_TYPE (f4)),
     f5, fold_convert (TREE_TYPE (f5), args_ptr),
     f6, fold_convert (TREE_TYPE (f6), check_fn));

  /* Build the assertion_context object on the stack; register it, exactly
     like the control object below.  Unlike a contract_violation object,
     this is never a compile-time constant: ARGS_PTR/CHECK_FN are runtime
     addresses of stack/function locals.  */
  tree ctx_var = build_decl (loc, VAR_DECL, NULL_TREE, ctx_type);
  DECL_ARTIFICIAL (ctx_var) = true;
  DECL_IGNORED_P (ctx_var) = true;
  DECL_CONTEXT (ctx_var) = current_function_decl;
  layout_decl (ctx_var, 0);
  DECL_INITIAL (ctx_var) = ctor;
  DECL_CHAIN (ctx_var) = BIND_EXPR_VARS (cc_bind);
  BIND_EXPR_VARS (cc_bind) = ctx_var;
  add_decl_expr (ctx_var);

  /* CTRL is a constant-expression naming a control OBJECT (pre<expr>, or the
     implicit std::contracts::default_v for a bare pre/post/contract_assert):
     constant-evaluate it and use that value, so distinct objects of the same
     type (e.g. carrying different diagnostic data) are preserved instead of
     collapsing to zero.  */
  tree ctrl_type = TREE_TYPE (ctrl);
  tree ctrl_init = cxx_constant_value (ctrl);

  /* Build the control object on the stack; register it.  */
  tree ctrl_var = build_decl (loc, VAR_DECL, NULL_TREE, ctrl_type);
  DECL_ARTIFICIAL (ctrl_var) = true;
  DECL_IGNORED_P (ctrl_var) = true;
  DECL_CONTEXT (ctrl_var) = current_function_decl;
  layout_decl (ctrl_var, 0);
  DECL_INITIAL (ctrl_var) = ctrl_init;
  DECL_CHAIN (ctrl_var) = BIND_EXPR_VARS (cc_bind);
  BIND_EXPR_VARS (cc_bind) = ctrl_var;
  add_decl_expr (ctrl_var);

  tree this_arg = build_fold_addr_expr (ctrl_var);
  tree this_type = TREE_TYPE (DECL_ARGUMENTS (op));
  this_arg = fold_convert (this_type, this_arg);

  tree fn_addr = build_addr_func (op, tf_warning_or_error);
  tree fntype = TREE_TYPE (TREE_TYPE (fn_addr));
  tree result_type = TREE_TYPE (fntype);
  if (SCALAR_TYPE_P (result_type) || VOID_TYPE_P (result_type))
    result_type = cv_unqualified (result_type);

  tree ctx_arg = fold_convert (t_ctx, build_fold_addr_expr (ctx_var));

  tree args[2] = { this_arg, ctx_arg };
  mark_used (op);
  return build_call_array_loc (loc, result_type, fn_addr, 2, args);
}

/* Build a genuine `std::source_location' value (of type SRC_LOC_TYPE, the
   real library class -- not the compiler-internal
   contracts_source_location_impl_type mirror get_src_loc_impl_ptr uses,
   which is a bare pointer, layout-compatible with std::source_location's
   single-pointer-member layout only for runtime/GENERIC purposes, not a
   value of the real class type constant evaluation requires) usable in a
   constexpr-evaluated context.  Mirrors the same lookup/construction
   reflect.cc's eval_source_location_of uses for
   std::meta::source_location_of: find the library's single pointer-typed
   data member (named _M_impl in libstdc++), and initialize it via the real
   __builtin_source_location () intrinsic -- the same one
   std::source_location::current()'s own library implementation calls --
   which the constexpr evaluator already fully supports.  */

static tree
build_real_source_location_value (location_t loc, tree src_loc_type,
				   tree fndecl)
{
  /* A control object's own assertion_context-shaped parameter type isn't
     required to declare a real std::source_location for this field's
     position -- some tests (e.g. d4324-cfg-observe.C) use a bare pointer
     there instead, as a pure layout placeholder for a field they never
     read.  TYPE_FIELDS is only meaningful for a class/record type, so
     check that first, before even attempting the field walk below.  */
  if (!CLASS_TYPE_P (src_loc_type))
    return build_zero_cst (src_loc_type);

  tree field = next_aggregate_field (TYPE_FIELDS (src_loc_type));
  if (!field || !POINTER_TYPE_P (TREE_TYPE (field))
      || next_aggregate_field (DECL_CHAIN (field)))
    return build_constructor (src_loc_type, NULL);

  tree decl = lookup_qualified_name (global_namespace,
				      get_identifier
					("__builtin_source_location"));
  if (TREE_CODE (decl) != FUNCTION_DECL
      || !fndecl_built_in_p (decl, BUILT_IN_FRONTEND)
      || DECL_FE_FUNCTION_CODE (decl) != CP_BUILT_IN_SOURCE_LOCATION
      || !require_deduced_type (decl, tf_warning_or_error))
    return build_constructor (src_loc_type, NULL);

  tree call = build_call_nary (TREE_TYPE (TREE_TYPE (decl)), decl, 0);
  SET_EXPR_LOCATION (call, loc);
  /* fold_builtin_source_location hard-codes current_function_decl to name
     the enclosing function; override it to the real one temporarily (see
     the identical pattern already used in cxx_maybe_build_cleanup,
     constexpr.cc), since current_function_decl itself cannot be relied on
     mid-constant-evaluation.  */
  temp_override<tree> ovr (current_function_decl, fndecl);
  call = fold_builtin_source_location (call);
  return build_constructor_single (src_loc_type, field, call);
}

/* Build a constexpr-eligible `bool (void *)' thunk for CONTRACT's
   condition, for use only by build_contract_control_constexpr_check below.
   Unlike build_predicate_thunk_function (used at genericization time,
   where the thunk may be called much later from arbitrary code and so
   must recover its real arguments through a type-erased pointer), this
   thunk is only ever constant-evaluated immediately, within the very same
   constant evaluation that is still evaluating CONTRACT's enclosing call
   -- so its body can reference CONTRACT_CONDITION's PARM_DECLs directly:
   the constexpr evaluator resolves a PARM_DECL's value by decl identity
   (via the global value map), not by which FUNCTION_DECL it nominally
   belongs to.  The `void *' parameter exists only to match
   assertion_context::__check's bool(*)(void*) field type; it is never
   read.  */

static tree
build_predicate_constexpr_thunk (tree contract)
{
  location_t loc = EXPR_LOCATION (contract);

  tree void_ptr_type = build_pointer_type (void_type_node);
  tree arg_types = tree_cons (NULL_TREE, void_ptr_type, void_list_node);
  tree fn_type = build_function_type (boolean_type_node, arg_types);

  tree name = clone_function_name_numbered ("__contract_consteval_pred",
					     "thunk");
  tree fn = build_lang_decl_loc (loc, FUNCTION_DECL, name, fn_type);
  DECL_CONTEXT (fn) = NULL_TREE;
  DECL_SOURCE_LOCATION (fn) = loc;
  SET_DECL_ASSEMBLER_NAME (fn, name);

  tree parm = build_lang_decl (PARM_DECL, get_identifier ("__unused"),
				void_ptr_type);
  DECL_CONTEXT (parm) = fn;
  DECL_ARTIFICIAL (parm) = true;
  suppress_warning (parm);
  DECL_ARGUMENTS (fn) = parm;
  DECL_RESULT (fn) = NULL_TREE; /* Let start_preparsed_function fill it in.  */

  TREE_STATIC (fn) = 0;
  DECL_ARTIFICIAL (fn) = 1;
  TREE_PUBLIC (fn) = 0;
  DECL_EXTERNAL (fn) = 0;
  DECL_INTERFACE_KNOWN (fn) = 1;
  /* Unlike the runtime-only core/thunk functions, this one must actually be
     usable from a constant expression -- and, unlike them, must NEVER be
     scheduled for real code generation: its body borrows CONTRACT's
     PARM_DECLs verbatim (see the function comment above), which the
     constexpr evaluator resolves by decl identity regardless of which
     FUNCTION_DECL they nominally belong to, but which do not correspond to
     any real storage in this thunk's own (nonexistent) stack frame.  Real
     RTL expansion of this function would try to reference those decls as
     if they belonged to it and crash.  So: no TREE_USED, no mark_used, and
     no expand_or_defer_fn below -- only maybe_save_constexpr_fundef
     (already run inside finish_function, since DECL_DECLARED_CONSTEXPR_P is
     set) is needed to make it constexpr-callable; nothing here should ever
     mark it reachable for ordinary codegen.  */
  DECL_DECLARED_CONSTEXPR_P (fn) = 1;
  suppress_warning (fn);

  /* Reuse CONTRACT_CONDITION's PARM_DECL references verbatim (see the
     function comment above) but duplicate the surrounding expression
     structure, so that finishing this unrelated function can't affect the
     original tree still owned by CONTRACT.  */
  tree condition = unshare_expr (CONTRACT_CONDITION (contract));
  condition = fold_convert (boolean_type_node, condition);

  /* See the matching comment in build_predicate_core_function: this nested
     synthesis must go through push_to_top_level/pop_from_top_level, not a
     bare push_function_context/pop_function_context.  */
  push_to_top_level ();
  start_preparsed_function (fn, NULL_TREE, SF_PRE_PARSED | SF_DEFAULT);
  cp_function_chain->can_throw = true;
  tree body = begin_function_body ();
  finish_return_stmt (condition);
  finish_function_body (body);
  fn = finish_function (/*inline_p=*/false);
  pop_from_top_level ();

  return fn;
}

/* CONTRACT names a control object (CONTRACT_CONTROL_OBJECT is non-NULL).
   Called only from cxx_eval_constant_expression's ASSERTION_STMT/
   PRECONDITION_STMT/POSTCONDITION_STMT case, to make constant evaluation of
   a contract that names a control object actually invoke that object's
   protocol -- is_ignored/operator() -- instead of falling back to the
   built-in TU-evaluation-semantic path the way it used to (that fallback
   remains, unchanged, for CONTRACT_CONTROL_OBJECT == NULL_TREE, i.e.
   -fcontract-control-objects off).

   Returns:
     - void_node if the control object is ignored: both ignored sub-cases
       (assumable or not) collapse to the same "skip entirely, don't touch
       the object at all" outcome here, since there is no
       optimizer-assumption concept during constant evaluation to give the
       assumable case a different, meaningful compile-time behavior.
     - error_mark_node (having already issued the same "no usable
       operator()" diagnostic build_contract_check's runtime path would
       eventually give) if the control type has no usable operator().
     - Otherwise, a BIND_EXPR that faithfully replays the same dispatch
       build_contract_check's control-object branch builds at
       genericization time for the runtime path -- constructing the
       control object and an assertion_context, then calling the control
       object's operator() -- so that C++26 throw/catch semantics around
       ctx.check() behave identically whether the contract is evaluated at
       compile time or run time.  The caller constant-evaluates this
       exactly like any other statement.

   FNDECL is the FUNCTION_DECL whose call is currently being constant
   evaluated (e.g. from the constexpr evaluator's own call-frame tracking,
   ctx->call->fundef->decl) -- current_function_decl itself cannot be
   relied on here, since constant evaluation of a call can happen from a
   context (e.g. a file-scope static_assert) where it is NULL or refers to
   an unrelated function.  */

tree
build_contract_control_constexpr_check (tree contract, tree fndecl,
					 bool quiet)
{
  tree ctrl = CONTRACT_CONTROL_OBJECT (contract);
  gcc_checking_assert (ctrl);
  contract_check_side side = contract_side_of (contract, fndecl);

  if (contract_control_is_ignored (ctrl, side, quiet))
    return void_node;

  tree op = contract_control_operator (ctrl);
  if (!op)
    {
      /* Unlike a genuine contract violation (handled by the caller via the
	 recognized __d4324_consteval_diagnose_violation call, deliberately
	 not quiet-gated to match check_for_failed_contracts), a missing
	 operator() is a malformed-program error, not something
	 manifestly-const-eval-awareness should force into visibility: a
	 merely-quiet trial evaluation (e.g. this same contract reached via
	 an ordinary static const initializer) must stay silent on failure
	 like any other quiet constexpr evaluation attempt, since
	 build_contract_check's own runtime path will raise this same error
	 for real if/when the function is ever actually genericized.  */
      if (!quiet)
	error_at (EXPR_LOCATION (contract),
		  "control object of type %qT has no usable "
		  "%<operator()%>", contract_control_naming_type (ctrl));
      return error_mark_node;
    }

  location_t loc = EXPR_LOCATION (contract);
  tree t_ctx = TREE_VALUE (FUNCTION_FIRST_USER_PARMTYPE (op));
  tree ctx_type = non_reference (t_ctx);

  tree thunk_fn = build_predicate_constexpr_thunk (contract);
  tree check_fn = build_addr_func (thunk_fn, tf_warning_or_error);
  /* Deliberately not mark_used: see the comment in
     build_predicate_constexpr_thunk on why this thunk must never be
     scheduled for real code generation.  */

  tree comment = contract_control_omits_comment (ctrl, side, quiet)
    ? NULL_TREE : CONTRACT_COMMENT (contract);
  if (!comment)
    /* Empty, not null: matches the "static empty string, never a null
       pointer" convention cp-gimplify.cc's build_source_location_impl
       already uses for a missing file/function name.  */
    comment = build_string_literal ("");

  /* Build a genuine `const assertion_context' CONSTRUCTOR directly against
     the real class's own (private) fields -- unlike
     build_contract_control_call's runtime path, which goes through a
     compiler-internal mirror type and a pointer-cast "reinterpret" to
     avoid needing the real class's constructor semantics at GENERIC/gimple
     time.  Constant evaluation enforces the C++ object model strictly, so
     that type-punned mirror value is not usable here; building the real
     type's CONSTRUCTOR instead -- exactly the same low-level mechanism
     already used to construct any other class object internally,
     independent of the class's aggregate-ness or access specifiers --
     sidesteps the type-punning question entirely.  */
  tree f0 = next_aggregate_field (TYPE_FIELDS (ctx_type));
  tree f1 = next_aggregate_field (DECL_CHAIN (f0));
  tree f2 = next_aggregate_field (DECL_CHAIN (f1));
  tree f3 = next_aggregate_field (DECL_CHAIN (f2));
  tree f4 = next_aggregate_field (DECL_CHAIN (f3));
  tree f5 = next_aggregate_field (DECL_CHAIN (f4));
  tree f6 = next_aggregate_field (DECL_CHAIN (f5));
  tree dummy_args_ptr = build_zero_cst (ptr_type_node); /* Never read.  */
  tree ctor = build_constructor_va
    (ctx_type, 7,
     f0, comment,
     f1, (contract_control_omits_source_location (ctrl, side, quiet)
	  ? build_constructor (TREE_TYPE (f1), NULL)
	  : build_real_source_location_value
	      (loc, TREE_TYPE (f1),
	       resolve_fndecl_for_diagnostic_name (fndecl))),
     f2, build_int_cst (TREE_TYPE (f2), contract_evaluation_semantic_value ()),
     f3, build_int_cst (TREE_TYPE (f3), get_contract_assertion_kind (contract)),
     f4, build_assertion_static_info_value (side, TREE_TYPE (f4)),
     f5, fold_convert (TREE_TYPE (f5), dummy_args_ptr),
     f6, fold_convert (TREE_TYPE (f6), check_fn));

  tree ctx_var = build_decl (loc, VAR_DECL, NULL_TREE, ctx_type);
  DECL_ARTIFICIAL (ctx_var) = true;
  DECL_INITIAL (ctx_var) = ctor;
  layout_decl (ctx_var, 0);

  /* CTRL is a constant-expression naming a control OBJECT (pre<expr>, or the
     implicit std::contracts::default_v for a bare pre/post/contract_assert):
     constant-evaluate it and use that value, exactly like
     build_contract_control_call's runtime path.  */
  tree ctrl_type = TREE_TYPE (ctrl);
  tree ctrl_init = cxx_constant_value (ctrl);

  tree ctrl_var = build_decl (loc, VAR_DECL, NULL_TREE, ctrl_type);
  DECL_ARTIFICIAL (ctrl_var) = true;
  DECL_INITIAL (ctrl_var) = ctrl_init;
  layout_decl (ctrl_var, 0);

  tree this_arg = build_fold_addr_expr (ctrl_var);
  tree this_type = TREE_TYPE (DECL_ARGUMENTS (op));
  this_arg = fold_convert (this_type, this_arg);

  tree fn_addr = build_addr_func (op, tf_warning_or_error);
  tree fntype = TREE_TYPE (TREE_TYPE (fn_addr));
  tree result_type = TREE_TYPE (fntype);

  tree ctx_arg = fold_convert (t_ctx, build_fold_addr_expr (ctx_var));

  tree args[2] = { this_arg, ctx_arg };
  mark_used (op);
  tree call = build_call_array_loc (loc, result_type, fn_addr, 2, args);

  /* Package CTRL_VAR/CTX_VAR's declarations and the call into a small,
     self-contained BIND_EXPR: the constexpr evaluator already knows how to
     evaluate BIND_EXPR/DECL_EXPR/VAR_DECL initialization -- that's how it
     evaluates any function body with locals -- so handing it this directly
     is the natural fit for being invoked mid-evaluation, unlike
     build_contract_control_call's runtime path, which splices its
     temporaries into an already-open, enclosing statement list (CC_BIND)
     that doesn't exist here.  */
  DECL_CHAIN (ctrl_var) = ctx_var;
  DECL_CHAIN (ctx_var) = NULL_TREE;

  tree bind = build3 (BIND_EXPR, void_type_node, ctrl_var, NULL_TREE,
		       NULL_TREE);
  tree stmt_list = alloc_stmt_list ();
  append_to_statement_list (build_stmt (loc, DECL_EXPR, ctrl_var), &stmt_list);
  append_to_statement_list (build_stmt (loc, DECL_EXPR, ctx_var), &stmt_list);
  append_to_statement_list (call, &stmt_list);
  BIND_EXPR_BODY (bind) = stmt_list;

  return bind;
}

/* -fcontract-symbolic-runtime-checks: build a CALL_EXPR to the matching
   runtime check_bool/check_range function for one recognized ACTION
   (oa_collect_symbolic_actions) -- LOC is used for the (rare) error if
   the runtime function itself can't be found (missing <contracts>
   include).  Returns error_mark_node on that failure.  */

static tree
oa_build_symbolic_check_call (location_t loc, oa_symbolic_action &action)
{
  if (!action.is_range)
    {
      tree fn = get_symbolic_check_bool_fn (loc);
      if (fn == error_mark_node)
	return error_mark_node;
      tree key = get_symbolic_predicate_key (action.pred_fn);
      tree obj = fold_convert (ptr_type_node, action.bool_arg);
      tree polarity = build_int_cst (boolean_type_node, action.polarity);
      return build_call_n (fn, 3, key, obj, polarity);
    }

  tree fn = get_symbolic_check_range_fn (loc);
  if (fn == error_mark_node)
    return error_mark_node;
  tree key = get_symbolic_field_key (action.field);
  tree obj = fold_convert (ptr_type_node, action.ptr_expr);
  tree has_lo = build_int_cst (boolean_type_node, action.has_lo);
  tree lo = build_int_cst (long_long_integer_type_node, action.lo);
  tree has_hi = build_int_cst (boolean_type_node, action.has_hi);
  tree hi = build_int_cst (long_long_integer_type_node, action.hi);
  return build_call_n (fn, 6, key, obj, has_lo, lo, has_hi, hi);
}

/* -fcontract-symbolic-runtime-checks: build a CALL_EXPR to the matching
   runtime establish_bool/establish_range function for one recognized
   ACTION, to be executed unconditionally (see
   build_symbolic_runtime_check's own comment for why the establishing
   side never goes through the control object at all).  */

static tree
oa_build_symbolic_establish_call (location_t loc, oa_symbolic_action &action)
{
  if (!action.is_range)
    {
      tree fn = get_symbolic_establish_bool_fn (loc);
      if (fn == error_mark_node)
	return error_mark_node;
      tree key = get_symbolic_predicate_key (action.pred_fn);
      tree obj = fold_convert (ptr_type_node, action.bool_arg);
      tree polarity = build_int_cst (boolean_type_node, action.polarity);
      return build_call_n (fn, 3, key, obj, polarity);
    }

  tree fn = get_symbolic_establish_range_fn (loc);
  if (fn == error_mark_node)
    return error_mark_node;
  tree key = get_symbolic_field_key (action.field);
  tree obj = fold_convert (ptr_type_node, action.ptr_expr);
  tree has_lo = build_int_cst (boolean_type_node, action.has_lo);
  tree lo = build_int_cst (long_long_integer_type_node, action.lo);
  tree has_hi = build_int_cst (boolean_type_node, action.has_hi);
  tree hi = build_int_cst (long_long_integer_type_node, action.hi);
  return build_call_n (fn, 6, key, obj, has_lo, lo, has_hi, hi);
}

/* Cache, keyed by the original CONTRACT tree's own identity (mirroring
   contract_predicate_core_fn's own cache immediately above), of the
   "shadow" CONTRACT_STATEMENT synthesized for a symbolic precondition/
   assertion's runtime check: a copy_node of CONTRACT with
   CONTRACT_CONDITION replaced by ACTIONS' check-calls, ANDed together
   in their original order.  Never mutates CONTRACT's own
   CONTRACT_CONDITION -- -fcontract-symbolic-proofs' static analysis
   reads that original, untouched tree separately, and is unaffected by
   this cache existing at all.  */
static GTY(()) hash_map<tree, tree> *symbolic_shadow_contract;

static tree
get_or_build_symbolic_shadow_contract (location_t loc, tree contract,
					vec<oa_symbolic_action> &actions)
{
  if (tree *cached = hash_map_safe_get (symbolic_shadow_contract, contract))
    return *cached;

  tree new_condition = NULL_TREE;
  for (unsigned i = 0; i < actions.length (); ++i)
    {
      tree call = oa_build_symbolic_check_call (loc, actions[i]);
      if (call == error_mark_node)
	return error_mark_node;
      new_condition = new_condition
	? build2 (TRUTH_ANDIF_EXPR, boolean_type_node, new_condition, call)
	: call;
    }

  tree shadow = copy_node (contract);
  CONTRACT_CONDITION (shadow) = new_condition;

  hash_map_maybe_create<hm_ggc> (symbolic_shadow_contract);
  symbolic_shadow_contract->put (contract, shadow);
  return shadow;
}

/* -fcontract-symbolic-runtime-checks ("the gem", see
   ~/gcc-axiom-contracts.md and .claude/plans/stateless-jumping-shore.md):
   build the runtime codegen for a symbolic CONTRACT once
   build_contract_check has determined the new flag is on.  CONDITION is
   CONTRACT_CONDITION (contract) as build_contract_check has already
   remapped it (remap_dummy_this/remap_retval).

   A postcondition unconditionally records each recognized conjunct's
   establishment and nothing else -- no control-object dispatch, no
   violation path, no thunk at all: per the document's own "isn't really
   a check... simply a straight-up establishment."

   A precondition or assertion instead goes through the ordinary
   control-object dispatch machinery (get_or_build_predicate_core_
   function et al.) completely unchanged, fed a synthesized shadow
   contract whose condition calls the matching runtime check function
   for each conjunct instead of the original (possibly bodiless)
   predicate -- per the document's "the runtime code that checks a
   symbolic pre() should invoke the control object's operator()."  */

static tree
build_symbolic_runtime_check (tree contract, tree ctrl, location_t loc,
			       tree condition)
{
  auto_vec<oa_symbolic_action> actions;
  if (!oa_collect_symbolic_actions (loc, condition, &actions))
    return error_mark_node;

  tree cc_bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, NULL);
  BIND_EXPR_BODY (cc_bind) = push_stmt_list ();

  if (TREE_CODE (contract) == ASSERTION_STMT)
    emit_builtin_observable_checkpoint ();

  if (POSTCONDITION_P (contract))
    {
      for (unsigned i = 0; i < actions.length (); ++i)
	{
	  tree call = oa_build_symbolic_establish_call (loc, actions[i]);
	  if (call == error_mark_node)
	    return error_mark_node;
	  finish_expr_stmt (call);
	}
    }
  else if (!actions.is_empty ())
    {
      /* If every conjunct was deferred to Mechanism B (oa_collect_
	 symbolic_actions silently skipping a bare-parameter comparison
	 -- see its own comment), ACTIONS is empty here: Mechanism A has
	 nothing left to check at all for this contract, so building a
	 shadow contract (whose condition would be NULL_TREE, an empty
	 ANDIF chain) and dispatching through it would be both pointless
	 and unsound to even attempt -- skip entirely, leaving this
	 contract's whole runtime verification to Mechanism B's own,
	 separate, per-call-site obligation check instead.  */
      tree shadow = get_or_build_symbolic_shadow_contract (loc, contract,
							    actions);
      if (shadow == error_mark_node)
	return error_mark_node;

      tree control_op = contract_control_operator (ctrl);
      tree core_fn = get_or_build_predicate_core_function (shadow,
							    current_function_decl);
      tree struct_type = build_predicate_arg_struct_type (core_fn, loc);
      tree thunk_fn = build_predicate_thunk_function (contract, core_fn,
						      struct_type);
      tree args_ptr = build_predicate_arg_struct_var (contract, struct_type,
						       cc_bind, loc);
      finish_expr_stmt (build_contract_control_call (contract, ctrl, control_op,
						      cc_bind, args_ptr,
						      thunk_fn));
    }

  BIND_EXPR_BODY (cc_bind) = pop_stmt_list (BIND_EXPR_BODY (cc_bind));
  return cc_bind;
}

/* Genericize a CONTRACT tree, but do not attach it to the current context,
   the caller is responsible for that.
   This is called during genericization.  */

tree
build_contract_check (tree contract)
{
  /* A specifier synthesized by maybe_inherit_virtual_contract has its
     real condition resolved lazily, here, rather than when first
     synthesized -- see resolve_inherited_contract's comment for why.  */
  {
    tree basefn, base_contract;
    if (contract_inherited_pending_p (contract, &basefn, &base_contract))
      resolve_inherited_contract (contract, current_function_decl,
				  basefn, base_contract);
  }

  /* D4324 step 3: rewrite any std::contracts::base_contract<Base>()
     calls the user wrote explicitly in this condition -- see
     resolve_base_contract_calls's comment for why this also has to
     happen here, at genericization time.  */
  resolve_base_contract_calls (contract, current_function_decl);

  tree ctrl = CONTRACT_CONTROL_OBJECT (contract);
  contract_check_side side
    = contract_side_of (contract, current_function_decl);

  /* D4324 step 1: a named control object decides, at compile time, whether
     this assertion is ignored for the TU's evaluation_semantic.  An ignored
     assertion emits no runtime check; if the control object's type is also
     assumable the predicate is handed to the optimizer as an assumption
     (evaluated by no one at runtime) instead.  */
  bool ignored = contract_control_is_ignored (ctrl, side);
  bool assumable = ignored && contract_control_assumable (ctrl, side);

  /* A named control object must provide operator() unconditionally, even
     when ignored/assumable -- so whether it's required never depends on
     the TU's compiled evaluation_semantic.  Only "no control object at all"
     (CTRL null, -fcontract-control-objects off) still falls through to the
     built-in evaluation-semantic switch below.  */
  tree control_op = NULL_TREE;
  if (ctrl)
    {
      control_op = contract_control_operator (ctrl);
      if (!control_op)
	{
	  error_at (EXPR_LOCATION (contract),
		    "control object of type %qT has no usable "
		    "%<operator()%>", contract_control_naming_type (ctrl));
	  return error_mark_node;
	}

      /* force_client_side_check and force_definition_side_check are
	 mutually exclusive: each names the one side this contract may run
	 on.  contract_runs_on_side deterministically routes a
	 (misconfigured) contract with both set to ccs_definition only, so
	 this fires exactly once for it, here.  */
      if (contract_control_forces_client_side (ctrl, side)
	  && contract_control_forces_definition_side (ctrl, side))
	{
	  error_at (EXPR_LOCATION (contract),
		    "control object of type %qT has both "
		    "%<force_client_side_check%> and "
		    "%<force_definition_side_check%> set to %<true%>",
		    contract_control_naming_type (ctrl));
	  return error_mark_node;
	}
    }

  if (ignored && !assumable)
    return void_node;

  /* Axiom/symbolic contracts (see ~/gcc-axiom-contracts.md): by default,
     a pre/post written against a control object whose is_symbolic(cfg)
     returns true has no runtime representation at all -- no predicate
     thunk, no arg struct, no control-object operator() call -- the
     contract exists purely for the (optional) static analyzer
     (-fcontract-symbolic-proofs) to consult from the untouched
     CONTRACT_CONDITION this function leaves behind pre-genericize.
     Unlike the ignored/assumable case above, there is no "hand it to
     the optimizer as an assumption" alternative here: this is a
     separate axis from evaluation_semantic entirely, so the check is
     unconditional, not gated on assumability.  -fcontract-symbolic-
     runtime-checks ("the gem") opts into real runtime codegen instead
     -- see build_symbolic_runtime_check -- dispatched below, once
     CONDITION has gone through the same remap_dummy_this/remap_retval
     processing as every other contract.  */
  bool symbolic_runtime = false;
  if (ctrl && contract_control_symbolic_like (ctrl, side))
    {
      if (!flag_contract_symbolic_runtime_checks)
	return void_node;
      symbolic_runtime = true;
    }

  contract_evaluation_semantic semantic = CES_ENFORCE;
  bool quick = false;
  bool calls_handler = false;
  if (!ctrl)
    {
      semantic = get_evaluation_semantic (contract);
      switch (semantic)
	{
	case CES_IGNORE:
	  return void_node;
	case CES_ENFORCE:
	case CES_OBSERVE:
	  calls_handler = true;
	  break;
	case CES_QUICK:
	  quick = true;
	  break;
	default:
	  gcc_unreachable ();
	}
    }

  location_t loc = EXPR_LOCATION (contract);

  remap_dummy_this (current_function_decl, &CONTRACT_CONDITION (contract));
  tree condition = CONTRACT_CONDITION (contract);
  if (condition == error_mark_node)
    return NULL_TREE;

  if (!flag_contract_checks_outlined && POSTCONDITION_P (contract))
    {
      remap_retval (current_function_decl, contract);
      condition = CONTRACT_CONDITION (contract);
      if (condition == error_mark_node)
	return NULL_TREE;
    }

  if (symbolic_runtime)
    return build_symbolic_runtime_check (contract, ctrl, loc, condition);

  /* D4324 step 1, assumable: emit an optimizer assumption over the predicate
     rather than a runtime check.  IFN_ASSUME does not evaluate the predicate
     at runtime, so this stays zero-cost while letting the optimizer simplify
     downstream code.  */
  if (assumable)
    return build_assume_call (loc, condition);

  tree terminate_wrapper = terminate_fn;
  if (flag_contracts_conservative_ipa)
    terminate_wrapper = declare_terminate_wrapper ();
  if (calls_handler)
    declare_violation_handler_wrappers ();

  /* Build a statement expression to hold a contract check, with the check
     potentially wrapped in a try-catch expr (P2900 mode only).  */
  tree cc_bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, NULL);
  BIND_EXPR_BODY (cc_bind) = push_stmt_list ();

  if (TREE_CODE (contract) == ASSERTION_STMT)
    emit_builtin_observable_checkpoint ();

  if (control_op)
    {
      /* D4324 step 2/3: unconditionally call the control object, handing it
	 a callback that evaluates the predicate on demand -- via a struct of
	 pointers to the real arguments and a thunk that unpacks it and calls
	 the outlined predicate function -- instead of the compiler evaluating
	 the predicate itself and only calling the control object on
	 violation.  */
      tree core_fn = get_or_build_predicate_core_function (contract,
							    current_function_decl);
      tree struct_type = build_predicate_arg_struct_type (core_fn, loc);
      tree thunk_fn = build_predicate_thunk_function (contract, core_fn,
						      struct_type);
      tree args_ptr = build_predicate_arg_struct_var (contract, struct_type,
						       cc_bind, loc);
      finish_expr_stmt (build_contract_control_call (contract, ctrl, control_op,
						      cc_bind, args_ptr,
						      thunk_fn));
    }
  else
    {
      /* The bare (non control-object) path evaluates the predicate eagerly
	 and, only if it's false, builds a violation object and calls the
	 built-in handler; in the default P2900 mode it may wrap a throwing
	 predicate in a try/catch that translates the exception into a
	 violation.  */
      tree cond = build_x_unary_op (loc, TRUTH_NOT_EXPR, condition, NULL_TREE,
				    tf_warning_or_error);

      tree violation = NULL_TREE;
      if (quick)
	/* We will not be calling a handler.  */
	violation = build_zero_cst (nullptr_type_node);
      else
	{
	  /* Build a violation object, with the contract settings.  */
	  tree ctor = build_contract_violation_ctor (contract);
	  gcc_checking_assert (TREE_CONSTANT (ctor));
	  violation = build_contract_violation_constant (ctor, contract);
	  violation = build_address (violation);
	}
      tree s_const = build_int_cst (uint16_type_node, semantic);

      /* P2900 only: translate an exception thrown while evaluating the
	 predicate into a violation.  D4324 leaves it to propagate to the
	 nearest noexcept boundary, so noexcept keeps its meaning.  */
      bool check_might_throw = !flag_contract_control_objects
	&& flag_exceptions && !expr_noexcept_p (condition, tf_none);
      if (check_might_throw)
	{
	  bool viol_is_var = false;
	  /* This will hold the computed condition.  */
	  tree check_failed = build_decl (loc, VAR_DECL, NULL,
					  boolean_type_node);
	  DECL_ARTIFICIAL (check_failed) = true;
	  DECL_IGNORED_P (check_failed) = true;
	  DECL_CONTEXT (check_failed) = current_function_decl;
	  layout_decl (check_failed, 0);
	  add_decl_expr (check_failed);
	  DECL_CHAIN (check_failed) = BIND_EXPR_VARS (cc_bind);
	  BIND_EXPR_VARS (cc_bind) = check_failed;
	  tree check_try = begin_try_block ();
	  finish_expr_stmt (cp_build_init_expr (check_failed, cond));
	  finish_try_block (check_try);

	  tree handler = begin_handler ();
	  finish_handler_parms (NULL_TREE, handler); /* catch (...) */
	  if (quick)
	    finish_expr_stmt (build_call_a (terminate_wrapper, 0, nullptr));
	  else
	    {
	      if (viol_is_var)
		{
		  /* We can update the detection mode here.  */
		  tree memb
		    = lookup_member (builtin_contract_violation_type,
				     get_identifier ("_M_detection_mode"),
				     1, 0, tf_warning_or_error);
		  tree r = cp_build_indirect_ref (loc, violation, RO_UNARY_STAR,
						  tf_warning_or_error);
		  r = build_class_member_access_expr (r, memb, NULL_TREE, false,
						      tf_warning_or_error);
		  r = cp_build_modify_expr
		    (loc, r, NOP_EXPR,
		     build_int_cst (uint16_type_node,
				    (uint16_t)CDM_EVAL_EXCEPTION),
		     tf_warning_or_error);
		  finish_expr_stmt (r);
		  finish_expr_stmt (build_call_n (tu_has_violation, 2,
						  violation, s_const));
		}
	      else
		/* We need to make a copy of the violation object to update.  */
		finish_expr_stmt (build_call_n (tu_has_violation_exception, 2,
						violation, s_const));
	      /* If we reach here, we have handled the exception thrown and do
		 not need further action.  */
	      tree e = cp_build_modify_expr (loc, check_failed, NOP_EXPR,
					     boolean_false_node,
					     tf_warning_or_error);
	      finish_expr_stmt (e);
	    }
	  finish_handler (handler);
	  finish_handler_sequence (check_try);
	  cond = check_failed;
	  BIND_EXPR_VARS (cc_bind) = nreverse (BIND_EXPR_VARS (cc_bind));
	}

      tree do_check = begin_if_stmt ();
      finish_if_stmt_cond (cond, do_check);
      if (quick)
	/* We will not be calling a handler.  */
	finish_expr_stmt (build_call_a (terminate_wrapper, 0, nullptr));
      else
	finish_expr_stmt (build_call_n (tu_has_violation, 2, violation, s_const));
      finish_then_clause (do_check);
      finish_if_stmt (do_check);
    }

  BIND_EXPR_BODY (cc_bind) = pop_stmt_list (BIND_EXPR_BODY (cc_bind));
  return cc_bind;
}

#include "gt-cp-contracts.h"
