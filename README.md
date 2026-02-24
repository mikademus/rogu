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

So The Kiroku Logger (Rogu) was written instead. Its goals are straightforward: a single #include, modern C++ syntax, minimal resource footprint, and solid coverage of the cases that actually come up — multiple output streams, per-stream log level control, and structured output formatting on the roadmap.

## Overview
Most logging frameworks are integration projects. Rogu is not. It is a single header file you drop into your project and include. There is nothing to build, no dependencies to satisfy, no configuration files to write.

Despite its minimal footprint, Rogu covers the features that matter in practice: multiple simultaneous output streams, per-stream log level control, optional asynchronous logging, and optional ANSI colour output. It supports both std::format-style and std::ostream-style call syntax, so it fits naturally into any existing codebase.

Rogu requires C++20.

## Installation
Copy `rogu.h` into your project and include it:
```cpp
#include "rogu.h"
```
That's it.

## Configuration
Features are enabled at compile time by defining macros before including the header. The header itself has them uncommented by default — comment out what you don't need:
```cpp
#define ROGU_ANSI              // Enable ANSI colour output
#define ROGU_ASYNC             // Enable asynchronous logging
#define ROGU_LOGLEVEL_PER_STREAM  // Enable per-stream log level control
#include "rogu.h"
```

## Usage
### Log levels
Rogu provides seven log levels in ascending severity order:

|Function | Prefix | Notes
|---|---|---|
| rogu::trace()    | >         | Execution flow
| rogu::debug()    | (none)    | Development output
| rogu::info()     | info:     | General information
| rogu::warning()  | Warning:  | 
| rogu::error()    | ERROR:    |
| rogu::critical() | CRITICAL: |
| rogu::record()   | (none)    | Always output on all streams, bypasses all level filters

### Invocation syntax
Both `std::format`-style and stream-style are supported and can be mixed on the same line:
```cpp
// Format style
rogu::info("Connected to {} on port {}", host, port);

// Stream style
rogu::warning() << "Unexpected value: " << value;

// Mixed: format string followed by stream continuation
rogu::error("Failed at step {}$", step) << " — reason: " << reason;
```

### Line breaks
Rogu automatically appends a newline after each log call. To suppress it — for example when building a line incrementally — append $ to the format string (inspired by the regex end-of-line marker):
```cpp
rogu::info("Loading$");
rogu::info(" ... done");
// Output: "Loading ... done\n"
```
Note: if your message legitimately ends with `$`, escape it or use stream continuation instead.

### Shorthand macros
```cpp
LOG_TRACE()   LOG_DEBUG()   LOG_INFO()
LOG_WARN()    LOG_ERR()     LOG_CRIT()   LOG_REC()
```
These expand to the corresponding `rogu::` function and accept the same arguments.

### Output streams
By default, Rogu discards all output until at least one stream is registered. Add any `std::ostream`:
```cpp
rogu::add_output(&std::cout);
std::ofstream logfile("app.log");
rogu::add_output(&logfile);
```
Multiple streams are written simultaneously. A typical setup logs human-readable output to `std::cout` and a persistent record to a file.

### Log level control
Global control — affects all streams:
```cpp
rogu::disable_log_level(rogu::log_level::debug);
rogu::enable_log_level(rogu::log_level::debug);
```
Per-stream control (requires `ROGU_LOGLEVEL_PER_STREAM`):
```cpp
// First delegate the level to per-stream decision
rogu::delegate_log_level(rogu::log_level::debug);

// Then enable or disable per stream
rogu::enable_log_level_for_stream(&std::cout, rogu::log_level::debug);
rogu::disable_log_level_for_stream(&logfile,  rogu::log_level::debug);
```
`rogu::record()` always outputs to all streams regardless of any level settings.

### Asynchronous logging
Requires `ROGU_ASYNC`. Log calls return immediately; a background thread handles writes.
```cpp
rogu::start_async();

// ... your application runs, logging freely across threads ...

rogu::stop_async();  // Flushes remaining queue before returning
```
Call `stop_async()` before application exit to ensure no messages are lost. Note: stream-style chaining (`<<`) after a log call is not recommended in async mode.

### ANSI colour output
Requires `ROGU_ANSI`. Each log level has an assigned colour applied automatically. You can also colorise strings directly:
```cpp
std::string highlighted = rogu::colorise(rogu::col::light_green, "all clear");
rogu::info("Status: {}", highlighted);
```
Available colours correspond to the EGA console palette: black, red, green, yellow, blue, magenta, cyan, grey, dark_grey, light_red, light_green, light_yellow, light_blue, light_magenta, light_cyan, white.

### Roadmap
* Timestamps with configurable format
* Source location
* Per-stream output formatters, enabling
  * structured output such as JSON
  * stream formatting (this includes reordering segments and changing prefixes etc)
* Compile-time level stripping — a #define ROGU_MIN_LEVEL that eliminates log calls below a threshold entirely at compile time, producing zero overhead in release builds.

### Licence
Licenced as-is under the MIT licence.
