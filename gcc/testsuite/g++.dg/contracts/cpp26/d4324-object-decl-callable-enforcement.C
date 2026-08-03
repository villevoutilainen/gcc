// D4324: declaration-level contracts on callable-typed object
// declarations (see .claude/plans/stateless-jumping-shore.md) --
// call-site runtime enforcement: a pre<ctrl>/post<ctrl> clause
// attached directly to a function-pointer object is checked at every
// call site referencing that declared name, using that call's own
// actual arguments and (for post) actual result, entirely independent
// of whatever the pointer currently targets. Confirms: a satisfying
// call triggers no violation; a violating call does; each argument
// (and the real call's own result) is evaluated exactly once, shared
// between the checks and the real call.
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
  { if (ctx.check ()) return; seen = label; }		// returns -> continue
  static const char* seen;
};
const char* labeled::seen = nullptr;

inline constexpr labeled pre_ctrl{"pre violated"};
inline constexpr labeled post_ctrl{"post violated"};

int add (int a, int b) { return a + b; }
int bad_add (int a, int b) { return a - b; }

void (*fp_pre) (int a, int b) pre<pre_ctrl> (a > 0 && b > 0);
int (*fp_post) (const int a, const int b) post<post_ctrl> (r: r == a + b);

void noop (int, int) {}

int side_effect_count = 0;
int counted_arg (int v) { ++side_effect_count; return v; }

int main ()
{
  fp_pre = noop;

  // A satisfying call: no violation.
  fp_pre (1, 2);
  if (labeled::seen)
    __builtin_abort ();

  // A violating call: the handle's own precondition fires, entirely
  // independent of what fp_pre currently targets (noop has no
  // contract of its own at all).
  fp_pre (-1, 2);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Postcondition: a satisfying call.
  fp_post = add;
  int r = fp_post (1, 2);
  if (r != 3 || labeled::seen)
    __builtin_abort ();

  // Postcondition: a violating call -- bad_add computes a - b, which
  // fails "r == a + b".
  fp_post = bad_add;
  r = fp_post (1, 2);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "post violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  // Each argument is evaluated exactly once, shared between the
  // pre-check, the real call, and (for fp_post) the post-check.
  fp_pre = noop;
  side_effect_count = 0;
  fp_pre (counted_arg (1), counted_arg (2));
  if (side_effect_count != 2 || labeled::seen)
    __builtin_abort ();

  fp_post = add;
  side_effect_count = 0;
  r = fp_post (counted_arg (3), counted_arg (4));
  if (side_effect_count != 2 || r != 7 || labeled::seen)
    __builtin_abort ();

  return 0;
}
