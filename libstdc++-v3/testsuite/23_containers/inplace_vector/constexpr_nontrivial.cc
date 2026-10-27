// { dg-do compile { target c++26 } }

// With std::start_lifetime (P3726R2), inplace_vector of a non-trivial type
// is usable during constant evaluation, including removing elements.

#include <inplace_vector>
#include <string>

consteval bool
test()
{
  std::inplace_vector<std::string, 4> v;
  v.emplace_back("one");
  v.emplace_back("two");
  v.emplace_back("three");
  v.pop_back();
  v.erase(v.begin());
  v.push_back("four");
  v.resize(1);
  return v.size() == 1 && v[0] == "two";
}
static_assert( test() );
