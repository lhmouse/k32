// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#ifndef K32_LOGIC_HYDRATED_ROLE_
#define K32_LOGIC_HYDRATED_ROLE_

#include "../../fwd.hpp"
namespace k32::logic {

class Role
  {
  private:
    int64_t m_roid = 0;
    cow_string m_nickname;
    phcow_string m_username;
    ::poseidon::UUID m_agent_srv;
    ::poseidon::UUID m_monitor_srv;
    steady_time m_dc_since;

  public:
#ifdef K32_FRIENDS_3543B0B1_DC5A_4F34_B9BB_CAE513821771_
    int64_t& mf_roid() { return this->m_roid;  }
    cow_string& mf_nickname() { return this->m_nickname;  }
    phcow_string& mf_username() { return this->m_username;  }
    ::poseidon::UUID& mf_agent_srv() { return this->m_agent_srv;  }
    ::poseidon::UUID& mf_monitor_srv() { return this->m_monitor_srv;  }
    steady_time& mf_dc_since() { return this->m_dc_since;  }
    Role() noexcept = default;
#endif
    Role(const Role&) = delete;
    Role& operator=(const Role&) & = delete;
    ~Role();

    // These fields are read-only.
    int64_t
    roid()
      const noexcept
      { return this->m_roid;  }

    const phcow_string&
    username()
      const noexcept
      { return this->m_username;  }

    const cow_string&
    nickname()
      const noexcept
      { return this->m_nickname;  }

    const ::poseidon::UUID&
    agent_service_uuid()
      const noexcept
      { return this->m_agent_srv;  }

    // Load role cultivation data from the database. This is an internal function
    // called by the role service, after basic information has been filled in.
    void
    parse_from_db_record(const ::taxon::V_object& db_record);

    // Create an avatar of this role. This is what others can see outdoors.
    void
    make_avatar(::taxon::V_object& avatar);

    // Create a profile snapshot of this role. This is what others can see on
    // the profile page.
    void
    make_profile(::taxon::V_object& profile);

    // Serialize role cultivation data which can then be stored into the database.
    void
    make_db_record(::taxon::V_object& db_record);

    // This function is called right after a role has been loaded from Redis.
    void
    on_login();

    // This function is called right after a role has been loaded from Redis, or
    // right after a client has reconnected.
    void
    on_connect();

    // This function is called right after a client has disconnected, or just
    // before a role is unloaded.
    void
    on_disconnect();

    // This function is called just before a role is unloaded.
    void
    on_logout();

    // This function is called for every clock tick.
    void
    on_every_second();
  };

inline
tinyfmt&
operator<<(tinyfmt& fmt, const Role& role)
  {
    return format(fmt, "role `$1` (`$2`)", role.roid(), role.nickname());
  }

}  // namespace k32::logic
#endif
