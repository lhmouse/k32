// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#include "clock.hpp"
#include <time.h>
namespace k32 {

Clock::
Clock()
  {
  }

Clock::
~Clock()
  {
  }

void
Clock::
set_virtual_offset(seconds secs)
  noexcept
  {
    this->m_offset = secs;
    POSEIDON_LOG_INFO(("Clock virtual offset has been set to $1"), this->m_offset);
  }

::time_t
Clock::
get_time_t()
  const noexcept
  {
    struct timespec ts;
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + this->m_offset.count();
  }

double
Clock::
get_double_time_t()
  const noexcept
  {
    struct timespec ts;
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<double>(ts.tv_sec + this->m_offset.count())
           + static_cast<double>(ts.tv_nsec) * 1.0e-9;
  }

system_time
Clock::
get_system_time()
  const noexcept
  {
    struct timespec ts;
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return system_clock::from_time_t(ts.tv_sec) + this->m_offset
           + nanoseconds(ts.tv_nsec);
  }

Clock_Fields
Clock::
get_system_time_fields()
  const noexcept
  {
    struct timespec ts;
    ::clock_gettime(CLOCK_REALTIME, &ts);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
    if(ROCKET_UNEXPECT(this->m_cached_time != ts.tv_sec)) {
      ::tm tm;
      ::localtime_r(&(ts.tv_sec), &tm);
      this->m_cached_fields.year = tm.tm_year + 1900;
      this->m_cached_fields.month = tm.tm_mon + 1;
      this->m_cached_fields.day_of_month = tm.tm_mday;
      this->m_cached_fields.hour = tm.tm_hour;
      this->m_cached_fields.minute = tm.tm_min;
      this->m_cached_fields.second = tm.tm_sec;
      this->m_cached_fields.tz_offset = (int32_t) tm.tm_gmtoff / 60000;
      this->m_cached_fields.dst = tm.tm_isdst > 0;
      this->m_cached_fields.day_of_week = tm.tm_wday + 1;
      this->m_cached_time = ts.tv_sec;
    }
    this->m_cached_fields.milliseconds = (uint32_t) ts.tv_nsec / 1000000;
#pragma GCC diagnostic pop
    return this->m_cached_fields;
  }

}  // namespace k32
