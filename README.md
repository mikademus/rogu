# Rogu: The Kiroku Logger

*Rogu* is the short form for **The Kiroku Logger** — *kiroku* (記録) is "record" in Japanese. A lightweight, self-contained, single-header C++ logger with no unnecessary dependencies and no integration overhead.

## Why Rogu Exists
Logging should be the simplest thing in a project. It rarely is.

When basic `std::cout` and `std::cerr` output stopped being sufficient — which happens reliably the moment a prototype becomes a project — the natural next step was to reach for an established logging library. The options are not lacking — log4cpp, log4cxx, Boost.Log, spdlog, glog (Google Logging), Poco Logger, g3log, reckless, Pantheios, and others. After working with several of these, a pattern emerged: they are all, without exception, far too much.

* **Too much to integrate** — most require a build step, a CMake module, a package manager entry, or all three.
* **Too much to configure** — Boost.Log in particular treats a basic setup as an exercise in template metaprogramming.
* **Too much to learn** — log4cpp and its descendants carry the weight of their Java origins, demanding appenders, layouts, and hierarchies before a single line is written.
* **Too much overhead** — some are opinionated about threading models, memory allocation, or output formats in ways that conflict with the surrounding codebase.
* And **too much code** — libraries that run to tens of thousands of lines to solve a problem that should need hundreds.

None of them offered what seemed like a reasonable baseline: a single header you include, a stream you register, and a set of log calls you make. No boilerplate, no build integration, no opinions about your project structure.

So The Kiroku Logger (Rogu) was written instead. Its goals are straightforward: a single #include, modern C++ syntax, minimal resource footprint, and solid coverage of the cases that actually come up — multiple output streams, per-stream log level control, and per-stream output formatting.

## Overview
Most logging frameworks are integration projects. Rogu is not. It is a single header file you drop into your project and include. There is nothing to build, no dependencies to satisfy, no configuration files to write.

Despite its minimal footprint, Rogu covers the features that matter in practice: multiple simultaneous output streams, per-stream log level and field visibility control, per-stream output formatting with configurable field order, optional timestamps, optional source location, optional asynchronous logging, and optional ANSI colour output. It supports both `std::format`-style and `std::ostream`-style call syntax, so it fits naturally into any existing codebase.

Rogu requires C++20.

## Installation
Copy `rogu.hpp` into your project and include it:
```cpp
#include "rogu.hpp"
```
That's it.

## Configuration
Features are enabled at compile time by defining macros before including the header. The header itself has them all enabled by default — comment out what you don't need:
```cpp
#define ROGU_ANSI            // Enable ANSI colour output
#define ROGU_ASYNC           // Enable asynchronous logging
#define ROGU_PER_STREAM      // Enable per-stream level and field control
#define ROGU_SOURCE_LOCATION // Enable {trace} source location token
#define ROGU_TIMESTAMP       // Enable {time} UTC timestamp token
#include "rogu.hpp"
```

## Usage

### Log levels
Rogu provides seven log levels in ascending severity order:

| Function | Level string | Notes |
|---|---|---|
| `rogu::trace()`    | trace    | Execution flow |
| `rogu::debug()`    | debug    | Development output |
| `rogu::info()`     | info     | General information |
| `rogu::warning()`  | warning  | |
| `rogu::error()`    | error    | |
| `rogu::critical()` | critical | |
| `rogu::record()`   | *(none)* | Always outputs to all streams; bypasses all level filters |

The level string is what appears in output via the `{ll}` format token. Its presentation — capitalisation, surrounding punctuation — is controlled by the format string, not by the library.

### Invocation syntax
Both `std::format`-style and stream-style are supported and can be mixed:
```cpp
// Format style
rogu::info("connected to {} on port {}", host, port);

// Stream style
rogu::warning() << "unexpected value: " << value;

// Mixed: format string followed by stream continuation
rogu::error("failed at step {}", step) << " reason: " << reason;
```

### Flags
An optional flags argument may be passed as the first argument to any log call. Multiple flags may be combined with `|`:

```cpp
rogu::info(rogu::no_break, "loading...");
rogu::info(rogu::msg_only, " done");
// Output: "loading... done\n"
```

| Flag | Effect |
|---|---|
| `rogu::no_break`    | Suppress the trailing newline |
| `rogu::no_time`     | Suppress `{time}` for this call. Requires `ROGU_TIMESTAMP`. |
| `rogu::force_time`  | Force `{time}` for this call, overriding `no_time` and any disabled state. Requires `ROGU_TIMESTAMP`. |
| `rogu::no_ll`       | Suppress `{ll}` for this call |
| `rogu::force_ll`    | Force `{ll}` for this call, overriding any disabled state |
| `rogu::no_msg`      | Suppress `{msg}` for this call |
| `rogu::force_msg`   | Force `{msg}` for this call, overriding any disabled state |
| `rogu::no_trace`    | Suppress `{trace}` for this call. Requires `ROGU_SOURCE_LOCATION`. |
| `rogu::force_trace` | Force `{trace}` for this call, overriding any disabled state. Requires `ROGU_SOURCE_LOCATION`. |
| `rogu::msg_only`    | Suppress all fields except `{msg}`. Equivalent to `no_time \| no_ll \| no_trace`. Useful for continuation lines after `no_break`. |

`force_*` flags override disabled field state for a single call. They have no effect if the corresponding token is absent from the format string — the format string governs layout, flags govern visibility.

### Shorthand macros
```cpp
LOG_TRACE()   LOG_DEBUG()   LOG_INFO()
LOG_WARN()    LOG_ERR()     LOG_CRIT()   LOG_REC()
```
These expand to the corresponding `rogu::` function and accept the same arguments including flags.

### Output streams
By default, Rogu discards all output until at least one stream is registered. Add any `std::ostream`:
```cpp
rogu::add_output(&std::cout);

std::ofstream logfile("app.log");
rogu::add_output(&logfile);
```
Multiple streams are written simultaneously. Each stream has its own format string and its own per-stream level and field settings. A typical setup logs human-readable output to `std::cout` and a persistent record to a file.

### Format strings
Each output stream renders log events using a format string containing tokens. The default format string is:
```
{time}{ll} {msg} ({trace})
```

The available tokens are:

| Token | Field | Requirement |
|---|---|---|
| `{time}`  | UTC timestamp (HH:MM:SS) | `ROGU_TIMESTAMP` |
| `{ll}`    | Log level string | |
| `{msg}`   | Message body | |
| `{trace}` | Source location (file:line) | `ROGU_SOURCE_LOCATION` |

Tokens whose feature macro is not defined produce no output and are silently skipped. Decoration outside tokens is emitted literally. Unrecognised tokens are passed through as-is. Literal braces are written as `{{` and `}}`.

```cpp
// Custom format: level in brackets, source location after "at"
rogu::add_output(&std::cout, "[{ll}] {msg} at {trace}");
// Output: "[info] server started at main.cpp:42"

// JSON-style output to a log file
rogu::add_output(&logfile, R"({"level":"{ll}","msg":"{msg}","src":"{trace}"})");
```

The format string of a registered stream can be changed at any time:
```cpp
rogu::set_formatter(&std::cout, "{ll} {msg}");
```

The `{msg}` token marks the position where `<<` stream continuations are inserted. Content written via `<<` appears at that position, with any text following `{msg}` in the format string appended afterwards. If `{msg}` is absent from the format string, the message body is not written.

### Log level control
Global control affects all streams:
```cpp
rogu::disable_log_level(rogu::log_level::debug);
rogu::enable_log_level(rogu::log_level::debug);
```

Per-stream control requires `ROGU_PER_STREAM`. A level must first be delegated before per-stream settings take effect:
```cpp
rogu::delegate_log_level(rogu::log_level::debug);

rogu::enable_log_level_for_stream(&std::cout, rogu::log_level::debug);
rogu::disable_log_level_for_stream(&logfile,  rogu::log_level::debug);
```
`rogu::record()` always outputs to all streams regardless of any level settings.

### Field visibility control
Each output field (`time`, `ll`, `msg`, `trace`) can be enabled or disabled independently of the format string. The format string governs layout; field control governs visibility.

Global control affects all streams:
```cpp
rogu::disable_field(rogu::field::trace);
rogu::enable_field(rogu::field::trace);
```

Per-stream control requires `ROGU_PER_STREAM`. A field must first be delegated before per-stream settings take effect:
```cpp
rogu::delegate_field(rogu::field::time);

rogu::enable_field_for_stream(&std::cout, rogu::field::time);
rogu::disable_field_for_stream(&logfile,  rogu::field::time);
```

Per-call flags (`no_*` and `force_*`) are the final override layer and take precedence over all global and per-stream state for that single call. The precedence order from highest to lowest is: per-call `force_*` → per-call `no_*` → per-stream state → global state.

### Asynchronous logging
Requires `ROGU_ASYNC`. Log calls return immediately; a background thread handles writes.
```cpp
rogu::start_async();

// ... your application runs, logging freely across threads ...

rogu::stop_async();  // flushes remaining queue before returning
```
Call `stop_async()` before application exit to ensure no messages are lost. Stream-style chaining (`<<`) is a no-op in async mode — the message is fully rendered before being enqueued and cannot be extended after the fact.

### ANSI colour output
Requires `ROGU_ANSI`. Each log level has an assigned colour applied automatically to the `{ll}` token. Individual strings can also be colourised directly:
```cpp
rogu::info("status: {}", rogu::colorise(rogu::col::light_green, "all clear"));
```
Colours applied via `rogu::colorise` are treated as message data and are not affected by formatter settings. Available colours correspond to the EGA console palette: black, red, green, yellow, blue, magenta, cyan, grey, dark_grey, light_red, light_green, light_yellow, light_blue, light_magenta, light_cyan, white.

### Roadmap
* Compile-time level stripping — a `#define ROGU_MIN_LEVEL` that eliminates log calls below a threshold entirely at compile time, producing zero overhead in release builds.

### Licence
Licenced as-is under the MIT licence.