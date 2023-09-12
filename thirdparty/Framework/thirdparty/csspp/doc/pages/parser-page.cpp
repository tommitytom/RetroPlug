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

/** \page parser_rules Parser Rules
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
 * The parser is composed of the following rules:
 *
 * \section parser_input Parser Input (CSS Preprocessor Detail)
 *
 * The parser input are nodes representing tokens as returned by a
 * lexer object.
 *
 * The parser has multiple entry points to accomodate the various
 * type of input data a CSS parser is expected to support. These
 * are:
 *
 * \li Style Tag -- in an HTML document, data in:
 * \code
 *      <style> ... </style>
 * \endcode
 * \li Inline Style -- in an HTML document, style found in the style attribute:
 * \code
 *      <div style="background: red">...</div>
 * \endcode
 * \li CSS file -- a .css file with CSS rules
 *
 * \section stylesheet Stylesheet "stylesheet" and "stylesheet-list" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="200" viewbox="0 0 345 200" width="345">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 121 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <g>
 *      <path d="M40.0 131a10 10 0 0 0 10 -10v-89a10 10 0 0 1 10 -10"/>
 *      <g>
 *       <path d="M60.0 22h224"/>
 *      </g>
 *      <path d="M284.0 22a10 10 0 0 1 10 10v89a10 10 0 0 0 10 10"/>
 *      <path d="M40.0 131h20"/>
 *      <g>
 *       <path d="M60.0 131h10"/>
 *       <g>
 *        <path d="M70.0 131a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *        <g>
 *         <rect height="22" width="164" x="90.0" y="90"/>
 *         <a xlink:href="lexer_rules.html#whitespace"><text x="172.0" y="105">WHITESPACE</text></a>
 *        </g>
 *        <path d="M254.0 101a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *        <path d="M70.0 131a10 10 0 0 0 10 -10v-40a10 10 0 0 1 10 -10"/>
 *        <g>
 *         <path d="M90.0 71h28.0"/>
 *         <path d="M226.0 71h28.0"/>
 *         <rect height="22" width="108" x="118.0" y="60"/>
 *         <a xlink:href="lexer_rules.html#cdc"><text x="172.0" y="75">CDC</text></a>
 *        </g>
 *        <path d="M254.0 71a10 10 0 0 1 10 10v40a10 10 0 0 0 10 10"/>
 *        <path d="M70.0 131a10 10 0 0 0 10 -10v-70a10 10 0 0 1 10 -10"/>
 *        <g>
 *         <path d="M90.0 41h28.0"/>
 *         <path d="M226.0 41h28.0"/>
 *         <rect height="22" width="108" x="118.0" y="30"/>
 *         <a xlink:href="lexer_rules.html#cdo"><text x="172.0" y="45">CDO</text></a>
 *        </g>
 *        <path d="M254.0 41a10 10 0 0 1 10 10v70a10 10 0 0 0 10 10"/>
 *        <path d="M70.0 131h20"/>
 *        <g>
 *         <path d="M90.0 131h16.0"/>
 *         <path d="M238.0 131h16.0"/>
 *         <rect height="22" width="132" x="106.0" y="120"/>
 *         <a xlink:href="#qualified-rule"><text x="172.0" y="135">qualified-rule</text></a>
 *        </g>
 *        <path d="M254.0 131h20"/>
 *        <path d="M70.0 131a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *        <g>
 *         <path d="M90.0 161h44.0"/>
 *         <path d="M210.0 161h44.0"/>
 *         <rect height="22" width="76" x="134.0" y="150"/>
 *         <a xlink:href="#at-rule"><text x="172.0" y="165">at-rule</text></a>
 *        </g>
 *        <path d="M254.0 161a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *       </g>
 *       <path d="M274.0 131h10"/>
 *       <path d="M70.0 131a10 10 0 0 0 -10 10v29a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M70.0 180h204"/>
 *       </g>
 *       <path d="M274.0 180a10 10 0 0 0 10 -10v-29a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M284.0 131h20"/>
 *     </g>
 *     <path d="M 304 131 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The stylesheet is an entry point used to parse rules found in a
 * \<style> tag. This includes rules such as the CDO and CDC which
 * allow the user to write the style data in what looks like a comment:
 *
 * \code
 *  <style>
 *    <!--
 *      div { margin: 0 }
 *    -->
 *  </style>
 * \endcode
 *
 * Other documents do not accept the CDO and CDC tokens.
 *
 * There are our corresponding YACC-like rules:
 *
 * \code
 *  stylesheet: <empty>
 *            | stylesheet-list
 *  
 *  stylesheet-list: CDO
 *                 | CDC
 *                 | WHITESPACE
 *                 | qualified-rule
 *                 | at-rule
 *                 | stylesheet-list stylesheet-list
 * \endcode
 *
 * \section rule_list Rule List "rule" and "rule-list" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="140" viewbox="0 0 345 140" width="345">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 61 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <g>
 *      <path d="M40.0 71a10 10 0 0 0 10 -10v-29a10 10 0 0 1 10 -10"/>
 *      <g>
 *       <path d="M60.0 22h224"/>
 *      </g>
 *      <path d="M284.0 22a10 10 0 0 1 10 10v29a10 10 0 0 0 10 10"/>
 *      <path d="M40.0 71h20"/>
 *      <g>
 *       <path d="M60.0 71h10"/>
 *       <g>
 *        <path d="M70.0 71a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *        <g>
 *         <rect height="22" width="164" x="90.0" y="30"/>
 *         <a xlink:href="lexer_rules.html#whitespace"><text x="172.0" y="45">WHITESPACE</text></a>
 *        </g>
 *        <path d="M254.0 41a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *        <path d="M70.0 71h20"/>
 *        <g>
 *         <path d="M90.0 71h16.0"/>
 *         <path d="M238.0 71h16.0"/>
 *         <rect height="22" width="132" x="106.0" y="60"/>
 *         <a xlink:href="#qualified-rule"><text x="172.0" y="75">qualified-rule</text></a>
 *        </g>
 *        <path d="M254.0 71h20"/>
 *        <path d="M70.0 71a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *        <g>
 *         <path d="M90.0 101h44.0"/>
 *         <path d="M210.0 101h44.0"/>
 *         <rect height="22" width="76" x="134.0" y="90"/>
 *         <a xlink:href="#at-rule"><text x="172.0" y="105">at-rule</text></a>
 *        </g>
 *        <path d="M254.0 101a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *       </g>
 *       <path d="M274.0 71h10"/>
 *       <path d="M70.0 71a10 10 0 0 0 -10 10v29a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M70.0 120h204"/>
 *       </g>
 *       <path d="M274.0 120a10 10 0 0 0 10 -10v-29a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M284.0 71h20"/>
 *     </g>
 *     <path d="M 304 71 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The Rule List includes qualified rules, @ rules, and whitespace tokens.
 *
 * There are our corresponding YACC-like rules:
 *
 * \code
 *  rule-list: <empty>
 *           | rule
 *           | rule rule-list
 *
 *  rule: WHITESPACE
 *      | qualified-rule
 *      | at-rule
 * \endcode
 *
 * \section at_rule At-Rule "at-rule" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="102" viewbox="0 0 589 102" width="589">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 31 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 41h10"/>
 *     <g>
 *      <rect height="22" width="164" x="50.0" y="30"/>
 *      <a xlink:href="lexer_rules.html#at-keyword"><text x="132.0" y="45">AT-KEYWORD</text>
 *     </g>
 *     <path d="M214 41h10"/>
 *     <g>
 *      <path d="M224.0 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *      <g>
 *       <path d="M244.0 21h160"/>
 *      </g>
 *      <path d="M404.0 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *      <path d="M224.0 41h20"/>
 *      <g>
 *       <path d="M244.0 41h10"/>
 *       <g>
 *        <rect height="22" width="140" x="254.0" y="30"/>
 *        <a xlink:href="#component-value"><text x="324.0" y="45">component-value</text></a>
 *       </g>
 *       <path d="M394.0 41h10"/>
 *       <path d="M254.0 41a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M254.0 61h140"/>
 *       </g>
 *       <path d="M394.0 61a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M404.0 41h20"/>
 *     </g>
 *     <g>
 *      <path d="M424.0 41h20"/>
 *      <g>
 *       <rect height="22" width="84" x="444.0" y="30"/>
 *       <a xlink:href="#curlybracket-block"><text x="486.0" y="45">{}-block</text></a>
 *      </g>
 *      <path d="M528.0 41h20"/>
 *      <path d="M424.0 41a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M444 71h28"/>
 *       <path d="M500 71h28"/>
 *       <rect height="22" rx="10" ry="10" width="28" x="472" y="60"/>
 *       <text x="486.0" y="75">;</text>
 *      </g>
 *      <path d="M528.0 71a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *     </g>
 *     <path d="M 548 41 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * At values define special rules. Some of the at-values are defined by
 * the CSS Preprocessor as extensions. However, the parser has not
 * specifics to handle such.
 *
 * There is our corresponding YACC-like rule:
 *
 * \code
 *  at-rule: AT-KEYWORD-TOKEN component-value block
 *         | AT-KEYWORD-TOKEN component-value ';'
 * \endcode
 *
 * \section qualified_rule Qualified Rule "qualified-rule" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="81" viewbox="0 0 385 81" width="385">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 31 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <g>
 *      <path d="M40.0 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *      <g>
 *       <path d="M60.0 21h160"/>
 *      </g>
 *      <path d="M220.0 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *      <path d="M40.0 41h20"/>
 *      <g>
 *       <path d="M60.0 41h10"/>
 *       <g>
 *        <rect height="22" width="140" x="70.0" y="30"/>
 *        <text x="140.0" y="45">component-value</text>
 *       </g>
 *       <path d="M210.0 41h10"/>
 *       <path d="M70.0 41a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M70.0 61h140"/>
 *       </g>
 *       <path d="M210.0 61a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M220.0 41h20"/>
 *     </g>
 *     <path d="M240 41h10"/>
 *     <g>
 *      <rect height="22" width="84" x="250.0" y="30"/>
 *      <a xlink:href="#curlybracket-block"><text x="292.0" y="45">{}-block</text></a>
 *     </g>
 *     <path d="M334 41h10"/>
 *     <path d="M 344 41 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Qualified rules are selectors followed by a block. The selector list can
 * be empty.
 *
 * \warning
 * Although it is not clear from the graph, a
 * <a href="#curlybracket-block">{}-block</a> ends a list of
 * <a href="#component-value">component-value</a>.
 *
 * Note that a component value is just whatever preserved token so you
 * can create a field with the qualified-rule grammar:
 *
 * \code
 *      IDENTIFIER ':' '{' ... '}'
 * \endcode
 *
 * In order to support the SASS syntax, we have an exception that allows
 * us to set variables in the "global scope". So the following works:
 *
 * \code
 *      $green: green;
 * \endcode
 *
 * Even when you are in the global scope. This is not a valid rule that
 * otherwise matches a valid CSS3 qualified rule. Note that a variable
 * rule can also be terminated by a block since the content of a variable
 * can be set to an entire block of data.
 *
 * There is our corresponding YACC-like rule:
 *
 * \code
 *      qualified-rule: component-value-list block
 *                    | VARIABLE WHITESPACE ':' component-value-list
 * \endcode
 *
 * \section declaration_list Declaration List "declaration-list" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="102" viewbox="0 0 649 102" width="649">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 31 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 41h10"/>
 *     <g>
 *      <rect height="22" width="104" x="50" y="30"/>
 *      <a xlink:href="lexer_rules.html#whitespace"><text x="102" y="45">WHITESPACE</text></a>
 *     </g>
 *     <path d="M154 41h10"/>
 *     <g>
 *      <path d="M164.0 41h20"/>
 *      <g>
 *       <g>
 *        <path d="M184 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *        <g>
 *         <path d="M204 21h108"/>
 *        </g>
 *        <path d="M312 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *        <path d="M184 41h20"/>
 *        <g>
 *         <rect height="22" width="108" x="204" y="30"/>
 *         <a xlink:href="#declaration"><text x="258" y="45">declaration</text>
 *        </g>
 *        <path d="M312.0 41h20"/>
 *       </g>
 *       <g>
 *        <path d="M332 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *        <g>
 *         <path d="M352 21h216"/>
 *        </g>
 *        <path d="M568 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *        <path d="M332 41h20"/>
 *        <g>
 *         <path d="M352 41h10"/>
 *         <g>
 *          <rect height="22" rx="10" ry="10" width="28" x="362" y="30"/>
 *          <text x="376" y="45">;</text>
 *         </g>
 *         <path d="M390 41h10"/>
 *         <path d="M400 41h10"/>
 *         <g>
 *          <rect height="22" width="148" x="410" y="30"/>
 *          <a xlink:href="#declaration-list"><text x="484.0" y="45">declaration-list</text></a>
 *         </g>
 *         <path d="M558 41h10"/>
 *        </g>
 *        <path d="M568 41h20"/>
 *       </g>
 *      </g>
 *      <path d="M588.0 41h20"/>
 *      <path d="M164.0 41a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M184 71h70"/>
 *       <path d="M518 71h70"/>
 *       <path d="M254 71h10"/>
 *       <g>
 *        <rect height="22" width="76" x="264" y="60"/>
 *        <a xlink:href="#at-rule"><text x="302" y="75">at-rule</text></a>
 *       </g>
 *       <path d="M340 71h10"/>
 *       <path d="M350 71h10"/>
 *       <g>
 *        <rect height="22" width="148" x="360" y="60"/>
 *        <a xlink:href="#declaration-list"><text x="434" y="75">declaration-list</text></a>
 *       </g>
 *       <path d="M508 71h10"/>
 *      </g>
 *      <path d="M588.0 71a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *     </g>
 *     <path d="M 608 41 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * A <a href="#declaration">declaration</a> list is a list of
 * <a href="#declaration">declarations</a> separated by semi-colons.
 * Such a list can be started with an <a href="#at-rule">at-rule</a>.
 *
 * Whitespaces can appear to separate various elements in such a list.
 * 
 * There is our corresponding YACC-like rule:
 *
 * \code
 * declaration-list: WHITESPACE
 *                 | WHITESPACE declaration ';' declaration-list
 *                 | WHITESPACE at-rule declaration-list
 * \endcode
 *
 * \section declaration Declaration "declaration" (CSS 3, CSS Preprocessor)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="156" viewbox="0 0 737 156" width="737">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 31 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 41h10"/>
 *     <g>
 *      <rect height="22" width="124" x="50.0" y="30"/>
 *      <a xlink:href="lexer_rules.html#identifier"><text x="112.0" y="45">IDENTIFIER</text></a>
 *     </g>
 *     <path d="M174 41h10"/>
 *     <path d="M184 41h10"/>
 *     <g>
 *      <rect height="22" width="104" x="194" y="30"/>
 *      <a xlink:href="lexer_rules.html#whitespace"><text x="246" y="45">WHITESPACE</text></a>
 *     </g>
 *     <path d="M298 41h10"/>
 *     <path d="M308 41h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="318" y="30"/>
 *      <text x="332" y="45">:</text>
 *     </g>
 *     <path d="M346 41h10"/>
 *     <g>
 *      <path d="M356.0 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *      <g>
 *       <path d="M376.0 21h160"/>
 *      </g>
 *      <path d="M536.0 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *      <path d="M356.0 41h20"/>
 *      <g>
 *       <path d="M376.0 41h10"/>
 *       <g>
 *        <rect height="22" width="140" x="386" y="30"/>
 *        <a xlink:href="#component-value"><text x="456" y="45">component-value</text></a>
 *       </g>
 *       <path d="M526 41h10"/>
 *       <path d="M386 41a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M386 61h140"/>
 *       </g>
 *       <path d="M526 61a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M536 41h20"/>
 *     </g>
 *     <g>
 *      <path d="M556.0 41h20"/>
 *      <g>
 *       <path d="M576.0 41h100"/>
 *      </g>
 *      <path d="M676.0 41h20"/>
 *
 *      <path d="M556.0 41a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *      <g>
 *       <rect height="22" width="100" x="576" y="50"/>
 *       <a xlink:href="#important"><text x="626.0" y="65">!important</text></a>
 *      </g>
 *      <path d="M676.0 61a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *
 *      <path d="M556.0 41a10 10 0 0 1 10 10v30a10 10 0 0 0 10 10"/>
 *      <g>
 *       <rect height="22" width="100" x="576" y="80"/>
 *       <a xlink:href="#global"><text x="626.0" y="95">!global</text></a>
 *      </g>
 *      <path d="M676.0 91a10 10 0 0 0 10 -10v-30a10 10 0 0 1 10 -10"/>
 *
 *      <path d="M556.0 41a10 10 0 0 1 10 10v60a10 10 0 0 0 10 10"/>
 *      <g>
 *       <rect height="22" width="100" x="576" y="110"/>
 *       <a xlink:href="#default"><text x="626.0" y="125">!default</text></a>
 *      </g>
 *      <path d="M676.0 121a10 10 0 0 0 10 -10v-60a10 10 0 0 1 10 -10"/>
 *
 *     </g>
 *     <path d="M 696 41 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * A declaration is a component value with the particularity of starting
 * with an \ref identifier "IDENTIFIER" which is followed by
 * a colon (:).
 *
 * To support variables, we also accept a \ref variable "VARIABLE"
 * followed by a colon (:).
 *
 * And to support declarations of functions, we support
 * \ref variable_function "VARIABLE_FUNCTION" followed by a colon (:).
 *
 * We also support two special extensions:
 *
 * * One named \ref global "!global" which is used with variable
 * declarations to mark a variable as a global variable wherever
 * it gets defined:
 *
 * \code
 *   a {
 *     $width: 3em !global;
 *     width: $width;
 *   }
 *   span {
 *     width: $width; // works because $width was marked !global
 *   }
 * \endcode
 *
 * * The other named \ref default "!default" which is used with
 * variable declarations to mark that specific declaration
 * as a default value meaning that if the variable is already
 * defined, it does not get modified:
 *
 * \code
 *   $width: 5em;
 *
 *   a {
 *     $width: 3em !default; // keeps 5em
 *     width: $width;
 *   }
 *   span {
 *     $width: 3em;
 *     width: $width; // use the 3em
 *   }
 * \endcode
 *
 * There is our corresponding YACC-like rule:
 *
 * \code
 * declaration: IDENTIFIER WHITESPACE ':' component-value-list
 *            | IDENTIFIER WHITESPACE ':' component-value-list !important
 *            | IDENTIFIER WHITESPACE ':' component-value-list !global
 *            | IDENTIFIER WHITESPACE ':' component-value-list !default
 * \endcode
 *
 * However, we do not enforce the exclamation flag name until later when
 * we know exactly how it is getting used.
 *
 * \section important !important "!important" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 617 62" width="617">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="50" y="20"/>
 *      <text x="64.0" y="35">!</text>
 *     </g>
 *     <path d="M78 31h10"/>
 *     <path d="M88 31h10"/>
 *     <g>
 *      <rect height="22" width="104" x="98" y="20"/>
 *      <a xlink:href="lexer_rules.html#whitespace"><text x="150" y="35">WHITESPACE</text></a>
 *     </g>
 *     <path d="M202 31h20"/>
 *     <g>
 *      <rect height="22" width="220" x="222" y="20"/>
 *      <a xlink:href="lexer_rules.html#identifier"><text x="332" y="35">IDENTIFIER "important"</text></a>
 *     </g>
 *     <path d="M442 31h10"/>
 *     <path d="M452 31h10"/>
 *     <g>
 *      <rect height="22" width="104" x="462" y="20"/>
 *      <a xlink:href="lexer_rules.html#whitespace"><text x="514" y="35">WHITESPACE</text></a>
 *     </g>
 *     <path d="M566 31h10"/>
 *     <path d="M 576 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The !important keyword can be used to prevent overloading certain
 * declaration. It is generally not recommended unless you are not
 * in full control of your entire CSS rules.
 *
 * There is our corresponding YACC-like rule:
 *
 * \code
 *      !important: '!' WHITESPACE IDENTIFIER(=="important") WHITESPACE
 * \endcode
 *
 * \section global !global "!global" (CSS Preprocessor Extension)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 617 62" width="617">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="50" y="20"/>
 *      <text x="64.0" y="35">!</text>
 *     </g>
 *     <path d="M78 31h10"/>
 *     <path d="M88 31h10"/>
 *     <g>
 *      <rect height="22" width="104" x="98" y="20"/>
 *      <a xlink:href="lexer_rules.html#whitespace"><text x="150" y="35">WHITESPACE</text></a>
 *     </g>
 *     <path d="M202 31h20"/>
 *     <g>
 *      <rect height="22" width="220" x="222" y="20"/>
 *      <a xlink:href="lexer_rules.html#identifier"><text x="332" y="35">IDENTIFIER "global"</text></a>
 *     </g>
 *     <path d="M442 31h10"/>
 *     <path d="M452 31h10"/>
 *     <g>
 *      <rect height="22" width="104" x="462" y="20"/>
 *      <a xlink:href="lexer_rules.html#whitespace"><text x="514" y="35">WHITESPACE</text></a>
 *     </g>
 *     <path d="M566 31h10"/>
 *     <path d="M 576 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The !global keyword can be used to mark a variable as global even
 * when declared within a sub-block. This makes the value of the variable
 * available to all the other blocks.
 *
 * Note that the use of the !global keyword is not recommended.
 *
 * There is our corresponding YACC-like rule:
 *
 * \code
 *      !global: '!' WHITESPACE IDENTIFIER(=="global") WHITESPACE
 * \endcode
 *
 * \section default !default "!default" (CSS Preprocessor Extension)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 617 62" width="617">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="50" y="20"/>
 *      <text x="64.0" y="35">!</text>
 *     </g>
 *     <path d="M78 31h10"/>
 *     <path d="M88 31h10"/>
 *     <g>
 *      <rect height="22" width="104" x="98" y="20"/>
 *      <a xlink:href="lexer_rules.html#whitespace"><text x="150" y="35">WHITESPACE</text></a>
 *     </g>
 *     <path d="M202 31h20"/>
 *     <g>
 *      <rect height="22" width="220" x="222" y="20"/>
 *      <a xlink:href="lexer_rules.html#identifier"><text x="332" y="35">IDENTIFIER "default"</text></a>
 *     </g>
 *     <path d="M442 31h10"/>
 *     <path d="M452 31h10"/>
 *     <g>
 *      <rect height="22" width="104" x="462" y="20"/>
 *      <a xlink:href="lexer_rules.html#whitespace"><text x="514" y="35">WHITESPACE</text></a>
 *     </g>
 *     <path d="M566 31h10"/>
 *     <path d="M 576 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The !default keyword can be used to mark a variable declaration as
 * the default declaration. This means the existing value of the variable,
 * if such exists, does not get modified.
 *
 * Note that when the !default keyword is used, it does not set the
 * variable if it is defined in any block or globally.
 *
 * There is our corresponding YACC-like rule:
 *
 * \code
 *      !default: '!' WHITESPACE IDENTIFIER(=="default") WHITESPACE
 * \endcode
 *
 * \section component_value Component Value "component-value" and "component-value-list" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="182" viewbox="0 0 261 182" width="261">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <g>
 *      <path d="M40.0 31h20"/>
 *      <g>
 *       <rect height="22" width="140" x="60.0" y="20"/>
 *       <a xlink:href="#preserved-token"><text x="130.0" y="35">preserved-token</text></a>
 *      </g>
 *      <path d="M200 31h20"/>
 *      <path d="M40 31a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M60.0 61h28.0"/>
 *       <path d="M172.0 61h28.0"/>
 *       <rect height="22" width="84" x="88.0" y="50"/>
 *       <a xlink:href="#curlybracket-block"><text x="130.0" y="65">{}-block</text></a>
 *      </g>
 *      <path d="M200 61a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *      <path d="M40 31a10 10 0 0 1 10 10v40a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M60 91h28"/>
 *       <path d="M172 91h28"/>
 *       <rect height="22" width="84" x="88" y="80"/>
 *       <a xlink:href="#parenthesis-block"><text x="130.0" y="95">()-block</text></a>
 *      </g>
 *      <path d="M200.0 91a10 10 0 0 0 10 -10v-40a10 10 0 0 1 10 -10"/>
 *      <path d="M40.0 31a10 10 0 0 1 10 10v70a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M60 121h28"/>
 *       <path d="M172 121h28"/>
 *       <rect height="22" width="84" x="88.0" y="110"/>
 *       <a xlink:href="#squarebracket-block"><text x="130" y="125">[]-block</text></a>
 *      </g>
 *      <path d="M200 121a10 10 0 0 0 10 -10v-70a10 10 0 0 1 10 -10"/>
 *      <path d="M40 31a10 10 0 0 1 10 10v100a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M60 151h4"/>
 *       <path d="M196 151h4"/>
 *       <rect height="22" width="132" x="64" y="140"/>
 *       <a xlink:href="#function-block"><text x="130" y="155">function-block</text></a>
 *      </g>
 *      <path d="M200 151a10 10 0 0 0 10 -10v-100a10 10 0 0 1 10 -10"/>
 *     </g>
 *     <path d="M 220 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * A component value is a <a href="#preserved-token">preserved token</a>
 * or a block. Blocks are viewed as "non-preserved tokens" and everything
 * else is viewed as a preserved token.
 *
 * Note that CSS defines preserved tokens because the parsing of various
 * parts of your CSS rules may end up being done by various compilers and
 * thus one compiler may fail where another succeeds and for that reason
 * the preserved tokens is a rather loose definition. For the CSS
 * Preprocessor, all tokens must be understood 100%. Anything that we
 * do not know about, we cannot verify and thus we want to reject.
 *
 * There is our corresponding YACC-like rule:
 *
 * \code
 * component-value: <empty>
 *                | {}-block
 *                | component-value-list
 *                | component-value-list {}-block
 *
 * component-value-list: component-value-item
 *                     | component-value-list component-value-item
 *
 * component-value-item: preserved-token
 *                     | ()-block
 *                     | []-block
 *                     | function-block
 * \endcode
 *
 * \warning
 * Notice that our Yacc rule does not follow the declaration of
 * component-value to the letter. All the other grammar rules expect
 * a list of component-value which may also be empty. However, the
 * main difference not conveyed in the graphs is the fact that a
 * <a href="#curlybracket-block">{}-block</a> can only be used at the end
 * of a list of 'component-value' entries.
 * (see http://www.w3.org/TR/css-syntax-3/#consume-a-qualified-rule0).
 * In other words, the following are three 'component-value' entries
 * and not just one:
 *
 * \code
 *      a { b: c } { d: e } { f: g } ...
 * \endcode
 *
 * \section preserved_token Preserved Token "preserved-token" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="62" viewbox="0 0 443 62" width="443">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 21 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 31h10"/>
 *     <g>
 *      <rect height="22" width="342" x="50" y="20"/>
 *      <text x="184" y="35">any token except '{', '(', '[',</text>
 *      <a xlink:href="lexer_rules.html#function"><text x="346" y="35">FUNCTION</text></a>
 *     </g>
 *     <path d="M392 31h10"/>
 *     <path d="M 402 31 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * The CSS 3 definition of a preserved token is:
 *
 * \par
 * Any token except the '{', '(', '[', and
 * <a href="lexer_rules.html#function">FUNCTION</a>.
 *
 * The importance of this is the fact that all preserved tokens are
 * expected to be kept in the parser output. Since we are a preprocessor
 * which compacts CSS, we do tend to ignore this definition because we
 * want to (1) remove any comment that is not marked as \@preserve,
 * (2) compress anything we can, (3) verify the syntax and only allow
 * what is permissible (functional on at least one browser.)
 *
 * This being said, we still attempt to keep as many tokens as we can
 * in our tree of nodes.
 *
 * Note that "any token" does not include ';' which marks the end of
 * the a declaration, an @-keyword, a variable set... Also, the
 * closing '}', ')', ']' should match open '{', '(', and '[' and
 * that's why they do not appear here either. Again, although this
 * is \em correct the fact is that we save the opening in our list
 * of preserved tokens.
 *
 * There is our corresponding YACC-like rule:
 *
 * \code
 *      preserved-token: ANY-TOKEN except '{', '(', '[', and FUNCTION
 * \endcode
 *
 * \section curlybracket_block {}-block "{}-block" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="81" viewbox="0 0 377 81" width="377">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 31 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 41h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="50" y="30"/>
 *      <text x="64" y="45">{</text>
 *     </g>
 *     <path d="M78 41h10"/>
 *     <g>
 *      <path d="M88.0 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *      <g>
 *       <path d="M108.0 21h160"/>
 *      </g>
 *      <path d="M268.0 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *      <path d="M88.0 41h20"/>
 *      <g>
 *       <path d="M108 41h10"/>
 *       <g>
 *        <rect height="22" width="140" x="118.0" y="30"/>
 *        <a xlink:href="#component-value"><text x="188.0" y="45">component-value</text></a>
 *       </g>
 *       <path d="M258.0 41h10"/>
 *       <path d="M118.0 41a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M118.0 61h140"/>
 *       </g>
 *       <path d="M258.0 61a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M268.0 41h20"/>
 *     </g>
 *     <path d="M288 41h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="298" y="30"/>
 *      <text x="312.0" y="45">}</text>
 *     </g>
 *     <path d="M326 41h10"/>
 *     <path d="M 336 41 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Define a sub-block of values.
 *
 * \warning
 * The rules as presented here do not specify how things are
 * really work: a {}-block is also a terminator rule when it is
 * found in a <a href="#component-value">component-value</a>.
 * See http://www.w3.org/TR/css-syntax-3/#consume-a-qualified-rule0
 * as a reference to that rule.
 *
 * Specifically, curly bracket blocks are used to add any level of
 * declarations within a qualified declaration (a declaration that
 * starts with a list of identifiers and other tokens representing
 * selectors.)
 *
 * There is our corresponding YACC-like rule:
 *
 * \code
 *      {}-block: '{' component-value-list '}'
 * \endcode
 *
 * \section parenthesis_block ()-block "()-block" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="81" viewbox="0 0 377 81" width="377">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 31 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 41h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="50" y="30"/>
 *      <text x="64" y="45">(</text>
 *     </g>
 *     <path d="M78 41h10"/>
 *     <g>
 *      <path d="M88.0 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *      <g>
 *       <path d="M108.0 21h160"/>
 *      </g>
 *      <path d="M268.0 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *      <path d="M88.0 41h20"/>
 *      <g>
 *       <path d="M108.0 41h10"/>
 *       <g>
 *        <rect height="22" width="140" x="118.0" y="30"/>
 *        <a xlink:href="#component-value"><text x="188.0" y="45">component-value</text></a>
 *       </g>
 *       <path d="M258.0 41h10"/>
 *       <path d="M118.0 41a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M118.0 61h140"/>
 *       </g>
 *       <path d="M258.0 61a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M268.0 41h20"/>
 *     </g>
 *     <path d="M288 41h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="298" y="30"/>
 *      <text x="312" y="45">)</text>
 *     </g>
 *     <path d="M326 41h10"/>
 *     <path d="M 336 41 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Define a sub-block of values.
 *
 * In general, parenthesis are used to group expressions together.
 *
 * There is our corresponding YACC-like rule:
 *
 * \code
 *      ()-block: '(' component-value-list ')'
 * \endcode
 *
 * \section squarebracket_block []-block "[]-block" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="81" viewbox="0 0 377 81" width="377">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 31 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *     <path d="M40 41h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="50" y="30"/>
 *      <text x="64" y="45">[</text>
 *     </g>
 *     <path d="M78 41h10"/>
 *     <g>
 *      <path d="M88.0 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *      <g>
 *       <path d="M108.0 21h160"/>
 *      </g>
 *      <path d="M268.0 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *      <path d="M88.0 41h20"/>
 *      <g>
 *       <path d="M108.0 41h10"/>
 *       <g>
 *        <rect height="22" width="140" x="118.0" y="30"/>
 *        <a xlink:href="#component-value"><text x="188.0" y="45">component-value</text></a>
 *       </g>
 *       <path d="M258.0 41h10"/>
 *       <path d="M118.0 41a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M118.0 61h140"/>
 *       </g>
 *       <path d="M258.0 61a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M268.0 41h20"/>
 *     </g>
 *     <path d="M288 41h10"/>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="298" y="30"/>
 *      <text x="312" y="45">]</text>
 *     </g>
 *     <path d="M326 41h10"/>
 *     <path d="M 336 41 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Define a sub-block of values.
 *
 * Specifically, the square bracket blocks are used to test the
 * attributes of an element in a list of selectors.
 *
 * There is our corresponding YACC-like rule:
 *
 * \code
 *      []-block: '[' component-value-list ']'
 * \endcode
 *
 * \section function_block Function Block "function-block" (CSS 3)
 *
 * \htmlonly
 *  <div class="railroad">
 *   <svg class="railroad-diagram" height="106" viewbox="0 0 547 106" width="547">
 *    <g transform="translate(.5 .5)">
 *     <path d="M 20 31 v 20 m 10 -20 v 20 m -10 -10 h 20.5"/>
 *
 *     <path d="M40 41h30"/>
 *     <g>
 *      <rect height="22" width="148" x="70" y="30"/>
 *      <a xlink:href="lexer_rules.html#function"><text x="144" y="45">FUNCTION</text></a>
 *     </g>
 *     <path d="M218 41h70"/>
 *
 *     <g>
 *      <path d="M40 41a10 10 0 0 1 10 10v10a10 10 0 0 0 10 10"/>
 *      <path d="M60 71h10"/>
 *      <g>
 *       <rect height="22" width="148" x="70" y="60"/>
 *       <a xlink:href="lexer_rules.html#variable_function"><text x="144" y="75">VARIABLE_FUNCTION</text></a>
 *      </g>
 *      <path d="M220 71h10"/>
 *      <path d="M230 71a10 10 0 0 0 10 -10v-10a10 10 0 0 1 10 -10"/>
 *     </g>
 *
 *     <g>
 *      <path d="M258 41a10 10 0 0 0 10 -10v0a10 10 0 0 1 10 -10"/>
 *      <g>
 *       <path d="M278 21h160"/>
 *      </g>
 *      <path d="M438 21a10 10 0 0 1 10 10v0a10 10 0 0 0 10 10"/>
 *      <g>
 *       <path d="M298 41h10"/>
 *       <g>
 *        <rect height="22" width="140" x="288" y="30"/>
 *        <a xlink:href="#component-value"><text x="358" y="45">component-value</text></a>
 *       </g>
 *       <path d="M448 41h10"/>
 *       <path d="M288 41a10 10 0 0 0 -10 10v0a10 10 0 0 0 10 10"/>
 *       <g>
 *        <path d="M288 61h140"/>
 *       </g>
 *       <path d="M428 61a10 10 0 0 0 10 -10v0a10 10 0 0 0 -10 -10"/>
 *      </g>
 *      <path d="M428 41h40"/>
 *     </g>
 *     <g>
 *      <rect height="22" rx="10" ry="10" width="28" x="468" y="30"/>
 *      <text x="482" y="45">)</text>
 *     </g>
 *     <path d="M496 41h10"/>
 *     <path d="M 506 41 h 20 m -10 -10 v 20 m 10 -20 v 20"/>
 *    </g>
 *   </svg>
 *  </div>
 * \endhtmlonly
 *
 * Functions are identifiers immediately followed by an open parenthesis,
 * variable functions are variables immediately followed by an open
 * parenthesis, either is then followed by a list of space or comma
 * separated parameters, and a closing parenthesis.
 * Note that the parsing is very similar to a
 * <a href="#parenthesis-block">()-block</a>.
 *
 * There is our corresponding YACC-like rule:
 *
 * \code
 *      function-block: FUNCTION component-value-list ')'
 *                    | VARIABLE_FUNCTION component-value-list ')'
 * \endcode
 */

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et syntax=doxygen
