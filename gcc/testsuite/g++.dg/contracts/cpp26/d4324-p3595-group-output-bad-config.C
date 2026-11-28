// D4324, QoI: a P3595 configuration's "group" output belongs to an
// assertion: an entry with one can't match "constexpr" or "caller".
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"constexpr":true},"output":{"group":"a"}},{"match":{"caller":true},"output":{"group":"a"}},{"match":{},"output":{"group":""}}\]} }
// { dg-error "an entry with a 'group' output cannot match 'constexpr' or 'caller'" "" { target *-*-* } 0 }
// { dg-error "'group' in contract configuration .* must be a non-empty string" "" { target *-*-* } 0 }

int f (int x) pre (x > 0);
