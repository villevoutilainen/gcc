// { dg-do run { target c++26 } }
// { dg-additional-options "-freflection" }
// Test std::define_static_string.
//
// Rewritten for the new std::basic_string_view-returning signature
// (was: a raw, NUL-terminated const CharT*). Two things changed that
// affect several assertions here, not just the declared type:
//
// 1. operator== on two basic_string_view objects is CONTENT equality,
//    not pointer identity. Several assertions below used to verify
//    that two differently-sourced calls with equal content produced
//    the SAME merged/deduplicated static object (pointer identity);
//    now that's only directly observable via .data(), so this file
//    checks .data() identity explicitly wherever that was the real
//    point, and leaves plain `==` for the (now weaker, content-only)
//    checks where identity isn't actually what's being tested.
// 2. The view's length stops at the first embedded CharT() value,
//    exactly like constructing a basic_string_view from a single,
//    NUL-terminated pointer (which is, in fact, exactly how it's
//    implemented) -- deliberate, matching ordinary string-literal
//    convention rather than preserving an explicitly-sized range's
//    exact length when that range happens to contain an embedded
//    NUL. This changes the expected length for a few of the
//    EXPLICITLY-sized inputs below (noted at each one).

#include <meta>
#include <ranges>
#include <span>

constexpr auto a = std::define_static_string ("abcd");
constexpr auto b = std::define_static_string (u8"abcd\N{LATIN SMALL LETTER AE}");
constexpr auto c = std::define_static_string (L"abcd");
constexpr auto d = std::define_static_string (u"abcd\0ef");
constexpr auto e = std::define_static_string (U"abcd\0ef\N{LATIN CAPITAL LETTER AE}");
constexpr auto f = std::define_static_string (std::string_view ("abcd", 5));
constexpr auto g = std::define_static_string (std::string_view ("abcdefg", 4));
constexpr auto h = std::define_static_string (std::u8string_view (u8"ab\0\N{LATIN SMALL LETTER AE}", 5));
constexpr auto i = std::define_static_string (std::string_view ("abcdefgh", 9).substr (0, 5));
constexpr auto j = std::define_static_string (std::string_view ("abcdefgh", 9).substr (4, 3));
constexpr auto k = std::define_static_string (std::u32string_view (U"abcdefgh", 9).substr (3, 4));
constexpr char8_t la[] = { u8'h', u8'e', u8'l', u8'l', u8'o', u8'\0' };
constexpr auto l = std::define_static_string (la);
constexpr std::span <const char8_t> ma (la);
constexpr auto m = std::define_static_string (ma);
constexpr auto n = std::define_static_string (ma.subspan (1, 4));
constexpr auto o = std::define_static_string (ma.subspan (1, 4) | std::views::reverse);
constexpr auto p = std::define_static_string (std::vector <wchar_t> { L'W', L'o', L'r', L'l', L'd' });
constexpr auto q = std::define_static_string (std::vector <char16_t> { u'e', u'x', u't', u'r', u'e', u'm', u'e', u'l', u'y',
								       u' ', u'l', u'o', u'n', u'g',
								       u' ', u's', u't', u'r', u'i', u'n', u'g',
								       u' ', u'w', u'i', u't', u'h',
								       u' ', u'n', u'o', u'n', u'-', u'A', u'S', u'C', u'I', u'I',
								       u' ', u'c', u'h', u'a', u'r', u'a', u'c', u't', u'e', u'r', u's',
								       u' ', u'\N{LATIN SMALL LETTER A WITH ACUTE}' });
const char *r = std::define_static_string ("some string").data ();
const char8_t *s = std::define_static_string (u8"\N{GRINNING FACE}\N{GRINNING FACE WITH SMILING EYES}").data ();

static_assert (std::is_same_v <decltype (a), const std::string_view>);
static_assert (a == "abcd" && a.size () == 4);
static_assert (b == u8"abcd\N{LATIN SMALL LETTER AE}");
static_assert (c == L"abcd");
// Embedded NUL at index 4: stops there, same as a plain NUL-terminated
// C-string would -- "abcd", not the full 7-element "abcd\0ef".
static_assert (d == u"abcd" && d.size () == 4);
static_assert (e == U"abcd" && e.size () == 4);
// f was explicitly sized to include the embedded NUL as content
// ("abcd\0", 5 elements) -- but still stops at the first NUL, giving
// "abcd" (4), the same as a and g. This is the clearest case of the
// length-semantics change: identity-via-merging can no longer be
// observed through operator== at all here (content is now equal to
// a/g where it wouldn't have been, pointer-identity-wise, before).
static_assert (f == "abcd" && f.size () == 4);
static_assert (g == a);
static_assert (h == u8"ab" && h.size () == 2);
static_assert (i == "abcde");
static_assert (a != i);
static_assert (j == "efg");
static_assert (a != j);
static_assert (k == U"defg");
static_assert (l == u8"hello" && l.size () == 5);
static_assert (m == l);
static_assert (n == u8"ello");
static_assert (o == u8"olle");
static_assert (p == L"World");
static_assert (q == u"extremely long string with non-ASCII characters \N{LATIN SMALL LETTER A WITH ACUTE}");
static_assert (std::define_static_string ("bar") != std::define_static_string ("baz"));

// Merged/deduplicated-static-storage identity, now only observable via
// .data() (operator== itself is content-only on a string_view) --
// direct replacements for this file's old pointer-equality assertions.
static_assert (g.data () == a.data ());
static_assert (m.data () == l.data ());
static_assert (std::define_static_string ("foobar").data ()
	       == std::define_static_string (std::vector <char> { 'f', 'o', 'o', 'b', 'a', 'r' }).data ());

template <typename T, std::basic_string_view<T> P>
struct C { std::basic_string_view<T> p = P; };

static_assert (std::is_same_v <C <char, std::define_static_string ("foobar")>,
			       C <char, std::define_static_string (std::vector <char> { 'f', 'o', 'o', 'b', 'a', 'r' })>>);
static_assert (!std::is_same_v <C <char, std::define_static_string ("foobar")>,
				C <char, std::define_static_string (std::vector <char> { 'f', 'o', 'O', 'b', 'a', 'r' })>>);
static_assert (std::is_same_v <C <wchar_t, std::define_static_string (L"hello")>,
			       C <wchar_t, std::define_static_string (L"hello")>>);
static_assert (std::is_same_v <C <char8_t, std::define_static_string (u8"hello\0")>,
			       C <char8_t, std::define_static_string (ma)>>);
static_assert (std::is_same_v <C <char8_t, std::define_static_string (u8"\N{LATIN SMALL LETTER AE}")>,
			       C <char8_t, std::define_static_string (u8"\N{LATIN SMALL LETTER AE}")>>);
static_assert (std::is_same_v <C <char16_t, std::define_static_string (u"\N{LATIN CAPITAL LETTER AE}")>,
			       C <char16_t, std::define_static_string (std::vector <char16_t> { u'\N{LATIN CAPITAL LETTER AE}' })>>);
static_assert (std::is_same_v <C <char32_t, std::define_static_string (U"\N{GRINNING FACE}\N{GRINNING FACE WITH SMILING EYES}")>,
			       C <char32_t, std::define_static_string (std::vector <char32_t> { U'\N{GRINNING FACE}', U'\N{GRINNING FACE WITH SMILING EYES}' })>>);

template <auto V>
consteval auto
foo ()
{
  return V[0];
}

static_assert (foo <std::define_static_string ("foo")> () == 'f');
static_assert (foo <std::define_static_string (L"bar")> () == L'b');
static_assert (foo <std::define_static_string (u8"qux")> () == u8'q');
static_assert (foo <std::define_static_string (u"\N{LATIN SMALL LETTER AE}\N{LATIN CAPITAL LETTER AE}")> () == u'\N{LATIN SMALL LETTER AE}');
static_assert (foo <std::define_static_string (U"\N{GRINNING FACE WITH SMILING EYES}\N{GRINNING FACE}")> () == U'\N{GRINNING FACE WITH SMILING EYES}');

int
main ()
{
  if (a != "abcd")
    __builtin_abort ();
  if (b != u8"abcd\N{LATIN SMALL LETTER AE}")
    __builtin_abort ();
  if (c != L"abcd")
    __builtin_abort ();
  if (d != u"abcd")
    __builtin_abort ();
  if (e != U"abcd")
    __builtin_abort ();
  if (f != "abcd")
    __builtin_abort ();
  if (g != a)
    __builtin_abort ();
  if (h != u8"ab")
    __builtin_abort ();
  if (i != "abcde")
    __builtin_abort ();
  if (a == i)
    __builtin_abort ();
  if (j != "efg")
    __builtin_abort ();
  if (a == j)
    __builtin_abort ();
  if (k != U"defg")
    __builtin_abort ();
  if (l != u8"hello")
    __builtin_abort ();
  if (m != l)
    __builtin_abort ();
  if (n != u8"ello")
    __builtin_abort ();
  if (o != u8"olle")
    __builtin_abort ();
  if (p != L"World")
    __builtin_abort ();
  if (q != u"extremely long string with non-ASCII characters \N{LATIN SMALL LETTER A WITH ACUTE}")
    __builtin_abort ();
  if (std::string_view (r) != std::string_view ("some string"))
    __builtin_abort ();
  if (std::u8string_view (s) != std::u8string_view (u8"\N{GRINNING FACE}\N{GRINNING FACE WITH SMILING EYES}"))
    __builtin_abort ();
}
