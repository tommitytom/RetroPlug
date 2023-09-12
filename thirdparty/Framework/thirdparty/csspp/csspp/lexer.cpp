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
 * \brief Implementation of the CSS Preprocessor lexer.
 *
 * The CSS Preprocessor lexer is based on the CSS 3 lexer with extensions to
 * also support the SASS syntax. For example, a lone '&' is supported by
 * our lexer as a reference to the outter selectors in order to write an
 * expression such as:
 *
 * \code{.scss}
 *      a {
 *          color: #00f;  // blue when not hovered
 *
 *          &:hover {
 *              color: #f0f;  // purple when hovered
 *          }
 *      }
 * \endcode
 *
 * There are a few other extensions too, such as support for C++ comments.
 *
 * \sa \ref lexer_rules
 */

#include    "csspp/lexer.h"

#include    "csspp/exception.h"
#include    "csspp/unicode_range.h"

#include    <cmath>
#include    <cstdio>
#include    <iostream>

namespace csspp
{

lexer::lexer(std::istream & in, position const & pos)
    : f_in(in)
    , f_position(pos)
    , f_start_position(pos)
{
}

node::pointer_t lexer::next_token()
{
    for(;;)
    {
        f_start_position = f_position;

        wide_char_t const c(getc());

//std::cerr << "--- got char " << std::hex << " 0x" << c << "\n";

        switch(c)
        {
        case EOF: // CSS uses 0xFFFD to represent EOF, we do not
            return node::pointer_t(new node(node_type_t::EOF_TOKEN, f_start_position));

        case '=':
            {
                wide_char_t const n(getc());
                if(n != '=')
                {
                    ungetc(n);
                }
                else
                {
                    // really warn about it?
                    error::instance() << f_position
                            << "we accepted '==' instead of '=' in an expression, you probably want to change the operator to just '=', though."
                            << error_mode_t::ERROR_WARNING;
                }

                return node::pointer_t(new node(node_type_t::EQUAL, f_start_position));
            }

        case ',':
            return node::pointer_t(new node(node_type_t::COMMA, f_start_position));

        case ':':
            {
                wide_char_t const n(getc());
                if(n == '=')
                {
                    return node::pointer_t(new node(node_type_t::ASSIGNMENT, f_start_position));
                }
                ungetc(n);
                return node::pointer_t(new node(node_type_t::COLON, f_start_position));
            }

        case ';':
            return node::pointer_t(new node(node_type_t::SEMICOLON, f_start_position));

        case '!':
            {
                wide_char_t const n(getc());
                if(n == '=')
                {
                    return node::pointer_t(new node(node_type_t::NOT_EQUAL, f_start_position));
                }
                ungetc(n);
                return node::pointer_t(new node(node_type_t::EXCLAMATION, f_start_position));
            }

        case '?':
            return node::pointer_t(new node(node_type_t::CONDITIONAL, f_start_position));

        case '>':
            {
                wide_char_t const n(getc());
                if(n == '=')
                {
                    return node::pointer_t(new node(node_type_t::GREATER_EQUAL, f_start_position));
                }
                ungetc(n);
                return node::pointer_t(new node(node_type_t::GREATER_THAN, f_start_position));
            }

        case '(':
            return node::pointer_t(new node(node_type_t::OPEN_PARENTHESIS, f_start_position));

        case ')':
            return node::pointer_t(new node(node_type_t::CLOSE_PARENTHESIS, f_start_position));

        case '[':
            return node::pointer_t(new node(node_type_t::OPEN_SQUAREBRACKET, f_start_position));

        case ']':
            return node::pointer_t(new node(node_type_t::CLOSE_SQUAREBRACKET, f_start_position));

        case '{':
            return node::pointer_t(new node(node_type_t::OPEN_CURLYBRACKET, f_start_position));

        case '}':
            return node::pointer_t(new node(node_type_t::CLOSE_CURLYBRACKET, f_start_position));

        case '.':
            {
                wide_char_t const n(getc());
                ungetc(n);
                if(n >= '0' && n <= '9')
                {
                    // found a decimal number
                    return number(c);
                }
                return node::pointer_t(new node(node_type_t::PERIOD, f_start_position));
            }
            //NOTREACHED

        case '&':
            {
                wide_char_t const n(getc());
                if(n == '&')
                {
                    return node::pointer_t(new node(node_type_t::AND, f_start_position));
                }
                ungetc(n);
                return node::pointer_t(new node(node_type_t::REFERENCE, f_start_position));
            }

        case '<':
            {
                wide_char_t const n(getc());
                if(n == '!')
                {
                    wide_char_t const p(getc());
                    if(p == '-')
                    {
                        wide_char_t const l(getc());
                        if(l == '-')
                        {
                            return node::pointer_t(new node(node_type_t::CDO, f_start_position));
                        }
                        ungetc(l);
                    }
                    ungetc(p);
                }
                else if(n == '=')
                {
                    return node::pointer_t(new node(node_type_t::LESS_EQUAL, f_start_position));
                }
                ungetc(n);
                return node::pointer_t(new node(node_type_t::LESS_THAN, f_start_position));
            }
            break;

        case '+':
            {
                wide_char_t const n(getc());
                if(n >= '0' && n <= '9')
                {
                    // found a positive number
                    ungetc(n);
                    return number(c);
                }
                if(n == '.')
                {
                    wide_char_t const p(getc());
                    if(p >= '0' && p <= '9')
                    {
                        // found a negative decimal number
                        ungetc(p);
                        ungetc(n);
                        return number(c);
                    }
                    ungetc(p);
                }
                ungetc(n);
                return node::pointer_t(new node(node_type_t::ADD, f_start_position));
            }
            //NOTREACHED

        case '-':
            {
                wide_char_t const n(getc());
                if(n >= '0' && n <= '9')
                {
                    // found a negative number
                    ungetc(n);
                    return number(c);
                }
                if(n == '.')
                {
                    wide_char_t const p(getc());
                    if(p >= '0' && p <= '9')
                    {
                        // found a negative decimal number
                        ungetc(p);
                        ungetc(n);
                        return number(c);
                    }
                    ungetc(p);
                }
                if(n == '-')
                {
                    wide_char_t const p(getc());
                    if(p == '>')
                    {
                        return node::pointer_t(new node(node_type_t::CDC, f_start_position));
                    }
                    ungetc(p);
                    ungetc(n);
                    // an identifier cannot start with two dashes in a row
                    return node::pointer_t(new node(node_type_t::SUBTRACT, f_start_position));
                }
                ungetc(n);
                if((is_identifier(n) || n == '\\')
                && (n < '0' || n > '9'))
                {
                    return identifier(c);
                }
                return node::pointer_t(new node(node_type_t::SUBTRACT, f_start_position));
            }
            //NOTREACHED

        case '^':
            {
                wide_char_t const n(getc());
                if(n == '=')
                {
                    return node::pointer_t(new node(node_type_t::PREFIX_MATCH, f_start_position));
                }
                ungetc(n);
                // character necessary by itself?
            }
            break;

        case '$':
            {
                wide_char_t const n(getc());
                if(n == '=')
                {
                    return node::pointer_t(new node(node_type_t::SUFFIX_MATCH, f_start_position));
                }
                if(is_variable(n))
                {
                    return variable(n);
                }
                ungetc(n);
                return node::pointer_t(new node(node_type_t::DOLLAR, f_start_position));
            }
            //NOTREACHED

        case '~':
            {
                wide_char_t const n(getc());
                if(n == '=')
                {
                    return node::pointer_t(new node(node_type_t::INCLUDE_MATCH, f_start_position));
                }
                ungetc(n);
                return node::pointer_t(new node(node_type_t::PRECEDED, f_start_position));
            }
            break;

        case '*':
            {
                wide_char_t const n(getc());
                if(n == '=')
                {
                    return node::pointer_t(new node(node_type_t::SUBSTRING_MATCH, f_start_position));
                }
                if(n == '*')
                {
                    return node::pointer_t(new node(node_type_t::POWER, f_start_position));
                }
                ungetc(n);
                return node::pointer_t(new node(node_type_t::MULTIPLY, f_start_position));
            }
            //NOTREACHED

        case '|':
            {
                wide_char_t const n(getc());
                if(n == '|')
                {
                    return node::pointer_t(new node(node_type_t::COLUMN, f_start_position));
                }
                if(n == '=')
                {
                    return node::pointer_t(new node(node_type_t::DASH_MATCH, f_start_position));
                }
                ungetc(n);
                // the pipe is used as a scoping operator "<name>|<name>"
                return node::pointer_t(new node(node_type_t::SCOPE, f_start_position));
            }
            break;

        case '"':
        case '\'':
            {
                std::string const str(string(c));
                node::pointer_t n(new node(node_type_t::STRING, f_start_position));
                n->set_string(str);
                return n;
            }
            //NOTREACHED

        case '/':
            {
                wide_char_t const n(getc());
                if(n == '*')
                {
                    node::pointer_t cn(comment(true));
                    if(cn)
                    {
                        return cn;
                    }
                    // silently let it go
                    continue;
                }
                else if(n == '/')
                {
                    node::pointer_t cn(comment(false));
                    if(cn)
                    {
                        error::instance()
                                << f_start_position
                                << "C++ comments should not be preserved as they are not supported by most CSS parsers."
                                << error_mode_t::ERROR_WARNING;
                        return cn;
                    }
                    // silently let it go
                    continue;
                }
                ungetc(n);
                return node::pointer_t(new node(node_type_t::DIVIDE, f_start_position));
            }

        case ' ':
        case '\t':
        case '\n':
        //case '\r': -- not needed since \r is transformed into \n by getc()
        case '\f':
            {
                // white spaces are signification in some places and
                // definitively not acceptable in others so we have to
                // create a token for them... this is important for the
                // parser, not so much for the output
                for(;;)
                {
                    wide_char_t const n(getc());
                    if(!is_space(n))
                    {
                        ungetc(n);
                        return node::pointer_t(new node(node_type_t::WHITESPACE, f_start_position));
                    }
                }
            }
            //NOTREACHED

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
            return number(c);

        case '#':
            {
                node::pointer_t n(hash());
                if(n)
                {
                    return n;
                }
                continue;
            }

        case '%':
            {
                wide_char_t const n(getc());
                if(!is_start_identifier(n))
                {
                    return node::pointer_t(new node(node_type_t::MODULO, f_start_position));
                }
                ungetc(n);
            }
#if __cplusplus >= 201700
            [[fallthrough]];
#endif
        case '\\':
        case '@':
            {
                node::pointer_t n(identifier(c));
                if(!n->is(node_type_t::EOF_TOKEN))
                {
                    return n;
                }
                // EOF_TOKEN is not returned, we may not be at the end of
                // the input stream, but that identifier was empty; the
                // identifier() function already generated an error
                continue;
            }
            break;

        case 'u':
        case 'U':
            {
                wide_char_t const n(getc());
                if(n == '+')
                {
                    wide_char_t const d(getc());
                    if(is_hex(d) || d == '?')
                    {
                        // U+<number>
                        return unicode_range(d);
                    }
                    ungetc(d);
                }
                ungetc(n);
                return identifier(c);
            }
            //NOTREACHED

        default:
            if(is_start_identifier(c))
            {
                return identifier(c);
            }
            break;

        }

        error::instance() << f_start_position << "invalid input character: U+" << error_mode_t::ERROR_HEX << c << "." << error_mode_t::ERROR_ERROR;
    }
}

wide_char_t lexer::mbtowc(char const * s)
{
    unsigned char c(static_cast<unsigned char>(*s));
    if(c < 0x80)
    {
        // ASCII is the same in UTF-8
        return c;
    }
    wide_char_t wc(0);
    size_t cnt(0);
    if(c >= 0xF0)
    {
        if(c >= 0xF8)
        {
            error::instance() << f_start_position << "byte U+" << error_mode_t::ERROR_HEX << c << " not valid in a UTF-8 stream." << error_mode_t::ERROR_ERROR;
            return 0xFFFD;
        }
        wc = c & 0x07;
        cnt = 3;
    }
    else if(c >= 0xE0)
    {
        wc = c & 0x0F;
        cnt = 2;
    }
    else if(c >= 0xC0)
    {
        wc = c & 0x1F;
        cnt = 1;
    }
    else
    {
        error::instance() << f_start_position << "byte U+" << error_mode_t::ERROR_HEX << c << " not valid to introduce a UTF-8 encoded character." << error_mode_t::ERROR_ERROR;
        return 0xFFFD;
    }

    for(++s; cnt > 0; --cnt, ++s)
    {
        // skip one character
        c = static_cast<unsigned char>(*s);
        if(c == '\0')
        {
            error::instance() << f_start_position << "sequence of bytes too short to represent a valid UTF-8 encoded character." << error_mode_t::ERROR_ERROR;
            return 0xFFFD;
        }
        if(c < 0x80 || c > 0xBF)
        {
            error::instance() << f_start_position << "invalid sequence of bytes, it cannot represent a valid UTF-8 encoded character." << error_mode_t::ERROR_ERROR;
            return 0xFFFD;
        }
        wc = (wc << 6) | (c & 0x3F);
    }
    if(*s != '\0')
    {
        error::instance() << f_start_position << "sequence of bytes too long, it cannot represent a valid UTF-8 encoded character." << error_mode_t::ERROR_ERROR;
        return 0xFFFD;
    }

    return wc;
}

void lexer::wctomb(wide_char_t const wc, char * mb, size_t max_length)
{
    // require a buffer large enough for the longest acceptable UTF-8 code
    if(max_length < 5)
    {
        // this is an internal (misuse) error
        throw csspp_exception_overflow("buffer too small to convert a wc to UTF-8.");
    }

    // in case of error, make sure the string is empty
    mb[0] = '\0';

    if(static_cast<wide_uchar_t>(wc) < 0x80)
    {
        // this would also encode '\0'... although it gets converted to 0xFFFD
        mb[0] = static_cast<char>(wc);
        mb[1] = '\0';
        return;
    }
    if(static_cast<wide_uchar_t>(wc) < 0x800)
    {
        mb[0] = static_cast<char>((wc >> 6) | 0xC0);
        mb[1] = (wc & 0x3F) | 0x80;
        mb[2] = '\0';
        return;
    }
    if(static_cast<wide_uchar_t>(wc) < 0x10000)
    {
        if(wc >= 0xD800 && wc <= 0xDFFF)
        {
            error::instance() << f_start_position << "surrogate characters cannot be encoded in UTF-8." << error_mode_t::ERROR_ERROR;
            return;
        }
        if(wc == 0xFFFE || wc == 0xFFFF)
        {
            error::instance() << f_start_position << "characters 0xFFFE and 0xFFFF are not valid." << error_mode_t::ERROR_ERROR;
            return;
        }

        mb[0] = static_cast<char>((wc >> 12) | 0xE0);
        mb[1] = ((wc >> 6) & 0x3F) | 0x80;
        mb[2] = (wc & 0x3F) | 0x80;
        mb[3] = '\0';
        return;
    }
    if(static_cast<wide_uchar_t>(wc) < 0x110000)
    {
        if((wc & 0xFFFF) == 0xFFFE || (wc & 0xFFFF) == 0xFFFF)
        {
            error::instance() << f_start_position << "any characters that end with 0xFFFE or 0xFFFF are not valid." << error_mode_t::ERROR_ERROR;
            return;
        }
        mb[0] = static_cast<char>((wc >> 18) | 0xF0);
        mb[1] = ((wc >> 12) & 0x3F) | 0x80;
        mb[2] = ((wc >> 6) & 0x3F) | 0x80;
        mb[3] = (wc & 0x3F) | 0x80;
        mb[4] = '\0';
        return;
    }

    error::instance() << f_start_position << "character too large, it cannot be encoded in UTF-8." << error_mode_t::ERROR_ERROR;
}

std::string lexer::wctomb(wide_char_t const wc)
{
    char mb[6];
    wctomb(wc, mb, sizeof(mb) / sizeof(mb[0]));
    return mb;
}

wide_char_t lexer::getc()
{
    wide_char_t c(0);

    // do we have characters in our unget buffer?
    if(f_ungetc_pos > 0)
    {
        // yes, retrieve the character from the last ungetc()
        --f_ungetc_pos;
        c = f_ungetc[f_ungetc_pos];
    }
    else
    {
        // no, read the next character from the input stream
        c = f_in.get();
        if(c >= 0x80)
        {
            // here we cleanly accept very long sequences
            if(c >= 0xC0 && c < 0xFF)
            {
                // starts as expected, now read the following byte sequence
                // for that UTF-8 character
                char mb[8];
                mb[0] = c;
                for(size_t i(1);; ++i)
                {
                    if(i >= sizeof(mb) / sizeof(mb[0]))
                    {
                        // remove the whole invalid sequence (this could be
                        // a character that is too long)
                        for(c = f_in.get(); c >= 0x80 && c <= 0xBF; c = f_in.get());
                        if(c != EOF)
                        {
                            f_in.unget();
                        }
                        error::instance() << f_start_position << "too many follow bytes, it cannot represent a valid UTF-8 character." << error_mode_t::ERROR_ERROR;
                        return 0xFFFD;
                    }
                    c = f_in.get();
                    if(c < 0x80 || c > 0xBF) // the test c < 0x80 includes EOF
                    {
                        if(c != EOF)
                        {
                            // make sure we do not lose the next byte
                            f_in.unget();
                        }
                        mb[i] = '\0';
                        break;
                    }
                    mb[i] = c;
                }
                c = mbtowc(mb);
            }
            else
            {
                error::instance() << f_start_position << "unexpected byte in input buffer: U+" << error_mode_t::ERROR_HEX << c << "." << error_mode_t::ERROR_ERROR;
                for(c = f_in.get(); c >= 0x80 && c <= 0xBF; c = f_in.get());
                if(c != EOF)
                {
                    f_in.unget();
                }
                return 0xFFFD;
            }
        }

        // special case for the "\n\r" sequence
        if(c == '\r')
        {
            f_position.next_line();
            c = f_in.get();
            if(c != '\n')
            {
                f_in.unget();
            }
            return '\n'; // simplify the rest of the lexer
        }
        else if(c == '\n')
        {
            f_position.next_line();
            return '\n';
        }
        else if(c == '\f')
        {
            // most editors probably don't count pages and lines...
            f_position.next_page();
            return '\n'; // simplify the rest of the lexer
        }
    }

    // invalid character read? if so convert to 0xFFFD
    if(c == '\0')
    {
        return 0xFFFD;
    }

    return c;
}

void lexer::ungetc(wide_char_t c)
{
    // ignore EOF
    if(c == EOF || c == 0xFFFD)
    {
        return;
    }

    // make sure only valid characters are ungotten
    if(c < 0 || c > 0x10FFFF)
    {
        // this error should never happen
        throw csspp_exception_logic("lexer called ungetc() with a character out of range."); // LCOV_EXCL_LINE
    }

    // make sure we do not overflow the buffer
    if(f_ungetc_pos >= sizeof(f_ungetc) / sizeof(f_ungetc[0]))
    {
        // this error should never happen
        throw csspp_exception_logic("lexer called ungetc() too many times and ran out of space"); // LCOV_EXCL_LINE
    }

    // push c in the unget buffer
    f_ungetc[f_ungetc_pos] = c;

    ++f_ungetc_pos;
}

int lexer::hex_to_dec(wide_char_t c)
{
    if(c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if(c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    if(c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }

    // this error should never happen
    throw csspp_exception_logic("hex_to_dec() called with an invalid digit."); // LCOV_EXCL_LINE
}

wide_char_t lexer::escape()
{
    wide_char_t c(getc());
    if(c == '\n')
    {
        // this is not allowed here
        error::instance() << f_start_position << "spurious newline character after a \\ character outside of a string." << error_mode_t::ERROR_ERROR;
        return 0xFFFD;
    }
    if(c == 0xFFFD)
    {
        // this is not allowed here
        error::instance() << f_start_position << "invalid character after a \\ character." << error_mode_t::ERROR_ERROR;
        return 0xFFFD;
    }
    if(c == EOF)
    {
        // this is considered valid in standard CSS
        error::instance() << f_start_position << "found EOF right after \\." << error_mode_t::ERROR_ERROR;
        return 0xFFFD;
    }

    // convert from hexadecimal?
    if(is_hex(c))
    {
        wide_char_t wc(hex_to_dec(c));
        for(int count(1); count < 6; ++count)
        {
            c = getc();
            if(!is_hex(c))
            {
                // the following space must be eaten!
                if(!is_space(c))
                {
                    // but other characters we keep
                    ungetc(c);
                }
                break;
            }
            wc = wc * 16 + hex_to_dec(c);
            if(wc >= 0x110000)
            {
                error::instance() << f_start_position << "escape character too large for Unicode." << error_mode_t::ERROR_ERROR;
                return 0xFFFD;
            }
        }
        if(wc == 0)
        {
            error::instance() << f_start_position << "escape character '\\0' is not acceptable in CSS." << error_mode_t::ERROR_ERROR;
            return 0xFFFD;
        }
        return wc;
    }
    else
    {
        // c is the character being escaped
        return c;
    }
}

node::pointer_t lexer::identifier(wide_char_t c)
{
    std::string id;
    std::string lowercase_id;
    node_type_t type(node_type_t::IDENTIFIER);

    if(c == '%')
    {
        type = node_type_t::PLACEHOLDER;
        c = getc();
    }
    else if(c == '@')
    {
        type = node_type_t::AT_KEYWORD;
        c = getc();
    }

    if(c == '-')
    {
        id += "-";
        lowercase_id += "-";
        c = getc();
    }

    if(c == '\\')
    {
        c = escape();
        if(c != 0xFFFD)
        {
            id += wctomb(c);
            lowercase_id += wctomb(std::tolower(c));
        }
    }
    else if(is_start_identifier(c))
    {
        id += wctomb(c);
        lowercase_id += wctomb(std::tolower(c));
    }
    else
    {
        if(type == node_type_t::AT_KEYWORD)
        {
            // (TBD: should '@' be returned by itself?)
            ungetc(c);
            error::instance() << f_start_position << "found an empty identifier." << error_mode_t::ERROR_ERROR;
            return node::pointer_t(new node(node_type_t::EOF_TOKEN, f_start_position));
        }
        // this should not happen because we do not call the identifier()
        // function with such invalid non-sense
        throw csspp_exception_logic("lexer::identifier() called with an invalid identifier start."); // LCOV_EXCL_LINE
    }

    for(;;)
    {
        c = getc();
        if(c == '\\')
        {
            c = escape();
            if(c == 0xFFFD)
            {
                // this happens when a backslash is the very last character
                // of an input file
                break;
            }
        }
        else if(!is_identifier(c))
        {
            break;
        }
        id += wctomb(c);
        lowercase_id += wctomb(std::tolower(c));
    }

    // this can happen if the '\' was followed by EOF
    // note that the '@' followed by something else than a valid
    // identifier start character is caught sooner (just before
    // the throw a couple of blocks up)
    if(id.empty())
    {
        // well... that was an "empty" token, so ignore and return EOF instead
        ungetc(c);
        error::instance() << f_start_position << "found an empty identifier." << error_mode_t::ERROR_ERROR;
        return node::pointer_t(new node(node_type_t::EOF_TOKEN, f_start_position));
    }

    if(c == '(' && type != node_type_t::AT_KEYWORD)
    {
        if(lowercase_id == "url")
        {
            // very special case of a URL
            // (this is nearly like a function except that the parameter
            // does not need to be a string even though it should be)
            do
            {
                // skip all whitespaces
                c = getc();
            }
            while(is_space(c));
            std::string url;
            if(c == '"' || c == '\'')
            {
                // 'c' represents the quote character
                url = string(c);
            }
            else
            {
                // no quotes, read data up to the next ')'
                // generate an error on any unexpected character
                url += wctomb(c);
                for(;;)
                {
                    c = getc();
                    if(c == ')'
                    || is_space(c))
                    {
                        break;
                    }
                    if(c == EOF
                    || c == '"'
                    || c == '\''
                    || c == '('
                    || is_non_printable(c))
                    {
                        error::instance() << f_start_position << "found an invalid URL, one with forbidden characters." << error_mode_t::ERROR_ERROR;
                        c = ')'; // simulate us ending cleanly to avoid a double error
                        break;
                    }

                    url += wctomb(c);
                }
            }

            // got the ')' yet?
            if(c != ')')
            {
                for(;;)
                {
                    c = getc();
                    if(c == ')')
                    {
                        break;
                    }
                    if(!is_space(c))
                    {
                        error::instance() << f_start_position << "found an invalid URL, one which includes spaces or has a missing ')'." << error_mode_t::ERROR_ERROR;
                        // TODO: determine whether we should break
                        //       or skip until we find a parenthesis
                        //       we may also want to check the character
                        //       (i.e. skip up to ')' or ';', '\n' etc.)
                        break;
                    }
                    // skip trailing spaces
                }
            }

            node::pointer_t n(new node(node_type_t::URL, f_start_position));
            n->set_string(url);
            return n;
        }
        else
        {
            // special case of a function
            node::pointer_t n(new node(node_type_t::FUNCTION, f_start_position));
            // functions are always considered case insensitive
            // (although some Microsoft old extensions were case sensitive...)
            n->set_string(lowercase_id);
            return n;
        }
    }

    ungetc(c);

    // we got an identifier
    node::pointer_t n(new node(type, f_start_position));
    n->set_string(id);
    n->set_lowercase_string(lowercase_id);
    return n;
}

node::pointer_t lexer::number(wide_char_t c)
{
    bool const has_sign(c == '-' || c == '+');
    int const sign(c == '-' ? -1 : 1);
    if(has_sign)
    {
        // skip the sign if we have one
        c = getc();
    }

    // the first part is an integer number
    integer_t integer(0);
    if(is_digit(c))
    {
        // number before the period ("integer")
        integer = c - '0';
        for(;;)
        {
            c = getc();
            if(!is_digit(c))
            {
                break;
            }
            uint64_t ni(static_cast<uint64_t>(integer) * 10 + c - '0');
            if(ni >= 0x8000000000000000LL)
            {
                // we accept all up to the time it goes negative
                error::instance() << f_start_position << "integral part too large for a number." << error_mode_t::ERROR_ERROR;
            }
            integer = static_cast<integer_t>(ni);
        }
    }

    // we can have a decimal part
    decimal_number_t decimal_part(0);
    decimal_number_t decimal_frac(1.0);
    if(c == '.')
    {
        for(;;)
        {
            c = getc();
            if(!is_digit(c))
            {
                break;
            }
            decimal_frac *= 10.0;
            decimal_part += (c - '0') / decimal_frac;
            if(decimal_frac >= 1e21 && decimal_frac < 1e22)
            {
                error::instance() << f_start_position << "fraction too large for a decimal number." << error_mode_t::ERROR_ERROR;
            }
        }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
        if(decimal_frac == 1.0)
#pragma GCC diagnostic pop
        {
            // TBD: I do not think that a number can be followed by a class
            //      so I do not think this error is a problem
            //          35.my-class
            error::instance() << f_start_position << "decimal number must have at least one digit after the decimal point." << error_mode_t::ERROR_ERROR;
            // this won't affect the resulting value, however it will
            // mark the number as a decimal number instead of an integer
            decimal_frac = 10.0;
        }
    }

    integer_t exponent(0);
    if(c == 'e' || c == 'E')
    {
        // we have to make sure this looks like an exponent otherwise
        // we are likely to break a dimension such as "em"
        bool is_exponent(false);
        wide_char_t const s(getc());
        if(s == '-' || s == '+')
        {
            wide_char_t const d(getc());
            if(is_digit(d))
            {
                is_exponent = true;
            }
            ungetc(d);
        }
        else if(is_digit(s))
        {
            is_exponent = true;
        }
        ungetc(s);
        if(is_exponent)
        {
            c = getc();
            integer_t exponent_sign(1);
            if(c == '-')
            {
                exponent_sign = -1;
                c = getc();
            }
            else if(c == '+')
            {
                c = getc();
            }
            if(!is_digit(c))
            {
                // see definition of is_exponent to understand why this is throw
                throw csspp_exception_logic("we just checked that there would be a digit here, optionally preceeded by a sign."); // LCOV_EXCL_LINE
            }
            for(; is_digit(c); c = getc())
            {
                exponent = exponent * 10 + c - '0';
                if(exponent >= 1024)
                {
                    error::instance() << f_start_position << "exponent too large for a decimal number." << error_mode_t::ERROR_ERROR;
                }
            }
            exponent *= exponent_sign;
        }
    }

    // dimension is empty by default (i.e. we are just dealing with a number)
    // if not empty, then the DECIMAL_NUMBER and INTEGER are dimensions
    std::string dimension;
    if(is_identifier(c)
    || c == '\\')
    {
        // unfortunately, calling the identifier() function would
        // (1) force the dimension to start with a start identifier
        // character; (2) create an unnecessary node; so instead we
        // duplicate the inner loop here
        for(;;)
        {
            if(c == '\\')
            {
                c = escape();
                if(c == 0xFFFD)
                {
                    // this happens when a backslash is the very last character
                    // of an input file
                    break;
                }
            }
            else if(!is_identifier(c))
            {
                ungetc(c);
                c = '\0'; // make sure it is not %
                break;
            }
            dimension += wctomb(std::tolower(c));
            c = getc();
        }
        // if the dimension is just "-" then it is wrong
        if(dimension == "-")
        {
            ungetc('-');
            dimension = "";
        }
    }
    else if(c == '%')
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
        if(decimal_frac == 1.0)
#pragma GCC diagnostic pop
        {
            decimal_frac = 10.0;
        }
    }
    else
    {
        ungetc(c);
    }

    node::pointer_t n;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
    if(exponent != 0
    || decimal_frac != 1.0)
#pragma GCC diagnostic pop
    {
        n.reset(new node(c == '%' ? node_type_t::PERCENT : node_type_t::DECIMAL_NUMBER, f_start_position));
        // Note: CSS defines this math as such and thus we follow that scheme
        //       instead of the usual immediate conversion
        //
        // TODO: We may want to check/know about gross overflows?
        //
//std::cerr << "+++ integer = [" << integer << "]\n"
//          << "+++ decimal_part = [" << decimal_part << "] / [" << decimal_frac << "]\n"
//          << "+++ exponent = [" << exponent << "]\n";
        n->set_decimal_number(sign * (static_cast<decimal_number_t>(integer) + decimal_part)
                            * pow(10.0, static_cast<decimal_number_t>(exponent)));
        if(c == '%')
        {
            // a percent value is generally from 0.0 to 1.0, so convert it now
            n->set_decimal_number(n->get_decimal_number() / 100.0);
        }
        else
        {
            n->set_string(dimension);
        }
    }
    else
    {
        n.reset(new node(node_type_t::INTEGER, f_start_position));
        n->set_integer(integer * sign);
        n->set_string(dimension);
    }
    n->set_boolean(has_sign);
    return n;
}

node::pointer_t lexer::hash()
{
    std::string str;
    for(;;)
    {
        wide_char_t c(getc());
        if(c == '\\')
        {
            c = escape();
            if(c == 0xFFFD)
            {
                break;
            }
        }
        else if(!is_hash_character(c))
        {
            ungetc(c);
            break;
        }
        str += wctomb(c);
    }

    if(str.empty())
    {
        error::instance() << f_start_position << "'#' by itself is not valid." << error_mode_t::ERROR_ERROR;
        return node::pointer_t();
    }

    node::pointer_t n(new node(node_type_t::HASH, f_start_position));
    n->set_string(str);
    return n;
}

std::string lexer::string(wide_char_t const quote)
{
    std::string str;
    for(;;)
    {
        wide_char_t c(getc());
        if(c == EOF)
        {
            // In CSS this is not considered an error, it very much is for us
            // (optimization of that kind is not allowed in our sources)
            error::instance() << f_start_position << "found an unterminated string." << error_mode_t::ERROR_ERROR;
            return str;
        }
        if(c == '\n')
        {
            // remember that whitespaces are significant in CSS
            ungetc(c);
            error::instance() << f_start_position << "found an unterminated string with an unescaped newline." << error_mode_t::ERROR_ERROR;
            return str;
        }
        if(c == quote)
        {
            return str;
        }
        if(c == '\\')
        {
            // escape
            wide_char_t n(getc());
            if(n == '\n')
            {
                c = '\n';
            }
            else if(n == EOF)
            {
                c = EOF;
            }
            else if(n == 0xFFFD)
            {
                // We have a special case here because ungetc(0xFFFD) does
                // nothing so we would not otherwise catch this error!
                error::instance() << f_start_position << "invalid character after a \\ character." << error_mode_t::ERROR_ERROR;
                c = EOF; // do not insert anything more in the string for this entry
            }
            else
            {
                ungetc(n);
                c = escape();
            }
        }

        if(c != EOF
        && c != 0xFFFD)
        {
            str += wctomb(c);
        }
    }
    //NOTREACHED
}

node::pointer_t lexer::comment(bool c_comment)
{
    std::string str;

    if(c_comment)
    {
        // skip leading spaces
        for(;;)
        {
            wide_char_t const c(getc());
            if(!is_space(c))
            {
                ungetc(c);
                break;
            }
        }

        // read up to the next "*/" sequence
        for(;;)
        {
            wide_char_t c(getc());
            if(c == EOF)
            {
                error::instance() << f_start_position << "unclosed C-like comment at the end of your document." << error_mode_t::ERROR_ERROR;
                break;
            }
            if(c == '*')
            {
                c = getc();
                if(c == '/')
                {
                    break;
                }
                ungetc(c);
                c = '*';
            }
            //else if(c == '\n') ... remove the starting '*' or ' *'?
            str += wctomb(c);
        }
    }
    else
    {
        // skip leading spaces, but not newlines!
        for(;;)
        {
            wide_char_t const c(getc());
            if(c != ' '
            && c != '\t')
            {
                ungetc(c);
                break;
            }
        }

        // read up to the next "\n" character, however, we also
        // save the following lines if these also are C++ like
        // comments because it certainly represents one block
        for(;;)
        {
            wide_char_t c(getc());
            if(c == EOF)
            {
                break;
            }
            if(c == '\n')
            {
                c = getc();
                if(c == '/')
                {
                    c = getc();
                    if(c == '/')
                    {
                        // include a newline, but not the "//" sequence
                        str += '\n';
                        // remove the first space if there is such
                        // it will be readded by the assembler
                        c = getc();
                        if(c != ' '
                        && c != '\t')
                        {
                            ungetc(c);
                        }
                        continue;
                    }
                    ungetc(c);
                    c = '/';
                }
                ungetc(c);

                // whitespaces can be significant in CSS, we want the '\n'
                // to generate one here too
                ungetc('\n');
                break;
            }
            str += wctomb(c);
        }
    }

    //
    // comments are kept only if marked with the special @-keyword:
    //   @preserve
    //
    if(str.find("@preserve") != std::string::npos)
    {
        // remove ending spaces
        while(!str.empty() && is_space(str.back()))
        {
            str.pop_back();
        }

        node::pointer_t n(new node(node_type_t::COMMENT, f_start_position));
        n->set_string(str);
        n->set_integer(c_comment ? 1 : 0); // make sure to keep the type of comment
        return n;
    }

    return node::pointer_t();
}

node::pointer_t lexer::unicode_range(wide_char_t d)
{
    // U+ was skipped in the next_token() function
    // 'd' represents the first digit on entry
    wide_char_t start(0);
    wide_char_t end(0);
    bool has_mask(false);
    for(int count(0);
        count < 6 && ((is_hex(d) && !has_mask) || d == '?');
        ++count, d = getc())
    {
        if(d == '?')
        {
            if(!has_mask)
            {
                end = start;
            }
            has_mask = true;
            start *= 16;
            end = end * 16 + 15;
        }
        else
        {
            start = start * 16 + hex_to_dec(d);
        }
    }

    // if no mask (? chars) then we may have a dash (-) and a specific end
    if(has_mask)
    {
        if(start >= 0x110000)
        {
            error::instance() << f_start_position << "unicode character too large, range is U+000000 to U+10FFFF." << error_mode_t::ERROR_ERROR;
            start = 0; // avoid a double error with start > end
        }
        // the end of a unicode range may include values that are not
        // representing valid Unicode characters; but we have to support
        // such to accept all possible masks (i.e. 1?????)
        if(end > 0x1FFFFF)
        {
            // this can legally happen when using a mask such as "1?????"
            end = 0x1FFFFF;
        }
    }
    else
    {
        if(d == '-')
        {
            // skip the '-'
            d = getc();

            // in this case the '?' are not allowed
            for(int count(0); count < 6 && is_hex(d); ++count, d = getc())
            {
                end = end * 16 + hex_to_dec(d);
            }
        }
        else
        {
            // not specified, same as start
            end = start;
        }

        if(start >= 0x110000
        || end >= 0x110000)
        {
            error::instance() << f_start_position << "unicode character too large, range is U+000000 to U+10FFFF." << error_mode_t::ERROR_ERROR;
            node::pointer_t n(new node(node_type_t::UNICODE_RANGE, f_start_position));
            return n;
        }
    }

    if(start > end)
    {
        error::instance() << f_start_position << "unicode range cannot have a start character larger than the end character." << error_mode_t::ERROR_ERROR;
        node::pointer_t n(new node(node_type_t::UNICODE_RANGE, f_start_position));
        return n;
    }

    // whatever character ended the range is pushed back
    ungetc(d);

    node::pointer_t n(new node(node_type_t::UNICODE_RANGE, f_start_position));
    unicode_range_t range(start, end);
    n->set_integer(range.get_range());
    return n;
}

node::pointer_t lexer::variable(wide_char_t c)
{
    std::string var;

    for(;;)
    {
        // SASS accepts '-' and '_' as the same character;
        // we suggest you use the underscore to be more compatible with
        // other languages that do not support a '-' in variable names
        if(c == '-')
        {
            c = '_';
        }
        var += wctomb(std::tolower(c));
        c = getc();
        if(!is_variable(c))
        {
            break;
        }
    }

    node::pointer_t n;

    if(c == '(')
    {
        // in this case we have a function call
        // functions can be defined using @mixin func(...) { ... }
        n.reset(new node(node_type_t::VARIABLE_FUNCTION, f_start_position));
    }
    else
    {
        ungetc(c);

        n.reset(new node(node_type_t::VARIABLE, f_start_position));
    }

    // we got a variable
    n->set_string(var);
    return n;
}

} // namespace csspp

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
