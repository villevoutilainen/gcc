// D4324: declaration-level contracts on callable-typed object
// declarations (see .claude/plans/stateless-jumping-shore.md), extended
// to pointer-to-member-function declarations -- a pre<ctrl>/post<ctrl>
// clause attached directly to a 'R (C::*)(Args...)'-typed object makes
// 'this' implicitly available in the condition, denoting whatever
// object the call is actually made through at each call site (there is
// no name to reference otherwise: unlike a class-type callable, whose
// own declared variable name is already in scope, a pointer-to-member-
// function's own declaration has no fixed associated object at all --
// it's supplied fresh by each '.*'/'->*' call). Confirms: 'this' (and
// an ordinary named parameter) resolve correctly in the condition; a
// satisfying call via both '.*' and '->*' triggers no violation; a
// violating call does; each call site is checked against *its own*
// object, not a fixed one; and checking happens correctly through
// virtual dispatch, independent of the compile-time static type of the
// object expression.
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

struct Base {
  int field;
  virtual int vmember (int x) { return field + x; }
};
struct Derived : Base {
  int vmember (int x) override { return field + x + 100; }
};

// 'this->field' and the ordinary named parameter 'x' both resolve in
// the same condition; the postcondition's own result identifier 'r'
// works alongside them exactly as it would for an ordinary function.
int (Base::*mfp)(int x) pre<pre_ctrl> (this->field >= 0 && x > 0)
			post<post_ctrl> (r: r >= 0) = &Base::vmember;

int
main ()
{
  Base b;
  b.field = 3;

  // A satisfying call via '.*': no violation.
  int r = (b.*mfp) (2);
  if (r != 5 || labeled::seen)
    __builtin_abort ();

  // A satisfying call via '->*', through a pointer: no violation.
  Base* bp = &b;
  r = (bp->*mfp) (4);
  if (r != 7 || labeled::seen)
    __builtin_abort ();

  // Precondition violated by the OBJECT's own state (this->field < 0),
  // not by the ordinary argument.
  Base neg;
  neg.field = -1;
  (neg.*mfp) (1);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Precondition violated by the ordinary argument, object state fine.
  (b.*mfp) (-1);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Each call site is checked against *its own* object: the same mfp,
  // invoked through two distinct objects in immediate succession, must
  // not carry state or a fixed receiver from one call to the next.
  Base ok1; ok1.field = 1;
  Base ok2; ok2.field = 2;
  (ok1.*mfp) (1);
  if (labeled::seen)
    __builtin_abort ();
  (ok2.*mfp) (1);
  if (labeled::seen)
    __builtin_abort ();

  // Virtual dispatch: mfp is called through a Base* that actually
  // refers to a Derived -- 'this' in the condition must denote the
  // real (Derived) object, and the real call must dispatch virtually,
  // independent of the pointer's own static (Base) type.
  Derived d;
  d.field = 10;
  Base* dp = &d;
  r = (dp->*mfp) (5);
  if (r != 115 || labeled::seen)		// Derived::vmember: 10+5+100
    __builtin_abort ();

  return 0;
}
