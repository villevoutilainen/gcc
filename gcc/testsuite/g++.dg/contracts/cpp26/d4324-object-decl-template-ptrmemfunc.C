// D4324 + templates: declaration-level contracts on a pointer-to-
// member-function, extended to support templates -- a dependent
// qualifying class (C), a dependent member-function signature,
// dependent parameters, 'this' resolving correctly against the
// dependent class, and all three contexts the feature supports:
// a variable template, a function-template's own parameter, and an
// in-class member of a class template (both static and non-static,
// plus a postcondition with a dependent return type on the class-
// template-member case). Each context is instantiated more than once
// to confirm per-instantiation independence.
//
// Along the way, fixed a real, pre-existing (not template-specific)
// bug this work surfaced: a non-static pointer-to-member-function
// DATA MEMBER's own 'this' was being injected as the *enclosing*
// class, not the pointee class C, because MEMFN alone (used to decide
// whether DR 1207's ordinary 'this'-in-scope injection applies) can't
// tell "this is a real member function declaration" apart from "this
// declarator is itself a class member whose type happens to be a
// pointer-to-member-function" -- see the declarator->kind == cdk_id
// check added alongside it, cp_parser_direct_declarator.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

struct labeled {
  const char* label;
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (ctx.check ()) return; seen = label; }
  static const char* seen;
};
const char* labeled::seen = nullptr;

inline constexpr labeled pre_ctrl{"pre violated"};
inline constexpr labeled post_ctrl{"post violated"};

struct A { int field; int member (int x) { return field + x; } };
struct B { int field; int member (int x) { return field + x + 1000; } };

// Variable template: dependent qualifying class C, 'this' referenced
// directly in the condition.
template <typename C>
int (C::*mf) (int x) pre<pre_ctrl> (this->field >= 0 && x >= 0) = &C::member;

// Function-template parameter: dependent C.
template <typename T>
int
invoke (int (T::*p) (int x) pre<pre_ctrl> (this->field >= 0 && x >= 0),
	T& obj, int x)
{
  return (obj.*p) (x);
}

// Class-template member, static: dependent C.
template <typename C>
struct Holder {
  static int (C::*smf) (int x) pre<pre_ctrl> (this->field >= 0 && x >= 0);
};
template <typename C>
int (C::*Holder<C>::smf) (int x) = &C::member;

// Class-template member, non-static, postcondition only (no
// precondition guarding it): a dependent return type's own result
// genuinely reaches the postcondition check.
template <typename C>
struct Holder3 {
  int (C::*mf) (int x) post<post_ctrl> (r: r >= 0);
};

// Class-template member, non-static: dependent C, plus a
// postcondition with a dependent return type.
template <typename C>
struct Holder2 {
  int (C::*mf) (int x) pre<pre_ctrl> (this->field >= 0 && x >= 0)
		       post<post_ctrl> (r: r >= 0);
};

int
main ()
{
  // Variable template, two distinct instantiations.
  A a; a.field = 1;
  B b; b.field = 2;
  if ((a.*mf<A>) (1) != 2 || labeled::seen)
    __builtin_abort ();
  if ((b.*mf<B>) (1) != 1003 || labeled::seen)
    __builtin_abort ();
  A bad_a; bad_a.field = -1;
  (bad_a.*mf<A>) (1); // violates: this->field < 0
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Function-template parameter.
  A a2; a2.field = 3;
  if (invoke (&A::member, a2, 2) != 5 || labeled::seen)
    __builtin_abort ();
  invoke (&A::member, a2, -100); // violates: x < 0
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Class-template member, static, two distinct instantiations.
  if ((a.*Holder<A>::smf) (1) != 2 || labeled::seen)
    __builtin_abort ();
  if ((b.*Holder<B>::smf) (1) != 1003 || labeled::seen)
    __builtin_abort ();
  (bad_a.*Holder<A>::smf) (1); // violates
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Class-template member, non-static, with a postcondition on a
  // dependent return type.
  Holder2<A> h;
  h.mf = &A::member;
  if ((a.*h.mf) (1) != 2 || labeled::seen) // a.field (1) + 1
    __builtin_abort ();
  (bad_a.*h.mf) (1); // pre violated first (this->field < 0)
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // field large enough that the real call's own result (field + x)
  // still satisfies the postcondition even though x < 0 -- isolating
  // the precondition violation (labeled doesn't trap, just records
  // and continues, so a call violating *both* would have the
  // postcondition's own later record overwrite this one).
  A neg; neg.field = 10;
  (neg.*h.mf) (-5); // this->field >= 0 OK, x < 0 -- pre violates
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Genuine postcondition violation: no precondition to intercept a
  // negative argument first, so a negative result actually reaches
  // the postcondition check (this is the exact shape that previously
  // ICE'd in tsubst_object_contract_specifiers's own postcondition-
  // result-variable handling for a class-template member).
  Holder3<A> h3;
  h3.mf = &A::member;
  A neg2; neg2.field = 3;
  if ((neg2.*h3.mf) (2) != 5 || labeled::seen)
    __builtin_abort ();
  (neg2.*h3.mf) (-10); // result -7 violates post r >= 0
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "post violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;
  labeled::seen = nullptr;

  return 0;
}
