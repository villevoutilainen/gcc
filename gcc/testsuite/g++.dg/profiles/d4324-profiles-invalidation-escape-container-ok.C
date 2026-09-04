// P3446R0 Invalidation profile: the same WidgetFactory shape as
// d4324-profiles-invalidation-escape-container-bad.C, but built
// entirely from received references -- confirms the container-escape
// check isn't simply flagging every such method call unconditionally.
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

Widget inner (Window &window, Logger &log)
{
  WidgetFactory factory (window, log);
  return factory.create_widget ();
}
