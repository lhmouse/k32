// This file is part of k32.
// Copyright (C) 2024-2026 LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_C84621A4_4E68_11F0_BA96_5254005015D2_
#include "http_requestor.hpp"
#include <poseidon/easy/easy_http_client.hpp>
#include <poseidon/easy/easy_https_client.hpp>
#include <deque>
namespace k32 {
namespace {

struct Connection_to_Host
  {
    wkptr<::poseidon::HTTP_Client_Session> weak_http_session;
    wkptr<::poseidon::HTTPS_Client_Session> weak_https_session;
    ::std::deque<wkptr<HTTP_Future>> request_queue;
  };

struct Implementation
  {
    ::poseidon::Easy_HTTP_Client http_client;
    ::poseidon::Easy_HTTPS_Client https_client;
    cow_dictionary<Connection_to_Host> connections_by_host;
  };

void
do_set_single_response(const shptr<Implementation>& impl, const cow_string& default_host,
                       ::poseidon::HTTP_S_Headers&& ht_resp, linear_buffer&& data)
  {
    if(ht_resp.status <= 199)
      return;

    auto& conn = impl->connections_by_host.open(default_host);
    if(conn.request_queue.empty())
      return;

    auto req = conn.request_queue.front().lock();
    conn.request_queue.pop_front();

    if(!req)
      return;

    for(const auto& r : ht_resp.headers)
      if(r.first == "Content-Type")
        req->mf_resp_content_type() = r.second.as_string();

    req->mf_resp_status_code() = ht_resp.status;
    req->mf_resp_payload().assign(data.begin(), data.end());
    req->mf_abstract_future_complete();
  }

void
do_remove_connection(const shptr<Implementation>& impl, const cow_string& default_host)
  {
    Connection_to_Host conn;
    impl->connections_by_host.find_and_erase(conn, default_host);

    while(!conn.request_queue.empty()) {
      auto req = conn.request_queue.front().lock();
      conn.request_queue.pop_front();

      if(!req)
        continue;

      req->mf_resp_status_code() = 0;
      req->mf_abstract_future_complete();
    }
  }

void
do_http_callback(const shptr<Implementation>& impl,
                 const shptr<::poseidon::HTTP_Client_Session>& session,
                 ::poseidon::Abstract_Fiber& /*fiber*/, ::poseidon::Easy_HTTP_Event event,
                 ::poseidon::HTTP_S_Headers&& ht_resp, linear_buffer&& data)
  {
    switch(event)
      {
      case ::poseidon::easy_http_open:
        POSEIDON_LOG_INFO(("Connected to `http://$1`"), session->http_default_host());
        break;

      case ::poseidon::easy_http_message:
        do_set_single_response(impl, session->http_default_host(), move(ht_resp), move(data));
        break;

      case ::poseidon::easy_http_close:
        do_remove_connection(impl, session->http_default_host());
        POSEIDON_LOG_INFO(("Disconnected from `http://$1`"), session->http_default_host());
        break;
      }
  }

void
do_https_callback(const shptr<Implementation>& impl,
                  const shptr<::poseidon::HTTPS_Client_Session>& session,
                  ::poseidon::Abstract_Fiber& /*fiber*/, ::poseidon::Easy_HTTP_Event event,
                  ::poseidon::HTTP_S_Headers&& ht_resp, linear_buffer&& data)
  {
    switch(event)
      {
      case ::poseidon::easy_http_open:
        POSEIDON_LOG_INFO(("Connected to `https://$1`"), session->https_default_host());
        break;

      case ::poseidon::easy_http_message:
        do_set_single_response(impl, session->https_default_host(), move(ht_resp), move(data));
        break;

      case ::poseidon::easy_http_close:
        do_remove_connection(impl, session->https_default_host());
        POSEIDON_LOG_INFO(("Disconnected from `https://$1`"), session->https_default_host());
        break;
      }
  }

}  // namespace

POSEIDON_HIDDEN_X_STRUCT(HTTP_Requestor,
    Implementation);

HTTP_Requestor::
HTTP_Requestor()
  {
  }

HTTP_Requestor::
~HTTP_Requestor()
  {
  }

void
HTTP_Requestor::
enqueue_weak(const shptr<HTTP_Future>& req)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    if(!req)
      POSEIDON_THROW(("Null request pointer"));

    uint32_t default_port = 0;
    ::poseidon::chars_view uri_sv;
    if(req->request_uri().starts_with("http://")) {
      default_port = 80;
      uri_sv = ::poseidon::chars_view(req->request_uri().data() + 7, req->request_uri().size() - 7);
    }
    else if(req->request_uri().starts_with("https://")) {
      default_port = 443;
      uri_sv = ::poseidon::chars_view(req->request_uri().data() + 8, req->request_uri().size() - 8);
    }
    else
      POSEIDON_THROW(("Invalid HTTP request URI `$1`"), req->request_uri());

    ::poseidon::Network_Reference caddr;
    if(::poseidon::parse_network_reference(caddr, uri_sv) != uri_sv.n)
        POSEIDON_THROW(("Invalid HTTP request URI `$1`"), req->request_uri());

    if(caddr.port.n == 0)
      caddr.port_num = static_cast<uint16_t>(default_port);

    // Allocate a connection.
    shptr<::poseidon::HTTP_Client_Session> http_session;
    shptr<::poseidon::HTTPS_Client_Session> https_session;

    auto default_host = sformat("$1:$2", caddr.host, caddr.port_num);
    auto& conn = this->m_impl->connections_by_host.open(default_host);
    if(default_port == 80) {
      http_session = conn.weak_http_session.lock();
      if(!http_session) {
        http_session = this->m_impl->http_client.connect(default_host, bindw(this->m_impl, do_http_callback));
        conn.weak_http_session = http_session;
        POSEIDON_LOG_INFO(("Connecting to `http://$1`"), http_session->http_default_host());
      }
    }
    else {
      https_session = conn.weak_https_session.lock();
      if(!https_session) {
        https_session = this->m_impl->https_client.connect(default_host, bindw(this->m_impl, do_https_callback));
        conn.weak_https_session = https_session;
        POSEIDON_LOG_INFO(("Connecting to `https://$1`"), https_session->https_default_host());
      }
    }

    // Add this future to the waiting list.
    conn.request_queue.emplace_back(req);

    // Send the request message.
    ::poseidon::HTTP_C_Headers ht_req;
    ht_req.method = ::poseidon::http_GET;
    ht_req.raw_path = cow_string(caddr.path);
    ht_req.raw_query = cow_string(caddr.query);
    ht_req.headers.emplace_back(&"Connection", &"keep-alive");

    if(!req->request_payload().empty()) {
      // Set request payload.
      ht_req.method = ::poseidon::http_POST;
      if(!req->request_content_type().empty())
        ht_req.headers.emplace_back(&"Content-Type", req->request_content_type());
    }

    if(http_session)
      http_session->http_request(move(ht_req), req->request_payload());
    else
      https_session->https_request(move(ht_req), req->request_payload());
  }

}  // namespace k32
