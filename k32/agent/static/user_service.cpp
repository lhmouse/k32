// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_6A8BAC8C_B8F6_4BDA_BD7F_B90D5BF07B81_
#include "user_service.hpp"
#include "../globals.hpp"
#include "../../common/static/service.hpp"
#include <poseidon/base/config_file.hpp>
#include <poseidon/easy/easy_hws_server.hpp>
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/http/http_query_parser.hpp>
#include <poseidon/fiber/mysql_check_table_future.hpp>
#include <poseidon/fiber/redis_query_future.hpp>
#include <poseidon/static/task_scheduler.hpp>
#include <poseidon/fiber/mysql_query_future.hpp>
#include <poseidon/mysql/mysql_connection.hpp>
#include <poseidon/static/mysql_connector.hpp>
namespace k32::agent {
namespace {

const cow_dictionary<User_Record> empty_user_map;

struct User_Connection
  {
    wkptr<::poseidon::WS_Server_Session> weak_session;
    steady_time rate_time;
    steady_time pong_time;
    int rate_counter = 0;

    int64_t current_roid = 0;
    ::poseidon::UUID current_logic_srv;
    cow_int64_dictionary<::taxon::V_object> cached_raw_avatars;
  };

struct Implementation
  {
    seconds redis_role_ttl;
    uint16_t client_port;
    uint16_t client_rate_limit;
    uint16_t max_number_of_roles_per_user = 0;
    uint8_t nickname_length_limits[2] = { };
    seconds client_ping_interval;

    cow_dictionary<User_Service::http_handler_type> http_handlers;
    cow_dictionary<User_Service::ws_authenticator_type> ws_authenticators;
    cow_dictionary<User_Service::ws_handler_type> ws_handlers;

    ::poseidon::Easy_Timer ping_timer;
    ::poseidon::Easy_Timer check_user_timer;
    ::poseidon::Easy_HWS_Server user_server;

    // connections from clients
    bool db_ready = false;
    cow_dictionary<User_Record> users;
    cow_dictionary<User_Connection> connections;
    ::std::vector<phcow_string> expired_username_list;
  };

phcow_string
do_get_username(const shptr<Implementation>& impl, const shptr<::poseidon::WS_Server_Session>& sp)
  {
    if(sp->session_user_data().is_null())
      return phcow_string();

    auto it = impl->connections.find(sp->session_user_data().as_string());
    if(it == impl->connections.end())
      return phcow_string();

    if(sp.owner_before(it->second.weak_session) || it->second.weak_session.owner_before(sp))
      return phcow_string();

    return it->first;
  }

::poseidon::UUID
do_find_my_monitor()
  {
    ::poseidon::UUID monitor_service_uuid;
    for(const auto& r : service.all_service_records())
      if((r.second.zone_id == service.zone_id()) && (r.second.service_type == "monitor"))
        monitor_service_uuid = r.first;

    if(monitor_service_uuid == ::poseidon::UUID::min())
      POSEIDON_THROW(("No monitor service online"));

    return monitor_service_uuid;
  }

void
do_publish_user_on_redis(::poseidon::Abstract_Fiber& fiber, const User_Record& uinfo, seconds ttl)
  {
    cow_vector<cow_string> redis_cmd;
    redis_cmd.emplace_back(&"SET");
    redis_cmd.emplace_back(sformat("$1/user/$2", service.application_name(), uinfo.username));
    redis_cmd.emplace_back(uinfo.serialize_to_string());
    redis_cmd.emplace_back(&"GET");
    redis_cmd.emplace_back(&"EX");
    redis_cmd.emplace_back(sformat("$1", ttl.count()));

    auto task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);

    if(!task2->result().is_nil()) {
      User_Record old_uinfo;
      old_uinfo.parse_from_string(task2->result().as_string());
      if(old_uinfo._agent_srv != uinfo._agent_srv) {
        // If the user exists a different service, disconnect them.
        POSEIDON_LOG_DEBUG(("`$1` login conflict with `$2`"), uinfo.username, old_uinfo._agent_srv);

        ::taxon::V_object tx_args;
        tx_args.try_emplace(&"username", old_uinfo.username.rdstr());
        tx_args.try_emplace(&"ws_status", static_cast<int>(user_ws_status_login_conflict));

        auto srv_q = new_sh<Service_Future>(old_uinfo._agent_srv, &"*user/kick", tx_args);
        service.launch(srv_q);
        fiber.yield(srv_q);
      }
    }

    POSEIDON_LOG_TRACE(("Published user `$1` on Redis"), uinfo.username);
  }

void
do_role_logout_common(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                      const phcow_string& username)
  {
    int64_t roid = impl->connections.at(username).current_roid;
    ::poseidon::UUID logic_service_uuid = impl->connections.at(username).current_logic_srv;

    // Bring this role offline. In order to prevent multiple logins, `current_roid`
    // must not be cleared until the request completes.
    ::taxon::V_object tx_args;
    tx_args.try_emplace(&"roid", roid);

    auto srv_q = new_sh<Service_Future>(logic_service_uuid, &"*role/logout", tx_args);
    service.launch(srv_q);
    fiber.yield(srv_q);

    // Unlock the connection.
    impl->connections.mut(username).current_roid = 0;
    impl->connections.mut(username).current_logic_srv = ::poseidon::UUID();
  }

void
do_role_login_common(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                     const phcow_string& username, int64_t roid)
  {
    if(impl->connections.at(username).current_roid == roid)
      return;

    if(impl->connections.at(username).current_roid != 0)
      do_role_logout_common(impl, fiber, username);

    // Select a logic server with lowest load factor.
    ::poseidon::UUID logic_service_uuid;
    double load_factor = HUGE_VAL;
    for(const auto& r : service.all_service_records())
      if((r.second.zone_id == service.zone_id()) && (r.second.service_type == "logic")
            && (r.second.load_factor <= load_factor))
        logic_service_uuid = r.first;

    if(logic_service_uuid == ::poseidon::UUID::min())
      POSEIDON_THROW(("No logic service online"));

    // Lock the connection.
    impl->connections.mut(username).current_roid = roid;
    impl->connections.mut(username).current_logic_srv = logic_service_uuid;

    ::taxon::V_object tx_args;
    tx_args.try_emplace(&"roid", roid);
    tx_args.try_emplace(&"agent_srv", service.service_uuid().to_string());
    tx_args.try_emplace(&"monitor_srv", do_find_my_monitor().to_string());

    auto srv_q = new_sh<Service_Future>(logic_service_uuid, &"*role/login", tx_args);
    service.launch(srv_q);
    fiber.yield(srv_q);

    auto status = srv_q->response(0).obj.at(&"status").as_string();
    POSEIDON_CHECK(status == "gs_ok");
  }

void
do_welcome_client(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                  const phcow_string& username, const shptr<::poseidon::WS_Server_Session>& session)
  {
    if(impl->connections.at(username).current_roid != 0)
      return;

    if(impl->connections.at(username).cached_raw_avatars.size() > 0) {
      // In case there's an online role, try reconnecting.
      ::taxon::V_object tx_args;
      tx_args.try_emplace(&"agent_srv", service.service_uuid().to_string());
      for(const auto& r : impl->connections.at(username).cached_raw_avatars)
        tx_args.open(&"roid_list").open_array().emplace_back(r.first);

      cow_vector<::poseidon::UUID> multicast_list;
      for(const auto& r : service.all_service_records())
        if(r.second.service_type == "logic")
          multicast_list.emplace_back(r.first);

      auto srv_q = new_sh<Service_Future>(multicast_list, &"*role/reconnect", tx_args);
      service.launch(srv_q);
      fiber.yield(srv_q);

      for(const auto& resp : srv_q->responses()) {
        auto ptr = resp.obj.ptr(&"status");
        if(ptr && ptr->is_string() && (ptr->as_string() == "gs_ok")) {
          // Use the online role.
          impl->connections.mut(username).current_roid = resp.obj.at(&"roid").as_integer();
          impl->connections.mut(username).current_logic_srv = resp.service_uuid;
          break;
        }
      }
    }

    if(impl->connections.at(username).current_roid != 0)
      return;

    // No role is online. If a fresh role exists, resume creation.
    int64_t fresh_roid = 0;
    for(const auto& r : impl->connections.at(username).cached_raw_avatars)
      if(r.second.empty())
        fresh_roid = r.first;

    if(fresh_roid != 0) {
      // Load this role into Redis, in case it's been unloaded.
      ::taxon::V_object tx_args;
      tx_args.try_emplace(&"roid", fresh_roid);

      auto srv_q = new_sh<Service_Future>(do_find_my_monitor(), &"*role/load", tx_args);
      service.launch(srv_q);
      fiber.yield(srv_q);

      do_role_login_common(impl, fiber, username, fresh_roid);
    }

    if(impl->connections.at(username).current_roid != 0)
      return;

    // No role is online. No role is being created. Send my role list to the
    // client, so the user may select an existing role, or create a new one.
    ::taxon::V_array avatar_list;
    for(const auto& r : impl->connections.at(username).cached_raw_avatars)
      avatar_list.emplace_back(r.second);

    ::taxon::V_object tx_args;
    tx_args.try_emplace(&"%opcode", &"=role/list");
    tx_args.try_emplace(&"avatar_list", avatar_list);

    auto str = ::taxon::Value(tx_args).to_string(::taxon::option_json_mode);
    session->ws_send(::poseidon::ws_TEXT, str);
  }

void
do_server_hws_callback(const shptr<Implementation>& impl,
                       const shptr<::poseidon::WS_Server_Session>& session,
                       ::poseidon::Abstract_Fiber& fiber, ::poseidon::Easy_HWS_Event event,
                       linear_buffer&& data)
  {
    switch(event)
      {
      case ::poseidon::easy_hws_open:
        {
          ::poseidon::Network_Reference uri;
          POSEIDON_CHECK(::poseidon::parse_network_reference(uri, data) == data.size());
          phcow_string path = ::poseidon::decode_and_canonicalize_uri_path(uri.path);

          // Wait for database verification.
          if(impl->db_ready == false) {
            POSEIDON_LOG_DEBUG(("User database verification in progress"));
            session->ws_shut_down(::poseidon::ws_status_try_again_later);
            return;
          }

          // Copy the authenticator, in case of fiber context switches.
          User_Service::ws_authenticator_type authenticator;
          impl->ws_authenticators.find_and_copy(authenticator, path);
          if(!authenticator) {
            session->ws_shut_down(user_ws_status_authentication_failure);
            return;
          }

          // Call the user-defined authenticator to get the username.
          User_Record uinfo;
          uinfo._agent_srv = service.service_uuid();
          try {
            authenticator(fiber, uinfo.username, cow_string(uri.query));
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Unhandled exception in `$1 $2`: $3"), path, uri.query, stdex);
            session->ws_shut_down(::poseidon::ws_status_unexpected_error);
            return;
          }

          if(uinfo.username.size() < 3) {
            POSEIDON_LOG_DEBUG(("Authenticated for `$1`"), path);
            session->ws_shut_down(user_ws_status_authentication_failure);
            return;
          }

          POSEIDON_LOG_INFO(("Authenticated `$1` from `$2`"), uinfo.username, session->remote_address());
          session->mut_session_user_data() = uinfo.username.rdstr();

          // Create the user if one doesn't exist. Ensure they can only log in
          // on a single instance.
          uinfo.login_address = session->remote_address();
          uinfo.login_time = system_clock::now();

          static constexpr char insert_into_user[] =
              R"!!!(
                INSERT INTO `user`
                  SET `username` = ?
                      , `login_address` = ?
                      , `creation_time` = ?
                      , `login_time` = ?
                      , `logout_time` = ?
                      , `banned_until` = '1999-01-01'
                  ON DUPLICATE KEY
                  UPDATE `login_address` = ?
                         , `login_time` = ?
              )!!!";

          cow_vector<::poseidon::MySQL_Value> sql_args;
          sql_args.emplace_back(uinfo.username.rdstr());            // SET `username` = ?
          sql_args.emplace_back(uinfo.login_address.to_string());   //     , `login_address` = ?
          sql_args.emplace_back(uinfo.login_time);                  //     , `creation_time` = ?
          sql_args.emplace_back(uinfo.login_time);                  //     , `login_time` = ?
          sql_args.emplace_back(uinfo.login_time);                  //     , `logout_time` = ?
          sql_args.emplace_back(uinfo.login_address.to_string());   // UPDATE `login_address` = ?
          sql_args.emplace_back(uinfo.login_time);                  //        , `login_time` = ?

          auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                      ::poseidon::mysql_connector.allocate_tertiary_connection(),
                                      &insert_into_user, sql_args);
          ::poseidon::task_scheduler.launch(task1);
          fiber.yield(task1);

          static constexpr char select_from_user[] =
              R"!!!(
                SELECT `creation_time`
                       , `logout_time`
                       , `banned_until`
                  FROM `user`
                  WHERE `username` = ?
              )!!!";

          sql_args.clear();
          sql_args.emplace_back(uinfo.username.rdstr());

          task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                      ::poseidon::mysql_connector.allocate_tertiary_connection(),
                                      &select_from_user, sql_args);
          ::poseidon::task_scheduler.launch(task1);
          fiber.yield(task1);

          if(task1->result_row_count() == 0) {
            POSEIDON_LOG_FATAL(("Could not find user `$1` in database"), uinfo.username);
            session->ws_shut_down(::poseidon::ws_status_unexpected_error);
            return;
          }

          uinfo.creation_time = task1->result_row(0).at(0).as_system_time();   // SELECT `creation_time`
          uinfo.logout_time = task1->result_row(0).at(1).as_system_time();     //        , `logout_time`
          uinfo.banned_until = task1->result_row(0).at(2).as_system_time();    //        , `banned_until`

          if(uinfo.login_time < uinfo.banned_until) {
            POSEIDON_LOG_DEBUG(("User `$1` is banned until `$2`"), uinfo.username, uinfo.banned_until);
            session->ws_shut_down(user_ws_status_ban);
            return;
          }

          // Find my roles.
          User_Connection uconn;
          uconn.weak_session = session;
          uconn.rate_time = steady_clock::now();
          uconn.pong_time = uconn.rate_time;

          ::taxon::V_object tx_args;
          tx_args.try_emplace(&"username", uinfo.username.rdstr());

          auto srv_q = new_sh<Service_Future>(do_find_my_monitor(), &"*role/list", tx_args);
          service.launch(srv_q);
          fiber.yield(srv_q);

          if(srv_q->response(0).error != "") {
            POSEIDON_LOG_WARN(("Could not list roles of `$1`: $2"), uinfo.username, srv_q->response(0).error);
            session->ws_shut_down(::poseidon::ws_status_try_again_later);
            return;
          }

          for(const auto& r : srv_q->response(0).obj.at(&"raw_avatars").as_object()) {
            // For intermediate servers, an avatar is transferred as a JSON
            // string. We will not send a raw string to the client, so parse it.
            // Mind a fresh role (that has just been created but has not been
            // loaded yet), whose avatar is an empty string.
            int64_t roid = 0;
            ::rocket::ascii_numget numg;
            POSEIDON_CHECK(numg.parse_DI(r.first.data(), r.first.length()) == r.first.length());
            numg.cast_I(roid, 0, INT64_MAX);

            ::taxon::V_object avatar;
            if(r.second.as_string_length() > 0) {
              ::taxon::Value temp_value;
              POSEIDON_CHECK(temp_value.parse(r.second.as_string()));
              avatar = temp_value.as_object();
            }

            POSEIDON_LOG_DEBUG(("Found role `$1` of user `$2`"), roid, uinfo.username);
            uconn.cached_raw_avatars.try_emplace(roid, avatar);
          }

          do_publish_user_on_redis(fiber, uinfo, impl->redis_role_ttl);

          if(auto ptr = impl->connections.ptr(uinfo.username))
            if(auto old_session = ptr->weak_session.lock())
              old_session->ws_shut_down(user_ws_status_login_conflict);

          impl->users.insert_or_assign(uinfo.username, uinfo);
          impl->connections.insert_or_assign(uinfo.username, uconn);
          POSEIDON_LOG_INFO(("`$1` connected from `$2`"), uinfo.username, session->remote_address());

          do_welcome_client(impl, fiber, uinfo.username, session);
          break;
        }

      case ::poseidon::easy_hws_text:
      case ::poseidon::easy_hws_binary:
        {
          const phcow_string username = do_get_username(impl, session);
          if(username.empty())
            return;

          ::taxon::Value temp_value;
          POSEIDON_CHECK(temp_value.parse(data.data(), data.size(), ::taxon::option_json_mode));
          ::taxon::V_object request = temp_value.as_object();
          temp_value.clear();

          phcow_string opcode;
          if(auto ptr = request.ptr(&"%opcode"))
            opcode = ptr->as_string();

          ::taxon::Value serial;
          if(auto ptr = request.ptr(&"%serial"))
            serial = *ptr;

          // Check message rate.
          double rate_limit = impl->client_rate_limit;
          auto rate_duration = steady_clock::now() - impl->connections.at(username).rate_time;
          if(rate_duration >= 1s)
            rate_limit *= duration_cast<duration<double>>(rate_duration).count();

          if(++ impl->connections.mut(username).rate_counter > rate_limit) {
            session->ws_shut_down(user_ws_status_message_rate_limit);
            return;
          }

          // Copy the handler, in case of fiber context switches.
          User_Service::ws_handler_type handler;
          impl->ws_handlers.find_and_copy(handler, opcode);
          if(!handler) {
            POSEIDON_LOG_WARN(("Unknown opcode `$1` from user `$2`"), opcode, username);
            session->ws_shut_down(user_ws_status_unknown_opcode);
            return;
          }

          // Call the user-defined handler to get response data.
          ::taxon::V_object response;
          try {
            handler(fiber, username, response, request);
            impl->connections.mut(username).pong_time = steady_clock::now();
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Unhandled exception in `$1 $2`: $3"), opcode, request, stdex);
            session->ws_shut_down(::poseidon::ws_status_unexpected_error);
            return;
          }

          if(serial.is_null())
            break;

          // The client expects a response, so send it.
          response.try_emplace(&"%serial", serial);
          temp_value = response;

          auto str = temp_value.to_string(::taxon::option_json_mode);
          session->ws_send(::poseidon::ws_TEXT, str);
          break;
        }

      case ::poseidon::easy_hws_pong:
        {
          const phcow_string username = do_get_username(impl, session);
          if(username.empty())
            return;

          impl->connections.mut(username).pong_time = steady_clock::now();
          POSEIDON_LOG_TRACE(("PONG: username `$1`"), username);
          break;
        }

      case ::poseidon::easy_hws_close:
        {
          const phcow_string username = do_get_username(impl, session);
          if(username.empty())
            return;

          User_Connection uconn;
          impl->connections.find_and_erase(uconn, username);

          if(uconn.current_roid != 0) {
            // Notify the logic server that the client has disconnected. If the
            // role has been offline for too long, they will be logged out.
            ::taxon::V_object tx_args;
            tx_args.try_emplace(&"roid", uconn.current_roid);

            auto srv_q = new_sh<Service_Future>(uconn.current_logic_srv, &"*role/disconnect", tx_args);
            service.launch(srv_q);
            fiber.yield(srv_q);
          }

          // Update logout time.
          static constexpr char update_user_logout_time[] =
              R"!!!(
                UPDATE `user`
                  SET `logout_time` = ?
                  WHERE `username` = ?
              )!!!";

          cow_vector<::poseidon::MySQL_Value> sql_args;
          sql_args.emplace_back(system_clock::now());
          sql_args.emplace_back(username.rdstr());

          auto task2 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                      ::poseidon::mysql_connector.allocate_tertiary_connection(),
                                      &update_user_logout_time, sql_args);
          ::poseidon::task_scheduler.launch(task2);
          fiber.yield(task2);

          POSEIDON_LOG_INFO(("`$1` disconnected from `$2`"), username, session->remote_address());
          break;
        }

      case ::poseidon::easy_hws_get:
      case ::poseidon::easy_hws_head:
        {
          ::poseidon::Network_Reference uri;
          POSEIDON_CHECK(::poseidon::parse_network_reference(uri, data) == data.size());
          phcow_string path = ::poseidon::decode_and_canonicalize_uri_path(uri.path);

          // Copy the handler, in case of fiber context switches.
          User_Service::http_handler_type handler;
          impl->http_handlers.find_and_copy(handler, path);
          if(!handler) {
            session->http_shut_down(::poseidon::http_status_not_found);
            return;
          }

          // Call the user-defined handler to get response data.
          cow_string response_content_type;
          cow_string response_payload;
          try {
            handler(fiber, response_content_type, response_payload, cow_string(uri.query));
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Unhandled exception in `$1 $2`: $3"), path, uri.query, stdex);
            session->http_shut_down(::poseidon::http_status_bad_request);
            return;
          }

          // Ensure there's `Content-Type`.
          if(response_content_type.empty() && !response_payload.empty())
            response_content_type = &"application/octet-stream";

          // Make an HTTP response.
          ::poseidon::HTTP_S_Headers resp;
          resp.status = ::poseidon::http_status_ok;
          resp.headers.emplace_back(&"Cache-Control", &"no-cache");
          resp.headers.emplace_back(&"Content-Type", response_content_type);
          session->http_response(event == ::poseidon::easy_hws_head, move(resp), response_payload);
          break;
        }
      }
  }

void
do_mysql_check_table_user(::poseidon::Abstract_Fiber& fiber)
  {
    ::poseidon::MySQL_Table_Structure table;
    table.name = &"user";
    table.engine = ::poseidon::mysql_engine_innodb;

    ::poseidon::MySQL_Table_Column column;
    column.name = &"username";
    column.type = ::poseidon::mysql_column_varchar;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"creation_time";
    column.type = ::poseidon::mysql_column_datetime;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"login_address";
    column.type = ::poseidon::mysql_column_varchar;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"login_time";
    column.type = ::poseidon::mysql_column_datetime;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"logout_time";
    column.type = ::poseidon::mysql_column_datetime;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"banned_until";
    column.type = ::poseidon::mysql_column_datetime;
    table.columns.emplace_back(column);

    ::poseidon::MySQL_Table_Index index;
    index.name = &"PRIMARY";
    index.type = ::poseidon::mysql_index_unique;
    index.columns.emplace_back(&"username");
    table.indexes.emplace_back(index);

    // This is in the user database.
    auto task = new_sh<::poseidon::MySQL_Check_Table_Future>(::poseidon::mysql_connector,
                              ::poseidon::mysql_connector.allocate_tertiary_connection(), table);
    ::poseidon::task_scheduler.launch(task);
    fiber.yield(task);
    POSEIDON_LOG_INFO(("Finished verification of MySQL table `$1`"), table.name);
  }

void
do_mysql_check_table_nickname(::poseidon::Abstract_Fiber& fiber)
  {
    ::poseidon::MySQL_Table_Structure table;
    table.name = &"nickname";
    table.engine = ::poseidon::mysql_engine_innodb;

    ::poseidon::MySQL_Table_Column column;
    column.name = &"nickname";
    column.type = ::poseidon::mysql_column_varchar;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"serial";
    column.type = ::poseidon::mysql_column_auto_increment;
    column.default_value = 15743;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"username";
    column.type = ::poseidon::mysql_column_varchar;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"creation_time";
    column.type = ::poseidon::mysql_column_datetime;
    table.columns.emplace_back(column);

    ::poseidon::MySQL_Table_Index index;
    index.name = &"PRIMARY";
    index.type = ::poseidon::mysql_index_unique;
    index.columns.emplace_back(&"nickname");
    table.indexes.emplace_back(index);

    index.clear();
    index.name = &"serial";
    index.type = ::poseidon::mysql_index_unique;
    index.columns.emplace_back(&"serial");
    table.indexes.emplace_back(index);

    // This is in the user database.
    auto task = new_sh<::poseidon::MySQL_Check_Table_Future>(::poseidon::mysql_connector,
                              ::poseidon::mysql_connector.allocate_tertiary_connection(), table);
    ::poseidon::task_scheduler.launch(task);
    fiber.yield(task);
    POSEIDON_LOG_INFO(("Finished verification of MySQL table `$1`"), table.name);
  }

void
do_star_nickname_acquire(const shptr<Implementation>& /*impl*/, ::poseidon::Abstract_Fiber& fiber,
                         const ::poseidon::UUID& /*request_service_uuid*/,
                         ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    cow_string nickname = request.at(&"nickname").as_string();
    POSEIDON_CHECK(nickname != "");

    phcow_string username = request.at(&"username").as_string();
    POSEIDON_CHECK(username != "");

    ////////////////////////////////////////////////////////////
    //
    static constexpr char insert_into_nickname[] =
        R"!!!(
          INSERT IGNORE INTO `nickname`
            SET `nickname` = ?
                , `username` = ?
                , `creation_time` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(nickname);               // SET `nickname` = ?
    sql_args.emplace_back(username.rdstr());       //     , `username` = ?
    sql_args.emplace_back(system_clock::now());    //     , `creation_time` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                               ::poseidon::mysql_connector.allocate_tertiary_connection(),
                               &insert_into_nickname, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);

    int64_t serial = -1;
    if(task1->match_count() != 0)
      serial = static_cast<int64_t>(task1->insert_id());
    else {
      // In this case, we still want to return a serial if the nickname belongs
      // to the same user. This makes the operation retryable.
      static constexpr char select_from_nickname[] =
          R"!!!(
            SELECT `serial`
              FROM `nickname`
              WHERE `nickname` = ?
                    AND `username` = ?
          )!!!";

      sql_args.clear();
      sql_args.emplace_back(nickname);          // WHERE `nickname` = ?
      sql_args.emplace_back(username.rdstr());  //       AND `username` = ?

      task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                               ::poseidon::mysql_connector.allocate_tertiary_connection(),
                               &select_from_nickname, sql_args);
      ::poseidon::task_scheduler.launch(task1);
      fiber.yield(task1);

      if(task1->result_row_count() == 0) {
        response.try_emplace(&"status", &"gs_nickname_conflict");
        return;
      }

      serial = task1->result_row(0).at(0).as_integer();  // SELECT `serial`
    }

    POSEIDON_LOG_INFO(("Acquired nickname `$1`"), nickname);

    response.try_emplace(&"serial", serial);
    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_nickname_release(const shptr<Implementation>& /*impl*/, ::poseidon::Abstract_Fiber& fiber,
                         const ::poseidon::UUID& /*request_service_uuid*/,
                         ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    cow_string nickname = request.at(&"nickname").as_string();
    POSEIDON_CHECK(nickname != "");

    ////////////////////////////////////////////////////////////
    //
    static constexpr char delete_from_nickname[] =
        R"!!!(
          DELETE FROM `nickname`
            WHERE `nickname` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(nickname);               // WHERE `nickname` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                               ::poseidon::mysql_connector.allocate_tertiary_connection(),
                               &delete_from_nickname, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);

    if(task1->match_count() == 0) {
      response.try_emplace(&"status", &"gs_nickname_not_found");
      return;
    }

    POSEIDON_LOG_INFO(("Released nickname `$1`"), nickname);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_ping_timer_callback(const shptr<Implementation>& impl,
                       const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                       ::poseidon::Abstract_Fiber& fiber, steady_time now)
  {
    if(impl->db_ready == false) {
      // Check tables.
      do_mysql_check_table_user(fiber);
      do_mysql_check_table_nickname(fiber);
      impl->db_ready = true;
    }

    // Ping clients, and mark connections that have been inactive for a couple
    // of intervals.
    for(auto it = impl->connections.mut_begin();  it != impl->connections.end();  ++it) {
      auto session = it->second.weak_session.lock();
      if(!session) {
        impl->expired_username_list.emplace_back(it->first);
        continue;
      }

      if(now - it->second.pong_time > impl->client_ping_interval * 3) {
        POSEIDON_LOG_DEBUG(("PING timed out: username `$1`"), it->first);
        session->ws_shut_down(user_ws_status_ping_timeout);
        impl->expired_username_list.emplace_back(it->first);
        continue;
      }

      it->second.rate_time = now;
      it->second.rate_counter = 0;

      if(now - it->second.pong_time > impl->client_ping_interval)
        session->ws_send(::poseidon::ws_PING, "");
    }

    while(impl->expired_username_list.size() != 0) {
      phcow_string username = move(impl->expired_username_list.back());
      impl->expired_username_list.pop_back();

      POSEIDON_LOG_DEBUG(("Unloading user information: $1"), username);
      impl->users.erase(username);
      impl->connections.erase(username);
    }
  }

void
do_check_user_timer_callback(const shptr<Implementation>& impl,
                             const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                             ::poseidon::Abstract_Fiber& fiber, steady_time /*now*/)
  {
    ::std::vector<phcow_string> username_list;
    username_list.reserve(impl->users.size());
    for(const auto& r : impl->users)
      username_list.emplace_back(r.first);

    while(!username_list.empty()) {
      auto username = move(username_list.back());
      username_list.pop_back();

      User_Record uinfo;
      if(!impl->users.find_and_copy(uinfo, username))
        continue;

      do_publish_user_on_redis(fiber, uinfo, impl->redis_role_ttl);
    }
  }

void
do_star_user_kick(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& /*fiber*/,
                  const ::poseidon::UUID& /*request_service_uuid*/,
                  ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    phcow_string username = request.at(&"username").as_string();
    POSEIDON_CHECK(username != "");

    int ws_status = 1008;
    if(auto ptr = request.ptr(&"ws_status"))
      ws_status = clamp_cast<int>(ptr->as_integer(), 1000, 4999);

    cow_string reason;
    if(auto ptr = request.ptr(&"reason"))
      reason = ptr->as_string();

    ////////////////////////////////////////////////////////////
    //
    shptr<::poseidon::WS_Server_Session> session;
    if(auto uconn = impl->connections.ptr(username))
      session = uconn->weak_session.lock();

    if(session == nullptr) {
      response.try_emplace(&"status", &"gs_user_not_online");
      return;
    }

    session->ws_shut_down(ws_status, reason);

    POSEIDON_LOG_INFO(("Kicked user `$1`: $2 $3"), username, ws_status, reason);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_user_check_role(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& /*fiber*/,
                        const ::poseidon::UUID& request_service_uuid,
                        ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    phcow_string username = request.at(&"username").as_string();
    POSEIDON_CHECK(username != "");

    int64_t roid = request.at(&"roid").as_integer();
    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    ////////////////////////////////////////////////////////////
    //
    User_Connection uconn;
    impl->connections.find_and_copy(uconn, username);
    if(uconn.weak_session.expired()) {
      response.try_emplace(&"status", &"gs_user_not_online");
      return;
    }

    if(uconn.current_roid != roid) {
      response.try_emplace(&"status", &"gs_roid_not_match");
      return;
    }

    if(uconn.current_logic_srv != request_service_uuid) {
      response.try_emplace(&"status", &"gs_service_uuid_not_match");
      return;
    }

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_user_push_message(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& /*fiber*/,
                          const ::poseidon::UUID& /*request_service_uuid*/,
                          ::taxon::V_object& /*response*/, const ::taxon::V_object& request)
  {
    ::std::vector<phcow_string> username_list;
    if(auto plist = request.ptr(&"username_list"))
      for(const auto& r : plist->as_array())
        username_list.emplace_back(r.as_string());

    if(auto ptr = request.ptr(&"username"))
      username_list.emplace_back(ptr->as_string());

    cow_string client_opcode = request.at(&"client_opcode").as_string();
    POSEIDON_CHECK(client_opcode != "");

    ::taxon::V_object client_data;
    if(auto ptr = request.ptr(&"client_data"))
      client_data = ptr->as_object();

    ////////////////////////////////////////////////////////////
    //
    cow_string str;
    for(const auto& username : username_list)
      if(auto uconn = impl->connections.ptr(username))
        if(auto session = uconn->weak_session.lock()) {
          if(str.size() == 0) {
            ::taxon::V_object obj = client_data;
            obj.try_emplace(&"%opcode", client_opcode);
            ::taxon::Value(obj).print_to(str, ::taxon::option_json_mode);
          }
          session->ws_send(::poseidon::ws_TEXT, str);
        }
  }

void
do_relay_deny(const shptr<Implementation>& /*impl*/, ::poseidon::Abstract_Fiber& /*fiber*/,
              const phcow_string& /*username*/, ::taxon::V_object& response,
              const ::taxon::V_object& /*request*/)
  {
    response.try_emplace(&"status", &"sc_opcode_denied");
  }

void
do_relay_forward_to_logic(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                          const phcow_string& username, ::taxon::V_object& response,
                          const ::taxon::V_object& request)
  {
    ::poseidon::UUID logic_service_uuid = impl->connections.at(username).current_logic_srv;
    if(logic_service_uuid.is_nil()) {
      response.try_emplace(&"status", &"sc_no_role_selected");
      return;
    }

    ::taxon::V_object tx_args;
    tx_args.try_emplace(&"roid", impl->connections.at(username).current_roid);
    tx_args.try_emplace(&"client_opcode", request.at(&"%opcode").as_string());
    tx_args.try_emplace(&"client_req", request);

    auto srv_q = new_sh<Service_Future>(logic_service_uuid, &"*role/on_client_request", tx_args);
    service.launch(srv_q);
    fiber.yield(srv_q);

    if(srv_q->response(0).error != "")
      POSEIDON_THROW(("Could not forward client request: $1"), srv_q->response(0).error);

    if(auto ptr = srv_q->response(0).obj.ptr(&"client_resp"))
      response = ptr->as_object();
  }

void
do_reload_relay_conf(const shptr<Implementation>& impl)
  {
    cow_dictionary<User_Service::ws_handler_type> temp_ws_handlers;
    ::poseidon::Config_File conf_file(&"relay.conf");

    for(const auto& r : conf_file.root()) {
      if(r.second.is_null())
        continue;
      else if(!r.second.is_string())
        POSEIDON_THROW((
            "Invalid `$1`: expecting a `string`, got `$2`",
            "[in configuration file '$3']"),
            r.first, r.second, conf_file.path());

      if(r.first.empty() || r.second.as_string().empty())
        continue;

      User_Service::ws_handler_type handler;
      if(r.second.as_string() == "denied")
        handler = bindw(impl, do_relay_deny);
      else if(r.second.as_string() == "logic")
        handler = bindw(impl, do_relay_forward_to_logic);
      else
        POSEIDON_THROW((
            "Invalid `$1`: unknown relay rule `$2`",
            "[in configuration file '$3']"),
            r.first, r.second, conf_file.path());

      if(temp_ws_handlers.try_emplace(r.first, handler).second == false)
        POSEIDON_THROW((
            "Duplicate rule for `$1`",
            "[in configuration file '$3']"),
            r.first, r.second, conf_file.path());
    }

    for(const auto& r : temp_ws_handlers)
      impl->ws_handlers.insert_or_assign(r.first, r.second);

    POSEIDON_LOG_INFO(("Reloaded relay rules for client opcodes from '$1'"), conf_file.path());
  }

void
do_star_user_reload_relay_conf(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& /*fiber*/,
                               const ::poseidon::UUID& /*request_service_uuid*/,
                               ::taxon::V_object& response, const ::taxon::V_object& /*request*/)
  {
    do_reload_relay_conf(impl);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_user_ban_set(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                     const ::poseidon::UUID& /*request_service_uuid*/,
                     ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    phcow_string username = request.at(&"username").as_string();
    POSEIDON_CHECK(username != "");

    system_time until = request.at(&"until").as_time();
    POSEIDON_CHECK(until.time_since_epoch() >= 946684800s);  // 2000-1-1

    ////////////////////////////////////////////////////////////
    //
    POSEIDON_CHECK(impl->db_ready);

    static constexpr char update_user_banned_until[] =
        R"!!!(
          UPDATE `user`
            SET `banned_until` = ?
            WHERE `username` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(until);              // SET `banned_until` = ?
    sql_args.emplace_back(username.rdstr());   // WHERE `username` = ?

    auto task2 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                ::poseidon::mysql_connector.allocate_tertiary_connection(),
                                &update_user_banned_until, sql_args);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);

    if(task2->match_count() == 0) {
      response.try_emplace(&"status", &"gs_user_not_found");
      return;
    }

    // Also update data in memory.
    if(auto uinfo = impl->users.mut_ptr(username))
      uinfo->banned_until = until;

    if(auto uconn = impl->connections.ptr(username))
      if(auto session = uconn->weak_session.lock())
        session->ws_shut_down(user_ws_status_ban);

    POSEIDON_LOG_INFO(("Set ban on `$1` until `$2`"), username, until);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_user_ban_lift(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                      const ::poseidon::UUID& /*request_service_uuid*/,
                      ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    phcow_string username = request.at(&"username").as_string();
    POSEIDON_CHECK(username != "");

    ////////////////////////////////////////////////////////////
    //
    POSEIDON_CHECK(impl->db_ready);

    static constexpr char update_user_banned_until[] =
        R"!!!(
          UPDATE `user`
            SET `banned_until` = '1999-01-01'
            WHERE `username` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(username.rdstr());   // WHERE `username` = ?

    auto task2 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                ::poseidon::mysql_connector.allocate_tertiary_connection(),
                                &update_user_banned_until, sql_args);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);

    if(task2->match_count() == 0) {
      response.try_emplace(&"status", &"gs_user_not_found");
      return;
    }

    // Also update data in memory.
    if(auto uinfo = impl->users.mut_ptr(username))
      uinfo->banned_until = system_time();

    POSEIDON_LOG_INFO(("Lift ban on `$1`"), username);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_plus_role_create(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                    const phcow_string& username, ::taxon::V_object& response,
                    const ::taxon::V_object& request)
  {
    cow_string nickname = request.at(&"nickname").as_string();
    POSEIDON_CHECK(nickname != "");

    ////////////////////////////////////////////////////////////
    //
    if(impl->connections.at(username).cached_raw_avatars.size() >= impl->max_number_of_roles_per_user) {
      response.try_emplace(&"status", &"sc_too_many_roles");
      return;
    }

    int nickname_length = 0;
    size_t offset = 0;
    while(offset < nickname.size()) {
      char32_t cp;
      if(!::asteria::utf8_decode(cp, nickname, offset)) {
        response.try_emplace(&"status", &"sc_nickname_invalid");
        return;
      }

      int w = ::wcwidth(static_cast<wchar_t>(cp));
      if(w <= 0) {
        response.try_emplace(&"status", &"sc_nickname_invalid");
        return;
      }

      nickname_length += w;
      if(nickname_length > impl->nickname_length_limits[1]) {
        response.try_emplace(&"status", &"sc_nickname_length_error");
        return;
      }
    }

    if(nickname_length < impl->nickname_length_limits[0]) {
      response.try_emplace(&"status", &"sc_nickname_length_error");
      return;
    }

    // Allocate a role ID.
    ::taxon::V_object tx_args;
    tx_args.try_emplace(&"nickname", nickname);
    tx_args.try_emplace(&"username", username.rdstr());

    auto srv_q = new_sh<Service_Future>(service.service_uuid(), &"*nickname/acquire", tx_args);
    service.launch(srv_q);
    fiber.yield(srv_q);

    auto status = srv_q->response(0).obj.at(&"status").as_string();
    if(status != "gs_ok") {
      POSEIDON_LOG_DEBUG(("Could not acquire nickname `$1`: $2"), nickname, status);
      response.try_emplace(&"status", &"sc_nickname_conflict");
      return;
    }

    int64_t roid = srv_q->response(0).obj.at(&"serial").as_integer();
    POSEIDON_LOG_DEBUG(("User `$1` created role `$2`(`$3`)"), username, roid, nickname);

    // Create the role in database.
    tx_args.clear();
    tx_args.try_emplace(&"roid", roid);
    tx_args.try_emplace(&"nickname", nickname);
    tx_args.try_emplace(&"username", username.rdstr());

    srv_q = new_sh<Service_Future>(do_find_my_monitor(), &"*role/create", tx_args);
    service.launch(srv_q);
    fiber.yield(srv_q);

    status = srv_q->response(0).obj.at(&"status").as_string();
    if(status != "gs_ok") {
      POSEIDON_LOG_WARN(("Could not create role `$1` (`$2`): $3"), roid, nickname, status);
      response.try_emplace(&"status", &"sc_nickname_conflict");
      return;
    }

    impl->connections.mut(username).cached_raw_avatars.try_emplace(roid);

    do_role_login_common(impl, fiber, username, roid);

    response.try_emplace(&"status", &"sc_ok");
  }

void
do_plus_role_login(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                   const phcow_string& username, ::taxon::V_object& response,
                   const ::taxon::V_object& request)
  {
    int64_t roid = clamp_cast<int64_t>(request.at(&"roid").as_number(), -1, INT64_MAX);
    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    ////////////////////////////////////////////////////////////
    //
    if(impl->connections.at(username).current_roid == roid) {
      response.try_emplace(&"status", &"sc_role_selected");
      return;
    }

    if(impl->connections.at(username).cached_raw_avatars.count(roid) == 0) {
      response.try_emplace(&"status", &"sc_role_unavailable");
      return;
    }

    // Load this role into Redis.
    ::taxon::V_object tx_args;
    tx_args.try_emplace(&"roid", roid);

    auto srv_q = new_sh<Service_Future>(do_find_my_monitor(), &"*role/load", tx_args);
    service.launch(srv_q);
    fiber.yield(srv_q);

    do_role_login_common(impl, fiber, username, roid);

    response.try_emplace(&"status", &"sc_ok");
  }

void
do_plus_role_logout(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                    const phcow_string& username, ::taxon::V_object& response,
                    const ::taxon::V_object& /*request*/)
  {
    ////////////////////////////////////////////////////////////
    //
    if(impl->connections.at(username).current_roid == 0) {
      response.try_emplace(&"status", &"sc_no_role_selected");
      return;
    }

    do_role_logout_common(impl, fiber, username);

    response.try_emplace(&"status", &"sc_ok");
  }

}  // namespace

POSEIDON_HIDDEN_X_STRUCT(User_Service,
    Implementation);

User_Service::
User_Service()
  {
  }

User_Service::
~User_Service()
  {
  }

void
User_Service::
add_http_handler(const phcow_string& path, const http_handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    if(this->m_impl->http_handlers.try_emplace(path, handler).second == false)
      POSEIDON_THROW(("Handler for `$1` already exists"), path);
  }

bool
User_Service::
set_http_handler(const phcow_string& path, const http_handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    return this->m_impl->http_handlers.insert_or_assign(path, handler).second;
  }

bool
User_Service::
remove_http_handler(const phcow_string& path)
  noexcept
  {
    if(!this->m_impl)
      return false;

    return this->m_impl->http_handlers.erase(path);
  }

void
User_Service::
add_ws_authenticator(const phcow_string& path, const ws_authenticator_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    if(this->m_impl->ws_authenticators.try_emplace(path, handler).second == false)
      POSEIDON_THROW(("Handler for `$1` already exists"), path);
  }

bool
User_Service::
set_ws_authenticator(const phcow_string& path, const ws_authenticator_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    return this->m_impl->ws_authenticators.try_emplace(path, handler).second;
  }

bool
User_Service::
remove_ws_authenticator(const phcow_string& path)
  noexcept
  {
    if(!this->m_impl)
      return false;

    return this->m_impl->ws_authenticators.erase(path);
  }

void
User_Service::
add_ws_handler(const phcow_string& opcode, const ws_handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    if(this->m_impl->ws_handlers.try_emplace(opcode, handler).second == false)
      POSEIDON_THROW(("Handler for `$1` already exists"), opcode);
  }

bool
User_Service::
set_ws_handler(const phcow_string& opcode, const ws_handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    return this->m_impl->ws_handlers.insert_or_assign(opcode, handler).second;
  }

bool
User_Service::
remove_ws_handler(const phcow_string& opcode)
  noexcept
  {
    if(!this->m_impl)
      return false;

    return this->m_impl->ws_handlers.erase(opcode);
  }

const cow_dictionary<User_Record>&
User_Service::
all_user_records()
  const noexcept
  {
    if(!this->m_impl)
      return empty_user_map;

    return this->m_impl->users;
  }

const User_Record&
User_Service::
find_user_record_opt(const phcow_string& username)
  const noexcept
  {
    if(!this->m_impl)
      return User_Record::null;

    auto ptr = this->m_impl->users.ptr(username);
    if(!ptr)
      return User_Record::null;

    return *ptr;
  }

void
User_Service::
reload(const ::poseidon::Config_File& conf_file)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // `redis_role_ttl`
    seconds redis_role_ttl = seconds(static_cast<int>(conf_file.get_integer_opt(
                                    &"redis_role_ttl", 600, 999999999).value_or(900)));

    // `agent.client_port_list[]`
    uint16_t client_port = static_cast<uint16_t>(conf_file.get_integer(
                        sformat("agent.client_port_list[$1]", service.service_index()), 1, 32767));

    // `agent.client_rate_limit`
    uint16_t client_rate_limit = static_cast<uint16_t>(conf_file.get_integer_opt(
                                    &"agent.client_rate_limit", 1, 65535).value_or(10));

    // `agent.client_ping_interval`
    seconds client_ping_interval = seconds(static_cast<int>(conf_file.get_integer_opt(
                                    &"agent.client_ping_interval", 1, 3600).value_or(30)));

    // `agent.max_number_of_roles_per_user`
    uint16_t max_number_of_roles_per_user = static_cast<uint16_t>(conf_file.get_integer_opt(
                                    &"agent.max_number_of_roles_per_user", 1, 65535).value_or(4));

    // `agent.nickname_length_limits[]`
    uint8_t nickname_length_limits_0 = static_cast<uint8_t>(conf_file.get_integer_opt(
                                    &"agent.nickname_length_limits[0]", 1, 255).value_or(1));
    uint8_t nickname_length_limits_1 = static_cast<uint8_t>(conf_file.get_integer_opt(
                                    &"agent.nickname_length_limits[1]", 1, 255).value_or(24));

    if(nickname_length_limits_0 > nickname_length_limits_1)
      POSEIDON_THROW((
          "Invalid `agent.nickname_length_limits`: invalid range: `$1` > `$3`",
          "[in configuration file '$2']"),
          nickname_length_limits_0, conf_file.path(), nickname_length_limits_1);

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->redis_role_ttl = redis_role_ttl;
    this->m_impl->client_port = client_port;
    this->m_impl->client_rate_limit = client_rate_limit;
    this->m_impl->client_ping_interval = client_ping_interval;
    this->m_impl->max_number_of_roles_per_user = max_number_of_roles_per_user;
    this->m_impl->nickname_length_limits[0] = nickname_length_limits_0;
    this->m_impl->nickname_length_limits[1] = nickname_length_limits_1;

    // Set up builtin handlers.
    this->m_impl->ws_handlers.insert_or_assign(&"+role/create", bindw(this->m_impl, do_plus_role_create));
    this->m_impl->ws_handlers.insert_or_assign(&"+role/login", bindw(this->m_impl, do_plus_role_login));
    this->m_impl->ws_handlers.insert_or_assign(&"+role/logout", bindw(this->m_impl, do_plus_role_logout));

    // Allow builtin handlers to be overridden; well, they may be.
    do_reload_relay_conf(this->m_impl);

    // Set up request handlers.
    service.set_handler(&"*user/kick", bindw(this->m_impl, do_star_user_kick));
    service.set_handler(&"*user/check_role", bindw(this->m_impl, do_star_user_check_role));
    service.set_handler(&"*user/push_message", bindw(this->m_impl, do_star_user_push_message));
    service.set_handler(&"*user/reload_relay_conf", bindw(this->m_impl, do_star_user_reload_relay_conf));
    service.set_handler(&"*user/ban/set", bindw(this->m_impl, do_star_user_ban_set));
    service.set_handler(&"*user/ban/lift", bindw(this->m_impl, do_star_user_ban_lift));
    service.set_handler(&"*nickname/acquire", bindw(this->m_impl, do_star_nickname_acquire));
    service.set_handler(&"*nickname/release", bindw(this->m_impl, do_star_nickname_release));

    // Restart the service.
    this->m_impl->ping_timer.start(150ms, 7001ms, bindw(this->m_impl, do_ping_timer_callback));
    this->m_impl->check_user_timer.start(2min, bindw(this->m_impl, do_check_user_timer_callback));
    this->m_impl->user_server.start(this->m_impl->client_port, bindw(this->m_impl, do_server_hws_callback));
  }

}  // namespace k32::agent
