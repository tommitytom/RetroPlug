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
 * \brief Test the assembler.cpp file.
 *
 * This test runs a battery of tests agains the assembler.cpp file to ensure
 * full coverage and many edge cases of CSS encoding.
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
#include    <fstream>
#include    <iostream>
#include    <sstream>


// C
//
#include    <string.h>
#include    <string.h>
#include    <unistd.h>
#include    <sys/stat.h>


// last include
//
#include    <snapdev/poison.h>



namespace
{

bool is_valid_mode(csspp::output_mode_t const mode)
{
    switch(mode)
    {
    case csspp::output_mode_t::COMPACT:
    case csspp::output_mode_t::COMPRESSED:
    case csspp::output_mode_t::EXPANDED:
    case csspp::output_mode_t::TIDY:
        return true;

    default:
        return false;

    }
}

bool is_valid_char(csspp::wide_char_t c)
{
    switch(c)
    {
    case 0:
    case 0xFFFD:
        return false;

    default:
        if(c >= 0xD800 && c <= 0xDFFF)
        {
            // UTF-16 surrogates are not valid wide characters
            return false;
        }
        return true;

    }
}

} // no name namespace

CATCH_TEST_CASE("Assemble rules", "[assembler]")
{
    CATCH_START_SECTION("with many spaces")
    {
        for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
            i <= static_cast<int>(csspp::output_mode_t::TIDY);
            ++i)
        {
            std::stringstream ss;
            ss << "div { color: black; }"
               << "span { border: 3px solid complement(#f7d0cf); }"
               << "p { font: 13px/135% sans-serif; }"
               << "section { width: calc(30px - 5%); }";
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

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
                CATCH_REQUIRE(out.str() ==
"div { color: #000 }\n"
"span { border: 3px solid #cff6f7 }\n"
"p { font: 13px/135% sans-serif }\n"
"section { width: calc(30px - 5%) }\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::COMPRESSED:
                CATCH_REQUIRE(out.str() ==
"div{color:#000}span{border:3px solid #cff6f7}p{font:13px/135% sans-serif}section{width:calc(30px - 5%)}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::EXPANDED:
                CATCH_REQUIRE(out.str() ==
"div\n"
"{\n"
"  color: #000;\n"
"}\n"
"span\n"
"{\n"
"  border: 3px solid #cff6f7;\n"
"}\n"
"p\n"
"{\n"
"  font: 13px/135% sans-serif;\n"
"}\n"
"section\n"
"{\n"
"  width: calc(30px - 5%);\n"
"}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::TIDY:
                CATCH_REQUIRE(out.str() ==
"div{color:#000}\n"
"span{border:3px solid #cff6f7}\n"
"p{font:13px/135% sans-serif}\n"
"section{width:calc(30px - 5%)}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            }

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("with a calc including a divide after parenthesis")
    {
        for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
            i <= static_cast<int>(csspp::output_mode_t::TIDY);
            ++i)
        {
            std::stringstream ss;
            ss << "#left-margin { width: calc((100% - 300px) / 2); }";
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

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
                CATCH_REQUIRE(out.str() ==
"#left-margin { width: calc( (100% - 300px)  / 2) }\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::COMPRESSED:
                CATCH_REQUIRE(out.str() ==
"#left-margin{width:calc( (100% - 300px) / 2)}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::EXPANDED:
                CATCH_REQUIRE(out.str() ==
"#left-margin\n"
"{\n"
"  width: calc( (100% - 300px)  / 2);\n"
"}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::TIDY:
                CATCH_REQUIRE(out.str() ==
"#left-margin{width:calc( (100% - 300px) / 2)}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            }

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("test multiple declarations in one rule")
    {
        for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
            i <= static_cast<int>(csspp::output_mode_t::TIDY);
            ++i)
        {
            std::stringstream ss;
            ss << "div\n"
               << "{\n"
               << "  color: invert(black);\n"
               << "  font-size: 1.3rem;\n"
               << "}\n"
               << "\n"
               << "span\n"
               << "{\n"
               << "  border: 3px solid darken(#f7d0cf, 3%);\n"
               << "\tborder-bottom-width: 1px;\n"
               << "\tbox-shadow: 1px 0px 7px #88aa11, 0px 3px 1px #aa8833;\n"
               << "  font: 17.2px/1.35em\tArial;\n"
               << "}\n"
               << "\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
                CATCH_REQUIRE(out.str() ==
"div { color: #fff; font-size: 1.3rem }\n"
"span { border: 3px solid #f5c3c2; border-bottom-width: 1px; box-shadow: 1px 0px 7px #8a1, 0px 3px 1px #a83; font: 17.2px/1.35em Arial }\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::COMPRESSED:
                CATCH_REQUIRE(out.str() ==
"div{color:#fff;font-size:1.3rem}span{border:3px solid #f5c3c2;border-bottom-width:1px;box-shadow:1px 0px 7px #8a1,0px 3px 1px #a83;font:17.2px/1.35em Arial}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::EXPANDED:
                CATCH_REQUIRE(out.str() ==
"div\n"
"{\n"
"  color: #fff;\n"
"  font-size: 1.3rem;\n"
"}\n"
"span\n"
"{\n"
"  border: 3px solid #f5c3c2;\n"
"  border-bottom-width: 1px;\n"
"  box-shadow: 1px 0px 7px #8a1, 0px 3px 1px #a83;\n"
"  font: 17.2px/1.35em Arial;\n"
"}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::TIDY:
                CATCH_REQUIRE(out.str() ==
"div{color:#fff;font-size:1.3rem}\n"
"span{border:3px solid #f5c3c2;border-bottom-width:1px;box-shadow:1px 0px 7px #8a1,0px 3px 1px #a83;font:17.2px/1.35em Arial}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            }

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("test multiple selector lists")
    {
        for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
            i <= static_cast<int>(csspp::output_mode_t::TIDY);
            ++i)
        {
            std::stringstream ss;
            ss << "div a b,\n"
               << "p span i\n"
               << "{\n"
               << "  color: mix(black, white);\n"
               << "\t  font-size: 1.3em;\n"
               << " \n"
               << "  border: 3px solid desaturate(#f7d0cf, 5%);\n"
               << "  \tbox-shadow: 1px 0px 7px #88aa11, 0px 3px 1px #aa8833, 1px 2px 3px #92af54;\n"
               << "\tborder-bottom-width: 1px;\n"
               << "}\n"
               << "\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
                CATCH_REQUIRE(out.str() ==
"div a b, p span i { color: gray; font-size: 1.3em; border: 3px solid #f6d1d0; box-shadow: 1px 0px 7px #8a1, 0px 3px 1px #a83, 1px 2px 3px #92af54; border-bottom-width: 1px }\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::COMPRESSED:
                CATCH_REQUIRE(out.str() ==
"div a b,p span i{color:gray;font-size:1.3em;border:3px solid #f6d1d0;box-shadow:1px 0px 7px #8a1,0px 3px 1px #a83,1px 2px 3px #92af54;border-bottom-width:1px}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::EXPANDED:
                CATCH_REQUIRE(out.str() ==
"div a b, p span i\n"
"{\n"
"  color: gray;\n"
"  font-size: 1.3em;\n"
"  border: 3px solid #f6d1d0;\n"
"  box-shadow: 1px 0px 7px #8a1, 0px 3px 1px #a83, 1px 2px 3px #92af54;\n"
"  border-bottom-width: 1px;\n"
"}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::TIDY:
                CATCH_REQUIRE(out.str() ==
"div a b,p span i{color:gray;font-size:1.3em;border:3px solid #f6d1d0;box-shadow:1px 0px 7px #8a1,0px 3px 1px #a83,1px 2px 3px #92af54;border-bottom-width:1px}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            }

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("test set_unit() to various numbers")
    {
        for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
            i <= static_cast<int>(csspp::output_mode_t::TIDY);
            ++i)
        {
            std::stringstream ss;
            ss << "div \n"
               << "{\n"
               << "  width: set_unit(33, px);\n"
               << "  height: set_unit(3.3, em);\n"
               << "  margin-left: set_unit(33px, 'cm');\n"
               << "  margin-right: set_unit(3.3em, mm);\n"
               //<< "  content: unit(123%);\n"
               //<< "  z-index: remove_unit(12.3%);\n"
               << "  margin-top: set_unit(123%, \"px\");\n"
               << "  margin-bottom: set_unit(45, \"%\");\n"
               << "  border-bottom-width: set_unit(4.5, \"%\");\n"
               << "  border-top-width: set_unit(45cm, \"%\");\n"
               << "  border-left-width: set_unit(.45vm, \"%\");\n"
               << "  border-right-width: set_unit(68, \"\");\n"
               << "  background-x: set_unit(68vm, \"\");\n"
               << "  background-y: set_unit(6.8cm, \"\");\n"
               << "  padding-left: set_unit(63.81%, \"\");\n"
               << "}\n"
               << "\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
                CATCH_REQUIRE(out.str() ==
"div { width: 33px; height: 3.3em; margin-left: 33cm; margin-right: 3.3mm; margin-top: 1.23px; margin-bottom: 4500%; border-bottom-width: 450%; border-top-width: 4500%; border-left-width: 45%; border-right-width: 68; background-x: 68; background-y: 6.8; padding-left: .638 }\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::COMPRESSED:
                CATCH_REQUIRE(out.str() ==
"div{width:33px;height:3.3em;margin-left:33cm;margin-right:3.3mm;margin-top:1.23px;margin-bottom:4500%;border-bottom-width:450%;border-top-width:4500%;border-left-width:45%;border-right-width:68;background-x:68;background-y:6.8;padding-left:.638}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::EXPANDED:
                CATCH_REQUIRE(out.str() ==
"div\n"
"{\n"
"  width: 33px;\n"
"  height: 3.3em;\n"
"  margin-left: 33cm;\n"
"  margin-right: 3.3mm;\n"
"  margin-top: 1.23px;\n"
"  margin-bottom: 4500%;\n"
"  border-bottom-width: 450%;\n"
"  border-top-width: 4500%;\n"
"  border-left-width: 45%;\n"
"  border-right-width: 68;\n"
"  background-x: 68;\n"
"  background-y: 6.8;\n"
"  padding-left: .638;\n"
"}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::TIDY:
                CATCH_REQUIRE(out.str() ==
"div{width:33px;height:3.3em;margin-left:33cm;margin-right:3.3mm;margin-top:1.23px;margin-bottom:4500%;border-bottom-width:450%;border-top-width:4500%;border-left-width:45%;border-right-width:68;background-x:68;background-y:6.8;padding-left:.638}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            }

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("test unitless() to various numbers")
    {
        for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
            i <= static_cast<int>(csspp::output_mode_t::TIDY);
            ++i)
        {
            std::stringstream ss;
            ss << "div \n"
               << "{\n"
               << "  width: unitless(33px) ? 15px : 3em;\n"
               << "  height: unitless(3.3em) ? 5cm : 3mm;\n"
               << "  content: unitless(123%) ? 'incorrect' : 'correct';\n"
               << "  border-bottom-width: unitless(4.5) ? 30vm : 45%;\n"
               << "  border-left-width: unitless(45) ? 131px : 4.15cm;\n"
               << "}\n"
               << "\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
                CATCH_REQUIRE(out.str() ==
"div { width: 3em; height: 3mm; content: \"correct\"; border-bottom-width: 30vm; border-left-width: 131px }\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::COMPRESSED:
                CATCH_REQUIRE(out.str() ==
"div{width:3em;height:3mm;content:\"correct\";border-bottom-width:30vm;border-left-width:131px}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::EXPANDED:
                CATCH_REQUIRE(out.str() ==
"div\n"
"{\n"
"  width: 3em;\n"
"  height: 3mm;\n"
"  content: \"correct\";\n"
"  border-bottom-width: 30vm;\n"
"  border-left-width: 131px;\n"
"}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::TIDY:
                CATCH_REQUIRE(out.str() ==
"div{width:3em;height:3mm;content:\"correct\";border-bottom-width:30vm;border-left-width:131px}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            }

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("test unique_id()")
    {
        for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
            i <= static_cast<int>(csspp::output_mode_t::TIDY);
            ++i)
        {
            std::stringstream ss;
            ss << "div\n"
               << "{\n"
               << "  content: string(unique_id());\n"
               << "  border: 3px unique_id() lightsteelblue;\n"
               << "}\n"
               << "a.withColor\n"
               << "{\n"
               << "  padding: unique_id();\n"
               << "}\n"
               << "\n";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

            // reset counter so we can compare with 1, 2, 3 each time
            csspp::expression::set_unique_id_counter(0);

            csspp::compiler c;
            c.set_root(n);
            c.set_date_time_variables(csspp_test::get_now());
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(false);

//std::cerr << "Compiler result is: [" << *n << "]\n";

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
                CATCH_REQUIRE(out.str() ==
"div { content: \"_csspp_unique1\"; border: 3px _csspp_unique2 #b0c4de }\n"
"a.withColor { padding: _csspp_unique3 }\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::COMPRESSED:
                CATCH_REQUIRE(out.str() ==
"div{content:\"_csspp_unique1\";border:3px _csspp_unique2 #b0c4de}a.withColor{padding:_csspp_unique3}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::EXPANDED:
                CATCH_REQUIRE(out.str() ==
"div\n"
"{\n"
"  content: \"_csspp_unique1\";\n"
"  border: 3px _csspp_unique2 #b0c4de;\n"
"}\n"
"a.withColor\n"
"{\n"
"  padding: _csspp_unique3;\n"
"}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::TIDY:
                CATCH_REQUIRE(out.str() ==
"div{content:\"_csspp_unique1\";border:3px _csspp_unique2 #b0c4de}\n"
"a.withColor{padding:_csspp_unique3}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            }

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("identifier with unicode characters and starting with a digit and including a period")
    {
        for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
            i <= static_cast<int>(csspp::output_mode_t::TIDY);
            ++i)
        {
            std::stringstream ss;
            // word "e'te'" (summer in French)
            ss << "\xc3\xa9t\xc3\xa9 { color: fade_in(gold, 0.2); }\n"
               << "\\33 21 { color: fade_out(tan, 0.2); }\n"
               << "\\36 face { color: orchid; }\n"
               << "\\30 BLUR { color: thistle; }\n"
               << "this\\.and\\.that { color: honeydew; }\n"
               << "\xe4\x96\x82 { color: dimgray; }\n"
               << "\xf0\x90\x90\x94\xf0\x90\x90\xa9\xf0\x90\x91\x85\xf0\x90\x90\xaf\xf0\x90\x91\x89\xf0\x90\x90\xaf\xf0\x90\x90\xbb { color: darkred; }\n"
               << "normal { color: mix(darkseagreen, lavender, 0.3); }\n";
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

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
                CATCH_REQUIRE(out.str() ==
"\xc3\xa9t\xc3\xa9 { color: #ffd700 }\n"
"\\33 21 { color: rgba(210,180,140,.8) }\n"
"\\36 face { color: #da70d6 }\n"
"\\30 BLUR { color: #d8bfd8 }\n"
"this\\.and\\.that { color: #f0fff0 }\n"
"\xe4\x96\x82 { color: #696969 }\n"
"\xf0\x90\x90\x94\xf0\x90\x90\xa9\xf0\x90\x91\x85\xf0\x90\x90\xaf\xf0\x90\x91\x89\xf0\x90\x90\xaf\xf0\x90\x90\xbb { color: #8b0000 }\n"
"normal { color: #ccd9da }\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::COMPRESSED:
                CATCH_REQUIRE(out.str() ==
"\xc3\xa9t\xc3\xa9{color:#ffd700}"
"\\33 21{color:rgba(210,180,140,.8)}"
"\\36 face{color:#da70d6}"
"\\30 BLUR{color:#d8bfd8}"
"this\\.and\\.that{color:#f0fff0}"
"\xe4\x96\x82{color:#696969}"
"\xf0\x90\x90\x94\xf0\x90\x90\xa9\xf0\x90\x91\x85\xf0\x90\x90\xaf\xf0\x90\x91\x89\xf0\x90\x90\xaf\xf0\x90\x90\xbb{color:#8b0000}"
"normal{color:#ccd9da}"
"\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::EXPANDED:
                CATCH_REQUIRE(out.str() ==
"\xc3\xa9t\xc3\xa9\n"
"{\n"
"  color: #ffd700;\n"
"}\n"
"\\33 21\n"
"{\n"
"  color: rgba(210,180,140,.8);\n"
"}\n"
"\\36 face\n"
"{\n"
"  color: #da70d6;\n"
"}\n"
"\\30 BLUR\n"
"{\n"
"  color: #d8bfd8;\n"
"}\n"
"this\\.and\\.that\n"
"{\n"
"  color: #f0fff0;\n"
"}\n"
"\xe4\x96\x82\n"
"{\n"
"  color: #696969;\n"
"}\n"
"\xf0\x90\x90\x94\xf0\x90\x90\xa9\xf0\x90\x91\x85\xf0\x90\x90\xaf\xf0\x90\x91\x89\xf0\x90\x90\xaf\xf0\x90\x90\xbb\n"
"{\n"
"  color: #8b0000;\n"
"}\n"
"normal\n"
"{\n"
"  color: #ccd9da;\n"
"}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::TIDY:
                CATCH_REQUIRE(out.str() ==
"\xc3\xa9t\xc3\xa9{color:#ffd700}\n"
"\\33 21{color:rgba(210,180,140,.8)}\n"
"\\36 face{color:#da70d6}\n"
"\\30 BLUR{color:#d8bfd8}\n"
"this\\.and\\.that{color:#f0fff0}\n"
"\xe4\x96\x82{color:#696969}\n"
"\xf0\x90\x90\x94\xf0\x90\x90\xa9\xf0\x90\x91\x85\xf0\x90\x90\xaf\xf0\x90\x91\x89\xf0\x90\x90\xaf\xf0\x90\x90\xbb{color:#8b0000}\n"
"normal{color:#ccd9da}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            }

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("whitespace at the beginning of a rule")
    {
        for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
            i <= static_cast<int>(csspp::output_mode_t::TIDY);
            ++i)
        {
            std::stringstream ss;
            ss << "  // spaces before a comment will produce problems\n"
               << "  // and such comments are not preserved as expected\n"
               << "  // @preserve this comment\n"
               << "\n"
               << ".face { content: quote(\xe4\x96\x82); }\n"
               << ".hair { color: fade_in(rgba(50, 100, 150, 0.14), 0.46); }\n"
               << ".plan { opacity: opacity(fade_in(rgba(50, 100, 150, 0.14), 0.46)); }\n";
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

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            VERIFY_ERRORS("test.css(3): warning: C++ comments should not be preserved as they are not supported by most CSS parsers.\n");

            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
                CATCH_REQUIRE(out.str() ==
"/* @preserve this comment */\n"
".face { content: \"\xe4\x96\x82\" }\n"
".hair { color: rgba(50,100,150,.6) }\n"
".plan { opacity: .6 }\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::COMPRESSED:
                CATCH_REQUIRE(out.str() ==
"/* @preserve this comment */\n"
".face{content:\"\xe4\x96\x82\"}"
".hair{color:rgba(50,100,150,.6)}"
".plan{opacity:.6}"
"\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::EXPANDED:
                CATCH_REQUIRE(out.str() ==
"/* @preserve this comment */\n"
".face\n"
"{\n"
"  content: \"\xe4\x96\x82\";\n"
"}\n"
".hair\n"
"{\n"
"  color: rgba(50,100,150,.6);\n"
"}\n"
".plan\n"
"{\n"
"  opacity: .6;\n"
"}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::TIDY:
                CATCH_REQUIRE(out.str() ==
"/* @preserve this comment */\n"
".face{content:\"\xe4\x96\x82\"}\n"
".hair{color:rgba(50,100,150,.6)}\n"
".plan{opacity:.6}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            }

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("whitespace at the beginning of a variable")
    {
        for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
            i <= static_cast<int>(csspp::output_mode_t::TIDY);
            ++i)
        {
            std::stringstream ss;
            ss << "  // an empty comment was creating trouble!\n"
               << "  //\n"
               << "\n"
               << "$color: #329182;\n"
               << "$image: url(\"images/ladybug.jpeg\");\n"
               << "$width: 300px;\n"
               << "\n"
               << "  p\n"
               << "  {\n"
               << "    // background color defined in variable\n"
               << "    background-color: grayscale($color);\n"
               << "\n"
               << "    // this is like \"body div.flowers\"\n"
               << "    div.flowers\n"
               << "    {\n"
               << "      background-image: $image;\n"
               << "      z-index: remove_unit($width);\n"
               << "    }\n"
               << "  }\n"
               << "\n";
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            csspp::parser p(l);

            csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

            csspp::compiler c;
            c.set_root(n);
            c.set_date_time_variables(csspp_test::get_now());
            c.add_path(csspp_test::get_script_path());
            c.add_path(csspp_test::get_version_script_path());

            c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            VERIFY_ERRORS("");

            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
                CATCH_REQUIRE(out.str() ==
"p { background-color: #626262 }\n"
"p div.flowers { background-image: url( images/ladybug.jpeg ); z-index: 300 }\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::COMPRESSED:
                CATCH_REQUIRE(out.str() ==
"p{background-color:#626262}"
"p div.flowers{background-image:url(images/ladybug.jpeg);z-index:300}"
"\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::EXPANDED:
                CATCH_REQUIRE(out.str() ==
"p\n"
"{\n"
"  background-color: #626262;\n"
"}\n"
"p div.flowers\n"
"{\n"
"  background-image: url( images/ladybug.jpeg );\n"
"  z-index: 300;\n"
"}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            case csspp::output_mode_t::TIDY:
                CATCH_REQUIRE(out.str() ==
"p{background-color:#626262}\n"
"p div.flowers{background-image:url(images/ladybug.jpeg);z-index:300}\n"
+ csspp_test::get_close_comment()
                    );
                break;

            }

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Assemble selectors", "[assembler] [selectors]")
{
    // check various selectors without the operators

    // simple identifiers
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "div span a { color: black; }";
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

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"div span a { color: #000 }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"div span a{color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"div span a\n"
"{\n"
"  color: #000;\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"div span a{color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test a simple attribute
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "div[foo] {color: black}\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"div[foo] { color: #000 }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"div[foo]{color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"div[foo]\n"
"{\n"
"  color: #000;\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"div[foo]{color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // Test with a class
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "div.foo{color:black}\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"div.foo { color: #000 }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"div.foo{color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"div.foo\n"
"{\n"
"  color: #000;\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"div.foo{color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // Test with an identifier
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "#foo div{color:black}\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"#foo div { color: #000 }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"#foo div{color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"#foo div\n"
"{\n"
"  color: #000;\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"#foo div{color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test an attribute with a test
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "div[foo=\"a b c\"] {color: black}\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"div[foo = \"a b c\"] { color: #000 }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"div[foo=\"a b c\"]{color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"div[foo = \"a b c\"]\n"
"{\n"
"  color: #000;\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"div[foo=\"a b c\"]{color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test an :lang() pseudo function
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "div:lang(fr) {color: black}\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"div:lang(fr) { color: #000 }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"div:lang(fr){color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"div:lang(fr)\n"
"{\n"
"  color: #000;\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"div:lang(fr){color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test an :not() pseudo function
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "div:not(:lang(fr)):not(:nth-child(2n+1)) {color: black}\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"div:not(:lang(fr)):not(:nth-child(odd)) { color: #000 }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"div:not(:lang(fr)):not(:nth-child(odd)){color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"div:not(:lang(fr)):not(:nth-child(odd))\n"
"{\n"
"  color: #000;\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"div:not(:lang(fr)):not(:nth-child(odd)){color:#000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // test the pseudo classes
    char const * pseudo_classes[] =
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
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        for(size_t j(0); j < sizeof(pseudo_classes) / sizeof(pseudo_classes[0]); ++j)
        {
            std::stringstream ss;
            ss << "div:" << pseudo_classes[j] << " {color: black}\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            std::stringstream expected;
            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
expected << "div:" << pseudo_classes[j] << " { color: #000 }\n"
         << csspp_test::get_close_comment();
                break;

            case csspp::output_mode_t::COMPRESSED:
expected << "div:" << pseudo_classes[j] << "{color:#000}\n"
         << csspp_test::get_close_comment();
                break;

            case csspp::output_mode_t::EXPANDED:
expected << "div:" << pseudo_classes[j] << "\n"
            "{\n"
            "  color: #000;\n"
            "}\n"
         << csspp_test::get_close_comment();
                break;

            case csspp::output_mode_t::TIDY:
expected << "div:" << pseudo_classes[j] << "{color:#000}\n"
         << csspp_test::get_close_comment();
                break;

            }

            CATCH_REQUIRE(out.str() == expected.str());
            CATCH_REQUIRE(c.get_root() == n);
        }
    }

    // test the pseudo classes
    char const * pseudo_elements[] =
    {
        "first-line",
        "first-letter",
        "before",
        "after"
    };
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        for(size_t j(0); j < sizeof(pseudo_elements) / sizeof(pseudo_elements[0]); ++j)
        {
            std::stringstream ss;
            ss << "div::" << pseudo_elements[j] << " {color: black}\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            std::stringstream expected;
            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
expected << "div::" << pseudo_elements[j] << " { color: #000 }\n"
         << csspp_test::get_close_comment();
                break;

            case csspp::output_mode_t::COMPRESSED:
expected << "div::" << pseudo_elements[j] << "{color:#000}\n"
         << csspp_test::get_close_comment();
                break;

            case csspp::output_mode_t::EXPANDED:
expected << "div::" << pseudo_elements[j] << "\n"
            "{\n"
            "  color: #000;\n"
            "}\n"
         << csspp_test::get_close_comment();
                break;

            case csspp::output_mode_t::TIDY:
expected << "div::" << pseudo_elements[j] << "{color:#000}\n"
         << csspp_test::get_close_comment();
                break;

            }

            CATCH_REQUIRE(out.str() == expected.str());
            CATCH_REQUIRE(c.get_root() == n);
        }
    }

    // test the An+B pseudo classes
    char const * pseudo_functions[] =
    {
        "nth-child",
        "nth-last-child",
        "nth-of-type",
        "nth-last-of-type"
    };
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        for(size_t j(0); j < sizeof(pseudo_functions) / sizeof(pseudo_functions[0]); ++j)
        {
            std::stringstream ss;
            ss << "div:" << pseudo_functions[j] << "(5n+2) {color: black}\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            std::stringstream expected;
            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
expected << "div:" << pseudo_functions[j] << "(5n+2) { color: #000 }\n"
         << csspp_test::get_close_comment();
                break;

            case csspp::output_mode_t::COMPRESSED:
expected << "div:" << pseudo_functions[j] << "(5n+2){color:#000}\n"
         << csspp_test::get_close_comment();
                break;

            case csspp::output_mode_t::EXPANDED:
expected << "div:" << pseudo_functions[j] << "(5n+2)\n"
            "{\n"
            "  color: #000;\n"
            "}\n"
         << csspp_test::get_close_comment();
                break;

            case csspp::output_mode_t::TIDY:
expected << "div:" << pseudo_functions[j] << "(5n+2){color:#000}\n"
         << csspp_test::get_close_comment();
                break;

            }

            CATCH_REQUIRE(out.str() == expected.str());
            CATCH_REQUIRE(c.get_root() == n);
        }
    }

    // test the scope operator
    char const * scope[] =
    {
        "*|div",
        "*|*",
        "div|*",
        "|div",
        "|*"
    };
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        for(size_t j(0); j < sizeof(scope) / sizeof(scope[0]); ++j)
        {
            std::stringstream ss;
            ss << "with " << scope[j] << " scope {color: black}\n";
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

//std::cerr << "Compiler result is: [" << *n << "]\n";

            std::stringstream out;
            csspp::assembler a(out);
            a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            std::stringstream expected;
            switch(static_cast<csspp::output_mode_t>(i))
            {
            case csspp::output_mode_t::COMPACT:
expected << "with " << scope[j] << " scope { color: #000 }\n"
         << csspp_test::get_close_comment();
                break;

            case csspp::output_mode_t::COMPRESSED:
expected << "with " << scope[j] << " scope{color:#000}\n"
         << csspp_test::get_close_comment();
                break;

            case csspp::output_mode_t::EXPANDED:
expected << "with " << scope[j] << " scope\n"
            "{\n"
            "  color: #000;\n"
            "}\n"
         << csspp_test::get_close_comment();
                break;

            case csspp::output_mode_t::TIDY:
expected << "with " << scope[j] << " scope{color:#000}\n"
         << csspp_test::get_close_comment();
                break;

            }

            CATCH_REQUIRE(out.str() == expected.str());
            CATCH_REQUIRE(c.get_root() == n);
        }
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Assemble numbers", "[assembler] [numbers]")
{
    // create strings with more single quotes (')
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::integer_t integer(rand() % 10000);
        csspp::decimal_number_t decimal_number(static_cast<csspp::decimal_number_t>(rand() % 10000) / 100.0);
        csspp::decimal_number_t percent(static_cast<csspp::decimal_number_t>(rand() % 10000) / 100.0);

        ss << "#wrapper div * span a:hover {\n"
           << "  width: " << integer << ";\n"
           << "  height: " << decimal_number << ";\n"
           << "  font-size: " << percent << "%;\n"
           << "}\n";

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        std::stringstream percent_stream;
        percent_stream << percent;
        std::string percent_str(percent_stream.str());
        if(percent_str.length() > 1
        && percent_str[0] == '0'
        && percent_str[1] == '.')
        {
            percent_str = percent_str.substr(1);
        }

        std::stringstream decimal_number_stream;
        decimal_number_stream << decimal_number;
        std::string decimal_number_str(decimal_number_stream.str());
        if(decimal_number_str.length() > 1
        && decimal_number_str[0] == '0'
        && decimal_number_str[1] == '.')
        {
            decimal_number_str = decimal_number_str.substr(1);
        }

        std::stringstream expected;
        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
expected << "#wrapper div * span a:hover {"
         << " width: " << integer << ";"
         << " height: " << decimal_number_str << ";"
         << " font-size: " << percent_str << "%"
         << " }\n"
         << csspp_test::get_close_comment();
            break;

        case csspp::output_mode_t::COMPRESSED:
expected << "#wrapper div * span a:hover{"
         << "width:" << integer << ";"
         << "height:" << decimal_number_str << ";"
         << "font-size:" << percent_str << "%"
         << "}\n"
         << csspp_test::get_close_comment();
            break;

        case csspp::output_mode_t::EXPANDED:
expected << "#wrapper div * span a:hover\n"
         << "{\n"
         << "  width: " << integer << ";\n"
         << "  height: " << decimal_number_str << ";\n"
         << "  font-size: " << percent_str << "%;\n"
         << "}\n"
         << csspp_test::get_close_comment();
            break;

        case csspp::output_mode_t::TIDY:
expected << "#wrapper div * span a:hover{"
         << "width:" << integer << ";"
         << "height:" << decimal_number_str << ";"
         << "font-size:" << percent_str << "%"
         << "}\n"
         << csspp_test::get_close_comment();
            break;

        }

        CATCH_REQUIRE(out.str() == expected.str());
        CATCH_REQUIRE(c.get_root() == n);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Assemble unicode range", "[assembler] [unicode-range-value] [at-keyword]")
{
    // a valid @supports
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "@font-face\n"
           << "{\n"
           << "  unicode-range: U+400-4fF;\n"
           << "  font-style: italic;\n"
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
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("");

//std::cerr << "Compiler result is: [" << *n << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"@font-face \n"
"{\n"
"unicode-range: U+4??; "
"font-style: italic"
"}\n"
"\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"@font-face {unicode-range:U+4??;font-style:italic}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"@font-face \n"
"{\n"
"  unicode-range: U+4??;\n"
"  font-style: italic}\n"
"\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"@font-face \n"
"{\n"
"unicode-range:U+4??;font-style:italic}\n"
"\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no left over?
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Assemble strings", "[assembler] [strings]")
{
    // create strings with more single quotes (')
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        std::string str;
        int const size(rand() % 20 + 1);
        int dq(rand() % 5 + 1);
        int sq(rand() % 8 + dq); // if sq >= dq use " for strings
        for(int j(0); j < size; ++j)
        {
            if(dq > 0 && rand() % 1 == 0)
            {
                --dq;
                str += '\\';
                str += '"';
            }
            if(sq > 0 && rand() % 1 == 0)
            {
                --sq;
                str += '\'';
            }
            str += static_cast<char>(rand() % 26 + 'a');
        }
        while(dq + sq > 0)
        {
            if(dq > 0 && rand() % 1 == 0)
            {
                --dq;
                str += '\\';
                str += '"';
            }
            if(sq > 0 && rand() % 1 == 0)
            {
                --sq;
                str += '\'';
            }
        }
        ss << "div::before { content: \""
           << str
           << "\" }";

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"div::before { content: \"" + str + "\" }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"div::before{content:\"" + str + "\"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"div::before\n"
"{\n"
"  content: \"" + str + "\";\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"div::before{content:\"" + str + "\"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // create strings with more double quotes (")
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        std::string str;
        int const size(rand() % 20 + 1);
        int sq(rand() % 5 + 1);
        int dq(rand() % 8 + 1 + sq);  // we need dq > sq
        for(int j(0); j < size; ++j)
        {
            if(dq > 0 && rand() % 1 == 0)
            {
                --dq;
                str += '"';
            }
            if(sq > 0 && rand() % 1 == 0)
            {
                --sq;
                str += '\\';
                str += '\'';
            }
            str += static_cast<char>(rand() % 26 + 'a');
        }
        while(dq + sq > 0)
        {
            if(dq > 0 && rand() % 1 == 0)
            {
                --dq;
                str += '"';
            }
            if(sq > 0 && rand() % 1 == 0)
            {
                --sq;
                str += '\\';
                str += '\'';
            }
        }
        ss << "div::after { content: '"
           << str
           << "' }";

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"div::after { content: '" + str + "' }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"div::after{content:'" + str + "'}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"div::after\n"
"{\n"
"  content: '" + str + "';\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"div::after{content:'" + str + "'}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Assemble URI", "[assembler] [uri]")
{
    // all characters can be inserted as is (no switching to string)
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        std::string name;
        int const size(rand() % 20 + 1);
        for(int j(0); j < size; ++j)
        {
            csspp::wide_char_t c(rand() % 0x110000);
            while(!is_valid_char(c)
               || c == '\''
               || c == '"'
               || c == '('
               || c == ')'
               || l->is_non_printable(c))
            {
                c = rand() % 0x110000;
            }
            name += l->wctomb(c);
        }
        ss << "div { background-image: url(/images/"
           << name
           << ".png); }";

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"div { background-image: url( /images/" + name + ".png ) }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"div{background-image:url(/images/" + name + ".png)}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"div\n"
"{\n"
"  background-image: url( /images/" + name + ".png );\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"div{background-image:url(/images/" + name + ".png)}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // at least one character requires the use of a string
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        std::string name;
        csspp::wide_char_t special(L'\0');
        int const size(rand() % 20 + 1);
        for(int j(0); j < size; ++j)
        {
            csspp::wide_char_t c;
            if(j == size / 2)
            {
                // this happens only once and is mandatory since 'size > 0'
                // is always true
                c = special = "'\"()"[rand() % 4];
            }
            else
            {
                c = rand() % 0x110000;
                while(!is_valid_char(c)
                   || c == '\''
                   || c == '"'
                   || c == '('
                   || c == ')'
                   || l->is_non_printable(c))
                {
                    c = rand() % 0x110000;
                }
            }
            name += l->wctomb(c);
        }
        std::string quote;
        quote += special == '"' ? '\'' : '"';
        ss << "div { background-image: url("
           << quote
           << "/images/"
           << name
           << ".png"
           << quote
           << "); }";

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"div { background-image: url( " + quote + "/images/" + name + ".png" + quote + " ) }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"div{background-image:url(" + quote + "/images/" + name + ".png" + quote + ")}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"div\n"
"{\n"
"  background-image: url( " + quote + "/images/" + name + ".png" + quote + " );\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"div{background-image:url(" + quote + "/images/" + name + ".png" + quote + ")}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Assemble C++ comment", "[assembler] [comment]")
{
    // One line C++ comment
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "// Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved. -- Assembler Test Version {$_csspp_version} -- @preserve\n"
           << "body.error { color: red }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        VERIFY_ERRORS("test.css(1): warning: C++ comments should not be preserved as they are not supported by most CSS parsers.\n");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"/* Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved. -- Assembler Test Version " CSSPP_VERSION " -- @preserve */\n"
"body.error { color: red }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"/* Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved. -- Assembler Test Version " CSSPP_VERSION " -- @preserve */\n"
"body.error{color:red}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"/* Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved. -- Assembler Test Version " CSSPP_VERSION " -- @preserve */\n"
"body.error\n"
"{\n"
"  color: red;\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"/* Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved. -- Assembler Test Version " CSSPP_VERSION " -- @preserve */\n"
"body.error{color:red}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // Multi-line C++ comment
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "// Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved.\n"
           << "// Assembler Test\n"
           << "// @preserve\n"
           << "body.error { color: red }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        VERIFY_ERRORS("test.css(1): warning: C++ comments should not be preserved as they are not supported by most CSS parsers.\n");

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"/* Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved.\n"
" * Assembler Test\n"
" * @preserve\n"
" */\n"
"body.error { color: red }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"/* Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved.\n"
" * Assembler Test\n"
" * @preserve\n"
" */\n"
"body.error{color:red}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"/* Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved.\n"
" * Assembler Test\n"
" * @preserve\n"
" */\n"
"body.error\n"
"{\n"
"  color: red;\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"/* Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved.\n"
" * Assembler Test\n"
" * @preserve\n"
" */\n"
"body.error{color:red}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Assemble @-keyword", "[assembler] [at-keyword]")
{
    // Standard @document with a sub-rule
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "@document url(http://www.example.com/), regexp(\"https://.*\")\n"
           << "{\n"
           << "  body { width: 8.5in; height: 9in; }\n"
           << "  div { border: .25in unquote(\"solid\") lightgray }\n"
           << "}\n"
           << "#edge { border: 1px unquote('solid') black }\n";
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

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"@document url( http://www.example.com/ ), regexp(\"https://.*\")\n"
"{\n"
"body { width: 8.5in; height: 9in }\n"
"div { border: .25in solid #d3d3d3 }\n"
"}\n"
"\n"
"#edge { border: 1px solid #000 }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"@document url(http://www.example.com/),regexp(\"https://.*\"){body{width:8.5in;height:9in}div{border:.25in solid #d3d3d3}}#edge{border:1px solid #000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"@document url( http://www.example.com/ ), regexp(\"https://.*\")\n"
"{\n"
"body\n"
"{\n"
"  width: 8.5in;\n"
"  height: 9in;\n"
"}\n"
"div\n"
"{\n"
"  border: .25in solid #d3d3d3;\n"
"}\n"
"}\n"
"\n"
"#edge\n"
"{\n"
"  border: 1px solid #000;\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"@document url(http://www.example.com/),regexp(\"https://.*\")\n"
"{\n"
"body{width:8.5in;height:9in}\n"
"div{border:.25in solid #d3d3d3}\n"
"}\n"
"\n"
"#edge{border:1px solid #000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // Standard @media with a sub-rule
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "@media screen or (printer and color) { body { width: 8.5in; height: 9in; } }\n"
           << "#edge { border: 1px solid black }\n";
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

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"@media screen or (printer and color) \n"
"{\n"
"body { width: 8.5in; height: 9in }\n"
"}\n"
"\n"
"#edge { border: 1px solid #000 }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"@media screen or (printer and color){body{width:8.5in;height:9in}}#edge{border:1px solid #000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"@media screen or (printer and color) \n"
"{\n"
"body\n"
"{\n"
"  width: 8.5in;\n"
"  height: 9in;\n"
"}\n"
"}\n"
"\n"
"#edge\n"
"{\n"
"  border: 1px solid #000;\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"@media screen or (printer and color)\n"
"{\n"
"body{width:8.5in;height:9in}\n"
"}\n"
"\n"
"#edge{border:1px solid #000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @media with many parenthesis and multiple sub-rules
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "@media not (screen or ((laser or matrix or jet-printer) and color)) {\n"
           << "  body { width: 8.5in; height: 9in; }\n"
           << "  div { margin: .15in; padding: .07in; }\n"
           << "  p { margin-bottom: 2em; }\n"
           << "}\n"
           << "#edge { border: 1px solid black }\n";
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

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"@media not (screen or ((laser or matrix or jet-printer) and color)) \n"
"{\n"
"body { width: 8.5in; height: 9in }\n"
"div { margin: .15in; padding: .07in }\n"
"p { margin-bottom: 2em }\n"
"}\n"
"\n"
"#edge { border: 1px solid #000 }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"@media not (screen or ((laser or matrix or jet-printer) and color)){body{width:8.5in;height:9in}div{margin:.15in;padding:.07in}p{margin-bottom:2em}}#edge{border:1px solid #000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"@media not (screen or ((laser or matrix or jet-printer) and color)) \n"
"{\n"
"body\n"
"{\n"
"  width: 8.5in;\n"
"  height: 9in;\n"
"}\n"
"div\n"
"{\n"
"  margin: .15in;\n"
"  padding: .07in;\n"
"}\n"
"p\n"
"{\n"
"  margin-bottom: 2em;\n"
"}\n"
"}\n"
"\n"
"#edge\n"
"{\n"
"  border: 1px solid #000;\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"@media not (screen or ((laser or matrix or jet-printer) and color))\n"
"{\n"
"body{width:8.5in;height:9in}\n"
"div{margin:.15in;padding:.07in}\n"
"p{margin-bottom:2em}\n"
"}\n"
"\n"
"#edge{border:1px solid #000}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // simple @import to see the ';' at the end of the line
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "@import url(//css.m2osw.com/store/colors.css) only screen or (printer and color);\n";
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

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"@import url( //css.m2osw.com/store/colors.css ) only screen or (printer and color) ;\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"@import url(//css.m2osw.com/store/colors.css) only screen or (printer and color);\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"@import url( //css.m2osw.com/store/colors.css ) only screen or (printer and color) ;\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"@import url(//css.m2osw.com/store/colors.css) only screen or (printer and color);\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // @keyframes
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "@keyframes name {\n"
           << "  from {\n"
           << "    left: 0;\n"
           << "  }\n"
           << "  33% {\n"
           << "    left: 5px;\n"
           << "  }\n"
           << "  67% {\n"
           << "    left: 45px;\n"
           << "  }\n"
           << "  to {\n"
           << "    left: 50px;\n"
           << "  }\n"
           << "};\n";
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

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"@keyframes name {\n"
"0% { left: 0 }\n"
"33% { left: 5px }\n"
"67% { left: 45px }\n"
"to { left: 50px }\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"@keyframes name{0%{left:0}33%{left:5px}67%{left:45px}to{left:50px}}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"@keyframes name {\n"
"0%\n"
"{\n"
"  left: 0;\n"
"}\n"
"33%\n"
"{\n"
"  left: 5px;\n"
"}\n"
"67%\n"
"{\n"
"  left: 45px;\n"
"}\n"
"to\n"
"{\n"
"  left: 50px;\n"
"}\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"@keyframes name{\n"
"0%{left:0}\n"
"33%{left:5px}\n"
"67%{left:45px}\n"
"to{left:50px}\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Assemble functions", "[assembler] [function]")
{
    // Test with gradient() function
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "$box($color, $width, $height): { border: 1px * 3 solid $color; width: $width * 1.5; height: $height };\n"
           << "a ~ b { -csspp-null: $box(#39458A, 300px, 200px); }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        std::stringstream expected;
        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
expected << "a ~ b { border: 3px solid #39458a; width: 450px; height: 200px }\n";
            break;

        case csspp::output_mode_t::COMPRESSED:
expected << "a~b{border:3px solid #39458a;width:450px;height:200px}\n";
            break;

        case csspp::output_mode_t::EXPANDED:
expected << "a ~ b\n"
 << "{\n"
 << "  border: 3px solid #39458a;\n"
 << "  width: 450px;\n"
 << "  height: 200px;\n"
 << "}\n";
            break;

        case csspp::output_mode_t::TIDY:
expected << "a~b{border:3px solid #39458a;width:450px;height:200px}\n";
            break;

        }
        expected << csspp_test::get_close_comment();
        CATCH_REQUIRE(out.str() == expected.str());

        CATCH_REQUIRE(c.get_root() == n);
    }

    // CSS Function which is an internal CSS Preprocess function
    // (meaning that it gets interpreted and replaced by a value)
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "a b { color: rgba(1 * 7, 2, 3, .5); }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        std::stringstream expected;
        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
expected << "a b { color: rgba(7,2,3,.5) }\n";  // TODO: add support for spaces in the color::to_string() function?
            break;

        case csspp::output_mode_t::COMPRESSED:
expected << "a b{color:rgba(7,2,3,.5)}\n";
            break;

        case csspp::output_mode_t::EXPANDED:
expected << "a b\n"
         << "{\n"
         << "  color: rgba(7,2,3,.5);\n"  // TODO: add support for spaces in the color::to_string() function?
         << "}\n";
            break;

        case csspp::output_mode_t::TIDY:
expected << "a b{color:rgba(7,2,3,.5)}\n";
            break;

        }
        expected << csspp_test::get_close_comment();
        CATCH_REQUIRE(out.str() == expected.str());

        CATCH_REQUIRE(c.get_root() == n);
    }

    // CSS Function which is not replaced by CSS Proprocessor
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "a b { transform: translate(percentage(-.50px), 0); }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        std::stringstream expected;
        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
expected << "a b { transform: translate(-50%, 0) }\n";
            break;

        case csspp::output_mode_t::COMPRESSED:
expected << "a b{transform:translate(-50%,0)}\n";
            break;

        case csspp::output_mode_t::EXPANDED:
expected << "a b\n"
         << "{\n"
         << "  transform: translate(-50%, 0);\n"
         << "}\n";
            break;

        case csspp::output_mode_t::TIDY:
expected << "a b{transform:translate(-50%,0)}\n";
            break;

        }
        expected << csspp_test::get_close_comment();
        CATCH_REQUIRE(out.str() == expected.str());

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Assemble placeholder", "[assembler] [placeholder]")
{
    // Test with gradient() function
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "$reddish: #e09756;\n"
           << "#context a%extreme { color: blue; font-weight: bold; font-size: 2em }\n"
           << ".error { color: $reddish }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        std::stringstream expected;
        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
expected << ".error { color: #e09756 }\n";
            break;

        case csspp::output_mode_t::COMPRESSED:
expected << ".error{color:#e09756}\n";
            break;

        case csspp::output_mode_t::EXPANDED:
expected << ".error\n"
 << "{\n"
 << "  color: #e09756;\n"
 << "}\n";
            break;

        case csspp::output_mode_t::TIDY:
expected << ".error{color:#e09756}\n";
            break;

        }
        expected << csspp_test::get_close_comment();
        CATCH_REQUIRE(out.str() == expected.str());

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Assemble operators", "[assembler] [operators]")
{
    // Selector unary operator
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "a * b { color: red; }\n";
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

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        std::stringstream expected;
        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
expected << "a * b { color: red }\n";
            break;

        case csspp::output_mode_t::COMPRESSED:
expected << "a * b{color:red}\n";
            break;

        case csspp::output_mode_t::EXPANDED:
expected << "a * b\n"
 << "{\n"
 << "  color: red;\n"
 << "}\n";
            break;

        case csspp::output_mode_t::TIDY:
expected << "a * b{color:red}\n";
            break;

        }
        expected << csspp_test::get_close_comment();
        CATCH_REQUIRE(out.str() == expected.str());

        CATCH_REQUIRE(c.get_root() == n);
    }

    // Selector binary operators
    {
        char const * selector_operator[] =
        {
            "+",
            "~",
            ">"
        };

        for(size_t op(0); op < sizeof(selector_operator) / sizeof(selector_operator[0]); ++op)
        {
            for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
                i <= static_cast<int>(csspp::output_mode_t::TIDY);
                ++i)
            {
                std::stringstream ss;
                ss << "a"
                   << ((rand() % 2) == 0 ? " " : "")
                   << selector_operator[op]
                   << ((rand() % 2) == 0 ? " " : "")
                   << "b { color: red; }\n";
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

                std::stringstream out;
                csspp::assembler a(out);
                a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                std::stringstream expected;
                switch(static_cast<csspp::output_mode_t>(i))
                {
                case csspp::output_mode_t::COMPACT:
expected << "a " << selector_operator[op] << " b { color: red }\n";
                    break;

                case csspp::output_mode_t::COMPRESSED:
expected << "a" << selector_operator[op] << "b{color:red}\n";
                    break;

                case csspp::output_mode_t::EXPANDED:
expected << "a " << selector_operator[op] << " b\n"
         << "{\n"
         << "  color: red;\n"
         << "}\n";
                    break;

                case csspp::output_mode_t::TIDY:
expected << "a" << selector_operator[op] << "b{color:red}\n";
                    break;

                }
                expected << csspp_test::get_close_comment();
                CATCH_REQUIRE(out.str() == expected.str());

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }

    // Attributes binary operators
    {
        char const * attribute_operator[] =
        {
            "=",
            "~=",
            "^=",
            "$=",
            "*=",
            "|="
        };

        for(size_t op(0); op < sizeof(attribute_operator) / sizeof(attribute_operator[0]); ++op)
        {
            for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
                i <= static_cast<int>(csspp::output_mode_t::TIDY);
                ++i)
            {
                std::stringstream ss;
                ss << "a["
                   << ((rand() % 2) != 0 ? " " : "")
                   << "b"
                   << ((rand() % 2) != 0 ? " " : "")
                   << attribute_operator[op]
                   << ((rand() % 2) != 0 ? " " : "")
                   << "3"
                   << ((rand() % 2) != 0 ? " " : "")
                   << "]"
                   << ((rand() % 2) != 0 ? "\n" : "")
                   << "{"
                   << ((rand() % 2) != 0 ? " " : "")
                   << "color"
                   << ((rand() % 2) != 0 ? " " : "")
                   << ":"
                   << ((rand() % 2) != 0 ? " " : "")
                   << "red"
                   << ((rand() % 2) != 0 ? " " : "")
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

                std::stringstream out;
                csspp::assembler a(out);
                a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                std::stringstream expected;
                switch(static_cast<csspp::output_mode_t>(i))
                {
                case csspp::output_mode_t::COMPACT:
expected << "a[b " << attribute_operator[op] << " 3] { color: red }\n";
                    break;

                case csspp::output_mode_t::COMPRESSED:
expected << "a[b" << attribute_operator[op] << "3]{color:red}\n";
                    break;

                case csspp::output_mode_t::EXPANDED:
expected << "a[b " << attribute_operator[op] << " 3]\n"
        "{\n"
        "  color: red;\n"
        "}\n";
                    break;

                case csspp::output_mode_t::TIDY:
expected << "a[b" << attribute_operator[op] << "3]{color:red}\n";
                    break;

                }
                expected << csspp_test::get_close_comment();
                CATCH_REQUIRE(out.str() == expected.str());

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }

    // '!' -- EXCLAMATION
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "*[b = 3] { color : red !"
           << ((rand() % 2) == 0 ? " " : "")
           << "important; }";
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

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(out.str() ==
"[b = 3] { color: red !important }\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(out.str() ==
"[b=3]{color:red!important}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(out.str() ==
"[b = 3]\n"
"{\n"
"  color: red !important;\n"
"}\n"
+ csspp_test::get_close_comment()
                );
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(out.str() ==
"[b=3]{color:red!important}\n"
+ csspp_test::get_close_comment()
                );
            break;

        }

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Assembler modes", "[assembler] [mode]")
{
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << static_cast<csspp::output_mode_t>(i);

        switch(static_cast<csspp::output_mode_t>(i))
        {
        case csspp::output_mode_t::COMPACT:
            CATCH_REQUIRE(ss.str() == "COMPACT");
            break;

        case csspp::output_mode_t::COMPRESSED:
            CATCH_REQUIRE(ss.str() == "COMPRESSED");
            break;

        case csspp::output_mode_t::EXPANDED:
            CATCH_REQUIRE(ss.str() == "EXPANDED");
            break;

        case csspp::output_mode_t::TIDY:
            CATCH_REQUIRE(ss.str() == "TIDY");
            break;

        }
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid assembler mode", "[assembler] [mode] [invalid]")
{
    // with many spaces
    for(int i(0); i < 100; ++i)
    {
        std::stringstream out;
        csspp::assembler a(out);
        csspp::position pos("test.css");
        csspp::node::pointer_t n(new csspp::node(csspp::node_type_t::LIST, pos));
        csspp::output_mode_t mode(static_cast<csspp::output_mode_t>(rand()));
        while(is_valid_mode(mode))
        {
            mode = static_cast<csspp::output_mode_t>(rand());
        }
        CATCH_REQUIRE_THROWS_AS(a.output(n, mode), csspp::csspp_exception_logic);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Inacceptable nodes", "[assembler] [invalid]")
{
    // list of "invalid" nodes in the assembler
    csspp::node_type_t node_types[] =
    {
        csspp::node_type_t::UNKNOWN,
        csspp::node_type_t::AND,
        csspp::node_type_t::ASSIGNMENT,
        csspp::node_type_t::BOOLEAN,
        csspp::node_type_t::CDC,
        csspp::node_type_t::CDO,
        csspp::node_type_t::CLOSE_CURLYBRACKET,
        csspp::node_type_t::CLOSE_PARENTHESIS,
        csspp::node_type_t::CLOSE_SQUAREBRACKET,
        csspp::node_type_t::COLUMN,
        csspp::node_type_t::COMMA,
        csspp::node_type_t::CONDITIONAL,
        csspp::node_type_t::DOLLAR,
        csspp::node_type_t::EOF_TOKEN,
        csspp::node_type_t::EXCLAMATION,
        csspp::node_type_t::GREATER_EQUAL,
        csspp::node_type_t::LESS_EQUAL,
        csspp::node_type_t::LESS_THAN,
        csspp::node_type_t::MODULO,
        csspp::node_type_t::NOT_EQUAL,
        csspp::node_type_t::NULL_TOKEN,
        csspp::node_type_t::PLACEHOLDER,
        csspp::node_type_t::POWER,
        csspp::node_type_t::REFERENCE,
        csspp::node_type_t::SEMICOLON,
        csspp::node_type_t::VARIABLE,
        csspp::node_type_t::VARIABLE_FUNCTION,
        csspp::node_type_t::max_type
    };

    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        for(size_t j(0); j < sizeof(node_types) / sizeof(node_types[0]); ++j)
        {
            csspp::position pos("test.css");
            csspp::node::pointer_t root(new csspp::node(node_types[j], pos));

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

            std::stringstream out;
            csspp::assembler a(out);
            CATCH_REQUIRE_THROWS_AS(a.output(root, static_cast<csspp::output_mode_t>(i)), csspp::csspp_exception_logic);
        }
    }
}

CATCH_TEST_CASE("CSS incompatible dimensions", "[assembler] [invalid] [dimension]")
{
    for(int i(static_cast<int>(csspp::output_mode_t::COMPACT));
        i <= static_cast<int>(csspp::output_mode_t::TIDY);
        ++i)
    {
        std::stringstream ss;
        ss << "span { border: 3px * 5px solid #f7d0cf; }";
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

        // no errors yet
        VERIFY_ERRORS("");

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, static_cast<csspp::output_mode_t>(i));

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        VERIFY_ERRORS("test.css(1): error: \"px * px\" is not a valid CSS dimension.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no error left over
    VERIFY_ERRORS("");
}

// vim: ts=4 sw=4 et
