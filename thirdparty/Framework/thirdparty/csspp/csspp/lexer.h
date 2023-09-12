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

// self
//
#include    "csspp/node.h"


namespace csspp
{

class lexer
{
public:
    typedef std::shared_ptr<lexer>  pointer_t;

                            lexer(std::istream & in, position const & pos);

    node::pointer_t         next_token();

    wide_char_t             mbtowc(char const * mb);
    void                    wctomb(wide_char_t const wc, char * mb, size_t max_length);
    std::string             wctomb(wide_char_t const wc);

    static bool constexpr is_space(wide_char_t c)
    {
        // when called '\r' and '\f' were already converted to '\n'
        return c == ' '
            || c == '\t'
            || c == '\n';
    }

    static bool constexpr is_non_printable(wide_char_t c)
    {
        return c == 0x00
            || c == 0x08
            || c == 0x0B
            || (c >= 0x0E && c <= 0x1F)
            || c == 0x7F
            || c == 0xFFFD;
    }

    static bool constexpr is_variable(wide_char_t c)
    {
        // part of identifier except escape
        return (c >= '0' && c <= '9')
            || (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z')
            || c == '-'
            || c == '_';
    }

    static bool constexpr is_identifier(wide_char_t c)
    {
        // part of identifier except escape
        return (c >= '0' && c <= '9')
            || (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z')
            || c == '-'
            || c == '_'
            || c >= 0x80;
    }

    static bool constexpr is_start_identifier(wide_char_t c)
    {
        // start except for the possible escape
        return (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z')
            || (c >= 0x80 && c != 0xFFFD)
            || c == '_';
    }

    static bool constexpr is_digit(wide_char_t c)
    {
        return c >= '0' && c <= '9';
    }

    static bool constexpr is_hex(wide_char_t c)
    {
        return (c >= '0' && c <= '9')
            || (c >= 'A' && c <= 'F')
            || (c >= 'a' && c <= 'f');
    }

    static bool constexpr is_hash_character(wide_char_t c)
    {
        return (c >= '0' && c <= '9')
            || (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z')
            || c == '-'
            || c == '_'
            || (c >= 0x80 && c != 0xFFFD);
    }

    static int              hex_to_dec(wide_char_t c);

private:
    static size_t const     UNGETSIZ = 16;

    wide_char_t             getc();
    void                    ungetc(wide_char_t c);

    wide_char_t             escape();
    node::pointer_t         identifier(wide_char_t c);
    node::pointer_t         number(wide_char_t c);
    std::string             string(wide_char_t const quote);
    node::pointer_t         comment(bool c_comment);
    node::pointer_t         unicode_range(wide_char_t c);
    node::pointer_t         hash();
    node::pointer_t         variable(wide_char_t c);

    std::istream &          f_in;
    position                f_position;
    position                f_start_position;
    wide_char_t             f_ungetc[UNGETSIZ];
    size_t                  f_ungetc_pos = 0;
};

} // namespace csspp
// vim: ts=4 sw=4 et
