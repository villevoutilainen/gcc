// P4222 Initialization profile: without the std::init profile, neither
// [[uninit]] on an object or member of a non-trivially initialized type
// nor a dynamically initialized global is diagnosed.
// { dg-do compile { target c++20 } }

struct Str { Str (); };	// a user-provided constructor

struct Aggr { Str str; int num; };
struct Bad { Str s [[uninit]]; };

int compute ();
int dynamic = compute ();

void f ()
{
  Aggr x [[uninit]];
  (void) x;
}
