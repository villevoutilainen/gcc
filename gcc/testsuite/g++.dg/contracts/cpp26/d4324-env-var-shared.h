// Shared by d4324-env-var-set.C / d4324-env-var-unset.C: byte-for-byte the
// same control object, function, and main() body are compiled in both --
// a control object can decide, at run time, using arbitrary library code
// (here, an environment variable), whether to enforce a violated
// precondition, independent of and in addition to the compile-time
// is_ignored mechanism.  Only whether the environment variable is
// actually set when the resulting binary runs differs between the two
// umbrella tests (via dg-set-target-env-var in exactly one of them), and
// that alone is what makes this same program correctly print one thing
// or the other.

#include <contracts>
#include <cstdio>
#include <cstdlib>

namespace sc = std::contracts;

struct my_less_mandatory {
  static constexpr bool
  is_ignored (sc::evaluation_config cfg) noexcept
  { return cfg == sc::evaluation_config::ignore; }

  static constexpr bool constify  = true;
  static constexpr bool assumable = false;

  void
  operator() (const sc::assertion_context& ctx) const
  {
    if (std::getenv ("D4324_ENV_VAR_TEST_IGNORE"))
      std::puts ("ignored via environment variable");
    else if (!ctx.check ())
      std::puts ("would terminate: precondition violated");
  }
};

inline constexpr my_less_mandatory my_less_mandatory_v{};

int g (int x) pre<my_less_mandatory_v>(x >= 0) { return x; }

int main ()
{
  g (-42);  // violates the precondition either way
  return 0;
}
