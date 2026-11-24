// D4324: a conveyor contract may call a range's conveyor begin() and end(),
// but not modify the range, which the predicate didn't create (its cone
// of evaluation): calling them, non-const, on it is rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

// A range of our own whose begin() and end() are conveyor -- and, like
// istream_view's, non-const, and begin() consumes an element each time.
struct countdown
{
  int left = 3;
  int begin () conveyor { if (left > 0) --left; return left; }
  int end () conveyor { return 0; }
};

int first (countdown& c)
  pre<std::contracts::conveyor_assert_v> (c.begin () != c.end ()) // { dg-error "object 'c' is not owned by the calling function, so the non-const member function 'int countdown::begin\\(\\)' may not be called on it" }
// { dg-error "so the non-const member function 'int countdown::end\\(\\)' may not be called on it" "" { target *-*-* } .-1 }
{
  return c.begin ();
}

int
main ()
{
  countdown c;
  return first (c);
}
