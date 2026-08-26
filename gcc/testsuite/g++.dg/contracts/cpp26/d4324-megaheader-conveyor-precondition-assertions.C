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
// Previously xfailed here too, for the identical, now-closed pointer-
// indexing gap d4324-megaheader-conveyor-assertions.C's own comment
// describes -- see that file's own comment, and the libstdc++ testsuite
// copies' own fuller explanation, for what fixed it. Clean under this
// file's own (stricter) flag combination now.

#include <bits/stdc++.h>

int main() { return 0; }
