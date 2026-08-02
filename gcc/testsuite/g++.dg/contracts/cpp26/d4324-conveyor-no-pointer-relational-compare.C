// D4324/P2680 item 8: a relational (or three-way) comparison between
// two pointers is banned outright in a conveyor function -- only same-
// array comparisons are well-defined, and this pass doesn't attempt to
// prove that, so it's rejected unconditionally (a point-of-construction
// check needing no dataflow, unlike the other two item 8 restrictions).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int* p, int* q) conveyor
{
  bool r = p < q; // { dg-error "pointer relational comparison not permitted in a conveyor function or predicate" }
  return r ? 1 : 0;
}

int main () { int x = 1, y = 2; return f (&x, &y); }
