// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/base/config_file.hpp>
namespace k32::agent {

Service service;
User_Service user_service;

}  // namespace k32::agent

using namespace k32;
using namespace k32::agent;

void
poseidon_module_main(void)
  {
    ::poseidon::Config_File conf_file(&"k32.conf");
    service.reload(conf_file, &"agent");
    user_service.reload(conf_file);

/*TEST*/

user_service.add_ws_authenticator(
  &"/t0",
  +[](::poseidon::Abstract_Fiber& fiber,
      phcow_string& username, const cow_string& request_raw_query)
    {
      POSEIDON_LOG_FATAL(("WS AUTH: $1"), request_raw_query);
      username = &"test01005";
    });

/*TEST*/


  }
