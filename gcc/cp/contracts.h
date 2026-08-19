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
extern void propagate_cdtor_contracts_to_clones	(tree);
extern tree constify_contract_access		(tree);
extern tree view_as_const			(tree);
extern contract_check_side contract_side_of	(tree, tree);
extern bool contract_control_constifies		(tree, contract_check_side, bool = false);
extern bool contract_control_is_conveyor		(tree, contract_check_side, bool = false);
extern bool contract_control_is_symbolic		(tree, contract_check_side, bool = false);
extern bool contract_control_conveyor_like		(tree, contract_check_side, bool = false);
extern bool contract_control_symbolic_like		(tree, contract_check_side, bool = false);
extern tree contract_default_control_object		(location_t);
extern void maybe_inherit_virtual_contract		(tree, tree);
extern void resolve_object_address_in_function		(tree);
extern bool oa_stmt_terminates_p			(tree);
extern void oa_mark_fn_if_expr_calls_active_contract	(tree, tree);

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

/* Three-way outcome shared by every range-based consult in contracts.cc
   (field-range/call-range subsumption, and, since the bounds-proving
   demo, the range-vs-range relational fallback) -- unlike a named-
   predicate obligation's own strict true/false, two *ranges* can also
   merely partially overlap: ESTABLISHED might satisfy REQUIRED for some
   but not all of its own possible values, which is neither a proof of
   correctness nor a proof of violation, just "cannot verify."  Declared
   here (not contracts.cc, where it originated) so contracts-gimple.cc's
   own range-based consult can reuse it too, rather than a separately-
   maintained duplicate -- see oa_proof_result immediately above for the
   same reasoning.  */
enum oa_range_subsumption_result
{
  OA_RANGE_SUBSUMED,    /* ESTABLISHED fully satisfies REQUIRED: proven.  */
  OA_RANGE_DISJOINT,    /* No possible overlap: provably violates.  */
  OA_RANGE_PARTIAL      /* Neither of the above: cannot verify.  */
};

/* Walk FNDECL's own pre-genericize body using the existing oa_walk_stmt
   machinery unchanged (so IILE recursion, loop-header/if-else merging,
   and existing fact tracking, including a callee's postcondition
   becoming a trusted fact at an assignment from its call, are all
   inherited for free) -- but additionally invoke CALLBACK at every call
   site encountered, in program order, with the environment as it stands
   at that exact point.

   ASSERT_CALLBACK, if non-NULL, additionally fires once per plain
   contract_assert statement (ASSERTION_STMT) the walk finds, in program
   order, with the environment as it stands just *before* the built-in
   engine's own oa_handle_assertion_stmt processes that statement.
   Unlike a callee's precondition conjunct, a contract_assert's own
   condition needs no positional-argument substitution at all -- it
   already refers directly to the enclosing function's own live decls
   -- so a plugin can consult it with the very same matcher/query
   functions used for a call's own conjuncts (oa_match_simple_
   comparison/oa_env_check_comparison, oa_match_predicate_conjunct/
   oa_env_check_predicate_fact, etc.), just without ever calling
   oa_substitute_call_arg/oa_substitute_call_expr first. Both new
   parameters default to NULL, so every existing caller wanting only
   call-site observation is unaffected.  */
extern void oa_walk_function_calls
  (tree fndecl,
   void (*callback) (tree call, tree callee, oa_analysis_env *env, void *data),
   void *data,
   void (*assert_callback) (tree stmt, oa_analysis_env *env,
			     void *data) = nullptr,
   void *assert_data = nullptr);

/* Is EXPR, evaluated under ENV's current facts, provably CMP CONST_VAL
   for every value it could take, provably CMP CONST_VAL for no value it
   could take, or is ENV's knowledge insufficient to conclude either?  */
extern oa_proof_result oa_env_check_comparison
  (oa_analysis_env *env, tree expr, tree_code cmp, tree const_val);

/* The relational-fact analogue of oa_env_check_comparison above: is
   SUBSTITUTED_PARAM (already positionally substituted at the plugin's
   own call site, the same way SUBSTITUTED_PARAM is for that function)
   provably REQUIRED_CODE SUBSTITUTED_OTHER (likewise substituted),
   given ENV's current facts?  REQUIRE_CONVEYOR is the same one-way-
   trust parameter oa_env_check_predicate_fact below already exposes: a
   conveyor-established relational fact satisfies either direction, a
   symbolic-established one only ever satisfies REQUIRE_CONVEYOR=false.
   Neither operand's own value is ever resolved except when both are
   already literal (ordinary constant folding) -- see oa_match_
   comparison_against_param's own comment for why.  */
extern oa_proof_result oa_env_check_relational_fact
  (oa_analysis_env *env, tree substituted_param, tree_code required_code,
   tree substituted_other, bool require_conveyor);

/* The call analogue of oa_env_check_relational_fact immediately above:
   is SUBSTITUTED_PARAM provably REQUIRED_CODE SUBSTITUTED_RHS_RECEIVER.
   SUBSTITUTED_RHS_CALLEE () (both already positionally substituted at
   the plugin's own call site), given ENV's current facts? Same
   REQUIRE_CONVEYOR meaning as that function.

   REQUIRED_OFFSET defaults to 0, checking the plain "SUBSTITUTED_PARAM
   REQUIRED_CODE CALL ()" shape oa_match_comparison_against_call
   recognizes. Pass a nonzero value -- the OFFSET_OUT oa_match_
   shifted_comparison_against_call fills in -- to instead check
   "(SUBSTITUTED_PARAM - REQUIRED_OFFSET) REQUIRED_CODE CALL ()", e.g.
   for a callee precondition shaped 'v.size () - idx < 10'.  */
extern oa_proof_result oa_env_check_call_relational_fact
  (oa_analysis_env *env, tree substituted_param, tree_code required_code,
   tree substituted_rhs_receiver, tree substituted_rhs_callee,
   bool require_conveyor, widest_int required_offset = 0);

/* The call-vs-call analogue of oa_env_check_call_relational_fact
   immediately above: is SUBSTITUTED_LHS_RECEIVER.SUBSTITUTED_LHS_
   CALLEE () provably REQUIRED_CODE SUBSTITUTED_RHS_RECEIVER.
   SUBSTITUTED_RHS_CALLEE () (all already positionally substituted at
   the plugin's own call site), given ENV's current facts? Same
   REQUIRE_CONVEYOR meaning as that function.  */
extern oa_proof_result oa_env_check_call_call_relational_fact
  (oa_analysis_env *env, tree substituted_lhs_receiver,
   tree substituted_lhs_callee, tree_code required_code,
   tree substituted_rhs_receiver, tree substituted_rhs_callee,
   bool require_conveyor);

/* Split COND into its top-level '&&' conjuncts.  */
extern void oa_collect_conjuncts_public (tree *cond, vec<tree *> *out);

/* Split COND into its top-level '||' disjuncts -- the De Morgan's-dual
   sibling of oa_collect_conjuncts_public immediately above, for a
   plugin that wants to check a disjunctive conjunct (e.g. a
   contract_assert's own 'x > 0 || x < -10') by trying each disjunct
   independently through oa_check_assertion_conjunct_public below:
   PROVEN_TRUE if any one disjunct is independently provable,
   PROVEN_FALSE only if every disjunct is independently provable false,
   else UNKNOWN.  */
extern void oa_collect_disjuncts_public (tree *cond, vec<tree *> *out);

/* Check CONJUNCT -- one of a contract_assert's own top-level '&&'
   conjuncts (oa_collect_conjuncts_public), or one of a disjunctive
   conjunct's own top-level '||' disjuncts (oa_collect_disjuncts_public)
   -- against ENV's current facts, given the plugin's own new ASSERT_
   CALLBACK (oa_walk_function_calls). Unlike every other consult
   function in this file, CONJUNCT is never positionally substituted:
   a contract_assert's own condition already refers directly to the
   enclosing function's own live decls, not another function's
   parameters, so this tries the full family of shapes this file's own
   built-in contract_assert checking recognizes (bare-scalar, ptr->
   field, call-range, relational, call-relational, call-call-
   relational, named-predicate, and the general compound-expression
   fallback) directly against those decls. REQUIRE_CONVEYOR: same
   one-way-trust meaning as oa_env_check_predicate_fact above -- pass
   true from the conveyor plugin, false from the symbolic plugin.  */
extern oa_proof_result oa_check_assertion_conjunct_public
  (oa_analysis_env *env, tree conjunct, bool require_conveyor);

/* Positionally substitute PARAM (one of CALLEE's own PARM_DECLs,
   including its implicit 'this') to CALL's actual argument expression
   at this call site. Returns NULL_TREE if PARAM isn't actually one of
   CALLEE's own parameters, or CALL doesn't supply that many arguments.
   Correctly handles CALL being an AGGR_INIT_EXPR (a constructor call) --
   use this instead of indexing CALL_EXPR_ARG/AGGR_INIT_EXPR_ARG
   directly, which is unsafe (an ICE in a checking build, a wrong
   argument silently read otherwise) for that shape.  */
extern tree oa_substitute_call_arg_public (tree callee, tree call, tree param);

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

/* The call analogue of oa_match_comparison_against_param immediately
   above: "PARAM OP RECEIVER.ACCESSOR ()" (e.g. 'i < v.size ()'), where
   ACCESSOR is a DECL_DECLARED_CONVEYOR_P accessor, or, when ALLOW_
   SYMBOLIC_ACCESSOR is true, a DECL_DECLARED_SYMBOLIC_P one instead,
   rather than another of the callee's own parameters. RHS_RECEIVER_OUT/
   RHS_CALLEE_OUT are not resolved values; the caller substitutes RHS_
   RECEIVER_OUT with a specific call site's own argument and consults a
   *call-relational* fact, the call-shaped counterpart of oa_env::
   relational_map.  */
extern bool oa_match_comparison_against_call
  (tree conjunct, tree *param_out, tree_code *code_out,
   tree *rhs_receiver_out, tree *rhs_callee_out, bool allow_symbolic_accessor);

/* The shift-shaped sibling of oa_match_comparison_against_call
   immediately above: "RECEIVER.ACCESSOR () - PARAM OP <literal>" or its
   mirror "PARAM - RECEIVER.ACCESSOR () OP <literal>" (e.g. 'v.size ()
   - idx < 10'), normalized so the recognized fact reads "(PARAM -
   OFFSET_OUT) CODE_OUT RECEIVER.ACCESSOR ()" -- feed OFFSET_OUT to
   oa_env_check_call_relational_fact's own REQUIRED_OFFSET parameter to
   consult it. Neither PARAM_OUT/RHS_RECEIVER_OUT/RHS_CALLEE_OUT is a
   resolved value, same as that function.  */
extern bool oa_match_shifted_comparison_against_call
  (tree conjunct, tree *param_out, tree_code *code_out,
   tree *rhs_receiver_out, tree *rhs_callee_out, widest_int *offset_out,
   bool allow_symbolic_accessor);

/* The call-vs-call analogue of oa_match_comparison_against_call
   immediately above: "RECEIVER_1.CALLEE_1 () OP RECEIVER_2.CALLEE_2 ()"
   (e.g. 'v.size () < w.size ()'), two calls compared against each other
   rather than a bare parameter against a call. Neither side is a
   privileged operand to canonicalize around (unlike that function's own
   PARAM/CALL asymmetry), so LHS/RHS are simply the operands as written.
   None of the four *_OUT values are resolved; the caller substitutes
   both receivers with a specific call site's own arguments and consults
   a *call-call-relational* fact.  */
extern bool oa_match_call_against_call
  (tree conjunct, tree *lhs_receiver_out, tree *lhs_callee_out,
   tree_code *code_out, tree *rhs_receiver_out, tree *rhs_callee_out,
   bool allow_symbolic_accessor);

/* If CONJUNCT has the shape "RESULT_ID OP other", where RESULT_ID is a
   postcondition's own already-known return-value binder and "other" is
   one of the postcondition-owning function's own parameters, recognize
   it and fill CODE_OUT/OTHER_OUT, oriented so the relation always reads
   "RESULT_ID CODE_OUT other".  The item-6 counterpart of oa_match_
   comparison_against_param above, for a postcondition relating its own
   return value to one of its own other parameters (e.g. 'post<ctrl>(r:
   r < q)').  */
extern bool oa_match_result_relation
  (tree conjunct, tree result_id, tree_code *code_out, tree *other_out);

/* The call analogue of oa_match_result_relation immediately above:
   "RESULT_ID OP RECEIVER.CALLEE ()" (e.g. 'post<ctrl>(r: r < this->
   size ())'), where CALLEE is a DECL_DECLARED_CONVEYOR_P accessor
   rather than another of the postcondition-owning function's own
   parameters.  */
extern bool oa_match_result_call_relation
  (tree conjunct, tree result_id, tree_code *code_out,
   tree *rhs_receiver_out, tree *rhs_callee_out, bool allow_symbolic_accessor);

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

/* oa_match_simple_comparison above, with its own "must be a bare
   PARM_DECL" restriction on the non-literal side dropped -- accepts
   *any* expression (e.g. 'percentage + this->m_value < 100.0'). Match
   only as a fallback once oa_match_simple_comparison has already
   declined, so the two never doubly match the same conjunct. EXPR_OUT
   still needs oa_substitute_call_expr_public's own recursive
   substitution below before it means anything at a specific call
   site -- this only recognizes the shape.  */
extern bool oa_match_general_comparison_public
  (tree conjunct, tree *expr_out, tree_code *code_out, tree *const_val_out);

/* Positionally substitute every PARM_DECL reached within EXPR (an
   arbitrary compound expression, e.g. one just recognized by
   oa_match_general_comparison_public above) to CALL's own actual
   argument expressions, recursing through PLUS_EXPR/MINUS_EXPR and a
   'param.field'/'param->field' COMPONENT_REF (including the implicit
   'this->field' shape). Returns NULL_TREE for anything it doesn't
   recognize -- a field reached through something other than one of
   CALLEE's own parameters, a call, or any other shape -- rather than
   guessing. The result can be handed straight to oa_env_check_
   comparison, which already knows how to resolve an arbitrary
   substituted expression's own range.  */
extern tree oa_substitute_call_expr_public (tree callee, tree call, tree expr);

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

/* Same cached, GIMPLE-pass-safe pattern as the active-status pair above,
   but for CONTRACT's own strict status (its control object is
   proven_conveyor/proven_symbolic specifically, not just analyzed_
   conveyor/analyzed_symbolic): an OA_UNKNOWN result for a strict
   contract is escalated from a warning ("cannot verify") to a hard
   error ("cannot prove"), mirroring the built-in engine's own
   oa_contract_conveyor_strict_p/oa_contract_symbolic_strict_p and the
   'strict' parameter threaded through oa_handle_precondition_simple_
   range_obligation and its siblings.  */
extern bool oa_contract_conveyor_strict_cached_p (tree contract);
extern bool oa_contract_symbolic_strict_cached_p (tree contract);

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

/* D4324: is FN a specialization of std::is_object_address itself (as
   opposed to some other call)? Used by call.cc/typeck.cc's callee-must-
   be-conveyor check to exempt it -- see contracts.cc's own comment.  */
extern bool is_object_address_fndecl_p (tree fn);

/* D4324: is FN std::unreachable itself? Used by the same callee-must-
   be-conveyor check to exempt it, so constexpr.cc's own, more specific
   "std::unreachable not permitted" diagnostic still fires undisturbed --
   see contracts.cc's own comment.  */
extern bool is_std_unreachable_fndecl_p (tree fn);

/* D4324: is a call to FN, with not-yet-adjusted object argument OBJ_ARG,
   a genuinely immediately-invoked closure call (the same narrow shape
   oa_iile_call_p recognizes, parameterized for build_over_call's own
   pre-CALL_EXPR-construction use)? Used by the callee-must-be-conveyor
   check to exempt it -- see contracts.cc's own comment for why this is
   load-bearing, not just a convenience.  */
extern bool is_iile_operator_call_p (tree fn, tree obj_arg);

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

/* The call-range analogue of oa_env_check_field_range_fact immediately
   above, for a call to CALLEE_FN (a DECL_DECLARED_CONVEYOR_P accessor)
   on the object identified by RECEIVER_EXPR, rather than a ptr->field
   access.  REQUIRE_CONVEYOR: same meaning as everywhere else in this
   file -- the conveyor plugin passes true, the symbolic plugin passes
   false.  */
extern oa_proof_result oa_env_check_call_range_fact
  (oa_analysis_env *env, tree receiver_expr, tree callee_fn, bool has_lo,
   tree lo, bool has_hi, tree hi, bool require_conveyor);

/* The floating-point analogue of oa_env_check_field_range_fact: same
   ptr->field consult, but against a float-typed field's own tracked
   range (m_contract_float_field_range_map). LO/HI are REAL_CST trees
   (see oa_precondition_float_field_range_obligations below, which
   supplies them in exactly this shape). Without this, a float-bounded
   field precondition (e.g. 'pre<ctrl>(this->value >= 0.0)') was
   invisible to every plugin -- oa_env_check_field_range_fact's own
   [lo,hi] machinery is integer-only.  */
extern oa_proof_result oa_env_check_float_field_range_fact
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

/* The floating-point analogue of oa_precondition_field_range_obligations
   immediately above -- CALLBACK receives REAL_CST (not INTEGER_CST)
   LO/HI bounds, built in the matched field's own type. Previously there
   was no way for a plugin to ever observe a float-bounded field
   precondition conjunct at all; see oa_env_check_float_field_range_fact's
   own comment for the consult-side half of this pair.  */
extern void oa_precondition_float_field_range_obligations
  (tree callee,
   void (*callback) (tree contract, tree field, tree base_parm,
		      bool has_lo, tree lo, bool has_hi, tree hi,
		      void *data),
   void *data);

/* The call-range analogue of oa_precondition_field_range_obligations
   immediately above: CALLBACK is invoked once per (contract, callee_fn,
   receiver parameter) match, for a call to a DECL_DECLARED_CONVEYOR_P
   accessor (e.g. 'n < this->size ()') rather than a ptr->field access.
   Same shared-substrate discipline as that function.  */
extern void oa_precondition_call_range_obligations
  (tree callee,
   void (*callback) (tree contract, tree callee_fn, tree receiver_parm,
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

/* Recognize CONJUNCT as "RECEIVER.ACCESSOR () OP const" -- the call-
   range analogue of oa_match_field_range_comparison immediately above,
   for a call to a DECL_DECLARED_CONVEYOR_P accessor (e.g. 'i < v.size
   ()'), or, when ALLOW_SYMBOLIC_ACCESSOR is true, a DECL_DECLARED_
   SYMBOLIC_P one instead, rather than a ptr->field access.  See the
   definition (contracts.cc) for the full rationale on the two-tag gate
   and why a plain, untagged accessor is never accepted either way.
   Exported for the same reason as that function: a GIMPLE-pass-based or
   plugin consumer that needs this shape directly.  */
extern bool oa_match_call_range_comparison
  (tree conjunct, tree *receiver_out, tree *callee_out, tree_code *code_out,
   tree *const_val_out, bool allow_symbolic_accessor);

/* True if A and B are both non-__restrict pointer/reference PARM_DECLs
   with the same (or void-compatible) pointee type, i.e. a caller could
   legally pass the same object through both -- D4324 Stage 3's own
   parameter-alias-group check, reused as-is by a GIMPLE-pass-based
   consumer's own analogous sweep (contracts-gimple.cc).  */
extern bool oa_could_alias_as_parameters_public (tree a, tree b);

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

/* True while building or evaluating a converted constant expression or
   any other manifestly constant expression (an explicit-specifier's own
   operand, a non-type template argument, an array bound, an enumerator
   value, a static_assert condition, ...): such an expression can never
   have side effects or UB by the core language's own rules (if it did,
   it simply wouldn't BE a constant expression, and evaluation fails
   with its own, conveyor-independent diagnostic), so none of
   conveyor_restrictions_active_p's restrictions have anything real to
   check here regardless of whether we're otherwise inside conveyor-
   restricted code.  See contracts.cc for the full rationale; set from
   both gcc/cp/call.cc's build_converted_constant_expr_internal (the
   call-building phase) and gcc/cp/constexpr.cc's cxx_eval_outermost_
   constant_expr (the actual interpreter) since a real regression showed
   up in only one of the two conditionally-explicit std::pair
   constructors tried, meaning both phases can independently reach a
   call this check would otherwise flag.  */
extern bool suppress_conveyor_restrictions_for_converted_constant_expr_p;

/* RAII sentinel for the flag just above: covers every return path of
   whatever scope it's declared in, unlike a manual save/restore pair,
   which is easy to miss one of on a function with many early returns
   (cxx_eval_outermost_constant_expr, notably).  */
class suppress_conveyor_restrictions_for_converted_constant_expr_sentinel
{
public:
  bool saved;
  bool active;
  /* ACTIVE lets a caller conditionally no-op this sentinel (e.g. for
     'if constexpr' specifically, not an ordinary, possibly side-
     effecting runtime 'if') without needing a separate, hand-written
     save/restore fallback path alongside the RAII one.  */
  suppress_conveyor_restrictions_for_converted_constant_expr_sentinel
    (bool active = true)
    : saved (suppress_conveyor_restrictions_for_converted_constant_expr_p),
      active (active)
  {
    if (active)
      suppress_conveyor_restrictions_for_converted_constant_expr_p = true;
  }
  ~suppress_conveyor_restrictions_for_converted_constant_expr_sentinel ()
  {
    if (active)
      suppress_conveyor_restrictions_for_converted_constant_expr_p = saved;
  }
};

/* Non-null while attempting a D4324 'conveyor(auto)' per-instantiation
   deduction (see maybe_instantiate_conveyor in pt.cc): points at the
   deduction attempt's own "did we find a violation" flag.  Every one
   of conveyor_restrictions_active_p's many scattered callers is
   expected to check conveyor_auto_probing_p () itself and, if true,
   call note_conveyor_auto_violation () instead of diagnosing and
   poisoning the expression -- a conveyor(auto) instantiation whose
   body happens to violate a mandatory rule must still build as an
   ordinary, ill-conveyor-but-otherwise-valid function: banning
   reinterpret_cast, for instance, doesn't stop it from being
   perfectly valid C++, just conveyor-incompatible C++, so there is
   nothing to poison in the AST built for the surrounding, non-probing
   compilation to see.  The usual shape at each call site is:

     if (conveyor_restrictions_active_p ())
       {
	 if (conveyor_auto_probing_p ())
	   note_conveyor_auto_violation ();
	 else
	   {
	     if (complain & tf_error)
	       error_at (loc, "...");
	     return error_mark_node;
	   }
       }

   i.e. the existing diagnose-and-poison block is untouched; only a
   probing early-out is added in front of it.  */
extern bool *conveyor_auto_probe_violation_p;

/* True while a conveyor(auto) probe (as above) is in progress.  */
inline bool
conveyor_auto_probing_p (void)
{
  return conveyor_auto_probe_violation_p != nullptr;
}

/* Record that the active conveyor(auto) probe found a violation.
   Must not be called unless conveyor_auto_probing_p ().  */
inline void
note_conveyor_auto_violation (void)
{
  gcc_checking_assert (conveyor_auto_probe_violation_p);
  *conveyor_auto_probe_violation_p = true;
}

/* RAII sentinel for a single conveyor(auto) probe attempt: while
   alive, conveyor_auto_probing_p () is true and every violation
   conveyor_restrictions_active_p's callers see anywhere sets
   *VIOLATION.  Probes nest correctly (a conveyor(auto) function's own
   body may itself call another conveyor(auto) function, whose
   deduction must run to answer the outer probe's own callee-is-
   conveyor question), each with its own VIOLATION flag -- this is a
   plain save/restore, not a single-entry guard.  Self-referential
   deduction (a conveyor(auto) function depending on its own
   conveyor-ness) is guarded separately, by maybe_instantiate_
   conveyor's own hash_set, not here.  */
class conveyor_auto_probe_sentinel
{
public:
  bool *saved;
  bool active;
  /* ACTIVE lets a caller conditionally no-op this sentinel (e.g. when
     D isn't actually an unresolved conveyor(auto) specialization)
     without a separate hand-written fallback path, matching
     suppress_conveyor_restrictions_for_converted_constant_expr_
     sentinel's own ACTIVE parameter above.  When inactive, VIOLATION
     is never touched.  */
  explicit conveyor_auto_probe_sentinel (bool *violation, bool active = true)
    : saved (conveyor_auto_probe_violation_p), active (active)
  {
    if (active)
      conveyor_auto_probe_violation_p = violation;
  }
  ~conveyor_auto_probe_sentinel ()
  {
    if (active)
      conveyor_auto_probe_violation_p = saved;
  }
};

extern void set_fn_contract_specifiers		(tree, tree);
extern void update_fn_contract_specifiers	(tree, tree);
extern tree get_fn_contract_specifiers		(tree);
extern void remove_decl_with_fn_contracts_specifiers (tree);
extern void remove_fn_contract_specifiers	(tree);
extern void update_contract_arguments		(tree, tree);

/* -Wfunction-pointer-contract-mismatch: resolve EXPR down to the decl
   whose own get_fn_contract_specifiers entry governs it (a function
   name/'&fn', or a function-pointer-typed VAR_DECL/PARM_DECL/
   FIELD_DECL used directly), or NULL_TREE if EXPR isn't that simple.
   See the function's own comment in contracts.cc.  */
extern tree fnptr_contract_owner		(tree);

/* -Wfunction-pointer-contract-mismatch: warn when copying SRC_EXPR into
   function-pointer/reference-typed DEST_DECL would change which
   contract governs calls made through the destination.  See the
   function's own comment in contracts.cc for the exact (deliberately
   asymmetric) semantics.  */
extern void maybe_warn_fnptr_contract_mismatch	(location_t, tree, tree);

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
