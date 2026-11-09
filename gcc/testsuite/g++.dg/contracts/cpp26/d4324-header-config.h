// Contract labels configured by data in a header -- pure library code:
// a label reads the rules (config::rules, d4324-header-config-data.h)
// when it is constructed, decides its semantic for each kind of
// assertion from those covering its group, and keeps the decisions as
// members that its non-static is_ignored and operator() read.
#ifndef D4324_HEADER_CONFIG_H
#define D4324_HEADER_CONFIG_H

#include <contracts>
#include <string_view>

namespace config {

using std::contracts::evaluation_semantic;
using std::contracts::assertion_kind;

// A rule's kind: any, or one of std::contracts::assertion_kind's.
enum rule_kind : unsigned char { kind_any = 0, kind_pre = 1, kind_post = 2,
				 kind_assert = 3 };

struct rule
{
  const char *group;
  rule_kind kind;
  evaluation_semantic semantic;
};

// Whether group G covers NAME: NAME is G, or nested in it ("G.x").
constexpr bool
covers (std::string_view g, std::string_view name)
{
  return name == g || (name.size () > g.size () && name.starts_with (g)
		       && name[g.size ()] == '.');
}

template <const auto& Rules>
struct label
{
  const char *name;
  // For pre, post and contract_assert: the semantic of the first rule
  // that matches, or 0: the build's.
  unsigned char sem[4] = {};

  consteval label (const char *n) : name (n)
  {
    for (unsigned k = 1; k <= 3; ++k)
      for (const rule& r : Rules)
	if ((r.kind == kind_any || r.kind == k) && covers (r.group, n))
	  {
	    sem[k] = (unsigned char) r.semantic;
	    break;
	  }
  }

  constexpr evaluation_semantic
  semantic (assertion_kind k, evaluation_semantic build) const
  {
    unsigned char s = sem[(unsigned) k];
    return s ? evaluation_semantic (s) : build;
  }

  constexpr bool
  is_ignored (std::contracts::assertion_static_info i) const noexcept
  {
    return semantic (i.kind (), i.semantic ())
	   == evaluation_semantic::ignore;
  }

  constexpr void
  operator() (const std::contracts::assertion_context& c) const
  {
    if (c.check ())
      return;
    auto info = c.info ();
    info.set_semantic (semantic (c.kind (), c.semantic ()));
    std::contracts::invoke_violation_handler
      (info, std::contracts::detection_mode::predicate_false);
    if (info.semantic () != evaluation_semantic::observe)
      __builtin_trap ();
  }
};

} // namespace config

#endif
