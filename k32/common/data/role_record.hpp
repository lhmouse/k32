// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#ifndef K32_COMMON_DATA_ROLE_RECORD_
#define K32_COMMON_DATA_ROLE_RECORD_

#include "../../fwd.hpp"
namespace k32 {

struct Role_Record
  {
    int64_t roid = 0;
    phcow_string username;
    cow_string nickname;
    system_time update_time;
    cow_string avatar;
    cow_string profile;
    cow_string whole;

    cow_string _home_host;
    cow_string _home_db;
    int _home_zone = 0;

#ifdef K32_FRIENDS_3543B0B1_DC5A_4F34_B9BB_CAE513821771_
    Role_Record() noexcept = default;
#endif
    Role_Record(const Role_Record&) = default;
    Role_Record(Role_Record&&) = default;
    Role_Record& operator=(const Role_Record&) & = default;
    Role_Record& operator=(Role_Record&&) & = default;
    ~Role_Record();

    static const Role_Record null;
    explicit operator bool()
      const noexcept { return this->roid == 0;  }

    void
    parse_from_string(const cow_string& str);

    cow_string
    serialize_to_string()
      const;
  };

}  // namespace k32
#endif
