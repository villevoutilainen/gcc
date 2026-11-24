// The program of d4324-embed-config-group-output-json-run.C and
// d4324-embed-config-group-output-compact-run.C, after their
// configuration (RULES): d4324-embed-config-group-output.json, or its
// compact form.  The translation unit's semantic is ignore.
inline constexpr cfg::configured<rules> configured_v{};

int reports;
void
handle_contract_violation (const std::contracts::contract_violation& v)
{
  if (v.semantic () != std::contracts::evaluation_semantic::observe)
    __builtin_abort ();
  ++reports;
}

// The library's configuration assigns "vendor.io"; its "vendor" rule
// matches it: observe.
namespace vendor { int f (int x) pre<configured_v> (x > 0) { return x; } }
// The compiler's configuration assigns "lib"; the library's "lib" rule
// matches it: observe.
namespace lib { int f (int x) pre<configured_v> (x > 0) { return x; } }
namespace app
{
  // Nothing matches: the translation unit's semantic.
  int f (int x) pre<configured_v> (x > 0) { return x; }
  // Its own entry, with both a group and a semantic, comes first among
  // those with a semantic: ignore, not the "special" rule's enforce.
  int special (int x) pre<configured_v> (x > 0) { return x; }
}

int
main ()
{
  vendor::f (0);
  lib::f (0);
  app::f (0);
  app::special (0);
  if (reports != 2)
    __builtin_abort ();
}
