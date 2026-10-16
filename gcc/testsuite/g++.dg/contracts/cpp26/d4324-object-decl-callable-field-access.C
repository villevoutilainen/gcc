// D4324: declaration-level contracts on callable-typed object
// declarations -- call-site enforcement through field access
// (obj.field(args) / ptr->field(args)).  Previously, a contract
// attached to a function-pointer, function-reference, or
// class-type-callable *field* was silently never checked when called
// through member access, because cp_build_function_call_vec
// (typeck.cc) and finish_call_expr's CLASS_TYPE_P branch
// (semantics.cc) both resolved the contract's "owner" via a plain
// DECL_P check on the raw callee expression -- but obj.field is a
// COMPONENT_REF, not a bare decl, so the check silently no-op'd.  A
// function-reference field has the extra wrinkle that accessing it
// produces an (implicit) INDIRECT_REF wrapping the COMPONENT_REF
// (REFERENCE_REF_P), which fnptr_contract_owner also needed to strip.
// Fixed by resolving the owner through fnptr_contract_owner (already
// used by the pointer-to-member-function .*/->* call path) at both
// call sites.
//
// Also locks in several shapes that already worked before this fix,
// since they're easy to accidentally break while touching this
// shared call-site-enforcement code: a static member reached via
// ClassName::staticmember(args) (never a COMPONENT_REF to begin
// with); a contracted pointer-to-member-function *value* itself being
// a field access (obj.*obj2.field / obj->*obj2.field), including the
// doubly-nested form where both operands are field accesses; and the
// cv-qualification-preserving re-verification (resolve_single_call_
// operator) for a const object calling through a class-type-callable
// field.
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

// --- plain function-pointer field ---

void add_void (int, int) { }
int add_ret (int a, int b) { return a + b; }
int sub_ret (int a, int b) { return a - b; }

struct FPObj {
  void (*fp) (int a, int b) pre<pre_ctrl> (a <= b);
  int (*fpr) (const int a, const int b) post<post_ctrl> (r: r == a + b);
};

// --- function-reference field ---

struct RefObj {
  void (&rf) (int a, int b) pre<pre_ctrl> (a <= b);
};

// --- class-type-callable (operator()) field ---

struct adder {
  int operator() (const int a, const int b) const { return a + b; }
};
struct suber {
  int operator() (const int a, const int b) const { return a - b; }
};

struct FunctorObj {
  adder add pre<pre_ctrl> (a, b: a > 0 && b > 0);
  adder add_post post<post_ctrl> (r, a, b: r == a + b);
};

// A separate object, carrying the "wrong" behavior under the same
// contract, since a plain functor field (unlike a std::function) can't
// be retargeted to a differently-typed callable at runtime.
struct FunctorObjBad {
  suber add_post post<post_ctrl> (r, a, b: r == a + b);
};

// --- static member, reached via ClassName::staticmember(args)
// (regression: already worked, no COMPONENT_REF involved) ---

struct StaticObj {
  static void (*sfp) (int a, int b) pre<pre_ctrl> (a <= b);
};
void (*StaticObj::sfp) (int a, int b) = add_void;

// --- pointer-to-member-function *value* being a field access
// (regression: already worked) ---

struct C {
  int field;
  int member (int x) const pre<pre_ctrl> (field >= 0 && x >= 0) { return field + x; }
};

struct Holder {
  int (C::*mf) (int x) const = &C::member;
};

struct NestedHolder {
  Holder h;
};

int main ()
{
  // Plain function-pointer field, via '.' -- precondition.
  FPObj fo{add_void, add_ret};
  fo.fp (1, 2);
  if (labeled::seen)
    __builtin_abort ();
  fo.fp (2, 1);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Plain function-pointer field, via '->' -- precondition.
  FPObj* pfo = &fo;
  pfo->fp (1, 2);
  if (labeled::seen)
    __builtin_abort ();
  pfo->fp (2, 1);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Plain function-pointer field -- postcondition, satisfying then
  // violating (retargeting the same field to a differently-behaving
  // function).
  int r = fo.fpr (1, 2);
  if (r != 3 || labeled::seen)
    __builtin_abort ();
  fo.fpr = sub_ret;
  r = fo.fpr (1, 2);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "post violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;
  r = pfo->fpr (3, 4);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "post violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Function-reference field, via '.' -- precondition.
  RefObj ro{add_void};
  ro.rf (1, 2);
  if (labeled::seen)
    __builtin_abort ();
  ro.rf (2, 1);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Class-type-callable (operator()) field, via '.' and '->'.
  FunctorObj fno{};
  fno.add (1, 2);
  if (labeled::seen)
    __builtin_abort ();
  fno.add (-1, 2);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  FunctorObj* pfno = &fno;
  pfno->add (1, 2);
  if (labeled::seen)
    __builtin_abort ();
  pfno->add (-1, 2);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  r = fno.add_post (1, 2);
  if (r != 3 || labeled::seen)
    __builtin_abort ();
  FunctorObjBad fno_bad{};
  r = fno_bad.add_post (1, 2);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "post violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // const object calling through a class-type-callable field: a
  // sanity check that the cv-qualification-preserving part of the
  // semantics.cc fix (leaving TREE_TYPE (contract_callee) alone for
  // resolve_single_call_operator) is actually exercised.
  const FunctorObj cfno{};
  cfno.add (1, 2);
  if (labeled::seen)
    __builtin_abort ();
  cfno.add (-1, 2);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Static member, reached via ClassName::staticmember(args).
  StaticObj::sfp (1, 2);
  if (labeled::seen)
    __builtin_abort ();
  StaticObj::sfp (2, 1);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Pointer-to-member-function value as a field access, '.*' and
  // '->*', satisfying then violating.
  C c{5};
  C* pc = &c;
  Holder hld{};
  if ((c.*hld.mf) (1) != 6 || labeled::seen)
    __builtin_abort ();
  if ((pc->*hld.mf) (1) != 6 || labeled::seen)
    __builtin_abort ();
  (c.*hld.mf) (-10);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;
  (pc->*hld.mf) (-10);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Doubly-nested: both the object operand and the
  // pointer-to-member-function value operand are field accesses.
  NestedHolder nh{};
  if ((c.*nh.h.mf) (1) != 6 || labeled::seen)
    __builtin_abort ();
  (c.*nh.h.mf) (-10);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  return 0;
}
