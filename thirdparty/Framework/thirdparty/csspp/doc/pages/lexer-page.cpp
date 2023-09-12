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

/** \page lexer_rules Lexer Rules
 * \tableofcontents
 *
 * \htmlonly
 * <style>
 *   svg.railroad-diagram
 *   {
 *     background-color: hsl(30, 20%, 95%);
 *   }
 *   svg.railroad-diagram path
 *   {
 *     stroke-width: 3;
 *     stroke: black;
 *     fill: rgba(0, 0, 0, 0);
 *   }
 *   svg.railroad-diagram a
 *   {
 *     text-decoration: none;
 *   }
 *   svg.railroad-diagram a:hover
 *   {
 *     fill: red;
 *   }
 *   svg.railroad-diagram text
 *   {
 *     font: bold 14px monospace;
 *     text-anchor: middle;
 *   }
 *   svg.railroad-diagram text.label
 *   {
 *     text-anchor: start;
 *   }
 *   svg.railroad-diagram text.comment
 *   {
 *     font: italic 12px monospace;
 *     fill: #666666;
 *   }
 *   svg.railroad-diagram rect
 *   {
 *     stroke-width: 3;
 *     stroke: black;
 *     fill: hsl(120, 100%, 90%);
 *   }
 * </style>
 * \endhtmlonly
 *
 * \note
 * Many of the SVG images below were taken from the
 * <a href="http://www.w3.org/TR/css-syntax-3/">CSS Syntax Module Level 3</a>
 * document.
 *
 * The lexer is composed of the following rules:
 *
 * \section input_stream Input Stream (CSS Preprocessor Detail)
 *
 * Contrary to CSS 3 which allows for any encoding as long as the first 128
 * bytes match ASCII sufficiently, CSS Preprocessor only accepts UTF-8. This
 * is because (1) 99% of the CSS files out there are ACSII anyway and
 * therefore already UTF-8 compatible and (2) because the Snap! Websites
 * environment is using UTF-8 throughout all of its documents (although in
 * memory text data may use a different format such as UTF-16 or UTF-32.)
 *
 * The input stream is checked for invalid data. The lexer generates an
 * error if an invalid character is found. Characters that are considered
 * invalid are:
 *
 * \li \0 -- the NULL terminator; the lexer can still parse strings, only
 *             you have to write such strings in an I/O buffer first and
 *             you just should not include the NULL terminator in that buffer;
 *             (see example below)
 * \li \xFFFD -- the INVALID character; in CSS 3, this character represents
 *               the EOF of a stream; in CSS Preprocessor, it is just viewed
 *               as an error
 * \li \x??FFFE and \x??FFFF -- any character that ends with FFFE or FFFF
 *                              is viewed as invalid and generates an error
 *
 * Note that the parsing will continue after such errors. However, if one
 * or more errors occured while parsing an input stream, you should not
 * use the output since it is likely invalid.
 *
 * An example using the CSS Preprocessor lexer with a string:
 *
 * \code
 *      ...
 *      // parse a string with CSS code
 *      std::stringstream ss;
 *      ss << my_css_string;
 *      csspp::position pos("string.css");
 *      csspp::lexer l(ss, pos);
 *      csspp::node::pointer_t n(l.next_token());
 *      ...
 * \endcode
 *
 * \section anything Any Character "ANYTHING" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 233 62" width="233">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" width="132" x="50.0" y="20"/>
 *      <text x="116.0" y="35">\??????</text>
 *     </g>
 *     <path d="M182 31h10"/>
 *     <path d="M 192 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * A valid character is any character code point defined between 0x000000
 * and 0x10FFFF inclusive.
 *
 * The \ref input-stream defines a small set of characters within that range
 * that are considered invalid in CSS Preprocessor streams. Any character
 * considered invalid is replaced by the 0xFFFD code point so the rest of
 * the implementation does not have to check for invalid characters each
 * time.
 *
 * \section ascii ASCII Character "ASCII" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 233 62" width="233">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" width="132" x="50.0" y="20"/>
 *      <text x="116.0" y="35">\0-7f</text>
 *     </g>
 *     <path d="M182 31h10"/>
 *     <path d="M 192 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * An ASCII character is any value between 0 and 127 inclusive.
 *
 * CSS 3 references ASCII and non-ASCII characters.
 *
 * \section non_ascii Non-ASCII Character "NON-ASCII" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 303 62" width="303">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" width="202" x="50.0" y="20"/>
 *      <a xlink:href="#anything"><text x="95" y="35">ANYTHING</text></a>
 *      <text x="160" y="35">except</text>
 *      <a xlink:href="#ascii"><text x="215" y="35">ASCII</text></a>
 *     </g>
 *     <path d="M252 31h10"/>
 *     <path d="M 262 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * A NON-ASCII character is any valid character code point over 127.
 *
 * \note
 * In CSS 2.x, characters between \80 and \9F were considered invalid
 * graphic controls.
 *
 * \section c_like_comment C-Like Comment "COMMENT" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="81" viewBox="0 0 497 81" width="497">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 31 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 41h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50.0" y="30"/>
 *      <text x="68.0" y="45">/&#x2A;</text>
 *     </g>
 *     <path d="M86 41h10"/>
 *     <g>
 *      <path d="M96.0 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *     <g>
 *      <path d="M116.0 21h264"/>
 *     </g>
 *     <path d="M380.0 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *     <path d="M96.0 41h20"/>
 *     <g>
 *      <path d="M116.0 41h10"/>
 *      <g>
 *       <rect height="22" width="244" x="126" y="30"/>
 *       <a xlink:href="#anything"><text x="168" y="45">ANYTHING</text></a>
 *       <text x="283" y="45">but * followed by /</text>
 *      </g>
 *      <path d="M370.0 41h10"/>
 *      <path d="M126.0 41a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M126.0 61h244"/>
 *      </g>
 *       <path d="M370.0 61a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M380.0 41h20"/>
 *     </g>
 *     <path d="M400 41h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="410.0" y="30"/>
 *      <text x="428.0" y="45">&#x2A;/</text>
 *     </g>
 *     <path d="M446 41h10"/>
 *     <path d="M 456 41 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Note that "anything" means any character that is not considered invalid
 * by the CSS Preprocessor implementation.
 *
 * \section cpp_comment C++ Comment "COMMENT" (CSS Preprocessor Extension)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="81" viewBox="0 0 397 81" width="397">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 31 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 41h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50.0" y="30"/>
 *      <text x="68.0" y="45">//</text>
 *     </g>
 *     <path d="M86 41h10"/>
 *     <g>
 *      <path d="M96.0 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *     <g>
 *      <path d="M116.0 21h164"/>
 *     </g>
 *     <path d="M280.0 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *     <path d="M96.0 41h20"/>
 *     <g>
 *      <path d="M116.0 41h10"/>
 *      <g>
 *       <rect height="22" width="144" x="126.0" y="30"/>
 *       <a xlink:href="#anything"><text x="168" y="45">ANYTHING</text></a>
 *       <text x="233" y="45">but \n</text>
 *      </g>
 *      <path d="M270.0 41h10"/>
 *      <path d="M126.0 41a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M126.0 61h144"/>
 *      </g>
 *       <path d="M270.0 61a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M280.0 41h20"/>
 *     </g>
 *     <path d="M300 41h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="310" y="30"/>
 *      <text x="328" y="45">\n</text>
 *     </g>
 *     <path d="M346 41h10"/>
 *     <path d="M 356 41 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Note that "anything" means any character that is not considered invalid
 * by the CSS Preprocessor implementation.
 *
 * The CSS Preprocessor returns a C++ comment appearing on multiple lines,
 * one after another, as a single C++ comment token. This is used that way
 * because it is possible to mark a comment as \@preserve in order to keep
 * said comment in the output. In most cases this is used in the comment at
 * the top or bottom which includes the copyright notice about the document.
 *
 * \code
 * // CSS Preprocessor
 * // Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved
 * //
 * // @preserve
 * \endcode
 *
 * \note
 * C++ comments are output as standard C-like comments so they are 100%
 * compatible with CSS.
 * 
 * \section newline Newline "NEWLINE" (CSS 3)
 *
 * \htmlonly
 *  <div class=railroad>
 *   <svg class=railroad-diagram height=152 viewbox="0 0 173 152" width="173">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <g>
 *      <path d="M40.0 31h20"/>
 *      <g>
 *       <path d="M60.0 31h8.0"/>
 *       <path d="M104.0 31h8.0"/>
 *       <rect height="22" rx="10" ry="10" width="36" x="68" y="20"/>
 *       <text x=86.0 y=35>\n</text>
 *      </g>
 *      <path d="M112.0 31h20"/>
 *      <path d="M40.0 31a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *      <g>
 *       <rect height="22" rx="10" ry="10" width="52" x="60" y="50"/>
 *       <text x="86" y="65">\r\n</text>
 *      </g>
 *      <path d="M112.0 61a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *      <path d="M40.0 31a10 10 0 0 1 10 10v40a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M60.0 91h8.0"/>
 *       <path d="M104.0 91h8.0"/>
 *       <rect height="22" rx="10" ry="10" width="36" x="68" y="80"/>
 *       <text x=86.0 y=95>\r</text>
 *      </g>
 *      <path d="M112.0 91a10 10 0 0 0 10 -10v-40a10 10 0 0 1 10 -10"/>
 *      <path d="M40.0 31a10 10 0 0 1 10 10v70a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M60.0 121h8.0"/>
 *       <path d="M104.0 121h8.0"/>
 *       <rect height="22" rx="10" ry="10" width="36" x="68" y="110"/>
 *       <text x=86.0 y=125>\f</text>
 *      </g>
 *      <path d="M112.0 121a10 10 0 0 0 10 -10v-70a10 10 0 0 1 10 -10"/>
 *     </g>
 *     <path d="M 132 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * CSS Preprocessor counts lines starting at one and incrementing anytime
 * a "\n", "\r", "\r\n" sequence is found.
 *
 * CSS Preprocessor resets the line counter back to one and increments the
 * page counter by one each time a "\f" is found.
 *
 * CSS Preprocessor also counts the total number of lines and pages in a
 * separate counter.
 *
 * The line number is used to print out errors. If you use paging (\f),
 * you may have a harder time to find your errors in the current version.
 *
 * Note that line counting also happens in C++ and C-like comments.
 *
 * \section non_printable Non-Printable "NON-PRINTABLE" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="182" viewbox="0 0 173 182" width="173">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <g>
 *
 *      <path d="M40.0 31h20"/>
 *      <g>
 *       <path d="M60.0 31h8.0"/>
 *       <path d="M114.0 31h8.0"/>
 *       <rect height="22" rx="10" ry="10" width="46" x="68" y="20"/>
 *       <text x="91" y="35">\0</text>
 *      </g>
 *      <path d="M122.0 31h20"/>
 *
 *      <path d="M40.0 31a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M60.0 61h8.0"/>
 *       <path d="M114.0 61h8.0"/>
 *       <rect height="22" rx="10" ry="10" width="46" x="68" y="50"/>
 *       <text x="91" y="65">\8</text>
 *      </g>
 *      <path d="M122.0 61a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *
 *      <path d="M40.0 31a10 10 0 0 1 10 10v40a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M60.0 91h8.0"/>
 *       <path d="M114.0 91h8.0"/>
 *       <rect height="22" rx="10" ry="10" width="46" x="68" y="80"/>
 *       <text x="91" y="95">\b</text>
 *      </g>
 *      <path d="M122.0 91a10 10 0 0 0 10 -10v-40a10 10 0 0 1 10 -10"/>
 *
 *      <path d="M40.0 31a10 10 0 0 1 10 10v70a10 10 0 0 0 10 10"/>
 *      <g>
 *       <rect height="22" rx="10" ry="10" width="62" x="60" y="110"/>
 *       <text x="91" y="125">\e-\1f</text>
 *      </g>
 *      <path d="M122.0 121a10 10 0 0 0 10 -10v-70a10 10 0 0 1 10 -10"/>
 *
 *      <path d="M40.0 31a10 10 0 0 1 10 10v100a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M60 151h8"/>
 *       <path d="M114 151h8"/>
 *       <rect height="22" rx="10" ry="10" width="46" x="68" y="140"/>
 *       <text x="91" y="155">\7f</text>
 *      </g>
 *      <path d="M122.0 151a10 10 0 0 0 10 -10v-100a10 10 0 0 1 10 -10"/>
 *
 *     </g>
 *     <path d="M 142 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * URL do not accept non-printable characters if not written between quotes.
 * This rule shows you which characters are considered non-printable in
 * CSS 3.
 *
 * \section whitespace Whitespace "WHITESPACE" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="122" viewbox="0 0 197 122" width="197">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <g>
 *      <path d="M40.0 31h20"/>
 *      <g>
 *       <path d="M60.0 31h8.0"/>
 *       <path d="M128.0 31h8.0"/>
 *       <rect height=22 rx=10 ry=10 width=60 x=68.0 y=20> </rect>
 *       <text x=98.0 y=35> space</text>
 *      </g>
 *      <path d="M136.0 31h20"/>
 *      <path d="M40.0 31a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M60.0 61h20.0"/>
 *       <path d="M116.0 61h20.0"/>
 *       <rect height="22" rx="10" ry="10" width="36" x="80" y="50"/>
 *       <text x="98" y="65">\t</text>
 *      </g>
 *      <path d="M136.0 61a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *      <path d="M40.0 31a10 10 0 0 1 10 10v40a10 10 0 0 0 10 10"/>
 *      <g>
 *       <rect height="22" width="76" x="60" y="80"/>
 *       <a xlink:href="#newline"><text x=98.0 y=95>NEWLINE</text></a>
 *      </g>
 *      <path d="M136.0 91a10 10 0 0 0 10 -10v-40a10 10 0 0 1 10 -10"/>
 *     </g>
 *     <path d="M 156 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Whitespaces are quite important in CSS since they are required in many
 * cases. For example, a dash (-) can start an identifier, so you want to
 * add a space after a dash if you want to use the minus sign.
 *
 * The CSS Preprocessor documentation often references the WHITESPACE token
 * meaning that any number of whitespaces, including zero. It may be written
 * as WHITESPACE* (0 or more whitespaces) or WHITESPACE+ (one or more
 * whitespaces) to be more explicit.
 *
 * CSS 3 defines a whitespace, a whitespace-token, and a ws* to represents
 * all those possibilities.
 *
 * \code{.scss}
 *      a - b    // 'a' 'minus' 'b'
 *      a -b     // 'a' '-b', which are two identifiers
 *      a-b      // 'a-b', which is one identifier
 * \endcode
 *
 * \section digit Decimal Digit "DIGIT" (CSS 3)
 *
 * \htmlonly
 *  <div class=railroad>
 *   <svg class=railroad-diagram height=62 viewbox="0 0 163 62" width=163>
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" width="52" x="50" y="20"/>
 *      <text x="76" y="35">0-9</text>
 *     </g>
 *     <path d="M102 31h10"/>
 *     <path d="M 112 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * An hexadecimal digit is any digit (0-9) and a letter from A to F
 * either in lowercase (a-f) or in uppercase (A-F).
 *
 * \section hexdigit Hexadecimal Digit "HEXDIGIT" (CSS 3)
 *
 * \htmlonly
 *  <div class=railroad>
 *   <svg class=railroad-diagram height=62 viewbox="0 0 233 62" width="233">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height=22 width=132 x=50.0 y=20> </rect>
 *      <text x=116.0 y=35>0-9 a-f or A-F</text>
 *     </g>
 *     <path d="M182 31h10"/>
 *     <path d="M 192 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * An hexadecimal digit is any digit (0-9) and a letter from A to F
 * either in lowercase (a-f) or in uppercase (A-F).
 *
 * \section escape Escape "ESCAPE" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="186" viewbox="0 0 441 186" width="441">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="50" y="20"/>
 *      <text x=64.0 y=35>\</text>
 *     </g>
 *     <path d="M78 31h10"/>
 *     <g>
 *      <path d="M88.0 31h20"/>
 *      <g>
 *       <path d="M108.0 31h30.0"/>
 *       <path d="M350.0 31h30.0"/>
 *       <rect height="22" width="212" x="138" y="20"/>
 *       <text x="159" y="35">not</text>
 *       <a xlink:href="#newline"><text x="207" y="35">NEWLINE</text></a>
 *       <text x="254" y="35">or</text>
 *       <a xlink:href="#hexdigit"><text x="303" y="35">HEXDIGIT</text></a>
 *      </g>
 *      <path d="M380.0 31h20"/>
 *      <path d="M88.0 31a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M108.0 61h10"/>
 *       <g>
 *        <path d="M118.0 61h10"/>
 *        <g>
 *         <rect height="22" width="92" x="128" y="50"/>
 *         <a xlink:href="#hexdigit"><text x="174" y="65">HEXDIGIT</text></a>
 *        </g>
 *        <path d="M220.0 61h10"/>
 *        <path d="M128.0 61a10 10 0 0 0 -10 10v10a10 10 0 0 0 10 10"/>
 *        <g>
 *         <path d="M128.0 91h9.5"/>
 *         <path d="M210.5 91h9.5"/>
 *         <text class=comment x=174.0 y=96>1-5 times</text>
 *        </g>
 *        <path d="M220.0 91a10 10 0 0 0 10 -10v-10a10 10 0 0 0 -10 -10"/>
 *       </g>
 *       <path d="M230.0 61h10"/>
 *       <g>
 *        <path d="M240.0 61h20"/>
 *        <g>
 *         <path d="M260.0 61h100"/>
 *        </g>
 *        <path d="M360.0 61h20"/>
 *        <path d="M240.0 61a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *        <g>
 *         <rect height="22" width="100" x="260" y="70"/>
 *         <a xlink:href="#whitespace"><text x="310" y="85">WHITESPACE</text></a>
 *        </g>
 *        <path d="M360.0 81a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *       </g>
 *      </g>
 *      <path d="M380.0 61a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *
 *      <path d="M88.0 31a10 10 0 0 1 10 10v74a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M108.0 125h10"/>
 *       <g>
 *        <path d="M118.0 125h10"/>
 *        <g>
 *         <rect height="22" width="92" x="128" y="114"/>
 *         <a xlink:href="#hexdigit"><text x="174" y="129">HEXDIGIT</text></a>
 *        </g>
 *        <path d="M220.0 125h10"/>
 *        <path d="M128.0 125a10 10 0 0 0 -10 10v10a10 10 0 0 0 10 10"/>
 *        <g>
 *         <path d="M128.0 155h9.5"/>
 *         <path d="M210.5 155h9.5"/>
 *         <text class=comment x="174" y="160">6 times</text>
 *        </g>
 *        <path d="M220.0 155a10 10 0 0 0 10 -10v-10a10 10 0 0 0 -10 -10"/>
 *       </g>
 *       <path d="M230.0 125h10"/>
 *       <g>
 *        <path d="M240.0 125h20"/>
 *        <g>
 *         <path d="M260.0 125h100"/>
 *        </g>
 *        <path d="M360.0 125h20"/>
 *       </g>
 *      </g>
 *      <path d="M380.0 125a10 10 0 0 0 10 -10v-74a10 10 0 0 1 10 -10"/>
 *
 *     </g>
 *     <path d="M 400 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Allow hexadecimal or direct escaping of any character, except the
 * new line character ('\' followed by any newline). There is an
 * exception to the newline character in strings.
 *
 * The hexadecimal syntax allows for any number from 0 to 0xFFFFFF.
 * However, the same constraint applies to escape characters and only
 * code points that are considered valid from the input stream are
 * considered valid in an escape sequence. This means any character
 * between 0 and 0x10FFFF except those marked as invalid in the
 * \ref input-stream section.
 *
 * \note
 * Contrary to the CSS 3 definition, this definition clearly shows that
 * the space after an escape is being skipped if the escape includes 1
 * to 5 digits. In case 6 digits are used, the space is NOT skipped in
 * our implementation. It looks like this is consistent with the text
 * explaining how the escape sequence works. If this is a bug, the lexer
 * will need to be changed.
 *
 * \section identifier Identifier "IDENTIFIER" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height=110 viewbox="0 0 729 110" width="729">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 31 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <g>
 *      <path d="M40.0 41h20"/>
 *      <g>
 *       <path d="M60.0 41h28"/>
 *      </g>
 *      <path d="M88.0 41h20"/>
 *      <path d="M40.0 41a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *      <g>
 *       <rect height="22" rx="10" ry="10" width="28" x="60" y="50"/>
 *       <text x="74" y="65">-</text>
 *      </g>
 *      <path d="M88.0 61a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *     </g>
 *     <g>
 *      <path d="M108.0 41h20"/>
 *      <g>
 *       <rect height="22" width="196" x="128".0 y="30"/>
 *       <text x="186" y="45">a-z A-Z _ or</text>
 *       <a xlink:href="#non-ascii"><text x="277" y="45">NON-ASCII</text></a>
 *      </g>
 *      <path d="M324.0 41h20"/>
 *      <path d="M108.0 41a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M128.0 71h64.0"/>
 *       <path d="M260.0 71h64.0"/>
 *       <rect height="22" width="68" x="192" y="60"/>
 *       <a xlink:href="#escape"><text x=226.0 y=75>ESCAPE</text></a>
 *      </g>
 *      <path d="M324.0 71a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *     </g>
 *     <g>
 *      <path d="M344.0 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *      <g>
 *       <path d="M364.0 21h304"/>
 *      </g>
 *      <path d="M668.0 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *      <path d="M344.0 41h20"/>
 *      <g>
 *       <path d="M364.0 41h10"/>
 *       <g>
 *        <path d="M374.0 41h20"/>
 *        <g>
 *         <rect height="22" width="244" x="394" y="30"/>
 *         <text x="475" y="45">a-z A-Z 0-9 _ - or</text>
 *         <a xlink:href="#non-ascii"><text x="590" y="45">NON-ASCII</text></a>
 *        </g>
 *        <path d="M638.0 41h20"/>
 *        <path d="M374.0 41a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *        <g>
 *         <path d="M394.0 71h88.0"/>
 *         <path d="M550.0 71h88.0"/>
 *         <rect height="22" width="68" x="482" y="60"/>
 *         <a xlink:href="#escape"><text x="516" y="75">ESCAPE</text></a>
 *        </g>
 *        <path d="M638.0 71a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *       </g>
 *       <path d="M658.0 41h10"/>
 *       <path d="M374.0 41a10 10 0 0 0 -10 10v29a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M374.0 90h284"/>
 *       </g>
 *       <path d="M658.0 90a10 10 0 0 0 10 -10v-29a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M668.0 41h20"/>
 *     </g>
 *     <path d="M 688 41 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * We do not have any exception to the identifier. Our lexer returns the
 * same identifiers as CSS 3 allows. Note that there is an extension
 * compared to CSS 2.x, characters 0x80 to 0x9F are accepted in identifiers.
 *
 * Note that since you can write an identifier using escape characters,
 * it can really be composed of any character except NEWLINE and invalid
 * characters. This allows for CSS to represent any character that can
 * be used in an attribute value in HTML.
 *
 * \code{.scss}
 *      .class [attribute] #id :state { ... }
 * \endcode
 *
 * \note
 * Identifiers that start with a dash must be at least two characters.
 *
 * \section function Function "FUNCTION" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 273 62" width="273">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" width="124" x="50.0" y="20"/>
 *      <a xlink:href="#identifier"><text x=112.0 y=35>IDENTIFIER</text></a>
 *     </g>
 *     <path d="M174 31h10"/>
 *     <path d="M184 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="194" y="20"/>
 *      <text x=208.0 y=35>(</text>
 *     </g>
 *     <path d="M222 31h10"/>
 *     <path d="M 232 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * A FUNCTION token is an IDENTIFIER immediately followed by an open
 * parenthesis. No WHITESPACE is allowed between the IDENTIFIER and
 * the parenthesis.
 *
 * \code{.css}
 *      a { color: rgba(255, 255, 255, 0.3); }
 * \endcode
 *
 * \section at_keyword At Keyword "AT_KEYWORD" (CSS 3)
 *
 * \htmlonly
 *  <div class=railroad>
 *   <svg class=railroad-diagram height=62 viewbox="0 0 273 62" width="273">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height=22 rx=10 ry=10 width=28 x=50.0 y="20"/>
 *      <text x=64.0 y=35>@</text>
 *     </g>
 *     <path d="M78 31h10"/>
 *     <path d="M88 31h10"/>
 *     <g>
 *      <rect height=22 width=124 x=98.0 y="20"/>
 *      <a xlink:href="#identifier"><text x=160.0 y=35>IDENTIFIER</text></a>
 *     </g>
 *     <path d="M222 31h10"/>
 *     <path d="M 232 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Various CSS definitions require an AT-KEYWORD. Note that a full
 * keyword can be defined, starting with a dash, with escape sequence,
 * etc.
 *
 * Our extensions generally make use of AT-KEYWORD commands to extend
 * the capabilities of CSS.
 *
 * \code{.css}
 *      @media { ... }
 * \endcode
 *
 * \section placeholder Placeholder "PLACEHOLDER" (CSS Preprocessor Extension)
 *
 * \htmlonly
 *  <div class=railroad>
 *   <svg class=railroad-diagram height=62 viewbox="0 0 273 62" width="273">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height=22 rx=10 ry=10 width=28 x=50.0 y="20"/>
 *      <text x=64.0 y=35>%</text>
 *     </g>
 *     <path d="M78 31h10"/>
 *     <path d="M88 31h10"/>
 *     <g>
 *      <rect height=22 width=124 x=98.0 y="20"/>
 *      <a xlink:href="#identifier"><text x=160.0 y=35>IDENTIFIER</text></a>
 *     </g>
 *     <path d="M222 31h10"/>
 *     <path d="M 232 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The PLACEHOLDER is a CSS Preprocessor extension allowing for
 * the definition of rules that do not get included in your CSS
 * unless they get referenced.
 *
 * \code{.scss}
 *      // a simple rule with a placeholder
 *      rule%one { ... }
 *
 *      // a reference to such a rule
 *      .extended
 *      {
 *          @extend %one;
 *      }
 * \endcode
 *
 * \warning
 * The placeholder token and rules are supported as expected. The @extend
 * is not yet supported in version 1.0.0 of CSS Preprocessor.
 *
 * \section variable Variable "VARIABLE" (CSS Preprocessor Extension)
 *
 * \htmlonly
 *  <div class=railroad>
 *   <svg class=railroad-diagram height=62 viewbox="0 0 293 62" width="293">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="50" y="20"/>
 *      <text x="64" y="35">$</text>
 *     </g>
 *     <path d="M78 31h10"/>
 *     <path d="M88 31h10"/>
 *     <g>
 *      <rect height="22" width="144" x="98" y="20"/>
 *      <text x="170" y="35">a-z A-Z 0-9 - _</text>
 *     </g>
 *     <path d="M242 31h10"/>
 *     <path d="M 252 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Variables are a CSS Preprocess extension, very similar to the variables
 * defined in the SASS language (also to variables in PHP).
 *
 * The name of a variable is very limited on purpose.
 *
 * \code{.scss}
 *      $text_color = #ff00ff;
 *
 *      font-color: $text_color;
 * \endcode
 *
 * \warning
 * The '-' character is allowed to be backward compatible with SASS, but
 * you are expected to use '_' instead. The lexer automatically transforms
 * all the '-' characters into '_' when reading the input stream.
 *
 * \section variable_function Variable Function "VARIABLE_FUNCTION" (CSS Preprocessor Extension)
 *
 * \htmlonly
 *  <div class=railroad>
 *   <svg class=railroad-diagram height=62 viewbox="0 0 341 62" width="341">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="50" y="20"/>
 *      <text x="64" y="35">$</text>
 *     </g>
 *
 *     <!--path d="M78 31h20"/>
 *
 *     <g>
 *      <rect height="22" width="104" x="98" y="20"/>
 *      <a xlink:href="#whitespace"><text x="150" y="35">WHITESPACE*</text></a>
 *     </g-->
 *
 *     <path d="M78 31h20"/>
 *     <g>
 *      <rect height="22" width="144" x="98" y="20"/>
 *      <text x="170" y="35">a-z A-Z 0-9 - _</text>
 *     </g>
 *     <path d="M242 31h20"/>
 *
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="262" y="20"/>
 *      <text x="276" y="35">(</text>
 *     </g>
 *
 *     <path d="M290 31h10"/>
 *     <path d="M 300 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Just like regular identifiers followed by a '(', we view variables
 * immediately followed by a '(' as Variable Functions.
 *
 * The name of a variable function is very limited on purpose.
 *
 * \code{.scss}
 *      $text_color($color): desaturate($color, 10%);
 *
 *      font-color: $text_color(#334699);
 * \endcode
 *
 * \warning
 * The '-' character is allowed to be backward compatible with SASS, but
 * you are expected to use '_' instead. The lexer automatically transforms
 * all the '-' characters into '_' when reading the input stream.
 *
 * \section hash Hash "HASH" (CSS 3)
 *
 * \htmlonly
 *  <div class=railroad>
 *   <svg class=railroad-diagram height=100 viewbox="0 0 453 100" width=453>
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height=22 rx=10 ry=10 width=28 x=50.0 y="20"/>
 *      <text x=64.0 y=35>#</text>
 *     </g>
 *     <path d="M78 31h10"/>
 *     <path d="M88 31h10"/>
 *     <g>
 *      <path d="M98.0 31h10"/>
 *      <g>
 *       <path d="M108.0 31h20"/>
 *       <g>
 *        <rect height="22" width="244" x="128" y="20"/>
 *        <text x="208" y="35">a-z A-Z 0-9 _ - or</text>
 *        <a xlink:href="#non-ascii"><text x="322" y="35">NON-ASCII</text></a>
 *       </g>
 *       <path d="M372.0 31h20"/>
 *       <path d="M108.0 31a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M128.0 61h88.0"/>
 *        <path d="M284.0 61h88.0"/>
 *        <rect height=22 width=68 x=216.0 y="50"/>
 *        <a xlink:href="#escape"><text x=250.0 y=65>ESCAPE</text></a>
 *       </g>
 *       <path d="M372.0 61a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *      </g>
 *      <path d="M392.0 31h10"/>
 *      <path d="M108.0 31a10 10 0 0 0 -10 10v29a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M108.0 80h284"/>
 *      </g>
 *      <path d="M392.0 80a10 10 0 0 0 10 -10v-29a10 10 0 0 0 -10 -10"/>
 *     </g>
 *     <path d="M402 31h10"/>
 *     <path d="M 412 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The HASH token is an identifier without the first character being
 * limited.
 *
 * \note
 * Contrary to an identifier, "-" by itself can be a HASH token.
 *
 * \section string String "STRING" (CSS 3)
 *
 * \htmlonly
 *  <div class=railroad>
 *   <svg class=railroad-diagram height=248 viewbox="0 0 481 248" width=481>
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 31 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <g>
 *      <path d="M40.0 41h20"/>
 *      <g>
 *       <path d="M60.0 41h10"/>
 *       <g>
 *        <rect height=22 rx=10 ry=10 width=28 x=70.0 y="30"/>
 *        <text x=84.0 y=45>"</text>
 *       </g>
 *       <path d="M98.0 41h10"/>
 *       <g>
 *        <path d="M108.0 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *        <g>
 *         <path d="M128.0 21h224"/>
 *        </g>
 *        <path d="M352.0 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *        <path d="M108.0 41h20"/>
 *        <g>
 *         <path d="M128.0 41h10"/>
 *         <g>
 *          <path d="M138.0 41h20"/>
 *          <g>
 *           <rect height="22" width="164" x="158" y="30"/>
 *           <text x="206" y="45">not " \ or</text>
 *           <a xlink:href="#newline"><text x="282" y="45">NEWLINE</text></a>
 *          </g>
 *          <path d="M322.0 41h20"/>
 *          <path d="M138.0 41a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *          <g>
 *           <path d="M158.0 71h48.0"/>
 *           <path d="M274.0 71h48.0"/>
 *           <rect height="22" width="68" x="206" y="60"/>
 *           <a xlink:href="#escape"><text x=240.0 y=75>ESCAPE</text></a>
 *          </g>
 *          <path d="M322.0 71a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *          <path d="M138.0 41a10 10 0 0 1 10 10v40a10 10 0 0 0 10 10"/>
 *          <g>
 *           <path d="M158.0 101h10.0"/>
 *           <path d="M312.0 101h10.0"/>
 *           <path d="M168.0 101h10"/>
 *           <g>
 *            <rect height="22" rx="10" ry="10" width="28" x="178" y="90"/>
 *            <text x=192.0 y=105>\</text>
 *           </g>
 *           <path d="M206.0 101h10"/>
 *           <path d="M216.0 101h10"/>
 *           <g>
 *            <rect height="22" width="76" x="226" y="90"/>
 *            <a xlink:href="#newline"><text x="264" y="105">NEWLINE</text></a>
 *           </g>
 *           <path d="M302.0 101h10"/>
 *          </g>
 *          <path d="M322.0 101a10 10 0 0 0 10 -10v-40a10 10 0 0 1 10 -10"/>
 *         </g>
 *         <path d="M342.0 41h10"/>
 *         <path d="M138.0 41a10 10 0 0 0 -10 10v59a10 10 0 0 0 10 10"/>
 *         <g>
 *          <path d="M138.0 120h204"/>
 *         </g>
 *         <path d="M342.0 120a10 10 0 0 0 10 -10v-59a10 10 0 0 0 -10 -10"/>
 *        </g>
 *        <path d="M352.0 41h20"/>
 *       </g>
 *       <path d="M372.0 41h10"/>
 *       <g>
 *        <rect height="22" rx="10" ry="10" width="28" x="382" y="30"/>
 *        <text x=396.0 y=45>"</text>
 *       </g>
 *       <path d="M410.0 41h10"/>
 *      </g>
 *      <path d="M420.0 41h20"/>
 *      <path d="M40.0 41a10 10 0 0 1 10 10v88a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M60.0 149h10"/>
 *       <g>
 *        <rect height="22" rx="10" ry="10" width="28" x="70" y="138"/>
 *        <text x="84" y="153">&apos;</text>
 *       </g>
 *       <path d="M98.0 149h10"/>
 *       <g>
 *        <path d="M108.0 149a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *        <g>
 *         <path d="M128.0 129h224"/>
 *        </g>
 *        <path d="M352.0 129a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *        <path d="M108.0 149h20"/>
 *        <g>
 *         <path d="M128.0 149h10"/>
 *         <g>
 *          <path d="M138.0 149h20"/>
 *          <g>
 *           <rect height="22" width="164" x="158" y="138"/>
 *           <text x="206" y="153">not &apos; \ or</text>
 *           <a xlink:href="#newline"><text x="282" y="153">NEWLINE</text></a>
 *          </g>
 *          <path d="M322.0 149h20"/>
 *          <path d="M138.0 149a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *          <g>
 *           <path d="M158.0 179h48.0"/>
 *           <path d="M274.0 179h48.0"/>
 *           <rect height="22" width="68" x="206" y="168"/>
 *           <a xlink:href="#escape"><text x="240" y="183">ESCAPE</text></a>
 *          </g>
 *          <path d="M322.0 179a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *          <path d="M138.0 149a10 10 0 0 1 10 10v40a10 10 0 0 0 10 10"/>
 *          <g>
 *           <path d="M158.0 209h10.0"/>
 *           <path d="M312.0 209h10.0"/>
 *           <path d="M168.0 209h10"/>
 *           <g>
 *            <rect height="22" rx="10" ry="10" width="28" x="178" y="198"/>
 *            <text x=192.0 y=213>\</text>
 *           </g>
 *           <path d="M206.0 209h10"/>
 *           <path d="M216.0 209h10"/>
 *           <g>
 *            <rect height="22" width="76" x="226" y="198"/>
 *            <a xlink:href="#newline"><text x=264.0 y=213>NEWLINE</text></a>
 *           </g>
 *           <path d="M302.0 209h10"/>
 *          </g>
 *          <path d="M322.0 209a10 10 0 0 0 10 -10v-40a10 10 0 0 1 10 -10"/>
 *         </g>
 *         <path d="M342.0 149h10"/>
 *         <path d="M138.0 149a10 10 0 0 0 -10 10v59a10 10 0 0 0 10 10"/>
 *         <g>
 *          <path d="M138.0 228h204"/>
 *         </g>
 *         <path d="M342.0 228a10 10 0 0 0 10 -10v-59a10 10 0 0 0 -10 -10"/>
 *        </g>
 *        <path d="M352.0 149h20"/>
 *       </g>
 *       <path d="M372.0 149h10"/>
 *       <g>
 *        <rect height="22" rx="10" ry="10" width="28" x="382" y="138"/>
 *        <text x="396" y="153">&apos;</text>
 *       </g>
 *       <path d="M410.0 149h10"/>
 *      </g>
 *      <path d="M420.0 149a10 10 0 0 0 10 -10v-88a10 10 0 0 1 10 -10"/>
 *     </g>
 *     <path d="M 440 41 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Strings can be written between '...' or "...". The string can contain
 * a quote if properly escaped. You may either escape the quote itself
 * or use the corresponding hexadecimal encoding:
 *
 * \code{.scss}
 *      '... \' or \27 ...'
 *      "... \" or \22 ..."
 * \endcode
 *
 * Of course, you can use ' in a string quoted with " and vice versa.
 *
 * Strings accept the backslashed followed by a newline to insert a
 * newline in the string and write that string on multiple lines. In
 * other words, the slash is removed, but not the '\\n' character.
 *
 * \warning
 * Note that the '\\n' is a C/C++ syntax which is not supported by
 * CSS. That is, in CSS '\\n' is equivalent to 'n'.
 *
 * \warning
 * As an extension, CSS Preprocessor does not allow for strings to
 * not be closed. This is always an error. CSS generally gives you
 * the option to make sure that improperly terminated style
 * attributes are still given a chance to function.
 *
 * \section url URL "URL" (CSS 3)
 *
 * \htmlonly
 *  <div class=railroad>
 *   <svg class=railroad-diagram height=102 viewbox="0 0 829 102" width="829">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 31 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 41h10"/>
 *     <g>
 *      <rect height="22" width="172" x="50" y="30"/>
 *      <a xlink:href="#identifier"><text x=136.0 y=45>IDENTIFIER "url"</text></a>
 *     </g>
 *     <path d="M222 41h10"/>
 *     <path d="M232 41h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="242" y="30"/>
 *      <text x=256.0 y=45>(</text>
 *     </g>
 *     <path d="M270 41h10"/>
 *     <path d="M280 41h10"/>
 *     <g>
 *      <rect height="22" width="104" x="290" y="30"/>
 *      <a xlink:href="#whitespace"><text x="342" y="45">WHITESPACE*</text></a>
 *     </g>
 *     <path d="M394 41h10"/>
 *     <g>
 *      <path d="M404 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *      <g>
 *       <path d="M424.0 21h296"/>
 *      </g>
 *      <path d="M720.0 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *      <path d="M404.0 41h20"/>
 *      <g>
 *       <g>
 *        <path d="M424 41h20"/>
 *        <g>
 *         <path d="M444 41h8"/>
 *         <path d="M568 41h8"/>
 *         <rect height="22" width="116" x="452" y="30"/>
 *         <a xlink:href="#url-unquoted"><text x="510" y="45">URL-UNQUOTED</text></a>
 *        </g>
 *        <path d="M576 41h20"/>
 *        <path d="M424 41a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *        <g>
 *         <path d="M444 71h10"/>
 *         <path d="M566 71h10"/>
 *         <rect height="22" width="116" x="452" y="60"/>
 *         <a xlink:href="#string"><text x="510" y="75">STRING</text></a>
 *        </g>
 *        <path d="M576 71a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *       </g>
 *       <path d="M596 41h10"/>
 *       <g>
 *        <rect height="22" width="104" x="606" y="30"/>
 *        <a xlink:href="#whitespace"><text x="658" y="45">WHITESPACE*</text></a>
 *       </g>
 *       <path d="M710.0 41h10"/>
 *      </g>
 *      <path d="M720.0 41h20"/>
 *     </g>
 *     <path d="M740 41h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="750" y="30"/>
 *      <text x="764" y="45">)</text>
 *     </g>
 *     <path d="M778 41h10"/>
 *     <path d="M 788 41 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The URL token is quite peculiar in CSS. This keyword was available
 * since CSS 1 which did not really offer functions per se. For that
 * reason it allows some backward compatible syntax which would certainly
 * be quite different in CSS 3 had they chosen to not allow as is URLs
 * to be entered (i.e. only allow quoted URLs.)
 *
 * Also because of that, the URL is a special token and not a function.
 * Note that the syntax allows for an empty URL, which is important
 * to be able to cancel a previous URL definition (overwrite a background
 * image with nothing.)
*
 * \code{.css}
 *      url(/images/background.png)
 *      url('images/background.png')
 * \endcode
 *
 * \section url_unquoted URL Unquoted "URL-UNQUOTED" (CSS 3)
 *
 * \htmlonly
 *  <div class=railroad>
 *   <svg class=railroad-diagram height=100 viewbox="0 0 509 100" width=509>
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <path d="M50.0 31h10"/>
 *      <g>
 *       <path d="M60.0 31h20"/>
 *       <g transform="translate(0 0)">
 *        <rect height="22" width="348" x="80" y="20"/>
 *        <text x="140" y="35">not " &apos; ( ) \ </text>
 *        <a xlink:href="#whitespace"><text x="244.5" y="35">WHITESPACE</text></a>
 *        <text x="297" y="35">or</text>
 *        <a xlink:href="#non-printable"><text x="361" y="35">NON-PRINTABLE</text></a>
 *       </g>
 *       <path d="M428.0 31h20"/>
 *       <path d="M60.0 31a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M80.0 61h140.0"/>
 *        <path d="M288.0 61h140.0"/>
 *        <rect height="22" width="68" x="220" y="50"/>
 *        <a xlink:href="#escape"><text x=254.0 y=65>ESCAPE</text></a>
 *       </g>
 *       <path d="M428.0 61a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *      </g>
 *      <path d="M448.0 31h10"/>
 *      <path d="M60.0 31a10 10 0 0 0 -10 10v29a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M60.0 80h388"/>
 *      </g>
 *      <path d="M448.0 80a10 10 0 0 0 10 -10v-29a10 10 0 0 0 -10 -10"/>
 *     </g>
 *     <path d="M458 31h10"/>
 *     <path d="M 468 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The URL can be nearly any kind of characters except spaces, parenthesis,
 * quotes, and the backslash. To include such character you may either
 * ESCAPE them or use quotes.
 *
 * \code{.css}
 *      url( It\'s\ the\ same\ as... )
 *      url( "It's the same as..." )
 * \endcode
 *
 * \section number Number "NUMBER", "INTEGER", "DECIMAL_NUMBER" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="179" viewbox="0 0 713 179" width="713">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 50 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <g>
 *
 *      <path d="M40.0 60a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *      <g>
 *       <rect height="22" rx="10" ry="10" width="28" x="60" y="29"/>
 *       <text x="74" y="44">+</text>
 *      </g>
 *      <path d="M88 40a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *
 *      <path d="M40 60h20"/>
 *      <g>
 *       <path d="M60 60h28"/>
 *      </g>
 *      <path d="M88 60h20"/>
 *
 *      <path d="M40 60a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *      <g>
 *       <rect height="22" rx="10" ry="10" width="28" x="60" y="69"/>
 *       <text x="74" y="84">-</text>
 *      </g>
 *      <path d="M88.0 80a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *
 *     </g>
 *     <g>
 *      <path d="M108.0 60h20"/>
 *      <g>
 *       <path d="M128.0 60h10"/>
 *       <g>
 *        <path d="M138.0 60h10"/>
 *        <g>
 *         <rect height="22" width="60" x="148" y="49"/>
 *         <a xlink:href="#digit"><text x="178" y="64">DIGIT</text></a>
 *        </g>
 *        <path d="M208.0 60h10"/>
 *        <path d="M148.0 60a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *        <g>
 *         <path d="M148.0 80h60"/>
 *        </g>
 *        <path d="M208.0 80a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *       </g>
 *       <path d="M218.0 60h10"/>
 *       <path d="M228.0 60h10"/>
 *       <g>
 *        <rect height="22" rx="10" ry="10" width="28" x="238" y="49"/>
 *        <text x="252" y="64">.</text>
 *       </g>
 *       <path d="M266.0 60h10"/>
 *       <path d="M276.0 60h10"/>
 *       <g>
 *        <path d="M286.0 60h10"/>
 *        <g>
 *         <rect height="22" width="60" x="296.0" y="49"/>
 *         <a xlink:href="#digit"><text x="326" y="64">DIGIT</text></a>
 *        </g>
 *        <path d="M356.0 60h10"/>
 *        <path d="M296.0 60a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *        <g>
 *         <path d="M296.0 80h60"/>
 *        </g>
 *        <path d="M356.0 80a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *       </g>
 *       <path d="M366.0 60h10"/>
 *      </g>
 *      <path d="M376.0 60h20"/>
 *      <path d="M108.0 60a10 10 0 0 1 10 10v19a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M128.0 99h84.0"/>
 *       <path d="M292.0 99h84.0"/>
 *       <path d="M212.0 99h10"/>
 *       <g>
 *        <rect height="22" width="60" x="222.0" y="88"/>
 *        <a xlink:href="#digit"><text x="252" y="103">DIGIT</text></a>
 *       </g>
 *       <path d="M282.0 99h10"/>
 *       <path d="M222.0 99a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M222.0 119h60"/>
 *       </g>
 *       <path d="M282.0 119a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M376.0 99a10 10 0 0 0 10 -10v-19a10 10 0 0 1 10 -10"/>
 *      <path d="M108.0 60a10 10 0 0 1 10 10v58a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M128 138h50"/>
 *       <path d="M326.0 138h50.0"/>
 *       <path d="M178.0 138h10"/>
 *       <g>
 *        <rect height="22" rx="10" ry="10" width="28" x="188" y="127"/>
 *        <text x="202" y="142">.</text>
 *       </g>
 *       <path d="M216.0 138h10"/>
 *       <path d="M226.0 138h10"/>
 *       <g>
 *        <path d="M236.0 138h10"/>
 *        <g>
 *         <rect height="22" width="60" x="246" y="127"/>
 *         <a xlink:href="#digit"><text x="276" y="142">DIGIT</text></a>
 *        </g>
 *        <path d="M306.0 138h10"/>
 *        <path d="M246.0 138a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *        <g>
 *         <path d="M246.0 158h60"/>
 *        </g>
 *        <path d="M306.0 158a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *       </g>
 *       <path d="M316.0 138h10"/>
 *      </g>
 *      <path d="M376.0 138a10 10 0 0 0 10 -10v-58a10 10 0 0 1 10 -10"/>
 *     </g>
 *     <g>
 *      <path d="M396.0 60h20"/>
 *      <g>
 *       <path d="M416.0 60h236"/>
 *      </g>
 *      <path d="M652.0 60h20"/>
 *      <path d="M396.0 60a10 10 0 0 1 10 10v28a10 10 0 0 0 10 10"/>
 *      <g>
 *       <g>
 *        <path d="M416.0 108h20"/>
 *        <g>
 *         <path d="M436.0 108h0.0"/>
 *         <path d="M464.0 108h0.0"/>
 *         <rect height="22" rx="10" ry="10" width="28" x="436" y="97"/>
 *         <text x="450" y="112">e</text>
 *        </g>
 *        <path d="M464.0 108h20"/>
 *        <path d="M416.0 108a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *        <g>
 *         <rect height="22" rx="10" ry="10" width="28" x="436" y="127"/>
 *         <text x="450" y="142">E</text>
 *        </g>
 *        <path d="M464.0 138a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *       </g>
 *       <g>
 *        <path d="M484.0 108a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *        <g>
 *         <rect height="22" rx="10" ry="10" width="28" x="504" y="77"/>
 *         <text x="518.0" y="92">+</text>
 *        </g>
 *        <path d="M532.0 88a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *        <path d="M484.0 108h20"/>
 *        <g>
 *         <path d="M504.0 108h28"/>
 *        </g>
 *        <path d="M532.0 108h20"/>
 *        <path d="M484.0 108a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *        <g>
 *         <rect height="22" rx="10" ry="10" width="28" x="504" y="117"/>
 *         <text x="518.0" y="132">-</text>
 *        </g>
 *        <path d="M532.0 128a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *       </g>
 *       <path d="M552.0 108h10"/>
 *       <g>
 *        <path d="M562.0 108h10"/>
 *        <g>
 *         <rect height="22" width="60" x="572.0" y="97"/>
 *         <a xlink:href="#digit"><text x="602.0" y="112">DIGIT</text></a>
 *        </g>
 *        <path d="M632.0 108h10"/>
 *        <path d="M572.0 108a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *        <g>
 *         <path d="M572.0 128h60"/>
 *        </g>
 *        <path d="M632.0 128a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *       </g>
 *       <path d="M642.0 108h10"/>
 *      </g>
 *      <path d="M652.0 108a10 10 0 0 0 10 -10v-28a10 10 0 0 1 10 -10"/>
 *     </g>
 *     <path d="M 672 60 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * CSS 3 distinguishes between integers and floating points, only the
 * definition of an integer is just a floating point with no decimal
 * digits after the period and no exponent.
 *
 * The CSS Preprocessor lexer returns two different types of tokens:
 * INTEGER and DECIMAL_NUMBER. The compiler may force the use of one
 * or the other in a few places where the type has to be an INTEGER
 * or a DECIMAL_NUMBER. For example, a PERCENT number always uses a
 * DECIMAL_NUMBER.
 *
 * The CSS 3 lexer is expected to include the signs as part of a
 * number (to simplify the rest of the grammar.) This is important
 * because otherwise rules such as a background field would look
 * like expressions:
 *
 * \code{.css}
 *      background: transparent url(images/example.png) +3px -5px;
 * \endcode
 *
 * Here the +3px and -5px are viewed as two distinct numbers. If
 * we were to make the + and - operators instead of part of the
 * numbers, these two numbers would look like a subtraction
 * (3px - 5px). When you write expressions, you should anyway always
 * add spaces around your operators. Another one that may get you
 * is negating the result of a function call. Without the space the
 * dash becomes part of the function name. In the following, you
 * are calling a function named 'color-saturate' instead of
 * subtracting 'saturate($color, -33%)' from '$color':
 *
 * \code{.scss}
 *      background-color: $color-saturate($color, -33%);
 * \endcode
 *
 * The correct expression would be:
 *
 * \code{.scss}
 *      background-color: $color - saturate($color, -33%);
 * \endcode
 *
 * Example of numbers:
 *
 * \code{.css}
 *      0
 *      -1.3
 *      .55
 *      +801
 *      9e3
 *      -1.001e+4
 *      .45e-7
 * \endcode
 *
 * \note
 * The number of decimal digits is limited to 20. If you write a number
 * with more decimal digits after the decimal point, then an error is
 * generated.
 *
 * \warning
 * When a number includes a decimal point or an exponent, it is
 * considered to be a DECIMAL_NUMBER even when that number is
 * otherwise an integer (1.001e4 = 10010 which is an integer.)
 *
 * \section dimension Dimension "DIMENSION" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 377 62" width="377">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" width="132" x="50" y="20"/>
 *      <a xlink:href="#number"><text x="116.0" y="35">NUMBER</text></a>
 *     </g>
 *     <path d="M182 31h10"/>
 *     <path d="M192 31h10"/>
 *     <g>
 *      <rect height="22" width="124" x="202.0" y="20"/>
 *      <a xlink:href="#identifier"><text x="264.0" y="35">IDENTIFIER</text></a>
 *     </g>
 *     <path d="M326 31h10"/>
 *     <path d="M 336 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * When a number is immediately followed by an identifier, the result is a
 * dimension.
 *
 * Note that the name of a dimension can start with the character 'e'
 * (i.e. "13em",) however, if the character 'e' is followed by a sign
 * ('+' or '-') or a \ref digit "DIGIT", then the 'e' is taken as the
 * exponent character.
 *
 * \warning
 * Note that "-" by itself is not a valid identifier. This means a number
 * followed by a dash and another number is clearly a subtraction.
 *
 * \code{.css}
 *      font-size: 12pt;
 *      height: 13px;
 *      width: 3em;
 * \endcode
 *
 * The lexer let you enter any dimension. At some point the compiler will
 * make sure that all dimensions are understood by CSS. That being said,
 * the CSS Proprocessor is likely to understand many other dimensions and
 * convert to on that CSS 3 understands.
 *
 * You may find a complete list of supported CSS 3 dimmensions here:
 * http://www.w3.org/TR/css3-values/
 *
 * \section percent Percent "PERCENT" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 281 62" width="281">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" width="132" x="50" y="20"/>
 *      <a xlink:href="#number"><text x="116" y="35">DECIMAL_NUMBER</text></a>
 *     </g>
 *     <path d="M182 31h10"/>
 *     <path d="M192 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="202" y="20"/>
 *      <text x="216.0" y="35">%</text>
 *     </g>
 *     <path d="M230 31h10"/>
 *     <path d="M 240 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * A PERCENT is a number immediately followed by the '%' character.
 * Internally a PERCENT is always represented as a decimal number,
 * even if the number was an integer (integers are automatically
 * converted as required.)
 *
 * \code{.css}
 *      33.33%
 *      100%
 *      2.25%
 * \endcode
 *
 * Note that the percent character can be appended using the escape
 * character. In that case, it is viewed as a dimension which will
 * fail validation.
 *
 * \code{.scss}
 *      // the following two numbers are DIMENSIONs, not PERCENT
 *      33.33\%
 *      100\25
 * \endcode
 *
 * \section unicode_range Unicode Range "UNICODE_RANGE" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="222" viewbox="0 0 600 222" width="600">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <g>
 *      <path d="M40.0 31h20"/>
 *      <g>
 *       <rect height="22" rx="10" ry="10" width="28" x="60" y="20"/>
 *       <text x="74.0" y="35">U</text>
 *      </g>
 *      <path d="M88.0 31h20"/>
 *      <path d="M40.0 31a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *      <g>
 *       <rect height="22" rx="10" ry="10" width="28" x="60" y="50"/>
 *       <text x="74.0" y="65">u</text>
 *      </g>
 *      <path d="M88.0 61a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *     </g>
 *     <path d="M108 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="118" y="20"/>
 *      <text x="132" y="35">+</text>
 *     </g>
 *     <path d="M146 31h10"/>
 *     <g>
 *      <path d="M156.0 31h20"/>
 *      <g>
 *       <path d="M176.0 31h125.5"/>
 *       <path d="M413.5 31h125.5"/>
 *       <path d="M301.5 31h10"/>
 *       <g>
 *        <rect height="22" width="92" x="311.5" y="20"/>
 *        <a xlink:href="#hexdigit"><text x="357.5" y="35">HEXDIGIT</text></a>
 *       </g>
 *       <path d="M403.5 31h10"/>
 *       <path d="M311.5 31a10 10 0 0 0 -10 10v10a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M311.5 61h9.5"/>
 *        <path d="M394.0 61h9.5"/>
 *        <text class="comment" x="357.5" y="66">1-6 times</text>
 *       </g>
 *       <path d="M403.5 61a10 10 0 0 0 10 -10v-10a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M539.0 31h20"/>
 *      <path d="M156.0 31a10 10 0 0 1 10 10v50a10 10 0 0 0 10 10"/>
 *      <g>
 *       <g>
 *        <path d="M176.0 101a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *        <g>
 *         <path d="M196.0 81h112"/>
 *        </g>
 *        <path d="M308.0 81a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *        <path d="M176.0 101h20"/>
 *        <g>
 *         <path d="M196.0 101h10"/>
 *         <g>
 *          <rect height="22" width="92" x="206.0" y="90"/>
 *          <a xlink:href="#hexdigit"><text x="252.0" y="105">HEXDIGIT</text></a>
 *         </g>
 *         <path d="M298.0 101h10"/>
 *         <path d="M206.0 101a10 10 0 0 0 -10 10v10a10 10 0 0 0 10 10"/>
 *         <g>
 *          <path d="M206.0 131h9.5"/>
 *          <path d="M288.5 131h9.5"/>
 *          <text class="comment" x="252" y="136">1-5 times</text>
 *         </g>
 *         <path d="M298.0 131a10 10 0 0 0 10 -10v-10a10 10 0 0 0 -10 -10"/>
 *        </g>
 *        <path d="M308.0 101h20"/>
 *       </g>
 *       <path d="M328.0 101h10"/>
 *       <g>
 *        <path d="M338.0 101h10"/>
 *        <g>
 *         <path d="M348.0 101h71.5"/>
 *         <path d="M447.5 101h71.5"/>
 *         <rect height="22" rx="10" ry="10" width="28" x="419" y="90"/>
 *         <text x="433.5" y="105">?</text>
 *        </g>
 *        <path d="M519.0 101h10"/>
 *        <path d="M348.0 101a10 10 0 0 0 -10 10v10a10 10 0 0 0 10 10"/>
 *        <g>
 *         <text class="comment" x="433.5" y="136">1 to (6 - digits) times</text>
 *        </g>
 *        <path d="M519.0 131a10 10 0 0 0 10 -10v-10a10 10 0 0 0 -10 -10"/>
 *       </g>
 *       <path d="M529.0 101h10"/>
 *      </g>
 *      <path d="M539.0 101a10 10 0 0 0 10 -10v-50a10 10 0 0 1 10 -10"/>
 *      <path d="M156.0 31a10 10 0 0 1 10 10v120a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M176.0 171h25.5"/>
 *       <path d="M513.5 171h25.5"/>
 *       <path d="M201.5 171h10"/>
 *       <g>
 *        <path d="M211.5 171h10"/>
 *        <g>
 *         <rect height="22" width="92" x="221.5" y="160"/>
 *         <a xlink:href="#hexdigit"><text x="267.5" y="175">HEXDIGIT</text></a>
 *        </g>
 *        <path d="M313.5 171h10"/>
 *        <path d="M221.5 171a10 10 0 0 0 -10 10v10a10 10 0 0 0 10 10"/>
 *        <g>
 *         <path d="M221.5 201h9.5"/>
 *         <path d="M304.0 201h9.5"/>
 *         <text class="comment" x="267.5" y="206">1-6 times</text>
 *        </g>
 *        <path d="M313.5 201a10 10 0 0 0 10 -10v-10a10 10 0 0 0 -10 -10"/>
 *       </g>
 *       <path d="M323.5 171h10"/>
 *       <path d="M333.5 171h10"/>
 *       <g>
 *        <rect height="22" rx="10" ry="10" width="28" x="343" y="160"/>
 *        <text x="357.5" y="175">-</text>
 *       </g>
 *       <path d="M371.5 171h10"/>
 *       <path d="M381.5 171h10"/>
 *       <g>
 *        <path d="M391.5 171h10"/>
 *        <g>
 *         <rect height="22" width="92" x="401.5" y="160"/>
 *         <a xlink:href="#hexdigit"><text x="447.5" y="175">HEXDIGIT</text></a>
 *        </g>
 *        <path d="M493.5 171h10"/>
 *        <path d="M401.5 171a10 10 0 0 0 -10 10v10a10 10 0 0 0 10 10"/>
 *        <g>
 *         <path d="M401.5 201h9.5"/>
 *         <path d="M484.0 201h9.5"/>
 *         <text class="comment" x="447.5" y="206">1-6 times</text>
 *        </g>
 *        <path d="M493.5 201a10 10 0 0 0 10 -10v-10a10 10 0 0 0 -10 -10"/>
 *       </g>
 *       <path d="M503.5 171h10"/>
 *      </g>
 *      <path d="M539.0 171a10 10 0 0 0 10 -10v-120a10 10 0 0 1 10 -10"/>
 *     </g>
 *     <path d="M 559 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Define a range of Unicode characters from their code points. The
 * expression allows for:
 *
 * \li One specific code point (U+<######>);
 * \li A mask with all the code points that match (U+<###>???);
 * \li A range with a start and an end code point (U+<######>-<######>).
 *
 * The mask mechanism actually generates a range like the third syntax,
 * only it replaces the '?' character with '0' for the start code point
 * and with 'f' for the end code point.
 *
 * \code{.scss}
 *      U+ff            // characters from 0xFF to 0xFF
 *      U+3??           // characters from 0x300 to 0x3FF
 *      U+320-34F       // characters from 0x320 to 0x34F explicitly
 * \endcode
 *
 * \note
 * The lexer makes use of the csspp::unicode_range_t class to record these
 * values in a \ref unicode-range "UNICODE_RANGE" node. The range is
 * then compressed and saved in one 64 bit number.
 *
 * A Unicode range is used by @font-face definitions to limit the number
 * of characters to be loaded for a page.
 *
 * \section include_match Include Match "INCLUDE_MATCH" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 137 62" width="137">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50" y="20"/>
 *      <text x="68.0" y="35">~=</text>
 *     </g>
 *     <path d="M86 31h10"/>
 *     <path d="M 96 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Match when the parameter on the right is included in the list of
 * parameters found on the left. The value on the left is a list of
 * whitespace separated words (i.e. a list of classes).
 *
 * \code{.scss}
 *      a[class ~= "green"]   // equivalent to a.green
 * \endcode
 *
 * \section dash_match Dash Match "DASH_MATCH" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 137 62" width="137">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50" y="20"/>
 *      <text x="68.0" y="35">|=</text>
 *     </g>
 *     <path d="M86 31h10"/>
 *     <path d="M 96 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Match when the first element of the hyphen separated list of words on
 * the left is equal to the value on the right.
 *
 * \code{.scss}
 *      [lang |= "en"]   // match lang="en-US"
 * \endcode
 *
 * \section prefix_match Prefix Match "PREFIX_MATCH" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 137 62" width="137">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50" y="20"/>
 *      <text x="68.0" y="35">^=</text>
 *     </g>
 *     <path d="M86 31h10"/>
 *     <path d="M 96 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Match when the value on the left starts with the value on the right.
 *
 * \code{.scss}
 *      a[entity ^= 'blue']  // match entity="blue-laggoon"
 * \endcode
 *
 * \section suffix_match Suffix Match "SUFFIX_MATCH" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 137 62" width="137">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50" y="20"/>
 *      <text x="68.0" y="35">$=</text>
 *     </g>
 *     <path d="M86 31h10"/>
 *     <path d="M 96 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Match when the value on the left ends with the value on the right.
 *
 * \code{.scss}
 *      a[entity $= 'laggoon']  // match entity="blue-laggoon"
 * \endcode
 *
 * \section substring_match Substring Match "SUBSTRING_MATCH" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 137 62" width="137">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50" y="20"/>
 *      <text x="68.0" y="35">*=</text>
 *     </g>
 *     <path d="M86 31h10"/>
 *     <path d="M 96 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Match when the value on the right is found in the value on the left.
 *
 * \code{.scss}
 *      a[entity *= '-lag']  // match entity="blue-laggoon"
 * \endcode
 *
 * \section column Colunm Match "COLUMN" (CSS 3 Token, CSS Preprocessor Extension)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 137 62" width="137">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50" y="20"/>
 *      <text x="68.0" y="35">||</text>
 *     </g>
 *     <path d="M86 31h10"/>
 *     <path d="M 96 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The CSS 3 documentation says:
 *
 * \par
 * \<column-token> has been added, to keep Selectors parsing in
 * single-token lookahead. 
 *
 * At this point I am not too sure whether that means it is only a lexer
 * artifact or whether it would be an operator people can use.
 *
 * \sa http://stackoverflow.com/questions/30702971/how-is-the-operator-used-in-css
 *
 * All of that being said, since we support the \ref logical_and, we accept
 * this operator as the Logical OR in our expressions.
 *
 * The OR operator takes two boolean value. If at least one of these
 * boolean value is true, then the result is true, otherwise it is false.
 *
 * You may also use the 'or' identifier.
 *
 * \code{.scss}
 *      width: $bool1 || $bool2 ? $left-column : $right-column;
 *          // or
 *      width: $bool1 or $bool2 ? $left-column : $right-column;
 * \endcode
 *
 * \section logical_and Logical AND "AND" (CSS Preprocessor extension)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 137 62" width="137">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50" y="20"/>
 *      <text x="68.0" y="35">&amp;&amp;</text>
 *     </g>
 *     <path d="M86 31h10"/>
 *     <path d="M 96 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The '&&' operator is the logical AND operator. It returns true when
 * its left and right handsides are both set to true.
 *
 * You may also use the 'and' identifier.
 *
 * \code{.scss}
 *      width: $bool1 && $bool2 ? $left-column : $right-column;
 *          // or
 *      width: $bool1 and $bool2 ? $left-column : $right-column;
 * \endcode
 *
 * \sa \ref column
 *
 * \section double_equal Double Equal "EQUAL" (CSS Preprocessor extension)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 137 62" width="137">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50" y="20"/>
 *      <text x="68.0" y="35">==</text>
 *     </g>
 *     <path d="M86 31h10"/>
 *     <path d="M 96 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * CSS 3 clearly uses '=' to test for equality. Somehow, SASS added '=='
 * which is really not consistent. To be more compatible with SASS, we
 * support both. At this point we do not warn or anything when '==' is
 * found. We may do so later. Internally, we immediately convert '=='
 * to the exact same token as '='.
 *
 * \section not_equal Not Equal "NOT_EQUAL" (CSS Preprocessor extension)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 137 62" width="137">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50" y="20"/>
 *      <text x="68.0" y="35">!=</text>
 *     </g>
 *     <path d="M86 31h10"/>
 *     <path d="M 96 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * We offer a 'not equal' operator for our expressions and also attributes.
 *
 * \code{.scss}
 *      color: $var != 3 ? red : blue;
 *         and
 *      p[book!='family'] { display: none; }
 * \endcode
 *
 * The attribute extension is converted by the compiler to valid CSS as
 * in:
 *
 * \code{.css}
 *      p:not([book='family']) { display: none; }
 * \endcode
 *
 * \section assignment Assignment "ASSIGNMENT" (CSS Preprocessor extension)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 137 62" width="137">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50" y="20"/>
 *      <text x="68.0" y="35">:=</text>
 *     </g>
 *     <path d="M86 31h10"/>
 *     <path d="M 96 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * We added the ':=' operator to allow one to set a variable within an
 * expression. For example, you could write an assignment of a long
 * expression, then reuse that value many times in the rest of the
 * expression:
 *
 * \code{.scss}
 *      (value := rather + complicated * expression ** here,
 *       display(value * value), enlarge(value))
 * \endcode
 *
 * \section less_equal Less or Equal To "LESS_EQUAL" (CSS Preprocessor extension)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 137 62" width="137">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50" y="20"/>
 *      <text x="68.0" y="35">&lt;=</text>
 *     </g>
 *     <path d="M86 31h10"/>
 *     <path d="M 96 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * We added the '\<=' operator to allow one to compare values in an
 * expression between each others.
 *
 * \code{.scss}
 *      width: $left-column <= $right-column ? $left-column : $right-column;
 *
 *      // which in this case is equivalent to:
 *      width: min($left-column, $right-column);
 * \endcode
 *
 * \section greater_equal Greater or Equal To "GREATER_EQUAL" (CSS Preprocessor extension)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 137 62" width="137">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50" y="20"/>
 *      <text x="68.0" y="35">&gt;=</text>
 *     </g>
 *     <path d="M86 31h10"/>
 *     <path d="M 96 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * We added the '>=' operator to allow one to compare values in an
 * expression between each others.
 *
 * \code{.scss}
 *      width: $left-column >= $right-column ? $left-column : $right-column;
 *
 *      // which in this case is equivalent to:
 *      width: max($left-column, $right-column);
 * \endcode
 *
 * \section power Power "POWER" (CSS Preprocessor extension)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 137 62" width="137">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="36" x="50" y="20"/>
 *      <text x="68.0" y="35">**</text>
 *     </g>
 *     <path d="M86 31h10"/>
 *     <path d="M 96 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The '**' operator is an extension that allow you to caculate the power
 * of a number (left hand side) by another (right hand side). Note that
 * dimensions (numbers with a unit) cannot be used with the '**' operator.
 *
 * \code{.scss}
 *      width: 1px * 2 ** 5;
 * \endcode
 *
 * \section cdo Comment Document Open "CDO" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 153 62" width="153">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="52" x="50" y="20"/>
 *      <text x="76.0" y="35">&lt;!--</text>
 *     </g>
 *     <path d="M102 31h10"/>
 *     <path d="M 112 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The Comment Document Open is understood so that way it can be skipped
 * when reading a block of data coming from an HTML \<style> tag.
 *
 * \section cdc Comment Document Close "CDC" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 153 62" width="153">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="52" x="50" y="20"/>
 *      <text x="76.0" y="35">--&gt;</text>
 *     </g>
 *     <path d="M102 31h10"/>
 *     <path d="M 112 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The Comment Document Close is understood so that way it can be skipped
 * when reading a block of data coming from an HTML \<style> tag.
 *
 * \section delimiter Delimiter "DELIMITER" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 197 62" width="197">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="96" x="50" y="20"/>
 *      <a xlink:href="#anything"><text x="98.0" y="35">ANYTHING</text></a>
 *     </g>
 *     <path d="M146 31h10"/>
 *     <path d="M 156 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The <a href="#delimiter">DELIMITER</a> is activated for any character
 * that does not activate any other lexer rule.
 *
 * For example, a period that is not followed by a <a href="#digit">DIGIT</a>
 * is returned as itself. The grammar generally shows delimiters using a
 * simple quoted string rather than its node_type_t name.
 *
 * The delimiters actually return a specific node_type_t value for each
 * one of these characters:
 *
 * \li = -- EQUAL
 * \li , -- COMMA
 * \li : -- COLON
 * \li ; -- SEMICOLON
 * \li ! -- EXCLAMATION
 * \li ? -- CONDITIONAL
 * \li &gt; -- GREATER_THAN
 * \li ( -- OPEN_PARENTHESIS
 * \li ) -- CLOSE_PARENTHESIS
 * \li [ -- OPEN_SQUAREBRACKET
 * \li ] -- CLOSE_SQUAREBRACKET
 * \li { -- OPEN_CURVLYBRACKET
 * \li } -- CLOSE_CURVLYBRACKET
 * \li . -- PERIOD
 * \li & -- REFERENCE
 * \li &lt; -- LESS
 * \li + -- ADD
 * \li - -- SUBTRACT (if by itself or at least not followed by an identifier)
 * \li $ -- DOLLAR
 * \li ~ -- PRECEDED
 * \li * -- MULTIPLY
 * \li | -- SCOPE
 * \li / -- DIVIDE
 * \li % -- MODULO
 *
 * Any character that does not match one of these DELIMITER characters,
 * or another lexer token, generates an immediate lexer error.
 *
 * \note
 * The EXCLAMATION is returned as a simple token by the lexer. The
 * parser will convert it to a form of identifier unless it is not
 * followed by an identifier in which case an error is generated.
 * The parser will also take care of removing whitespaces.
 *
 * \note
 * The DIVIDE character is viewed as a \em standard CSS 3 separator when
 * used in the font field (actually, any field that match as defined in
 * the scripts/validation/has-font-metrics.scss script) as in:
 *
 * \code{.scss}
 *      font: 17px/1.3em helvetica;
 * \endcode
 *
 * \note
 * The lexer cannot know what to do with the DIVIDE. The compiler,
 * however, knows at the time it runs the expression since it
 * has the name of the field name 'font'. In that case it tells the
 * expression class to handle the DIVIDE as a CSS 3 separator. That
 * mean the sequence \<number> / \<number> will generate the token
 * FONT_METRICS.
 *
 * \note
 * In any other field, that do not have a name that matches, the slash
 * is viewed as a regular DIVIDE operator, so \<number> / \<number> is
 * calculated and the result of the operation is returned.
 *
 * \note
 * In order to issue a division in a font field, one can use parenthesis:
 *
 * \code
 *      $height: 480px;
 *      font: ($height / 32) / 1.3em helvetica;
 * \endcode
 */

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et syntax=doxygen
