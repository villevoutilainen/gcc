// D4324 (experimental): a "group" output in a P3595 contract
// configuration read by a control object -- (its compact form, written by d4324-embed-config-to-compact.py, read with #embed) --
// as in the compiler's own configuration: a first pass assigns the group,
// and the rules naming a group match it; without one, they match the
// group the compiler's configuration assigns
// (assertion_static_info::configured_group()).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -freflection -fcontract-evaluation-semantic=ignore" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"namespace":"lib"},"output":{"group":"lib.core"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "d4324-embed-config-compact.h"

inline constexpr unsigned char config_data[] = {
#embed "d4324-embed-config-group-output.bin"
};
inline constexpr cfg::rules_t rules = cfg::parse_compact (config_data);

#include "d4324-embed-config-group-output-demo.h"
