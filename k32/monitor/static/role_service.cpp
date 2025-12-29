// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_3543B0B1_DC5A_4F34_B9BB_CAE513821771_
#include "role_service.hpp"
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
#include <list>
namespace k32::monitor {
namespace {

const cow_int64_dictionary<Role_Record> empty_role_record_map;

struct Implementation
  {
    seconds redis_role_ttl;

    ::poseidon::Easy_Timer save_timer;

    // remote data from mysql
    bool db_ready = false;
    cow_int64_dictionary<Role_Record> role_records;
    ::std::list<static_vector<int64_t, 255>> save_buckets;
  };

void
do_mysql_check_table_role(::poseidon::Abstract_Fiber& fiber)
  {
    ::poseidon::MySQL_Table_Structure table;
    table.name = &"role";
    table.engine = ::poseidon::mysql_engine_innodb;

    ::poseidon::MySQL_Table_Column column;
    column.name = &"roid";
    column.type = ::poseidon::mysql_column_int64;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"username";
    column.type = ::poseidon::mysql_column_varchar;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"nickname";
    column.type = ::poseidon::mysql_column_varchar;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"update_time";
    column.type = ::poseidon::mysql_column_datetime;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"avatar";
    column.type = ::poseidon::mysql_column_blob;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"profile";
    column.type = ::poseidon::mysql_column_blob;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"whole";
    column.type = ::poseidon::mysql_column_blob;
    table.columns.emplace_back(column);

    ::poseidon::MySQL_Table_Index index;
    index.name = &"PRIMARY";
    index.type = ::poseidon::mysql_index_unique;
    index.columns.emplace_back(&"roid");
    table.indexes.emplace_back(index);

    index.clear();
    index.name = &"username";
    index.type = ::poseidon::mysql_index_multi;
    index.columns.emplace_back(&"username");
    table.indexes.emplace_back(index);

    index.clear();
    index.name = &"nickname";
    index.type = ::poseidon::mysql_index_multi;
    index.columns.emplace_back(&"nickname");
    table.indexes.emplace_back(index);

    // This is in the default database.
    auto task = new_sh<::poseidon::MySQL_Check_Table_Future>(::poseidon::mysql_connector, table);
    ::poseidon::task_scheduler.launch(task);
    fiber.yield(task);
    POSEIDON_LOG_INFO(("Finished verification of MySQL table `$1`"), table.name);
  }

void
do_store_role_record_into_redis(::poseidon::Abstract_Fiber& fiber, Role_Record& roinfo, seconds ttl)
  {
    cow_vector<cow_string> redis_cmd;
    redis_cmd.emplace_back(&"SET");
    redis_cmd.emplace_back(sformat("$1/role/$2", service.application_name(), roinfo.roid));
    redis_cmd.emplace_back(roinfo.serialize_to_string());
    redis_cmd.emplace_back(&"NX");  // no replace
    redis_cmd.emplace_back(&"GET");
    redis_cmd.emplace_back(&"EX");
    redis_cmd.emplace_back(sformat("$1", ttl.count()));

    auto task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);

    if(!task2->result().is_nil())
      roinfo.parse_from_string(task2->result().as_string());

    POSEIDON_LOG_INFO(("Loaded from MySQL: role `$1` (`$2`), updated on `$3`"),
                      roinfo.roid, roinfo.nickname, roinfo.update_time);
  }

void
do_star_role_list(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                  const ::poseidon::UUID& /*request_service_uuid*/,
                  ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    // * Request Parameters
    //
    //   - `roid` <sub>integer</sub> : Unique ID of role to create.
    //   - `nickname` <sub>string</sub> : Nickname of new role.
    //   - `username` <sub>string</sub> : Owner of new role.
    //
    // * Response Parameters
    //
    //   - `status` <sub>string</sub> : [General status code.](#general-status-codes)
    //
    // * Description
    //
    //   Creates a new role in the _default_ database. By design，the caller should
    //   call `*nickname/acquire` first to acquire ownership of `nickname`, then pass
    //   `serial` as `roid`. After a role is created, it will be loaded into Redis
    //   automatically.

    ////////////////////////////////////////////////////////////
    //
    phcow_string username = request.at(&"username").as_string();
    POSEIDON_CHECK(username != "");

    POSEIDON_CHECK(impl->db_ready);

    ////////////////////////////////////////////////////////////
    //
    static constexpr char select_avatar_from_role[] =
        R"!!!(
          SELECT `roid`
                 , `avatar`
            FROM `role`
            WHERE `username` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(username.rdstr());       // WHERE `username` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                                        &select_avatar_from_role, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);

    ::std::vector<Role_Record> db_records;
    for(const auto& row : task1->result_rows()) {
      Role_Record roinfo;
      roinfo.roid = row.at(0).as_integer();      // SELECT `roid`
      roinfo.avatar = row.at(1).as_blob();       //        , `avatar`
      db_records.emplace_back(move(roinfo));
    }

    POSEIDON_LOG_INFO(("Found $1 role(s) for user `$2`"), db_records.size(), username);

    if(db_records.size() > 0) {
      // See whether Redis contains unflushed role records.
      cow_vector<cow_string> redis_cmd;
      redis_cmd.emplace_back(&"MGET");
      for(size_t k = 0;  k < db_records.size();  ++k)
        redis_cmd.emplace_back(sformat("$1/role/$2", service.application_name(), db_records.at(k).roid));

      auto task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
      ::poseidon::task_scheduler.launch(task2);
      fiber.yield(task2);

      for(size_t k = 0;  k < db_records.size();  ++k)
        if(!task2->result().as_array().at(k).is_nil())
          db_records.at(k).parse_from_string(task2->result().as_array().at(k).as_string());
    }

    // Encode avatars in an object, and return it.
    ::taxon::V_object raw_avatars;
    for(const auto& roinfo : db_records)
      raw_avatars.try_emplace(sformat("$1", roinfo.roid), roinfo.avatar);

    response.try_emplace(&"raw_avatars", raw_avatars);
    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_role_create(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                    const ::poseidon::UUID& /*request_service_uuid*/,
                    ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    // * Request Parameters
    //
    //   - `roid` <sub>integer</sub> : Unique ID of role to create.
    //   - `nickname` <sub>string</sub> : Nickname of new role.
    //   - `username` <sub>string</sub> : Owner of new role.
    //
    // * Response Parameters
    //
    //   - `status` <sub>string</sub> : [General status code.](#general-status-codes)
    //
    // * Description
    //
    //   Creates a new role in the _default_ database. By design，the caller should
    //   call `*nickname/acquire` first to acquire ownership of `nickname`, then pass
    //   `serial` as `roid`. After a role is created, it will be loaded into Redis
    //   automatically.

    ////////////////////////////////////////////////////////////
    //
    int64_t roid = request.at(&"roid").as_integer();
    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    phcow_string username = request.at(&"username").as_string();
    POSEIDON_CHECK(username != "");

    cow_string nickname = request.at(&"nickname").as_string();
    POSEIDON_CHECK(nickname != "");

    POSEIDON_CHECK(impl->db_ready);

    ////////////////////////////////////////////////////////////
    //
    Role_Record roinfo;
    roinfo.roid = roid;
    roinfo.username = username;
    roinfo.nickname = nickname;
    roinfo.update_time = system_clock::now();

    auto mysql_conn = ::poseidon::mysql_connector.allocate_default_connection();
    roinfo._home_host = ::poseidon::hostname;
    roinfo._home_db = mysql_conn->service_uri();
    roinfo._home_zone = service.zone_id();

    static constexpr char insert_into_role[] =
        R"!!!(
          INSERT IGNORE INTO `role`
            SET `roid` = ?
                , `username` = ?
                , `nickname` = ?
                , `update_time` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(roinfo.roid);               // SET `roid` = ?
    sql_args.emplace_back(roinfo.username.rdstr());   //     , `username` = ?
    sql_args.emplace_back(roinfo.nickname);           //     , `nickname` = ?
    sql_args.emplace_back(roinfo.update_time);        //     , `update_time` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                               move(mysql_conn), &insert_into_role, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);

    if(task1->match_count() == 0) {
      // In this case, we still want to load a role if it belongs to the same
      // user. This makes the operation retryable.
      static constexpr char select_from_role[] =
          R"!!!(
            SELECT `nickname`
                   , `update_time`
                   , `avatar`
                   , `profile`
                   , `whole`
              FROM `role`
              WHERE `roid` = ?
                    AND `username` = ?
          )!!!";

      sql_args.clear();
      sql_args.emplace_back(roid);                // WHERE `roid` = ?
      sql_args.emplace_back(username.rdstr());    //       AND `username` = ?

      task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                                     &select_from_role, sql_args);
      ::poseidon::task_scheduler.launch(task1);
      fiber.yield(task1);

      if(task1->result_row_count() == 0) {
        response.try_emplace(&"status", &"gs_roid_conflict");
        return;
      }

      roinfo.nickname = task1->result_row(0).at(0).as_blob();            // SELECT `nickname`
      roinfo.update_time = task1->result_row(0).at(1).as_system_time();  //        , `update_time`
      roinfo.avatar = task1->result_row(0).at(2).as_blob();              //        , `avatar`
      roinfo.profile = task1->result_row(0).at(3).as_blob();             //        , `profile`
      roinfo.whole = task1->result_row(0).at(4).as_blob();               //        , `whole`
    }

    do_store_role_record_into_redis(fiber, roinfo, impl->redis_role_ttl);
    impl->role_records.insert_or_assign(roinfo.roid, roinfo);

    POSEIDON_LOG_INFO(("Created role `$1` (`$2`)"), roinfo.roid, roinfo.nickname);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_role_load(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                  const ::poseidon::UUID& /*request_service_uuid*/,
                  ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    // * Request Parameters
    //
    //   - `roid` <sub>integer</sub> : ID of role to load.
    //
    // * Response Parameters
    //
    //   - `status` <sub>string</sub> : [General status code.](#general-status-codes)
    //
    // * Description
    //
    //   Loads a role from the _default_ database into Redis. The monitor keeps track
    //   of roles that have been loaded by itself, and periodically writes snapshots
    //   from Redis back into the database.

    ////////////////////////////////////////////////////////////
    //
    int64_t roid = request.at(&"roid").as_integer();
    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    POSEIDON_CHECK(impl->db_ready);

    ////////////////////////////////////////////////////////////
    //
    Role_Record roinfo;
    roinfo.roid = roid;

    auto mysql_conn = ::poseidon::mysql_connector.allocate_default_connection();
    roinfo._home_host = ::poseidon::hostname;
    roinfo._home_db = mysql_conn->service_uri();
    roinfo._home_zone = service.zone_id();

    static constexpr char select_from_role[] =
        R"!!!(
          SELECT `username`
                 , `nickname`
                 , `update_time`
                 , `avatar`
                 , `profile`
                 , `whole`
            FROM `role`
            WHERE `roid` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(roinfo.roid);    // WHERE `roid` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                               move(mysql_conn), &select_from_role, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);

    if(task1->result_row_count() == 0) {
      response.try_emplace(&"status", &"gs_roid_not_found");
      return;
    }

    roinfo.username = task1->result_row(0).at(0).as_blob();            // SELECT `username`
    roinfo.nickname = task1->result_row(0).at(1).as_blob();            //        , `nickname`
    roinfo.update_time = task1->result_row(0).at(2).as_system_time();  //        , `update_time`
    roinfo.avatar = task1->result_row(0).at(3).as_blob();              //        , `avatar`
    roinfo.profile = task1->result_row(0).at(4).as_blob();             //        , `profile`
    roinfo.whole = task1->result_row(0).at(5).as_blob();               //        , `whole`

    do_store_role_record_into_redis(fiber, roinfo, impl->redis_role_ttl);
    impl->role_records.insert_or_assign(roinfo.roid, roinfo);

    POSEIDON_LOG_INFO(("Loaded role `$1` (`$2`)"), roinfo.roid, roinfo.nickname);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_store_role_record_into_mysql(::poseidon::Abstract_Fiber& fiber,
                                uniptr<::poseidon::MySQL_Connection>&& mysql_conn_opt,
                                Role_Record& roinfo)
  {
    POSEIDON_LOG_INFO(("#sav# Storing into MySQL: role `$1` (`$2`), updated on `$3`"),
                      roinfo.roid, roinfo.nickname, roinfo.update_time);

    static constexpr char update_role[] =
        R"!!!(
          UPDATE `role`
            SET `username` = ?
                , `nickname` = ?
                , `update_time` = ?
                , `avatar` = ?
                , `profile` = ?
                , `whole` = ?
            WHERE `roid` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(roinfo.username.rdstr());   // SET `username` = ?
    sql_args.emplace_back(roinfo.nickname);           //     , `nickname` = ?
    sql_args.emplace_back(roinfo.update_time);        //     , `update_time` = ?
    sql_args.emplace_back(roinfo.avatar);             //     , `avatar` = ?
    sql_args.emplace_back(roinfo.profile);            //     , `profile` = ?
    sql_args.emplace_back(roinfo.whole);              //     , `whole` = ?
    sql_args.emplace_back(roinfo.roid);               // WHERE `roid` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                                        move(mysql_conn_opt), &update_role, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);

    POSEIDON_LOG_INFO(("#sav# Stored into MySQL: role `$1` (`$2`), updated on `$3`"),
                      roinfo.roid, roinfo.nickname, roinfo.update_time);
  }

void
do_star_role_unload(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                    const ::poseidon::UUID& /*request_service_uuid*/,
                    ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    // * Request Parameters
    //
    //   - `roid` <sub>integer</sub> : ID of role to unload.
    //
    // * Response Parameters
    //
    //   - `status` <sub>string</sub> : [General status code.](#general-status-codes)
    //
    // * Description
    //
    //   Writes a role back into the database and unloads it from Redis.

    ////////////////////////////////////////////////////////////
    //
    int64_t roid = request.at(&"roid").as_integer();
    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    POSEIDON_CHECK(impl->db_ready);

    ////////////////////////////////////////////////////////////
    //
    cow_vector<cow_string> redis_cmd;
    redis_cmd.emplace_back(&"GET");
    redis_cmd.emplace_back(sformat("$1/role/$2", service.application_name(), roid));

    auto task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);

    if(task2->result().is_nil()) {
      impl->role_records.erase(roid);
      response.try_emplace(&"status", &"gs_role_not_loaded");
      return;
    }

    // Write role information to MySQL. This is a slow operation, and data may
    // change when it is being executed. Therefore we will have to verify that
    // the value on Redis is unchanged before deleting it safely.
    Role_Record roinfo;
    do {
      roinfo.parse_from_string(task2->result().as_string());

      auto mysql_conn = ::poseidon::mysql_connector.allocate_default_connection();
      if((roinfo._home_host != ::poseidon::hostname) || (roinfo._home_db != mysql_conn->service_uri())) {
        ::poseidon::mysql_connector.pool_connection(move(mysql_conn));
        response.try_emplace(&"status", &"gs_role_foreign");
        return;
      }

      do_store_role_record_into_mysql(fiber, move(mysql_conn), roinfo);

      static constexpr char redis_delete_if_unchanged[] =
          R"!!!(
            local value = redis.call('GET', KEYS[1])
            if value == ARGV[1] then
              redis.call('DEL', KEYS[1])
              return nil
            else
              return value
            end
          )!!!";

      redis_cmd.clear();
      redis_cmd.emplace_back(&"EVAL");
      redis_cmd.emplace_back(&redis_delete_if_unchanged);
      redis_cmd.emplace_back(&"1");   // one key
      redis_cmd.emplace_back(sformat("$1/role/$2", service.application_name(), roinfo.roid));  // KEYS[1]
      redis_cmd.emplace_back(task2->result().as_string());  // ARGV[1]

      task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
      ::poseidon::task_scheduler.launch(task2);
      fiber.yield(task2);
    } while(!task2->result().is_nil());
    impl->role_records.erase(roid);

    POSEIDON_LOG_INFO(("Unloaded role `$1` (`$2`)"), roinfo.roid, roinfo.nickname);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_role_flush(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber,
                   const ::poseidon::UUID& /*request_service_uuid*/,
                   ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    // * Request Parameters
    //
    //   - `roid` <sub>integer</sub> : ID of role to flush.
    //
    // * Response Parameters
    //
    //   - `status` <sub>string</sub> : [General status code.](#general-status-codes)
    //
    // * Description
    //
    //   Writes a role back into the database.

    ////////////////////////////////////////////////////////////
    //
    int64_t roid = request.at(&"roid").as_integer();
    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    POSEIDON_CHECK(impl->db_ready);

    ////////////////////////////////////////////////////////////
    //
    POSEIDON_LOG_INFO(("#sav# Flushing role `$1`"), roid);

    cow_vector<cow_string> redis_cmd;
    redis_cmd.emplace_back(&"GET");
    redis_cmd.emplace_back(sformat("$1/role/$2", service.application_name(), roid));

    auto task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);

    if(task2->result().is_nil()) {
      impl->role_records.erase(roid);
      response.try_emplace(&"status", &"gs_role_not_loaded");
      return;
    }

    // Write a snapshot of role information to MySQL.
    Role_Record roinfo;
    roinfo.parse_from_string(task2->result().as_string());

    auto mysql_conn = ::poseidon::mysql_connector.allocate_default_connection();
    if((roinfo._home_host != ::poseidon::hostname) || (roinfo._home_db != mysql_conn->service_uri())) {
      ::poseidon::mysql_connector.pool_connection(move(mysql_conn));
      response.try_emplace(&"status", &"gs_role_foreign");
      return;
    }

    impl->role_records.insert_or_assign(roinfo.roid, roinfo);
    do_store_role_record_into_mysql(fiber, move(mysql_conn), roinfo);

    POSEIDON_LOG_INFO(("Flushed role `$1` (`$2`)"), roinfo.roid, roinfo.nickname);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_save_timer_callback(const shptr<Implementation>& impl,
                       const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                       ::poseidon::Abstract_Fiber& fiber, steady_time /*now*/)
  {
    if(impl->db_ready == false) {
      // Check tables.
      do_mysql_check_table_role(fiber);
      impl->db_ready = true;
    }

    if(impl->save_buckets.empty()) {
      // Arrange online roles for writing. Initially, users are divided into 20
      // buckets. For each timer tick, one bucket will be popped and written.
      while(impl->save_buckets.size() < 20)
        impl->save_buckets.emplace_back();

      for(const auto& r : impl->role_records) {
        auto current_bucket = impl->save_buckets.begin();
        impl->save_buckets.splice(impl->save_buckets.end(), impl->save_buckets, current_bucket);
        if(current_bucket->size() >= current_bucket->capacity()) {
          ptrdiff_t sp = static_cast<ptrdiff_t>(current_bucket->capacity() / 2);
          impl->save_buckets.emplace_back(current_bucket->move_begin(), current_bucket->move_begin() + sp);
          current_bucket->erase(current_bucket->begin(), current_bucket->begin() + sp);
        }
        current_bucket->push_back(r.first);
      }
    }

    auto bucket = move(impl->save_buckets.back());
    impl->save_buckets.pop_back();
    while(!bucket.empty()) {
      int64_t roid = bucket.back();
      bucket.pop_back();

      cow_vector<cow_string> redis_cmd;
      redis_cmd.emplace_back(&"GET");
      redis_cmd.emplace_back(sformat("$1/role/$2", service.application_name(), roid));

      auto task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
      ::poseidon::task_scheduler.launch(task2);
      fiber.yield(task2);

      if(task2->result().is_nil()) {
        impl->role_records.erase(roid);
        continue;
      }

      // Write a snapshot of role information to MySQL.
      Role_Record roinfo;
      roinfo.parse_from_string(task2->result().as_string());

      impl->role_records.insert_or_assign(roinfo.roid, roinfo);
      do_store_role_record_into_mysql(fiber, nullptr, roinfo);
    }
  }

}  // namespace

POSEIDON_HIDDEN_X_STRUCT(Role_Service,
    Implementation);

Role_Service::
Role_Service()
  {
  }

Role_Service::
~Role_Service()
  {
  }

const cow_int64_dictionary<Role_Record>&
Role_Service::
all_role_records()
  const noexcept
  {
    if(!this->m_impl)
      return empty_role_record_map;

    return this->m_impl->role_records;
  }

const Role_Record&
Role_Service::
find_role_record_opt(int64_t roid)
  const noexcept
  {
    if(!this->m_impl)
      return Role_Record::null;

    auto ptr = this->m_impl->role_records.ptr(roid);
    if(!ptr)
      return Role_Record::null;

    return *ptr;
  }

void
Role_Service::
reload(const ::poseidon::Config_File& conf_file)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // `redis_role_ttl`
    seconds redis_role_ttl = seconds(static_cast<int>(conf_file.get_integer_opt(
                                    &"redis_role_ttl", 600, 999999999).value_or(900)));

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->redis_role_ttl = redis_role_ttl;

    // Set up request handlers.
    service.set_handler(&"*role/list", bindw(this->m_impl, do_star_role_list));
    service.set_handler(&"*role/create", bindw(this->m_impl, do_star_role_create));
    service.set_handler(&"*role/load", bindw(this->m_impl, do_star_role_load));
    service.set_handler(&"*role/unload", bindw(this->m_impl, do_star_role_unload));
    service.set_handler(&"*role/flush", bindw(this->m_impl, do_star_role_flush));

    // Restart the service.
    this->m_impl->save_timer.start(100ms, 11001ms, bindw(this->m_impl, do_save_timer_callback));
  }

}  // namespace k32::monitor
