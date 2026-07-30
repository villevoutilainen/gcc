// D4324: __glibcxx_requires_subscript's condition mixes a parameter
// substituted from the real call site (_N) with literal text written
// in the macro's own body (< this->size()) -- two different macro-
// expansion origins, which defeats noexcept_assert_v's own "recover
// the real source text" fallback (CONTRACT_COMMENT's
// get_source_text_between needs a single, contiguous range in one
// file), producing a pretty-printed, fully-qualified dump of the
// resolved tree instead of the actual written condition.
// __glibcxx_assert_msg supplies an explicit message instead, built via
// preprocessor stringification of just the real call-site tokens
// concatenated with literal text -- this confirms the diagnostic for a
// real, ordinary out-of-bounds vector access matches the exact,
// pre-contracts text, not a resolved-tree dump.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-shouldfail "genuine out-of-bounds access, contracts routes through __glibcxx_assert_msg" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#define _GLIBCXX_ASSERTIONS
#include <vector>

int main ()
{
  std::vector<int> v{1, 2, 3};
  return v[10];
}

// { dg-output "Assertion '__n < this->size\\(\\)' failed" }
