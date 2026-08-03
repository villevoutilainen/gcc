// D4324: declaration-level contracts on callable-typed object
// declarations, extended to class-type callables (see
// .claude/plans/stateless-jumping-shore.md) -- call-site runtime
// enforcement: a pre<ctrl>/post<ctrl> clause attached directly to a
// class-type callable object (an ordinary functor, or a
// std::function) is checked at every call site referencing that
// declared name, using that call's own actual arguments and (for
// post) actual result -- exactly like the already-shipped function-
// pointer case.  Confirms: a satisfying call triggers no violation; a
// violating call does; each argument (and the real call's own result)
// is evaluated exactly once, shared between the checks and the real
// call; and this all works identically whether the callable is an
// ordinary functor or a std::function.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <functional>

namespace sc = std::contracts;

struct labeled {
  const char* label;
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (ctx.check ()) return; seen = label; }		// returns -> continue
  static const char* seen;
};
const char* labeled::seen = nullptr;

inline constexpr labeled pre_ctrl{"pre violated"};
inline constexpr labeled post_ctrl{"post violated"};

// A value parameter used in a postcondition must itself be declared
// const (P2900 constification) -- harmless for the pre<>-only uses
// below too, since top-level const on a by-value parameter doesn't
// affect the function's own type at all.
struct adder {
  int operator() (const int a, const int b) const { return a + b; }
};
// A fixed functor with the "wrong" behavior, used to exercise a
// postcondition violation: a plain functor's own operator() can't be
// reassigned at runtime the way a std::function can, so this is a
// separate, deliberately-incorrect object with its own contract,
// rather than the same object retargeted.
struct suber {
  int operator() (const int a, const int b) const { return a - b; }
};

// Ordinary functor objects.
adder add_functor pre<pre_ctrl> (a > 0 && b > 0);
adder add_post_functor post<post_ctrl> (r: r == a + b);
suber sub_post_functor post<post_ctrl> (r: r == a + b);

// std::function objects -- its own operator()'s real parameter names
// aren't a stable, guessable spelling, so name them explicitly via a
// binder list (result name first, then the parameters, positionally).
std::function<int (int, int)> add_fn pre<pre_ctrl> (a, b: a > 0 && b > 0);
std::function<int (int, int)> add_post_fn post<post_ctrl> (r, a, b: r == a + b);

int side_effect_count = 0;
int counted_arg (int v) { ++side_effect_count; return v; }

int main ()
{
  // Ordinary functor -- precondition: a satisfying call, then a
  // violating one (argument-dependent, so the same fixed functor
  // object works for both).
  add_functor (1, 2);
  if (labeled::seen)
    __builtin_abort ();
  add_functor (-1, 2);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Ordinary functor -- postcondition: a satisfying call (adder always
  // computes a + b), then a violating one (suber computes a - b).
  int r = add_post_functor (1, 2);
  if (r != 3 || labeled::seen)
    __builtin_abort ();
  r = sub_post_functor (1, 2);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "post violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // std::function -- precondition.
  add_fn = adder{};
  add_fn (1, 2);
  if (labeled::seen)
    __builtin_abort ();
  add_fn (-1, 2);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // std::function -- postcondition, satisfying then violating,
  // entirely independent of what the std::function currently targets
  // (unlike the plain functor case above, a std::function *can* be
  // retargeted at runtime).
  add_post_fn = adder{};
  r = add_post_fn (1, 2);
  if (r != 3 || labeled::seen)
    __builtin_abort ();
  add_post_fn = suber{};
  r = add_post_fn (1, 2);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "post violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Each argument is evaluated exactly once, shared between the
  // pre-check, the real call, and (for the post-checked objects) the
  // post-check.
  side_effect_count = 0;
  add_functor (counted_arg (1), counted_arg (2));
  if (side_effect_count != 2 || labeled::seen)
    __builtin_abort ();

  add_post_fn = adder{};
  side_effect_count = 0;
  r = add_post_fn (counted_arg (3), counted_arg (4));
  if (side_effect_count != 2 || r != 7 || labeled::seen)
    __builtin_abort ();

  return 0;
}
