// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }
// The members of an anonymous union or struct are public members of it,
// even when the anonymous member itself is in a private section of the
// enclosing class.  Access is checked on the enclosing class's member, so
// a class marked "structural" can hold one in its private section.

#include <type_traits>

struct PubAnon { union { int a; float b; }; };
static_assert (std::is_structural_v<PubAnon>);

// Not marked "structural": the private anonymous member disqualifies it.
class PrivAnon { union { int a; float b; }; };
static_assert (!std::is_structural_v<PrivAnon>);
class ProtAnon { protected: union { int a; }; };
static_assert (!std::is_structural_v<ProtAnon>);

class MarkedAnon structural
{
  union { int a; float b; };
public:
  constexpr MarkedAnon (int x) : a (x) {}
  constexpr int get () const { return a; }
};
static_assert (std::is_structural_v<MarkedAnon>);
template <MarkedAnon M> constexpr int use () { return M.get (); }
static_assert (use <MarkedAnon (3)> () == 3);

// GNU anonymous struct.
class MarkedAnonStruct structural
{
  __extension__ struct { int x; };
public:
  constexpr MarkedAnonStruct () : x (1) {}
};
static_assert (std::is_structural_v<MarkedAnonStruct>);

// The anonymous union's members must still be of structural type.
struct NS { NS (); };
class MarkedBad structural // { dg-error "marked .structural. but is not a structural type" }
{
  union { int a; NS n; };
};
