// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_FWD_
#define K32_FWD_

#include <asteria/value.hpp>
#include <asteria/utils.hpp>
#include <poseidon/base/uuid.hpp>
#include <poseidon/socket/ipv6_address.hpp>
#include <poseidon/fiber/abstract_fiber.hpp>
#include <poseidon/utils.hpp>
#include <taxon.hpp>
namespace k32 {
namespace noadl = k32;
using namespace ::poseidon::fwd;

POSEIDON_USING cow_dictionary = cow_hashmap<phcow_string, Ts..., phcow_string::hash>;
POSEIDON_USING cow_int32_dictionary = cow_hashmap<int32_t, Ts..., ::std::hash<uint32_t>>;
POSEIDON_USING cow_int64_dictionary = cow_hashmap<int64_t, Ts..., ::std::hash<uint64_t>>;
POSEIDON_USING cow_uuid_dictionary = cow_hashmap<::poseidon::UUID, Ts..., ::poseidon::UUID::hash>;

// Private WebSocket status codes to clients
enum User_WS_Status : uint16_t
  {
    user_ws_status_authentication_failure    = 4301,
    user_ws_status_login_conflict            = 4302,
    user_ws_status_unknown_opcode            = 4303,
    user_ws_status_message_rate_limit        = 4304,
    user_ws_status_ping_timeout              = 4305,
    user_ws_status_ban                       = 4306,
  };

// Broken-down wallclock time
struct Clock_Fields
  {
    uint32_t year          : 12;  //    0 - 4095
    uint32_t month         :  4;  //    1 - 12
    uint32_t day_of_month  :  5;  //    1 - 31
    uint32_t hour          :  5;  //    0 - 23
    uint32_t minute        :  6;  //    0 - 59
    uint32_t second        :  6;  //    0 - 60 (leap)
    uint32_t milliseconds  : 10;  //    0 - 999
    int32_t tz_offset      : 10;  // -720 - 720
    uint32_t dst           :  1;  // daylight saving time
    uint32_t day_of_week   :  3;  //    1 - 7
    uint32_t reserved      :  2;
  };

// Callback helper
template<typename xSelf, typename xOther, typename... xArgs>
ROCKET_ALWAYS_INLINE
shared_function<vfn<xArgs...>>
bindw(const shptr<xSelf>& self, vfn<const shptr<xOther>&, xArgs...>* pfunc)
  {
    return shared_function<vfn<xArgs...>>(
        [w = wkptr<xOther>(self), pfunc] (xArgs&&... args) {
          if(const auto other = w.lock())
            (*pfunc) (other, forward<xArgs>(args)...);
        });
  }

}  // namespace k32
#endif
