// mdspan with each layout, extents and accessor.
#include "types.h"
#include <mdspan>
#include <array>

namespace census
{
  template<class M>
  int use (M m)
  {
    int n = m.rank () + m.rank_dynamic () + m.size () + m.empty ()
	    + m.extent (0) + m.is_unique () + m.is_exhaustive ()
	    + m.is_strided () + m.stride (0);
    n += m[0, 0] == m[0, 0];
    n += m.mapping ().required_span_size ();
    return n;
  }

  int all ()
  {
    std::array<int, 12> a {};
    std::array<Val, 12> va {};
    int n = 0;
    n += use (std::mdspan (a.data (), 3, 4));
    n += use (std::mdspan<int, std::extents<int, 3, 4>> (a.data ()));
    n += use (std::mdspan<int, std::dextents<long, 2>, std::layout_left> (a.data (), 3, 4));
    n += use (std::mdspan<Val, std::extents<unsigned, 3, std::dynamic_extent>,
			  std::layout_right> (va.data (), 4));
    std::layout_stride::mapping<std::dextents<int, 2>>
      sm (std::dextents<int, 2> (3, 4), std::array<int, 2> { 4, 1 });
    n += use (std::mdspan<int, std::dextents<int, 2>, std::layout_stride> (a.data (), sm));
    std::extents<int, 3, std::dynamic_extent> e (4);
    n += e.extent (1) + e.static_extent (0);
    std::dextents<int, 2> de (e);
    n += de == e;
    auto sub = std::submdspan (std::mdspan (a.data (), 3, 4), 1, std::full_extent);
    n += sub.extent (0);
    return n;
  }
}

int main () { return census::all () == 0; }
