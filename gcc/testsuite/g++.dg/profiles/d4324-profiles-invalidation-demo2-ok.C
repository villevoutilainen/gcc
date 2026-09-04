// P3446R0 / P4296R0 Invalidation profile, worked demo #2 (also
// distributed as a standalone Compiler Explorer demo): a broader
// tour of code the profile accepts as-is, covering several features
// together rather than one at a time -- safe-return positions,
// [[owning_ptr]]/[[owner]], ordinary (non-explicit) destruction,
// reinterpret_cast to a non-pointer type, Rule #0/#1 cross-container
// safety, and the free-function [[not_invalidating]] opt-out -- all
// in one enforced translation unit.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

// --- Returning pointers/references: only a LOCAL is unsafe. ---

int global_value = 42;

int *to_global ()         { return &global_value; }
int *to_static ()         { static int s = 0; return &s; }
int *to_received (int *p) { return p; }

// --- owner<T>: 'delete' requires an [[owning_ptr]] -- or its
//     alternate spelling from the talk, [[owner]]. Both work. ---

void destroy_owning_ptr ([[owning_ptr]] int *p) { delete p; }
void destroy_owner      ([[owner]]      int *p) { delete p; }

// --- Ordinary destruction (block-scope exit, or 'delete' of an
//     owning pointer) is not an "explicit destructor call" -- only
//     literally writing 'x.~T()' is. ---

struct Resource { ~Resource () {} };

void ordinary_lifetime ()
{
  Resource r; // destroyed at scope exit -- fine.
}

void destroy_resource ([[owning_ptr]] Resource *p)
{
  delete p; // fine: p is owning.
}

// --- A reinterpret_cast whose TARGET type isn't a pointer (e.g.
//     inspecting a pointer's bit pattern) can't be used to reuse
//     storage as a different type, so it's left alone. ---

typedef __UINTPTR_TYPE__ uintptr_t;

uintptr_t bit_pattern_of (int *p)
{
  return reinterpret_cast<uintptr_t> (p);
}

// --- Containers/iterators: a non-const call is assumed to
//     invalidate other outstanding iterators/pointers UNLESS marked
//     [[not_invalidating]] -- begin()/end()/similar accessors opt out
//     because they don't actually mutate anything. ---

template<typename T> struct ToyListIterator { T *p; };

template<typename T> struct ToyList
{
  [[not_invalidating]] ToyListIterator<T> begin () { return ToyListIterator<T> {}; }
  [[not_invalidating]] ToyListIterator<T> end ()   { return ToyListIterator<T> {}; }
  ToyListIterator<T> erase (ToyListIterator<T> it) { return it; }
};

template<typename T> struct ToyVector
{
  void push_back (ToyListIterator<T> it) { }
};

struct Element { };

bool can_process (ToyListIterator<Element>) { return true; }

// Rule #0 ("unrelated types can't alias") plus Rule #1 ("patently
// independent containers can't alias") together accept moving an
// iterator from one container kind into a genuinely unrelated one,
// then continuing to use it as erase()'s OWN argument on the
// original container -- P4296R0 S7.6.2, also the CppCon 2026
// "Profiles" talk's own invalidation-prototype demo (slide 41:
// "list element into a vector").
ToyVector<Element>
list_into_vector (ToyList<Element> &elems)
{
  ToyVector<Element> result;
  auto iter = elems.begin ();
  if (can_process (iter))
    {
      result.push_back (iter);  // Rule #0: ToyVector and ToyList
                                 // are unrelated templates.
      iter = elems.erase (iter);
    }
  return result;
}

// --- A plain (non-member) function is assumed to invalidate a
//     non-const container-typed argument by default -- exactly the
//     same default a non-const member call already gets.
//     [[not_invalidating]] on the PARAMETER opts a specific argument
//     out of that assumption. ---

void inspect ([[not_invalidating]] ToyList<Element> &elems);

void use_after_inspect (ToyList<Element> &elems)
{
  auto iter = elems.begin ();
  inspect (elems);          // opted out: iter is still trusted below.
  (void) can_process (iter);
}
