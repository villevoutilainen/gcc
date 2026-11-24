// D4324, QoI: P3595R0's "dynamic" output expands to a control object, so
// it needs -fcontract-control-objects.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{},"output":{"semantic":"enforce","dynamic":{"name":"f"}}}\]} }
// { dg-error "'dynamic' needs '-fcontract-control-objects'" "" { target *-*-* } 0 }

int f (int x) pre (x > 0);
