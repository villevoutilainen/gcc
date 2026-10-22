// P4222 Initialization profile: once an [[uninit]] local aggregate has
// been initialized as a whole, its fields can be written individually and
// read.  (Filling it in field by field without that is rejected; see
// d4324-profiles-uninit-aggregate-whole-object-bad.C.)
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct P {
  int x [[uninit]];
  int y [[uninit]];
};

void f ()
{
  [[uninit]] P p;
  p = {};		// initialize the whole object first
  p.x = 1;
  p.y = 2;
  int z = p.x + p.y;
  (void) z;
}
