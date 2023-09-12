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
#include    <stdexcept>


namespace csspp
{

class csspp_exception_logic : public std::logic_error
{
public:
    csspp_exception_logic(std::string const & whatmsg) : logic_error(whatmsg) {}
};

class csspp_exception_runtime : public std::runtime_error
{
public:
    csspp_exception_runtime(std::string const & whatmsg) : std::runtime_error(whatmsg) {}
};

class csspp_exception_overflow : public std::overflow_error
{
public:
    csspp_exception_overflow(std::string const & whatmsg) : overflow_error(whatmsg) {}
};

class csspp_exception_lexer : public csspp_exception_runtime
{
public:
    csspp_exception_lexer(std::string const & whatmsg) : csspp_exception_runtime(whatmsg) {}
};

class csspp_exception_invalid_character : public csspp_exception_runtime
{
public:
    csspp_exception_invalid_character(std::string const & whatmsg) : csspp_exception_runtime(whatmsg) {}
};

class csspp_exception_invalid_token : public csspp_exception_runtime
{
public:
    csspp_exception_invalid_token(std::string const & whatmsg) : csspp_exception_runtime(whatmsg) {}
};

class csspp_exception_unexpected_token : public csspp_exception_runtime
{
public:
    csspp_exception_unexpected_token(std::string const & whatmsg) : csspp_exception_runtime(whatmsg) {}
};

class csspp_exception_exit : public csspp_exception_runtime
{
public:
    csspp_exception_exit(int new_exit_code) : csspp_exception_runtime("fatal error"), f_exit_code(new_exit_code) {}

    int     exit_code() const { return f_exit_code; }

private:
    int     f_exit_code = 0;
};

} // namespace csspp
// vim: ts=4 sw=4 et
