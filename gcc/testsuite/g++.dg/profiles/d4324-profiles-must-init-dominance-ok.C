// P4222 Initialization profile, Phase 3: the paper's own flagship
// [[must_init]] use case -- a [[uninit]] local whose address is
// passed to a [[must_init]] parameter is considered initialized on
// every path from that call, and a direct write is an initializing
// event too.  initialize/write_x are declared but never defined in
// this TU, matching the common case of a function only declared in a
// header.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int use (int);
void initialize (int* q [[must_init]]);
void take_uninit (int* p [[ref_to_uninit]]);

void straight_line ()
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
