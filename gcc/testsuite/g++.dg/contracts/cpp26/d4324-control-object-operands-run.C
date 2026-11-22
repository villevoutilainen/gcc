// D4324: a control object whose operator() accepts the assertion context
// followed by the condition's operands is called with them: each variable
// the condition names (a parameter, this, a local, a lambda's capture, a
// postcondition's result), once, in order of first appearance in the
// condition as written, as a const lvalue.  Here a custom violation
// handling function receives them as a vector<any>.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <any>
#include <string>
#include <vector>

namespace sc = std::contracts;

std::vector<std::any> seen;
int plain_calls, operand_calls;

void
handle_violation (const sc::assertion_context &, std::vector<std::any> ops)
{
  seen = std::move (ops);
}

struct report_cco
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  void operator() (const sc::assertion_context &c) const
  {
    ++plain_calls;
    (void) c.check ();
  }
  template <class... T>
  void operator() (const sc::assertion_context &c, const T &...ops) const
  {
    ++operand_calls;
    if (!c.check ())
      handle_violation (c, std::vector<std::any>{ std::any (ops)... });
  }
};

int f (int i, int x) pre<report_cco{}> (i < x) { return i; }
int g (int x, int i, int j) pre<report_cco{}> (x < i && x > j) { return x; }
int unused (int a, int b, int c) pre<report_cco{}> (c < a) { return b; }
long h (int i, std::string s)
{
  long n = 3;
  // Folding makes this '(long) i > n'; the order is the source's.
  contract_assert<report_cco{}> (n < i || s.size () > 10);
  return n;
}
template <class T> T tf (T a, T b) pre<report_cco{}> (a < b) { return a; }
struct S
{
  int d = 9;
  int m (int n) const pre<report_cco{}> (d < n) { return n; }
};
int pr (const int x) post<report_cco{}> (r : r > x) { return x; }
int lam (int k)
{
  auto l = [k] (int q) { contract_assert<report_cco{}> (k < q); return q; };
  return l (1);
}

template <class T>
T
at (unsigned i)
{
  return std::any_cast<T> (seen.at (i));
}

int
main ()
{
  f (7, 5);
  if (seen.size () != 2 || at<int> (0) != 7 || at<int> (1) != 5)
    __builtin_abort ();
  g (1, 5, 3);
  if (seen.size () != 3 || at<int> (0) != 1 || at<int> (1) != 5
      || at<int> (2) != 3)
    __builtin_abort ();
  unused (1, 2, 3);
  if (seen.size () != 2 || at<int> (0) != 3 || at<int> (1) != 1)
    __builtin_abort ();
  h (2, "ab");
  if (seen.size () != 3 || at<long> (0) != 3 || at<int> (1) != 2
      || at<std::string> (2) != "ab")
    __builtin_abort ();
  tf (3.5, 1.5);
  if (seen.size () != 2 || at<double> (0) != 3.5 || at<double> (1) != 1.5)
    __builtin_abort ();
  S s;
  s.m (2);
  if (seen.size () != 2 || at<const S *> (0) != &s || at<int> (1) != 2)
    __builtin_abort ();
  pr (4);
  if (seen.size () != 2 || at<int> (0) != 4 || at<int> (1) != 4)
    __builtin_abort ();
  lam (5);
  if (seen.size () != 2 || at<int> (0) != 5 || at<int> (1) != 1)
    __builtin_abort ();
  if (plain_calls != 0 || operand_calls != 8)
    __builtin_abort ();
}
