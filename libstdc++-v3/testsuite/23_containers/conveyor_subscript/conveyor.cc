// D4324: the subscript operators of array, vector, inplace_vector and
// basic_string are conveyor -- their index is checked or a precondition
// its callers prove, so the element is one of the container's objects --
// and so may be called from conveyor code and don't end the container
// (the lifetime rule).
// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects" }
// { dg-do compile { target c++26 } }

#include <array>
#include <vector>
#include <inplace_vector>
#include <string>

#define OBJ(x) pre<std::contracts::conveyor_assert_v> (std::is_object_address (&x))

char s1 (const std::string& s) conveyor OBJ(s) { return s.empty () ? 0 : s[0]; }
char s2 (const std::string& s) conveyor OBJ(s) { return s[s.size ()]; }
int i1 (std::inplace_vector<int, 4>& v) conveyor OBJ(v) { return v.empty () ? 0 : v[0]; }
int a1 (std::array<int, 3>& a) conveyor OBJ(a) { return a[2]; }
int v1 (std::vector<int>& v) conveyor OBJ(v) { return v.empty () ? 0 : v[0]; }

int user ()
{
  std::array<int, 3> a{1, 2, 3};
  std::vector<int> v{1};
  std::string s ("x");
  int n = a[0] + a[1] + v[0] + s[0];
  return n + (int) (a.size () + v.size () + s.size ());
}
