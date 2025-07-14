// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_COMMON_STATIC_SERVICE_
#define K32_COMMON_STATIC_SERVICE_

#include "../../fwd.hpp"
#include "../data/service_record.hpp"
#include "../fiber/service_future.hpp"
namespace k32 {

class Service
  {
  private:
    struct X_Implementation;
    shptr<X_Implementation> m_impl;

  public:
    Service();

  public:
    Service(const Service&) = delete;
    Service& operator=(const Service&) & = delete;
    ~Service();

    // This callback is invoked when a service request message is received.
    // `request` is the same field as in the source `Service_Future`.
    using handler_type = shared_function<
            void (
              ::poseidon::Abstract_Fiber& fiber,
              const ::poseidon::UUID& request_service_uuid,
              ::taxon::V_object& response,  // output parameter
              const ::taxon::V_object& request)>;

    // Adds a new handler for requests from other servers. If a new handler
    // already exists, an exception is thrown.
    void
    add_handler(const phcow_string& opcode, const handler_type& handler);

    // Adds a new handler, or replaces an existing one, for requests from other
    // servers. If a new handler has been added, `true` is returned. If an
    // existent handler has been overwritten, `false` is returned.
    bool
    set_handler(const phcow_string& opcode, const handler_type& handler);

    // Removes a handler for requests from other servers.
    bool
    remove_handler(const phcow_string& opcode)
      noexcept;

    // Returns the UUID of the active service. If service is not active, a nil
    // UUID is returned.
    const ::poseidon::UUID&
    service_uuid()
      const noexcept;

    // Returns the 0-based index of the active service. This value is unique in
    // all instances sharing the same configuration file. If there is no active
    // service, -1 is returned.
    int
    service_index()
      const noexcept;

    // Returns the application name of the active service. If there is no active
    // service, an empty string is returned.
    const cow_string&
    application_name()
      const noexcept;

    // Gets the pre-configured zone ID of this service. This identifies services
    // that belong in the same zone within the same application. If no service is
    // running, zero is returned.
    int
    zone_id()
      const noexcept;

    // Gets the pre-configured start time of this zone. If no service is running,
    // '1970-01-01 00:00:00 UTC' is returned.
    system_time
    zone_start_time()
      const noexcept;

    // Returns the type of the active service. If there is no active service, an
    // empty string is returned.
    const cow_string&
    service_type()
      const noexcept;

    // Gets all service records, cached from Redis.
    const cow_uuid_dictionary<Service_Record>&
    all_service_records()
      const noexcept;

    // Gets properties of a remote service.
    const Service_Record&
    find_service_record_opt(const ::poseidon::UUID& remote_service_uuid)
      const noexcept;

    // Reloads configuration. If `application_name` or `application_password`
    // is changed, a new service (with a new UUID) is initiated.
    void
    reload(const ::poseidon::Config_File& conf_file, const cow_string& service_type);

    // Initiates an asynchronous service request. After this function returns,
    // the caller may wait on the future. If this function fails, an exception
    // is thrown, and there is no effect.
    void
    launch(const shptr<Service_Future>& req);
  };

}  // namespace k32
#endif
