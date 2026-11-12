// D4324 (P4445 2.3.2.3): under -fcontract-symbolic-runtime-checks, a fact
// can be established and checked during static initialization, before the
// run-time library's own: the record is constructed on first use.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

struct io_facility
{
  bool is_opened (io_facility *) const symbolic;
  void open () post<std::contracts::symbolic_assert_v> (is_opened (this)) {}
  void write_data () pre<std::contracts::symbolic_assert_v> (is_opened (this)) {}
};

// Established and checked during static initialization.
struct opener_checker
{
  io_facility f;
  opener_checker () { f.open (); f.write_data (); }
} global1;

// Established during static initialization, checked in main.  (A call such
// as write_data (), given &f, ends the fact, as the static analysis
// assumes, so nothing is called on it in between.)
struct opener
{
  io_facility f;
  opener () { f.open (); }
} global2;

int main () { global2.f.write_data (); }
