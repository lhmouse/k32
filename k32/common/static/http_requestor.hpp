// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#ifndef K32_COMMON_STATIC_HTTP_REQUESTOR_
#define K32_COMMON_STATIC_HTTP_REQUESTOR_

#include "../../fwd.hpp"
#include "../fiber/http_future.hpp"
namespace k32 {

class HTTP_Requestor
  {
  private:
    struct X_Implementation;
    shptr<X_Implementation> m_impl;

  public:
    HTTP_Requestor();

  public:
    HTTP_Requestor(const HTTP_Requestor&) = delete;
    HTTP_Requestor& operator=(const HTTP_Requestor&) & = delete;
    ~HTTP_Requestor();

    // Enqueues an HTTP request. After this function returns, the caller shall
    // wait on the future; if the source `shptr` goes out of scope before a
    // request is initiated, then it is silently discarded. If this function
    // fails, an exception is thrown, and there is no effect.
    void
    enqueue_weak(const shptr<HTTP_Future>& req);
  };

}  // namespace k32
#endif
