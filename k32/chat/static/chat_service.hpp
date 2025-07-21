// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_CHAT_STATIC_CHAT_SERVICE_
#define K32_CHAT_STATIC_CHAT_SERVICE_

#include "../../fwd.hpp"
#include "../../common/data/chat_thread.hpp"
namespace k32::chat {

class Chat_Service
  {
  private:
    struct X_Implementation;
    shptr<X_Implementation> m_impl;

  public:
    Chat_Service();

  public:
    Chat_Service(const Chat_Service&) = delete;
    Chat_Service& operator=(const Chat_Service&) & = delete;
    ~Chat_Service();

    // Gets all chat threads of this zone.
    const cow_dictionary<Chat_Thread>&
    all_chat_threads()
      const noexcept;

    // Gets a single chat thread.
    const Chat_Thread&
    find_chat_thread_opt(const phcow_string& thread_key)
      const noexcept;

    // Reloads configuration.
    void
    reload(const ::poseidon::Config_File& conf_file);
  };

}  // namespace k32::chat
#endif
