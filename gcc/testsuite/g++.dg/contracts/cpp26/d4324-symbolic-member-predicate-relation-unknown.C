// D4324: a non-static member symbolic predicate given another object,
// 'is_linked (o)', relates two objects -- not a fact the analysis can
// track, so its precondition can never be established.  It's reported,
// never silently skipped.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct node {
  bool is_linked (node*) const symbolic;
  void link (node* const o) post<symbolic_ctrl_v>(is_linked (o));
  void send (node* o) pre<symbolic_ctrl_v>(is_linked (o));
};

void f (node& a, node& b)
{
  a.link (&b);
  a.send (&b); // { dg-warning "cannot verify that 'bool node::is_linked\\(node\\*\\) const' holds" }
  // { dg-message "tracked only for one object" "" { target *-*-* } .-1 }
}
