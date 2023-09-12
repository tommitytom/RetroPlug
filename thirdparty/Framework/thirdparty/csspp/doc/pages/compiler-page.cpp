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

//! \page compiler_reference CSS Preprocessor Reference
//! \tableofcontents
//!
//! CSS Preprocessor is an extension to the CSS language that adds features
//! not otherwise available in CSS to make it easier to quickly write
//! advanced CSS documents.
//!
//! \section features Features
//!
//! The main features of CSS Preprocessor are:
//!
//! * A Validator which verifies that the syntax of all the fields that
//!   you are using are all valid;
//! * Variables to make it more dynamic, our variables support being set
//!   to absolutely anything;
//! * Nesting of rules to avoid having to write complete selectors for
//!   each rule;
//! * Nesting of declarations to avoid having to write complete field
//!   names for each declaration;
//! * User defined functions to apply to field values;
//! * Control directives, including conditional compiling;
//! * Beautified or compressed output.
//!
//! \section other_projects Other CSS Projects
//!
//! There are other projects availables that handle CSS in various ways
//! albeit similar to what CSS Preprocessor offers. We will attempt to
//! add as many of the options available in those projects to the
//! CSS Preprocessor.
//!
//! \li <a href="http://sass-lang.com/">SASS</a>
//! \li <a href="http://lesscss.org/">{less}</a>
//!
//! If you know of other projects we could list here, let us know.
//!
//! \section syntax Syntax
//!
//! The syntax supported by the CSS Preprocessor language follows the
//! standard CSS 3 syntax with just a few exceptions. Files are expected
//! to be named with extension .scss, although the compiler does not
//! enforce the extension in any way.
//!
//! All input files must be UTF-8. Since most CSS files are written in
//! ASCII, this is not likely going to be a problem.
//!
//! The one main exception to the CSS 3 syntax is the setting of a variable
//! at the top level (i.e. a global variable). Setting a variable looks like
//! declaring a field:
//!
//! \code{.scss}
//!     // valid for variables
//!     $color: #123;
//!     $width: 50px;
//!
//!     $block: {
//!         color: $color;
//!         width: $width
//!     };    // <- notice this mandatory colon in this case
//! \endcode
//!
//! In CSS 3, this is not allowed at the top level, which expects lists of
//! selectors followed by a block or \@-rules.
//!
//! Another exception is the support of nested fields. These look like
//! qualified rules by default, but selectors can have a ':' only if followed
//! by an identifier, so a colon followed by a '{' is clearly not a qualified
//! rule. Note that to further ensure the validity of the rule, we also
//! enforce a ';' at the end of the construct. With all of that we can safely
//! change the behavior and support the nested fields as SASS does (except for
//! the ';' at the end of the {}-block):
//!
//! \code{.scss}
//!     font: {
//!         family: helvetica;
//!         style: italic;
//!         size: 120%;
//!     };   // <- notice the mandatory ';' in this case
//!
//!     // which becomes
//!     font-family: helvetica;
//!     font-style: italic;
//!     font-size: 120%;
//! \endcode
//!
//! Other exceptions are mainly in the lexer which support additional tokens:
//! the variable syntax ($\<name>), the reference character (&), and
//! the placeholder extension (%\<identifier>).
//!
//! However, anything that is not supported generates an error and no output
//! is generated. This allows you to write scripts and makefiles that make
//! sure that your output is always valid CSS before you publish it.
//!
//! \section known_bugs Known Bugs
//!
//! * Case Sensitivity
//!
//! At this time, the CSS Preprocessor does not handle identifiers correctly.
//! It will force them all to lowercase, meaning that the case is not valid
//! for documents such as XML that are not case insensitive like HTML. We
//! only expect our preprocessor to be used for CSS that is itself used with
//! HTML.
//!
//! \section not_implemented Not Yet Implemented
//!
//! There are many features from SASS that are not yet implemented in csspp
//! but are eventually mentioned in this documentation. We do intend to add
//! support for most of the features present in SASS, possibly with a few
//! tweaks, but SASS is already pretty advanced so it will take us a little
//! bit of time to reach completion of the csspp project in that realm.
//!
//! That being said, csspp is already quite functional, especially for
//! a first level validation run and for compressing your existing CSS
//! files as much as CSS makes it possible (removing unnecessary spaces,
//! shortening colors, etc.)
//!
//! \section using_csspp Using the CSS Preprocessor
//!
//! \subsection command_line_tool csspp in your shell
//!
//! You may use the \ref src/csspp.cpp "CSS Preprocessor command line tool" to compile your
//! SCSS files. It is very similar to using a compiler:
//!
//! \code{.sh}
//!      csspp input.scss -o output.css
//! \endcode
//!
//! The command line tool supports many options. By default the output is
//! written to standard output. The tool exits with 1 on errors and 0 on
//! warnings or no messages.
//!
//! \subsection cpp_api The libcsspp API (C++)
//!
//! If you are writing a C++ application, you may directly include the
//! library. With cmake, you may use the FindCSSPP macros as in:
//!
//! \code{.cmake}
//!      # cmake loads CSSPP_INCLUDE_DIRS and CSSPP_LIBRARIES
//!      find_package(CSSPP REQUIRED)
//! \endcode
//!
//! Then look at the <a href="annotated.html">API documentation</a>
//! for details on how to use the csspp objects. You may  check out
//! the src/csspp.cpp file as an example of use of the CSS Preprocessor
//! library.
//!
//! In general, you want to open a file, give it to a lexer object.
//! Create a parser and parse the input. With the resulting node tree,
//! create a compiler and compile the parser tree. The compiled tree
//! can then be output using an assembler object.
//!
//! The compiler automatically runs all the validation steps
//! currently supported by csspp.
//!
//! \code
//!     #include    <csspp/compiler.h>
//!     #include    <csspp/parser.h>
//!
//!     std::ifstream in;
//!     if(!in.open("my-file.scss"))
//!     {
//!         std::cerr << "error: cannot open file.\n";
//!         exit(1);
//!     }
//!     csspp::position pos("my-file.scss");
//!     csspp::lexer l(in, pos);
//!     csspp::parser p(l);
//!     csspp::node::pointer_t root(p.stylesheet());
//!     csspp::compiler c;
//!     c.set_root(root);
//!     //c.set_...(); -- setup various flags
//!     //c.add_paths("."); -- add various path to use with @import
//!     c.compile();
//!     csspp::assembler a(std::cout);
//!     a.output(c.get_root());
//! \endcode
//!
//! \section comments Comments (C and C++)
//!
//! The CSS Preprocessor supports standard C (and thus CSS)
//! and C++ comments:
//!
//! \code{.scss}
//! /* a standard C-like comment
//!  * which can span on multiple lines */
//!
//! // A C++-like comment
//! \endcode
//!
//! C++ comments that span on multiple lines are viewed as one comment.
//!
//! \code{.scss}
//!      // This 4 lines comment is viewed as just one comment
//!      // which makes it possible to use C++ comments for large
//!      // blocks as if you where using C-like comments
//!      // (which is important if you use the @preserve keyword)
//! \endcode
//!
//! All comments are removed from the output except those that include
//! the special "\@preserve" keyword. This is useful to include comments
//! such as copyrights.
//!
//! \warning
//! We do not allow CSS tricks including weird use of comments in .scss
//! files. Although the output could include such, we assume that the final
//! output is specialized for a specific browser so such tricks are never
//! necessary. Actually, only comments marked with \@preserve are kept and
//! a preserved comment appearing in the wrong place will generally create
//! an error. Plus, Internet Explorer has been improved to much better
//! support the normal expected CSS syntax and the standardized way of
//! extending fields that such tricks have pretty become obsolete.
//!
//! Variable expension is provided for comments with the \@preserve keyword.
//! The variables have to be written between curly brackets as in:
//!
//! \code{.scss}
//!      /* My Project (c) 2015  My Company
//!       * @preserve
//!       * Generated by csspp version {$_csspp_version}
//!       */
//! \endcode
//!
//! To be SASS compatible, we will also remove a preceeding '#' character:
//!
//! \code{.scss}
//!      /* Version: #{$my_project_version} */
//! \endcode
//!
//! \warning
//! We may look into compiling the contents of the #{...} \em block as
//! SASS does. At this point we only support variables in such blocks
//! and you can do the computation in your variable prior to the comment.
//!
//! Note that C++ like comments are saved as normal C comments in the
//! final output. In other words, we make C++ comments CSS compatible
//! in the end.
//!
//! \section at_commands @-commands
//!
//! The CSS Preprocess compiler adds a pletoria of
//! \ref at_keywords "\@-commands" to support
//! various useful capabilities of the compiler.
//!
//! The compiler will also validate the syntax for all the
//! \@-commands it knows about.
//!
//! \section selectors Selectors
//!
//! The same selectors as CSS 3 are supported by the CSS Preprocessor.
//! All the lists of selectors get compiled to make sure they are valid
//! CSS code.
//!
//! At some point, the compiler will also verify each identifier, class
//! name, attribute name, attribute value, and identifier will be checked
//! (if you want to, it won't be mandatory.)
//!
//! Also like SASS, we support the %\<name> selector. This allows for
//! creating rules that do not automatically get inserted in the output.
//! This allows for the definition of various CSS libraries with rules
//! that get used only when referenced.
//!
//! Detailed information about the supported selectors and extensions
//! is found in \ref selectors_rules.
//!
//! \section expressions Expressions
//!
//! The CSS Preprocessor adds support for C-like
//! \ref expression_by_type_page "expressions".
//! The syntax is described in \ref expression_page.
//!
//! Expressions are accepted in various places:
//!
//! 1) In fields declarations:
//!
//! \code{.scss}
//!     ... <field-name> ':' ... <expressions> ... ';'
//! \endcode
//!
//! 2) between certain \@-keyword and there block or semicolon (;):
//!
//! \code{.scss}
//!     ... @-keyword <expressions> { ... }
//!     ... @-keyword <expressions> ;
//! \endcode
//!

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et syntax=doxygen
