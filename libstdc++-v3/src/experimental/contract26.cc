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
#include <cstdint> // std::uintptr_t
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>

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
  // A control object's message, alongside the predicate.
  if (const char* message = violation.message())
    std::cerr << ": " << message;

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
    // When it was established (symbolic_epoch), so that a call's
    // invalidation spares what the call itself established
    // (__contracts_symbolic_invalidate_range_before).
    unsigned long long epoch;
  };

  std::mutex symbolic_mutex;
  unsigned long long symbolic_epoch;

  // Whether this thread is inside a record operation.  Recording a fact
  // can allocate while symbolic_mutex is held, and an allocation in a
  // translation unit built with -fcontract-symbolic-runtime-checks clears
  // the records of the storage it gives -- a replacement operator new, or
  // one with a local whose type declares a symbolic predicate, would
  // otherwise deadlock on the mutex (P4445 2.3.2.3).  Such a nested call
  // concerns storage the outer operation just allocated, which has no
  // records: it does nothing.
  thread_local bool symbolic_busy;

  struct symbolic_entry
  {
    bool ok;
    symbolic_entry() noexcept : ok(!symbolic_busy) { symbolic_busy = true; }
    ~symbolic_entry() { if (ok) symbolic_busy = false; }
  };
  using symbolic_record_map
    = std::unordered_map<symbolic_record_key, symbolic_record_value,
			 symbolic_record_key_hash>;

  // The records, constructed on first use and never destroyed: a fact can
  // be established or cleared by a constructor or destructor of a global
  // object, before this library's own static initialization or after its
  // static destruction (P4445 2.3.2.3).  symbolic_mutex needs neither: it
  // is constant-initialized.
  symbolic_record_map&
  symbolic_records()
  {
    static symbolic_record_map* records = new symbolic_record_map;
    return *records;
  }

  // A relation's record (symbolic facts about several objects and
  // values): keyed by the predicate's tag and, for each argument, an
  // object's address or a value's bytes.  OBJS, the objects' addresses,
  // end it with any of their lifetimes; SCOPE, for a relation with a
  // value argument, the block it was established in, ends it on every
  // exit from that block (__contracts_symbolic_end_scope).
  struct symbolic_relation_value
  {
    bool polarity;
    const void* scope;
    std::vector<const void*> objs;
    unsigned long long epoch;
  };

  using symbolic_relation_map
    = std::unordered_map<std::string, symbolic_relation_value>;

  symbolic_relation_map&
  symbolic_relations()
  {
    static symbolic_relation_map* relations = new symbolic_relation_map;
    return *relations;
  }

  std::string
  relation_key(const void* key, unsigned n, const void* const* args,
	       const std::size_t* sizes, std::vector<const void*>* objs)
  {
    std::string k(reinterpret_cast<const char*>(&key), sizeof key);
    for (unsigned i = 0; i < n && i < 4; ++i)
      {
	k.push_back(sizes[i] ? 'v' : 'o');
	if (sizes[i])
	  {
	    std::size_t sz = sizes[i];
	    k.append(reinterpret_cast<const char*>(&sz), sizeof sz);
	    k.append(static_cast<const char*>(args[i]), sizes[i]);
	  }
	else
	  {
	    k.append(reinterpret_cast<const char*>(&args[i]), sizeof args[i]);
	    if (objs)
	      objs->push_back(args[i]);
	  }
      }
    return k;
  }
}

void
__contracts_symbolic_establish_bool(const void* key, const void* obj,
				     bool polarity) noexcept
{
  symbolic_entry entry;
  if (!entry.ok)
    return;
  symbolic_record_value v{};
  v.is_range = false;
  v.bool_val = polarity;
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  v.epoch = ++symbolic_epoch;
  symbolic_records()[symbolic_record_key{key, obj}] = v;
}

bool
__contracts_symbolic_check_bool(const void* key, const void* obj,
				 bool polarity) noexcept
{
  symbolic_entry entry;
  if (!entry.ok)
    return false;
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  auto it = symbolic_records().find(symbolic_record_key{key, obj});
  if (it == symbolic_records().end() || it->second.is_range)
    return false;
  return it->second.bool_val == polarity;
}

void
__contracts_symbolic_establish_range(const void* key, const void* obj,
				      bool has_lo, long long lo,
				      bool has_hi, long long hi) noexcept
{
  symbolic_entry entry;
  if (!entry.ok)
    return;
  symbolic_record_value v{};
  v.is_range = true;
  v.has_lo = has_lo;
  v.lo = lo;
  v.has_hi = has_hi;
  v.hi = hi;
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  v.epoch = ++symbolic_epoch;
  symbolic_records()[symbolic_record_key{key, obj}] = v;
}

bool
__contracts_symbolic_check_range(const void* key, const void* obj,
				  bool has_lo, long long lo,
				  bool has_hi, long long hi) noexcept
{
  symbolic_entry entry;
  if (!entry.ok)
    return false;
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  auto it = symbolic_records().find(symbolic_record_key{key, obj});
  if (it == symbolic_records().end() || !it->second.is_range)
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

void
__contracts_symbolic_establish_relation(const void* key, unsigned n,
					 const void* a0, std::size_t s0,
					 const void* a1, std::size_t s1,
					 const void* a2, std::size_t s2,
					 const void* a3, std::size_t s3,
					 bool polarity,
					 const void* scope) noexcept
{
  symbolic_entry entry;
  if (!entry.ok)
    return;
  const void* args[4] = { a0, a1, a2, a3 };
  const std::size_t sizes[4] = { s0, s1, s2, s3 };
  symbolic_relation_value v{polarity, scope, {}, 0};
  std::string k = relation_key(key, n, args, sizes, &v.objs);
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  v.epoch = ++symbolic_epoch;
  symbolic_relations()[std::move(k)] = std::move(v);
}

bool
__contracts_symbolic_check_relation(const void* key, unsigned n,
				     const void* a0, std::size_t s0,
				     const void* a1, std::size_t s1,
				     const void* a2, std::size_t s2,
				     const void* a3, std::size_t s3,
				     bool polarity) noexcept
{
  symbolic_entry entry;
  if (!entry.ok)
    return false;
  const void* args[4] = { a0, a1, a2, a3 };
  const std::size_t sizes[4] = { s0, s1, s2, s3 };
  std::string k = relation_key(key, n, args, sizes, nullptr);
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  auto it = symbolic_relations().find(k);
  return it != symbolic_relations().end() && it->second.polarity == polarity;
}

void
__contracts_symbolic_end_scope(const void* scope) noexcept
{
  symbolic_entry entry;
  if (!entry.ok)
    return;
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  std::erase_if(symbolic_relations(),
		[scope](const auto& entry)
		{ return entry.second.scope == scope; });
}

void
__contracts_symbolic_invalidate_object(const void* obj) noexcept
{
  symbolic_entry entry;
  if (!entry.ok)
    return;
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  std::erase_if(symbolic_records(),
		[obj](const auto& entry) { return entry.first.obj == obj; });
  std::erase_if(symbolic_relations(),
		[obj](const auto& entry)
		{
		  for (const void* o : entry.second.objs)
		    if (o == obj)
		      return true;
		  return false;
		});
}

unsigned long long
__contracts_symbolic_epoch() noexcept
{
  symbolic_entry entry;
  if (!entry.ok)
    return 0;
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  return symbolic_epoch;
}

void
__contracts_symbolic_invalidate_range_before(const void* obj,
					     std::size_t size,
					     unsigned long long epoch) noexcept
{
  symbolic_entry entry;
  if (!entry.ok)
    return;
  auto first = reinterpret_cast<std::uintptr_t>(obj);
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  std::erase_if(symbolic_records(),
		[first, size, epoch](const auto& entry) {
		  auto addr = reinterpret_cast<std::uintptr_t>(entry.first.obj);
		  return addr - first < size && entry.second.epoch <= epoch;
		});
  std::erase_if(symbolic_relations(),
		[first, size, epoch](const auto& entry)
		{
		  if (entry.second.epoch > epoch)
		    return false;
		  for (const void* o : entry.second.objs)
		    if (reinterpret_cast<std::uintptr_t>(o) - first < size)
		      return true;
		  return false;
		});
}

void
__contracts_symbolic_invalidate_range(const void* obj,
				      std::size_t size) noexcept
{
  symbolic_entry entry;
  if (!entry.ok)
    return;
  auto first = reinterpret_cast<std::uintptr_t>(obj);
  std::lock_guard<std::mutex> lock(symbolic_mutex);
  std::erase_if(symbolic_records(),
		[first, size](const auto& entry) {
		  auto addr = reinterpret_cast<std::uintptr_t>(entry.first.obj);
		  return addr - first < size;
		});
  std::erase_if(symbolic_relations(),
		[first, size](const auto& entry)
		{
		  for (const void* o : entry.second.objs)
		    if (reinterpret_cast<std::uintptr_t>(o) - first < size)
		      return true;
		  return false;
		});
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
