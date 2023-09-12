// Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved
//
// https://snapwebsites.org/project/csspp
// contact@m2osw.com
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

/** \file
 * \brief csspp main unit test.
 *
 * This test suite uses catch.hpp, for details see:
 *
 *   https://github.com/philsquared/Catch/blob/master/docs/tutorial.md
 */

// Tell catch we want it to add the runner code in this file.
#define CATCH_CONFIG_RUNNER

// There seem to be a conflict between our csspp headers and the catch
// environment which ends up throwing an error about a missing noexcept
//
#pragma GCC diagnostic ignored "-Wnoexcept"


// csspp
//
#include    <csspp/csspp.h>
#include    <csspp/error.h>
#include    <csspp/node.h>


// self
//
#include    "catch_main.h"


// libexcept
//
#include    <libexcept/exception.h>


// snapdev
//
#include    <snapdev/not_used.h>


// C++
//
#include    <cstring>


// C
//
#include    <stdlib.h>


// last include
//
#include    <snapdev/poison.h>


namespace csspp_test
{

// static variables
namespace
{

trace_error * g_trace_error;

time_t const g_now(1435871798); // 07/02/2015 14:16:38

}

trace_error::trace_error()
{
    csspp::error::instance().set_error_stream(m_error_message);
}

trace_error & trace_error::instance()
{
    if(g_trace_error == nullptr)
    {
        g_trace_error = new trace_error();
    }
    return *g_trace_error;
}

void trace_error::expected_error(std::string const & msg, char const * filename, int line)
{
    std::string e(m_error_message.str());
    m_error_message.str("");

    std::string::size_type pos(e.find("/scripts"));
    if(pos != std::string::npos)
    {
        e = e.substr(pos + 1);
    }

//std::cerr << "require " << msg << "\n";
//std::cerr << "    got " << e << "\n";
    if(e != msg)
    {
        // print a message otherwise filename & line get lost
        std::cerr << filename << "(" << line << "): error: error messages are not equal.\n"; // LCOV_EXCL_LINE
    }
    CATCH_REQUIRE(e == msg);
}

our_unicode_range_t::our_unicode_range_t(csspp::wide_char_t start, csspp::wide_char_t end)
    : f_start(start)
    , f_end(end)
{
}

void our_unicode_range_t::set_start(csspp::wide_char_t start)
{
    f_start = start;
}

void our_unicode_range_t::set_end(csspp::wide_char_t end)
{
    f_end = end;
}

void our_unicode_range_t::set_range(csspp::range_value_t range)
{
    f_start = static_cast<csspp::wide_char_t>(range);
    f_end = static_cast<csspp::wide_char_t>(range >> 32);
}

csspp::wide_char_t our_unicode_range_t::get_start() const
{
    return f_start;
}

csspp::wide_char_t our_unicode_range_t::get_end() const
{
    return f_end;
}

csspp::range_value_t our_unicode_range_t::get_range() const
{
    return f_start + (static_cast<csspp::range_value_t>(f_end) << 32);
}

void compare(std::string const & generated, std::string const & expected, char const * filename, int line)
{
    char const *g(generated.c_str());
    char const *e(expected.c_str());

    int pos(1);
    for(; *g != '\0' && *e != '\0'; ++pos)
    {
        // one line from the left
        char const *start;
        for(start = g; *g != '\n' && *g != '\0'; ++g);
        std::string gs(start, g - start);
        if(*g == '\n')
        {
            ++g;
        }

        // one line from the right
        start = e;
        for(start = e; *e != '\n' && *e != '\0'; ++e);
        std::string es(start, e - start);
        if(*e == '\n')
        {
            ++e;
        }

        if(gs != es)
        {
            std::cerr << filename << "(" << line << "):error: compare trees: on line " << pos << ": \"" << gs << "\" != \"" << es << "\".\n"; // LCOV_EXCL_LINE
        }
        CATCH_REQUIRE(gs == es);
    }

    if(*g != '\0' && *e != '\0')
    {
        throw std::logic_error("we reached this line when the previous while() implies at least one of g or e is pointing to '\\0'."); // LCOV_EXCL_LINE
    }

    if(*g != '\0')
    {
        std::cerr << filename << "(" << line << "):error: compare trees: on line " << pos << ": end of expected reached, still have \"" << g << "\" left in generated.\n"; // LCOV_EXCL_LINE
    }
    CATCH_REQUIRE(*g == '\0');

    if(*e != '\0')
    {
        std::cerr << filename << "(" << line << "):error: compare trees: on line " << pos << ": end of generated reached, still have \"" << e << "\" left in expected.\n"; // LCOV_EXCL_LINE
    }
    CATCH_REQUIRE(*e == '\0');
}


std::string g_script_path;
std::string g_version_script_path;
bool        g_show_errors;


std::string get_script_path()
{
    return g_script_path;
}


std::string get_version_script_path()
{
    return g_version_script_path;
}


std::string get_default_variables(default_variables_flags_t const flags)
{
#define STRINGIFY_CONTENT(str)  #str
#define STRINGIFY(str)  STRINGIFY_CONTENT(str)
    return

std::string() +

"    V:_csspp_day\n"
"      LIST\n"
"        VARIABLE \"_csspp_day\"\n"
"        STRING \"02\"\n"
"    V:_csspp_e\n"
"      LIST\n"
"        VARIABLE \"_csspp_e\"\n"
"        DECIMAL_NUMBER \"\" D:2.718\n"
"    V:_csspp_hour\n"
"      LIST\n"
"        VARIABLE \"_csspp_hour\"\n"
"        STRING \"14\"\n"
"    V:_csspp_ln10e\n"
"      LIST\n"
"        VARIABLE \"_csspp_ln10e\"\n"
"        DECIMAL_NUMBER \"\" D:2.303\n"
"    V:_csspp_ln2e\n"
"      LIST\n"
"        VARIABLE \"_csspp_ln2e\"\n"
"        DECIMAL_NUMBER \"\" D:0.693\n"
"    V:_csspp_log10e\n"
"      LIST\n"
"        VARIABLE \"_csspp_log10e\"\n"
"        DECIMAL_NUMBER \"\" D:0.434\n"
"    V:_csspp_log2e\n"
"      LIST\n"
"        VARIABLE \"_csspp_log2e\"\n"
"        DECIMAL_NUMBER \"\" D:1.443\n"
"    V:_csspp_major\n"
"      LIST\n"
"        VARIABLE \"_csspp_major\"\n"
"        INTEGER \"\" I:" STRINGIFY(CSSPP_VERSION_MAJOR) "\n"
"    V:_csspp_minor\n"
"      LIST\n"
"        VARIABLE \"_csspp_minor\"\n"
"        INTEGER \"\" I:" STRINGIFY(CSSPP_VERSION_MINOR) "\n"
"    V:_csspp_minute\n"
"      LIST\n"
"        VARIABLE \"_csspp_minute\"\n"
"        STRING \"16\"\n"
"    V:_csspp_month\n"
"      LIST\n"
"        VARIABLE \"_csspp_month\"\n"
"        STRING \"07\"\n"
"    V:_csspp_no_logo\n"
"      LIST\n"
"        VARIABLE \"_csspp_no_logo\"\n"
"        BOOLEAN B:" + ((flags & flag_no_logo_true) != 0 ? "true" : "false") + "\n"
"    V:_csspp_patch\n"
"      LIST\n"
"        VARIABLE \"_csspp_patch\"\n"
"        INTEGER \"\" I:" STRINGIFY(CSSPP_VERSION_PATCH) "\n"
"    V:_csspp_pi\n"
"      LIST\n"
"        VARIABLE \"_csspp_pi\"\n"
"        DECIMAL_NUMBER \"\" D:3.142\n"
"    V:_csspp_pi_rad\n"
"      LIST\n"
"        VARIABLE \"_csspp_pi_rad\"\n"
"        DECIMAL_NUMBER \"rad\" D:3.142\n"
"    V:_csspp_second\n"
"      LIST\n"
"        VARIABLE \"_csspp_second\"\n"
"        STRING \"38\"\n"
"    V:_csspp_sqrt2\n"
"      LIST\n"
"        VARIABLE \"_csspp_sqrt2\"\n"
"        DECIMAL_NUMBER \"\" D:1.414\n"
"    V:_csspp_time\n"
"      LIST\n"
"        VARIABLE \"_csspp_time\"\n"
"        STRING \"14:16:38\"\n"
"    V:_csspp_usdate\n"
"      LIST\n"
"        VARIABLE \"_csspp_usdate\"\n"
"        STRING \"07/02/2015\"\n"
"    V:_csspp_version\n"
"      LIST\n"
"        VARIABLE \"_csspp_version\"\n"
"        STRING \"" CSSPP_VERSION "\"\n"
"    V:_csspp_year\n"
"      LIST\n"
"        VARIABLE \"_csspp_year\"\n"
"        STRING \"2015\"\n"
"    V:adjust_hue\n"
"      LIST\n"
"        FUNCTION \"adjust_hue\"\n"
"          ARG\n"
"            VARIABLE \"color\"\n"
"          ARG\n"
"            VARIABLE \"angle\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"hsla\"\n"
"                FUNCTION \"deg2rad\"\n"
"                  FUNCTION \"hue\"\n"
"                    VARIABLE \"color\"\n"
"                WHITESPACE\n"
"                ADD\n"
"                WHITESPACE\n"
"                VARIABLE \"angle\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                FUNCTION \"saturation\"\n"
"                  VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                FUNCTION \"lightness\"\n"
"                  VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                FUNCTION \"alpha\"\n"
"                  VARIABLE \"color\"\n"
"    V:complement\n"
"      LIST\n"
"        FUNCTION \"complement\"\n"
"          ARG\n"
"            VARIABLE \"color\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"adjust_hue\"\n"
"                VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                VARIABLE \"_csspp_pi_rad\"\n"
"    V:darken\n"
"      LIST\n"
"        FUNCTION \"darken\"\n"
"          ARG\n"
"            VARIABLE \"color\"\n"
"          ARG\n"
"            VARIABLE \"percent\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"lighten\"\n"
"                VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                SUBTRACT\n"
"                WHITESPACE\n"
"                VARIABLE \"percent\"\n"
"    V:deg2rad\n"
"      LIST\n"
"        FUNCTION \"deg2rad\"\n"
"          ARG\n"
"            VARIABLE \"angle\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              VARIABLE \"angle\"\n"
"              WHITESPACE\n"
"              MULTIPLY\n"
"              WHITESPACE\n"
"              VARIABLE \"_csspp_pi_rad\"\n"
"              WHITESPACE\n"
"              DIVIDE\n"
"              WHITESPACE\n"
"              INTEGER \"deg\" I:180\n"
"    V:desaturate\n"
"      LIST\n"
"        FUNCTION \"desaturate\"\n"
"          ARG\n"
"            VARIABLE \"color\"\n"
"          ARG\n"
"            VARIABLE \"percent\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"saturate\"\n"
"                VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                SUBTRACT\n"
"                WHITESPACE\n"
"                VARIABLE \"percent\"\n"
"    V:fade_in\n"
"      LIST\n"
"        FUNCTION \"fade_in\"\n"
"          ARG\n"
"            VARIABLE \"color\"\n"
"          ARG\n"
"            VARIABLE \"number\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"opacify\"\n"
"                VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                VARIABLE \"number\"\n"
"    V:fade_out\n"
"      LIST\n"
"        FUNCTION \"fade_out\"\n"
"          ARG\n"
"            VARIABLE \"color\"\n"
"          ARG\n"
"            VARIABLE \"number\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"transparentize\"\n"
"                VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                VARIABLE \"number\"\n"
"    V:grayscale\n"
"      LIST\n"
"        FUNCTION \"grayscale\"\n"
"          ARG\n"
"            VARIABLE \"color\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"saturate\"\n"
"                VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                SUBTRACT\n"
"                WHITESPACE\n"
"                FUNCTION \"saturation\"\n"
"                  VARIABLE \"color\"\n"
"    V:invert\n"
"      LIST\n"
"        FUNCTION \"invert\"\n"
"          ARG\n"
"            VARIABLE \"color\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"rgba\"\n"
"                DECIMAL_NUMBER \"\" D:255\n"
"                WHITESPACE\n"
"                SUBTRACT\n"
"                WHITESPACE\n"
"                FUNCTION \"red\"\n"
"                  VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                DECIMAL_NUMBER \"\" D:255\n"
"                WHITESPACE\n"
"                SUBTRACT\n"
"                WHITESPACE\n"
"                FUNCTION \"green\"\n"
"                  VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                DECIMAL_NUMBER \"\" D:255\n"
"                WHITESPACE\n"
"                SUBTRACT\n"
"                WHITESPACE\n"
"                FUNCTION \"blue\"\n"
"                  VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                FUNCTION \"alpha\"\n"
"                  VARIABLE \"color\"\n"
"    V:lighten\n"
"      LIST\n"
"        FUNCTION \"lighten\"\n"
"          ARG\n"
"            VARIABLE \"color\"\n"
"          ARG\n"
"            VARIABLE \"percent\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"hsla\"\n"
"                FUNCTION \"hue\"\n"
"                  VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                FUNCTION \"saturation\"\n"
"                  VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                FUNCTION \"lightness\"\n"
"                  VARIABLE \"color\"\n"
"                WHITESPACE\n"
"                ADD\n"
"                WHITESPACE\n"
"                VARIABLE \"percent\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                FUNCTION \"alpha\"\n"
"                  VARIABLE \"color\"\n"
"    V:mix\n"
"      LIST\n"
"        FUNCTION \"mix\"\n"
"          ARG\n"
"            VARIABLE \"color1\"\n"
"          ARG\n"
"            VARIABLE \"color2\"\n"
"          ARG\n"
"            VARIABLE \"weight\"\n"
"            DECIMAL_NUMBER \"\" D:0.5\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              VARIABLE \"color1\"\n"
"              WHITESPACE\n"
"              MULTIPLY\n"
"              WHITESPACE\n"
"              VARIABLE \"weight\"\n"
"              WHITESPACE\n"
"              ADD\n"
"              WHITESPACE\n"
"              VARIABLE \"color2\"\n"
"              WHITESPACE\n"
"              MULTIPLY\n"
"              OPEN_PARENTHESIS\n"
"                DECIMAL_NUMBER \"\" D:1\n"
"                WHITESPACE\n"
"                SUBTRACT\n"
"                WHITESPACE\n"
"                VARIABLE \"weight\"\n"
"    V:opacify\n"
"      LIST\n"
"        FUNCTION \"opacify\"\n"
"          ARG\n"
"            VARIABLE \"color\"\n"
"          ARG\n"
"            VARIABLE \"number\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"rgba\"\n"
"                VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                FUNCTION \"alpha\"\n"
"                  VARIABLE \"color\"\n"
"                WHITESPACE\n"
"                ADD\n"
"                WHITESPACE\n"
"                VARIABLE \"number\"\n"
"    V:opacity\n"
"      LIST\n"
"        FUNCTION \"opacity\"\n"
"          ARG\n"
"            VARIABLE \"color\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"alpha\"\n"
"                VARIABLE \"color\"\n"
"    V:quote\n"
"      LIST\n"
"        FUNCTION \"quote\"\n"
"          ARG\n"
"            VARIABLE \"identifier\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"string\"\n"
"                VARIABLE \"identifier\"\n"
"    V:remove_unit\n"
"      LIST\n"
"        FUNCTION \"remove_unit\"\n"
"          ARG\n"
"            VARIABLE \"value\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"unit\"\n"
"                VARIABLE \"value\"\n"
"              WHITESPACE\n"
"              EQUAL\n"
"              WHITESPACE\n"
"              STRING \"%\"\n"
"              WHITESPACE\n"
"              CONDITIONAL\n"
"              WHITESPACE\n"
"              FUNCTION \"decimal_number\"\n"
"                VARIABLE \"value\"\n"
"              WHITESPACE\n"
"              COLON\n"
"              WHITESPACE\n"
"              FUNCTION \"type_of\"\n"
"                VARIABLE \"value\"\n"
"              WHITESPACE\n"
"              EQUAL\n"
"              WHITESPACE\n"
"              STRING \"integer\"\n"
"              WHITESPACE\n"
"              CONDITIONAL\n"
"              WHITESPACE\n"
"              VARIABLE \"value\"\n"
"              WHITESPACE\n"
"              DIVIDE\n"
"              WHITESPACE\n"
"              FUNCTION \"integer\"\n"
"                STRING \"1\"\n"
"                WHITESPACE\n"
"                ADD\n"
"                WHITESPACE\n"
"                FUNCTION \"unit\"\n"
"                  VARIABLE \"value\"\n"
"              WHITESPACE\n"
"              COLON\n"
"              WHITESPACE\n"
"              VARIABLE \"value\"\n"
"              WHITESPACE\n"
"              DIVIDE\n"
"              WHITESPACE\n"
"              FUNCTION \"decimal_number\"\n"
"                STRING \"1\"\n"
"                WHITESPACE\n"
"                ADD\n"
"                WHITESPACE\n"
"                FUNCTION \"unit\"\n"
"                  VARIABLE \"value\"\n"
"    V:saturate\n"
"      LIST\n"
"        FUNCTION \"saturate\"\n"
"          ARG\n"
"            VARIABLE \"color\"\n"
"          ARG\n"
"            VARIABLE \"percent\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"hsla\"\n"
"                FUNCTION \"hue\"\n"
"                  VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                FUNCTION \"saturation\"\n"
"                  VARIABLE \"color\"\n"
"                WHITESPACE\n"
"                ADD\n"
"                WHITESPACE\n"
"                VARIABLE \"percent\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                FUNCTION \"lightness\"\n"
"                  VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                FUNCTION \"alpha\"\n"
"                  VARIABLE \"color\"\n"
"    V:set_unit\n"
"      LIST\n"
"        FUNCTION \"set_unit\"\n"
"          ARG\n"
"            VARIABLE \"value\"\n"
"          ARG\n"
"            VARIABLE \"unit\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"string\"\n"
"                VARIABLE \"unit\"\n"
"              WHITESPACE\n"
"              EQUAL\n"
"              WHITESPACE\n"
"              STRING \"%\"\n"
"              WHITESPACE\n"
"              CONDITIONAL\n"
"              WHITESPACE\n"
"              FUNCTION \"percentage\"\n"
"                VARIABLE \"value\"\n"
"              WHITESPACE\n"
"              COLON\n"
"              WHITESPACE\n"
"              FUNCTION \"type_of\"\n"
"                VARIABLE \"value\"\n"
"              WHITESPACE\n"
"              EQUAL\n"
"              WHITESPACE\n"
"              STRING \"integer\"\n"
"              WHITESPACE\n"
"              CONDITIONAL\n"
"              WHITESPACE\n"
"              FUNCTION \"integer\"\n"
"                FUNCTION \"string\"\n"
"                  FUNCTION \"remove_unit\"\n"
"                    VARIABLE \"value\"\n"
"                WHITESPACE\n"
"                ADD\n"
"                WHITESPACE\n"
"                FUNCTION \"string\"\n"
"                  VARIABLE \"unit\"\n"
"              WHITESPACE\n"
"              COLON\n"
"              WHITESPACE\n"
"              FUNCTION \"decimal_number\"\n"
"                FUNCTION \"string\"\n"
"                  FUNCTION \"remove_unit\"\n"
"                    VARIABLE \"value\"\n"
"                WHITESPACE\n"
"                ADD\n"
"                WHITESPACE\n"
"                FUNCTION \"string\"\n"
"                  VARIABLE \"unit\"\n"
"    V:transparentize\n"
"      LIST\n"
"        FUNCTION \"transparentize\"\n"
"          ARG\n"
"            VARIABLE \"color\"\n"
"          ARG\n"
"            VARIABLE \"number\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"rgba\"\n"
"                VARIABLE \"color\"\n"
"                COMMA\n"
"                WHITESPACE\n"
"                FUNCTION \"alpha\"\n"
"                  VARIABLE \"color\"\n"
"                WHITESPACE\n"
"                SUBTRACT\n"
"                WHITESPACE\n"
"                VARIABLE \"number\"\n"
"    V:unitless\n"
"      LIST\n"
"        FUNCTION \"unitless\"\n"
"          ARG\n"
"            VARIABLE \"number\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"unit\"\n"
"                VARIABLE \"number\"\n"
"              WHITESPACE\n"
"              EQUAL\n"
"              WHITESPACE\n"
"              STRING \"\"\n"
"    V:unquote\n"
"      LIST\n"
"        FUNCTION \"unquote\"\n"
"          ARG\n"
"            VARIABLE \"string\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              FUNCTION \"identifier\"\n"
"                VARIABLE \"string\"\n"

;

#undef STRINGIFY
#undef STRINGIFY_CONTENT
}

std::string get_close_comment(bool token)
{
    if(token)
    {
        return  "  COMMENT \"@preserve -- CSS file parsed by http://csspp.org/ v" CSSPP_VERSION " on 07/02/2015\" I:1\n";
    }
    else
    {
        return "/* @preserve -- CSS file parsed by http://csspp.org/ v" CSSPP_VERSION " on 07/02/2015 */\n";
    }
}

time_t get_now()
{
    return g_now;
}

Catch::Clara::Parser add_command_line_options(Catch::Clara::Parser const & cli)
{
    return cli
         | Catch::Clara::Opt(g_show_errors)
            ["--show-errors"]
            ("make the csspp compile more verbose, which means printing all errors.")
         | Catch::Clara::Opt(g_script_path, "scripts")
            ["--scripts"]
            ("specify the location of the CSS Preprocessor system scripts.")
         | Catch::Clara::Opt(g_version_script_path, "version-script")
            ["--version-script"]
            ("define the path to the version script.")
         ;
}

int init_test(Catch::Session & session)
{
    snapdev::NOT_USED(session);

    // unless we get a loop going forever, we should never hit this limit
    //
    csspp::node::limit_nodes_to(1'000'000);

    csspp::error::instance().set_verbose(g_show_errors);

    // before running we need to initialize the error tracker
    //
    snapdev::NOT_USED(csspp_test::trace_error::instance());

    return 0;
}

} // csspp_test namespace

int main(int argc, char *argv[])
{
    return SNAP_CATCH2_NAMESPACE::snap_catch2_main(
              "eventdispatcher"
            , CSSPP_VERSION
            , argc
            , argv
            , []() { libexcept::set_collect_stack(libexcept::collect_stack_t::COLLECT_STACK_NO); }
            , &csspp_test::add_command_line_options
            , &csspp_test::init_test
        );
}

// vim: ts=4 sw=4 et
