// D4324, QoI: a P3595 configuration's "group" output assigns a matching
// assertion a group, in a first pass over the entries with one; the
// other entries' "group" match then sees it as if the code had given it.
// A control object reads it as assertion_static_info::configured_group().
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"namespace":"vendor"},"output":{"group":"vendor.io"}},{"match":{"group":"vendor"},"output":{"semantic":"observe"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <string_view>
namespace sc = std::contracts;

int handler_calls, peeked;

void
handle_contract_violation (const sc::contract_violation& v)
{
  if (v.semantic () != sc::evaluation_semantic::observe)
    __builtin_abort ();
  ++handler_calls;
}

namespace cfg
{
  struct peek
  {
    static constexpr bool is_ignored (sc::assertion_static_info i)
    { return std::string_view (i.configured_group ()) != "vendor.io"; }
    static constexpr bool constify (sc::assertion_static_info) { return false; }
    static constexpr bool assumable (sc::assertion_static_info) { return false; }
    void operator() (const sc::assertion_context& c) const
    {
      if (std::string_view (c.static_info ().configured_group ())
	  == "vendor.io")
	++peeked;
    }
  };
  inline constexpr peek peek_v {};
}

namespace vendor
{
  int f (int x) pre (x > 0) { return x; }		// observe
  int g (int x) pre<cfg::peek_v> (x > 0) { return x; }
}

int unconfigured (int x) pre (x > 0) { return x; }	// ignore

int
main ()
{
  vendor::f (0);
  if (handler_calls != 1)
    __builtin_abort ();
  vendor::g (0);
  if (peeked != 1)
    __builtin_abort ();
  unconfigured (0);
  if (handler_calls != 1)
    __builtin_abort ();
}
