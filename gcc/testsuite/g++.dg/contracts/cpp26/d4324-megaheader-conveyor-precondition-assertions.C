// D4324: redundant copy of libstdc++-v3/testsuite/17_intro/headers/
// c++2026/stdc++_conveyor_precondition_assertions.cc, kept here too so
// a routine "dg.exp=*contracts*" compiler-testsuite run also exercises
// the strictest whole-library <bits/stdc++.h> sweep (this one also
// enables _GLIBCXX_PRECONDITION_ASSERTIONS, exercising every
// __glibcxx_requires_*/__glibcxx_assert-guarded precondition check
// throughout the library too) -- see d4324-megaheader-conveyor-
// assertions.C in this same directory, and the libstdc++ testsuite
// copy's own comment, for the full rationale. Update all four files
// together if any one's expected outcome changes.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS -fcontracts -fcontract-control-objects" }
//
// Same known, deferred gap as d4324-megaheader-conveyor-assertions.C
// (is_object_address can't compose through pointer arithmetic/
// indexing); see that file's own comment for the full explanation.
// Remove this dg-xfail-if once that engine gap is closed.
// { dg-xfail-if "is_object_address can't compose through pointer indexing" { *-*-* } }

#include <bits/stdc++.h>

int main() { return 0; }
