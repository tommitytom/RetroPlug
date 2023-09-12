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
 * \brief Test the color.cpp file.
 *
 * This test runs a battery of tests agains the color.cpp
 * implementation to ensure full coverage.
 */

// csspp
//
#include    <csspp/color.h>

#include    <csspp/exception.h>
#include    <csspp/lexer.h>


// self
//
#include    "catch_main.h"


// C++
//
#include    <iomanip>
#include    <sstream>


// C
//
#include    <math.h>
#include    <string.h>


// last include
//
#include    <snapdev/poison.h>



namespace
{

char const test_colors[] =
"aliceblue  #f0f8ff  240,248,255\n"
"antiquewhite  #faebd7  250,235,215\n"
"aqua  #00ffff  0,255,255\n"
"aquamarine  #7fffd4  127,255,212\n"
"azure  #f0ffff  240,255,255\n"
"beige  #f5f5dc  245,245,220\n"
"bisque  #ffe4c4  255,228,196\n"
"black  #000000  0,0,0\n"
"blanchedalmond  #ffebcd  255,235,205\n"
"blue  #0000ff  0,0,255\n"
"blueviolet  #8a2be2  138,43,226\n"
"brown  #a52a2a  165,42,42\n"
"burlywood  #deb887  222,184,135\n"
"cadetblue  #5f9ea0  95,158,160\n"
"chartreuse  #7fff00  127,255,0\n"
"chocolate  #d2691e  210,105,30\n"
"coral  #ff7f50  255,127,80\n"
"cornflowerblue  #6495ed  100,149,237\n"
"cornsilk  #fff8dc  255,248,220\n"
"crimson  #dc143c  220,20,60\n"
"cyan  #00ffff  0,255,255\n"
"darkblue  #00008b  0,0,139\n"
"darkcyan  #008b8b  0,139,139\n"
"darkgoldenrod  #b8860b  184,134,11\n"
"darkgray  #a9a9a9  169,169,169\n"
"darkgreen  #006400  0,100,0\n"
"darkgrey  #a9a9a9  169,169,169\n"
"darkkhaki  #bdb76b  189,183,107\n"
"darkmagenta  #8b008b  139,0,139\n"
"darkolivegreen  #556b2f  85,107,47\n"
"darkorange  #ff8c00  255,140,0\n"
"darkorchid  #9932cc  153,50,204\n"
"darkred  #8b0000  139,0,0\n"
"darksalmon  #e9967a  233,150,122\n"
"darkseagreen  #8fbc8f  143,188,143\n"
"darkslateblue  #483d8b  72,61,139\n"
"darkslategray  #2f4f4f  47,79,79\n"
"darkslategrey  #2f4f4f  47,79,79\n"
"darkturquoise  #00ced1  0,206,209\n"
"darkviolet  #9400d3  148,0,211\n"
"deeppink  #ff1493  255,20,147\n"
"deepskyblue  #00bfff  0,191,255\n"
"dimgray  #696969  105,105,105\n"
"dimgrey  #696969  105,105,105\n"
"dodgerblue  #1e90ff  30,144,255\n"
"firebrick  #b22222  178,34,34\n"
"floralwhite  #fffaf0  255,250,240\n"
"forestgreen  #228b22  34,139,34\n"
"fuchsia  #ff00ff  255,0,255\n"
"gainsboro  #dcdcdc  220,220,220\n"
"ghostwhite  #f8f8ff  248,248,255\n"
"gold  #ffd700  255,215,0\n"
"goldenrod  #daa520  218,165,32\n"
"gray  #808080  128,128,128\n"
"green  #008000  0,128,0\n"
"greenyellow  #adff2f  173,255,47\n"
"grey  #808080  128,128,128\n"
"honeydew  #f0fff0  240,255,240\n"
"hotpink  #ff69b4  255,105,180\n"
"indianred  #cd5c5c  205,92,92\n"
"indigo  #4b0082  75,0,130\n"
"ivory  #fffff0  255,255,240\n"
"khaki  #f0e68c  240,230,140\n"
"lavender  #e6e6fa  230,230,250\n"
"lavenderblush  #fff0f5  255,240,245\n"
"lawngreen  #7cfc00  124,252,0\n"
"lemonchiffon  #fffacd  255,250,205\n"
"lightblue  #add8e6  173,216,230\n"
"lightcoral  #f08080  240,128,128\n"
"lightcyan  #e0ffff  224,255,255\n"
"lightgoldenrodyellow  #fafad2  250,250,210\n"
"lightgray  #d3d3d3  211,211,211\n"
"lightgreen  #90ee90  144,238,144\n"
"lightgrey  #d3d3d3  211,211,211\n"
"lightpink  #ffb6c1  255,182,193\n"
"lightsalmon  #ffa07a  255,160,122\n"
"lightseagreen  #20b2aa  32,178,170\n"
"lightskyblue  #87cefa  135,206,250\n"
"lightslategray  #778899  119,136,153\n"
"lightslategrey  #778899  119,136,153\n"
"lightsteelblue  #b0c4de  176,196,222\n"
"lightyellow  #ffffe0  255,255,224\n"
"lime  #00ff00  0,255,0\n"
"limegreen  #32cd32  50,205,50\n"
"linen  #faf0e6  250,240,230\n"
"magenta  #ff00ff  255,0,255\n"
"maroon  #800000  128,0,0\n"
"mediumaquamarine  #66cdaa  102,205,170\n"
"mediumblue  #0000cd  0,0,205\n"
"mediumorchid  #ba55d3  186,85,211\n"
"mediumpurple  #9370db  147,112,219\n"
"mediumseagreen  #3cb371  60,179,113\n"
"mediumslateblue  #7b68ee  123,104,238\n"
"mediumspringgreen  #00fa9a  0,250,154\n"
"mediumturquoise  #48d1cc  72,209,204\n"
"mediumvioletred  #c71585  199,21,133\n"
"midnightblue  #191970  25,25,112\n"
"mintcream  #f5fffa  245,255,250\n"
"mistyrose  #ffe4e1  255,228,225\n"
"moccasin  #ffe4b5  255,228,181\n"
"navajowhite  #ffdead  255,222,173\n"
"navy  #000080  0,0,128\n"
"oldlace  #fdf5e6  253,245,230\n"
"olive  #808000  128,128,0\n"
"olivedrab  #6b8e23  107,142,35\n"
"orange  #ffa500  255,165,0\n"
"orangered  #ff4500  255,69,0\n"
"orchid  #da70d6  218,112,214\n"
"palegoldenrod  #eee8aa  238,232,170\n"
"palegreen  #98fb98  152,251,152\n"
"paleturquoise  #afeeee  175,238,238\n"
"palevioletred  #db7093  219,112,147\n"
"papayawhip  #ffefd5  255,239,213\n"
"peachpuff  #ffdab9  255,218,185\n"
"peru  #cd853f  205,133,63\n"
"pink  #ffc0cb  255,192,203\n"
"plum  #dda0dd  221,160,221\n"
"powderblue  #b0e0e6  176,224,230\n"
"purple  #800080  128,0,128\n"
"red  #ff0000  255,0,0\n"
"rosybrown  #bc8f8f  188,143,143\n"
"royalblue  #4169e1  65,105,225\n"
"saddlebrown  #8b4513  139,69,19\n"
"salmon  #fa8072  250,128,114\n"
"sandybrown  #f4a460  244,164,96\n"
"seagreen  #2e8b57  46,139,87\n"
"seashell  #fff5ee  255,245,238\n"
"sienna  #a0522d  160,82,45\n"
"silver  #c0c0c0  192,192,192\n"
"skyblue  #87ceeb  135,206,235\n"
"slateblue  #6a5acd  106,90,205\n"
"slategray  #708090  112,128,144\n"
"slategrey  #708090  112,128,144\n"
"snow  #fffafa  255,250,250\n"
"springgreen  #00ff7f  0,255,127\n"
"steelblue  #4682b4  70,130,180\n"
"tan  #d2b48c  210,180,140\n"
"teal  #008080  0,128,128\n"
"thistle  #d8bfd8  216,191,216\n"
"tomato  #ff6347  255,99,71\n"
"turquoise  #40e0d0  64,224,208\n"
"violet  #ee82ee  238,130,238\n"
"wheat  #f5deb3  245,222,179\n"
"white  #ffffff  255,255,255\n"
"whitesmoke  #f5f5f5  245,245,245\n"
"yellow  #ffff00  255,255,0\n"
"yellowgreen  #9acd32  154,205,50\n"
;

} // no name namespace


CATCH_TEST_CASE("Invalid colors", "[color] [invalid]")
{
    csspp::color c;

    CATCH_REQUIRE(!c.set_color("", false));
    CATCH_REQUIRE(!c.set_color("1", false));
    CATCH_REQUIRE(!c.set_color("12", false));
    CATCH_REQUIRE(!c.set_color("1234", false));
    CATCH_REQUIRE(!c.set_color("12345", false));
    CATCH_REQUIRE(!c.set_color("1234567", false));
    CATCH_REQUIRE(!c.set_color("12345G", false));
    CATCH_REQUIRE(!c.set_color("/01234", false));
    CATCH_REQUIRE(!c.set_color("/05", false));
    CATCH_REQUIRE(!c.set_color("44G", false));
    CATCH_REQUIRE(!c.set_color("#333", false));
    CATCH_REQUIRE(!c.set_color("unknown", false));

    CATCH_REQUIRE(!c.set_color("", true));
    CATCH_REQUIRE(!c.set_color("1", true));
    CATCH_REQUIRE(!c.set_color("12", true));
    CATCH_REQUIRE(!c.set_color("123", true)); // this would work with false
    CATCH_REQUIRE(!c.set_color("1234", true));
    CATCH_REQUIRE(!c.set_color("12345", true));
    CATCH_REQUIRE(!c.set_color("123456", true)); // this would work with false
    CATCH_REQUIRE(!c.set_color("1234567", true));
    CATCH_REQUIRE(!c.set_color("12345G", true));
    CATCH_REQUIRE(!c.set_color("/01234", true));
    CATCH_REQUIRE(!c.set_color("/05", true));
    CATCH_REQUIRE(!c.set_color("44G", true));
    CATCH_REQUIRE(!c.set_color("#333", true));
    CATCH_REQUIRE(!c.set_color("unknown", true));
}

CATCH_TEST_CASE("Default color", "[color] [default]")
{
    csspp::color c;
    CATCH_REQUIRE(c.get_color() == 0xFF000000U);

    csspp::color_component_t cr;
    csspp::color_component_t cg;
    csspp::color_component_t cb;
    csspp::color_component_t ca;
    c.get_color(cr, cg, cb, ca);
    CATCH_REQUIRE(SNAP_CATCH2_NAMESPACE::nearly_equal(cr, 0.0f, 0.0f));
    CATCH_REQUIRE(SNAP_CATCH2_NAMESPACE::nearly_equal(cg, 0.0f, 0.0f));
    CATCH_REQUIRE(SNAP_CATCH2_NAMESPACE::nearly_equal(cb, 0.0f, 0.0f));
    CATCH_REQUIRE(SNAP_CATCH2_NAMESPACE::nearly_equal(ca, 1.0f, 0.0f));
}

CATCH_TEST_CASE("Verify #XXX colors", "[color] [parse]")
{
    for(int i(0); i < 0x1000; ++i)
    {
        csspp::byte_component_t const r((i >> 0) & 15);
        csspp::byte_component_t const g((i >> 4) & 15);
        csspp::byte_component_t const b((i >> 8) & 15);

        std::stringstream ss;
        ss << std::hex << static_cast<int>(r) << static_cast<int>(g) << static_cast<int>(b);

        csspp::color c;
        CATCH_REQUIRE(c.set_color(ss.str(), false));

        CATCH_REQUIRE(c.get_color() == (r * 0x11) + (g * 0x1100) + (b * 0x110000) + 0xFF000000);
        CATCH_REQUIRE(c.is_solid());
        CATCH_REQUIRE(!c.is_transparent());

        csspp::color_component_t cr;
        csspp::color_component_t cg;
        csspp::color_component_t cb;
        csspp::color_component_t ca;
        c.get_color(cr, cg, cb, ca);
        CATCH_REQUIRE((fabs(cr - (r * 0x11U) / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cg - (g * 0x11U) / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cb - (b * 0x11U) / 255.0) < 0.0001));
        CATCH_REQUIRE(SNAP_CATCH2_NAMESPACE::nearly_equal(ca, 1.0f, 0.0f));
    }
}

CATCH_TEST_CASE("Verify #XXXXXX colors", "[color] [parse]")
{
    for(int i(0); i < 0x1000000; i += rand() % 2000 + 1)
    {
        csspp::byte_component_t const r((i >>  0) & 255);
        csspp::byte_component_t const g((i >>  8) & 255);
        csspp::byte_component_t const b((i >> 16) & 255);

        std::stringstream ss;
        ss << std::hex << std::setfill('0')
                       << std::setw(2) << static_cast<int>(r)
                       << std::setw(2) << static_cast<int>(g)
                       << std::setw(2) << static_cast<int>(b);

        csspp::color c;
        CATCH_REQUIRE(c.set_color(ss.str(), false));

        CATCH_REQUIRE(c.get_color() == (r * 0x1) + (g * 0x100) + (b * 0x10000) + 0xFF000000);
        CATCH_REQUIRE(c.is_solid());
        CATCH_REQUIRE(!c.is_transparent());

        csspp::color_component_t cr;
        csspp::color_component_t cg;
        csspp::color_component_t cb;
        csspp::color_component_t ca;
        c.get_color(cr, cg, cb, ca);
        CATCH_REQUIRE((fabs(cr - r / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cg - g / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cb - b / 255.0) < 0.0001));
        CATCH_REQUIRE(SNAP_CATCH2_NAMESPACE::nearly_equal(ca, 1.0f, 0.0f));
    }
}

CATCH_TEST_CASE("Verify named colors", "[color] [parse]")
{
    // we got raw data here, parse it and make sure it is equal to
    // what our library produces
    const char *s(test_colors);
    while(*s != '\0')
    {
        std::string name;
        std::string hash;

        // read the name
        for(; *s != ' '; ++s)
        {
            name += *s;
        }

        // skip the spaces
        for(; *s == ' '; ++s);

        // read the hash
        CATCH_REQUIRE(*s == '#');
        // we do ++s at the start to skip the '#'
        for(++s; *s != ' '; ++s)
        {
            hash += *s;
        }

        // covert the hash to a color
        csspp::byte_component_t const r(csspp::lexer::hex_to_dec(hash[0]) * 16 + csspp::lexer::hex_to_dec(hash[1]));
        csspp::byte_component_t const g(csspp::lexer::hex_to_dec(hash[2]) * 16 + csspp::lexer::hex_to_dec(hash[3]));
        csspp::byte_component_t const b(csspp::lexer::hex_to_dec(hash[4]) * 16 + csspp::lexer::hex_to_dec(hash[5]));

        // skip to the next line (or EOS)
        for(; *s != '\n'; ++s);
        ++s; // skip the '\n'

        csspp::color c;
        CATCH_REQUIRE(c.set_color(name, false));
        CATCH_REQUIRE(c.is_solid());
        CATCH_REQUIRE(!c.is_transparent());
        CATCH_REQUIRE(c.get_color() == (r << 0) + (g << 8) + (b << 16) + 0xFF000000);

        csspp::color_component_t cr;
        csspp::color_component_t cg;
        csspp::color_component_t cb;
        csspp::color_component_t ca;
        c.get_color(cr, cg, cb, ca);
        CATCH_REQUIRE((fabs(cr - r / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cg - g / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cb - b / 255.0) < 0.0001));
        CATCH_REQUIRE(SNAP_CATCH2_NAMESPACE::nearly_equal(ca, 1.0f, 0.0f));
    }

    // verify the transparent color
    {
        csspp::color c;
        CATCH_REQUIRE(c.set_color("transparent", false));
        CATCH_REQUIRE(!c.is_solid());
        CATCH_REQUIRE(c.is_transparent());
        CATCH_REQUIRE(c.get_color() == 0);

        csspp::color_component_t cr;
        csspp::color_component_t cg;
        csspp::color_component_t cb;
        csspp::color_component_t ca;
        c.get_color(cr, cg, cb, ca);
        CATCH_REQUIRE(SNAP_CATCH2_NAMESPACE::nearly_equal(cr, 0.0f, 0.0f));
        CATCH_REQUIRE(SNAP_CATCH2_NAMESPACE::nearly_equal(cg, 0.0f, 0.0f));
        CATCH_REQUIRE(SNAP_CATCH2_NAMESPACE::nearly_equal(cb, 0.0f, 0.0f));
        CATCH_REQUIRE(SNAP_CATCH2_NAMESPACE::nearly_equal(ca, 0.0f, 0.0f));
    }
}

CATCH_TEST_CASE("Direct colors", "[color]")
{
    for(int i(0); i < 1000; ++i)
    {
        csspp::byte_component_t const r(rand() & 255);
        csspp::byte_component_t const g(rand() & 255);
        csspp::byte_component_t const b(rand() & 255);
        csspp::byte_component_t const a(rand() & 255);

        csspp::color c;
        c.set_color(r, g, b, a);
        CATCH_REQUIRE(c.get_color() == (r * 0x1U) + (g * 0x100U) + (b * 0x10000U) + (a * 0x1000000U));

        if(a == 255U)
        {
            CATCH_REQUIRE(c.is_solid());
        }
        else
        {
            CATCH_REQUIRE(!c.is_solid());
        }
        if(a == 0U)
        {
            CATCH_REQUIRE(c.is_transparent());
        }
        else
        {
            CATCH_REQUIRE(!c.is_transparent());
        }

        csspp::color_component_t cr;
        csspp::color_component_t cg;
        csspp::color_component_t cb;
        csspp::color_component_t ca;
        c.get_color(cr, cg, cb, ca);
        CATCH_REQUIRE((fabs(cr - r / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cg - g / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cb - b / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(ca - a / 255.0) < 0.0001));

        // try again with one uint32_t value
        c.set_color((r * 0x1U) + (g * 0x100U) + (b * 0x10000U) + (a * 0x1000000U));
        CATCH_REQUIRE(c.get_color() == (r * 0x1U) + (g * 0x100U) + (b * 0x10000U) + (a * 0x1000000U));

        if(a == 255U)
        {
            CATCH_REQUIRE(c.is_solid());
        }
        else
        {
            CATCH_REQUIRE(!c.is_solid());
        }
        if(a == 0U)
        {
            CATCH_REQUIRE(c.is_transparent());
        }
        else
        {
            CATCH_REQUIRE(!c.is_transparent());
        }

        c.get_color(cr, cg, cb, ca);
        CATCH_REQUIRE((fabs(cr - r / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cg - g / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cb - b / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(ca - a / 255.0) < 0.0001));

        // try again with component values
        c.set_color(static_cast<csspp::color_component_t>(r / 255.0),
                    static_cast<csspp::color_component_t>(g / 255.0),
                    static_cast<csspp::color_component_t>(b / 255.0),
                    static_cast<csspp::color_component_t>(a / 255.0));
        CATCH_REQUIRE(c.get_color() == (r * 0x1U) + (g * 0x100U) + (b * 0x10000U) + (a * 0x1000000U));

        if(a == 255U)
        {
            CATCH_REQUIRE(c.is_solid());
        }
        else
        {
            CATCH_REQUIRE(!c.is_solid());
        }
        if(a == 0U)
        {
            CATCH_REQUIRE(c.is_transparent());
        }
        else
        {
            CATCH_REQUIRE(!c.is_transparent());
        }

        c.get_color(cr, cg, cb, ca);
        CATCH_REQUIRE((fabs(cr - r / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cg - g / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cb - b / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(ca - a / 255.0) < 0.0001));

        // try again with doubles
        c.set_color(r / 255.0, g / 255.0, b / 255.0, a / 255.0);
        CATCH_REQUIRE(c.get_color() == (r * 0x1U) + (g * 0x100U) + (b * 0x10000U) + (a * 0x1000000U));

        if(a == 255U)
        {
            CATCH_REQUIRE(c.is_solid());
        }
        else
        {
            CATCH_REQUIRE(!c.is_solid());
        }
        if(a == 0U)
        {
            CATCH_REQUIRE(c.is_transparent());
        }
        else
        {
            CATCH_REQUIRE(!c.is_transparent());
        }

        c.get_color(cr, cg, cb, ca);
        CATCH_REQUIRE((fabs(cr - r / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cg - g / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cb - b / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(ca - a / 255.0) < 0.0001));

        // make sure no clamping happens on the floating point numbers
        c.set_color(r / -255.0, g + 265.0, b / 255.0, a / 255.0);
        CATCH_REQUIRE(c.get_color() == (0 * 0x1U) + (0xFF00U) + (b * 0x10000U) + (a * 0x1000000U));

        if(a == 255U)
        {
            CATCH_REQUIRE(c.is_solid());
        }
        else
        {
            CATCH_REQUIRE(!c.is_solid());
        }
        if(a == 0U)
        {
            CATCH_REQUIRE(c.is_transparent());
        }
        else
        {
            CATCH_REQUIRE(!c.is_transparent());
        }

        c.get_color(cr, cg, cb, ca);
        CATCH_REQUIRE((fabs(cr - r / -255.0) < 0.0001));
        CATCH_REQUIRE((fabs(cg - (g + 265.0)) < 0.0001));
        CATCH_REQUIRE((fabs(cb - b / 255.0) < 0.0001));
        CATCH_REQUIRE((fabs(ca - a / 255.0) < 0.0001));
    }
}

CATCH_TEST_CASE("HSLA colors", "[color]")
{
    csspp::color c;

    // red
    c.set_hsl(0.0 * M_PI / 180.0, 1.0, 0.5, 1.0);
    CATCH_REQUIRE(c.get_color() == 0xFF0000FFU);

    // lime
    c.set_hsl(120.0 * M_PI / 180.0, 1.0, 0.5, 1.0);
    CATCH_REQUIRE(c.get_color() == 0xFF00FF00U);

    // darkgreen
    c.set_hsl(120.0 * M_PI / 180.0, 1.0, 0.25, 1.0);
    CATCH_REQUIRE(c.get_color() == 0xFF008000U);

    // lightgreen
    c.set_hsl(120.0 * M_PI / 180.0, 1.0, 0.75, 1.0);
    CATCH_REQUIRE(c.get_color() == 0xFF80FF80U);

    // blue
    c.set_hsl(120.0 * M_PI / 180.0, 1.0, 0.5, 0.5);
    CATCH_REQUIRE(c.get_color() == 0x8000FF00U);

    // orange
    c.set_hsl(30.0 * M_PI / 180.0, 1.0, 0.5, 0.5);
    CATCH_REQUIRE(c.get_color() == 0x800080FFU);

    for(int i(0); i < 100; ++i)
    {
        // black
        c.set_hsl(rand() % 3600 * M_PI / 180.0, 0.0, 0.0, 0.5);
        CATCH_REQUIRE(c.get_color() == 0x80000000U);

        // white
        c.set_hsl(rand() % 3600 * M_PI / 180.0, 0.0, 1.0, 0.5);
        CATCH_REQUIRE(c.get_color() == 0x80FFFFFFU);

        // gray
        c.set_hsl(rand() % 3600 * M_PI / 180.0, 0.0, 0.5, 0.5);
        CATCH_REQUIRE(c.get_color() == 0x80808080U);
    }

    // ...
    c.set_hsl(61.8 * M_PI / 180.0, 0.638, 0.393, 0.25);
    CATCH_REQUIRE(c.get_color() == 0x4024A4A0U);

    // ...
    c.set_hsl(162.4 * M_PI / 180.0, 0.779, 0.447, 0.25);
    CATCH_REQUIRE(c.get_color() == 0x4097CB19U);

    // ...
    c.set_hsl(180.0 * M_PI / 180.0, 1.0, 0.75, 0.75);
    CATCH_REQUIRE(c.get_color() == 0xBFFFFF80U);

    // ...
    c.set_hsl(251.1 * M_PI / 180.0, 0.832, 0.511, 0.75);
    CATCH_REQUIRE(c.get_color() == 0xBFEA1B41U);

    // ...
    // helper computation to make sure the assembler tests work as expected
    c.set_color(0xf7, 0xd0, 0xcf, 0xff);
    {
        csspp::color_component_t hue;
        csspp::color_component_t saturation;
        csspp::color_component_t lightness;
        csspp::color_component_t alpha;
        c.get_hsl(hue, saturation, lightness, alpha);

        // add 180deg to the hue
        c.set_hsl(hue + M_PI, saturation, lightness, alpha);
        CATCH_REQUIRE(c.get_color() == 0xFFF7F6CFU);

        // darken by 3%
        c.set_hsl(hue, saturation, lightness - 0.03, alpha);
        CATCH_REQUIRE(c.get_color() == 0xFFC2C3F5U);

        // desaturate by 5%
        c.set_hsl(hue, saturation - 0.05, lightness, alpha);
        CATCH_REQUIRE(c.get_color() == 0xFFD0D1F6U);
    }

    // ...
    // helper computation to make sure the unary expression tests work as expected
    c.set_color(0x56, 0xaf, 0x9b, 0xff);
    {
        csspp::color_component_t hue;
        csspp::color_component_t saturation;
        csspp::color_component_t lightness;
        csspp::color_component_t alpha;
        c.get_hsl(hue, saturation, lightness, alpha);

        // add 180deg to the hue
        c.set_hsl(hue + M_PI, saturation, lightness, alpha);
        CATCH_REQUIRE(c.get_color() == 0xFF6A56AFU);
    }

    // ...
    c.set_hsl(-251.1 * M_PI / 180.0, 0.832, 0.511, 0.75);
    CATCH_REQUIRE(c.get_color() == 0xBF1B1B1BU);
    {
        csspp::color_component_t hue;
        csspp::color_component_t saturation;
        csspp::color_component_t lightness;
        csspp::color_component_t alpha;
        c.get_hsl(hue, saturation, lightness, alpha);
    }

    for(int i(0); i < 20000; ++i)
    {
        int red;
        int green;
        int blue;
        int alpha(rand() & 255);
        // to make sure we hit all the lines we check the get_hsl()
        // with a few specific colors
        switch(i)
        {
        case 0:
            // check black
            red   = 0;
            green = 0;
            blue  = 0;
            break;

        case 1:
            // check white
            red   = 255;
            green = 255;
            blue  = 255;
            break;

        case 2:
            // check red
            red   = 255;
            green = 0;
            blue  = 0;
            break;

        case 3:
            // check green
            red   = 0;
            green = 255;
            blue  = 0;
            break;

        case 4:
            // check blue
            red   = 0;
            green = 0;
            blue  = 255;
            break;

        case 5:
            // check gray
            red   = 128;
            green = 128;
            blue  = 128;
            break;

        default:
            red   = rand() & 255;
            green = rand() & 255;
            blue  = rand() & 255;
            break;

        }
        c.set_color(red, green, blue, alpha);

        csspp::color_component_t hue;
        csspp::color_component_t saturation;
        csspp::color_component_t lightness;
        csspp::color_component_t hsl_alpha;
        c.get_hsl(hue, saturation, lightness, hsl_alpha);
        CATCH_REQUIRE(fabs(hsl_alpha - alpha / 255.0) < 0.00001);

        // setting those values back must resolve to the
        // exact same RGBA as what we defined earlier
        c.set_hsl(hue, saturation, lightness, hsl_alpha);

        csspp::rgba_color_t col(c.get_color());
        csspp::byte_component_t r((col >>  0) & 255);
        csspp::byte_component_t g((col >>  8) & 255);
        csspp::byte_component_t b((col >> 16) & 255);
        csspp::byte_component_t a((col >> 24) & 255);
        CATCH_REQUIRE(r == red  );
        CATCH_REQUIRE(g == green);
        CATCH_REQUIRE(b == blue );
        CATCH_REQUIRE(a == alpha);

//        if(red - green >= 10
//        || red - blue >= 10
//        || green - blue >= 10)
//        {
//            c.adjust_hue(180.0);// * M_PI / 180.0);
//            csspp::color_component_t new_hue;
//            csspp::color_component_t new_saturation;
//            csspp::color_component_t new_lightness;
//            c.get_hsl(new_hue, new_saturation, new_lightness, hsl_alpha);
//            CATCH_REQUIRE(fabs(hsl_alpha - alpha / 255.0) < 0.00001);
//
//            // hue is not considered valid when RGB are equal
//            double hue_diff(fabs(fabs(hue - new_hue) - 180.0));
////if(hue_diff > 3.0)
////{
////std::cerr << "rgb: " << static_cast<int>(r) << ", " << static_cast<int>(g) << ", " << static_cast<int>(b)
////          << " old hue: " << hue << " & new hue: " << new_hue << " diff = " << fabs(new_hue - hue) << " delta " << fabs(fabs(new_hue - hue) - 180.0) << "\n";
////}
//            CATCH_REQUIRE(hue_diff <= 0.0001);
//
//            // restore the color to test the adjust_hue() function
//            c.set_hsl(hue, saturation, lightness, hsl_alpha);
//            c.adjust_saturation(0.2); // +20%
//            c.get_hsl(new_hue, new_saturation, new_lightness, hsl_alpha);
//            double saturation_diff(fabs(fabs(saturation - new_saturation) - 0.2));
////if(saturation_diff > 0.0001)
////{
////std::cerr << "old saturation: " << saturation << " -> " << new_saturation << " diff = " << fabs(new_saturation - saturation) << "\n";
////}
//            CATCH_REQUIRE(saturation_diff < 0.0001);
//
//            // restore the color to test the adjust_lightness() function
//            c.set_hsl(hue, saturation, lightness, hsl_alpha);
//            c.adjust_lightness(0.2); // +20%
//            c.get_hsl(new_hue, new_saturation, new_lightness, hsl_alpha);
//            double lightness_diff(fabs(fabs(lightness - new_lightness) - 0.2));
////if(lightness_diff > 0.0001)
////{
////std::cerr << "old lightness: " << lightness << " -> " << new_lightness << " diff = " << fabs(new_lightness - lightness) << "\n";
////}
//            CATCH_REQUIRE(lightness_diff < 0.0001);
//        }
    }
}

CATCH_TEST_CASE("Color to string", "[color] [output]")
{
    csspp::color c;

    // a few special cases
    {
        c.set_color(static_cast<csspp::byte_component_t>(0xC0),
                    static_cast<csspp::byte_component_t>(0xC0),
                    static_cast<csspp::byte_component_t>(0xC0),
                    static_cast<csspp::byte_component_t>(0xFF));
        CATCH_REQUIRE(c.to_string() == "silver");

        c.set_color(static_cast<csspp::byte_component_t>(0x80),
                    static_cast<csspp::byte_component_t>(0x80),
                    static_cast<csspp::byte_component_t>(0x80),
                    static_cast<csspp::byte_component_t>(0xFF));
        CATCH_REQUIRE(c.to_string() == "gray");

        c.set_color(static_cast<csspp::byte_component_t>(0x80),
                    static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0xFF));
        CATCH_REQUIRE(c.to_string() == "maroon");

        c.set_color(static_cast<csspp::byte_component_t>(0xFF),
                    static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0xFF));
        CATCH_REQUIRE(c.to_string() == "red");

        c.set_color(static_cast<csspp::byte_component_t>(0x80),
                    static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0x80),
                    static_cast<csspp::byte_component_t>(0xFF));
        CATCH_REQUIRE(c.to_string() == "purple");

        c.set_color(static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0x80),
                    static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0xFF));
        CATCH_REQUIRE(c.to_string() == "green");

        c.set_color(static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0xFF),
                    static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0xFF));
        CATCH_REQUIRE(c.to_string() == "lime");

        c.set_color(static_cast<csspp::byte_component_t>(0x80),
                    static_cast<csspp::byte_component_t>(0x80),
                    static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0xFF));
        CATCH_REQUIRE(c.to_string() == "olive");

        c.set_color(static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0x80),
                    static_cast<csspp::byte_component_t>(0xFF));
        CATCH_REQUIRE(c.to_string() == "navy");

        c.set_color(static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0xFF),
                    static_cast<csspp::byte_component_t>(0xFF));
        CATCH_REQUIRE(c.to_string() == "blue");

        c.set_color(static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0x80),
                    static_cast<csspp::byte_component_t>(0x80),
                    static_cast<csspp::byte_component_t>(0xFF));
        CATCH_REQUIRE(c.to_string() == "teal");

        c.set_color(static_cast<csspp::byte_component_t>(0x00),
                    static_cast<csspp::byte_component_t>(0xFF),
                    static_cast<csspp::byte_component_t>(0xFF),
                    static_cast<csspp::byte_component_t>(0xFF));
        CATCH_REQUIRE(c.to_string() == "aqua");
    }

    // #XXX cases -- none are special cases
    for(uint32_t i(0); i < 0x1000; ++i)
    {
        c.set_color(static_cast<csspp::byte_component_t>(((i >> 0) & 15) * 0x11),
                    static_cast<csspp::byte_component_t>(((i >> 4) & 15) * 0x11),
                    static_cast<csspp::byte_component_t>(((i >> 8) & 15) * 0x11),
                    static_cast<csspp::byte_component_t>(0xFF));
        switch(c.get_color())
        {
        case (255U << 0) | (0U << 8) | (0U << 16) | (255U << 24):
            CATCH_REQUIRE(c.to_string() == "red");
            break;

        case (0U << 0) | (255U << 8) | (0U << 16) | (255U << 24):
            CATCH_REQUIRE(c.to_string() == "lime");
            break;

        case (0U << 0) | (0U << 8) | (255U << 16) | (255U << 24):
            CATCH_REQUIRE(c.to_string() == "blue");
            break;

        case (0U << 0) | (255U << 8) | (255U << 16) | (255U << 24):
            CATCH_REQUIRE(c.to_string() == "aqua");
            break;

        default:
            {
                std::stringstream ss;
                ss << std::hex << "#"
                    << static_cast<int>((i >> 0) & 15)
                    << static_cast<int>((i >> 4) & 15)
                    << static_cast<int>((i >> 8) & 15);
                CATCH_REQUIRE(c.to_string() == ss.str());
            }
            break;

        }
    }

    // #XXXXXX cases -- make sure to skip the special cases
    for(uint32_t i(0); i < 10000; ++i)
    {
        csspp::byte_component_t r, g, b;

        bool valid(false);
        do
        {
            r = rand() % 255;
            g = rand() % 255;
            b = rand() % 255;
            switch((r << 16) | (g << 8) | (b << 0))
            {
            case 0xc0c0c0:
            case 0x808080:
            case 0x800000:
            case 0xff0000:
            case 0x800080:
            case 0x008000:
            case 0x00ff00:
            case 0x808000:
            case 0x000080:
            case 0x0000ff:
            case 0x008080:
            case 0x00ffff:
                // since we randomize the colors we hit these very few case
                // rather rarely (we could test all the colors too, but that
                // 16 million so a tad bit slow, that being said, I tried
                // once just in case)
                valid = false; // LCOV_EXCL_LINE
                break;         // LCOV_EXCL_LINE

            default:
                valid = (r >> 4) != (r & 15)
                     || (g >> 4) != (g & 15)
                     || (b >> 4) != (b & 15);
                break;

            }
        }
        while(!valid);

        c.set_color(r, g, b, static_cast<csspp::byte_component_t>(0xFF));

        std::stringstream ss;
        ss << "#" << std::hex << std::setfill('0')
                  << std::setw(2) << static_cast<int>(r)
                  << std::setw(2) << static_cast<int>(g)
                  << std::setw(2) << static_cast<int>(b);
        CATCH_REQUIRE(c.to_string() == ss.str());
    }

    // transparent special case
    {
        c.set_color(static_cast<csspp::byte_component_t>(0),
                    static_cast<csspp::byte_component_t>(0),
                    static_cast<csspp::byte_component_t>(0),
                    static_cast<csspp::byte_component_t>(0));
        CATCH_REQUIRE(c.to_string() == "transparent");
    }

    // rgba()
    for(uint32_t i(0); i < 0x1000000U; i += rand() % 500 + 1)
    {
        csspp::byte_component_t const r((i >>  0) & 255);
        csspp::byte_component_t const g((i >>  0) & 255);
        csspp::byte_component_t const b((i >>  0) & 255);
        csspp::byte_component_t const a((rand() % 254) + 1);  // avoid 0 and 255

        c.set_color(r, g, b, a);
        csspp::safe_precision_t precision(2);
        std::stringstream ss;
        ss << "rgba(" << static_cast<int>(r)
               << "," << static_cast<int>(g)
               << "," << static_cast<int>(b)
               << "," << csspp::decimal_number_to_string(static_cast<int>(a) / 255.0, true)
               << ")";
        CATCH_REQUIRE(c.to_string() == ss.str());
    }
}

// vim: ts=4 sw=4 et
