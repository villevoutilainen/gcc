// P4222 Initialization profile: a member whose type's default
// initialization does something can't be left uninitialized by the
// class's constructors, so it can't be marked [[uninit]]; a member of a
// trivially initialized type can.
// { dg-do compile { target c++20 } }

[[profiles::enforce(std::init)]];

struct Str { Str (); };	// a user-provided constructor

struct Bad
{
  Str s [[uninit]];	// { dg-error "member .Bad::s. cannot be marked ...uninit..." }
  int n [[uninit]];
};

template <class T>
struct Holder
{
  T t [[uninit]];		// { dg-error "cannot be marked ...uninit..." }
};

void use ()
{
  Holder<int> hi [[uninit]];	// a trivially initialized member: OK
  hi.t = 1;
  (void) hi;
}

void use_nontrivial ()
{
  Holder<Str> hs;		// { dg-message "required from here" }
  (void) hs;
}
