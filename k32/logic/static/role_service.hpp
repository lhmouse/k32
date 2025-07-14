// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_LOGIC_STATIC_ROLE_SERVICE_
#define K32_LOGIC_STATIC_ROLE_SERVICE_

#include "../../fwd.hpp"
#include "../hydrated/role.hpp"
namespace k32::logic {

class Role_Service
  {
  private:
    struct X_Implementation;
    shptr<X_Implementation> m_impl;

  public:
    Role_Service();

  public:
    Role_Service(const Role_Service&) = delete;
    Role_Service& operator=(const Role_Service&) & = delete;
    ~Role_Service();

    // This callback is invoked when a request message from a client is
    // received.
    using handler_type = shared_function<
            void (
              ::poseidon::Abstract_Fiber& fiber,
              int64_t roid,
              ::taxon::V_object& response,  // output parameter
              const ::taxon::V_object& request)>;

    // Adds a new handler for requests from clients. If a new handler already
    // exists, an exception is thrown.
    void
    add_handler(const phcow_string& opcode, const handler_type& handler);

    // Adds a new handler, or replaces an existing one, for requests from
    // clients. If a new handler has been added, `true` is returned. If an
    // existent handler has been overwritten, `false` is returned.
    bool
    set_handler(const phcow_string& opcode, const handler_type& handler);

    // Removes a handler for requests from clients.
    bool
    remove_handler(const phcow_string& opcode)
      noexcept;

    // Gets a hydrated role on this service.
    shptr<Role>
    find_online_role_opt(int64_t roid)
      const noexcept;

    // Reloads configuration.
    void
    reload(const ::poseidon::Config_File& conf_file);
  };

}  // namespace k32::logic
#endif
