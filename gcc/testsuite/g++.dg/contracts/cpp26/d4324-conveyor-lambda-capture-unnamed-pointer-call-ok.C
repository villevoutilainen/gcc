// D4324: a call handed a pointer that can't be named (returned by
// another call) may change any object whose address has escaped -- but
// not a non-mutable lambda's own closure, which its body doesn't name to
// the callee: what the lambda captured (here 'this') is still what it
// was.  As in the AST engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int& get_ref (int& r) { return r; }
void bump (int& r) { ++r; }

struct S {
  int n = 0;
  void sat () conveyor pre<std::contracts::conveyor_assert_v>(std::is_object_address (this)) { }
  void step ()
    pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (this))
  {
    auto l = [this] ()
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (this))
      {
	bump (get_ref (n));
	sat ();
      };
    l ();
  }
};

int main () { S s; s.step (); }
