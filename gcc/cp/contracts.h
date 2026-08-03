/* Definitions for C++26 contracts.

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

#ifndef GCC_CP_CONTRACT_H
#define GCC_CP_CONTRACT_H

#include <cstdint>

/* Contract assertion kind */
/* Must match relevant enums in <contracts> header  */

enum contract_assertion_kind : uint16_t {
  CAK_INVALID = 0 ,
  CAK_PRE = 1 ,
  CAK_POST = 2 ,
  CAK_ASSERT = 3,
};

/* Per P2900R14 + D3290R3 + extensions.  */
enum contract_evaluation_semantic : uint16_t {
  CES_INVALID = 0,
  CES_IGNORE = 1,
  CES_OBSERVE = 2,
  CES_ENFORCE = 3,
  CES_QUICK = 4,
};

enum detection_mode : uint16_t {
  CDM_UNSPECIFIED = 0,
  CDM_PREDICATE_FALSE = 1,
  CDM_EVAL_EXCEPTION = 2
};

/* Contract evaluation_semantic */
#define CONTRACT_EVALUATION_SEMANTIC(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 0))

#define CONTRACT_ASSERTION_KIND(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 1))

#define CONTRACT_CHECK(NODE) \
  (TREE_CHECK3 (NODE, ASSERTION_STMT, PRECONDITION_STMT, POSTCONDITION_STMT))

/* True if NODE is any kind of contract.  */
#define CONTRACT_P(NODE)			\
  (TREE_CODE (NODE) == ASSERTION_STMT		\
   || TREE_CODE (NODE) == PRECONDITION_STMT	\
   || TREE_CODE (NODE) == POSTCONDITION_STMT)

/* True if NODE is a contract condition.  */
#define CONTRACT_CONDITION_P(NODE)		\
  (TREE_CODE (NODE) == PRECONDITION_STMT	\
   || TREE_CODE (NODE) == POSTCONDITION_STMT)

/* True if NODE is a precondition.  */
#define PRECONDITION_P(NODE)           \
  (TREE_CODE (NODE) == PRECONDITION_STMT)

/* True if NODE is a postcondition.  */
#define POSTCONDITION_P(NODE)          \
  (TREE_CODE (NODE) == POSTCONDITION_STMT)

/* True iff the FUNCTION_DECL NODE currently has any contracts.  */
#define DECL_HAS_CONTRACTS_P(NODE) \
  (get_fn_contract_specifiers (NODE) != NULL_TREE)

/* The wrapper of the original source location of a list of contracts.  */
#define CONTRACT_SOURCE_LOCATION_WRAPPER(NODE) \
  (TREE_PURPOSE (TREE_VALUE (NODE)))

/* The original source location of a list of contracts.  */
#define CONTRACT_SOURCE_LOCATION(NODE) \
  (EXPR_LOCATION (CONTRACT_SOURCE_LOCATION_WRAPPER (NODE)))

/* The actual code _STMT for a contract specifier.  */
#define CONTRACT_STATEMENT(NODE) \
  (TREE_VALUE (TREE_VALUE (NODE)))

/* The parsed condition of the contract.  */
#define CONTRACT_CONDITION(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 2))

/* True iff the condition of the contract NODE is not yet parsed.  */
#define CONTRACT_CONDITION_DEFERRED_P(NODE) \
  (TREE_CODE (CONTRACT_CONDITION (NODE)) == DEFERRED_PARSE)

/* The raw comment of the contract.  */
#define CONTRACT_COMMENT(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 3))

/* A std::source_location, if provided.  */
#define CONTRACT_STD_SOURCE_LOC(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 4))

/* The optional control OBJECT named as pre<expr>/post<expr>/
   contract_assert<expr>, where expr is a constant-expression.  NULL_TREE
   means -fcontract-control-objects is off, so the built-in
   evaluation-semantic path is used; with the flag on, a bare
   pre/post/contract_assert has this already resolved to the implicit
   std::contracts::default_v object (see contract_default_control_object),
   so NULL_TREE never reaches this slot in that configuration.  */
#define CONTRACT_CONTROL_OBJECT(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 5))

/* The VAR_DECL of a postcondition result. For deferred contracts, this
   is an IDENTIFIER.  */
#define POSTCONDITION_IDENTIFIER(NODE) \
  (TREE_OPERAND (POSTCONDITION_STMT_CHECK (NODE), 6))

/* For a FUNCTION_DECL of a guarded function, this holds the function decl
   where pre contract checks are emitted.  */
#define DECL_PRE_FN(NODE) \
  (get_precondition_function ((NODE)))

/* For a FUNCTION_DECL of a guarded function, this holds the function decl
   where post contract checks are emitted.  */
#define DECL_POST_FN(NODE) \
  (get_postcondition_function ((NODE)))

/* True iff the FUNCTION_DECL is the pre function for a guarded function.  */
#define DECL_IS_PRE_FN_P(NODE) \
  (DECL_DECLARES_FUNCTION_P (NODE) && DECL_LANG_SPECIFIC (NODE) \
   && CONTRACT_HELPER (NODE) == ldf_contract_pre)

/* True iff the FUNCTION_DECL is the post function for a guarded function.  */
#define DECL_IS_POST_FN_P(NODE) \
  (DECL_DECLARES_FUNCTION_P (NODE) && DECL_LANG_SPECIFIC (NODE) \
   && CONTRACT_HELPER (NODE) == ldf_contract_post)

#define DECL_IS_WRAPPER_FN_P(NODE) \
  (DECL_DECLARES_FUNCTION_P (NODE) && DECL_LANG_SPECIFIC (NODE) && \
   DECL_CONTRACT_WRAPPER (NODE))

/* Allow specifying a sub-set of contract kinds to copy.  */
enum contract_match_kind
{
  cmk_all,
  cmk_pre,
  cmk_post
};

/* Which side of a call a contract's runtime check is happening on: at the
   function's own definition, via a caller-side (client) wrapper, or
   neither (ccs_not_applicable, for a contract_assert, which has no
   caller/definition distinction at all -- it's a statement inside a
   function body, not a precondition/postcondition on a boundary callers
   can see).  Mirrors std::contracts::assertion_check_side exactly (see
   build_assertion_static_info_value in contracts.cc).  */
enum contract_check_side { ccs_definition, ccs_wrapper, ccs_not_applicable };

/* contracts.cc */

extern void init_contracts			(void);

extern tree grok_contract			(tree, tree, tree, cp_expr, location_t, tree = NULL_TREE);
extern tree finish_contract_specifier 		(tree, tree);
extern tree finish_contract_condition		(cp_expr);
extern void update_late_contract		(tree, tree, cp_expr);
extern void check_redecl_contract		(tree, tree);
extern void check_redecl_object_contract	(tree, tree);
extern void set_contract_positional_parms	(tree, tree);
extern tree get_contract_positional_parms	(tree);
extern tree maybe_object_contract_check_call	(tree, tree, vec<tree, va_gc> *, unsigned = 0);
extern tree resolve_single_call_operator	(tree);
extern tree build_call_operator_contract_params (tree);
extern tree invalidate_contract			(tree);
extern tree copy_and_remap_contracts		(tree, tree, contract_match_kind = cmk_all,
						 bool for_wrapper = false);
extern tree constify_contract_access		(tree);
extern tree view_as_const			(tree);
extern contract_check_side contract_side_of	(tree, tree);
extern bool contract_control_constifies		(tree, contract_check_side);
extern bool contract_control_is_conveyor		(tree, contract_check_side);
extern tree contract_default_control_object		(location_t);
extern void maybe_inherit_virtual_contract		(tree, tree);
extern void resolve_object_address_in_function		(tree);
extern bool oa_stmt_terminates_p			(tree);

/* True while parsing/substituting a contract condition that opts into
   constification via its control type's constify member (D4324: off by
   default).  */
extern bool contract_condition_constify_p;

/* True while parsing/substituting a contract condition whose control
   type opts into D4324 conveyor-function rules (is_conveyor member).  */
extern bool contract_condition_conveyor_p;

/* True if conveyor-function syntactic restrictions should be rejected
   right now (see contracts.cc for the exact conditions).  */
extern bool conveyor_restrictions_active_p		(void);

extern void set_fn_contract_specifiers		(tree, tree);
extern void update_fn_contract_specifiers	(tree, tree);
extern tree get_fn_contract_specifiers		(tree);
extern void remove_decl_with_fn_contracts_specifiers (tree);
extern void remove_fn_contract_specifiers	(tree);
extern void update_contract_arguments		(tree, tree);

extern tree make_postcondition_variable		(cp_expr);
extern tree make_postcondition_variable		(cp_expr, tree);
extern void check_param_in_postcondition	(tree, location_t);
extern void check_postconditions_in_redecl	(tree, tree);
extern void maybe_update_postconditions		(tree);
extern void rebuild_postconditions		(tree);
extern bool check_postcondition_result		(tree, tree, location_t);

extern bool contract_any_deferred_p 		(tree);

extern tree get_precondition_function		(tree);
extern tree get_postcondition_function		(tree);
extern tree get_orig_for_outlined		(tree);

extern void start_function_contracts		(tree);
extern void maybe_apply_function_contracts	(tree);
extern void finish_function_outlined_contracts	(tree);
extern void set_contract_functions		(tree, tree, tree);

extern tree maybe_contract_wrap_call		(tree, tree);
extern bool emit_contract_wrapper_func		(bool);
extern void maybe_emit_violation_handler_wrappers (void);

extern tree build_contract_check		(tree);
extern tree build_contract_control_constexpr_check (tree, tree, bool);
extern tree maybe_replace_d4324_violation_handler_call (tree, tree);

/* Test if EXP is a contract const wrapper node.  */

inline bool
contract_const_wrapper_p (const_tree exp)
{
  /* A wrapper node has code VIEW_CONVERT_EXPR, and the flag base.private_flag
     is set. The wrapper node is used to used to constify entities inside
     contract assertions.  */
  return ((TREE_CODE (exp) == VIEW_CONVERT_EXPR) && CONST_WRAPPER_P (exp));
}

/* If EXP is a contract_const_wrapper_p, return the wrapped expression.
   Otherwise, do nothing. */

inline tree
strip_contract_const_wrapper (tree exp)
{
  if (contract_const_wrapper_p (exp))
    return TREE_OPERAND (exp, 0);
  else
    return exp;
}

/* TODO : decide if we should push the tests into contracts.cc  */
extern contract_evaluation_semantic get_evaluation_semantic (const_tree);

/* Will this contract be ignored.  */

inline bool
contract_ignored_p (const_tree contract)
{
  return (get_evaluation_semantic (contract) <= CES_IGNORE);
}

/* Will this contract be evaluated?  */

inline bool
contract_evaluated_p (const_tree contract)
{
  return (get_evaluation_semantic (contract) >= CES_OBSERVE);
}

/* Is the contract terminating?  */

inline bool
contract_terminating_p (const_tree contract)
{
  return (get_evaluation_semantic (contract) == CES_ENFORCE
	  || get_evaluation_semantic (contract) == CES_QUICK);
}

#endif /* ! GCC_CP_CONTRACT_H */
