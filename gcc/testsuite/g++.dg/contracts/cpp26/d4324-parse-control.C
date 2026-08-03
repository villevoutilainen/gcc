// D4324: parse the optional contract control specifier on
// pre/post/contract_assert.  pre<obj>(cond) naming a constexpr control
// object parses; the bare forms still default (now to
// std::contracts::default_v); a '<' that starts a predicate is still
// less-than; an object of a class-template-instantiation control type
// parses; and an empty '<>' also defaults to std::contracts::default_v,
// exactly like the bare form (pre<>/post<>/contract_assert<>).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fsyntax-only" }

#include <contracts>

static_assert (__cpp_contracts >= 202502L);

struct review { void operator() (const std::contracts::assertion_context&) const {} };
struct mandatory { void operator() (const std::contracts::assertion_context&) const {} };
template<class T> struct ctl { void operator() (const std::contracts::assertion_context&) const {} };

inline constexpr review review_v{};
inline constexpr mandatory mandatory_v{};
inline constexpr ctl<int> ctl_int_v{};

int f (int x) pre<review_v>(x > 0) { return x; }
int g (int x) pre(x > 0) { return x; }
int p (int x) post<review_v>(r: r > 0) { return x; }
int q (int x) post(r: r > 0) { return x; }
int b (int x) pre<ctl_int_v>(x > 0) { return x; }
int lt (int x, int y) pre(x < y) { return x; }

void h (int x)
{
  contract_assert<mandatory_v>(x > 0);
  contract_assert(x > 0);
  contract_assert(x < 0);
  contract_assert<>(x > 0);   // empty '<>': std::contracts::default_v
}

int e (int x) pre<>(x > 0) { return x; }
int r (int x) post<>(res: res > 0) { return x; }
