// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#include "virtual_clock.hpp"
#include "../globals.hpp"
#include <poseidon/base/config_file.hpp>
#include <time.h>
namespace k32::logic {
namespace {

struct Implementation
  {
    ::time_t offset;
    ::time_t cached_fields_ts;
    Clock_Fields cached_fields;
  };

void
do_star_virtual_clock_set_offset(const shptr<Implementation>& impl,
                                 ::poseidon::Abstract_Fiber& /*fiber*/,
                                 const ::poseidon::UUID& /*request_service_uuid*/,
                                 ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    // * Request Parameters
    //   - `offset` <sub>integer</sub> : Offset in seconds.
    //
    // * Response Parameters
    //   - `status` <sub>string</sub> : [General status code.](#general-status-codes)
    //
    // * Description
    //   Sets offset of the virtual clock.

    ////////////////////////////////////////////////////////////
    //
    int64_t offset = request.at(&"offset").as_integer();
    POSEIDON_CHECK((offset >= -999999999) && (offset <= +999999999));

    ////////////////////////////////////////////////////////////
    //
    impl->offset = static_cast<::time_t>(offset);
    POSEIDON_LOG_INFO(("Clock virtual offset has been set to $1"), seconds(impl->offset));

    response.try_emplace(&"status", &"gs_ok");
  }

struct timespec
do_get_virtual_timespec(::time_t offset)
  {
    struct timespec ts;
    ::clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += offset;
    return ts;
  }

}  // namespace

POSEIDON_HIDDEN_X_STRUCT(Virtual_Clock,
    Implementation);

Virtual_Clock::
Virtual_Clock()
  {
  }

Virtual_Clock::
~Virtual_Clock()
  {
  }

::time_t
Virtual_Clock::
get_time_t()
  const noexcept
  {
    if(!this->m_impl)
      return 0;

    auto ts = do_get_virtual_timespec(this->m_impl->offset);
    return ts.tv_sec;
  }

double
Virtual_Clock::
get_double_time_t()
  const noexcept
  {
    if(!this->m_impl)
      return 0;

    auto ts = do_get_virtual_timespec(this->m_impl->offset);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1.0e-9;
  }

system_time
Virtual_Clock::
get_system_time()
  const noexcept
  {
    if(!this->m_impl)
      return system_time();

    auto ts = do_get_virtual_timespec(this->m_impl->offset);
    return system_clock::from_time_t(ts.tv_sec) + nanoseconds(ts.tv_nsec);
  }

Clock_Fields
Virtual_Clock::
get_system_time_fields()
  const noexcept
  {
    if(!this->m_impl)
      return Clock_Fields();

    auto ts = do_get_virtual_timespec(this->m_impl->offset);
    if(ROCKET_UNEXPECT(ts.tv_sec != this->m_impl->cached_fields_ts)) {
      ::tm tm;
      ::localtime_r(&(ts.tv_sec), &tm);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
      this->m_impl->cached_fields.year = tm.tm_year + 1900;
      this->m_impl->cached_fields.month = tm.tm_mon + 1;
      this->m_impl->cached_fields.day_of_month = tm.tm_mday;
      this->m_impl->cached_fields.hour = tm.tm_hour;
      this->m_impl->cached_fields.minute = tm.tm_min;
      this->m_impl->cached_fields.second = tm.tm_sec;
      this->m_impl->cached_fields.milliseconds = 0;
      this->m_impl->cached_fields.tz_offset = static_cast<int32_t>(tm.tm_gmtoff) / 60000;
      this->m_impl->cached_fields.dst = tm.tm_isdst > 0;
      this->m_impl->cached_fields.day_of_week = tm.tm_wday + 1;
      this->m_impl->cached_fields.reserved = 0;
      this->m_impl->cached_fields_ts = ts.tv_sec;
    }
    this->m_impl->cached_fields.milliseconds = static_cast<uint32_t>(ts.tv_nsec) / 1000000;
#pragma GCC diagnostic pop
    return this->m_impl->cached_fields;
  }

void
Virtual_Clock::
reload(const ::poseidon::Config_File& conf_file)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // `logic.virtual_clock_offset`
    ::time_t virtual_clock_offset = static_cast<::time_t>(conf_file.get_integer_opt(
                          &"logic.virtual_clock_offset", -99999999, 99999999).value_or(0));

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->offset = virtual_clock_offset;

    // Set up request handlers.
    service.set_handler(&"*virtual_clock/set_offset", bindw(this->m_impl, do_star_virtual_clock_set_offset));
  }

}  // namespace k32::logic
