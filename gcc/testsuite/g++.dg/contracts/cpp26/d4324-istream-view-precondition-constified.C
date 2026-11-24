// D4324/C++26: a precondition that would walk an input range is rejected
// by constification where the range can't be iterated through const:
// inside the predicate, iv is const, and istream_view has no begin()
// const (its begin() reads an element from the stream each time).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <ranges>
#include <sstream>

using istream_view = std::ranges::basic_istream_view<int, char>;

int first (istream_view& iv)
  pre (iv.begin () != iv.end ())	// { dg-error "discards qualifiers" }
{
  return *iv.begin ();
}

int
main ()
{
  std::istringstream in ("1 2 3");
  istream_view iv (in);
  return first (iv);
}
