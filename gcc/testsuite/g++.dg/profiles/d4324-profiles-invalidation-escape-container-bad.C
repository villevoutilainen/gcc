// P3446R0 Invalidation profile: a container built from a pointer to a
// local must not be returned, and neither may a value derived from a
// method call on such a container -- flagged by default even though
// the analyzer cannot see which field the call's result actually
// depends on (CppCon 2026 "Profiles" talk's own WidgetFactory/Logger
// worked example, slides 46-48).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

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
  return factory.create_widget (); // { dg-error "may hold a pointer to a local" }
}
