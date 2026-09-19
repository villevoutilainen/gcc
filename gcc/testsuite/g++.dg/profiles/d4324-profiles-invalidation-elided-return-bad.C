// P3446R0 Invalidation profile: companion to d4324-profiles-
// invalidation-no-dangling-elided-return-ok.C -- the identical shape,
// minus the std::no_dangling() wrapper, must still be correctly
// diagnosed. Confirms the ok test's clean compile is genuinely due to
// no_dangling()'s own effect on this elided-return-chain shape, not
// ip_resolve_nrv_call/ip_call_returns_type_p (invalidation-profile-
// gimple.cc) having somehow made the checker inert here for some
// other reason -- a bare RESULT_DECL constructed via guaranteed copy
// elision through create_widget()'s own call is still correctly
// traced to the nearest producing call and its arguments (factory's
// own contents, built from &log) checked, same as ever. Widget needs
// a real pointer-capable field ('int *w;', matching the canonical
// Widget every other test in this family already uses, e.g.
// d4324-profiles-invalidation-demo1-bad.C) -- an entirely fieldless
// Widget structurally cannot carry &log out no matter what
// create_widget() does, so ip_call_escapes_locally_p's own type-
// capacity gate (added alongside ip_var_contents_escape_locally_p's
// pre-existing, identical one) correctly treats it as safe, exactly
// like an empty struct returned directly.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct Window { };
struct Logger { void log (const char *); };
struct Widget { Widget (Window); int *w; };

struct WidgetFactory
{
  Window *window_;
  Logger *logger_ = nullptr;
  WidgetFactory (Window &w, Logger &l) : window_ (&w), logger_ (&l) { }
  WidgetFactory (Window &w) : window_ (&w) { }

  Widget create_widget ()
  {
    if (logger_) logger_->log ("Creating new Widget");
    return Widget { *window_ };
  }
};

Widget inner (Window &window)
{
  Logger log;
  WidgetFactory factory (window, log);
  return factory.create_widget (); // { dg-error "may hold a pointer to a local" }
}
