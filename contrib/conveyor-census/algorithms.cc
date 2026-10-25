// <algorithm> and <ranges> algorithms with each iterator category and
// element type.
#include "types.h"
#include <algorithm>
#include <numeric>
#include <vector>
#include <functional>
#include <random>

namespace census
{
  template<class T>
  int random_access (T *b, T *e)
  {
    int n = 0;
    std::sort (b, e);
    std::stable_sort (b, e);
    std::partial_sort (b, b + 1, e);
    std::nth_element (b, b + 1, e);
    n += std::is_sorted (b, e) + std::binary_search (b, e, T (1));
    n += std::lower_bound (b, e, T (1)) != std::upper_bound (b, e, T (1));
    std::make_heap (b, e);
    std::push_heap (b, e);
    std::pop_heap (b, e);
    std::sort_heap (b, e);
    n += std::is_heap (b, e);
    std::reverse (b, e);
    std::rotate (b, b + 1, e);
    std::shuffle (b, e, std::minstd_rand ());
    std::inplace_merge (b, b + 1, e);
    n += *std::min_element (b, e) == *std::max_element (b, e);
    std::ranges::sort (b, e);
    std::ranges::stable_sort (b, e);
    n += std::ranges::is_sorted (b, e);
    std::ranges::reverse (b, e);
    n += std::ranges::binary_search (b, e, T (1));
    return n;
  }

  template<class T, template<class> class It>
  int iterators ()
  {
    T a[4] = { T (1), T (2), T (3), T (4) };
    T out[8];
    range<T, It> r (a);
    int n = 0;
    n += std::count (r.begin (), r.end (), T (1));
    range<T, It> r2 (a);
    n += std::find (r2.begin (), r2.end (), T (2)) != r2.end ();
    range<T, It> r3 (a);
    n += std::count_if (r3.begin (), r3.end (), [] (const T &x) { return x == x; });
    range<T, It> r4 (a);
    std::copy (r4.begin (), r4.end (), out);
    range<T, It> r5 (a);
    n += std::equal (r5.begin (), r5.end (), out);
    rrange<T, It> r6 (a);
    n += std::ranges::count (r6, T (1));
    rrange<T, It> r7 (a);
    n += std::ranges::find (r7, T (3)) != r7.end ();
    rrange<T, It> r8 (a);
    std::ranges::copy (r8, out);
    rrange<T, It> r9 (a);
    n += std::ranges::all_of (r9, [] (const T &x) { return x == x; });
    if constexpr (std::forward_iterator<typename range<T, It>::iterator>)
      {
	range<T, It> f (a);
	n += std::adjacent_find (f.begin (), f.end ()) == f.end ();
	range<T, It> g (a);
	n += std::search (g.begin (), g.end (), out, out + 1) == g.end ();
	range<T, It> h (a);
	std::replace (h.begin (), h.end (), T (1), T (5));
	rrange<T, It> k (a);
	n += std::ranges::max_element (k) != k.end ();
      }
    if constexpr (std::bidirectional_iterator<typename range<T, It>::iterator>)
      {
	range<T, It> b (a);
	std::reverse (b.begin (), b.end ());
	range<T, It> c (a);
	std::copy_backward (c.begin (), c.end (), out + 8);
      }
    return n;
  }

  template<class T>
  int numeric ()
  {
    std::vector<T> v = { T (1), T (2), T (3) };
    std::vector<T> w (3);
    int n = 0;
    std::iota (v.begin (), v.end (), T (1));
    n += std::accumulate (v.begin (), v.end (), T (0)) == T (6);
    n += std::reduce (v.begin (), v.end ()) == T (6);
    std::partial_sum (v.begin (), v.end (), w.begin ());
    std::adjacent_difference (v.begin (), v.end (), w.begin ());
    n += std::inner_product (v.begin (), v.end (), w.begin (), T (0)) == T (0);
    std::exclusive_scan (v.begin (), v.end (), w.begin (), T (0));
    n += std::transform_reduce (v.begin (), v.end (), w.begin (), T (0)) == T (0);
    return n;
  }

  int all ()
  {
    int n = 0;
    int ai[4] = { 4, 3, 2, 1 };
    Val av[4] = { 4, 3, 2, 1 };
    n += random_access (ai, ai + 4) + random_access (av, av + 4);
    n += iterators<int, input_iterator_wrapper> ();
    n += iterators<int, forward_iterator_wrapper> ();
    n += iterators<int, bidirectional_iterator_wrapper> ();
    n += iterators<int, random_access_iterator_wrapper> ();
    n += iterators<Val, forward_iterator_wrapper> ();
    n += iterators<Val, random_access_iterator_wrapper> ();
    n += numeric<int> () + numeric<long> () + numeric<double> ();
    n += std::max ({ 1, 2, 3 }) + std::min (Val (1), Val (2)).v
	 + std::clamp (5, 1, 3) + std::ranges::max ({ 1, 2 });
    return n;
  }
}

int main () { return census::all () == 0; }
