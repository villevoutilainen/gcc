// P3589: malformed profiles::suppress 'rule:'/'justification:' syntax
// must be a plain, single parse error with proper recovery -- never a
// cascade of follow-on errors, and never the ICE this whole rule:
// grammar addition was written to fix in the first place (see
// d4324-profiles-suppress-subrule-bad.C's own comment for the crash
// this is guarding against).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

[[profiles::suppress(std::init, rule "x")]] // { dg-error "expected .:." }
void f ();

[[profiles::suppress(std::init, rule: )]] // { dg-error "expected string-literal" }
void g ();

[[profiles::suppress(std::init, rule: "x", justification "y")]] // { dg-error "expected .:." }
void h ();

[[profiles::suppress(std::init, bogus: "x")]] // { dg-error "expected .rule." }
void k ();

// The generic recovery bug this file guards against (a failed
// require_close never skipping to the closing parenthesis) applied to
// profiles::enforce too, not just suppress -- confirm it recovers
// cleanly rather than cascading, even though enforce itself never
// recognizes 'rule:' at all (IS_SUPPRESS gates that grammar off).
[[profiles::enforce(std::init, rule: "x")]]; // { dg-error "expected .\\)." }

