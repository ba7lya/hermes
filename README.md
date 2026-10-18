# ba7lya.hermes

[![ci](https://github.com/ba7lya/hermes/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/ba7lya/hermes/actions/workflows/ci.yml) [![nightly](https://github.com/ba7lya/hermes/actions/workflows/nightly.yml/badge.svg?branch=main)](https://github.com/ba7lya/hermes/actions/workflows/nightly.yml) [![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

"The Hermes Standard for vendor-independent machine-to-machine communication in SMT assembly" is a
new, non-proprietary open protocol, based on TCP/IP and XML, that takes the exchange of PCB related
data between the different machines in electronics assembly lines to the next level. It was
initiated and developed by a group of leading equipment suppliers, bundling their expertise in order
to achieve a great step towards advanced process integration. And the story continues: The Hermes
Standard Initiative is open to all equipment vendors who want to actively participate in bringing
the benefits of Industry 4.0 to their customers.

This repository is a from-scratch, modern C++ implementation of **IPC-HERMES-9852, version 1.6**.
It covers all three channels the standard defines:

* the **horizontal channel** (machine to machine), with the downstream and upstream interfaces,
  the complete state chart of Figure 19, board transport, `BoardForecast`, `QueryBoardInfo` /
  `SendBoardInfo`, `Command`, `Notification` and cyclic `CheckAlive` handling;
* the **vertical channel** (machine to supervisory system), with the state chart of Figure 22,
  board tracking, work order exchange and the Hermes capability exchange;
* the **configuration channel** (engineering station to machine, port 1248), with
  `SetConfiguration`, `GetConfiguration` and `CurrentConfiguration`.

The code is plain C++23: `std::expected` for errors, `std::variant` for messages, `std::optional`
for the optional XML attributes, `asio` for the transport and `pugixml` for the XML codec. There is
no reflection library and no code generation; the wire format is written down field by field, one
function per message, exactly as the tables of clause 3 define it.

## Requirements

* CMake 3.27 for the shipped presets (a manual configure needs 3.21)
* a C++23 compiler: MSVC 19.35 (Visual Studio 2022) or later, GCC 12 or later, Clang 17 or later
* [vcpkg](https://github.com/microsoft/vcpkg), either checked out in `thirdparty/vcpkg` or provided
  through `VCPKG_ROOT`; the manifest `vcpkg.json` pulls in `asio` and `pugixml` (plus `gtest` and
  `google-benchmark` for the optional test and benchmark targets). When vcpkg is not the submodule,
  pass `-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake` to the configure step.

## Building

The repository ships CMake presets for the supported toolchains. The usual sequence is

```sh
cmake --preset win-msvc-2026-x64
cmake --build --preset win-msvc-2026-x64-dbg
ctest --preset win-msvc-2026-x64-test -C Debug --output-on-failure
```

with `win-msvc-2022-x64` for Visual Studio 2022, `linux-gcc-14-x64` / `linux-clang-19-x64` on Linux,
and the `-asan`, `-tsan` or `-ubsan` variants for instrumented builds. Each configure preset has a
matching `-dbg` / `-rel` build preset and a `-test` test preset. The build options are

| Option | Default | Effect |
| --- | --- | --- |
| `ba7lya.hermes_BUILD_TEST` | `OFF` | builds the unit, integration and end-to-end test executables |
| `ba7lya.hermes_BUILD_EXAMPLE` | `OFF` | builds the three runnable examples |
| `ba7lya.hermes_BUILD_BENCHMARK` | `OFF` | builds the XML codec benchmark |

The library target is `ba7lya::hermes`; its include directory is `include/`, so consumers write
`#include "hermes.hxx"` in-tree (or include a single facade header). An installed consumer
writes `#include <ba7lya/hermes/hermes.hxx>`.

## Quick start

One machine runs the downstream interface, the machine downstream of it runs the upstream
interface. Both sides own an I/O thread, so the application only registers callbacks and calls
`send()`:

```cpp
#include "hermes.hxx"

using namespace ba7lya::hermes;

downstream_settings provider_settings;
provider_settings.machine_id_ = "UpstreamMachine";
provider_settings.port_ = 50100;
provider_settings.features_.check_alive_response_ = true;

downstream provider(provider_settings);
provider.on_state([](session_id, channel_state state) { /* ... */ });
provider.on_message([](session_id, channel_state, const any_message& message) {
    if (const auto* ready = std::get_if<machine_ready>(&message)) { /* accept it */ }
});
provider.start();

board_available board;
board.board_id_ = "11111111-2222-3333-4444-555555555555";
board.board_id_created_by_ = "UpstreamMachine";
board.failed_board_ = board_quality::GOOD;
board.flipped_board_ = flipped_board::TOP_SIDE_UP;
provider.send(session, board); // returns std::expected<void, error_info>
```

The handshake (`ServiceDescription` exchange) and the `CheckAlive` pings are performed by the
library. The application decides when a board is offered, accepted, transported, and what a
received `Notification` or `Command` means. See `examples/` for complete programs:

* `downstream_upstream_pair` walks one board through the whole horizontal sequence;
* `vertical_example` connects a machine and a supervisory system and exchanges board tracking,
  work order and configuration data;
* `configuration_example` reads and writes the configuration of a machine with the client functions.

## Repository layout

| Directory | Contents |
| --- | --- |
| `include` | the public API: protocol types and messages, XML codec, settings, and the six facades |
| `src` | the implementation: the XML codec, the TCP framing, the asio layer, the state machines and the facades |
| `tests` | GoogleTest suites: codec and framing unit tests, loopback integration tests, raw XML end-to-end tests |
| `examples` | the three runnable programs above |
| `benchmarks` | the XML codec benchmark |
| `doc` | implementation notes (`architecture.md`) |
| `uml` | PlantUML class diagrams of the public API and of the session layer |
| `thirdparty` | the vcpkg submodule (not initialised in CI; the manifest pins the ports) |

## Tests

```sh
ctest --preset win-msvc-2026-x64-test -C Debug --output-on-failure
```

The suites are

* `ba7lya.hermes_ut` – XML round trips of every message, exact wire format of representative
  messages, parse errors, the TCP framing and the state chart table;
* `ba7lya.hermes_integration` – downstream and upstream over loopback TCP: handshake, transport
  sequences, forecast, board info, commands, notifications, `CheckAlive`, protocol errors and
  reconnects, plus the configuration service;
* `ba7lya.hermes_e2e` – the vertical channel and the downstream interface driven by raw XML
  documents, including the `CheckAlive` timeout and malformed input.

## Implementation notes

* **Framing.** A Hermes TCP stream has no length prefix. `src/framing.cxx` scans the bytes for the
  end of the `<Hermes>` root element, tolerates documents split over any number of reads, skips
  declarations, comments, CDATA and processing instructions, and rejects documents above 64 KiB.
* **Threading.** Every facade owns one `asio::io_context` and one thread. Callbacks run on that
  thread; `send()`, `apply()`, `post()` and `stop()` may be called from any thread. A facade must
  not be destroyed from one of its own callbacks.
* **Error policy.** The peer's documents are validated against the state chart. A message that is
  not allowed, a missing mandatory attribute or a `BoardId` mismatch terminates the connection
  after a `Notification` of code `PROTOCOL_ERROR`, as clause 2.3.6 asks for. Unknown messages and
  unknown attributes are ignored and discarded, as clause 3.1 asks for. Local `send()` calls are
  validated against the interface role and, unless `state_check::RECEIVED_ONLY` is configured,
  against the current state; an invalid call reports `error_code::CLIENT` without touching the
  connection.
* **`CheckAlive`.** Each side pings with the configured interval and answers a ping with a pong
  when its `supported_features::check_alive_response_` is set. When the peer announced the same
  feature, a missing pong within the configured timeout closes the session.
* **Wire format.** Message fields are XML attributes, the lists (`SupportedFeatures`,
  `OptionalMessages`, `Attributes`, `SubBoards`, the configuration lists) are child elements. The
  `Timestamp` attribute of the envelope is written by the transport for diagnostic purposes and is
  exposed on parsing. A message whose `SubBoards` list would exceed the 64 KiB limit is sent
  without the list.
* **channel_state charts.** The transition tables of Figure 19 and Figure 22 are encoded in
  `src/horizontal.cxx` and `src/vertical.cxx` and are covered message by message by the unit tests.

## References

* The Hermes Standard Initiative: <https://www.the-hermes-standard.org/>
* IPC-HERMES-9852, *The Global Standard for Machine-to-Machine Communication in SMT Assembly*,
  version 1.6, July 2024.

## License

Released under the MIT license. See [LICENSE](LICENSE).
