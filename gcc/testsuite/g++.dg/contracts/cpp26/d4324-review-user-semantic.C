// D4324: on a violation the compiler makes exactly one runtime call,
// T::operator()(comment, loc, cfg), and branches on the returned
// violation_response (contract-terminate on terminate, continue on proceed).
// This replaces the built-in __tu_has_violation semantic switch with a call to
// the user's control object - a user-defined semantic in a few lines of
// library code and no compiler change.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-evaluation-semantic=enforce -fdump-tree-gimple" }

namespace std {
struct source_location {
  constexpr source_location () = default;
};
namespace contracts {
enum class evaluation_config : unsigned {
  ignore = 0, observe = 1, enforce = 2, quick_enforce = 3
};
enum class violation_response { proceed, terminate };
}
}

bool logged;
struct review {
  static constexpr bool is_ignored (std::contracts::evaluation_config) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  std::contracts::violation_response
  operator() (const char *, std::source_location,
	      std::contracts::evaluation_config) const
  { logged = true; return std::contracts::violation_response::proceed; }
};

int f (int x) pre<review>(x > 0) { return x; }

// The user's operator() is called on violation, not a hard-coded semantic.
// { dg-final { scan-tree-dump "review::operator" "gimple" } }
// The built-in violation entry point is not used on the control path.
// { dg-final { scan-tree-dump-not "__tu_has_violation" "gimple" } }
