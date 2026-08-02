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

static bool contract_control_is_ignored (tree, contract_check_side);
static bool contract_control_assumable (tree, contract_check_side);
static bool contract_control_forces_client_side (tree, contract_check_side);
static bool contract_control_forces_definition_side (tree, contract_check_side);
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
	&& contract_control_is_conveyor (CONTRACT_CONTROL_OBJECT (contract),
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

static tree
get_src_loc_impl_ptr_for (location_t loc, tree fndecl)
{
  if (!contracts_source_location_impl_type)
    get_contracts_source_location_impl_type ();

  /* We might be an outlined function.  */
  if (DECL_IS_PRE_FN_P (fndecl) || DECL_IS_POST_FN_P (fndecl))
    fndecl = get_orig_for_outlined (fndecl);
  /* We might be a wrapper.  */
  if (DECL_IS_WRAPPER_FN_P (fndecl))
    fndecl = get_orig_func_for_wrapper (fndecl);

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
   under -fcontract-control-objects), or NULL_TREE.  The
   is_ignored/constify/assumable/operator() members looked up below are
   always static, so only CTRL's type is ever needed for member lookup;
   return it.  */

static tree
contract_control_naming_type (tree ctrl)
{
  return ctrl ? TREE_TYPE (ctrl) : NULL_TREE;
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
    (info_type, 4,
     f0, build_int_cst (TREE_TYPE (f0), contract_evaluation_semantic_value ()),
     f1, build_int_cst (TREE_TYPE (f1), side_val),
     f2, boolean_false_node,
     f3, boolean_false_node);
}

/* Constant-evaluate CTRL::NAME(info) for the current translation unit's
   evaluation_semantic and the given SIDE, where NAME is a static member
   function taking a single std::contracts::assertion_static_info parameter
   (e.g. is_ignored, constify, assumable, omit_comment, ...).  Returns 1 if
   it folds to a compile-time true, 0 if it folds to false, and -1 if CTRL
   has no such usable compile-time member (no member by that name, not a
   static function, not callable with one assertion_static_info argument,
   or doesn't constant-fold to a bool).  */

static int
contract_control_bool_member (tree ctrl, const char *name,
			       contract_check_side side)
{
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
  if (!fn || TREE_CODE (fn) != FUNCTION_DECL || !DECL_STATIC_FUNCTION_P (fn))
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
     graceful failure), so that assumption must be checked here first.  */
  tree parm_types = TYPE_ARG_TYPES (TREE_TYPE (fn));
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
  tree obj = build_dummy_object (ctrl);
  tree call = build_new_method_call (obj, member, &args, NULL_TREE,
				     LOOKUP_NORMAL, NULL, tf_none);
  if (!call || call == error_mark_node)
    return -1;

  tree val = maybe_constant_value (call);
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
contract_control_is_ignored (tree ctrl, contract_check_side side)
{
  return contract_control_bool_member (ctrl, "is_ignored", side) == 1;
}

/* True if the control type CTRL opts into constification
   (constify(cfg) == true for the TU's evaluation_semantic).  A bare
   contract, or a control type without a usable constify getter, does not
   constify.  */

bool
contract_control_constifies (tree ctrl, contract_check_side side)
{
  return contract_control_bool_member (ctrl, "constify", side) == 1;
}

/* True if the control type CTRL's is_conveyor(cfg) returns true for the
   TU's evaluation_semantic, meaning the predicate itself (not CTRL's own
   operator()) must satisfy the D4324 conveyor-function syntactic
   restrictions.  A bare contract, or a control type without a usable
   is_conveyor getter, is not conveyor.  */

bool
contract_control_is_conveyor (tree ctrl, contract_check_side side)
{
  return contract_control_bool_member (ctrl, "is_conveyor", side) == 1;
}

/* True if the control type CTRL's assumable(cfg) returns true for the TU's
   evaluation_semantic, meaning an ignored predicate may be handed to the
   optimizer as an assumption.  */

static bool
contract_control_assumable (tree ctrl, contract_check_side side)
{
  return contract_control_bool_member (ctrl, "assumable", side) == 1;
}

/* True if the control type CTRL's omit_comment(cfg) returns true for the
   TU's evaluation_semantic, meaning it never needs the assertion's
   stringified condition text, so the compiler should not embed it at all.
   Optional: a control type without this getter (or one that returns false)
   keeps the existing behaviour of always storing it.  */

static bool
contract_control_omits_comment (tree ctrl, contract_check_side side)
{
  return contract_control_bool_member (ctrl, "omit_comment", side) == 1;
}

/* True if the control type CTRL's omit_source_location(cfg) returns true
   for the TU's evaluation_semantic, meaning it never needs the assertion's
   std::source_location, so the compiler should not build/embed one at all.
   Optional, same default-false behaviour as contract_control_omits_comment.  */

static bool
contract_control_omits_source_location (tree ctrl, contract_check_side side)
{
  return contract_control_bool_member (ctrl, "omit_source_location", side) == 1;
}

/* True if the control type CTRL's force_client_side_check(cfg) returns true
   for the TU's evaluation_semantic, meaning any contract naming it is
   checked only via the caller-side (client) wrapper mechanism, never at the
   function's own definition, regardless of
   -fcontracts-client-check/-fcontracts-definition-check.  Optional, same
   default-false behaviour as contract_control_omits_comment.  */

static bool
contract_control_forces_client_side (tree ctrl, contract_check_side side)
{
  return contract_control_bool_member (ctrl, "force_client_side_check", side) == 1;
}

/* True if the control type CTRL's force_definition_side_check(cfg) returns
   true for the TU's evaluation_semantic, the mirror image of
   contract_control_forces_client_side: any contract naming it is checked
   only at the function's own definition, never via a caller-side wrapper.  */

static bool
contract_control_forces_definition_side (tree ctrl, contract_check_side side)
{
  return contract_control_bool_member (ctrl, "force_definition_side_check", side) == 1;
}

/* True if the control type CTRL's inherited(cfg) returns true for SIDE,
   meaning a contract-less override of the function this contract is
   attached to should behave, on that side, as if it had explicitly
   declared the identical contract (same control object).  See
   maybe_inherit_virtual_contract.  */

static bool
contract_control_inherited (tree ctrl, contract_check_side side)
{
  return contract_control_bool_member (ctrl, "inherited", side) == 1;
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
   synthesize_inherited_specifier), and if so, set *BASEFN/*BASE_CONTRACT
   to what it's pending against.  Once resolve_inherited_contract
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
   of the actual integer type being reasoned about.  */

struct oa_range_fact
{
  tree base;
  bool has_lo, has_hi;
  widest_int lo, hi;
};

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

  oa_env copy ()
  {
    oa_env r;
    for (auto it : m_map)
      r.m_map.put (it.first, it.second);
    for (auto it : m_nz_map)
      r.m_nz_map.put (it.first, it.second);
    for (auto it : m_range_map)
      r.m_range_map.put (it.first, it.second);
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
    m_range_map.empty ();
    for (auto it : other.m_range_map)
      m_range_map.put (it.first, it.second);
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
  hash_map<tree, oa_range_fact> m_range_map;
};

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
static bool oa_call_postcondition_range_p (tree call, oa_range_fact *out);

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

  while (TREE_CODE (expr) == NON_LVALUE_EXPR
	 || TREE_CODE (expr) == NOP_EXPR
	 || TREE_CODE (expr) == CONVERT_EXPR)
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

  while (TREE_CODE (expr) == NON_LVALUE_EXPR
	 || TREE_CODE (expr) == NOP_EXPR
	 || TREE_CODE (expr) == CONVERT_EXPR)
    expr = TREE_OPERAND (expr, 0);

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

/* Forward-declared: oa_refine_range_for_condition (defined below)
   decomposes a condition at top-level && the same way contract
   conditions already are, via this existing helper defined later in
   the file.  */
static void oa_collect_conjuncts (tree *cond, vec<tree *> *conjuncts);

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

  while (TREE_CODE (expr) == NON_LVALUE_EXPR
	 || TREE_CODE (expr) == NOP_EXPR
	 || TREE_CODE (expr) == CONVERT_EXPR)
    expr = TREE_OPERAND (expr, 0);

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
	  tree arr = TREE_OPERAND (op, 0);
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
     instead of recovering the intended negative byte count.  */
  if (TREE_CODE (expr) == POINTER_PLUS_EXPR)
    {
      tree ptr = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 0));
      tree byte_off = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (expr, 1));
      if (TREE_CODE (byte_off) != INTEGER_CST)
	return false;

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

      widest_int byte_k = widest_int::from (wi::to_offset (byte_off),
					     SIGNED);
      widest_int rem;
      widest_int elt_k = wi::divmod_trunc (byte_k, elt_size, SIGNED, &rem);
      if (rem != 0)
	/* Not an exact multiple of the pointee's size -- e.g. a
	   reinterpreted or otherwise non-array-normalized offset.
	   Conservatively unprovable rather than guessing.  */
	return false;

      out->base = ptr_fact.base;
      out->has_lo = ptr_fact.has_lo;
      out->has_hi = ptr_fact.has_hi;
      if (out->has_lo)
	out->lo = ptr_fact.lo + elt_k;
      if (out->has_hi)
	out->hi = ptr_fact.hi + elt_k;
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
  if (TREE_CODE (expr) == CALL_EXPR && oa_call_postcondition_range_p (expr, out))
    return true;

  return false;
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

static void
oa_refine_single_comparison (tree conjunct, oa_env &env, bool asserted_true)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));

  enum tree_code code = TREE_CODE (c);
  if (code != LT_EXPR && code != LE_EXPR && code != GT_EXPR
      && code != GE_EXPR && code != EQ_EXPR)
    return;

  tree op0 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
  tree op1 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 1));

  /* An assignment written directly as this comparison's own operand
     ('(i = compute ()) > 0') evaluates to its LHS's newly assigned
     value -- so for refinement purposes it plays the exact same role
     the bare decl would.  Safe to unwrap here (into DECL0/DECL1, kept
     separate from OP0/OP1 themselves): this function is only ever
     invoked (via oa_refine_range_for_condition) on the top-level
     condition or a top-level &&-conjunct of it, so reaching the
     then-branch already guarantees this operand's assignment was
     actually evaluated. Deliberately does *not* also substitute this
     unwrapped form into the *other* (non-decl) operand's role below --
     if a comparison assigns on both sides ('(i = f()) > (j = g())'),
     the non-tracked side's own freshly assigned value is unknown here
     (oa_track_condition_assignment only ever tracks one, the first
     found), and must not be resolved via ENV's possibly-stale prior
     fact for that decl.  */
  tree decl0 = (TREE_CODE (op0) == INIT_EXPR || TREE_CODE (op0) == MODIFY_EXPR)
    ? STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (op0, 0)) : op0;
  tree decl1 = (TREE_CODE (op1) == INIT_EXPR || TREE_CODE (op1) == MODIFY_EXPR)
    ? STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (op1, 0)) : op1;

  tree decl, other;
  bool flipped;
  if ((VAR_P (decl0) || TREE_CODE (decl0) == PARM_DECL)
      && INTEGRAL_TYPE_P (TREE_TYPE (decl0)))
    decl = decl0, other = op1, flipped = false;
  else if ((VAR_P (decl1) || TREE_CODE (decl1) == PARM_DECL)
	   && INTEGRAL_TYPE_P (TREE_TYPE (decl1)))
    decl = decl1, other = op0, flipped = true;
  else
    return;

  oa_range_fact other_fact;
  if (!oa_get_range (other, env, &other_fact) || other_fact.base != NULL_TREE
      || !other_fact.has_lo || !other_fact.has_hi
      || other_fact.lo != other_fact.hi)
    /* The comparison's other side must resolve to a single, exactly-
       known point (a literal, or a decl whose own range is already an
       exact point) -- comparing against a genuine, non-degenerate
       range on both sides is a further generalization not attempted in
       E1.  */
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
      return;
    }
  env.range_set (decl, refined);
}

/* D4324/P2680 item 8, Increment E1: refine THEN_ENV/ELSE_ENV's range
   facts from COND, the condition of an IF_STMT/COND_EXPR (or,
   symmetrically, a loop's own condition -- Increment E3). A top-level
   '&&' conjunct chain is decomposed the same way oa_collect_conjuncts
   already does for contract conditions, applying each conjunct's
   then-refinement in sequence (sound: all conjuncts must hold for the
   then-branch to be reached) -- the else-branch of a *compound*
   condition is deliberately never refined at all (De Morgan's gives a
   disjunction of negations, not a single conjunction representable the
   same way; conservatively left unconstrained, the same discipline
   used throughout this pass). A single, non-compound condition refines
   both branches.  */

static void
oa_refine_range_for_condition (tree cond, oa_env &then_env, oa_env &else_env)
{
  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts (&cond, &conjuncts);
  for (unsigned i = 0; i < conjuncts.length (); ++i)
    oa_refine_single_comparison (*conjuncts[i], then_env, /*asserted_true=*/true);
  if (conjuncts.length () == 1)
    oa_refine_single_comparison (cond, else_env, /*asserted_true=*/false);
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
   either *is* the condition itself, or is directly nested as the
   operand of a comparison/negation wrapping it ('(i = compute()) > 0',
   '(p = f()) != nullptr', '!(p = f())') -- never one nested inside a
   '&&'/'||' chain, where whether the assignment actually executes at
   all depends on short-circuit evaluation of an earlier operand.
   Blindly tracking a fact from an assignment that might not have run
   would be unsound; this restriction avoids that question entirely by
   simply not looking there. Reuses the *tracking* logic oa_walk_stmt's
   own INIT_EXPR/MODIFY_EXPR case already has (oa_provable_p/oa_
   provably_nonzero_p/oa_get_range), not its scanning logic (already
   run separately, once, on the whole condition by the caller).  */

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
    }
}

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
			    const char *what, tree diag_expr)
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
  widest_int max_w = wi::to_widest (max);
  if (total.lo < 0 || total.hi > max_w)
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

   An INDIRECT_REF ('*p', no subscript syntax) is different: a bare
   dereference is common and legitimate for perfectly ordinary,
   non-array-related pointers (whose validity is is_object_address's
   separate concern entirely) -- so this is only ever checked *when P
   already carries a tracked array-offset fact* (Increment E2): only
   then does "was this dereference formed via array-related pointer
   arithmetic, and is the offset still in range" actually apply. A
   plain pointer with no such fact is silently left alone here.

   Only meaningful within a function actually declared with the
   'conveyor' keyword -- checked by the caller, not here.  */

static void
oa_scan_array_bounds_in_expr (tree *expr, oa_env &env)
{
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
	  if (!oa_get_range (base, *e, &base_fact) || base_fact.base == NULL_TREE)
	    /* No tracked array-offset fact at all -- an ordinary
	       dereference, not something this rule is about; silently
	       left alone (is_object_address's separate mechanism is
	       what validates a plain pointer's own basic validity).  */
	    return NULL_TREE;
	  oa_check_offset_in_bounds (t, TREE_TYPE (base_fact.base), base_fact,
				     "pointer dereference", NULL_TREE);
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
   was found unprovable, or was found outside a conveyor/non-ignored
   context (CONVEYOR_OK false means this whole COND is not itself
   inside a conveyor-checked predicate, so any is_object_address found
   here at all is a well-formedness error, provable or not).  */

static bool
oa_resolve_condition (tree *cond, oa_env &env, bool conveyor_ok,
		      bool trust = false)
{
  bool ok = true;

  struct walk_data { oa_env *env; bool conveyor_ok; bool trust; bool *ok; };
  walk_data data = { &env, conveyor_ok, trust, &ok };

  cp_walk_tree (cond, [](tree *tp, int *, void *data_) -> tree
    {
      walk_data *d = (walk_data *) data_;
      tree arg;
      if (!is_object_address_call_p (*tp, &arg))
	return NULL_TREE;

      if (!d->conveyor_ok)
	{
	  error_at (EXPR_LOCATION (*tp),
		    "%<std::is_object_address%> may only be used inside "
		    "a conveyor-checked predicate");
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
   position inside a larger boolean expression.  */

static void
oa_collect_conjuncts (tree *cond, vec<tree *> *conjuncts)
{
  tree c = *cond;
  if (c && (TREE_CODE (c) == TRUTH_ANDIF_EXPR
	    || TREE_CODE (c) == TRUTH_AND_EXPR))
    {
      oa_collect_conjuncts (&TREE_OPERAND (c, 0), conjuncts);
      oa_collect_conjuncts (&TREE_OPERAND (c, 1), conjuncts);
      return;
    }
  conjuncts->safe_push (cond);
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

static bool
oa_nonzero_conjunct_p (tree conjunct, tree *decl_out)
{
  tree c = STRIP_ANY_LOCATION_WRAPPER (conjunct);
  while (TREE_CODE (c) == CLEANUP_POINT_EXPR)
    c = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
  if (TREE_CODE (c) != NE_EXPR)
    return false;

  tree op0 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 0));
  tree op1 = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (c, 1));
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
  if (!contract_control_is_conveyor (ctrl, side))
    return false;
  return !contract_control_is_ignored (ctrl, side);
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
			"required by %qD's precondition", substituted, callee);
	      inform (DECL_SOURCE_LOCATION (callee), "declared here");
	    }
	}
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

static bool
oa_call_postcondition_range_p (tree call, oa_range_fact *out)
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
      auto_vec<tree *> conjuncts;
      oa_collect_conjuncts (&cond, &conjuncts);
      for (unsigned i = 0; i < conjuncts.length (); ++i)
	oa_refine_single_comparison (*conjuncts[i], scratch, /*asserted_true=*/true);
    }
  if (!any || !result_id)
    return false;
  return scratch.range_get (result_id, out);
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

static void
oa_scan_calls_in_expr (tree *expr, oa_env &env)
{
  cp_walk_tree (expr, [](tree *tp, int *, void *data_) -> tree
    {
      oa_env *e = (oa_env *) data_;
      tree t = *tp;
      if (t == NULL_TREE || t == error_mark_node || TREE_CODE (t) != CALL_EXPR)
	return NULL_TREE;
      tree arg;
      if (is_object_address_call_p (t, &arg))
	return NULL_TREE;
      oa_handle_call_precondition_obligation (t, *e);
      return NULL_TREE;
    }, &env, NULL);
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
	      "only be used directly inside a conveyor-checked "
	      "%<contract_assert%>, %<pre%>, or %<post%> condition");
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
   first.  */

static void
oa_handle_precondition_stmt (tree contract, oa_env &env)
{
  bool conveyor_ok = oa_contract_conveyor_active_p (contract);
  tree cond = CONTRACT_CONDITION (contract);
  if (cond == NULL_TREE || cond == error_mark_node)
    return;

  auto_vec<tree *> conjuncts;
  oa_collect_conjuncts (&cond, &conjuncts);
  auto_vec<tree> facts;
  auto_vec<tree> nz_facts;
  if (conveyor_ok)
    for (unsigned i = 0; i < conjuncts.length (); ++i)
      {
	tree arg;
	if (is_object_address_call_p (*conjuncts[i], &arg))
	  facts.safe_push (STRIP_ANY_LOCATION_WRAPPER (arg));
	else if (oa_nonzero_conjunct_p (*conjuncts[i], &arg))
	  nz_facts.safe_push (arg);
      }

  if (!oa_resolve_condition (&cond, env, conveyor_ok, /*trust=*/true))
    {
      CONTRACT_CONDITION (contract) = error_mark_node;
      return;
    }
  CONTRACT_CONDITION (contract) = cond;

  if (!conveyor_ok)
    return;

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
     same thing as a then-branch refinement.  Silently does nothing for
     any conjunct shape it doesn't recognize (already covered above, or
     neither), so it's safe to call unconditionally over every
     conjunct.  */
  if (conveyor_ok)
    for (unsigned i = 0; i < conjuncts.length (); ++i)
      oa_refine_single_comparison (*conjuncts[i], env, /*asserted_true=*/true);
}

/* Forward-declared: the statement walker recurses into itself, and into
   the postcondition handler defined below it (which needs the return-
   tracking globals the walker itself accumulates).  */
static void oa_walk_stmt (tree *stmt, oa_env &env);
static void oa_handle_postcondition_stmt (tree contract);

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
   a later increment will add).  */

static void
oa_handle_assertion_stmt (tree stmt, oa_env &env)
{
  bool conveyor_ok = oa_contract_conveyor_active_p (stmt);
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
  auto_vec<tree> facts;
  auto_vec<tree> nz_facts;
  if (conveyor_ok)
    for (unsigned i = 0; i < conjuncts.length (); ++i)
      {
	tree arg;
	if (is_object_address_call_p (*conjuncts[i], &arg))
	  facts.safe_push (STRIP_ANY_LOCATION_WRAPPER (arg));
	else if (oa_nonzero_conjunct_p (*conjuncts[i], &arg))
	  nz_facts.safe_push (arg);
      }

  if (!oa_resolve_condition (&cond, env, conveyor_ok))
    {
      CONTRACT_CONDITION (stmt) = error_mark_node;
      return;
    }
  CONTRACT_CONDITION (stmt) = cond;

  if (!conveyor_ok)
    return;

  for (unsigned i = 0; i < facts.length (); ++i)
    {
      tree e = facts[i];
      if (VAR_P (e) || TREE_CODE (e) == PARM_DECL)
	env.set (e, true);
    }
  for (unsigned i = 0; i < nz_facts.length (); ++i)
    env.nz_set (nz_facts[i], true);
  /* Increment E4: the same comparison-shaped-conjunct fact-seeding as
     oa_handle_precondition_stmt above, applied here too -- a preceding,
     conveyor, non-ignored contract_assert's own comparison conjunct
     establishes a usable range fact for later code, the same escape
     hatch already used for is_object_address/nonzero-ness.  */
  if (conveyor_ok)
    for (unsigned i = 0; i < conjuncts.length (); ++i)
      oa_refine_single_comparison (*conjuncts[i], env, /*asserted_true=*/true);
}

/* Collect, into PTR_OUT/NZ_OUT (each deduplicated), every pointer-typed
   (respectively integer-typed) VAR_DECL/PARM_DECL that is the target of
   a plain INIT_EXPR/MODIFY_EXPR assignment anywhere within *STMT -- a
   plain syntactic scan, independent of provability, used only to
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
   there).  */

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
      if (TREE_CODE (t) != INIT_EXPR && TREE_CODE (t) != MODIFY_EXPR)
	return NULL_TREE;
      tree lhs = STRIP_ANY_LOCATION_WRAPPER (TREE_OPERAND (t, 0));
      if (!(VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL))
	return NULL_TREE;
      if (POINTER_TYPE_P (TREE_TYPE (lhs)) && !d->ptr_out->contains (lhs))
	d->ptr_out->safe_push (lhs);
      else if (INTEGRAL_TYPE_P (TREE_TYPE (lhs)) && !d->nz_out->contains (lhs))
	d->nz_out->safe_push (lhs);
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
     refinement IF_STMT/COND_EXPR already apply, just without a
     parallel else-branch -- a loop body is only ever entered when the
     condition is true, there is no "else" here at all.  */
  auto walk_parts = [&] (oa_env &e)
    {
      for (unsigned i = 0; i < parts.length (); ++i)
	{
	  oa_walk_stmt (parts[i], e);
	  if (cond && parts[i] == cond)
	    {
	      auto_vec<tree *> conjuncts;
	      tree c = *cond;
	      oa_collect_conjuncts (&c, &conjuncts);
	      for (unsigned j = 0; j < conjuncts.length (); ++j)
		oa_refine_single_comparison (*conjuncts[j], e, /*asserted_true=*/true);
	    }
	}
    };

  oa_env scratch = env.copy ();
  walk_parts (scratch);

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
      walk_parts (checkenv);
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
      walk_parts (checkenv);
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
  for (unsigned i = 0; i < range_targets.length (); ++i)
    {
      tree d = range_targets[i];
      oa_range_fact pre_fact;
      bool pre_ok = env.range_get (d, &pre_fact);

      oa_env checkenv = env.copy ();
      checkenv.range_invalidate (d);

      bool saved_tracking = oa_return_tracking;
      oa_return_tracking = false;
      walk_parts (checkenv);
      oa_return_tracking = saved_tracking;

      oa_range_fact post_fact;
      bool post_ok = checkenv.range_get (d, &post_fact);

      range_result_decls.safe_push (d);
      if (!pre_ok || !post_ok || pre_fact.base != post_fact.base)
	{
	  range_result_has_fact.safe_push (false);
	  range_result_facts.safe_push (oa_range_fact ());
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
    }

  for (unsigned i = 0; i < range_result_decls.length (); ++i)
    {
      if (range_result_has_fact[i])
	env.range_set (range_result_decls[i], range_result_facts[i]);
      else
	env.range_invalidate (range_result_decls[i]);
    }
}

/* D4324/P2680: does control ever fall through past the end of STMT?
   Used by oa_walk_stmt's IF_STMT/COND_EXPR cases to decide, after
   walking both branches, whether the code following the if/else is
   only ever reachable via one particular branch (because the other
   unconditionally returns, throws, or hits a noreturn call) -- in
   which case that branch's facts should be used as-is for the merge
   point, rather than blindly ANDed/unioned with the terminating
   branch's (which never actually reaches there). Recognizes:
   RETURN_EXPR; THROW_EXPR; a CALL_EXPR to a function GCC already
   knows is noreturn (call_expr_flags, the same query used everywhere
   else in the compiler for this question -- picks up __builtin_trap,
   __builtin_unreachable, std::unreachable, abort, and any other
   [[noreturn]]-attributed callee uniformly, no name-matching needed);
   and, recursively, a nested IF_STMT/COND_EXPR where *both* arms
   terminate, or a STATEMENT_LIST/BIND_EXPR's last real statement.
   Deliberately conservative for anything else (default: does NOT
   terminate, i.e. assume it might fall through) -- safe, just
   occasionally missing a case, the discipline used throughout this
   pass. Not attempted: break/continue (transfer control within an
   enclosing loop, not analogous to falling through past this if),
   switch, goto, or a loop that never falls through by construction
   (e.g. 'while (true) { ... return ...; }' with no break).  */

static bool
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
      return true;

    case CALL_EXPR:
      return (call_expr_flags (t) & ECF_NORETURN) != 0;

    case IF_STMT:
      return (oa_stmt_terminates_p (THEN_CLAUSE (t))
	      && oa_stmt_terminates_p (ELSE_CLAUSE (t)));

    case COND_EXPR:
      return (oa_stmt_terminates_p (TREE_OPERAND (t, 1))
	      && oa_stmt_terminates_p (TREE_OPERAND (t, 2)));

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
      /* Item 8's narrow div/mod and array-bound restrictions, only
	 within a function actually declared 'conveyor'.  */
      if (current_function_decl && DECL_DECLARED_CONVEYOR_P (current_function_decl))
	{
	  oa_scan_div_mod_in_expr (&TREE_OPERAND (t, 0), env);
	  oa_scan_array_bounds_in_expr (&TREE_OPERAND (t, 0), env);
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

    case PRECONDITION_STMT:
      oa_handle_precondition_stmt (t, env);
      return;

    case POSTCONDITION_STMT:
      oa_handle_postcondition_stmt (t);
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
	      {
		oa_scan_div_mod_in_expr (&DECL_INITIAL (decl), env);
		oa_scan_array_bounds_in_expr (&DECL_INITIAL (decl), env);
	      }
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
	/* The RHS commonly flows directly from a call (e.g.
	   'int* q = deref(p);') -- discharge any call-site precondition
	   obligation (item 7) for every call reached from here, for the
	   same reason as RETURN_EXPR above.  */
	oa_scan_calls_in_expr (&TREE_OPERAND (t, 1), env);
	/* Item 8's narrow div/mod and array-bound restrictions, only
	   within a function actually declared 'conveyor'.  */
	if (current_function_decl && DECL_DECLARED_CONVEYOR_P (current_function_decl))
	  {
	    oa_scan_div_mod_in_expr (&TREE_OPERAND (t, 1), env);
	    oa_scan_array_bounds_in_expr (&TREE_OPERAND (t, 1), env);
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
	  }
	else if ((VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL)
		 && INTEGRAL_TYPE_P (TREE_TYPE (lhs)))
	  {
	    /* Item 8's narrow "provably nonzero" tracking, parallel to the
	       pointer tracking above.  */
	    env.nz_set (lhs, oa_provably_nonzero_p (rhs, env));
	    /* Increment E1's value-range tracking, parallel again.  */
	    oa_range_fact fact;
	    if (oa_get_range (rhs, env, &fact))
	      env.range_set (lhs, fact);
	    else
	      env.range_invalidate (lhs);
	  }
	return;
      }

    case COND_EXPR:
      {
	/* The condition itself was previously never walked at all here --
	   see the IF_STMT case's comment below for why this matters and
	   what these three steps do; identical treatment for both
	   shapes.  */
	oa_scan_calls_in_expr (&TREE_OPERAND (t, 0), env);
	if (current_function_decl && DECL_DECLARED_CONVEYOR_P (current_function_decl))
	  {
	    oa_scan_div_mod_in_expr (&TREE_OPERAND (t, 0), env);
	    oa_scan_array_bounds_in_expr (&TREE_OPERAND (t, 0), env);
	  }
	oa_scan_stray_is_object_address (&TREE_OPERAND (t, 0));
	/* Closes the assignment-in-condition gap: update the assigned
	   decl's tracked facts in ENV itself (never scanning inside
	   &&/||, see oa_track_condition_assignment's own comment) before
	   ENV is copied into THEN_ENV/ELSE_ENV below, so both branches
	   (and the post-merge state) see the up-to-date facts.  */
	oa_track_condition_assignment (TREE_OPERAND (t, 0), env);

	oa_env then_env = env.copy ();
	oa_env else_env = env.copy ();
	/* Increment E1: refine both branches' range facts from the
	   condition before walking into them, so nested code (and the
	   post-merge state after the if/else) sees the narrowed bounds.  */
	oa_refine_range_for_condition (TREE_OPERAND (t, 0), then_env, else_env);
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
	/* The condition operand itself: previously never walked at all,
	   meaning a call embedded in an if/ternary condition never got
	   item 7's call-site precondition-obligation check, and a stray
	   is_object_address used directly inside one (outside any
	   contract) wasn't flagged by the well-formedness gate either --
	   both simply invisible to this pass (unlike a loop's own
	   condition, already covered by oa_handle_loop's "repeated part"
	   list for item 4). Mirrors the exact shape already used for
	   RETURN_EXPR's value and INIT_EXPR/MODIFY_EXPR's RHS: an
	   explicit call-site-obligation scan, explicit narrow item-8
	   scans (only within a function actually declared conveyor, same
	   gating used everywhere else), and an explicit stray-is_object_
	   address scan -- deliberately *not* a full oa_walk_stmt dispatch
	   on the condition (unlike a plain sub-statement): the condition
	   here plays the same role as RETURN_EXPR's/INIT_EXPR's own
	   value/RHS, which don't recurse via oa_walk_stmt into themselves
	   either, precisely to avoid re-dispatching into (and so
	   double-scanning/double-reporting through) the very same
	   CALL_EXPR/INIT_EXPR/MODIFY_EXPR cases that already perform
	   their own oa_scan_calls_in_expr internally. This deliberately
	   still doesn't dispatch the condition through oa_walk_stmt for
	   that reason -- but an assignment written directly in the
	   condition itself ('if ((p = f()) != nullptr)') is separately
	   handled by oa_track_condition_assignment below, which updates
	   the assigned decl's tracked facts without re-scanning for
	   calls/div-mod/array-bounds (already done above), narrowly
	   scoped to a top-level assignment only (never inside &&/||,
	   see its own comment for why).  */
	oa_scan_calls_in_expr (&IF_COND (t), env);
	if (current_function_decl && DECL_DECLARED_CONVEYOR_P (current_function_decl))
	  {
	    oa_scan_div_mod_in_expr (&IF_COND (t), env);
	    oa_scan_array_bounds_in_expr (&IF_COND (t), env);
	  }
	oa_scan_stray_is_object_address (&IF_COND (t));
	oa_track_condition_assignment (IF_COND (t), env);

	oa_env then_env = env.copy ();
	oa_env else_env = env.copy ();
	/* Increment E1: refine both branches' range facts from the
	   condition before walking into them, so nested code (and the
	   post-merge state after the if/else) sees the narrowed bounds.  */
	oa_refine_range_for_condition (IF_COND (t), then_env, else_env);
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
      oa_scan_calls_in_expr (stmt, env);
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
	 through unchecked.  */
      oa_scan_stray_is_object_address (&t);
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
   sharing reason explained on oa_handle_precondition_stmt above.  */

static void
oa_handle_postcondition_stmt (tree contract)
{
  bool conveyor_ok = oa_contract_conveyor_active_p (contract);
  tree cond = CONTRACT_CONDITION (contract);
  if (cond == NULL_TREE || cond == error_mark_node)
    return;

  oa_env ret_env;
  tree result_id = POSTCONDITION_IDENTIFIER (contract);
  if (result_id && (VAR_P (result_id) || TREE_CODE (result_id) == PARM_DECL))
    ret_env.set (result_id, oa_return_seen && oa_return_all_provable);

  if (!oa_resolve_condition (&cond, ret_env, conveyor_ok))
    {
      CONTRACT_CONDITION (contract) = error_mark_node;
      return;
    }
  CONTRACT_CONDITION (contract) = cond;
}

/* Top-level entry point, called from finish_function alongside
   check_conveyor_function_body, at the same pre-genericize timing.  */

void
resolve_object_address_in_function (tree fndecl)
{
  if (!flag_contract_control_objects)
    return;
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

  oa_env env;

  /* Whether a RETURN_EXPR needs to be tracked at all is known upfront
     (a pure existence query over the pristine specifier list -- same
     conveyor/is_ignored status as the body's copy, no identity-sharing
     concern since nothing is mutated here); the actual resolution of
     the postcondition itself always happens on the body's own embedded
     copy, via the POSTCONDITION_STMT case in oa_walk_stmt below.  */
  oa_return_tracking = oa_has_active_postcondition (fndecl);
  oa_return_all_provable = false;
  oa_return_seen = false;

  oa_walk_stmt (&body, env);

  oa_return_tracking = false;
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
	  : build_real_source_location_value (loc, TREE_TYPE (f1),
					       current_function_decl)),
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

  if (contract_control_is_ignored (ctrl, side))
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

  tree comment = contract_control_omits_comment (ctrl, side)
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
     f1, (contract_control_omits_source_location (ctrl, side)
	  ? build_constructor (TREE_TYPE (f1), NULL)
	  : build_real_source_location_value (loc, TREE_TYPE (f1), fndecl)),
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
