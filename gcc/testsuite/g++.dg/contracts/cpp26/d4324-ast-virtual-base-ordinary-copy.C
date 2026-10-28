// D4324: a defaulted copy or move of a class with a virtual base reads the
// source's vtable pointer, so a conveyor one requires its source to be an
// object.  One that isn't conveyor -- here because a member's assignment
// isn't -- is ordinary code, whose dereferences owe nothing, so its
// callers owe nothing either (std::osyncstream's move assignment).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

struct H { int v; };
struct M { M &operator= (const M &); M (); M (const M &); };
struct V : virtual H { M m; };

void assign (V &a, V &b) { a = b; }
V copy (const V &v) { return v; }
