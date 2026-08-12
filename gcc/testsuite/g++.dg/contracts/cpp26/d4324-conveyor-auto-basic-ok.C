// D4324: 'conveyor(auto)' on a function template deduces conveyor-ness
// per specialization, from whether that specialization's own body
// happens to satisfy the mandatory conveyor rules -- unlike bare
// 'conveyor', which requires every instantiation, unconditionally, to
// satisfy them. Here, 'call_get<Conveyor>' deduces conveyor (its body
// only calls Conveyor::get(), itself conveyor-tagged), so it may be
// called from real conveyor-restricted code. See .claude/plans/
// lazy-stirring-pearl.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct Conveyor
{
  bool get () const conveyor { return true; }
};

template<typename _Tp>
bool
call_get (_Tp const& t) conveyor(auto)
{ return t.get (); }

bool user_of_conveyor (Conveyor& c) conveyor
{ return call_get (c); }

int
main ()
{
  Conveyor c;
  return user_of_conveyor (c) ? 0 : 1;
}
