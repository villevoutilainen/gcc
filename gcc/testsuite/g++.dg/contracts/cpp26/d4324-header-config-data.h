// The build's contract configuration for d4324-header-config-run.C, as
// constant data in a header: which one a build uses is chosen by which
// header it includes (-include, an include path).  Each rule names a
// group (covering the groups nested in it: "lib" covers "lib.io"), an
// assertion kind or any, and a semantic; the first rule that matches
// decides.
#ifndef D4324_HEADER_CONFIG_DATA_H
#define D4324_HEADER_CONFIG_DATA_H

#include "d4324-header-config.h"

namespace config {
  using enum std::contracts::evaluation_semantic;
  inline constexpr rule rules[] = {
    { "lib.io",  kind_any,  observe },
    { "lib",     kind_post, ignore },
    { "lib",     kind_any,  enforce },
    { "app",     kind_pre,  observe },
  };
}

#endif
