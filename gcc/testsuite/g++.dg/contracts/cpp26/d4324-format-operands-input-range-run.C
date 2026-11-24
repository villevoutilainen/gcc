// D4324: std::contracts::format_operands never walks an input range: a
// subrange of istream_iterator would be consumed, and reporting a
// violation must not change the program.  Such an operand prints as '?',
// as does a tuple or a vector holding one; a forward range is printed.
// So does a range that can't be iterated through const (istream_view),
// instead of failing to compile.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <format>
#include <iterator>
#include <ranges>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace sc = std::contracts;

std::string last;

struct rich
{
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void operator() (const sc::assertion_context&) const { }
  template<typename... Ops>
    void operator() (const sc::assertion_context& c, const Ops&... ops) const
    {
      if (!c.check ())
	last = sc::format_operands (c, ops...);
    }
};
inline constexpr rich rich_v {};

using R = std::ranges::subrange<std::istream_iterator<int>,
				std::default_sentinel_t>;

void one (R r, bool ok) pre<rich_v> (ok && r.empty ()) { }
void in_tuple (std::tuple<int, R> t, bool ok)
  pre<rich_v> (ok && std::get<1> (t).empty ()) { }
void in_vector (std::vector<R> v, bool ok) pre<rich_v> (ok && v.empty ()) { }
void forward (std::vector<int> v, bool ok) pre<rich_v> (ok && v.empty ()) { }
using IV = std::ranges::basic_istream_view<int, char>;
void view (IV& iv, bool ok) pre<rich_v> (ok && &iv != nullptr) { }

int
main ()
{
  std::istringstream in ("1 2 3 4 5");
  R r (std::istream_iterator<int> (in), std::default_sentinel);

  one (r, false);
  if (last != "false && r.empty () [r: ?]")
    __builtin_abort ();
  in_tuple ({7, r}, false);
  if (last != "false && std::get<1> (?).empty ()")
    __builtin_abort ();
  in_vector ({r}, false);
  if (last != "false && v.empty () [v: ?]")
    __builtin_abort ();
  forward ({1, 2, 3}, false);
  if (last != "false && v.empty () [v: [1, 2, 3]]")
    __builtin_abort ();

  std::istringstream in2 ("1 2 3");
  IV iv (in2);
  view (iv, false);
  if (last != "false && &? != nullptr")
    __builtin_abort ();

  // Nothing was consumed: the subrange's iterator holds 1, the stream the
  // rest.
  int x = 0;
  if (*r.begin () != 1 || !(in >> x) || x != 2)
    __builtin_abort ();
}
