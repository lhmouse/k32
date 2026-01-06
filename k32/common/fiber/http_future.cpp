// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_C84621A4_4E68_11F0_BA96_5254005015D2_
#include "http_future.hpp"
namespace k32 {

HTTP_Future::
HTTP_Future(const cow_string& req_uri)
  {
    this->m_req_uri = req_uri;
  }

HTTP_Future::
HTTP_Future(const cow_string& req_uri, const cow_string& req_content_type,
            const cow_string& req_payload)
  {
    this->m_req_uri = req_uri;
    this->m_req_content_type = req_content_type;
    this->m_req_payload = req_payload;
  }

HTTP_Future::
~HTTP_Future()
  {
  }

void
HTTP_Future::
do_on_abstract_future_initialize()
  {
  }

}  // namespace k32
