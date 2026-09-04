// P3446R0 Invalidation profile: std::no_dangling() (<utility>) is a
// manual, unproven "this doesn't dangle" assertion -- the
// invalidation profile's analogue of std::now_init() for the
// initialization profile -- wrapping the same WidgetFactory/Logger
// call d4324-profiles-invalidation-escape-container-bad.C flags
// suppresses that diagnostic.
//
// This explicit exemption is still needed here, even though system
// headers are auto-exempt (profiles_header_exempt_p's own
// in_system_header_at check, profiles.cc) and real installed/Compiler
// Explorer usage would need no exemption at all: this in-tree
// DejaGnu run builds against the not-yet-installed build tree via
// '-nostdinc++'/explicit '-I', which never marks anything as a
// system header the way a normal compiler invocation's own built-in
// default include path does -- see d4324-profiles-system-header-
// exempt-ok.C for a dedicated test of the auto-exemption itself, via
// '-isystem'. Exemption is transitive (profiles_header_exempt_p):
// exempting <utility> itself also covers every implementation-detail
// header it transitively #includes (bits/stl_pair.h,
// bits/stl_algobase.h, etc., plus <new>/<concepts>/<compare>/
// <type_traits>/<source_location>/<contracts> at whichever dialect
// pulls them in) -- none of those are what this test is actually
// about, and naming them individually would be non-portable
// (implementation-detail header sets and names are not part of the
// standard and can differ across implementations or library
// versions).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

struct Window { };
struct Logger { };
struct Widget { int *w; };

struct WidgetFactory
{
  Window *window_;
  Logger *logger_;
  WidgetFactory (Window &w, Logger &l) : window_ (&w), logger_ (&l) { }
  Widget create_widget () { return Widget {}; }
};

Widget inner (Window &window)
{
  Logger log;
  WidgetFactory factory (window, log);
  return std::no_dangling (factory.create_widget ());
}
