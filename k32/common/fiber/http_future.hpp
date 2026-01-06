// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#ifndef K32_COMMON_FIBER_HTTP_FUTURE_
#define K32_COMMON_FIBER_HTTP_FUTURE_

#include "../../fwd.hpp"
#include <poseidon/fiber/abstract_future.hpp>
namespace k32 {

class HTTP_Future
  :
    public ::poseidon::Abstract_Future
  {
  private:
    cow_string m_req_uri;
    cow_string m_req_content_type;
    cow_string m_req_payload;

    uint32_t m_resp_status_code;
    cow_string m_resp_content_type;
    cow_string m_resp_payload;

  public:
    explicit HTTP_Future(const cow_string& req_uri);

    HTTP_Future(const cow_string& req_uri, const cow_string& req_content_type,
                const cow_string& req_payload);

  private:
    virtual
    void
    do_on_abstract_future_initialize()
      override;

  public:
#ifdef K32_FRIENDS_C84621A4_4E68_11F0_BA96_5254005015D2_
    uint32_t& mf_resp_status_code() { return this->m_resp_status_code;  }
    cow_string& mf_resp_content_type() { return this->m_resp_content_type;  }
    cow_string& mf_resp_payload() { return this->m_resp_payload;  }
    void mf_abstract_future_complete() { this->do_abstract_future_initialize_once();  }
#endif
    HTTP_Future(const HTTP_Future&) = delete;
    HTTP_Future& operator=(const HTTP_Future&) = delete;
    virtual ~HTTP_Future();

    // Gets the URI of the request message. This field is set by the constructor.
    const cow_string&
    request_uri()
      const noexcept
      { return this->m_req_uri;  }

    // Gets the `Content-Type` of the request message. This field is set by the
    // constructor.
    const cow_string&
    request_content_type()
      const noexcept
      { return this->m_req_content_type;  }

    // Gets the payload of the request message. For a GET message, the payload
    // shall be empty. This field is set by the constructor.
    const cow_string&
    request_payload()
      const noexcept
      { return this->m_req_payload;  }

    // Gets the status code of the response message. If the connection is closed
    // without a response, zero is returned. If `successful()` yields `false`,
    // an exception is thrown, and there is no effect.
    uint32_t
    response_status_code()
      const
      {
        this->check_success();
        return this->m_resp_status_code;
      }

    // Gets the `Content-Type` of the response message. If there was no such
    // header, an empty string is returned. If `successful()` yields `false`, an
    // exception is thrown, and there is no effect.
    const cow_string&
    response_content_type()
      const
      {
        this->check_success();
        return this->m_resp_content_type;
      }

    // Gets the payload of the response message. If `successful()` yields
    // `false`, an exception is thrown, and there is no effect.
    const cow_string&
    response_payload()
      const
      {
        this->check_success();
        return this->m_resp_payload;
      }
  };

}  // namespace k32
#endif
