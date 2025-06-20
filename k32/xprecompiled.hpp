// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_XPRECOMPILED_
#define K32_XPRECOMPILED_

// Prevent use of standard streams.
#define _IOS_BASE_H  1
#define _STREAM_ITERATOR_H  1
#define _STREAMBUF_ITERATOR_H  1
#define _GLIBCXX_ISTREAM  1
#define _GLIBCXX_OSTREAM  1
#define _GLIBCXX_IOSTREAM  1

#include <rocket/cow_string.hpp>
#include <rocket/cow_vector.hpp>
#include <rocket/cow_hashmap.hpp>
#include <rocket/static_vector.hpp>
#include <rocket/prehashed_string.hpp>
#include <rocket/unique_handle.hpp>
#include <rocket/unique_posix_file.hpp>
#include <rocket/unique_posix_dir.hpp>
#include <rocket/unique_posix_fd.hpp>
#include <rocket/variant.hpp>
#include <rocket/optional.hpp>
#include <rocket/array.hpp>
#include <rocket/reference_wrapper.hpp>
#include <rocket/tinyfmt.hpp>
#include <rocket/tinyfmt_str.hpp>
#include <rocket/tinyfmt_file.hpp>
#include <rocket/ascii_numget.hpp>
#include <rocket/ascii_numput.hpp>
#include <rocket/atomic.hpp>
#include <rocket/mutex.hpp>
#include <rocket/recursive_mutex.hpp>
#include <rocket/condition_variable.hpp>
#include <rocket/shared_function.hpp>
#include <rocket/static_char_buffer.hpp>

#include <iterator>
#include <memory>
#include <utility>
#include <exception>
#include <typeinfo>
#include <type_traits>
#include <functional>
#include <algorithm>
#include <numeric>
#include <string>
#include <bitset>
#include <chrono>
#include <array>
#include <vector>
#include <deque>
#include <list>
#include <forward_list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <cstdio>
#include <climits>
#include <cmath>
#include <cfenv>
#include <cfloat>
#include <cstring>
#include <cerrno>

#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <wchar.h>
#include <cxxabi.h>
#include <x86intrin.h>
#include <nmmintrin.h>
#include <immintrin.h>

#include <asteria/value.hpp>
#include <asteria/utils.hpp>
#include <poseidon/base/uuid.hpp>
#include <poseidon/fiber/abstract_fiber.hpp>
#include <poseidon/static/fiber_scheduler.hpp>
#include <poseidon/utils.hpp>
#include <taxon.hpp>

#endif
