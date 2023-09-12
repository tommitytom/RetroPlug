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
 * \brief Test the expression.cpp file: "(..., ..., ...)" (list) operator.
 *
 * This test runs a battery of tests agains the expression.cpp "," (list)
 * operator to ensure full coverage and that all possible left
 * hand side and right hand side types are checked for the equality
 * CSS Preprocessor extensions.
 *
 * Note that all the tests use the full chain: lexer, parser, compiler,
 * and assembler to make sure the results are correct. So these tests
 * exercise the assembler even more than the assembler tests, except that
 * it only checks that compressed results are correct instead of all
 * output modes, since its only goal is covering all the possible
 * expression cases and not the assembler, compiler, parser, and lexer
 * classes.
 */

// csspp
//
#include    <csspp/assembler.h>
#include    <csspp/compiler.h>
#include    <csspp/exception.h>
#include    <csspp/parser.h>


// self
//
#include    "catch_main.h"


// C++
//
#include    <sstream>


// last include
//
#include    <snapdev/poison.h>



CATCH_TEST_CASE("Expression arrays", "[expression] [list] [array]")
{
    CATCH_START_SECTION("test a compiled array")
    {
        std::stringstream ss;
        ss << "div { z-index: (15, 1, -39, 44, 10); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        // to verify that the result is still an INTEGER we have to
        // test the root node here
        std::stringstream compiler_out;
        compiler_out << *n;
        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          ARRAY\n"
"            INTEGER \"\" I:15\n"
"            INTEGER \"\" I:1\n"
"            INTEGER \"\" I:-39\n"
"            INTEGER \"\" I:44\n"
"            INTEGER \"\" I:10\n"
+ csspp_test::get_close_comment(true)

            );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("create an array and retrieve each element")
    {
        int const results[6] =
        {
            // first number is unused, just makes it more practical to have it
            0, 15, 1, -39, 44, 10
        };

        for(int idx(1); idx <= 5; ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: (15, 1, -39, 44, 10)["
               << idx
               << "]; }"
               << "span { z-index: (15, 1, -39, 44, 10)["
               << -idx
               << "]; }";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            csspp::compiler c;
            c.set_root(n);
            c.set_date_time_variables(csspp_test::get_now());
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

            // to verify that the result is still an INTEGER we have to
            // test the root node here
            std::stringstream compiler_out;
            compiler_out << *n;
            VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(results[idx]) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(results[6 - idx]) + "\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:" + std::to_string(results[idx]) + "}"
"span{z-index:" + std::to_string(results[6 - idx]) + "}"
"\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("use list to do some computation and retrieve the last result")
    {
        std::stringstream ss;
        ss << "div {\n"
           << "  border: (v := 3px, w := 51px, x := v + w, x / 2.7)[-1] solid #f1a932;\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        // to verify that the result is still an INTEGER we have to
        // test the root node here
        std::stringstream compiler_out;
        compiler_out << *n;
        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"px\" D:20\n"
"          WHITESPACE\n"
"          IDENTIFIER \"solid\"\n"
"          WHITESPACE\n"
"          COLOR H:ff32a9f1\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{"
  "border:20px solid #f1a932"
"}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression maps", "[expression] [list] [map]")
{
    CATCH_START_SECTION("test a compiled map")
    {
        std::stringstream ss;
        ss << "div { z-index: (a: 15, b:1,c: -39,d:44,  e  :  10  ); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        // to verify that the result is still an INTEGER we have to
        // test the root node here
        std::stringstream compiler_out;
        compiler_out << *n;
        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          MAP\n"
"            IDENTIFIER \"a\"\n"
"            INTEGER \"\" I:15\n"
"            IDENTIFIER \"b\"\n"
"            INTEGER \"\" I:1\n"
"            IDENTIFIER \"c\"\n"
"            INTEGER \"\" I:-39\n"
"            IDENTIFIER \"d\"\n"
"            INTEGER \"\" I:44\n"
"            IDENTIFIER \"e\"\n"
"            INTEGER \"\" I:10\n"
+ csspp_test::get_close_comment(true)

            );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("create a map and retrieve each element with block-[] (number and name) and '.<name>'")
    {
        int const results[6] =
        {
            // first number is unused, just makes it more practical to have it
            0, 15, 1, -39, 44, 10
        };
        char const * names[6] =
        {
            "", "abc", "bear", "charly", "dear", "electric"
        };

        // retrieve using an index
        for(int idx(1); idx <= 5; ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: (abc: 15, bear: 1, charly : -39, dear: 44, electric: 10)["
               << idx
               << "]; }"
               << "span { z-index: (abc : 15, bear:1, charly: -39, dear:44, electric : 10)["
               << -idx
               << "]; }";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            csspp::compiler c;
            c.set_root(n);
            c.set_date_time_variables(csspp_test::get_now());
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

            // to verify that the result is still an INTEGER we have to
            // test the root node here
            std::stringstream compiler_out;
            compiler_out << *n;
            VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(results[idx]) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(results[6 - idx]) + "\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:" + std::to_string(results[idx]) + "}"
"span{z-index:" + std::to_string(results[6 - idx]) + "}"
"\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }

        // retrieve using an identifier, a string, and the period syntax
        for(int idx(1); idx <= 5; ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: (abc: 15, bear: 1, charly: -39, dear: 44, electric: 10)"
               << (rand() % 4 ? "" : " ")
               << "["
               << (rand() % 4 ? "" : " ")
               << names[idx]
               << (rand() % 4 ? "" : " ")
               << "]; }"
               << "p { z-index: (abc: 15, bear: 1, charly: -39, dear: 44, electric: 10)"
               << (rand() % 4 ? "" : " ")
               << "["
               << (rand() % 4 ? "" : " ")
               << "'"
               << names[idx]
               << "'"
               << (rand() % 4 ? "" : " ")
               << "]; }"
               << "span { z-index: (abc: 15, bear: 1, charly: -39, dear: 44, electric: 10)"
               << (rand() % 4 ? "" : " ")
               << "."
               << (rand() % 4 ? "" : " ")
               << names[idx]
               << "; }";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            csspp::compiler c;
            c.set_root(n);
            c.set_date_time_variables(csspp_test::get_now());
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

            // to verify that the result is still an INTEGER we have to
            // test the root node here
            std::stringstream compiler_out;
            compiler_out << *n;
            VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(results[idx]) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(results[idx]) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(results[idx]) + "\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:" + std::to_string(results[idx]) + "}"
"p{z-index:" + std::to_string(results[idx]) + "}"
"span{z-index:" + std::to_string(results[idx]) + "}"
"\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("test with empty entries in a map")
    {
        char const * results[6] =
        {
            // first number is unused, just makes it more practical to have it
            "", "15", "-3", "", "44", "11"
        };
        char const * names[6] =
        {
            "", "fab", "kangoroo", "angles", "style", "more"
        };

        // retrieve using an index
        for(int idx(1); idx <= 5; ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: (fab: 15, kangoroo: -3, angles: , style: 44, more: 11)["
               << idx
               << "]; }"
               << "span { z-index: (fab: 15, kangoroo: -3, angles: , style: 44, more: 11)["
               << -idx
               << "]; }";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            csspp::compiler c;
            c.set_root(n);
            c.set_date_time_variables(csspp_test::get_now());
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

            // to verify that the result is still an INTEGER we have to
            // test the root node here
            std::stringstream compiler_out;
            compiler_out << *n;

            if(idx == 3)
            {
                VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          NULL_TOKEN\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          NULL_TOKEN\n"
+ csspp_test::get_close_comment(true)

                    );
            }
            else
            {
                VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + results[idx] + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + results[6 - idx] + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + results[idx] + "}"
"span{z-index:" + results[6 - idx] + "}"
"\n"
+ csspp_test::get_close_comment()

                        );
            }

            CATCH_REQUIRE(c.get_root() == n);
        }

        // retrieve using an identifier, a string, and the period syntax
        for(int idx(1); idx <= 5; ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: (fab: 15, kangoroo: -3, angles: , style: 44, more: 11)"
               << (rand() % 4 ? "" : " ")
               << "["
               << (rand() % 4 ? "" : " ")
               << names[idx]
               << (rand() % 4 ? "" : " ")
               << "]; }"
               << "p { z-index: (fab: 15, kangoroo: -3, angles: , style: 44, more: 11)"
               << (rand() % 4 ? "" : " ")
               << "["
               << (rand() % 4 ? "" : " ")
               << "'"
               << names[idx]
               << "'"
               << (rand() % 4 ? "" : " ")
               << "]; }"
               << "span { z-index: (fab: 15, kangoroo: -3, angles: , style: 44, more: 11)"
               << (rand() % 4 ? "" : " ")
               << "."
               << (rand() % 4 ? "" : " ")
               << names[idx]
               << "; }";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            csspp::compiler c;
            c.set_root(n);
            c.set_date_time_variables(csspp_test::get_now());
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

            // to verify that the result is still an INTEGER we have to
            // test the root node here
            std::stringstream compiler_out;
            compiler_out << *n;

            if(idx == 3)
            {
                VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          NULL_TOKEN\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          NULL_TOKEN\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          NULL_TOKEN\n"
+ csspp_test::get_close_comment(true)

                    );
            }
            else
            {
                VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + results[idx] + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + results[idx] + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + results[idx] + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + results[idx] + "}"
"p{z-index:" + results[idx] + "}"
"span{z-index:" + results[idx] + "}"
"\n"
+ csspp_test::get_close_comment()

                        );
            }

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("test once more with no ending value")
    {
        char const * results[6] =
        {
            // first number is unused, just makes it more practical to have it
            "", "15", "-3", "-19", "44", ""
        };
        char const * names[6] =
        {
            "", "fab", "kangoroo", "angles", "style", "more"
        };

        // retrieve using an index
        for(int idx(1); idx <= 5; ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: (fab: 15, kangoroo: -3, angles: -19, style: 44, more: )["
               << idx
               << "]; }"
               << "span { z-index: (fab: 15, kangoroo: -3, angles: -19, style: 44, more: )["
               << -idx
               << "]; }";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            csspp::compiler c;
            c.set_root(n);
            c.set_date_time_variables(csspp_test::get_now());
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

            // to verify that the result is still an INTEGER we have to
            // test the root node here
            std::stringstream compiler_out;
            compiler_out << *n;

            VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"

+ (idx == 5 ?
"          NULL_TOKEN\n" :
"          INTEGER \"\" I:" + std::string(results[idx]) + "\n") +

"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"

+ (idx == 1 ?
"          NULL_TOKEN\n" :
"          INTEGER \"\" I:" + std::string(results[6 - idx]) + "\n")

+ csspp_test::get_close_comment(true)

                );

            // 1 and 5 have NULL_TOKEN that the assembler would barf on
            if(idx != 1 && idx != 5)
            {
                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + results[idx] + "}"
"span{z-index:" + results[6 - idx] + "}"
"\n"
+ csspp_test::get_close_comment()

                        );
            }

            CATCH_REQUIRE(c.get_root() == n);
        }

        // retrieve using an identifier, a string, and the period syntax
        for(int idx(1); idx <= 5; ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: (fab: 15, kangoroo: -3, angles: -19, style: 44, more: )"
               << (rand() % 4 ? "" : " ")
               << "["
               << (rand() % 4 ? "" : " ")
               << names[idx]
               << (rand() % 4 ? "" : " ")
               << "]; }"
               << "p { z-index: (fab: 15, kangoroo: -3, angles: -19, style: 44, more: )"
               << (rand() % 4 ? "" : " ")
               << "["
               << (rand() % 4 ? "" : " ")
               << "'"
               << names[idx]
               << "'"
               << (rand() % 4 ? "" : " ")
               << "]; }"
               << "span { z-index: (fab: 15, kangoroo: -3, angles: -19, style: 44, more: )"
               << (rand() % 4 ? "" : " ")
               << "."
               << (rand() % 4 ? "" : " ")
               << names[idx]
               << "; }";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            csspp::compiler c;
            c.set_root(n);
            c.set_date_time_variables(csspp_test::get_now());
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

            // to verify that the result is still an INTEGER we have to
            // test the root node here
            std::stringstream compiler_out;
            compiler_out << *n;

            if(idx == 5)
            {
                VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          NULL_TOKEN\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          NULL_TOKEN\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          NULL_TOKEN\n"
+ csspp_test::get_close_comment(true)

                    );
            }
            else
            {
                VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + results[idx] + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + results[idx] + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + results[idx] + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + results[idx] + "}"
"p{z-index:" + results[idx] + "}"
"span{z-index:" + results[idx] + "}"
"\n"
+ csspp_test::get_close_comment()

                        );
            }

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression invalid lists", "[expression] [list] [array] [map] [invalid]")
{
    CATCH_START_SECTION("array was an invalid number")
    {
        std::stringstream ss;
        ss << "div { border: (1, ?, 3); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: unsupported type CONDITIONAL as a unary expression token.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("array accessed with an invalid index")
    {
        std::stringstream ss;
        ss << "div { border: (1, 2, 3)[?]; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: unsupported type CONDITIONAL as a unary expression token.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("dereferencing something which cannot be dereferenced")
    {
        std::stringstream ss;
        ss << "div { border: U+A??[1]; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: unsupported type UNICODE_RANGE for the 'array[<index>]' operation.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("array accessed with a decimal number index")
    {
        std::stringstream ss;
        ss << "div { border: (1, 2, 3)[3.4]; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: an integer, an identifier, or a string was expected as the index (defined in '[ ... ]'). A DECIMAL_NUMBER was not expected.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("array[0] is invalid")
    {
        std::stringstream ss;
        ss << "div { border: (1, 2, 3)[0]; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: index 0 is out of range. The allowed range is 1 to 3.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("array[-x or +y] are invalid when out of range")
    {
        for(int idx(4); idx <= 100; ++idx)
        {
            // from the front
            {
                std::stringstream ss;
                ss << "div { border: (1, 2, 3)["
                   << idx
                   << "]; }";
                csspp::position pos("test.css");
                csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

                csspp::parser p(l);

                csspp::node::pointer_t n(p.stylesheet());

                csspp::compiler c;
                c.set_root(n);
                c.set_date_time_variables(csspp_test::get_now());
                c.add_path(csspp_test::get_script_path());
                c.add_path(csspp_test::get_version_script_path());

                c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

                std::stringstream errmsg;
                errmsg << "test.css(1): error: index "
                       << idx
                       << " is out of range. The allowed range is 1 to 3.\n";
                VERIFY_ERRORS(errmsg.str());

                CATCH_REQUIRE(c.get_root() == n);
            }

            // from the back
            {
                std::stringstream ss;
                ss << "div { border: (1, 2, 3)[-"
                   << idx
                   << "]; }";
                csspp::position pos("test.css");
                csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

                csspp::parser p(l);

                csspp::node::pointer_t n(p.stylesheet());

                csspp::compiler c;
                c.set_root(n);
                c.set_date_time_variables(csspp_test::get_now());
                c.add_path(csspp_test::get_script_path());
                c.add_path(csspp_test::get_version_script_path());

                c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

                std::stringstream errmsg;
                errmsg << "test.css(1): error: index "
                       << -idx
                       << " is out of range. The allowed range is 1 to 3.\n";
                VERIFY_ERRORS(errmsg.str());

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("array.field is not invalid")
    {
        std::stringstream ss;
        ss << "div { border: (1, 2, 3).unexpected; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: unsupported left handside type ARRAY for the '<map>.<identifier>' operation.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("map with an invalid number")
    {
        std::stringstream ss;
        ss << "div { border: (aaa: 1, bbb: ?, ccc: 3); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: unsupported type CONDITIONAL as a unary expression token.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("map accessed with an invalid index")
    {
        std::stringstream ss;
        ss << "div { border: (poors: 1, man: 2, test: 3)[?]; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: unsupported type CONDITIONAL as a unary expression token.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("map accessed with a decimal number index")
    {
        std::stringstream ss;
        ss << "div { border: (map: 1, and: 2, decimal_number: 3)[3.4]; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: an integer, an identifier, or a string was expected as the index (defined in '[ ... ]'). A DECIMAL_NUMBER was not expected.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("map[0] is invalid")
    {
        std::stringstream ss;
        ss << "div { border: (zero: 1, as: 2, index: 3)[0]; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: index 0 is out of range. The allowed range is 1 to 3.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("map[-x or +y] are invalid when out of range")
    {
        for(int idx(4); idx <= 100; ++idx)
        {
            // from the front
            {
                std::stringstream ss;
                ss << "div { border: (large: 1, index: 2, out-of-range: 3)["
                   << idx
                   << "]; }";
                csspp::position pos("test.css");
                csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

                csspp::parser p(l);

                csspp::node::pointer_t n(p.stylesheet());

                csspp::compiler c;
                c.set_root(n);
                c.set_date_time_variables(csspp_test::get_now());
                c.add_path(csspp_test::get_script_path());
                c.add_path(csspp_test::get_version_script_path());

                c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

                std::stringstream errmsg;
                errmsg << "test.css(1): error: index "
                       << idx
                       << " is out of range. The allowed range is 1 to 3.\n";
                VERIFY_ERRORS(errmsg.str());

                CATCH_REQUIRE(c.get_root() == n);
            }

            // from the back
            {
                std::stringstream ss;
                ss << "div { border: (negative: 1, offset: 2, out-of-range-too: 3)[-"
                   << idx
                   << "]; }";
                csspp::position pos("test.css");
                csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

                csspp::parser p(l);

                csspp::node::pointer_t n(p.stylesheet());

                csspp::compiler c;
                c.set_root(n);
                c.set_date_time_variables(csspp_test::get_now());
                c.add_path(csspp_test::get_script_path());
                c.add_path(csspp_test::get_version_script_path());

                c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

                std::stringstream errmsg;
                errmsg << "test.css(1): error: index "
                       << -idx
                       << " is out of range. The allowed range is 1 to 3.\n";
                VERIFY_ERRORS(errmsg.str());

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("map[unknown] is similar to an 'out of range' error")
    {
        std::stringstream ss;
        ss << "div { border: (large: 1, index: 2, out-of-range: 3)['unknown']; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: 'map[\"unknown\"]' is not set.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("map . 123 is not possible")
    {
        std::stringstream ss;
        ss << "div { border: (large: 1, index: 2, out-of-range: 3) . 123; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: only an identifier is expected after a '.'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

// vim: ts=4 sw=4 et
