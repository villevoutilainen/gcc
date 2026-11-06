// D4324: -fsyntax-only never lowers a function, so the GIMPLE engine
// never runs: say so rather than accept silently what compiling would
// reject (P4445 BUG-39).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple -fsyntax-only" }
// { dg-warning "'-fsyntax-only' doesn't run the GIMPLE contract analysis engine; '-fcontract-analysis-engine=gimple' checks nothing" "" { target *-*-* } 0 }

int add (int a, int b) conveyor { return a + b; }
