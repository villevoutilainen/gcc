<!-- Companion to D4324_REFERENCE.md. Defines the Compiler Explorer demos the D4324 fork must produce, why each matters, and what the implementor must make work for each to land. -->

# D4324 Compiler Explorer Demo Goals

## Bottom line up front

The D4324 reference implementation exists to produce a small set of Compiler Explorer links that prove the "minimum language, maximum library" design works in a real compiler. Four demos carry the argument: (1) a side-by-side that shows P2900 generates exception-handling scaffolding around every contract while D4324 does not, (2) an ignored assertion that compiles to nothing, (3) a user-defined checked semantic written in ten lines of library code with no compiler change, and (4) contracts and a core-language-UB profile operating as independent peers. Build those four first. Each demo is simultaneously a persuasion artifact and a reference-implementation acceptance test, so implementing the demo correctly and implementing the feature correctly are the same task.

## Why these demos matter

This committee's stated preference is existing practice and implementation experience over paper designs. A design defended only on paper invites an endless "paper versus paper" debate that a contested room resolves by deferring. A running compiler changes the question from "which expert do I believe" to "download it and run it," which is the one move that lets an undecided reviewer say yes without adjudicating a disagreement between authors. The demos are the mechanism that converts the D4324 design from an argument into evidence.

Each demo maps to a specific claim the paper makes, and each is chosen because its result survives compression. A screenshot of two assembly panes, or an empty function body where a check used to be, is a fact that a reader repeats in a hallway or a thread without re-deriving it. The demos that matter are the ones where the compiler output is self-evidently in D4324's favor and cannot be waved away as opinion.

The demos are also the reference-implementation acceptance criteria. Demos 1, 2, 3, and 8 correspond one-to-one with the four demonstrations the paper commits to (an ignored assertion emits no code and no exception scaffolding; a predicate exception propagates without translation and noexcept keeps its meaning; a user-defined semantic is added with a library function and no compiler change; no core-language undefined behavior is routed through the contract-violation handler). If the demos compile and show the intended output, the implementation has met its stated bar. Full extraction of the machinery each demo relies on is in `D4324_REFERENCE.md` (Section 7 is the ordered implementation plan; Section 5.9 has runnable test skeletons).

A secondary, quieter argument rides on the demos: they are cheap to build because the design is small. Standing them up on top of the existing GCC contracts branch in a single mailing cycle is itself evidence for the thesis. A design that a small team can prototype quickly is a design with a small language surface. Do not undercut that by gold-plating; a rough prototype that shows the four results is worth more than a polished one that ships late.

## The demos, in priority order

### Tier 1: headliners (build these first)

**1. The exception-scaffolding side-by-side**

- What you show: the same `int f(int x) noexcept pre(x > 0)` compiled by the P2900 GCC branch in the left pane and the D4324 fork in the right pane, with the assembly diffed.
- What to look at: P2900 emits a landing pad, an exception table, and cleanup code wrapping the predicate; D4324 emits a plain compare-and-call with none of it, and `noexcept(f(...))` remains `true`.
- Why it matters: this is the most persuasive single demo because it turns P2900's most-criticized behavior (exception-to-violation translation, which forces exception-handling code around every assertion and produces the backtrace implementers have called useless) into visible machine code. It is verifiable by anyone with the link, which is exactly the audience that trusts assembly over authors.
- What must work: exception-to-violation translation is removed by default, so codegen must not wrap the predicate in a try/catch (the block deleted per reference Section 2.9); a `noexcept` function carrying a contract must keep `noexcept` true, so the emitted violation call must not introduce a cleanup or landing-pad region under a non-throwing predicate; the D4324 fork must accept the identical bare `pre(cond)` source the P2900 branch accepts, so the diff is apples-to-apples.

**2. Zero-cost ignore**

- What you show: `pre<std::contracts::ignore>(expensive_check(x))` at the ignore configuration, ideally beside the library-call form (the P4009 plain-function shape) that wraps the predicate in a call.
- What to look at: the type-based form compiles to an empty function with no call to `expensive_check` and no emitted code; the library-call form must evaluate the predicate to make its call before any library code can decide to ignore it.
- Why it matters: this is the zero-overhead principle made literal (what you do not use you do not pay for) and the cleanest answer to "why not just a library?" The answer is that only the compiler can skip the predicate, which is precisely the residue D4324 keeps in the language.
- What must work: step one of the three-step algorithm must be implemented so that `build_contract_check` returns no code when `T::is_ignored(cfg)` folds to a compile-time true, and the predicate is not evaluated; the compiler must evaluate `is_ignored(cfg)` as a constant expression at code-generation time; `cfg` is supplied by the TU default (`-fcontract-evaluation-semantic=ignore`) or hard-coded by the control type.

**3. A user-defined semantic in ten lines, no compiler change**

- What you show: in the same buffer, a user defines `struct review { ... }` (log and continue) and a second custom control such as `struct count_and_continue { ... }`, then writes `pre<review>(cond)` and `pre<count_and_continue>(cond)`.
- What to look at: both compile and call the user's `operator()`; there is no new flag, no compiler rebuild, and no compiler-generated per-semantic entry point.
- Why it matters: this is the "maximum library" thesis in one screenshot, and its force multiplies against the contrast that the P3850 branch needed roughly 36,000 lines and a dedicated flag to add two fixed semantics. Adding a semantic here is writing a function.
- What must work: the entire control-object pipeline end to end (parser accepts the optional `<type-id>`, grok stores the control type on the node, resolution reads the compile-time members, codegen emits a single call to `T::operator()(comment, loc, cfg)` and branches on the returned `violation_response`), plus the `assertion_control` concept and `default_control` in `<contracts>`; a user type that satisfies the concept must work with no compiler involvement.

**4. Contracts and a profile as independent peers**

- What you show: an author-written `pre(cond)` routing through its control object, alongside a `std::core_ub`-style profile check on a core-language operation (for example a signed-overflow or null-dereference check) that does not route through the contract-violation handler; then the contract handler is replaced and the UB check's response is shown to be unchanged.
- What to look at: swapping `handle_contract_violation` changes the contract response but not the core-language-UB response, because the two are wired independently.
- Why it matters: this is the architecture that the ownership question turns on. It shows in running code that the contracts core has no claim on core-language undefined behavior, so it cannot become the substrate through which that behavior is configured. It is the demo that makes the profiles-ownership papers concrete.
- What must work: the contracts core must lower only author-written `pre`/`post`/`contract_assert` (it already does); a minimal core-language-UB profile check must exist whose response is owned independently of the contract handler (a second header in the shape of P4317R0, or at minimum a stub that terminates on its own path); the demo must be able to replace the contract handler without touching the profile path.

### Tier 2: supporting demos

**5. Constification off by default**

- What you show: a predicate over an overload set where the const and non-const overloads differ, compiled under the default and then under `pre<std::contracts::constified>(cond)`.
- What to look at: by default the predicate binds the same overload the function body would (no silent divergence); opting in restores constification.
- Why it matters: it answers the "verifiable by local inspection" concern directly. Under P2900 a predicate can quietly call a different overload than the same text in the body; D4324 makes constification a control-object property rather than a language rule.
- What must work: `constify_contract_access` must be gated on the resolved control object's `constify` member (default false); the control type must be resolvable at the point constification is applied (constification currently runs during name lookup, so either resolve the control type early for this purpose or defer constification into codegen step two, per reference Section 2.8).

**6. Bloomberg's `bsls_review` as a library control object**

- What you show: the log-and-continue `review` control (constify true, logs, returns proceed) running at the observe configuration, with `bsls_assert`-style termination expressed as a separate `mandatory` control.
- What to look at: the review control logs and the program continues; the mandatory control terminates.
- Why it matters: it demonstrates that a deployed, real-world practice is expressible as a library control object with no language feature, and that log-and-continue lives at the library level where the post-violation state is defined. This closes the argument that the design cannot express existing production behavior.
- What must work: the `review` and `mandatory` controls in `<contracts>`; codegen must honor the `violation_response` return, continuing on `proceed` and contract-terminating on `terminate` (reference Section 2.2).

**7. Guaranteed-enforced `mandatory` is assumable**

- What you show: `pre<std::contracts::mandatory>(x > 0)` where `mandatory` has `assumable` true, with a downstream operation the optimizer can simplify given the predicate.
- What to look at: at the ignore configuration the optimizer uses the predicate as an assumption (for example a later bounds check is eliminated); when enforced it terminates directly with no handler indirection.
- Why it matters: it shows the guaranteed-enforced strand as a library control object and the compile-time resolution of assumability, which is the one capability a library alone cannot provide.
- What must work: step one of the algorithm must emit an optimizer assumption (an unreachable-on-negation of the predicate) when `T::is_ignored(cfg)` is true and `T::assumable` is true; the assumption must be in a form the optimizer consumes so the effect is visible.

**8. The common syntax is unchanged**

- What you show: bare `pre(cond)`, `post(r: cond)`, and `contract_assert(cond)` compiled under D4324.
- What to look at: they produce the same checked, terminating default as P2900; naming a control object is the exception, not the rule.
- Why it matters: it answers the "one common greppable spelling is the point" objection. The uniform form tools and readers rely on is preserved; the control object is opt-in.
- What must work: a bare contract with no `<type-id>` must bind `std::contracts::default_v`, and `default_control` must produce enforce-by-default checked, terminating code generation.

## What every demo depends on (the shared minimal pipeline)

All eight demos rest on the same core, so implement this first and the demos become configuration of it. Detail and full function bodies are in `D4324_REFERENCE.md`.

- Parser: an optional `< type-id >` after the `pre` / `post` / `contract_assert` introducer, defaulting to `std::contracts::default_v` when absent (reference Section 3.5, 3.7).
- AST: the control-object type stored on the contract node (reuse operand 5), with the semantic slots left for lazy resolution (reference Section 3.2, 3.3).
- Resolution: read the compile-time members `is_ignored(cfg)`, `constify`, and `assumable` off the control type, with `cfg` the TU-wide `evaluation_config` (reference Section 2.4).
- Codegen: the three-step `build_contract_check` (ignore-or-assume, then constified-or-not predicate, then a single `T::operator()(comment, loc, cfg)` call branching on proceed or terminate), with the P3850 entry-point matrix, descriptor data blocks, and default exception translation all deleted (reference Section 2.2, 2.11).
- Library: a `<contracts>` header providing `evaluation_config`, `violation_response`, the `assertion_control` concept, and `default_control` / `review` / `mandatory`, reusing the P2900 `contract_violation` object and `handle_contract_violation` handler (reference Section 4.7, 4.8).
- Flags: `-fcontracts` (already present) and `-fcontract-evaluation-semantic=` for the TU `cfg` default; no per-paper flags and no separate contracts runtime library (reference Section 5.1, 5.4).

## Sequencing

Build the shared pipeline, then the four headliners in order (1, 3, 4 are the argument; 2 is the quickest win and a good first target once the pipeline compiles). The supporting demos harden the case against detailed technical review and can follow. In a presentation, lead with demo 1 for the visual, then demo 3 for the thesis, then demo 4 for the stake; keep the rest in reserve for the floor.

---

*2026-07-18 15:24 - Opus 4.8*
