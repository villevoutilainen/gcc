// Parse a P3595 contract configuration (JSON) into cfg::rules_t at compile
// time.  Supported: an array of {"match": {...}, "output": {"semantic": S}}
// with match keys "kind", "namespace", "function", "location" ("file" or
// "file:N-M,K"), "caller" (boolean), "group".  Strings have no escapes.
#ifndef D4324_EMBED_CONFIG_JSON_H
#define D4324_EMBED_CONFIG_JSON_H

#include "d4324-embed-config-common.h"

namespace cfg {

struct json_parser
{
  const unsigned char *p, *e;

  constexpr void ws ()
  { while (p != e && (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')) ++p; }
  constexpr bool peek (char c) { ws (); return p != e && *p == c; }
  constexpr void expect (char c)
  {
    if (!peek (c))
      throw "contract configuration: unexpected character";
    ++p;
  }
  constexpr std::string_view str (char (&buf)[128])
  {
    expect ('"');
    unsigned n = 0;
    while (p != e && *p != '"')
      {
	if (*p == '\\' || n == sizeof buf)
	  throw "contract configuration: unsupported string";
	buf[n++] = char (*p++);
      }
    expect ('"');
    return {buf, n};
  }
  constexpr bool boolean ()
  {
    ws ();
    auto word = [&] (std::string_view w)
      {
	for (char c : w)
	  if (p == e || *p++ != c)
	    throw "contract configuration: bad literal";
      };
    if (p != e && *p == 't') { word ("true"); return true; }
    word ("false");
    return false;
  }

  static constexpr unsigned char
  semantic_of (std::string_view s)
  {
    if (s == "ignore") return 1;
    if (s == "observe") return 2;
    if (s == "enforce") return 3;
    if (s == "quick_enforce") return 4;
    throw "contract configuration: unknown semantic";
  }

  static constexpr void
  location (rule& r, std::string_view loc)
  {
    auto colon = loc.rfind (':');
    if (colon == loc.npos)
      {
	r.file.assign (loc);
	return;
      }
    r.file.assign (loc.substr (0, colon));
    std::string_view lines = loc.substr (colon + 1);
    while (!lines.empty ())
      {
	auto num = [&] ()
	  {
	    std::uint32_t v = 0;
	    if (lines.empty () || lines[0] < '0' || lines[0] > '9')
	      throw "contract configuration: bad line range";
	    while (!lines.empty () && lines[0] >= '0' && lines[0] <= '9')
	      v = v * 10 + (lines[0] - '0'), lines.remove_prefix (1);
	    return v;
	  };
	std::uint32_t lo = num (), hi = lo;
	if (!lines.empty () && lines[0] == '-')
	  lines.remove_prefix (1), hi = num ();
	if (r.nlines == 8)
	  throw "contract configuration: too many line ranges";
	r.lines[2 * r.nlines] = lo;
	r.lines[2 * r.nlines + 1] = hi;
	++r.nlines;
	if (!lines.empty () && lines[0] == ',')
	  lines.remove_prefix (1);
      }
  }

  constexpr void match (rule& r)
  {
    expect ('{');
    while (!peek ('}'))
      {
	char kb[128], vb[128];
	std::string_view key = str (kb);
	expect (':');
	if (key == "caller")
	  r.caller = boolean ();
	else
	  {
	    std::string_view v = str (vb);
	    if (key == "kind")
	      r.kind = (v == "pre" ? 1 : v == "post" ? 2
			: v == "contract_assert" ? 3
			: throw "contract configuration: bad kind");
	    else if (key == "namespace")
	      r.ns.assign (v.starts_with ("::") ? v.substr (2) : v);
	    else if (key == "function")
	      r.function.assign (v.starts_with ("::") ? v.substr (2) : v);
	    else if (key == "location")
	      location (r, v);
	    else if (key == "group")
	      r.group.assign (v);
	    else
	      throw "contract configuration: unknown match key";
	  }
	if (!peek ('}'))
	  expect (',');
      }
    expect ('}');
  }

  constexpr rules_t parse ()
  {
    rules_t rs;
    expect ('[');
    while (!peek (']'))
      {
	if (rs.n == 64)
	  throw "contract configuration: too many entries";
	rule& r = rs.r[rs.n++];
	expect ('{');
	while (!peek ('}'))
	  {
	    char kb[128];
	    std::string_view key = str (kb);
	    expect (':');
	    if (key == "match")
	      match (r);
	    else if (key == "output")
	      {
		expect ('{');
		char sb[128], vb[128];
		if (str (sb) != "semantic")
		  throw "contract configuration: output needs a semantic";
		expect (':');
		r.semantic = semantic_of (str (vb));
		expect ('}');
	      }
	    else
	      throw "contract configuration: unknown entry key";
	    if (!peek ('}'))
	      expect (',');
	  }
	expect ('}');
	if (!r.semantic)
	  throw "contract configuration: entry without output";
	if (!peek (']'))
	  expect (',');
      }
    expect (']');
    rs.index ();
    return rs;
  }
};

template <std::size_t N>
consteval rules_t
parse_json (const unsigned char (&data)[N])
{
  return json_parser{data, data + N}.parse ();
}

} // namespace cfg

#endif
