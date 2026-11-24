// D4324: std::contracts::format_operands_to and format_operands, for a
// control object of the user's own: a violated assertion's condition
// with its operands' values in place, then, in brackets, the operands
// the format string leaves as text (the objects of member calls), each
// printed with std::format -- strings and characters quoted and escaped,
// a tuple and a vector as the standard library formats them.  Into a
// buffer on the stack, so the violation path allocates nothing.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

namespace sc = std::contracts;

int calls;
char comment[512];
char message[512];

void
handle_contract_violation (const sc::contract_violation& v)
{
  ++calls;
  std::strcpy (comment, v.comment ());
  std::strcpy (message, v.message () ? v.message () : "(null)");
}

// Reports a violation with its operands' values as the message.
struct values_cco
{
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }

  void operator() (const sc::assertion_context& c) const
  {
    if (!c.check ())
      __builtin_abort ();	// every condition below has operands
  }

  template<typename... Ops>
    void operator() (const sc::assertion_context& c, const Ops&... ops) const
    {
      if (c.check ())
	return;
      char buf[512];
      sc::assertion_info info = c.info ();
      info.set_message (sc::format_operands_to (buf, c, ops...));
      sc::invoke_violation_handler (info, sc::detection_mode::predicate_false);
    }
};

int
f (const std::string& s, std::tuple<int, std::string> t, std::vector<int> v,
   char ch)
  pre<values_cco{}> (s.size () > 3 && std::get<0> (t) > 0 && v.empty ()
		     && ch != 'x')
{ return 0; }

int g (int n, const std::string& s) pre<values_cco{}> (n < s.size ()) { return 0; }

// A buffer too small: truncated, still terminated.
struct small_cco : values_cco
{
  using values_cco::operator();
  template<typename... Ops>
    void operator() (const sc::assertion_context& c, const Ops&... ops) const
    {
      if (c.check ())
	return;
      char buf[6];
      sc::assertion_info info = c.info ();
      info.set_message (sc::format_operands_to (buf, c, ops...));
      sc::invoke_violation_handler (info, sc::detection_mode::predicate_false);
    }
};
int h (int n) pre<small_cco{}> (n > 1000) { return 0; }

static void
expect (const char *c, const char *m)
{
  if (std::strcmp (comment, c) || std::strcmp (message, m))
    __builtin_abort ();
}

int
main ()
{
  f ("", {0, "a\"b"}, {1, 2, 3}, 'x');
  expect ("s.size () > 3 && std::get<0> (t) > 0 && v.empty () && ch != 'x'",
	  "s.size () > 3 && std::get<0> ((0, \"a\\\"b\")) > 0 && v.empty () "
	  "&& 'x' != 'x' [s: \"\", v: [1, 2, 3]]");
  g (5, "hello");
  expect ("n < s.size ()", "5 < s.size () [s: \"hello\"]");
  h (7);
  expect ("n > 1000", "7 > 1");
  if (calls != 3)
    __builtin_abort ();
}
