# Architecture

This note describes how the implementation is put together. It assumes the reader knows the
protocol; the section numbers refer to IPC-HERMES-9852, version 1.6.

## Layers

```
include/                 public API and protocol vocabulary
  version.hxx            the constants of the standard (ports, sizes, route/action values)
  error.hxx              error_info / error_code
  enums.hxx              every enumerator of the standard, with its wire spelling
  types.hxx              sub_board, the feature sets, the configuration list entries, connection_info
  messages.hxx           one struct per message, the message traits, any_message
  xml.hxx                envelope, to_xml(), parse()
  settings.hxx           the settings aggregates of the facades
  channel.hxx            the callback types
  downstream.hxx         the server interface (board provider)
  upstream.hxx           the client interface (board receiver)
  vertical_service.hxx   the machine side of the vertical channel
  vertical_client.hxx    the supervisory system side
  configuration_service.hxx   the port 1248 service
  configuration_client.hxx    the synchronous client functions

src/
  xml.cxx                the wire codec, field by field
  framing.cxx            turning a byte stream back into documents
  net.hxx / net.cxx      io_context runner, framed connection, connector, acceptor
  horizontal.cxx         the state chart of Figure 19 and a horizontal session
  vertical.cxx           the state chart of Figure 22 and a vertical session
  configuration.cxx      a configuration session
  downstream.cxx         the facade, the session registry and the listener
  upstream.cxx           the facade, the connector and the reconnect loop
  vertical_service.cxx   the facade and the session registry
  vertical_client.cxx    the facade and the reconnect loop
  configuration_service.cxx   the facade and the session registry
  configuration_client.cxx    the blocking client functions
```

Every public facade hides its implementation behind `struct impl`; the headers only pull in the
protocol vocabulary, `<asio.hpp>` is not part of the public API.

## Messages and the wire format

Each message is a plain aggregate. A mandatory attribute of the standard is a plain member, an
optional one is a `std::optional`, a list is a `std::vector`. The member names are the library's
own snake_case names; the mapping back to the XML spelling lives in exactly one place, `src/xml.cxx`,
where each message has a `write()` and a `read()` function.

* Attributes are written in the order of the standard's tables, which keeps documents stable and
  easy to diff.
* An optional member that is empty is left out; a mandatory member is always written.
* The lists are elements: `<SupportedFeatures>` with one child per feature, `<SubBoards>` with one
  `<SB>` per subboard, `<UpstreamConfigurations>` with one `<UpstreamConfiguration>` per entry, and
  so on. The reader also accepts configuration entries without their wrapper element, because
  implementations differ in that detail.
* Unknown attributes and unknown child elements are ignored. A message element this implementation
  does not know is reported as `error_code::UNSUPPORTED`; the transport drops it with a warning.
* The envelope may carry a `Timestamp`. `parse()` exposes it, `to_xml(envelope)` writes it; the
  transport stamps every document it sends. The XML declaration is omitted: UTF-8 is the only
  encoding the standard allows.
* A document that would exceed 65 536 bytes because of its `SubBoards` list is serialized again
  without the list, which the standard allows for machines with limited memory (3.6).
* Floating point values are written with the shortest representation that reads back unchanged;
  enums travel as their integer value; an unknown value of an optional enum is discarded, an
  unknown value of a mandatory enum is an `error_code::PEER` error.

## Transport

`detail::connection` owns one connected socket. It runs one read loop and one write queue:

* The read loop appends every read to a `frame_extractor` and hands each complete document to the
  session. The extractor scans for the closing `</Hermes>` at depth zero, skipping declarations,
  comments, CDATA sections and processing instructions, so a document may be split anywhere. It
  rejects a stream whose first element is not `<Hermes>` and a document above `MAX_MESSAGE_SIZE`.
* The write queue serializes `asio::async_write` operations. A document that is queued while
  another one is in flight waits for it, which preserves the order the application sent them in.
  The queue is only released after the in-flight write completed; clearing it earlier would leave
  asio with a dangling buffer.
* `send_and_close()` queues a last document (the `Notification` of a protocol error or of a
  shutdown request) and closes the socket once it was written.

`detail::connector` resolves and connects, retries after `reconnect_delay` on failure and is
restarted by the client facade after an established connection was lost. `detail::acceptor` binds,
listens and hands every accepted socket to the facade together with the peer's `connection_info`.

## Sessions and state charts

A session owns the protocol state of one connection. It performs the handshake, validates every
document against the state chart, answers `CheckAlive` pings, marks the current state atomically and
reports state changes, messages, traces and the disconnect to the facade. Both horizontal facades
share `detail::horizontal_session`; the `horizontal_role` decides which messages the interface may
send and which it expects to receive.

The handshake is automatic:

* the upstream (client) interface sends its `ServiceDescription` as soon as the socket is up;
* the downstream (server) interface answers with its own as soon as the peer's description arrived;
* both start pinging in state `NOT_AVAILABLE_NOT_READY`.

The transition table of Figure 19 is encoded in `next_state(channel_state, message name)`:

| From | Message | To |
| --- | --- | --- |
| `SOCKET_CONNECTED` | `ServiceDescription` | `SERVICE_DESCRIPTION_DOWNSTREAM` |
| `SERVICE_DESCRIPTION_DOWNSTREAM` | `ServiceDescription` | `NOT_AVAILABLE_NOT_READY` |
| `NOT_AVAILABLE_NOT_READY` | `BoardAvailable` | `BOARD_AVAILABLE` |
| `NOT_AVAILABLE_NOT_READY` | `MachineReady` | `MACHINE_READY` |
| `BOARD_AVAILABLE` | `RevokeBoardAvailable` | `NOT_AVAILABLE_NOT_READY` |
| `BOARD_AVAILABLE` | `MachineReady` | `AVAILABLE_AND_READY` |
| `MACHINE_READY` | `RevokeMachineReady` | `NOT_AVAILABLE_NOT_READY` |
| `MACHINE_READY` | `BoardAvailable` | `AVAILABLE_AND_READY` |
| `MACHINE_READY` | `StartTransport` | `TRANSPORTING` |
| `AVAILABLE_AND_READY` | `RevokeBoardAvailable` | `MACHINE_READY` |
| `AVAILABLE_AND_READY` | `RevokeMachineReady` | `BOARD_AVAILABLE` |
| `AVAILABLE_AND_READY` | `StartTransport` | `TRANSPORTING` |
| `TRANSPORTING` | `TransportFinished` | `TRANSPORT_FINISHED` |
| `TRANSPORTING` | `StopTransport` | `TRANSPORT_STOPPED` |
| `TRANSPORTING` | `BoardAvailable`, `RevokeBoardAvailable` | `TRANSPORTING` (race, 2.3.6) |
| `TRANSPORT_STOPPED` | `TransportFinished` | `NOT_AVAILABLE_NOT_READY` |
| `TRANSPORT_STOPPED` | `BoardAvailable`, `RevokeBoardAvailable` | `TRANSPORT_STOPPED` (race) |
| `TRANSPORT_FINISHED` | `StopTransport` | `NOT_AVAILABLE_NOT_READY` |

`Notification`, `CheckAlive`, `Command`, `QueryBoardInfo` and `SendBoardInfo` may travel in any
state without a transition. `BoardForecast` is accepted in `NOT_AVAILABLE_NOT_READY` and
`MACHINE_READY` and ignored elsewhere. A state-bearing message without a transition, a message of
the wrong direction, or a `BoardId` of `StopTransport`/`TransportFinished` that does not match the
one of `StartTransport` is a protocol error: the session sends a `Notification` with code
`PROTOCOL_ERROR`, severity `FATAL`, and terminates the connection. A `StartTransport` whose
`BoardId` does not match the last `BoardAvailable` is not a protocol error (3.10); the session
reports a warning trace and leaves the decision to the application.

While a downstream session is established, a further connection is not turned into a session: it
is answered with a `Notification` of code `CONNECTION_REFUSED` and closed, while the established
connection is retained, as 2.3.2 asks for.

The vertical chart of Figure 22 is simpler because every feature message leaves the state alone:

| From | Message | To |
| --- | --- | --- |
| `SOCKET_CONNECTED` | `SupervisoryServiceDescription` | `SUPERVISORY_SERVICE_DESCRIPTION` |
| `SUPERVISORY_SERVICE_DESCRIPTION` | `SupervisoryServiceDescription` | `CONNECTED` |

Again the client (the supervisory system) sends first and the service answers. The feature
messages are only accepted in `CONNECTED`; `Notification`, `CheckAlive` and `Command` are accepted
at any time.

## Facades and threading

A facade owns

* one `detail::io_runner` (an `asio::io_context`, its work guard and its thread),
* the callbacks the application registered,
* a registry of the live sessions (or the one session, for the client facades),
* the acceptor (server facades) or the connector (client facades).

`start()` spawns the I/O thread and posts the first listener/connector step. `on_listening()` reports
the bound port, on the I/O thread once the listener is up or synchronously when it is already bound;
register it before `start()`. `stop()` posts the shutdown of every session, lets the I/O thread drain
and joins it. `apply()` replaces the settings, closes the live sessions with a `Notification` of code
`CONNECTION_RESET` and restarts the listener or the connector. The destructors call `stop()`.

All callbacks run on the I/O thread. `send()`, `send_raw()`, `post()`, `apply()` and `stop()` may be
called from any thread; they either copy what they need under a mutex or post to the I/O thread.
`send()` validates the message against the interface role and the current state synchronously and
returns `std::expected<void, error_info>`; `error_code::CLIENT` means the application asked for something
the standard does not allow. A facade must not be destroyed from within one of its own callbacks.

Each session keeps its own session id, handed out by the facade and stable for the life of the
connection. The client facades create a new id for every reconnect; the id of the old session is
not reused.

## Configuration channel

The configuration service has no handshake. A session reads `GetConfiguration` and answers with the
`CurrentConfiguration` the application handler returned, and reads `SetConfiguration` and applies
it, or answers with a `Notification` of code `CONFIGURATION_ERROR` when the handler returned an
error. The client functions in `configuration_client.cxx` open one connection per operation, wait
for the answer with a deadline and close the connection again, as clause 2.4.1 asks for. An accepted
`SetConfiguration` is not answered by the standard, so a read timeout after a write is treated as
success unless a `Notification` arrived first.

## Tests

* The unit tests cover the codec (all messages, exact documents, optional fields, errors, the 64 KiB
  rule), the framing (split reads, several documents per read, prologue, comments/CDATA, garbage,
  oversized documents) and the horizontal transition table including its role assignments.
* The integration tests run a real downstream server and upstream client on loopback ports and walk
  the handshake, both transport orders, revocations, forecast, board info, commands, notifications,
  `CheckAlive`, protocol errors, board id checks, reconnects and shutdown notifications, and the
  configuration service with its client functions.
* The end-to-end tests run the vertical channel and drive the downstream interface with raw XML
  documents over a plain socket: exact handshake, unknown message, protocol error, missing
  `CheckAlive` response and malformed bytes.
