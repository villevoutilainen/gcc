/* P3589 Profiles framework, for the C++ front end.

   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "cp-tree.h"
#include "stringpool.h"
#include "diagnostic.h"
#include "attribs.h"
#include "profiles.h"

/* Increment 1: the profile registry is a minimal, fixed table -- one
   entry, "std::init", the informal name P4222's initialization
   profile is already known by (see the CppCon 2026 "Profiles" talk).
   Grows into a real, open-ended, string-keyed table (matching e.g.
   gcc/opts.cc's own sanitizer_opts[] as a structural template) once a
   second profile exists to justify the extra machinery.  */

struct profiles_registry_entry
{
  const char *name;
  unsigned bit;
};

static constexpr profiles_registry_entry profiles_registry[] =
{
  { "std::init", 1u << 0 },
};

static unsigned
profiles_lookup (const char *name)
{
  for (const auto &entry : profiles_registry)
    if (strcmp (entry.name, name) == 0)
      return entry.bit;
  return 0;
}

/* Which profiles are enforced, for the whole translation unit.  Sound
   for Increment 1 specifically because of the placement restriction
   profiles_handle_enforce_attribute enforces below: profiles::enforce
   must appear at global scope before any non-empty-declaration, so
   "enforced at all" and "enforced for the entire TU" coincide.  A
   module-declaration-scoped dominion (P3589's own more general model)
   is later work; nothing here assumes it won't need revisiting then.
   One bitmask per compilation, never reset: GCC only ever compiles one
   TU per process.  */
static unsigned profiles_enforced_mask;

/* True once cp_parser_declaration (parser.cc) has processed anything
   other than an empty-declaration.  See profiles_note_nonempty_
   declaration's own call site for why this is a sound, unified signal
   rather than a per-declaration-kind audit.  */
static bool profiles_seen_nonempty_declaration_p;

void
profiles_note_nonempty_declaration (void)
{
  profiles_seen_nonempty_declaration_p = true;
}

/* ATTR is a profiles::enforce attribute (TREE_VALUE (ATTR) is the
   single-element TREE_LIST cp_parser_profiles_attribute_args built,
   whose own TREE_VALUE is the profile-name IDENTIFIER_NODE -- see that
   function's own comment in parser.cc for the current, deliberately
   minimal grammar). Validates placement and registers the profile as
   enforced for the rest of the translation unit.  */

static void
profiles_handle_enforce_attribute (tree attr, location_t loc)
{
  tree arg_list = TREE_VALUE (attr);
  if (arg_list == NULL_TREE || arg_list == error_mark_node)
    /* Parse error already diagnosed by cp_parser_profiles_attribute_args.  */
    return;
  tree name_id = TREE_VALUE (arg_list);

  if (current_namespace != global_namespace)
    {
      error_at (loc, "%<profiles::enforce%> only allowed at global scope");
      return;
    }
  if (profiles_seen_nonempty_declaration_p)
    {
      error_at (loc, "%<profiles::enforce%> must appear before any "
		"declaration in the translation unit");
      return;
    }

  const char *name = IDENTIFIER_POINTER (name_id);
  unsigned bit = profiles_lookup (name);
  if (!bit)
    {
      error_at (loc, "unknown profile %qs", name);
      return;
    }
  profiles_enforced_mask |= bit;
}

/* True if the named profile is enforced for this translation unit.
   NAME matches profiles_registry's own spelling, e.g. "std::init".  */

bool
profiles_enforced_p (const char *name)
{
  unsigned bit = profiles_lookup (name);
  return bit != 0 && (profiles_enforced_mask & bit) != 0;
}

/* Give STD_ATTRS -- the attribute-specifier-seq of an empty-declaration
   at location ATTRS_LOC -- real semantic meaning, matching what the
   standard actually says ("the attribute-specifier-seq appertains to
   the empty-declaration") instead of the unconditional "attribute
   ignored" warning cp_parser_declaration used to give it regardless of
   what the attribute was.

   Only profiles::enforce is recognized so far. This deliberately does
   not route through decl_attributes (attribs.cc): that function
   requires a real decl/type as its own first argument and is used
   throughout the compiler on that assumption, so making arbitrary
   attribute-table entries (including plugin-registered ones) usable on
   an empty-declaration -- which has no decl to attach to at all -- is
   a separate, materially larger undertaking than this one attribute
   namespace needs, not something to fold in here.  Anything this
   function doesn't recognize keeps exactly the same generic warning
   empty-declarations have always gotten.  */

void
cp_finish_empty_declaration (location_t attrs_loc, tree std_attrs)
{
  bool recognized_any = false;
  for (tree a = std_attrs; a; a = TREE_CHAIN (a))
    {
      tree name = get_attribute_name (a);
      tree ns = get_attribute_namespace (a);
      if (ns == profiles_identifier && is_attribute_p ("enforce", name))
	{
	  profiles_handle_enforce_attribute (a, attrs_loc);
	  recognized_any = true;
	}
    }

  if (!recognized_any
      && std_attrs != NULL_TREE
      && any_nonignored_attribute_p (std_attrs))
    warning_at (attrs_loc, OPT_Wattributes, "attribute ignored");
}
