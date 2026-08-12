// D4324: the motivating use case for 'conveyor(auto)' -- a generic,
// widely-shared customization-point-object-shaped utility, templated
// on the type it's called with, dispatching (via 'if constexpr') to
// either a member call or a fallback. Marking such a function plain
// 'conveyor' is unusable in practice: it would make EVERY
// instantiation's body -- including ones for types nobody ever
// intended to use from conveyor-restricted code -- subject to the
// mandatory rules unconditionally, rejecting any instantiation whose
// underlying type isn't itself conveyor-tagged, even from completely
// unrelated, non-conveyor callers (this exact shape broke a real,
// already-passing library test earlier in this feature's own
// development -- see .claude/plans/lazy-stirring-pearl.md). With
// 'conveyor(auto)', each instantiation is judged independently: the
// conveyor-tagged type's instantiation is usable as a conveyor callee,
// the plain type's instantiation is simply an ordinary function,
// usable everywhere else exactly as if the specifier were never
// written at all.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct Tagged
{
  constexpr bool has_member () const conveyor { return true; }
};

struct Plain
{
  constexpr bool has_member () const { return true; }
};

template<typename _Tp>
concept __has_member = requires (const _Tp& t) { t.has_member (); };

template<typename _Tp>
constexpr bool
get_it (const _Tp& t) conveyor(auto)
{
  if constexpr (__has_member<_Tp>)
    return t.has_member ();
  else
    return false;
}

bool user_of_tagged (const Tagged& t) conveyor
{ return get_it (t); }

int
main ()
{
  Tagged tagged;
  Plain plain;
  int failures = 0;
  if (!user_of_tagged (tagged)) ++failures;
  /* Ordinary, non-conveyor use of the untagged instantiation: never
     restricted, works exactly as if 'conveyor(auto)' weren't there.  */
  if (!get_it (plain)) ++failures;
  return failures;
}
