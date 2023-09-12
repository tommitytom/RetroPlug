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

/** \page selectors_rules CSS Preprocessor Reference -- Selectors
 * \tableofcontents
 *
 * CSS Preprocessor parses all the selectors it finds in all the source
 * files it parses. This ensures that only valid selectors are output
 * making it easier to find potential errors in your source CSS files.
 *
 * The supported selectors are all the selected supported in CSS 3 and
 * the %<name> selector which is used to allow for optional rules
 * defined in CSS libraries.
 *
 * Source:
 * http://www.w3.org/TR/selectors/#selectors
 *
 * \section asterisk Select All (*)
 *
 * The asterisk (*) character can be used to select any one tag.
 *
 * For example the following says any 'a' tag which appears in a tag
 * defined inside a 'div' tag (i.e. \<div>\<at least one other tag>\<a>):
 *
 * \code
 *      div * a { color: orange; }
 * \endcode
 *
 * \section element Element Selection (E)
 *
 * Any identifier is taken as the name of an element (i.e. a tag in your
 * HTML). A tag must match one to one.
 *
 * You may add more constraints to only match elements with a given class
 * (.\<class-name>), to only that one element with a given identifier
 * (#\<identifier>), to only match elements with an attribute defined
 * ([\<attribute-name>]) or an attribute with a certain value
 * ([\<attribute-name>=\<value>]), all of those, except the identifier test,
 * can be repeated any number of times.
 *
 * \section has_attribute Element Has Attribute (E[attr])
 *
 * Test whether an element has a given attribute. In this case, the value
 * of the attribute is not checked, only that the attribute is defined.
 *
 * The name of the element (E) is not required. Without it, any element
 * that has the given attribute in the tree will match.
 *
 * \code
 *      div[book]           // matches  <div book="red"> or <div book>
 * \endcode
 *
 * \section attribute_equal Element Has Attribute with Specific Value (E[attr="value"])
 *
 * Test whether an element has a given attribute set to a specific value.
 *
 * The name of the element (E) is not required. Without it, any element
 * that has the given attribute in the tree will match.
 *
 * \code
 *      div[book="red"]      // matches  <div book="red">
 * \endcode
 *
 * \note
 * If value can be represented by an identifier, then the quotations are
 * not required:
 *
 * \code
 *      div[book=red]        // matches  <div book="red">
 * \endcode
 *
 * \section attribute_not_equal Element Does Not Have Attribute with Specific Value (E[attr!="value"])
 *
 * Test whether an element has a given attribute set to a specific value.
 * If so, then refuse that element.
 *
 * The name of the element (E) is not required. Without it, any element
 * that does not have the given attribute in the tree will match.
 *
 * \code
 *      div[book!="red"]      // matches  <div book="blue">
 * \endcode
 *
 * \note
 * When "value" can be represented by one identifier, then the quotes are
 * not required:
 *
 * \code
 *      div[book!=red]        // matches  <div book="green">
 * \endcode
 *
 * \warning
 * Note that CSS 3 does not support the \b != operator. The CSS
 * Preprocessor replaces this syntax with:
 *
 * \code
 *      div:not([book=red])
 * \endcode
 *
 * \section include_match Include Match (E[attr~="value"])
 *
 * The include match, written tilde (~) and equal (=), no spaces in between,
 * is used to check a list of whitespace separated items in the specified
 * attribute. When attr is "class", it is the same as using the period
 * selector.
 *
 * So with this operator you can access other attributes in a way
 * similar to the class attribute.
 *
 * The name of the element (E) is not required. Without it, any element
 * that has the given attribute in the tree will match.
 *
 * \code
 *      div.wrapper    or     div[class~="wrapper"]
 *                              // matches <div class="section wrapper screen">
 *      div[color~="green"]     // matches <div color="red green blue">
 * \endcode
 *
 * \note
 * If value can be represented by an identifier, then the quotations are
 * not required.
 *
 * \code
 *      div[color~=green]       // matches <div color="red green blue">
 * \endcode
 *
 * \section prefix_match Prefix Match (E[attr^="value"])
 *
 * The prefix match, written circumflex accent (^) and equal (=), no
 * spaces in between, is used to check whether an attribute starts
 * with the specified string.
 *
 * The name of the element (E) is not required. Without it, any element
 * that has the given attribute in the tree will match.
 *
 * \code
 *      div[section^="1.1"]  // matches 1.1, 1.1.0, 1.1.1, 1.1.2, 1.1b, etc.
 * \endcode
 *
 * \note
 * If value can be represented by an identifier, then the quotations are
 * not required.
 *
 * \code
 *      div[section^=sub]
 * \endcode
 *
 * \section suffix_match Suffix Match (E[attr$="value"])
 *
 * The suffix match, written dollar ($) and equal (=), no spaces in between,
 * is used to check whether an attribute ends with the specified string.
 *
 * The name of the element (E) is not required. Without it, any element
 * that has the given attribute in the tree will match.
 *
 * \code
 *      div[height$=13]  // match height="313" or height="113"
 * \endcode
 *
 * \note
 * If value can be represented by an identifier, then the quotations are
 * not required.
 *
 * \code
 *      div[height^=small]
 * \endcode
 *
 * \section substring_match Substring Match (E[attr*="value"])
 *
 * The suffix match, written asterisk (*) and equal (=), no spaces in between,
 * is used to check whether an attribute includes the specified string,
 * anywhere.
 *
 * The name of the element (E) is not required. Without it, any element
 * that has the given attribute in the tree will match.
 *
 * \code
 *      div[code*=part]  // match code="55 part 3" or height="spartians"
 * \endcode
 *
 * \note
 * If value can be represented by an identifier, then the quotations are
 * not required.
 *
 * \code
 *      div[code^=part]
 * \endcode
 *
 * \section dash_match Dash Match (E[attr|="value"])
 *
 * The dash match, written pipe (|) and equal (=), no spaces in between,
 * is used to check a language in the hreflang attribute of an anchor
 * tag. It is very unlikely that you will ever need this matching
 * operator unless you are in the academic world or have a website similar
 * to Wikipedia with translations of your many pages.
 *
 * The value on the right side is checked against the list of languages
 * defined in scripts/languages.scss, and when a country is defined,
 * it is checked against the list of countries defined in
 * script/countries.scss, lists which can be updates as required.
 *
 * http://www.rfc-editor.org/rfc/bcp/bcp47.txt
 *
 * \note
 * If value can be represented by an identifier, then the quotations are
 * not required.
 *
 * \code
 *      div[hreflang^=en]
 * \endcode
 *
 * \section root Root (E:root)
 *
 * The :root pseudo class matches an element E if it is the root tag
 * of the document.
 *
 * Since the root of an HTML document is \<html>, it generally is not
 * required to specify :root.
 *
 * \code
 *      html:root { margin: 0 }
 * \endcode
 *
 * \section nth_child Nth Child (E:nth-child(n))
 *
 * The :nth-child() pseudo function matches an element which is a child of
 * E which position matches 'n'. The value 'n' can be defined as:
 *
 * \code
 * An+B
 * \endcode
 *
 * Where A and B have to be integers. The CSS Preprocessor will parse that
 * definition to make sure it is valid and possibly optimize it if possible.
 *
 * The letter 'n' appears verbatim. A and B are optional and the sign of the
 * integer can be negative.
 *
 * \code
 *      3n+1
 *      -5n-2
 *      7n-4
 * \endcode
 *
 * The function applies the values A and B and is a match if:
 * \f$\begin{cases} A \neq 0, & { ( child_{position} - 1 ) \bmod A - B = 0 }
 * \\ A = 0, & { child_{position} - B = 0 }\end{cases}\f$.
 *
 * \todo
 * The math needs to be enhanced to include the case when A is negative.
 * I think it is wrong in that case in the current formula.
 *
 * \section nth_last_child Nth Last Child (E:nth-last-child(n))
 *
 * The :nth-last-child() pseudo function matches an element which is a child
 * of E which position, counting from the last child, matches 'n'. The value
 * 'n' is defined as in \ref nth_child.
 *
 * \section nth_of_type Nth of Type (E:nth-of-type(n))
 *
 * Search for the Nth element which is a sibling of E and has the same type
 * (same tag name) as E. The value 'n' is defined as in \ref nth_child, only
 * the elements that match the type are counted.
 *
 * See also \ref first_of_type.
 *
 * \section nth_last_of_type Nth last of Type (E:nth-last-of-type(n))
 *
 * Search for the Nth element, counting from the last sibling, which is
 * a sibling of E and has the same type (same tag name) as E. The value
 * 'n' is defined as in \ref nth_child, only the elements that match the
 * type are counted.
 *
 * See also \ref last_of_type.
 *
 * \section first_child First Child (E:first-child)
 *
 * This pseudo class selects the first child of element E.
 *
 * \section last_child Last Child (E:last-child)
 *
 * This pseudo class selects the last child of element E.
 *
 * \section first_of_type First of Type (E:first-of-type)
 *
 * Search for the first element of type E in the current list of siblings.
 *
 * This is equivalent to :nth-of-type(1), see \ref nth_of_type.
 *
 * \code
 *      // first cell in a row
 *      tr > td:first-of-type
 * \endcode
 *
 * \section last_of_type Last of Type (E:last-of-type)
 *
 * Search for the last element of type E in the current list of siblings.
 *
 * This is equivalent to nth-last-of-type(1), see \ref nth_last_of_type.
 *
 * \code
 *      // last cell in a row
 *      tr > td:last-of-type
 * \endcode
 *
 * \section only_child Only Child (E:only-child)
 *
 * Return a tag if it is an only child (i.e. the parent of that tag has
 * exactly 1 child).
 *
 * This is equivalent to :first-child:last-child or
 * :nth-child(1):nth-last-child(1) with lower specificity.
 *
 * \section only_of_type Only of Type (E:only-of-type)
 *
 * Search for tags with a specific type (as specified by E), if only one
 * tag of that type is found in a list of siblings, then it matches.
 *
 * This is equivalent to :first-of-type:last-of-type or
 * :nth-type-of(1):nth-last-type-of(1) with lower specificity.
 *
 * \code
 *      // apply CSS to cells of tables with a single column
 *      tr > td:only-of-type
 * \endcode
 *
 * \section empty Empty (E:empty)
 *
 * Search for elements that have no children.
 *
 * \code
 *      // hide empty span elements
 *      span:empty { display: none }
 * \endcode
 *
 * \section link New Link (E:link)
 *
 * The pseudo class "link" applies to anchors that have not yet been
 * visited. This class only applies to anchor (A) tags.
 *
 * \code
 *      :link { color: blue }
 * \endcode
 *
 * \section visited Visited Link (E:visited)
 *
 * The pseudo class "visited" applies to anchors that have already been
 * visited. This class only applies to anchor (A) tags.
 *
 * \code
 *      :visited { color: purple }
 * \endcode
 *
 * \section active Active Link (E:active)
 *
 * The pseudo class "active" applies to anchors that are currently
 * clicked (i.e. the user is pressing the mouse button and did not
 * yet release the button.)
 *
 * This class only applies to anchor (A) tags.
 *
 * \code
 *      :active { color: red }
 * \endcode
 *
 * \section hover Hover Link (E:hover)
 *
 * The pseudo class ":hover" applies to anchors that are currently
 * being hovered by a mouse pointer.
 *
 * This class only applies to anchor (A) tags.
 *
 * \code
 *      :hover { color: green }
 * \endcode
 *
 * \section focus Element with Focus (E:focus)
 *
 * The pseudo class ":focus" applies to anchors and input elements
 * (including tags marked with contenteditable="true"). It is true
 * for one element: the one which is currently focused by the
 * browser.
 *
 * \code
 *      :focus { color: cyan }
 * \endcode
 *
 * \section target Target Element (E:target)
 *
 * The pseudo class ":target" applies to the element with identifier
 * defined in the URI after the '#' character (also called the anchor).
 *
 * \code
 *      :target { background-color: yellow }
 * \endcode
 *
 * \section lang Language (E:lang(n))
 *
 * The special :lang() pseudo function checks for a "lang" attribute in
 * this element or any parent element. The system handling the language
 * may actually use other definitions than just the lang attribute.
 *
 * \section enabled Enabled Element (E:enabled)
 *
 * The pseudo class ":enabled" applies to elements that can be enabled
 * and disabled (i.e. \<input> and \<button> tags.) This class applies
 * to elements that are current enabled (i.e. editable.)
 *
 * \code
 *      :enabled { font-size: 150% }
 * \endcode
 *
 * \section disabled Disabled Element (E:disabled)
 *
 * The pseudo class ":disabled" applies to elements that can be enabled
 * and disabled (i.e. \<input> and \<button> tags.) This class applies
 * to elements that are current disabled (i.e. cannot be modified by
 * the end user.)
 *
 * \code
 *      :disabled { background-color: gray }
 * \endcode
 *
 * \section checked Checked Element (E:checked)
 *
 * The pseudo class ":checked" applies to elements that can be checked
 * (i.e. checkboxes and radio buttons.)
 *
 * \code
 *      :checked { border: 1px solid #fff }
 * \endcode
 *
 * \section first_line First Line Pseudo Element (E::first-line) -- uses '::' since CSS 3
 *
 * The pseudo element "::first-list" applies to "elements" (characters, inline
 * images, etc.) that appear on the first line of a paragraph.
 *
 * \code
 *      p::first-line { font-variant: small-caps }
 * \endcode
 *
 * \section first_letter First Letter Pseudo Element (E::first-letter) -- uses '::' since CSS 3
 *
 * The pseudo element "::first-letter" applies to the first letter that appears
 * in E.
 *
 * \code
 *      // make the first character appear on 3 lines and make sure said lines
 *      p::first-letter { font-size: 300%; float: left; }
 * \endcode
 *
 * \section before Insert Before (E::before) -- uses '::' since CSS 3
 *
 * The pseudo element "::before" injects the string specified with
 * "content: \<string>" at the beginning of the specified element.
 *
 * \code
 *      p::before { content: "Inject This"; }
 * \endcode
 *
 * \section after Insert After (E::after) -- uses '::' since CSS 3
 *
 * The pseudo element "::after" injects the string specified with
 * "content: \<string>" at the end of the specified element.
 *
 * \code
 *      p::after { content: "Append That"; }
 * \endcode
 *
 * \section class_selector Class Selector (E.class)
 *
 * The pseudo class ".class" matches an element with a class attribute
 * that includes the word "class". This is a shortcut of the ~= operator
 * used with the name "class" as in:
 *
 * \code
 *      p.super-class   <=>   p[class~="super-class"]
 * \endcode
 *
 * A match against multiple classes can be specified by avoiding spaces
 * as in p.class1.class2.class3...
 *
 * \section identifier_selector Identifier Selector (E#id)
 *
 * The pseudo class "\#id" matches an element which has identifier "id".
 * Since identifiers are expected to be unique, selectors before a \#id
 * are generally useless unless your document may include the same
 * identifier more than once.
 *
 * \code
 *      #id { ... }
 * \endcode
 *
 * \section not_selector Not Selector (E:not(s))
 *
 * The pseudo function ":not(s)" matches an element which does not match
 * the specified selector "s".
 *
 * The "s" selector can only be a \em simple selector. This include most
 * except selectors that start with "::" and ":not(s)". We also do not
 * allow our extensions (%name and &:hover in the not().)
 *
 * \code
 *      // a paragraph except the one with identifier #id
 *      p:not(#id) { ... }
 * \endcode
 *
 * \section selector_selector Selector Selector (E F)
 *
 * A whitespace between two selectors E and F mean an element of type E
 * and a descendant element of E of type F. There are no other constraints
 * than the type, attribute, etc. (since E and F can be any type of
 * selector.)
 *
 * \section selector_child Selector Child (E > F)
 *
 * A greater than (>) between two selectors E and F mean an element of type E
 * and a direct descendant element (i.e. a child) of E of type F. There are
 * no other constraints than the type, attribute, etc. (since E and F can
 * be any type of selector.)
 *
 * \section selector_next Selector Sibling (E + F)
 *
 * A plus (+) between two selectors E and F mean an element of type E and
 * the next sibling of type F. There are no other constraints than the type,
 * attribute, etc. (since E and F can be any type of selector.)
 *
 * E and F must be one after another. If F should should be any sibling,
 * then use the ~ operator instead (\ref selector_sibling).
 *
 * \section selector_sibling Selector Sibling (E ~ F)
 *
 * A tilde (~) between two selectors E and F mean an element of type E
 * followed by a sibling of type F. There are no other constraints than
 * the type, attribute, etc. (since E and F can be any type of selector.)
 *
 * This is similar to the + operator (\ref selector_next), only F can be
 * any sibling after E.
 *
 * \section selector_grammar Grammar used to parse the selectors
 *
 * The definition of the grammar appears in CSS 3, the selectors:
 *
 * http://www.w3.org/TR/selectors/
 *
 * \note
 * The :not() function is considered to be a term ("complex term") because
 * it cannot be used within itself (so :not(:not(...)) is not allowed.)
 *
 * There is a more yacc like grammar definition:
 *
 * \code
 * selector-list: selector
 *              | selector-list ',' selector
 *
 * selector: term
 *         | selector WHITESPACE '>' WHITESPACE term
 *         | selector WHITESPACE '+' WHITESPACE term
 *         | selector WHITESPACE '~' WHITESPACE term
 *         | selector WHITESPACE term
 *         | selector term
 *
 * term: simple-term
 *     | PLACEHOLDER
 *     | REFERENCE
 *     | ':' FUNCTION (="not") component-value-list ')'
 *     | ':' ':' IDENTIFIER
 *
 * simple-term: universal-selector
 *            | qualified-name
 *            | HASH
 *            | ':' IDENTIFIER
 *            | ':' FUNCTION (!="not") component-value-list ')'
 *            | '.' IDENTIFIER
 *            | '[' WHITESPACE attribute-check WHITESPACE ']'
 *
 * universal-selector: IDENTIFIER '|' '*'
 *                   | '*' '|' '*'
 *                   | '|' '*'
 *                   | '*'
 *
 * qualified-name: IDENTIFIER '|' IDENTIFIER
 *               | '*' '|' IDENTIFIER
 *               | '|' IDENTIFIER
 *               | IDENTIFIER
 *
 * attribute-check: qualified-name
 *                | qualified-name WHITESPACE attribute-operator WHITESPACE attribute-value
 *
 * attribute-operator: '='
 *                   | '!='
 *                   | '~='
 *                   | '^='
 *                   | '$='
 *                   | '*='
 *                   | '|='
 *
 * attribute-value: IDENTIFIER
 *                | INTEGER
 *                | DECIMAL_NUMBER
 *                | STRING
 * \endcode
 *
 * All operators have the same priority and all selections are always going
 * from left to right.
 *
 * The FUNCTION parsing changes for all n-th functions to re-read the input
 * data as an A+Bn which generates a new token as expected.
 *
 * Further we detect whether the same HASH appears more than once. Something
 * like:
 *
 * \code
 *      #my-div .super-class #my-div { ... }
 * \endcode
 *
 * is not going to work (assuming that the document respects the idea that
 * 'my-div' cannot be used more than once since identifiers are expected
 * to be unique.)
 */

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et syntax=doxygen
