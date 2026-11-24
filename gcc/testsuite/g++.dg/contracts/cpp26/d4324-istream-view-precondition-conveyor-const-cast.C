// D4324: a conveyor contract can't walk an input range even through a
// const_cast: istream_view's begin() and end() aren't conveyor.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <ranges>
#include <sstream>

using istream_view = std::ranges::basic_istream_view<int, char>;

int first (istream_view& iv)
  pre<std::contracts::conveyor_assert_v> (const_cast<istream_view&> (iv).begin () // { dg-error "'conveyor', not permitted in a conveyor function or predicate" }
					  != const_cast<istream_view&> (iv).end ()) // { dg-error "'conveyor', not permitted in a conveyor function or predicate" }
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
