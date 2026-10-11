// P3446R0 Invalidation profile: contrast cases for the sibling
// d4324-profiles-invalidation-ctor-wrapping-member-call-bad.C -- all
// three must stay clean.
//
// 'BareRef': 'w(r)' (no explicit '&') passed to a REFERENCE-typed
// 'SomeWrapperRef' parameter -- the acknowledged "Wrapper(ref)" shape,
// deliberately out of scope. This checker cannot know, and must not
// guess, whether SomeWrapperRef's own constructor copies the value
// (safe) or retains the reference (where the existing self-trust
// story, not this check, already applies).
//
// 'PlainRefPtr': 'w(&r)' where 'r' is a plain, non-const 'int&' --
// cannot have been bound to a temporary at all (a language rule), so
// taking its address and handing it to SomeWrapperPtr's constructor
// carries none of the risk a 'const T&'/'T&&' parameter's binding
// does, exactly mirroring the already-established plain-reference
// cases throughout this file.
//
// 'WrappedPtr': the explicit, opt-in 'std::no_dangling' escape hatch
// works here exactly as it does for an ordinary pointer value -- the
// programmer has independently verified this specific address is
// safe to hand off.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

struct SomeWrapperRef
{
  SomeWrapperRef (const int &r);
};

struct SomeWrapperPtr
{
  SomeWrapperPtr (const int *p);
};

struct BareRef
{
  SomeWrapperRef w;
  BareRef (const int &r) : w (r) {}
};

struct PlainRefPtr
{
  SomeWrapperPtr w;
  PlainRefPtr (int &r) : w (&r) {}
};

struct WrappedPtr
{
  SomeWrapperPtr w;
  WrappedPtr (const int &r) : w (std::no_dangling (&r)) {}
};
