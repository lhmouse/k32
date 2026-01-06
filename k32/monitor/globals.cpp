// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/base/config_file.hpp>
namespace k32::monitor {

Service service;
Role_Service role_service;

}  // namespace k32::monitor

using namespace k32;
using namespace k32::monitor;

void
poseidon_module_main(void)
  {
    ::poseidon::Config_File conf_file(&"k32.conf");
    service.reload(conf_file, &"monitor");
    role_service.reload(conf_file);
  }
