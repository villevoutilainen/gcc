// { dg-options "-D_GLIBCXX_ASSERTIONS -D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }
// { dg-do run { target c++26 } }

// D4324: submdspan with extent_slice compiles with conveyor assertions
// (P4445 BUG-35).  The slice checks call the non-conveyor
// extents::extent, which can end the object the slice, a reference
// parameter, refers to, so they are plain hardened assertions.
#include <mdspan>
#include <testsuite_hooks.h>

int
main()
{
  int a[100]{};
  std::mdspan<int, std::dextents<int, 1>> m(a, 100);
  // Run-time offset, extent and stride.
  VERIFY( std::submdspan(m, std::extent_slice{1, 2, 1}).extent(0) == 2 );
  VERIFY( std::submdspan(m, std::extent_slice{1, 3, 4}).extent(0) == 3 );
  VERIFY( std::submdspan(m, std::extent_slice{5, 0, 2}).extent(0) == 0 );
  // Stride one.
  VERIFY( std::submdspan(m, std::extent_slice{1, 5, std::cw<1>}).extent(0)
	  == 5 );
  // Constant extent.
  VERIFY( std::submdspan(m, std::extent_slice{1, std::cw<3>, 2}).extent(0)
	  == 3 );
}
