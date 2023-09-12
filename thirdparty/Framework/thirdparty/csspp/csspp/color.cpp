// Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved
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
 * \brief Implementation of the CSS Preprocessor color class.
 *
 * The CSS Preprocessor works on colors using the color class.
 *
 * A node that represents a color will generally be composed of one or
 * more tokens. In the end, it can transformed in one 32 bit value
 * representing the RGB and Alpha channels values from 0 to 255.
 *
 * The class also understands names and is able to convert a color
 * into in string as small as possible (i.e. compress colors.)
 */

// self
//
#include    "csspp/lexer.h"

#include    "csspp/exception.h"


// C++
//
#include    <cmath>
#include    <iomanip>
#include    <iostream>


// last include
//
//#include    <snapdev/poison.h>



namespace csspp
{

namespace
{

color_table_t const color_names[] =
{
    { 240,248,255,255, "aliceblue" },
    { 250,235,215,255, "antiquewhite" },
    {   0,255,255,255, "aqua" },
    { 127,255,212,255, "aquamarine" },
    { 240,255,255,255, "azure" },
    { 245,245,220,255, "beige" },
    { 255,228,196,255, "bisque" },
    {   0,  0,  0,255, "black" },
    { 255,235,205,255, "blanchedalmond" },
    {   0,  0,255,255, "blue" },
    { 138, 43,226,255, "blueviolet" },
    { 165, 42, 42,255, "brown" },
    { 222,184,135,255, "burlywood" },
    {  95,158,160,255, "cadetblue" },
    { 127,255,  0,255, "chartreuse" },
    { 210,105, 30,255, "chocolate" },
    { 255,127, 80,255, "coral" },
    { 100,149,237,255, "cornflowerblue" },
    { 255,248,220,255, "cornsilk" },
    { 220, 20, 60,255, "crimson" },
    {   0,255,255,255, "cyan" },
    {   0,  0,139,255, "darkblue" },
    {   0,139,139,255, "darkcyan" },
    { 184,134, 11,255, "darkgoldenrod" },
    { 169,169,169,255, "darkgray" },
    {   0,100,  0,255, "darkgreen" },
    { 169,169,169,255, "darkgrey" },
    { 189,183,107,255, "darkkhaki" },
    { 139,  0,139,255, "darkmagenta" },
    {  85,107, 47,255, "darkolivegreen" },
    { 255,140,  0,255, "darkorange" },
    { 153, 50,204,255, "darkorchid" },
    { 139,  0,  0,255, "darkred" },
    { 233,150,122,255, "darksalmon" },
    { 143,188,143,255, "darkseagreen" },
    {  72, 61,139,255, "darkslateblue" },
    {  47, 79, 79,255, "darkslategray" },
    {  47, 79, 79,255, "darkslategrey" },
    {   0,206,209,255, "darkturquoise" },
    { 148,  0,211,255, "darkviolet" },
    { 255, 20,147,255, "deeppink" },
    {   0,191,255,255, "deepskyblue" },
    { 105,105,105,255, "dimgray" },
    { 105,105,105,255, "dimgrey" },
    {  30,144,255,255, "dodgerblue" },
    { 178, 34, 34,255, "firebrick" },
    { 255,250,240,255, "floralwhite" },
    {  34,139, 34,255, "forestgreen" },
    { 255,  0,255,255, "fuchsia" },
    { 220,220,220,255, "gainsboro" },
    { 248,248,255,255, "ghostwhite" },
    { 255,215,  0,255, "gold" },
    { 218,165, 32,255, "goldenrod" },
    { 128,128,128,255, "gray" },
    {   0,128,  0,255, "green" },
    { 173,255, 47,255, "greenyellow" },
    { 128,128,128,255, "grey" },
    { 240,255,240,255, "honeydew" },
    { 255,105,180,255, "hotpink" },
    { 205, 92, 92,255, "indianred" },
    {  75,  0,130,255, "indigo" },
    { 255,255,240,255, "ivory" },
    { 240,230,140,255, "khaki" },
    { 230,230,250,255, "lavender" },
    { 255,240,245,255, "lavenderblush" },
    { 124,252,  0,255, "lawngreen" },
    { 255,250,205,255, "lemonchiffon" },
    { 173,216,230,255, "lightblue" },
    { 240,128,128,255, "lightcoral" },
    { 224,255,255,255, "lightcyan" },
    { 250,250,210,255, "lightgoldenrodyellow" },
    { 211,211,211,255, "lightgray" },
    { 144,238,144,255, "lightgreen" },
    { 211,211,211,255, "lightgrey" },
    { 255,182,193,255, "lightpink" },
    { 255,160,122,255, "lightsalmon" },
    {  32,178,170,255, "lightseagreen" },
    { 135,206,250,255, "lightskyblue" },
    { 119,136,153,255, "lightslategray" },
    { 119,136,153,255, "lightslategrey" },
    { 176,196,222,255, "lightsteelblue" },
    { 255,255,224,255, "lightyellow" },
    {   0,255,  0,255, "lime" },
    {  50,205, 50,255, "limegreen" },
    { 250,240,230,255, "linen" },
    { 255,  0,255,255, "magenta" },
    { 128,  0,  0,255, "maroon" },
    { 102,205,170,255, "mediumaquamarine" },
    {   0,  0,205,255, "mediumblue" },
    { 186, 85,211,255, "mediumorchid" },
    { 147,112,219,255, "mediumpurple" },
    {  60,179,113,255, "mediumseagreen" },
    { 123,104,238,255, "mediumslateblue" },
    {   0,250,154,255, "mediumspringgreen" },
    {  72,209,204,255, "mediumturquoise" },
    { 199, 21,133,255, "mediumvioletred" },
    {  25, 25,112,255, "midnightblue" },
    { 245,255,250,255, "mintcream" },
    { 255,228,225,255, "mistyrose" },
    { 255,228,181,255, "moccasin" },
    { 255,222,173,255, "navajowhite" },
    {   0,  0,128,255, "navy" },
    { 253,245,230,255, "oldlace" },
    { 128,128,  0,255, "olive" },
    { 107,142, 35,255, "olivedrab" },
    { 255,165,  0,255, "orange" },
    { 255, 69,  0,255, "orangered" },
    { 218,112,214,255, "orchid" },
    { 238,232,170,255, "palegoldenrod" },
    { 152,251,152,255, "palegreen" },
    { 175,238,238,255, "paleturquoise" },
    { 219,112,147,255, "palevioletred" },
    { 255,239,213,255, "papayawhip" },
    { 255,218,185,255, "peachpuff" },
    { 205,133, 63,255, "peru" },
    { 255,192,203,255, "pink" },
    { 221,160,221,255, "plum" },
    { 176,224,230,255, "powderblue" },
    { 128,  0,128,255, "purple" },
    { 255,  0,  0,255, "red" },
    { 188,143,143,255, "rosybrown" },
    {  65,105,225,255, "royalblue" },
    { 139, 69, 19,255, "saddlebrown" },
    { 250,128,114,255, "salmon" },
    { 244,164, 96,255, "sandybrown" },
    {  46,139, 87,255, "seagreen" },
    { 255,245,238,255, "seashell" },
    { 160, 82, 45,255, "sienna" },
    { 192,192,192,255, "silver" },
    { 135,206,235,255, "skyblue" },
    { 106, 90,205,255, "slateblue" },
    { 112,128,144,255, "slategray" },
    { 112,128,144,255, "slategrey" },
    { 255,250,250,255, "snow" },
    {   0,255,127,255, "springgreen" },
    {  70,130,180,255, "steelblue" },
    { 210,180,140,255, "tan" },
    {   0,128,128,255, "teal" },
    { 216,191,216,255, "thistle" },
    { 255, 99, 71,255, "tomato" },
    {   0,  0,  0,  0, "transparent" },
    {  64,224,208,255, "turquoise" },
    { 238,130,238,255, "violet" },
    { 245,222,179,255, "wheat" },
    { 255,255,255,255, "white" },
    { 245,245,245,255, "whitesmoke" },
    { 255,255,  0,255, "yellow" },
    { 154,205, 50,255, "yellowgreen" }
};

size_t const color_names_size(sizeof(color_names) / sizeof(color_names[0]));

byte_component_t color_component_to_byte(color_component_t c)
{
    // first clamp
    if(c >= static_cast<color_component_t>(1.0))
    {
        return 255;
    }
    else if(c <= static_cast<color_component_t>(0.0))
    {
        return 0;
    }

    // in range, compute the corresponding value
    return static_cast<byte_component_t>(c * static_cast<color_component_t>(255.0) + static_cast<color_component_t>(0.5));
}

} // no name namespace

void color::set_color(uint32_t const rgba)
{
    set_color((rgba >>  0) & 255,
              (rgba >>  8) & 255,
              (rgba >> 16) & 255,
              (rgba >> 24) & 255);
}

void color::set_color(byte_component_t red, byte_component_t green, byte_component_t blue, byte_component_t alpha)
{
    f_red   = static_cast<color_component_t>(red)   / static_cast<color_component_t>(255.0);
    f_green = static_cast<color_component_t>(green) / static_cast<color_component_t>(255.0);
    f_blue  = static_cast<color_component_t>(blue)  / static_cast<color_component_t>(255.0);
    f_alpha = static_cast<color_component_t>(alpha) / static_cast<color_component_t>(255.0);
}

void color::set_color(int red, int green, int blue, int alpha)
{
    f_red   = static_cast<color_component_t>(red)   / static_cast<color_component_t>(255.0);
    f_green = static_cast<color_component_t>(green) / static_cast<color_component_t>(255.0);
    f_blue  = static_cast<color_component_t>(blue)  / static_cast<color_component_t>(255.0);
    f_alpha = static_cast<color_component_t>(alpha) / static_cast<color_component_t>(255.0);
}

void color::set_color(unsigned int red, unsigned int green, unsigned int blue, unsigned int alpha)
{
    f_red   = static_cast<color_component_t>(red)   / static_cast<color_component_t>(255.0);
    f_green = static_cast<color_component_t>(green) / static_cast<color_component_t>(255.0);
    f_blue  = static_cast<color_component_t>(blue)  / static_cast<color_component_t>(255.0);
    f_alpha = static_cast<color_component_t>(alpha) / static_cast<color_component_t>(255.0);
}

void color::set_color(color_component_t red, color_component_t green, color_component_t blue, color_component_t alpha)
{
    f_red   = red;
    f_green = green;
    f_blue  = blue;
    f_alpha = alpha;
}

void color::set_color(double red, double green, double blue, double alpha)
{
    f_red   = red;
    f_green = green;
    f_blue  = blue;
    f_alpha = alpha;
}

bool color::set_color(std::string const & name, bool name_only)
{
    // we assume that the color name was written as an identifier and thus
    // it is already in lowercase and can be compared directly
    {
        size_t i(0);
        size_t j(color_names_size);
#ifdef _DEBUG
        for(size_t k(1); k < color_names_size; ++k)
        {
            if(std::string(color_names[k - 1].f_name) >= color_names[k].f_name)
            {
                throw csspp_exception_logic("colors are not in alphabetical order, our binary search would break."); // LCOV_EXCL_LINE
            }
        }
#endif
        while(i < j)
        {
            size_t const p((j - i) / 2 + i);
            if(color_names[p].f_name == name)
            {
                set_color(color_names[p].f_red,
                          color_names[p].f_green,
                          color_names[p].f_blue,
                          color_names[p].f_alpha);
                return true;
            }
            if(color_names[p].f_name < name)
            {
                i = p + 1;
            }
            else
            {
                j = p;
            }
        }
    }

    if(!name_only)
    {

        // if not a direct name, it has to be a valid hexadecimal string
        // of 3 or 6 digits
        if(name.length() == 3)
        {
            if(lexer::is_hex(name[0])
            && lexer::is_hex(name[1])
            && lexer::is_hex(name[2]))
            {
                set_color(lexer::hex_to_dec(name[0]) * 0x11,
                          lexer::hex_to_dec(name[1]) * 0x11,
                          lexer::hex_to_dec(name[2]) * 0x11,
                          255);
                return true;
            }
        }
        else if(name.length() == 6)
        {
            if(lexer::is_hex(name[0])
            && lexer::is_hex(name[1])
            && lexer::is_hex(name[2])
            && lexer::is_hex(name[3])
            && lexer::is_hex(name[4])
            && lexer::is_hex(name[5]))
            {
                set_color(lexer::hex_to_dec(name[0]) * 16 + lexer::hex_to_dec(name[1]),
                          lexer::hex_to_dec(name[2]) * 16 + lexer::hex_to_dec(name[3]),
                          lexer::hex_to_dec(name[4]) * 16 + lexer::hex_to_dec(name[5]),
                          255);
                return true;
            }
        }

    }

    return false;
}

void color::set_hsl(color_component_t hue, color_component_t saturation, color_component_t lightness, color_component_t alpha)
{
    // see: http://en.wikipedia.org/wiki/HSL_and_HSV
    double const chroma = (1.0 - fabs(2.0 * lightness - 1.0)) * saturation;
    double const h1 = 6.0 * fmod(hue, M_PI * 2.0) / (M_PI * 2.0);
    double const x = chroma * (1.0 - fabs(fmod(h1, 2) - 1.0));
    double r, g, b;

    if(h1 >= 0.0 && h1 < 1.0)
    {
        r = chroma;
        g = x;
        b = 0.0;
    }
    else if(h1 >= 1.0 && h1 < 2.0)
    {
        r = x;
        g = chroma;
        b = 0.0;
    }
    else if(h1 >= 2.0 && h1 < 3.0)
    {
        r = 0.0;
        g = chroma;
        b = x;
    }
    else if(h1 >= 3.0 && h1 < 4.0)
    {
        r = 0.0;
        g = x;
        b = chroma;
    }
    else if(h1 >= 4.0 && h1 < 5.0)
    {
        r = x;
        g = 0.0;
        b = chroma;
    }
    else if(h1 >= 5.0 && h1 < 6.0)
    {
        r = chroma;
        g = 0;
        b = x;
    }
    else
    {
        // negative hues generally end up here
        r = 0.0;
        g = 0.0;
        b = 0.0;
    }

    double const m(lightness - 0.5 * chroma);

    f_red   = static_cast<color_component_t>(r + m);
    f_green = static_cast<color_component_t>(g + m);
    f_blue  = static_cast<color_component_t>(b + m);

    f_alpha = alpha;
}

void color::get_hsl(color_component_t & hue, color_component_t & saturation, color_component_t & lightness, color_component_t & alpha) const
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
    // see: http://en.wikipedia.org/wiki/HSL_and_HSV
    double const maximum(std::max(f_red, std::max(f_green, f_blue)));
    double const minimum(std::min(f_red, std::min(f_green, f_blue)));
    double const chroma(maximum - minimum);

//std::cerr << " min/max/C " << minimum << "/" << maximum << "/" << chroma;

    // warning: we define chroma in degrees just as set_hsl() expects
    //          but most of our angles are kept in radians otherwise
    if(chroma == 0.0)
    {
        // this is really "undefined"...
        // absolutely any angle would work just fine
        hue = 0.0;
    }
    else if(maximum == f_red)
    {
        hue = (f_green - f_blue) / chroma;
    }
    else if(maximum == f_green)
    {
        hue = (f_blue - f_red) / chroma + 2.0;
    }
    else //if(maximum == f_blue)
    {
        hue = (f_red - f_green) / chroma + 4.0;
    }
    if(hue < 0)
    {
        hue += 6.0;
    }
    hue = fmod(hue, 6.0) / 6.0 * M_PI * 2.0;

    lightness = (minimum + maximum) / 2.0;

    if(lightness == 0.0 || lightness == 1.0)
    {
        saturation = 0.0;
    }
    else
    {
        saturation = chroma / (1.0 - fabs(lightness * 2.0 - 1.0));
    }

    alpha = f_alpha;
#pragma GCC diagnostic pop
}

rgba_color_t color::get_color() const
{
    // the color_component_to_byte() function clamps each color component
    // between 0 and 1 before converting it to a uint8_t
    return (color_component_to_byte(f_red)   <<  0)
         | (color_component_to_byte(f_green) <<  8)
         | (color_component_to_byte(f_blue)  << 16)
         | (color_component_to_byte(f_alpha) << 24);
}

void color::get_color(color_component_t & red, color_component_t & green, color_component_t & blue, color_component_t & alpha) const
{
    red   = f_red;
    green = f_green;
    blue  = f_blue;
    alpha = f_alpha;
}

bool color::is_solid() const
{
    return f_alpha >= 1.0;
}

bool color::is_transparent() const
{
    return f_alpha <= 0.0;
}

std::string color::to_string() const
{
    rgba_color_t const col(get_color());

    byte_component_t const red  ((col >>  0) & 255);
    byte_component_t const green((col >>  8) & 255);
    byte_component_t const blue ((col >> 16) & 255);
    byte_component_t const alpha((col >> 24) & 255);

    if(alpha == 255) // is_solid()
    {
        // we will have to do some testing, but with compression, always
        // use #RGB or #RRGGBB is probably better than saving 1 character
        // here or there... (because compression is all about repeated
        // bytes that can be saved in a small number of bits.)
        switch(col)
        {
        case (192UL << 0) | (192UL << 8) | (192UL << 16) | (255UL << 24): // #c0c0c0
            return "silver";

        case (128UL << 0) | (128UL << 8) | (128UL << 16) | (255UL << 24): // #808080
            return "gray";

        case (128UL << 0) | (  0UL << 8) | (  0UL << 16) | (255UL << 24): // #800000
            return "maroon";

        case (255UL << 0) | (  0UL << 8) | (  0UL << 16) | (255UL << 24): // #ff0000
            return "red";

        case (128UL << 0) | (  0UL << 8) | (128UL << 16) | (255UL << 24): // #800080
            return "purple";

        case (  0UL << 0) | (128UL << 8) | (  0UL << 16) | (255UL << 24): // #008000
            return "green";

        case (  0UL << 0) | (255UL << 8) | (  0UL << 16) | (255UL << 24): // #00ff00
            return "lime"; // == #0f0

        case (128UL << 0) | (128UL << 8) | (  0UL << 16) | (255UL << 24): // #808000
            return "olive";

        case (  0UL << 0) | (  0UL << 8) | (128UL << 16) | (255UL << 24): // #000080
            return "navy";

        case (  0UL << 0) | (  0UL << 8) | (255UL << 16) | (255UL << 24): // #0000ff
            return "blue"; // == #00f

        case (  0UL << 0) | (128UL << 8) | (128UL << 16) | (255UL << 24): // #008080
            return "teal";

        case (  0UL << 0) | (255UL << 8) | (255UL << 16) | (255UL << 24): // #00ffff
            return "aqua"; // == #0ff

        }

        // output #RGB or #RRGGBB
        std::stringstream ss;
        ss << std::hex << "#";

        if(((red   >> 4) == (red   & 15))
        && ((green >> 4) == (green & 15))
        && ((blue  >> 4) == (blue  & 15)))
        {
            // we can use the smaller format (#RGB)
            ss << static_cast<int>(red & 15) << static_cast<int>(green & 15) << static_cast<int>(blue & 15);
            return ss.str();
        }

        // cannot simplify (#RRGGBB)
        ss << std::setfill('0')
           << std::setw(2) << static_cast<int>(red)
           << std::setw(2) << static_cast<int>(green)
           << std::setw(2) << static_cast<int>(blue);
        return ss.str();
    }
    else
    {
        if(alpha == 0) // is_transparent()
        {
            return "transparent"; // rgba(0,0,0,0)
        }

        // when alpha is specified we have to use the rgba() function
        safe_precision_t safe(2);
        return "rgba(" + std::to_string(static_cast<int>(red))
                 + "," + std::to_string(static_cast<int>(green))
                 + "," + std::to_string(static_cast<int>(blue))
                 + "," + decimal_number_to_string(f_alpha, true) + ")";
    }
}

} // namespace csspp

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
