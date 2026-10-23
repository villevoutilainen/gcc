// D4324: the ordinary-code proof of a constructor's or destructor's
// receiver isn't made in a system header.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include "d4324-cdtor-receiver-system-header.h"

void use (H* p) { kill (p); S s; }
