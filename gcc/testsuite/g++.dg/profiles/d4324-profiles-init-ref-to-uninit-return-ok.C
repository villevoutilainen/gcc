// P4222 Initialization profile: [[ref_to_uninit]] on a FUNCTION_DECL
// itself marks its own RETURN value as referring to [[uninit]] memory
// (the "void* [[ref_to_uninit]] malloc(size_t);"-shaped case) -- a
// flavored function's result assigned into a matching, flavored
// destination is a legitimate match, whether via initialization or a
// later assignment.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

typedef __SIZE_TYPE__ size_t;

[[ref_to_uninit]] void* my_malloc (size_t n);

int main ()
{
  void* p [[ref_to_uninit]] = my_malloc (4);
  p = my_malloc (8);
}
