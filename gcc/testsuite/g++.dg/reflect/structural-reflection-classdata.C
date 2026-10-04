// { dg-do run { target c++26 } }
// { dg-additional-options "-freflection" }
// A compile-time class/member introspection system built entirely on
// "structural"-marked classes holding std::string_view/std::span data,
// nested four levels deep: ClassData -> span<MemberFunctionData> ->
// MemberFunctionData -> (two string_views and)
// span<FunctionParameterData> -> FunctionParameterData -> string_view.
// Independent, real-world-derived coverage beyond this directory's own
// narrower structural*.C tests -- trimmed down from a user-contributed
// example (reflecting a namespace's own classes and their member
// functions at compile time, then using the result at runtime).

#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Mine
{
  class Foo1
  {
  private:
    int mNumber = 0;
    std::string mText;
  public:
    const std::string getText () { return mText; }
    void setText (const std::string &text) { mText = text; }
    int getNumber () { return mNumber; }
    void setNumber (int number) { mNumber = number; }
  };

  class Foo2
  {
  private:
    int mNumber = 0;
    std::string mText;
  public:
    const std::string getText () { return mText; }
    void setText (const std::string &text) { mText = text; }
    int getNumber () { return mNumber; }
    void setNumber (int number) { mNumber = number; }
  };
}

class FunctionParameterData structural
{
private:
  std::string_view param_type;
public:
  constexpr FunctionParameterData (std::string_view pt) : param_type (pt) {}
  constexpr std::string_view get_param_type () const { return param_type; }
};

class MemberFunctionData structural
{
private:
  std::string_view name;
  std::string_view return_type;
  std::span<const FunctionParameterData> params;
public:
  constexpr MemberFunctionData (std::string_view n, std::string_view ret,
				 std::span<const FunctionParameterData> parms)
    : name (n), return_type (ret), params (parms) {}
  constexpr std::string_view get_name () const { return name; }
  constexpr std::string_view get_return_type () const { return return_type; }
  constexpr std::span<const FunctionParameterData> get_params () const { return params; }
};

class ClassData structural
{
private:
  std::string_view name;
  std::span<const MemberFunctionData> members;
public:
  constexpr ClassData (std::string_view n,
			std::span<const MemberFunctionData> mems)
    : name (n), members (mems) {}
  constexpr std::string_view get_name () const { return name; }
  constexpr std::span<const MemberFunctionData> get_members () const { return members; }
};

consteval std::span<const FunctionParameterData>
get_params (const std::meta::info &fun)
{
  std::vector<FunctionParameterData> ret;
  for (auto &&x : std::meta::parameters_of (fun))
    ret.push_back (FunctionParameterData (
      std::define_static_string (std::meta::display_string_of (type_of (x)))));
  return std::define_static_array (ret);
}

consteval std::span<const MemberFunctionData>
get_members (const std::meta::info &cls)
{
  std::vector<MemberFunctionData> ret;
  for (auto &&x : std::meta::members_of (cls, std::meta::access_context::unprivileged ()))
    if (std::meta::is_function (x) && std::meta::has_identifier (x))
      ret.push_back (MemberFunctionData (
	std::define_static_string (std::meta::identifier_of (x)),
	std::define_static_string (
	  std::meta::display_string_of (std::meta::return_type_of (x))),
	get_params (x)));
  return std::define_static_array (ret);
}

consteval std::span<const ClassData>
get_classes ()
{
  std::vector<ClassData> ret;
  for (auto &&x : std::meta::members_of (^^Mine, std::meta::access_context::unprivileged ()))
    if (std::meta::is_type (x) && std::meta::is_class_type (x))
      ret.push_back (ClassData (std::define_static_string (std::meta::identifier_of (x)),
				 get_members (x)));
  return std::define_static_array (ret);
}

constexpr std::span<const ClassData> data = get_classes ();

static_assert (data.size () == 2);
static_assert (data[0].get_name () == "Foo1");
static_assert (data[1].get_name () == "Foo2");
static_assert (data[0].get_members ().size () == 4);
static_assert (data[1].get_members ().size () == 4);

int
main ()
{
  for (auto &&cls : data)
    {
      if (cls.get_name () != "Foo1" && cls.get_name () != "Foo2")
	__builtin_abort ();
      if (cls.get_members ().size () != 4)
	__builtin_abort ();

      bool saw_getText = false, saw_setText = false;
      bool saw_getNumber = false, saw_setNumber = false;
      for (auto &&mem : cls.get_members ())
	{
	  if (mem.get_name () == "getText")
	    {
	      saw_getText = true;
	      if (mem.get_params ().size () != 0)
		__builtin_abort ();
	    }
	  else if (mem.get_name () == "setText")
	    {
	      saw_setText = true;
	      if (mem.get_return_type () != "void")
		__builtin_abort ();
	      if (mem.get_params ().size () != 1)
		__builtin_abort ();
	    }
	  else if (mem.get_name () == "getNumber")
	    {
	      saw_getNumber = true;
	      if (mem.get_return_type () != "int")
		__builtin_abort ();
	      if (mem.get_params ().size () != 0)
		__builtin_abort ();
	    }
	  else if (mem.get_name () == "setNumber")
	    {
	      saw_setNumber = true;
	      if (mem.get_return_type () != "void")
		__builtin_abort ();
	      if (mem.get_params ().size () != 1)
		__builtin_abort ();
	      if (mem.get_params ()[0].get_param_type () != "int")
		__builtin_abort ();
	    }
	  else
	    __builtin_abort ();
	}
      if (!saw_getText || !saw_setText || !saw_getNumber || !saw_setNumber)
	__builtin_abort ();
    }
}
