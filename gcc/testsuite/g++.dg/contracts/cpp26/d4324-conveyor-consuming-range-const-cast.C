// D4324: as d4324-conveyor-consuming-range.C, through a const_cast: the
// cone of evaluation goes by who owns the object, not by its constness,
// so the calls are still rejected.
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
  pre<std::contracts::conveyor_assert_v> (const_cast<countdown&> (c).begin () // { dg-error "object 'c' is not owned by the calling function, so the non-const member function 'int countdown::begin\\(\\)' may not be called on it" }
					  != const_cast<countdown&> (c).end ()) // { dg-error "so the non-const member function 'int countdown::end\\(\\)' may not be called on it" }
{
  return c.begin ();
}

int
main ()
{
  countdown c;
  return first (c);
}
