// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_6A8BAC8C_B8F6_4BDA_BD7F_B90D5BF07B81_
#include "user_record.hpp"
namespace k32 {

const User_Record User_Record::null;

User_Record::
~User_Record()
  {
  }

void
User_Record::
parse_from_string(const cow_string& str)
  {
    ::taxon::Value temp_value;
    POSEIDON_CHECK(temp_value.parse(str));
    ::taxon::V_object root = temp_value.as_object();
    temp_value.clear();

    this->username = root.at(&"username").as_string();
    this->login_address = ::poseidon::IPv6_Address(root.at(&"login_address").as_string());
    this->creation_time = root.at(&"creation_time").as_time();
    this->login_time = root.at(&"login_time").as_time();
    this->logout_time = root.at(&"logout_time").as_time();
    this->banned_until = root.at(&"banned_until").as_time();

    this->_agent_srv = ::poseidon::UUID(root.at(&"@agent_srv").as_string());
  }

cow_string
User_Record::
serialize_to_string()
  const
  {
    ::taxon::V_object root;

    root.try_emplace(&"username", this->username.rdstr());
    root.try_emplace(&"login_address", this->login_address.to_string());
    root.try_emplace(&"creation_time", this->creation_time);
    root.try_emplace(&"login_time", this->login_time);
    root.try_emplace(&"logout_time", this->logout_time);
    root.try_emplace(&"banned_until", this->banned_until);

    root.try_emplace(&"@agent_srv", this->_agent_srv.to_string());

    return ::taxon::Value(root).to_string();
  }

}  // namespace k32
