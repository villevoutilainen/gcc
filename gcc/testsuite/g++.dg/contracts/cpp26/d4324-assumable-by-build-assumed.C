// D4324 (implementation extension: assumable is not in P4324R0): a control
// object can be assumed in some builds and only ignored in others, chosen
// by the build.  Here the build gives the object's group entry
// "mylib.assume" the ignore semantic, so the predicate is assumed: the
// branch guarded by its negation is removed.  See
// d4324-assumable-by-build-ignored.C for the other build.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontracts-group-evaluation-semantic=mylib.assume:ignore -O2 -fdump-tree-optimized" }

#include <contracts>

namespace sc = std::contracts;

// Assumed when the build gives the group entry "mylib.assume" the ignore
// semantic; otherwise it follows the configured semantic, and an ignored
// assertion is just ignored.
constexpr bool
same (const char *a, const char *b)
{
  for (; *a && *a == *b; ++a, ++b)
    ;
  return *a == *b;
}

struct assume_by_build
{
  static constexpr bool
  assume (sc::assertion_static_info i)
  {
    for (const auto &r : i.group_semantic_rules ())
      if (same (r.group_name (), "mylib.assume"))
	return r.semantic () == sc::evaluation_semantic::ignore;
    return false;
  }
  static constexpr bool is_ignored (sc::assertion_static_info i)
  { return assume (i) || i.semantic () == sc::evaluation_semantic::ignore; }
  static constexpr bool assumable (sc::assertion_static_info i)
  { return assume (i); }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  void operator() (const sc::assertion_context &c) const
  {
    if (!c.check ())
      sc::invoke_violation_handler (c.info (), sc::detection_mode::predicate_false);
  }
};

void sink (int);

int f (int x) pre<assume_by_build{}> (x > 5)
{
  if (x <= 5)
    sink (x);
  return x;
}

// { dg-final { scan-tree-dump-not "sink" "optimized" } }
