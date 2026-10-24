// P3446R0/P4296R0 Invalidation profile: a class returned by value,
// built in place.  A class too large for registers is written straight
// into the return slot ('<retval> = {};', '<retval>.tm_year = 126;'),
// which the escape check didn't follow, so every such return was taken
// for a possibly dangling one -- 'std::tm t{}; t.tm_year = 126; return
// t;' among them.
// { dg-do compile { target c++17 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "ctime")]];
[[profiles::exempt(std::invalidation, angle_header: "cstring")]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <ctime>
#include <cstring>
#include <utility>

struct Big { int a[9]; long g; const char *z; };
static Big gb;
Big b_direct () { return Big{}; }
Big b_local () { Big t{}; return t; }
Big b_global () { return gb; }
Big b_literal () { Big t{}; t.z = "lit"; return t; }

std::tm t_zero () { std::tm t{}; return t; }
std::tm t_year () { std::tm t{}; t.tm_year = 126; return t; }
std::tm t_by_value (std::tm in) { return in; }
std::tm t_memset () { std::tm t; std::memset (&t, 0, sizeof t); return t; }
std::tm t_mktime () { std::tm t{}; t.tm_year = 126; std::mktime (&t); return t; }
static std::tm g_tm;
std::tm t_global () { return g_tm; }

// std::no_dangling's result written straight into the return slot
// ('<retval> = std::no_dangling<tm> (t);') is what's returned -- not a
// local of the same type ending nearby, which the escape check used to
// take for it, defeating the escape hatch for every class returned in
// memory.
std::tm t_localtime_r ()
{
  std::time_t now = std::time (nullptr);
  std::tm t;
  localtime_r (&now, &t);
  return std::no_dangling (t);
}
void fill (Big *, const long *);
Big b_filled () { long n = 1; Big t; fill (&t, &n); return std::no_dangling (t); }
