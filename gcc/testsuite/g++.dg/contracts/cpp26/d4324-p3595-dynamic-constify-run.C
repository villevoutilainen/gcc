// D4324, QoI: P3595R0's "dynamic" output replacing a control object that
// constifies (chosen in the code, so it must be config_replaceable): the
// condition was parsed constified, so the compiler picks the constifying
// dynamic_control, dynamic_v<&F, true>.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fdump-tree-original" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{},"output":{"semantic":"observe","dynamic":{"name":"mylib::contract_semantic"}}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

int handler_calls;

void
handle_contract_violation (const sc::contract_violation&)
{
  ++handler_calls;
}

namespace cfg
{
  struct constifying
  {
    static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
    static constexpr bool constify (sc::assertion_static_info) { return true; }
    static constexpr bool assumable (sc::assertion_static_info) { return false; }
    static constexpr bool config_replaceable (sc::assertion_static_info) { return true; }
    void operator() (const sc::assertion_context&) const { __builtin_abort (); }
  };
  inline constexpr constifying constifying_v {};
}

int f (int x) pre<cfg::constifying_v> (x > 0) { return x; }

int
main ()
{
  f (0);
  if (handler_calls != 1)
    __builtin_abort ();
}

// { dg-final { scan-tree-dump {dynamic_control<_Fn, _Constify, _Sem>::operator\(\).*_Fn\)\(\) = mylib::contract_semantic; bool _Constify = true;} "original" } }
