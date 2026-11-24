// D4324: a P3595 configuration entry with a "control" can't match
// "constexpr": the control object is chosen for an assertion (or, with
// "caller", for its caller-side checks), not for each evaluation.  An
// "output" needs a "semantic", a "control", a "dynamic" or a "group".
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"constexpr":true},"output":{"control":"std::contracts::default_v"}},{"match":{"kind":"pre"},"output":{}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }
// { dg-error "an entry with a 'control' can't match 'constexpr'" "" { target *-*-* } 0 }
// { dg-error "'output' needs a 'semantic', a 'control', a 'dynamic' or a 'group'" "" { target *-*-* } 0 }

int f (int x) pre (x > 0);
