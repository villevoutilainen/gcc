// D4324: a P3595 configuration whose "control" doesn't name a constexpr
// control object is an error at an assertion the entry matches (once per
// name); an assertion it doesn't match is unaffected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"namespace":"a"},"output":{"control":"nope::v"}},{"match":{"namespace":"b"},"output":{"control":"b::not_constexpr"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace a
{
  int f (int x) pre (x > 0); // { dg-error "'nope::v', the 'control' of a matching contract configuration entry, does not name a 'constexpr' control object" }
  int g (int x) pre (x > 0);
}

namespace b
{
  std::contracts::default_control not_constexpr;
  int h (int x) pre (x > 0); // { dg-error "'b::not_constexpr', the 'control' of a matching" }
}

int unmatched (int x) pre (x > 0);
