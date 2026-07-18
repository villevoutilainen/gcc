// D4324: constification is off by default, so a contract predicate binds the
// same overload the function body would - no silent divergence under overload
// resolution.  Naming a control type whose constify member is true restores
// constification for the assertions that name it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-evaluation-semantic=enforce" }

namespace std {
namespace contracts {
enum class evaluation_config : unsigned {
  ignore = 0, observe = 1, enforce = 2, quick_enforce = 3
};
}
}

struct constified {
  static constexpr bool is_ignored (std::contracts::evaluation_config) { return false; }
  static constexpr bool constify = true;
  static constexpr bool assumable = false;
};

struct S { bool probe (); bool probe () const; };
struct T { bool probe (); bool probe () const; };

int f (S s) pre (s.probe ()) { return 0; }		// default: non-const
int g (T t) pre<constified>(t.probe ()) { return 0; }	// opt-in: const

// Default: the predicate binds the non-const overload the body would bind ...
// { dg-final { scan-assembler "_ZN1S5probeEv" } }
// ... and never the const overload.
// { dg-final { scan-assembler-not "_ZNK1S5probeEv" } }
// A constify=true control restores constification: the const overload binds.
// { dg-final { scan-assembler "_ZNK1T5probeEv" } }
