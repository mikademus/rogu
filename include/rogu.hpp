//============================================================================
// rogu: The Kiroku Logger
//============================================================================
//
// INVOKATION:
//----------------
// Function syntax:
//      logger::xxx("{} {}", "std::format", "style");
// Stream syntax:
//      logger::xxx() << "std::ostream " << "style";
// Shorthand macros:
//      LOG_XXX();  // LOG_DEBUG(), LOG_ERR(), etc.
//
// LOG LEVELS:
//----------------
// logger::debug(fmt = "", args) << more;
// logger::trace(fmt = "", args) << more;
// logger::info(fmt = "", args) << more;
// logger::warning(fmt = "", args) << more;
// logger::error(fmt = "", args) << more;
// logger::critical(fmt = "", args) << more;
// logger::record(fmt = "", args) << more;      // Always output on all streams, regardless of flags
//
// LINE BREAKS
//-----------------
// The logger automatically inserts a line feed after each string. 
// Adding '$' at end of string (inspired by regex EOL marker) 
// prevents automatic line break.
//      logger::info("This line will not break$");
//
// COMPILE TIME CONTROLS
//-------------------------
// Define these before including log.h to control:
//      #define LOGGER_ANSI     // Enable ANSI color support
//      #define LOGGER_ASYNC    // Enable async logging
//      #define LOGGER_LOGLEVEL_PER_STREAM    // Enable global and per-stream selection of logging

#ifndef KIROKU_INCLUDE
#define KIROKU_INCLUDE

// Uncomment to enable feature:
#define LOGGER_ANSI       /* Enable ANSI colour output */
#define LOGGER_ASYNC      /* Enable asynchronous logging */
#define LOGGER_LOGLEVEL_PER_STREAM

#include <format>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

#ifdef LOGGER_ASYNC
#include <queue>
#include <thread>
#include <condition_variable>
#endif

#define LOG_REC logger::record
#define LOG_CRIT logger::critical
#define LOG_ERR logger::error
#define LOG_WARN logger::warning
#define LOG_INFO logger::info
#define LOG_DEBUG logger::debug
#define LOG_TRACE logger::trace

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

#ifdef LOGGER_ANSI
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
    
        static const char* colour_escape_code = "\033[";
        static const char* reset_colours_code = "\033[0m";
    
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
        enum struct ll_state { off = -1, per_stream = 0, on = 1 };

        struct logger_state
        {
            static std::vector<std::ostream*>& outputs()
            {
                static std::vector<std::ostream*> outputs;
                return outputs;
            }

#ifdef LOGGER_LOGLEVEL_PER_STREAM    
            static std::vector<std::pair<std::ostream*, uint8_t>>& log_levels()
            {
                static std::vector<std::pair<std::ostream*, uint8_t>> levels;
                return levels;
            }
#endif

            static ll_state (&master_levels())[7]
            {
#ifdef LOGGER_LOGLEVEL_PER_STREAM    
                static ll_state levels[7] = {ll_state::per_stream, ll_state::per_stream, ll_state::per_stream, ll_state::per_stream, ll_state::per_stream, ll_state::per_stream, ll_state::on};
#else
                static ll_state levels[7] = {ll_state::on, ll_state::on, ll_state::on, ll_state::on, ll_state::on, ll_state::on, ll_state::on};
#endif
                return levels;
            }
        };

        inline std::vector<std::ostream*>& get_outputs() { return logger_state::outputs(); }

        static std::mutex output_mutex;

#ifdef LOGGER_LOGLEVEL_PER_STREAM    
        inline bool ll_enabled(uint8_t state, log_level ll)
        {
            return state & (1 << (int) ll);
        }

        inline void set_bits(uint8_t& bits, log_level mask) { bits |= 1 << (int) mask; }
        inline void clear_bits(uint8_t& bits, log_level mask) { bits &= ~(1 << (int) mask); }
#endif

#ifdef LOGGER_ASYNC
        struct async_log_message
        {
            std::string text;
            std::vector<std::ostream*> streams;
            bool no_line_break;
        };

        static struct async_state
        {
            std::queue<async_log_message> queue;
            std::mutex queue_mutex;
            std::condition_variable queue_cv;
            std::thread worker;
            bool running = false;

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
#ifdef LOGGER_ANSI
                            *stream << msg.text << ansi::reset_colours_code << (msg.no_line_break ? "" : "\n");
#else
                            *stream << msg.text << (msg.no_line_break ? "" : "\n");
#endif
                        }
                        lock.lock();
                    }
                }
            }
        } async_logger;
#endif

        struct stream_wrapper
        {
            std::vector<std::ostream*>* streams{nullptr};
            bool no_line_break{true};

            stream_wrapper(std::vector<std::ostream*>* s, bool nlb) : streams(s), no_line_break(nlb) {}

            ~stream_wrapper() 
            { 
                if (streams) 
                    for (auto* stream : *streams)
#ifdef LOGGER_ANSI
                        *stream << ansi::reset_colours_code << (no_line_break ? "" : "\n");
#else
                        *stream << (no_line_break ? "" : "\n");
#endif
            }

            stream_wrapper& operator<<(char c) 
            {
                if (streams)
                    for (auto* stream : *streams)
                        *stream << c;
                return *this;
            }

            stream_wrapper& operator<<(const char* s) 
            {
                if (streams)
                    for (auto* stream : *streams)
                        *stream << s;
                return *this;
            }

            stream_wrapper& operator<<(std::ostream& (*pf)(std::ostream&)) 
            {
                if (streams)
                    for (auto* stream : *streams)
                        pf(*stream);
                return *this;
            }

            template<typename T>
            stream_wrapper& operator<<(const T& value) 
            {
                if (streams)
                    for (auto* stream : *streams)
                        *stream << value;
                return *this;
            }
        };

#ifdef LOGGER_ANSI
        inline ansi::fg to_fg(logger::col c)
        {
            return static_cast<ansi::fg>(static_cast<int>(c) + 30);
        }

        inline ansi::bg to_bg(logger::col c)
        {
            return static_cast<ansi::bg>(static_cast<int>(c) + 40);
        }
#endif        

        inline int trailing_no_break_marker(std::string_view s)
        {            
            return s.ends_with('$') ? 1 : 0;            
        }

        template <typename... Args>
        void log_message(col colour, std::string_view prefix, std::string_view s, Args&&... args)
        {
            int pop_nobreak = trailing_no_break_marker(s);
            std::string_view formatted_str = s.substr(0, s.size() - pop_nobreak);
            std::string message = std::format("{}{}", logger::colorise(colour, prefix), 
                                              std::vformat(formatted_str, std::make_format_args(args...)));
            for (auto* stream : get_outputs())
                *stream << message;
        }

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

        static null_stream cnull;        

        static void initialise_outputs() 
        {
            if (get_outputs().empty())
                get_outputs().push_back(&cnull);
        }

        template <log_level Level, col Colour>
        struct log_traits
        {
            static constexpr log_level level = Level;
            static constexpr col colour = Colour;
        };

        template <typename Traits, typename... Args>
        stream_wrapper log(std::string_view prefix, std::string_view s = "", Args&&... args)
        {
            initialise_outputs();
            if (Traits::level != log_level::record && logger_state::master_levels()[(int) Traits::level] == ll_state::off)
                return stream_wrapper(nullptr, false);

            static thread_local std::vector<std::ostream*> active_streams;
            active_streams.clear();
            for (auto* stream : get_outputs())
            {
                if (Traits::level == log_level::record)
                {
                    active_streams.push_back(stream);
                    continue;
                }
                auto master_state = logger_state::master_levels()[(int) Traits::level];
                bool include = false;
                if (master_state == ll_state::on)
                    include = true;
                else if (master_state == ll_state::per_stream)
                {
#ifdef LOGGER_LOGLEVEL_PER_STREAM
                    for (const auto& [s, bits] : logger_state::log_levels())
                        if (s == stream)
                        {
                            include = ll_enabled(bits, Traits::level);
                            break;
                        }
                    if (!include && logger_state::log_levels().empty())
                        include = true;
#else
                    include = true;
#endif
                }
                if (include)
                    active_streams.push_back(stream);
            }
            if (active_streams.empty())
                return stream_wrapper(nullptr, false);

#ifdef LOGGER_ASYNC
            if (async_logger.running)
            {
                int pop_nobreak = trailing_no_break_marker(s);
                std::string formatted;
                if constexpr (sizeof...(args) == 0)
                    formatted = std::format("{}{}", logger::colorise(Traits::colour, prefix),
                                            s.substr(0, s.size() - pop_nobreak));
                else
                    formatted = std::format("{}{}", logger::colorise(Traits::colour, prefix),
                                            std::vformat(s.substr(0, s.size() - pop_nobreak), 
                                                         std::make_format_args(args...)));
                
                std::lock_guard<std::mutex> lock(async_logger.queue_mutex);
                async_logger.queue.push({std::move(formatted), std::move(active_streams), pop_nobreak == 1});
                async_logger.queue_cv.notify_one();
                return stream_wrapper(&async_logger.queue.back().streams, pop_nobreak == 1);
            }
#endif

            std::lock_guard<std::mutex> lock(output_mutex);
            int pop_nobreak = trailing_no_break_marker(s);
            std::string formatted;
            if constexpr (sizeof...(args) == 0)
                formatted = std::format("{}{}", logger::colorise(Traits::colour, prefix),
                                        s.substr(0, s.size() - pop_nobreak));
            else
                formatted = std::format("{}{}", logger::colorise(Traits::colour, prefix),
                                        std::vformat(s.substr(0, s.size() - pop_nobreak), 
                                                     std::make_format_args(args...)));
            for (auto* stream : active_streams)
                *stream << formatted;
            return stream_wrapper(&active_streams, pop_nobreak == 1);
        }
    }

    inline std::string colorise(col fg, std::string_view str)
    {
#ifdef LOGGER_ANSI
        return std::format("{}{}{}", ansi::colour_code(impl::to_fg(fg)), str, ansi::reset_colours_code);
#else
        return std::string(str);
#endif
    }

    inline auto debug    = [](std::string_view s = "", auto&&... args) { return impl::log<impl::log_traits<log_level::debug,    col::grey>>("", s, std::forward<decltype(args)>(args)...); };
    inline auto trace    = [](std::string_view s = "", auto&&... args) { return impl::log<impl::log_traits<log_level::trace,    col::light_blue>>("> ", s, std::forward<decltype(args)>(args)...); };
    inline auto info     = [](std::string_view s = "", auto&&... args) { return impl::log<impl::log_traits<log_level::info,     col::light_blue>>("info: ", s, std::forward<decltype(args)>(args)...); };
    inline auto warning  = [](std::string_view s = "", auto&&... args) { return impl::log<impl::log_traits<log_level::warning,  col::yellow>>("Warning: ", s, std::forward<decltype(args)>(args)...); };
    inline auto error    = [](std::string_view s = "", auto&&... args) { return impl::log<impl::log_traits<log_level::error,    col::light_red>>("ERROR: ", s, std::forward<decltype(args)>(args)...); };
    inline auto critical = [](std::string_view s = "", auto&&... args) { return impl::log<impl::log_traits<log_level::critical, col::light_red>>("CRITICAL: ", s, std::forward<decltype(args)>(args)...); };
    inline auto record   = [](std::string_view s = "", auto&&... args) { return impl::log<impl::log_traits<log_level::record,   col::grey>>("", s, std::forward<decltype(args)>(args)...); };

#ifdef LOGGER_ASYNC
    static void start_async()
    {
        std::lock_guard<std::mutex> lock(impl::async_logger.queue_mutex);
        if (!impl::async_logger.running)
        {
            impl::async_logger.running = true;
            impl::async_logger.worker = std::thread(&impl::async_state::process, &impl::async_logger);
        }
    }

    static void stop_async()
    {
        std::unique_lock<std::mutex> lock(impl::async_logger.queue_mutex);
        if (impl::async_logger.running)
        {
            impl::async_logger.running = false;
            lock.unlock();
            impl::async_logger.queue_cv.notify_one();
            impl::async_logger.worker.join();
            while (!impl::async_logger.queue.empty())
            {
                impl::async_log_message msg = std::move(impl::async_logger.queue.front());
                impl::async_logger.queue.pop();
                for (auto* stream : msg.streams)
#ifdef LOGGER_ANSI
                    *stream << msg.text << ansi::reset_colours_code << (msg.no_line_break ? "" : "\n");
#else
                    *stream << msg.text << (msg.no_line_break ? "" : "\n");
#endif
            }
        }
    }
#endif

    static void add_output(std::ostream* stream)
    {
        if (!stream) return;
        std::lock_guard<std::mutex> lock(impl::output_mutex);
        impl::initialise_outputs();
        impl::get_outputs().push_back(stream);
#ifdef LOGGER_LOGLEVEL_PER_STREAM    
        impl::logger_state::log_levels().emplace_back(stream, 0b1111111);
#endif
    }

    static void disable_log_level(log_level ll)
    {
        impl::logger_state::master_levels()[(int) ll] = impl::ll_state::off;
    }

    static void enable_log_level(log_level ll)
    {
        impl::logger_state::master_levels()[(int) ll] = impl::ll_state::on;
    }

#ifdef LOGGER_LOGLEVEL_PER_STREAM    
    static void delegate_log_level(log_level ll)
    {
        impl::logger_state::master_levels()[(int) ll] = impl::ll_state::per_stream;
    }

    static void disable_log_level_for_stream(std::ostream* s, log_level ll)
    {
        for (auto& [stream, bits] : impl::logger_state::log_levels())
            if (stream == s) impl::clear_bits(bits, ll);
    }

    static void enable_log_level_for_stream(std::ostream* s, log_level ll)
    {
        for (auto& [stream, bits] : impl::logger_state::log_levels())
            if (stream == s) impl::set_bits(bits, ll);
    }
#endif    
}

#endif // LOGGER_INCLUDE_GUARD