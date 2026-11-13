// D4324: outside a symbolic contract's condition a symbolic function may
// not be named: ordinary code, and a conveyor function's body too, would
// call a function that has no definition.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct File;
bool is_open (const File*) symbolic;
bool usable (const File* f) conveyor { return f != nullptr && is_open (f); } // { dg-error "declared 'symbolic', may only be used in the condition of a symbolic contract" }
bool plain (const File* f) { return is_open (f); } // { dg-error "declared 'symbolic', may only be used in the condition of a symbolic contract" }
