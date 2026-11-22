// D4324: std::contracts::assertion_info_for follows the P3595
// configuration for the site it is given: by kind, by compile time or run
// time, and by namespace, by default the caller's and otherwise the one
// passed explicitly.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -freflection" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"namespace":"lib"},"output":{"semantic":"ignore"}},{"match":{"kind":"pre"},"output":{"semantic":"observe"}},{"match":{"constexpr":true},"output":{"semantic":"quick_enforce"}}\]} }

#include <contracts>
#include <meta>

namespace sc = std::contracts;
using es = sc::evaluation_semantic;

struct cco
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  constexpr void operator() (const sc::assertion_context &) const {}
};

namespace lib
{
  consteval es here ()
  { return sc::assertion_info_for (cco{}, sc::assertion_kind::assert).semantic (); }
}

consteval es at_global ()
{ return sc::assertion_info_for (cco{}, sc::assertion_kind::assert).semantic (); }

static_assert (lib::here () == es::ignore);			// namespace rule
static_assert (at_global () == es::enforce);			// no rule: the default
static_assert (sc::assertion_info_for (cco{}, sc::assertion_kind::pre).semantic ()
	       == es::observe);					// kind rule
static_assert (sc::assertion_info_for (cco{}, sc::assertion_kind::assert, true).semantic ()
	       == es::quick_enforce);				// constexpr rule
// An explicit namespace.
static_assert (sc::assertion_info_for (cco{}, sc::assertion_kind::assert, false,
				       std::source_location::current (),
				       std::meta::info {}, ^^lib).semantic ()
	       == es::ignore);
