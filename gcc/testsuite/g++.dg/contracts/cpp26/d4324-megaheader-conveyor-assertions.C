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
// Previously xfailed here for is_object_address failing to compose
// through pointer ARITHMETIC/INDEXING (e.g. std::barrier's own __state
// [__current].__tickets[__round]), and then for a different, unrelated
// gap only reached once that fix let std::barrier/__unicode's own
// array access reach std::array::operator[] for the first time --
// both CLOSED 2026-08-26, see the libstdc++ testsuite copies' own
// comments for the full explanation of both fixes. Clean now.

#include <bits/stdc++.h>

int main() { return 0; }
