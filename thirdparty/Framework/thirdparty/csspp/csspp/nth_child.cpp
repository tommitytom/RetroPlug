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
 * \brief Implementation of the CSS Preprocessor nth-child handling.
 *
 * The CSS Preprocessor, while verifying a grammar, will check each and every
 * parameter. This includes the syntax of the An+B in a function such as
 * the ':nth-child(...)' one.
 *
 * \section grammar Grammar
 *
 * The grammar allows for one or two signed integers, the first followed
 * by an 'n' (or 'N' since we are case insensitive). Both of those numbers
 * can be omitted. When an integer is omitted, it defaults to 0.
 *
 * Spaces can appear anywhere, we just remove them all. In CSS 3, spaces
 * are not allowed between the sign and INTEGER of the first integer, nor
 * between the first integer and the 'n' character.
 *
 * nth: optionally-signed-integer 'n' signed-integer
 *    | optionally-signed-integer 'n'
 *    | optionally-signed-integer
 *    | 'ODD'
 *    | 'EVEN'
 *
 * optionally-signed-integer: INTEGER
 *                          | signed-integer
 *
 * signed-integer: '+' INTEGER
 *               | '-' INTEGER
 */

#include    "csspp/nth_child.h"

#include    <cstdio>
#include    <iostream>

namespace csspp
{

namespace
{

int const g_shift_a = 0;
int const g_shift_b = 32;

} // no name namespace

nth_child::nth_child(integer_t an_plus_b)
    : f_a(static_cast<repeat_integer_t>(an_plus_b >> g_shift_a))
    , f_b(static_cast<repeat_integer_t>(an_plus_b >> g_shift_b))
{
}

nth_child::nth_child(repeat_integer_t a, repeat_integer_t b)
    : f_a(a)
    , f_b(b)
{
}

// got an error if "get_error().empty()" is false
nth_child::nth_child(std::string const & an_plus_b)
{
    parse(an_plus_b);
}

void nth_child::set_a(repeat_integer_t a)
{
    f_a = a;
}

void nth_child::set_b(repeat_integer_t b)
{
    f_b = b;
}

repeat_integer_t nth_child::get_a() const
{
    return f_a;
}

repeat_integer_t nth_child::get_b() const
{
    return f_b;
}

integer_t nth_child::get_nth() const
{
    return ((static_cast<integer_t>(f_a) & 0xFFFFFFFF) << g_shift_a) | ((static_cast<integer_t>(f_b) & 0xFFFFFFFF) << g_shift_b);
}

std::string nth_child::get_error() const
{
    return f_error;
}

bool nth_child::parse(std::string const & an_plus_b)
{
    class in_t
    {
    public:
        enum class token_t
        {
            EOF_TOKEN,
            ODD,
            EVEN,
            INTEGER,
            PLUS,
            MINUS,
            N,
            UNKNOWN
        };

        in_t(std::string in)
            : f_in(in)
        {
        }

        wide_char_t getc()
        {
            if(f_unget != '\0')
            {
                wide_char_t const c(f_unget);
                f_unget = '\0';
                return c;
            }

            if(f_pos < f_in.length())
            {
                // TBD: as far as I know, we have no need to test
                //      whether we have UTF-8 characters in this
                //      case
                wide_char_t const c(f_in[f_pos]);
                ++f_pos;
                return c;
            }
            return EOF;
        }

        void ungetc(wide_char_t c)
        {
            f_unget = c;
        }

        token_t token(integer_t & value)
        {
            value = 0;

            // read the next non-space character
            wide_char_t c;
            do
            {
                c = getc();
            }
            while(c == ' ');

            switch(c)
            {
            case EOF:
                return token_t::EOF_TOKEN;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                for(;;)
                {
                    value = value * 10 + c - '0';
                    c = getc();
                    if(c < '0' || c > '9')
                    {
                        ungetc(c);
                        return token_t::INTEGER;
                    }
                }
                /*NOTREACHED*/

            case 'n':
            case 'N':
                return token_t::N;

            case 'e':
            case 'E':
                c = getc();
                if(c == 'v' || c == 'V')
                {
                    c = getc();
                    if(c == 'e' || c == 'E')
                    {
                        c = getc();
                        if(c == 'n' || c == 'N')
                        {
                            return token_t::EVEN;
                        }
                    }
                }
                break;

            case 'o':
            case 'O':
                c = getc();
                if(c == 'd' || c == 'D')
                {
                    c = getc();
                    if(c == 'd' || c == 'D')
                    {
                        return token_t::ODD;
                    }
                }
                break;

            case '+':
                return token_t::PLUS;

            case '-':
                return token_t::MINUS;

            }

            return token_t::UNKNOWN;
        }

    private:
        std::string     f_in;
        size_t          f_pos = 0;
        wide_char_t     f_unget = '\0';
    };

    f_error = "";

    in_t in(an_plus_b);

    integer_t first_int(0);
    in_t::token_t token(in.token(first_int));

    // EVEN
    if(token == in_t::token_t::EVEN)
    {
        f_a = 2;
        f_b = 0;
        if(in.token(first_int) == in_t::token_t::EOF_TOKEN)
        {
            return true;
        }
        f_error = "'even' cannot be followed by anything else in an An+B expression.";
        return false;
    }

    // ODD
    if(token == in_t::token_t::ODD)
    {
        f_a = 2;
        f_b = 1;
        if(in.token(first_int) == in_t::token_t::EOF_TOKEN)
        {
            return true;
        }
        f_error = "'odd' cannot be followed by anything else in an An+B expression.";
        return false;
    }

    // optional sign (+ or -)
    int first_sign(1);
    if(token == in_t::token_t::PLUS)
    {
        token = in.token(first_int);
    }
    else if(token == in_t::token_t::MINUS)
    {
        token = in.token(first_int);
        first_sign = -1;
    }

    // in this case we must have an integer
    if(token != in_t::token_t::INTEGER)
    {
        f_error = "In an An+B expression, we expect an optional signed followed by a number or 'even' or 'odd'.";
        return false;
    }

    integer_t second_int(0);
    token = in.token(second_int);
    if(token == in_t::token_t::EOF_TOKEN)
    {
        // just a number, this is 'B' by itself
        f_a = 0;
        f_b = first_int * first_sign;
        return true;
    }

    if(token != in_t::token_t::N)
    {
        // first number must be followed by 'n'
        f_error = "The first number has to be followed by the 'n' character.";
        return false;
    }

    int second_sign(1);
    token = in.token(second_int);
    if(token == in_t::token_t::PLUS)
    {
        // ...
    }
    else if(token == in_t::token_t::MINUS)
    {
        second_sign = -1;
    }
    else
    {
        // the sign is mandatory between An and B
        f_error = "A sign (+ or -) is expected between the 'An' and the 'B' parts in 'An+B'.";
        return false;
    }

    token = in.token(second_int);
    if(token != in_t::token_t::INTEGER)
    {
        // B has to be an integer
        f_error = "The value B must be a valid integer in 'An+B'.";
        return false;
    }

    f_a = first_int * first_sign;
    f_b = second_int * second_sign;

    if(in.token(second_int) == in_t::token_t::EOF_TOKEN)
    {
        return true;
    }

    f_error = "An 'An+B' expression cannot be followed by anything else.";
    return false;
}

std::string nth_child::to_string() const
{
    // TODO: add code to reduce these because B should always be between 0 and A - 1
    //       and A can always be positive (but I need to do some testing.)
    if(f_a == 2 && f_b == 0)
    {
        // 2n+0 or "2n", which is shorter than "even"
        return "2n";
    }
    if(f_a == 2 && f_b == 1)
    {
        // 2n+1 or "odd"
        return "odd";
    }

    // we return f_b in priority because of a and b are zero "0"
    // is shorter than "0n"
    if(f_a == 0)
    {
        return std::to_string(f_b);
    }

    if(f_b == 0)
    {
        return std::to_string(f_a) + "n";
    }

    return std::to_string(f_a) + "n" + (f_b >= 0 ? "+" : "") + std::to_string(f_b);
}

} // namespace csspp

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
