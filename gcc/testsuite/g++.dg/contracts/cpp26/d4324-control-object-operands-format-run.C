// D4324: assertion_static_info gives a control object its condition's
// operands' names (operand_count, operand_name) and the condition as a
// std::format string over them (operand_format), so it can report the
// actual values.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace sc = std::contracts;

std::string msg, names;

struct format_cco
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  void operator() (const sc::assertion_context &c) const { (void) c.check (); }
  template <class... T>
  void operator() (const sc::assertion_context &c, const T &...ops) const
  {
    if (c.check ())
      return;
    const sc::assertion_static_info &info = c.static_info ();
    names.clear ();
    for (std::size_t k = 0; k < info.operand_count (); ++k)
      names += std::string (info.operand_name (k)) + ";";
    if constexpr ((std::formattable<T, char> && ...))
      msg = std::vformat (info.operand_format (), std::make_format_args (ops...));
    else
      msg = info.comment ();
  }
};

// Compile-time access from a query.
struct query_cco : format_cco
{
  static constexpr bool is_ignored (sc::assertion_static_info i) noexcept
  { return i.operand_count () == 99; }
};

int f (int i, int x) pre<format_cco{}> (i < x) { return i; }
int g (int x, int i, int j) pre<format_cco{}> (x < i && x > j) { return x; }
int h (const std::vector<int> &v, unsigned i) pre<format_cco{}> (i < v.size ()) { return 0; }
struct S
{
  int d_size = 7;
  int m (int n) const pre<format_cco{}> (d_size < n) { return n; }
};
int braces (int a) pre<format_cco{}> (a == std::string_view ("{x}").size ()) { return a; }
int pr (const int x) post<format_cco{}> (r : r > x) { return x; }
int q (int i) pre<query_cco{}> (i > 0) { return i; }

int
main ()
{
  f (7, 5);
  if (msg != "7 < 5" || names != "i;x;")
    __builtin_abort ();
  g (1, 5, 3);
  if (msg != "1 < 5 && 1 > 3" || names != "x;i;j;")
    __builtin_abort ();
  h ({1, 2}, 4);
  if (msg != "4 < v.size ()" || names != "i;v;")
    __builtin_abort ();
  S s;
  s.m (2);
  if (names != "this;n;")
    __builtin_abort ();
  braces (1);
  if (msg != "1 == std::string_view (\"{x}\").size ()")
    __builtin_abort ();
  pr (4);
  if (msg != "4 > 4" || names != "r;x;")
    __builtin_abort ();
  q (0);
  if (names != "i;")
    __builtin_abort ();
}
