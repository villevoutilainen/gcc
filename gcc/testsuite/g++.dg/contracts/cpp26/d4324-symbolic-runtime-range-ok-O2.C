// D4324: d4324-symbolic-runtime-range-ok.C optimized.  At -O1 and above a
// two-sided range 'x >= 40 && x < 100' was folded into one unsigned
// comparison before the symbolic run-time check read its conjuncts,
// and was rejected as an unsupported conjunct (P4445 BUG-27).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks -O2" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "d4324-symbolic-runtime-range-ok.C"
