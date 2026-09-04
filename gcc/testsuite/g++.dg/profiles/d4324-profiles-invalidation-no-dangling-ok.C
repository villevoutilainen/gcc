// P3446R0 Invalidation profile: std::no_dangling() (<utility>) is a
// manual, unproven "this doesn't dangle" assertion -- the
// invalidation profile's analogue of std::now_init() for the
// initialization profile -- wrapping the same WidgetFactory/Logger
// call d4324-profiles-invalidation-escape-container-bad.C flags
// suppresses that diagnostic.  <utility>'s own transitively-included
// headers need exempting first: none of them are what this test is
// actually about (confirmed via direct probing, not guessed).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "new")]];
[[profiles::exempt(std::invalidation, angle_header: "concepts")]];
[[profiles::exempt(std::invalidation, angle_header: "compare")]];
[[profiles::exempt(std::invalidation, angle_header: "type_traits")]];
[[profiles::exempt(std::invalidation, angle_header: "bits/stl_pair.h")]];
[[profiles::exempt(std::invalidation, angle_header: "bits/stl_algobase.h")]];
[[profiles::exempt(std::invalidation, angle_header: "bits/stl_iterator_base_types.h")]];
[[profiles::exempt(std::invalidation, angle_header: "bits/stl_iterator_base_funcs.h")]];
[[profiles::exempt(std::invalidation, angle_header: "source_location")]];
[[profiles::exempt(std::invalidation, angle_header: "contracts")]];

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
