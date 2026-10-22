// P4222 Initialization profile: a field of an uninitialized class object
// can't be written until the object as a whole is initialized, so the
// object can't be filled in member by member.
// { dg-do compile { target c++20 } }

[[profiles::enforce(std::init)]];

struct Point { int a; int b; };
struct Str { Str (); ~Str (); };

void sink (Point&);

void field_by_field ()
{
  Point p [[uninit]];
  p.a = 1;		// { dg-error "member .Point::a. of .p. is written before .p. is initialized as a whole" }
  p.b = 2;		// { dg-error "member .Point::b. of .p. is written before .p. is initialized as a whole" }
  sink (p);
}

void one_branch (bool c)
{
  Point p [[uninit]];
  if (c)
    p = {1, 2};
  p.a = 3;		// { dg-error "is written before .p. is initialized as a whole" }
  sink (p);		// { dg-error "before it is provably initialized" }
}

void too_early ()
{
  Point p [[uninit]];
  p.a = 1;		// { dg-error "is written before .p. is initialized as a whole" }
  p = {1, 2};
  sink (p);
}

struct Holder
{
  Point m [[uninit]];
  Holder (char)
  {
    m.a = 1;		// { dg-error "member .Point::a. of .Holder::m. is written before .Holder::m. is initialized as a whole" }
    m.b = 2;		// { dg-error "member .Point::b. of .Holder::m. is written before .Holder::m. is initialized as a whole" }
  }
};

struct Aggr { Str s; Point m [[uninit]]; };
void member_of_default_initialized ()
{
  Aggr g;
  g.m.a = 3;		// { dg-error "member .Point::a. of .Aggr::m. is written before .Aggr::m. is initialized as a whole" }
}
