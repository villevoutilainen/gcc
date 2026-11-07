// D4324: outside a contract condition, a strict predicate and a conveyor
// function, a symbolic function may not be called: ordinary code would
// call a function that has no definition.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct File;
bool is_open (const File*) symbolic;
bool usable (const File* f) conveyor { return f != nullptr && is_open (f); }
bool plain (const File* f) { return is_open (f); } // { dg-error "declared 'symbolic', may only be used" }
