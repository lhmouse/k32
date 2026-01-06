// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/base/config_file.hpp>
namespace k32::chat {

Service service;
Chat_Service chat_service;

}  // namespace k32::chat

using namespace k32;
using namespace k32::chat;

void
poseidon_module_main(void)
  {
    ::poseidon::Config_File conf_file(&"k32.conf");
    service.reload(conf_file, &"chat");
    chat_service.reload(conf_file);
  }
