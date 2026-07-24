// D4324: on a violation the compiler makes exactly one runtime call,
// T::operator()(comment, loc, cfg).  The operator returns void: returning
// means continue, and a terminating control terminates in its own body.
// This replaces the built-in __tu_has_violation semantic switch with a call to
// the user's control object - a user-defined semantic in a few lines of
// library code and no compiler change.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce -fdump-tree-gimple" }

namespace std {
struct source_location {
  constexpr source_location () = default;
};
namespace contracts {
enum class evaluation_config : unsigned {
  ignore = 0, observe = 1, enforce = 2, quick_enforce = 3
};
}
}

bool logged;
struct review {
  static constexpr bool is_ignored (std::contracts::evaluation_config) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  void
  operator() (const char *, std::source_location,
	      std::contracts::evaluation_config) const
  { logged = true; }
};

inline constexpr review review_v{};

int f (int x) pre<review_v>(x > 0) { return x; }

// The user's operator() is called on violation, not a hard-coded semantic.
// { dg-final { scan-tree-dump "review::operator" "gimple" } }
// The built-in violation entry point is not used on the control path.
// { dg-final { scan-tree-dump-not "__tu_has_violation" "gimple" } }
