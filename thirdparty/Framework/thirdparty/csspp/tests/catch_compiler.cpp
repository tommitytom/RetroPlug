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
 * \brief Test the compiler.cpp file.
 *
 * This test runs a battery of tests agains the compiler.cpp file to ensure
 * full coverage and many edge cases as expected by CSS 3 and many of the
 * CSS Preprocessor extensions.
 */

// csspp
//
#include    <csspp/compiler.h>

#include    <csspp/exception.h>
#include    <csspp/parser.h>


// self
//
#include    "catch_main.h"


// C++
//
#include    <fstream>
#include    <iostream>
#include    <sstream>


// C
//
#include    <string.h>
#include    <unistd.h>
#include    <sys/stat.h>


// last include
//
#include    <snapdev/poison.h>



void free_char(char * ptr)
{
    free(ptr);
}

CATCH_TEST_CASE("Compile set_date_time_variables() called too soon", "[compiler] [invalid]")
{
        csspp::compiler c;
        CATCH_REQUIRE_THROWS_AS(c.set_date_time_variables(csspp_test::get_now()), csspp::csspp_exception_logic);
}

CATCH_TEST_CASE("Compile simple stylesheets", "[compiler] [stylesheet] [attribute]")
{
    // with many spaces
    {
        std::stringstream ss;
        ss << "/* testing compile */"
           << "body, a[q] > b[p=\"344.5\"] + c[z=33] ~ d[e], html *[ ff = fire ] *.blue { background : white url( /images/background.png ) }"
           << "/* @preserver test \"Compile Simple Stylesheet\" */";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"body\"\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"q\"\n"
"      GREATER_THAN\n"
"      IDENTIFIER \"b\"\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"p\"\n"
"        EQUAL\n"
"        STRING \"344.5\"\n"
"      ADD\n"
"      IDENTIFIER \"c\"\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"z\"\n"
"        EQUAL\n"
"        INTEGER \"\" I:33\n"
"      PRECEDED\n"
"      IDENTIFIER \"d\"\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"e\"\n"
"    ARG\n"
"      IDENTIFIER \"html\"\n"
"      WHITESPACE\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"ff\"\n"
"        EQUAL\n"
"        IDENTIFIER \"fire\"\n"
"      WHITESPACE\n"
"      PERIOD\n"
"      IDENTIFIER \"blue\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"background\"\n"
"        ARG\n"
"          COLOR H:ffffffff\n"
"          WHITESPACE\n"
"          URL \"/images/background.png\"\n"
"  COMMENT \"@preserver test \"Compile Simple Stylesheet\"\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // without spaces
    {
        std::stringstream ss;
        ss << "/* testing compile */"
           << "body,a[q]>b[p=\"344.5\"]+c[z=33]~d[e],html *[ff=fire] *.blue { background:white url(/images/background.png) }"
           << "/* @preserver test \"Compile Simple Stylesheet\" with version #{$_csspp_major}.#{$_csspp_minor} */";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"body\"\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"q\"\n"
"      GREATER_THAN\n"
"      IDENTIFIER \"b\"\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"p\"\n"
"        EQUAL\n"
"        STRING \"344.5\"\n"
"      ADD\n"
"      IDENTIFIER \"c\"\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"z\"\n"
"        EQUAL\n"
"        INTEGER \"\" I:33\n"
"      PRECEDED\n"
"      IDENTIFIER \"d\"\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"e\"\n"
"    ARG\n"
"      IDENTIFIER \"html\"\n"
"      WHITESPACE\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"ff\"\n"
"        EQUAL\n"
"        IDENTIFIER \"fire\"\n"
"      WHITESPACE\n"
"      PERIOD\n"
"      IDENTIFIER \"blue\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"background\"\n"
"        ARG\n"
"          COLOR H:ffffffff\n"
"          WHITESPACE\n"
"          URL \"/images/background.png\"\n"
"  COMMENT \"@preserver test \"Compile Simple Stylesheet\" with version 1.0\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // rules with !important
    {
        std::stringstream ss;
        ss << "div.blackness { color: red !important }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"      PERIOD\n"
"      IDENTIFIER \"blackness\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\" F:important\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // rules with ! important
    {
        std::stringstream ss;
        ss << "div.blackness { color: red ! important }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"      PERIOD\n"
"      IDENTIFIER \"blackness\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\" F:important\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // rules with !important and no spaces
    {
        std::stringstream ss;
        ss << "div.blackness { color: red!important }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"      PERIOD\n"
"      IDENTIFIER \"blackness\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\" F:important\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // empty rules have to compile too
    {
        std::stringstream ss;
        ss << "div.blackness section.light span.clear\n"
           << "{\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables()
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // special IE8 value which has to be skipped
    {
        std::stringstream ss;
        ss << ".transparent img\n"
           << "{\n"
           << "  $alpha: 5% * 4;\n"
           << "  filter: opacity($alpha);\n"
           << "  filter: alpha( opacity=20 );\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        // no error left over
        VERIFY_ERRORS(
                "test.css(4): warning: the alpha(), chroma() and similar functions of the filter field are Internet Explorer specific extensions which are not supported across browsers.\n"
                "test.css(5): warning: the alpha(), chroma() and similar functions of the filter field are Internet Explorer specific extensions which are not supported across browsers.\n"
            );

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      PERIOD\n"
"      IDENTIFIER \"transparent\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"img\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"        V:alpha\n"
"          LIST\n"
"            VARIABLE \"alpha\"\n"
"            LIST\n"
"              PERCENT D:0.05\n"
"              WHITESPACE\n"
"              MULTIPLY\n"
"              WHITESPACE\n"
"              INTEGER \"\" I:4\n"
"      LIST\n"
"        DECLARATION \"filter\"\n"
"          FUNCTION \"opacity\"\n"
"            PERCENT D:0.05\n"
"            WHITESPACE\n"
"            MULTIPLY\n"
"            WHITESPACE\n"
"            INTEGER \"\" I:4\n"
"        DECLARATION \"filter\"\n"
"          FUNCTION \"alpha\"\n"
"            IDENTIFIER \"opacity\"\n"
"            EQUAL\n"
"            INTEGER \"\" I:20\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // a simple test with '--no-logo' specified
    {
        std::stringstream ss;
        ss << ".box\n"
           << "{\n"
           << "  color: $_csspp_no_logo ? red : blue;\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.set_no_logo();
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        // no error left over
        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables(csspp_test::flag_no_logo_true) +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      PERIOD\n"
"      IDENTIFIER \"box\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
//+ csspp_test::get_close_comment(true) -- with --no-logo this is gone

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }
}

CATCH_TEST_CASE("Compile user defined functions", "[compiler] [function]")
{
    CATCH_START_SECTION("deg2rad() function and translate() CSS function")
    {
        std::stringstream ss;
        ss << "/* testing user defined functions */\n"
           << "body { angle : deg2rad( 32deg ) }\n"
           << "a b { transform: translate(12%, 0); }\n"
           << "/* @preserver test \"Compile User Functions\" */\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"body\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"angle\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"rad\" D:0.559\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"transform\"\n"
"        ARG\n"
"          FUNCTION \"translate\"\n"
"            ARG\n"
"              PERCENT D:0.12\n"
"            ARG\n"
"              INTEGER \"\" I:0\n"
"  COMMENT \"@preserver test \"Compile User Functions\"\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("define an $undefined variable but no undefined() function")
    {
        // this is not invalid, until we check for all the CSS function names
        std::stringstream ss;
        ss << "/* testing user defined functions */\n"
           << "$zzzundefined: 12%;\n"
           << "body { angle : zzzundefined( 3rad ) }\n"
           << "/* @preserver test \"Compile User Functions\" */\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"    V:zzzundefined\n"
"      LIST\n"
"        VARIABLE \"zzzundefined\"\n"
"        PERCENT D:0.12\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"body\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"angle\"\n"
"        ARG\n"
"          FUNCTION \"zzzundefined\"\n"
"            ARG\n"
"              INTEGER \"rad\" I:3\n"
"  COMMENT \"@preserver test \"Compile User Functions\"\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("function with default parameters")
    {
        // this is not invalid, until we check for all the CSS function names
        std::stringstream ss;
        ss << "/* testing user defined functions */\n"
           << "body { color : mix(lightgrey, moccasin) }\n"
           << "/* @preserver test \"Compile User Functions\" */\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"body\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ffc4dce9\n"
"  COMMENT \"@preserver test \"Compile User Functions\"\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("function with *complex* default parameter")
    {
        // this is not invalid, until we check for all the CSS function names
        std::stringstream ss;
        ss << "/* testing user defined functions */\n"
           << "@mixin zzz_my_func($normal, $complex: 3px 7% #ff39af) { @return 3; }\n"
           << "body { z-index : zzz_my_func('fromage') }\n"
           << "/* @preserver test \"Compile User Functions\" */\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"    V:zzz_my_func\n"
"      LIST\n"
"        FUNCTION \"zzz_my_func\"\n"
"          ARG\n"
"            VARIABLE \"normal\"\n"
"          ARG\n"
"            VARIABLE \"complex\"\n"
"            INTEGER \"px\" I:3\n"
"            WHITESPACE\n"
"            PERCENT D:0.07\n"
"            WHITESPACE\n"
"            HASH \"ff39af\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              INTEGER \"\" I:3\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"body\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:3\n"
"  COMMENT \"@preserver test \"Compile User Functions\"\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("function called with a *complex* parameter")
    {
        // this is not invalid, until we check for all the CSS function names
        std::stringstream ss;
        ss << "/* testing user defined functions */\n"
           << "@mixin zzz_my_func($complex) { @return 3; }\n"
           << "body { z-index : zzz_my_func('fromage' 3px rational) }\n"
           << "/* @preserver test \"Compile User Functions\" */\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"    V:zzz_my_func\n"
"      LIST\n"
"        FUNCTION \"zzz_my_func\"\n"
"          ARG\n"
"            VARIABLE \"complex\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          COMPONENT_VALUE\n"
"            AT_KEYWORD \"return\" I:0\n"
"              INTEGER \"\" I:3\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"body\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:3\n"
"  COMMENT \"@preserver test \"Compile User Functions\"\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()
}

CATCH_TEST_CASE("Compile invalid declaration in link with user defined functions", "[compiler] [invalid] [function]")
{
    CATCH_START_SECTION("attempt to call a function with an invalid definition")
    {
        std::stringstream ss;
        ss << "/* testing user defined functions */\n"
           << "@mixin my_func($good, bad) { @return 3; }\n"
           << "body { angle : my_func( 32deg, -3px ) }\n"
           << "/* @preserver test \"Compile Invalid Functions\" */\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        // no error left over
        VERIFY_ERRORS("test.css(2): error: function declarations expect variables for each of their arguments, not a IDENTIFIER.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("attempt to call a function with a missing argument")
    {
        std::stringstream ss;
        ss << "/* testing user defined functions */\n"
           << "body { color : desaturate( white ) }\n"
           << "/* @preserver test \"Compile Invalid Functions\" */\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        // no error left over
        VERIFY_ERRORS("scripts/system/functions.scss(39): error: missing function variable named \"percent\" when calling desaturate();.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("@return is empty")
    {
        std::stringstream ss;
        ss << "/* testing user defined functions */\n"
           << "@mixin zzz_my_func() { @return { nothing; } }\n"
           << "body { color : zzz_my_func( ) }\n"
           << "/* @preserver test \"Compile Invalid Functions\" */\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        // no error left over
        VERIFY_ERRORS("test.css(2): error: @return must be followed by a valid expression.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("@return with an invalid expression")
    {
        std::stringstream ss;
        ss << "/* testing user defined functions */\n"
           << "@mixin zzz_my_func() { @return -; }\n"
           << "body { color : zzz_my_func( ) }\n"
           << "/* @preserver test \"Compile Invalid Functions\" */\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        // no error left over
        VERIFY_ERRORS("test.css(2): error: unsupported type EOF_TOKEN as a unary expression token.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Check all argify", "[compiler] [stylesheet]")
{
    // valid argify with/without spaces
    {
        std::stringstream ss;
        ss << "a,b{color:red}\n"
           << "a, b{color:hsl(0,100%,50%)}\n"
           << "a,b ,c{color:rgb(255,0,0)}\n"
           << "a , b,c{color:hsla(0,100%,50%,1)}\n"
           << "a{color:rgba(255,0,0,1)}\n"
           << "a {color:red}\n"
           << "a,b {color:red}\n"
           ;
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    ARG\n"
"      IDENTIFIER \"c\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    ARG\n"
"      IDENTIFIER \"c\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // now check declarations with multiple entries like text-shadow
    {
        std::stringstream ss;
        ss << "a, b\n"
           << "{\n"
           << "  text-shadow: 1px 3px 2px #f0e933, 7px 1px 5px #88ff45;\n"
           << "  box-shadow: 2.5em 4.3em 1.25em #ffee33, 7.3px 1.25px 4.11px #88ff55, 3.32em 2.45em 4.11em #ee5599;\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"text-shadow\"\n"
"          ARG\n"
"            INTEGER \"px\" I:1\n"
"            WHITESPACE\n"
"            INTEGER \"px\" I:3\n"
"            WHITESPACE\n"
"            INTEGER \"px\" I:2\n"
"            WHITESPACE\n"
"            COLOR H:ff33e9f0\n"
"          ARG\n"
"            INTEGER \"px\" I:7\n"
"            WHITESPACE\n"
"            INTEGER \"px\" I:1\n"
"            WHITESPACE\n"
"            INTEGER \"px\" I:5\n"
"            WHITESPACE\n"
"            COLOR H:ff45ff88\n"
"        DECLARATION \"box-shadow\"\n"
"          ARG\n"
"            DECIMAL_NUMBER \"em\" D:2.5\n"
"            WHITESPACE\n"
"            DECIMAL_NUMBER \"em\" D:4.3\n"
"            WHITESPACE\n"
"            DECIMAL_NUMBER \"em\" D:1.25\n"
"            WHITESPACE\n"
"            COLOR H:ff33eeff\n"
"          ARG\n"
"            DECIMAL_NUMBER \"px\" D:7.3\n"
"            WHITESPACE\n"
"            DECIMAL_NUMBER \"px\" D:1.25\n"
"            WHITESPACE\n"
"            DECIMAL_NUMBER \"px\" D:4.11\n"
"            WHITESPACE\n"
"            COLOR H:ff55ff88\n"
"          ARG\n"
"            DECIMAL_NUMBER \"em\" D:3.32\n"
"            WHITESPACE\n"
"            DECIMAL_NUMBER \"em\" D:2.45\n"
"            WHITESPACE\n"
"            DECIMAL_NUMBER \"em\" D:4.11\n"
"            WHITESPACE\n"
"            COLOR H:ff9955ee\n"

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }
}

CATCH_TEST_CASE("Invalid arguments", "[compiler] [invalid]")
{
    // A starting comma is illegal
    {
        std::stringstream ss;
        ss << ",a{color:red}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        // no error left over
        VERIFY_ERRORS("test.css(1): error: dangling comma at the beginning of a list of arguments or selectors.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // An ending comma is illegal
    {
        std::stringstream ss;
        ss << "a,{color:red}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        // no error left over
        VERIFY_ERRORS("test.css(1): error: dangling comma at the end of a list of arguments or selectors.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // Two commas in a row is illegal
    {
        std::stringstream ss;
        ss << "a,,b{color:red}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        // no error left over
        VERIFY_ERRORS("test.css(1): error: two commas in a row are invalid in a list of arguments or selectors.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // Just a comma is illegal
    {
        std::stringstream ss;
        ss << ",{color:red}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        // no error left over
        VERIFY_ERRORS("test.css(1): error: dangling comma at the beginning of a list of arguments or selectors.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // A repeated hash
    {
        std::stringstream ss;
        ss << "#color div #color { color : red }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: found #color twice in selector: \"#color div #color\".\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // rules with !important at the wrong place
    {
        std::stringstream ss;
        ss << "div.blackness { color: !important red }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): warning: A special flag, !important in this case, must only appear at the end of a declaration.\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"      PERIOD\n"
"      IDENTIFIER \"blackness\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\" F:important\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Selector attribute tests", "[compiler] [stylesheet] [attribute]")
{
    char const * op[] =
    {
        "=",    "EQUAL",
        "~=",   "INCLUDE_MATCH",
        "^=",   "PREFIX_MATCH",
        "$=",   "SUFFIX_MATCH",
        "*=",   "SUBSTRING_MATCH",
        "|=",   "DASH_MATCH"
    };
    char const * val[] =
    {
        "c",        "IDENTIFIER \"c\"",
        "' c '",    "STRING \" c \"",
        "123",      "INTEGER \"\" I:123",
        "1.23",     "DECIMAL_NUMBER \"\" D:1.23"
    };

    // TODO: rewrite that one to use a few less lines
    for(size_t i(0); i < sizeof(op) / sizeof(op[0]); i += 2)
    {
        for(size_t j(0); j < sizeof(val) / sizeof(val[0]); j += 2)
        {
            for(size_t k(0); k < (1 << 4); ++k)
            {
                // a[b op c | 'str' | 123 | 1.23] {color:red}
                std::stringstream ss;
                ss << "a[";
                if((k & (1 << 0)) != 0)
                {
                    ss << " ";
                }
                ss << "b";
                if((k & (1 << 1)) != 0)
                {
                    ss << " ";
                }
                ss << op[i];
                if((k & (1 << 2)) != 0)
                {
                    ss << " ";
                }
                ss << val[j];
                if((k & (1 << 3)) != 0)
                {
                    ss << " ";
                }
                ss << "]"
                   << (rand() % 2 == 0 ? " " : "")
                   << "{"
                   << (rand() % 2 == 0 ? " " : "")
                   << "color:"
                   << (rand() % 2 == 0 ? "rgb(255,0,0)" : "rgba(255,0,0,1.0)")
                   << (rand() % 2 == 0 ? " " : "")
                   << "}\n";
                csspp::position pos("test.css");
                csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

                csspp::parser p(l);

                csspp::node::pointer_t n(p.stylesheet());

                // no errors so far
                VERIFY_ERRORS("");

                csspp::compiler c;
                c.set_root(n);
                c.clear_paths();
                c.add_path(csspp_test::get_script_path());
                c.add_path(csspp_test::get_version_script_path());

                c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

                VERIFY_ERRORS("");

                std::stringstream out;
                out << *n;
                std::stringstream expected;
                expected <<
                        "LIST\n"
                        "  COMPONENT_VALUE\n"
                        "    ARG\n"
                        "      IDENTIFIER \"a\"\n"
                        "      OPEN_SQUAREBRACKET\n"
                        "        IDENTIFIER \"b\"\n"
                     << "        " << op[i + 1] << "\n"
                     << "        " << val[j + 1] << "\n"
                        "    OPEN_CURLYBRACKET B:true\n"
                        "      DECLARATION \"color\"\n"
                        "        ARG\n"
                        "          COLOR H:ff0000ff\n"
                    ;
                VERIFY_TREES(out.str(), expected.str());

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }

    // a[b!=c]
    for(size_t j(0); j < sizeof(val) / sizeof(val[0]); j += 2)
    {
        for(size_t k(0); k < (1 << 4); ++k)
        {
            std::stringstream ss;
            ss << "a[";
            if((k & (1 << 0)) != 0)
            {
                ss << " ";
            }
            ss << "b";
            if((k & (1 << 1)) != 0)
            {
                ss << " ";
            }
            ss << "!=";
            if((k & (1 << 2)) != 0)
            {
                ss << " ";
            }
            ss << val[j];
            if((k & (1 << 3)) != 0)
            {
                ss << " ";
            }
            ss << "]"
               << (rand() % 2 == 0 ? " " : "")
               << "{"
               << (rand() % 2 == 0 ? " " : "")
               << "color:red"
               << (rand() % 2 == 0 ? " " : "")
               << "}\n";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);
            c.clear_paths();
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            VERIFY_ERRORS("");

            std::stringstream out;
            out << *n;
            std::stringstream expected;
            expected <<
                    "LIST\n"
                    "  COMPONENT_VALUE\n"
                    "    ARG\n"
                    "      IDENTIFIER \"a\"\n"
                    "      COLON\n"
                    "      FUNCTION \"not\"\n"
                    "        OPEN_SQUAREBRACKET\n"
                    "          IDENTIFIER \"b\"\n"
                    "          EQUAL\n"
                    "          " << val[j + 1] << "\n"
                    "    OPEN_CURLYBRACKET B:true\n"
                    "      DECLARATION \"color\"\n"
                    "        ARG\n"
                    "          COLOR H:ff0000ff\n"
                ;
            VERIFY_TREES(out.str(), expected.str());

            CATCH_REQUIRE(c.get_root() == n);
        }
    }

    // Test with just 'b' and various spaces combinations
    for(size_t k(0); k < (1 << 2); ++k)
    {
        // a[b] {color:red}
        std::stringstream ss;
        ss << "a[";
        if((k & (1 << 0)) != 0)
        {
            ss << " ";
        }
        ss << "b";
        if((k & (1 << 1)) != 0)
        {
            ss << " ";
        }
        ss << "]"
           << (rand() % 2 == 0 ? " " : "")
           << "{"
           << (rand() % 2 == 0 ? " " : "")
           << "color:red"
           << (rand() % 2 == 0 ? " " : "")
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        std::stringstream expected;
        expected <<
                "LIST\n"
                "  COMPONENT_VALUE\n"
                "    ARG\n"
                "      IDENTIFIER \"a\"\n"
                "      OPEN_SQUAREBRACKET\n"
                "        IDENTIFIER \"b\"\n"
                "    OPEN_CURLYBRACKET B:true\n"
                "      DECLARATION \"color\"\n"
                "        ARG\n"
                "          COLOR H:ff0000ff\n"
            ;
        VERIFY_TREES(out.str(), expected.str());

        CATCH_REQUIRE(c.get_root() == n);
    }
}

CATCH_TEST_CASE("Invalid attributes", "[compiler] [invalid]")
{
    // attribute name cannot be an integer, decimal number, opening
    // brackets or parenthesis, delimiter, etc. only an identifier
    CATCH_START_SECTION("Missing operator or value")
    {
        char const * invalid_value[] =
        {
            "123",
            "1.23",
            "'1.23'",
            "1.23%",
            "(b)",
            "[b]",
            "{b}",
            "+b",
            //"@b",
            //"<!--",
            //"-->",
            //")",
            //"}",
            ",b,",
            "/* @preserve this comment */",
            "|=b",
            "/b",
            "$ b",
            "=b",
            "!b",
            "b(1)",
            ">b",
            "#123",
            "~=b",
            "*b",
            ".top",
            "%name",
            "~b",
            "&b",
            "|b",
            //";b",
        };

        for(auto iv : invalid_value)
        {
            std::stringstream ss;
            ss << "a[" << iv << "]{color:red}\n";
            csspp::position pos("test.css");
//std::cerr << "Test <<<" << ss.str() << ">>>\n";
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);
            c.clear_paths();
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            VERIFY_ERRORS("test.css(1): error: an attribute selector expects to first find an identifier.\n");

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // attribute only accept a very few binary operators: =, |=, ~=, $=, ^=, *=
    // anything else is an error (including another identifier)
    CATCH_START_SECTION("Not an attribute operator")
    {
        char const * invalid_value[] =
        {
            "identifier-too",
            "123",
            "1.23",
            "'1.23'",
            "1.23%",
            "(b)",
            "[b]",
            //"{b}", -- causes parser/lexer problems at this time... not too sure whether that's normal though
            "+",
            ",",
            "/* @preserve this comment */",
            "/",
            "$",
            "!",
            ">",
            ">=",
            "<=",
            "<",
            ":=",
            "?",
            "&&",
            "#123",
            "*",
            "**",
            ".top",
            "%name",
            "~",
            "&",
            "|",
            "||"
        };

        for(auto iv : invalid_value)
        {
            std::stringstream ss;
            ss << "a[b " << iv << " c]{color:red}\n";
            csspp::position pos("test.css");
//std::cerr << "Test <<<" << ss.str() << ">>>\n";
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            VERIFY_ERRORS("test.css(1): error: expected attribute operator missing, supported operators are '=', '!=', '~=', '^=', '$=', '*=', and '|='.\n");

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // attribute and a binary operators: =, |=, ~=, $=, ^=, *=
    // not followed by any value
    CATCH_START_SECTION("Valid operators, missing right hand side value")
    {
        char const * invalid_value[] =
        {
            "=",
            " =",
            "= ",
            " = ",
            "!=",
            " !=",
            "!= ",
            " != ",
            "|=",
            " |=",
            "|= ",
            " |= ",
            "~=",
            " ~=",
            "~= ",
            " ~= ",
            "$=",
            " $=",
            "$= ",
            " $= ",
            "^=",
            " ^=",
            "^= ",
            " ^= ",
            "*=",
            " *=",
            "*= ",
            " *= ",
        };

        for(auto iv : invalid_value)
        {
            std::stringstream ss;
            ss << "a[b" << iv << "]{color:red}\n";
            csspp::position pos("test.css");
//std::cerr << "Test <<<" << ss.str() << ">>>\n";
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            VERIFY_ERRORS("test.css(1): error: the attribute selector is expected to be an IDENTIFIER optionally followed by an operator and a value.\n");

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // attribute value can only be identifier, string, integer,
    // and decimal number
    CATCH_START_SECTION("Valid operators, invalid right hand side value")
    {
        char const * invalid_value[] =
        {
            "1.23%",
            "(b)",
            "[b]",
            "{b}",
            "+",
            //"@b",
            //"<!--",
            //"-->",
            //")",
            //"}",
            ",",
            "/* @preserve this comment */",
            "|=",
            "/",
            "$",
            //"=",
            //"!",
            ">",
            "#123",
            "~=",
            "*",
            ".top",
            "%name",
            "~",
            "&",
            "|",
            //";b",
        };
        char const *op[] =
        {
            "=",
            "|=",
            "~=",
            "$=",
            "^=",
            "*="
        };

        for(auto iv : invalid_value)
        {
            std::stringstream ss;
            ss << "a[b" << op[rand() % 6] << iv << "]{color:red}\n";
            csspp::position pos("test.css");
//std::cerr << "Test <<<" << ss.str() << ">>>\n";
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            // the node that caused a problem is:
            // LIST
            //   COMPONENT_VALUE
            //     ARG
            //       ...
            //       OPEN_SQUAREBRACKET
            //         ...
            //         ...
            //         <this one>
            csspp::node::pointer_t op_node(n->get_child(0)->get_child(0)->get_child(1)->get_child(2));

            std::stringstream errmsg;
            errmsg << "test.css(1): error: attribute selector value must be an identifier, a string, an integer, or a decimal number, a "
                   << op_node->get_type()
                   << " is not acceptable.\n";
            VERIFY_ERRORS(errmsg.str());

            CATCH_REQUIRE(c.get_root() == n);
        }

        for(auto iv : invalid_value)
        {
            std::stringstream ss;
            ss << "a[b" << op[rand() % 6] << " " << iv << "]{color:red}\n";
            csspp::position pos("test.css");
//std::cerr << "Test <<<" << ss.str() << ">>>\n";
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            // the node that caused a problem is:
            // LIST
            //   COMPONENT_VALUE
            //     ARG
            //       ...
            //       OPEN_SQUAREBRACKET
            //         ...
            //         ...
            //         <this one>
            csspp::node::pointer_t op_node(n->get_child(0)->get_child(0)->get_child(1)->get_child(2));

            std::stringstream errmsg;
            errmsg << "test.css(1): error: attribute selector value must be an identifier, a string, an integer, or a decimal number, a "
                   << op_node->get_type()
                   << " is not acceptable.\n";
            VERIFY_ERRORS(errmsg.str());

            CATCH_REQUIRE(c.get_root() == n);
        }

        for(auto iv : invalid_value)
        {
            std::stringstream ss;
            ss << "a[b" << op[rand() % 6] << iv << " ]{color:red}\n";
            csspp::position pos("test.css");
//std::cerr << "Test <<<" << ss.str() << ">>>\n";
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            // the node that caused a problem is:
            // LIST
            //   COMPONENT_VALUE
            //     ARG
            //       ...
            //       OPEN_SQUAREBRACKET
            //         ...
            //         ...
            //         <this one>
            csspp::node::pointer_t op_node(n->get_child(0)->get_child(0)->get_child(1)->get_child(2));

            std::stringstream errmsg;
            errmsg << "test.css(1): error: attribute selector value must be an identifier, a string, an integer, or a decimal number, a "
                   << op_node->get_type()
                   << " is not acceptable.\n";
            VERIFY_ERRORS(errmsg.str());

            CATCH_REQUIRE(c.get_root() == n);
        }

        for(auto iv : invalid_value)
        {
            std::stringstream ss;
            ss << "a[b" << op[rand() % 6] << " " << iv << " ]{color:red}\n";
            csspp::position pos("test.css");
//std::cerr << "Test <<<" << ss.str() << ">>>\n";
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            // the node that caused a problem is:
            // LIST
            //   COMPONENT_VALUE
            //     ARG
            //       ...
            //       OPEN_SQUAREBRACKET
            //         ...
            //         ...
            //         <this one>
            csspp::node::pointer_t op_node(n->get_child(0)->get_child(0)->get_child(1)->get_child(2));

            std::stringstream errmsg;
            errmsg << "test.css(1): error: attribute selector value must be an identifier, a string, an integer, or a decimal number, a "
                   << op_node->get_type()
                   << " is not acceptable.\n";
            VERIFY_ERRORS(errmsg.str());

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // attribute value can only be one token
    CATCH_START_SECTION("Valid operators, right hand side value followed by something")
    {
        char const * invalid_value[] =
        {
            "identifier",
            "123",
            "1.23",
            "'1.23'",
            "1.23%",
            "(b)",
            "[b]",
            "{b}",
            "+",
            //"@b",
            //"<!--",
            //"-->",
            //")",
            //"}",
            ",",
            "/* @preserve this comment */",
            "|=",
            "/",
            "$",
            "=",
            //"!",
            ">",
            "#123",
            "~=",
            "*",
            ".top",
            "%name",
            "~",
            "&",
            "|",
            //";b",
        };
        char const *op[] =
        {
            "=",
            "|=",
            "~=",
            "$=",
            "^=",
            "*="
        };

        for(auto iv : invalid_value)
        {
            // without a space these gets glued to "c"
            std::string const v(iv);
            if(v == "identifier"    // "cidentifier"
            || v == "123"           // "c123"
            || v[0] == '(')         // "c(...)"
            {
                continue;
            }
            std::stringstream ss;
            ss << "a[b" << op[rand() % 6] << "c" << iv << "]{color:red}\n";
            csspp::position pos("test.css");
//std::cerr << "Test <<<" << ss.str() << ">>>\n";
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            // the node that caused a problem is:
            // LIST
            //   COMPONENT_VALUE
            //     ARG
            //       ...
            //       OPEN_SQUAREBRACKET
            //         ...
            //         ...
            //         <this one>
            csspp::node::pointer_t op_node(n->get_child(0)->get_child(0)->get_child(1)->get_child(3));

            std::stringstream errmsg;
            errmsg << "test.css(1): error: attribute selector cannot be followed by more than one value, found "
                   << op_node->get_type()
                   << " after the value, missing quotes?\n";
            VERIFY_ERRORS(errmsg.str());

            CATCH_REQUIRE(c.get_root() == n);
        }

        for(auto iv : invalid_value)
        {
            std::stringstream ss;
            ss << "a[b" << op[rand() % 6] << "c " << iv << "]{color:red}\n";
            csspp::position pos("test.css");
//std::cerr << "Test <<<" << ss.str() << ">>>\n";
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            // the node that caused a problem is:
            // LIST
            //   COMPONENT_VALUE
            //     ARG
            //       ...
            //       OPEN_SQUAREBRACKET
            //         ...
            //         ...
            //         <this one>
            csspp::node::pointer_t op_node(n->get_child(0)->get_child(0)->get_child(1)->get_child(3));

            std::stringstream errmsg;
            errmsg << "test.css(1): error: attribute selector cannot be followed by more than one value, found "
                   << op_node->get_type()
                   << " after the value, missing quotes?\n";
            VERIFY_ERRORS(errmsg.str());

            CATCH_REQUIRE(c.get_root() == n);
        }

        for(auto iv : invalid_value)
        {
            // without a space these gets glued to "c"
            std::string const v(iv);
            if(v == "identifier"    // "cidentifier"
            || v == "123"           // "c123"
            || v[0] == '(')         // "c(...)"
            {
                continue;
            }
            std::stringstream ss;
            ss << "a[b" << op[rand() % 6] << "c" << iv << " ]{color:red}\n";
            csspp::position pos("test.css");
//std::cerr << "Test <<<" << ss.str() << ">>>\n";
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            // the node that caused a problem is:
            // LIST
            //   COMPONENT_VALUE
            //     ARG
            //       ...
            //       OPEN_SQUAREBRACKET
            //         ...
            //         ...
            //         <this one>
            csspp::node::pointer_t op_node(n->get_child(0)->get_child(0)->get_child(1)->get_child(3));

            std::stringstream errmsg;
            errmsg << "test.css(1): error: attribute selector cannot be followed by more than one value, found "
                   << op_node->get_type()
                   << " after the value, missing quotes?\n";
            VERIFY_ERRORS(errmsg.str());

            CATCH_REQUIRE(c.get_root() == n);
        }

        for(auto iv : invalid_value)
        {
            std::stringstream ss;
            ss << "a[b" << op[rand() % 6] << "c " << iv << " ]{color:red}\n";
            csspp::position pos("test.css");
//std::cerr << "Test <<<" << ss.str() << ">>>\n";
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            // the node that caused a problem is:
            // LIST
            //   COMPONENT_VALUE
            //     ARG
            //       ...
            //       OPEN_SQUAREBRACKET
            //         ...
            //         ...
            //         <this one>
            csspp::node::pointer_t op_node(n->get_child(0)->get_child(0)->get_child(1)->get_child(3));

            std::stringstream errmsg;
            errmsg << "test.css(1): error: attribute selector cannot be followed by more than one value, found "
                   << op_node->get_type()
                   << " after the value, missing quotes?\n";
            VERIFY_ERRORS(errmsg.str());

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // attribute value can only be one token
    CATCH_START_SECTION("Valid operators, right hand side value missing, no spaces")
    {
        char const *op[] =
        {
            "=",
            "!=",
            "|=",
            "~=",
            "$=",
            "^=",
            "*="
        };

        for(auto o : op)
        {
            std::stringstream ss;
            ss << "a[b" << o << "]{color:red}\n";
            csspp::position pos("test.css");
//std::cerr << "Test <<<" << ss.str() << ">>>\n";
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            // the node that caused a problem is:
            // LIST
            //   COMPONENT_VALUE
            //     ARG
            //       ...
            //       OPEN_SQUAREBRACKET
            //         ...
            //         ...
            //         <this one>
            //csspp::node::pointer_t op_node(n->get_child(0)->get_child(0)->get_child(1)->get_child(3));

            std::stringstream errmsg;
            errmsg << "test.css(1): error: the attribute selector is expected to be an IDENTIFIER optionally followed by an operator and a value.\n";
            VERIFY_ERRORS(errmsg.str());

            CATCH_REQUIRE(c.get_root() == n);
        }

        for(auto o : op)
        {
            std::stringstream ss;
            ss << "a[b" << o << " ]{color:red}\n";
            csspp::position pos("test.css");
//std::cerr << "Test <<<" << ss.str() << ">>>\n";
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            // the node that caused a problem is:
            // LIST
            //   COMPONENT_VALUE
            //     ARG
            //       ...
            //       OPEN_SQUAREBRACKET
            //         ...
            //         ...
            //         <this one>
            //csspp::node::pointer_t op_node(n->get_child(0)->get_child(0)->get_child(1)->get_child(3));

            std::stringstream errmsg;
            errmsg << "test.css(1): error: the attribute selector is expected to be an IDENTIFIER optionally followed by an operator and a value.\n";
            VERIFY_ERRORS(errmsg.str());

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Undefined paths", "[compiler] [invalid]")
{
    // compile without defining the paths
    //
    // (The result may be a success if you installed CSS Preprocessor
    // before since it will look for the scripts at "the right place!"
    // when the packages are installed properly on a system.)
    {
        std::stringstream ss;
        ss << ":lang(fr) {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        // c.add_path(...); -- check system default
//c.add_path(csspp_test::get_script_path());
//c.add_path(csspp_test::get_version_script_path());

        std::stringstream ignore;
        csspp::safe_error_stream_t safe_output(ignore);

        try
        {
            c.compile(true);

            // in case the system scripts are there, we want to check
            // that the result is fine
            std::stringstream out;
            out << *n;
            VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      COLON\n"
"      FUNCTION \"lang\"\n"
"        IDENTIFIER \"fr\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"

                );

        }
        catch(csspp::csspp_exception_exit const &)
        {
            CATCH_REQUIRE(ignore.str() == "validation/pseudo-nth-functions(1): fatal: validation script \"validation/pseudo-nth-functions\" was not found.\n");
        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Simple terms", "[compiler] [stylesheet]")
{
    // simple terms are:
    //      HASH
    //      IDENTIFIER
    //      IDENTIFIER '|' IDENTIFIER
    //      IDENTIFIER '|' '*'
    //      '*'
    //      '*' '|' IDENTIFIER
    //      '*' '|' '*'
    //      '|' IDENTIFIER
    //      '|' '*'
    //      ':' IDENTIFIER -- see below
    //      ':' FUNCTION ... ')'
    //      '.' IDENTIFIER
    //      '[' ... ']'
    {
        std::stringstream ss;
        ss << "#abd identifier ns|id namespace|* * *|abc *|*"
               << " |abc |* a:root :nth-child(3n+4) .class [foo]"
           << "{color:red;width:12px}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        // no error left over
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
// #abd
"      HASH \"abd\"\n"
"      WHITESPACE\n"
// identifier
"      IDENTIFIER \"identifier\"\n"
"      WHITESPACE\n"
// ns|id
"      IDENTIFIER \"ns\"\n"
"      SCOPE\n"
"      IDENTIFIER \"id\"\n"
"      WHITESPACE\n"
// namespace|*
"      IDENTIFIER \"namespace\"\n"
"      SCOPE\n"
"      MULTIPLY\n"
"      WHITESPACE\n"
// *
"      MULTIPLY\n"
"      WHITESPACE\n"
// *|abc
"      MULTIPLY\n"
"      SCOPE\n"
"      IDENTIFIER \"abc\"\n"
"      WHITESPACE\n"
// *|*
"      MULTIPLY\n"
"      SCOPE\n"
"      MULTIPLY\n"
"      WHITESPACE\n"
// |abc
"      SCOPE\n"
"      IDENTIFIER \"abc\"\n"
"      WHITESPACE\n"
// |*
"      SCOPE\n"
  "      MULTIPLY\n"
"      WHITESPACE\n"
// a:root
"      IDENTIFIER \"a\"\n"
"      COLON\n"
"      IDENTIFIER \"root\"\n"
"      WHITESPACE\n"
// :nth-child
"      COLON\n"
"      FUNCTION \"nth-child\"\n"
"        AN_PLUS_B S:3n+4\n"
"      WHITESPACE\n"
// .class
"      PERIOD\n"
"      IDENTIFIER \"class\"\n"
//"      WHITESPACE\n"
// [foo]
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"foo\"\n"
// {color:red}
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:ff0000ff\n"
"        DECLARATION \"width\"\n"
"          ARG\n"
"            INTEGER \"px\" I:12\n"

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // check all pseudo-classes
    {
        char const * pseudo_name_table[] =
        {
            "root",
            "first-child",
            "last-child",
            "first-of-type",
            "last-of-type",
            "only-child",
            "only-of-type",
            "empty",
            "link",
            "visited",
            "active",
            "hover",
            "focus",
            "target",
            "enabled",
            "disabled",
            "checked"
        };

        for(auto pseudo_name : pseudo_name_table)
        {

            std::stringstream ss;
            ss << ":"
               << pseudo_name
               << "{color:red}\n";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);
            c.clear_paths();
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            std::stringstream out;
            out << *n;
            VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      COLON\n"
"      IDENTIFIER \"" + std::string(pseudo_name) + "\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"

            );

            CATCH_REQUIRE(c.get_root() == n);
        }

        // no error left over
        VERIFY_ERRORS("");
    }

    // check all pseudo-classes
    {
        char const * pseudo_name_table[] =
        {
            "root",
            "first-child",
            "last-child",
            "first-of-type",
            "last-of-type",
            "only-child",
            "only-of-type",
            "empty",
            "link",
            "visited",
            "active",
            "hover",
            "focus",
            "target",
            "enabled",
            "disabled",
            "checked"
        };

        for(auto pseudo_name : pseudo_name_table)
        {

            std::stringstream ss;
            ss << ":"
               << pseudo_name
               << "{color:red}\n";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);
            c.clear_paths();
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            std::stringstream out;
            out << *n;
            VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      COLON\n"
"      IDENTIFIER \"" + std::string(pseudo_name) + "\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"

            );

            CATCH_REQUIRE(c.get_root() == n);
        }

        // no error left over
        VERIFY_ERRORS("");
    }

    // test all nth pseudo-functions
    {
        char const * nth_functions[] =
        {
            "child",
            "last-child",
            "of-type",
            "last-of-type"
        };
        for(size_t i(0); i < sizeof(nth_functions) / sizeof(nth_functions[0]); ++i)
        {
            std::stringstream ss;
            ss << "div a:nth-" << nth_functions[i] << "(3n+1)"
               << "{color:#651}";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);
            c.clear_paths();
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(true);

            // no error left over
            VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            std::stringstream out;
            out << *n;
            VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
// #abd
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
// identifier
"      IDENTIFIER \"a\"\n"
"      COLON\n"
"      FUNCTION \"nth-" + std::string(nth_functions[i]) + "\"\n"
"        AN_PLUS_B S:3n+1\n"
// {color:blue}
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff115566\n"

                );

            CATCH_REQUIRE(c.get_root() == n);
        }

        // no error left over
        VERIFY_ERRORS("");
    }

    // test the lang() function
    {
        std::stringstream ss;
        ss << "div q:lang(zu-za)"
           << "{color:#651}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        // no error left over
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
// #abd
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
// identifier
"      IDENTIFIER \"q\"\n"
"      COLON\n"
"      FUNCTION \"lang\"\n"
"        IDENTIFIER \"zu-za\"\n"
// {color:#651}
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff115566\n"

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test the lang() function with 3 parameters
    {
        std::stringstream ss;
        ss << "div b:lang(fr-ca-nc)"
           << "{color:brisque}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        // no error left over
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
// #abd
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
// identifier
"      IDENTIFIER \"b\"\n"
"      COLON\n"
"      FUNCTION \"lang\"\n"
"        IDENTIFIER \"fr-ca-nc\"\n"
// {color:#651}
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          IDENTIFIER \"brisque\"\n"

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test the lang() multiple times to verify that the cache works
    {
        std::stringstream ss;
        ss << "div b:lang(qu-vg-rr),section i:lang(ks-sm-dp)"
           << "{color:brisque}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        // no error left over
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
// #abd
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
// identifier
"      IDENTIFIER \"b\"\n"
"      COLON\n"
"      FUNCTION \"lang\"\n"
"        IDENTIFIER \"qu-vg-rr\"\n"
"    ARG\n"
// #abd
"      IDENTIFIER \"section\"\n"
"      WHITESPACE\n"
// identifier
"      IDENTIFIER \"i\"\n"
"      COLON\n"
"      FUNCTION \"lang\"\n"
"        IDENTIFIER \"ks-sm-dp\"\n"
// {color:#651}
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          IDENTIFIER \"brisque\"\n"

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // one :not(...)
    {
        std::stringstream ss;
        ss << "div:not(.chocolate) {color:coral}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
// #abd
"      IDENTIFIER \"div\"\n"
// :not(...)
"      COLON\n"
"      FUNCTION \"not\"\n"
"        PERIOD\n"
"        IDENTIFIER \"chocolate\"\n"
// {color:coral}
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff507fff\n"

            );

        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // two :not(...) in a row
    {
        std::stringstream ss;
        ss << "div:not(.red):not(.blue) {color:coral}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
// #abd
"      IDENTIFIER \"div\"\n"
// :not(...)
"      COLON\n"
"      FUNCTION \"not\"\n"
"        PERIOD\n"
"        IDENTIFIER \"red\"\n"
// :not(...)
"      COLON\n"
"      FUNCTION \"not\"\n"
"        PERIOD\n"
"        IDENTIFIER \"blue\"\n"
// {color:coral}
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff507fff\n"

            );

        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // two #hash generate an information message
    {
        std::stringstream ss;
        ss << "#first and #second {color:coral}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): info: found multiple #id entries, note that in most cases, assuming your HTML is proper (identifiers are not repeated) then only the last #id is necessary.\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
// #first #second
"      HASH \"first\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"and\"\n"
"      WHITESPACE\n"
"      HASH \"second\"\n"
// {color:coral}
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff507fff\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }
}

CATCH_TEST_CASE("Invalid simple terms", "[compiler] [invalid]")
{
    // two terms in one :not(...)
    {
        std::stringstream ss;
        ss << "div:not(.red.blue) {color:coral}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: the :not() function accepts at most one simple term.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // scope must be followed by * or IDENTIFIER
    {
        std::stringstream ss;
        ss << "*| {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: the scope operator (|) requires a right hand side identifier or '*'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // scope must be followed by * or IDENTIFIER
    {
        std::stringstream ss;
        ss << "*|.white {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: the right hand side of a scope operator (|) must be an identifier or '*'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // scope must be followed by * or IDENTIFIER
    {
        std::stringstream ss;
        ss << "div.white | {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: a scope selector (|) must be followed by an identifier or '*'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // scope must be followed by * or IDENTIFIER
    {
        std::stringstream ss;
        ss << "div.white |#hash {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: the right hand side of a scope operator (|) must be an identifier or '*'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // scope must be followed by * or IDENTIFIER
    {
        std::stringstream ss;
        ss << "#hash and #hash {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found #hash twice in selector: \"#hash and #hash\".\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // ':' must be followed by an IDENTIFIER or a FUNCTION
    {
        std::stringstream ss;
        ss << "div.white : {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: a selector list cannot end with a standalone ':'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // ':' must be followed a known pseudo-class name
    {
        std::stringstream ss;
        ss << "div.white :unknown {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("scripts/validation/pseudo-classes.scss(38): error: unknown is not a valid name for a pseudo class; CSS only supports root, first-child, last-child, first-of-type, last-of-type, only-child, only-of-type, empty, link, visitived, active, hover, focus, target, enabled, disabled, and checked. (functions are not included in this list since you did not use '(' at the end of the word.)\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // ':' must be followed a known pseudo-function name
    {
        std::stringstream ss;
        ss << "div.white :unknown() {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("scripts/validation/pseudo-functions.scss(20): error: unknown is not a valid name for a pseudo function; CSS only supports lang() and not().\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // ':' must be followed an identifier or a function
    {
        std::stringstream ss;
        ss << "div.white :.shark {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: a ':' selector must be followed by an identifier or a function, a PERIOD was found instead.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // '>' at the wrong place
    {
        std::stringstream ss;
        ss << "div.white > {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found token GREATER_THAN, which is expected to be followed by another selector term.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // :not(INTEGER) is not good
    {
        std::stringstream ss;
        ss << "div.white:not(11) {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found token INTEGER, which is not a valid selector token (simple term).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // :not(FUNCTION) is not good
    {
        std::stringstream ss;
        ss << "div.white:not(func()) {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found function \"func()\", which may be a valid selector token but only if immediately preceeded by one ':' (simple term).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // :not(>) is not good
    {
        std::stringstream ss;
        ss << "div.white:not(>) {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found token GREATER_THAN, which cannot be used to start a selector expression.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // :not(+) is not good
    {
        std::stringstream ss;
        ss << "div.white:not(+) {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found token ADD, which cannot be used to start a selector expression.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // :not(~) is not good
    {
        std::stringstream ss;
        ss << "div.white:not(~) {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found token PRECEDED, which cannot be used to start a selector expression.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // :not(:) is not good
    {
        std::stringstream ss;
        ss << "div.white:not(:) {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: a selector list cannot end with a standalone ':'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // '.' by itself (at the end)
    {
        std::stringstream ss;
        ss << "div.lone . {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: a selector list cannot end with a standalone '.'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // '.' must be followed by IDENTIFIER
    {
        std::stringstream ss;
        ss << "div.lone .< {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: a class selector (after a period: '.') must be an identifier.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test an invalid An+B in an :nth-child() function
    {
        std::stringstream ss;
        ss << "div:nth-child(3+5) {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: The first number has to be followed by the 'n' character.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // :not(:not(...))
    {
        std::stringstream ss;
        ss << "div:not(:not(.red)) {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: the :not() selector does not accept an inner :not().\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // :not(:.white)
    {
        std::stringstream ss;
        ss << "div:not(:.white) {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: a ':' selector must be followed by an identifier or a function, a FUNCTION was found instead.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // :lang() accepts only one argument
    {
        std::stringstream ss;
        ss << "div:lang(red blue) {color:bisque}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: a lang() function selector must have exactly one identifier as its parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // invalid name for :lang()
    {
        std::stringstream ss;
        ss << "div:lang(notalanguagename) {color:bisque}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("scripts/validation/languages.scss(154): error: notalanguagename is not a valid language name for :lang().\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // invalid name for :lang(), with a valid country
    {
        std::stringstream ss;
        ss << "div:lang(stillnotalanguagename-us) {color:bisque}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("scripts/validation/languages.scss(154): error: stillnotalanguagename is not a valid language name for :lang().\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // invalid name for :lang(), with a valid country
    {
        std::stringstream ss;
        ss << "div:lang(mn-withaninvalidcountry-andmore) {color:bisque}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("scripts/validation/countries.scss(267): error: withaninvalidcountry is not a valid country name for :lang().\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // :lang() name must be an identifier
    {
        std::stringstream ss;
        ss << "div:lang(\"de\") {color:bisque}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: a lang() function selector expects an identifier as its parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Complex terms", "[compiler] [stylesheet]")
{
    // [complex] terms are:
    // term: simple-term
    //     | PLACEHOLDER
    //     | REFERENCE
    //     | ':' FUNCTION (="not") component-value-list ')'
    //     | ':' ':' IDENTIFIER

    CATCH_START_SECTION("test a placeholder")
    {
        std::stringstream ss;
        ss << "div p%image"
           << "{color:blue}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        // no error left over
        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
// #abd
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
// identifier
"      IDENTIFIER \"p\"\n"
"      PLACEHOLDER \"image\"\n"
// {color:blue}
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ffff0000\n"

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("test a reference")
    {
        std::stringstream ss;
        ss << "div a"
           << "{color:blue;&:hover{color:red}}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        // no error left over
        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
// #abd
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
// identifier
"      IDENTIFIER \"a\"\n"
// {color:blue}
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:ffff0000\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
// &:hover
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"a\"\n"
"      COLON\n"
"      IDENTIFIER \"hover\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("test the not() function")
    {
        std::stringstream ss;
        ss << "div a:not(:hover)"
           << "{color:#175}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        // no error left over
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
// #abd
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
// identifier
"      IDENTIFIER \"a\"\n"
"      COLON\n"
"      FUNCTION \"not\"\n"
"        COLON\n"
"        IDENTIFIER \"hover\"\n"
// {color:blue}
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff557711\n"

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("test the not() function + a sub-function")
    {
        std::stringstream ss;
        ss << "div a:not(:nth-last-of-type(5n+3))"
           << "{color:#175}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        // no error left over
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
// #abd
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
// identifier
"      IDENTIFIER \"a\"\n"
"      COLON\n"
"      FUNCTION \"not\"\n"
"        COLON\n"
"        FUNCTION \"nth-last-of-type\"\n"
"          AN_PLUS_B S:5n+3\n"
// {color:blue}
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff557711\n"

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check all pseudo-elements")
    {
        char const * pseudo_name_table[] =
        {
            "first-line",
            "first-letter",
            "before",
            "after"
        };

        for(auto pseudo_name : pseudo_name_table)
        {
            std::stringstream ss;
            ss << "div ::"
               << pseudo_name
               << "{color:teal}\n";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);
            c.clear_paths();
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            std::stringstream out;
            out << *n;
            VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
"      COLON\n"
"      COLON\n"
"      IDENTIFIER \"" + std::string(pseudo_name) + "\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff808000\n"

            );

            CATCH_REQUIRE(c.get_root() == n);
        }

        // no error left over
        VERIFY_ERRORS("");
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check filter with alpha() function")
    {
        std::stringstream ss;
        ss << "div {\n"
           << "  font: 15px/150% helvetica;\n"
           << "  filter: alpha(opacity=20);\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(3): warning: the alpha(), chroma() and similar functions of the filter field are Internet Explorer specific extensions which are not supported across browsers.\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"font\"\n"
"          ARG\n"
"            FONT_METRICS FM:15px/150%\n"
"            WHITESPACE\n"
"            IDENTIFIER \"helvetica\"\n"
"        DECLARATION \"filter\"\n"
"          FUNCTION \"alpha\"\n"
"            IDENTIFIER \"opacity\"\n"
"            EQUAL\n"
"            INTEGER \"\" I:20\n"

            );

        CATCH_REQUIRE(c.get_root() == n);

        // no error left over
        VERIFY_ERRORS("");
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check \"-filter\" with alpha() function")
    {
        std::stringstream ss;
        ss << "div {\n"
           << "  font: 15px/150% helvetica;\n"
           << "  -filter: alpha(opacity=20);\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(3): warning: the alpha(), chroma() and similar functions of the filter field are Internet Explorer specific extensions which are not supported across browsers.\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"font\"\n"
"          ARG\n"
"            FONT_METRICS FM:15px/150%\n"
"            WHITESPACE\n"
"            IDENTIFIER \"helvetica\"\n"
"        DECLARATION \"-filter\"\n"
"          FUNCTION \"alpha\"\n"
"            IDENTIFIER \"opacity\"\n"
"            EQUAL\n"
"            INTEGER \"\" I:20\n"

            );

        CATCH_REQUIRE(c.get_root() == n);

        // no error left over
        VERIFY_ERRORS("");
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check progid:...")
    {
        std::stringstream ss;
        ss << "div {\n"
           << "  -filter: progid:DXImageTransform.Microsoft.AlphaImageLoader(opacity=20);\n"
           << "  font: 17.2px/1.35em;\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(2): warning: the alpha(), chroma() and similar functions of the filter field are Internet Explorer specific extensions which are not supported across browsers.\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"-filter\"\n"
"          IDENTIFIER \"progid\"\n"
"          COLON\n"
"          IDENTIFIER \"DXImageTransform\"\n"
"          PERIOD\n"
"          IDENTIFIER \"Microsoft\"\n"
"          PERIOD\n"
"          FUNCTION \"alphaimageloader\"\n"  // functions names always in lowercase...
"            IDENTIFIER \"opacity\"\n"
"            EQUAL\n"
"            INTEGER \"\" I:20\n"
"        DECLARATION \"font\"\n"
"          ARG\n"
"            FONT_METRICS FM:17.2px/1.35em\n"

            );

        CATCH_REQUIRE(c.get_root() == n);

        // no error left over
        VERIFY_ERRORS("");
    }
    CATCH_END_SECTION()
}

CATCH_TEST_CASE("Invalid complex terms", "[compiler] [invalid]")
{
    // '::' must be followed by an IDENTIFIER
    {
        std::stringstream ss;
        ss << "div.white :: {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: a selector list cannot end with a '::' without an identifier after it.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // '::' must be followed a known pseudo-element name
    {
        std::stringstream ss;
        ss << "div.white ::unknown {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("scripts/validation/pseudo-elements.scss(39): error: unknown is not a valid name for a pseudo element; CSS only supports after, before, first-letter, first-line, grammar-error, marker, placeholder, selection, and spelling-error.\n");
    }

    // '::' must be followed an IDENTIFIER
    {
        std::stringstream ss;
        ss << "div.white ::.shark {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: a pseudo element name (defined after a '::' in a list of selectors) must be defined using an identifier.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // '>' cannot start a selector list
    {
        std::stringstream ss;
        ss << "> div.white {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found token GREATER_THAN, which cannot be used to start a selector expression.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // '+' cannot start a selector list
    {
        std::stringstream ss;
        ss << "+ div.white {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found token ADD, which cannot be used to start a selector expression.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // '~' cannot start a selector list
    {
        std::stringstream ss;
        ss << "~ div.white {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found token PRECEDED, which cannot be used to start a selector expression.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // selector cannot start with a FUNCTION
    {
        std::stringstream ss;
        ss << "func() div.white {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found function \"func()\", which may be a valid selector token but only if immediately preceeded by one ':' (term).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // selectors do not support INTEGER
    {
        std::stringstream ss;
        ss << "13 div.white {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found token INTEGER, which is not a valid selector token (term).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // selectors do not support DECIMAL_NUMBER
    {
        std::stringstream ss;
        ss << "13.25 div.white {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found token DECIMAL_NUMBER, which is not a valid selector token (term).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // selectors do not support PERCENT
    {
        std::stringstream ss;
        ss << "13% div.white {color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: found token PERCENT, which is not a valid selector token (term).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // check pseudo-elements not at the end
    {
        char const * pseudo_name_table[] =
        {
            "first-line",
            "first-letter",
            "before",
            "after"
        };

        for(auto pseudo_name : pseudo_name_table)
        {
            std::stringstream ss;
            ss << "div ::"
               << pseudo_name
               << " span {color:teal}\n";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);
            c.clear_paths();
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            VERIFY_ERRORS("test.css(1): error: a pseudo element name (defined after a '::' in a list of selectors) must be defined as the last element in the list of selectors.\n");

            CATCH_REQUIRE(c.get_root() == n);
        }

        // no error left over
        VERIFY_ERRORS("");
    }

    // check the few invalid characters before "identifier ':' ..."
    {
        std::stringstream ss;
        ss << "div {\n"
           << "  *border: 1px solid #fff;\n"
           << "  .filter: opacity(0.2);\n"
           << "  #color: chocolate;\n"
           << "  !exclamation: 100px * 3;\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(2): warning: the '[*|.|!]<field-name>: ...' syntax is not allowed in csspp, we offer other ways to control field names per browser and do not allow such tricks.\n"
                "test.css(3): warning: the '[*|.|!]<field-name>: ...' syntax is not allowed in csspp, we offer other ways to control field names per browser and do not allow such tricks.\n"
                "test.css(3): warning: the alpha(), chroma() and similar functions of the filter field are Internet Explorer specific extensions which are not supported across browsers.\n"
                "test.css(4): warning: the '#<field-name>: ...' syntax is not allowed in csspp, we offer other ways to control field names per browser and do not allow such tricks.\n"
                "test.css(5): warning: the '#<field-name>: ...' syntax is not allowed in csspp, we offer other ways to control field names per browser and do not allow such tricks.\n"
            );

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"border\"\n"
"          ARG\n"
"            INTEGER \"px\" I:1\n"
"            WHITESPACE\n"
"            IDENTIFIER \"solid\"\n"
"            WHITESPACE\n"
"            COLOR H:ffffffff\n"
"        DECLARATION \"filter\"\n"
"          FUNCTION \"opacity\"\n"
"            DECIMAL_NUMBER \"\" D:0.2\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:ff1e69d2\n"
"        DECLARATION \"exclamation\"\n"
"          ARG\n"
"            INTEGER \"px\" I:300\n"

            );

        CATCH_REQUIRE(c.get_root() == n);

        // no error left over
        VERIFY_ERRORS("");
    }

    // check that & cannot be used in the middle of a selector list
    {
        std::stringstream ss;
        ss << "div {\n"
           << "  span &:hover {\n"
           << "    border: 1px solid #fff;\n"
           << "  }\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(1): error: a selector reference (&) can only appear as the very first item in a list of selectors.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid node", "[compiler] [invalid]")
{
    // create a fake node tree with some invalid node types to
    // exercise the compile() switch default entry
    {
        csspp::node_type_t invalid_types[] =
        {
            csspp::node_type_t::COMMA,
            csspp::node_type_t::ADD,
            csspp::node_type_t::CLOSE_CURLYBRACKET,
        };

        for(size_t idx(0); idx < sizeof(invalid_types) / sizeof(invalid_types[0]); ++idx)
        {
            csspp::position pos("invalid-types.scss");
            csspp::node::pointer_t n(new csspp::node(invalid_types[idx], pos));

            csspp::compiler c;
            c.set_root(n);
            c.clear_paths();
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            CATCH_REQUIRE_THROWS_AS(c.compile(true), csspp::csspp_exception_unexpected_token);

            CATCH_REQUIRE(c.get_root() == n);
        }
    }

    // qualified rule must start with an identifier
    {
        std::stringstream ss;
        ss << "{color:red}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: a qualified rule without selectors is not valid.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // qualified rule must start with an identifier
    {
        std::stringstream ss;
        ss << "this would be a declaration without a colon;";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // the qualified rule is invalid...
        VERIFY_ERRORS("test.css(1): error: A qualified rule must end with a { ... } block.\n");

        // ...but we still compile it so we get a specific error that we do
        // not get otherwise.
        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: expected a ':' after the identifier of this declaration value; got a: COMPONENT_VALUE instead.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // a declaration needs an identifier
    {
        std::stringstream ss;
        ss << "rule{+: red;}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: expected an identifier to start a declaration value; got a: ADD instead.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // a declaration needs an identifier
    {
        std::stringstream ss;
        ss << "div {\n"
           << "  font-size: 80%;\n"
           << "  font-style; italic;\n"
           << "  text-align: right;\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS(
                "test.css(2): error: somehow a declaration list is missing a field name or ':'.\n"
                "test.css(3): error: somehow a declaration list is missing a field name or ':'.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Compile font metrics", "[compiler] [font-metrics]")
{
    // define a sub-declaration inside a declaration
    {
        std::stringstream ss;
        ss << "body {\n"
           << "\tbackground-color: white;\n"
           << "\tcolor: #333;\n"
           << "\tfont: 62.5%/1.5 \"Helvetica Neue\", Helvetica, Verdana, Arial, FreeSans, \"Liberation Sans\", sans-serif;\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        //VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"body\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"background-color\"\n"
"          ARG\n"
"            COLOR H:ffffffff\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:ff333333\n"
"        DECLARATION \"font\"\n"
"          ARG\n"
"            FONT_METRICS FM:62.5%/1.5\n"
"            WHITESPACE\n"
"            STRING \"Helvetica Neue\"\n"
"          ARG\n"
"            IDENTIFIER \"Helvetica\"\n"
"          ARG\n"
"            IDENTIFIER \"Verdana\"\n"
"          ARG\n"
"            IDENTIFIER \"Arial\"\n"
"          ARG\n"
"            IDENTIFIER \"FreeSans\"\n"
"          ARG\n"
"            STRING \"Liberation Sans\"\n"
"          ARG\n"
"            IDENTIFIER \"sans-serif\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // define a sub-declaration inside a declaration
    {
        std::stringstream ss;
        ss << "body {\n"
           << "\tfont: 15pt/135% \"Helvetica Neue\", Helvetica, Verdana, Arial, FreeSans, \"Liberation Sans\", sans-serif;\n"
           << "\tcolor: #333;\n"
           << "\tbackground-color: white;\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        //VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"body\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"font\"\n"
"          ARG\n"
"            FONT_METRICS FM:15pt/135%\n"
"            WHITESPACE\n"
"            STRING \"Helvetica Neue\"\n"
"          ARG\n"
"            IDENTIFIER \"Helvetica\"\n"
"          ARG\n"
"            IDENTIFIER \"Verdana\"\n"
"          ARG\n"
"            IDENTIFIER \"Arial\"\n"
"          ARG\n"
"            IDENTIFIER \"FreeSans\"\n"
"          ARG\n"
"            STRING \"Liberation Sans\"\n"
"          ARG\n"
"            IDENTIFIER \"sans-serif\"\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:ff333333\n"
"        DECLARATION \"background-color\"\n"
"          ARG\n"
"            COLOR H:ffffffff\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // still no errors
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Nested declarations", "[compiler] [nested]")
{
    // define a sub-declaration inside a declaration
    {
        std::stringstream ss;
        ss << "div\n"
           << "{\n"
           << "  font:\n"
           << "  {\n"
           << "    color: red;\n"
           << "    family: 34px white \n"
           << "    {\n"
           << "      name: helvetica;\n"
           << "      group: sans-serif;\n"
           << "    };\n"
           << "    size: 3px + 5px;\n"
           << "  };\n"
           << "  a\n"
           << "  {\n"
           << "    text-decoration: underline;\n"
           << "    &:hover\n"
           << "    {\n"
           << "      text-align: center;\n"
           << "    }\n"
           << "  }\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"font-color\"\n"
"          ARG\n"
"            COLOR H:ff0000ff\n"
"        DECLARATION \"font-family\"\n"
"          ARG\n"
"            INTEGER \"px\" I:34\n"
"            WHITESPACE\n"
"            COLOR H:ffffffff\n"
"        DECLARATION \"font-family-name\"\n"
"          ARG\n"
"            IDENTIFIER \"helvetica\"\n"
"        DECLARATION \"font-family-group\"\n"
"          ARG\n"
"            IDENTIFIER \"sans-serif\"\n"
"        DECLARATION \"font-size\"\n"
"          ARG\n"
"            INTEGER \"px\" I:8\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"a\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"text-decoration\"\n"
"          ARG\n"
"            IDENTIFIER \"underline\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"a\"\n"
"      COLON\n"
"      IDENTIFIER \"hover\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"text-align\"\n"
"        ARG\n"
"          IDENTIFIER \"center\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // define a sub-declaration inside a declaration
    {
        std::stringstream ss;
        ss << "div { margin: { left: 300px + 51px / 3; top: 3px + 5px }; }"
           << " $size: 300px;"
           << " p { margin: 10px + $size * 3 25px - $size * 3 }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:size\n"
"      LIST\n"
"        VARIABLE \"size\"\n"
"        INTEGER \"px\" I:300\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"margin-left\"\n"
"        ARG\n"
"          INTEGER \"px\" I:317\n"
"      DECLARATION \"margin-top\"\n"
"        ARG\n"
"          INTEGER \"px\" I:8\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"margin\"\n"
"        ARG\n"
"          INTEGER \"px\" I:910\n"
"          WHITESPACE\n"
"          INTEGER \"px\" I:-875\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    CATCH_START_SECTION("just one sub-declaration inside a field definition")
    {
        std::stringstream ss;
        ss << "p.boxed { border: { width: 25px + 5px; }; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"      PERIOD\n"
"      IDENTIFIER \"boxed\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border-width\"\n"
"        ARG\n"
"          INTEGER \"px\" I:30\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // define the sub-declaration in a variable
    {
        std::stringstream ss;
        ss << "$m : { left: 300px + 51px / 3; top: 3px + 5px };"
           << " div { margin: $m; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:m\n"
"      LIST\n"
"        VARIABLE \"m\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          LIST\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"left\"\n"
"              COLON\n"
"              WHITESPACE\n"
"              INTEGER \"px\" I:300\n"
"              WHITESPACE\n"
"              ADD\n"
"              WHITESPACE\n"
"              INTEGER \"px\" I:51\n"
"              WHITESPACE\n"
"              DIVIDE\n"
"              WHITESPACE\n"
"              INTEGER \"\" I:3\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"top\"\n"
"              COLON\n"
"              WHITESPACE\n"
"              INTEGER \"px\" I:3\n"
"              WHITESPACE\n"
"              ADD\n"
"              WHITESPACE\n"
"              INTEGER \"px\" I:5\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"margin-left\"\n"
"        ARG\n"
"          INTEGER \"px\" I:317\n"
"      DECLARATION \"margin-top\"\n"
"        ARG\n"
"          INTEGER \"px\" I:8\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // 5 levels nested declarations
    {
        std::stringstream ss;
        ss << "border {\n"
           << "  left: 0;\n"
           << "  width {\n"
           << "    right {\n"
           << "      height: 300px + 51px / 3;\n"
           << "      top {\n"
           << "        color { edge: white; };\n"
           << "        position: 3px + 5px;\n"
           << "        chain { key: attached; };\n"
           << "      };"
           << "    };"
           << "  };"
           << "  height {\n"
           << "    position: 33px;\n"
           << "  };\n"
           << "}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"border\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"left\"\n"
"          ARG\n"
"            INTEGER \"\" I:0\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"border\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"width\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"border\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"width\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"right\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"height\"\n"
"          ARG\n"
"            INTEGER \"px\" I:317\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"border\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"width\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"right\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"top\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"position\"\n"
"          ARG\n"
"            INTEGER \"px\" I:8\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"border\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"width\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"right\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"top\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"color\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"edge\"\n"
"        ARG\n"
"          COLOR H:ffffffff\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"border\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"width\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"right\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"top\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"chain\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"key\"\n"
"        ARG\n"
"          IDENTIFIER \"attached\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"border\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"height\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"position\"\n"
"        ARG\n"
"          INTEGER \"px\" I:33\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // Test that functions prevent a field to look like a declaration
    {
        std::stringstream ss;
        ss << "border {\n"
           << "  left:not(.long) div{color: red};\n"
           << "}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

        //ss << "border {\n"
        //   << "  left:not(.long) div{color: red};\n"
        //   << "}";
"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"border\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"border\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"left\"\n"
"      COLON\n"
"      FUNCTION \"not\"\n"
"        PERIOD\n"
"        IDENTIFIER \"long\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

//    // define the sub-declaration in a variable
//    {
//std::cerr << "------------------------------------------------ WORKING ON straight entry\n";
//        std::stringstream ss;
//        ss << "$m : left: 300px + 51px / 3; top: 3px + 5px;"
//           << " div { margin: $m; }";
//        csspp::position pos("test.css");
//        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));
//
//        csspp::parser p(l);
//
//        csspp::node::pointer_t n(p.stylesheet());
//
//        // no errors so far
//        VERIFY_ERRORS("");
//
//        csspp::compiler c;
//        c.set_root(n);
//        c.clear_paths();
//        c.add_path(csspp_test::get_script_path());
//        c.add_path(csspp_test::get_version_script_path());
//
//        c.compile(true);
//
//std::cerr << "Result is: [" << *c.get_root() << "]\n";
//
//        VERIFY_ERRORS("");
//
//        std::stringstream out;
//        out << *n;
//        VERIFY_TREES(out.str(),
//
//"LIST\n"
//"    V:m\n"
//"      OPEN_CURLYBRACKET\n"
//"        COMPONENT_VALUE\n"
//"          IDENTIFIER \"left\"\n"
////"          COLON\n"
////"          WHITESPACE\n"
////"          INTEGER \"px\" I:300\n"
////"          WHITESPACE\n"
////"          ADD\n"
////"          WHITESPACE\n"
////"          INTEGER \"px\" I:51\n"
////"          WHITESPACE\n"
////"          DIVIDE\n"
////"          WHITESPACE\n"
////"          INTEGER \"\" I:3\n"
//"        COMPONENT_VALUE\n"
//"          IDENTIFIER \"top\"\n"
////"          COLON\n"
////"          WHITESPACE\n"
////"          INTEGER \"px\" I:3\n"
////"          WHITESPACE\n"
////"          ADD\n"
////"          WHITESPACE\n"
////"          INTEGER \"px\" I:5\n"
//"  COMPONENT_VALUE\n"
//"    ARG\n"
//"      IDENTIFIER \"div\"\n"
//"    OPEN_CURLYBRACKET\n"
//"      DECLARATION \"margin\"\n"
//"        DECLARATION \"left\"\n"
//"          INTEGER \"px\" I:317\n"
//"        DECLARATION \"top\"\n"
//"          INTEGER \"px\" I:8\n"
//
//            );
//
//        CATCH_REQUIRE(c.get_root() == n);
//    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid nested declarations", "[compiler] [nested] [invalid]")
{
    // define a sub-declaration inside a declaration
    {
        std::stringstream ss;
        ss << "div\n"
           << "{\n"
           << "  font:\n"
           << "  {\n"
           << "    color: red;\n"
           << "    span { margin: 0; }" // <- you cannot do that
           << "  };\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(5): error: a nested declaration cannot include a rule.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Advanced variables", "[compiler] [variable]")
{
    // define a variable function with a parameter
    {
        std::stringstream ss;
        ss << "$m( $width, $border: 1px ) : { left: $width + 51px / 3; top: $border + 5px };"
           << " div { margin: $m(300px, 3px); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:m\n"
"      LIST\n"
"        VARIABLE_FUNCTION \"m\"\n"
"          ARG\n"
"            VARIABLE \"width\"\n"
"          ARG\n"
"            VARIABLE \"border\"\n"
"            INTEGER \"px\" I:1\n"
"        OPEN_CURLYBRACKET B:false\n"
"          LIST\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"left\"\n"
"              COLON\n"
"              WHITESPACE\n"
"              VARIABLE \"width\"\n"
"              WHITESPACE\n"
"              ADD\n"
"              WHITESPACE\n"
"              INTEGER \"px\" I:51\n"
"              WHITESPACE\n"
"              DIVIDE\n"
"              WHITESPACE\n"
"              INTEGER \"\" I:3\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"top\"\n"
"              COLON\n"
"              WHITESPACE\n"
"              VARIABLE \"border\"\n"
"              WHITESPACE\n"
"              ADD\n"
"              WHITESPACE\n"
"              INTEGER \"px\" I:5\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"margin-left\"\n"
"        ARG\n"
"          INTEGER \"px\" I:317\n"
"      DECLARATION \"margin-top\"\n"
"        ARG\n"
"          INTEGER \"px\" I:8\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // define a variable function with a parameter and more spaces
    {
        std::stringstream ss;
        ss << "$m( $width, $border : 1px ) : { left: $width + 51px / 3; top: $border + 5px };"
           << " div { margin: $m(300px, 3px); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:m\n"
"      LIST\n"
"        VARIABLE_FUNCTION \"m\"\n"
"          ARG\n"
"            VARIABLE \"width\"\n"
"          ARG\n"
"            VARIABLE \"border\"\n"
"            INTEGER \"px\" I:1\n"
"        OPEN_CURLYBRACKET B:false\n"
"          LIST\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"left\"\n"
"              COLON\n"
"              WHITESPACE\n"
"              VARIABLE \"width\"\n"
"              WHITESPACE\n"
"              ADD\n"
"              WHITESPACE\n"
"              INTEGER \"px\" I:51\n"
"              WHITESPACE\n"
"              DIVIDE\n"
"              WHITESPACE\n"
"              INTEGER \"\" I:3\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"top\"\n"
"              COLON\n"
"              WHITESPACE\n"
"              VARIABLE \"border\"\n"
"              WHITESPACE\n"
"              ADD\n"
"              WHITESPACE\n"
"              INTEGER \"px\" I:5\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"margin-left\"\n"
"        ARG\n"
"          INTEGER \"px\" I:317\n"
"      DECLARATION \"margin-top\"\n"
"        ARG\n"
"          INTEGER \"px\" I:8\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test a variable function default parameter
    {
        std::stringstream ss;
        ss << "$m( $width, $border: 1px ) : { left: $width + 51px / 3; top: $border + 5px };"
           << " div { margin: $m(300px); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:m\n"
"      LIST\n"
"        VARIABLE_FUNCTION \"m\"\n"
"          ARG\n"
"            VARIABLE \"width\"\n"
"          ARG\n"
"            VARIABLE \"border\"\n"
"            INTEGER \"px\" I:1\n"
"        OPEN_CURLYBRACKET B:false\n"
"          LIST\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"left\"\n"
"              COLON\n"
"              WHITESPACE\n"
"              VARIABLE \"width\"\n"
"              WHITESPACE\n"
"              ADD\n"
"              WHITESPACE\n"
"              INTEGER \"px\" I:51\n"
"              WHITESPACE\n"
"              DIVIDE\n"
"              WHITESPACE\n"
"              INTEGER \"\" I:3\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"top\"\n"
"              COLON\n"
"              WHITESPACE\n"
"              VARIABLE \"border\"\n"
"              WHITESPACE\n"
"              ADD\n"
"              WHITESPACE\n"
"              INTEGER \"px\" I:5\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"margin-left\"\n"
"        ARG\n"
"          INTEGER \"px\" I:317\n"
"      DECLARATION \"margin-top\"\n"
"        ARG\n"
"          INTEGER \"px\" I:6\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // a multi value default
    {
        std::stringstream ss;
        ss << "$m( $width, $border: 1px 3px ) : { left: $width + 51px / 3; top: $border };"
           << " div { margin: $m(300px); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:m\n"
"      LIST\n"
"        VARIABLE_FUNCTION \"m\"\n"
"          ARG\n"
"            VARIABLE \"width\"\n"
"          ARG\n"
"            VARIABLE \"border\"\n"
"            INTEGER \"px\" I:1\n"
"            WHITESPACE\n"
"            INTEGER \"px\" I:3\n"
"        OPEN_CURLYBRACKET B:false\n"
"          LIST\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"left\"\n"
"              COLON\n"
"              WHITESPACE\n"
"              VARIABLE \"width\"\n"
"              WHITESPACE\n"
"              ADD\n"
"              WHITESPACE\n"
"              INTEGER \"px\" I:51\n"
"              WHITESPACE\n"
"              DIVIDE\n"
"              WHITESPACE\n"
"              INTEGER \"\" I:3\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"top\"\n"
"              COLON\n"
"              WHITESPACE\n"
"              VARIABLE \"border\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"margin-left\"\n"
"        ARG\n"
"          INTEGER \"px\" I:317\n"
"      DECLARATION \"margin-top\"\n"
"        ARG\n"
"          INTEGER \"px\" I:1\n"
"          WHITESPACE\n"
"          INTEGER \"px\" I:3\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // a variable function with multiple fields copied
    {
        std::stringstream ss;
        ss << "$m( $border ) : { $border };"
           << " br { border: $m(3px 1px 2px 4px); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:m\n"
"      LIST\n"
"        VARIABLE_FUNCTION \"m\"\n"
"          ARG\n"
"            VARIABLE \"border\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            VARIABLE \"border\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"br\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          INTEGER \"px\" I:3\n"
"          WHITESPACE\n"
"          INTEGER \"px\" I:1\n"
"          WHITESPACE\n"
"          INTEGER \"px\" I:2\n"
"          WHITESPACE\n"
"          INTEGER \"px\" I:4\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test a default variable
    {
        std::stringstream ss;
        ss << "$m : 300px;\n"
           << "$m : 53px !default;\n"
           << "div { margin: $m; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:m\n"
"      LIST\n"
"        VARIABLE \"m\"\n"
"        INTEGER \"px\" I:300\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"margin\"\n"
"        ARG\n"
"          INTEGER \"px\" I:300\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test a variable inside a qualified rule {}-block
    {
        std::stringstream ss;
        ss << "div { $size: 300px;\n"
           << " entry: {\n"
           << "   width: $size;\n"
           << "   height: $size * 3 / 4;\n"
           << " };\n"
           << " junior: $size + 13px;\n"
           << "}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"        V:size\n"
"          LIST\n"
"            VARIABLE \"size\"\n"
"            INTEGER \"px\" I:300\n"
"      LIST\n"
"        DECLARATION \"entry-width\"\n"
"          ARG\n"
"            INTEGER \"px\" I:300\n"
"        DECLARATION \"entry-height\"\n"
"          ARG\n"
"            INTEGER \"px\" I:225\n"
"        DECLARATION \"junior\"\n"
"          ARG\n"
"            INTEGER \"px\" I:313\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test that blocks define locations to save variables as expected
    {
        std::stringstream ss;
        ss << "$size: 100px;\n"
           << "div { $size: 300px;\n"
           << " entry: {\n"
           << "   $size: 50px;\n"
           << "   width: $size;\n"
           << "   height: $size * 3 / 4;\n"
           << " };\n"
           << " junior: $size + 13px;\n"
           << "}\n"
           << "section { diameter: $size }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:size\n"
"      LIST\n"
"        VARIABLE \"size\"\n"
"        INTEGER \"px\" I:100\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"        V:size\n"
"          LIST\n"
"            VARIABLE \"size\"\n"
"            INTEGER \"px\" I:300\n"
"      LIST\n"
"        DECLARATION \"entry-width\"\n"
"          ARG\n"
"            INTEGER \"px\" I:50\n"
"        DECLARATION \"entry-height\"\n"
"          ARG\n"
"            INTEGER \"px\" I:37\n"
"        DECLARATION \"junior\"\n"
"          ARG\n"
"            INTEGER \"px\" I:313\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"section\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"diameter\"\n"
"        ARG\n"
"          INTEGER \"px\" I:100\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test that !global forces definitions to be global
    {
        std::stringstream ss;
        ss << "$size: 100px;\n"
           << "div { $size: 300px !global;\n"
           << "  entry: {\n"
           << "    $size: 50px ! global;\n"
           << "    width: $size;\n"
           << "    height: $size * 3 / 4;\n"
           << "  };\n"
           << "  junior: $size + 13px;\n"
           << "}\n"
           << "section { diameter: $size }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:size\n"
"      LIST\n"
"        VARIABLE \"size\"\n"
"        INTEGER \"px\" I:50\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"entry-width\"\n"
"          ARG\n"
"            INTEGER \"px\" I:50\n"
"        DECLARATION \"entry-height\"\n"
"          ARG\n"
"            INTEGER \"px\" I:37\n"
"        DECLARATION \"junior\"\n"
"          ARG\n"
"            INTEGER \"px\" I:63\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"section\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"diameter\"\n"
"        ARG\n"
"          INTEGER \"px\" I:50\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test that !default prevents redefinitions of existing variables
    {
        std::stringstream ss;
        ss << "$size: 100px;\n"
           << "div { $size: 300px !default;\n"
           << "  entry: {\n"
           << "    $size: 50px ! default;\n"
           << "    width: $size;\n"
           << "    height: $size * 3 / 4;\n"
           << "  };\n"
           << "  junior: $size + 13px;\n"
           << "}\n"
           << "section { diameter: $size }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:size\n"
"      LIST\n"
"        VARIABLE \"size\"\n"
"        INTEGER \"px\" I:100\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"entry-width\"\n"
"          ARG\n"
"            INTEGER \"px\" I:100\n"
"        DECLARATION \"entry-height\"\n"
"          ARG\n"
"            INTEGER \"px\" I:75\n"
"        DECLARATION \"junior\"\n"
"          ARG\n"
"            INTEGER \"px\" I:113\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"section\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"diameter\"\n"
"        ARG\n"
"          INTEGER \"px\" I:100\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test a null variable
    {
        std::stringstream ss;
        ss << "$empty-variable: null;\n"
           << "div { border: 1px solid $empty-variable; }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:empty_variable\n"
"      LIST\n"
"        VARIABLE \"empty_variable\"\n"
"        NULL_TOKEN\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          INTEGER \"px\" I:1\n"
"          WHITESPACE\n"
"          IDENTIFIER \"solid\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test inexistant variable when 'accept empty' flag is ON
    {
        std::stringstream ss;
        ss << "div { border: 1px solid $undefined; }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          INTEGER \"px\" I:1\n"
"          WHITESPACE\n"
"          IDENTIFIER \"solid\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test @include instead of $blah
    {
        std::stringstream ss;
        ss << "$var: { div { border: 1px solid #ffe093; } };"
           << "@include var;\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        VARIABLE \"var\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"div\"\n"
"            OPEN_CURLYBRACKET B:true\n"
"              COMPONENT_VALUE\n"
"                IDENTIFIER \"border\"\n"
"                COLON\n"
"                WHITESPACE\n"
"                INTEGER \"px\" I:1\n"
"                WHITESPACE\n"
"                IDENTIFIER \"solid\"\n"
"                WHITESPACE\n"
"                HASH \"ffe093\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          INTEGER \"px\" I:1\n"
"          WHITESPACE\n"
"          IDENTIFIER \"solid\"\n"
"          WHITESPACE\n"
"          COLOR H:ff93e0ff\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test @include with a function definition
    {
        std::stringstream ss;
        ss << "$var($width): { div { border: $width solid #ffe093; } };"
           << "@include var(7px);\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        VARIABLE_FUNCTION \"var\"\n"
"          ARG\n"
"            VARIABLE \"width\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"div\"\n"
"            OPEN_CURLYBRACKET B:true\n"
"              COMPONENT_VALUE\n"
"                IDENTIFIER \"border\"\n"
"                COLON\n"
"                WHITESPACE\n"
"                VARIABLE \"width\"\n"
"                WHITESPACE\n"
"                IDENTIFIER \"solid\"\n"
"                WHITESPACE\n"
"                HASH \"ffe093\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          INTEGER \"px\" I:7\n"
"          WHITESPACE\n"
"          IDENTIFIER \"solid\"\n"
"          WHITESPACE\n"
"          COLOR H:ff93e0ff\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test @include with @mixin
    {
        std::stringstream ss;
        ss << "@mixin nice-button { div { border: 3px solid #ffe093; } }"
           << "@include nice-button;\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:nice-button\n"
"      LIST\n"
"        IDENTIFIER \"nice-button\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"div\"\n"
"            OPEN_CURLYBRACKET B:true\n"
"              COMPONENT_VALUE\n"
"                IDENTIFIER \"border\"\n"
"                COLON\n"
"                WHITESPACE\n"
"                INTEGER \"px\" I:3\n"
"                WHITESPACE\n"
"                IDENTIFIER \"solid\"\n"
"                WHITESPACE\n"
"                HASH \"ffe093\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          INTEGER \"px\" I:3\n"
"          WHITESPACE\n"
"          IDENTIFIER \"solid\"\n"
"          WHITESPACE\n"
"          COLOR H:ff93e0ff\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test @include with @mixin
    {
        std::stringstream ss;
        ss << "@mixin var($width) { div { border: $width solid #ffe093; } }"
           << "@include var(7px);\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        FUNCTION \"var\"\n"
"          ARG\n"
"            VARIABLE \"width\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"div\"\n"
"            OPEN_CURLYBRACKET B:true\n"
"              COMPONENT_VALUE\n"
"                IDENTIFIER \"border\"\n"
"                COLON\n"
"                WHITESPACE\n"
"                VARIABLE \"width\"\n"
"                WHITESPACE\n"
"                IDENTIFIER \"solid\"\n"
"                WHITESPACE\n"
"                HASH \"ffe093\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          INTEGER \"px\" I:7\n"
"          WHITESPACE\n"
"          IDENTIFIER \"solid\"\n"
"          WHITESPACE\n"
"          COLOR H:ff93e0ff\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test $var with @mixin definition
    {
        std::stringstream ss;
        ss << "@mixin var { 1px solid #ff0000 }"
           << "div {border:$var}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        IDENTIFIER \"var\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          COMPONENT_VALUE\n"
"            INTEGER \"px\" I:1\n"
"            WHITESPACE\n"
"            IDENTIFIER \"solid\"\n"
"            WHITESPACE\n"
"            HASH \"ff0000\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          INTEGER \"px\" I:1\n"
"          WHITESPACE\n"
"          IDENTIFIER \"solid\"\n"
"          WHITESPACE\n"
"          COLOR H:ff0000ff\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test $var with @mixin definition
    {
        std::stringstream ss;
        ss << "@mixin var { rock.paper#scissors }"
           << "$var {border:blue}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        //VERIFY_ERRORS("test.css(1): info: found an #id entry which is not at the beginning of the list of selectors; unless your HTML changes that much, #id should be the first selector only.\n");
        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        IDENTIFIER \"var\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"rock\"\n"
"            PERIOD\n"
"            IDENTIFIER \"paper\"\n"
"            HASH \"scissors\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"rock\"\n"
"      PERIOD\n"
"      IDENTIFIER \"paper\"\n"
"      HASH \"scissors\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          COLOR H:ffff0000\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test $var with @mixin definition
    {
        std::stringstream ss;
        ss << "@mixin var { rock.paper#scissors, with.more#selectors }"
           << "$var {border:blue}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        //VERIFY_ERRORS(
        //        "test.css(1): info: found an #id entry which is not at the beginning of the list of selectors; unless your HTML changes that much, #id should be the first selector only.\n"
        //        "test.css(1): info: found an #id entry which is not at the beginning of the list of selectors; unless your HTML changes that much, #id should be the first selector only.\n"
        //    );
        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        IDENTIFIER \"var\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"rock\"\n"
"            PERIOD\n"
"            IDENTIFIER \"paper\"\n"
"            HASH \"scissors\"\n"
"            COMMA\n"
"            WHITESPACE\n"
"            IDENTIFIER \"with\"\n"
"            PERIOD\n"
"            IDENTIFIER \"more\"\n"
"            HASH \"selectors\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"rock\"\n"
"      PERIOD\n"
"      IDENTIFIER \"paper\"\n"
"      HASH \"scissors\"\n"
"    ARG\n"
"      IDENTIFIER \"with\"\n"
"      PERIOD\n"
"      IDENTIFIER \"more\"\n"
"      HASH \"selectors\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          COLOR H:ffff0000\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test $var with @mixin definition
    {
        std::stringstream ss;
        ss << "@mixin var { rock.paper#scissors{border:blue} }"
           << "div {$var}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        //VERIFY_ERRORS("test.css(1): info: found an #id entry which is not at the beginning of the list of selectors; unless your HTML changes that much, #id should be the first selector only.\n");
        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        IDENTIFIER \"var\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"rock\"\n"
"            PERIOD\n"
"            IDENTIFIER \"paper\"\n"
"            HASH \"scissors\"\n"
"            OPEN_CURLYBRACKET B:true\n"
"              COMPONENT_VALUE\n"
"                IDENTIFIER \"border\"\n"
"                COLON\n"
"                IDENTIFIER \"blue\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"rock\"\n"
"      PERIOD\n"
"      IDENTIFIER \"paper\"\n"
"      HASH \"scissors\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          COLOR H:ffff0000\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test $var with @mixin definition
    {
        std::stringstream ss;
        ss << "@mixin var { border : 1px solid #eeeeee }"
           << "div {$var}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        IDENTIFIER \"var\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"border\"\n"
"            WHITESPACE\n"
"            COLON\n"
"            WHITESPACE\n"
"            INTEGER \"px\" I:1\n"
"            WHITESPACE\n"
"            IDENTIFIER \"solid\"\n"
"            WHITESPACE\n"
"            HASH \"eeeeee\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          INTEGER \"px\" I:1\n"
"          WHITESPACE\n"
"          IDENTIFIER \"solid\"\n"
"          WHITESPACE\n"
"          COLOR H:ffeeeeee\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test $var with @mixin definition
    {
        std::stringstream ss;
        ss << "@mixin var{border:1px solid #eeeeee}"
           << "div{$var}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        IDENTIFIER \"var\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"border\"\n"
"            COLON\n"
"            INTEGER \"px\" I:1\n"
"            WHITESPACE\n"
"            IDENTIFIER \"solid\"\n"
"            WHITESPACE\n"
"            HASH \"eeeeee\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          INTEGER \"px\" I:1\n"
"          WHITESPACE\n"
"          IDENTIFIER \"solid\"\n"
"          WHITESPACE\n"
"          COLOR H:ffeeeeee\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test $var with @mixin definition
    {
        struct mixin_info_t
        {
            mixin_info_t(char const *selector, char const *v1, char const *r1, char const *r2 = nullptr, char const *r3 = nullptr, char const *r4 = nullptr, char const *r5 = nullptr)
                : f_selector(selector)
            {
                f_result.push_back(r1);
                if(r2)
                {
                    f_result.push_back(r2);
                    if(r3)
                    {
                        f_result.push_back(r3);
                        if(r4)
                        {
                            f_result.push_back(r4);
                            if(r5)
                            {
                                f_result.push_back(r5);
                            }
                        }
                    }
                }

                if(v1)
                {
                    f_variable.push_back(v1);
                }
                if(strcmp(r1, "WHITESPACE") != 0)
                {
                    f_variable.push_back(r1);
                }
                if(r2)
                {
                    f_variable.push_back(r2);
                    if(r3)
                    {
                        f_variable.push_back(r3);
                        if(r4)
                        {
                            f_variable.push_back(r4);
                            if(r5)
                            {
                                f_variable.push_back(r5);
                            }
                        }
                    }
                }
            }

            std::string                             f_selector = std::string();
            std::vector<std::string>                f_variable = std::vector<std::string>();
            std::vector<std::string>                f_result = std::vector<std::string>();
        };
        mixin_info_t * start[5];
        start[0] = new mixin_info_t("*", nullptr, "WHITESPACE", "MULTIPLY");
        start[1] = new mixin_info_t("[foo='bar']", nullptr, "WHITESPACE", "OPEN_SQUAREBRACKET", "  IDENTIFIER \"foo\"", "  EQUAL", "  STRING \"bar\"");
        start[2] = new mixin_info_t(".color", nullptr, "WHITESPACE", "PERIOD", "IDENTIFIER \"color\"");
        start[3] = new mixin_info_t("&:hover", "REFERENCE", "COLON", "IDENTIFIER \"hover\"");
        start[4] = new mixin_info_t("#peculiar", nullptr, "WHITESPACE", "HASH \"peculiar\"");

        for(size_t i(0); i < sizeof(start) / sizeof(start[0]); ++i)
        {

            std::stringstream ss;
            ss << "@mixin var{" << start[i]->f_selector << " div p{color:#eeeeee}}"
               << "div{$var}\n";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

            // no errors so far
            VERIFY_ERRORS("");

            csspp::compiler c;
            c.set_root(n);
            c.clear_paths();
            c.set_empty_on_undefined_variable(true);
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

            VERIFY_ERRORS("");

            std::stringstream out;
            out << *n;

            std::stringstream expected;
            expected <<
"LIST\n"
"    V:var\n"
"      LIST\n"
"        IDENTIFIER \"var\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          COMPONENT_VALUE\n";

            for(auto s : start[i]->f_variable)
            {
                expected << "            " << s << "\n";
            }

            expected <<
"            WHITESPACE\n"
"            IDENTIFIER \"div\"\n"
"            WHITESPACE\n"
"            IDENTIFIER \"p\"\n"
"            OPEN_CURLYBRACKET B:true\n"
"              COMPONENT_VALUE\n"
"                IDENTIFIER \"color\"\n"
"                COLON\n"
"                HASH \"eeeeee\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n";

            for(auto s : start[i]->f_result)
            {
                expected << "      " << s << "\n";
            }

            expected <<
"      WHITESPACE\n"
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ffeeeeee\n";

            VERIFY_TREES(out.str(), expected.str());

            CATCH_REQUIRE(c.get_root() == n);
        }
    }

    // test $var with @mixin definition
    {
        std::stringstream ss;
        ss << "$var: { * div p { color: #eeeeee } };\n"
           << "div { $var }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;

        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        VARIABLE \"var\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            MULTIPLY\n"
"            WHITESPACE\n"
"            IDENTIFIER \"div\"\n"
"            WHITESPACE\n"
"            IDENTIFIER \"p\"\n"
"            OPEN_CURLYBRACKET B:true\n"
"              COMPONENT_VALUE\n"
"                IDENTIFIER \"color\"\n"
"                COLON\n"
"                WHITESPACE\n"
"                HASH \"eeeeee\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
"      MULTIPLY\n"
"      WHITESPACE\n"
"      IDENTIFIER \"div\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ffeeeeee\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // wrote this test out of a mistake really,
    // but it generates an empty {}-block which is a good test
    {
        std::stringstream ss;
        ss << "$b: border;"
           << "$v: 1px solid white;"
           << "div{$b: $v}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:b\n"
"      LIST\n"
"        VARIABLE \"b\"\n"
"        IDENTIFIER \"border\"\n"
"    V:v\n"
"      LIST\n"
"        VARIABLE \"v\"\n"
"        LIST\n"
"          INTEGER \"px\" I:1\n"
"          WHITESPACE\n"
"          IDENTIFIER \"solid\"\n"
"          WHITESPACE\n"
"          IDENTIFIER \"white\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid variables", "[compiler] [variable] [invalid]")
{
    // undefined variable with whitespace before
    {
        std::stringstream ss;
        ss << "div { margin: $m; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(1): error: variable named \"m\" is not set.\n"
                "test.css(1): error: somehow a declaration list is missing fields, this happens if you used an invalid variable.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // null variable in a place where something is required
    {
        std::stringstream ss;
        ss << "$m: null;\n"
           << "div { margin: $m; }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(2): error: somehow a declaration list is missing fields, this happens if you used an invalid variable.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // undefined variable without whitespace
    {
        std::stringstream ss;
        ss << "div{margin:$m;}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(1): error: variable named \"m\" is not set.\n"
                "test.css(1): error: somehow a declaration list is missing fields, this happens if you used an invalid variable.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // variable type mismatch (func/var)
    {
        std::stringstream ss;
        ss << "$m($p): $p / 3;"
           << "div { margin: $m; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(1): error: variable named \"m\" is not a function and it cannot be referenced as such.\n"
                "test.css(1): error: somehow a declaration list is missing fields, this happens if you used an invalid variable.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // variable type mismatch (var/func)
    {
        std::stringstream ss;
        ss << "$m: 3px;"
           << "div { margin: $m(6px); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(1): error: variable named \"m\" is a function and it can only be referenced with a function ($m() or @include m;).\n"
                "test.css(1): error: somehow a declaration list is missing fields, this happens if you used an invalid variable.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // variable is missing in function call
    {
        std::stringstream ss;
        ss << "$sum($a1, $a2, $a3): $a1 + $a2 + $a3;"
           << "div { margin: $sum(6px, 309px); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(1): error: missing function variable named \"a3\" when calling sum() or using @include sum();).\n"
                "test.css(1): error: somehow a declaration list is missing fields, this happens if you used an invalid variable.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // variable parameter is not a variable
    {
        std::stringstream ss;
        ss << "$sum(a1): $a1;"
           << "div { margin: $sum(6px); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(1): error: function declarations expect variables for each of their arguments, not a IDENTIFIER.\n"
                //"test.css(1): error: function declaration requires all parameters to be variables, IDENTIFIER is not acceptable.\n" -- removed not useful
                "test.css(1): error: somehow a declaration list is missing fields, this happens if you used an invalid variable.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // missing value for optional parameter
    {
        std::stringstream ss;
        ss << "$sum($a1, $a2: 3px, $a3): ($a1+$a2)/$a3;"
           << "div { margin: $sum(6px, 7px, 3); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(1): error: function declarations with optional parameters must make all parameters optional from the first one that is given an optional value up to the end of the list of arguments.\n"
                //"test.css(1): error: unsupported type LIST as a unary expression token.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // missing ':' to define the optional value
    {
        std::stringstream ss;
        ss << "$sum($a1, $a2 3px, $a3): ($a1+$a2)/$a3;"
           << "div { margin: $sum(6px, 7px, 3); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(1): error: function declarations expect variable with optional parameters to use a ':' after the variable name and before the optional value.\n"
                //"test.css(1): error: unsupported type LIST as a unary expression token.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test @include with something else than an identifier or function
    {
        std::stringstream ss;
        ss << "@include url(invalid/token/for/include);\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: @include is expected to be followed by an IDENTIFIER or a FUNCTION naming the variable/mixin to include.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test @include with something else than an identifier or function
    {
        std::stringstream ss;
        ss << "$empty:null;\n"
           << "$empty{color:pink;}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(2): error: a qualified rule without selectors is not valid.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @mixin with one parameter
    {
        std::stringstream ss;
        ss << "@mixin nice-button;";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: a @mixin definition expects exactly two parameters: an identifier or function and a {}-block.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @mixin with one parameter
    {
        std::stringstream ss;
        ss << "@mixin { div { border: 3px solid #ffe093; } }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: a @mixin definition expects exactly two parameters: an identifier or function and a {}-block.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @mixin with too many entries (i.e. "color" " " "#ff3241")
    {
        std::stringstream ss;
        ss << "@mixin color #ff3241;";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: a @mixin definition expects exactly two parameters: an identifier or function and a {}-block.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @mixin not with a {}-block
    {
        std::stringstream ss;
        ss << "@mixin color#ff3241;";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: a @mixin definition expects a {}-block as its second parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @mixin not with a IDENTIFIER or FUNCTION as first parameter
    {
        std::stringstream ss;
        ss << "@mixin #ff3241 { color: full; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: a @mixin expects either an IDENTIFIER or a FUNCTION as its first parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @mixin with VARIABLE generates an special error
    {
        std::stringstream ss;
        ss << "@mixin $var { color: full; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: a @mixin must use an IDENTIFIER or FUNCTION and no a VARIABLE or VARIABLE_FUNCTION.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @mixin with VARIABLE generates an special error
    {
        std::stringstream ss;
        ss << "@mixin $var($a1, $a2) { color: $a1 + $a2 / 2.5; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.set_empty_on_undefined_variable(true);
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: a @mixin must use an IDENTIFIER or FUNCTION and no a VARIABLE or VARIABLE_FUNCTION.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // try !global at the wrong place and see the warning
    {
        std::stringstream ss;
        ss << "$size: 100px;\n"
           << "div { $size: !global 300px;\n"
           << "  entry: {\n"
           << "    $size: ! global 50px;\n"
           << "    width: $size;\n"
           << "    height: $size * 3 / 4;\n"
           << "  };\n"
           << "  junior: $size + 13px;\n"
           << "}\n"
           << "section { diameter: $size }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(2): warning: A special flag, !global in this case, must only appear at the end of a declaration.\n"
                "test.css(3): warning: A special flag, !global in this case, must only appear at the end of a declaration.\n"
            );

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:size\n"
"      LIST\n"
"        VARIABLE \"size\"\n"
"        INTEGER \"px\" I:50\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"entry-width\"\n"
"          ARG\n"
"            INTEGER \"px\" I:50\n"
"        DECLARATION \"entry-height\"\n"
"          ARG\n"
"            INTEGER \"px\" I:37\n"
"        DECLARATION \"junior\"\n"
"          ARG\n"
"            INTEGER \"px\" I:63\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"section\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"diameter\"\n"
"        ARG\n"
"          INTEGER \"px\" I:50\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // try !default at the wrong place and see the warning
    {
        std::stringstream ss;
        ss << "$size: 100px;\n"
           << "div { $size: !default 300px;\n"
           << "  entry: {\n"
           << "    $size: ! default 50px;\n"
           << "    width: $size;\n"
           << "    height: $size * 3 / 4;\n"
           << "  };\n"
           << "  junior: $size + 13px;\n"
           << "}\n"
           << "section { diameter: $size }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(2): warning: A special flag, !default in this case, must only appear at the end of a declaration.\n"
                "test.css(3): warning: A special flag, !default in this case, must only appear at the end of a declaration.\n"
            );

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:size\n"
"      LIST\n"
"        VARIABLE \"size\"\n"
"        INTEGER \"px\" I:100\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"entry-width\"\n"
"          ARG\n"
"            INTEGER \"px\" I:100\n"
"        DECLARATION \"entry-height\"\n"
"          ARG\n"
"            INTEGER \"px\" I:75\n"
"        DECLARATION \"junior\"\n"
"          ARG\n"
"            INTEGER \"px\" I:113\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"section\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"diameter\"\n"
"        ARG\n"
"          INTEGER \"px\" I:100\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("At-Keyword ignored", "[compiler] [at-keyword]")
{
    // make sure @<not supported> is left alone as expected by CSS 3
    {
        std::stringstream ss;
        ss << "@unknown \"This works?\";";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"unknown\" I:0\n"
"    STRING \"This works?\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // make sure @<not supported> is left alone as expected by CSS 3
    {
        std::stringstream ss;
        ss << "@unknown \"Question?\" { this one has a block }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"unknown\" I:0\n"
"    STRING \"Question?\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"this\"\n"
"        WHITESPACE\n"
"        IDENTIFIER \"one\"\n"
"        WHITESPACE\n"
"        IDENTIFIER \"has\"\n"
"        WHITESPACE\n"
"        IDENTIFIER \"a\"\n"
"        WHITESPACE\n"
"        IDENTIFIER \"block\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("At-Keyword messages", "[compiler] [output]")
{
    // generate an error with @error
    {
        std::stringstream ss;
        ss << "@error \"This is an error.\";";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: This is an error.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // generate a warning with @warning
    {
        std::stringstream ss;
        ss << "@warning \"This is a warning.\";";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): warning: This is a warning.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // output a message with @info
    {
        std::stringstream ss;
        ss << "@info \"This is an info message.\";";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): info: This is an info message.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // make sure @message does the same as @info
    {
        std::stringstream ss;
        ss << "@message \"This is an info message.\";";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): info: This is an info message.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test @debug does nothing by default
    {
        std::stringstream ss;
        ss << "@debug \"This is a debug message.\";";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        // by default debug messages do not make it to the output
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // make sure @debug does the same as @info
    {
        std::stringstream ss;
        ss << "@debug \"This is a debug message.\";";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        csspp::error::instance().set_show_debug(true);
        c.compile(true);
        csspp::error::instance().set_show_debug(false);

        VERIFY_ERRORS("test.css(1): debug: This is a debug message.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("At-Keyword with qualified rules", "[compiler] [at-keyword]")
{
    // a valid @document
    {
        std::stringstream ss;
        ss << "@document { body { content: \"Utf-16\" } }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"document\" I:0\n"
"    OPEN_CURLYBRACKET B:true\n"
"      COMPONENT_VALUE\n"
"        ARG\n"
"          IDENTIFIER \"body\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          DECLARATION \"content\"\n"
"            ARG\n"
"              STRING \"Utf-16\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // a valid @document with @if inside of there
    {
        std::stringstream ss;
        ss << "$agent: 'Firefox';\n"
           << "@document {\n"
           << "body { content: \"Utf-16\" }\n"
           << "@if $agent = 'Firefox' { body { margin: 0 } div { border: 1px solid white } }\n"
           << " }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:agent\n"
"      LIST\n"
"        VARIABLE \"agent\"\n"
"        STRING \"Firefox\"\n"
"  AT_KEYWORD \"document\" I:0\n"
"    OPEN_CURLYBRACKET B:true\n"
"      COMPONENT_VALUE\n"
"        ARG\n"
"          IDENTIFIER \"body\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          DECLARATION \"content\"\n"
"            ARG\n"
"              STRING \"Utf-16\"\n"
"      COMPONENT_VALUE\n"
"        ARG\n"
"          IDENTIFIER \"body\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          DECLARATION \"margin\"\n"
"            ARG\n"
"              INTEGER \"\" I:0\n"
"      COMPONENT_VALUE\n"
"        ARG\n"
"          IDENTIFIER \"div\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          DECLARATION \"border\"\n"
"            ARG\n"
"              INTEGER \"px\" I:1\n"
"              WHITESPACE\n"
"              IDENTIFIER \"solid\"\n"
"              WHITESPACE\n"
"              COLOR H:ffffffff\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // a valid @media
    {
        std::stringstream ss;
        ss << "@media screen { i { font-style: normal } }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"media\" I:0\n"
"    ARG\n"
"      IDENTIFIER \"screen\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      COMPONENT_VALUE\n"
"        ARG\n"
"          IDENTIFIER \"i\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          DECLARATION \"font-style\"\n"
"            ARG\n"
"              IDENTIFIER \"normal\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // nested @media
    {
        std::stringstream ss;
        ss << "@media screen {\n"
           << "  i { font-style: normal }\n"
           << "  @media max-width(12cm) {\n"
           << "    b { font-weight: normal }\n"
           << "  }\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"media\" I:0\n"
"    ARG\n"
"      IDENTIFIER \"screen\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      COMPONENT_VALUE\n"
"        ARG\n"
"          IDENTIFIER \"i\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          DECLARATION \"font-style\"\n"
"            ARG\n"
"              IDENTIFIER \"normal\"\n"
"      COMPONENT_VALUE\n"
"        AT_KEYWORD \"media\" I:0\n"
"          ARG\n"
"            FUNCTION \"max-width\"\n"
"              INTEGER \"cm\" I:12\n"
"          OPEN_CURLYBRACKET B:true\n"
"            COMPONENT_VALUE\n"
"              ARG\n"
"                IDENTIFIER \"b\"\n"
"              OPEN_CURLYBRACKET B:true\n"
"                DECLARATION \"font-weight\"\n"
"                  ARG\n"
"                    IDENTIFIER \"normal\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // a valid @supports
    {
        std::stringstream ss;
        ss << "@supports not (screen and desktop) { b { font-weight: normal } }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"supports\" I:0\n"
"    ARG\n"
"      IDENTIFIER \"not\"\n"
"      OPEN_PARENTHESIS\n"
"        IDENTIFIER \"screen\"\n"
"        WHITESPACE\n"
"        IDENTIFIER \"and\"\n"
"        WHITESPACE\n"
"        IDENTIFIER \"desktop\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      COMPONENT_VALUE\n"
"        ARG\n"
"          IDENTIFIER \"b\"\n"
"        OPEN_CURLYBRACKET B:true\n"
"          DECLARATION \"font-weight\"\n"
"            ARG\n"
"              IDENTIFIER \"normal\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid at-keyword expecting qualified rules", "[compiler] [at-keyword]")
{
    // a @supports without a {}-block
    {
        std::stringstream ss;
        ss << "@supports not (screen and desktop);\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"supports\" I:0\n"
"    IDENTIFIER \"not\"\n"
"    OPEN_PARENTHESIS\n"
"      IDENTIFIER \"screen\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"and\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"desktop\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("At-Keyword with declarations", "[compiler] [at-keyword]")
{
    // a valid @page
    {
        std::stringstream ss;
        ss << "@page { left: 2in; right: 2.2in; }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"page\" I:0\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"left\"\n"
"          ARG\n"
"            INTEGER \"in\" I:2\n"
"        DECLARATION \"right\"\n"
"          ARG\n"
"            DECIMAL_NUMBER \"in\" D:2.2\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @page with an @media inside
    {
        std::stringstream ss;
        ss << "@page {\n"
           << "  left: 2in;\n"
           << "  right: 2.2in;\n"
           << "  @media screen {\n"
           << "    .arg { color: grey }\n"
           << "  }\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"page\" I:0\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"left\"\n"
"          ARG\n"
"            INTEGER \"in\" I:2\n"
"        DECLARATION \"right\"\n"
"          ARG\n"
"            DECIMAL_NUMBER \"in\" D:2.2\n"
"        COMPONENT_VALUE\n"
"          AT_KEYWORD \"media\" I:0\n"
"            ARG\n"
"              IDENTIFIER \"screen\"\n"
"            OPEN_CURLYBRACKET B:true\n"
"              COMPONENT_VALUE\n"
"                ARG\n"
"                  PERIOD\n"
"                  IDENTIFIER \"arg\"\n"
"                OPEN_CURLYBRACKET B:true\n"
"                  DECLARATION \"color\"\n"
"                    ARG\n"
"                      COLOR H:ff808080\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // a valid @supports
    {
        std::stringstream ss;
        ss << "@font-face{unicode-range: U+4??;font-style:italic}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"font-face\" I:0\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"unicode-range\"\n"
"          ARG\n"
"            UNICODE_RANGE I:5493263172608\n"
"        DECLARATION \"font-style\"\n"
"          ARG\n"
"            IDENTIFIER \"italic\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Charset", "[compiler] [invalid]")
{
    // a valid @charset
    {
        std::stringstream ss;
        ss << "@charset \"Utf-8\";\n"
           << "html{margin:0}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"html\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"margin\"\n"
"        ARG\n"
"          INTEGER \"\" I:0\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // a valid @charset with many spaces
    {
        std::stringstream ss;
        ss << "   @charset   \"   UTF-8   \"   ;\n"
           << "html{margin:0}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"html\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"margin\"\n"
"        ARG\n"
"          INTEGER \"\" I:0\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // an @charset with a refused encoding
    {
        std::stringstream ss;
        ss << "@charset \"iso-8859-6\";\n"
           << "html{margin:0}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: we only support @charset \"utf-8\";, any other encoding is refused.\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"html\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"margin\"\n"
"        ARG\n"
"          INTEGER \"\" I:0\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // an @charset with a decimal number
    {
        std::stringstream ss;
        ss << "@charset 8859.6;\n"
           << "html{margin:0}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: the @charset is expected to be followed by exactly one string.\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"html\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"margin\"\n"
"        ARG\n"
"          INTEGER \"\" I:0\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Conditional compilation", "[compiler] [conditional]")
{
    // script with @if / @else if / @else keywords
    {
        std::stringstream ss;
        ss << "$var: true;\n"
           << "@if $var { @message \"Got here! (1)\" ; }\n"
           << "@else if $var { @message \"Got here! (2)\";}\n"
           << "@else{@message\"Got here! (3)\";}\n"
           << "ul { list: cross; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(2): info: Got here! (1)\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        VARIABLE \"var\"\n"
"        IDENTIFIER \"true\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"ul\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"list\"\n"
"        ARG\n"
"          IDENTIFIER \"cross\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // script with @if / @else if / @else keywords
    {
        std::stringstream ss;
        ss << "$var: 2;\n"
           << "@if $var = 1 { @message \"Got here! (1)\" ; }\n"
           << "@else if $var = 2 { @message \"Got here! (2)\";}\n"
           << "@else{@message\"Got here! (3)\";}\n"
           << "ul { list: cross; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(3): info: Got here! (2)\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        VARIABLE \"var\"\n"
"        INTEGER \"\" I:2\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"ul\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"list\"\n"
"        ARG\n"
"          IDENTIFIER \"cross\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // script with @if / @else if / @else keywords
    {
        std::stringstream ss;
        ss << "$var: -192;\n"
           << "@if $var = 1 { @message \"Got here! (1)\" ; }\n"
           << "@else if $var = 2 { @message \"Got here! (2)\";}\n"
           << "@else{@message\"Got here! (3)\";}\n"
           << "ul { list: cross; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(4): info: Got here! (3)\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        VARIABLE \"var\"\n"
"        INTEGER \"\" I:-192\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"ul\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"list\"\n"
"        ARG\n"
"          IDENTIFIER \"cross\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid conditional", "[compiler] [conditional] [invalid]")
{
    // script with @if / @else if / @else keywords
    // invalid "@else if" which includes an expression
    {
        std::stringstream ss;
        ss << "$zzvar: false;\n"
           << "@if { @message \"Got here! (1)\" ; }\n"
           << "@else if { @message \"Got here! (2)\";}\n"
           << "@else{@message\"Got here! (3)\";}\n"
           << "ul { list: cross; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(2): error: @if is expected to have exactly 2 parameters: an expression and a block. This @if has 1 parameters.\n"
                "test.css(3): error: '@else if ...' is missing an expression or a block.\n"
                //"test.css(3): error: a standalone @else is not legal, it has to be preceeded by an @if ... or @else if ...\n"
                //"test.css(4): error: a standalone @else is not legal, it has to be preceeded by an @if ... or @else if ...\n"
            );

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"    V:zzvar\n"
"      LIST\n"
"        VARIABLE \"zzvar\"\n"
"        IDENTIFIER \"false\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"ul\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"list\"\n"
"        ARG\n"
"          IDENTIFIER \"cross\"\n"
+ csspp_test::get_close_comment(true)

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // script with @if / @else if / @else keywords
    // invalid "@else if" which includes an expression
    {
        std::stringstream ss;
        ss << "$var: false;\n"
           << "@if $var { @message \"Got here! (1)\" ; }\n"
           << "@else if + { @message \"Got here! (2)\";}\n"
           << "@else{@message\"Got here! (3)\";}\n"
           << "ul { list: cross; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(3): error: unsupported type OPEN_CURLYBRACKET as a unary expression token.\n"
                "test.css(3): error: '@else { ... }' is expected to have 1 parameter, '@else if ... { ... }' is expected to have 2 parameters. This @else has 2 parameters.\n"
                //"test.css(4): error: a standalone @else is not legal, it has to be preceeded by an @if ... or @else if ...\n"
            );

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        VARIABLE \"var\"\n"
"        IDENTIFIER \"false\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"ul\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"list\"\n"
"        ARG\n"
"          IDENTIFIER \"cross\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // script with @if / @else if / @else keywords
    // invalid "@else" which includes an expression
    {
        std::stringstream ss;
        ss << "$var: false;\n"
           << "@if $var { @message \"Got here! (1)\" ; }\n"
           << "@else if $var { @message \"Got here! (2)\";}\n"
           << "@else $var {@message\"Got here! (3)\";}\n"   // TODO: this doesn't get caught?!
           << "ul { list: cross; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(""
                "test.css(4): error: '@else { ... }' is expected to have 1 parameter, '@else if ... { ... }' is expected to have 2 parameters. This @else has 2 parameters.\n"
                //"test.css(3): error: '@else if ...' is missing an expression or a block.\n"
                //"test.css(3): error: '@else { ... }' cannot follow another '@else { ... }'. Maybe you are missing an 'if expr'?\n"
                //"test.css(4): error: a standalone @else is not legal, it has to be preceeded by an @if ... or @else if ...\n"
                //"test.css(3): info: Got here! (2)\n"
            );

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        VARIABLE \"var\"\n"
"        IDENTIFIER \"false\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"ul\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"list\"\n"
"        ARG\n"
"          IDENTIFIER \"cross\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // script with @if / @else if / @else keywords
    // spurious "@else"
    {
        std::stringstream ss;
        ss << "$var: false;\n"
           << "@if $var { @message \"Got here! (1)\" ; }\n"
           << "@else if $var { @message \"Got here! (2)\";}\n"
           << "@else {@message\"Got here! (3)\";}\n"
           << "@else { @message\"Spurious! (4)\";}\n"
           << "ul { list: cross; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(""
                "test.css(4): error: '@else { ... }' cannot follow another '@else { ... }'. Maybe you are missing an 'if expr'?\n"
                "test.css(5): error: a standalone @else is not legal, it has to be preceeded by an @if ... or @else if ...\n"
                //"test.css(4): info: Got here! (3)\n"
            );

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"    V:var\n"
"      LIST\n"
"        VARIABLE \"var\"\n"
"        IDENTIFIER \"false\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"ul\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"list\"\n"
"        ARG\n"
"          IDENTIFIER \"cross\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("User @import", "[compiler] [at-keyword]")
{
    // @import with a valid URL
    {
        // write a file (in a block so it gets flushed and closed)
        {
            std::ofstream importing;
            importing.open("importing.scss");
            CATCH_REQUIRE(!!importing);
            importing << "/* @preserve this worked! {$_csspp_version} */";
        }
        std::stringstream ss;
        {
            std::unique_ptr<char, void (*)(char *)> cwd(get_current_dir_name(), free_char);
            ss << "@import url(file://"
               << cwd.get()
               << "/importing.scss);";
        }
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMMENT \"@preserve this worked! " CSSPP_VERSION "\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        unlink("importing.scss");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @import with a valid path as a URL (thus not recognized as a file://)
    {
        std::stringstream ss;
        ss << "@import url(system/version);";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"import\" I:0\n"
"    URL \"system/version\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @import with a valid path as a URL (thus not recognized as a file://)
    {
        std::stringstream ss;
        ss << "@import 'http://csspp.org/css/special.css';";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"import\" I:0\n"
"    STRING \"http://csspp.org/css/special.css\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid @import", "[compiler] [at-keyword] [invalid]")
{
    // @import with URL representing a an inexistant file
    {
        std::stringstream ss;
        ss << "@import url(file:///this/shall/not/exist/anywhere/on/your/drive);";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): info: @import uri(/this/shall/not/exist/anywhere/on/your/drive); left alone by the CSS Preprocessor, no matching file found.\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"import\" I:0\n"
"    URL \"file:///this/shall/not/exist/anywhere/on/your/drive\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @import with URL representing a an inexistant file
    {
        std::stringstream ss;
        ss << "@import url(file://this/shall/not/exist/either/on/your/drive);";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): info: @import uri(/this/shall/not/exist/either/on/your/drive); left alone by the CSS Preprocessor, no matching file found.\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"import\" I:0\n"
"    URL \"file://this/shall/not/exist/either/on/your/drive\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @import with a string that includes a URL
    {
        std::stringstream ss;
        ss << "@import \"file://this/shall/not/ever/exist/on/your/drive\";";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): info: @import \"/this/shall/not/ever/exist/on/your/drive\"; left alone by the CSS Preprocessor, no matching file found.\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"import\" I:0\n"
"    STRING \"file://this/shall/not/ever/exist/on/your/drive\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @import with a string that includes a URL
    {
        std::stringstream ss;
        ss << "@import \"include/a/file:///in/the/filename/but/still/a/regular/filename\";";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): info: @import \"include/a/file:///in/the/filename/but/still/a/regular/filename\"; left alone by the CSS Preprocessor, no matching file found.\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"import\" I:0\n"
"    STRING \"include/a/file:///in/the/filename/but/still/a/regular/filename\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @import a script named "" (empty string!)
    {
        std::stringstream ss;
        ss << "@import \"\";";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(true);

        VERIFY_ERRORS("test.css(1): error: @import \"\"; and @import url(); are not valid.\n");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"import\" I:0\n"
"    STRING \"\"\n"

            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid variable in comment", "[compiler] [conditional] [invalid]")
{
    // variable is not defined
    {
        std::stringstream ss;
        ss << "/* @preserve this variable is #{$unknown} */\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(1): warning: variable named \"unknown\", used in a comment, is not set.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // variable is not defined
    {
        std::stringstream ss;
        ss << "$func($arg): { color: $arg + #010101; };\n"
           << "/* @preserve this variable is #{$func(#030303)} */\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(2): warning: variable named \"func\", is a function which is not supported in a comment.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // variable is not defined
    {
        std::stringstream ss;
        ss << "$simple_var: { color: #0568FF + #010101; };\n"
           << "/* @preserve this variable is #{$simple_var(#030303)} */\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS(
                "test.css(2): warning: variable named \"simple_var\", is not a function, yet you referenced it as such (and functions are not yet supported in comments).\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Compile keyframes", "[compiler] [stylesheet] [attribute]")
{
    {
        std::stringstream ss;
        ss << "/* testing keyframes */"
           << "normal { right: 45px; }\n"
           << "@keyframes progress-bar-stripes\n"
              "{\n"
              "  from {\n"
              "    background-position: 40px 0;\n"
              "    left: 0;\n"
              "  }\n"
              "  30% {\n"
              "    background-position: 30px 0;\n"
              "    left: 20px;\n"
              "  }\n"
              "  60% {\n"
              "    background-position: 5px 0;\n"
              "    left: 27px;\n"
              "  }\n"
              "  to {\n"
              "    background-position: 0 0;\n"
              "    left: 35px;\n"
              "  }\n"
              "}\n"
           << "/* @preserver test \"Compile keyframes\" */";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"normal\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"right\"\n"
"        ARG\n"
"          INTEGER \"px\" I:45\n"
"  AT_KEYWORD \"keyframes\" I:0\n"
"    IDENTIFIER \"progress-bar-stripes\"\n"
"    FRAME D:0\n"
"      DECLARATION \"background-position\"\n"
"        ARG\n"
"          INTEGER \"px\" I:40\n"
"          WHITESPACE\n"
"          INTEGER \"\" I:0\n"
"      DECLARATION \"left\"\n"
"        ARG\n"
"          INTEGER \"\" I:0\n"
"    FRAME D:0.3\n"
"      DECLARATION \"background-position\"\n"
"        ARG\n"
"          INTEGER \"px\" I:30\n"
"          WHITESPACE\n"
"          INTEGER \"\" I:0\n"
"      DECLARATION \"left\"\n"
"        ARG\n"
"          INTEGER \"px\" I:20\n"
"    FRAME D:0.6\n"
"      DECLARATION \"background-position\"\n"
"        ARG\n"
"          INTEGER \"px\" I:5\n"
"          WHITESPACE\n"
"          INTEGER \"\" I:0\n"
"      DECLARATION \"left\"\n"
"        ARG\n"
"          INTEGER \"px\" I:27\n"
"    FRAME D:1\n"
"      DECLARATION \"background-position\"\n"
"        ARG\n"
"          INTEGER \"\" I:0\n"
"          WHITESPACE\n"
"          INTEGER \"\" I:0\n"
"      DECLARATION \"left\"\n"
"        ARG\n"
"          INTEGER \"px\" I:35\n"
"  COMMENT \"@preserver test \"Compile keyframes\"\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // without spaces
    {
        std::stringstream ss;
        ss << "/* testing compile */"
           << "body,a[q]>b[p=\"344.5\"]+c[z=33]~d[e],html *[ff=fire] *.blue { background:white url(/images/background.png) }"
           << "/* @preserver test \"Compile Simple Stylesheet\" with version #{$_csspp_major}.#{$_csspp_minor} */";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"body\"\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"q\"\n"
"      GREATER_THAN\n"
"      IDENTIFIER \"b\"\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"p\"\n"
"        EQUAL\n"
"        STRING \"344.5\"\n"
"      ADD\n"
"      IDENTIFIER \"c\"\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"z\"\n"
"        EQUAL\n"
"        INTEGER \"\" I:33\n"
"      PRECEDED\n"
"      IDENTIFIER \"d\"\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"e\"\n"
"    ARG\n"
"      IDENTIFIER \"html\"\n"
"      WHITESPACE\n"
"      OPEN_SQUAREBRACKET\n"
"        IDENTIFIER \"ff\"\n"
"        EQUAL\n"
"        IDENTIFIER \"fire\"\n"
"      WHITESPACE\n"
"      PERIOD\n"
"      IDENTIFIER \"blue\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"background\"\n"
"        ARG\n"
"          COLOR H:ffffffff\n"
"          WHITESPACE\n"
"          URL \"/images/background.png\"\n"
"  COMMENT \"@preserver test \"Compile Simple Stylesheet\" with version 1.0\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // rules with !important
    {
        std::stringstream ss;
        ss << "div.blackness { color: red !important }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"      PERIOD\n"
"      IDENTIFIER \"blackness\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\" F:important\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // rules with ! important
    {
        std::stringstream ss;
        ss << "div.blackness { color: red ! important }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"      PERIOD\n"
"      IDENTIFIER \"blackness\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\" F:important\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // rules with !important and no spaces
    {
        std::stringstream ss;
        ss << "div.blackness { color: red!important }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"      PERIOD\n"
"      IDENTIFIER \"blackness\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\" F:important\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // empty rules have to compile too
    {
        std::stringstream ss;
        ss << "div.blackness section.light span.clear\n"
           << "{\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables()
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // special IE8 value which has to be skipped
    {
        std::stringstream ss;
        ss << ".transparent img\n"
           << "{\n"
           << "  $alpha: 5% * 4;\n"
           << "  filter: opacity($alpha);\n"
           << "  filter: alpha( opacity=20 );\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        // no error left over
        VERIFY_ERRORS(
                "test.css(4): warning: the alpha(), chroma() and similar functions of the filter field are Internet Explorer specific extensions which are not supported across browsers.\n"
                "test.css(5): warning: the alpha(), chroma() and similar functions of the filter field are Internet Explorer specific extensions which are not supported across browsers.\n"
            );

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      PERIOD\n"
"      IDENTIFIER \"transparent\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"img\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"        V:alpha\n"
"          LIST\n"
"            VARIABLE \"alpha\"\n"
"            LIST\n"
"              PERCENT D:0.05\n"
"              WHITESPACE\n"
"              MULTIPLY\n"
"              WHITESPACE\n"
"              INTEGER \"\" I:4\n"
"      LIST\n"
"        DECLARATION \"filter\"\n"
"          FUNCTION \"opacity\"\n"
"            PERCENT D:0.05\n"
"            WHITESPACE\n"
"            MULTIPLY\n"
"            WHITESPACE\n"
"            INTEGER \"\" I:4\n"
"        DECLARATION \"filter\"\n"
"          FUNCTION \"alpha\"\n"
"            IDENTIFIER \"opacity\"\n"
"            EQUAL\n"
"            INTEGER \"\" I:20\n"
+ csspp_test::get_close_comment(true)

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // a simple test with '--no-logo' specified
    {
        std::stringstream ss;
        ss << ".box\n"
           << "{\n"
           << "  color: $_csspp_no_logo ? red : blue;\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.set_no_logo();
        c.clear_paths();
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        // no error left over
        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
+ csspp_test::get_default_variables(csspp_test::flag_no_logo_true) +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      PERIOD\n"
"      IDENTIFIER \"box\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"color\"\n"
"        ARG\n"
"          COLOR H:ff0000ff\n"
//+ csspp_test::get_close_comment(true) -- with --no-logo this is gone

            );

        // no error left over
        VERIFY_ERRORS("");

        CATCH_REQUIRE(c.get_root() == n);
    }
}

// This does not work under Linux, the ifstream.open() accepts a
// directory name as input without generating an error
//
//CATCH_TEST_CASE("Cannot open file", "[compiler] [invalid] [input]")
//{
//    // generate an error with @error
//    {
//        // create a directory in place of the script, so it exists
//        // and is readable but cannot be opened
//        rmdir("pseudo-nth-functions.scss"); // in case you run more than once
//        CATCH_REQUIRE(mkdir("pseudo-nth-functions.scss", 0700) == 0);
//
//        std::stringstream ss;
//        ss << "div:nth-child(3n+2){font-style:normal}";
//        csspp::position pos("test.css");
//        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));
//
//        csspp::parser p(l);
//
//        csspp::node::pointer_t n(p.stylesheet());
//
//        // no errors so far
//        VERIFY_ERRORS("");
//
//        csspp::compiler c;
//        c.set_root(n);
//        c.clear_paths();
//        c.add_path(".");
//
//        CATCH_REQUIRE_THROWS_AS(c.compile(true), csspp::csspp_exception_exit);
//
//        // TODO: use an RAII class instead
//        rmdir("pseudo-nth-functions.scss"); // in case you run more than once
//
//        VERIFY_ERRORS("pseudo-nth-functions(1): fatal: validation script \"pseudo-nth-functions\" was not found.\n");
//
//        CATCH_REQUIRE(c.get_root() == n);
//    }
//
//    // no left over?
//    VERIFY_ERRORS("");
//}

// vim: ts=4 sw=4 et
