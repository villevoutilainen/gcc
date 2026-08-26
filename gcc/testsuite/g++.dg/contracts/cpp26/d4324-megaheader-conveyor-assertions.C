// D4324: redundant copy of libstdc++-v3/testsuite/17_intro/headers/
// c++2026/stdc++_conveyor_assertions.cc, kept here too so a routine
// "dg.exp=*contracts*" compiler-testsuite run also exercises the whole-
// library <bits/stdc++.h> conveyor-assertions sweep -- the libstdc++
// testsuite copy is only run on request (via libstdc++-v3/testsuite's
// own check-DEJAGNU/conformance.exp), not as part of this directory's
// own routine verification bar, so a contracts.cc engine regression
// here could otherwise go unnoticed until the next explicit library
// sweep. See that file's own comment for the full rationale; update
// both files together if either one's expected outcome changes.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects" }
//
// One known, deferred gap: is_object_address can't compose through
// pointer ARITHMETIC/INDEXING (e.g. std::barrier's own __state
// [__current].__tickets[__round], or __unicode::_Utf_iterator's own
// array-indexed internal buffer), only through 'this'-based field
// access -- see stdc++_conveyor_assertions.cc's own comment for the
// full explanation. Remove this dg-xfail-if once that engine gap is
// closed.
// { dg-xfail-if "is_object_address can't compose through pointer indexing" { *-*-* } }

#include <bits/stdc++.h>

int main() { return 0; }
