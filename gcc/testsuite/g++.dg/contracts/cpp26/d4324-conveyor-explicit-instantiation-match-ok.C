// D4324, Increment T: an explicit instantiation writing 'conveyor'
// where the template pattern is itself conveyor is accepted.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

template <class T> T f (T x) conveyor { return x; }
template int f<int> (int) conveyor;

int main () { return f<int> (5) - 5; }
