// D4324 (experimental): a P3595 contract configuration as an ordinary
// control object.  The configuration, d4324-embed-config.bin (the compact
// form of d4324-embed-config.json, written by d4324-embed-config-to-
// compact.py), is read with #embed and parsed at compile time; one named
// control object's compute_semantic query matches its rules -- "kind",
// "namespace", "function", "location" and "caller" -- against
// assertion_declaration_info, and the semantic it computes is the one the
// rest of the control object sees.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -freflection -fcontract-evaluation-semantic=ignore -fcontracts-client-check=pre" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "d4324-embed-config-compact.h"

inline constexpr unsigned char config_data[] = {
#embed "d4324-embed-config.bin"
};
inline constexpr cfg::rules_t rules = cfg::parse_compact (config_data);

#include "d4324-embed-config-demo.h"
