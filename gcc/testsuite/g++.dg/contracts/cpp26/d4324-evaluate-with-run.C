// D4324: std::contracts::assertion_info_for gives the static info an
// assertion naming a control object would have at a site, by the
// configuration and the control object's compute_semantic, and
// std::contracts::evaluate_with has the control object evaluate a
// predicate with it -- an assertion that isn't written as one.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -freflection -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstring>

namespace sc = std::contracts;

int checks, violations, pred_calls;
const char *last_comment, *last_message;
sc::evaluation_semantic last_semantic;
int last_ops;

struct record_cco
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  constexpr void operator() (const sc::assertion_context &c) const
  {
    ++checks;
    last_ops = 0;
    if (!c.check ())
      {
	++violations;
	last_comment = c.comment ();
	last_message = c.message ();
	last_semantic = c.semantic ();
      }
  }
  void operator() (const sc::assertion_context &c, const int &a, const int &b) const
  {
    (*this) (c);
    last_ops = a + b;
  }
};

// A control object whose compute_semantic overrides the configuration.
struct enforcing_cco : record_cco
{
  static constexpr sc::evaluation_semantic
  compute_semantic (sc::assertion_static_info) noexcept
  { return sc::evaluation_semantic::enforce; }
};

namespace ns
{
  constexpr sc::assertion_static_info site_info ()
  { return sc::assertion_info_for (record_cco{}, sc::assertion_kind::assert); }
}

const char *
lazy_message_calls (int &n)
{
  ++n;
  return "lazy";
}

int
main ()
{
  // The configuration's semantic, and the call's location.
  constexpr sc::assertion_static_info i1
    = sc::assertion_info_for (record_cco{}, sc::assertion_kind::assert);
  static_assert (i1.semantic () == sc::evaluation_semantic::observe);
  static_assert (i1.kind () == sc::assertion_kind::assert);
  constexpr auto line = sc::assertion_info_for (record_cco{}, sc::assertion_kind::assert).location ().line ();
  static_assert (line == __LINE__ - 1);
  static_assert (i1.comment ()[0] == '\0');

  // compute_semantic applies.
  constexpr sc::assertion_static_info i2
    = sc::assertion_info_for (enforcing_cco{}, sc::assertion_kind::pre);
  static_assert (i2.semantic () == sc::evaluation_semantic::enforce);
  static_assert (i2.kind () == sc::assertion_kind::pre);

  // From a function in a namespace.
  constexpr sc::assertion_static_info i3 = ns::site_info ();
  static_assert (i3.semantic () == sc::evaluation_semantic::observe);

  // The predicate is evaluated only when the control object checks it.
  int x = 3;
  sc::evaluate_with (record_cco{}, i1, [&] { ++pred_calls; return x > 0; },
		     "x > 0");
  if (checks != 1 || violations != 0 || pred_calls != 1)
    __builtin_abort ();

  // A violation: comment, message, semantic reach the control object.
  sc::evaluate_with (record_cco{}, i1, [&] { return x > 5; }, "x > 5",
		     "x is too small");
  if (violations != 1 || std::strcmp (last_comment, "x > 5")
      || std::strcmp (last_message, "x is too small")
      || last_semantic != sc::evaluation_semantic::observe)
    __builtin_abort ();

  // A lazy message is computed only when asked for.
  int n = 0;
  sc::evaluate_with (record_cco{}, i1, [&] { return x > 0; }, "x > 0",
		     [&] { return lazy_message_calls (n); });
  if (n != 0)
    __builtin_abort ();
  sc::evaluate_with (record_cco{}, i1, [&] { return x > 5; }, "x > 5",
		     [&] { return lazy_message_calls (n); });
  if (n != 1 || std::strcmp (last_message, "lazy"))
    __builtin_abort ();

  // Operands, when the control object takes them.
  int a = 4, b = 2;
  sc::evaluate_with (record_cco{}, i1, [&] { return a < b; }, "a < b",
		     nullptr, a, b);
  if (last_ops != 6)
    __builtin_abort ();

  // Without a comment or message, both are empty (never null).
  sc::evaluate_with (record_cco{}, i1, [&] { return false; });
  if (!last_message || *last_message || !last_comment || *last_comment)
    __builtin_abort ();
}
