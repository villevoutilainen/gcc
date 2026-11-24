// D4324/C++26: casting the constification of a precondition away compiles,
// and then checking it walks the input range: istream_view's begin()
// reads an element each time, so the precondition consumes the 1 and the
// function returns 2.  (A conveyor contract rejects this: see
// d4324-istream-view-precondition-conveyor-const-cast.C.)
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <ranges>
#include <sstream>

using istream_view = std::ranges::basic_istream_view<int, char>;

int first (istream_view& iv)
  pre (const_cast<istream_view&> (iv).begin ()
       != const_cast<istream_view&> (iv).end ())
{
  return *iv.begin ();
}

int
main ()
{
  std::istringstream in ("1 2 3");
  istream_view iv (in);
  if (first (iv) != 2)
    __builtin_abort ();
}
