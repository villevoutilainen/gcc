// { dg-do compile { target c++26 } }

// basic_string_view is marked with the C++26 "structural" class-head
// keyword, so it can be used directly as a non-type template
// parameter despite its own private data members -- and this must be
// a pure addition: every other observable property stays unchanged.

#include <string_view>
#include <type_traits>

static_assert( std::is_trivially_copyable_v<std::string_view> );
static_assert( std::is_standard_layout_v<std::string_view> );
static_assert( std::is_trivially_copyable_v<std::wstring_view> );
static_assert( std::is_standard_layout_v<std::wstring_view> );

// The actual new capability: direct NTTP usage, no reflection involved.
// A bare string literal's address isn't reliably usable as (part of) a
// template argument -- that's precisely the gap std::define_static_
// string exists to close, orthogonal to this feature -- so these use
// a named, static-storage array instead, same as span's own structural
// test does with its own array.
template<std::string_view V>
  struct holder { static constexpr std::string_view value = V; };

constexpr char world[] = "world";
constexpr std::string_view sv (world);
static_assert( holder<sv>::value == "world" );

template<std::wstring_view V>
  struct wholder { static constexpr std::wstring_view value = V; };
constexpr wchar_t wide[] = L"wide";
constexpr std::wstring_view wsv (wide);
static_assert( wholder<wsv>::value == L"wide" );

constexpr char abc[] = "abc";
constexpr char abd[] = "abd";
static_assert( !std::is_same_v<holder<std::string_view (abc)>,
				holder<std::string_view (abd)>> );
static_assert( std::is_same_v<holder<std::string_view (abc)>,
			       holder<std::string_view (abc)>> );
