// Range adaptors and views, over containers and iterator categories.
#include "types.h"
#include <ranges>
#include <vector>
#include <list>
#include <forward_list>
#include <map>
#include <string>
#include <sstream>
#include <algorithm>

namespace census
{
  template<class R>
  int consume (R &&r)
    CENSUS_OBJECT (r)
  {
    int n = 0;
    for (auto &&x : r)
      n += sizeof (x) > 0;
    if constexpr (std::ranges::sized_range<R>)
      n += std::ranges::size (r) + std::ranges::ssize (r);
    if constexpr (std::ranges::forward_range<R>)
      n += std::ranges::distance (r) + !std::ranges::empty (r);
    return n;
  }

  template<class C>
  int pipelines (C &c)
    CENSUS_OBJECT (c)
  {
    namespace v = std::views;
    using T = std::ranges::range_value_t<C>;
    auto even = [] (const T &x) { return x == x; };
    auto twice = [] (const T &x) { return x; };
    int n = 0;
    n += consume (c | v::filter (even));
    n += consume (c | v::transform (twice));
    n += consume (c | v::take (2) | v::drop (1));
    n += consume (c | v::take_while (even) | v::drop_while (even));
    n += consume (v::all (c));
    n += consume (c | v::enumerate);
    n += consume (c | v::as_const);
    n += consume (v::zip (c, c));
    n += consume (c | v::chunk (2));
    n += consume (c | v::stride (2));
    if constexpr (std::ranges::forward_range<C>)
      {
	n += consume (c | v::slide (2));
	n += consume (c | v::adjacent<2>);
	n += consume (c | v::chunk_by (std::ranges::equal_to {}));
	n += consume (v::cartesian_product (c, c));
      }
    if constexpr (std::ranges::bidirectional_range<C>)
      n += consume (c | v::reverse);
    if constexpr (std::ranges::common_range<C>)
      n += consume (c | v::common);
    std::ranges::subrange s (std::ranges::begin (c), std::ranges::end (c));
    n += consume (s);
    std::ranges::ref_view rv (c);
    n += consume (rv);
    return n;
  }

  // Views over owned and nested ranges, their iterators, and the
  // view_interface accessors.
  int more_views ()
  {
    namespace v = std::views;
    int n = 0;
    std::vector<int> nested[2] = { { 1, 2 }, { 3 } };
    auto j = nested | v::join;
    auto it = j.end ();
    --it;
    n += *it;
    auto o = std::vector<int> { 1, 2, 3 } | v::all;
    n += *o.begin () + o.size () + (o.end () != o.begin ());
    n += o.front () + o.back ();
    auto t = v::iota (0, 5) | v::take (3);
    n += t.front ();
    const int a[5] = { 1, 2, 3, 4, 5 };
    auto at = a | v::adjacent_transform<3> ([] (auto... x) { return (x + ...); });
    n += at.begin () == at.end ();
    auto c = v::iota (0, 5) | v::take_while ([] (int x) { return x < 3; })
	     | v::common;
    n += c.begin () == c.end ();
    std::vector<int> fr (std::from_range, a);
    n += fr.size ();
    std::unique_ptr<int> ups[] = { std::make_unique<int> (1),
				   std::make_unique<int> (2) };
    for (auto up : ups | v::as_rvalue)
      n += *up;
    return n;
  }

  int all ()
  {
    namespace v = std::views;
    int n = 0;
    std::vector<int> vi = { 1, 2, 3, 4 };
    std::vector<Val> vv = { 1, 2, 3 };
    std::list<int> li = { 1, 2, 3 };
    std::forward_list<Val> fl = { 1, 2 };
    n += pipelines (vi) + pipelines (vv) + pipelines (li) + pipelines (fl);
    int a[4] = { 1, 2, 3, 4 };
    rrange<int, input_iterator_wrapper> in (a);
    n += consume (in | v::take (2));
    n += consume (a) + std::ranges::ssize (a);
    n += more_views ();
    rrange<int, forward_iterator_wrapper> fw (a);
    n += pipelines (fw);
    std::vector<std::vector<int>> nested = { { 1 }, { 2, 3 } };
    n += consume (nested | v::join);
    n += consume (nested | v::join_with (0));
    std::string s = "a,b,c";
    n += consume (s | v::split (','));
    n += consume (s | v::lazy_split (','));
    n += consume (v::iota (0, 5)) + consume (v::repeat (1, 3))
	 + consume (v::single (Val (1))) + consume (v::empty<int>);
    n += consume (v::concat (vi, vi));
    std::map<int, Val> m = { { 1, 2 } };
    n += consume (m | v::keys) + consume (m | v::values) + consume (m | v::elements<1>);
    std::istringstream is ("1 2 3");
    n += consume (v::istream<int> (is));
    auto vec = vi | v::transform ([] (int x) { return x; }) | std::ranges::to<std::vector> ();
    n += vec.size ();
    return n;
  }
}

int main () { return census::all () == 0; }
