// P3446R0 / P4296R0 Invalidation profile, worked demo #1 (also
// distributed as a standalone Compiler Explorer demo): three
// functions telling one story.
//
//  1) purely_safe():        happy path, no annotation needed at all.
//  2) needs_no_dangling():  happy path, but ONLY because of
//                           std::no_dangling() -- remove it and this
//                           function alone starts failing to compile
//                           with exactly the diagnostic (3) gets.
//  3) diagnosed():          the exact same shape as (2), without the
//                           assertion -- rejected by the profile.
//
// A real (installed, or Compiler Explorer) g++ finds its own bundled
// <utility> via its built-in system include path, so no explicit
// profiles::exempt is needed there at all (system headers are auto-
// exempt, in_system_header_at, profiles.cc); this in-tree DejaGnu run
// builds against the not-yet-installed tree via plain '-I', which
// never gets that treatment, so the exemption below is a test-harness
// artifact, not part of the demo itself -- see d4324-profiles-system-
// header-exempt-ok.C for a dedicated test of the real default.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

int global_value = 42;

// (1) Returning a pointer to a global, or a pointer you were handed,
// never dangles -- ordinary code, nothing to annotate.
int *purely_safe (int *received)
{
  if (received)
    return received;
  return &global_value;
}

// A small "factory" shape, straight out of Bjarne's own CppCon 2026
// "Profiles" talk (the WidgetFactory/Logger worked example).
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

// (2) "log" is a local. The profile can't see far enough into
// create_widget() to prove its result doesn't depend on it, so by
// default this would be flagged -- std::no_dangling() is the manual,
// unproven "I promise this doesn't dangle" assertion that lets it
// through.
Widget needs_no_dangling (Window &window)
{
  Logger log;
  WidgetFactory factory (window, log);
  return std::no_dangling (factory.create_widget ());
}

// (3) Identical shape, no assertion -- diagnosed.
Widget diagnosed (Window &window)
{
  Logger log;
  WidgetFactory factory (window, log);
  return factory.create_widget (); // { dg-error "may hold a pointer to a local" }
}
