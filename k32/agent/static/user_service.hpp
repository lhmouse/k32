// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_AGENT_STATIC_USER_SERVICE_
#define K32_AGENT_STATIC_USER_SERVICE_

#include "../../fwd.hpp"
#include "../../common/data/user_record.hpp"
namespace k32::agent {

class User_Service
  {
  private:
    struct X_Implementation;
    shptr<X_Implementation> m_impl;

  public:
    User_Service();

  public:
    User_Service(const User_Service&) = delete;
    User_Service& operator=(const User_Service&) & = delete;
    ~User_Service();

    // This callback is invoked when an HTTP GET or HEAD message is received from
    // a client. `request_raw_query` is the query string in the request URI.
    using http_handler_type = shared_function<
            void (
              ::poseidon::Abstract_Fiber& fiber,
              cow_string& response_content_type,  // output parameter
              cow_string& response_payload,  // output parameter
              const cow_string& request_raw_query)>;

    // Adds a new HTTP handler for requests from users. If a new handler
    // already exists, an exception is thrown.
    void
    add_http_handler(const phcow_string& path, const http_handler_type& handler);

    // Adds a new HTTP handler, or replaces an existing one, for requests from
    // users. If a new handler has been added, `true` is returned. If an existent
    // handler has been overwritten, `false` is returned.
    bool
    set_http_handler(const phcow_string& path, const http_handler_type& handler);

    // Removes an HTTP handler for requests from users.
    bool
    remove_http_handler(const phcow_string& path)
      noexcept;

    // This callback is invoked when a WebSocket connection is established from
    // a client. `request_raw_query` is the query string in the request URI.
    using ws_authenticator_type = shared_function<
            void (
              ::poseidon::Abstract_Fiber& fiber,
              phcow_string& username,  // output parameter
              const cow_string& request_raw_query)>;

    // Adds a new WebSocket authentication handler for users. If a new handler
    // already exists, an exception is thrown.
    void
    add_ws_authenticator(const phcow_string& path, const ws_authenticator_type& handler);

    // Adds a new WebSocket authentication handler, or replaces an existing one,
    // for users. If a new handler has been added, `true` is returned. If an
    // existent handler has been overwritten, `false` is returned.
    bool
    set_ws_authenticator(const phcow_string& path, const ws_authenticator_type& handler);

    // Removes a WebSocket authentication handler for requests from users.
    bool
    remove_ws_authenticator(const phcow_string& path)
      noexcept;

    // This callback is invoked when a WebSocket (text or binary) message is
    // received from a client.
    using ws_handler_type = shared_function<
            void (
              ::poseidon::Abstract_Fiber& fiber,
              const phcow_string& username,
              ::taxon::V_object& response,  // output parameter
              const ::taxon::V_object& request)>;

    // Adds a new WebSocket handler for requests from users. If a new handler
    // already exists, an exception is thrown.
    void
    add_ws_handler(const phcow_string& opcode, const ws_handler_type& handler);

    // Adds a new WebSocket handler, or replaces an existing one, for requests
    // from users. If a new handler has been added, `true` is returned. If an
    // existent handler has been overwritten, `false` is returned.
    bool
    set_ws_handler(const phcow_string& opcode, const ws_handler_type& handler);

    // Removes a WebSocket handler for requests from users.
    bool
    remove_ws_handler(const phcow_string& opcode)
      noexcept;

    // Gets all online users.
    const cow_dictionary<User_Record>&
    all_user_records()
      const noexcept;

    // Gets properties of a user.
    const User_Record&
    find_user_record_opt(const phcow_string& username)
      const noexcept;

    // Reloads configuration.
    void
    reload(const ::poseidon::Config_File& conf_file);
  };

}  // namespace k32::agent
#endif
