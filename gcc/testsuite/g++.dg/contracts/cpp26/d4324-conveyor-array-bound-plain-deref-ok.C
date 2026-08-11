// D4324/P2680 item 8, Increment E2 (and W2): an ordinary pointer
// dereference with no tracked array-offset fact still needs no further
// bound check here, but *only* because 'p' is separately provable as a
// valid object address in its own right ('&a', a local variable's
// address) -- this is not a blanket "no array-offset fact means never
// flagged" exemption (see Increment W2: a dereference with neither an
// array-offset fact nor an is_object_address proof is now a mandatory
// error, closing a real UB-freedom gap the array-bound rule alone left
// open).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  int a = x;
  int* p = &a;
  return *p;
}

int main () { return f (1) - 1; }
