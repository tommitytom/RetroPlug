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
 * \brief Test the parser.cpp file.
 *
 * This test runs a battery of tests agains the parser.cpp file to ensure
 * full coverage and many edge cases as expected by CSS 3.
 *
 * Note that the basic grammar that the parser implements is compatible
 * with CSS 1 and 2.1.
 *
 * Remember that the parser does not do any verification other than the
 * ability to parse the input data. So whether the rules are any good
 * is not known at the time the parser returns.
 */

// csspp
//
#include    <csspp/exception.h>
#include    <csspp/parser.h>


// self
//
#include    "catch_main.h"


// C++
//
#include    <sstream>


// C
//
#include    <string.h>


// last include
//
#include    <snapdev/poison.h>



namespace
{

} // no name namespace




CATCH_TEST_CASE("Simple stylesheets", "[parser] [stylesheet] [rules]")
{
    {
        std::stringstream ss;
        ss << "<!-- body { background : white url( /images/background.png ) } -->";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"body\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"background\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        WHITESPACE\n"
"        IDENTIFIER \"white\"\n"
"        WHITESPACE\n"
"        URL \"/images/background.png\"\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }

    {
        std::stringstream ss;
        ss << "<!-- body { background : white url( /images/background.png ) } --><!-- div { border: 1px; } -->";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"body\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"background\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        WHITESPACE\n"
"        IDENTIFIER \"white\"\n"
"        WHITESPACE\n"
"        URL \"/images/background.png\"\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"border\"\n"
"        COLON\n"
"        WHITESPACE\n"
"        INTEGER \"px\" I:1\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }

    // one large rule with semicolons inside
    {
        std::stringstream ss;
        ss << "div\n"
           << "{\n"
           << "    background-color: rgba(33, 77, 99, 0.3);\n"
           << "    color: rgba(0, 3, 5, 0.95);\n"
           << "    font-style: italic;\n"
           << "}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      LIST\n"
"        COMPONENT_VALUE\n"
"          IDENTIFIER \"background-color\"\n"
"          COLON\n"
"          WHITESPACE\n"
"          FUNCTION \"rgba\"\n"
"            INTEGER \"\" I:33\n"
"            COMMA\n"
"            WHITESPACE\n"
"            INTEGER \"\" I:77\n"
"            COMMA\n"
"            WHITESPACE\n"
"            INTEGER \"\" I:99\n"
"            COMMA\n"
"            WHITESPACE\n"
"            DECIMAL_NUMBER \"\" D:0.3\n"
"        COMPONENT_VALUE\n"
"          IDENTIFIER \"color\"\n"
"          COLON\n"
"          WHITESPACE\n"
"          FUNCTION \"rgba\"\n"
"            INTEGER \"\" I:0\n"
"            COMMA\n"
"            WHITESPACE\n"
"            INTEGER \"\" I:3\n"
"            COMMA\n"
"            WHITESPACE\n"
"            INTEGER \"\" I:5\n"
"            COMMA\n"
"            WHITESPACE\n"
"            DECIMAL_NUMBER \"\" D:0.95\n"
"        COMPONENT_VALUE\n"
"          IDENTIFIER \"font-style\"\n"
"          COLON\n"
"          WHITESPACE\n"
"          IDENTIFIER \"italic\"\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }

    // a comment, a simple rule, a comment
    {
        std::stringstream ss;
        ss << "// $Id: ...$\n"
           << "div { border: 1px; }\n"
           << "/* @preserve Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved. */";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"border\"\n"
"        COLON\n"
"        WHITESPACE\n"
"        INTEGER \"px\" I:1\n"
"  COMMENT \"@preserve Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved.\" I:1\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }

    // one empty C-like comment
    {
        std::stringstream ss;
        ss << "div { /**/ border: 1px; /**/ }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"border\"\n"
"        COLON\n"
"        WHITESPACE\n"
"        INTEGER \"px\" I:1\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }

    // multiple empty C-like comments
    {
        std::stringstream ss;
        ss << "div { /**/ /**/ /**/ border: 1px; /**/ /**/ /**/ }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"border\"\n"
"        COLON\n"
"        WHITESPACE\n"
"        INTEGER \"px\" I:1\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }

    // cascading fields
    {
        std::stringstream ss;
        ss << "div {\n"
           << "  font: { family: ivory; size: 16pt; style: italic };\n"
           << "  border: { color: #112389; width: 1px } /**/ ;\n"
           << "  color: /* text color */ white;\n"
           << "}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";
        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      LIST\n"
"        COMPONENT_VALUE\n"
"          IDENTIFIER \"font\"\n"
"          COLON\n"
"          OPEN_CURLYBRACKET B:false\n"
"            LIST\n"
"              COMPONENT_VALUE\n"
"                IDENTIFIER \"family\"\n"
"                COLON\n"
"                WHITESPACE\n"
"                IDENTIFIER \"ivory\"\n"
"              COMPONENT_VALUE\n"
"                IDENTIFIER \"size\"\n"
"                COLON\n"
"                WHITESPACE\n"
"                INTEGER \"pt\" I:16\n"
"              COMPONENT_VALUE\n"
"                IDENTIFIER \"style\"\n"
"                COLON\n"
"                WHITESPACE\n"
"                IDENTIFIER \"italic\"\n"
"        COMPONENT_VALUE\n"
"          IDENTIFIER \"border\"\n"
"          COLON\n"
"          OPEN_CURLYBRACKET B:false\n"
"            LIST\n"
"              COMPONENT_VALUE\n"
"                IDENTIFIER \"color\"\n"
"                COLON\n"
"                WHITESPACE\n"
"                HASH \"112389\"\n"
"              COMPONENT_VALUE\n"
"                IDENTIFIER \"width\"\n"
"                COLON\n"
"                WHITESPACE\n"
"                INTEGER \"px\" I:1\n"
"        COMPONENT_VALUE\n"
"          IDENTIFIER \"color\"\n"
"          COLON\n"
"          WHITESPACE\n"
"          IDENTIFIER \"white\"\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }

    // verify support of an empty {}-block
    {
        std::stringstream ss;
        ss << "div section span {}\n"
           << "div p b {}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";
        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    WHITESPACE\n"
"    IDENTIFIER \"section\"\n"
"    WHITESPACE\n"
"    IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    WHITESPACE\n"
"    IDENTIFIER \"p\"\n"
"    WHITESPACE\n"
"    IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      LIST\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }
}

CATCH_TEST_CASE("Invalid stylesheets", "[parser] [stylesheet] [invalid]")
{
    // closing '}' one too many times
    {
        std::stringstream ss;
        ss << "<!-- body { background : white url( /images/background.png ) } --> }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS("test.css(1): error: Unexpected closing block of type: CLOSE_CURLYBRACKET.\n");
    }

    // closing ']' one too many times
    {
        std::stringstream ss;
        ss << "<!-- body[browser~=\"great\"]] { background : white url( /images/background.png ) } -->";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS(
                "test.css(1): error: A qualified rule must end with a { ... } block.\n"
                "test.css(1): error: Unexpected closing block of type: CLOSE_SQUAREBRACKET.\n"
            );
    }

    // closing ')' one too many times
    {
        std::stringstream ss;
        ss << "<!-- body[browser~=\"great\"] { background : white url( /images/background.png ); border-top-color: rgb(1,2,3)); } -->";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS(
                "test.css(1): error: Block expected to end with CLOSE_CURLYBRACKET but got CLOSE_PARENTHESIS instead.\n"
                //"test.css(1): error: Unexpected closing block of type: CLOSE_PARENTHESIS.\n"
            );
    }

    // extra ';'
    {
        std::stringstream ss;
        ss << "illegal { semi: colon };";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS(
                "test.css(1): error: A qualified rule cannot end a { ... } block with a ';'.\n"
            );
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Simple rules", "[parser] [rule-list]")
{
    // a simple valid rule
    {
        std::stringstream ss;
        ss << " body { background : gradient(to bottom, #012, #384513 75%, #452) } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.rule_list());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"body\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"background\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        WHITESPACE\n"
"        FUNCTION \"gradient\"\n"
"          IDENTIFIER \"to\"\n"
"          WHITESPACE\n"
"          IDENTIFIER \"bottom\"\n"
"          COMMA\n"
"          WHITESPACE\n"
"          HASH \"012\"\n"
"          COMMA\n"
"          WHITESPACE\n"
"          HASH \"384513\"\n"
"          WHITESPACE\n"
"          PERCENT D:0.75\n"
"          COMMA\n"
"          WHITESPACE\n"
"          HASH \"452\"\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }

    // a simple valid rule
    {
        std::stringstream ss;
        ss << " div { color: blue; }"
           << " @media screen { viewport: 1000px 500px; } "
           << " div#op{color:hsl(120,1,0.5)}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.rule_list());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        WHITESPACE\n"
"        IDENTIFIER \"blue\"\n"
"  AT_KEYWORD \"media\" I:0\n"
"    IDENTIFIER \"screen\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"viewport\"\n"
"        COLON\n"
"        WHITESPACE\n"
"        INTEGER \"px\" I:1000\n"
"        WHITESPACE\n"
"        INTEGER \"px\" I:500\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    HASH \"op\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        FUNCTION \"hsl\"\n"
"          INTEGER \"\" I:120\n"
"          COMMA\n"
"          INTEGER \"\" I:1\n"
"          COMMA\n"
"          DECIMAL_NUMBER \"\" D:0.5\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }
}

CATCH_TEST_CASE("Nested rules", "[parser] [rule-list]")
{
    // at rule inside another at rule
    {
        std::stringstream ss;
        ss << "@if true { @message \"blah\"; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.rule_list());

        // no error left over
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"if\" I:0\n"
"    IDENTIFIER \"true\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        AT_KEYWORD \"message\" I:0\n"
"          STRING \"blah\"\n"

            );
    }
}

CATCH_TEST_CASE("Invalid rules", "[parser] [rule-list] [invalid]")
{
    // breaks on the <!--
    {
        std::stringstream ss;
        ss << "<!-- body { background : white url( /images/background.png ) } -->";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.rule_list());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS("test.css(1): error: HTML comment delimiters (<!-- and -->) are not allowed in this CSS document.\n");
    }

    // breaks on the -->
    {
        std::stringstream ss;
        ss << "body { background : white url( /images/background.png ) 44px } -->";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.rule_list());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS(
                "test.css(1): error: A qualified rule cannot be empty; you are missing a { ... } block.\n"
                "test.css(1): error: HTML comment delimiters (<!-- and -->) are not allowed in this CSS document.\n"
            );
    }

    // breaks on the }
    {
        std::stringstream ss;
        ss << "body { background : white url( /images/background.png ) } }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.rule_list());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS(
                "test.css(1): error: A qualified rule cannot be empty; you are missing a { ... } block.\n"
                "test.css(1): error: Unexpected closing block of type: CLOSE_CURLYBRACKET.\n"
            );
    }

    // breaks on the ]
    {
        std::stringstream ss;
        ss << "body[lili=\"joe\"]] { background : white url( /images/background.png ) } }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.rule_list());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS(
                "test.css(1): error: A qualified rule must end with a { ... } block.\n"
                "test.css(1): error: Unexpected closing block of type: CLOSE_SQUAREBRACKET.\n"
            );
    }

    // breaks on the )
    {
        std::stringstream ss;
        ss << " body[lili=\"joe\"] { background : white url( /images/background.png ); color:rgba(0,0,0,0)); } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.rule_list());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS(
                "test.css(1): error: Block expected to end with CLOSE_CURLYBRACKET but got CLOSE_PARENTHESIS instead.\n"
                //"test.css(1): error: Unexpected closing block of type: CLOSE_PARENTHESIS.\n"
            );
    }

    // a @-rule cannot be empty
    {
        std::stringstream ss;
        ss << " div { color: blue; }"
           << " @media";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.rule_list());

//std::cerr << "Result is: [" << *n << "]\n";

        VERIFY_ERRORS("test.css(1): error: At '@' command cannot be empty (missing expression or block) unless ended by a semicolon (;).\n");
    }

    // a @-rule cannot be empty
    {
        std::stringstream ss;
        ss << "@media test and (this one too) or (that maybe)";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.rule_list());

//std::cerr << "Result is: [" << *n << "]\n";

        VERIFY_ERRORS("test.css(1): error: At '@' command must end with a block or a ';'.\n");
    }

    // :INTEGER is not valid, plus it is viewed as a nested rule!
    {
        std::stringstream ss;
        ss << "div:556 {color:bisque}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        VERIFY_ERRORS("test.css(1): error: Variable set to a block and a nested property block must end with a semicolon (;) after said block.\n");
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("One simple rule", "[parser] [rule]")
{
    // a simple valid rule
    {
        std::stringstream ss;
        ss << " body { background : gradient(to bottom, #012, #384513 75%, #452) } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.rule());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"COMPONENT_VALUE\n"
"  IDENTIFIER \"body\"\n"
"  OPEN_CURLYBRACKET B:false\n"
"    COMPONENT_VALUE\n"
"      IDENTIFIER \"background\"\n"
"      WHITESPACE\n"
"      COLON\n"
"      WHITESPACE\n"
"      FUNCTION \"gradient\"\n"
"        IDENTIFIER \"to\"\n"
"        WHITESPACE\n"
"        IDENTIFIER \"bottom\"\n"
"        COMMA\n"
"        WHITESPACE\n"
"        HASH \"012\"\n"
"        COMMA\n"
"        WHITESPACE\n"
"        HASH \"384513\"\n"
"        WHITESPACE\n"
"        PERCENT D:0.75\n"
"        COMMA\n"
"        WHITESPACE\n"
"        HASH \"452\"\n"

            );

    }

    // a simple valid rule
    {
        std::stringstream ss;
        ss << " div { color: blue; }"
           << " @media screen { viewport: 1000px 500px; } "
           << " div#op{color:hsl(120,1,0.5)}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.rule());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"COMPONENT_VALUE\n"
"  IDENTIFIER \"div\"\n"
"  OPEN_CURLYBRACKET B:false\n"
"    COMPONENT_VALUE\n"
"      IDENTIFIER \"color\"\n"
"      COLON\n"
"      WHITESPACE\n"
"      IDENTIFIER \"blue\"\n"

            );

        n = p.rule();

        out.str("");
        out << *n;
        VERIFY_TREES(out.str(),

"AT_KEYWORD \"media\" I:0\n"
"  IDENTIFIER \"screen\"\n"
"  OPEN_CURLYBRACKET B:false\n"
"    COMPONENT_VALUE\n"
"      IDENTIFIER \"viewport\"\n"
"      COLON\n"
"      WHITESPACE\n"
"      INTEGER \"px\" I:1000\n"
"      WHITESPACE\n"
"      INTEGER \"px\" I:500\n"

            );

        n = p.rule();

        out.str("");
        out << *n;
        VERIFY_TREES(out.str(),

"COMPONENT_VALUE\n"
"  IDENTIFIER \"div\"\n"
"  HASH \"op\"\n"
"  OPEN_CURLYBRACKET B:false\n"
"    COMPONENT_VALUE\n"
"      IDENTIFIER \"color\"\n"
"      COLON\n"
"      FUNCTION \"hsl\"\n"
"        INTEGER \"\" I:120\n"
"        COMMA\n"
"        INTEGER \"\" I:1\n"
"        COMMA\n"
"        DECIMAL_NUMBER \"\" D:0.5\n"

            );

    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid one rule", "[parser] [rule] [invalid]")
{
    // breaks on the <!--
    {
        std::stringstream ss;
        ss << "<!-- body { background : white url( /images/background.png ) } -->";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.rule());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS("test.css(1): error: HTML comment delimiters (<!-- and -->) are not allowed in this CSS document.\n");
    }

    // breaks on the -->
    {
        std::stringstream ss;
        ss << "--> body { background : white url( /images/background.png ) }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.rule());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS("test.css(1): error: HTML comment delimiters (<!-- and -->) are not allowed in this CSS document.\n");
    }

    // breaks on the }
    {
        std::stringstream ss;
        ss << "body { background : white url( /images/background.png ) } }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // the first read works as expected
        csspp::node::pointer_t n(p.rule());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"COMPONENT_VALUE\n"
"  IDENTIFIER \"body\"\n"
"  OPEN_CURLYBRACKET B:false\n"
"    COMPONENT_VALUE\n"
"      IDENTIFIER \"background\"\n"
"      WHITESPACE\n"
"      COLON\n"
"      WHITESPACE\n"
"      IDENTIFIER \"white\"\n"
"      WHITESPACE\n"
"      URL \"/images/background.png\"\n"

            );

        // this failed with an error, no need to check the "broken" output
        n = p.rule();

        VERIFY_ERRORS(
                "test.css(1): error: A qualified rule cannot be empty; you are missing a { ... } block.\n"
                //"test.css(1): error: Unexpected closing block of type: CLOSE_CURLYBRACKET.\n"
            );
    }

    // breaks on the ]
    {
        std::stringstream ss;
        ss << "body[lili=\"joe\"]] { background : white url( /images/background.png ) } }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule up to the spurious ']' is all proper
        csspp::node::pointer_t n(p.rule());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"COMPONENT_VALUE\n"
"  IDENTIFIER \"body\"\n"
"  OPEN_SQUAREBRACKET\n"
"    IDENTIFIER \"lili\"\n"
"    EQUAL\n"
"    STRING \"joe\"\n"

            );

        // this failed with an error, no need to check the "broken" output
        n = p.rule();

        VERIFY_ERRORS(
                "test.css(1): error: A qualified rule must end with a { ... } block.\n"
                "test.css(1): error: Unexpected closing block of type: CLOSE_SQUAREBRACKET.\n"
            );
    }

    // breaks on the )
    {
        std::stringstream ss;
        ss << " body[lili=\"joe\"] { background : white url( /images/background.png ); color:rgba(0,0,0,0)); } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.rule());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS(
                "test.css(1): error: Block expected to end with CLOSE_CURLYBRACKET but got CLOSE_PARENTHESIS instead.\n"
                //"test.css(1): error: Unexpected closing block of type: CLOSE_PARENTHESIS.\n"
            );
    }

//    // a @-rule cannot be empty
//    {
//        std::stringstream ss;
//        ss << " div { color: blue; }"
//           << " @media";
//        csspp::position pos("test.css");
//        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));
//
//        csspp::parser p(l);
//
//        csspp::node::pointer_t n(p.rule());
//
////std::cerr << "Result is: [" << *n << "]\n";
//
//        std::stringstream out;
//        out << *n;
//        VERIFY_TREES(out.str(),
//
//"COMPONENT_VALUE\n"
//"  IDENTIFIER \"div\"\n"
//"  OPEN_CURLYBRACKET B:false\n"
//"    IDENTIFIER \"color\"\n"
//"    COLON\n"
//"    WHITESPACE\n"
//"    IDENTIFIER \"blue\"\n"
//
//            );
//
//        // this failed with an error, no need to check the "broken" output
//        n = p.rule();
//
//        VERIFY_ERRORS("test.css(1): error: At '@' command cannot be empty (missing expression or block) unless ended by a semicolon (;).\n");
//    }
//
//    // a @-rule cannot be empty
//    {
//        std::stringstream ss;
//        ss << "@media test and (this one too) or (that maybe)";
//        csspp::position pos("test.css");
//        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));
//
//        csspp::parser p(l);
//
//        csspp::node::pointer_t n(p.rule());
//
////std::cerr << "Result is: [" << *n << "]\n";
//
//        VERIFY_ERRORS("test.css(1): error: At '@' command must end with a block or a ';'.\n");
//    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Simple component values", "[parser] [component-value]")
{
    // a simple valid rule
    {
        std::stringstream ss;
        ss << " body { background : gradient(to bottom, #012, #384513 75%, #452) } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.component_value_list());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"COMPONENT_VALUE\n"
"  IDENTIFIER \"body\"\n"
"  OPEN_CURLYBRACKET B:false\n"
"    COMPONENT_VALUE\n"
"      IDENTIFIER \"background\"\n"
"      WHITESPACE\n"
"      COLON\n"
"      WHITESPACE\n"
"      FUNCTION \"gradient\"\n"
"        IDENTIFIER \"to\"\n"
"        WHITESPACE\n"
"        IDENTIFIER \"bottom\"\n"
"        COMMA\n"
"        WHITESPACE\n"
"        HASH \"012\"\n"
"        COMMA\n"
"        WHITESPACE\n"
"        HASH \"384513\"\n"
"        WHITESPACE\n"
"        PERCENT D:0.75\n"
"        COMMA\n"
"        WHITESPACE\n"
"        HASH \"452\"\n"

            );

    }

    // a simple valid rule
    {
        std::stringstream ss;
        ss << " div { color: blue; }"
           << " @media screen { viewport: 1000px 500px; } "
           << " div#op{color:hsl(120,1,0.5)}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.component_value_list());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"COMPONENT_VALUE\n"
"  IDENTIFIER \"div\"\n"
"  OPEN_CURLYBRACKET B:false\n"
"    COMPONENT_VALUE\n"
"      IDENTIFIER \"color\"\n"
"      COLON\n"
"      WHITESPACE\n"
"      IDENTIFIER \"blue\"\n"

            );

        n = p.rule();

        out.str("");
        out << *n;
        VERIFY_TREES(out.str(),

"AT_KEYWORD \"media\" I:0\n"
"  IDENTIFIER \"screen\"\n"
"  OPEN_CURLYBRACKET B:false\n"
"    COMPONENT_VALUE\n"
"      IDENTIFIER \"viewport\"\n"
"      COLON\n"
"      WHITESPACE\n"
"      INTEGER \"px\" I:1000\n"
"      WHITESPACE\n"
"      INTEGER \"px\" I:500\n"

            );

        n = p.rule();

        out.str("");
        out << *n;
        VERIFY_TREES(out.str(),

"COMPONENT_VALUE\n"
"  IDENTIFIER \"div\"\n"
"  HASH \"op\"\n"
"  OPEN_CURLYBRACKET B:false\n"
"    COMPONENT_VALUE\n"
"      IDENTIFIER \"color\"\n"
"      COLON\n"
"      FUNCTION \"hsl\"\n"
"        INTEGER \"\" I:120\n"
"        COMMA\n"
"        INTEGER \"\" I:1\n"
"        COMMA\n"
"        DECIMAL_NUMBER \"\" D:0.5\n"

            );

    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid component values", "[parser] [component-value] [invalid]")
{
    // breaks on missing }
    {
        std::stringstream ss;
        ss << "body { background : white url( /images/background.png )";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.component_value_list());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS(
                "test.css(1): error: Block expected to end with CLOSE_CURLYBRACKET but got EOF_TOKEN instead.\n"
            );
    }

    // breaks on missing ]
    {
        std::stringstream ss;
        ss << "body[lili=\"joe\" { background : white url( /images/background.png ) } }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.component_value_list());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS(
                "test.css(1): error: Block expected to end with CLOSE_SQUAREBRACKET but got CLOSE_CURLYBRACKET instead.\n"
            );
    }

    // breaks on missing )
    {
        std::stringstream ss;
        ss << " body[lili=\"joe\"] { background : white url( /images/background.png ); color:rgba(0,0,0,0; } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.component_value_list());

//std::cerr << "Result is: [" << *n << "]\n";

        // this failed with an error, no need to check the "broken" output

        VERIFY_ERRORS(
                "test.css(1): error: Block expected to end with CLOSE_PARENTHESIS but got CLOSE_CURLYBRACKET instead.\n"
            );
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Simple one component value", "[parser] [component-value]")
{
    // a simple valid rule
    {
        std::stringstream ss;
        ss << " body { background : gradient(to bottom, #012, #384513 75%, #452) }"
           << " @media screen { viewport: 1000px 500px; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        char const *results[] =
        {
            "WHITESPACE\n",

            "IDENTIFIER \"body\"\n",

            "WHITESPACE\n",

            "OPEN_CURLYBRACKET B:false\n"
            "  COMPONENT_VALUE\n"
            "    IDENTIFIER \"background\"\n"
            "    WHITESPACE\n"
            "    COLON\n"
            "    WHITESPACE\n"
            "    FUNCTION \"gradient\"\n"
            "      IDENTIFIER \"to\"\n"
            "      WHITESPACE\n"
            "      IDENTIFIER \"bottom\"\n"
            "      COMMA\n"
            "      WHITESPACE\n"
            "      HASH \"012\"\n"
            "      COMMA\n"
            "      WHITESPACE\n"
            "      HASH \"384513\"\n"
            "      WHITESPACE\n"
            "      PERCENT D:0.75\n"
            "      COMMA\n"
            "      WHITESPACE\n"
            "      HASH \"452\"\n",

            "WHITESPACE\n",

            "AT_KEYWORD \"media\" I:0\n",

            "WHITESPACE\n",

            "IDENTIFIER \"screen\"\n",

            "WHITESPACE\n",

            "OPEN_CURLYBRACKET B:false\n"
            "  COMPONENT_VALUE\n"
            "    IDENTIFIER \"viewport\"\n"
            "    COLON\n"
            "    WHITESPACE\n"
            "    INTEGER \"px\" I:1000\n"
            "    WHITESPACE\n"
            "    INTEGER \"px\" I:500\n",

            // make sure to keep the following to make sure we got everything
            // through the parser
            "EOF_TOKEN\n"
        };

        for(size_t i(0); i < sizeof(results) / sizeof(results[0]); ++i)
        {
            csspp::node::pointer_t n(p.component_value());
            std::stringstream out;
            out << *n;
            VERIFY_TREES(out.str(), results[i]);
            csspp_test::compare(out.str(), results[i], __FILE__, i + 1);
        }

    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid one component value", "[parser] [component-value] [invalid]")
{
    // breaks on missing }
    {
        std::stringstream ss;
        ss << "body { background : 123";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        char const *results[] =
        {
            "IDENTIFIER \"body\"\n",

            "WHITESPACE\n",

            "OPEN_CURLYBRACKET B:false\n"
            "  COMPONENT_VALUE\n"
            "    IDENTIFIER \"background\"\n"
            "    WHITESPACE\n"
            "    COLON\n"
            "    WHITESPACE\n"
            "    INTEGER \"\" I:123\n",

            // make sure to keep the following to make sure we got everything
            // through the parser
            "EOF_TOKEN\n"
        };

        for(size_t i(0); i < sizeof(results) / sizeof(results[0]); ++i)
        {
            csspp::node::pointer_t n(p.component_value());
            std::stringstream out;
            out << *n;
            VERIFY_TREES(out.str(), results[i]);
            csspp_test::compare(out.str(), results[i], __FILE__, i + 1);
        }

        VERIFY_ERRORS("test.css(1): error: Block expected to end with CLOSE_CURLYBRACKET but got EOF_TOKEN instead.\n");
    }

    // breaks on missing ]
    {
        std::stringstream ss;
        ss << "body[color='55'";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        char const *results[] =
        {
            "IDENTIFIER \"body\"\n",

            "OPEN_SQUAREBRACKET\n"
            "  IDENTIFIER \"color\"\n"
            "  EQUAL\n"
            "  STRING \"55\"\n",

            // make sure to keep the following to make sure we got everything
            // through the parser
            "EOF_TOKEN\n"
        };

        for(size_t i(0); i < sizeof(results) / sizeof(results[0]); ++i)
        {
            csspp::node::pointer_t n(p.component_value());
            std::stringstream out;
            out << *n;
            VERIFY_TREES(out.str(), results[i]);
            csspp_test::compare(out.str(), results[i], __FILE__, i + 1);
        }

        VERIFY_ERRORS("test.css(1): error: Block expected to end with CLOSE_SQUAREBRACKET but got EOF_TOKEN instead.\n");
    }

    // breaks on missing )
    {
        std::stringstream ss;
        ss << "body{color:rgba(1,2}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        char const *results[] =
        {
            "IDENTIFIER \"body\"\n",

            "OPEN_CURLYBRACKET B:false\n"
            "  COMPONENT_VALUE\n"
            "    IDENTIFIER \"color\"\n"
            "    COLON\n"
            "    FUNCTION \"rgba\"\n"
            "      INTEGER \"\" I:1\n"
            "      COMMA\n"
            "      INTEGER \"\" I:2\n",

            // make sure to keep the following to make sure we got everything
            // through the parser
            "EOF_TOKEN\n"
        };

        for(size_t i(0); i < sizeof(results) / sizeof(results[0]); ++i)
        {
            csspp::node::pointer_t n(p.component_value());
            std::stringstream out;
            out << *n;
            VERIFY_TREES(out.str(), results[i]);
            csspp_test::compare(out.str(), results[i], __FILE__, i + 1);
        }

        VERIFY_ERRORS("test.css(1): error: Block expected to end with CLOSE_PARENTHESIS but got CLOSE_CURLYBRACKET instead.\n");
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Simple declarations", "[parser] [declaration]")
{
    // a simple valid declaration
    {
        std::stringstream ss;
        ss << " background : gradient(to bottom, #012, #384513 75%, #452) { width: 300px } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.declaration_list());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  DECLARATION \"background\"\n"
"    COMPONENT_VALUE\n"
"      FUNCTION \"gradient\"\n"
"        IDENTIFIER \"to\"\n"
"        WHITESPACE\n"
"        IDENTIFIER \"bottom\"\n"
"        COMMA\n"
"        WHITESPACE\n"
"        HASH \"012\"\n"
"        COMMA\n"
"        WHITESPACE\n"
"        HASH \"384513\"\n"
"        WHITESPACE\n"
"        PERCENT D:0.75\n"
"        COMMA\n"
"        WHITESPACE\n"
"        HASH \"452\"\n"
"      OPEN_CURLYBRACKET B:false\n"
"        COMPONENT_VALUE\n"
"          IDENTIFIER \"width\"\n"
"          COLON\n"
"          WHITESPACE\n"
"          INTEGER \"px\" I:300\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }

    // a @-rule in a declaration
    {
        std::stringstream ss;
        ss << " @enhanced capabilities { background : gradient(to bottom, #012, #384513 75%, #452) } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.declaration_list());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"enhanced\" I:0\n"
"    IDENTIFIER \"capabilities\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"background\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        WHITESPACE\n"
"        FUNCTION \"gradient\"\n"
"          IDENTIFIER \"to\"\n"
"          WHITESPACE\n"
"          IDENTIFIER \"bottom\"\n"
"          COMMA\n"
"          WHITESPACE\n"
"          HASH \"012\"\n"
"          COMMA\n"
"          WHITESPACE\n"
"          HASH \"384513\"\n"
"          WHITESPACE\n"
"          PERCENT D:0.75\n"
"          COMMA\n"
"          WHITESPACE\n"
"          HASH \"452\"\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }

    // multiple declarations require a ';'
    {
        std::stringstream ss;
        ss << "a: 33px; b: 66px; c: 123px";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.declaration_list());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  DECLARATION \"a\"\n"
"    COMPONENT_VALUE\n"
"      INTEGER \"px\" I:33\n"
"  DECLARATION \"b\"\n"
"    COMPONENT_VALUE\n"
"      INTEGER \"px\" I:66\n"
"  DECLARATION \"c\"\n"
"    COMPONENT_VALUE\n"
"      INTEGER \"px\" I:123\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }

    // multiple declarations require a ';'
    {
        std::stringstream ss;
        ss << "a: 33px ! important ; b: 66px !global ; c: 123px 55em !import";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.declaration_list());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  DECLARATION \"a\"\n"
"    COMPONENT_VALUE\n"
"      INTEGER \"px\" I:33\n"
"      EXCLAMATION \"important\"\n"
"  DECLARATION \"b\"\n"
"    COMPONENT_VALUE\n"
"      INTEGER \"px\" I:66\n"
"      EXCLAMATION \"global\"\n"
"  DECLARATION \"c\"\n"
"    COMPONENT_VALUE\n"
"      INTEGER \"px\" I:123\n"
"      WHITESPACE\n"
"      INTEGER \"em\" I:55\n"
"      EXCLAMATION \"import\"\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }
}

CATCH_TEST_CASE("Invalid declarations", "[parser] [declaration] [invalid]")
{
    // declarations must end with EOF
    {
        std::stringstream ss;
        ss << " background : gradient(to bottom, #012, #384513 75%, #452) { width: 300px } <!--";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.declaration_list());

//std::cerr << "Result is: [" << *n << "]\n";

        VERIFY_ERRORS("test.css(1): error: the end of the stream was not reached in this declaration, we stopped on a CDO.\n");
    }

    // declarations missing a ':'
    {
        std::stringstream ss;
        ss << " background gradient(to bottom, #012, #384513 75%, #452) { width: 300px } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.declaration_list());

//std::cerr << "Result is: [" << *n << "]\n";

        VERIFY_ERRORS(
                "test.css(1): error: ':' missing in your declaration starting with \"background\".\n"
            );
    }

    // '!' without an identifier
    {
        std::stringstream ss;
        ss << "background: !gradient(to bottom, #012, #384513 75%, #452)";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        // rule list does not like <!-- and -->
        csspp::node::pointer_t n(p.declaration_list());

//std::cerr << "Result is: [" << *n << "]\n";

        VERIFY_ERRORS(
                "test.css(1): error: A '!' must be followed by an identifier, got a FUNCTION instead.\n"
                //"test.css(1): error: the end of the stream was not reached in this declaration, we stopped on a FUNCTION.\n"
            );
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Multi-line, multi-level stylesheet", "[parser] [rules]")
{
    {
        std::stringstream ss;
        ss << "body { background : white url( /images/background.png ) }"
              "div.power-house { !important margin: 0; color: red ; }"
              "a { text-decoration: none; }"
              "$green: #080;"
              "#doll { background-color: $green; &:hover { color: teal; } }"
              "@supports (background-color and border-radius) or (background-image) { body > E ~ F + G H { font-style: italic } }"
           ;
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"body\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"background\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        WHITESPACE\n"
"        IDENTIFIER \"white\"\n"
"        WHITESPACE\n"
"        URL \"/images/background.png\"\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    PERIOD\n"
"    IDENTIFIER \"power-house\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      LIST\n"
"        COMPONENT_VALUE\n"
"          EXCLAMATION \"important\"\n"
"          IDENTIFIER \"margin\"\n"
"          COLON\n"
"          WHITESPACE\n"
"          INTEGER \"\" I:0\n"
"        COMPONENT_VALUE\n"
"          IDENTIFIER \"color\"\n"
"          COLON\n"
"          WHITESPACE\n"
"          IDENTIFIER \"red\"\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"a\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"text-decoration\"\n"
"        COLON\n"
"        WHITESPACE\n"
"        IDENTIFIER \"none\"\n"
"  COMPONENT_VALUE\n"
"    VARIABLE \"green\"\n"
"    COLON\n"
"    WHITESPACE\n"
"    HASH \"080\"\n"
"  COMPONENT_VALUE\n"
"    HASH \"doll\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      LIST\n"
"        COMPONENT_VALUE\n"
"          IDENTIFIER \"background-color\"\n"
"          COLON\n"
"          WHITESPACE\n"
"          VARIABLE \"green\"\n"
"        COMPONENT_VALUE\n"
"          REFERENCE\n"
"          COLON\n"
"          IDENTIFIER \"hover\"\n"
"          OPEN_CURLYBRACKET B:false\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"color\"\n"
"              COLON\n"
"              WHITESPACE\n"
"              IDENTIFIER \"teal\"\n"
"  AT_KEYWORD \"supports\" I:0\n"
"    OPEN_PARENTHESIS\n"
"      IDENTIFIER \"background-color\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"and\"\n"
"      WHITESPACE\n"
"      IDENTIFIER \"border-radius\"\n"
"    WHITESPACE\n"
"    IDENTIFIER \"or\"\n"
"    OPEN_PARENTHESIS\n"
"      IDENTIFIER \"background-image\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"body\"\n"
"        WHITESPACE\n"
"        GREATER_THAN\n"
"        WHITESPACE\n"
"        IDENTIFIER \"E\"\n"
"        WHITESPACE\n"
"        PRECEDED\n"
"        WHITESPACE\n"
"        IDENTIFIER \"F\"\n"
"        WHITESPACE\n"
"        ADD\n"
"        WHITESPACE\n"
"        IDENTIFIER \"G\"\n"
"        WHITESPACE\n"
"        IDENTIFIER \"H\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"font-style\"\n"
"            COLON\n"
"            WHITESPACE\n"
"            IDENTIFIER \"italic\"\n"

            );
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Is variable set", "[parser] [variable] [invalid]")
{
    // simple test with a value + value (SASS compatible)
    {
        std::stringstream ss;
        ss << "$a: 33px;";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    VARIABLE \"a\"\n"
"    COLON\n"
"    WHITESPACE\n"
"    INTEGER \"px\" I:33\n"

            );

        csspp::node::pointer_t var(n->get_child(0));
        CATCH_REQUIRE(csspp::parser::is_variable_set(var, false));
        CATCH_REQUIRE_FALSE(csspp::parser::is_variable_set(var, true));
    }

    // case were we actually use a variable to define a selector
    // this is not a variable set
    {
        std::stringstream ss;
        ss << "$a .cute { color: red; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    VARIABLE \"a\"\n"
"    WHITESPACE\n"
"    PERIOD\n"
"    IDENTIFIER \"cute\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        WHITESPACE\n"
"        IDENTIFIER \"red\"\n"

            );

        csspp::node::pointer_t var(n->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_variable_set(var, false));
        CATCH_REQUIRE_FALSE(csspp::parser::is_variable_set(var, true));
    }

    // test with a variable block
    {
        std::stringstream ss;
        ss << "$a: { color: red; };";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    VARIABLE \"a\"\n"
"    COLON\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        WHITESPACE\n"
"        IDENTIFIER \"red\"\n"

            );

        csspp::node::pointer_t var(n->get_child(0));
        CATCH_REQUIRE(csspp::parser::is_variable_set(var, false));
        CATCH_REQUIRE(csspp::parser::is_variable_set(var, true));
    }

    // test with the missing ';'
    {
        std::stringstream ss;
        ss << "$a: { color: red; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // the ';' at the end is missing
        VERIFY_ERRORS("test.css(1): error: Variable set to a block and a nested property block must end with a semicolon (;) after said block.\n");
    }

    // simple test with a value + value (SASS compatible)
    {
        std::stringstream ss;
        ss << "$a($arg1): 33px;";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    VARIABLE_FUNCTION \"a\"\n"
"      VARIABLE \"arg1\"\n"
"    COLON\n"
"    WHITESPACE\n"
"    INTEGER \"px\" I:33\n"

            );

        csspp::node::pointer_t var(n->get_child(0));
        CATCH_REQUIRE(csspp::parser::is_variable_set(var, false));
        CATCH_REQUIRE_FALSE(csspp::parser::is_variable_set(var, true));
    }

    // case were we actually use a variable to define a selector
    // this is not a variable set
    {
        std::stringstream ss;
        ss << "$a(33) .cute { color: red; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    VARIABLE_FUNCTION \"a\"\n"
"      INTEGER \"\" I:33\n"
"    WHITESPACE\n"
"    PERIOD\n"
"    IDENTIFIER \"cute\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        WHITESPACE\n"
"        IDENTIFIER \"red\"\n"

            );

        csspp::node::pointer_t var(n->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_variable_set(var, false));
        CATCH_REQUIRE_FALSE(csspp::parser::is_variable_set(var, true));
    }

    // test with a variable block
    {
        std::stringstream ss;
        ss << "$a($arg1): { color: red; };";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    VARIABLE_FUNCTION \"a\"\n"
"      VARIABLE \"arg1\"\n"
"    COLON\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        WHITESPACE\n"
"        IDENTIFIER \"red\"\n"

            );

        csspp::node::pointer_t var(n->get_child(0));
        CATCH_REQUIRE(csspp::parser::is_variable_set(var, false));
        CATCH_REQUIRE(csspp::parser::is_variable_set(var, true));
    }

    // test with the missing ';'
    {
        std::stringstream ss;
        ss << "$a($arg1): { color: red; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // the ';' at the end is missing
        VERIFY_ERRORS("test.css(1): error: Variable set to a block and a nested property block must end with a semicolon (;) after said block.\n");
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Is nested declaration", "[parser] [variable] [invalid]")
{
    // which a field name with a simple nested declaration
    {
        std::stringstream ss;
        ss << "width : { color : red } ;";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"width\"\n"
"    WHITESPACE\n"
"    COLON\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        WHITESPACE\n"
"        IDENTIFIER \"red\"\n"

            );

        csspp::node::pointer_t var(n->get_child(0));
        CATCH_REQUIRE(csspp::parser::is_nested_declaration(var));
    }

    // which a field name with a simple nested declaration
    {
        std::stringstream ss;
        ss << "width :nth-child(3n+2) span{ color : red } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"width\"\n"
"    WHITESPACE\n"
"    COLON\n"
"    FUNCTION \"nth-child\"\n"
"      INTEGER \"n\" I:3\n"
"      INTEGER \"\" I:2\n"
"    WHITESPACE\n"
"    IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        WHITESPACE\n"
"        IDENTIFIER \"red\"\n"

            );

        csspp::node::pointer_t var(n->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(var));
    }

    // which a field name with a simple nested declaration
    {
        std::stringstream ss;
        ss << "width :not(.color) { color : red } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"width\"\n"
"    WHITESPACE\n"
"    COLON\n"
"    FUNCTION \"not\"\n"
"      PERIOD\n"
"      IDENTIFIER \"color\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        WHITESPACE\n"
"        IDENTIFIER \"red\"\n"

            );

        csspp::node::pointer_t var(n->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(var));
    }

    // in this case it is clear that none are declarations
    // (although the span:not() is not valid since it is
    // missing the {}-block at the end)
    {
        std::stringstream ss;
        ss << "div { span :not(.wrap); p { color : red } } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      LIST\n"
"        COMPONENT_VALUE\n"
"          IDENTIFIER \"span\"\n"
"          WHITESPACE\n"
"          COLON\n"
"          FUNCTION \"not\"\n"
"            PERIOD\n"
"            IDENTIFIER \"wrap\"\n"
"        COMPONENT_VALUE\n"
"          IDENTIFIER \"p\"\n"
"          OPEN_CURLYBRACKET B:false\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"color\"\n"
"              WHITESPACE\n"
"              COLON\n"
"              WHITESPACE\n"
"              IDENTIFIER \"red\"\n"

            );

        // check the first COMPONENT_VALUE
        csspp::node::pointer_t div(n->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(div));

        // check the second COMPONENT_VALUE
        csspp::node::pointer_t span(n->get_child(0)->get_child(1)->get_child(0)->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(span));

        // check the third COMPONENT_VALUE
        csspp::node::pointer_t p_tag(n->get_child(0)->get_child(1)->get_child(0)->get_child(1));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(p_tag));
    }

    // in this case it is clear that none are declarations
    // (although the span:not() is not valid since it is
    // missing the {}-block at the end)
    {
        std::stringstream ss;
        ss << "div { span : { color : red }; } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"span\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"color\"\n"
"            WHITESPACE\n"
"            COLON\n"
"            WHITESPACE\n"
"            IDENTIFIER \"red\"\n"

            );

        // check the first COMPONENT_VALUE
        csspp::node::pointer_t div(n->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(div));

        // check the second COMPONENT_VALUE
        csspp::node::pointer_t span(n->get_child(0)->get_child(1)->get_child(0));
        CATCH_REQUIRE(csspp::parser::is_nested_declaration(span));

        // check the third COMPONENT_VALUE
        csspp::node::pointer_t p_tag(n->get_child(0)->get_child(1)->get_child(0)->get_child(3)->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(p_tag));
    }

    // ':' always marks a selector
    {
        std::stringstream ss;
        ss << "div { span :section : { color : red }; } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"span\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        IDENTIFIER \"section\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"color\"\n"
"            WHITESPACE\n"
"            COLON\n"
"            WHITESPACE\n"
"            IDENTIFIER \"red\"\n"
"      LIST\n"

            );

        // check the first COMPONENT_VALUE
        csspp::node::pointer_t div(n->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(div));

        // check the second COMPONENT_VALUE
        csspp::node::pointer_t span(n->get_child(0)->get_child(1)->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(span));

        // check the third COMPONENT_VALUE
        csspp::node::pointer_t p_tag(n->get_child(0)->get_child(1)->get_child(0)->get_child(6)->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(p_tag));
    }

    // '%<id>' always marks a selector
    {
        std::stringstream ss;
        ss << "div { span :section%july { color : red }; } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"span\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        IDENTIFIER \"section\"\n"
"        PLACEHOLDER \"july\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"color\"\n"
"            WHITESPACE\n"
"            COLON\n"
"            WHITESPACE\n"
"            IDENTIFIER \"red\"\n"
"      LIST\n"

            );

        // check the first COMPONENT_VALUE
        csspp::node::pointer_t div(n->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(div));

        // check the second COMPONENT_VALUE
        csspp::node::pointer_t span(n->get_child(0)->get_child(1)->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(span));

        // check the third COMPONENT_VALUE
        csspp::node::pointer_t p_tag(n->get_child(0)->get_child(1)->get_child(0)->get_child(5)->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(p_tag));
    }

    // 'E ~ E' always marks a selector
    {
        std::stringstream ss;
        ss << "div { span :section ~ july { color : red }; } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"span\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        IDENTIFIER \"section\"\n"
"        WHITESPACE\n"
"        PRECEDED\n"
"        WHITESPACE\n"
"        IDENTIFIER \"july\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"color\"\n"
"            WHITESPACE\n"
"            COLON\n"
"            WHITESPACE\n"
"            IDENTIFIER \"red\"\n"
"      LIST\n"

            );

        // check the first COMPONENT_VALUE
        csspp::node::pointer_t div(n->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(div));

        // check the second COMPONENT_VALUE
        csspp::node::pointer_t span(n->get_child(0)->get_child(1)->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(span));

        // check the third COMPONENT_VALUE
        csspp::node::pointer_t p_tag(n->get_child(0)->get_child(1)->get_child(0)->get_child(8)->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(p_tag));
    }

    // 'E & E' always marks a selector
    {
        std::stringstream ss;
        ss << "div { span :section & july { color : red }; } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"span\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        IDENTIFIER \"section\"\n"
"        WHITESPACE\n"
"        REFERENCE\n"
"        WHITESPACE\n"
"        IDENTIFIER \"july\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"color\"\n"
"            WHITESPACE\n"
"            COLON\n"
"            WHITESPACE\n"
"            IDENTIFIER \"red\"\n"
"      LIST\n"

            );

        // check the first COMPONENT_VALUE
        csspp::node::pointer_t div(n->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(div));

        // check the second COMPONENT_VALUE
        csspp::node::pointer_t span(n->get_child(0)->get_child(1)->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(span));

        // check the third COMPONENT_VALUE
        csspp::node::pointer_t p_tag(n->get_child(0)->get_child(1)->get_child(0)->get_child(8)->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(p_tag));
    }

    // 'E|E' always marks a selector
    {
        std::stringstream ss;
        ss << "div { span :section *|july { color : red }; } ";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"span\"\n"
"        WHITESPACE\n"
"        COLON\n"
"        IDENTIFIER \"section\"\n"
"        WHITESPACE\n"
"        MULTIPLY\n"
"        SCOPE\n"
"        IDENTIFIER \"july\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"color\"\n"
"            WHITESPACE\n"
"            COLON\n"
"            WHITESPACE\n"
"            IDENTIFIER \"red\"\n"
"      LIST\n"

            );

        // check the first COMPONENT_VALUE
        csspp::node::pointer_t div(n->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(div));

        // check the second COMPONENT_VALUE
        csspp::node::pointer_t span(n->get_child(0)->get_child(1)->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(span));

        // check the third COMPONENT_VALUE
        csspp::node::pointer_t p_tag(n->get_child(0)->get_child(1)->get_child(0)->get_child(8)->get_child(0));
        CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(p_tag));
    }

    // a nested block must end with a ';'
    {
        std::stringstream ss;
        ss << "width : { color : red }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("test.css(1): error: Variable set to a block and a nested property block must end with a semicolon (;) after said block.\n");
    }

    // test special cases which woudl be really hard to get from the
    // normal parser/lexer combo
    for(int i(0); i < (1 << 5); ++i)
    {
        csspp::position pos("test.css");
        csspp::node::pointer_t root(new csspp::node(csspp::node_type_t::LIST, pos));

        // name WS ':' WS '{'
        if((i & (1 << 0)) != 0)
        {
            csspp::node::pointer_t name(new csspp::node(csspp::node_type_t::IDENTIFIER, pos));
            name->set_string("field-name");
            root->add_child(name);
        }

        if((i & (1 << 1)) != 0)
        {
            csspp::node::pointer_t whitespace1(new csspp::node(csspp::node_type_t::WHITESPACE, pos));
            root->add_child(whitespace1);
        }

        if((i & (1 << 2)) != 0)
        {
            csspp::node::pointer_t colon(new csspp::node(csspp::node_type_t::COLON, pos));
            root->add_child(colon);
        }

        if((i & (1 << 3)) != 0)
        {
            csspp::node::pointer_t whitespace2(new csspp::node(csspp::node_type_t::WHITESPACE, pos));
            root->add_child(whitespace2);
        }

        if((i & (1 << 4)) != 0)
        {
            csspp::node::pointer_t curlybracket(new csspp::node(csspp::node_type_t::OPEN_CURLYBRACKET, pos));
            root->add_child(curlybracket);
        }

        // this one is "valid"
        switch(i)
        {
        case 0x1F: // all with and without spaces are valid
        case 0x1D:
        case 0x17:
        case 0x15:
            CATCH_REQUIRE(csspp::parser::is_nested_declaration(root));
            break;

        default:
            CATCH_REQUIRE_FALSE(csspp::parser::is_nested_declaration(root));
            break;

        }
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Rules defined inside an @-Keyword", "[parser] [variable] [invalid]")
{
    // which a field name with a simple nested declaration
    {
        std::stringstream ss;
        ss << "@document url(http://www.example.com/), regexp(\"https://.*\")\n"
           << "{\n"
           << "  body { width: 8.5in; height: 9in; }\n"
           << "  div { border: 0.25in solid lightgray }\n"
           << "}\n"
           << "#edge { border: 1px solid black }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no error happened
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  AT_KEYWORD \"document\" I:0\n"
"    URL \"http://www.example.com/\"\n"
"    COMMA\n"
"    WHITESPACE\n"
"    FUNCTION \"regexp\"\n"
"      STRING \"https://.*\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"body\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          LIST\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"width\"\n"
"              COLON\n"
"              WHITESPACE\n"
"              DECIMAL_NUMBER \"in\" D:8.5\n"
"            COMPONENT_VALUE\n"
"              IDENTIFIER \"height\"\n"
"              COLON\n"
"              WHITESPACE\n"
"              INTEGER \"in\" I:9\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"div\"\n"
"        OPEN_CURLYBRACKET B:false\n"
"          COMPONENT_VALUE\n"
"            IDENTIFIER \"border\"\n"
"            COLON\n"
"            WHITESPACE\n"
"            DECIMAL_NUMBER \"in\" D:0.25\n"
"            WHITESPACE\n"
"            IDENTIFIER \"solid\"\n"
"            WHITESPACE\n"
"            IDENTIFIER \"lightgray\"\n"
"  COMPONENT_VALUE\n"
"    HASH \"edge\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"border\"\n"
"        COLON\n"
"        WHITESPACE\n"
"        INTEGER \"px\" I:1\n"
"        WHITESPACE\n"
"        IDENTIFIER \"solid\"\n"
"        WHITESPACE\n"
"        IDENTIFIER \"black\"\n"

            );

    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Parse argify", "[parser] [stylesheet]")
{
    {
        std::stringstream ss;
        ss << "a,b{color:red}\n"
           << "a, b{color:red}\n"
           << "a,b ,c{color:red}\n"
           << "a , b,c{color:red}\n"
           << "a{color:red}\n"
           << "a {color:red}\n"
           << "a,b {color:red}\n"
           ;
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"a\"\n"
"    COMMA\n"
"    IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"a\"\n"
"    COMMA\n"
"    WHITESPACE\n"
"    IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"a\"\n"
"    COMMA\n"
"    IDENTIFIER \"b\"\n"
"    WHITESPACE\n"
"    COMMA\n"
"    IDENTIFIER \"c\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"a\"\n"
"    WHITESPACE\n"
"    COMMA\n"
"    WHITESPACE\n"
"    IDENTIFIER \"b\"\n"
"    COMMA\n"
"    IDENTIFIER \"c\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"a\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"a\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"a\"\n"
"    COMMA\n"
"    IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"

            );

        // Argify the list under each COMPONENT_VALUE
        CATCH_REQUIRE(n->is(csspp::node_type_t::LIST));

        size_t const max_children(n->size());
        for(size_t idx(0); idx < max_children; ++idx)
        {
            csspp::node::pointer_t component_value(n->get_child(idx));
            CATCH_REQUIRE(component_value->is(csspp::node_type_t::COMPONENT_VALUE));
            CATCH_REQUIRE(csspp::parser::argify(component_value));
        }

//std::cerr << "Argified result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        std::stringstream out2;
        out2 << *n;
        VERIFY_TREES(out2.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    ARG\n"
"      IDENTIFIER \"c\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    ARG\n"
"      IDENTIFIER \"c\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"a\"\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"

            );

        // no error left over
        VERIFY_ERRORS("");
    }
}

CATCH_TEST_CASE("Invalid argify", "[parser] [stylesheet]")
{
    // A starting comma is illegal
    {
        std::stringstream ss;
        ss << ",a{color:red}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Result is: [" << *n << "]\n";

        // no errors so far
        VERIFY_ERRORS("");

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    COMMA\n"
"    IDENTIFIER \"a\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"

            );

        // Argify the list under each COMPONENT_VALUE
        CATCH_REQUIRE(n->is(csspp::node_type_t::LIST));

        size_t const max_children(n->size());
        for(size_t idx(0); idx < max_children; ++idx)
        {
            csspp::node::pointer_t component_value(n->get_child(idx));
            CATCH_REQUIRE(component_value->is(csspp::node_type_t::COMPONENT_VALUE));
            CATCH_REQUIRE_FALSE(csspp::parser::argify(component_value));
        }

        VERIFY_ERRORS("test.css(1): error: dangling comma at the beginning of a list of arguments or selectors.\n");
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

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"a\"\n"
"    COMMA\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"

            );

        // Argify the list under each COMPONENT_VALUE
        CATCH_REQUIRE(n->is(csspp::node_type_t::LIST));

        size_t const max_children(n->size());
        for(size_t idx(0); idx < max_children; ++idx)
        {
            csspp::node::pointer_t component_value(n->get_child(idx));
            CATCH_REQUIRE(component_value->is(csspp::node_type_t::COMPONENT_VALUE));
            CATCH_REQUIRE_FALSE(csspp::parser::argify(component_value));
        }

        VERIFY_ERRORS("test.css(1): error: dangling comma at the end of a list of arguments or selectors.\n");
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

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"a\"\n"
"    COMMA\n"
"    COMMA\n"
"    IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"

            );

        // Argify the list under each COMPONENT_VALUE
        CATCH_REQUIRE(n->is(csspp::node_type_t::LIST));

        size_t const max_children(n->size());
        for(size_t idx(0); idx < max_children; ++idx)
        {
            csspp::node::pointer_t component_value(n->get_child(idx));
            CATCH_REQUIRE(component_value->is(csspp::node_type_t::COMPONENT_VALUE));
            CATCH_REQUIRE_FALSE(csspp::parser::argify(component_value));
        }

        VERIFY_ERRORS("test.css(1): error: two commas in a row are invalid in a list of arguments or selectors.\n");
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

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    COMMA\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"

            );

        // Argify the list under each COMPONENT_VALUE
        CATCH_REQUIRE(n->is(csspp::node_type_t::LIST));

        size_t const max_children(n->size());
        for(size_t idx(0); idx < max_children; ++idx)
        {
            csspp::node::pointer_t component_value(n->get_child(idx));
            CATCH_REQUIRE(component_value->is(csspp::node_type_t::COMPONENT_VALUE));
            CATCH_REQUIRE_FALSE(csspp::parser::argify(component_value));
        }

        VERIFY_ERRORS("test.css(1): error: dangling comma at the beginning of a list of arguments or selectors.\n");
    }

    // calling argify with the wrong separators
    {
        std::stringstream ss;
        ss << "a,b{color:red}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        // no errors so far
        VERIFY_ERRORS("");

//std::cerr << "Result is: [" << *n << "]\n";

        std::stringstream out;
        out << *n;
        VERIFY_TREES(out.str(),

"LIST\n"
"  COMPONENT_VALUE\n"
"    IDENTIFIER \"a\"\n"
"    COMMA\n"
"    IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:false\n"
"      COMPONENT_VALUE\n"
"        IDENTIFIER \"color\"\n"
"        COLON\n"
"        IDENTIFIER \"red\"\n"

            );

        // Attempt to argify the list under each COMPONENT_VALUE using
        // the wrong type
        CATCH_REQUIRE(n->is(csspp::node_type_t::LIST));

        size_t const max_children(n->size());
        for(size_t idx(0); idx < max_children; ++idx)
        {
            csspp::node::pointer_t component_value(n->get_child(idx));
            CATCH_REQUIRE(component_value->is(csspp::node_type_t::COMPONENT_VALUE));

            for(csspp::node_type_t w(csspp::node_type_t::UNKNOWN);
                w <= csspp::node_type_t::max_type;
                w = static_cast<csspp::node_type_t>(static_cast<int>(w) + 1))
            {
                switch(w)
                {
                case csspp::node_type_t::COMMA:
                case csspp::node_type_t::DIVIDE:
                    continue;

                default:
                    break;

                }
                CATCH_REQUIRE_THROWS_AS(csspp::parser::argify(component_value, w), csspp::csspp_exception_logic);
            }
        }

        VERIFY_ERRORS("");
    }

    // no error left over
    VERIFY_ERRORS("");
}

// vim: ts=4 sw=4 et
