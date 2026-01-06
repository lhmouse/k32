// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/base/config_file.hpp>
namespace k32::logic {

Service service;
Virtual_Clock virtual_clock;
Role_Service role_service;

}  // namespace k32::logic

using namespace k32;
using namespace k32::logic;

void
poseidon_module_main(void)
  {
    ::poseidon::Config_File conf_file(&"k32.conf");
    service.reload(conf_file, &"logic");
    virtual_clock.reload(conf_file);
    role_service.reload(conf_file);


/*TEST*/

role_service.add_handler(
  &"+test/meow",
  +[](::poseidon::Abstract_Fiber& fiber,
      int64_t roid, ::taxon::V_object& resp, const ::taxon::V_object& req)
    {
      POSEIDON_LOG_FATAL(("ROLE $1 REQ: $2"), roid, req);
      resp.try_emplace(&"meow", 42);
    });

/*TEST*/

  }
