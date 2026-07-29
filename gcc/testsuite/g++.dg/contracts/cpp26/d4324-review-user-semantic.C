// D4324: on a violation the compiler makes exactly one runtime call,
// T::operator()(comment, loc, cfg).  The operator returns void: returning
// means continue, and a terminating control terminates in its own body.
// This replaces the built-in __tu_has_violation semantic switch with a call to
// the user's control object - a user-defined semantic in a few lines of
// library code and no compiler change.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce -fdump-tree-gimple" }

#include <contracts>

bool logged;
struct review {
  static constexpr bool is_ignored (std::contracts::evaluation_semantic) { return false; }
  static constexpr bool constify (std::contracts::evaluation_semantic) { return false; }
  static constexpr bool assumable (std::contracts::evaluation_semantic) { return false; }
  void
  operator() (const std::contracts::assertion_context& ctx) const
  { if (ctx.check ()) return; logged = true; }
};

inline constexpr review review_v{};

int f (int x) pre<review_v>(x > 0) { return x; }

// The user's operator() is called on violation, not a hard-coded semantic.
// { dg-final { scan-tree-dump "review::operator" "gimple" } }
// The built-in violation entry point is not used on the control path.
// { dg-final { scan-tree-dump-not "__tu_has_violation" "gimple" } }
