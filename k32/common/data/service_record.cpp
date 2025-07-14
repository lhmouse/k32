// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
#include "service_record.hpp"
namespace k32 {

const Service_Record Service_Record::null;

Service_Record::
~Service_Record()
  {
  }

void
Service_Record::
parse_from_string(const cow_string& str)
  {
    ::taxon::Value temp_value;
    POSEIDON_CHECK(temp_value.parse(str));
    ::taxon::V_object root = temp_value.as_object();
    temp_value.clear();

    this->service_uuid = ::poseidon::UUID(root.at(&"service_uuid").as_string());
    this->application_name = root.at(&"application_name").as_string();
    this->zone_id = static_cast<int>(root.at(&"zone_id").as_integer());
    this->service_index = static_cast<int>(root.at(&"service_index").as_integer());
    this->zone_start_time = root.at(&"zone_start_time").as_time();
    this->service_type = root.at(&"service_type").as_string();

    this->load_factor = root.at(&"load_factor").as_number();
    this->hostname = root.at(&"hostname").as_string();

    this->addresses.clear();
    for(const auto& r : root.at(&"addresses").as_array())
      this->addresses.emplace_back(r.as_string());
  }

cow_string
Service_Record::
serialize_to_string()
  const
  {
    ::taxon::V_object root;

    root.try_emplace(&"service_uuid", this->service_uuid.to_string());
    root.try_emplace(&"application_name", this->application_name);
    root.try_emplace(&"zone_id", this->zone_id);
    root.try_emplace(&"service_index", this->service_index);
    root.try_emplace(&"zone_start_time", this->zone_start_time);
    root.try_emplace(&"service_type", this->service_type);

    root.try_emplace(&"load_factor", this->load_factor);
    root.try_emplace(&"hostname", this->hostname);

    auto pa = &(root.open(&"addresses").open_array());
    for(const auto& addr : this->addresses)
      pa->emplace_back(addr.to_string());

    return ::taxon::Value(root).to_string();
  }

}  // namespace k32
