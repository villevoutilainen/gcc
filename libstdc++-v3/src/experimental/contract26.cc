// -*- C++ -*- std::contracts::contract_violation and friends

// Copyright The GNU Toolchain Authors.
//
// This file is part of GCC.
//
// GCC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// GCC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

#include <contracts>
#include <exception> // std::terminate
#include <mutex>
#include <unordered_map>

#ifndef __cpp_lib_contracts
# error "This file requires C++26 contracts support to be enabled"
#endif

#if _GLIBCXX_HOSTED && _GLIBCXX_VERBOSE
# include <iostream>
# include <cxxabi.h>
#endif

void __handle_contract_violation(const std::contracts::contract_violation &violation) noexcept
{
#if _GLIBCXX_HOSTED && _GLIBCXX_VERBOSE

  std::cerr << "contract violation in function " << violation.location().function_name()
    << " at " << violation.location().file_name() << ':' << violation.location().line()
    << ": " << violation.comment();

  const char* delimiter = "\n[";

  std::cerr << delimiter << "assertion_kind:";
   switch (violation.kind())
   {
     case std::contracts::assertion_kind::pre:
       std::cerr << " pre";
       break;
     case std::contracts::assertion_kind::post:
       std::cerr << " post";
       break;
     case std::contracts::assertion_kind::assert:
       std::cerr << " assert";
       break;
     default:
       std::cerr << " unknown" << (int) violation.semantic();
   }
   delimiter = ", ";

  std::cerr << delimiter << "semantic:";
  switch (violation.semantic())
  {
    case std::contracts::evaluation_semantic::enforce:
      std::cerr << " enforce";
      break;
    case std::contracts::evaluation_semantic::observe:
      std::cerr << " observe";
      break;
    default:
      std::cerr << " unknown" << (int) violation.semantic();
  }
  delimiter = ", ";

  std::cerr << delimiter << "mode:";
  switch (violation.mode())
  {
    case std::contracts::detection_mode::predicate_false:
      std::cerr << " predicate_false";
      break;
    case std::contracts::detection_mode::evaluation_exception:
      std::cerr << " evaluation_exception";
      break;
    default:
      std::cerr << "unknown";
  }
  delimiter = ", ";

  if (violation.mode() == std::contracts::detection_mode::evaluation_exception)
    {
      /* Based on the impl. in vterminate.cc.  */
      std::type_info *t = __cxxabiv1::__cxa_current_exception_type();
      if (t)
	{
	  int status = -1;
	  char *dem = 0;
	  // Note that "name" is the mangled name.
	  char const *name = t->name();
	  dem = __cxxabiv1::__cxa_demangle(name, 0, 0, &status);
	  std::cerr << ": threw an instance of '";
	  std::cerr << ( status == 0 ? dem : name) << "'";
	}
      else
	std::cerr << ": threw an unknown type";
    }

  std::cerr << delimiter << "terminating:"
	    << (violation.is_terminating () ? " yes" : " no");

  if (delimiter[0] == ',')
    std::cerr << ']';

  std::cerr << std::endl;
#endif
}

namespace std _GLIBCXX_VISIBILITY(default)
{
_GLIBCXX_BEGIN_NAMESPACE_VERSION

namespace contracts
{

void invoke_default_contract_violation_handler(const std::contracts::contract_violation& violation) noexcept
{
  return __handle_contract_violation(violation);
}

void __d4324_log_violation(const char* comment, std::source_location loc) noexcept
{
#if _GLIBCXX_HOSTED && _GLIBCXX_VERBOSE
  std::cerr << "contract violation in function " << loc.function_name()
    << " at " << loc.file_name() << ':' << loc.line()
    << ": " << (comment ? comment : "(no predicate text)") << std::endl;
#endif
}

[[noreturn]] void __d4324_terminate() noexcept
{
  std::terminate();
}

namespace
{
  // -fcontract-symbolic-runtime-checks ("the gem"): one process-wide,
  // mutex-guarded record store, shared across every TU and thread that
  // links against this library -- the whole point being that an
  // establishing call in one TU can be consulted by a checking call in
  // another. KEY identifies the predicate function (for a bool record)
  // or the field (for a range record) via a compiler-synthesized,
  // comdat-folded tag object (see get_symbolic_predicate_key /
  // get_symbolic_field_key in gcc/cp/contracts.cc), so it is stable and
  // identical across every TU that names the same declaration. OBJ is
  // the tracked object's own runtime pointer value.
  struct symbolic_record_key
  {
    const void* key;
    const void* obj;

    friend bool
    operator==(const symbolic_record_key&, const symbolic_record_key&) noexcept
      = default;
  };

  struct symbolic_record_key_hash
  {
    std::size_t
    operator()(const symbolic_record_key& k) const noexcept
    {
      std::size_t h1 = std::hash<const void*>{}(k.key);
      std::size_t h2 = std::hash<const void*>{}(k.obj);
      return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
  };

  struct symbolic_record_value
  {
    bool is_range;
    bool bool_val;
    bool has_lo;
    bool has_hi;
    long long lo;
    long long hi;
  };

  std::mutex symbolic_mutex;
  std::unordered_map<symbolic_record_key, symbolic_record_value,
		      symbolic_record_key_hash> symbolic_records;
}

void
__contracts_symbolic_establish_bool(const void* key, const void* obj,
				     bool polarity) noexcept
{
  symbolic_record_value v{};
  v.is_range = false;
  v.bool_val = polarity;
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  symbolic_records[symbolic_record_key{key, obj}] = v;
}

bool
__contracts_symbolic_check_bool(const void* key, const void* obj,
				 bool polarity) noexcept
{
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  auto it = symbolic_records.find(symbolic_record_key{key, obj});
  if (it == symbolic_records.end() || it->second.is_range)
    return false;
  return it->second.bool_val == polarity;
}

void
__contracts_symbolic_establish_range(const void* key, const void* obj,
				      bool has_lo, long long lo,
				      bool has_hi, long long hi) noexcept
{
  symbolic_record_value v{};
  v.is_range = true;
  v.has_lo = has_lo;
  v.lo = lo;
  v.has_hi = has_hi;
  v.hi = hi;
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  symbolic_records[symbolic_record_key{key, obj}] = v;
}

bool
__contracts_symbolic_check_range(const void* key, const void* obj,
				  bool has_lo, long long lo,
				  bool has_hi, long long hi) noexcept
{
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  auto it = symbolic_records.find(symbolic_record_key{key, obj});
  if (it == symbolic_records.end() || !it->second.is_range)
    return false;
  const symbolic_record_value& est = it->second;
  // The established range must entail (be a subset of) the required
  // one: a required bound the established fact doesn't itself have, or
  // has but looser than required, fails the check.
  if (has_lo && (!est.has_lo || est.lo < lo))
    return false;
  if (has_hi && (!est.has_hi || est.hi > hi))
    return false;
  return true;
}

}
}

__attribute__ ((weak)) void
handle_contract_violation (const std::contracts::contract_violation &violation)
{
  return __handle_contract_violation(violation);
}

#if _GLIBCXX_INLINE_VERSION
// The compiler expects the contract_violation class to be in an unversioned
// namespace, so provide a forwarding function with the expected symbol name.
extern "C" void
_Z25handle_contract_violationRKNSt9contracts18contract_violationE
(const std::contracts::contract_violation &violation)
{ handle_contract_violation(violation); }

extern "C" void
_Z27__handle_contract_violationRKNSt9contracts18contract_violationE
(const std::contracts::contract_violation &violation)
{ __handle_contract_violation(violation); }

extern "C" void
_Z41invoke_default_contract_violation_handlerRKNSt9contracts18contract_violationE
(const std::contracts::contract_violation &violation)
{ invoke_default_contract_violation_handler(violation); }

#endif
