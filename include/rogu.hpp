//============================================================================
// rogu: The Kiroku Logger
// version: 0.6.0
//============================================================================
//
// INVOCATION
//------------
// Function syntax:
//      rogu::info("connected to {} on port {}", host, port);
//
// Stream syntax:
//      rogu::warning() << "unexpected value: " << value;
//
// Mixed — format string followed by stream continuation:
//      rogu::error("failed at step {}", step) << " reason: " << reason;
//
// Flags as optional first argument:
//      rogu::info(rogu::no_break, "loading...");
//      rogu::info(rogu::msg_only, " done");
//
// Shorthand macros expand to the corresponding rogu:: function:
//      LOG_TRACE()  LOG_DEBUG()  LOG_INFO()
//      LOG_WARN()   LOG_ERR()    LOG_CRIT()  LOG_REC()
//
//
// LOG LEVELS
//------------
// In ascending severity order:
//
//      rogu::trace()     Execution flow
//      rogu::debug()     Development output
//      rogu::info()      General information
//      rogu::warning()   Warnings
//      rogu::error()     Errors
//      rogu::critical()  Critical errors
//      rogu::record()    Always outputs to all streams; bypasses all level filters
//
//
// FLAGS
//-------
// Flags may be combined with | and passed as the first argument to any log call.
//
//      rogu::no_break    Suppress the trailing newline. Use with rogu::msg_only
//                        on the next call to continue the same line.
//      rogu::msg_only    Suppress all fields except the message body. Useful for
//                        continuation lines after rogu::no_break.
//      rogu::no_time     Suppress the timestamp for this call regardless of
//                        formatter settings. Requires ROGU_TIMESTAMP.
//      rogu::force_time  Force the timestamp for this call regardless of
//                        formatter settings. Overrides rogu::no_time.
//                        Requires ROGU_TIMESTAMP.
//
//
// OUTPUT STREAMS
//----------------
// Rogu discards all output until at least one stream is registered:
//      rogu::add_output(&std::cout);
//      rogu::add_output(&logfile, "{ll} {msg}");  // optional format string
//
// Multiple streams are written simultaneously. Each stream has its own
// format string and its own per-stream level settings.
//
//
// FORMAT STRINGS
//----------------
// Each stream renders log events using a format string containing tokens.
// The default format string is: {time}{ll} {msg} ({trace})
//
// Tokens:
//      {time}    UTC timestamp (HH:MM:SS). Requires ROGU_TIMESTAMP.
//      {ll}      Log level string (debug, info, warning, etc.)
//      {msg}     Message body. The << stream continuation writes here.
//      {trace}   Source location (file:line). Requires ROGU_SOURCE_LOCATION.
//
// Decoration outside tokens is emitted literally:
//      "[{ll}] {msg} at {trace}"  →  "[info] connected at main.cpp:42"
//
// Escape sequences:
//      {{  →  literal {
//      }}  →  literal }
//
// Unrecognised tokens are passed through literally: {foo} → {foo}
//
// The format string for a registered stream can be changed at any time:
//      rogu::set_formatter(&std::cout, "{ll} {msg}");
//
//
// LOG LEVEL CONTROL
//-------------------
// Global control affects all streams:
//      rogu::disable_log_level(rogu::log_level::debug);
//      rogu::enable_log_level(rogu::log_level::debug);
//
// Per-stream control requires ROGU_PER_STREAM:
//      rogu::delegate_log_level(rogu::log_level::debug);   // hand off to per-stream
//      rogu::enable_log_level_for_stream(&std::cout, rogu::log_level::debug);
//      rogu::disable_log_level_for_stream(&logfile,  rogu::log_level::debug);
//
//
// ASYNCHRONOUS LOGGING
//----------------------
// Requires ROGU_ASYNC. Log calls return immediately; a background thread
// handles writes. Stream-style chaining (<<) is a no-op in async mode.
//      rogu::start_async();
//      // ... application runs ...
//      rogu::stop_async();   // flushes remaining queue before returning
//
//
// ANSI COLOUR OUTPUT
//--------------------
// Requires ROGU_ANSI. Each log level has an assigned colour. Strings can
// also be colourised directly:
//      rogu::info("status: {}", rogu::colorise(rogu::col::light_green, "ok"));
//
// Available colours (EGA palette):
//      black  red  green  yellow  blue  magenta  cyan  grey
//      dark_grey  light_red  light_green  light_yellow  light_blue
//      light_magenta  light_cyan  white
//
//
// COMPILE-TIME CONTROLS
//-----------------------
// Define before including rogu.hpp. The header has all enabled by default —
// comment out what you do not need:
//
//      #define ROGU_ANSI                // Enable ANSI colour output
//      #define ROGU_ASYNC               // Enable asynchronous logging
//      #define ROGU_PER_STREAM          // Enable per-stream level control
//      #define ROGU_SOURCE_LOCATION     // Enable {trace} source location token
//      #define ROGU_TIMESTAMP           // Enable {time} UTC timestamp token
//
#ifndef KIROKU_INCLUDE
#define KIROKU_INCLUDE

// Uncomment to enable feature:
#define ROGU_ANSI            /* Enable ANSI colour output */
#define ROGU_ASYNC           /* Enable asynchronous logging */
#define ROGU_PER_STREAM      /* Enable per-stream settings */
#define ROGU_SOURCE_LOCATION /* Enable trace source log location */
#define ROGU_TIMESTAMP       /* Enable time/datestamps */

#include <atomic>
#include <format>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#ifdef ROGU_ASYNC
#include <queue>
#include <thread>
#include <condition_variable>
#endif

#ifdef ROGU_SOURCE_LOCATION
#include <concepts>
#include <source_location>
#endif

#ifdef ROGU_TIMESTAMP
#include <chrono>
#endif

#define LOG_REC rogu::record
#define LOG_CRIT rogu::critical
#define LOG_ERR rogu::error
#define LOG_WARN rogu::warning
#define LOG_INFO rogu::info
#define LOG_DEBUG rogu::debug
#define LOG_TRACE rogu::trace

namespace rogu
{
    enum struct log_level : int
    {
        trace,
        debug,
        info,
        warning,
        error,
        critical,
        record
    };

    enum struct log_flags : uint16_t
    {
        none         = 0,
        no_break     = 1 << 0,   // Suppress trailing newline
        no_time      = 1 << 1,   // Suppress {time} for this call
        force_time   = 1 << 2,   // Force {time} for this call, overrides no_time
        no_ll        = 1 << 3,   // Suppress {ll} for this call
        force_ll     = 1 << 4,   // Force {ll} for this call
        no_msg       = 1 << 5,   // Suppress {msg} for this call
        force_msg    = 1 << 6,   // Force {msg} for this call
        no_trace     = 1 << 7,   // Suppress {trace} for this call
        force_trace  = 1 << 8,   // Force {trace} for this call
        msg_only     = no_time | no_ll | no_trace,  // Suppress all fields except {msg}
        // Note: if fields are added, update msg_only accordingly.
    };

    inline constexpr log_flags operator|(log_flags a, log_flags b)
    {
        return static_cast<log_flags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
    }

    inline constexpr bool has_flag(log_flags flags, log_flags f)
    {
        return static_cast<uint16_t>(flags) & static_cast<uint16_t>(f);
    }

    inline constexpr log_flags no_break   = log_flags::no_break;
    inline constexpr log_flags no_time    = log_flags::no_time;
    inline constexpr log_flags force_time = log_flags::force_time;
    inline constexpr log_flags no_ll      = log_flags::no_ll;
    inline constexpr log_flags force_ll   = log_flags::force_ll;
    inline constexpr log_flags no_msg     = log_flags::no_msg;
    inline constexpr log_flags force_msg  = log_flags::force_msg;
    inline constexpr log_flags no_trace   = log_flags::no_trace;
    inline constexpr log_flags force_trace = log_flags::force_trace;
    inline constexpr log_flags msg_only   = log_flags::msg_only;

    static_assert(static_cast<int>(log_level::record) < 8,
        "log_level has exceeded 7 values; uint8_t per-stream bitmask is no longer sufficient");

    enum struct field : uint8_t
    {
        time,
        ll,
        msg,
        trace
    };

    static_assert(static_cast<int>(field::trace) < 8,
        "field has exceeded 7 values; uint8_t per-stream field bitmask is no longer sufficient");

    enum col
    {
        black           = 0,
        red             = 1,
        green           = 2,
        yellow          = 3,
        blue            = 4,
        magenta         = 5,
        cyan            = 6,
        grey            = 7,
        dark_grey       = 60,
        light_red       = 61,
        light_green     = 62,
        light_yellow    = 63,
        light_blue      = 64,
        light_magenta   = 65,
        light_cyan      = 66,
        white           = 67,
    };

    inline std::string colorise(col fg, std::string_view str); // forward declaration

#ifdef ROGU_ANSI
    namespace ansi
    {
        enum struct fg
        {
            black      = 30,     bright_black      = 90,
            red        = 31,     bright_red        = 91,
            green      = 32,     bright_green      = 92,
            yellow     = 33,     bright_yellow     = 93,
            blue       = 34,     bright_blue       = 94,
            magenta    = 35,     bright_magenta    = 95,
            cyan       = 36,     bright_cyan       = 96,
            white      = 37,     bright_white      = 97,
        };
    
        enum struct bg
        {
            black      = 40,     bright_black      = 100,
            red        = 41,     bright_red        = 101,
            green      = 42,     bright_green      = 102,
            yellow     = 43,     bright_yellow     = 103,
            blue       = 44,     bright_blue       = 104,
            magenta    = 45,     bright_magenta    = 105,
            cyan       = 46,     bright_cyan       = 106,
            white      = 47,     bright_white      = 107,
        };
    
        inline constexpr const char* colour_escape_code = "\033[";
        inline constexpr const char* reset_colours_code = "\033[0m";
    
        inline std::string colour_code(fg col_fg)
        {   
            return std::format("{}{}{}", colour_escape_code, (int) col_fg, 'm');
        }
    
        inline std::string colour_code(bg col_bg)
        {
            return std::format("{}{}{}", colour_escape_code, (int) col_bg, 'm');
        }
    
        inline std::string colour_code(fg col_fg, bg col_bg)
        {
            return std::format("{}{}", colour_code(col_fg), colour_code(col_bg));
        }
    }
#endif

    namespace impl
    {
        inline constexpr const char* default_format_str = "{time}{ll} {msg} ({trace})";

        struct log_event
        {
            log_level           level;
            std::string_view    level_str;
            rogu::col           level_colour;
            std::string_view    message;
            rogu::log_flags     flags;
            uint8_t             active_fields;  // bitmask of rogu::field values
#ifdef ROGU_TIMESTAMP
            std::chrono::sys_seconds timestamp;
#endif
#ifdef ROGU_SOURCE_LOCATION
            std::source_location location;
#endif
        };


        enum struct ll_state { off = -1, per_stream = 0, on = 1 };

        struct stream_entry
        {
            std::ostream*   stream;
#ifdef ROGU_PER_STREAM
            uint8_t         level_bits = 0b1111111;
            uint8_t         field_bits = 0b1111;
#endif
            std::string     format_str;
        };

        struct logger_state
        {
            static std::vector<stream_entry>& entries()
            {
                static std::vector<stream_entry> entries;
                return entries;
            }

            static ll_state (&master_levels())[7]
            {
#ifdef ROGU_PER_STREAM
                static ll_state levels[7] = {ll_state::per_stream, ll_state::per_stream, ll_state::per_stream, ll_state::per_stream, ll_state::per_stream, ll_state::per_stream, ll_state::on};
#else
                static ll_state levels[7] = {ll_state::on, ll_state::on, ll_state::on, ll_state::on, ll_state::on, ll_state::on, ll_state::on};
#endif
                return levels;
            }

            static ll_state (&master_fields())[4]
            {
#ifdef ROGU_PER_STREAM
                static ll_state fields[4] = {ll_state::per_stream, ll_state::per_stream, ll_state::per_stream, ll_state::per_stream};
#else
                static ll_state fields[4] = {ll_state::on, ll_state::on, ll_state::on, ll_state::on};
#endif
                return fields;
            }
        };

        struct rendered_event
        {
            std::string before_msg;
            std::string after_msg;
            bool        msg_seen = false;
        };

        inline rendered_event render(const std::string& format_str, const log_event& e)
        {
            rendered_event result;
            std::string* out = &result.before_msg;
            std::size_t i = 0;
            while (i < format_str.size())
            {
                if (format_str[i] == '{')
                {
                    if (i + 1 < format_str.size() && format_str[i + 1] == '{')
                    {
                        *out += '{';
                        i += 2;
                        continue;
                    }
                    std::size_t close = format_str.find('}', i + 1);
                    if (close == std::string::npos)
                    {
                        *out += format_str[i++];
                        continue;
                    }
                    std::string_view token(format_str.data() + i + 1, close - i - 1);
                    if (token == "msg")
                    {
                        if (e.active_fields & (1 << (int) rogu::field::msg))
                            out = &result.after_msg;
                        result.msg_seen = true;
                    }
                    else if (token == "ll")
                    {
                        if (e.active_fields & (1 << (int) rogu::field::ll))
                        {
#ifdef ROGU_ANSI
                            *out += rogu::colorise(e.level_colour, e.level_str);
#else
                            *out += e.level_str;
#endif
                        }
                    }
                    else if (token == "time")
                    {
#ifdef ROGU_TIMESTAMP
                        if (e.active_fields & (1 << (int) rogu::field::time))
                            *out += std::format("{:%H:%M:%S} ", e.timestamp);
#endif
                    }
                    else if (token == "trace")
                    {
#ifdef ROGU_SOURCE_LOCATION
                        if (e.active_fields & (1 << (int) rogu::field::trace))
                            *out += std::format("{}:{}", e.location.file_name(), e.location.line());
#endif
                    }
                    else
                    {
                        *out += '{';
                        *out += token;
                        *out += '}';
                    }
                    i = close + 1;
                }
                else if (format_str[i] == '}' && i + 1 < format_str.size() && format_str[i + 1] == '}')
                {
                    *out += '}';
                    i += 2;
                }
                else
                {
                    *out += format_str[i++];
                }
            }
            return result;
        }

        inline std::mutex output_mutex;

#ifdef ROGU_PER_STREAM    
        inline bool ll_enabled(uint8_t state, log_level ll)
        {
            return state & (1 << (int) ll);
        }

        inline bool field_enabled(uint8_t state, rogu::field f)
        {
            return state & (1 << (int) f);
        }

        inline void set_bits(uint8_t& bits, log_level mask) { bits |= 1 << (int) mask; }
        inline void clear_bits(uint8_t& bits, log_level mask) { bits &= ~(1 << (int) mask); }

        inline void set_field_bits(uint8_t& bits, rogu::field mask) { bits |= 1 << (int) mask; }
        inline void clear_field_bits(uint8_t& bits, rogu::field mask) { bits &= ~(1 << (int) mask); }
#endif

#ifdef ROGU_ASYNC
        struct async_log_message
        {
            std::string text;
            std::vector<std::ostream*> streams;
            bool no_line_break;
        };

        struct async_state
        {
            std::queue<async_log_message> queue;
            std::mutex queue_mutex;
            std::condition_variable queue_cv;
            std::thread worker;
            std::atomic<bool> running = false;

            void process()
            {
                while (running)
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    queue_cv.wait(lock, [this] { return !queue.empty() || !running; });
                    while (!queue.empty())
                    {
                        async_log_message msg = std::move(queue.front());
                        queue.pop();
                        lock.unlock();
                        for (auto* stream : msg.streams)
                        {
#ifdef ROGU_ANSI
                            *stream << msg.text << ansi::reset_colours_code << (msg.no_line_break ? "" : "\n");
#else
                            *stream << msg.text << (msg.no_line_break ? "" : "\n");
#endif
                        }
                        lock.lock();
                    }
                }
            }
        };
        inline async_state async_logger;
#endif

#ifdef ROGU_SOURCE_LOCATION
        struct format_with_location
        {
            std::string_view fmt;
            std::source_location loc;

            template<typename T>
                requires std::convertible_to<T, std::string_view>
            format_with_location(T&& f, std::source_location l = std::source_location::current())
                : fmt(std::forward<T>(f)), loc(std::move(l)) {}
        };
#else
        struct format_with_location
        {
            std::string_view fmt;

            template<typename T>
                requires std::convertible_to<T, std::string_view>
            format_with_location(T&& f) : fmt(std::forward<T>(f)) {}
        };
#endif

#ifdef ROGU_TIMESTAMP
        inline std::string timestamp()
        {
            auto now = std::chrono::system_clock::now();
            auto secs = std::chrono::floor<std::chrono::seconds>(now);
            return std::format("{:%H:%M:%S} ", secs);
        }
#else
        inline std::string_view timestamp() { return ""; }
#endif

        struct stream_wrapper
        {
            std::vector<std::ostream*> streams;
            std::string                after_msg;
            bool                       no_line_break{true};

            stream_wrapper() : no_line_break(true) {}
            stream_wrapper(std::vector<std::ostream*> s, std::string after, bool nlb)
                : streams(std::move(s)), after_msg(std::move(after)), no_line_break(nlb) {}

            ~stream_wrapper()
            {
                for (auto* stream : streams)
#ifdef ROGU_ANSI
                    *stream << after_msg << ansi::reset_colours_code << (no_line_break ? "" : "\n");
#else
                    *stream << after_msg << (no_line_break ? "" : "\n");
#endif
            }

            stream_wrapper& operator<<(char c)
            {
                for (auto* stream : streams)
                    *stream << c;
                return *this;
            }

            stream_wrapper& operator<<(const char* s)
            {
                for (auto* stream : streams)
                    *stream << s;
                return *this;
            }

            stream_wrapper& operator<<(std::ostream& (*pf)(std::ostream&))
            {
                for (auto* stream : streams)
                    pf(*stream);
                return *this;
            }

            template<typename T>
            stream_wrapper& operator<<(const T& value)
            {
                for (auto* stream : streams)
                    *stream << value;
                return *this;
            }
        };

#ifdef ROGU_ANSI
        inline ansi::fg to_fg(rogu::col c)
        {
            return static_cast<ansi::fg>(static_cast<int>(c) + 30);
        }

        inline ansi::bg to_bg(rogu::col c)
        {
            return static_cast<ansi::bg>(static_cast<int>(c) + 40);
        }
#endif        

        struct null_buffer : std::streambuf 
        {
            int overflow(int c) override { return traits_type::not_eof(c); }
        };

        struct null_stream : std::ostream 
        {
            null_stream() : std::ostream(&nb) {}
        private:
            null_buffer nb;
        };

        inline null_stream cnull;        

        inline void initialise_outputs_locked()
        {
            // Note: caller must hold output_mutex.
            if (logger_state::entries().empty())
            {
                stream_entry entry;
                entry.stream = &cnull;
                entry.format_str = default_format_str;
#ifdef ROGU_PER_STREAM
                entry.level_bits = 0b1111111;
                entry.field_bits = 0b1111;
#endif
                logger_state::entries().push_back(std::move(entry));
            }
        }

        template <log_level Level, col Colour>
        struct log_traits
        {
            static constexpr log_level level = Level;
            static constexpr col colour = Colour;
        };

        inline uint8_t resolve_fields(const stream_entry& entry, rogu::log_flags flags)
        {
            uint8_t result = 0;
            for (int i = 0; i <= (int) rogu::field::trace; ++i)
            {
                auto f = static_cast<rogu::field>(i);
                auto master = logger_state::master_fields()[i];

                bool active = false;
                if (master == ll_state::on)
                    active = true;
                else if (master == ll_state::off)
                    active = false;
#ifdef ROGU_PER_STREAM
                else if (master == ll_state::per_stream)
                    active = field_enabled(entry.field_bits, f);
#else
                else
                    active = true;
#endif
                // Per-call flags are the final override layer.
                // Precedence: force > no > global/per-stream state.
                switch (f)
                {
                    case rogu::field::time:
                        if (has_flag(flags, rogu::log_flags::force_time)) active = true;
                        else if (has_flag(flags, rogu::log_flags::no_time)) active = false;
                        break;
                    case rogu::field::ll:
                        if (has_flag(flags, rogu::log_flags::force_ll)) active = true;
                        else if (has_flag(flags, rogu::log_flags::no_ll)) active = false;
                        break;
                    case rogu::field::msg:
                        if (has_flag(flags, rogu::log_flags::force_msg)) active = true;
                        else if (has_flag(flags, rogu::log_flags::no_msg)) active = false;
                        break;
                    case rogu::field::trace:
                        if (has_flag(flags, rogu::log_flags::force_trace)) active = true;
                        else if (has_flag(flags, rogu::log_flags::no_trace)) active = false;
                        break;
                }
                if (active) result |= (1 << i);
            }
            return result;
        }

        template <typename Traits, typename... Args>
        stream_wrapper log(std::string_view prefix, log_flags flags,
                           format_with_location fwl = {""}, Args&&... args)
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            initialise_outputs_locked();
            if (Traits::level != log_level::record && logger_state::master_levels()[(int) Traits::level] == ll_state::off)
                return stream_wrapper{};

            static thread_local std::vector<stream_entry*> active_entries;
            active_entries.clear();
            for (auto& entry : logger_state::entries())
            {
                if (Traits::level == log_level::record)
                {
                    active_entries.push_back(&entry);
                    continue;
                }
                auto master_state = logger_state::master_levels()[(int) Traits::level];
                bool include = false;
                if (master_state == ll_state::on)
                    include = true;
                else if (master_state == ll_state::per_stream)
                {
#ifdef ROGU_PER_STREAM
                    include = ll_enabled(entry.level_bits, Traits::level);
#else
                    include = true;
#endif
                }
                if (include)
                    active_entries.push_back(&entry);
            }
            if (active_entries.empty())
                return stream_wrapper{};

            int pop_nobreak = has_flag(flags, rogu::log_flags::no_break) ? 1 : 0;
            std::string_view raw_msg = fwl.fmt;//.substr(0, fwl.fmt.size() - pop_nobreak);

            std::string message;
            if constexpr (sizeof...(args) == 0)
                message = raw_msg;
            else
                message = std::vformat(raw_msg, std::make_format_args(args...));

            log_event event
            {
                .level        = Traits::level,
                .level_str    = prefix,
                .level_colour = Traits::colour,
                .message      = message,
                .flags        = flags,
#ifdef ROGU_TIMESTAMP
                .timestamp    = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
#endif
#ifdef ROGU_SOURCE_LOCATION
                .location     = fwl.loc,
#endif
            };

            if (has_flag(flags, log_flags::msg_only))
                event.level_str = "";

#ifdef ROGU_ASYNC
            if (async_logger.running)
            {
                static thread_local std::vector<std::pair<std::ostream*, std::string>> async_outputs;
                async_outputs.clear();
                for (auto* entry : active_entries)
                {
                    rendered_event r = has_flag(flags, log_flags::msg_only)
                        ? rendered_event{std::string(message), {}}
                        : render(entry->format_str, event);
                    std::string full = std::move(r.before_msg) + message + std::move(r.after_msg);
                    async_outputs.push_back({entry->stream, std::move(full)});
                }
                std::lock_guard<std::mutex> async_lock(async_logger.queue_mutex);
                for (auto& [stream, text] : async_outputs)
                    async_logger.queue.push({std::move(text), {stream}, pop_nobreak == 1});
                async_logger.queue_cv.notify_one();
                return stream_wrapper{};
            }
#endif

            static thread_local std::vector<std::ostream*> active_streams;
            active_streams.clear();
            std::string after_msg_str;
            for (auto* entry : active_entries)
            {
                log_event event
                {
                    .level         = Traits::level,
                    .level_str     = prefix,
                    .level_colour  = Traits::colour,
                    .message       = message,
                    .flags         = flags,
                    .active_fields = resolve_fields(*entry, flags),
#ifdef ROGU_TIMESTAMP
                    .timestamp     = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
#endif
#ifdef ROGU_SOURCE_LOCATION
                    .location      = fwl.loc,
#endif
                };
                rendered_event r = render(entry->format_str, event);
                *entry->stream << r.before_msg;
                if (r.msg_seen && (event.active_fields & (1 << (int) rogu::field::msg)))
                    *entry->stream << message;
                after_msg_str = std::move(r.after_msg);
                active_streams.push_back(entry->stream);
            }
            return stream_wrapper{std::move(active_streams), std::move(after_msg_str), pop_nobreak == 1};
        }
    } // ns impl

    inline std::string colorise(col fg, std::string_view str)
    {
#ifdef ROGU_ANSI
        return std::format("{}{}{}", ansi::colour_code(impl::to_fg(fg)), str, ansi::reset_colours_code);
#else
        return std::string(str);
#endif
    }

//============================================================================    
// LOG LEVEL PRINTERS
// These are what the macros resolve to:
//============================================================================    
template<typename... Args>
    impl::stream_wrapper debug(impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::debug, col::grey>>("debug", log_flags::none, fwl, std::forward<Args>(args)...);
    }
    template<typename... Args>
    impl::stream_wrapper debug(log_flags flags, impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::debug, col::grey>>("debug", flags, fwl, std::forward<Args>(args)...);
    }

    template<typename... Args>
    impl::stream_wrapper trace(impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::trace, col::light_blue>>("trace", log_flags::none, fwl, std::forward<Args>(args)...);
    }
    template<typename... Args>
    impl::stream_wrapper trace(log_flags flags, impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::trace, col::light_blue>>("trace", flags, fwl, std::forward<Args>(args)...);
    }

    template<typename... Args>
    impl::stream_wrapper info(impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::info, col::light_blue>>("info", log_flags::none, fwl, std::forward<Args>(args)...);
    }
    template<typename... Args>
    impl::stream_wrapper info(log_flags flags, impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::info, col::light_blue>>("info", flags, fwl, std::forward<Args>(args)...);
    }

template<typename... Args>
    impl::stream_wrapper warning(impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::warning, col::yellow>>("warning", log_flags::none, fwl, std::forward<Args>(args)...);
    }
    template<typename... Args>
    impl::stream_wrapper warning(log_flags flags, impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::warning, col::yellow>>("warning", flags, fwl, std::forward<Args>(args)...);
    }

    template<typename... Args>
    impl::stream_wrapper error(impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::error, col::light_red>>("error", log_flags::none, fwl, std::forward<Args>(args)...);
    }
    template<typename... Args>
    impl::stream_wrapper error(log_flags flags, impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::error, col::light_red>>("error", flags, fwl, std::forward<Args>(args)...);
    }

    template<typename... Args>
    impl::stream_wrapper critical(impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::critical, col::light_red>>("critical", log_flags::none, fwl, std::forward<Args>(args)...);
    }
    template<typename... Args>
    impl::stream_wrapper critical(log_flags flags, impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::critical, col::light_red>>("critical", flags, fwl, std::forward<Args>(args)...);
    }

    template<typename... Args>
    impl::stream_wrapper record(impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::record, col::grey>>("", log_flags::none, fwl, std::forward<Args>(args)...);
    }
    template<typename... Args>
    impl::stream_wrapper record(log_flags flags, impl::format_with_location fwl = {""}, Args&&... args)
    {
        return impl::log<impl::log_traits<log_level::record, col::grey>>("", flags, fwl, std::forward<Args>(args)...);
    }

#ifdef ROGU_ASYNC
    inline void start_async()
    {
        std::lock_guard<std::mutex> lock(impl::async_logger.queue_mutex);
        if (!impl::async_logger.running)
        {
            impl::async_logger.running = true;
            impl::async_logger.worker = std::thread(&impl::async_state::process, &impl::async_logger);
        }
    }

    inline void stop_async()
    {
        std::unique_lock<std::mutex> lock(impl::async_logger.queue_mutex);
        if (impl::async_logger.running)
        {
            impl::async_logger.running = false;
            lock.unlock();
            impl::async_logger.queue_cv.notify_one();
            impl::async_logger.worker.join();

            // Worker has exited; acquire mutex before touching the queue
            lock.lock();
            while (!impl::async_logger.queue.empty())
            {
                impl::async_log_message msg = std::move(impl::async_logger.queue.front());
                impl::async_logger.queue.pop();
                lock.unlock();
                for (auto* stream : msg.streams)
#ifdef ROGU_ANSI
                    *stream << msg.text << rogu::ansi::reset_colours_code << (msg.no_line_break ? "" : "\n");
#else
                    *stream << msg.text << (msg.no_line_break ? "" : "\n");
#endif
                lock.lock();
            }
        }
    }
#endif

    inline void add_output(std::ostream* stream, std::string format_str = impl::default_format_str)
    {
        if (!stream) return;
        std::lock_guard<std::mutex> lock(impl::output_mutex);
        impl::initialise_outputs_locked();
        impl::stream_entry entry;
        entry.stream = stream;
        entry.format_str = std::move(format_str);
#ifdef ROGU_PER_STREAM
        entry.level_bits = 0b1111111;
        entry.field_bits = 0b1111;
#endif
        impl::logger_state::entries().push_back(std::move(entry));
    }

    inline void set_formatter(std::ostream* stream, std::string format_str)
    {
        std::lock_guard<std::mutex> lock(impl::output_mutex);
        for (auto& entry : impl::logger_state::entries())
            if (entry.stream == stream)
            {
                entry.format_str = std::move(format_str);
                return;
            }
    }

    inline void disable_log_level(log_level ll)
    {
        impl::logger_state::master_levels()[(int) ll] = impl::ll_state::off;
    }

    inline void enable_log_level(log_level ll)
    {
        impl::logger_state::master_levels()[(int) ll] = impl::ll_state::on;
    }

    inline void disable_field(rogu::field f)
    {
        impl::logger_state::master_fields()[(int) f] = impl::ll_state::off;
    }

    inline void enable_field(rogu::field f)
    {
        impl::logger_state::master_fields()[(int) f] = impl::ll_state::on;
    }

#ifdef ROGU_PER_STREAM    
    inline void delegate_log_level(log_level ll)
    {
        impl::logger_state::master_levels()[(int) ll] = impl::ll_state::per_stream;
    }

    inline void disable_log_level_for_stream(std::ostream* s, log_level ll)
    {
        std::lock_guard<std::mutex> lock(impl::output_mutex);
        for (auto& entry : impl::logger_state::entries())
            if (entry.stream == s) impl::clear_bits(entry.level_bits, ll);
    }

    inline void enable_log_level_for_stream(std::ostream* s, log_level ll)
    {
        std::lock_guard<std::mutex> lock(impl::output_mutex);
        for (auto& entry : impl::logger_state::entries())
            if (entry.stream == s) impl::set_bits(entry.level_bits, ll);
    }

    inline void delegate_field(rogu::field f)
    {
        impl::logger_state::master_fields()[(int) f] = impl::ll_state::per_stream;
    }

    inline void disable_field_for_stream(std::ostream* s, rogu::field f)
    {
        std::lock_guard<std::mutex> lock(impl::output_mutex);
        for (auto& entry : impl::logger_state::entries())
            if (entry.stream == s) impl::clear_field_bits(entry.field_bits, f);
    }

    inline void enable_field_for_stream(std::ostream* s, rogu::field f)
    {
        std::lock_guard<std::mutex> lock(impl::output_mutex);
        for (auto& entry : impl::logger_state::entries())
            if (entry.stream == s) impl::set_field_bits(entry.field_bits, f);
    }
#endif
}

#endif // KIROKU_INCLUDE