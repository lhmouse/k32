// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#ifndef K32_LOGIC_STATIC_VIRTUAL_CLOCK_
#define K32_LOGIC_STATIC_VIRTUAL_CLOCK_

#include "../../fwd.hpp"
namespace k32::logic {

class Virtual_Clock
  {
  private:
    struct X_Implementation;
    shptr<X_Implementation> m_impl;

  public:
    Virtual_Clock();

  public:
    Virtual_Clock(const Virtual_Clock&) = delete;
    Virtual_Clock& operator=(const Virtual_Clock&) & = delete;
    ~Virtual_Clock();

    // The virtual clock offset is the number of seconds from the physical
    // time of the current system to the virtual time of this service.
    seconds
    virtual_offset()
      const noexcept __attribute__((__pure__));

    // These functions obtain the current virtual time.
    ::time_t
    get_time_t()
      const noexcept __attribute__((__pure__));

    double
    get_double_time_t()
      const noexcept __attribute__((__pure__));

    system_time
    get_system_time()
      const noexcept __attribute__((__pure__));

    Clock_Fields
    get_system_time_fields()
      const noexcept __attribute__((__pure__));

    // Reloads configuration.
    void
    reload(const ::poseidon::Config_File& conf_file);
  };

}  // namespace k32::logic
#endif
