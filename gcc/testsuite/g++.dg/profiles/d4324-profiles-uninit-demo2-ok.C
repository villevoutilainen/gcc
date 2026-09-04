// P4222 Initialization profile, worked demo #2 (also distributed as
// a standalone Compiler Explorer demo): a broader tour of code the
// profile accepts as-is, covering several features together rather
// than one at a time -- real Definite Assignment Analysis
// (straight-line/branch-merge/loop), [[ref_to_uninit]], [[must_init]]
// call-site dominance, array bulk-init, and class member coverage --
// all in one enforced translation unit.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int use (int);

// --- Ordinary code: every local initialized up front needs no
//     annotation at all. ---

int trivially_initialized (int a, int b)
{
  int sum = a + b;
  return sum;
}

// --- [[uninit]] opts a local out of "must be initialized where
//     declared" -- but real Definite Assignment Analysis still
//     requires it be written before any read, on every path. ---

// Straight-line: written, then read.
void straight_line ()
{
  [[uninit]] int x;
  x = 5;
  use (x);
}

// Both arms of an if/else assign it -- accepted at the merge point.
void both_branches_assign (bool c)
{
  [[uninit]] int x;
  if (c)
    x = 1;
  else
    x = 2;
  use (x);
}

// Assigned at the top of every loop iteration before its own read
// within that same iteration.
void loop_assigns_before_read ()
{
  [[uninit]] int x;
  for (int i = 0; i < 3; ++i)
    {
      x = i;
      use (x);
    }
}

// --- [[ref_to_uninit]] on a pointer initialized directly from an
//     [[uninit]] variable's address: the pointer itself now carries
//     the "points to possibly-uninitialized memory" flavor, rather
//     than needing now_init(). ---

void ref_to_uninit_pointer ()
{
  [[uninit]] int x;
  int *p [[ref_to_uninit]] = &x;
  (void) p;
}

// --- [[must_init]] on a callee's parameter: the paper's own
//     flagship use case. A [[uninit]] local whose address is passed
//     to a [[must_init]] parameter is considered initialized on
//     every path from that call onward -- a direct write is an
//     initializing event too. 'initialize'/'take_uninit' are only
//     declared here, matching the common case of a function whose
//     definition lives elsewhere. ---

void initialize (int *q [[must_init]]);
void take_uninit (int *p [[ref_to_uninit]]);

void must_init_dominance ()
{
  [[uninit]] int x;
  initialize (&x);
  use (x);
}

void direct_write_also_initializes ()
{
  [[uninit]] int x;
  take_uninit (&x);
  x = 5;
  use (x);
}

// --- Arrays: once a [[uninit]] array is proven initialized by a
//     recognized [[must_init]] bulk-initialization call, ordinary
//     (including computed-index) element access is fine. ---

void fill (int *p [[must_init]], int n);

void array_bulk_init (int i)
{
  [[uninit]] int arr[10];
  fill (arr, 10);
  int x = arr[0];
  int y = arr[i];
  arr[i] = y + x;
}

// --- Classes: a non-union class with no user-declared constructor
//     and a trivial default constructor is treated like a scalar --
//     a full initializer at the declaration is enough. A constructor
//     covering every member -- via the member-initializer-list, an
//     NSDMI, or (for a member marked literally [[uninit]]) not
//     covering it at all, since the entire point of [[uninit]] is
//     that no promise is made even by the constructor -- is accepted
//     too, one flavor per struct below. Each is actually instantiated
//     (classes(), not just defined): an inline constructor that's
//     declared but never called never gets GIMPLE-compiled at all, so
//     merely defining these would never exercise the checker that
//     verifies them (ip_check_constructor_member, init-profile-
//     gimple.cc). ---

struct Aggregate
{
  int x;
  int y;
};

void aggregate_use ()
{
  Aggregate ag = {1, 2};
  (void) ag;
}

struct CoveredByCtorList
{
  int m1;
  int m2;
  CoveredByCtorList (int x) : m1{x}, m2{x} {}
};

struct CoveredByNsdmi
{
  int m1 = 7;
  int m2;
  CoveredByNsdmi (int x) : m2{x} {}
};

// m2 is genuinely never assigned anywhere -- and that's fine: no
// promise is made about a literally-[[uninit]] member even by its own
// constructor, only checked (elsewhere, not yet by this pass) at
// whatever point something actually reads it.
struct MemberDeliberatelyUninit
{
  int m1;
  int m2 [[uninit]];
  MemberDeliberatelyUninit (int x) : m1{x} {}
};

// A [[ref_to_uninit]] member is different: its own pointer VALUE
// still needs a real assignment (here, via the member-initializer-
// list) -- only its pointee's content is exempted.
struct MemberPointsToUninit
{
  int m1;
  int *m2 [[ref_to_uninit]];
  MemberPointsToUninit (int x, int *p) : m1{x}, m2{p} {}
};

void classes ()
{
  CoveredByCtorList c1 (1);
  CoveredByNsdmi c2 (2);
  MemberDeliberatelyUninit c3 (3);
  int scratch = 0;
  MemberPointsToUninit c4 (4, &scratch);
  (void) c1;
  (void) c2;
  (void) c3;
  (void) c4;
}
