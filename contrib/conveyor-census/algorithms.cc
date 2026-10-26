// <algorithm> and <ranges> algorithms with each iterator category and
// element type.
#include "types.h"
#include <algorithm>
#include <numeric>
#include <vector>
#include <functional>
#include <random>
#include <list>
#include <memory>

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

  // The range overloads of the ranges algorithms.
  int ranges_api ()
  {
    namespace r = std::ranges;
    std::vector<int> v = { 3, 1, 2 }, w = { 1, 2, 3 }, out (8);
    std::list<int> l = { 1, 2, 3 };
    auto pred = [] (int x) { return x > 1; };
    int n = 0;
    n += r::all_of (v, pred) + r::any_of (v, pred) + r::none_of (v, pred);
    r::for_each (v, [] (int) {});
    n += (r::find (v, 1) != v.end ()) + (r::find (l, 1) != l.end ());
    n += (r::find_if (v, pred) != v.end ()) + (r::find_if_not (v, pred) != v.end ());
    n += r::find_end (v, w).begin () != v.end ();
    n += r::find_first_of (v, w) != v.end ();
    n += r::adjacent_find (v) != v.end ();
    n += r::count (v, 1) + r::count_if (v, pred) + r::count (l, 1);
    n += r::mismatch (v, w).in1 != v.end ();
    n += r::equal (v, w) + r::equal (l, w) + r::is_permutation (v, w);
    n += r::search (v, w).begin () != v.end ();
    n += r::search_n (v, 1, 1).begin () != v.end ();
    n += r::starts_with (v, w) + r::ends_with (v, w);
    n += r::contains (v, 1) + r::contains_subrange (v, w);
    n += r::find_last (v, 1).begin () != v.end ();
    n += r::find_last_if (v, pred).begin () != v.end ();
    r::copy (v, out.begin ());
    r::copy_if (v, out.begin (), pred);
    r::copy_backward (v, out.end ());
    r::move (w, out.begin ());
    r::move_backward (w, out.end ());
    r::transform (v, out.begin (), [] (int x) { return x; });
    r::transform (v, w, out.begin (), [] (int x, int) { return x; });
    r::replace (out, 1, 2);
    r::replace_if (out, pred, 2);
    r::replace_copy (v, out.begin (), 1, 2);
    r::replace_copy_if (v, out.begin (), pred, 2);
    r::fill (out, 1);
    r::generate (out, [] { return 1; });
    n += r::remove (out, 1).begin () != out.end ();
    n += r::remove_if (out, pred).begin () != out.end ();
    r::remove_copy (v, out.begin (), 1);
    r::remove_copy_if (v, out.begin (), pred);
    n += r::unique (out).begin () != out.end ();
    r::unique_copy (v, out.begin ());
    r::reverse (out);
    r::reverse (l);
    r::reverse_copy (v, out.begin ());
    r::rotate (out, out.begin () + 1);
    r::rotate_copy (v, v.begin () + 1, out.begin ());
    r::shuffle (out, std::minstd_rand ());
    r::sample (v, out.begin (), 2, std::minstd_rand ());
    r::shift_left (out, 1);
    r::shift_right (out, 1);
    n += r::is_partitioned (v, pred);
    r::partition (out, pred);
    r::partition_copy (v, out.begin (), out.begin () + 4, pred);
    r::stable_partition (out, pred);
    n += r::partition_point (out, pred) != out.end ();
    r::sort (out);
    r::stable_sort (out);
    r::partial_sort (out, out.begin () + 1);
    r::partial_sort_copy (v, out);
    n += r::is_sorted (out) + (r::is_sorted_until (out) != out.end ());
    r::nth_element (out, out.begin () + 1);
    n += r::lower_bound (out, 1) != r::upper_bound (out, 1);
    n += r::equal_range (out, 1).begin () != out.end ();
    n += r::binary_search (out, 1);
    r::sort (v);
    r::merge (v, w, out.begin ());
    r::inplace_merge (out, out.begin () + 1);
    n += r::includes (v, w);
    r::set_union (v, w, out.begin ());
    r::set_intersection (v, w, out.begin ());
    r::set_difference (v, w, out.begin ());
    r::set_symmetric_difference (v, w, out.begin ());
    r::make_heap (out);
    r::push_heap (out);
    r::pop_heap (out);
    r::sort_heap (out);
    n += r::is_heap (out) + (r::is_heap_until (out) != out.end ());
    n += (r::min (v) == r::max (v)) + (r::minmax (v).min == 0);
    n += r::min_element (v) != r::max_element (v);
    n += r::minmax_element (v).min != v.end ();
    n += r::lexicographical_compare (v, w) + r::lexicographical_compare (v, l);
    n += r::next_permutation (out).found + r::prev_permutation (out).found;
    n += r::fold_left (v, 0, std::plus<> {}) + *r::fold_left_first (v, std::plus<> {});
    n += r::fold_right (v, 0, std::plus<> {}) + *r::fold_right_last (v, std::plus<> {});
    n += r::fold_left_with_iter (v, 0, std::plus<> {}).value;
    n += *r::fold_left_first_with_iter (v, std::plus<> {}).value;
    r::iota (out, 0);
    r::uninitialized_copy (v, out);
    r::uninitialized_move (w, out);
    r::uninitialized_fill (out, 1);
    r::uninitialized_value_construct (out);
    r::uninitialized_default_construct (out);
    r::destroy (out);
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
    n += ranges_api ();
    n += std::max ({ 1, 2, 3 }) + std::min (Val (1), Val (2)).v
	 + std::clamp (5, 1, 3) + std::ranges::max ({ 1, 2 });
    return n;
  }
}

int main () { return census::all () == 0; }
