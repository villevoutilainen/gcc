// Sequence and associative containers, used with each element type and
// allocator in types.h.
#include "types.h"
#include <vector>
#include <deque>
#include <list>
#include <forward_list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <flat_map>
#include <flat_set>
#include <array>
#include <span>
#include <algorithm>

namespace census
{
  template<class C>
  int use_sequence (C &c, const C &other)
    CENSUS_OBJECT (c) CENSUS_OBJECT (other)
  {
    using T = typename C::value_type;
    int n = 0;
    c.emplace_back (1);
    c.emplace (c.begin (), 2);
    if constexpr (std::copyable<T>)
      {
	T t (3);
	c.push_back (t);
	c.insert (c.end (), t);
	c.insert (c.end (), 2, t);
	c.assign (2, t);
	C copy (c);
	copy = other;
	n += copy == other;
	n += copy < other;
	c.resize (4, t);
      }
    c.push_back (T (4));
    for (auto &x : c)
      n += x == x;
    for (auto it = c.rbegin (); it != c.rend (); ++it)
      ++n;
    n += c.size () + c.empty () + (c.max_size () > 0);
    n += c.front () == c.back ();
    c.erase (c.begin ());
    c.pop_back ();
    c.resize (2);
    if constexpr (requires { c.shrink_to_fit (); })
      c.shrink_to_fit ();
    C moved (std::move (c));
    c = std::move (moved);
    c.swap (moved);
    std::swap (c, moved);
    n += std::erase_if (c, [] (const T &x) { return x == x; });
    c.clear ();
    (void) c.get_allocator ();
    return n;
  }

  template<class C>
  int use_random_access (C &c)
    CENSUS_OBJECT (c)
  {
    int n = 0;
    if (!c.empty ())
      {
	n += c[0] == c.at (0);
	n += c.end () - c.begin ();
      }
    return n;
  }

  template<class T, template<class> class A>
  int sequences ()
  {
    int n = 0;
    {
      std::vector<T, A<T>> v, w;
      n += use_sequence (v, w) + use_random_access (v);
      if constexpr (std::copyable<T>)
	n += v.capacity () + (v.data () != nullptr);
    }
    {
      std::deque<T, A<T>> d, e;
      n += use_sequence (d, e) + use_random_access (d);
      d.emplace_front (1);
      d.pop_front ();
    }
    if constexpr (std::default_initializable<T>)
      {
	// The sized constructors.
	std::vector<T, A<T>> v (3);
	std::deque<T, A<T>> d (3);
	std::list<T, A<T>> l (3);
	n += v.size () + d.size () + l.size ();
	if constexpr (std::copyable<T>)
	  {
	    std::vector<T, A<T>> v2 (3, v[0]);
	    std::deque<T, A<T>> d2 (3, d[0]);
	    std::list<T, A<T>> l2 (3, l.front ());
	    n += v2.size () + d2.size () + l2.size ();
	  }
      }
    {
      std::list<T, A<T>> l, m;
      n += use_sequence (l, m);
      l.emplace_front (1);
      l.sort ();
      l.unique ();
      l.reverse ();
      l.merge (m);
      l.splice (l.begin (), m);
      l.remove_if ([] (const T &) { return false; });
    }
    {
      std::forward_list<T, A<T>> f;
      f.emplace_front (1);
      f.emplace_after (f.before_begin (), 2);
      f.sort ();
      f.reverse ();
      for (auto &x : f)
	n += x == x;
      f.erase_after (f.before_begin ());
      f.pop_front ();
    }
    return n;
  }

  template<class K, class V, template<class> class A>
  int associative ()
  {
    int n = 0;
    using P = std::pair<const K, V>;
    {
      std::map<K, V, std::less<K>, A<P>> m, o;
      m.try_emplace (K (1), 2);
      m.emplace (K (2), 3);
      n += m.count (K (1)) + m.contains (K (2));
      n += m.find (K (1)) != m.end ();
      n += m.lower_bound (K (1)) != m.upper_bound (K (1));
      auto nh = m.extract (K (1));
      if (nh)
	m.insert (std::move (nh));
      m.merge (o);
      for (auto &[k, v] : m)
	n += k == k;
      m.erase (K (2));
      n += m == o;
      std::erase_if (m, [] (const auto &) { return false; });
    }
    {
      std::multimap<K, V, std::less<K>, A<P>> m;
      m.emplace (K (1), 2);
      n += m.count (K (1));
      auto [b, e] = m.equal_range (K (1));
      n += b == e;
    }
    {
      std::set<K, std::less<K>, A<K>> s, t;
      s.emplace (1);
      s.insert (K (2));
      n += s.contains (K (1));
      auto nh = s.extract (s.begin ());
      s.insert (std::move (nh));
      s.merge (t);
      n += s == t;
    }
    {
      std::unordered_map<K, V, std::hash<int>, std::equal_to<K>,
			 A<P>> u, w;
      u.try_emplace (K (1), 2);
      n += u.count (K (1)) + u.bucket_count () + (u.load_factor () >= 0);
      auto nh = u.extract (K (1));
      if (nh)
	u.insert (std::move (nh));
      u.merge (w);
      u.rehash (8);
      n += u == w;
    }
    {
      std::unordered_set<K, std::hash<int>, std::equal_to<K>, A<K>> u;
      u.emplace (1);
      n += u.contains (K (1));
    }
    return n;
  }

  template<class T, std::size_t N>
  int fixed ()
  {
    std::array<T, N> a {};
    int n = a.size () + (a[0] == a.at (0)) + (a.front () == a.back ());
    std::span<T> s (a);
    n += s.size () + (s[0] == s.front ()) + s.subspan (1).size ()
	 + s.first (1).size () + s.last (1).size ();
    std::span<const T, N> cs (a);
    n += (cs.size_bytes () > 0) + (s.size_bytes () > 0);
    for (auto &x : a)
      n += x == x;
    return n;
  }

  int flat ()
  {
    std::flat_map<int, Val> m;
    m.emplace (1, Val (2));
    m.try_emplace (2, 3);
    int n = m.contains (1) + m.count (2) + (m.find (1) != m.end ());
    n += m[1].v;
    std::flat_set<Val> s;
    s.emplace (1);
    s.insert (Val (2));
    n += s.contains (Val (1));
    std::flat_multimap<int, int> mm;
    mm.emplace (1, 2);
    n += mm.count (1);
    return n;
  }

#ifdef _GLIBCXX_CONVEYOR_ASSERTIONS
  // A user container whose members are conveyor: the non-member access
  // functions (std::size, std::empty, std::data, std::ssize) call them on
  // their own parameter.
  struct ConveyorRange
  {
    int d[2] = { 1, 2 };
    std::size_t size () const conveyor { return 2; }
    bool empty () const conveyor { return false; }
    int *data () conveyor { return d; }
    const int *data () const conveyor { return d; }
  };

  // A user sequence whose members are conveyor, for the back and front
  // insert iterators, which call them through their stored container
  // pointer.
  struct ConveyorSeq
  {
    using value_type = int;
    using const_reference = const int &;
    int n = 0;
    void push_back (int v) conveyor { n = v; }
    void push_front (int v) conveyor { n = v; }
  };

  int insert_iterators ()
  {
    ConveyorSeq s;
    int v = 1;
    auto b = std::back_inserter (s);
    *b = v;
    *b = 2;
    auto f = std::front_inserter (s);
    *f = v;
    *f = 3;
    return s.n;
  }

  int nonmember_access ()
  {
    ConveyorRange r;
    const ConveyorRange &cr = r;
    int n = std::size (r) + std::empty (r) + std::ssize (cr);
    n += (std::data (r) != nullptr) + (std::data (cr) != nullptr);
    return n;
  }
#endif

  // An int key wrapped so the hashers above apply.
  struct Key
  {
    int v;
    Key (int i) : v (i) { }
    operator int () const { return v; }
    friend auto operator<=> (const Key &, const Key &) = default;
  };

  int takes_object (const Val &x)
    CENSUS_OBJECT (x)
  { return x.v; }

  // Members of the sequences that call their own accessors, and the
  // range constructors and members given an array.
  int sequence_extras ()
  {
    int n = 0;
    const std::vector<int> cv = { 1, 2 };
    n += *cv.rbegin () + *(cv.rend () - 1);
    std::vector<int> v;
    v.reserve (4);
    std::array<long, 4> a;
    a.fill (1);
#ifdef _GLIBCXX_CONVEYOR_ASSERTIONS
    // What the library promises in conveyor mode only.
    std::array<Val, 3> av {};
    n += takes_object (av[1]);
#endif
    const int r[2] = { 1, 2 };
    std::deque<int> d (std::from_range, r);
    std::list<int> l (std::from_range, r);
    std::vector<int> w (std::from_range, r);
    v.append_range (r);
    d.append_range (r);
    l.append_range (r);
    v.assign_range (r);
    d.assign_range (r);
    l.assign_range (r);
    v.insert_range (v.begin (), r);
    d.insert_range (d.begin (), r);
    l.insert_range (l.begin (), r);
    d.prepend_range (r);
    l.prepend_range (r);
    std::forward_list<int> f (std::from_range, r);
    f.assign_range (r);
    f.prepend_range (r);
    f.insert_range_after (f.before_begin (), r);
    n += *f.begin ();
    n += d.size () + l.size () + w.size () + a[0];
    return n;
  }

  int all ()
  {
    int n = sequence_extras ();
    n += sequences<int, std::allocator> ();
    n += sequences<Val, std::allocator> ();
    n += sequences<ThrowCopy, std::allocator> ();
    n += sequences<AddrOp, std::allocator> ();
    n += sequences<int, MinAlloc> ();
    n += sequences<Val, PropAlloc> ();
    n += sequences<int, ExtAlloc> ();
    n += associative<int, int, std::allocator> ();
    n += associative<Key, Val, std::allocator> ();
    n += associative<int, Val, MinAlloc> ();
    n += associative<int, int, PropAlloc> ();
    n += fixed<int, 4> () + fixed<Val, 3> () + fixed<AddrOp, 2> ();
    n += flat ();
#ifdef _GLIBCXX_CONVEYOR_ASSERTIONS
    n += nonmember_access () + insert_iterators ();
#endif
    return n;
  }
}

int main () { return census::all () == 0; }
