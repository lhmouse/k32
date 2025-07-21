# Table of Contents

1. [General Status Codes](#general-status-codes)
2. [Agent Service Opcodes](#agent-service-opcodes)
   1. [`*user/kick`](#userkick)
   2. [`*user/check_role`](#usercheck_role)
   3. [`*user/push_message`](#userpush_message)
   4. [`*user/reload_relay_conf`](#userreload_relay_conf)
   5. [`*user/ban/set`](#userbanset)
   6. [`*user/ban/lift`](#userbanlift)
   7. [`*nickname/acquire`](#nicknameacquire)
   8. [`*nickname/release`](#nicknamerelease)
3. [Monitor Service Opcodes](#monitor-service-opcodes)
   1. [`*role/list`](#rolelist)
   2. [`*role/create`](#rolecreate)
   3. [`*role/load`](#roleload)
   4. [`*role/unload`](#roleunload)
   5. [`*role/flush`](#roleflush)
4. [Logic Service Opcodes](#logic-service-opcodes)
   1. [`*role/login`](#rolelogin)
   2. [`*role/logout`](#rolelogout)
   3. [`*role/reconnect`](#rolereconnect)
   4. [`*role/disconnect`](#roledisconnect)
   5. [`*role/on_client_request`](#roleon_client_request)
   6. [`*clock/set_virtual_offset`](#clockset_virtual_offset)

## General Status Codes

Whenever `status` occurs as a response parameter, it may be one of the following
strings:

|Status Code                  |Description                                   |
|:----------------------------|:---------------------------------------------|
|`gs_ok`                      |Operation completed successfully.             |
|`gs_user_not_online`         |User not online.                              |
|`gs_user_not_found`          |User not found in database.                   |
|`gs_nickname_conflict`       |Nickname already exists in database.          |
|`gs_nickname_not_found`      |Nickname not found in database.               |
|`gs_roid_conflict`           |Role ID already exists in database.           |
|`gs_roid_not_found`          |Role ID not found in database.                |
|`gs_roid_not_match`          |Role ID not match.                            |
|`gs_service_uuid_not_match`  |Service UUID not match.                       |
|`gs_role_not_loaded`         |Role not loaded in Redis.                     |
|`gs_role_foreign`            |Role belongs to another server.               |
|`gs_role_not_logged_in`      |Role not logged in.                           |
|`gs_reconnect_noop`          |No role to reconnect.                         |
|`gs_role_handler_not_found`  |No handler for client opcode.                 |
|`gs_role_handler_except`     |Exception in handler for client opcode.       |

[back to table of contents](#table-of-contents)

## Agent Service Opcodes

### `*user/kick`

* Service Type
  - `"agent"`

* Request Parameters
  - `username` <sub>string</sub> : Name of user to kick.
  - `ws_status` <sub>integer, optional</sub> : WebSocket status code.
  - `reason` <sub>string, optional</sub> : Additional reason string.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Terminates the connection from a user, by sending a WebSocket closure
  notification of `ws_status` and `reason`. The default value for `ws_status` is
  `1008` (_Policy Violation_).

[back to table of contents](#table-of-contents)

### `*user/check_role`

* Service Type
  - `"agent"`

* Request Parameters
  - `username` <sub>string</sub> : Name of user to test.
  - `roid` <sub>integer</sub> : ID of current role.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Checks whether the specified user is online with the specified role.

[back to table of contents](#table-of-contents)

### `*user/push_message`

* Service Type
  - `"agent"`

* Request Parameters
  - `username` <sub>strings, optional</sub> : A single target user.
  - `username_list` <sub>array of strings, optional</sub> : List of target users.
  - `client_opcode` <sub>string</sub> : Opcode to send to clients.
  - `client_data` <sub>object, optional</sub> : Additional data for this opcode.

* Response Parameters
  - _None_

* Description
  Sends a message to all clients in `username` and `username_list`. If a user is
  not online on this service, they are silently ignored.

[back to table of contents](#table-of-contents)

### `*user/reload_relay_conf`

* Service Type
  - `"agent"`

* Request Parameters
  - _None_

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Reloads relay rules for client opcodes from `relay.conf`.

[back to table of contents](#table-of-contents)

### `*user/ban/set`

* Service Type
  - `"agent"`

* Request Parameters
  - `username` <sub>string</sub> : Name of user to ban.
  - `until` <sub>timestamp</sub> : Ban in effect until this time point.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Sets a ban on a user until a given time point. If the user is online, they are
  kicked with `reason`.

[back to table of contents](#table-of-contents)

### `*user/ban/lift`

* Service Type
  - `"agent"`

* Request Parameters
  - `username` <sub>string</sub> : Name of user to ban.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Lifts a ban on a user.

[back to table of contents](#table-of-contents)

### `*nickname/acquire`

* Service Type
  - `"agent"`

* Request Parameters
  - `nickname` <sub>string</sub> : Nickname to acquire.
  - `username` <sub>string</sub> : Owner of new nickname.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)
  - `serial` <sub>integer, optional</sub> : Serial number of new nickname.

* Description
  Attempts to acquire ownership of a nickname and returns its serial number.
  Both the nickname and the serial number are unique within the _user_ database.
  If the nickname already exists under the same username, the old serial
  number is returned. If the nickname already exists under a different username,
  no serial number is returned.

[back to table of contents](#table-of-contents)

### `*nickname/release`

* Service Type
  - `"agent"`

* Request Parameters
  - `nickname` <sub>string</sub> : Nickname to release.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Releases ownership of a nickname so it can be re-acquired by others.

[back to table of contents](#table-of-contents)

## Monitor Service Opcodes

### `*role/list`

* Service Type
  - `"monitor"`

* Request Parameters
  - `username` <sub>string</sub> : Owner of roles to list.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)
  - `raw_avatars` <sub>object of strings</sub> : Roles that have been found.
    - _key_ <sub>string</sub> : Role ID as decimal string.
    - _value_ <sub>string</sub> : Raw avatar data, encoded in JSON.

* Description
  Searches the _default_ database for all roles that belong to `username`, and
  returns their avatars. The result is not cached.

[back to table of contents](#table-of-contents)

### `*role/create`

* Service Type
  - `"monitor"`

* Request Parameters
  - `roid` <sub>integer</sub> : Unique ID of role to create.
  - `nickname` <sub>string</sub> : Nickname of new role.
  - `username` <sub>string</sub> : Owner of new role.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Creates a new role in the _default_ database. By design，the caller should
  call `*nickname/acquire` first to acquire ownership of `nickname`, then pass
  `serial` as `roid`. After a role is created, it will be loaded into Redis
  automatically.

[back to table of contents](#table-of-contents)

### `*role/load`

* Service Type
  - `"monitor"`

* Request Parameters
  - `roid` <sub>integer</sub> : ID of role to load.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Loads a role from the _default_ database into Redis. The monitor keeps track
  of roles that have been loaded by itself, and periodically writes snapshots
  from Redis back into the database.

[back to table of contents](#table-of-contents)

### `*role/unload`

* Service Type
  - `"monitor"`

* Request Parameters
  - `roid` <sub>integer</sub> : ID of role to unload.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Writes a role back into the database and unloads it from Redis.

[back to table of contents](#table-of-contents)

### `*role/flush`

* Service Type
  - `"monitor"`

* Request Parameters
  - `roid` <sub>integer</sub> : ID of role to flush.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Writes a role back into the database.

[back to table of contents](#table-of-contents)

## Logic Service Opcodes

### `*role/login`

* Service Type
  - `"logic"`

* Request Parameters
  - `roid` <sub>integer</sub> : ID of role to load.
  - `agent_srv` <sub>string</sub> : UUID of _agent_ that holds client connection.
  - `monitor_srv` <sub>string</sub> : UUID of _monitor_ that holds client data.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Loads a role from Redis and triggers a _login_ event. If the role has not been
  loaded into Redis, this operation fails.

[back to table of contents](#table-of-contents)

### `*role/logout`

* Service Type
  - `"logic"`

* Request Parameters
  - `roid` <sub>integer</sub> : ID of role to unload.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Triggers a _logout_ event, writes the role back to Redis, and unloads it.

[back to table of contents](#table-of-contents)

### `*role/reconnect`

* Service Type
  - `"logic"`

* Request Parameters
  - `roid_list` <sub>array of integers</sub> : List of IDs of roles to check.
  - `agent_srv` <sub>string</sub> : UUID of _agent_ that holds client connection.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)
  - `roid` <sub>integer</sub> : ID of role that has reconnected.

* Description
  If a role in `roid_list` has been loaded, triggers a _reconnect_ event.
  Otherwise no role is loaded, and an error is returned.

[back to table of contents](#table-of-contents)

### `*role/disconnect`

* Service Type
  - `"logic"`

* Request Parameters
  - `roid` <sub>integer</sub> : ID of role to disconnect.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Triggers a _disconnect_ event.

[back to table of contents](#table-of-contents)

### `*role/on_client_request`

* Service Type
  - `"logic"`

* Request Parameters
  - `roid` <sub>integer</sub> : ID of role on this client.
  - `client_opcode` <sub>string</sub> : Opcode from client.
  - `client_req` <sub>object, optional</sub> : Additional data for this opcode.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)
  - `client_resp` <sub>object, optional</sub> : Additional response data.

* Description
  Handles a request message from a client.

[back to table of contents](#table-of-contents)

### `*clock/set_virtual_offset`

* Service Type
  - `"logic"`

* Request Parameters
  - `offset` <sub>integer</sub> : Virtual offset in seconds.

* Response Parameters
  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description
  Sets virtual offset of the logic clock.

[back to table of contents](#table-of-contents)
