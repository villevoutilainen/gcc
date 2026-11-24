// D4324: the control object a P3595 configuration's "caller" entry
// chooses for a redundant caller-side check must constify the condition
// as the declaration's control object did: the condition was parsed once,
// with that one.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"caller":true,"function":"f"},"output":{"control":"constifying_v"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

struct constifying
{
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return true; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void operator() (const sc::assertion_context&) const {}
};
inline constexpr constifying constifying_v {};

int f (int x) pre (x > 0); // { dg-error "must constify its condition as the declaration's does" }

int g () { return f (1); }
