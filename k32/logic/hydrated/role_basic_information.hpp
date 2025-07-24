// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_LOGIC_HYDRATED_ROLE_BASIC_INFORMATION_
#define K32_LOGIC_HYDRATED_ROLE_BASIC_INFORMATION_

#include "../../fwd.hpp"
namespace k32::logic {

class Role_Basic_Information
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
    Role_Basic_Information() noexcept = default;
#endif
    Role_Basic_Information(const Role_Basic_Information&) = delete;
    Role_Basic_Information& operator=(const Role_Basic_Information&) & = delete;
    ~Role_Basic_Information();

    // These fields are read-only.
    int64_t
    roid()
      const noexcept { return this->m_roid;  }

    const phcow_string&
    username()
      const noexcept { return this->m_username;  }

    const cow_string&
    nickname()
      const noexcept { return this->m_nickname;  }

    const ::poseidon::UUID&
    agent_service_uuid()
      const noexcept { return this->m_agent_srv;  }

    bool
    disconnected()
      const noexcept { return this->m_agent_srv.is_nil();  }
  };

inline
tinyfmt&
operator<<(tinyfmt& fmt, const Role_Basic_Information& role)
  {
    return format(fmt, "role `$1` (`$2`)", role.roid(), role.nickname());
  }

}  // namespace k32::logic
#endif
