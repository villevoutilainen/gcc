// D4324: a control object's groups() must be a member function callable
// with no arguments that returns a range of names; anything else named
// groups is an error, not a control object in no group.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"group":"perf"},"output":{"semantic":"observe"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

struct base_ctrl
{
  static constexpr bool is_ignored (assertion_static_info) { return false; }
  static constexpr bool constify (assertion_static_info) { return false; }
  static constexpr bool assumable (assertion_static_info) { return false; }
  void operator() (const assertion_context& c) const { if (!c.check ()) __builtin_abort (); }
};

// A data member, not a function.
struct data_ctrl : base_ctrl
{
  const char *groups = "perf";
};
inline constexpr data_ctrl data_v {};

// Needs an argument.
struct arg_ctrl : base_ctrl
{
  static constexpr const char *const *groups (int) { return nullptr; }
};
inline constexpr arg_ctrl arg_v {};

// Returns one name, not a range of names.
struct one_ctrl : base_ctrl
{
  static constexpr const char *groups () { return "perf"; }
};
inline constexpr one_ctrl one_v {};

int f (int x) pre<data_v>(x > 0) { return x; } // { dg-error "'groups' of control object 'const data_ctrl' is not a member function" }
int g (int x) pre<arg_v>(x > 0) { return x; } // { dg-error "'groups' of control object 'const arg_ctrl' cannot be called with no arguments" }
int h (int x) pre<one_v>(x > 0) { return x; } // { dg-error "'groups' of control object 'const one_ctrl' does not return a range of names" }
