// D4324: a function contract specifier with a control object may follow a
// trailing return type; trying 'pre<ctrl>' as the start of a
// pointer-to-member declarator mustn't turn it into a template-id first.
// 'pre<X>::*' is still a pointer to member of a class template 'pre'.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct S { int v; };

inline auto f1 (const S &x) -> int
  pre<std::contracts::conveyor_assert_v> (std::is_object_address (&x))
{ return x.v; }

template<class T>
inline auto f2 (const T &x) -> decltype (x.v)
  pre<std::contracts::conveyor_assert_v> (std::is_object_address (&x))
  post<std::contracts::noexcept_assert_v> (r: r == x.v)
{ return x.v; }

inline auto f3 (const S &x) -> int pre (x.v > 0) { return x.v; }

// A trailing pointer-to-member return type still parses.
inline auto f4 () -> int S::* { return &S::v; }
inline auto f5 () -> int S::* pre<std::contracts::noexcept_assert_v> (true)
{ return &S::v; }

namespace n
{
  template<class> struct pre { int m; };
  inline auto f6 () -> int pre<int>::* { return &pre<int>::m; }
  inline auto f7 () -> int pre<int>::* pre<std::contracts::noexcept_assert_v> (true)
  { return &pre<int>::m; }
  int h () { pre<int> p{2}; return p.*f6 () + p.*f7 (); }
}

int g () { S s{1}; return f1 (s) + f2 (s) + f3 (s) + s.*f4 () + s.*f5 (); }
