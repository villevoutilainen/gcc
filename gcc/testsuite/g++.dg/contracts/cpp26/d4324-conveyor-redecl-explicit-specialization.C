// D4324: an explicit specialization may differ from the primary template
// with respect to 'conveyor', exactly as is already permitted for
// 'constexpr' ([dcl.constexpr]).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

template <class T> T f (T x) conveyor { return x; }
template <> int f<int> (int x) { return x + 1; }

int main () { return f<int> (1) - (int) f<double> (1.0) - 1; }
