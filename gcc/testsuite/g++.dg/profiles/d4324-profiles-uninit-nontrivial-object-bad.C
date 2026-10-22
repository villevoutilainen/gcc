// P4222 Initialization profile: an object whose type's default
// initialization does something -- a constructor, a default member
// initializer, a member or base that does -- can't be left
// uninitialized as a whole, so it can't be marked [[uninit]]: a class
// that leaves some members uninitialized marks those members.  Arrays of
// such a type likewise.
// { dg-do compile { target c++20 } }

[[profiles::enforce(std::init)]];

struct Str { Str (); };	// a user-provided constructor

struct Aggr { Str str; int num [[uninit]]; };
struct Nsdmi { int a = 1; int b [[uninit]]; };
struct Triv { int a; int b; };

void whole ()
{
  Aggr x [[uninit]];	   // { dg-error "cannot be marked ...uninit...: its type" }
  Aggr arr [[uninit]] [2]; // { dg-error "cannot be marked ...uninit...: its type" }
  Nsdmi n [[uninit]];	   // { dg-error "cannot be marked ...uninit...: its type" }
}

void members_only ()
{
  Aggr x;		   // some members left uninitialized: OK
  Triv t [[uninit]];	   // nothing to initialize: OK
  t = {1, 2};
  (void) t;
}

template <class T>
void dependent ()
{
  T t [[uninit]];	   // { dg-error "cannot be marked ...uninit...: its type" }
  (void) t;
}

template void dependent<Aggr> ();
