// Contracts support core for -*- C++ -*-

// Copyright The GNU Toolchain Authors.
//
// This file is part of GCC.
//
// GCC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// GCC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

/** @file bits/contracts.h
 *  This is an internal header file, included by other library headers.
 *  Do not attempt to use it directly. @headername{contracts}
 *
 *  The core of contracts support: everything the library's own headers
 *  need, reachable from every header (bits/version.h includes it), so it
 *  includes nothing heavier than <source_location> and <bits/stringfwd.h>.
 *  <contracts> adds the user-facing control objects, whose violation
 *  reports format run-time data with std::format.
 */

#ifndef _GLIBCXX_BITS_CONTRACTS_H
#define _GLIBCXX_BITS_CONTRACTS_H 1

#pragma GCC system_header

#define __glibcxx_want_contracts
#define __glibcxx_want_replaceable_contract_violation_handler
#include <bits/version.h>

#ifdef __cpp_lib_contracts
// Deliberately not #include <concepts> here (the only thing in this file
// that ever needed it was the assertion_control concept, now moved to
// <concepts> itself -- see that header's own matching comment for why),
// and deliberately not #include <bits/exception_ptr.h> either -- nothing
// here actually names exception_ptr/current_exception/etc (the comments
// about an exception propagating past a violation handler are about the
// *user's* control-object code, not this header's own), and that header
// pulls in <bits/move.h> for its own std::forward/std::move use, which
// needs <type_traits>. Keeping this header free of any <type_traits>/
// <concepts> dependency, transitive or direct, avoids a real, confirmed
// circular-include failure when <contracts> is reached from a context
// where <type_traits> is itself still mid-body (e.g. <type_traits>'s own
// #include <bits/version.h>, for its own feature-test-macro needs).
#include <source_location>

namespace std _GLIBCXX_VISIBILITY(default)
{
  // As declared in <exception>, which this header can't include (see
  // above).  The control types call it when a violation terminates.
  extern "C++" void terminate() noexcept __attribute__ ((__noreturn__,__cold__));

_GLIBCXX_BEGIN_NAMESPACE_VERSION

  // As <bits/stringfwd.h> and <string_view> declare them (with their
  // default template arguments), declared here so that __simple_print can
  // print them: this header is reached from <bits/stringfwd.h> itself,
  // through <bits/c++config.h>, before it declares them.
_GLIBCXX_BEGIN_NAMESPACE_CXX11
  template<typename _CharT, typename _Traits, typename _Alloc>
    class basic_string;
_GLIBCXX_END_NAMESPACE_CXX11

  template<typename _CharT, typename _Traits>
    class basic_string_view;

namespace contracts
{
  // From P2900R14

  enum class assertion_kind : __UINT16_TYPE__ {
    pre = 1,
    post = 2,
    assert = 3,

    /* Implementation-defined values should have a minimum value of 1000. */
  };

  enum class evaluation_semantic : __UINT16_TYPE__ {
    ignore = 1,
    observe = 2,
    enforce = 3,
    quick_enforce = 4,

    /* Implementation-defined values should have a minimum value of 1000.  */
  };

  enum class detection_mode : __UINT16_TYPE__ {
    predicate_false = 1,
    evaluation_exception = 2,

    /* Implementation-defined values should have a minimum value of 1000. */
  };

  // D4324: which side of a call a contract's runtime check is happening
  // on. A contract_assert has no caller/definition distinction at all
  // (it's a statement inside a function body, not a precondition or
  // postcondition on a boundary callers can see), hence not_applicable.
  enum class assertion_check_side : __UINT16_TYPE__ {
    not_applicable = 0,
    definition = 1,
    client = 2,
  };

  // D4324: one group-name -> evaluation_semantic override rule, sourced
  // from -fcontracts-group-evaluation-semantic=group:semantic (real
  // compiler-provided data, not the <contract_labels> P3400 namespace's
  // own identification_label facet -- this type is usable by any
  // control type, not just a P3400 label; see assertion_static_info
  // below). Unlike <contract_labels>'s own group_name_view (which
  // ordinary label code *does* construct directly, via aggregate-init,
  // hence its public fields), a group_semantic_rule is only ever
  // constructed by the compiler -- by directly filling these private
  // fields, the same technique already used for assertion_static_info/
  // std::source_location elsewhere in this header -- and only ever
  // read by library code, so it stays properly encapsulated, matching
  // assertion_static_info's own private-fields-plus-accessors style.
  class group_semantic_rule
  {
    const char* _M_group_name = nullptr;
    evaluation_semantic _M_semantic = evaluation_semantic::ignore;

  public:
    constexpr const char* group_name() const noexcept { return _M_group_name; }
    constexpr evaluation_semantic semantic() const noexcept { return _M_semantic; }
  };

  // A minimal, dependency-free view over an array of group_semantic_rule
  // -- deliberately not std::span: <span> transitively reaches <concepts>
  // (via <array>/<compare>), which re-enters this header (see
  // <concepts>'s own assertion_control comment for the identical
  // <type_traits> hazard) while it's still mid-body, a real,
  // confirmed circular-include failure. Also only ever compiler-
  // constructed (see group_semantic_rule above), so it stays
  // encapsulated too: private fields, begin()/end()/size() only.
  class group_semantic_rules_view
  {
    const group_semantic_rule* _M_data = nullptr;
    __SIZE_TYPE__ _M_size = 0;

  public:
    constexpr const group_semantic_rule* begin() const noexcept { return _M_data; }
    constexpr const group_semantic_rule* end() const noexcept { return _M_data + _M_size; }
    constexpr __SIZE_TYPE__ size() const noexcept { return _M_size; }
  };

  // D4324: static (compile-time-known) properties of where a contract
  // occurs, independent of any one particular runtime evaluation.
  // is_virtual(): the contract's function is virtual; overrides_virtual():
  // it also overrides a base class's virtual function.  Both are false
  // for a contract_assert.
  class assertion_static_info {
    evaluation_semantic _M_semantic;
    assertion_check_side _M_side;
    bool _M_is_virtual;
    bool _M_overrides_virtual;
    group_semantic_rules_view _M_group_rules;
    const char* _M_comment;
    assertion_kind _M_kind;
    source_location _M_location;
    // The condition's operands (see operand_count): their names, each
    // followed by a NUL, and the format string.
    const char* _M_operand_names;
    __SIZE_TYPE__ _M_operand_count;
    const char* _M_operand_format;

    friend struct __evaluate_with_access;

  public:
    constexpr evaluation_semantic semantic() const noexcept { return _M_semantic; }
    constexpr assertion_check_side side() const noexcept { return _M_side; }
    constexpr bool is_virtual() const noexcept { return _M_is_virtual; }
    constexpr bool overrides_virtual() const noexcept { return _M_overrides_virtual; }

    // The whole -fcontracts-group-evaluation-semantic= table for this
    // TU (the same for every assertion in it), in command-line order.
    constexpr group_semantic_rules_view group_semantic_rules() const noexcept
    { return _M_group_rules; }

    // The assertion's predicate text, as assertion_context::comment()
    // gives it, but available to a control object's queries at compile
    // time; null where no assertion is being built.
    constexpr const char* comment() const noexcept { return _M_comment; }

    // The assertion's kind and source location, as assertion_context::kind()
    // and location() give them, but available to a control object's queries
    // at compile time; kind() is 0 and location() empty where no assertion
    // is being built.
    constexpr assertion_kind kind() const noexcept { return _M_kind; }
    constexpr source_location location() const noexcept { return _M_location; }

    // The operands of the assertion's condition, which a control object
    // whose operator() accepts them after the assertion_context is given:
    // each parameter (this included), automatic variable and, for a
    // postcondition, result the condition names, once, in order of first
    // appearance.  operand_name(K) is the K-th one's name as written
    // ("this" for the object), operand_format() the condition's text with
    // each use of an operand's value replaced by a std::format replacement
    // field naming its position ("{0} < {1}"), other braces doubled: an
    // operand that is the object of a member call ('v' in 'v.size()'), and
    // this, stay text.  All empty where comment() is.
    constexpr __SIZE_TYPE__ operand_count() const noexcept
    { return _M_operand_count; }

    constexpr const char* operand_name(__SIZE_TYPE__ __k) const noexcept
    {
      if (__k >= _M_operand_count || !_M_operand_names)
	return nullptr;
      const char* __p = _M_operand_names;
      for (; __k; --__k)
	{
	  while (*__p)
	    ++__p;
	  ++__p;
	}
      return __p;
    }

    constexpr const char* operand_format() const noexcept
    { return _M_operand_format; }
  };

#if __cpp_impl_reflection
  // D4324 (experimental): what a control object's compile-time query sees
  // when it takes this type instead of assertion_static_info -- the static
  // info, a reflection of the function the assertion belongs to (per
  // info.kind(), the function a pre/post is declared on, or the one whose
  // body contains a contract_assert; the null reflection outside any
  // function), and one of the namespace enclosing it.  A consteval-only
  // type, so such a query is consteval.
  struct assertion_declaration_info {
    assertion_static_info info;
    decltype(^^int) function;
    // The innermost namespace enclosing the assertion (classes skipped):
    // that of function, or, outside any function, where the assertion
    // is; the global namespace at the outermost.  Never null.
    decltype(^^int) enclosing_namespace;
  };
#endif

  using __vendor_ext = void;

  class contract_violation {
    __UINT16_TYPE__ _M_version;
    assertion_kind _M_assertion_kind;
    evaluation_semantic _M_evaluation_semantic;
    detection_mode _M_detection_mode;
    const char* _M_comment;
    const void* _M_src_loc_ptr;
    __vendor_ext* _M_ext;
    const char* _M_message;	// Version 2 and later.

  public:
    // cannot be copied or moved or assigned to
    contract_violation(const contract_violation&) = delete;
    contract_violation& operator=(const contract_violation&) = delete;

    assertion_kind kind() const noexcept { return _M_assertion_kind; }
    evaluation_semantic semantic() const noexcept { return _M_evaluation_semantic; }
    detection_mode mode() const noexcept { return _M_detection_mode; }
    const char* comment() const noexcept { return _M_comment; }
    // The message a control object passed along with the comment
    // (assertion_info::set_message), or null.  A violation object built
    // before the field existed (version 1) has none.
    const char* message() const noexcept
    { return _M_version >= 2 ? _M_message : nullptr; }
    std::source_location location() const noexcept {
      std::source_location __loc;
      __loc._M_impl
	   = static_cast<const source_location::__impl*>(_M_src_loc_ptr);
      return __loc;
    }
    bool is_terminating () const noexcept {
      return _M_evaluation_semantic == std::contracts::evaluation_semantic::enforce
	    || _M_evaluation_semantic == std::contracts::evaluation_semantic::quick_enforce;
    }
  };

  void invoke_default_contract_violation_handler(const contract_violation&) noexcept;

  // D4324: the return type of an optional validate(assertion_static_info)
  // Contract Control Object member (see <contract_labels>'s own
  // label_base::validate for a real user, and contract_control_validates
  // in gcc/cp/contracts.cc for the compiler-side dispatch) -- a
  // compile-time-only acceptability gate
  // for a control object's own configuration, entirely separate from
  // is_ignored's "should this run" question. valid == false is diagnosed
  // as a hard, compile-time error; message, if non-null, is folded into
  // that diagnostic instead of a generic fallback, so a control object
  // can explain *why* it rejected an assertion. Not part of P2900/P3400
  // -- a general, D4324-only fifth CCO member.
  struct contract_validation_result {
    bool valid;
    const char* message = nullptr;
  };

  // D4324 "Minimum Language, Maximum Library" control-object surface.

  // A control object's call operator returns void: returning means the
  // program continues past the assertion; to end the program the operator
  // terminates in its own body.  No violation_response is needed.
  //
  // The operator is handed the predicate itself rather than its already-
  // computed result: ctx.check() evaluates it on demand.  This lets the
  // control object decide whether, when, and how many times to evaluate
  // the predicate, and whether to let an exception from it propagate or
  // contain it, instead of the compiler always evaluating eagerly and
  // only calling the operator on failure.

  // D4324 (paper reviewer feedback): the subset of assertion_context
  // relevant to reporting a violation -- comment/location/semantic/kind,
  // the same four fields invoke_violation_handler below used to take as
  // four separate arguments. Deliberately a *different* kind of type from
  // assertion_context: that type is given no public constructor on
  // purpose, so ordinary user code can never grow an ABI dependency on
  // its exact layout (it stays free to gain new private members across
  // library versions); assertion_info has no such constraint, so it can
  // be freely constructed, copied, and mutated by any control object,
  // built-in or user-written -- from scratch, or seeded from an existing
  // assertion_context via assertion_context::info() below and then
  // adjusted (e.g. a per-instance message overriding the predicate's own
  // auto-derived comment(), or a control type that always reports
  // evaluation_semantic::enforce regardless of the ambient semantic).
  class assertion_info
  {
  public:
    constexpr
    assertion_info (const char* __comment, std::source_location __location,
		    evaluation_semantic __semantic, assertion_kind __kind) noexcept
    : _M_comment(__comment), _M_location(__location),
      _M_semantic(__semantic), _M_kind(__kind)
    { }

    constexpr const char* comment() const noexcept { return _M_comment; }
    constexpr std::source_location location() const noexcept { return _M_location; }
    constexpr evaluation_semantic semantic() const noexcept { return _M_semantic; }
    constexpr assertion_kind kind() const noexcept { return _M_kind; }

    constexpr void set_comment(const char* __comment) noexcept
    { _M_comment = __comment; }
    constexpr void set_location(std::source_location __location) noexcept
    { _M_location = __location; }
    constexpr void set_semantic(evaluation_semantic __semantic) noexcept
    { _M_semantic = __semantic; }
    constexpr void set_kind(assertion_kind __kind) noexcept
    { _M_kind = __kind; }

    // A message for the violation handler, alongside the comment: what
    // contract_violation::message() gives it.  Null by default.
    constexpr const char* message() const noexcept { return _M_message; }
    constexpr void set_message(const char* __message) noexcept
    { _M_message = __message; }

  private:
    const char* _M_comment;
    std::source_location _M_location;
    evaluation_semantic _M_semantic;
    assertion_kind _M_kind;
    const char* _M_message = nullptr;
  };

  // The comment, location, semantic and kind are those of static_info():
  // the comment is empty, not null, and the location empty, when the
  // control object omits them.
  struct assertion_context {
    constexpr const char* comment() const noexcept
    { return __static_info.comment(); }
    constexpr std::source_location location() const noexcept
    { return __static_info.location(); }
    constexpr evaluation_semantic semantic() const noexcept
    { return __static_info.semantic(); }
    constexpr assertion_kind kind() const noexcept
    { return __static_info.kind(); }
    constexpr const assertion_static_info& static_info() const noexcept
    { return __static_info; }

    // Evaluates the predicate.  Deliberately not noexcept: the predicate
    // may throw, and a control object may want that exception to
    // propagate rather than be forced into std::terminate() here.
    constexpr bool check() const { return __check(__args); }

    // Bundles the four fields above into an assertion_info -- see that
    // type's own comment for why it exists as a separate, freely
    // user-constructible type rather than just widening this one.
    constexpr assertion_info info() const noexcept
    {
      assertion_info __i(comment(), location(), semantic(), kind());
      __i.set_message(message());
      return __i;
    }

    // A message for the violation handler that the code evaluating the
    // assertion supplied (evaluate_with); null in the context of an
    // assertion written as one (pre, post, contract_assert), whose
    // control object supplies its own if any.  A control object that
    // reports forwards it (assertion_info::set_message, as info() does).
    constexpr const char* message() const
    { return __message_fn ? __message_fn(__message_arg) : nullptr; }

  private:
    assertion_static_info __static_info;
    void* __args;
    bool (*__check)(void*);
    // Not set by the compiler.
    const char* (*__message_fn)(const void*);
    const void* __message_arg;

    friend struct __evaluate_with_access;
  };

  // D4324: evaluating an assertion that isn't written as one.
  // assertion_info_for gives the static info an assertion naming a
  // control object would have at a site, by the configuration;
  // evaluate_with then has the control object evaluate a predicate with
  // it, as the compiler has it evaluate an assertion's -- so a check in
  // ordinary code (a library's assertion macro) gets the configuration and
  // the control object's whole behavior.
  struct __evaluate_with_access
  {
    template<typename _Pred>
      static constexpr bool
      __check(void* __p)
      { return static_cast<bool>((*static_cast<_Pred*>(__p))()); }

    static constexpr const char*
    __message_value(const void* __p)
    { return static_cast<const char*>(__p); }

    template<typename _Msg>
      static constexpr const char*
      __message_call(const void* __p)
      { return (*static_cast<const _Msg*>(__p))(); }

    template<typename _Pred, typename _Msg>
      static constexpr assertion_context
      __make(const assertion_static_info& __info, _Pred& __pred,
	     const char* __comment, const _Msg& __message)
      {
	assertion_context __ctx;
	__ctx.__static_info = __info;
	if (__comment)
	  __ctx.__static_info._M_comment = __comment;
	__ctx.__args = const_cast<void*>(static_cast<const volatile void*>(
			 __builtin_addressof(__pred)));
	__ctx.__check = &__check<_Pred>;
	if constexpr (__is_same(_Msg, decltype(nullptr)))
	  {
	    __ctx.__message_fn = nullptr;
	    __ctx.__message_arg = nullptr;
	  }
	else if constexpr (requires { static_cast<const char*>(__message); })
	  {
	    __ctx.__message_fn = &__message_value;
	    __ctx.__message_arg = static_cast<const char*>(__message);
	  }
	else
	  {
	    __ctx.__message_fn = &__message_call<_Msg>;
	    __ctx.__message_arg = __builtin_addressof(__message);
	  }
	return __ctx;
      }
  };

  // Has CTRL evaluate an assertion with static info INFO (typically from
  // assertion_info_for) whose predicate is PRED, a callable with no
  // arguments whose result is contextually converted to bool, called when
  // (and as often as) CTRL calls the context's check(); exceptions it
  // throws propagate from check().  COMMENT, if given, is the context's
  // comment(), MESSAGE its message(): a const char*, or a callable
  // returning one, called only when message() is.  If CTRL is callable
  // with the context followed by OPS, it is called so, otherwise with the
  // context alone.  The predicate is not constified.
  template<typename _Ctrl, typename _Pred, typename _Msg = decltype(nullptr),
	   typename... _Ops>
    constexpr void
    evaluate_with(const _Ctrl& __ctrl, const assertion_static_info& __info,
		  _Pred&& __pred, const char* __comment = nullptr,
		  const _Msg& __message = nullptr, const _Ops&... __ops)
    {
      using _Pt = __remove_reference(_Pred);
      const assertion_context __ctx
	= __evaluate_with_access::__make<_Pt, _Msg>(__info, __pred,
						    __comment, __message);
      if constexpr (sizeof...(_Ops) > 0
		    && requires { __ctrl(__ctx, __ops...); })
	__ctrl(__ctx, __ops...);
      else
	__ctrl(__ctx);
    }

#if __has_builtin(__builtin_contract_static_info)
  // The static info an assertion of KIND naming control object CTRL would
  // be given at LOC, of FUNCTION in ENCLOSING_NAMESPACE (by default, the
  // caller's), for its evaluation at compile time if CONSTANT_EVALUATION,
  // else at run time: the semantic the configuration gives it there, then
  // CTRL's compute_semantic.  Its side is not applicable; it has no comment
  // and no operands.
#if __cpp_impl_reflection
  template<typename _Ctrl>
    consteval assertion_static_info
    assertion_info_for(const _Ctrl& __ctrl, assertion_kind __kind,
		       bool __constant_evaluation = false,
		       source_location __loc = source_location::current(),
		       decltype(^^int) __function
			 = __builtin_contract_current_scope(0),
		       decltype(^^int) __enclosing_namespace
			 = __builtin_contract_current_scope(1))
    {
      return __builtin_contract_static_info(__ctrl, __kind,
					    __constant_evaluation, __loc,
					    __function, __enclosing_namespace);
    }
#else
  // Without reflection, no function and the global namespace.
  template<typename _Ctrl>
    consteval assertion_static_info
    assertion_info_for(const _Ctrl& __ctrl, assertion_kind __kind,
		       bool __constant_evaluation = false,
		       source_location __loc = source_location::current())
    {
      return __builtin_contract_static_info(__ctrl, __kind,
					    __constant_evaluation, __loc, 0, 0);
    }
#endif
#endif

  // Every one of these bool-returning static member functions -- the
  // mandatory is_ignored/constify/assumable below, and the optional
  // omit_comment/omit_source_location/force_client_side_check/
  // force_definition_side_check -- takes an assertion_static_info, so a
  // control type can answer differently depending on the evaluation
  // semantic (e.g. constify only under enforce) *and* on which side
  // (client/definition/not_applicable) this particular occurrence is
  // being checked on. None of them take a full assertion_context:
  // they're all evaluated before one can exist -- e.g. constify is
  // consulted during template substitution, before any
  // predicate-evaluation machinery exists, and omit_comment/
  // omit_source_location decide whether to even build the real runtime
  // assertion_context fields in the first place.
  //
  // A control type may also optionally declare
  //   static constexpr bool omit_comment(assertion_static_info) { return true; }
  //   static constexpr bool omit_source_location(assertion_static_info) { return true; }
  // to tell the compiler it never needs assertion_context::comment()/
  // location() for this control, so it doesn't need to build/embed
  // either one at all (both default to false -- i.e. keep storing --
  // when absent, so existing control types are unaffected). Deliberately
  // not part of this concept: unlike constify/assumable, their absence
  // must still make a control type usable.
  //
  // A control type may also optionally declare
  //   static constexpr bool force_client_side_check(assertion_static_info) { return true; }
  //   static constexpr bool force_definition_side_check(assertion_static_info) { return true; }
  // (mutually exclusive -- a program is ill-formed if a control type
  // returns true from both for the same query) to pin every contract
  // naming it to one specific side, regardless of the
  // -fcontracts-client-check/-fcontracts-definition-check command-line
  // policy: force_client_side_check means the contract is checked only
  // via a caller-side wrapper, never at the function's own definition;
  // force_definition_side_check is the mirror image. Both default to
  // false when absent, so existing control types are unaffected and the
  // ordinary command-line policy applies as before.
  //
  // A control type may also optionally declare
  //   static constexpr bool inherited(assertion_static_info) { return true; }
  // to let a virtual function's contract propagate to an override that
  // declares no contract of its own: if a contract-less override's
  // base contract's control type returns true from inherited() for a
  // given side (client/definition), the override behaves, on that
  // side, as if it had explicitly declared the identical contract
  // (same control object). Evaluated independently per side -- a
  // control type may choose to be inherited on only one of the two.
  // Defaults to false when absent, so existing control types opt in to
  // nothing new and are unaffected.
  //
  // A control type may also optionally declare
  //   static constexpr bool is_conveyor(assertion_static_info) { return true; }
  // (D4324) to require that the predicate itself -- not this control
  // type's own operator(), which is never checked -- satisfy the
  // conveyor-function syntactic restrictions (see the 'conveyor'
  // function-specifier): no reinterpret_cast, no new/delete/throw/
  // co_await, no base-to-derived static_cast, no narrowing conversion,
  // every local explicitly initialized, at least one return statement
  // for a non-void condition. Evaluated the same way as
  // is_ignored/constify -- before any predicate-evaluation machinery
  // exists -- and consulted regardless of whether the predicate is
  // otherwise ignored, since it is a static well-formedness property of
  // the program, not a runtime behavior. Defaults to false when absent,
  // so existing control types are unaffected.
  //
  // A control type may also optionally declare any of the following five
  // (D4324), each defaulting to false when absent so existing control
  // types are unaffected:
  //
  //   static constexpr bool never_proven(assertion_static_info) { return true; }
  // Exempts a contract_assert naming this control type from ever being
  // checked against already-established facts -- no diagnostic of its
  // own, ever, regardless of -fcontract-conveyor-proofs/-fcontract-
  // symbolic-proofs or analyzed_conveyor/proven_conveyor below. It still
  // establishes itself as a trusted fact for later code in the same
  // function, exactly like any other conveyor/symbolic contract_assert --
  // this only ever suppresses the "am I actually true" half, never the
  // "trust me from here on" half. Meant for a library's own internal,
  // defensive assertions deep in generic code, evaluated in a context
  // where they are essentially never provable and were never meant to
  // be -- see std::never_proven_conveyor below.
  //
  //   static constexpr bool analyzed_conveyor(assertion_static_info) { return true; }
  //   static constexpr bool proven_conveyor(assertion_static_info) { return true; }
  // Each implies is_conveyor (conveyor UB-freedom restrictions and
  // is_object_address eligibility both apply) and *additionally* forces
  // -fcontract-conveyor-proofs-equivalent analysis on for this specific
  // contract, regardless of the command-line flag -- "in the context it
  // appears in": a contract_assert naming one gets its own condition
  // checked against ambient facts even in an otherwise-unanalyzed TU; a
  // precondition naming one makes every caller's own argument obligation
  // checked too. analyzed_conveyor is lenient (an unprovable conjunct is
  // a warning); proven_conveyor is strict (unprovable is also an error,
  // matching WG14 P4021R2's compile_assert() outcome table exactly:
  // proof true, proof false, or cannot prove are the only three
  // outcomes, and only the first compiles clean -- except deterministic,
  // since this runs through the same static analysis as everything else
  // here, not an optimizer's own dead-code elimination). No separate
  // compiler-wide "error instead of warn" mode exists for the lenient
  // form: that's just -Werror.
  //
  //   static constexpr bool analyzed_symbolic(assertion_static_info) { return true; }
  //   static constexpr bool proven_symbolic(assertion_static_info) { return true; }
  // The is_symbolic-flavor mirror of the two immediately above, in every
  // respect (implies is_symbolic; analyzed_symbolic lenient, proven_
  // symbolic strict).

  // The assertion_control concept that checks the three mandatory traits
  // above (is_ignored/constify/assumable) plus a callable operator() now
  // lives in <concepts> instead of here -- see that header's own comment
  // for why (this header must not need <concepts>/<type_traits> itself).

  // Recognized specially by the compiler (see gcc/cp/contracts.cc's
  // build_contract_check) wherever it appears inside the condition of a
  // pre<>/post<> on a virtual member function that overrides another;
  // never actually defined/linked anywhere. _Base must name an
  // accessible, unambiguous (direct or indirect) base of the enclosing
  // class that itself declares a corresponding override -- i.e. exactly
  // what a qualified call like _Base::f(...) would need to resolve to,
  // but naming only the predicate part, not a real call: no access to
  // _Base's override is required beyond what naming it as a base already
  // grants, even when the override's own condition refers to a private
  // member of _Base.
  //
  // Evaluates to the conjunction of all of _Base's own contracts of the
  // *same kind* as the pre<>/post<> it's used in (all its preconditions
  // if used inside a pre<>, all its postconditions if used inside a
  // post<>) -- one term per named ancestor, meant to be combined with
  // ordinary boolean operators: base_contract<Base>() || extra_check(x)
  // weakens a precondition (Eiffel/Ada-style), base_contract<Base>() &&
  // extra_check(r) strengthens a postcondition, and simply never calling
  // it at all is a valid policy too (no inheritance, as if this feature
  // didn't exist). If _Base's own override has no contract of the
  // matching kind at all, this evaluates to true (an absent precondition
  // or postcondition vacuously accepts anything), so a composed
  // expression doesn't need every ancestor to declare a matching kind.
  //
  // Distinct from, and independent of, a control type's own inherited()
  // (which governs a contract-*less* override picking up its base's
  // contract wholesale, automatically): base_contract<Base>() is always
  // an explicit, opt-in reference written by the author of the
  // override's own contract.
  template <class _Base>
  bool base_contract();

  // Recognized specially by the compiler during genericization (see
  // gcc/cp/cp-gimplify.cc's cp_genericize_r); never actually
  // defined/linked anywhere. Builds the same contract_violation object
  // and invokes the same real, user-replaceable ::handle_contract_violation
  // the compiler already synthesizes for a bare (control-object-less)
  // contract -- letting a control object's operator() reach that same
  // handler without needing to construct a contract_violation itself
  // (its fields are private; duplicating the compiler's layout-compatible
  // "reinterpret" trick in library code is neither possible nor wanted).
  // Does nothing else: no severity decision, no termination -- that
  // stays entirely the calling control object's own responsibility.
  // Every field is exactly what the caller passes, including SEMANTIC: a
  // control object may itself transform the effective evaluation
  // semantic (e.g. downgrade enforce to observe under some runtime
  // condition) independent of the TU's own
  // -fcontract-evaluation-semantic=, so it is a real argument here, not
  // something the compiler fills in from the command line. Deliberately
  // NOT noexcept: the real, bare (control-object-less) contract path lets
  // a throwing ::handle_contract_violation propagate to its caller
  // unmodified, and this must match -- a calling control object may
  // itself want to catch that exception (e.g. to log it, or to terminate
  // with its own diagnostic instead of the default one), which is only
  // possible if the exception isn't forced into std::terminate() at this
  // call's own noexcept boundary first.
  void __d4324_invoke_violation_handler(assertion_kind kind,
					 evaluation_semantic semantic,
					 detection_mode mode,
					 const char* comment,
					 std::source_location loc,
					 const char* message = nullptr);

  // The plain-named public entry point control objects actually call;
  // forwards straight to __d4324_invoke_violation_handler above, which
  // the compiler recognizes by name during genericization. Ordinary
  // library code -- ends up inlined away at the call site, leaving only
  // the compiler-rewritten call behind. Not noexcept, for the same reason
  // as __d4324_invoke_violation_handler above.
  //
  // D4324 (paper reviewer feedback): takes an assertion_info bundling
  // the four fields __d4324_invoke_violation_handler still wants
  // separately, rather than four of this function's own five arguments
  // -- __d4324_invoke_violation_handler itself is left untouched (the
  // compiler recognizes and destructures its call by fixed argument
  // position, see gcc/cp/contracts.cc's maybe_replace_d4324_violation_
  // handler_call), so this is purely a library-side ergonomics change,
  // just unpacking INFO into the same five arguments it always forwarded.
  inline void
  invoke_violation_handler(const assertion_info& info, detection_mode mode)
  {
    __d4324_invoke_violation_handler(info.kind(), info.semantic(), mode,
				      info.comment(), info.location(),
				      info.message());
  }


  // -fcontract-symbolic-runtime-checks (~/gcc-axiom-contracts.md, "the
  // gem"): a symbolic postcondition's runtime establishment, and a
  // symbolic precondition's runtime consultation of that established
  // fact -- shared across translation units and threads. The _bool pair
  // is for a pred_fn(arg)/!pred_fn(arg)-shaped conjunct (KEY identifies
  // the predicate function, OBJ is the argument's runtime pointer
  // value); the _range pair is for a ptr->field OP const-shaped
  // conjunct (KEY identifies the field, OBJ is the pointer's runtime
  // value, [LO,HI) is the established/required bound, gated by
  // HAS_LO/HAS_HI). The compiler calls these directly
  // (build_cp_library_fn_ptr, gcc/cp/contracts.cc) rather than through
  // ordinary name lookup, so they are never meant to be called from
  // user code.
  void __contracts_symbolic_establish_bool(const void* key, const void* obj,
					    bool polarity) noexcept;
  bool __contracts_symbolic_check_bool(const void* key, const void* obj,
					bool polarity) noexcept;
  void __contracts_symbolic_establish_range(const void* key, const void* obj,
					     bool has_lo, long long lo,
					     bool has_hi, long long hi) noexcept;
  bool __contracts_symbolic_check_range(const void* key, const void* obj,
					 bool has_lo, long long lo,
					 bool has_hi, long long hi) noexcept;

  // -fcontract-symbolic-runtime-checks: called once at the start of the
  // destructor of any class that declares at least one symbolic member
  // predicate (see class_has_symbolic_predicate_p / begin_destructor_body,
  // gcc/cp/decl.cc), so that a fact established for OBJ doesn't survive
  // OBJ's own lifetime. Without this, a later object that happens to
  // reuse OBJ's address (most commonly automatic storage duration: two
  // sequential, non-overlapping local objects at the same stack offset)
  // would silently inherit a stale fact it never established itself.
  // Removes every record keyed by OBJ regardless of which predicate (KEY)
  // established it, since a single destructor call is the right place to
  // invalidate all of them at once, not just the one the compiler happens
  // to already know this class declares.
  void __contracts_symbolic_invalidate_object(const void* obj) noexcept;

  // -fcontract-symbolic-runtime-checks: a relation, a symbolic predicate
  // of N (2 to 4) arguments (KEY as for _bool).  Argument I is an
  // object's address AI when its size SI is 0, otherwise a value, SI
  // bytes at AI.  A relation with a value argument is established at the
  // call site, bound to SCOPE, a block-local token whose block's every
  // exit ends it (__contracts_symbolic_end_scope); one of objects only
  // has a null SCOPE and lives as long as its objects.
  void __contracts_symbolic_establish_relation(const void* key, unsigned n,
	const void* a0, __SIZE_TYPE__ s0, const void* a1, __SIZE_TYPE__ s1,
	const void* a2, __SIZE_TYPE__ s2, const void* a3, __SIZE_TYPE__ s3,
	bool polarity, const void* scope) noexcept;
  bool __contracts_symbolic_check_relation(const void* key, unsigned n,
	const void* a0, __SIZE_TYPE__ s0, const void* a1, __SIZE_TYPE__ s1,
	const void* a2, __SIZE_TYPE__ s2, const void* a3, __SIZE_TYPE__ s3,
	bool polarity) noexcept;
  void __contracts_symbolic_end_scope(const void* scope) noexcept;

  // -fcontract-symbolic-runtime-checks: called when an object's lifetime
  // ends -- on every exit from an automatic object's or a temporary's
  // scope, exceptional ones included, and before a delete-expression
  // frees its storage -- for any object of a type with a class that
  // declares a symbolic predicate anywhere inside it (see
  // type_has_symbolic_predicate_p, gcc/cp/decl.cc), whether or not its
  // destructor is trivial. Removes every record keyed by an address in
  // [OBJ, OBJ + SIZE): the object's own and its subobjects', at any
  // depth.
  void __contracts_symbolic_invalidate_range(const void* obj,
					     __SIZE_TYPE__ size) noexcept;

  // -fcontract-symbolic-runtime-checks: around a call that isn't conveyor
  // and is given a pointer or reference to such an object, which may
  // change it, as the static analysis assumes ("any other call that takes
  // its address"): __contracts_symbolic_epoch before the call, then
  // __contracts_symbolic_invalidate_range_before removes the records in
  // [OBJ, OBJ + SIZE) established no later than EPOCH -- before the call,
  // not by the callee's own postconditions.
  unsigned long long __contracts_symbolic_epoch() noexcept;
  void __contracts_symbolic_invalidate_range_before(const void* obj,
						    __SIZE_TYPE__ size,
						    unsigned long long epoch)
    noexcept;

  // Mirrors gcc/cp/contracts.cc's declare_terminate_wrapper /
  // __tu_terminate_wrapper, the existing P2900 workaround for GCC
  // PR121936 (see -fcontracts-conservative-ipa). default_control's and
  // mandatory's operator() are ordinary inline functions, visible and
  // inlinable in every TU that includes <contracts>; without this
  // wrapper, once a TU's cfg() constant-folds and the call to the
  // noreturn std::terminate() becomes unconditional, IPA is free to
  // infer that a caller reaching past the call proves the precondition
  // held -- sound only within the TU that compiled it, since cfg()
  // reflects that TU's own -fcontract-evaluation-semantic=, which may
  // legitimately differ between TUs (the same hazard P2900's own
  // mechanism exists to prevent from leaking across TU boundaries, e.g.
  // under LTO or via -fipa-cp-clone's per-call-site specialization).
  // Deliberately NOT noreturn itself, even though its body unconditionally
  // calls the noreturn std::terminate: noipa alone blocks IPA
  // from *analyzing* the body to infer noreturn-ness, but does not and
  // cannot override an explicit noreturn declared directly on the
  // function being called, so the wrapper must decline that declaration
  // too for callers to see it as possibly-returning.
  [[gnu::noipa]] inline void
  __d4324_terminate_wrapper() noexcept
  { std::terminate(); }

  // Compile-time diagnostics, following P2758 ("Emitting messages at
  // compile time").  These stand in for the std::constexpr_print_str,
  // std::constexpr_warning_str and std::constexpr_error_str that P2758
  // proposes; they are in std::contracts so that they do not clash with
  // those, and can be replaced by them once they exist.  The control types
  // below report a contract violation found during constant evaluation
  // with them, so a user-written control type can do the same.
  //
  // Outside constant evaluation they do nothing.  The diagnostic is
  // reported at the call.  An error makes the program ill-formed; when it
  // is reported by a control object's operator() for a contract, the
  // compiler treats the contract as violated with a terminating semantic,
  // as it does for a contract without a control object.

  // A message: a null-terminated string, or anything with data() and
  // size(), such as std::string_view.  (This header can't include
  // <string_view>.)
  class constexpr_message
  {
    const char* _M_data;
    __SIZE_TYPE__ _M_size;

  public:
    constexpr
    constexpr_message(const char* __s) noexcept
    : _M_data(__s), _M_size(__builtin_strlen(__s)) { }

    template<typename _Str>
      requires requires (const _Str& __s) {
	static_cast<const char*>(__s.data());
	static_cast<__SIZE_TYPE__>(__s.size());
      }
      constexpr
      constexpr_message(const _Str& __s) noexcept
      : _M_data(__s.data()), _M_size(__s.size()) { }

    constexpr const char*
    data() const noexcept { return _M_data; }

    constexpr __SIZE_TYPE__
    size() const noexcept { return _M_size; }
  };

  constexpr void
  constexpr_print_str(constexpr_message __msg) noexcept
  {
    if consteval
      { __builtin_constexpr_diag(0 | 16, "", __msg); }
  }

  // TAG is "" or letters, digits and underscores.
  constexpr void
  constexpr_warning_str(constexpr_message __tag,
			constexpr_message __msg) noexcept
  {
    if consteval
      { __builtin_constexpr_diag(1 | 16, __tag, __msg); }
  }

  constexpr void
  constexpr_error_str(constexpr_message __tag,
		      constexpr_message __msg) noexcept
  {
    if consteval
      { __builtin_constexpr_diag(2 | 16, __tag, __msg); }
  }

  // The control objects, and the "name"group literal.  Inline, so that
  // they are std::contracts:: names as well: 'using namespace
  // std::contracts::controls;' brings in just these, and
  // 'namespace SC = std::contracts::controls;' names them.
  inline namespace controls
  {

  // Whether the group a rule names, RULE, covers the group NAME: NAME
  // itself, or any group nested in it by a dotted suffix -- "lib" covers
  // "lib" and "lib.io", not "library".
  constexpr bool
  __group_matches(const char* __rule, const char* __name) noexcept
  {
    if (!__rule || !__name)
      return false;
    while (*__rule && *__rule == *__name)
      {
	++__rule;
	++__name;
      }
    return *__rule == '\0' && (*__name == '\0' || *__name == '.');
  }

  // The semantic for an assertion in group GROUP (none if null): the
  // first -fcontracts-group-evaluation-semantic= rule covering it, else
  // the configured one.  (A P3595 configuration's "group" rules have
  // already given INFO's semantic, through groups().)
  constexpr evaluation_semantic
  __group_semantic(const char* __group, assertion_static_info __info) noexcept
  {
    if (__group)
      for (const group_semantic_rule& __rule : __info.group_semantic_rules())
	if (__group_matches(__rule.group_name(), __group))
	  return __rule.semantic();
    return __info.semantic();
  }

  // What a default_control-like control object does for a violated
  // assertion whose semantic is SEM: report it, with MESSAGE (if not
  // null) alongside the comment, and terminate unless observing.
  constexpr void
  __default_fail(const assertion_context& __ctx, evaluation_semantic __sem,
		 const char* __message)
  {
    const char* __what = __message ? __message : __ctx.comment();
    if consteval
      {
	if (__sem == evaluation_semantic::observe)
	  constexpr_warning_str ("contract", __what);
	else
	  constexpr_error_str ("contract", __what);
      }
    else
      {
	// quick_enforce terminates at once, without the violation handler.
	if (__sem != evaluation_semantic::quick_enforce)
	  {
	    assertion_info __info{__ctx.comment(), __ctx.location(), __sem,
				  __ctx.kind()};
	    __info.set_message(__message);
	    invoke_violation_handler(__info, detection_mode::predicate_false);
	  }
	if (__sem != evaluation_semantic::observe)
	  __d4324_terminate_wrapper();
      }
  }

  // ... for an assertion, which it checks first.
  constexpr void
  __default_check(const assertion_context& __ctx, evaluation_semantic __sem,
		  const char* __message)
  {
    if (__ctx.check())
      return;
    __default_fail(__ctx, __sem, __message);
  }

  // ---- Printing run-time data simply, without <format> ----
  //
  // What the library's own control objects print an assertion's operands
  // with: integers, characters, bool, enumerations (as their underlying
  // value), pointers (in hex), nullptr, C strings, std::string and
  // std::string_view (of char), and floating-point values (round-trip
  // precision, through the C library's snprintf; in hex in a freestanding
  // build); anything else is printed as '?'.  <contracts>' control
  // objects print anything std::format can.

  // Text written into a fixed buffer, truncated if it doesn't fit; always
  // NUL-terminated.
  struct __contract_text
  {
    char* _M_cur;
    char* _M_end;	// the last byte, kept for the NUL

    void
    __put(char __c) noexcept
    {
      if (_M_cur != _M_end)
	*_M_cur++ = __c;
    }

    void
    __put(const char* __s, __SIZE_TYPE__ __n) noexcept
    {
      for (__SIZE_TYPE__ __i = 0; __i != __n; ++__i)
	__put(__s[__i]);
    }

    void
    __put(const char* __s) noexcept
    {
      while (*__s)
	__put(*__s++);
    }

    void
    __put_unsigned(unsigned long long __v, unsigned __base = 10) noexcept
    {
      char __digits[64];
      int __n = 0;
      do
	{
	  __digits[__n++] = "0123456789abcdef"[__v % __base];
	  __v /= __base;
	}
      while (__v);
      while (__n)
	__put(__digits[--__n]);
    }
  };

  // The integer types __simple_print prints as numbers (bool and char
  // are printed otherwise).
  template<typename _Tp>
    struct __is_simple_integer { static constexpr bool value = false; };
#define _GLIBCXX_SIMPLE_INTEGER(_Tp) \
  template<> struct __is_simple_integer<_Tp> { static constexpr bool value = true; };
  _GLIBCXX_SIMPLE_INTEGER(signed char)
  _GLIBCXX_SIMPLE_INTEGER(unsigned char)
  _GLIBCXX_SIMPLE_INTEGER(short)
  _GLIBCXX_SIMPLE_INTEGER(unsigned short)
  _GLIBCXX_SIMPLE_INTEGER(int)
  _GLIBCXX_SIMPLE_INTEGER(unsigned int)
  _GLIBCXX_SIMPLE_INTEGER(long)
  _GLIBCXX_SIMPLE_INTEGER(unsigned long)
  _GLIBCXX_SIMPLE_INTEGER(long long)
  _GLIBCXX_SIMPLE_INTEGER(unsigned long long)
  _GLIBCXX_SIMPLE_INTEGER(wchar_t)
#ifdef __cpp_char8_t
  _GLIBCXX_SIMPLE_INTEGER(char8_t)
#endif
  _GLIBCXX_SIMPLE_INTEGER(char16_t)
  _GLIBCXX_SIMPLE_INTEGER(char32_t)
#undef _GLIBCXX_SIMPLE_INTEGER

  template<typename _Tp>
    struct __is_char_string { static constexpr bool value = false; };
  template<typename _Traits, typename _Alloc>
    struct __is_char_string<basic_string<char, _Traits, _Alloc>>
    { static constexpr bool value = true; };
  template<typename _Traits>
    struct __is_char_string<basic_string_view<char, _Traits>>
    { static constexpr bool value = true; };

  inline void
  __simple_print_double(__contract_text& __out, double __v) noexcept
  {
#if _GLIBCXX_HOSTED
    char __buf[64];
    int __n = __builtin_snprintf(__buf, sizeof __buf, "%.*g",
				 __DBL_DECIMAL_DIG__, __v);
    if (__n > 0)
      __out.__put(__buf);
#else
    // C99's %a: exact, and needs no C library.
    unsigned long long __bits = __builtin_bit_cast(unsigned long long, __v);
    if (__bits >> 63)
      __out.__put('-');
    int __exp = int((__bits >> 52) & 0x7ff);
    unsigned long long __mant = __bits & ((1ull << 52) - 1);
    if (__exp == 0x7ff)
      {
	__out.__put(__mant ? "nan" : "inf");
	return;
      }
    __out.__put(__exp ? "0x1" : "0x0");
    if (__mant)
      {
	__out.__put('.');
	while (!(__mant & 0xf))
	  __mant >>= 4;
	__out.__put_unsigned(__mant, 16);
      }
    __out.__put('p');
    int __e = __exp ? __exp - 1023 : (__mant ? -1022 : 0);
    if (__e < 0)
      {
	__out.__put('-');
	__e = -__e;
      }
    else
      __out.__put('+');
    __out.__put_unsigned(unsigned(__e));
#endif
  }

  inline void
  __simple_print_long_double(__contract_text& __out, long double __v) noexcept
  {
#if _GLIBCXX_HOSTED
    char __buf[80];
    int __n = __builtin_snprintf(__buf, sizeof __buf, "%.*Lg",
				 __LDBL_DECIMAL_DIG__, __v);
    if (__n > 0)
      __out.__put(__buf);
#else
    __simple_print_double(__out, double(__v));
#endif
  }

  // Defined at the end of this header, after what it needs.
  template<typename _Tp>
    void
    __simple_print_string(__contract_text& __out, const _Tp& __v) noexcept;

  template<typename _Tp>
    void
    __simple_print(__contract_text& __out, const _Tp& __v) noexcept
    {
      if constexpr (__is_same(_Tp, bool))
	__out.__put(__v ? "true" : "false");
      else if constexpr (__is_same(_Tp, char))
	__out.__put(__v);
      else if constexpr (__is_same(_Tp, decltype(nullptr)))
	__out.__put("nullptr");
      else if constexpr (__is_same(_Tp, float) || __is_same(_Tp, double))
	__simple_print_double(__out, double(__v));
      else if constexpr (__is_same(_Tp, long double))
	__simple_print_long_double(__out, __v);
      else if constexpr (__is_enum(_Tp))
	__simple_print(__out, static_cast<__underlying_type(_Tp)>(__v));
      else if constexpr (__is_simple_integer<_Tp>::value)
	{
	  if constexpr (_Tp(-1) < _Tp(0))
	    {
	      long long __s = __v;
	      if (__s < 0)
		{
		  __out.__put('-');
		  __out.__put_unsigned(0ull - (unsigned long long)__s);
		}
	      else
		__out.__put_unsigned((unsigned long long)__s);
	    }
	  else
	    __out.__put_unsigned((unsigned long long)__v);
	}
      else if constexpr (__is_same(_Tp, const char*) || __is_same(_Tp, char*))
	{
	  if (__v)
	    __out.__put(__v);
	  else
	    __out.__put("nullptr");
	}
      else if constexpr (__is_pointer(_Tp))
	{
	  __out.__put("0x");
	  __out.__put_unsigned((unsigned long long)(__UINTPTR_TYPE__)__v, 16);
	}
      else if constexpr (__is_char_string<_Tp>::value)
	__simple_print_string(__out, __v);
      else
	__out.__put('?');
    }

  template<typename _Tp>
    void
    __simple_print_erased(__contract_text& __out, const void* __p) noexcept
    { __simple_print(__out, *static_cast<const _Tp*>(__p)); }

  // FMT, an assertion_static_info::operand_format() string, with each
  // {N} replaced by operand N, printed by PRINT[N], and {{ and }} by
  // { and }.
  inline void
  __format_operands(__contract_text& __out, const char* __fmt,
		    const void* const* __ops,
		    void (*const* __print)(__contract_text&, const void*),
		    __SIZE_TYPE__ __count) noexcept
  {
    for (const char* __p = __fmt; *__p; ++__p)
      {
	if ((__p[0] == '{' && __p[1] == '{') || (__p[0] == '}' && __p[1] == '}'))
	  {
	    __out.__put(*__p++);
	    continue;
	  }
	if (*__p == '{')
	  {
	    __SIZE_TYPE__ __n = 0;
	    const char* __q = __p + 1;
	    while (*__q >= '0' && *__q <= '9')
	      __n = __n * 10 + static_cast<__SIZE_TYPE__>(*__q++ - '0');
	    if (*__q == '}' && __q != __p + 1 && __n < __count)
	      {
		__print[__n](__out, __ops[__n]);
		__p = __q;
		continue;
	      }
	  }
	__out.__put(*__p);
      }
  }

  // The message a violation report gives, written into BUF (of size N):
  // MESSAGE (if not null), then the condition with the operands' values
  // (printed by PRINT) -- the condition's own text stays the comment.
  inline const char*
  __operand_message(char* __buf, __SIZE_TYPE__ __n,
		    const assertion_context& __ctx, const char* __message,
		    const void* const* __ops,
		    void (*const* __print)(__contract_text&, const void*),
		    __SIZE_TYPE__ __count)
  {
    // No format string (-fcontract-violation-information=none): no
    // values either.
    const char* __fmt = __ctx.static_info().operand_format();
    if (!__fmt)
      return __message;
    __contract_text __out{__buf, __buf + __n - 1};
    if (__message)
      {
	__out.__put(__message);
	__out.__put(": ");
      }
    __format_operands(__out, __fmt, __ops, __print, __count);
    *__out._M_cur = '\0';
    return __buf;
  }

  // ... with the operands printed simply.
  template<__SIZE_TYPE__ _Nm, typename... _Ops>
    const char*
    __simple_operand_message(char (&__buf)[_Nm], const assertion_context& __ctx,
			     const char* __message, const _Ops&... __ops) noexcept
    {
      const void* __ptrs[] = { static_cast<const void*>(__builtin_addressof(__ops))... };
      void (*const __print[])(__contract_text&, const void*)
	= { &__simple_print_erased<_Ops>... };
      return __operand_message(__buf, _Nm, __ctx, __message, __ptrs, __print,
			       sizeof...(_Ops));
    }

  // MESSAGE, then VALUES (the condition with its operands' values) if not
  // null: "MESSAGE: VALUES", or VALUES alone if MESSAGE is null.
  template<__SIZE_TYPE__ _Nm>
    const char*
    __message_with_values(char (&__buf)[_Nm], const char* __message,
			  const char* __values) noexcept
    {
      if (!__values)
	return __message;
      if (!__message)
	return __values;
      __contract_text __out{__buf, __buf + _Nm - 1};
      __out.__put(__message);
      __out.__put(": ");
      __out.__put(__values);
      *__out._M_cur = '\0';
      return __buf;
    }

  // For a report that has one string: TEXT, then VALUES in parentheses
  // if not null.
  template<__SIZE_TYPE__ _Nm>
    const char*
    __text_with_values(char (&__buf)[_Nm], const char* __text,
		       const char* __values) noexcept
    {
      if (!__values)
	return __text;
      __contract_text __out{__buf, __buf + _Nm - 1};
      __out.__put(__text);
      __out.__put(" (");
      __out.__put(__values);
      __out.__put(')');
      *__out._M_cur = '\0';
      return __buf;
    }

  // The size of the buffer a control object formats a violation's
  // message into, on the stack, on the violation path only.
  inline constexpr __SIZE_TYPE__ __contract_message_size = 512;

  // A range of at most one name: *_M_name, or none if it is null.
  struct __one_group
  {
    const char* const* _M_name;
    constexpr const char* const* begin() const noexcept { return _M_name; }
    constexpr const char* const* end() const noexcept
    { return _M_name + (*_M_name != nullptr); }
  };

  // A contract group's name, as "name"group writes it.  It is a control
  // object, default_control in that group, and what annotated takes to
  // tell a group from a message.
  struct group_name
  {
    const char* _M_name = nullptr;

    constexpr __one_group
    groups() const noexcept
    { return { &_M_name }; }

    constexpr bool
    is_ignored(assertion_static_info __info) const noexcept
    { return __group_semantic(_M_name, __info) == evaluation_semantic::ignore; }

    static constexpr bool
    constify(assertion_static_info) noexcept { return false; }
    static constexpr bool
    assumable(assertion_static_info) noexcept { return false; }

    constexpr void
    operator()(const assertion_context& __ctx) const
    { __default_check(__ctx, __group_semantic(_M_name, __ctx.static_info()),
		      nullptr); }

    // With the condition's operands: the message gives their values.
    template<typename... _Ops>
      constexpr void
      operator()(const assertion_context& __ctx, const _Ops&... __ops) const
      {
	if (__ctx.check())
	  return;
	const evaluation_semantic __sem
	  = __group_semantic(_M_name, __ctx.static_info());
	if consteval
	  {
	    __default_fail(__ctx, __sem, nullptr);
	  }
	else
	  {
	    if (__sem == evaluation_semantic::quick_enforce)
	      __default_fail(__ctx, __sem, nullptr);
	    else
	      {
  	    char __buf[__contract_message_size];
  	    __default_fail(__ctx, __sem,
  			   __simple_operand_message(__buf, __ctx, nullptr,
  						    __ops...));
	      }
	  }
      }
  };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wliteral-suffix"
  constexpr group_name
  operator""group(const char* __s, __SIZE_TYPE__) noexcept
  { return group_name{__s}; }
#pragma GCC diagnostic pop

  // A control object's optional group and message: the base of every
  // control object below, so that each can be written
  //   C("lib.io"group, "x must be positive"), C("lib.io"group),
  //   C("x must be positive"), or C().
  // groups() gives the group, if any, for a configuration's "group" rules
  // to match; __semantic gives the semantic its
  // -fcontracts-group-evaluation-semantic= rules give.
  struct __annotation
  {
    const char* _M_group = nullptr;
    const char* _M_message = nullptr;

    // Conveyor, so that conveyor code (e.g. __container_assert) can make
    // control objects.
    constexpr __annotation() noexcept _GLIBCXX_CONVEYOR = default;

    constexpr explicit
    __annotation(group_name __g) noexcept _GLIBCXX_CONVEYOR
    : _M_group(__g._M_name) { }

    constexpr
    __annotation(group_name __g, const char* __message) noexcept
      _GLIBCXX_CONVEYOR
    : _M_group(__g._M_name), _M_message(__message) { }

    constexpr explicit
    __annotation(const char* __message) noexcept _GLIBCXX_CONVEYOR
    : _M_message(__message) { }

    constexpr const char* group() const noexcept { return _M_group; }
    constexpr const char* message() const noexcept { return _M_message; }

    constexpr __one_group
    groups() const noexcept
    { return { &_M_group }; }

    constexpr evaluation_semantic
    __semantic(assertion_static_info __info) const noexcept
    { return __group_semantic(_M_group, __info); }
  };





  // Backs libstdc++'s own internal precondition checks (__glibcxx_assert,
  // active under _GLIBCXX_ASSERTIONS): unlike default_control/review
  // above, is_ignored is unconditionally false -- there is no "observe" or
  // "ignore" behavior for this type at all, ever, regardless of
  // -fcontract-evaluation-semantic= (the same is now true of mandatory
  // above too). What differs from mandatory is only *how* it reports/
  // terminates on failure, controlled by two separate, purely compile-time
  // macros:
  //
  //   _GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER, together with
  //   _GLIBCXX_ASSERTIONS itself, routes through the same replaceable
  //   ::handle_contract_violation as any other contract (always reporting
  //   evaluation_semantic::enforce, regardless of the TU's own semantic --
  //   this type has no other semantic to report). A handler that throws
  //   must never let that exception escape the assertion itself: it's
  //   caught and discarded, and the assertion terminates anyway -- a
  //   handler throwing is a bug in the replaced handler, not something
  //   recoverable here, and is treated exactly like an ordinary failure.
  //
  //   Otherwise (plain _GLIBCXX_ASSERTIONS, or neither macro -- these two
  //   macros decide only how this type fails, never whether it's active),
  //   this reconstructs __glibcxx_assert_fail's own existing diagnostic
  //   from the assertion_context instead of forwarding __FILE__/__LINE__/
  //   __PRETTY_FUNCTION__ directly: those, written literally in this
  //   operator()'s own body, would name this header's own location, not
  //   the real call site's -- ctx.location()/ctx.comment() are what the
  //   compiler actually populates per call site. A per-instance message
  //   (see below), when present, is reported in place of (not alongside)
  //   the predicate's own auto-derived source text, so the existing,
  //   unmodified 4-argument std::__glibcxx_assert_fail can be reused
  //   as-is -- no new overload, no new exported symbol.
  //
  // Like every other control type here, it takes an optional group and
  // message (__annotation): std::contracts::noexcept_assert_v below is
  // the shared, message-less instance for the ordinary case, and
  // noexcept_assert("some message") -- one per distinct message -- lets
  // one call site splice its own diagnostic text in place of the
  // predicate's own stringified condition.
  struct noexcept_assert : __annotation {
    using __annotation::__annotation;

    static constexpr bool
    is_ignored(assertion_static_info) noexcept { return false; }

    static constexpr bool
    constify(assertion_static_info) noexcept { return false; }
    static constexpr bool
    assumable(assertion_static_info) noexcept { return false; }

    constexpr void
    operator()(const assertion_context& __ctx) const noexcept
    {
      if (__ctx.check())
	return;
      __fail(__ctx, _M_message, nullptr);
    }

    // With the condition's operands: the message gives their values.
    template<typename... _Ops>
      constexpr void
      operator()(const assertion_context& __ctx, const _Ops&... __ops)
	const noexcept
      {
	if (__ctx.check())
	  return;
	if consteval
	  {
	    __fail(__ctx, _M_message, nullptr);
	  }
	else
	  {
	    char __buf[__contract_message_size];
	    __fail(__ctx, _M_message,
		   __simple_operand_message(__buf, __ctx, nullptr, __ops...));
	  }
      }

    // A violation, reported with MESSAGE, and VALUES, the condition with
    // its operands' values, if not null.
    constexpr void
    __fail(const assertion_context& __ctx, const char* __message,
	   const char* __values) const noexcept
    {
      const char* __text = __message ? __message : __ctx.comment();

#if defined(_GLIBCXX_ASSERTIONS) && defined(_GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER)
      if consteval
	{
	  constexpr_error_str ("contract", __text);
	}
      else
	{
	  try
	    {
	      // The handler gets the predicate as the comment and this
	      // object's message alongside it.
	      assertion_info __info(__ctx.comment(), __ctx.location(),
				    evaluation_semantic::enforce, __ctx.kind());
	      char __mbuf[__contract_message_size];
	      __info.set_message(__message_with_values(__mbuf, __message,
						       __values));
	      invoke_violation_handler(__info, detection_mode::predicate_false);
	    }
	  catch (...) { }
	  __d4324_terminate_wrapper();
	}
#else
      if consteval
	{
	  constexpr_error_str ("contract", __text);
	}
      else
	{
#ifdef _GLIBCXX_VERBOSE_ASSERT
	  char __buf[__contract_message_size];
	  std::__glibcxx_assert_fail (__ctx.location().file_name(),
				       __ctx.location().line(),
				       __ctx.location().function_name(),
				       __text_with_values(__buf, __text,
							  __values));
#else
	  __builtin_abort();
#endif
	}
#endif
    }
  };

  inline constexpr noexcept_assert noexcept_assert_v{};

  // Identical to noexcept_assert above in every respect but one: this
  // additionally declares is_conveyor() true, so a __glibcxx_assert
  // built on it (see _GLIBCXX_CONVEYOR_ASSERTIONS in bits/c++config)
  // routes through GCC's mandatory conveyor UB-freedom checks -- not
  // just the ordinary runtime/consteval check every control type here
  // performs, but a static guarantee that the condition text itself,
  // and every function reachable from it, is free of unprovable
  // division/modulo, array/pointer indexing, and pointer dereference.
  // A separate type, not a flag added to noexcept_assert itself:
  // is_conveyor is an *optional* trait (defaults to false when absent,
  // never part of the assertion_control concept above), so adding it
  // to the existing, already-shipped type would retroactively impose
  // these new mandatory restrictions on every current
  // _GLIBCXX_ASSERTIONS build -- this must be strictly opt-in.
  struct conveyor_assert : __annotation {
    using __annotation::__annotation;

    static constexpr bool
    is_ignored(assertion_static_info) noexcept { return false; }

    static constexpr bool
    constify(assertion_static_info) noexcept { return false; }
    static constexpr bool
    assumable(assertion_static_info) noexcept { return false; }

    static constexpr bool
    is_conveyor(assertion_static_info) noexcept { return true; }

    constexpr void
    operator()(const assertion_context& __ctx) const noexcept
    {
      if (__ctx.check())
	return;
      __fail(__ctx, _M_message, nullptr);
    }

    // With the condition's operands: the message gives their values.
    template<typename... _Ops>
      constexpr void
      operator()(const assertion_context& __ctx, const _Ops&... __ops)
	const noexcept
      {
	if (__ctx.check())
	  return;
	if consteval
	  {
	    __fail(__ctx, _M_message, nullptr);
	  }
	else
	  {
	    char __buf[__contract_message_size];
	    __fail(__ctx, _M_message,
		   __simple_operand_message(__buf, __ctx, nullptr, __ops...));
	  }
      }

    // A violation, reported with MESSAGE, and VALUES, the condition with
    // its operands' values, if not null.
    constexpr void
    __fail(const assertion_context& __ctx, const char* __message,
	   const char* __values) const noexcept
    {
      const char* __text = __message ? __message : __ctx.comment();

#if defined(_GLIBCXX_ASSERTIONS) && defined(_GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER)
      if consteval
	{
	  constexpr_error_str ("contract", __text);
	}
      else
	{
	  try
	    {
	      // The handler gets the predicate as the comment and this
	      // object's message alongside it.
	      assertion_info __info(__ctx.comment(), __ctx.location(),
				    evaluation_semantic::enforce, __ctx.kind());
	      char __mbuf[__contract_message_size];
	      __info.set_message(__message_with_values(__mbuf, __message,
						       __values));
	      invoke_violation_handler(__info, detection_mode::predicate_false);
	    }
	  catch (...) { }
	  __d4324_terminate_wrapper();
	}
#else
      if consteval
	{
	  constexpr_error_str ("contract", __text);
	}
      else
	{
#ifdef _GLIBCXX_VERBOSE_ASSERT
	  char __buf[__contract_message_size];
	  std::__glibcxx_assert_fail (__ctx.location().file_name(),
				       __ctx.location().line(),
				       __ctx.location().function_name(),
				       __text_with_values(__buf, __text,
							  __values));
#else
	  __builtin_abort();
#endif
	}
#endif
    }
  };

  inline constexpr conveyor_assert conveyor_assert_v{};

  // Identical to conveyor_assert above in every respect but one: this
  // additionally declares never_proven() true, exempting a contract_
  // assert naming it from ever being checked against ambient facts (no
  // diagnostic of its own, ever -- it still establishes itself as a
  // trusted fact for later code, same as conveyor_assert's own
  // contract_asserts do). __glibcxx_assert (bits/c++config) routes
  // through this, not conveyor_assert_v: the library's own internal,
  // defensive assertions are deep in generic code, evaluated in a
  // context where they are essentially never provable and were never
  // meant to be -- conveyor_assert_v itself stays untouched for anyone
  // relying on its own, already-shipped behavior.
  struct never_proven_conveyor : __annotation {
    using __annotation::__annotation;

    static constexpr bool
    is_ignored(assertion_static_info) noexcept { return false; }

    static constexpr bool
    constify(assertion_static_info) noexcept { return false; }
    static constexpr bool
    assumable(assertion_static_info) noexcept { return false; }

    static constexpr bool
    is_conveyor(assertion_static_info) noexcept { return true; }
    static constexpr bool
    never_proven(assertion_static_info) noexcept { return true; }

    constexpr void
    operator()(const assertion_context& __ctx) const noexcept
    {
      if (__ctx.check())
	return;
      __fail(__ctx, _M_message, nullptr);
    }

    // With the condition's operands: the message gives their values.
    template<typename... _Ops>
      constexpr void
      operator()(const assertion_context& __ctx, const _Ops&... __ops)
	const noexcept
      {
	if (__ctx.check())
	  return;
	if consteval
	  {
	    __fail(__ctx, _M_message, nullptr);
	  }
	else
	  {
	    char __buf[__contract_message_size];
	    __fail(__ctx, _M_message,
		   __simple_operand_message(__buf, __ctx, nullptr, __ops...));
	  }
      }

    // A violation, reported with MESSAGE, and VALUES, the condition with
    // its operands' values, if not null.
    constexpr void
    __fail(const assertion_context& __ctx, const char* __message,
	   const char* __values) const noexcept
    {
      const char* __text = __message ? __message : __ctx.comment();

#if defined(_GLIBCXX_ASSERTIONS) && defined(_GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER)
      if consteval
	{
	  constexpr_error_str ("contract", __text);
	}
      else
	{
	  try
	    {
	      // The handler gets the predicate as the comment and this
	      // object's message alongside it.
	      assertion_info __info(__ctx.comment(), __ctx.location(),
				    evaluation_semantic::enforce, __ctx.kind());
	      char __mbuf[__contract_message_size];
	      __info.set_message(__message_with_values(__mbuf, __message,
						       __values));
	      invoke_violation_handler(__info, detection_mode::predicate_false);
	    }
	  catch (...) { }
	  __d4324_terminate_wrapper();
	}
#else
      if consteval
	{
	  constexpr_error_str ("contract", __text);
	}
      else
	{
#ifdef _GLIBCXX_VERBOSE_ASSERT
	  char __buf[__contract_message_size];
	  std::__glibcxx_assert_fail (__ctx.location().file_name(),
				       __ctx.location().line(),
				       __ctx.location().function_name(),
				       __text_with_values(__buf, __text,
							  __values));
#else
	  __builtin_abort();
#endif
	}
#endif
    }
  };

  inline constexpr never_proven_conveyor never_proven_conveyor_v{};

  /// @cond undocumented
  // The control object of libstdc++'s container assertions under
  // _GLIBCXX_CONVEYOR_ASSERTIONS (debug/assertions.h): never_proven_conveyor,
  // except noexcept_assert -- an ordinary checked assertion -- for a
  // container whose allocator has a fancy pointer, whose operations can't be
  // conveyor.  An allocator declaring a pointer type that isn't a raw
  // pointer has one; std::allocator declares none.  _Tp is the container, or
  // a container adaptor, whose underlying container decides -- when it is
  // one of the library's own sequence containers (__std_adaptor_container_v,
  // specialized next to each of them).  Any other underlying container is
  // the user's, whose operations aren't known to be conveyor, so its
  // adaptor's assertions are ordinary checked ones.
  template<typename _Tp>
    inline constexpr bool __std_adaptor_container_v = false;

  template<typename _Tp>
    consteval bool
    __container_conveyor_p() noexcept
    {
      if constexpr (requires { typename _Tp::container_type; })
	{
	  if constexpr (__std_adaptor_container_v<typename _Tp::container_type>)
	    return __container_conveyor_p<typename _Tp::container_type>();
	  else
	    return false;
	}
      else if constexpr (requires { typename _Tp::allocator_type::pointer; })
	return __is_pointer(typename _Tp::allocator_type::pointer);
      else
	return true;
    }

  template<bool _Conveyor>
    struct __container_assert
    {
      static consteval never_proven_conveyor
      __make(const char* __msg = nullptr) noexcept _GLIBCXX_CONVEYOR
      { return never_proven_conveyor(__msg); }
    };

  template<>
    struct __container_assert<false>
    {
      static consteval noexcept_assert
      __make(const char* __msg = nullptr) noexcept _GLIBCXX_CONVEYOR
      { return noexcept_assert(__msg); }
    };

  // A container's declared precondition: proven, so the caller's code is
  // checked against it, for a container whose operations can be conveyor;
  // an ordinary checked precondition for one with a fancy pointer, whose
  // can't (__container_conveyor_p).
  template<bool _Conveyor>
    struct __container_pre
    {
      static consteval conveyor_assert
      __make() noexcept _GLIBCXX_CONVEYOR
      { return conveyor_assert{}; }
    };

  template<>
    struct __container_pre<false>
    {
      static consteval noexcept_assert
      __make() noexcept _GLIBCXX_CONVEYOR
      { return noexcept_assert(); }
    };

  // The precondition that such a container is an object: proven, or for
  // one with a fancy pointer, which can't be proven, never-proven.
  template<bool _Conveyor>
    struct __container_object_pre
    {
      static consteval conveyor_assert
      __make() noexcept _GLIBCXX_CONVEYOR
      { return conveyor_assert{}; }
    };

  template<>
    struct __container_object_pre<false>
    {
      static consteval never_proven_conveyor
      __make() noexcept _GLIBCXX_CONVEYOR
      { return never_proven_conveyor(); }
    };
  /// @endcond

  // Identical to conveyor_assert above, but declares analyzed_conveyor()
  // true instead of (in addition to being) is_conveyor(): forces
  // -fcontract-conveyor-proofs-equivalent analysis on for any contract
  // naming this, regardless of the command-line flag, with a lenient
  // outcome on an unprovable conjunct (a warning, not an error) -- see
  // this header's own analyzed_conveyor documentation above.
  struct analyzed_conveyor_t : __annotation {
    using __annotation::__annotation;

    static constexpr bool
    is_ignored(assertion_static_info) noexcept { return false; }

    static constexpr bool
    constify(assertion_static_info) noexcept { return false; }
    static constexpr bool
    assumable(assertion_static_info) noexcept { return false; }

    static constexpr bool
    analyzed_conveyor(assertion_static_info) noexcept { return true; }

    constexpr void
    operator()(const assertion_context& __ctx) const noexcept
    {
      if (__ctx.check())
	return;
      __fail(__ctx, _M_message, nullptr);
    }

    // With the condition's operands: the message gives their values.
    template<typename... _Ops>
      constexpr void
      operator()(const assertion_context& __ctx, const _Ops&... __ops)
	const noexcept
      {
	if (__ctx.check())
	  return;
	if consteval
	  {
	    __fail(__ctx, _M_message, nullptr);
	  }
	else
	  {
	    char __buf[__contract_message_size];
	    __fail(__ctx, _M_message,
		   __simple_operand_message(__buf, __ctx, nullptr, __ops...));
	  }
      }

    // A violation, reported with MESSAGE, and VALUES, the condition with
    // its operands' values, if not null.
    constexpr void
    __fail(const assertion_context& __ctx, const char* __message,
	   const char* __values) const noexcept
    {
      const char* __text = __message ? __message : __ctx.comment();

#if defined(_GLIBCXX_ASSERTIONS) && defined(_GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER)
      if consteval
	{
	  constexpr_error_str ("contract", __text);
	}
      else
	{
	  try
	    {
	      // The handler gets the predicate as the comment and this
	      // object's message alongside it.
	      assertion_info __info(__ctx.comment(), __ctx.location(),
				    evaluation_semantic::enforce, __ctx.kind());
	      char __mbuf[__contract_message_size];
	      __info.set_message(__message_with_values(__mbuf, __message,
						       __values));
	      invoke_violation_handler(__info, detection_mode::predicate_false);
	    }
	  catch (...) { }
	  __d4324_terminate_wrapper();
	}
#else
      if consteval
	{
	  constexpr_error_str ("contract", __text);
	}
      else
	{
#ifdef _GLIBCXX_VERBOSE_ASSERT
	  char __buf[__contract_message_size];
	  std::__glibcxx_assert_fail (__ctx.location().file_name(),
				       __ctx.location().line(),
				       __ctx.location().function_name(),
				       __text_with_values(__buf, __text,
							  __values));
#else
	  __builtin_abort();
#endif
	}
#endif
    }
  };

  inline constexpr analyzed_conveyor_t analyzed_conveyor_v{};

  // Identical to analyzed_conveyor_t above, but declares proven_
  // conveyor() true instead: same forced-on analysis, but strict --
  // an unprovable conjunct is also an error, not just a warning. See
  // this header's own proven_conveyor documentation above.
  struct proven_conveyor_t : __annotation {
    using __annotation::__annotation;

    static constexpr bool
    is_ignored(assertion_static_info) noexcept { return false; }

    static constexpr bool
    constify(assertion_static_info) noexcept { return false; }
    static constexpr bool
    assumable(assertion_static_info) noexcept { return false; }

    static constexpr bool
    proven_conveyor(assertion_static_info) noexcept { return true; }

    constexpr void
    operator()(const assertion_context& __ctx) const noexcept
    {
      if (__ctx.check())
	return;
      __fail(__ctx, _M_message, nullptr);
    }

    // With the condition's operands: the message gives their values.
    template<typename... _Ops>
      constexpr void
      operator()(const assertion_context& __ctx, const _Ops&... __ops)
	const noexcept
      {
	if (__ctx.check())
	  return;
	if consteval
	  {
	    __fail(__ctx, _M_message, nullptr);
	  }
	else
	  {
	    char __buf[__contract_message_size];
	    __fail(__ctx, _M_message,
		   __simple_operand_message(__buf, __ctx, nullptr, __ops...));
	  }
      }

    // A violation, reported with MESSAGE, and VALUES, the condition with
    // its operands' values, if not null.
    constexpr void
    __fail(const assertion_context& __ctx, const char* __message,
	   const char* __values) const noexcept
    {
      const char* __text = __message ? __message : __ctx.comment();

#if defined(_GLIBCXX_ASSERTIONS) && defined(_GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER)
      if consteval
	{
	  constexpr_error_str ("contract", __text);
	}
      else
	{
	  try
	    {
	      // The handler gets the predicate as the comment and this
	      // object's message alongside it.
	      assertion_info __info(__ctx.comment(), __ctx.location(),
				    evaluation_semantic::enforce, __ctx.kind());
	      char __mbuf[__contract_message_size];
	      __info.set_message(__message_with_values(__mbuf, __message,
						       __values));
	      invoke_violation_handler(__info, detection_mode::predicate_false);
	    }
	  catch (...) { }
	  __d4324_terminate_wrapper();
	}
#else
      if consteval
	{
	  constexpr_error_str ("contract", __text);
	}
      else
	{
#ifdef _GLIBCXX_VERBOSE_ASSERT
	  char __buf[__contract_message_size];
	  std::__glibcxx_assert_fail (__ctx.location().file_name(),
				       __ctx.location().line(),
				       __ctx.location().function_name(),
				       __text_with_values(__buf, __text,
							  __values));
#else
	  __builtin_abort();
#endif
	}
#endif
    }
  };

  inline constexpr proven_conveyor_t proven_conveyor_v{};

  struct assume : __annotation {
    using __annotation::__annotation;

    static constexpr bool
    is_ignored(assertion_static_info) noexcept
    { return true; }

    static constexpr bool
    constify(assertion_static_info) noexcept { return false; }
    static constexpr bool
    assumable(assertion_static_info) noexcept { return true; }

    // Never actually called, for the same reason as ignore::operator():
    // always ignored (unconditionally, unlike mandatory::is_ignored, which
    // is tied to the TU's semantic), so the predicate is always handed to
    // the optimizer as an assumption instead of being called through here.
    constexpr void
    operator()(const assertion_context&) const noexcept
    { }
  };

  inline constexpr assume assume_v{};

  } // inline namespace controls

} // namespace contracts

// D4324/P2680 (Dos Reis, "Contracts for C++: Prioritizing Safety"):
// std::is_object_address(p) is a compile-time-only proof predicate,
// usable inside a conveyor-checked contract predicate (see the
// 'conveyor' function-specifier and a control object's is_conveyor
// member). Unlike an ordinary predicate sub-expression it is never
// evaluated at runtime: the compiler either proves, via definite-
// assignment reasoning (and facts contributed by prior non-ignored
// preconditions/postconditions/contract_asserts), that the argument
// provably designates a real object, or the program is ill-formed
// (a hard compile-time error) -- there is no third, "checked but
// false" outcome, because there is no general way to check pointer
// validity at runtime in the first place.
//
// Declared (never defined) as a plain function template, exactly like
// std::contracts::base_contract<Base>() above: recognized by the
// compiler purely by which template a CALL_EXPR's callee is a
// specialization of (is_object_address_call_p in gcc/cp/contracts.cc),
// nothing in the parser needs to know about it. Lives directly in
// namespace std (not std::contracts), matching std::unreachable's own
// placement as a general-purpose utility rather than a contracts-
// specific one.
template <class _Tp>
bool is_object_address(_Tp*);

namespace contracts
{
inline namespace controls
{
  // __simple_print of a std::string or std::string_view.  data () and
  // size () are conveyor members, whose callers must prove the object
  // they're called on: a never-proven assertion states it, as
  // _GLIBCXX_CONVEYOR_TRUST does, whatever the library's mode.
  // (is_object_address is never evaluated at run time, so it never
  // reports, printing the string.)
  template<typename _Tp>
    void
    __simple_print_string(__contract_text& __out, const _Tp& __v) noexcept
    {
#ifdef __cpp_contract_control_objects
      contract_assert<never_proven_conveyor_v>
	(std::is_object_address(__builtin_addressof(__v)));
#endif
      __out.__put(__v.data(), __v.size());
    }
} // inline namespace controls
} // namespace contracts

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std

#endif // __cpp_lib_contracts
#endif // _GLIBCXX_BITS_CONTRACTS_H
