// P4222 Initialization profile: a namespace-scope variable or static
// data member, thread_local included, is constant-initialized (constinit,
// a constant expression, a constexpr constructor) or zero-initialized --
// never dynamically, in an order unspecified across translation units.
// A function-local static, initialized on first use, may be.
// { dg-do compile { target c++20 } }

[[profiles::enforce(std::init)]];

int compute ();
struct Lit { constexpr Lit () : v (0) {} int v; };
struct Dyn { Dyn (); int v; };

int dynamic = compute ();		// { dg-error "non-constant initialization" }
// { dg-message "constinit" "" { target *-*-* } .-1 }
thread_local int tls = compute ();	// { dg-error "non-constant initialization" }
struct S { static int member; };
int S::member = compute ();		// { dg-error "non-constant initialization" }

constinit int c1 = 3;
constexpr int c2 = 4;
int zeroed;
const int c3 = 5;
Lit lit;				// a constexpr constructor
Dyn dyn;				// { dg-error "non-constant initialization" }
struct S2 { static inline int member = 6; };

int lazy ()
{
  static int x = compute ();		// on first use: OK
  return x;
}

[[profiles::suppress(std::init)]] int suppressed = compute ();
