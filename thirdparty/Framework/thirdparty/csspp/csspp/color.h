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
#pragma once

// C++ lib
//
#include    <cstdint>
#include    <string>


namespace csspp
{

typedef float           color_component_t;
typedef uint8_t         byte_component_t;
typedef uint32_t        rgba_color_t;

struct color_table_t
{
    byte_component_t        f_red   = 0;
    byte_component_t        f_green = 0;
    byte_component_t        f_blue  = 0;
    byte_component_t        f_alpha = 0;
    char const *            f_name  = nullptr;
};

class color
{
public:
    void                    set_color(rgba_color_t const rgba);
    void                    set_color(byte_component_t red, byte_component_t green, byte_component_t blue, byte_component_t alpha);
    void                    set_color(unsigned int red, unsigned int green, unsigned int blue, unsigned int alpha);
    void                    set_color(int red, int green, int blue, int alpha);
    void                    set_color(color_component_t red, color_component_t green, color_component_t blue, color_component_t alpha);
    void                    set_color(double red, double green, double blue, double alpha);
    void                    set_hsl(color_component_t h, color_component_t s, color_component_t l, color_component_t alpha);
    void                    get_hsl(color_component_t & hue, color_component_t & saturation, color_component_t & lightness, color_component_t & alpha) const;
    void                    adjust_hue(float hue);
    void                    adjust_saturation(float change);
    void                    adjust_lightness(float change);
    bool                    set_color(std::string const & name, bool name_only);
    rgba_color_t            get_color() const;
    void                    get_color(color_component_t & red, color_component_t & green, color_component_t & blue, color_component_t & alpha) const;

    bool                    is_solid() const;
    bool                    is_transparent() const;

    std::string             to_string() const;

private:
    color_component_t       f_red   = 0.0;
    color_component_t       f_green = 0.0;
    color_component_t       f_blue  = 0.0;
    color_component_t       f_alpha = 1.0;
};

} // namespace csspp
// vim: ts=4 sw=4 et
