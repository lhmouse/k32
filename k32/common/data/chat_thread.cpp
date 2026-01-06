// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_A40BD99F_5E7D_486F_A905_656CBDBE52AB_
#include "chat_thread.hpp"
namespace k32 {

const Chat_Thread Chat_Thread::null;

Chat_Thread::
~Chat_Thread()
  {
  }

void
Chat_Thread::
parse_from_string(const cow_string& str)
  {
    ::taxon::Value temp_value;
    POSEIDON_CHECK(temp_value.parse(str));
    ::taxon::V_object root = temp_value.as_object();
    temp_value.clear();

    this->thread_key = root.at(&"thread_key").as_string();
    this->update_time = root.at(&"update_time").as_time();

    this->messages.clear();
    for(const auto& r : root.at(&"messages").as_array()) {
      auto& msg = this->messages.emplace_back();
      msg.first = r.as_array().at(0).as_time();
      msg.second = r.as_array().at(1).as_string();
    }
  }

cow_string
Chat_Thread::
serialize_to_string()
  const
  {
    ::taxon::V_object root;

    root.try_emplace(&"thread_key", this->thread_key);
    root.try_emplace(&"update_time", this->update_time);

    auto pa = &(root.open(&"messages").open_array());
    for(const auto& msg : this->messages) {
      auto& sub = pa->emplace_back().open_array();
      sub.emplace_back(msg.first);   // 0
      sub.emplace_back(msg.second);  // 1
    }

    return ::taxon::Value(root).to_string();
  }

}  // namespace k32
