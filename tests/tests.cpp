#include "../include/rogu.hpp"
#include "../../maat/include/maat.hpp"

#include <sstream>
#include <string>
#include <thread>
#include <chrono>

//============================================================================
// Test helpers
//============================================================================

// Resets all logger state between tests. Accesses impl internals deliberately —
// this is test infrastructure, not production code.
inline void reset_rogu()
{
    rogu::impl::logger_state::entries().clear();
}

// Registers a single ostringstream as the sole output and returns a reference
// to the captured content. Caller must keep ss alive for the duration of the test.
inline void capture(std::ostringstream& ss)
{
    reset_rogu();
    rogu::add_output(&ss);
}

// Returns true if haystack contains needle.
inline bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

// Returns true if needle appears before other in haystack.
inline bool before(const std::string& haystack, const std::string& needle, const std::string& other)
{
    auto a = haystack.find(needle);
    auto b = haystack.find(other);
    return a != std::string::npos && b != std::string::npos && a < b;
}

inline std::string strip_ansi(const std::string& s)
{
    std::string out;
    std::size_t i = 0;
    while (i < s.size())
    {
        if (s[i] == '\033' && i + 1 < s.size() && s[i+1] == '[')
        {
            i += 2;
            while (i < s.size() && s[i] != 'm') ++i;
            ++i;
        }
        else
            out += s[i++];
    }
    return out;
}

//============================================================================
// OUTPUT CAPTURE
//============================================================================

bool test_no_output_before_registration()
{
    reset_rogu();
    // No stream registered — should not crash and should produce no output.
    // There is no observable side-effect to check other than absence of crash
    // and the null stream receiving the write silently.
    rogu::info("should be silent");
    return true;
}

using namespace maat;

bool test_output_reaches_stream()
{
    std::ostringstream ss;
    capture(ss);
    rogu::info("hello");
    REQUIRE(contains(ss.str(), "hello"), "message not found in output");
    return true;
}

bool test_multiple_streams()
{
    reset_rogu();
    std::ostringstream a, b;
    rogu::add_output(&a);
    rogu::add_output(&b);
    rogu::info("broadcast");
    REQUIRE(contains(a.str(), "broadcast"), "message not in first stream");
    REQUIRE(contains(b.str(), "broadcast"), "message not in second stream");
    return true;
}

bool test_newline_appended()
{
    std::ostringstream ss;
    capture(ss);
    rogu::info("line");
    REQUIRE(ss.str().back() == '\n', "newline not appended");
    return true;
}


//============================================================================
// LOG LEVELS
//============================================================================

bool test_level_prefixes()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{ll} {msg}");

    rogu::trace("t"); EXPECT(contains(strip_ansi(ss.str()), "trace"), "trace prefix missing");
    ss.str(""); ss.clear();
    rogu::debug("d"); EXPECT(contains(strip_ansi(ss.str()), "debug"), "debug prefix missing");
    ss.str(""); ss.clear();
    rogu::info("i"); EXPECT(contains(strip_ansi(ss.str()), "info"), "info prefix missing");
    ss.str(""); ss.clear();
    rogu::warning("w"); EXPECT(contains(strip_ansi(ss.str()), "warning"), "warning prefix missing");
    ss.str(""); ss.clear();
    rogu::error("e"); EXPECT(contains(strip_ansi(ss.str()), "error"), "error prefix missing");
    ss.str(""); ss.clear();
    rogu::critical("c"); EXPECT(contains(strip_ansi(ss.str()), "critical"), "critical prefix missing");
    return true;
}

bool test_record_always_outputs()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{msg}");
    rogu::disable_log_level(rogu::log_level::info);
    rogu::info("suppressed");
    rogu::record("always");
    rogu::enable_log_level(rogu::log_level::info);
    REQUIRE(!contains(ss.str(), "suppressed"), "disabled level should not appear");
    REQUIRE(contains(ss.str(), "always"), "record should always appear");
    return true;
}

bool test_disable_enable_level()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{msg}");
    rogu::disable_log_level(rogu::log_level::debug);
    rogu::debug("invisible");
    EXPECT(!contains(ss.str(), "invisible"), "disabled level should produce no output");
    rogu::enable_log_level(rogu::log_level::debug);
    rogu::debug("visible");
    EXPECT(contains(ss.str(), "visible"), "re-enabled level should produce output");
    return true;
}


//============================================================================
// PER-STREAM LEVEL CONTROL
//============================================================================

bool test_per_stream_level_control()
{
    reset_rogu();
    std::ostringstream verbose, quiet;
    rogu::add_output(&verbose);
    rogu::add_output(&quiet);
    rogu::set_formatter(&verbose, "{msg}");
    rogu::set_formatter(&quiet, "{msg}");

    rogu::delegate_log_level(rogu::log_level::debug);
    rogu::enable_log_level_for_stream(&verbose, rogu::log_level::debug);
    rogu::disable_log_level_for_stream(&quiet, rogu::log_level::debug);

    rogu::debug("selective");

    EXPECT(contains(verbose.str(), "selective"), "verbose stream should receive debug");
    EXPECT(!contains(quiet.str(), "selective"), "quiet stream should not receive debug");
    return true;
}

bool test_delegate_falls_back_to_per_stream_bits()
{
    reset_rogu();
    std::ostringstream ss;
    rogu::add_output(&ss);
    rogu::set_formatter(&ss, "{msg}");

    // With delegate and per-stream bit enabled, output should appear.
    rogu::delegate_log_level(rogu::log_level::info);
    rogu::enable_log_level_for_stream(&ss, rogu::log_level::info);
    rogu::info("present");
    REQUIRE(contains(ss.str(), "present"), "delegated+enabled level should produce output");

    ss.str(""); ss.clear();
    rogu::disable_log_level_for_stream(&ss, rogu::log_level::info);
    rogu::info("absent");
    REQUIRE(!contains(ss.str(), "absent"), "delegated+disabled level should produce no output");

    rogu::enable_log_level(rogu::log_level::info);
    return true;
}


//============================================================================
// FORMAT STRING / RENDERER
//============================================================================

bool test_default_field_order()
{
    std::ostringstream ss;
    capture(ss);
    // Default format: {time}{ll} {msg} ({trace})
    // With ROGU_TIMESTAMP and ROGU_SOURCE_LOCATION both defined,
    // time should appear before level, level before message.
    rogu::info("body");
    std::string out = ss.str();
#ifdef ROGU_TIMESTAMP
    EXPECT(before(out, ":", "info"), "timestamp should precede level");
#endif
    EXPECT(before(out, "info", "body"), "level should precede message");
    return true;
}

bool test_custom_format_string()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "[{ll}] {msg}");
    rogu::info("payload");
    std::string out = strip_ansi(ss.str());
    REQUIRE(contains(out, "[info]"), "custom format brackets not found");
    REQUIRE(contains(out, "payload"), "message not found");
    REQUIRE(before(out, "[info]", "payload"), "level should precede message");
    return true;
}

bool test_brace_escape()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{{ll}} {msg}");
    rogu::info("escaped");
    std::string out = ss.str();
    REQUIRE(contains(out, "{ll}"), "escaped token should appear literally");
    REQUIRE(!before(out, "info", "escaped"), "level should not be substituted when escaped");
    return true;
}

bool test_unrecognised_token_passthrough()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{unknown} {msg}");
    rogu::info("through");
    std::string out = ss.str();
    REQUIRE(contains(out, "{unknown}"), "unrecognised token should pass through literally");
    return true;
}

bool test_msg_token_absent()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{ll}");
    rogu::info("ignored");
    // Message body should not appear since {msg} is not in the format string.
    // The << continuation after the format render should still append.
    std::string out = ss.str();
    REQUIRE(!contains(out, "ignored"), "message should not appear when {msg} absent from format");
    return true;
}

bool test_stream_continuation_position()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{ll} {msg} end");
    rogu::info("mid") << "stream";
    std::string out = ss.str();
    // Expected: "info midstream end\n"
    // "stream" is appended by operator<< after {msg} substitution,
    // before after_msg ("end") is written by the destructor.
    REQUIRE(before(out, "mid", "end"), "message should precede after_msg");
    REQUIRE(before(out, "stream", "end"), "stream continuation should precede after_msg");
    REQUIRE(before(out, "info", "mid"), "level should precede message");
    return true;
}

bool test_source_location_present()
{
#ifdef ROGU_SOURCE_LOCATION
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{msg} {trace}");
    rogu::info("located");
    std::string out = ss.str();
    REQUIRE(contains(out, "tests.cpp"), "source filename should appear in output");
#endif
    return true;
}

bool test_timestamp_present()
{
#ifdef ROGU_TIMESTAMP
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{time}{msg}");
    rogu::info("timed");
    std::string out = ss.str();
    // Timestamp is HH:MM:SS — check for the colon separator as a proxy.
    REQUIRE(contains(out, ":"), "timestamp colon separator not found");
    REQUIRE(before(out, ":", "timed"), "timestamp should precede message");
#endif
    return true;
}


//============================================================================
// FLAGS
//============================================================================

bool test_no_break_suppresses_newline()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{msg}");
    rogu::info(rogu::no_break, "partial");
    REQUIRE(ss.str().back() != '\n', "no_break should suppress newline");
    return true;
}

bool test_no_break_continuation()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{msg}");
    rogu::info(rogu::no_break, "first ");
    rogu::info(rogu::msg_only, "second");
    std::string out = strip_ansi(ss.str());
    REQUIRE(contains(out, "first second"), "no_break continuation should produce joined output");
    return true;
}

bool test_msg_only_suppresses_decoration()
{
    std::ostringstream ss;
    capture(ss);
    // Default format includes level — msg_only should suppress it.
    rogu::set_formatter(&ss, "{ll} {msg}");
    rogu::info(rogu::msg_only, "bare");
    std::string out = ss.str();
    REQUIRE(!contains(out, "info"), "msg_only should suppress level field");
    REQUIRE(contains(out, "bare"), "message should still appear with msg_only");
    return true;
}

bool test_no_time_suppresses_timestamp()
{
#ifdef ROGU_TIMESTAMP
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{time}{msg}");
    rogu::info(rogu::no_time, "untimestamped");
    std::string out = ss.str();
    // If no_time works, there should be no digit sequences before the message
    // that look like a timestamp. Check that the output starts with the message.
    REQUIRE(out.find("untimestamped") == 0, "no_time should suppress timestamp; message should be first");
#endif
    return true;
}

bool test_force_time_overrides_no_time()
{
#ifdef ROGU_TIMESTAMP
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{time}{msg}");
    rogu::info(rogu::no_time | rogu::force_time, "forcedtime");
    std::string out = ss.str();
    REQUIRE(before(out, ":", "forcedtime"), "force_time should override no_time and emit timestamp");
#endif
    return true;
}


//============================================================================
// FORMAT STYLE
//============================================================================

bool test_format_args_substituted()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{msg}");
    rogu::info("{} + {} = {}", 1, 2, 3);
    REQUIRE(contains(ss.str(), "1 + 2 = 3"), "format args should be substituted in message");
    return true;
}

bool test_stream_only_call()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{msg}");
    rogu::info() << "streamed";
    REQUIRE(contains(ss.str(), "streamed"), "stream-only call should produce output");
    return true;
}

bool test_mixed_format_and_stream()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{msg}");
    rogu::info("fmt ") << "stream";
    REQUIRE(contains(ss.str(), "fmt stream"), "mixed format+stream should concatenate");
    return true;
}


//============================================================================
// ASYNC
//============================================================================

#ifdef ROGU_ASYNC
bool test_async_messages_reach_stream()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{msg}");
    rogu::start_async();
    rogu::info("async_msg");
    rogu::stop_async();
    REQUIRE(contains(ss.str(), "async_msg"), "async message should reach stream after stop_async");
    return true;
}

bool test_async_ordering_single_thread()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{msg}");
    rogu::start_async();
    rogu::info("first");
    rogu::info("second");
    rogu::info("third");
    rogu::stop_async();
    std::string out = ss.str();
    REQUIRE(before(out, "first", "second"), "async messages should be ordered: first before second");
    REQUIRE(before(out, "second", "third"), "async messages should be ordered: second before third");
    return true;
}

bool test_async_record_bypasses_filter()
{
    std::ostringstream ss;
    capture(ss);
    rogu::set_formatter(&ss, "{msg}");
    rogu::disable_log_level(rogu::log_level::info);
    rogu::start_async();
    rogu::info("suppressed");
    rogu::record("always");
    rogu::stop_async();
    rogu::enable_log_level(rogu::log_level::info);
    REQUIRE(!contains(ss.str(), "suppressed"), "disabled level should not appear in async mode");
    REQUIRE(contains(ss.str(), "always"), "record should always appear in async mode");
    return true;
}
#endif


//============================================================================
// MAIN
//============================================================================

int main()
{
    SUBCAT("output capture");
    RUN_TEST(test_no_output_before_registration);
    RUN_TEST(test_output_reaches_stream);
    RUN_TEST(test_multiple_streams);
    RUN_TEST(test_newline_appended);

    SUBCAT("log levels");
    RUN_TEST(test_level_prefixes);
    RUN_TEST(test_record_always_outputs);
    RUN_TEST(test_disable_enable_level);

    SUBCAT("per-stream level control");
    RUN_TEST(test_per_stream_level_control);
    RUN_TEST(test_delegate_falls_back_to_per_stream_bits);

    SUBCAT("format string and renderer");
    RUN_TEST(test_default_field_order);
    RUN_TEST(test_custom_format_string);
    RUN_TEST(test_brace_escape);
    RUN_TEST(test_unrecognised_token_passthrough);
    RUN_TEST(test_msg_token_absent);
    RUN_TEST(test_stream_continuation_position);
    RUN_TEST(test_source_location_present);
    RUN_TEST(test_timestamp_present);

    SUBCAT("flags");
    RUN_TEST(test_no_break_suppresses_newline);
    RUN_TEST(test_no_break_continuation);
    RUN_TEST(test_msg_only_suppresses_decoration);
    RUN_TEST(test_no_time_suppresses_timestamp);
    RUN_TEST(test_force_time_overrides_no_time);

    SUBCAT("call syntax");
    RUN_TEST(test_format_args_substituted);
    RUN_TEST(test_stream_only_call);
    RUN_TEST(test_mixed_format_and_stream);

#ifdef ROGU_ASYNC
    SUBCAT("async");
    RUN_TEST(test_async_messages_reach_stream);
    RUN_TEST(test_async_ordering_single_thread);
    RUN_TEST(test_async_record_bypasses_filter);
#endif

    maat::print_summary();
    return 0;
}