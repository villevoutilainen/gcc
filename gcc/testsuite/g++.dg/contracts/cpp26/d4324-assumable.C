// D4324: a guaranteed-enforced control type whose assumable member is true
// lets the optimizer treat an ignored predicate as an assumption, so a
// downstream operation can be simplified.  At the ignore configuration the
// predicate is handed to the optimizer (no runtime evaluation), and a later
// branch that contradicts it is eliminated.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore -O2 -fdump-tree-optimized" }

namespace std {
namespace contracts {
enum class evaluation_config : unsigned {
  ignore = 0, observe = 1, enforce = 2, quick_enforce = 3
};
}
}

struct mandatory {
  static constexpr bool is_ignored (std::contracts::evaluation_config c)
  { return c == std::contracts::evaluation_config::ignore; }
  static constexpr bool constify = false;
  static constexpr bool assumable = true;
};

void sink (int);

int f (int x) pre<mandatory>(x > 5)
{
  if (x <= 5)
    sink (x);		// dead: the predicate x > 5 is assumed
  return x;
}

// The dead branch guarded by the negation of the assumed predicate is gone.
// { dg-final { scan-tree-dump-not "sink" "optimized" } }
