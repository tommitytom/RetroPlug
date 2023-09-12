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

// WARNING:
//   We show C-like comments in this block so we use //! as the introducer
//   in order to keep everything looking like a comment even when we have
//   a */ in the comment.
//
//   If you use gvim, you may want to change your comment setup to:
//
//      set comments=s1:/*,mb:*,ex:*/,b://,b://!
//
//   in order to include the //! introducer (assuming you don't already
//   have it in there!) You could also include ///
//


/** \page at_keywords CSS Preprocessor &At;-keywords
 * \tableofcontents
 *
 * \section at_charset @charset "utf-8";
 *
 * The csspp compiler accepts the \@charset keyword, but only if it
 * specifies UTF-8. At this time the compiler does not check whether
 * the \@charset was appearing at the very beginning of the file.
 * Also, the specification is not kept in the compiled tree so it
 * does not get output either.
 *
 * If the string following the \@charset is not UTF-8 (in any case)
 * then an error is generated. If the syntax is invalid, an error
 * is also generated (i.e. the \@charset must be followed by a
 * string.)
 *
 * \section at_debug @debug \<string> ;
 *
 * Print out a debug message (i.e. \<string>). By default a system
 * should not display debug messages. The csspp command line tool
 * shows debug messages only if the --debug command line option is
 * used.
 *
 * If the message is somehow empty, then a default message is printed.
 *
 * \section at_else @else { code }
 *
 * The \@else command offers a way to run code when all \ref at_if "\@if"
 * and \ref at_else_if "\@else if" preceeding this \@else have a false
 * expression.
 *
 * The precense of an \@else somewhere else than after an \ref at_if "\@if"
 * or an \ref at_else_if "\@else if" is an error.
 *
 * \code{.scss}
 *     @if $pseudo_name = "nth-child" {
 *       // do something if nth-child
 *     }
 *     @else if $pseudo_name = "first-child" {
 *       // do something if first-child
 *     }
 *     @else {
 *       @error "we do not know what to do then...";
 *     }
 * \endcode
 *
 * \section at_else_if @else if \<expression> { code }
 *
 * The \@else if command offers a way to verify another expression
 * and execute code only if all previous \ref at_if "\@if" and \@else
 * if were all false and this \@else if expression is true.
 *
 * The precense of an \@else if somewhere else than after an \ref at_if "\@if"
 * or another \@else if is an error. In particular, an \@else if cannot
 * appear after an \ref at_else "\@else".
 *
 * \code{.scss}
 *     @if $pseudo_name = "nth-child" {
 *       // do something if nth-child
 *     }
 *     @else if $pseudo_name = "first-child" {
 *       // do something if first-child
 *     }
 * \endcode
 *
 * \section at_error @error \<string> ;
 *
 * Print out an error message (i.e. \<string>). If the message is somehow
 * empty, print out a default error message.
 *
 * The error is counted just like any other errors, so a tool, such as
 * the csspp compiler, will return with an exit code of 1.
 *
 * \section at_if @if \<expression> { code }
 *
 * The \@if command adds the possibility to write code that gets
 * compiled only when the expression is true.
 *
 * An \@if command can be followed by an \ref at_else_if "\@else if"
 * or an \ref at_else "\@else".
 *
 * For example, validation code generally checks various values
 * such as strings and generate an error if the values are not
 * considered valid:
 *
 * \code{.scss}
 *     @if $pseudo_name != "nth-child" {
 *       @error $pseudo_name + " is not a known pseudo function.";
 *     }
 * \endcode
 *
 * \section at_import @import \<string> ;
 *
 * The \@import command includes another .scss or .css file in place.
 * This allows you to create libraries of CSS rules that you can
 * reuse in various places.
 *
 * The \@import is ignored if the specified path or URL points to a
 * file that cannot be loaded. If the URL specify a domain name then
 * the compiler ignores it and the \@import is left alone.
 *
 * At this point the \@import conditions are completely ignored if
 * present.
 *
 * If a file can be loaded, then the \@import is removed from the
 * source and replaced by the contents of the file.
 *
 * There is no depth limit to the \@import, however, the compiler
 * will do its best to avoid loops (importing A which imports B
 * and in turn attempts to re-import A.)
 *
 * \code{.scss}
 *     @import "extensions/colorful.scss";
 * \endcode
 *
 * An imported file can include anything. For example, the 
 * system/version.scss file only includes a set of variables
 * representing the version of the compiler.
 *
 * \note
 * There is also an \ref at_include "\@include" command, which is used
 * to include \em complex variables when the simple $\<varname> is not
 * going to work right.
 *
 * \section at_include @include \<identifier\> ;
 *
 * This \@include is used to insert a variable in place. In some
 * circumstance you may want to insert a rather complex variable
 * and this will generally make it work.
 *
 * This is particularly useful in case you want to insert a variable
 * representing fields. That being said, the csspp way is to use
 * the null field name:
 *
 * \code
 *     @mixin complex { color: red; }
 *
 *     // this works as is since we are defining 'background-color: red'
 *     body { background: $complex }
 *
 *     // however, to define the text color we have a problem which
 *     // can be resolved these ways:
 *     p { -csspp-null: $complex }
 *     span { @include complex; }
 * \endcode
 *
 * Of course, the \@include itself gets removed from the resulting
 * CSS file.
 *
 * Note that the \@include can also be used to insert a variable
 * function:
 *
 * \code
 *     @mixin colorful($color, $border-width: 1px) {
 *         color: $color;
 *         border: $border-width solid black;
 *     }
 *
 *     p.error { @include colorful(red, 3px); }
 * \endcode
 *
 * The final output will look like:
 *
 * \code
 *     p.error {
 *         color: red;
 *         border: 3px solid black;
 *     }
 * \endcode
 *
 * \note
 * If you wanted to include another CSS or SCSS file in your SCSS file,
 * you were looking for \ref at_import "\@import" and not the \@include.
 *
 * \section at_message @message \<string> ; or @info \<string> ;
 *
 * Print out an informational message (i.e. \<string>). If the message
 * is empty, then a default message gets written.
 *
 * The error and warning counters do not get touched when an informational
 * message is output.
 *
 * \section at_mixin @mixin \<identifier> { ... }
 *
 * The \@mixin comes from SASS. It is a way to declare a \em complex
 * variable. Although csspp does not require it, we have it to be
 * a little more compatible to SASS.
 *
 * The name of the variable is specified right after the \@mixin keyword.
 * It may be an identifier or a function declaration. The code inside
 * the curly brackets is what represents the value of the variable.
 *
 * \code
 *     @mixin red-color { color: #f00 }
 *
 *     @mixin your-color($color) { color: $color }
 * \endcode
 *
 * Just like other variables, \@mixin can be inserted using the
 * name of the variable:
 *
 * \code
 *     border { -csspp-null: $red-color; }
 *     border { -csspp-null: $your-color(#0f1598); }
 * \endcode
 *
 * To be more SASS compatible, we also support the \ref at_include "\@include".
 *
 * \section at_return @return \<expression> ;
 *
 * When defining \ref at_mixin functions, the result of the function
 * can be returned using the \@return keyword.
 *
 * The \@return keyword is expected to be used last in the list of commands
 * in the function block. It immediately ends the processing of the function
 * and makes the final expression the returned result to the caller.
 *
 * The call is like an internal function. System defined functions are
 * found in the scripts/system/functions.scss. For example, the
 * \ref opacity_function function is an overload of the
 * \ref alpha_function function. We define it as an
 * external function as follow:
 *
 * \code
 *  @mixin opacity($color)
 *  {
 *      @return alpha($color);
 *  }
 * \endcode
 *
 * One can use the \ref opacity_function function in a rule such as:
 *
 * \code
 *  div { z-index: opacity($color); }
 * \endcode
 *
 * \section at_warning @warning \<string> ;
 *
 * Print out a warning message (i.e. \<string>). If the message is
 * somehow empty, print out a default warning message.
 *
 * The warning is counted just like any other warnings and if the
 * user requested that warnings be managed like errors, an error
 * is printed and counted as expected.
 */

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et syntax=doxygen
