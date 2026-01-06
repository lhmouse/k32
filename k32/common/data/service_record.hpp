// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#ifndef K32_COMMON_DATA_SERVICE_RECORD_
#define K32_COMMON_DATA_SERVICE_RECORD_

#include "../../fwd.hpp"
namespace k32 {

struct Service_Record
  {
    ::poseidon::UUID service_uuid;
    cow_string application_name;
    int zone_id = 0;
    int service_index = 0;
    system_time zone_start_time;
    cow_string service_type;

    double load_factor = 0;
    cow_string hostname;
    cow_vector<::poseidon::IPv6_Address> addresses;

#ifdef K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
    Service_Record() noexcept = default;
#endif
    Service_Record(const Service_Record&) = default;
    Service_Record(Service_Record&&) = default;
    Service_Record& operator=(const Service_Record&) & = default;
    Service_Record& operator=(Service_Record&&) & = default;
    ~Service_Record();

    static const Service_Record null;
    explicit operator bool()
      const noexcept { return !this->service_uuid.is_nil();  }

    void
    parse_from_string(const cow_string& str);

    cow_string
    serialize_to_string()
      const;
  };

}  // namespace k32
#endif
