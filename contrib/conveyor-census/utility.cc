// Vocabulary types and function wrappers.
#include "types.h"
#include <optional>
#include <variant>
#include <tuple>
#include <utility>
#include <any>
#include <expected>
#include <functional>
#include <bitset>
#include <compare>

namespace census
{
  template<class T>
  int vocabulary ()
  {
    int n = 0;
    std::optional<T> o (std::in_place, 1), p;
    n += o.has_value () + (*o == *o) + (o.value () == *o) + (o == p) + (o < p);
    n += o.value_or (T (2)) == T (2);
    p = o;
    p.emplace (3);
    p.reset ();
    o.swap (p);
    n += o.transform ([] (const T &x) { return x == x; }).has_value ();
    n += o.and_then ([] (const T &) { return std::optional<int> (1); }).has_value ();

    std::variant<std::monostate, T> va (std::in_place_index<1>, 1), vb;
    n += va.index () + std::holds_alternative<T> (va) + (va == vb) + (va < vb);
    n += std::visit ([] (const auto &x) { return sizeof (x) > 0; }, va);
    n += std::get_if<T> (&va) != nullptr;
    vb.template emplace<T> (2);
    va.swap (vb);

    std::tuple<int, T, std::string> t (1, T (2), "x"), u;
    n += std::get<0> (t) + (std::get<1> (t) == T (2)) + (t == u) + (t < u);
    auto [a, b, c] = t;
    n += a + (b == b) + c.size ();
    n += std::apply ([] (int x, const T &, const std::string &) { return x; }, t);
    std::pair<T, int> pr (T (1), 2), ps;
    n += (pr == ps) + (pr < ps) + pr.second;
    std::swap (pr, ps);

    std::expected<T, int> e (std::in_place, 1), f (std::unexpect, 2);
    n += e.has_value () + (e.value () == T (1)) + f.error () + (e == f);
    n += e.transform ([] (const T &x) { return x == x; }).has_value ();
    return n;
  }

  int functions ()
  {
    int n = 0;
    auto add = [] (int x, int y) { return x + y; };
    std::function<int (int, int)> f = add;
    n += f (1, 2) + std::invoke (add, 1, 2) + std::invoke_r<long> (add, 1, 2);
    std::move_only_function<int (int)> m = [] (int x) { return x; };
    n += m (1);
    std::copyable_function<int (int)> cf = [] (int x) { return x; };
    n += cf (1);
    std::function_ref<int (int, int)> r = add;
    n += r (1, 2);
    auto b = std::bind_front (add, 1);
    n += b (2) + std::bind_back (add, 1) (2);
    auto bd = std::bind (add, std::placeholders::_1, 2);
    n += bd (1);
    Val v (1);
    std::reference_wrapper<Val> rw (v);
    n += rw.get ().v + std::ref (v).get ().v + std::cref (v).get ().v;
    n += std::not_fn ([] (int) { return false; }) (1);
    n += std::mem_fn (&Val::v) (v);
    std::hash<int> h;
    n += h (1) > 0;
    std::any a = Val (1);
    n += a.has_value () + (std::any_cast<Val> (a).v == 1);
    std::bitset<16> bs (5);
    n += bs.count () + bs.test (0) + (bs << 1).to_ulong ();
    return n;
  }

  int all ()
  {
    return vocabulary<int> () + vocabulary<Val> () + vocabulary<ThrowCopy> ()
	   + vocabulary<AddrOp> () + functions ();
  }
}

int main () { return census::all () == 0; }
