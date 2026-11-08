// D4324: a control object can give the violation handler a message
// alongside the predicate text (assertion_info::set_message,
// contract_violation::message); a plain contract has none.  A control
// object's queries see the predicate text at compile time
// (assertion_static_info::comment).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstring>
#include <string_view>
namespace sc = std::contracts;

const char *last_comment, *last_message;
int violations;
void handle_contract_violation (const sc::contract_violation &v)
{
  ++violations;
  last_comment = v.comment ();
  last_message = v.message ();
}

struct msg {
  const char *text;
  static constexpr bool is_ignored (sc::assertion_static_info si)
  { return std::string_view (si.comment ()) == "ignored_me"; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void operator() (const sc::assertion_context &c) const
  {
    if (c.check ())
      return;
    if (std::strcmp (c.static_info ().comment (), c.comment ()) != 0)
      __builtin_abort ();
    auto i = c.info ();
    i.set_message (text);
    sc::invoke_violation_handler (i, sc::detection_mode::predicate_false);
  }
};

bool ignored_me = false;
int f (int x) pre<msg{"x must be positive"}> (x > 0) { return x; }
int g (int x) pre<msg{"never reported"}> (ignored_me) { return x; }
int h (int x) pre (x > 0) { return x; }

int
main ()
{
  f (-1);
  if (violations != 1 || std::strcmp (last_comment, "x > 0") != 0
      || std::strcmp (last_message, "x must be positive") != 0)
    __builtin_abort ();
  g (1);		// ignored: its query saw the predicate text
  if (violations != 1)
    __builtin_abort ();
  h (-1);
  if (violations != 2 || std::strcmp (last_comment, "x > 0") != 0
      || last_message != nullptr)
    __builtin_abort ();
}
