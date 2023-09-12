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

/** \page expression_by_type_page CSS Preprocessor Reference -- Expressions by Type
 * \tableofcontents
 *
 * The CSS Preprocessor supports many operations of the many types it
 * supports. The following entries describe the various capabilities
 * offered by the expression part of the compiler.
 *
 * \section expression_with_identifier Identifiers
 *
 * Some identifiers are viewed as the named representation of a value.
 * The following are the values that get converted when an identifier
 * is found as a unary value.
 *
 * \li false -- the Boolean value FALSE; note that is viewed as a Boolean
 *     and not the integer number 0; so many opeartions will not work
 *     with this value (i.e. "false + false" is not valid.)
 * \li null -- the null or "empty" value; in general this represents
 *     emptiness, it can be used to create an empty variable, it is
 *     also used as the result of the intersection of two unicode
 *     ranges that have no characters in common.
 * \li true -- the Boolean value TRUE; note that is viewed as a Boolean
 *     and not the integer number 1; so many opeartions will not work
 *     with this value (i.e. "true + true" is not valid.)
 *
 * The other use is a color name. In that case, the identifier is
 * transformed in a color object. The assembler output capability
 * will transform colors to the smallest possible representation
 * which may be an identifier (i.e. the color red is generally
 * output as the identifier red.)
 *
 * When appearing in place of an operator, the identifier may be converted
 * to such. A certain number of operators were given common names as found
 * in other languages and in SASS. There are the operators we currently
 * support as identifiers (operator, identifier, full name):
 *
 * \li ** -- pow -- power
 * \li * -- mul -- multiplication
 * \li / -- div -- division
 * \li % -- mod -- modulo
 * \li || -- or -- logical or
 * \li && -- and -- logical and
 * \li ?: -- if() -- conditional (the \ref if_function is actually a function)
 * \li != -- not-equal -- not equal
 *
 * \note
 * The pow keyword only represents the power operator. We do not offer a
 * function since one can use the operator.
 *
 * \section expression_with_strings Strings
 *
 * Strings can be concatenated using the + operator. The
 * operation is not commutative.
 *
 * \code{.scss}
 *      "Hello " + "world" + "!" = "Hello world!"
 * \endcode
 *
 * Strings can be duplicated using the * operator. The operation is
 * commutative.
 *
 * \code{.scss}
 *      "Ho! " * 3 = "Ho! Ho! Ho! "
 * \endcode
 *
 * \section expression_with_unicode_ranges Unicode Ranges
 *
 * The @font-face keyword accepts a field named unicode-range which
 * accepts ranges of Unicode character code points.
 *
 * These ranges can be intersected between each others uing * operator.
 * The operation is commutative.
 *
 * \code{.scss}
 *      U+1??? * U+17?? = U+17??
 * \endcode
 *
 * As a side effect, the union accepts NULL as one or both input. The
 * resulting range will always be NULL.
 *
 * \code{.scss}
 *      U+1??? * U+7?? = NULL
 *      // and thus
 *      U+1??? * U+7?? * U0???-1??? = NULL
 * \endcode
 *
 * At this time we do not allow for a union because we cannot give a
 * valid answer if the intersection of two ranges is null when dealing
 * with a union (i.e. a unicode range with a gap requires two ranges.)
 *
 * \section expression_with_booleans Booleans
 *
 * The two Boolean values are true and false. The relation operators
 * return Boolean values. Boolean values can be used with the ||
 * and the && operators. They are also used as the first parameter
 * of the conditional operation (\<boolean> ? \<value if true>
 * : \<value if false>).
 *
 * Note that the system also views all numbers as boolean values when
 * used in a context where a Boolean value is expected in that case
 * a number which is equal to zero represents false. Any other number
 * represents true.
 *
 * Similarly, strings that are empty represent false and strings that
 * are not empty represent true. Note that the '\0' character does
 * not make it in our strings so it can be used as a string terminator
 * and thus a string with just '\0' is considered empty.
 *
 * Maps and arrays can also be converted to a Boolean value. An
 * empty map or array represent the value false. A non empty map
 * or array represent the value true.
 *
 * The NULL token is viewed as false.
 *
 * The black color, whatever the alpha level (red = 0.0, green = 0.0,
 * and blue = 0.0), is considered false. Any other color is considered
 * true.
 *
 * An identifier other than null, true, and false is considered to be an
 * invalid Boolean value. Note that in expression, identifiers that
 * represent a color will be transformed in that color first and thus
 * the color scheme applies (i.e. black is false, chocolate is true.)
 *
 * \section expression_with_numbers Numbers (integers and decimal numbers)
 *
 * All operators accept numbers (integers and decimal numbers.)
 *
 * Operators that expect booleans (&& and ||) transform numbers to true
 * unless they represent zero.
 *
 * \subsection expression_with_percent Percent Operations
 *
 * Percent numbers can be compared, added, subtracted between each others.
 *
 * Percent numbers can be used to multiply and divide any other number,
 * including numbers with dimensions and other percentages. The results
 * are just as expected: a multiplication or division of that number
 * by a unitless number equal to the percent number divided by 100.
 * In this case, the other number dimension does not change.
 *
 * \code{.scss}
 *      13px * 50% = 7.5px
 * \endcode
 *
 * Note that percent numbers are always considered to be decimal numbers
 * and therefore the result is always a decimal number even if the other
 * number was an integer and if the precent number \em looks \em like
 * an integer too.
 *
 * A percent number at any given power remains a percent number.
 *
 * \subsection expression_with_number_relations Numbers and Relations
 *
 * These operators always return true or false. You may compare integers
 * against decimal numbers. The compiler does not currently issue a
 * warning if you use the = and != operators against decimal numbers.
 *
 * \subsection expression_with_number_arithmetic_operations Arithmetic Operations
 *
 * When dealing with integers, the results are always integers:
 *
 * \f[
 *  { 10 \over 3 } = 3
 *  \tag{Integer}
 * \f]
 *
 * Expressions that involve at least one decimal number always return
 * a decimal number:
 *
 * \f[
 * 	{ 10 \over 3.0 } = 3.\overline{3}
 * 	\\ or
 * 	\\ { 10.0 \over 3 } = 3.\overline{3}
 * 	\\ or
 * 	\\ { 10.0 \over 3.0 } = 3.\overline{3}
 * 	\tag{Decimal Number}
 * \f]
 *
 * The modulo operator (%) can be used with floating point numbers (see
 * `man fmod()` for details.)
 *
 * \note
 * When expressions are executed with a decimal number, then results are
 * always decimal numbers. However, if the final decimal number can be
 * represented as an integer, the assembler output will be an integer.
 * In other word, there is a loss of information for the convenience of
 * the final minified CSS output.
 *
 * \subsection expression_with_dimensions Arithmetic Operations on Dimensions
 *
 * Just like with unitless numbers, you may apply arithmetic operations
 * against numbers that have dimenions.
 *
 * Relations require the left and right hand side numbers to all have the
 * same dimensions:
 *
 * \f[
 *  3px < 7px \tag{Correct}
 * \f]
 *
 * whereas the following generates an error:
 *
 * \f[
 *  3px < 7em \tag{Wrong}
 * \f]
 *
 * Additions (+), subtractions (-), and modulo (% or mod) are like
 * relations, they only accept the exact same dimensions on the left
 * and right hand side to function.
 *
 * \subsubsection expression_with_dimensions_multiplication Multiplication and Dimensions
 *
 * Multiplications work with any dimensions as follow. The result
 * is a set of mixed dimensions:
 *
 * \f[
 *  3px \times 7em = 21px \cdot em
 *  \tag{Multiplication}
 * \f]
 *
 * In CSS Preprocessor files (.scss files) you may enter such dimensions
 * using the asterisk as in (the backslash is required):
 *
 * \code{.scss}
 *      21px\*em
 * \endcode
 *
 * The power operator works in a similar way to the multiplication
 * operator. The power has to be a unitless number and must be a
 * positive integer (\f$power \in \Bbb{Z}^*\f$). In all other cases
 * the power operator generates an error if the left hand side number
 * has a dimension. The result looks like this:
 *
 * \f[
 *  (3px) ^ 5 = 3px^5
 *  \\ since
 *  \\ 3px \times 3px \times 3px \times 3px \times 3px = 3px^5
 *  \tag{Power}
 * \f]
 *
 * \subsubsection expression_with_dimensions_division Division and Dimensions
 *
 * Divisions work with any dimensions. The result is a set of mixed
 * dimensions.
 *
 * \f[
 *  { 21px \over 7em } = 3px/em
 *  \\ or
 *  \\ { 21px \over 7em } = 3px \cdot em^{-1}
 *  \tag{Division}
 * \f]
 *
 * When writing such in CSS Preprocessor files, dimensions found on the
 * right side of the '/' operator are put after a slash in the list of
 * dimensions (the backslash is required):
 *
 * \code{.scss}
 *      3px\/em
 * \endcode
 *
 * When you have more than one dimension over or under, all dimensions
 * that are over are written before the slash (/), and all dimensions that
 * are under are written after the slash. All dimensions over or
 * under are separated by an asterisk (*).
 *
 * \code{.scss}
 *      3px\*em\*cm\/vw\*mm
 * \endcode
 *
 * Note that you should not write the same dimension before and after the
 * slash (/) since that represents 1, unitless.
 *
 * \note
 * Internally, dimensions make use of a space (one of my previous example
 * would look like this string, internally: "px * em * cm / vw * mm".) We
 * do not force you to add spaces. However, you can only put 0 or 1 space
 * at this time. I may fix that problem later (i.e. allow any number of
 * spaces.) Spaces are supported for SASS compatibility. A space at the
 * end of a dimension is also accepted.
 *
 * \subsubsection expression_with_dimensions_inverted_dimensions Only Inverted Dimensions
 *
 * When dealing with numbers, it may happen that you end up with an
 * inverted dimension only. In math or physics, we pretty much never
 * write such as a result, but if you were to do so, it would look
 * like this:
 *
 * \f[
 *      { 1 \over 3 \, m } = 3 m^{-1}
 *      \tag{Inverted}
 * \f]
 *
 * For CSS Preprocessor to support any type of units while operating
 * on various numbers, this case happens, so it had to have a way to
 * allow such inverted dimensions. This is done by using "1 /" as
 * an introducer. So the previous example could be written as follow
 * in an .scss file (backslashes are required):
 *
 * \code{.scss}
 *      3\31\/m
 * \endcode
 *
 * Note that the dimension has to start with a 1 which is why you need
 * to write that digit using the backslash syntax and '31' represents
 * the ASCII code in hexadecimal.
 *
 * \subsubsection expression_with_dimensions_multiplicative Dimensions Simplifications
 *
 * When multiplying or dividing two numbers against each others, the
 * system computes a new set of dimensions before the slash (/) and a
 * list of dimensions after the slash (/). If any dimension is found
 * in both lists, then they both get removed. As a result, a dimension
 * can only be found at the first or the second list, not both. However,
 * it can be duplicated in either list (i.e. \f$cm^2\f$ is represented
 * as "cm * cm").
 *
 * These operations allow you to remove dimensions from a number. For
 * example, to transform a size in pixels to a unitless number, you
 * may do the following (see also \ref remove_unit_function, \ref unit_function, and \ref unitless_function):
 *
 * \code{.scss}
 *      15px / 1px = 15
 * \endcode
 *
 * Similarly, you can convert a value to another as in:
 *
 * \code{.scss}
 *      15px * 0.33em\/px = 5em
 *
 *               or
 *
 *      15px * 0.33em / 1px = 5em
 * \endcode
 *
 * \subsubsection expression_with_complex_dimensions Complex Dimension (Horrible) Syntax
 *
 * One might ask: "Why is the syntax of multi-dimensions so terrible?"
 * and I would have to say that it very much looks like a quite
 * legitimate question.
 *
 * The CSS syntax allows an identifier like string to follow a number.
 * That string supports backslashes, but not the asterisk nor the slash
 * character (also no spaces if you wanted to also include a space before
 * and after each asterisk and slash character.) There are many reasons
 * for that, but with CSS Preprocessor we could anyway not distinguish
 * between an asterisk used for multiplications, a slash used for
 * divisions or as a separator, and spaces used to separate objects.
 *
 * We may later want to support a string written between quotes such
 * as in:
 *
 * \code{.scss}
 *      25"px * px"
 * \endcode
 *
 * But that should be viewed as an integer (25) followed by a string
 * ("px * px"). I do not think that happens too often in CSS code, so
 * it could be useful to support such and make it a lot easier to write
 * complex dimensions. Plus, INTEGER + STRING can (and probably should)
 * be written with a space in between (INTEGER + WHITESPACE + STRING)
 * and that would not be taken as a dimension.
 *
 * \note
 * The origin of the * and / in complex dimensions comes from SASS.
 *
 * \section expression_with_colors Colors
 *
 * Colors can be manipulated with some of the operators: add (+),
 * subtract (-), multiply (*), divide (/), modulo (%), not().
 * However, all those manipulations make use of the RGB components.
 * You may want to look into using HSL functions instead. These
 * functions generally makes it easier to deal with colors.
 *
 * Colors can be transformed to a Boolean value (See
 * \ref expression_with_booleans.) Black is false, whatever the
 * alpha channel, any other color is true.
 *
 * Note that we do not clamp color components until the time we have to
 * output the result. So one can safely add two colors and then divide
 * them by 2.0 to get the average and thus merge two colors together.
 *
 * \f[
 *  { { \begin{pmatrix}r_1, g_1, b_1, a_1\end{pmatrix}
 *  +
 *  \begin{pmatrix}r_2, g_2, b_2, a_2\end{pmatrix} }
 *  \over 2 }
 *  =
 *  { \begin{pmatrix} { {r_1 + r_2} \over 2}, { {g_1 + g_2} \over 2 },
 *  { {b_1 + b_2} \over 2 }, { {a_1 + a_2} \over 2 } \end{pmatrix} }
 * \f]
 *
 * The same operation using the CSS Preprocessor syntax:
 *
 * \code{.scss}
 *    (red + blue) / 2
 * \endcode
 *
 * Note that color components are always saved as 32 bit floats so
 * all operations are always viewed as floating point operations
 * even if the product or division uses an integer and the color
 * is defined using the rgb() function with integers. Also, this
 * may throw you off since the operations may not give you the
 * expected results unless you remember that rgb() components are
 * all first divided by 255.0.
 *
 * The supported operations are:
 *
 * \li color + color -- each component of each color is added
 * \li color + number -- add an offset to each color component
 * \li color - color -- each component of each color is subtracted
 * \li color - number -- subtract an offset from each color component
 * \li color * color -- each component of each color is multiplied
 * \li color / color -- each component of each color is divided
 * \li color % color -- each component of each color is "clamped"
 * \li color * number -- each component is multiplied by number
 * \li number * color -- each component is multiplied by number
 * \li color / number -- each component is divided by number, number
 *     cannot be zero
 * \li color % number -- each component is "clamped" using number, number
 *     cannot be zero
 * \li color || color -- true if one of the colors is not black
 * \li color && color -- true if both colors are not black
 * \li color == color -- true if both colors are the same (compared after
 *     conversion to #aabbggrr)
 * \li color != color -- true if both colors are different (compared after
 *     conversion to #aabbggrr)
 *
 * \note
 * The color +/- number is expected to be used with a decimal number.
 * If you add or subtract an integer, the color becomes white (+1)
 * or becomes transparent (-1) unless the offset is zero. Note that
 * the alpha channel is also affected.
 *
 * \f[
 *  \begin{pmatrix} r + k, g + k, b + k, a + k \end{pmatrix}
 *  =
 *  \begin{pmatrix} r, g, b, a \end{pmatrix} + k
 *  \tag{Add Offset k to Color}
 * \f]
 *
 * \note
 * In most cases, it probably makes more sense to use \ref frgba_function
 * to add the offset to only the color components. There is an example
 * where the offset is 3 (which would be \f$k = { 3 \over 255 }\f$ in the
 * previous example) and does not affect the alpha channel:
 *
 * \code{.scss}
 *      chocolate + rgba(3, 3, 3, 0)
 * \endcode
 *
 * \note
 * The || and && operators can also be used with other parameters
 * than colors on either side of the operator.
 *
 * \warning
 * All colors have an alpha channel and arithmetic operations may
 * change the alpha channel making an otherwise opaque color
 * semi-transparent. You may use various tricks to force a
 * color to be opaque.
 *
 * \code{.scss}
 *      // if your color computation is not going to create a negative alpha
 *      (color calculations...) + rgba(0, 0, 0, 1.0)
 *
 *      // when alpha may really be any value
 *      rgb(color calculations...)
 *      // or explicitly
 *      rgba(color calculations..., 1.0)
 * \endcode
 */

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// Also do: :syntax sync fromstart
// vim: ts=4 sw=4 et syntax=doxygen
