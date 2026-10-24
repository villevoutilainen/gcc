// P3446R0/P4296R0 Invalidation profile: a local's address stored into
// part of a returned class after a whole-object write ('t = {}; t.z =
// &c;') used to go unseen -- only the last whole-object write was
// checked.  Every store into part of the object, and every call given
// its address, is now checked, wherever it is in the function.
// { dg-do compile { target c++17 } }

[[profiles::enforce(std::invalidation)]];

struct P { int a; const char *z; };
struct Big { int a[9]; long g; const char *z; };
struct In { const char *z; };
struct Out { int a; In in; };
static Big gb;
void fill (Big *, const char *);

P p1 () { char c = 'x'; P t{}; t.z = &c; return t; }			// { dg-error "returning a pointer or container" }
P p2 () { char c = 'x'; P t{}; t.a = 1; t.z = &c; return t; }		// { dg-error "returning a pointer or container" }
P p3 (bool b) { char c = 'x'; P t{}; if (b) t.z = &c; return t; }	// { dg-error "returning a pointer or container" }
Big b1 () { char c = 'x'; Big t{}; t.z = &c; return t; }		// { dg-error "returning a pointer or container" }
Big b2 () { char c = 'x'; Big t = gb; t.z = &c; return t; }		// { dg-error "returning a pointer or container" }
Out n1 () { char c = 'x'; Out t{}; t.in.z = &c; return t; }		// { dg-error "returning a pointer or container" }
Big f1 () { char c = 'x'; Big t{}; fill (&t, &c); return t; }		// { dg-error "returning a pointer or container" }
Big f2 () { Big t{}; fill (&t, "lit"); return t; }

// Only a store that may come before the return counts.  With several
// returns, merged into one without a location of its own, each is
// reported where it is.
void use (const P &);
P r1 (bool b) { char c = 'x'; P t{}; if (b) return t; t.z = &c; use (t); return P{}; }
P r2 (bool b) { char c = 'x'; P t{}; if (b) { t.z = &c; return t; } return P{}; } // { dg-error "returning a pointer or container" }
P r3 (bool b) { char c = 'x'; P t{}; if (b) return t;
		t.z = &c; return t; } // { dg-error "returning a pointer or container" }
P r4 (int n) { char c = 'x'; P t{}; for (int i = 0; i < n; ++i) { if (i == 3) return t; t.z = &c; } return P{}; } // { dg-error "returning a pointer or container" }
