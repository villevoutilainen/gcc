// D4324: the control objects and the "name"group literal are in the
// inline namespace std::contracts::controls: still std::contracts names,
// brought in alone by 'using namespace std::contracts::controls;', and
// named through an alias of it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

// Still std::contracts names.
int a (int x) pre<std::contracts::default_v>(x > 0) { return x; }
int b (int x) pre<std::contracts::annotated ("m")>(x > 0) { return x; }

// Through an alias.
namespace SC = std::contracts::controls;
int c (int x) pre<SC::review_v>(x > 0) { return x; }
int d (int x) pre<SC::annotated ("m")>(x > 0) { return x; }
static_assert (__is_same (SC::default_control, std::contracts::default_control));

namespace only_controls
{
  using namespace std::contracts::controls;
  int e (int x) pre<annotated ("lib.io"group, "m")>(x > 0) { return x; }
  int f (int x) pre<mandatory_v>(x > 0) { return x; }
  // Not the rest of std::contracts.
  assertion_kind k; // { dg-error "'assertion_kind' does not name a type" }
}

namespace all_contracts
{
  using namespace std::contracts;
  int g (int x) pre<annotated ("lib.io"group)>(x > 0) { return x; }
  assertion_kind k = assertion_kind::pre;
}
