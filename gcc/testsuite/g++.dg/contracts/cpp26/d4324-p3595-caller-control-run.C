// D4324: a P3595 configuration entry with "caller" and a "control"
// chooses the control object of the redundant caller-side check, for
// calls from the code its "caller" object matches: here the calls from
// ours::caller get cfg::audit_v.  The definition's own check keeps the
// declaration's control object (std::contracts::default_v, ignored by the
// translation unit's semantic); other callers get no caller-side check.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"caller":{"function":"ours::caller"},"function":"vendor::f"},"output":{"control":"cfg::audit_v"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int handler_calls, audit_calls;

void
handle_contract_violation (const sc::contract_violation&)
{
  ++handler_calls;
}

namespace cfg
{
  struct audit
  {
    static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
    static constexpr bool constify (sc::assertion_static_info) { return false; }
    static constexpr bool assumable (sc::assertion_static_info) { return false; }
    void operator() (const sc::assertion_context& c) const
    { if (!c.check ()) ++audit_calls; }
  };
  inline constexpr audit audit_v {};
}

namespace vendor
{
  int f (int x) pre (x > 0) { return x; }
}

namespace ours
{
  void caller () { vendor::f (0); }
  void other () { vendor::f (0); }
}

int
main ()
{
  ours::caller ();
  if (audit_calls != 1 || handler_calls != 0)
    __builtin_abort ();
  ours::other ();
  if (audit_calls != 1 || handler_calls != 0)
    __builtin_abort ();
}
