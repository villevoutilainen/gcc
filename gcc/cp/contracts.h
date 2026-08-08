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

/* contracts-gimple.cc.  Declared here (rather than alongside the other
   make_pass_* declarations in tree-pass.h) because this pass only exists
   in the C++ front end's own object file; tree-pass.h is shared by every
   language driver via passes.def/pass-instances.def, so a declaration
   there would force cc1/lto1 to resolve a symbol they never link in.
   init_contracts calls register_pass (tree-pass.h) directly, exactly as a
   plugin's own PLUGIN_PASS_MANAGER_SETUP callback would, instead of
   listing this pass in passes.def.  */
class gimple_opt_pass;
extern gimple_opt_pass *make_pass_contracts_gimple (gcc::context *ctxt);

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
extern bool contract_control_is_symbolic		(tree, contract_check_side);
extern tree contract_default_control_object		(location_t);
extern void maybe_inherit_virtual_contract		(tree, tree);
extern void resolve_object_address_in_function		(tree);
extern bool oa_stmt_terminates_p			(tree);

/* A standalone GCC plugin's own entry points into the object-address
   ("oa_*") analysis engine (see
   .claude/plans/stateless-jumping-shore.md) -- deliberately a small,
   separate, opaque-handle API, not a raw exposure of oa_env/oa_range_fact
   (both stay entirely private to contracts.cc).  Never used by anything
   in the compiler itself; resolve_object_address_in_function above (the
   compiler's own, mandatory, always-armed-with-no-callback entry point)
   is completely unaffected by any of this.  */

/* Opaque; a plugin only ever holds a pointer, never the definition.  */
struct oa_analysis_env;

/* A static checker's answer is never just binary -- see the "Diagnostics"
   discussion in .claude/plans/stateless-jumping-shore.md.  OA_PROVEN_FALSE
   is a real, confirmed violation; OA_UNKNOWN is a much weaker "couldn't
   verify" signal and must not be reported with the same severity.  */
enum oa_proof_result { OA_PROVEN_TRUE, OA_PROVEN_FALSE, OA_UNKNOWN };

/* Walk FNDECL's own pre-genericize body using the existing oa_walk_stmt
   machinery unchanged (so IILE recursion, loop-header/if-else merging,
   and existing fact tracking, including a callee's postcondition
   becoming a trusted fact at an assignment from its call, are all
   inherited for free) -- but additionally invoke CALLBACK at every call
   site encountered, in program order, with the environment as it stands
   at that exact point.  */
extern void oa_walk_function_calls
  (tree fndecl,
   void (*callback) (tree call, tree callee, oa_analysis_env *env, void *data),
   void *data);

/* Is EXPR, evaluated under ENV's current facts, provably CMP CONST_VAL
   for every value it could take, provably CMP CONST_VAL for no value it
   could take, or is ENV's knowledge insufficient to conclude either?  */
extern oa_proof_result oa_env_check_comparison
  (oa_analysis_env *env, tree expr, tree_code cmp, tree const_val);

/* Split COND into its top-level '&&' conjuncts.  */
extern void oa_collect_conjuncts_public (tree *cond, vec<tree *> *out);

/* If CONJUNCT has the shape "param OP const" (bare PARM_DECL only, same
   restriction as the compiler's own is_object_address(param) matching),
   recognize it and fill PARAM_OUT/CODE_OUT/CONST_VAL_OUT.  */
extern bool oa_match_simple_comparison
  (tree conjunct, tree *param_out, tree_code *code_out, tree *const_val_out);

/* If CONJUNCT has the shape "paramA OP paramB", where paramB is
   *another* of the same callee's own parameters (not a literal) --
   optionally reached through an implicit conversion operator -- rather
   than a constant, recognize it and fill PARAM_OUT/CODE_OUT/OTHER_OUT.
   Unlike oa_match_simple_comparison above, OTHER_OUT is not a resolved
   value; the caller substitutes it with a specific call site's own
   argument and consults a *relational* fact (oa_env::relational_get),
   never a value one.  */
extern bool oa_match_comparison_against_param
  (tree conjunct, tree *param_out, tree_code *code_out, tree *other_out);

/* True if an established relational fact of code ESTABLISHED (e.g.
   LT_EXPR) is strong enough to satisfy a required comparison of code
   REQUIRED (e.g. an established '<' satisfies a required '<=').  */
extern bool oa_relational_code_implies (tree_code established, tree_code required);

/* Does "A CODE B" hold, where A and B are both ordinary compile-time
   INTEGER_CST literals -- plain constant folding, not resolving any
   parameter's own opaque meaning.  */
extern bool oa_relational_literal_holds (tree_code code, tree a, tree b);

/* Is CONTRACT (a PRECONDITION_STMT/POSTCONDITION_STMT belonging to
   OWNER_FN) currently conveyor-active (non-ignored)?  */
extern bool oa_contract_conveyor_active_public (tree contract, tree owner_fn);

/* Mirrors oa_contract_conveyor_active_public immediately above, for the
   symbolic side.  */
extern bool oa_contract_symbolic_active_public (tree contract, tree owner_fn);

/* Cached, GIMPLE-pass-safe readers of CONTRACT's own conveyor-/
   symbolic-active status, populated once per function at reliable
   front-end time (oa_cache_contract_flavors) -- unlike oa_contract_
   conveyor_active_public/oa_contract_symbolic_active_public
   immediately above, which call straight into oa_contract_conveyor_
   active_p/oa_contract_symbolic_active_p's own real semantic analysis
   (overload resolution + constexpr evaluation) every time, found by
   direct testing to silently answer incorrectly once called from
   GIMPLE-pass timing (see ~/gimple-contract-analysis.md, Sections
   9.3/10) -- these two are pure cache lookups, safe to call from
   anywhere, at any time, including after genericization/gimplification.
   Prefer these over the *_public pair above for any consumer that
   might run post-front-end.  */
extern bool oa_contract_conveyor_active_cached_p (tree contract);
extern bool oa_contract_symbolic_active_cached_p (tree contract);

/* Recognize CONJUNCT as "pred_fn (decl)" or its negation "!pred_fn
   (decl)" -- the named-predicate shape both -fcontract-conveyor-proofs
   and -fcontract-symbolic-proofs already share internally.  Fills
   PRED_FN_OUT/ARG_DECL_OUT/NEGATED_OUT.  */
extern bool oa_match_predicate_conjunct
  (tree conjunct, tree *pred_fn_out, tree *arg_decl_out, bool *negated_out);

/* If CALL is a call to a specialization of std::is_object_address,
   return true and set *ARG to its (single) argument expression --
   the exact same recognizer the compiler's own mandatory UB-freedom
   pass and both built-in provers already use internally
   (contracts.cc), now also exported so a plugin can recognize the
   shape of a *declared* precondition/postcondition/contract_assert
   conjunct directly (e.g. via get_fn_contract_specifiers +
   CONTRACT_CONDITION) without needing its own, separately-maintained
   copy of the recognition logic. Deliberately front-end/GENERIC-level
   only: it operates on the *declared* condition tree, unaffected by
   whether the owning function's own body has been genericized/
   gimplified/outlined into a predicate-core function yet -- see the
   "front-end-assisted" design in ~/gimple-contract-analysis.md, whose
   whole point is that a GIMPLE-time consumer never needs to chase the
   outlining machinery to answer "what does this contract say," only
   to answer "is this fact true right here," which it does with its
   own, GIMPLE/SSA-native machinery instead.  */
extern bool is_object_address_call_p (tree call, tree *arg);

/* D4324/P2680 item 8, Increment E-divmod: true if CONJUNCT is of the
   form 'E != 0' or '0 != E' (either operand order), with *DECL_OUT set
   to E (a direct VAR_DECL/PARM_DECL reference only) -- the nonzero-
   fact analogue of is_object_address_call_p immediately above, and
   exported for the same reason (a GIMPLE-pass-based consumer needs to
   recognize a *declared* condition's own shape without a separate,
   drifting copy of the recognition logic).  Previously static; no
   change to its own implementation.  */
extern bool oa_nonzero_conjunct_p (tree conjunct, tree *decl_out);

/* Is OBJ_EXPR established by ENV's current (real, cross-statement-
   tracked) facts to have PRED_FN hold at REQUIRED_POLARITY, established
   at the opposite polarity, or unknown?  Backed by the same shared
   substrate both built-in checkers consult for their own named-predicate
   obligations (see .claude/plans/well-we-last-discussed-ethereal-
   duckling.md).  REQUIRE_CONVEYOR: the trust relationship between the
   two flavors is one-way -- a conveyor-established fact (backed by real
   UB-freedom verification) is trustworthy enough for a symbolic
   obligation to rely on, but a symbolic-established fact (never
   verified, trusted outright) must never satisfy a conveyor obligation.
   Pass true from the conveyor plugin, false from the symbolic plugin.  */
extern oa_proof_result oa_env_check_predicate_fact
  (oa_analysis_env *env, tree obj_expr, tree pred_fn, bool required_polarity,
   bool require_conveyor);

/* Same three-way verdict, for a bare scalar's own contract-established
   range (distinct from oa_env_check_comparison's ordinary dataflow
   range: this one is only ever established by a callee's own
   postcondition, never inferred from an arbitrary computation).  Each
   bound is optional -- pass has_lo/has_hi false (LO/HI then ignored) for
   an open bound.  Symbolic-only substrate (see oa_precondition_scalar_
   range_obligations's own comment), so there is no REQUIRE_CONVEYOR
   parameter here -- nothing conveyor-side ever establishes into this
   map, so there is no wrong-direction fact to guard against.  */
extern oa_proof_result oa_env_check_scalar_range_fact
  (oa_analysis_env *env, tree expr, bool has_lo, tree lo, bool has_hi,
   tree hi);

/* Same, for FIELD of the object identified by BASE_EXPR (this->field-
   style).  REQUIRE_CONVEYOR: see oa_env_check_predicate_fact's own
   comment -- this map is genuinely shared between both flavors, so the
   same one-way trust rule applies here too.  */
extern oa_proof_result oa_env_check_field_range_fact
  (oa_analysis_env *env, tree base_expr, tree field, bool has_lo, tree lo,
   bool has_hi, tree hi, bool require_conveyor);

/* Collect every distinct PARM_DECL compared by a bare-scalar conjunct of
   CALLEE's own active *symbolic* preconditions, and the combined
   [lo,hi] each implies -- hides oa_range_fact behind plain tree bounds
   (has_lo/has_hi false and the corresponding bound NULL_TREE for an
   open bound).  CALLBACK is invoked once per (contract, param) match
   found.  Symbolic-only, unlike oa_precondition_field_range_
   obligations below: m_contract_scalar_range_map has no conveyor-side
   gap to close, since -fcontract-conveyor-proofs's own bare-scalar
   checking already gets full cross-statement tracking from the
   general-purpose m_range_map (see oa_call_symbolic_range_p's own
   comment in contracts.cc).  */
extern void oa_precondition_scalar_range_obligations
  (tree callee,
   void (*callback) (tree contract, tree param, bool has_lo, tree lo,
		      bool has_hi, tree hi, void *data),
   void *data);

/* Same, for the ptr->field shape (this->field/param->field-style
   conjuncts) -- CALLBACK is invoked once per (contract, field, base
   parameter) match found.  Genuinely shared between conveyor- and
   symbolic-active preconditions (unlike oa_precondition_scalar_range_
   obligations, which stays symbolic-only): a caller that only cares
   about one flavor should filter matches by checking oa_contract_
   conveyor_active_public/oa_contract_symbolic_active_public on the
   CONTRACT passed to its own callback.  */
extern void oa_precondition_field_range_obligations
  (tree callee,
   void (*callback) (tree contract, tree field, tree base_parm,
		      bool has_lo, tree lo, bool has_hi, tree hi,
		      void *data),
   void *data);

/* If CONJUNCT has the ptr->field shape ('ptr->field OP const' or
   '(*ptr).field OP const'), recognize it and fill FIELD_OUT/
   PTR_EXPR_OUT/CODE_OUT/CONST_VAL_OUT -- the ptr->field analogue of
   oa_match_simple_comparison above.  PTR_EXPR_OUT is presented wrapped
   for const-qualified access (typically a NOP_EXPR around the real
   PARM_DECL, including 'this') -- pass it through
   oa_strip_symbolic_ptr_expr_public below before resolving identity.
   Exported (alongside that stripping helper) for a GIMPLE-pass-based
   consumer that needs to recognize this shape directly rather than
   through oa_precondition_field_range_obligations's own PRECONDITION_
   P/oa_contract_fact_tracking_active_p-gated iteration -- e.g. to
   collect the *established* (POSTCONDITION_P) side of this same shape,
   which that export doesn't cover.  */
extern bool oa_match_field_range_comparison
  (tree conjunct, tree *field_out, tree *ptr_expr_out, tree_code *code_out,
   tree *const_val_out);

/* Strip PTR_EXPR (as extracted by oa_match_field_range_comparison) down
   to its own real decl -- see that function's own comment.  */
extern tree oa_strip_symbolic_ptr_expr_public (tree ptr_expr);

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
