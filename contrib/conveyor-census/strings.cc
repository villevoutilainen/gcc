// basic_string and basic_string_view, with each character type and
// allocator.
#include "types.h"
#include <string>
#include <string_view>
#include <charconv>

namespace census
{
  // Every form of operator+: each operand a string lvalue or rvalue, a
  // character, a character array or a string view.
  template<class S>
  int concatenate (const S &s, const S &t)
    CENSUS_OBJECT (s) CENSUS_OBJECT (t)
  {
    using C = typename S::value_type;
    const C *p = t.c_str ();
    std::basic_string_view<C> v (t);
    std::size_t n = 0;
    n += (s + t).size () + (p + s).size () + (C ('x') + s).size ()
	 + (s + p).size () + (s + C ('y')).size ();
    n += (S (s) + t).size () + (s + S (t)).size () + (S (s) + S (t)).size ();
    n += (p + S (s)).size () + (C ('x') + S (s)).size ()
	 + (S (s) + p).size () + (S (s) + C ('y')).size ();
#if __glibcxx_string_view >= 202403L
    n += (s + v).size () + (S (s) + v).size () + (v + s).size ()
	 + (v + S (s)).size ();
#endif
    return n;
  }

  template<class S>
  int use_string (S &s, const S &t)
    CENSUS_OBJECT (s) CENSUS_OBJECT (t)
  {
    using C = typename S::value_type;
    int n = 0;
    s.push_back (C ('a'));
    s.append (t);
    s.append (3, C ('b'));
    s.insert (0, t);
    s.insert (s.begin (), C ('c'));
    s.replace (0, 1, t);
    s += t;
    s += C ('d');
    n += s.find (t) + s.rfind (C ('a')) + s.find_first_of (t)
	 + s.find_last_not_of (C ('x'));
    n += s.compare (t) + (s == t) + (s < t) + (s <=> t == 0);
    n += s.starts_with (C ('a')) + s.ends_with (t) + s.contains (C ('z'));
    S sub = s.substr (1, 2);
    n += sub.size () + s.length () + s.capacity () + (s.max_size () > 0);
    n += s[0] == s.at (0);
    n += s.front () == s.back ();
    s.resize (10, C ('e'));
    s.erase (0, 1);
    s.erase (s.begin ());
    s.pop_back ();
    s.reserve (64);
    s.shrink_to_fit ();
    std::basic_string_view<C> v = s;
    n += v.size () + v.find (C ('a')) + v.substr (1).size ()
	 + (v == std::basic_string_view<C> (t));
    v.remove_prefix (1);
    v.remove_suffix (1);
    S copy (s);
    S moved (std::move (copy));
    copy = moved;
    copy.swap (moved);
    n += std::erase (s, C ('a')) + std::erase_if (s, [] (C) { return false; });
    n += (s.c_str () != nullptr) + (s.data () != nullptr);
    n += concatenate (s, t);
    s.clear ();
    return n;
  }

  // Every overload of the find family.
  template<class S>
  int find_all (const S &s, const S &t)
    CENSUS_OBJECT (s) CENSUS_OBJECT (t)
  {
    using C = typename S::value_type;
    const C p[] = { C ('a'), C ('b'), C () };
    std::basic_string_view<C> v (p, 2);
    int n = 0;
#define CENSUS_FIND(F) \
    n += s.F (t) + s.F (v) + s.F (p, 0, 2) + s.F (p) + s.F (C ('a'));
    CENSUS_FIND (find)
    CENSUS_FIND (rfind)
    CENSUS_FIND (find_first_of)
    CENSUS_FIND (find_last_of)
    CENSUS_FIND (find_first_not_of)
    CENSUS_FIND (find_last_not_of)
#undef CENSUS_FIND
    return n;
  }

  // The other overloads taking a string, a view, a pointer or a range.
  template<class S>
  int overloads_all (S &s, const S &t)
    CENSUS_OBJECT (s) CENSUS_OBJECT (t)
  {
    using C = typename S::value_type;
    const C p[] = { C ('a'), C ('b'), C () };
    std::basic_string_view<C> v (p, 2);
    int n = 0;
    s.assign (t); s.assign (t, 0, 1); s.assign (v); s.assign (v, 0, 1);
    s.assign (p, 2); s.assign (p); s.assign (2, C ('a'));
    s.assign (p, p + 2); s.assign ({ C ('a') });
    s.append (t, 0, 1); s.append (v, 0, 1); s.append (p, 2); s.append (p);
    s.append (p, p + 2); s.append ({ C ('a') });
    s += v; s += p; s += { C ('a') };
    s.insert (0, t, 0, 1); s.insert (0, v); s.insert (0, v, 0, 1);
    s.insert (0, p, 2); s.insert (0, p); s.insert (0, 2, C ('a'));
    s.insert (s.begin (), 2, C ('a')); s.insert (s.begin (), p, p + 2);
    s.insert (s.begin (), { C ('a') });
    s.replace (0, 1, t, 0, 1); s.replace (0, 1, v); s.replace (0, 1, v, 0, 1);
    s.replace (0, 1, p, 2); s.replace (0, 1, p); s.replace (0, 1, 2, C ('a'));
    s.replace (s.begin (), s.begin () + 1, t);
    s.replace (s.begin (), s.begin () + 1, v);
    s.replace (s.begin (), s.begin () + 1, p, 2);
    s.replace (s.begin (), s.begin () + 1, p);
    s.replace (s.begin (), s.begin () + 1, 2, C ('a'));
    s.replace (s.begin (), s.begin () + 1, p, p + 2);
    s.replace (s.begin (), s.begin () + 1, { C ('a') });
    n += s.compare (v) + s.compare (0, 1, t) + s.compare (0, 1, t, 0, 1)
	 + s.compare (0, 1, v) + s.compare (0, 1, v, 0, 1) + s.compare (p)
	 + s.compare (0, 1, p) + s.compare (0, 1, p, 2);
    n += s.starts_with (v) + s.starts_with (p) + s.ends_with (v)
	 + s.ends_with (p) + s.ends_with (C ('a')) + s.contains (v)
	 + s.contains (p) + s.contains (t);
    C buf[4];
    n += s.copy (buf, 2) + s.substr ().size ();
    s.erase (s.begin (), s.begin () + 1);
    s.insert (0, t);
    s.resize (4);
    s.append_range (v); s.assign_range (v); s.insert_range (s.begin (), v);
    s.replace_with_range (s.begin (), s.begin () + 1, v);
    s.resize_and_overwrite (2, [] (C *, std::size_t k) { return k; });
    n += s.empty () + (s.get_allocator () == t.get_allocator ());
    n += *s.begin () == *s.cbegin () && *s.rbegin () == *s.crbegin ();
    n += s.end () == s.cend () || s.rend () == s.crend ();
    return n;
  }

  template<class C, class A>
  int strings ()
  {
    std::basic_string<C, std::char_traits<C>, A> s, t;
    return use_string (s, t) + find_all (s, t) + overloads_all (s, t);
  }

  int all ()
  {
    int n = 0;
    n += strings<char, std::allocator<char>> ();
    n += strings<wchar_t, std::allocator<wchar_t>> ();
    n += strings<char8_t, std::allocator<char8_t>> ();
    n += strings<char16_t, std::allocator<char16_t>> ();
    n += strings<char32_t, std::allocator<char32_t>> ();
    n += strings<char, MinAlloc<char>> ();
    n += strings<char, PropAlloc<char>> ();
    std::string s = std::to_string (42) + std::to_string (1.5);
    n += std::stoi (s) + (std::stod ("1.5") > 1);
    char buf[32];
    auto r = std::to_chars (buf, buf + 32, 42);
    int x = 0;
    std::from_chars (buf, r.ptr, x);
    n += x;
    using namespace std::literals;
    n += ("abc"s).size () + ("abc"sv).size ();
    return n;
  }
}

int main () { return census::all () == 0; }
