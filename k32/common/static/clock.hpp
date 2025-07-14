// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_COMMON_STATIC_CLOCK_
#define K32_COMMON_STATIC_CLOCK_

#include "../../fwd.hpp"
namespace k32 {

class Clock
  {
  private:
    seconds m_offset = 0s;
    mutable ::time_t m_cached_time = 0;
    mutable Clock_Fields m_cached_fields = { };

  public:
    Clock();

  public:
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) & = delete;
    ~Clock();

    // The virtual clock offset is the number of seconds from the physical time
    // of the current system to the virtual time of this process.
    seconds
    virtual_offset()
      const noexcept
      { return this->m_offset;  }

    void
    set_virtual_offset(seconds secs)
      noexcept;

    // These functions obtain the current virtual time.
    ::time_t
    get_time_t()
      const noexcept;

    double
    get_double_time_t()
      const noexcept;

    system_time
    get_system_time()
      const noexcept;

    Clock_Fields
    get_system_time_fields()
      const noexcept;
  };

}  // namespace k32
#endif
