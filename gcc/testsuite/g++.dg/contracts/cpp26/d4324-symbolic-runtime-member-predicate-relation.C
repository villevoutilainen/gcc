// D4324: -fcontract-symbolic-runtime-checks has no record for a relation
// between two objects, 'is_linked (o)' on a non-static member predicate.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct node {
  bool is_linked (node*) symbolic;
  void send (node* o) pre<symbolic_ctrl_v>(is_linked (o)) {} // { dg-error "unsupported conjunct" }
};
