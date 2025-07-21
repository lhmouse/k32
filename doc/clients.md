# Table of Contents

1. [Connection Establishment](#connection-establishment)
2. [Connection Termination](#connection-termination)
3. [Message Formats](#message-formats)
4. [Client-to-Server Opcodes](#client-to-server-opcodes)
   1. [`+role/create`](#rolecreate)
   2. [`+role/login`](#rolelogin)
   3. [`+role/logout`](#rolelogout)
5. [Server-to-Client Opcodes](#server-to-client-opcodes)
   1. [`=role/list`](#rolelist)
   2. [`=role/login`](#rolelogin-1)

## Connection Establishment

A client shall establish a plain, non-SSL/TLS WebSocket connection to one of the
ports of the _agent_ process, as specified in `etc/k32.conf`:

```
agent
{
  client_port_list = [ 3801, 3802, 3803 ]
}
```

The request URI is used to authenticate the client.

[back to table of contents](#table-of-contents)

## Connection Termination

A client may terminate a connection at any time, with or without a WebSocket
closure notification. A WebSocket status code that is sent by a client has no
meaning and is ignored.

A server may terminate a connection at any time. Whenever possible, a server
should send a WebSocket closure notification. In addition to
[well-known status codes](iana.org/assignments/websocket/websocket.xhtml), these
status codes are defined:

```
user_ws_status_authentication_failure    = 4301,
user_ws_status_login_conflict            = 4302,
user_ws_status_unknown_opcode            = 4303,
user_ws_status_message_rate_limit        = 4304,
user_ws_status_ping_timeout              = 4305,
user_ws_status_ban                       = 4306,
```

[back to table of contents](#table-of-contents)

## Message Formats

Both a client and a server shall send messages as JSON objects, encoded as
either text or binary WebSocket frames. Fields of client-to-server messages are
defined as follows:

* `%opcode` <sub>string, required</sub> : Functionality of this message.
* `%serial` <sub>any value, optional</sub> : An arbitrary value which, if not
  `null`, will be echoed back to the client in a subsequent response message.

Fields of server-to-client messages are defined as follows:

* `%opcode` <sub>string, optional</sub> : Functionality of this message. This is
  only set when a server sends a message actively; when responding to a previous
  request message from the client, this field is not set.
* `%serial` <sub>any value, optional</sub> : If a previous message from the
  client had `serial`, this echos the value back, maintaining a one-to-one
  relationship between a request-response message pair. The server shall never
  send both `serial` and `opcode`.

Field names without `%` are free for any protocol-specific use.

[back to table of contents](#table-of-contents)

## Client-to-Server Opcodes

Whenever `status` occurs as a response parameter, it may be one of the following
strings:

|Status Code                 |Description                                    |
|:---------------------------|:----------------------------------------------|
|`sc_ok`                     |Operation completed successfully.              |
|`sc_opcode_denied`          |Opcode temporarily denied.                     |
|`sc_role_selected`          |Role is already selected and online.           |
|`sc_no_role_selected`       |No role selected.                              |
|`sc_role_unavailable`       |Role not available to current user.            |
|`sc_nickname_invalid`       |Nickname not valid.                            |
|`sc_nickname_length_error`  |Nickname length out of range.                  |
|`sc_nickname_conflict`      |Nickname already exists.                       |
|`sc_too_many_roles`         |Max number of roles exceeded.                  |
|`sc_role_creation_failure`  |Could not create role; internal error.         |

[back to table of contents](#table-of-contents)

### `+role/create`

* Request Parameters
  - `nickname` <sub>string</sub> : Nickname of new role.

* Response Parameters
  - `status` <sub>string</sub> : [General client-to-server status code.](#client-to-server-opcodes)

* Description
  Creates a new role, and logs into the new role automatically.

[back to table of contents](#table-of-contents)

### `+role/login`

* Request Parameters
  - `roid` <sub>number</sub> : Unique ID of role to log into.

* Response Parameters
  - `status` <sub>string</sub> : [General client-to-server status code.](#client-to-server-opcodes)

* Description
  Selects a role to log into, which shall be one of the roles that were returned
  in an earlier `=role/list` notification.

[back to table of contents](#table-of-contents)

### `+role/logout`

* Request Parameters
  - _None_

* Response Parameters
  - `status` <sub>string</sub> : [General client-to-server status code.](#client-to-server-opcodes)

* Description
  Logs out from a role. After logging out, the server will send a new role list.

[back to table of contents](#table-of-contents)

## Server-to-Client Opcodes

### `=role/list`

* Notification Parameters
  - `avatar_list` <sub>array of objects</sub> : List of available roles.

* Description
  This notification is sent after a user is authenticated successfully but no
  role is currently online. The client may choose to log into an existing role
  from `avatar_list`, or to create a new role.

[back to table of contents](#table-of-contents)

### `=role/login`

* Notification Parameters
  - TODO

* Description
  This notification is sent when the user, either selects a role to log into, or
  reconnects to the server while a role is already online.

[back to table of contents](#table-of-contents)
