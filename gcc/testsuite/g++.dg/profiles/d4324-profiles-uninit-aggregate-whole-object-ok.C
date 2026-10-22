// P4222 Initialization profile: a field of an uninitialized class object
// can be written once the object as a whole is initialized: by a whole-
// object assignment from another object or from a braced initializer
// (complete, partial or designated), by std::construct_at, or by a
// [[must_init]] call.
// { dg-do compile { target c++20 } }

[[profiles::enforce(std::init)]];
[[profiles::exempt(std::init, angle_header: "memory")]];

#include <memory>

struct Point { int a; int b; };
struct Str { Str (); ~Str (); };

Point make ();
void sink (Point&);
void fill (Point* p [[must_init]]);

void braced_partial () { Point p [[uninit]]; p = {1}; p.b = 7; sink (p); }
void braced_full () { Point p [[uninit]]; p = {1, 2}; p.a = 3; sink (p); }
void braced_runtime (int x) { Point p [[uninit]]; p = {x, x}; p.b = x; sink (p); }
void designated () { Point p [[uninit]]; p = {.b = 666}; p.a = 1; sink (p); }
void from_object () { Point p [[uninit]]; Point p2{}; p = p2; p.a = 1; sink (p); }
void from_call () { Point p [[uninit]]; p = make (); p.a = 1; sink (p); }
void constructed () { Point p [[uninit]]; std::construct_at (&p, 1, 2); p.a = 5; sink (p); }
void must_init () { Point p [[uninit]]; fill (&p); p.a = 1; sink (p); }
void both_branches (bool c)
{
  Point p [[uninit]];
  if (c)
    p = {1, 2};
  else
    p = make ();
  p.a = 3;
  sink (p);
}

// An [[uninit]] member in a constructor.
struct Holder
{
  Point m [[uninit]];
  Holder (int x) { m = {x, x}; m.a = 1; }
  Holder (bool) : m{1, 2} { m.b = 3; }
};

// An [[uninit]] member of a default-initialized object.
struct Aggr { Str s; Point m [[uninit]]; };
void member_of_default_initialized () { Aggr g; g.m = {1, 2}; g.m.a = 3; }
