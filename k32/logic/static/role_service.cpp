// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_3543B0B1_DC5A_4F34_B9BB_CAE513821771_
#include "role_service.hpp"
#include "../globals.hpp"
#include "../../common/data/role_record.hpp"
#include <poseidon/base/config_file.hpp>
#include <poseidon/base/datetime.hpp>
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/fiber/redis_query_future.hpp>
#include <poseidon/static/task_scheduler.hpp>
#include <list>
namespace k32::logic {
namespace {

struct Hydrated_Role
  {
    Role_Record roinfo;
    shptr<Role> role;
  };

struct Implementation
  {
    seconds redis_role_ttl;
    seconds disconnect_to_logout_duration;

    cow_dictionary<Role_Service::handler_type> handlers;

    ::poseidon::Easy_Timer save_timer;
    ::poseidon::Easy_Timer every_second_timer;

    // online roles
    cow_int64_dictionary<Hydrated_Role> hyd_roles;
    ::std::list<static_vector<int64_t, 255>> save_buckets;
  };

void
do_set_role_record_common_fields(::taxon::V_object& temp_obj, const shptr<Role>& role)
  {
    temp_obj.insert_or_assign(&"roid", role->roid());
    temp_obj.insert_or_assign(&"username", role->username().rdstr());
    temp_obj.insert_or_assign(&"nickname", role->nickname());
  }

void
do_store_role_into_redis(::poseidon::Abstract_Fiber& fiber, Hydrated_Role& hyd, seconds ttl)
  {
    POSEIDON_LOG_DEBUG(("Storing role `$1`: preparing data"), hyd.roinfo.roid);

    ROCKET_ASSERT(hyd.roinfo.roid == hyd.role->roid());
    hyd.roinfo.username = hyd.role->username();
    hyd.roinfo.nickname = hyd.role->nickname();
    hyd.roinfo.update_time = system_clock::now();

    ::taxon::V_object temp_obj;
    hyd.role->make_avatar(temp_obj);
    do_set_role_record_common_fields(temp_obj, hyd.role);
    hyd.roinfo.avatar = ::taxon::Value(temp_obj).to_string();

    temp_obj.clear();
    hyd.role->make_profile(temp_obj);
    do_set_role_record_common_fields(temp_obj, hyd.role);
    hyd.roinfo.profile = ::taxon::Value(temp_obj).to_string();

    temp_obj.clear();
    hyd.role->make_db_record(temp_obj);
    do_set_role_record_common_fields(temp_obj, hyd.role);
    hyd.roinfo.whole = ::taxon::Value(temp_obj).to_string();

    POSEIDON_LOG_INFO(("#sav# Saving into Redis: role `$1` (`$2`), updated on `$3`"),
                      hyd.roinfo.roid, hyd.roinfo.nickname, hyd.roinfo.update_time);

    cow_vector<cow_string> redis_cmd;
    redis_cmd.emplace_back(&"SET");
    redis_cmd.emplace_back(sformat("$1/role/$2", service.application_name(), hyd.roinfo.roid));
    redis_cmd.emplace_back(hyd.roinfo.serialize_to_string());
    redis_cmd.emplace_back(&"EX");
    redis_cmd.emplace_back(sformat("$1", ttl.count()));

    auto task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);

    POSEIDON_LOG_INFO(("#sav# Saved into Redis: role `$1` (`$2`), updated on `$3`"),
                      hyd.roinfo.roid, hyd.roinfo.nickname, hyd.roinfo.update_time);
  }

void
do_flush_role_to_mysql(::poseidon::Abstract_Fiber& fiber, Hydrated_Role& hyd)
  {
    ::poseidon::UUID monitor_service_uuid;
    for(const auto& r : service.all_service_records())
      if((r.second.zone_id == hyd.roinfo._home_zone) && (r.second.service_type == "monitor")) {
        monitor_service_uuid = r.first;
        if(r.first == hyd.role->mf_monitor_srv())
          break;
      }

    if(monitor_service_uuid != hyd.role->mf_monitor_srv()) {
      // Switch to new monitor.
      POSEIDON_LOG_ERROR(("Monitor `$1` seems to be down"), hyd.role->mf_monitor_srv());
      hyd.role->mf_monitor_srv() = monitor_service_uuid;
    }

    ::taxon::V_object tx_args;
    tx_args.try_emplace(&"roid", hyd.roinfo.roid);

    auto srv_q = new_sh<Service_Future>(monitor_service_uuid, &"*role/flush", tx_args);
    service.launch(srv_q);
    fiber.yield(srv_q);

    POSEIDON_LOG_INFO(("#sav# Flushed to MySQL: role `$1` (`$2`), updated on `$3`"),
                      hyd.roinfo.roid, hyd.roinfo.nickname, hyd.roinfo.update_time);
  }

void
do_save_timer_callback(const shptr<Implementation>& impl,
                       const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                       ::poseidon::Abstract_Fiber& fiber, steady_time now)
  {
    if(!impl->hyd_roles.empty()) {
      // Check that every user is still on the same agent with the same role.
      // Requests are made in parallel.
      struct Agent_Request
        {
          cow_dictionary<int64_t> roids_by_username;
          ::taxon::V_array username_list;
          shptr<Service_Future> srv_q;
        };

      cow_uuid_dictionary<Agent_Request> agent_requests;
      for(const auto& r : impl->hyd_roles)
        if(r.second.role->agent_service_uuid() != ::poseidon::UUID()) {
          auto& ar = agent_requests.open(r.second.role->agent_service_uuid());
          ar.roids_by_username.try_emplace(r.second.roinfo.username, r.second.roinfo.roid);
          ar.username_list.emplace_back(r.second.roinfo.username.rdstr());
        }

      for(auto it = agent_requests.mut_begin();  it != agent_requests.end();  ++it) {
        ::taxon::V_object tx_args;
        tx_args.try_emplace(&"username_list", it->second.username_list);

        it->second.srv_q = new_sh<Service_Future>(it->first, &"*user/check_roles", tx_args);
        service.launch(it->second.srv_q);
      }

      for(auto it = agent_requests.mut_begin();  it != agent_requests.end();  ++it) {
        fiber.yield(it->second.srv_q);

        bool agent_down = true;
        for(const auto& resp : it->second.srv_q->responses()) {
          auto ptr = resp.obj.ptr(&"roles");
          if(ptr && ptr->is_object()) {
            // If a user is on their original agent with the same role, then
            // they are not expired.
            agent_down = false;
            for(const auto& rr : ptr->as_object())
              if(rr.second.is_object()
                  && (rr.second.as_object().at(&"roid").as_integer()
                      == it->second.roids_by_username.at(rr.first))
                  && (::poseidon::UUID(rr.second.as_object().at(&"logic_srv").as_string())
                      == service.service_uuid()))
                it->second.roids_by_username.erase(rr.first);
          }
        }

        for(const auto& rr : it->second.roids_by_username) {
          Hydrated_Role hyd;
          if(!impl->hyd_roles.find_and_copy(hyd, rr.second))
            continue;

          if(hyd.role->mf_agent_srv() != it->first)
            continue;

          hyd.role->mf_agent_srv() = ::poseidon::UUID();
          hyd.role->mf_dc_since() = now;
          hyd.role->on_disconnect();

          if(agent_down) {
            // It's likely that the application is being shut down, so flush all
            // roles immediately.
            do_store_role_into_redis(fiber, hyd, impl->redis_role_ttl);
            impl->hyd_roles.find_and_assign(rr.second, hyd);
            do_flush_role_to_mysql(fiber, hyd);
          }
        }
      }
    }

    if(impl->save_buckets.empty()) {
      // Arrange online roles for writing. Initially, users are divided into 20
      // buckets. For each timer tick, one bucket will be popped and written.
      while(impl->save_buckets.size() < 20)
        impl->save_buckets.emplace_back();

      for(const auto& r : impl->hyd_roles) {
        auto current_bucket = impl->save_buckets.begin();
        impl->save_buckets.splice(impl->save_buckets.end(), impl->save_buckets, current_bucket);
        if(current_bucket->size() >= current_bucket->capacity()) {
          ptrdiff_t sp = static_cast<ptrdiff_t>(current_bucket->capacity() / 2);
          impl->save_buckets.emplace_back(current_bucket->move_begin(), current_bucket->move_begin() + sp);
          current_bucket->erase(current_bucket->begin(), current_bucket->begin() + sp);
        }
        current_bucket->push_back(r.first);
      }
    }

    auto bucket = move(impl->save_buckets.back());
    impl->save_buckets.pop_back();
    while(!bucket.empty()) {
      int64_t roid = bucket.back();
      bucket.pop_back();

      // Serialize role data for saving. As this is an asynchronous operation,
      // `impl->hyd_roles` may change between iterations. It's crucial that we
      // limit scopes of pointers, references, and iterators.
      Hydrated_Role hyd;
      impl->hyd_roles.find_and_copy(hyd, roid);
      if(!hyd.role)
        continue;

      if(now - hyd.role->mf_dc_since() >= impl->disconnect_to_logout_duration) {
        // Role has been disconnected for too long.
        POSEIDON_LOG_DEBUG(("Logging out role `$1` due to inactivity"), hyd.roinfo.roid);
        hyd.role->on_logout();

        do_store_role_into_redis(fiber, hyd, impl->redis_role_ttl);
        impl->hyd_roles.erase(roid);
        do_flush_role_to_mysql(fiber, hyd);
      }
      else {
        do_store_role_into_redis(fiber, hyd, impl->redis_role_ttl);
        impl->hyd_roles.find_and_assign(roid, hyd);
      }
    }
  }

void
do_star_role_login(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                   const ::poseidon::UUID& /*request_service_uuid*/,
                   ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    // * Request Parameters
    //   - `roid` <sub>integer</sub> : ID of role to load.
    //   - `agent_srv` <sub>string</sub> : UUID of _agent_ that holds client connection.
    //   - `monitor_srv` <sub>string</sub> : UUID of _monitor_ that holds client data.
    //
    // * Response Parameters
    //   - `status` <sub>string</sub> : [General status code.](#general-status-codes)
    //
    // * Description
    //   Loads a role from Redis and triggers a _login_ event. If the role has not been
    //   loaded into Redis, this operation fails.

    ////////////////////////////////////////////////////////////
    //
    int64_t roid = request.at(&"roid").as_integer();
    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    ::poseidon::UUID agent_service_uuid(request.at(&"agent_srv").as_string());
    POSEIDON_CHECK(!agent_service_uuid.is_nil());

    ::poseidon::UUID monitor_service_uuid(request.at(&"monitor_srv").as_string());
    POSEIDON_CHECK(!monitor_service_uuid.is_nil());

    ////////////////////////////////////////////////////////////
    //
    Hydrated_Role hyd;
    if(!impl->hyd_roles.find_and_copy(hyd, roid)) {
      // Load role from Redis.
      cow_vector<cow_string> redis_cmd;
      redis_cmd.emplace_back(&"GETEX");
      redis_cmd.emplace_back(sformat("$1/role/$2", service.application_name(), roid));
      redis_cmd.emplace_back(&"EX");
      redis_cmd.emplace_back(sformat("$1", impl->redis_role_ttl.count()));

      auto task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
      ::poseidon::task_scheduler.launch(task2);
      fiber.yield(task2);

      if(task2->result().is_nil()) {
        response.try_emplace(&"status", &"gs_role_not_loaded");
        return;
      }

      hyd.roinfo.parse_from_string(task2->result().as_string());
      hyd.role = new_sh<Role>();

      hyd.role->mf_roid() = hyd.roinfo.roid;
      hyd.role->mf_nickname() = hyd.roinfo.nickname;
      hyd.role->mf_username() = hyd.roinfo.username;

      if(hyd.roinfo.whole.size() != 0) {
        // For a new role, this value is an empty string and can't be parsed.
        ::taxon::Value temp_value;
        POSEIDON_CHECK(temp_value.parse(hyd.roinfo.whole));
        hyd.role->parse_from_db_record(temp_value.as_object());
      }

      auto result = impl->hyd_roles.try_emplace(hyd.roinfo.roid, hyd);
      if(!result.second)
        hyd = result.first->second;  // load conflict
      else
        hyd.role->on_login();
    }

    hyd.role->mf_agent_srv() = agent_service_uuid;
    hyd.role->mf_monitor_srv() = monitor_service_uuid;
    hyd.role->mf_dc_since() = steady_time::max();
    hyd.role->on_connect();

    do_store_role_into_redis(fiber, hyd, impl->redis_role_ttl);
    impl->hyd_roles.find_and_assign(roid, hyd);
    do_flush_role_to_mysql(fiber, hyd);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_role_logout(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                    const ::poseidon::UUID& /*request_service_uuid*/,
                    ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    // * Request Parameters
    //   - `roid` <sub>integer</sub> : ID of role to unload.
    //
    // * Response Parameters
    //   - `status` <sub>string</sub> : [General status code.](#general-status-codes)
    //
    // * Description
    //   Triggers a _logout_ event, writes the role back to Redis, and unloads it.

    ////////////////////////////////////////////////////////////
    //
    int64_t roid = request.at(&"roid").as_integer();
    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    ////////////////////////////////////////////////////////////
    //
    Hydrated_Role hyd;
    impl->hyd_roles.find_and_copy(hyd, roid);
    if(!hyd.role) {
      response.try_emplace(&"status", &"gs_role_not_logged_in");
      return;
    }

    hyd.role->mf_agent_srv() = ::poseidon::UUID();
    hyd.role->mf_dc_since() = steady_clock::now();
    hyd.role->on_disconnect();
    hyd.role->on_logout();

    do_store_role_into_redis(fiber, hyd, impl->redis_role_ttl);
    impl->hyd_roles.erase(roid);
    do_flush_role_to_mysql(fiber, hyd);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_role_reconnect(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& /*fiber*/,
                       const ::poseidon::UUID& /*request_service_uuid*/,
                       ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    ::std::vector<int64_t> roid_list;
    for(const auto& r : request.at(&"roid_list").as_array()) {
      POSEIDON_CHECK((r.as_integer() >= 1) && (r.as_integer() <= 8'99999'99999'99999));
      roid_list.push_back(r.as_integer());
    }

    ::poseidon::UUID agent_service_uuid(request.at(&"agent_srv").as_string());
    POSEIDON_CHECK(!agent_service_uuid.is_nil());

    ////////////////////////////////////////////////////////////
    //
    Hydrated_Role hyd;
    for(int64_t roid : roid_list)
      impl->hyd_roles.find_and_copy(hyd, roid);
    if(!hyd.role) {
      response.try_emplace(&"status", &"gs_reconnect_noop");
      return;
    }

    hyd.role->mf_agent_srv() = agent_service_uuid;
    hyd.role->mf_dc_since() = steady_time::max();
    hyd.role->on_connect();

    response.try_emplace(&"roid", hyd.roinfo.roid);
    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_role_disconnect(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& /*fiber*/,
                        const ::poseidon::UUID& /*request_service_uuid*/,
                        ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    // * Request Parameters
    //   - `roid` <sub>integer</sub> : ID of role to disconnect.
    //
    // * Response Parameters
    //   - `status` <sub>string</sub> : [General status code.](#general-status-codes)
    //
    // * Description
    //   Triggers a _disconnect_ event.

    ////////////////////////////////////////////////////////////
    //
    int64_t roid = request.at(&"roid").as_integer();
    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    ////////////////////////////////////////////////////////////
    //
    Hydrated_Role hyd;
    impl->hyd_roles.find_and_copy(hyd, roid);
    if(!hyd.role) {
      response.try_emplace(&"status", &"gs_role_not_logged_in");
      return;
    }

    hyd.role->mf_agent_srv() = ::poseidon::UUID();
    hyd.role->mf_dc_since() = steady_clock::now();
    hyd.role->on_disconnect();

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_role_on_client_request(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                               const ::poseidon::UUID& /*request_service_uuid*/,
                               ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    // * Request Parameters
    //   - `roid` <sub>integer</sub> : ID of role on this client.
    //   - `client_opcode` <sub>string</sub> : Opcode from client.
    //   - `client_req` <sub>object, optional</sub> : Additional data for this opcode.
    //
    // * Response Parameters
    //   - `status` <sub>string</sub> : [General status code.](#general-status-codes)
    //   - `client_resp` <sub>object, optional</sub> : Additional response data.
    //
    // * Description
    //   Handles a request message from a client.

    ////////////////////////////////////////////////////////////
    //
    int64_t roid = request.at(&"roid").as_integer();
    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    phcow_string client_opcode = request.at(&"client_opcode").as_string();
    POSEIDON_CHECK(client_opcode != "");

    ::taxon::V_object client_req;
    if(auto ptr = request.ptr(&"client_req"))
      client_req = ptr->as_object();

    ////////////////////////////////////////////////////////////
    //
    Role_Service::handler_type handler;
    impl->handlers.find_and_copy(handler, client_opcode);
    if(!handler) {
      response.try_emplace(&"status", &"gs_role_handler_not_found");
      return;
    }

    // Call the user-defined handler to get response data.
    ::taxon::V_object client_resp;
    try {
      handler(fiber, roid, client_resp, client_req);
    }
    catch(exception& stdex) {
      POSEIDON_LOG_ERROR(("Unhandled exception in `$1 $2`: $3"), client_opcode, client_req, stdex);
      response.try_emplace(&"status", &"gs_role_handler_except");
      return;
    }

    response.try_emplace(&"client_resp", client_resp);
    response.try_emplace(&"status", &"gs_ok");
  }

void
do_every_second_timer_callback(const shptr<Implementation>& impl,
                               const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                               ::poseidon::Abstract_Fiber& /*fiber*/, steady_time /*now*/)
  {
    ::std::vector<wkptr<Role>> weak_roles;
    weak_roles.reserve(impl->hyd_roles.size());
    for(const auto& r : impl->hyd_roles)
      weak_roles.emplace_back(r.second.role);

    while(!weak_roles.empty()) {
      auto role = weak_roles.back().lock();
      weak_roles.pop_back();

      if(!role)
        continue;

      POSEIDON_CATCH_EVERYTHING(role->on_every_second());
    }
  }

}  // namespace

POSEIDON_HIDDEN_X_STRUCT(Role_Service,
    Implementation);

Role_Service::
Role_Service()
  {
  }

Role_Service::
~Role_Service()
  {
  }

void
Role_Service::
add_handler(const phcow_string& opcode, const handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    if(this->m_impl->handlers.try_emplace(opcode, handler).second == false)
      POSEIDON_THROW(("Handler for `$1` already exists"), opcode);
  }

bool
Role_Service::
set_handler(const phcow_string& opcode, const handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    return this->m_impl->handlers.insert_or_assign(opcode, handler).second;
  }

bool
Role_Service::
remove_handler(const phcow_string& opcode)
  noexcept
  {
    if(!this->m_impl)
      return false;

    return this->m_impl->handlers.erase(opcode);
  }

shptr<Role>
Role_Service::
find_online_role_opt(int64_t roid)
  const noexcept
  {
    if(!this->m_impl)
      return nullptr;

    auto hyd = this->m_impl->hyd_roles.ptr(roid);
    if(!hyd)
      return nullptr;

    return hyd->role;
  }

void
Role_Service::
reload(const ::poseidon::Config_File& conf_file)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // `redis_role_ttl`
    seconds redis_role_ttl = seconds(static_cast<int>(conf_file.get_integer_opt(
                                    &"redis_role_ttl", 600, 999999999).value_or(900)));

    // `logic.disconnect_to_logout_duration`
    seconds disconnect_to_logout_duration = seconds(static_cast<int>(conf_file.get_integer_opt(
                                    &"logic.disconnect_to_logout_duration", 1, 999999999).value_or(60)));

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->redis_role_ttl = redis_role_ttl;
    this->m_impl->disconnect_to_logout_duration = disconnect_to_logout_duration;

    // Set up request handlers.
    service.set_handler(&"*role/login", bindw(this->m_impl, do_star_role_login));
    service.set_handler(&"*role/logout", bindw(this->m_impl, do_star_role_logout));
    service.set_handler(&"*role/reconnect", bindw(this->m_impl, do_star_role_reconnect));
    service.set_handler(&"*role/disconnect", bindw(this->m_impl, do_star_role_disconnect));
    service.set_handler(&"*role/on_client_request", bindw(this->m_impl, do_star_role_on_client_request));

    // Restart the service.
    this->m_impl->save_timer.start(3001ms, bindw(this->m_impl, do_save_timer_callback));
    this->m_impl->every_second_timer.start(1s, bindw(this->m_impl, do_every_second_timer_callback));
  }

}  // namespace k32::logic
