// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_COMMON_DATA_CHAT_THREAD_
#define K32_COMMON_DATA_CHAT_THREAD_

#include "../../fwd.hpp"
namespace k32 {

struct Chat_Thread
  {
    phcow_string thread_key;
    system_time update_time;
    cow_bivector<system_time, cow_string> messages;

#ifdef K32_FRIENDS_A40BD99F_5E7D_486F_A905_656CBDBE52AB_
    Chat_Thread() noexcept = default;
#endif
    Chat_Thread(const Chat_Thread&) = default;
    Chat_Thread(Chat_Thread&&) = default;
    Chat_Thread& operator=(const Chat_Thread&) & = default;
    Chat_Thread& operator=(Chat_Thread&&) & = default;
    ~Chat_Thread();

    static const Chat_Thread null;
    explicit operator bool()
      const noexcept { return !this->thread_key.empty();  }

    void
    parse_from_string(const cow_string& str);

    cow_string
    serialize_to_string()
      const;
  };

}  // namespace k32
#endif
