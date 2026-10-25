// basic_string and basic_string_view, with each character type and
// allocator.
#include "types.h"
#include <string>
#include <string_view>
#include <charconv>

namespace census
{
  template<class S>
  int use_string (S &s, const S &t)
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
    s.clear ();
    return n;
  }

  template<class C, class A>
  int strings ()
  {
    std::basic_string<C, std::char_traits<C>, A> s, t;
    return use_string (s, t);
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
