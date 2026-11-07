// D4324: a malformed P3595 contract configuration is an error.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"knd":"pre"},"output":{"semantic":"observe"}},{"match":{"kind":"pre"},"output":{"semantic":"maybe"}},{"match":{}},{"match":{"constexpr":1},"output":{"semantic":"ignore"}}\]} }
// { dg-additional-options "-fcontract-configuration=nope" }
// { dg-additional-options "-fcontract-configuration-file=does-not-exist.json" }
// { dg-error "unknown key 'knd'" "" { target *-*-* } 0 }
// { dg-error "'output' needs a 'semantic'" "" { target *-*-* } 0 }
// { dg-error "an entry needs an 'output' object" "" { target *-*-* } 0 }
// { dg-error "'constexpr' must be a boolean" "" { target *-*-* } 0 }
// { dg-error "invalid JSON in contract configuration" "" { target *-*-* } 0 }
// { dg-error "cannot open contract configuration file 'does-not-exist.json'" "" { target *-*-* } 0 }
