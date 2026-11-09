// D4324 (experimental): contract groups need no compiler support.  A
// group is a control object's own data, so a label reads the build's
// configuration itself -- d4324-embed-config-groups.json, read with
// #embed and parsed at compile time -- decides its semantic for its
// group, hierarchically ("lib" covers "lib.net"), when it is constructed,
// and keeps it as a member its is_ignored and operator() read.  No
// compute_semantic, no group_names, no -fcontracts-group-evaluation-
// semantic; a group no rule names gets the build's semantic.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -freflection -fcontract-evaluation-semantic=enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "d4324-embed-config-json.h"

inline constexpr unsigned char config_data[] = {
#embed "d4324-embed-config-groups.json"
};
inline constexpr cfg::rules_t rules = cfg::parse_json (config_data);

using namespace std::contracts;

int reports;
evaluation_semantic last;

void
handle_contract_violation (const contract_violation& v)
{
  ++reports;
  last = v.semantic ();
  if (v.semantic () == evaluation_semantic::enforce)
    throw 0;
}

struct group_label
{
  const char *name;
  unsigned char sem;		// The configuration's, or 0: the build's.

  consteval group_label (const char *n)
    : name (n), sem (rules.semantic_for_group (n)) { }

  constexpr evaluation_semantic
  semantic (evaluation_semantic build) const
  { return sem ? evaluation_semantic (sem) : build; }

  constexpr bool
  is_ignored (assertion_static_info i) const noexcept
  { return semantic (i.semantic ()) == evaluation_semantic::ignore; }

  constexpr void
  operator() (const assertion_context& c) const
  {
    if (c.check ())
      return;
    auto info = c.info ();
    info.set_semantic (semantic (c.semantic ()));
    invoke_violation_handler (info, detection_mode::predicate_false);
    if (info.semantic () != evaluation_semantic::observe)
      std::terminate ();
  }
};

inline constexpr group_label lib_io {"lib.io"};
inline constexpr group_label lib_net {"lib.net"};
inline constexpr group_label app {"app"};

void io (int x) pre<lib_io> (x > 0) { }
void net (int x) pre<lib_net> (x > 0) { }
void run (int x) pre<app> (x > 0) { }

int
main ()
{
  io (0);			// "lib.io": observe.
  if (reports != 1 || last != evaluation_semantic::observe)
    __builtin_abort ();
  net (0);			// "lib" covers "lib.net": ignore.
  if (reports != 1)
    __builtin_abort ();
  try { run (0); __builtin_abort (); } catch (int) { }	// The build's.
  if (reports != 2 || last != evaluation_semantic::enforce)
    __builtin_abort ();
}
