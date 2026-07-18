// D4324: parse the optional contract control type on pre/post/contract_assert.
// pre<T>(cond), post<T>(r: cond) and contract_assert<T>(cond) parse; the bare
// forms still default; a '<' that starts a predicate is still less-than; a
// nested template control type parses; and a malformed empty control type is
// rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fsyntax-only" }

static_assert (__cpp_contracts >= 202502L);

struct review {};
struct mandatory {};
template<class T> struct ctl {};

int f (int x) pre<review>(x > 0) { return x; }
int g (int x) pre(x > 0) { return x; }
int p (int x) post<review>(r: r > 0) { return x; }
int q (int x) post(r: r > 0) { return x; }
int b (int x) pre<ctl<int>>(x > 0) { return x; }
int lt (int x, int y) pre(x < y) { return x; }

void h (int x)
{
  contract_assert<mandatory>(x > 0);
  contract_assert(x > 0);
  contract_assert(x < 0);
  contract_assert<>(x > 0);   // { dg-error "expected type-specifier" }
}
