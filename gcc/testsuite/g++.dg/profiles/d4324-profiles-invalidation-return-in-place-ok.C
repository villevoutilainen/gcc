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

#include <ctime>
#include <cstring>

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
