// Vocabulary types and function wrappers.
#include "types.h"
#include <optional>
#include <memory>
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

  // The rest of optional's interface: const and rvalue accessors, every
  // comparison, the monadic operations of every value category,
  // conversions, swap, iteration, and optional<T&>.
  template<class T>
  int optional_api ()
  {
    int n = 0;
    std::optional<T> o (std::in_place, 1), p;
    const std::optional<T> c (std::in_place, 2);
    n += (c.value () == *c) + (*c.operator-> () == *c) + bool (c);
    n += (std::move (o).value () == T (1)) + (*std::move (c) == T (2));
    n += std::move (o).value_or (T (3)) == T (3);
    n += (o != p) + (o > p) + (o <= p) + (o >= p) + (c != o);
    n += (o == std::nullopt) + (std::nullopt == o) + (o != std::nullopt);
    n += (o == T (1)) + (T (1) == o) + (o != T (1)) + (T (1) != o);
    n += (o < T (1)) + (T (1) < o) + (o > T (1)) + (T (1) > o);
    n += (o <= T (1)) + (T (1) <= o) + (o >= T (1)) + (T (1) >= o);
    auto id = [] (const T &x) { return std::optional<T> (x); };
    auto eq = [] (const T &x) { return x == x; };
    n += c.and_then (id).has_value () + std::move (o).and_then (id).has_value ();
    n += std::move (c).and_then (id).has_value ();
    n += c.transform (eq).has_value () + std::move (o).transform (eq).has_value ();
    n += std::move (c).transform (eq).has_value ();
    auto none = [] { return std::optional<T> (); };
    n += c.or_else (none).has_value () + std::move (o).or_else (none).has_value ();
    std::optional<std::optional<T>> nested (std::in_place, std::in_place, 4);
    n += nested.value ().has_value ();
    std::optional<long> l (3);
    std::optional<long> m (l), k (std::move (l));
    m = std::optional<int> (5);
    k = std::move (m);
    swap (o, p);
    n += o.begin () == o.end ();
    n += c.begin () == c.end ();
    T v (6);
    std::optional<T &> r (v);
    n += (r.value () == v) + (r.value_or (v) == v) + (*r == v);
    n += (r == o) + (r == std::nullopt) + (r.begin () != r.end ());
    return n;
  }

  // Copying an allocator trusts the source, as copies do: no
  // CENSUS_OBJECT here.
  int allocator_copies (const std::allocator<int> &a)
  {
    std::allocator<int> b (a);
    std::allocator<long> c (a);
    return b == std::allocator<int> (c);
  }

  int all ()
  {
    return vocabulary<int> () + vocabulary<Val> () + vocabulary<ThrowCopy> ()
	   + vocabulary<AddrOp> () + functions () + optional_api<int> ()
	   + optional_api<Val> () + allocator_copies (std::allocator<int> ());
  }
}

int main () { return census::all () == 0; }
