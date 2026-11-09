// The program of d4324-embed-config-json-run.C and
// d4324-embed-config-compact-run.C, after its configuration (RULES):
// d4324-embed-config.json, or its compact form d4324-embed-config.bin.
#include <cstdio>

inline constexpr cfg::configured<rules> configured_v{};

int reports, observed;
void
handle_contract_violation (const std::contracts::contract_violation& v)
{
  ++reports;
  observed += v.semantic () == std::contracts::evaluation_semantic::observe;
}

namespace vendor { int f (int x) pre<configured_v> (x > 0) { return x; } }
namespace ours
{
  int g (int x) pre<configured_v> (x > 0) { return x; }
  int quiet (int x) pre<configured_v> (x > 0) { return x; }
  int p (int x) post<configured_v> (r: r > 0) { return x; }
  int lined (int x) pre<configured_v> (x > 0) { return x; }	// line 22
  void a (int x) { contract_assert<configured_v> (x > 0); }
  template <class T> T t (T x) pre<configured_v> (x > 0) { return x; }
}

int
main ()
{
  vendor::f (0);	// Checked on the caller's side only ("caller").
  ours::g (0);		// In its definition only ("namespace").
  ours::quiet (0);	// Ignored ("function").
  ours::p (0);		// Ignored ("kind").
  ours::lined (0);	// Ignored ("location").
  ours::a (0);		// A contract_assert ("namespace").
  ours::t (0);		// A function template's ("namespace").
  // All observe, although the translation unit's semantic is ignore.
  if (reports != 4 || observed != 4)
    __builtin_abort ();
}
