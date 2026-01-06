// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
#include "service_future.hpp"
namespace k32 {

Service_Future::
Service_Future(const cow_vector<::poseidon::UUID>& multicast_list,
               const phcow_string& opcode, const ::taxon::V_object& request)
  {
    this->m_opcode = opcode;
    this->m_request = request;

    this->m_responses.reserve(multicast_list.size());
    for(const auto& target_service_uuid : multicast_list)
      this->m_responses.emplace_back().service_uuid = target_service_uuid;
  }

Service_Future::
Service_Future(const ::poseidon::UUID& target_service_uuid, const phcow_string& opcode,
               const ::taxon::V_object& request)
  {
    this->m_opcode = opcode;
    this->m_request = request;

    this->m_responses.emplace_back().service_uuid = target_service_uuid;
  }

Service_Future::
~Service_Future()
  {
  }

void
Service_Future::
do_on_abstract_future_initialize()
  {
  }

}  // namespace k32
