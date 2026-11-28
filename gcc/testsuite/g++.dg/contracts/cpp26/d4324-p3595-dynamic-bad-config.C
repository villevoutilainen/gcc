// D4324, QoI: errors in P3595R0's "dynamic" output.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{},"output":{"control":"x::v","dynamic":{"name":"f"}}},{"match":{"constexpr":false},"output":{"dynamic":{"name":"f"}}},{"match":{},"output":{"dynamic":{}}},{"match":{},"output":{"dynamic":{"name":"f","linkage":"Ada"}}},{"match":{},"output":{"dynamic":{"name":"f","provideweak":1}}},{"match":{},"output":{"dynamic":"f"}},{"match":{"kind":"pre"},"output":{"dynamic":{"name":"g"}}},{"match":{"kind":"post"},"output":{"semantic":"observe","dynamic":{"name":"g"}}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }
// { dg-error "an 'output' cannot have both a 'control' and a 'dynamic'" "" { target *-*-* } 0 }
// { dg-error "an entry with a 'dynamic' cannot match 'constexpr'" "" { target *-*-* } 0 }
// { dg-error "'dynamic' needs a 'name'" "" { target *-*-* } 0 }
// { dg-error "'linkage' must be 'C' or 'C\\+\\+'" "" { target *-*-* } 0 }
// { dg-error "'provideweak' must be a boolean" "" { target *-*-* } 0 }
// { dg-error "'dynamic' must be a JSON object" "" { target *-*-* } 0 }
// { dg-error "the entries naming the 'dynamic' function 'g' must give it the same 'linkage', 'semantic' and 'provideweak'" "" { target *-*-* } 0 }

#include <contracts>

int f (int x) pre (x > 0);
