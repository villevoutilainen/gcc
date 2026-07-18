<!-- Extracted from GCC branch berne/contracts-p3850 @ 6537a96fd170 (2026-07-14) compared against upstream master @ ed1cb8abdbf (2026-07-18). Self-contained reference; the branch does not need to be consulted again. -->

# D4324 Implementation Reference (extracted from the P3850 branch)

This document is a self-contained extraction of everything useful in the
`contracts-p3850` branch for implementing the D4324 "Minimum Language, Maximum
Library" contracts design on top of GCC. Once this file exists, the P3850 branch
is a dead reference: the load-bearing code, the AST layout, the enums, the
runtime ABI, the flags, the tests, and the P3400/P3595 resolution seam are all
reproduced below with `> D4324 mapping:` notes and per-area change checklists.

## How to use this file

- Read Section 1 for the shape of the two implementations and the one insight
  that organizes everything else.
- Sections 2 through 6 are the extracted material, each ending with a change
  checklist or keep-vs-drop table.
- Section 7 is the consolidated implementation game plan: an ordered sequence
  built from the per-section checklists, starting from upstream master.

## D4324 design in one paragraph

Contract syntax does two jobs: bind a predicate to a declaration, and name an
assertion-control object via a template argument (`pre<review>(cond)`; bare
`pre(cond)` is `pre<std::contracts::default_v>(cond)`). The compiler reads three
compile-time members of the control-object type `T` and makes one runtime call:
`T::is_ignored(cfg) -> bool`, `T::constify -> bool`, `T::assumable -> bool`, and
`c(comment, loc, cfg) -> violation_response {proceed, terminate}`. Three-step
algorithm: (1) if `is_ignored`, emit an optimizer assumption when `assumable`
else nothing, and stop; (2) evaluate the predicate constified iff `constify`,
else as written; (3) on violation call the control object, and if it returns
`terminate`, contract-terminate, else proceed. `evaluation_config` is
`{ignore=0, observe=1, enforce=2, quick_enforce=3}` plus reserved ranges.
Constification is off by default; exception-to-violation translation is removed
by default (predicate exceptions propagate, stopped at `noexcept`). The library
owns the response. Adding a semantic is writing a library function, not changing
the compiler. No core-language undefined behavior is routed through the contract
handler (profiles own that).

## Provenance

- Branch: `berne/contracts-p3850`, HEAD `6537a96fd170` (2026-07-14), remote
  `github.com/notadragon/gnu_gcc`.
- Baseline: upstream `master`, HEAD `ed1cb8abdbf` (2026-07-18),
  `git://gcc.gnu.org/git/gcc.git`.
- Extraction method: branch files materialized with `git show`, diffed against
  the working-tree master, analyzed by five parallel readers. Code blocks quote
  branch source verbatim (including source comments that contain double dashes).

---

## 1. Architecture overview

### 1.1 What the P3850 branch is

The `contracts-p3850` branch is a roughly 36,000-line implementation of the full
P3850 umbrella layered on GCC's P2900R14 contracts. It bundles many papers at
once: P3097 (contracts on virtual functions), P3098 (postcondition captures),
P3099 (user diagnostic messages), P3100 (implicit contract assertions plus the
`assume` semantic), P3290 (manual violation API and `<cassert>` integration),
P3400 (assertion-control labels composed from constexpr facets), P4283
(requires-clauses on assertions), P4298 (`noexcept_observe` / `noexcept_enforce`
semantics), D4299 (C contracts), and P3595 (a JSON/CLI configuration system).
It replaces the P2900 violation path with a descriptor-chain ABI and a new
pure-C `libcontracts` runtime plus a matrix of `__cxa_contract_violation_*`
entry points.

Master (upstream trunk) has only P2900R14: a TU-wide
`-fcontract-evaluation-semantic`, a compile-time `contract_violation` constant,
`__tu_has_violation` wrappers, and always-on constification and
exception-to-violation translation.

| Dimension | Master (P2900) | Branch (P3850) | D4324 |
|---|---|---|---|
| Semantic selection | TU-wide flag, stamped at parse time | Lazy resolve: JSON/CLI config + label `compute_semantic` + dynamic selectors | Compile-time members of the control-object type named in source |
| Steering unit | none (global) | P3400 label composed from facets | one control-object type |
| Violation dispatch | `__tu_has_violation(&violation, semantic)` | `__cxa_contract_violation_{kind}_{semantic}_{mode}[_noexcept]` matrix into `libcontracts` | one call `T::operator()(comment, loc, cfg)` |
| Violation object | compile-time `contract_violation` constant | descriptor-chain data block walked at runtime | reuse P2900 `contract_violation` + `handle_contract_violation` |
| Constification | always on | always on (except P3098 captures) | gated on `T::constify` (default off) |
| Exception to violation | always on (try/catch, `_exception` wrapper) | always on (try/catch, `_ex` entry point) | removed by default (propagate to `noexcept`) |
| Runtime library | none extra | new `libcontracts` (pure C) + `libstdc++exp` | none extra (reuse P2900 handler) |
| Flags | `-fcontracts`, `-fcontract-evaluation-semantic=` | per-paper flag zoo + `-fcontract-configuration*` | `-fcontracts` (+ TU default semantic) |

### 1.2 The pipeline, three ways

```mermaid
flowchart TD
  subgraph masterFlow ["Master: P2900 baseline"]
    mParse["parse pre/post/assert (no control arg)"] --> mGrok["grok_contract: stamp TU-wide semantic into op0"]
    mGrok --> mGen["cp-gimplify: build_contract_check"]
    mGen --> mSwitch["switch on semantic: ignore/observe/enforce/quick"]
    mSwitch --> mCall["call __tu_has_violation(&violation, semantic)"]
    mCall --> mHandler["handle_contract_violation (library)"]
  end

  subgraph branchFlow ["Branch: P3850"]
    bParse["parse pre<label>(cond) + message/captures/requires"] --> bGrok["grok_contract: store label at op5, validate facets"]
    bGrok --> bGen["cp-gimplify: build_contract_check"]
    bGen --> bResolve["ensure_evaluation_semantic: config resolve + compute_semantic + dynamic"]
    bResolve --> bEmit["emit_check_for_semantic per resolved arm"]
    bEmit --> bEntry["call __cxa_contract_violation_kind_semantic_mode"]
    bEntry --> bDispatch["libcontracts __contract_dispatch_core (chain walk)"]
    bDispatch --> bHandler["handle_contract_violation (library)"]
  end

  subgraph d4324Flow ["D4324: minimum language"]
    dParse["parse pre<T>(cond), default T = default_v"] --> dGrok["grok_contract: store control TYPE at op5"]
    dGrok --> dGen["cp-gimplify: build_contract_check"]
    dGen --> dStep["three-step: is_ignored(cfg) / constify / assumable"]
    dStep --> dCall["one call T::operator()(comment, loc, cfg)"]
    dCall --> dResp["branch on {proceed, terminate}"]
    dResp --> dHandler["default_control calls invoke_default_contract_violation_handler"]
  end
```

The genericization entry point (`cp-gimplify.cc`, the
`ASSERTION_STMT`/`PRECONDITION_STMT`/`POSTCONDITION_STMT` cases dispatching to
`build_contract_check`) is byte-for-byte identical between master and the
branch. D4324 keeps it too. All the divergence is inside `build_contract_check`
and downstream.

### 1.3 Where everything lives (branch file inventory)

| Concern | Files |
|---|---|
| Parser, AST, codegen | `gcc/cp/parser.cc`, `gcc/cp/contracts.cc` (6563 lines on branch), `gcc/cp/contracts.h`, `gcc/cp/cp-tree.def`, `gcc/cp/cp-gimplify.cc` (unchanged) |
| Constification / name lookup | `gcc/cp/semantics.cc`, `gcc/cp/pt.cc` |
| Config engine (P3595) | `gcc/c-family/contracts-config.{cc,h}`, `contracts-config-query.h`, `contracts-config-source.h`, `gcc/cp/contracts-config.{cc,h}` |
| Flags / options | `gcc/c-family/c.opt`, `gcc/c-family/c-opts.cc` |
| Feature-test macros | `gcc/c-family/c-cppbuiltin.cc` |
| Link integration | `gcc/cp/g++spec.cc` |
| Runtime (pure C) | `libcontracts/dispatch.c`, `accessors.c`, `c_api.c`, `contracts-abi.h`, `libcontracts.map` |
| Library surface | `libstdc++-v3/include/std/contracts`, `libstdc++-v3/include/bits/contracts_abi.h`, `libstdc++-v3/include/bits/assert_contract.h` |
| Library runtime | `libstdc++-v3/src/c++26/contract26.cc`, `contracts_abi.cc` (branch) vs `libstdc++-v3/src/experimental/contract26.cc` (master) |
| C headers | `gcc/ginclude/contracts.h`, `gcc/ginclude/stdcontracts.h` |
| Tests | `gcc/testsuite/g++.dg/contracts/cpp26/{p3097,p3098,p3099,p3100,p3290,p3400,p3595,p4283,p4298}-*.C`, `gcc/testsuite/gcc.dg/contracts/*.c`, `gcc/testsuite/lib/contracts-dg.exp` |

### 1.4 The core insight for D4324

The P3850 branch is the maximal answer to the same question D4324 asks: let
source code steer contract code generation. P3400 assertion-control labels are
the direct ancestor of D4324 control objects. The branch proves the seam works
end to end (name a steering object, have the compiler read compile-time
properties off it, call it on violation), but it pays for generality with a
config engine, a descriptor-chain ABI, a separate C runtime, an entry-point
matrix, dynamic runtime selectors, and a per-paper flag zoo.

D4324 keeps the seam and deletes the machinery. Every section below is organized
around that split: what to reuse (the genericization hook, the lazy-resolution
choke point, the `view_as_const` mechanism, the AST operand-expansion technique,
the P2900 handler and violation object, the test-harness patterns) and what to
delete (config engine, descriptor chain, `libcontracts`, entry-point matrix,
`_noexcept` wrappers, dynamic selectors, label facet composition, per-paper
flags, and the default exception-to-violation translation).

---

## 2. Codegen core (contracts.cc)

Source files compared:
- **Branch (P3850):** `cabinet/_scratch/d4324-extract/branch/gcc_cp_contracts.cc` (6563 lines)
- **Master baseline:** `gnu_gcc/gcc/cp/contracts.cc` (~3055 lines)

---

### 2.1 Lowering entry point (cp-gimplify.cc)

`git diff master berne/contracts-p3850 -- gcc/cp/cp-gimplify.cc` is **empty**. Genericization of contract statement nodes is unchanged between master and branch.

Hook in `cp_genericize_r` (master and branch identical):

```cpp
    case ASSERTION_STMT:
    case PRECONDITION_STMT:
    case POSTCONDITION_STMT:
      if (tree check = build_contract_check (stmt))
	{
	  *stmt_p = check;
	  return cp_genericize_r (stmt_p, walk_subtrees, data);
	}
      /* If we didn't build a check, replace it with void_node so we don't
	 leak contracts into GENERIC.  */
      *stmt_p = void_node;
      *walk_subtrees = 0;
      break;
```

> **D4324 mapping:** Keep this hook unchanged. D4324 only replaces the body of `build_contract_check()`; genericization still turns `*_STMT` nodes into a `BIND_EXPR` statement expression (or `void_node` when ignored).

---

### 2.2 `build_contract_check()` - master vs branch

#### Master (baseline)

Master reads a **TU-wide, parse-time** semantic via `get_evaluation_semantic()`, builds a compile-time `contract_violation` constant, and calls TU-local wrappers `__tu_has_violation` / `__tu_has_violation_exception`:

```cpp
tree
build_contract_check (tree contract)
{
  contract_evaluation_semantic semantic = get_evaluation_semantic (contract);
  bool quick = false;
  bool calls_handler = false;
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

  tree terminate_wrapper = terminate_fn;
  if (flag_contracts_conservative_ipa)
    terminate_wrapper = declare_terminate_wrapper ();
  if (calls_handler)
    declare_violation_handler_wrappers ();

  bool check_might_throw = (flag_exceptions
			    && !expr_noexcept_p (condition, tf_none));

  tree cc_bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, NULL);
  BIND_EXPR_BODY (cc_bind) = push_stmt_list ();

  if (TREE_CODE (contract) == ASSERTION_STMT)
    emit_builtin_observable_checkpoint ();
  tree cond = build_x_unary_op (loc, TRUTH_NOT_EXPR, condition, NULL_TREE,
				tf_warning_or_error);
  tree violation;
  bool viol_is_var = false;
  if (quick)
    violation = build_zero_cst (nullptr_type_node);
  else
    {
      tree ctor = build_contract_violation_ctor (contract);
      gcc_checking_assert (TREE_CONSTANT (ctor));
      violation = build_contract_violation_constant (ctor, contract);
      violation = build_address (violation);
    }

  tree s_const = build_int_cst (uint16_type_node, semantic);
  /* try/catch for predicate exceptions - see section 2.9 */
  /* ... */
  tree do_check = begin_if_stmt ();
  finish_if_stmt_cond (cond, do_check);
  if (quick)
    finish_expr_stmt (build_call_a (terminate_wrapper, 0, nullptr));
  else
    finish_expr_stmt (build_call_n (tu_has_violation, 2, violation, s_const));
  finish_then_clause (do_check);
  finish_if_stmt (do_check);

  BIND_EXPR_BODY (cc_bind) = pop_stmt_list (BIND_EXPR_BODY (cc_bind));
  return cc_bind;
}
```

#### Branch (P3850) - FULL body

Branch **splits** codegen: lazy semantic resolution, per-semantic emission, P3595 dynamic dispatch, and new ABI entry points. No `tu_has_violation` / `build_contract_violation_*` remain on the branch.

```cpp
tree
build_contract_check (tree contract)
{
  contract_evaluation_semantic semantic
    = ensure_evaluation_semantic (contract, current_function_decl, false);

  /* Plain (non-dynamic) contract: emit the single resolved check.  */
  const char *dyn_name = contract_dynamic_name (contract);
  if (!dyn_name)
    return emit_check_for_semantic (contract, semantic);

  /* P3595 dynamic selection.  Dispatch on the selector's runtime return value,
     emitting each distinct check body exactly once and driving an unknown value
     to an enforced violation.

     This runs during genericization, where the parser's switch machinery
     (finish_case_label et al.) is not available, so each dispatch is built
     as an if / else-if cascade comparing a value against each semantic.  The
     statement-tree if builders (begin_if_stmt ...) are the same ones the
     non-dynamic check body uses at this stage.

     When the contract's label transforms the raw value non-trivially (an
     allowed_semantics facet narrows the set, or a compute_semantic facet is
     present), a TWO-STAGE form is emitted (P3595 design 4):

       stage 1: eff = T(raw), mapping each of ignore/observe/enforce/quick
		to its compile-time transform T() (or CES_INVALID when the
		result is disallowed); an unknown raw value maps to CES_INVALID.
       stage 2: dispatch on eff, calling emit_check_for_semantic for the four
		valid semantics and emit_enforced_violation for CES_INVALID.

     When the map is the identity (no transforming label) stage 1 is skipped and
     stage 2 dispatches directly on the raw selector value.  */
  bool provideweak = contract_dynamic_provideweak (contract);
  unsigned char linkage = contract_dynamic_linkage (contract);

  tree fndecl = get_dynamic_selector_decl (dyn_name, linkage, provideweak,
					   semantic);
  tree ret_type = TREE_TYPE (TREE_TYPE (fndecl));

  tree cc_bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, NULL);
  BIND_EXPR_BODY (cc_bind) = push_stmt_list ();

  /* The violation data block is identical for every dispatch arm of this
     contract (same source location, comment, kind, ...), so build it ONCE here
     and reuse its address across all arms and the enforced-violation default,
     rather than emitting a duplicate global per arm.  */
  tree block_type;
  tree ctor = build_contract_data_block_ctor (contract, &block_type);
  tree data_var = build_contract_data_block_constant (ctor, block_type,
						      contract);
  tree data_addr = build_address (data_var);

  tree call = build_call_n (fndecl, 0);
  tree raw = save_expr (call);

  bool transforms
    = contract_label_transforms_p (contract, current_function_decl);

  tree dispatch_val = raw;
  tree dispatch_type = ret_type;

  if (transforms)
    {
      location_t loc = EXPR_LOCATION (contract);
      tree eff = build_decl (loc, VAR_DECL, NULL, short_unsigned_type_node);
      DECL_ARTIFICIAL (eff) = true;
      DECL_IGNORED_P (eff) = true;
      DECL_CONTEXT (eff) = current_function_decl;
      layout_decl (eff, 0);
      add_decl_expr (eff);
      DECL_CHAIN (eff) = BIND_EXPR_VARS (cc_bind);
      BIND_EXPR_VARS (cc_bind) = eff;

      auto_vec<tree, 4> map_ifs;
      for (int s = CES_IGNORE; s <= CES_QUICK; s++)
	{
	  tree cmp = build2 (EQ_EXPR, boolean_type_node, raw,
			     build_int_cst (ret_type, s));
	  tree if_stmt = begin_if_stmt ();
	  finish_if_stmt_cond (cmp, if_stmt);
	  bool ok = false;
	  contract_evaluation_semantic eff_sem
	    = transform_semantic (contract, current_function_decl,
				  (contract_evaluation_semantic) s, &ok);
	  int eff_val = !ok ? (int) CES_INVALID
		      : eff_sem == CES_ASSUME ? (int) CES_IGNORE
		      : (int) eff_sem;
	  finish_expr_stmt
	    (cp_build_modify_expr (loc, eff, NOP_EXPR,
				   build_int_cst (short_unsigned_type_node,
						  eff_val),
				   tf_warning_or_error));
	  finish_then_clause (if_stmt);
	  begin_else_clause (if_stmt);
	  map_ifs.safe_push (if_stmt);
	}
      finish_expr_stmt
	(cp_build_modify_expr (loc, eff, NOP_EXPR,
			       build_int_cst (short_unsigned_type_node,
					      (int) CES_INVALID),
			       tf_warning_or_error));
      for (int i = map_ifs.length () - 1; i >= 0; i--)
	{
	  finish_else_clause (map_ifs[i]);
	  finish_if_stmt (map_ifs[i]);
	}

      dispatch_val = eff;
      dispatch_type = short_unsigned_type_node;
    }

  auto_vec<tree, 4> if_stmts;
  for (int s = CES_IGNORE; s <= CES_QUICK; s++)
    {
      tree cmp = build2 (EQ_EXPR, boolean_type_node, dispatch_val,
			 build_int_cst (dispatch_type, s));
      tree if_stmt = begin_if_stmt ();
      finish_if_stmt_cond (cmp, if_stmt);
      tree body = emit_check_for_semantic (contract,
					   (contract_evaluation_semantic) s,
					   data_addr);
      if (body && body != void_node && body != error_mark_node)
	add_stmt (body);
      finish_then_clause (if_stmt);
      begin_else_clause (if_stmt);
      if_stmts.safe_push (if_stmt);
    }

  tree def_body = emit_enforced_violation (contract, data_addr);
  if (def_body && def_body != error_mark_node)
    add_stmt (def_body);

  for (int i = if_stmts.length () - 1; i >= 0; i--)
    {
      finish_else_clause (if_stmts[i]);
      finish_if_stmt (if_stmts[i]);
    }

  BIND_EXPR_BODY (cc_bind) = pop_stmt_list (BIND_EXPR_BODY (cc_bind));
  return cc_bind;
}
```

**Key delta:** master = one semantic, one path, violation object + `__tu_has_violation( violation*, semantic )`. Branch = `ensure_evaluation_semantic` first; plain path delegates to `emit_check_for_semantic`; dynamic path builds one shared data block and an if-cascade over selector values (optionally two-stage through `transform_semantic` / label `compute_semantic`).

> **D4324 mapping:** Replace the entire semantic switch and P3595 dynamic cascade with the three-step algorithm keyed off `evaluation_config` and compile-time members of the assertion-control type `T`. One runtime call to `T::operator()(comment, loc, cfg)` replaces the `__cxa_contract_violation_*` matrix and `emit_enforced_violation` default arm. `build_contract_check` becomes: resolve cfg, call `emit_contract_check_for_control(contract, T, cfg)` (new helper).

---

### 2.3 `emit_check_for_semantic()` (branch) - FULL body

Extracted per-semantic codegen shared by plain and dynamic paths.

```cpp
static tree
emit_check_for_semantic (tree contract, contract_evaluation_semantic semantic,
			 tree shared_data_addr = NULL_TREE)
{
  bool quick = false;
  bool calls_handler = false;
  switch (semantic)
    {
    case CES_IGNORE:
    case CES_ASSUME:
      /* P3100 "assume" emits no check for now, exactly like "ignore".  */
      return void_node;
    case CES_ENFORCE:
    case CES_OBSERVE:
    case CES_NOEXCEPT_ENFORCE:
    case CES_NOEXCEPT_OBSERVE:
      calls_handler = true;
      break;
    case CES_QUICK:
      quick = true;
      break;
    default:
      gcc_unreachable ();
    }

  location_t loc = EXPR_LOCATION (contract);

  remap_dummy_this (current_function_decl, &CONTRACT_CONDITION (contract));
  if (CONTRACT_CONDITION (contract) == error_mark_node)
    return NULL_TREE;

  if (POSTCONDITION_P (contract) && !flag_contract_checks_outlined)
    {
      remap_retval (current_function_decl, contract);
      if (CONTRACT_CONDITION (contract) == error_mark_node)
	return NULL_TREE;
    }

  tree condition = unshare_expr (CONTRACT_CONDITION (contract));

  tree terminate_wrapper = terminate_fn;
  if (flag_contracts_conservative_ipa)
    terminate_wrapper = declare_terminate_wrapper ();

  contract_assertion_kind kind = get_contract_assertion_kind (contract);

  bool check_might_throw = (flag_exceptions
			    && !expr_noexcept_p (condition, tf_none));
  bool is_noexcept = (semantic == CES_NOEXCEPT_ENFORCE
		      || semantic == CES_NOEXCEPT_OBSERVE);

  tree cc_bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, NULL);
  BIND_EXPR_BODY (cc_bind) = push_stmt_list ();

  if (TREE_CODE (contract) == ASSERTION_STMT)
    emit_builtin_observable_checkpoint ();
  tree cond = build_x_unary_op (loc, TRUTH_NOT_EXPR, condition, NULL_TREE,
				tf_warning_or_error);

  tree data_addr = NULL_TREE;
  if (!quick && calls_handler)
    {
      if (shared_data_addr)
	data_addr = shared_data_addr;
      else
	{
	  tree block_type;
	  tree ctor = build_contract_data_block_ctor (contract, &block_type);
	  tree data_var = build_contract_data_block_constant (ctor, block_type,
							      contract);
	  data_addr = build_address (data_var);
	}
    }

  tree entry_pf = NULL_TREE;
  tree entry_ex = NULL_TREE;
  if (calls_handler)
    {
      entry_pf = declare_cxa_entry_point (kind, semantic,
					  CDM_PREDICATE_FALSE, is_noexcept);
      if (check_might_throw)
	entry_ex = declare_cxa_entry_point (kind, semantic,
					    CDM_EVAL_EXCEPTION, is_noexcept);
    }

  if (check_might_throw)
    {
      tree check_failed = build_decl (loc, VAR_DECL, NULL, boolean_type_node);
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
	  finish_expr_stmt (build_call_n (entry_ex, 1, data_addr));
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
    finish_expr_stmt (build_call_a (terminate_wrapper, 0, nullptr));
  else
    {
      finish_expr_stmt (build_call_n (entry_pf, 1, data_addr));
      if (semantic == CES_OBSERVE)
	emit_builtin_observable_checkpoint ();
    }
  finish_then_clause (do_check);
  finish_if_stmt (do_check);

  BIND_EXPR_BODY (cc_bind) = pop_stmt_list (BIND_EXPR_BODY (cc_bind));
  return cc_bind;
}
```

**Switch logic:**

| Semantic | `calls_handler` | `quick` | `is_noexcept` | Emitted check |
|---|---|---|---|---|
| `CES_IGNORE`, `CES_ASSUME` | - | - | - | `void_node` (no code) |
| `CES_ENFORCE`, `CES_OBSERVE` | yes | no | false | data block + `_pf` entry; observe adds observable checkpoint |
| `CES_NOEXCEPT_ENFORCE`, `CES_NOEXCEPT_OBSERVE` | yes | no | true | same with `_noexcept` suffix |
| `CES_QUICK` | no | yes | - | direct `terminate_wrapper` on failure; exception path also terminates |

> **D4324 mapping:** Collapse this switch into: (1) if `T::is_ignored(cfg)` then optionally emit assumption when `T::assumable`, else stop; (2) evaluate predicate (constified iff `T::constify`); (3) on violation, one call to control object, branch on `{proceed, terminate}`. Delete per-semantic entry-point selection, `quick` direct-terminate path (library decides), and observe checkpoint (library concern).

---

### 2.4 `ensure_evaluation_semantic()` (branch) - FULL body

Lazy resolution choke point: config lookup, label `compute_semantic`, cache, dynamic descriptor.

```cpp
contract_evaluation_semantic
ensure_evaluation_semantic (tree contract, tree fndecl, bool in_ce)
{
  tree *slot = in_ce
    ? &CONTRACT_CONSTEXPR_EVALUATION_SEMANTIC (contract)
    : &CONTRACT_EVALUATION_SEMANTIC (contract);

  if (*slot != NULL_TREE)
    return (contract_evaluation_semantic) tree_to_uhwi (*slot);

  contract_query q = make_contract_query (contract, fndecl);
  q.caller_side = false;
  q.in_constant_evaluation = in_ce;

  auto_vec<const char *> groups_vec;
  fill_query_groups (&q, contract, groups_vec);

  contract_config_result res = contract_config_resolve (&q);
  uint16_t sem = (uint16_t) res.semantic;
  if (sem == CES_INVALID)
    {
      error_at (EXPR_LOCATION (contract),
		"no valid evaluation semantic for contract assertion");
      sem = in_ce ? CES_OBSERVE : CES_ENFORCE;
    }

  sem = apply_compute_semantic (CONTRACT_LABEL (contract), sem,
				q.allowed_mask, EXPR_LOCATION (contract));

  *slot = build_int_cst (uint16_type_node, sem);

  if (!in_ce && res.dyn_name)
    {
      unsigned HOST_WIDE_INT packed
	= ((unsigned HOST_WIDE_INT) res.dyn_linkage << 1)
	  | (res.dyn_provideweak ? 1 : 0);
      CONTRACT_DYNAMIC (contract)
	= build_tree_list (get_identifier (res.dyn_name),
			   build_int_cst (uint16_type_node, packed));
    }

  return (contract_evaluation_semantic) sem;
}
```

Master's `get_evaluation_semantic()` reads `CONTRACT_EVALUATION_SEMANTIC` set at parse time (four C++26 semantics only); no config resolution, no constexpr slot, no dynamic descriptor.

> **D4324 mapping:** Replace with reading compile-time `T::is_ignored`, `T::constify`, `T::assumable` from the template argument of `pre<T>` / `post<T>` / `assert<T>`, plus TU-wide `evaluation_config` (ignore/observe/enforce/quick_enforce). Cache `cfg` on the contract node instead of `CES_*` semantic. Delete `contract_config_resolve`, P3595 dynamic descriptor, and label `compute_semantic` at this layer (library owns response semantics). This function stays the single resolution choke point; only its body changes.

---

### 2.5 `contract_base_allowed_mask()` (branch) - FULL body

```cpp
static inline uint16_t
contract_base_allowed_mask ()
{
  uint16_t mask = CES_ALL_ALLOWED;
  if (flag_contracts_allow_assume)
    mask |= (1 << CES_ASSUME);
  if (flag_contracts_p4298)
    mask |= (1 << CES_NOEXCEPT_ENFORCE) | (1 << CES_NOEXCEPT_OBSERVE);
  return mask;
}
```

Used in `make_contract_query()` as `q.allowed_mask = label_mask & contract_base_allowed_mask()`, gating which semantics labels and config can select.

> **D4324 mapping:** Not needed for D4324's four-value `evaluation_config` enum. Delete flag-gated semantic bitmask; control-object type `T` and `cfg` fully determine behavior at codegen time.

---

### 2.6 Entry-point naming - `get_cxa_entry_point_name()` / `declare_cxa_entry_point()` (branch)

#### `get_cxa_entry_point_name()` - FULL body

```cpp
static const char *
get_cxa_entry_point_name (contract_assertion_kind kind,
			  contract_evaluation_semantic semantic,
			  int detection_mode,
			  bool is_noexcept)
{
  const char *kind_str;
  switch (kind)
    {
    case CAK_PRE: kind_str = "pre"; break;
    case CAK_POST: kind_str = "post"; break;
    case CAK_ASSERT: kind_str = "assert"; break;
    case CAK_POST_CAPTURE: kind_str = "post_capture"; break;
    default: gcc_unreachable ();
    }

  const char *sem_str;
  switch (semantic)
    {
    case CES_ENFORCE: sem_str = "enforce"; break;
    case CES_OBSERVE: sem_str = "observe"; break;
    case CES_NOEXCEPT_ENFORCE: sem_str = "noexcept_enforce"; break;
    case CES_NOEXCEPT_OBSERVE: sem_str = "noexcept_observe"; break;
    default: gcc_unreachable ();
    }

  const char *mode_str;
  switch (detection_mode)
    {
    case CDM_PREDICATE_FALSE: mode_str = "pf"; break;
    case CDM_EVAL_EXCEPTION: mode_str = "ex"; break;
    default: gcc_unreachable ();
    }

  char buf[128];
  if (is_noexcept)
    snprintf (buf, sizeof (buf),
	      "__cxa_contract_violation_%s_%s_%s_noexcept",
	      kind_str, sem_str, mode_str);
  else
    snprintf (buf, sizeof (buf),
	      "__cxa_contract_violation_%s_%s_%s",
	      kind_str, sem_str, mode_str);

  return ggc_strdup (buf);
}
```

Examples: `__cxa_contract_violation_pre_enforce_pf`, `__cxa_contract_violation_post_observe_ex_noexcept`.

#### `declare_cxa_entry_point()` - FULL body

```cpp
static tree
declare_cxa_entry_point (contract_assertion_kind kind,
			 contract_evaluation_semantic semantic,
			 int detection_mode,
			 bool is_noexcept)
{
  const char *name = get_cxa_entry_point_name (kind, semantic,
					       detection_mode, is_noexcept);

  if (!cxa_entry_point_cache)
    cxa_entry_point_cache = hash_map<nofree_string_hash, tree>::create_ggc (16);

  tree *cached = cxa_entry_point_cache->get (name);
  if (cached)
    return *cached;

  bool is_noreturn = (semantic == CES_ENFORCE
		      || semantic == CES_NOEXCEPT_ENFORCE);
  tree fntype = build_function_type_list (void_type_node,
					  ptr_type_node, NULL_TREE);
  if (is_noexcept)
    fntype = build_exception_variant (fntype, NULL_TREE);
  tree fndecl = build_lang_decl (FUNCTION_DECL,
				 get_identifier (name), fntype);
  SET_DECL_LANGUAGE (fndecl, lang_c);
  TREE_PUBLIC (fndecl) = true;
  DECL_EXTERNAL (fndecl) = true;
  DECL_ARTIFICIAL (fndecl) = true;
  if (is_noreturn)
    TREE_THIS_VOLATILE (fndecl) = true;

  tree parms = build_decl (BUILTINS_LOCATION, PARM_DECL,
			   NULL_TREE, ptr_type_node);
  DECL_CONTEXT (parms) = fndecl;
  DECL_ARGUMENTS (fndecl) = parms;

  cxa_entry_point_cache->put (name, fndecl);
  return fndecl;
}
```

Master had no CXA matrix; it declared `__tu_has_violation(const contract_violation&, uint16_t semantic)` and `__tu_has_violation_exception(...)` instead.

> **D4324 mapping:** Delete both functions and the cache. Emit a direct call to the instantiated control object's `operator()` (or a single mangled runtime helper the library provides). Violation metadata (comment, loc) passes as call arguments, not via a rodata data block keyed by entry-point name.

---

### 2.7 Violation data-block construction (branch)

Replaces master's `build_contract_violation_ctor()` + `build_contract_violation_constant()` (compile-time `contract_violation` struct with embedded semantic and detection_mode fields).

#### `build_contract_data_block_ctor()` - key function (FULL)

```cpp
static tree
build_contract_data_block_ctor (tree contract, tree *out_type)
{
  init_contract_descriptor_tables ();

  location_t loc = EXPR_LOCATION (contract);

  tree label = CONTRACT_LABEL (contract);
  bool has_label = (label && label != error_mark_node
		    && TREE_TYPE (label)
		    && !type_dependent_expression_p (label));
  bool has_handler = false;
  bool has_query = false;
  tree label_ptr_val = build_zero_cst (ptr_type_node);
  tree local_handler_val = build_zero_cst (ptr_type_node);
  tree query_function_val = build_zero_cst (ptr_type_node);

  if (has_label)
    {
      tree label_type = TREE_TYPE (label);
      if (local_violation_trampoline_map && VAR_P (label))
	{
	  tree *trampoline_p = local_violation_trampoline_map->get (label_type);
	  if (trampoline_p)
	    {
	      has_handler = true;
	      label_ptr_val = build_address (label);
	      local_handler_val = build_address (*trampoline_p);
	    }
	}
      if (query_trampoline_map && VAR_P (label))
	{
	  tree *trampoline_p = query_trampoline_map->get (label_type);
	  if (trampoline_p)
	    {
	      has_query = true;
	      if (!has_handler)
		label_ptr_val = build_address (label);
	      query_function_val = build_address (*trampoline_p);
	    }
	}
    }

  tree block_type;
  tree desc_var;
  unsigned nfields;

  if (has_handler && has_query)
    {
      block_type = contract_data_block_full_type;
      desc_var = contract_desc_full_var;
      nfields = 11;
    }
  else if (has_handler)
    {
      block_type = contract_data_block_label_type;
      desc_var = contract_desc_label_var;
      nfields = 10;
    }
  else if (has_query)
    {
      block_type = contract_data_block_query_type;
      desc_var = contract_desc_query_var;
      nfields = 10;
    }
  else
    {
      block_type = contract_data_block_basic_type;
      desc_var = contract_desc_basic_var;
      nfields = 8;
    }

  tree fndecl = current_function_decl;
  if (DECL_IS_PRE_FN_P (fndecl) || DECL_IS_POST_FN_P (fndecl))
    fndecl = get_orig_for_outlined (fndecl);
  if (DECL_IS_WRAPPER_FN_P (fndecl))
    fndecl = get_orig_func_for_wrapper (fndecl);

  const char *file = LOCATION_FILE (loc);
  if (!file)
    file = "";
  tree file_str = build_string_literal (file);

  const char *funcname = "";
  if (fndecl)
    funcname = cxx_printable_name (fndecl, 2);
  tree func_str = build_string_literal (funcname);

  tree line_val = build_int_cst (unsigned_type_node, LOCATION_LINE (loc));
  tree col_val = build_int_cst (unsigned_type_node, LOCATION_COLUMN (loc));

  tree comment = CONTRACT_COMMENT (contract);
  if (!comment)
    comment = build_string_literal ("");

  tree message = CONTRACT_MESSAGE (contract);
  if (message)
    message = build_string_literal (TREE_STRING_LENGTH (message),
				    TREE_STRING_POINTER (message));
  else
    message = build_zero_cst (const_string_type_node);

  tree desc_ptr = build_address (desc_var);

  tree fields[11];
  {
    tree f = TYPE_FIELDS (block_type);
    for (unsigned i = 0; i < nfields; i++)
      {
	fields[i] = next_aggregate_field (f);
	f = DECL_CHAIN (fields[i]);
      }
  }

  tree ctor;
  if (has_handler && has_query)
    ctor = build_constructor_va
      (block_type, 11,
       fields[0], desc_ptr,
       fields[1], build_zero_cst (ptr_type_node),
       fields[2], file_str,
       fields[3], func_str,
       fields[4], line_val,
       fields[5], col_val,
       fields[6], comment,
       fields[7], message,
       fields[8], local_handler_val,
       fields[9], query_function_val,
       fields[10], label_ptr_val);
  else if (has_handler)
    ctor = build_constructor_va
      (block_type, 10,
       fields[0], desc_ptr,
       fields[1], build_zero_cst (ptr_type_node),
       fields[2], file_str,
       fields[3], func_str,
       fields[4], line_val,
       fields[5], col_val,
       fields[6], comment,
       fields[7], message,
       fields[8], local_handler_val,
       fields[9], label_ptr_val);
  else if (has_query)
    ctor = build_constructor_va
      (block_type, 10,
       fields[0], desc_ptr,
       fields[1], build_zero_cst (ptr_type_node),
       fields[2], file_str,
       fields[3], func_str,
       fields[4], line_val,
       fields[5], col_val,
       fields[6], comment,
       fields[7], message,
       fields[8], query_function_val,
       fields[9], label_ptr_val);
  else
    ctor = build_constructor_va
      (block_type, 8,
       fields[0], desc_ptr,
       fields[1], build_zero_cst (ptr_type_node),
       fields[2], file_str,
       fields[3], func_str,
       fields[4], line_val,
       fields[5], col_val,
       fields[6], comment,
       fields[7], message);

  TREE_READONLY (ctor) = true;
  TREE_CONSTANT (ctor) = true;

  *out_type = block_type;
  return ctor;
}
```

#### `build_contract_data_block_constant()` - FULL body

```cpp
static tree
build_contract_data_block_constant (tree ctor, tree block_type, tree contract)
{
  tree var = contracts_tu_local_named_var (EXPR_LOCATION (contract),
					  "Lcontract_data", block_type);
  DECL_INITIAL (var) = ctor;
  varpool_node::finalize_decl (var);
  return var;
}
```

Master equivalent built a `contract_violation` object with `_M_detection_mode = CDM_PREDICATE_FALSE` baked in at compile time; branch moves detection mode into the **symbol name** (`_pf` vs `_ex`) and passes only a `void*` data-block pointer to the runtime.

> **D4324 mapping:** Delete the entire data-block / descriptor-table infrastructure. Pass `comment`, source location, and `cfg` directly as arguments to `T::operator()`. No rodata violation object; no label handler/query trampolines in the compiler.

---

### 2.8 Constification - `view_as_const` / `constify_contract_access`

Constification runs during **semantic analysis** (name lookup in `semantics.cc` / `pt.cc`), not inside `build_contract_check` / `emit_check_for_semantic`. There is **no** gating on evaluation semantic, `-fcontracts-*` flags, or `T::constify` on either master or branch.

#### Master

```cpp
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
      CONST_WRAPPER_P (decl) = true;
    }
  return decl;
}

tree
constify_contract_access (tree decl)
{
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
```

#### Branch (P3098 capture exemption added)

```cpp
tree
view_as_const (tree decl)
{
  /* ... identical to master ... */
}

tree
constify_contract_access (tree decl)
{
  if (!TREE_READONLY (decl)
      && (VAR_P (decl)
	  || (TREE_CODE (decl) == PARM_DECL)
	  || (REFERENCE_REF_P (decl)
	      && (VAR_P (TREE_OPERAND (decl, 0))
		  || (TREE_CODE (TREE_OPERAND (decl, 0)) == PARM_DECL)
		  || (TREE_CODE (TREE_OPERAND (decl, 0))
		      == TEMPLATE_PARM_INDEX)))))
    {
      /* P3098: Skip const-ification for postcondition capture variables.  */
      if (VAR_P (decl) && DECL_ARTIFICIAL (decl))
	return decl;
      if (REFERENCE_REF_P (decl)
	  && VAR_P (TREE_OPERAND (decl, 0))
	  && DECL_ARTIFICIAL (TREE_OPERAND (decl, 0)))
	return decl;

      decl = view_as_const (decl);
    }

  return decl;
}
```

**Precise statement:** On the branch, constification is still **always-on** for every identifier accessed inside a contract predicate during parsing/semantics, except `DECL_ARTIFICIAL` postcondition capture variables (P3098). It is **not** gated on `CES_*` semantic, `-fcontracts-*`, or any control-object property. Codegen does not re-constify; it evaluates `CONTRACT_CONDITION` as already transformed.

> **D4324 mapping:** Gate constification on compile-time `T::constify` (default off). Apply at the same semantic-analysis sites (`constify_contract_access`) or defer to codegen step 2 ("evaluate constified predicate else as-written"). Keep the P3098 capture exemption regardless. Note the challenge: constification currently happens during parsing/name-lookup, before the control-object type is necessarily known for deferred contracts, so D4324 must ensure `T::constify` is resolvable at the point constification is applied (or defer constification later).

---

### 2.9 Exception-to-violation translation

#### Master - inside `build_contract_check`

```cpp
  bool check_might_throw = (flag_exceptions
			    && !expr_noexcept_p (condition, tf_none));
  /* ... */
  if (check_might_throw)
    {
      tree check_failed = build_decl (loc, VAR_DECL, NULL, boolean_type_node);
      /* ... bind to cc_bind ... */
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
	      /* mutate _M_detection_mode to CDM_EVAL_EXCEPTION, then: */
	      finish_expr_stmt (build_call_n (tu_has_violation, 2,
					      violation, s_const));
	    }
	  else
	    finish_expr_stmt (build_call_n (tu_has_violation_exception, 2,
					    violation, s_const));
	  finish_expr_stmt (cp_build_modify_expr (loc, check_failed, NOP_EXPR,
						  boolean_false_node, ...));
	}
      finish_handler (handler);
      finish_handler_sequence (check_try);
      cond = check_failed;
    }
```

Detection mode lived **in the violation object** (`CDM_EVAL_EXCEPTION`) or a dedicated `__tu_has_violation_exception` entry point.

#### Branch - inside `emit_check_for_semantic`

Same try/catch structure, but handler calls the **`_ex` symbol variant** with the **same data block** (detection mode encoded in the name, not the data):

```cpp
  if (check_might_throw)
    {
      /* ... same check_failed / try setup ... */
      tree handler = begin_handler ();
      finish_handler_parms (NULL_TREE, handler);
      if (quick)
	finish_expr_stmt (build_call_a (terminate_wrapper, 0, nullptr));
      else
	{
	  finish_expr_stmt (build_call_n (entry_ex, 1, data_addr));
	  /* entry_ex = declare_cxa_entry_point(..., CDM_EVAL_EXCEPTION, ...) */
	  finish_expr_stmt (cp_build_modify_expr (loc, check_failed, NOP_EXPR,
						  boolean_false_node, ...));
	}
      finish_handler (handler);
      /* ... */
    }
```

`entry_ex` name example: `__cxa_contract_violation_pre_enforce_ex`.

> **D4324 mapping:** **Delete the entire try/catch block** in codegen. Predicate exceptions propagate as ordinary exceptions (stopped at `noexcept` boundaries). No `_ex` entry points, no `CDM_EVAL_EXCEPTION`, no `__tu_has_violation_exception`. This is a major simplification vs both master and branch, and it is what makes the `noexcept` guarantee hold "for free."

---

### 2.10 Semantic predicates

#### Branch-only (new in P3850)

```cpp
static inline bool
contract_semantic_emits_no_check (unsigned sem)
{
  return sem == CES_IGNORE || sem == CES_ASSUME;
}

static inline bool
contract_semantic_is_nonthrowing (unsigned sem)
{
  return sem == CES_IGNORE || sem == CES_QUICK || sem == CES_ASSUME
	 || sem == CES_NOEXCEPT_ENFORCE || sem == CES_NOEXCEPT_OBSERVE;
}

bool
contract_constexpr_ignored_p (const_tree contract)
{
  contract_evaluation_semantic s = get_constexpr_evaluation_semantic (contract);
  return s <= CES_IGNORE || contract_semantic_emits_no_check (s);
}

bool
contract_constexpr_terminating_p (const_tree contract)
{
  contract_evaluation_semantic s = get_constexpr_evaluation_semantic (contract);
  return s == CES_ENFORCE || s == CES_QUICK || s == CES_NOEXCEPT_ENFORCE;
}
```

- `contract_semantic_emits_no_check` - **new**; codegen treats `assume` like `ignore`.
- `contract_semantic_is_nonthrowing` - **new**; wrapper `noexcept` inference (P3097/P3098).
- `contract_constexpr_ignored_p` - **new**; constexpr slot, includes assume.
- `contract_constexpr_terminating_p` - **new**; constexpr slot, adds `CES_NOEXCEPT_ENFORCE`.

#### In `contracts.h` (unchanged text on branch vs master)

```cpp
inline bool
contract_ignored_p (const_tree contract)
{
  return (get_evaluation_semantic (contract) <= CES_IGNORE);
}

inline bool
contract_evaluated_p (const_tree contract)
{
  return (get_evaluation_semantic (contract) >= CES_OBSERVE);
}

inline bool
contract_terminating_p (const_tree contract)
{
  return (get_evaluation_semantic (contract) == CES_ENFORCE
	  || get_evaluation_semantic (contract) == CES_QUICK);
}
```

**Behavior change on branch:** `get_evaluation_semantic()` now requires prior `ensure_evaluation_semantic(..., false)` (asserts cached slot). `contract_ignored_p` does **not** treat `CES_ASSUME` as ignored (only `<= CES_IGNORE`); assume is handled separately via `contract_semantic_emits_no_check` in codegen paths.

Master had no `contract_semantic_*` helpers and no constexpr semantic slot helpers.

> **D4324 mapping:** Replace runtime semantic predicates with queries on `evaluation_config` + `T::is_ignored(cfg)` / `T::assumable`. Add `contract_control_ignored_p(contract, cfg)` etc. Delete `contract_semantic_is_nonthrowing`, P4298 noexcept semantics, and assume-specific branching. `contract_terminating_p` becomes "violation call returned terminate" at codegen time, not a compile-time semantic enum test.

---

### 2.11 Codegen: D4324 change checklist

**Modify**
- `build_contract_check()` - three-step algorithm; remove P3595 dynamic if-cascade; single control-object violation call
- `constify_contract_access()` - gate on compile-time `T::constify` (default off); keep P3098 capture exemption
- `get_evaluation_semantic()` / contract node slots - store `evaluation_config` + resolved control-object type instead of `CES_*`
- `contract_ignored_p`, `contract_evaluated_p`, `contract_terminating_p` in `contracts.h` - rebase on `evaluation_config` + `T::is_ignored`

**Add**
- `ensure_evaluation_config()` or extend lazy resolution to read TU `evaluation_config` and instantiate `T`
- `emit_contract_check_for_control()` (or similar) - steps 1 to 3: ignored/assume, predicate eval, `T::operator()(comment, loc, cfg)` -> proceed/terminate
- Compile-time extraction of `T::is_ignored`, `T::constify`, `T::assumable` from the template argument of `pre<T>` / `post<T>` / `assert<T>`
- Optimizer assumption emission when `T::assumable && T::is_ignored(cfg)`

**Delete**
- `emit_check_for_semantic()`, `emit_enforced_violation()`
- `ensure_evaluation_semantic()` downstream config/label resolution used only for codegen semantics (`contract_config_resolve`, `apply_compute_semantic`, `make_contract_query`, `contract_base_allowed_mask`, P3595 dynamic descriptor caching) - keep the function as the choke point, change its body
- `get_cxa_entry_point_name()`, `declare_cxa_entry_point()`, `cxa_entry_point_cache`
- Entire data-block layer: `init_contract_data_block_types()`, `init_contract_descriptor_tables()`, `build_contract_data_block_ctor()`, `build_contract_data_block_constant()`, descriptor table types/vars
- try/catch exception-to-violation translation in per-check emission (both master `tu_has_violation_exception` pattern and branch `_ex` entry points)
- Master remnants if still present: `build_contract_violation_ctor()`, `build_contract_violation_constant()`, `declare_violation_handler_wrappers()`, `tu_has_violation*`
- Branch-only semantic helpers tied to removed semantics: `contract_semantic_is_nonthrowing()`, P4298 `CES_NOEXCEPT_*` paths, `-fcontracts-allow-assume` assume-as-ignore codegen

**Keep unchanged**
- `cp-gimplify.cc` genericization hook (`ASSERTION_STMT` / `PRECONDITION_STMT` / `POSTCONDITION_STMT` -> `build_contract_check`)
- `view_as_const()` mechanism (VCE + `CONST_WRAPPER_P`)
- Postcondition `remap_retval`, `remap_dummy_this`, outlined-check paths (unless D4324 scope explicitly changes them)

---

## 3. Parser, AST layout, and enums

Reference extracted from GCC `berne/contracts-p3850` vs master (`gnu_gcc/gcc/cp/`). Master has a compact 5/6-operand layout with evaluation semantic at operand 0; the branch expands to 12/14 operands, moves semantics to lazy slots 9-11, and adds P3099/P3098/P3400/P4283/P3595 fields.

### 3.1 Master contract node definitions (`cp-tree.def`)

```cpp
/* Different flavors of contracts.

   Assertions and preconditions have two operands: a node containing
   the their mode and condition. Postconditions have an additional
   operand to store the optional name for the result value.

   CONTRACT_SEMANTIC has the computed behavior of the contract.  */
DEFTREECODE (ASSERTION_STMT, "assertion_stmt", tcc_statement, 5)
DEFTREECODE (PRECONDITION_STMT, "precondition_stmt", tcc_statement, 5)
DEFTREECODE (POSTCONDITION_STMT, "postcondition_stmt", tcc_statement, 6)
```

Master operand layout (from `contracts.h` on master):

| Index | Macro | Meaning |
|------:|-------|---------|
| 0 | `CONTRACT_EVALUATION_SEMANTIC` | TU-wide semantic from `-fcontract-evaluation-semantic` (set eagerly in `grok_contract`) |
| 1 | `CONTRACT_ASSERTION_KIND` | `CAK_PRE` / `CAK_POST` / `CAK_ASSERT` |
| 2 | `CONTRACT_CONDITION` | Parsed predicate (or `DEFERRED_PARSE`) |
| 3 | `CONTRACT_COMMENT` | Stringified condition for diagnostics |
| 4 | `CONTRACT_STD_SOURCE_LOC` | Optional `std::source_location` |
| 5 | `POSTCONDITION_IDENTIFIER` | Post only: result name (`VAR_DECL` or deferred `IDENTIFIER`) |

### 3.2 Branch contract node definitions (`cp-tree.def`)

```cpp
/* Different flavors of contracts (P3595 configuration system).

   ASSERTION_STMT, PRECONDITION_STMT: 12 operands

   Group 1 -- Structural (ops 0-6, immutable after parse):
     Op0: kind (CAK_PRE, CAK_POST, CAK_ASSERT, etc.)
     Op1: condition (the assertion itself)
     Op2: comment (optional user comment string)
     Op3: source_location (location_t as tree)
     Op4: message (optional diagnostic message, P3099)
     Op5: label (optional assertion-control label, P3400)
     Op6: requires_clause (optional requires-clause constraint, P4283)

   Group 2 -- Config resolution inputs (ops 7-8, eager, set at parse time):
     Op7: allowed_mask (uint16_t INTEGER_CST from label; NULL_TREE=CES_ALL_ALLOWED)
     Op8: groups (TREE_LIST of STRING_CSTs from label; NULL_TREE=none)

   Group 3 -- Evaluation semantics (ops 9-11, lazy, NULL_TREE=unresolved):
     Op9:  runtime callee-side semantic
     Op10: constexpr callee-side semantic
     Op11: runtime dynamic-selector descriptor (P3595 output.dynamic), a
	   TREE_LIST (PURPOSE=IDENTIFIER_NODE name, VALUE=INTEGER_CST packing
	   dyn_linkage<<1|dyn_provideweak), or NULL_TREE when not dynamic

   POSTCONDITION_STMT: 14 operands (ops 0-11 same as above, plus):
     Op12: postcondition_identifier (optional result name)
     Op13: postcondition_captures (optional TREE_LIST of capture VAR_DECLs, P3098)  */
DEFTREECODE (ASSERTION_STMT, "assertion_stmt", tcc_statement, 12)
DEFTREECODE (PRECONDITION_STMT, "precondition_stmt", tcc_statement, 12)
DEFTREECODE (POSTCONDITION_STMT, "postcondition_stmt", tcc_statement, 14)
```

Branch operand table (macro names from branch `contracts.h`):

| Index | Macro | Group | Meaning |
|------:|-------|-------|---------|
| 0 | `CONTRACT_ASSERTION_KIND` | 1 | `CAK_*` assertion kind |
| 1 | `CONTRACT_CONDITION` | 1 | Predicate or `DEFERRED_PARSE` |
| 2 | `CONTRACT_COMMENT` | 1 | Raw comment string |
| 3 | `CONTRACT_STD_SOURCE_LOC` | 1 | Optional `std::source_location` |
| 4 | `CONTRACT_MESSAGE` | 1 | P3099 diagnostic message (`STRING_CST` or NULL) |
| 5 | `CONTRACT_LABEL` | 1 | P3400 assertion-control label expression (or NULL) |
| 6 | `CONTRACT_REQUIRES_CLAUSE` | 1 | P4283 requires-clause (or NULL) |
| 7 | `CONTRACT_ALLOWED_MASK` | 2 | `uint16_t` bitmask from label's `allowed_semantics`; NULL = unrestricted |
| 8 | `CONTRACT_GROUPS` | 2 | Lazy `TREE_LIST` of group name strings from label |
| 9 | `CONTRACT_EVALUATION_SEMANTIC` | 3 | Runtime semantic (lazy; NULL = unresolved) |
| 10 | `CONTRACT_CONSTEXPR_EVALUATION_SEMANTIC` | 3 | Constexpr semantic (lazy; NULL = unresolved) |
| 11 | `CONTRACT_DYNAMIC` | 3 | P3595 dynamic selector descriptor (`TREE_LIST`) |
| 12 | `POSTCONDITION_IDENTIFIER` | 4 | Post only: result name |
| 13 | `POSTCONDITION_CAPTURES` | 4 | Post only: P3098 capture `VAR_DECL` list |

> **D4324 mapping:** Reuse operand 5 (`CONTRACT_LABEL` slot) for the **control-object TYPE** (`TYPE` node from `<type-id>`), renamed e.g. `CONTRACT_CONTROL_TYPE`. D4324 does not need operands 7-8 (label facet bitmasks/groups) or 11 (P3595 dynamic descriptor) at parse time; semantics come from compile-time members on the control type (`is_ignored`, `constify`, `assumable`) resolved later. Operand count can stay 12/14 if D4324 adopts message (4), requires-clause (6), and post captures (13); otherwise trim unused slots.

### 3.3 Accessor macros (branch `contracts.h`)

```cpp
#define CONTRACT_CHECK(NODE) \
  (TREE_CHECK3 (NODE, ASSERTION_STMT, PRECONDITION_STMT, POSTCONDITION_STMT))

/* Group 1 -- Structural (ops 0-6, immutable after parse).  */

/* The assertion kind (CAK_PRE, CAK_POST, CAK_ASSERT).  */
#define CONTRACT_ASSERTION_KIND(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 0))

/* The parsed condition of the contract.  */
#define CONTRACT_CONDITION(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 1))

/* The raw comment of the contract.  */
#define CONTRACT_COMMENT(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 2))

/* A std::source_location, if provided.  */
#define CONTRACT_STD_SOURCE_LOC(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 3))

/* The user-defined diagnostic message (P3099), or NULL_TREE if none.  */
#define CONTRACT_MESSAGE(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 4))

/* The assertion-control label (P3400), or NULL_TREE if none.  */
#define CONTRACT_LABEL(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 5))

/* The requires-clause constraint (P4283), or NULL_TREE if none.  */
#define CONTRACT_REQUIRES_CLAUSE(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 6))

/* Group 2 -- Config resolution inputs (ops 7-8, eager, set at parse time).  */

/* Bitmask of semantics allowed by the label's allowed_semantics facet
   (uint16_t INTEGER_CST).  NULL_TREE means CES_ALL_ALLOWED.  */
#define CONTRACT_ALLOWED_MASK(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 7))

/* Group names from the label's group_names facet (TREE_LIST of
   STRING_CSTs), populated lazily by fill_query_groups.
   NULL_TREE = not yet extracted; error_mark_node = no groups.  */
#define CONTRACT_GROUPS(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 8))

/* Group 3 -- Evaluation semantics (ops 9-10, lazy, NULL_TREE=unresolved).  */

/* The runtime callee-side evaluation semantic.  */
#define CONTRACT_EVALUATION_SEMANTIC(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 9))

/* The constexpr callee-side evaluation semantic.  */
#define CONTRACT_CONSTEXPR_EVALUATION_SEMANTIC(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 10))

/* The runtime dynamic-selector descriptor (P3595 output.dynamic), set
   lazily by ensure_evaluation_semantic when !in_ce.  NULL_TREE means the
   contract has no dynamic selection.  When present it is a TREE_LIST whose
   TREE_PURPOSE is an IDENTIFIER_NODE (the selector name) and whose
   TREE_VALUE is an INTEGER_CST packing (dyn_linkage << 1 | dyn_provideweak).  */
#define CONTRACT_DYNAMIC(NODE) \
  (TREE_OPERAND (CONTRACT_CHECK (NODE), 11))

/* Group 4 -- Postcondition-specific (ops 12-13, POSTCONDITION_STMT only).  */

/* The VAR_DECL of a postcondition result.  For deferred contracts, this
   is an IDENTIFIER.  */
#define POSTCONDITION_IDENTIFIER(NODE) \
  (TREE_OPERAND (POSTCONDITION_STMT_CHECK (NODE), 12))

/* The postcondition captures -- a TREE_LIST of capture VAR_DECLs with
   DECL_INITIAL set to the initializer expression, or NULL_TREE if the
   postcondition has no captures (P3098).  */
#define POSTCONDITION_CAPTURES(NODE) \
  (TREE_OPERAND (POSTCONDITION_STMT_CHECK (NODE), 13))
```

Branch `grok_contract` declaration (three new trailing parameters):

```cpp
extern tree grok_contract			(tree, tree, tree, cp_expr,
						 location_t,
						 tree = NULL_TREE,
						 tree = NULL_TREE,
						 tree = NULL_TREE);
```

> **D4324 mapping:** Add `CONTRACT_CONTROL_TYPE(NODE)` at index 5 (replacing `CONTRACT_LABEL`). Keep `CONTRACT_EVALUATION_SEMANTIC` (op 9) and `CONTRACT_CONSTEXPR_EVALUATION_SEMANTIC` (op 10) as lazy resolution targets populated from control-object queries. Drop or stub `CONTRACT_ALLOWED_MASK`, `CONTRACT_GROUPS`, `CONTRACT_DYNAMIC`, and all `apply_label_*` / trampoline paths unless D4324 later adopts P3595 dynamic selection.

### 3.4 Enums

#### Master (`gnu_gcc/gcc/cp/contracts.h`)

```cpp
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
```

#### Branch shared config (`c-family/contracts-config.h`)

```cpp
enum contract_assertion_kind : unsigned short {
  CAK_INVALID = 0,
  CAK_PRE = 1,
  CAK_POST = 2,
  CAK_ASSERT = 3,
  CAK_MANUAL = 4,
  CAK_CASSERT = 5,
  CAK_POST_CAPTURE = 6,
};

enum contract_evaluation_semantic : unsigned short {
  CES_INVALID = 0,
  CES_IGNORE = 1,
  CES_OBSERVE = 2,
  CES_ENFORCE = 3,
  CES_QUICK = 4,
  CES_ASSUME = 5,
  CES_NOEXCEPT_OBSERVE = 6,
  CES_NOEXCEPT_ENFORCE = 7,
};

enum contract_dyn_linkage {
  CDL_CXX = 0,   /* "C++" (default): name may be qualified; compiler mangles */
  CDL_C   = 1    /* "C": name is the verbatim symbol (no mangling) */
};

#define CES_ALL_ALLOWED \
  ((1 << CES_IGNORE) | (1 << CES_OBSERVE) \
   | (1 << CES_ENFORCE) | (1 << CES_QUICK))

#define CES_ALL_ALLOWED_WITH_ASSUME \
  (CES_ALL_ALLOWED | (1 << CES_ASSUME))

#define CES_ALL_ALLOWED_WITH_EXTENSIONS \
  (CES_ALL_ALLOWED_WITH_ASSUME \
   | (1 << CES_NOEXCEPT_ENFORCE) | (1 << CES_NOEXCEPT_OBSERVE))
```

Branch `contracts.h` moves enums to `contracts-config.h` and retains only:

```cpp
enum detection_mode : uint16_t {
  CDM_UNSPECIFIED = 0,
  CDM_PREDICATE_FALSE = 1,
  CDM_EVAL_EXCEPTION = 2
};
```

> **D4324 mapping:** **Keep** `CAK_PRE`, `CAK_POST`, `CAK_ASSERT`, and core `CES_IGNORE` / `CES_OBSERVE` / `CES_ENFORCE` / `CES_QUICK`. **Keep** `CES_ASSUME` if D4324's `assumable` member maps to assume semantics. **Drop** (unless explicitly adopted) `CAK_MANUAL`, `CAK_CASSERT`, `CAK_POST_CAPTURE`, `CES_NOEXCEPT_OBSERVE`, `CES_NOEXCEPT_ENFORCE`, `contract_dyn_linkage`, and all `CES_ALL_ALLOWED*` bitmask helpers tied to P3400 label facets. Note that D4324's `evaluation_config` enum is a distinct concept (the build-mode selector passed to `is_ignored`/`operator()`), not the same as `contract_evaluation_semantic`.

### 3.5 Parser additions (branch `parser.cc` diff)

Grammar order after introducer (`pre` / `post` / `contract_assert`):

1. Optional `< ... >` assertion-control specifier (P3400)
2. Optional `requires (...)` (P4283)
3. Optional `[[attributes]]`
4. Optional `[ capture-list ]` (P3098, post only)
5. `(` predicate `)` with optional `, message`

#### `cp_parser_contract_control_using_directive` (P3400 namespace visibility)

```cpp
/* Parse a contract-control using directive:
     using contract_control namespace_opt nested-name-specifier_opt namespace-name ;
   Names from the specified namespace become visible only within
   assertion-control expressions in the enclosing scope.  */

static void
cp_parser_contract_control_using_directive (cp_parser* parser)
{
  cp_parser_require_keyword (parser, RID_USING, RT_USING);
  cp_lexer_consume_token (parser->lexer);  /* contract_control */
  if (cp_lexer_next_token_is_keyword (parser->lexer, RID_NAMESPACE))
    cp_lexer_consume_token (parser->lexer);

  cp_parser_global_scope_opt (parser, /*current_scope_valid_p=*/false);
  cp_parser_nested_name_specifier_opt (parser,
				       /*typename_keyword_p=*/false,
				       /*check_dependency_p=*/true,
				       /*type_p=*/false,
				       /*is_declaration=*/true);
  tree namespace_decl = cp_parser_namespace_name (parser);

  if (namespace_decl && namespace_decl != error_mark_node)
    finish_contract_control_using_directive (namespace_decl);

  cp_parser_require (parser, CPP_SEMICOLON, RT_SEMICOLON);
}
```

Also: `RID_CONTRACT_CONTROL` postfix form in `cp_parser_postfix_expression` sets `in_assertion_control_expression_p` and parses a constant expression inside `__contract_control(...)`.

#### `cp_parser_diagnostic_message` and `cp_parser_contract_message` (P3099)

```cpp
static tree
cp_parser_diagnostic_message (cp_parser *parser, bool *non_string_p)
{
  if (non_string_p)
    *non_string_p = false;

  bool string_lit = true;
  for (unsigned int i = 1; ; ++i)
    {
      cp_token *tok = cp_lexer_peek_nth_token (parser->lexer, i);
      if (cp_parser_is_pure_string_literal (tok))
	continue;
      else if (tok->type == CPP_CLOSE_PAREN)
	break;
      string_lit = false;
      break;
    }

  if (!string_lit)
    {
      if (non_string_p)
	*non_string_p = true;
      location_t loc = cp_lexer_peek_token (parser->lexer)->location;
      tree message = cp_parser_conditional_expression (parser);
      if (TREE_CODE (message) == STRING_CST)
	message = build1_loc (loc, PAREN_EXPR, TREE_TYPE (message), message);
      return message;
    }
  else if (cxx_dialect >= cxx26)
    return cp_parser_unevaluated_string_literal (parser);
  else
    return cp_parser_string_literal (parser, /*translate=*/false,
				     /*wide_ok=*/true);
}

static tree
cp_parser_contract_message (cp_parser *parser)
{
  if (!flag_contracts_p3099
      || !cp_lexer_next_token_is (parser->lexer, CPP_COMMA))
    return NULL_TREE;

  cp_lexer_consume_token (parser->lexer);
  return cp_parser_diagnostic_message (parser);
}
```

#### `cp_parser_assertion_control_specifier` (P3400 label / control expression)

Parses `< constant-expression >`, not `[= label]`:

```cpp
static tree
cp_parser_assertion_control_specifier (cp_parser *parser)
{
  if (!cp_lexer_next_token_is (parser->lexer, CPP_LESS))
    return NULL_TREE;

  if (!flag_contracts_p3400)
    {
      error_at (cp_lexer_peek_token (parser->lexer)->location,
		"assertion-control labels require %<-fcontracts-p3400%>");
      /* skip <...> recovery ... */
      return NULL_TREE;
    }

  cp_lexer_consume_token (parser->lexer);

  bool saved_greater_than_is_operator_p
    = parser->greater_than_is_operator_p;
  parser->greater_than_is_operator_p = false;

  auto aco_override
    = make_temp_override (in_assertion_control_expression_p, true);

  cp_expr label_expr
    = cp_parser_constant_expression (parser, NIC_NONE, NULL);

  parser->greater_than_is_operator_p = saved_greater_than_is_operator_p;

  if (cp_lexer_next_token_is (parser->lexer, CPP_RSHIFT))
    {
      cp_token *token = cp_lexer_peek_token (parser->lexer);
      token->type = CPP_GREATER;
    }
  else if (!cp_parser_require (parser, CPP_GREATER, RT_GREATER))
    return error_mark_node;

  if (label_expr == error_mark_node)
    return error_mark_node;

  return label_expr;
}
```

#### `cp_parser_contract_requires_clause` (P4283)

```cpp
static tree
cp_parser_contract_requires_clause (cp_parser *parser)
{
  if (cp_lexer_peek_token (parser->lexer)->keyword != RID_REQUIRES)
    return NULL_TREE;

  location_t loc = cp_lexer_peek_token (parser->lexer)->location;

  if (!flag_contracts_p4283)
    {
      error_at (loc,
		"requires clause on contract assertions requires "
		"%<-fcontracts-p4283%>");
      cp_lexer_consume_token (parser->lexer);
      if (cp_lexer_next_token_is (parser->lexer, CPP_OPEN_PAREN))
	cp_parser_skip_to_closing_parenthesis (parser, true, false, true);
      return error_mark_node;
    }

  if (!processing_template_decl)
    {
      error_at (loc,
		"requires clause on contract assertion only allowed on "
		"templated functions");
      cp_lexer_consume_token (parser->lexer);
      if (cp_lexer_next_token_is (parser->lexer, CPP_OPEN_PAREN))
	cp_parser_skip_to_closing_parenthesis (parser, true, false, true);
      return error_mark_node;
    }

  return cp_parser_requires_clause_opt (parser, /*lambda_p=*/true);
}
```

#### `cp_parser_contract_assert`

```cpp
  /* Parse optional assertion-control-specifier: < constant-expression >  */
  tree label = cp_parser_assertion_control_specifier (parser);

  /* Parse optional requires-clause (P4283).  */
  tree requires_clause = cp_parser_contract_requires_clause (parser);

  if (requires_clause == error_mark_node)
    {
      cp_parser_skip_to_end_of_statement (parser);
      cp_parser_consume_semicolon_at_end_of_statement (parser);
      return error_mark_node;
    }

  /* Reject captures on contract_assert (P3098: captures only on post).  */
  if (cp_lexer_next_token_is (parser->lexer, CPP_OPEN_SQUARE))
    {
      error_at (cp_lexer_peek_token (parser->lexer)->location,
		"postcondition captures only allowed on %<post%> assertions");
      /* skip [...] recovery ... */
    }

  begin_scope (sk_contract, current_function_decl);
  bool old_pc = processing_postcondition_predicate;
  processing_postcondition_predicate = false;
  cp_expr condition = cp_parser_conditional_expression (parser);

  tree message = cp_parser_contract_message (parser);

  tree contract = grok_contract (cont_assert, /*mode*/NULL_TREE,
			    /*result*/NULL_TREE, condition, loc, message,
			    label, requires_clause);
  processing_postcondition_predicate = old_pc;
```

#### `cp_maybe_function_contract_specifier` look-ahead

Skips `< ... >`, `requires (...)`, attributes, and `[ ... ]` before `(`:

```cpp
  size_t n = 2;
  /* Skip optional assertion-control-specifier: < ... >  */
  if (cp_lexer_nth_token_is (parser->lexer, n, CPP_LESS))
    { /* depth walk over < > >> */ }
  /* Skip optional requires-clause: requires ( ... )  (P4283)  */
  if (cp_lexer_nth_token_is_keyword (parser->lexer, n, RID_REQUIRES))
    { /* depth walk over (...) */ }
  if (cp_nth_tokens_can_be_std_attribute_p (parser, n))
    n = cp_parser_skip_std_attribute_spec_seq (parser, n);
  /* Skip optional postcondition capture list: [ ... ] */
  if (cp_lexer_nth_token_is (parser->lexer, n, CPP_OPEN_SQUARE))
    { /* depth walk over [...] */ }
  if (cp_lexer_nth_token_is (parser->lexer, n, CPP_OPEN_PAREN))
    return contract_name;
```

#### `cp_parser_function_contract_specifier`

After consuming `pre`/`post`:

```cpp
  tree label = cp_parser_assertion_control_specifier (parser);
  tree requires_clause = cp_parser_contract_requires_clause (parser);

  if (requires_clause == error_mark_node)
    {
      if (cp_lexer_next_token_is (parser->lexer, CPP_OPEN_PAREN))
	cp_parser_skip_to_closing_parenthesis (parser, true, false, true);
      return error_mark_node;
    }

  /* Parse optional postcondition capture list: [ capture-list ].
     First pass: validate syntax, save token caches for init-captures.  */
  struct pending_capture {
    tree name;
    location_t loc;
    cp_token_cache *init_tokens;
    tree param_decl;
    bool pack_expansion;
  };
  auto_vec<pending_capture, 4> pending_captures;
  /* ... capture validation and token-cache saving ... */

  /* Deferred path: */
  contract = grok_contract (contract_name, /*mode*/NULL_TREE, identifier,
				condition, loc, /*message*/NULL_TREE, label,
				requires_clause);
  POSTCONDITION_CAPTURES (CONTRACT_CHECK (contract)) = cap_list;

  /* Immediate path: parse captures inside sk_contract scope first,
     then enable processing_postcondition_predicate, then predicate: */
  tree message = cp_parser_contract_message (parser);
  contract = grok_contract (contract_name, /*mode*/NULL_TREE, result,
				condition, loc, message, label,
				requires_clause);
  POSTCONDITION_CAPTURES (CONTRACT_CHECK (contract)) = captures;
```

> **D4324 mapping:** Replace `cp_parser_assertion_control_specifier`'s `cp_parser_constant_expression` with **`cp_parser_type_id`** (or template-id for `pre<review>`). No `in_assertion_control_expression_p` / `using contract_control` / facet validation. Default bare `pre(cond)` stores NULL at op 5; semantic resolution substitutes `std::contracts::default_v`. Parser change is roughly 30 lines vs branch's constant-expression + namespace machinery.

### 3.6 `grok_contract`: master vs branch

#### Master: `mode` discarded, semantic written to operand 0

```cpp
tree
grok_contract (tree contract_spec, tree mode, tree result, cp_expr condition,
	       location_t loc)
{
  /* ... kind dispatch ... */
  if (code != POSTCONDITION_STMT)
    contract = build5_loc (loc, code, void_type_node, mode,
			   NULL_TREE, NULL_TREE, NULL_TREE, NULL_TREE);
  else
    {
      contract = build_nt (code, mode, NULL_TREE, NULL_TREE,
			   NULL_TREE, NULL_TREE, result);
      TREE_TYPE (contract) = void_type_node;
      SET_EXPR_LOCATION (contract, loc);
    }

  CONTRACT_ASSERTION_KIND (contract) = build_int_cst (uint16_type_node, kind);

  /* Overwrites operand 0 regardless of /*mode*/NULL_TREE from parser */
  CONTRACT_EVALUATION_SEMANTIC (contract)
    = build_int_cst (uint16_type_node, (uint16_t)
		     flag_contract_evaluation_semantic);

  if (TREE_CODE (condition) == DEFERRED_PARSE)
    {
      CONTRACT_CONDITION (contract) = condition;
      return contract;
    }

  CONTRACT_COMMENT (contract) = build_comment (condition);
  condition = finish_contract_condition (condition);
  CONTRACT_CONDITION (contract) = condition;
  return contract;
}
```

Parser always passes `/*mode*/NULL_TREE`; TU flag wins immediately.

#### Branch: expanded node, label stored at op 5, semantics lazy

```cpp
tree
grok_contract (tree contract_spec, tree mode, tree result, cp_expr condition,
	       location_t loc, tree message, tree label,
	       tree requires_clause)
{
  /* PRE/ASSERT: build_nt with 12 NULL_TREE operands */
  /* POST: build_nt with 14 operands; result at op 12 */

  CONTRACT_LABEL (contract) = label;
  CONTRACT_REQUIRES_CLAUSE (contract) = requires_clause;

  /* Validate label type has nested assertion_control_object (P3400) */
  /* Generate local_violation_trampoline / query_trampoline if facets present */
  /* Probe label.allowed_semantics -> CONTRACT_ALLOWED_MASK */

  CONTRACT_ASSERTION_KIND (contract) = build_int_cst (uint16_type_node, kind);

  /* Semantic slots 9-10 stay NULL_TREE; filled by ensure_evaluation_semantic */
  finish_contract_message (contract, message, condition, loc);

  if (TREE_CODE (condition) == DEFERRED_PARSE)
    {
      CONTRACT_CONDITION (contract) = condition;
      return contract;
    }

  CONTRACT_COMMENT (contract) = build_comment (condition);
  CONTRACT_COMMENT (contract)
    = apply_label_string_facet (label, "compute_comment",
				CONTRACT_COMMENT (contract), loc);
  condition = finish_contract_condition (condition);
  CONTRACT_CONDITION (contract) = condition;
  return contract;
}
```

`finish_contract_message` on branch:

```cpp
tree
finish_contract_message (tree contract, tree message, tree condition,
			 location_t loc)
{
  /* strip location wrapper; cexpr_str extract to STRING_CST if not deferred */
  CONTRACT_MESSAGE (contract) = message;
  CONTRACT_MESSAGE (contract)
    = apply_label_string_facet (CONTRACT_LABEL (contract), "compute_message",
				CONTRACT_MESSAGE (contract), loc);
  return CONTRACT_MESSAGE (contract);
}
```

Key behavioral change: **`mode` parameter is unused** on the branch too; control flows through **`label`** (operand 5) and lazy resolution via `ensure_evaluation_semantic` / `contract_config_resolve`, not through operand 0.

> **D4324 mapping:** Extend `grok_contract` to accept `tree control_type` (from `<type-id>`) instead of/in addition to `label`. Validate that `control_type` satisfies D4324's assertion-control-object concept (simpler than P3400's nested `assertion_control_object` + facet probes). Store type at operand 5; defer `is_ignored`/`constify`/`assumable`/`operator()` resolution to semantic analysis. No `CONTRACT_ALLOWED_MASK` computation at grok time.

### 3.7 Parser/AST: D4324 change checklist

| Area | Exact edit |
|------|------------|
| **`cp-tree.def`** | Expand `ASSERTION_STMT` / `PRECONDITION_STMT` to 12 operands, `POSTCONDITION_STMT` to 14 (or trimmed subset). Document op 5 as control-object **type**. |
| **`contracts.h`** | Reorder accessors to branch layout. Add `CONTRACT_CONTROL_TYPE` at op 5. Keep lazy `CONTRACT_EVALUATION_SEMANTIC` (op 9) and `CONTRACT_CONSTEXPR_EVALUATION_SEMANTIC` (op 10). Add `CONTRACT_MESSAGE`, `CONTRACT_REQUIRES_CLAUSE`, `POSTCONDITION_CAPTURES` if adopted. |
| **`contracts-config.h`** | Keep core `CAK_*` and `CES_*` through `CES_QUICK`; add `CES_ASSUME` only if needed. Omit P3400/P3595 extensions unless required. |
| **Parser: new helper** | `cp_parser_contract_control_type` - parse optional `< type-id >` after introducer; default NULL (maps to `std::contracts::default_v`). |
| **Parser: `cp_parser_contract_assert`** | Call control-type helper before requires/attrs/captures; pass type to `grok_contract`. |
| **Parser: `cp_parser_function_contract_specifier`** | Same control-type call; update deferred and immediate `grok_contract` calls. |
| **Parser: `cp_maybe_function_contract_specifier`** | Extend look-ahead to skip `< type-id >` (reuse branch's angle-bracket depth walk). |
| **`grok_contract` signature** | `(tree spec, tree mode, tree result, cp_expr cond, location_t loc, tree message = NULL, tree control_type = NULL, tree requires = NULL)` |
| **`grok_contract` body** | `build_nt` 12/14 operands; set `CONTRACT_CONTROL_TYPE`; skip P3400 facet/trampoline/`allowed_mask` logic; leave semantic slots NULL for later control-object resolution. |
| **`ensure_evaluation_semantic`** | New resolution path: read compile-time members from `CONTRACT_CONTROL_TYPE` instead of `CONTRACT_LABEL` + `contract_config_resolve` query. |
| **Drop (D4324 minimal)** | `cp_parser_assertion_control_specifier`, `cp_parser_contract_control_using_directive`, `RID_CONTRACT_CONTROL`, `CONTRACT_ALLOWED_MASK`, `CONTRACT_GROUPS`, `CONTRACT_DYNAMIC`, label facet trampolines. |

---

## 4. Library surface and runtime ABI

GCC's `contracts-p3850` branch implements a full descriptor-chain ABI with a separate pure-C `libcontracts` runtime, a matrix of specialized `__cxa_contract_violation_*` entry points, and C++ `_noexcept` wrappers. D4324 collapses this into a single library-side `control_object(comment, loc, cfg) -> violation_response` call, reusing P2900's `contract_violation` object and `handle_contract_violation` handler.

### 4.1 `<contracts>` header (P3850 branch)

The branch header extends P2900 with D4298 noexcept semantics, P3100 `assume`, P3400 labels, and P3290 manual-violation API. `contract_violation` holds only a chain pointer; all fields are read via `__cxa_find_field`.

```cpp
enum class evaluation_semantic : __UINT16_TYPE__ {
  unspecified = 0,
  ignore = 1,
  observe = 2,
  enforce = 3,
  quick_enforce = 4,
  assume = 5,              // P3100
  noexcept_observe = 6,    // D4298
  noexcept_enforce = 7,    // D4298
};

constexpr bool
is_nonthrowing(evaluation_semantic __s) noexcept
{
  return __s == evaluation_semantic::ignore
      || __s == evaluation_semantic::quick_enforce
      || __s == evaluation_semantic::assume
      || __s == evaluation_semantic::noexcept_enforce
      || __s == evaluation_semantic::noexcept_observe;
}

class contract_violation {
  const __cxxabiv1::__cxa_contract_data_block* _M_chain;
public:
  explicit contract_violation(
      const __cxxabiv1::__cxa_contract_data_block* __chain) noexcept
  : _M_chain(__chain) {}

  assertion_kind kind() const noexcept;
  evaluation_semantic semantic() const noexcept;
  contracts::detection_mode detection_mode() const noexcept;
  const char* comment() const noexcept;
  const char* message() const noexcept;
  std::source_location location() const noexcept;
  bool is_terminating() const noexcept;
  void* query_control_object(const void* __key,
                             std::size_t __index = 0) const;
};
```

Member implementations (in `contract26.cc`) walk the chain:

```cpp
assertion_kind contract_violation::kind() const noexcept {
  auto __raw = __cxa_find_field_value<__UINT8_TYPE__>(
      _M_chain, CXA_FIELD_ASSERTION_KIND, CXA_AK_UNSPECIFIED);
  return static_cast<assertion_kind>(__raw);
}
// semantic(), detection_mode(), comment(), message(), location()
// all use __cxa_find_field_value / __cxa_find_field_ptr similarly
```

**P3400 labels** (`__cpp_lib_contracts_labels`): facet concepts (`semantic_computation_label`, `local_violation_label`, `compute_comment_label`, etc.), `review_t` (maps enforce/quick_enforce to observe), `fixed_message_label_t`, `assertion_group_label`, and `operator|` combining labels into `__combined_label`.

**P3290 manual-violation API** (`__cpp_lib_contracts_api`):

```cpp
[[noreturn]] void handle_enforced_contract_violation(
    const char* __comment,
    const std::source_location& __location
        = std::source_location::current());

void handle_observed_contract_violation(/* ... */);

[[noreturn]] void handle_quick_enforced_contract_violation(/* ... */) noexcept;

// D4298: nothrow_t overloads in inline namespace __p4298 when
// __cpp_contracts_nonthrowing_semantics is defined
[[noreturn]] void handle_enforced_contract_violation(
    std::nothrow_t, const char* __comment,
    const std::source_location& __location = ...) noexcept;
void handle_observed_contract_violation(
    std::nothrow_t, const char* __comment,
    const std::source_location& __location = ...) noexcept;
```

**Master (P2900 baseline) contrast** - flat struct, inline accessors, no chain:

```cpp
class contract_violation {
  __UINT16_TYPE__ _M_version;
  assertion_kind _M_assertion_kind;
  evaluation_semantic _M_evaluation_semantic;
  detection_mode _M_detection_mode;
  const char* _M_comment;
  const void* _M_src_loc_ptr;
  __vendor_ext* _M_ext;
public:
  assertion_kind kind() const noexcept { return _M_assertion_kind; }
  evaluation_semantic semantic() const noexcept { return _M_evaluation_semantic; }
  detection_mode mode() const noexcept { return _M_detection_mode; }
  const char* comment() const noexcept { return _M_comment; }
  std::source_location location() const noexcept { /* ... */ }
  bool is_terminating() const noexcept { /* enforce || quick_enforce */ }
};
```

> **D4324 mapping:** Keeps P2900-style flat `contract_violation` (or equivalent populated object) and `handle_contract_violation`. Deletes descriptor-chain storage, `__cxa_find_field`, `message()`, `query_control_object()`, and the entire P3400 labels namespace. Adds `evaluation_config { ignore=0, observe=1, enforce=2, quick_enforce=3 }` and three control objects instead of label facets.

### 4.2 Descriptor-chain ABI

Wire structs live in `<bits/contracts_abi.h>` (C++) and `libcontracts/contracts-abi.h` (C mirror). Layouts must match exactly.

**Wire enums:**

```cpp
enum __cxa_evaluation_semantic_t : __UINT8_TYPE__ {
  CXA_ES_UNSPECIFIED      = 0x00,
  CXA_ES_IGNORE           = 0x01,
  CXA_ES_OBSERVE          = 0x02,
  CXA_ES_ENFORCE          = 0x03,
  CXA_ES_QUICK_ENFORCE    = 0x04,
  CXA_ES_NOEXCEPT_OBSERVE = 0x06,
  CXA_ES_NOEXCEPT_ENFORCE = 0x07,
};

enum __cxa_detection_mode_t : __UINT8_TYPE__ {
  CXA_DM_UNSPECIFIED          = 0x00,
  CXA_DM_PREDICATE_FALSE      = 0x01,
  CXA_DM_EVALUATION_EXCEPTION = 0x02,
};
```

**Struct layouts:**

```cpp
struct __cxa_source_location {
  const char* file_name;
  const char* function_name;
  unsigned line;
  unsigned column;
};

union __cxa_descriptor_data_t {
  __UINTPTR_TYPE__ offset;   // standard fields (ID < 0x40)
  const void*      pointer;  // extended fields (ID >= 0x40)
};

// Binary layout:
//   byte 0:     [table_version : 4][vendor_id : 4]
//   byte 1:     num_entries
//   bytes 2..:  field_ids[num_entries]
//   <pad to alignof(__cxa_descriptor_data_t)>
//   data[num_entries]
struct __cxa_descriptor_table_t {
  __UINT8_TYPE__ header;
  __UINT8_TYPE__ num_entries;
  __UINT8_TYPE__ field_ids[];
  // data[] at aligned offset via data() accessor
};

struct __cxa_contract_data_block {
  const __cxa_descriptor_table_t*  descriptor;
  const __cxa_contract_data_block* next;  // linked list, first-found-wins
  // field data follows at byte offsets from this struct
};
```

**Field IDs** (representative): `CXA_FIELD_SOURCE_LOCATION=0x01`, `COMMENT=0x02`, `MESSAGE=0x03`, `LOCAL_HANDLER=0x04`, `QUERY_FUNCTION=0x05`, `LABEL_PTR=0x06`, `ASSERTION_KIND=0x07`, `EVALUATION_SEMANTIC=0x08`, `DETECTION_MODE=0x09`, `EXCEPTION_PTR=0x0A`, `EXTENDED=0x40`.

> **D4324 mapping:** Deletes the entire descriptor-chain ABI. The compiler passes `(comment, source_location, evaluation_config)` directly to `control_object`; no wire structs, no chain walking, no `CXA_*` wire enums.

### 4.3 Dispatch core (`libcontracts`)

**Chain walking** (`dispatch.c`):

```c
const void *
__cxa_find_field (const __cxa_contract_data_block *chain,
                  const uint8_t *ids, uint8_t num_ids)
{
  const __cxa_contract_data_block *block;
  for (block = chain; block; block = block->next) {
    const __cxa_descriptor_table_t *desc = block->descriptor;
    const union __cxa_descriptor_data_t *fdata;
    uint8_t i, j;
    if (!desc) continue;
    fdata = desc_data (desc);
    for (i = 0; i < desc->num_entries; ++i)
      for (j = 0; j < num_ids; ++j)
        if (desc->field_ids[i] == ids[j]) {
          if (desc->field_ids[i] >= CXA_FIELD_EXTENDED)
            return fdata[i].pointer;
          return (const char *) block + fdata[i].offset;
        }
  }
  return NULL;
}
```

**Core dispatch** (full):

```c
void
__contract_dispatch_core (const __cxa_contract_data_block *chain,
                          uint8_t semantic)
{
  struct { const void *chain; } cv;
  const void *hp;
  const void *lpp;
  __cxa_local_handler_fn_t handler_fn = NULL;
  const void *label_ptr = NULL;
  int handled = 0;

  cv.chain = chain;

  /* Local (per-assertion) handler dispatch. */
  hp = find_field1 (chain, CXA_FIELD_LOCAL_HANDLER);
  if (hp)
    handler_fn = *(__cxa_local_handler_fn_t const *) hp;
  lpp = find_field1 (chain, CXA_FIELD_LABEL_PTR);
  if (lpp)
    label_ptr = *(const void *const *) lpp;

  if (handler_fn && handler_fn (label_ptr, &cv) == 1)
    handled = 1;

  /* Global handler: user replacement, else default, else minimal fallback. */
  if (!handled) {
    if (__handle_contract_violation)
      __handle_contract_violation (&cv);
    else if (__contract_invoke_default_handler)
      __contract_invoke_default_handler (&cv);
    else
      __contract_minimal_default (chain);
  }

  /* Post-handler: enforce AND noexcept_enforce abort on normal return. */
  if (semantic == CXA_ES_ENFORCE || semantic == CXA_ES_NOEXCEPT_ENFORCE)
    abort ();
}
```

Specialized entry points prepend an override block via `__dispatch_with_override_core(data, kind, semantic, mode)` before calling the core. Universal entry `__cxa_contract_violation(void* data)` reads semantic from the chain.

**Representative accessors** (`accessors.c`):

```c
const char *
stdc_contract_violation_comment (const void *cv)
{
  const void *p = find1 (cv_chain (cv), CXA_FIELD_COMMENT);
  return p ? *(const char *const *) p : "";
}

int
stdc_contract_violation_is_terminating (const void *cv)
{
  uint8_t s = cv_u8 (cv_chain (cv), CXA_FIELD_EVALUATION_SEMANTIC,
                     CXA_ES_UNSPECIFIED);
  return (s == CXA_ES_ENFORCE || s == CXA_ES_QUICK_ENFORCE) ? 1 : 0;
}
```

**C API purpose** (`c_api.c`): P3290 C functions (`stdc_handle_*_contract_violation_explicit`) and D4299 compiler-emitted helpers (`__c_contract_check_enforce/observe`) build a stack `c_p3290_data_block_t` with a fixed 5-field descriptor and call `__cxa_contract_violation`.

> **D4324 mapping:** Deletes `libcontracts` entirely. Dispatch becomes: compiler calls `control_object` -> library builds `contract_violation` -> `default_control` switches on `evaluation_config` -> calls `invoke_default_contract_violation_handler` or user `handle_contract_violation`. Termination policy lives in the control object, not a separate post-dispatch `abort()`.

### 4.4 `_noexcept` wrapper matrix (`contracts_abi.cc`)

Pure-C dispatch lives in `libcontracts`; C++ provides terminate-on-throw barriers for noexcept sites.

**Universal wrapper:**

```cpp
extern "C" void
__cxa_contract_violation_noexcept (void* __data) noexcept
{
  auto* __chain = static_cast<const __cxa_contract_data_block*>(__data);
  __UINT8_TYPE__ __sem = __cxa_find_field_value<__UINT8_TYPE__>(
      __chain, CXA_FIELD_EVALUATION_SEMANTIC, CXA_ES_UNSPECIFIED);
  try { __contract_dispatch_core(__chain, __sem); }
  catch (...) { std::terminate(); }
}
```

**Macro-generated specialized wrappers** (observe example; enforce/noexcept_observe/noexcept_enforce follow same pattern):

```cpp
#define CXA_OBSERVE_NX(kind_name, kind_val, mode_name, mode_val)      \
extern "C" void                                                       \
__cxa_contract_violation_##kind_name##_observe_##mode_name##_noexcept \
    (void* __data) noexcept                                           \
{                                                                     \
  try { __dispatch_with_override_core(                                \
            __data, kind_val, CXA_ES_OBSERVE, mode_val); }            \
  catch (...) { std::terminate(); }                                   \
}

CXA_OBSERVE_NX(pre,    CXA_AK_PRE,    pf, CXA_DM_PREDICATE_FALSE)
CXA_OBSERVE_NX(pre,    CXA_AK_PRE,    ex, CXA_DM_EVALUATION_EXCEPTION)
/* ... post, assert, post_capture x pf/ex ... */

#define CXA_ENFORCE_NX(kind_name, kind_val, mode_name, mode_val)      \
extern "C" [[noreturn]] void                                          \
__cxa_contract_violation_##kind_name##_enforce_##mode_name##_noexcept \
    (void* __data) noexcept                                           \
{                                                                     \
  try { __dispatch_with_override_core(                                \
            __data, kind_val, CXA_ES_ENFORCE, mode_val); }            \
  catch (...) { std::terminate(); }                                   \
  __builtin_unreachable();                                            \
}
/* CXA_NOEXCEPT_OBSERVE / CXA_NOEXCEPT_ENFORCE macros similarly */
```

The non-noexcept counterparts are generated in `dispatch.c` via `CXA_OBSERVE` / `CXA_ENFORCE` macros. `<bits/contracts_abi.h>` declares the full matrix: 4 kinds x 4 semantics x 2 detection modes x 2 noexcept variants = dozens of symbols.

> **D4324 mapping:** Deletes the entire entry-point matrix and all `_noexcept` wrappers. Exception boundaries are handled by the control object's return type (`violation_response`) and `is_nonthrowing(cfg)` at compile time.

### 4.5 Default handler, aliases, P3290/P4298 (`contract26.cc`)

**Weak replaceable handler:**

```cpp
static void __handle_contract_violation_default(
    const std::contracts::contract_violation &violation) noexcept
{ /* verbose stderr diagnostic when _GLIBCXX_VERBOSE */ }

__attribute__ ((weak)) void
handle_contract_violation (const std::contracts::contract_violation &violation)
{
  return __handle_contract_violation_default(violation);
}

extern "C" __attribute__ ((weak)) void
__handle_contract_violation (const std::contracts::contract_violation &violation)
{
  return __handle_contract_violation_default(violation);
}

extern "C" void
__contract_invoke_default_handler
    (const std::contracts::contract_violation &violation)
{
  std::contracts::invoke_default_contract_violation_handler(violation);
}
```

**P3290 manual violations** build a fixed descriptor-chain block and call `__cxa_contract_violation`:

```cpp
static void __do_handle_violation(const char* __comment,
                                  const std::source_location& __location,
                                  __UINT8_TYPE__ __semantic)
{
  __p3290_data_block_t __data = {
    reinterpret_cast<const __cxa_descriptor_table_t*>(&__p3290_desc),
    nullptr,
    { __location.file_name(), __location.function_name(),
      __location.line(), __location.column() },
    __comment ? __comment : "",
    CXA_AK_MANUAL, __semantic, CXA_DM_UNSPECIFIED,
  };
  __cxa_contract_violation(
      const_cast<void*>(static_cast<const void*>(&__data)));
}
```

**P4298 nothrow_t overloads** report `CXA_ES_NOEXCEPT_ENFORCE` / `CXA_ES_NOEXCEPT_OBSERVE` in inline namespace `__p4298`.

**`__cxa_handle_cassert_violation`** (P3290 assert integration):

```cpp
extern "C++" [[noreturn]] void
__cxa_handle_cassert_violation(const char* __comment,
                               std::source_location __location) noexcept
{
  __p3290_data_block_t __data = { /* ... CXA_AK_CASSERT, CXA_ES_ENFORCE,
                                     CXA_DM_PREDICATE_FALSE ... */ };
  try {
    __cxa_contract_violation(
        const_cast<void*>(static_cast<const void*>(&__data)));
  } catch (...) { std::abort(); }
  std::abort();
}
```

> **D4324 mapping:** Keeps `handle_contract_violation`, `invoke_default_contract_violation_handler`, and P3290 manual-violation entry points (adapted to call `control_object` instead of `__cxa_contract_violation`). Keeps assert integration conceptually. Deletes `__handle_contract_violation` C alias, `__contract_invoke_default_handler`, and descriptor-backed P3290 data blocks.

### 4.6 C header `<contracts.h>`

```c
typedef struct contract_violation_t contract_violation_t;

/* Accessors */
const char* stdc_contract_violation_comment(const contract_violation_t*);
const char* stdc_contract_violation_file(const contract_violation_t*);
/* ... function, line, column, kind, semantic, detection_mode, is_terminating */

#define STDC_CONTRACT_PRE     0x01
#define STDC_CONTRACT_ENFORCE 0x03
/* ... */

void stdc_handle_enforced_contract_violation_explicit(
    const char* __comment, const char* __file,
    const char* __func, unsigned __line) __attribute__((__noreturn__));
/* observed, quick_enforced + convenience macros */

/* <stdcontracts.h> adds C keyword macros: pre -> _Pre, etc. */
```

Purpose: opaque C-facing view of the descriptor-chain violation object plus P3290 explicit-location manual API. Implementation is entirely in `libcontracts`.

> **D4324 mapping:** Deletes C `<contracts.h>` / `libcontracts` C accessors. C contract checks would call the same library control path as C++ (or remain unsupported).

### 4.7 D4324 control objects (replacement surface)

What D4324 adds in place of the P3850 machinery (illustrative; final spelling per the paper's Appendix A):

```cpp
enum class evaluation_config : unsigned {
  ignore = 0, observe = 1, enforce = 2, quick_enforce = 3
};

enum class violation_response { proceed, terminate };

template <class T>
concept assertion_control =
  std::is_empty_v<T> &&
  requires (T c, const char* comment, std::source_location loc,
            evaluation_config cfg) {
    { T::is_ignored(cfg)   } -> std::same_as<bool>;
    { T::constify          } -> std::convertible_to<bool>;
    { T::assumable         } -> std::convertible_to<bool>;
    { c(comment, loc, cfg) } -> std::same_as<violation_response>;
  };

struct default_control {
  static constexpr bool is_ignored(evaluation_config cfg)
  { return cfg == evaluation_config::ignore; }
  static constexpr bool constify  = false;
  static constexpr bool assumable = false;
  violation_response operator()(const char* comment, std::source_location loc,
                                evaluation_config cfg) const;
};
inline constexpr default_control default_v{};
```

The compiler emits a direct call to the selected control object. `default_control` switches on `cfg` and calls `invoke_default_contract_violation_handler` with a populated `contract_violation`.

### 4.8 Library/runtime: D4324 keep-vs-drop table

| Component | P3850 (branch) | D4324 |
|---|---|---|
| `contract_violation` object | Chain pointer + `__cxa_find_field` accessors | Flat/populated object (P2900-style) |
| `handle_contract_violation` / default handler | Yes (weak, C + C++ aliases) | **Keep** |
| `invoke_default_contract_violation_handler` | Yes | **Keep** |
| Descriptor-chain ABI (`__cxa_contract_data_block`, wire enums) | Yes | **Delete** |
| `libcontracts` (pure C runtime) | Yes (`dispatch.c`, `accessors.c`, `c_api.c`) | **Delete** |
| `__cxa_contract_violation_*` entry-point matrix | Yes (kind x semantic x mode x noexcept) | **Delete** |
| `_noexcept` C++ wrappers (`contracts_abi.cc`) | Yes (try/catch -> terminate) | **Delete** |
| P3400 labels namespace | Yes (`review_t`, combined labels, concepts) | **Delete** (replaced by 3 control objects) |
| P3290 manual-violation API | Yes (via descriptor chain + ABI entry) | **Keep** (adapted call path) |
| P4298 `noexcept_observe/enforce` wire semantics | Yes (`CXA_ES_0x06/0x07`) | **Delete** (compile-time `is_nonthrowing`) |
| C `<contracts.h>` / `stdc_*` accessors | Yes | **Delete** |
| `assertion_control` / `control_object` dispatch | No | **Add** |
| `evaluation_config` enum | No (uses `evaluation_semantic` + wire `CXA_ES_*`) | **Add** (ignore/observe/enforce/quick_enforce) |
| Local label handlers (`CXA_FIELD_LOCAL_HANDLER`) | Yes | **Delete** |
| `__contract_invoke_default_handler` C fallback | Yes | **Delete** |

---

## 5. Flags, build integration, feature-test macros, and tests

Reference extraction from GCC branch `berne/contracts-p3850`, focused on P4298 (`noexcept_enforce` / `noexcept_observe`) as the template for adding a new evaluation semantic in the P3850 architecture.

> **D4324 mapping:** D4324 wants a minimal surface: keep `-fcontracts` (master already has it), optionally one flag for the TU-default checked semantic (`-fcontract-evaluation-semantic=`, already present), and a feature-test macro. Drop the P3850 per-paper flag zoo (`-fcontracts-p3850`, `-fcontracts-p3097`, ...), P3595 JSON config (`-fcontract-configuration*`, `-fcontracts-group-evaluation-semantic=`), and `-fcontracts-allow-assume`. Semantics are selected by control-object **types in source**, not flags or JSON. D4324 needs **no** `-lcontracts` / `-lstdc++exp` link (handler dispatch stays in-header or inline). Tests become mostly `dg-do compile` with `scan-tree-dump` / godbolt-style scans, plus a few `dg-do run` for handler behavior.

---

### 5.1 New and modified `-fcontract*` options (`c.opt`)

#### Warning

```text
Wcontract-configuration
C ObjC C++ ObjC++ Var(warn_contract_configuration) Init(1) Warning
Warn about problems in the contract configuration (P3595).
```

#### Modified base flag

```text
fcontracts
C++ ObjC++ Var(flag_contracts) Init(0) LangEnabledBy(C++ ObjC++,fcontracts-p3097 || fcontracts-p3098 || fcontracts-p3099 || fcontracts-p3100 || fcontracts-p3290 || fcontracts-p3400 || fcontracts-p3850 || fcontracts-p4283 || fcontracts-p4298)
Enable features proposed for C++26 Contracts.
```

Each per-paper sub-flag below **implies** `-fcontracts` via `LangEnabledBy`. `-fcontracts-p3850` implies all of its children.

#### Extended evaluation semantic enum and flag

```text
EnumValue
Enum(contract_semantic) String(assume) Value(5)

EnumValue
Enum(contract_semantic) String(noexcept_observe) Value(6)

EnumValue
Enum(contract_semantic) String(noexcept_enforce) Value(7)

fcontract-evaluation-semantic=
C C++ ObjC ObjC++ Joined RejectNegative Enum(contract_semantic) Var(flag_contract_evaluation_semantic) Init(3)
-fcontract-evaluation-semantic=[ignore|observe|enforce|quick_enforce|assume|noexcept_enforce|noexcept_observe]	Select the contract evaluation semantic (defaults to enforce).
```

| Variable | Default | Notes |
|---|---|---|
| `flag_contract_evaluation_semantic` | `3` (`enforce`) | Was C++-only; now also C/ObjC |

Also widened to C/ObjC (no new variables): `fcontracts-client-check=` (`flag_contract_client_check`, default `0`), `fcontracts-definition-check=` (`flag_contracts_definition_check`, default `1`).

#### P3595 configuration flags (no `Var()` - handled in `c-opts.cc`)

```text
fcontracts-group-evaluation-semantic=
C C++ ObjC ObjC++ Joined RejectNegative
-fcontracts-group-evaluation-semantic=<group>:<semantic>	Set the evaluation semantic for contracts in a specific group.

fcontract-configuration=
C C++ ObjC ObjC++ Joined RejectNegative
-fcontract-configuration=<json>	Provide inline JSON contract configuration.

fcontract-configuration-file=
C C++ ObjC ObjC++ Joined RejectNegative
-fcontract-configuration-file=<path>	Load contract configuration from a JSON file.
```

#### Umbrella and per-paper enable flags

| Flag | Variable | Default | Implied by | Help text |
|---|---|---|---|---|
| `-fcontracts-p3850` | `flag_contracts_p3850` | `0` | - | Enable all P3850 contract extensions for C++29. |
| `-fcontracts-p3099` | `flag_contracts_p3099` | `0` | `-fcontracts-p3850` | Enable P3099 user-defined diagnostic messages for contract assertions. |
| `-fcontracts-p3290` | `flag_contracts_p3290` | `0` | `-fcontracts-p3850` | Enable P3290 contract violation trigger API and assert integration. |
| `-fcontracts-p3098` | `flag_contracts_p3098` | `0` | `-fcontracts-p3850` | Enable P3098 postcondition captures on contract assertions. |
| `-fcontracts-p3400` | `flag_contracts_p3400` | `0` | `-fcontracts-p3850` | Enable P3400 assertion-control labels on contract assertions. |
| `-fcontracts-p3097` | `flag_contracts_p3097` | `0` | `-fcontracts-p3850` | Enable P3097 contract assertions on virtual functions. |
| `-fcontracts-p4283` | `flag_contracts_p4283` | `0` | `-fcontracts-p3850` | Enable P4283 requires clauses on contract assertions. |
| `-fcontracts-p3100` | `flag_contracts_p3100` | `0` | `-fcontracts-p3850` | Enable P3100 implicit contract assertions and the assume evaluation semantic. |
| `-fcontracts-p4298` | `flag_contracts_p4298` | `0` | - | Enable P4298 `noexcept_enforce` and `noexcept_observe` evaluation semantics. |
| `-fcontracts-p4299` | `flag_contracts_p4299` | `0` | - | Enable C contracts support (D4299). C/ObjC only. |
| `-fcontracts-allow-assume` | `flag_contracts_allow_assume` | `0` | - | Allow the assume contract evaluation semantic to be used; a resolved assume is otherwise treated as ignore. |

> **D4324 mapping:** Keep `-fcontracts` and `-fcontract-evaluation-semantic=` (for TU build-mode default only). Drop everything else in this table except possibly reusing the `-fcontract-evaluation-semantic=` enum extension pattern for any implementation-defined default-checked semantic D4324 adds at the TU level.

---

### 5.2 Flag processing (`c-opts.cc`)

#### Config source accumulation (command-line order)

```diff
+vec<contract_config_source> contracts_config_sources;
+
+static bool contract_evaluation_semantic_explicit = false;
```

Handlers push into `contracts_config_sources`:

```diff
+    case OPT_fcontracts_group_evaluation_semantic_:
+      {
+	contract_config_source src;
+	src.kind = CCSK_GROUP_SEMANTIC;
+	src.arg = xstrdup (arg);
+	contracts_config_sources.safe_push (src);
+      }
+      break;
+
+    case OPT_fcontract_configuration_:
+      {
+	contract_config_source src;
+	src.kind = CCSK_JSON_INLINE;
+	src.arg = xstrdup (arg);
+	contracts_config_sources.safe_push (src);
+      }
+      break;
+
+    case OPT_fcontract_configuration_file_:
+      {
+	contract_config_source src;
+	src.kind = CCSK_JSON_FILE;
+	src.arg = xstrdup (arg);
+	contracts_config_sources.safe_push (src);
+      }
+      break;
+
+    case OPT_fcontract_evaluation_semantic_:
+      if (contract_evaluation_semantic_explicit)
+	error ("%<-fcontract-evaluation-semantic=%> may only be "
+	       "specified once");
+      contract_evaluation_semantic_explicit = true;
+      break;
```

#### C++26 enables contracts without clobbering per-paper flags

```diff
-  /* Contracts are in C++26.  */
-  SET_OPTION_IF_UNSET (&global_options, &global_options_set,
-		       flag_contracts, cxx_dialect >= cxx26);
+  /* Contracts are in C++26, so C++26 mode enables -fcontracts.  Only ever turn
+     it on here -- never force it off -- so the LangEnabledBy implications from
+     the per-paper sub-flags (each -fcontracts-pNNNN implies -fcontracts, and
+     -fcontracts-p3850 implies the individual paper flags; see c.opt) are
+     preserved in pre-C++26 dialects.  ...  An explicit -fno-contracts still wins ... */
+  if (cxx_dialect >= cxx26)
+    SET_OPTION_IF_UNSET (&global_options, &global_options_set,
+			 flag_contracts, true);
```

Implication chain: `-fcontracts-p3850` -> all P3850 children -> each child `LangEnabledBy` -> `-fcontracts`. `-fcontracts-p4298` is **outside** the umbrella (standalone gate for noexcept semantics). `-std=c++26` turns on `-fcontracts` but does **not** auto-enable `-fcontracts-p4298`.

#### Eager config parse

```diff
+  if ((flag_contracts || flag_contracts_p4299)
+      && !contracts_config_sources.is_empty ())
+    contract_config_init ();
```

> **D4324 mapping:** Drop config-source accumulation and `contract_config_init()`. Keep the C++26 `SET_OPTION_IF_UNSET(flag_contracts, true)` pattern and the "explicit `-fno-contracts` wins" behavior. D4324's TU default semantic comes from `-fcontract-evaluation-semantic=` (build-mode `cfg`), not JSON.

---

### 5.3 Feature-test macros (`c-cppbuiltin.cc`)

```diff
-      if (flag_contracts)
+      if (flag_contracts && flag_contracts_p3097)
+	cpp_define (pfile, "__cpp_contracts=202606L");
+      else if (flag_contracts)
 	cpp_define (pfile, "__cpp_contracts=202502L");
       else if (cxx_dialect >= cxx26)
 	cpp_warn (pfile, "__cpp_contracts");
+      if (flag_contracts_p3099)
+	cpp_define (pfile, "__cpp_contracts_message=202606L");
+      if (flag_contracts_p3290)
+	cpp_define (pfile, "__gcc_contracts_p3290=202606L");
+      if (flag_contracts_p3400)
+	cpp_define (pfile, "__cpp_contracts_labels=202606L");
+      if (flag_contracts_p3098)
+	cpp_define (pfile, "__cpp_contracts_postcondition_captures=202606L");
+      if (flag_contracts_p4283)
+	cpp_define (pfile, "__cpp_contracts_requires=202606L");
+      if (flag_contracts_p3100)
+	cpp_define (pfile, "__gcc_contracts_p3100=202606L");
+      if (flag_contracts_p4298)
+	cpp_define (pfile, "__cpp_contracts_nonthrowing_semantics=202607L");
```

| Macro | Guard | Value |
|---|---|---|
| `__cpp_contracts` | `flag_contracts && flag_contracts_p3097` | `202606L` |
| `__cpp_contracts` | `flag_contracts` (else) | `202502L` |
| `__cpp_contracts_message` | `flag_contracts_p3099` | `202606L` |
| `__gcc_contracts_p3290` | `flag_contracts_p3290` | `202606L` |
| `__cpp_contracts_labels` | `flag_contracts_p3400` | `202606L` |
| `__cpp_contracts_postcondition_captures` | `flag_contracts_p3098` | `202606L` |
| `__cpp_contracts_requires` | `flag_contracts_p4283` | `202606L` |
| `__gcc_contracts_p3100` | `flag_contracts_p3100` | `202606L` |
| `__cpp_contracts_nonthrowing_semantics` | `flag_contracts_p4298` | `202607L` |

> **D4324 mapping:** Define one macro under `-fcontracts`, e.g. `__cpp_contracts=202607L` (or an implementation-defined `__cpp_contracts_minimal=...`). Per-semantic and per-paper macros are unnecessary - control-object types in source are the capability gate. No `__cpp_contracts_nonthrowing_semantics` unless D4324 adopts P4298 semantics verbatim.

---

### 5.4 Link and build integration (`g++spec.cc` + build tree)

#### Driver link logic

Triggers `need_experimental` for any contracts use:

```diff
 	case OPT_fcontracts:
+	case OPT_fcontracts_p3097:
+	... (all C++ paper flags) ...
+	case OPT_fcontracts_p4298:
+	case OPT_std_c__26:
+	case OPT_std_gnu__26:
 	  need_experimental = true;
```

Appended link args (3 slots when `need_experimental`):

```diff
+	  generate_option (OPT_u, "__contract_invoke_default_handler", 1,
+			   CL_DRIVER, &new_decoded_options[j++]);
 	  generate_option (OPT_l, "stdc++exp", 1, CL_DRIVER,
 			   &new_decoded_options[j++]);
+	  generate_option (OPT_l, "contracts", 1, CL_DRIVER,
+			   &new_decoded_options[j++]);
```

`-u __contract_invoke_default_handler` forces the default C++ handler out of the static `libstdc++exp` archive (weak refs from `libcontracts` alone would not pull it). Order: `-u` then `-lstdc++exp` then `-lcontracts`. C-only `-fcontracts-p4299` is excluded here; it links `libcontracts` via a separate path in `gcc.cc`.

#### New top-level `libcontracts`

- New directory `libcontracts/` added to `target-libcontracts` in `configure.ac`.
- `libcontracts/Makefile.am`: installs `libcontracts.la` (`dispatch.c`, `accessors.c`, `c_api.c`); pure-C ABI core with version script `LIBCONTRACTS_1.0`.
- ABI enum includes P4298 values: `CXA_ES_NOEXCEPT_OBSERVE = 0x06`, `CXA_ES_NOEXCEPT_ENFORCE = 0x07`.

#### libstdc++ C++26 convenience library

`libstdc++-v3/src/c++26/Makefile.am`:

```makefile
sources = \
	  contract26.cc \
	  contracts_abi.cc
...
contract26.lo: contract26.cc
	$(LTCXXCOMPILE) -fcontracts -fcontracts-p3290 -c $<
contracts_abi.lo: contracts_abi.cc
	$(LTCXXCOMPILE) -fcontracts -fcontracts-p3290 -c $<
```

`contracts_abi.cc` emits all `__cxa_contract_violation_{kind}_{semantic}_{mode}[_noexcept]` entry points, including P4298 `noexcept_enforce` / `noexcept_observe` variants. `contract26.cc` provides handler dispatch including `std::contracts::__p4298` inline-namespace overloads selected when `__cpp_contracts_nonthrowing_semantics` is defined.

> **D4324 mapping:** No `-lcontracts`, no `-lstdc++exp`, no `-u __contract_invoke_default_handler`. D4324 handler dispatch should be header-only or statically linked from libstdc++ proper without a separate contracts runtime library. Build `contract26.cc`-equivalent sources only if D4324 needs shared runtime helpers at all.

---

### 5.5 Mangling (`mangle.cc` diff)

The `mangle.cc` diff on this branch is **not** contract-specific:

```diff
-	     && !cp_expr_location (probe)
+	     && !EXPR_LOCATION (probe)
...
-    <special-name> ::= TC <type> <offset number> _ <base type>  */
+    <special-name> ::= CT <type> <offset number> _ <base type>  */
```

Contract-related naming lives elsewhere:

1. **Runtime entry points** (compiler-generated, unmangled C symbols):
   `__cxa_contract_violation_{pre|post|assert|post_capture}_{enforce|observe|noexcept_enforce|noexcept_observe}_{pf|ex}[_noexcept]`

2. **P3595 dynamic selectors**: C++ linkage names mangle normally (`mylib::contract_semantic` -> `_ZN5mylib17contract_semanticEv`); C linkage uses verbatim assembler name.

3. **P3400 control-object types** (labels, combined controls): normal Itanium mangling of user-defined `struct` types passed to `operator()`.

4. **P4298 libstdc++ handler overloads**: separate mangled definitions in inline namespace `std::contracts::__p4298` vs plain `std::contracts` variants, selected at call site based on whether `__cpp_contracts_nonthrowing_semantics` was defined in the **caller's** TU.

> **D4324 mapping:** D4324 control-object types (`ignore`, `enforce`, `review`, ...) mangle as ordinary C++ types. No new Itanium production rules. If D4324 adds a user-defined `review` semantic, its dispatch function name is a design choice (mangled C++ `operator()` on the control type, not a new `__cxa_*` matrix).

---

### 5.6 Test harness (`contracts-dg.exp`)

`gcc/testsuite/lib/contracts-dg.exp` provides:

| Proc | Role |
|---|---|
| `contracts_link_flags` | Adds `-L.../libcontracts/.libs` to `TEST_ALWAYS_FLAGS` and extends `LD_LIBRARY_PATH`. Deliberately does **not** append `-lcontracts` (driver pulls it in). |
| `contracts_init` | Prepends link flags; gates the whole subdir via `check_no_compiler_messages_nocache` compiling a tiny C contract TU with `-fcontracts-p4299 -fcontract-evaluation-semantic=observe`. |
| `contracts_finish` | Restores `TEST_ALWAYS_FLAGS`. |

Common test patterns in the P4298 suite:

- `{ dg-do compile { target c++26 } }` for constexpr/flag-gating tests.
- `{ dg-do run { target c++26 } }` + `{ dg-skip-if ... { ! hostedlib } }` for handler/runtime behavior (needs hosted libstdc++).
- `{ dg-additional-options "-fcontracts -fcontracts-p4298 ..." }` for paper-specific semantics.
- `{ dg-additional-options "-fcontract-configuration-file=${srcdir}/.../foo.json" }` for P3595 sidecars.
- `{ dg-shouldfail }` + `{ dg-output "GOT_SIGABRT..." }` for abort-vs-hang discrimination.
- `{ dg-final { scan-tree-dump "..." "gimple" } }` for codegen inspection.

#### scan-tree-dump migration

Branch updates observe codegen test from legacy internal helper to public ABI entry:

```diff
-// { dg-final { scan-tree-dump "__builtin_observable.*__tu_has_violation.*__builtin_observable" "gimple" } }
+// { dg-final { scan-tree-dump "__builtin_observable.*__cxa_contract_violation.*__builtin_observable" "gimple" } }
```

Some P4298 noexcept tests also scan for `eh_must_not_throw` in GIMPLE.

> **D4324 mapping:** Harness simplifies to `-fcontracts` only, no `contracts_init` libcontracts gate, no JSON sidecars. Primary pattern: `dg-do compile` + `-fdump-tree-gimple` + `scan-tree-dump` for zero-cost ignore (no check emitted), noexcept preservation (no EH scaffolding), and control-object `operator()` call sites. Reserve `dg-do run` for handler-behavior tests that do not need a separate runtime library.

---

### 5.7 Representative test files

#### `p4298-flag.C` (flag gate - positive compile)

```cpp
// D4298: noexcept_enforce/noexcept_observe are only valid evaluation
// semantics when -fcontracts-p4298 is enabled.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontracts-p4298 -fcontract-evaluation-semantic=noexcept_enforce" }
int f(int x) pre(x > 0) { return x; }
int main() { return f(1); }
```

#### `p4298-noexcept-enforce-abort.C` (runtime abort discipline)

```cpp
// D4298: noexcept_enforce behaves just like enforce when the handler
// returns normally: the ABI must abort() promptly, not fall through to
// __builtin_unreachable() (undefined behavior) and not std::terminate().
// ...
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontracts-p4298 -fcontract-evaluation-semantic=noexcept_enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }
// { dg-shouldfail "noexcept_enforce aborts rather than hanging or falling through" }
// { dg-output "GOT_SIGABRT(\n|\r\n|\r)" }

#include <contracts>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

void handle_contract_violation(const std::contracts::contract_violation&)
{
  // Returns normally; the ABI must still abort() on completion.
}

extern "C" void on_sigabrt(int)
{
  std::fputs("GOT_SIGABRT\n", stderr);
  std::fflush(stderr);
  std::signal(SIGABRT, SIG_DFL);
  std::raise(SIGABRT);
}

extern "C" void on_alarm(int)
{
  _exit(3);
}

int f(int x) pre(x > 0) { return x; }

int main()
{
  std::signal(SIGABRT, on_sigabrt);
  std::signal(SIGALRM, on_alarm);
  alarm(5);
  f(-1);
  return 0;
}
```

#### `p4298-normal.C` + `p4298-normal.json` (JSON config sidecar)

```cpp
// D4298: normal (non-terminating) execution paths for noexcept_observe and
// noexcept_enforce.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontracts-p4298" }
// { dg-additional-options "-fcontract-configuration-file=${srcdir}/g++.dg/contracts/cpp26/p4298-normal.json" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

static int violations = 0;

void handle_contract_violation(const std::contracts::contract_violation&)
{
  ++violations;
}

// Contract on this line -> configured to noexcept_observe.
int observe_fn(int x) pre(x > 0) { return x; }

// Contract outside the listed line -> catch-all, configured to
// noexcept_enforce.
int enforce_fn(int x) pre(x > 0) { return x; }

int main()
{
  int r1 = observe_fn(-1);
  if (r1 != -1) __builtin_abort ();
  if (violations != 1) __builtin_abort ();

  int r2 = enforce_fn(1);
  if (r2 != 1) __builtin_abort ();
  if (violations != 1) __builtin_abort ();
}
```

```json
[
  {"match": {"location": "p4298-normal.C:20", "caller": false}, "output": {"semantic": "noexcept_observe"}},
  {"output": {"semantic": "noexcept_enforce"}}
]
```

#### `contracts-handler-alias.c` (C handler alias pattern)

```c
/* Test that defining handle_contract_violation makes it callable as handler.  */
/* { dg-do run } */
/* { dg-options "-fcontracts-p4299 -fcontract-evaluation-semantic=observe" } */
/* { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } } */

#include <contracts.h>

static int handler_called = 0;

void handle_contract_violation(const contract_violation_t* cv)
{
  handler_called++;
}

int guarded(int x) _Pre(x > 0)
{
  return x;
}

int main(void)
{
  guarded(-1);
  if (handler_called != 1)
    __builtin_abort();
  return 0;
}
```

---

### 5.8 Minimal semantic-extension touch-point list (P4298 roadmap)

Ordered checklist for adding a new evaluation semantic (e.g. `noexcept_enforce`) in the P3850 architecture:

1. **Enum values** - `gcc/c-family/contracts-config.h`: add `CES_NOEXCEPT_*` to `contract_evaluation_semantic`; extend `CES_ALL_ALLOWED_WITH_EXTENSIONS` bitmask. Mirror in `cp/contracts.h` if C++ frontend has a parallel enum.

2. **`c.opt` enum + flag help** - Add `EnumValue` for `contract_semantic`; extend `-fcontract-evaluation-semantic=` help string.

3. **Config string parse** - `gcc/c-family/contracts-config.cc` `parse_semantic_name()`: map `"noexcept_enforce"` / `"noexcept_observe"` to enum values (needed even if D4324 drops JSON - still used by the `-fcontract-evaluation-semantic=` flag).

4. **Per-paper flag gate** - `c.opt`: new `-fcontracts-p4298` -> `flag_contracts_p4298`. `contract_base_allowed_mask()` in `gcc/cp/contracts.cc`: OR in `(1<<CES_NOEXCEPT_ENFORCE)|(1<<CES_NOEXCEPT_OBSERVE)` when flag set.

5. **Semantic predicates** - `contracts.cc`:
   - `contract_semantic_is_nonthrowing()` - classify new semantic as nonthrowing.
   - `contract_semantic_emits_no_check()` - if zero-cost (not applicable for P4298).
   - Terminating-semantic predicate (enforce-like vs observe-like) for constexpr checking and noreturn entry points.

6. **`emit_check_for_semantic()`** - Add cases to the `switch (semantic)`: set `calls_handler`, `is_noexcept`, choose terminating vs returning path; call `declare_cxa_entry_point()` with new semantic string.

7. **Entry-point naming** - `get_cxa_entry_point_name()`: new `sem_str` cases produce `__cxa_contract_violation_{kind}_noexcept_enforce_{pf|ex}[_noexcept]`.

8. **Runtime / ABI** - `libcontracts/contracts-abi.h`: `CXA_ES_NOEXCEPT_*` wire values. `libstdc++-v3/src/c++26/contracts_abi.cc`: emit specialized entry points. `libstdc++-v3/src/c++26/contract26.cc`: handler overloads (e.g. `__p4298` namespace) reporting correct semantic to user handler. `libcontracts/dispatch.c`: terminating-path logic for new enum.

9. **Library surface** - `libstdc++-v3/include/std/contracts`: `evaluation_semantic` enum values; `is_nonthrowing()` update; any control-object / label facet bitmask updates (P3400).

10. **Feature-test macro + tests** - `c-cppbuiltin.cc`: `__cpp_contracts_nonthrowing_semantics=202607L` under `flag_contracts_p4298`. Tests: flag-gate compile, constexpr terminating, runtime abort/terminate/observe paths, JSON override, `is_nonthrowing()` (P3400 interaction), GIMPLE scans.

> **D4324 mapping:** Steps 4 and 10 shrink dramatically (no per-paper flag, no JSON tests). Step 8 may vanish entirely if dispatch is header-only. Steps 1-3, 5-7 remain, but resolution comes from **control-object type** (`review{}.operator()(cfg, ...)`) rather than flag/config. Replace step 7's `__cxa_*` matrix with a single mangled dispatch call or inlined check sequence per control type.

---

### 5.9 The four D4324 demo tests

Proposed minimal test suite for D4324's compiler surface. All use `{ dg-do compile { target c++26 } }` and `-fcontracts` only unless noted. (Predicate/control-naming syntax follows the D4324 paper; `pre<review>(cond)` shown here.)

#### 1. `d4324-ignore-zero-cost.C` - ignore emits nothing

```cpp
// D4324: ignore control object -> zero runtime check.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fdump-tree-gimple" }

#include <contracts>
int f(int x) pre<std::contracts::ignore>(x > 0) { return x; }
int main() { return f(1); }

// { dg-final { scan-tree-dump-not "__cxa_contract_violation" "gimple" } }
// { dg-final { scan-tree-dump-not "eh_" "gimple" } }
```

**Scan for:** absence of any violation entry-point call and absence of EH scaffolding.

#### 2. `d4324-noexcept-preserved.C` - checked semantic preserves noexcept

```cpp
// D4324: non-throwing checked semantic must not add EH tables to a noexcept function.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fdump-tree-gimple" }

#include <contracts>
int f(int x) noexcept pre(x > 0) { return x; }
int main() { return f(1); }

// { dg-final { scan-tree-dump "eh_must_not_throw" "gimple" } }
// { dg-final { scan-tree-dump-not "CATCH" "gimple" } }
```

**Scan for:** `eh_must_not_throw` on the function; no `CATCH`/`TRY` around the predicate when all contracts are statically nonthrowing.

#### 3. `d4324-review-user-semantic.C` - user-defined `review` via library type

```cpp
// D4324: review control object selects implementation-defined semantic at compile time.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fdump-tree-gimple" }

#include <contracts>  // provides struct review { ... };
int f(int x) pre<std::contracts::review>(x > 0) { return x; }
int main() { return f(1); }

// { dg-final { scan-tree-dump "review.*cl" "gimple" } }
```

**Scan for:** mangled `review::operator()` call in GIMPLE, proving control-object dispatch rather than a hard-coded semantic flag.

#### 4. `d4324-handler-no-ub.C` - handler return does not fall through to UB

```cpp
// D4324: terminating checked semantic must abort after handler returns, not UB/hang.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts" }
// { dg-shouldfail "terminating semantic aborts after handler return" }
// { dg-output "GOT_SIGABRT(\n|\r\n|\r)" }

#include <contracts>
#include <csignal>
#include <cstdio>
#include <cstdlib>

void handle_contract_violation(const std::contracts::contract_violation&) {}

extern "C" void on_sigabrt(int) {
  std::fputs("GOT_SIGABRT\n", stderr);
  std::fflush(stderr);
  std::signal(SIGABRT, SIG_DFL);
  std::raise(SIGABRT);
}

int f(int x) pre<std::contracts::enforce>(x > 0) { return x; }

int main() {
  std::signal(SIGABRT, on_sigabrt);
  f(-1);
  return 0;
}
```

**Scan for:** process abort (SIGABRT marker in `dg-output`); must not reach `main`'s `return 0`. No separate runtime library required if D4324 inlines abort after handler call in generated code.

---

## 6. P3400 control objects and P3595 config (the D4324-relevant seam)

On GCC branch `berne/contracts-p3850`, assertion-control labels (P3400) and configuration resolution (P3595) form a two-stage pipeline: **config picks a base evaluation semantic**, then **label facets transform and constrain it**. D4324 collapses both stages into a single compile-time control-object type named in source. This section documents the branch implementation so D4324 can reuse the resolution/codegen seam without reinventing it.

---

### 6.1 What a P3400 label/control object is (source-level shape)

A P3400 **assertion-control label** is a constexpr class object attached to a contract assertion via template syntax. The type must expose a nested `assertion_control_object` member type (the marker that satisfies the control-object concept). Labels are composed with `operator|` and looked up through `using contract_control namespace ...` or the header-provided `using contract_control namespace std::contracts::labels`.

**Basic attachment syntax** (`pre<label>(cond)`, `post<label>(r: cond)`, `contract_assert<label>(cond)`):

```cpp
struct empty_label_t {
  using assertion_control_object = empty_label_t;
};
constexpr empty_label_t empty_label;

void f(int x)
  pre<empty_label>(x > 0)
{
}

int g(int x)
  post<empty_label>(r: r >= 0)
{
  return x;
}

void h() {
  contract_assert<empty_label>(true);
}
```

**`using contract_control` and `contract_control(expr)`** enable unqualified label names and stored combined labels:

```cpp
namespace my_labels {
  struct my_label_t {
    using assertion_control_object = my_label_t;
  };
  constexpr my_label_t my_label{};
}
using contract_control namespace my_labels;

void f(int x)
  pre<my_label>(x > 0)   // unqualified lookup in assertion-control position
{
}

constexpr auto combined = contract_control(audit | user_review);
void g(int x)
  pre<combined>(x > 0)
{
}
```

After `#include <contracts>`, the header injects `using contract_control namespace std::contracts::labels`, so `empty_label`, `review`, etc. are visible unqualified.

> **D4324 mapping:** D4324 uses the same attachment surface (`pre<Control>(...)`) but names a **control-object type** with static members (`is_ignored`, `constify`, `assumable`) and an `operator()(comment, loc, cfg)` instead of a bag of optional facet methods. D4324 **drops** label composition (`operator|`), `contract_control` namespace injection, group literals (`"safety"group`), and the `assertion_control_object` nested-type marker in favor of a single `assertion_control` concept on the type itself.

---

### 6.2 Facet concepts and built-in labels from `<contracts>`

P3400 defines optional **facets** as C++20 concepts. Each facet-capable label exposes `using assertion_control_object = ...` and one or more constexpr methods the compiler probes at compile time.

**Core concepts and built-ins** (from branch `libstdc++` `<contracts>`):

```cpp
namespace std::contracts::labels {

  struct empty_label_t {
    using assertion_control_object = empty_label_t;
  };
  inline constexpr empty_label_t empty_label{};

  template<typename _Tp>
  concept semantic_computation_label =
    requires { typename _Tp::assertion_control_object; } &&
    requires(const _Tp __t, contracts::evaluation_semantic __s) {
      __t.compute_semantic(__s);
    };

  template<typename _Tp>
  concept allowed_semantics_label =
    requires { typename _Tp::assertion_control_object; } &&
    requires(const _Tp __t) {
      contracts::evaluation_semantic_set({__t.allowed_semantics});
    };

  template<typename _Tp>
  concept local_violation_label =
    requires { typename _Tp::assertion_control_object; } &&
    requires(_Tp __t, const contracts::contract_violation& __v) {
      __t.handle_contract_violation(__v);
    };

  template<typename _Tp>
  concept compute_comment_label =
    requires { typename _Tp::assertion_control_object; } &&
    requires(const _Tp __t, const char* __c) {
      __t.compute_comment(__c);
    };

  // ... compute_message_label, queryable_label, identification_label ...

  struct review_t {
    using assertion_control_object = review_t;
    constexpr contracts::evaluation_semantic
    compute_semantic(contracts::evaluation_semantic __in) const
    {
      if (__in == contracts::evaluation_semantic::enforce
          || __in == contracts::evaluation_semantic::quick_enforce)
        return contracts::evaluation_semantic::observe;
      return __in;
    }
  };
  inline constexpr review_t review{};

  // Combined labels via operator| - chains facets left-to-right
  template<typename _Lhs, typename _Rhs>
  struct __combined_label /* ... */ {
    constexpr contracts::evaluation_semantic
    compute_semantic(contracts::evaluation_semantic __s) const
      requires (semantic_computation_label<_Lhs> || semantic_computation_label<_Rhs>)
    {
      if constexpr (semantic_computation_label<_Lhs>)
        __s = _M_lhs.compute_semantic(__s);
      if constexpr (semantic_computation_label<_Rhs>)
        __s = _M_rhs.compute_semantic(__s);
      return __s;
    }
    // ... compute_comment, handle_contract_violation, query, group_names ...
  };
}

using contract_control namespace std::contracts::labels;
```

**Facet-to-D4324 alignment:**

| P3400 facet | Role on branch | D4324 analog |
|---|---|---|
| `compute_semantic(in) -> out` | Transforms config-resolved semantic | `is_ignored(cfg)` + semantic selection folded into one static query |
| `handle_contract_violation(v)` | Local violation handler; may return `handled` | `operator()(comment, loc, cfg) -> violation_response` |
| `compute_comment(c) -> c'` | Transforms diagnostic comment string | `comment` argument to `operator()` |
| `allowed_semantics` | Bitmask gate on which semantics are legal | Concept constraint / static validation on control type |
| `group_names` + config | Feeds P3595 group matching | **Dropped** - no external config |
| `query(key, index)` | Exposes data through violation object | **Dropped** in minimal D4324 |

> **D4324 mapping:** D4324's `review`-equivalent is a control type whose `is_ignored(observe_cfg)` is false and whose `operator()` observes rather than terminates - the branch `review` label achieves the same effect by remapping `enforce` to `observe` at semantic-resolution time, before codegen chooses observe-vs-enforce paths.

---

### 6.3 The `review` facet in tests (direct D4324 analog)

With `-fcontract-evaluation-semantic=enforce`, the library `review` label downgrades enforce to observe: handler runs, execution continues.

```cpp
#include <contracts>

static int handler_count = 0;

void handle_contract_violation(const std::contracts::contract_violation& v) {
  ++handler_count;
}

void f(int x)
  pre<review>(x > 0)   // enforce -> observe via compute_semantic
{
}

int main() {
  f(-1);  // handler called, continues (not terminate)
  if (handler_count != 1) __builtin_abort();
  f(-2);
  if (handler_count != 2) __builtin_abort();
}
```

**Combined labels** chain `compute_semantic` left-to-right; `review | empty_label` and `empty_label | review` both apply `review`'s transform:

```cpp
using std::contracts::labels::operator|;

void f(int x) pre<(review | empty_label)>(x > 0) {}
void g(int x) pre<(empty_label | review)>(x > 0) {}
void h(int x) pre<(review | review)>(x > 0) {}  // enforce -> observe -> observe
```

**Local handler facet** (`handle_contract_violation`) intercepts before the global handler; combined labels call LHS first, then RHS if LHS returned `not_handled`:

```cpp
struct lhs_handled_t {
  using assertion_control_object = lhs_handled_t;
  violation_handled handle_contract_violation(const contract_violation&) const {
    ++lhs_calls;
    return violation_handled::handled;
  }
};
constexpr lhs_handled_t lhs_handled{};

void case_both_handle(int x)
  pre<(lhs_handled | rhs_handled)>(x > 0) {}
```

**`allowed_semantics` facet** intersects with config-resolved semantic; fallback order applies when assume is disallowed:

```cpp
struct ig_ob_t {
  using assertion_control_object = ig_ob_t;
  static constexpr evaluation_semantic_set allowed_semantics =
    {evaluation_semantic::ignore, evaluation_semantic::observe};
};
constexpr ig_ob_t ig_ob{};

// Config = assume; label allows {ignore, observe} -> clamped to ignore
void f_ignore(int x) pre<ig_ob>(chk(x)) {}

// Config = assume; label allows {observe} -> clamped to observe
void f_observe(int x) pre<ob>(chk(x)) {}
```

> **D4324 mapping:** D4324 replaces `compute_semantic` + external config + `allowed_semantics` clamp with a single `T::is_ignored(cfg)` that encodes both "should this assertion emit code?" and which semantic path applies. D4324 replaces `handle_contract_violation` with `T::operator()(comment, loc, cfg)`. D4324 **drops** multi-label chaining, group identification, and query facets.

---

### 6.4 P3595 configuration resolution engine

Configuration is an **ordered list of rules** accumulated from CLI flags and JSON files. First matching rule wins. Resolution is frontend-agnostic; C++ fills namespace from `fndecl`.

**Source kinds** (`contracts-config-source.h`):

```cpp
enum contract_config_source_kind {
  CCSK_GROUP_SEMANTIC,   // -fcontracts-group-evaluation-semantic=group:semantic
  CCSK_JSON_INLINE,      // inline JSON on command line
  CCSK_JSON_FILE,        // -fcontract-configuration-file=path
};

extern vec<contract_config_source> contracts_config_sources;
```

**Per-assertion query** (`contracts-config-query.h`):

```cpp
struct contract_query {
  tree fndecl;
  int kind;                        // CAK_PRE, CAK_POST, CAK_ASSERT, ...
  bool caller_side;
  bool in_constant_evaluation;
  tree caller_fndecl;
  unsigned short allowed_mask;     // from label's allowed_semantics facet
  vec<const char *> *groups;       // from label's group_names facet
  location_t loc;
  location_t caller_loc;

  const char *get_ns () const;
  const char *get_location_file () const;
  int get_location_line () const;
  // ... caller location/namespace accessors ...
};
```

**Config entry** (private to `contracts-config.cc`):

```cpp
struct contract_config_entry {
  int kind;                        // -1 = wildcard
  int caller_side;                 // -1 = callee only; 0/1 = explicit
  int constexpr_eval;              // -1 = wildcard
  const char *group;
  const char *ns;
  const char *location_file;
  vec<contract_line_range> *location_lines;
  const char *caller_ns;
  const char *caller_location_file;
  vec<contract_line_range> *caller_location_lines;
  contract_evaluation_semantic semantic;
  char *dyn_name;                  // P3595 dynamic selector; NULL = static
  unsigned char dyn_linkage;       // CDL_CXX or CDL_C
  bool dyn_provideweak;
  bool has_semantic;
};
```

**Resolution result**:

```cpp
struct contract_config_result {
  contract_evaluation_semantic semantic;  // clamped default / weak fallback
  const char *dyn_name;                    // NULL => not dynamic
  unsigned char dyn_linkage;
  bool dyn_provideweak;
};
```

**Semantics enum** (matches `-fcontract-evaluation-semantic=` and libstdc++):

```cpp
enum contract_evaluation_semantic : unsigned short {
  CES_INVALID = 0,
  CES_IGNORE = 1,
  CES_OBSERVE = 2,
  CES_ENFORCE = 3,
  CES_QUICK = 4,
  CES_ASSUME = 5,
  CES_NOEXCEPT_OBSERVE = 6,
  CES_NOEXCEPT_ENFORCE = 7,
};

#define CES_ALL_ALLOWED \
  ((1 << CES_IGNORE) | (1 << CES_OBSERVE) \
   | (1 << CES_ENFORCE) | (1 << CES_QUICK))

#define CES_ALL_ALLOWED_WITH_ASSUME (CES_ALL_ALLOWED | (1 << CES_ASSUME))
```

**`contract_config_init`** builds the ordered rule list:

1. User sources from `contracts_config_sources` (group flags, inline JSON, JSON files) in command-line order
2. Implicit rules from `-fcontract-client-check` / `-fcontract-definition-check`
3. Final catch-all: `-fcontract-evaluation-semantic=` default

**`contract_config_resolve`** (near-full):

```cpp
static contract_evaluation_semantic fallback_order[] =
  { CES_OBSERVE, CES_ENFORCE, CES_QUICK, CES_IGNORE };

static contract_evaluation_semantic
clamp_semantic_to_allowed (contract_evaluation_semantic candidate,
                           const contract_query *query)
{
  if (candidate == CES_ASSUME
      && !(query->allowed_mask & (1 << CES_ASSUME))
      && (query->allowed_mask & (1 << CES_IGNORE)))
    return CES_IGNORE;

  if (query->allowed_mask & (1 << candidate))
    return candidate;

  for (auto sem : fallback_order)
    if (query->allowed_mask & (1 << sem))
      return sem;

  return CES_INVALID;
}

contract_config_result
contract_config_resolve (const contract_query *query)
{
  if (!global_config_initialized)
    contract_config_init ();

  contract_config_result r = { CES_INVALID, NULL, CDL_CXX, false };

  contract_config_entry *entry = NULL;
  FOR_EACH_VEC_ELT (global_config.entries, i, e)
    {
      if (!config_entry_matches (e, query))
        continue;

      /* In constexpr eval, dynamic-without-semantic is non-matching */
      if (query->in_constant_evaluation && e->dyn_name && !e->has_semantic)
        continue;

      entry = e;
      break;
    }

  if (!entry)
    {
      r.semantic = query->caller_side ? CES_IGNORE : CES_INVALID;
      return r;
    }

  r.semantic = clamp_semantic_to_allowed (entry->semantic, query);

  if (!query->in_constant_evaluation && entry->dyn_name)
    {
      r.dyn_name = entry->dyn_name;
      r.dyn_linkage = entry->dyn_linkage;
      r.dyn_provideweak = entry->dyn_provideweak;
    }

  return r;
}
```

**Matching criteria** (`config_entry_matches`): kind, caller-side flag, constexpr-eval flag, group prefix ( `"safety"` matches `"safety.extra"`), namespace prefix, filename suffix + optional line ranges, and caller namespace/location when `caller_side`.

**Example JSON** (dynamic selector with compile-time default):

```json
[
  {"output": {"semantic": "ignore",
              "dynamic": {"linkage": "C++", "name": "p3595_sel_compute"}}}
]
```

**Group config test** (`-fcontracts-group-evaluation-semantic=safety:observe`):

```cpp
void f_safety(int x) pre<"safety"group>(x > 0) { }  // observe via group rule
void f_other(int x) pre<"other"group>(x > 0) { }    // default enforce
void f_plain(int x) pre(x > 0) { }                   // default enforce
```

> **D4324 mapping:** D4324 **drops the entire config engine** (JSON, CLI rule lists, group/namespace/location matching, caller-side rules, dynamic selectors). D4324's `evaluation_config cfg` is a single TU-wide compile-time constant; the **type named in source is the control object**. The branch's `allowed_mask` / clamp / fallback-order logic is the closest existing analog for validating that a label's allowed semantics intersect the resolved semantic - D4324 folds this into concept satisfaction and `is_ignored(cfg)` at compile time.

---

### 6.5 How resolution + label facets combine (`ensure_evaluation_semantic`)

The C++ front end lazily resolves semantics in **`ensure_evaluation_semantic(contract, fndecl, in_ce)`**. Order:

1. **Build query** from AST (`make_contract_query`): kind from tree code, `allowed_mask` from label's `allowed_semantics` facet intersected with flag-gated base set (`-fcontracts-allow-assume`, `-fcontracts-p4298`)
2. **Fill groups** from label's `group_names` static member (if `identification_label`)
3. **`contract_config_resolve(&q)`** - first-match config rule, clamp to `allowed_mask`
4. **`apply_compute_semantic(label, sem, allowed_mask)`** - constexpr-call label's `compute_semantic` if present; error if result outside allowed set
5. **Cache** in `CONTRACT_EVALUATION_SEMANTIC` (runtime) or `CONTRACT_CONSTEXPR_EVALUATION_SEMANTIC` (constexpr); store dynamic descriptor in `CONTRACT_DYNAMIC` when `dyn_name != NULL`

(Function body reproduced in Section 2.4.)

**At label attach time** (`grok_contract`), the front end:

- Validates nested `assertion_control_object`
- Probes `allowed_semantics.contains(sem)` for each semantic, stores narrowed mask in `CONTRACT_ALLOWED_MASK`
- Applies `compute_comment` / `compute_message` facets to diagnostic strings via constexpr evaluation
- Materializes label as `static` variable when local handler or query trampoline needs its address

**Caller-side resolution** (`resolve_caller_semantic`) repeats config resolve with `caller_side = true`, adds `CES_IGNORE` to allowed mask, and applies `compute_semantic` only when the resolved semantic actually emits a check (preserves caller-side opt-in).

> **D4324 mapping:** D4324's three-step algorithm (`is_ignored` -> constify/assume -> `operator()`) replaces this entire two-stage pipeline with one compile-time query on the named control type. Reuse opportunity: the **codegen dispatch on resolved semantic** (ignore / observe / enforce / quick / assume) and the **comment/location plumbing into the violation path** - not the config matcher itself.

---

### 6.6 P3595 dynamic selectors and the if-cascade codegen shape

When config resolves to a **dynamic** entry, runtime semantic comes from a selector function; the compile-time `semantic` field is the weak-definition default and the constexpr-eval value.

**Test wiring**:

```cpp
struct to_observe_t {
  using assertion_control_object = to_observe_t;
  constexpr evaluation_semantic compute_semantic(evaluation_semantic) const
  { return evaluation_semantic::observe; }
};
constexpr to_observe_t to_observe{};

evaluation_semantic p3595_sel_compute()
{ return evaluation_semantic::enforce; }

void f(int x) pre<to_observe>(x > 0) { }
// Runtime: selector returns enforce -> compute_semantic -> observe -> handler+continue
```

**Codegen** (`build_contract_check` during genericization): call selector `raw = selector()`; if the label transforms (has `compute_semantic` or a narrowed `allowed_semantics`), emit a two-stage if-cascade (stage 1 maps `raw` to `eff = T(raw)`, stage 2 dispatches on `eff`); otherwise a single-stage if-cascade on `raw`. Full body in Section 2.2.

> **D4324 mapping:** D4324 **drops dynamic selectors entirely**. Semantic is fixed at compile time from `T::is_ignored(cfg)`. The if-cascade exists only as the branch's P3595+P3400 interaction artifact; D4324 codegen is a straight sequence on compile-time-resolved behavior.

---

### 6.7 D4324 vs P3400: the simplification

| Capability | P3400 mechanism (branch) | D4324 mechanism |
|---|---|---|
| Name control on assertion | `pre<label>(cond)` template arg; optional `contract_control` lookup | Same syntax; names a **control type** |
| Semantic selection | P3595 config resolve -> `compute_semantic` facet -> clamp to `allowed_semantics` | Static `T::is_ignored(cfg)` (+ implicit semantic in cfg) |
| Skip codegen / predicate eval | Config `ignore` or clamped semantic | `T::is_ignored(cfg) == true`; optional `T::assumable` for optimizer assumption |
| Constification | Language rule (P2900) | `T::constify` static member |
| Violation handling | Global `handle_contract_violation` + optional label `handle_contract_violation` facet | `T::operator()(comment, loc, cfg) -> violation_response` |
| Comment / message | `compute_comment` / `compute_message` facets on label | `comment` argument to `operator()` |
| Allowed semantics gate | `allowed_semantics` facet + runtime clamp/fallback | Concept constraint on control type |
| External configuration | JSON files, CLI flags, groups, namespace/location/caller rules | **None** - type in source is the control object |
| Runtime semantic switching | P3595 dynamic selector + if-cascade | **None** - compile-time `cfg` only |
| Label composition | `operator\|` combining facets | **None** - one type per assertion |
| Group identification | `"name"group` literal + config group rules | **None** |
| Query / custom violation data | `queryable_label` facet | **None** in minimal core |

---

## 7. D4324 implementation game plan

This section consolidates the five per-area checklists into an ordered plan. It is
the "what to do" companion to the "what exists" material above.

### 7.1 Starting point

Branch `d4324` from upstream `master` (already the working tree). Do **not** fork
from the P3850 branch; use it only as the reference captured in this file. The
P3850 code is a superset with a config engine, descriptor ABI, and C runtime that
D4324 deliberately omits, so starting from it would mean deleting more than
adding.

Two demonstrable properties make the minimal-language case on their own and
should anchor the demos: a zero-cost `ignore` (no code, no EH scaffolding) and a
`noexcept` that keeps its meaning (no exception-to-violation translation).

### 7.2 Ordered steps

1. **Parser and AST (Section 3.7).**
   - Expand the three contract tree codes in `cp-tree.def` (12/12/14 operands, or
     a trimmed subset), documenting operand 5 as the control-object type.
   - Add `CONTRACT_CONTROL_TYPE` at operand 5 in `contracts.h`; keep the lazy
     semantic slots at 9 and 10.
   - Add `cp_parser_contract_control_type` parsing an optional `< type-id >`
     after the introducer, defaulting to `std::contracts::default_v`. Wire it into
     `cp_parser_contract_assert`, `cp_parser_function_contract_specifier`, and the
     `cp_maybe_function_contract_specifier` look-ahead (reuse the branch's
     angle-bracket depth walk).
   - Extend `grok_contract` to store the control type; drop P3400 facet
     validation, `allowed_mask`, and trampoline generation.

2. **Semantic resolution (Sections 2.4, 3.6).**
   - Keep `ensure_evaluation_semantic` as the single choke point; replace its body
     with: read the TU `evaluation_config`, instantiate the control type, and read
     `T::is_ignored(cfg)`, `T::constify`, `T::assumable` as constant expressions.
   - Cache the resolved `cfg` (and the control type) on the node instead of a
     `CES_*` value.

3. **Codegen (Section 2.11).**
   - Rewrite `build_contract_check` to the three-step algorithm; delete the P3595
     dynamic if-cascade and `emit_check_for_semantic`/`emit_enforced_violation`.
   - Emit one call to `T::operator()(comment, loc, cfg)` and branch on
     `{proceed, terminate}`; delete `get_cxa_entry_point_name`,
     `declare_cxa_entry_point`, and the entry-point cache.
   - Delete the data-block/descriptor construction; pass comment and location as
     call arguments.
   - Delete the try/catch exception-to-violation block (both the master and branch
     forms). Predicate exceptions propagate to the nearest `noexcept`.
   - Gate constification on `T::constify` at the `constify_contract_access` sites;
     keep the P3098 capture exemption. Resolve the ordering concern: the control
     type must be known when constification is applied (constification currently
     runs during name lookup), so either resolve the control type early for this
     purpose or defer constification into codegen step 2.
   - Keep the `cp-gimplify.cc` genericization hook and `view_as_const` unchanged.

4. **Library (Section 4.8).**
   - Add `evaluation_config`, `violation_response`, the `assertion_control`
     concept, and `default_control`/`review`/`mandatory` to `<contracts>` (see
     Section 4.7 and the paper's Appendix A).
   - Reuse the P2900 flat `contract_violation` and `handle_contract_violation` /
     `invoke_default_contract_violation_handler`. Delete the descriptor chain,
     `libcontracts`, the entry-point matrix, the `_noexcept` wrappers, the P3400
     labels namespace, and the C `<contracts.h>`.

5. **Flags, macros, build (Sections 5.1 to 5.4).**
   - Keep `-fcontracts` and `-fcontract-evaluation-semantic=` (the TU `cfg`
     default). Drop the per-paper flag zoo and the `-fcontract-configuration*`
     family.
   - Define one feature-test macro under `-fcontracts`.
   - No `libcontracts`, no `-lcontracts`/`-lstdc++exp`/`-u ...` link edits; keep
     handler dispatch header-only or in libstdc++ proper.

6. **Tests (Section 5.9).**
   - Ship the four demo tests: zero-cost ignore, `noexcept` preserved, user-defined
     `review` via a library type, and terminate-after-handler. These are the
     deliverable and the Compiler Explorer story.

### 7.3 Consolidated reuse-vs-delete

**Reuse (from the branch, as pattern or code):**
- `cp-gimplify.cc` genericization hook (unchanged)
- `ensure_evaluation_semantic` as the resolution choke point (new body)
- `view_as_const` / `CONST_WRAPPER_P` constification mechanism
- the AST operand-expansion technique and macro layout
- the P2900 `contract_violation` + `handle_contract_violation` surface
- the test-harness patterns (`dg-do compile` + `scan-tree-dump`, `dg-shouldfail` +
  `dg-output` for abort discipline)
- the P3400 `review` facet as the conceptual template for the `review` control
  object

**Delete (the machinery D4324 exists to avoid):**
- the P3595 config engine (`contracts-config*`, JSON/CLI sources, matching)
- the descriptor-chain ABI (`bits/contracts_abi.h`, wire enums)
- `libcontracts` (the pure-C runtime)
- the `__cxa_contract_violation_*` entry-point matrix and `_noexcept` wrappers
- P3595 dynamic selectors and the if-cascade codegen
- P3400 label facet composition, trampolines, `allowed_mask`, groups, queries
- the per-paper flag zoo and the config flags
- the default exception-to-violation translation
- always-on constification (becomes `T::constify`-gated)

### 7.4 What never to reopen

Everything needed from the P3850 branch is in this file: the codegen choke points
and their full bodies (Section 2), the AST/parser/enum layout (Section 3), the
runtime ABI and library surface (Section 4), the flags/build/tests (Section 5),
and the P3400/P3595 seam that D4324 simplifies (Section 6). The branch does not
need to be checked out or consulted again for the D4324 implementation.

---

*2026-07-18 14:36 - Opus 4.8*
