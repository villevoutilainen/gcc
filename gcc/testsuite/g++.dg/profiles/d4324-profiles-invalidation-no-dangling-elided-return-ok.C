// P3446R0 Invalidation profile: std::no_dangling() must also cure the
// exact WidgetFactory/Logger shape when the returned class type has a
// user-declared constructor (Widget(Window), as opposed to
// d4324-profiles-invalidation-no-dangling-ok.C's own trivial-
// aggregate Widget) -- under the Itanium C++ ABI, a class with ANY
// user-declared constructor is returned via an invisible reference
// (guaranteed copy elision straight through the whole
// create_widget()/no_dangling() call chain into the caller's own
// return slot), never via an ordinary by-value/register return the
// way a trivial aggregate is. ip_check_return_escape's own RETVAL is
// therefore a bare RESULT_DECL with no separately-clobbered named
// local anywhere to substitute in (ip_resolve_nrv_var finds nothing),
// and BOTH create_widget's own call and the no_dangling call wrapping
// it have a genuinely NULL gimple_call_lhs -- neither is a "VAR =
// call(...)"-shaped write ip_defines_var_p's own existing checks ever
// recognized. Confirmed this reproduces the user-reported false
// positive exactly (a Stroustrup CppCon "Profiles" talk worked
// example, the same one d4324-profiles-invalidation-no-dangling-ok.C
// and d4324-profiles-invalidation-demo1-bad.C are themselves drawn
// from) and that it is fixed by ip_resolve_nrv_call/ip_call_returns_
// type_p (invalidation-profile-gimple.cc): find the nearest preceding
// elided aggregate-returning call of matching return type and recurse
// into ip_call_escapes_locally_p on it directly, exactly as every
// other call-shaped value already does.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

struct Window { };
struct Logger { void log (const char *); };
struct Widget { Widget (Window); };

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
  return std::no_dangling (factory.create_widget ());
}
