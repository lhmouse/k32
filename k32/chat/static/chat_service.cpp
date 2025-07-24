// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_A40BD99F_5E7D_486F_A905_656CBDBE52AB_
#include "chat_service.hpp"
#include "../globals.hpp"
#include <poseidon/base/config_file.hpp>
#include <poseidon/easy/easy_ws_server.hpp>
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/fiber/mysql_check_table_future.hpp>
#include <poseidon/fiber/redis_query_future.hpp>
#include <poseidon/static/task_scheduler.hpp>
#include <poseidon/fiber/mysql_query_future.hpp>
#include <poseidon/mysql/mysql_connection.hpp>
#include <poseidon/static/mysql_connector.hpp>
#include <map>
#include <list>
#include <algorithm>
namespace k32::chat {
namespace {

const cow_dictionary<Chat_Thread> empty_chat_thread_map;

struct Implementation
  {
    uint32_t max_number_of_messages_per_thread;
    seconds cached_thread_ttl;

    ::poseidon::Easy_Timer save_timer;

    // remote data from mysql
    bool db_ready = false;
    cow_dictionary<Chat_Thread> chat_threads;
    ::std::list<static_vector<phcow_string, 255>> save_buckets;
  };

void
do_mysql_check_table_chat(::poseidon::Abstract_Fiber& fiber)
  {
    ::poseidon::MySQL_Table_Structure table;
    table.name = &"chat";
    table.engine = ::poseidon::mysql_engine_innodb;

    ::poseidon::MySQL_Table_Column column;
    column.name = &"thread_key";
    column.type = ::poseidon::mysql_column_varchar;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"update_time";
    column.type = ::poseidon::mysql_column_datetime;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"whole";
    column.type = ::poseidon::mysql_column_blob;
    table.columns.emplace_back(column);

    ::poseidon::MySQL_Table_Index index;
    index.name = &"PRIMARY";
    index.type = ::poseidon::mysql_index_unique;
    index.columns.emplace_back(&"thread_key");
    table.indexes.emplace_back(index);

    // This is in the default database.
    auto task = new_sh<::poseidon::MySQL_Check_Table_Future>(::poseidon::mysql_connector, table);
    ::poseidon::task_scheduler.launch(task);
    fiber.yield(task);
    POSEIDON_LOG_INFO(("Finished verification of MySQL table `$1`"), table.name);
  }

void
do_star_chat_check_threads(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                           const ::poseidon::UUID& /*request_service_uuid*/,
                           ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    // * Request Parameters
    //   - `thread_key_list` <sub>array of strings</sub> : List of threads to check.
    //   - `last_check_time` <sub>timestamp, optional</sub> : Timestamp of last check.
    //
    // * Response Parameters
    //   - `status` <sub>string</sub> : [General status code.](#general-status-codes)
    //   - `raw_payload_list` <sub>array of strings</sub> : Message payloads, encoded
    //     in JSON and sorted by time of creation.
    //   - `check_time` <sub>timestamp</sub> : Timestamp of last check.
    //
    // * Description
    //   Retrieves messages from all threads in `thread_key_list`. If `last_check_time`
    //   is specified, only messages whose timestamp is _greater than_ `last_check_time`
    //   are returned.

    ////////////////////////////////////////////////////////////
    //
    ::std::vector<phcow_string> thread_key_list;
    if(auto plist = request.ptr(&"thread_key_list"))
      for(const auto& r : plist->as_array())
        thread_key_list.emplace_back(r.as_string());

    system_time last_check_time;
    if(auto ptr = request.ptr(&"last_check_time"))
      last_check_time = ptr->as_time();

    POSEIDON_CHECK(impl->db_ready);

    ////////////////////////////////////////////////////////////
    //
    ::std::multimap<system_time, cow_string> sorted_messages;
    const pair<system_time, cow_string> origin(last_check_time, &"");

    for(const auto& thread_key : thread_key_list)
      if(!impl->chat_threads.count(thread_key)) {
        // Load thread from MySQL.
        static constexpr char select_from_chat[] =
            R"!!!(
              SELECT `whole`
                FROM `chat`
                WHERE `thread_key` = ?
            )!!!";

        cow_vector<::poseidon::MySQL_Value> sql_args;
        sql_args.emplace_back(thread_key.rdstr());     // WHERE `thread_key` = ?

        auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                                            &select_from_chat, sql_args);
        ::poseidon::task_scheduler.launch(task1);
        fiber.yield(task1);

        if(task1->result_row_count() == 0)
          continue;

        Chat_Thread thread;
        thread.parse_from_string(task1->result_row(0).at(0).as_blob());    // SELECT `whole`
        impl->chat_threads.try_emplace(thread_key, thread);
      }

    // Copy messages.
    for(const auto& thread_key : thread_key_list)
      if(auto pth = impl->chat_threads.ptr(thread_key))
        sorted_messages.insert(
            ::std::upper_bound(pth->messages.begin(), pth->messages.end(), origin),
            pth->messages.end());

    // Pack sorted messages.
    ::taxon::V_array raw_payload_list;
    raw_payload_list.reserve(sorted_messages.size());
    for(const auto& r : sorted_messages)
      raw_payload_list.emplace_back(r.second);

    response.try_emplace(&"raw_payload_list", raw_payload_list);
    response.try_emplace(&"check_time", origin.first);
    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_chat_save_message(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                          const ::poseidon::UUID& /*request_service_uuid*/,
                          ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    // * Request Parameters
    //   - `"thread_key"` <sub>string</sub> : Key of thread to append new message.
    //   - `"raw_payload"` <sub>string</sub> : Message payload, encoded in JSON.
    //
    // * Response Parameters
    //   - `status` <sub>string</sub> : [General status code.](#general-status-codes)
    //
    // * Description
    //   Appends a new message to the end of `thread_key`.

    ////////////////////////////////////////////////////////////
    //
    phcow_string thread_key = request.at(&"thread_key").as_string();
    POSEIDON_CHECK(thread_key != "");

    cow_string raw_payload = request.at(&"raw_payload").as_string();
    POSEIDON_CHECK(raw_payload != "");

    POSEIDON_CHECK(impl->db_ready);

    ////////////////////////////////////////////////////////////
    //
    Chat_Thread thread;
    if(!impl->chat_threads.find_and_copy(thread, thread_key)) {
      // Load thread from MySQL. If no such thread exists, create a new one.
      static constexpr char select_from_chat[] =
          R"!!!(
            SELECT `whole`
              FROM `chat`
              WHERE `thread_key` = ?
          )!!!";

      cow_vector<::poseidon::MySQL_Value> sql_args;
      sql_args.emplace_back(thread_key.rdstr());     // WHERE `thread_key` = ?

      auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                                          &select_from_chat, sql_args);
      ::poseidon::task_scheduler.launch(task1);
      fiber.yield(task1);

      thread.thread_key = thread_key;
      thread.update_time = system_clock::now();

      if(task1->result_row_count() != 0)
        thread.parse_from_string(task1->result_row(0).at(0).as_blob());    // SELECT `whole`

      auto result = impl->chat_threads.try_emplace(thread_key, thread);
      if(!result.second)
        thread = result.first->second;  // load conflict
    }

    // Truncate the message list.
    if(thread.messages.size() > impl->max_number_of_messages_per_thread + 200)
      thread.messages.erase(0, thread.messages.size() - impl->max_number_of_messages_per_thread);

    // Append a new message. The timestamp is truncated to milliseconds to avoid
    // round-off errors.
    thread.update_time = system_clock::now();
    thread.messages.emplace_back(time_point_cast<milliseconds>(thread.update_time), raw_payload);
    impl->chat_threads.insert_or_assign(thread_key, thread);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_save_timer_callback(const shptr<Implementation>& impl,
                       const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                       ::poseidon::Abstract_Fiber& fiber, steady_time /*now*/)
  {
    if(impl->db_ready == false) {
      // Check tables.
      do_mysql_check_table_chat(fiber);
      impl->db_ready = true;
    }

    if(impl->save_buckets.empty()) {
      // Arrange online chat threads for writing. Initially, threads are divided
      // into 20 buckets. For each time tick, one bucket will be popped and
      // written.
      while(impl->save_buckets.size() < 20)
        impl->save_buckets.emplace_back();

      for(const auto& r : impl->chat_threads) {
        auto current_bucket = impl->save_buckets.begin();
        impl->save_buckets.splice(impl->save_buckets.end(), impl->save_buckets, current_bucket);
        if(current_bucket->size() >= current_bucket->capacity()) {
          ptrdiff_t sp = static_cast<ptrdiff_t>(current_bucket->capacity() / 2);
          impl->save_buckets.emplace_back(current_bucket->begin(), current_bucket->begin() + sp);
          current_bucket->erase(current_bucket->begin(), current_bucket->begin() + sp);
        }
        current_bucket->push_back(r.first);
      }
    }

    auto bucket = move(impl->save_buckets.back());
    impl->save_buckets.pop_back();
    while(!bucket.empty()) {
      phcow_string thread_key = move(bucket.back());
      bucket.pop_back();

      Chat_Thread thread;
      if(!impl->chat_threads.find_and_copy(thread, thread_key))
        continue;

      if(system_clock::now() - thread.update_time > impl->cached_thread_ttl)
        impl->chat_threads.erase(thread_key);

      // Truncate the message list.
      if(thread.messages.size() > impl->max_number_of_messages_per_thread)
        thread.messages.erase(0, thread.messages.size() - impl->max_number_of_messages_per_thread);

      // Write this thread to MySQL.
      static constexpr char replace_into_chat[] =
          R"!!!(
            REPLACE INTO `chat`
              SET `thread_key` = ?
                  , `update_time` = ?
                  , `whole` = ?
          )!!!";

      cow_vector<::poseidon::MySQL_Value> sql_args;
      sql_args.emplace_back(thread.thread_key.rdstr());      // SET `thread_key` = ?
      sql_args.emplace_back(thread.update_time);             //     , `update_time` = ?
      sql_args.emplace_back(thread.serialize_to_string());   //     , `whole` = ?

      auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                                          &replace_into_chat, sql_args);
      ::poseidon::task_scheduler.launch(task1);
      fiber.yield(task1);
    }
  }

}  // namespace

POSEIDON_HIDDEN_X_STRUCT(Chat_Service,
    Implementation);

Chat_Service::
Chat_Service()
  {
  }

Chat_Service::
~Chat_Service()
  {
  }

const cow_dictionary<Chat_Thread>&
Chat_Service::
all_chat_threads()
  const noexcept
  {
    if(!this->m_impl)
      return empty_chat_thread_map;

    return this->m_impl->chat_threads;
  }

const Chat_Thread&
Chat_Service::
find_chat_thread_opt(const phcow_string& thread_key)
  const noexcept
  {
    if(!this->m_impl)
      return Chat_Thread::null;

    auto ptr = this->m_impl->chat_threads.ptr(thread_key);
    if(!ptr)
      return Chat_Thread::null;

    return *ptr;
  }

void
Chat_Service::
reload(const ::poseidon::Config_File& conf_file)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // `chat.max_number_of_messages_per_thread`
    uint32_t max_number_of_messages_per_thread = static_cast<uint32_t>(conf_file.get_integer_opt(
                        &"chat.max_number_of_messages_per_thread", 1, 99999999).value_or(100));

    // `chat.cached_thread_ttl`
    seconds cached_thread_ttl = seconds(static_cast<int>(conf_file.get_integer_opt(
                        &"chat.cached_thread_ttl", 600, 999999999).value_or(900)));

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->max_number_of_messages_per_thread = max_number_of_messages_per_thread;
    this->m_impl->cached_thread_ttl = cached_thread_ttl;

    // Set up request handlers.
    service.set_handler(&"*chat/check_threads", bindw(this->m_impl, do_star_chat_check_threads));
    service.set_handler(&"*chat/save_message", bindw(this->m_impl, do_star_chat_save_message));

    // Restart the service.
    this->m_impl->save_timer.start(100ms, 11001ms, bindw(this->m_impl, do_save_timer_callback));
  }

}  // namespace k32::chat
