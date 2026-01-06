// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
#include "service_response.hpp"
namespace k32 {

const Service_Response Service_Response::null;

Service_Response::
~Service_Response()
  {
  }

void
Service_Response::
parse_from_string(const cow_string& str)
  {
    ::taxon::Value temp_value;
    POSEIDON_CHECK(temp_value.parse(str));
    ::taxon::V_object root = temp_value.as_object();
    temp_value.clear();

    this->service_uuid = ::poseidon::UUID(root.at(&"service_uuid").as_string());
    this->request_uuid = ::poseidon::UUID(root.at(&"request_uuid").as_string());
    this->obj = root.at(&"obj").as_object();
    this->error = root.at(&"error").as_string();
    this->complete = root.at(&"complete").as_boolean();
  }

cow_string
Service_Response::
serialize_to_string()
  const
  {
    ::taxon::V_object root;

    root.try_emplace(&"service_uuid", this->service_uuid.to_string());
    root.try_emplace(&"request_uuid", this->request_uuid.to_string());
    root.try_emplace(&"obj", this->obj);
    root.try_emplace(&"error", this->error);
    root.try_emplace(&"complete", this->complete);

    return ::taxon::Value(root).to_string();
  }

}  // namespace k32
