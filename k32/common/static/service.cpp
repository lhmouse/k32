// This file is part of
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
#include "service.hpp"
#include <poseidon/base/config_file.hpp>
#include <poseidon/base/appointment.hpp>
#include <poseidon/easy/easy_ws_server.hpp>
#include <poseidon/easy/easy_ws_client.hpp>
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/static/fiber_scheduler.hpp>
#include <poseidon/fiber/redis_query_future.hpp>
#include <poseidon/fiber/redis_scan_and_get_future.hpp>
#include <poseidon/base/abstract_task.hpp>
#include <poseidon/static/task_scheduler.hpp>
#include <poseidon/http/http_query_parser.hpp>
#define OPENSSL_API_COMPAT  0x10100000L
#include <openssl/md5.h>
#include <sys/types.h>
#include <net/if.h>
#include <ifaddrs.h>
namespace k32 {
namespace {

const cow_uuid_dictionary<Service_Record> empty_service_record_map;

struct Remote_Service_Connection_Record
  {
    wkptr<::poseidon::WS_Client_Session> weak_session;
    cow_bivector<wkptr<Service_Future>, ::poseidon::UUID> weak_futures;
  };

struct Implementation
  {
    ::poseidon::Appointment appointment;
    cow_string application_name;
    cow_string application_password;
    int zone_id = 0;
    system_time zone_start_time;
    cow_string service_type;

    ::poseidon::UUID service_uuid;
    steady_time service_start_time;
    cow_dictionary<Service::handler_type> handlers;

    ::poseidon::Easy_Timer publish_timer;
    ::poseidon::Easy_Timer subscribe_timer;
    ::poseidon::Easy_WS_Server private_server;
    ::poseidon::Easy_WS_Client private_client;

    int64_t perf_time = 0;
    int64_t perf_cpu_time = 0;

    // remote data from redis
    cow_uuid_dictionary<Service_Record> remote_services;
    cow_uuid_dictionary<Remote_Service_Connection_Record> remote_connections;
    ::std::vector<::poseidon::UUID> expired_remote_service_uuid_list;
  };

::poseidon::UUID
do_get_service_uuid(const ::poseidon::TCP_Socket& socket)
  {
    ::poseidon::UUID service_uuid;
    if(socket.session_user_data().is_binary())
      ::memcpy(&service_uuid, socket.session_user_data().as_binary_data(), 16);
    return service_uuid;
  }

void
do_set_service_uuid(::poseidon::TCP_Socket& socket, const ::poseidon::UUID& service_uuid)
  {
    socket.mut_session_user_data() = cow_bstring(service_uuid.data(), 16);
  }

void
do_salt_password(char* pw, const ::poseidon::UUID& s, int64_t ts, const cow_string& password)
  {
    ::MD5_CTX ctx;
    ::MD5_Init(&ctx);

    ::MD5_Update(&ctx, s.data(), s.size());
    uint64_t be = ROCKET_HTOBE64(static_cast<uint64_t>(ts));
    ::MD5_Update(&ctx, &be, 8);
    ::MD5_Update(&ctx, password.data(), password.size());

    uint8_t checksum[16];
    ::MD5_Final(checksum, &ctx);
    ::poseidon::hex_encode_16_partial(pw, checksum);
  }

void
do_set_response(const wkptr<Service_Future>& weak_req, const ::poseidon::UUID& request_uuid,
                const ::taxon::V_object& response, const cow_string& error)
  {
    if(!error.empty())
      POSEIDON_LOG_ERROR(("Received service error: $1"), error);

    auto req = weak_req.lock();
    if(!req)
      return;

    bool all_received = true;
    for(auto p = req->mf_responses().mut_begin();  p != req->mf_responses().end();  ++p)
      if(p->request_uuid != request_uuid)
        all_received &= p->complete;
      else {
        p->obj = response;
        p->error = error;
        p->complete = true;
      }

    if(all_received)
      req->mf_abstract_future_complete();
  }

struct Local_Request_Fiber final : ::poseidon::Abstract_Fiber
  {
    wkptr<Implementation> m_weak_impl;
    wkptr<Service_Future> m_weak_req;
    ::poseidon::UUID m_request_uuid;
    phcow_string m_opcode;
    ::taxon::V_object m_request;

    Local_Request_Fiber(const shptr<Implementation>& impl, const shptr<Service_Future>& req,
                        const ::poseidon::UUID& request_uuid, const phcow_string& opcode,
                        const ::taxon::V_object& request)
      :
        m_weak_impl(impl), m_weak_req(req), m_request_uuid(request_uuid),
        m_opcode(opcode), m_request(request)
      {
      }

    virtual
    void
    do_on_abstract_fiber_execute()
      override
      {
        const auto impl = this->m_weak_impl.lock();
        if(!impl)
          return;

        ::taxon::V_object response;
        tinyfmt_str error_fmt;

        // Copy the handler, in case of fiber context switches.
        Service::handler_type handler;
        impl->handlers.find_and_copy(handler, this->m_opcode);
        if(!handler)
          format(error_fmt, "No handler for `$1` on $2", this->m_opcode, impl->service_type);
        else
          try {
            handler(*this, impl->service_uuid, response, this->m_request);
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Unhandled exception in `$1 $2`: $3"), this->m_opcode, this->m_request, stdex);
            format(error_fmt, "$1", stdex);
          }

        // If the caller will be waiting, set the response.
        do_set_response(this->m_weak_req, this->m_request_uuid, response, error_fmt.get_string());
      }
  };

void
do_client_ws_callback(const shptr<Implementation>& impl,
                      const shptr<::poseidon::WS_Client_Session>& session,
                      ::poseidon::Abstract_Fiber& /*fiber*/,
                      ::poseidon::Easy_WS_Event event, linear_buffer&& data)
  {
    switch(event)
      {
      case ::poseidon::easy_ws_open:
        POSEIDON_LOG_INFO(("Connected to service `$1`: $2"), session->remote_address(), data);
        break;

      case ::poseidon::easy_ws_text:
      case ::poseidon::easy_ws_binary:
        {
          const ::poseidon::UUID remote_service_uuid = do_get_service_uuid(*session);
          if(remote_service_uuid.is_nil())
            return;

          ::taxon::Value temp_value;
          POSEIDON_CHECK(temp_value.parse(data.data(), data.size()));
          ::taxon::V_object response = temp_value.as_object();
          temp_value.clear();

          ::poseidon::UUID request_uuid;
          if(auto ptr = response.ptr(&"@uuid"))
            request_uuid = ::poseidon::UUID(ptr->as_string());

          cow_string error;
          if(auto ptr = response.ptr(&"@error"))
            error = ptr->as_string();

          // Set the request future.
          Remote_Service_Connection_Record conn;
          impl->remote_connections.find_and_copy(conn, remote_service_uuid);

          for(const auto& r : conn.weak_futures)
            if(r.second == request_uuid) {
              do_set_response(r.first, request_uuid, response, error);
              break;
            }

          POSEIDON_LOG_TRACE(("Received response: request_uuid `$1`"), request_uuid);
          break;
        }

      case ::poseidon::easy_ws_pong:
        break;

      case ::poseidon::easy_ws_close:
        {
          const ::poseidon::UUID remote_service_uuid = do_get_service_uuid(*session);
          if(remote_service_uuid.is_nil())
            return;

          Remote_Service_Connection_Record conn;
          impl->remote_connections.find_and_erase(conn, remote_service_uuid);

          for(const auto& r : conn.weak_futures)
            if(auto req = r.first.lock()) {
              bool all_received = true;
              for(auto p = req->mf_responses().mut_begin();  p != req->mf_responses().end();  ++p)
                if(p->service_uuid != remote_service_uuid)
                  all_received &= p->complete;
                else {
                  POSEIDON_LOG_ERROR(("Connection to service `$1` has been lost"), remote_service_uuid);
                  p->error = &"Connection lost";
                  p->complete = true;
                }

              if(all_received)
                req->mf_abstract_future_complete();
            }

          POSEIDON_LOG_INFO(("Disconnected from `$1`: $2"), session->remote_address(), data);
          break;
        }
    }
  }

struct Remote_Response_Task final : ::poseidon::Abstract_Task
  {
    wkptr<::poseidon::WS_Server_Session> m_weak_session;
    ::poseidon::UUID m_request_uuid;
    ::taxon::V_object m_response;
    cow_string m_error;

    Remote_Response_Task(const shptr<::poseidon::WS_Server_Session>& session,
                         const ::poseidon::UUID& request_uuid,
                         const ::taxon::V_object& response, const cow_string& error)
      :
        m_weak_session(session),
        m_request_uuid(request_uuid), m_response(response), m_error(error)
      {
      }

    virtual
    void
    do_on_abstract_task_execute()
      override
      {
        const auto session = this->m_weak_session.lock();
        if(!session)
          return;

        this->m_response.try_emplace(&"@uuid", this->m_request_uuid.to_string());
        if(!this->m_error.empty())
          this->m_response.try_emplace(&"@error", this->m_error);

        auto str = ::taxon::Value(this->m_response).to_string();
        session->ws_send(::poseidon::ws_TEXT, str);
      }
  };

void
do_send_remote_response(const shptr<::poseidon::WS_Server_Session>& session,
                        const ::poseidon::UUID& request_uuid,
                        const ::taxon::V_object& response, const cow_string& error)
  {
    if(request_uuid.is_nil())
      return;

    auto task4 = new_sh<Remote_Response_Task>(session, request_uuid, response, error);
    ::poseidon::task_scheduler.launch(task4);
  }

struct Remote_Request_Fiber final : ::poseidon::Abstract_Fiber
  {
    wkptr<Implementation> m_weak_impl;
    wkptr<::poseidon::WS_Server_Session> m_weak_session;
    ::poseidon::UUID m_request_uuid;
    phcow_string m_opcode;
    ::taxon::V_object m_request;

    Remote_Request_Fiber(const shptr<Implementation>& impl,
                         const shptr<::poseidon::WS_Server_Session>& session,
                         const ::poseidon::UUID& request_uuid,
                         const phcow_string& opcode, const ::taxon::V_object& request)
      :
        m_weak_impl(impl), m_weak_session(session), m_request_uuid(request_uuid),
        m_opcode(opcode), m_request(request)
      {
      }

    virtual
    void
    do_on_abstract_fiber_execute()
      override
      {
        const auto impl = this->m_weak_impl.lock();
        if(!impl)
          return;

        const auto session = this->m_weak_session.lock();
        if(!session)
          return;

        const ::poseidon::UUID request_service_uuid = do_get_service_uuid(*session);
        if(request_service_uuid.is_nil())
          return;

        ::taxon::V_object response;
        tinyfmt_str error_fmt;

        // Copy the handler, in case of fiber context switches.
        Service::handler_type handler;
        impl->handlers.find_and_copy(handler, this->m_opcode);
        if(!handler)
          format(error_fmt, "No handler for `$1` on $2", this->m_opcode, impl->service_type);
        else
          try {
            handler(*this, request_service_uuid, response, this->m_request);
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Unhandled exception in `$1 $2`: $3"), this->m_opcode, this->m_request, stdex);
            format(error_fmt, "$1", stdex);
          }

        // If the caller will be waiting, set the response.
        do_send_remote_response(session, this->m_request_uuid, response, error_fmt.get_string());
      }
  };

void
do_server_ws_callback(const shptr<Implementation>& impl,
                      const shptr<::poseidon::WS_Server_Session>& session,
                      ::poseidon::Abstract_Fiber& /*fiber*/,
                      ::poseidon::Easy_WS_Event event, linear_buffer&& data)
  {
    switch(event)
      {
      case ::poseidon::easy_ws_open:
        {
          ::poseidon::Network_Reference uri;
          POSEIDON_CHECK(::poseidon::parse_network_reference(uri, data) == data.size());
          cow_string path = ::poseidon::decode_and_canonicalize_uri_path(uri.path);

          if(path.size() != 37) {
            session->ws_shut_down(::poseidon::ws_status_forbidden);
            return;
          }

          path.erase(0, 1);
          ::poseidon::UUID uuid_in_path;
          if((uuid_in_path.parse(path) != 36) || (uuid_in_path != impl->service_uuid)) {
            session->ws_shut_down(::poseidon::ws_status_forbidden);
            return;
          }

          // Check authentication.
          ::poseidon::UUID request_service_uuid;
          try {
            cow_string req_pw;
            int64_t req_ts = 0;

            ::poseidon::HTTP_Query_Parser parser;
            parser.reload(cow_string(uri.query));
            while(parser.next_element())
              if(parser.current_name() == "s")
                request_service_uuid = ::poseidon::UUID(parser.current_value().as_string());
              else if(parser.current_name() == "ts")
                req_ts = parser.current_value().as_integer();
              else if(parser.current_name() == "pw")
                req_pw = parser.current_value().as_string();

            POSEIDON_CHECK(request_service_uuid != ::poseidon::UUID());
            int64_t now = ::time(nullptr);
            POSEIDON_CHECK((req_ts >= now - 60) && (req_ts <= now + 60));
            char auth_pw[33];
            do_salt_password(auth_pw, request_service_uuid, req_ts, impl->application_password);
            POSEIDON_CHECK(req_pw == auth_pw);
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Authentication error from `$1`: $2"), session->remote_address(), stdex);
            session->ws_shut_down(::poseidon::ws_status_unauthorized);
            return;
          }

          do_set_service_uuid(*session, request_service_uuid);
          POSEIDON_LOG_INFO(("Accepted service from `$1`: $2"), session->remote_address(), data);
          break;
        }

      case ::poseidon::easy_ws_text:
      case ::poseidon::easy_ws_binary:
        {
          const ::poseidon::UUID request_service_uuid = do_get_service_uuid(*session);
          if(request_service_uuid.is_nil())
            return;

          ::taxon::Value temp_value;
          POSEIDON_CHECK(temp_value.parse(data.data(), data.size()));
          ::taxon::V_object request = temp_value.as_object();
          temp_value.clear();

          phcow_string opcode;
          if(auto ptr = request.ptr(&"@opcode"))
            opcode = ptr->as_string();

          ::poseidon::UUID request_uuid;
          if(auto ptr = request.ptr(&"@uuid"))
            request_uuid = ::poseidon::UUID(ptr->as_string());

          // Handle the request in another fiber, so it's stateless.
          auto fiber3 = new_sh<Remote_Request_Fiber>(impl, session, request_uuid, opcode, request);
          ::poseidon::fiber_scheduler.launch(fiber3);
          break;
        }

      case ::poseidon::easy_ws_pong:
        break;

      case ::poseidon::easy_ws_close:
        {
          const ::poseidon::UUID request_service_uuid = do_get_service_uuid(*session);
          if(request_service_uuid.is_nil())
            return;

          POSEIDON_LOG_INFO(("Disconnected from `$1`: $2"), session->remote_address(), data);
          break;
        }
      }
  }

struct Remote_Request_Task final : ::poseidon::Abstract_Task
  {
    wkptr<::poseidon::WS_Client_Session> m_weak_session;
    wkptr<Service_Future> m_weak_req;
    ::poseidon::UUID m_request_uuid;
    phcow_string m_opcode;
    ::taxon::V_object m_request;

    Remote_Request_Task(const shptr<::poseidon::WS_Client_Session>& session,
                        const shptr<Service_Future>& req, const ::poseidon::UUID& request_uuid,
                        const phcow_string& opcode, const ::taxon::V_object& request)
      :
        m_weak_session(session), m_weak_req(req), m_request_uuid(request_uuid),
        m_opcode(opcode), m_request(request)
      {
      }

    virtual
    void
    do_on_abstract_task_execute()
      override
      {
        const auto session = this->m_weak_session.lock();
        if(!session)
          return;

        this->m_request.try_emplace(&"@opcode", this->m_opcode);
        if(!this->m_weak_req.expired())
          this->m_request.try_emplace(&"@uuid", this->m_request_uuid.to_string());

        auto str = ::taxon::Value(this->m_request).to_string();
        session->ws_send(::poseidon::ws_TEXT, str);
      }
  };

void
do_subscribe_timer_callback(const shptr<Implementation>& impl,
                            const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                            ::poseidon::Abstract_Fiber& fiber, steady_time /*now*/)
  {
    cow_uuid_dictionary<Service_Record> remote_services;

    auto pattern = sformat("$1/service/*", impl->application_name);
    auto task2 = new_sh<::poseidon::Redis_Scan_and_Get_Future>(::poseidon::redis_connector, pattern);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);
    POSEIDON_LOG_TRACE(("Fetched $1 services"), task2->result().size());

    for(const auto& r : task2->result()) {
      Service_Record remote;
      try {
        remote.parse_from_string(r.second);
      }
      catch(exception& stdex) {
        POSEIDON_LOG_WARN(("Invalid service `$1`: $2"), r.first, stdex);
        continue;
      }

      if(remote.application_name != impl->application_name)
        continue;

      remote_services.try_emplace(remote.service_uuid, remote);
      POSEIDON_LOG_TRACE(("Received service `$1`: $2"), r.first, r.second);
    }

    for(const auto& r : impl->remote_services)
      if(remote_services.count(r.first) == 0)
        POSEIDON_LOG_WARN(("Service DOWN: `$1`: $2 $3 $4"),
                          r.first, r.second.zone_id, r.second.service_type,
                          r.second.service_index);

    for(const auto& r : remote_services)
      if(impl->remote_services.count(r.first) == 0)
        POSEIDON_LOG_INFO(("Service UP: `$1`: $2 $3 $4"),
                          r.first, r.second.zone_id, r.second.service_type,
                          r.second.service_index);

    impl->remote_services = remote_services;

    // Purge connections that have been lost, as well as services that have
    // been removed from Redis.
    for(const auto& r : impl->remote_connections) {
      auto session = r.second.weak_session.lock();
      if(session && impl->remote_services.count(r.first))
        continue;

      if(session)
        session->ws_shut_down(::poseidon::ws_status_normal);

      POSEIDON_LOG_INFO(("Purging expired service `$1`"), r.first);
      impl->expired_remote_service_uuid_list.emplace_back(r.first);
    }

    while(impl->expired_remote_service_uuid_list.size() != 0) {
      const ::poseidon::UUID remote_service_uuid = impl->expired_remote_service_uuid_list.back();
      impl->expired_remote_service_uuid_list.pop_back();

      Remote_Service_Connection_Record conn;
      if(!impl->remote_connections.find_and_erase(conn, remote_service_uuid))
        continue;

      for(const auto& r : conn.weak_futures)
        if(auto req = r.first.lock()) {
          bool all_received = true;
          for(auto p = req->mf_responses().mut_begin();  p != req->mf_responses().end();  ++p)
            if(p->service_uuid != remote_service_uuid)
              all_received &= p->complete;
            else {
              POSEIDON_LOG_ERROR(("Connection to service `$1` has been lost"), remote_service_uuid);
              p->error = &"Connection lost";
              p->complete = true;
            }

          if(all_received)
            req->mf_abstract_future_complete();
        }

    }
  }

void
do_publish_timer_callback(const shptr<Implementation>& impl,
                          const shptr<::poseidon::Abstract_Timer>& timer,
                          ::poseidon::Abstract_Fiber& fiber, steady_time now)
  {
    if(impl->appointment.index() < 0)
      return;

    if(impl->service_start_time == steady_time())
      impl->service_start_time = now;

    // Compose my service data.
    Service_Record local;
    local.service_uuid = impl->service_uuid;
    local.application_name = impl->application_name;
    local.zone_id = impl->zone_id;
    local.service_index = impl->appointment.index();
    local.zone_start_time = impl->zone_start_time;
    local.service_type = impl->service_type;
    local.hostname = ::poseidon::hostname;

    // Estimate my load factor.
    struct timespec ts;
    ::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    int64_t t0 = ts.tv_sec * 1000000000 + ts.tv_nsec;
    double perf_cpu_duration = clamp_cast<double>(t0 - impl->perf_cpu_time, 0, INT64_MAX);
    impl->perf_cpu_time = t0;

    ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    t0 = ts.tv_sec * 1000000000 + ts.tv_nsec;
    double perf_duration = clamp_cast<double>(t0 - impl->perf_time, 1, INT64_MAX);
    impl->perf_time = t0;

    local.load_factor = perf_cpu_duration / perf_duration;

    // Get all running network interfaces.
    ::poseidon::IPv6_Address addr = impl->private_server.local_address();
    if(addr.port() != 0) {
      ::rocket::unique_ptr<::ifaddrs, vfn<::ifaddrs*>> guard(nullptr, ::freeifaddrs);
      ::ifaddrs* ifa = nullptr;
      if(::getifaddrs(&ifa) == 0)
        guard.reset(ifa);
      else
        POSEIDON_LOG_ERROR(("Network configuration error: ${errno:full}]"));

      for(ifa = guard;  ifa;  ifa = ifa->ifa_next)
        if(!(ifa->ifa_flags & IFF_RUNNING) || !ifa->ifa_addr)
          continue;
        else if(ifa->ifa_addr->sa_family == AF_INET) {
          // IPv4
          auto sa = reinterpret_cast<::sockaddr_in*>(ifa->ifa_addr);
          ::memcpy(addr.mut_data(), ::poseidon::ipv4_unspecified.data(), 16);
          ::memcpy(addr.mut_data() + 12, &(sa->sin_addr), 4);
          local.addresses.emplace_back(addr.to_string());
        }
        else if(ifa->ifa_addr->sa_family == AF_INET6) {
          // IPv6
          auto sa = reinterpret_cast<::sockaddr_in6*>(ifa->ifa_addr);
          addr.set_addr(sa->sin6_addr);
          local.addresses.emplace_back(addr.to_string());
        }
    }

    // Publish my service information on Redis.
    cow_vector<cow_string> cmd;
    cmd.emplace_back(&"SET");
    cmd.emplace_back(sformat("$1/service/$2", impl->application_name, impl->service_uuid));
    cmd.emplace_back(local.serialize_to_string());
    cmd.emplace_back(&"EX");
    cmd.emplace_back("10");

    auto task1 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, cmd);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);
    POSEIDON_CHECK(task1->status() == "OK");
    POSEIDON_LOG_TRACE(("Published service `$1`: $2"), cmd.at(1), cmd.at(2));

    if(impl->service_start_time - now <= 60s)
      do_subscribe_timer_callback(impl, timer, fiber, now);
  }

}  // namespace

POSEIDON_HIDDEN_X_STRUCT(Service,
    Implementation);

Service::
Service()
  {
  }

Service::
~Service()
  {
  }

void
Service::
add_handler(const phcow_string& opcode, const handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    if(this->m_impl->handlers.try_emplace(opcode, handler).second == false)
      POSEIDON_THROW(("Handler for `$1` already exists"), opcode);
  }

bool
Service::
set_handler(const phcow_string& opcode, const handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    return this->m_impl->handlers.insert_or_assign(opcode, handler).second;
  }

bool
Service::
remove_handler(const phcow_string& opcode)
  noexcept
  {
    if(!this->m_impl)
      return false;

    return this->m_impl->handlers.erase(opcode);
  }

const ::poseidon::UUID&
Service::
service_uuid()
  const noexcept
  {
    if(!this->m_impl)
      return ::poseidon::UUID();

    return this->m_impl->service_uuid;
  }

int
Service::
service_index()
  const noexcept
  {
    if(!this->m_impl)
      return 0;

    return this->m_impl->appointment.index();
  }

const cow_string&
Service::
application_name()
  const noexcept
  {
    if(!this->m_impl)
      return ::poseidon::empty_cow_string;

    return this->m_impl->application_name;
  }

int
Service::
zone_id()
  const noexcept
  {
    if(!this->m_impl)
      return 0;

    return this->m_impl->zone_id;
  }

system_time
Service::
zone_start_time()
  const noexcept
  {
    if(!this->m_impl)
      return system_time();

    return this->m_impl->zone_start_time;
  }

const cow_string&
Service::
service_type()
  const noexcept
  {
    if(!this->m_impl)
      return ::poseidon::empty_cow_string;

    return this->m_impl->service_type;
  }

const cow_uuid_dictionary<Service_Record>&
Service::
all_service_records()
  const noexcept
  {
    if(!this->m_impl)
      return empty_service_record_map;

    return this->m_impl->remote_services;
  }

const Service_Record&
Service::
find_service_record_opt(const ::poseidon::UUID& remote_service_uuid)
  const noexcept
  {
    if(!this->m_impl)
      return Service_Record::null;

    auto ptr = this->m_impl->remote_services.ptr(remote_service_uuid);
    if(!ptr)
      return Service_Record::null;

    return *ptr;
  }

void
Service::
reload(const ::poseidon::Config_File& conf_file, const cow_string& service_type)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // Read service configuration. These fields are required.
    cow_string application_name = conf_file.get_string(&"application_name");
    POSEIDON_LOG_DEBUG(("Initializing: $1"), application_name);
    cow_string application_password = conf_file.get_string(&"application_password");
    cow_string lock_directory = conf_file.get_string(&"lock_directory");

    int zone_id = static_cast<int>(conf_file.get_integer(&"zone_id", 0, 9999999));
    cow_string zone_start_time_str = conf_file.get_string(&"zone_start_time");
    POSEIDON_LOG_DEBUG(("Initializing: zone $1 started on $2"), zone_id, zone_start_time_str);
    system_time zone_start_time = ::poseidon::DateTime(zone_start_time_str).as_system_time();

    if(application_name.size() < 2)
      POSEIDON_THROW((
          "Invalid `application_name`: name too short",
          "[in configuration file '$2']"),
          application_name, conf_file.path());

    for(char ch : application_name)
      if(::iscntrl(ch) || (!::isalnum(ch) && !::strchr("_.-:~!()", ch)))
        POSEIDON_THROW((
            "Invalid `application_name`: character `$1` not allowed",
            "[in configuration file '$2']"),
            ch, conf_file.path());

    if(zone_start_time > system_clock::now())
      POSEIDON_THROW((
          "Invalid `zone_start_time`: time point `$1` is in the future",
          "[in configuration file '$2']"),
          zone_start_time, conf_file.path());

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->service_type = service_type;
    this->m_impl->application_name = application_name;
    this->m_impl->application_password = application_password;
    this->m_impl->zone_id = zone_id;
    this->m_impl->zone_start_time = zone_start_time;

    // Set up constants.
    if(this->m_impl->service_uuid.is_nil())
      this->m_impl->service_uuid = ::poseidon::UUID::random();

    if(this->m_impl->appointment.index() == -1)
      this->m_impl->appointment.enroll(sformat("$1/$2.lock", lock_directory, service_type));

    // Restart the service.
    this->m_impl->publish_timer.start(1500ms, 6101ms, bindw(this->m_impl, do_publish_timer_callback));
    this->m_impl->subscribe_timer.start(31001ms, bindw(this->m_impl, do_subscribe_timer_callback));
    this->m_impl->private_server.start(0, bindw(this->m_impl, do_server_ws_callback));
  }

void
Service::
launch(const shptr<Service_Future>& req)
  {
    if(!req)
      POSEIDON_THROW(("Null request pointer"));

    if(!this->m_impl)
      POSEIDON_THROW(("Service not initialized"));

    bool all_received = true;
    for(size_t k = 0;  k != req->mf_responses().size();  ++k) {
      auto& resp = req->mf_responses().mut(k);
      resp.request_uuid = ::poseidon::UUID::random();

      if(resp.service_uuid == this->m_impl->service_uuid) {
        // This is myself, so there's no need to send it over network.
        auto fiber3 = new_sh<Local_Request_Fiber>(this->m_impl, req, resp.request_uuid, req->opcode(),
                                                  req->request());
        ::poseidon::fiber_scheduler.launch(fiber3);
        all_received = false;
      }
      else {
        auto srv = this->m_impl->remote_services.ptr(resp.service_uuid);
        if(!srv) {
          POSEIDON_LOG_ERROR(("Service `$1` not found"), resp.service_uuid);
          resp.error = &"Service not found";
          resp.complete = true;
          continue;
        }

        // Send the request asynchronously.
        auto& conn = this->m_impl->remote_connections.open(resp.service_uuid);
        auto session = conn.weak_session.lock();
        if(!session) {
          // Find an address to connect to. If the address is loopback, it shall
          // only be accepted if the target service is on the same machine, and in
          // this case it takes precedence over a private address.
          auto use_addr = ::poseidon::ipv6_invalid;
          for(const auto& addr : srv->addresses)
            if(addr.classify() != ::poseidon::ip_address_loopback)
              use_addr = addr;
            else if(srv->hostname == ::poseidon::hostname) {
              use_addr = addr;
              break;
            }

          if(use_addr == ::poseidon::ipv6_invalid) {
            POSEIDON_LOG_ERROR(("Service `$1` has no address"), resp.service_uuid);
            resp.error = &"Service unreachable";
            resp.complete = true;
            continue;
          }

          tinyfmt_str saddr_fmt;
          format(saddr_fmt, "$1/$2?s=$3", use_addr, resp.service_uuid, this->m_impl->service_uuid);
          int64_t now = ::time(nullptr);
          format(saddr_fmt, "&ts=$1", now);
          char auth_pw[33];
          do_salt_password(auth_pw, this->m_impl->service_uuid, now, this->m_impl->application_password);
          format(saddr_fmt, "&pw=$1", auth_pw);

          cow_string saddr = saddr_fmt.get_string();
          session = this->m_impl->private_client.connect(saddr, bindw(this->m_impl, do_client_ws_callback));

          do_set_service_uuid(*session, resp.service_uuid);
          conn.weak_session = session;
          POSEIDON_LOG_INFO(("Connecting to service `$1` at `$2`"), resp.service_uuid, use_addr);
        }

        // Add this future to the waiting list.
        size_t index = SIZE_MAX;
        for(size_t b = 0;  b != conn.weak_futures.size();  ++b)
          if(conn.weak_futures[b].first.expired()) {
            index = b;
            break;
          }

        if(index == SIZE_MAX)
          conn.weak_futures.emplace_back(req, resp.request_uuid);
        else
          conn.weak_futures.mut(index) = ::std::make_pair(req, resp.request_uuid);

        // Send and wait.
        auto task2 = new_sh<Remote_Request_Task>(session, req, resp.request_uuid, req->opcode(),
                                                 req->request());
        ::poseidon::task_scheduler.launch(task2);
        all_received = false;
      }
    }

    if(all_received)
      req->mf_abstract_future_complete();
  }

}  // namespace k32
