// P3446R0 Invalidation profile: std::no_dangling() (<utility>) is a
// manual, unproven "this doesn't dangle" assertion -- the
// invalidation profile's analogue of std::now_init() for the
// initialization profile -- wrapping the same WidgetFactory/Logger
// call d4324-profiles-invalidation-escape-container-bad.C flags
// suppresses that diagnostic.  Exemption is transitive
// (profiles_header_exempt_p, profiles.cc): exempting <utility> itself
// also covers every implementation-detail header it transitively
// #includes (bits/stl_pair.h, bits/stl_algobase.h, etc., plus
// <new>/<concepts>/<compare>/<type_traits>/<source_location>/
// <contracts> at whichever dialect pulls them in) -- none of those
// are what this test is actually about, and naming them individually
// would be non-portable (implementation-detail header sets and names
// are not part of the standard and can differ across implementations
// or library versions).
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
