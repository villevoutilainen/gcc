// P3446R0/P4296R0 Invalidation profile: a class returned through a
// hidden return-slot pointer, built by stores into its members -- a
// value initialization or a constructor folded into member stores
// ('<retval>->p = 0B;'), a member constructed by a call
// ('<retval>->__p = make_unique ();'), or elements constructed in a
// loop over the slot.  Only stores of the whole object were seen, so
// with none of those the return was taken for a possibly dangling one
// ('std::weak_ptr<int> x{}; return x;').
// { dg-do compile { target c++17 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "array")]];
[[profiles::exempt(std::invalidation, angle_header: "string")]];
[[profiles::exempt(std::invalidation, angle_header: "memory")]];

#include <array>
#include <string>
#include <memory>

struct Q { int *p; int *q; Q () = default; Q (const Q &o) : p (o.p), q (o.q) {} ~Q () {} };	// { dg-error "initializing member" }
Q value_init () { Q t{}; return t; }
Q value_init_tmp () { return Q{}; }

static int g;
struct P { int *p; P () : p (nullptr) {} P (const P &) = default; ~P () {} };
P member_global () { P t; t.p = &g; return t; }
P member_null_loop (int n) { P t; for (int i = 0; i < n; ++i) t.p = nullptr; return t; }
void put (int **slot, int *v);
P member_by_call_global () { P t; put (&t.p, &g); return t; }

std::weak_ptr<int> weak () { std::weak_ptr<int> x{}; return x; }
std::array<std::string, 2> strings () { std::array<std::string, 2> x{}; return x; }
std::array<std::string, 2> strings_tmp () { return std::array<std::string, 2>{}; }
auto closure () { return [p = std::make_unique<int> ()] { return *p; }; }
