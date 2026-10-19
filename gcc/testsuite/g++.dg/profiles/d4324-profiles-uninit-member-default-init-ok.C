// P4222 Initialization profile: [[uninit]] members of a default-
// initialized object are only tracked where the constructor is known
// to leave them alone (implicitly-defined or defaulted).  A write, a
// whole-object initialization, or anything else that might initialize
// the member -- a user-provided constructor, a member function call,
// passing the object by reference -- means no diagnostic.
// { dg-do compile { target c++20 } }

[[profiles::enforce(std::init)]];

struct Str { Str (); ~Str (); };

struct Aggr2
{
  Str str;
  int num [[uninit]];
  void set ();
};

struct User
{
  User ();
  Str str;
  int num [[uninit]];
};

struct OutOfLine
{
  OutOfLine ();
  Str str;
  int num [[uninit]];
};

OutOfLine::OutOfLine () = default;	// user-provided

void fill (Aggr2 &);

void unused () { Aggr2 a; }
int written () { Aggr2 a; a.num = 3; return a.num; }
int value_initialized () { Aggr2 a{}; return a.num; }
int user_ctor () { User u; return u.num; }
int out_of_line_default () { OutOfLine o; return o.num; }
int member_call () { Aggr2 a; a.set (); return a.num; }
int by_reference () { Aggr2 a; fill (a); return a.num; }
int both_branches (bool c)
{
  Aggr2 a;
  if (c)
    a.num = 1;
  else
    a.num = 2;
  return a.num;
}
int local_static () { static Aggr2 a; return a.num; }	// zero-initialized
