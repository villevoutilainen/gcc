// D4324/P3400: operator| combines two labels into one control object
// whose is_ignored/constify/assumable/operator() (via label_base) work
// out correctly when the combination is well-formed (no disjoint
// allowed_semantics, no overlapping dimensions).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contract_labels>
namespace P3400 = std::contracts::P3400;

// terminating (allowed_semantics-only) + audit (identification-only):
// no facet overlap at all, trivially compatible.
int f (int x) pre<P3400::terminating | P3400::audit>(x > 0) { return x; }

// terminating + always_enforce: both restrict to {enforce,
// quick_enforce} (intersecting to the same nonempty set), and
// always_enforce's compute_semantic forces enforce, which is already
// within that intersection -- well-formed.
int g (int x) pre<P3400::terminating | P3400::always_enforce>(x > 0) { return x; }

int
main ()
{
  if (f (1) != 1 || g (1) != 1)
    __builtin_abort ();
  return 0;
}
