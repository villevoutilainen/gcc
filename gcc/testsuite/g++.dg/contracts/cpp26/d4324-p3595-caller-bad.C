// D4324, QoI: an invalid "caller" in a P3595 configuration.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"caller":1},"output":{"semantic":"observe"}},{"match":{"caller":{"function":"f"}},"output":{"semantic":"observe"}}\]} }
// { dg-error "'caller' must be a boolean or a JSON object" "" { target *-*-* } 0 }
// { dg-error "unknown key 'function'" "" { target *-*-* } 0 }
