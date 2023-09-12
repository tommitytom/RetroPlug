// CSS Preprocessor
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
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

// Documentation only file

/** \mainpage CSS Preprocessor
 * \tableofcontents
 *
 * The following is a quick list of available pages in this documentation
 * that we think you will find useful.
 *
 * \section introduction Introduction
 *
 * The CSS Preprocessor is a C++ library that one can use to read, parse,
 * compile, and assemble (output) CSS files. The parsed data appears as
 * a tree of tokens. The assembler can output the data compressed or
 * beautified.
 *
 * The data is not really useable to display HTML in a browser, at least,
 * it would be rather complicated in its current form. We may change that
 * at a later time and then replace the assembler to output the objects
 * as valid and minified CSS instead of keeping the tree as we do now.
 *
 * \section csspreprocessor_example CSS Preprocessor Example
 *
 * The following is an example of input data to the CSS Preprocessor.
 * The sample shows the use of two variables, and of nested rules
 * which I think is one of the most powerful feature of the CSS
 * Preprocessor.
 *
 * \include doc/sample.scss
 *
 * First there is the command line used to process the file (assuming
 * you installed the package as expected by default). As we can see,
 * I use the "expanded" style to show a beautified version. To generate
 * a CSS as compressed as possible, use the compressed style (which is
 * the default).
 *
 * \code
 * csspp --style expanded --output sample.css sample.scss
 * \endcode
 *
 * The following is the resulting output:
 *
 * \include doc/sample.css
 *
 * \section csspreprocessor_references CSS Preprocessor References
 *
 * The command line tool is documented in src/csspp.cpp (make sure to click
 * on the More... link to see all the details.)
 *
 * The supported CSS extensions are defined in the \ref compiler_reference.
 * Look closely, it has sub-pages such as the @-keywords and expression
 * that define many of the features of the compiler.
 *
 * \section internals_references Lexer & Parser References
 *
 * The code of the lexer and parser classes is based on W3C documents
 * which we extended to support our own syntactical extensions. The
 * following pages describes those extensions. If you are not a programmer
 * of the CSS Preprocessor, it probably won't be of much interest to you.
 *
 * * \ref lexer_rules
 * * \ref parser_rules
 *
 * \section w3c_references W3C References
 *
 * I find it really difficult to find documents on the w3c website.
 * I am hoping that one of these days someone will tell me, hey dude,
 * the summary is here: ... So far, I only found scattered documents
 * and links from other websites to those documents of interest.
 *
 * There are links for CSS 3 on the w3c website:
 *
 * * http://www.w3.org/TR/css-syntax-3/ (lexer, grammar)
 * * http://www.w3.org/TR/css3-color/ (colors)
 * * http://www.w3.org/TR/css3-values/ (dimensions and calc(), toggle(), attr() functions)
 * * http://www.w3.org/TR/selectors/ (rule selectors)
 *
 * \image html csspp-logo.png "CSS Preprocessor Logo"
 */

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// Also do: :syntax sync fromstart
// vim: ts=4 sw=4 et syntax=doxygen
