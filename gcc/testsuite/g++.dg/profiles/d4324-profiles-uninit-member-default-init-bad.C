// P4222 Initialization profile: an object whose type has non-trivial
// default initialization can't be marked [[uninit]], but its class can
// have [[uninit]] members.  When the object is default-initialized by an
// implicitly-defined or defaulted default constructor, those members
// are known to be left uninitialized, so reading one before writing it
// is diagnosed.
// { dg-do compile { target c++20 } }

[[profiles::enforce(std::init)]];

struct Str { Str (); ~Str (); };

struct Aggr2
{
  Str str;
  int num [[uninit]];	// { dg-message "declared ...uninit... here" }
};

struct Dflt
{
  Dflt () = default;
  Str str;
  int num [[uninit]];
};

void take (int *);

int
read_implicit ()
{
  Aggr2 ag2;
  return ag2.num;	// { dg-error "member .Aggr2::num. of .ag2. read before it is definitely assigned" }
}

int
read_defaulted ()
{
  Dflt d;
  return d.num;		// { dg-error "member .Dflt::num. of .d. read before it is definitely assigned" }
}

int
read_one_branch (bool c)
{
  Aggr2 a;
  if (c)
    a.num = 1;
  return a.num;		// { dg-error "member .Aggr2::num. of .a. read before it is definitely assigned" }
}

void
escape_member ()
{
  Aggr2 a;
  take (&a.num);	// { dg-error "address of ...uninit... member .Aggr2::num. of .a. is taken here" }
}

template <class T>
int
read_template ()
{
  T t;
  return t.num;		// { dg-error "member .Aggr2::num. of .t. read before it is definitely assigned" }
}

template int read_template<Aggr2> ();
