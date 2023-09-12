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

/** \page expression_page CSS Preprocessor Reference -- Expressions
 * \tableofcontents
 *
 * The CSS Preprocessor is capable of compiling simple expressions.
 * These are used with @-keywords and field values.
 *
 * Since expressions can appear in lists of values of a field, an
 * expression may stop mid-stream.
 *
 * You may be more interested in reading the \ref expression_by_type_page
 * instead of this page, as that other page is less technical
 * in regard to the compiler implementation and closer to an
 * end user point of view.
 *
 * \section expression Expression
 *
 * Expressions are composed of many elements which, when we reach the
 * expression resolver, are in a flat list except for square bracket
 * parameters, function parameters, and parenthesis sub-expressions
 * which appear in a sub-list of these items.
 *
 * The expression parser is defined below in increasing order of
 * priority. The priority is important since many operations are
 * to be applied before others, for example, the multiplications
 * are to be applied before additions.
 *
 * \code{.scss}
 *      3 + 5 * 2  // result is 3 + 10 = 13 and not 8 * 2 = 16
 * \endcode
 *
 * The only \em strange operator is the power (**) which does not
 * accept being used more than once without extra parenthesis.
 * So an expression such as this does not compile:
 *
 * \code{.scss}
 *      5 ** 3 ** 2    // error, need parenthesis somewhere
 * \endcode
 *
 * Whereas the following will work since they use one set of parenthesis:
 *
 * \code{.scss}
 *      (5 ** 3) ** 2  // works, = 15625
 *      5 ** (3 ** 2)  // works, = 1953125
 * \endcode
 *
 * \subsection expression_rule CSS Preprocessor Expression
 *
 * All expressions are conditional expressions.
 *
 * Although we support lists of expressions, when parsing the expressions
 * found after an \@-keyword or in a declaration, we do not see those
 * as lists (contrary to SASS which sees pretty much everything as a list.)
 *
 * \code{.y}
 *  expression: conditional
 * \endcode
 *
 * \subsection expression_list CSS Preprocessor List Expression
 *
 * The CSS Preprocessor accepts lists that are lists of
 * comma separated expressions.
 *
 * Items in a list can be labelled. This means these can be preceeded
 * by a name (an identifier) and a colon. Whitespaces are allowed around
 * the colon. Lists with labelled items are called maps. When labels
 * are used, they have to be used on all items or an error is generated.
 * Maps can be accessed using the square bracket notation ($map[name])
 * or the period notation ($map.name).
 *
 * The value is optional when labelling. If not specified, NULL is used.
 *
 * Lists without labels are output as arrays which can be indexed using
 * integers ($array[3]).
 *
 * \code{.y}
 *  expression-list: assignment
 *                 | list
 *                 | map
 * 
 *  list: assignment
 *      | list ',' assignment
 * 
 *  map: IDENTIFIER ':' assignment
 *     | map ',' IDENTIFIER ':' assignment
 * \endcode
 *
 * \subsection expression_assignment CSS Preprocessor Assignment Expression
 *
 * The inline assignment operator (:=) can be used to set a variable
 * within an expression. These are always viewed as \em local variables.
 * These are really only available within the very expression being
 * computed.
 *
 * \warning
 * These work, but only when written between parenthesis. This requires
 * a post expression to retrieve the result, though, because parenthesis
 * are viewed as definitions of arrays or maps.
 *
 * \code{.y}
 *  assignment: conditional
 *            | IDENTIFIER ':=' conditional
 * \endcode
 *
 * \subsection expression_conditional CSS Preprocessor Conditional Expression
 *
 * Like in C/C++ we offer a conditional operator. The question mark
 * is not otherwise used by CSS so it is safe here.
 *
 * Note that like SASS, CSS Preprocessor also supports the
 * \ref at_if "\@if" keyword and the \ref if_function "if()" function.
 *
 * \code{.y}
 *  conditional: logical_or
 *             | conditional '?' expression_list ':' logical_or
 * \endcode
 *
 * \subsection expression_logical_or CSS Preprocessor Logical OR Expression
 *
 * Compare two values that the compiler can convert to a Boolean
 * value and apply the logical OR to those two values (i.e. if both
 * values represent false, then return false, otherwise return true.)
 *
 * \code{.y}
 * logical_or: logical_and
 *           | logical_or IDENTIFIER (='or') logical_and
 *           | logical_or '||' logical_and
 * \endcode
 *
 * \subsection expression_logical_and CSS Preprocessor Logical AND Expression
 *
 * Compare two values that the compiler can convert to a Boolean
 * value and apply the logical AND to those two values (i.e. if both
 * values represent true, then return true, otherwise return false.)
 *
 * \code
 * logical_and: equality
 *            | logical_and IDENTIFIER (='and') equality
 *            | logical_and '&&' equality
 * \endcode
 *
 * \subsection expression_equality CSS Preprocess Equality Expression
 *
 * Compare the two values as specified by the equality operator.
 *
 * The '=' compares both value for exact equality. The '!='
 * compares both values for exact inequality.
 *
 * The other operators attempt a match as CSS would do assuming
 * that the left hand side represents the actual value to check.
 *
 * \code{.scss}
 *      "This is a lizard" *= "is"   // returns true, phrase includes "is"
 *      "This is a lizard" ^= "is"   // returns false, phrase does not start with "is"
 *      "This is a lizard" $= "is"   // returns false, phrase does not end with "is"
 *      ...
 * \endcode
 *
 * Note that the '!=' operator is available in SASS but is not otherwise
 * an CSS 3 operator. Also, the '==' operator is accepted, but it should
 * not be used (however, SASS expects '==' in its expressions, so for
 * remote compatibility.)
 *
 * The result of all the equality operators is always true or false.
 *
 * \code{.y}
 * equality: relational
 *         | equality '=' relational
 *         | equality '!=' relational
 *         | equality '~=' relational
 *         | equality '^=' relational
 *         | equality '$=' relational
 *         | equality '*=' relational
 *         | equality '|=' relational
 * \endcode
 *
 * \subsection expression_relational CSS Preprocessor Relational Expression
 *
 * Relational work as expected in other languages.
 *
 * The result of the relational operators is always true or false.
 *
 * \code{.y}
 * relational: additive
 *           | relational '<' additive
 *           | relational '<=' additive
 *           | relational '>' additive
 *           | relational '>=' additive
 * \endcode
 *
 * \subsection expression_additive CSS Preprocessor Additive Expression
 *
 * The addition operator works for numbers (decimal numbers and integers).
 * If one of the two operands is a decimal number then the result is
 * a decimal number.
 *
 * The addition and substractions work on dimensions. Only both numbers
 * must have the exact same dimension. To add or remove a dimension,
 * use a multiplicative operator.
 *
 * The addition operator accepts colors. Remember that while working
 * with colors in expressions, colors do not get clamped.
 *
 * \f[
 * \begin{vmatrix} result_r
 * \\ result_g
 * \\ result_b \end{vmatrix}
 * =
 * \begin{vmatrix} lhs_r + rhs_r
 * \\ lhs_g + rhs_g
 * \\ lhs_b + rhs_b \end{vmatrix}
 * =
 * \begin{vmatrix} lhs_r
 * \\ lhs_g
 * \\ lhs_b \end{vmatrix}
 * +
 * \begin{vmatrix} rhs_r
 * \\ rhs_g
 * \\ rhs_b \end{vmatrix}
 * \f]
 *
 * The addition operator accepts strings and identifiers. In that
 * case it performs a concatenation.
 *
 * The addition operator accepts maps. In this case, it merges both
 * maps together. When both maps have fields with the same name, the
 * values of the map on the right hand side are kept.
 *
 * The addition operator accepts lists. In this case the right hand
 * side list is concatenated to the left hand side list.
 *
 * The subtraction works as expected with numbers. If one of the two
 * operands is a decimal number then the result is a decimal number.
 *
 * The subtraction accepts colors. Again, colors do not get clamped
 * while being worked on.
 *
 * \f[
 * \begin{vmatrix} result_r
 * \\ result_g
 * \\ result_b \end{vmatrix}
 * =
 * \begin{vmatrix} lhs_r - rhs_r
 * \\ lhs_g - rhs_g
 * \\ lhs_b - rhs_b \end{vmatrix}
 * =
 * \begin{vmatrix} lhs_r
 * \\ lhs_g
 * \\ lhs_b \end{vmatrix}
 * -
 * \begin{vmatrix} rhs_r
 * \\ rhs_g
 * \\ rhs_b \end{vmatrix}
 * \f]
 *
 * The subtraction accepts maps. In this case fields that appear in
 * the right hand side map are removed the from the left hand side
 * map and the resulting left hand side map is returned.
 *
 * The subtraction accepts lists. In this case, any item
 * in the right hand side list that also appears in the left hand side
 * list is removed from the left hand side list. The result is the
 * resulting left hand side list.
 *
 * Note that the '+' and '-' will work as expected, however, the
 * lexers immediately generates positive and negative numbers.
 * If you need to do an addition or a subtraction, you may want
 * to conside adding spaces after your '+' and '-' operators.
 *
 * \code{.y}
 *  additive: multiplicative
 *          | additive '+' multiplicative
 *          | additive '-' multiplicative
 * \endcode
 *
 * \subsection expression_multiplicative CSS Preprocessor Multiplicative Expression
 *
 * The multiplicative expression works on numbers, colors, and strings.
 * The following sub-sections describes how the multiplicative operators
 * work on each type of input.
 *
 * \code{.y}
 *  multiplicative: power
 *                | multiplicative '*' power
 *                | multiplicative IDENTIFIER(="mul") power
 *                | multiplicative '/' power
 *                | multiplicative IDENTIFIER(="div") power
 *                | multiplicative '%' power
 *                | multiplicative IDENTIFIER(="mod") power
 * \endcode
 *
 * \subsubsection expression_multiplicative_numbers Multiplicative Expression with Numbers (with and without dimensions)
 *
 * If one of the numbers is a decimal number, then the result is a
 * decimal number. Otherwise, the operation is performed with
 * integers and the result is always an integer (i.e. the divide
 * operator does not magically convert the numbers to decimal
 * numbers.) Percent numbers are viewed as decimal numbers.
 *
 * All the multiplicative operators can be used with dimensions.
 *
 * Multiplications augment dimensions in something that looks
 * like "dim1 * dim2" (i.e. '3em x 5px' -> '15em\\ \\*\\ px').
 * The syntax uses a list of dimensions separated by " * ".
 * The spaces are optional when you type such a dimension.
 * Note that way you can easily calculate the surface of a
 * rectangle or the volume of a rectangular cuboid:
 *
 * \f[
 * S mm ^ 2 = W mm \times H mm
 * \\
 * V mm ^ 3 = W mm \times H mm \times D mm
 * \f]
 *
 * Only the CSS Preprocessor keeps dimensions such as \f$mm^2\f$
 * are defined as "mm * mm".
 *
 * Divisions reduce dimensions, the opposite of multiplications.
 * So a dimension like "dim1 * dim2" divided by a dimension "dim1"
 * results in a dimension "dim2". If the dimension in the divisor
 * is not present in the list of existing dimensions, then the
 * result looks like "dim1 * dim2 / dim3". If the dividend is
 * dimension less, then the dimension becomes "1 / dim1".
 *
 * This is particularly useful to convert a dimension into another:
 *
 * \code{.scss}
 *      3cm * 0.393701in / 1cm = 1.1811in
 * \endcode
 *
 * since 1 centimeter is about 0.393701 inches.
 *
 * The modulo operator can be used with dimensions. Both dimensions
 * must be exactly equal (like for additions and subtractions.) For
 * example "10px % 3px" resuts in "1px". The reason for the modulo
 * operator to work this way is as follow:
 *
 * Say you have a value \em result calculated as follow:
 *
 * \f[
 * result = lhs \bmod rhs \tag{modulo}
 * \f]
 *
 * You may rewrite this expression as:
 *
 * \f[
 * lhs = rhs \cdot k + result \tag{k factor}
 * \f]
 *
 * where k is a dimension less factor. As we can see, for the math to
 * work in the \b k \b factor expression, lhs, rhs, and result must all have
 * the same dimension.
 *
 * \sa http://math.stackexchange.com/questions/1353284/how-do-we-deal-with-units-when-using-the-modulo-operation
 *
 * \subsubsection expression_multiplicative_colors Multiplicative Expressions with Colors
 *
 * The multiplicative operators all work against colors and the operations
 * are commutative. The modulo always uses a floating point modulo. Remember
 * that while running operations against colors, the colors do not get clamped
 * and components are floating point numbers.
 *
 * \f[
 * \begin{vmatrix} result_r
 * \\ result_g
 * \\ result_b \end{vmatrix}
 * =
 * \begin{vmatrix} lhs_r * rhs
 * \\ lhs_g * rhs
 * \\ lhs_b * rhs \end{vmatrix}
 * =
 * \begin{vmatrix} lhs_r
 * \\ lhs_g
 * \\ lhs_b \end{vmatrix}
 * *
 * rhs
 * \f]
 *
 * \f[
 * \begin{vmatrix} result_r
 * \\ result_g
 * \\ result_b \end{vmatrix}
 * =
 * \begin{vmatrix} lhs_r / rhs
 * \\ lhs_g / rhs
 * \\ lhs_b / rhs \end{vmatrix}
 * =
 * \begin{vmatrix} lhs_r
 * \\ lhs_g
 * \\ lhs_b \end{vmatrix}
 * /
 * rhs
 * \f]
 *
 * \f[
 * \begin{vmatrix} result_r
 * \\ result_g
 * \\ result_b \end{vmatrix}
 * =
 * \begin{vmatrix} lhs_r \mod rhs
 * \\ lhs_g \mod rhs
 * \\ lhs_b \mod rhs \end{vmatrix}
 * =
 * \begin{vmatrix} lhs_r
 * \\ lhs_g
 * \\ lhs_b \end{vmatrix}
 * \mod
 * rhs
 * \f]
 *
 * \subsubsection expression_multiplicative_strings Multiplicative Expressions with Strings (Duplication)
 *
 * The multiply operator (*) can be used with a string and an integer.
 * This will duplicate the string that many times. If the integer
 * is zero, the empty string is returned. You certainly want to
 * limit the integer in this case otherwise the string is gooing to
 * take a lot of memory.
 *
 * If the integer is negative, an error results.
 *
 * \subsubsection expression_multiplicative_unicode_range Multiplicative Expressions with Unicode Ranges (Intersection)
 *
 * The multiply operator (*) can be used with a Unicode Range. In that
 * case the multiply represents an intersection and only common
 * characters in both ranges are kept in the resulting range. If
 * the range becomes empty, then the result is NULL instead.
 *
 * Since the result of the operation may be NULL, we also accept NULL
 * as one of the operands or both and in that case NULL is also returned.
 *
 * \subsection expression_power CSS Preprocessor Power Expression
 *
 * The power operator let you calculte a number (left hand side) to the
 * power of another number (the right hand side).
 *
 * Note that the power is not looping. If you want to calculate a power
 * b power c (\f$\large a^{b^c}\f$), you will need to add parenthese:
 *
 * \code{.scss}
 *      a ** (b ** c)
 * \endcode
 *
 * Even though the power operator would otherwise be properly handled
 * with a right to left priority, by default a double or more power
 * expression is not allowed.
 *
 * \code{.y}
 *  power: post
 *       | post '**' post
 * \endcode
 *
 * \subsubsection expression_power_dimension Power Expression with Dimensions
 *
 * The left handside of a power operation can have a dimension if the
 * right hand side is a positive integer:
 * \f$power \in \Bbb{Z}^*\f$.
 * The dimension will be duplicated a number of time equal to the power.
 *
 * \f[
 *      (13cm)^2 = 169cm^2
 * \f]
 *
 * \subsection expression_post CSS Preprocessor Post Expression
 *
 * The post expression allows for access to elements of a list (an array)
 * or a map (a keyed array).
 *
 * The post.\<identifier> syntax is only allowed with maps. The square
 * bracket syntax is allowed with both: maps and arrays.
 *
 * Note that arrays are 1 based (the first element is at index 1,)
 * just like in XML.
 *
 * \code{.y}
 *  post: unary
 *      | post '[' expression_list ']'
 *      | post '.' IDENTIFIER
 * \endcode
 *
 * \subsection expression_unary Unary CSS Preprocessor Unary Expression Expression
 *
 * Unary expressions are composed of literals (identifiers, strings,
 * numbers, etc.) and a few unary operators.
 *
 * The parenthesis allow you to write expression lists (arrays and maps).
 *
 * The function expression may get converted if CSS Preprocessor knows
 * that function internally. For example the \ref if_function "if()"
 * is not understood by CSS 3 and will be reduced by the CSS
 * Preprocessor.
 *
 * The HASH tokens found in expressions are expected to be valid colors.
 * The unary() function will transform HASH tokens into COLOR tokens.
 * Also, if an IDENTIFIER token represents the name of a color, then it
 * is also converted into a COLOR token. Note that identifiers are not
 * transformed unless they appear in a place where an expression is found.
 * So identifiers in a selector do not get converted. Note that it is
 * possible to force an identifier that represents a color to be left
 * as an identifier by using the identifier() function:
 *
 * \code{.scss}
 *      // here the identifier 'red' will not be converted to a COLOR token
 *      color: identifier("red");
 * \endcode
 *
 * Note that the '+' and '-' will work as expected, however, the
 * lexers immediately generates positive and negative numbers.
 * If you need to do an addition or a subtraction, you may want
 * to conside adding spaces after your '+' and '-' operators.
 *
 * \code{.y}
 *  unary: ARRAY
 *       | BOOLEAN
 *       | DECIMAL_NUMBER
 *       | EXCLAMATION
 *       | FUNCTION expression_list ')'
 *       | HASH (-> COLOR)
 *       | IDENTIFIER
 *       | INTEGER
 *       | MAP
 *       | NULL_TOKEN
 *       | PERCENT
 *       | STRING
 *       | UNICODE_RANGE
 *       | URL
 *       | '(' expression_list ')'
 *       | '+' power
 *       | '-' power
 * \endcode
 *
 * \section functions CSS Preprocessor Internal Functions
 *
 * The CSS Preprocessor expression compiler will reduce everything it
 * can including functions. There are three types of functions to
 * consider here:
 *
 * \li CSS Functions
 *
 * A CSS Function is one that the CSS Preprocessor does not understand
 * and that the user did not overload. These functions are left alone
 * and are expected to be valid CSS functions (at some point the
 * compiler will check that such is indeed the case.)
 *
 * \li User Functions
 *
 * As shown with variable declarations and \ref at_mixin "\@mixin", it
 * is possible for the end user of the CSS Preprocessor compiler to
 * write his own functions. For example, you could write a function
 * that computes the average of three colors:
 *
 * \code{.scss}
 *      $three_color_avg($c1, $c2, $c3):
 *      {
 *          @return ($c1 + $c2 + $c3) / 3;
 *      }
 * \endcode
 *
 * \note
 * \@return is not implemented yet... You can already use functions, but
 * their result is their body verbatim instead of one returned value.
 *
 * \li Internal Functions
 *
 * The CSS Preprocessor understands a certain number of functions
 * internally. This allows for basic functionality that can then be
 * used in to declare user functions and extend CSS Preprocessor
 * even further and much faster than writing C++ code for each
 * single function (yes, it will be slower to execute, but the library
 * can grow very rapidely that way.)
 *
 * The internal functions are described below.
 *
 * \subsection abs_function abs()
 *
 * \code{.scss}
 *      abs(number)
 * \endcode
 *
 * Calculate the absolute value of number. If the number is an integer,
 * then it remains an integer.
 *
 * \f[
 * result = |number|
 * \f]
 *
 * \subsection acos_function acos()
 *
 * \code{.scss}
 *      acos(number)
 * \endcode
 *
 * Calculate the arccosine value of number.
 *
 * \f[
 * result = cos^{-1}(number)
 * \f]
 *
 * \subsection alpha_function alpha()
 *
 * \code{.scss}
 *      alpha(color)
 * \endcode
 *
 * The \b alpha() function retrieves the current alpha value of the
 * specified color.
 *
 * \f[
 *  result = color_a
 * \f]
 *
 * The color components are not clamped so the alpha value may be
 * out of range. However, the default range is from 0.0 to 1.0 inclusive.
 *
 * We also offer a user defined function named \ref opacity_function
 * which also returns the alpha channel of a color.
 *
 * \sa \ref opacity_function
 *
 * \subsection asin_function asin()
 *
 * \code{.scss}
 *      asin(number)
 * \endcode
 *
 * Calculate the arcsine value of number.
 *
 * \f[
 * result = sin^{-1}(number)
 * \f]
 *
 * \subsection atan_function atan()
 *
 * \code{.scss}
 *      atan(number)
 * \endcode
 *
 * Calculate the arctangent value of number.
 *
 * \f[
 * result = tan^{-1}(number)
 * \f]
 *
 * \subsection blue_function blue()
 *
 * \code{.scss}
 *      blue(color)
 * \endcode
 *
 * The \b blue() function retrieves the current blue value of the
 * specified color.
 *
 * \f[
 *  result = color_b
 * \f]
 *
 * The color components are not clamped so the blue value may be
 * out of range. However, the default range is from 0.0 to 255.0
 * inclusive. The value is returned as a decimal number.
 *
 * \subsection ceil_function ceil()
 *
 * \code{.scss}
 *      ceil(number)
 * \endcode
 *
 * Calculate the ceiling value of number.
 *
 * \f[
 * result = \lceil number \rceil
 * \f]
 *
 * \subsection cos_function cos()
 *
 * \code{.scss}
 *      cos(number)
 * \endcode
 *
 * Calculate the cosine value of number.
 *
 * \subsection decimal_number_function decimal_number()
 *
 * \code{.scss}
 *      decimal_number(expression)
 * \endcode
 *
 * This function is a \em cast that transforms its parameter in a
 * decimal number.
 *
 * The function is useful to force an integer in a decimal number.
 *
 * Note that a percentage becomes a decimal number without dimensions.
 *
 * The function also attempts to transform strings into numbers
 * returned as decimal numbers. Those strings may also include
 * a dimension:
 *
 * \code{.scss}
 *      @mixin my_set_unit($value, $unit): {
 *          @return decimal_number(string($value) + string($unit));
 *      }
 * \endcode
 *
 * \sa \ref integer_function
 * \sa \ref percentage_function
 * \sa \ref string_function
 *
 * \subsection floor_function floor()
 *
 * \code{.scss}
 *      floor(number)
 * \endcode
 *
 * Calculate the floor value of number.
 *
 * \f[
 * result = \lfloor number \rfloor
 * \f]
 *
 * \subsection frgb_function frgb()
 *
 * \code{.scss}
 *      frgb(color)
 *        or
 *      frgb(red, green, blue)
 * \endcode
 *
 * The \b frgb() function force the alpha channel of a color to 1.0 (opaque)
 * or transforms three numbers in a color with its alpha channel set to 1.0.
 *
 * The color components are expected to be values from 0.0 to 1.0. They may
 * be integers or decimal numbers. The numbers can be out of range in
 * which case they will be clamped by the assembler whenever the output
 * is generated. This allows you to apply various mathematical functions
 * to your colors before the output gets generated.
 *
 * \subsection frgba_function frgba()
 *
 * \code{.scss}
 *      frgba(color, alpha)
 *        or
 *      frgba(red, green, blue, alpha)
 * \endcode
 *
 * The \b frgba() function force the alpha channel of a color to the
 * specified value or transforms four numbers in a color with its
 * alpha channel specified.
 *
 * The color components and alpha channel are expected to be values
 * from 0.0 to 1.0. All the numbers can be out of range. They get
 * clamped only at the time they are output by the assembler. This
 * allows you to apply various mathematical functions to your colors
 * before the output gets generated.
 *
 * \subsection function_exists_function function_exists()
 *
 * \code{.scss}
 *      function_exists(name)
 * \endcode
 *
 * The \b function_exists() function returns true if the \p name'd function
 * exists.
 *
 * The name can be a string or an identifier.
 *
 * Note that in CSS Preprocessor, functions may be defined locally, just
 * like variables. This function returns true whether the function is
 * global or not.
 *
 * \subsection global_variable_exists_function global_variable_exists()
 *
 * \code{.scss}
 *      global_variable_exists(name)
 * \endcode
 *
 * The \b global_variable_exists() function checks whether a global variable
 * of the specified \p name exists.
 *
 * \p name can be a string or an identifier representing the name of the
 * variable to be checked.
 *
 * \subsection green_function green()
 *
 * \code{.scss}
 *      green(color)
 * \endcode
 *
 * The \b green() function retrieves the current green value of the
 * specified \p color.
 *
 * \f[
 *  result = color_g
 * \f]
 *
 * The color components are not clamped so the green value may be
 * out of range. However, the default range is from 0.0 to 255.0
 * inclusive. The value is returned as a decimal number.
 *
 * \subsection hsl_function hsl()
 *
 * \code{.scss}
 *      hsl(hue, saturation, lightness)
 * \endcode
 *
 * The \b hsl() function creates an opaque color using the specified hue,
 * which should be an angle, and saturation and lightness, which should
 * be percentages.
 *
 * The HSL color is immediately converted to RGB.
 *
 * \subsection hsla_function hsla()
 *
 * \code{.scss}
 *      hsla(hue, saturation, lightness, alpha)
 * \endcode
 *
 * The \b hsla() function creates a color using the specified hue,
 * which should be an angle, saturation and lightness, which should
 * be percentages, and alpha which should be a number from 0.0 to 1.0.
 *
 * The HSL color is immediately converted to RGB. The alpha channel is
 * used as is.
 *
 * \subsection hue_function hue()
 *
 * \code{.scss}
 *      hue(color)
 * \endcode
 *
 * Extract the hue component of a color. Note that we keep our colors
 * as RGBA floats so extracting the hue includes a step calculating
 * the HSL value out of which we return the hue.
 *
 * \f[
 *  result = color_h
 * \f]
 *
 * \subsection identifier_function identifier()
 *
 * \code{.scss}
 *      identifier(expression)
 * \endcode
 *
 * This function is a \em cast that transforms its parameter in an
 * identifier. This can be useful to convert a command line argument,
 * which is a string, into something that's legal in CSS.
 *
 * \code{.scss}
 *      // this would put "solid" or "dashed" in the rule
 *      border: 1px $_csspp_args[3] red;
 *
 *      // this puts solid or dashed as an identifier
 *      border: 1px identifier($_csspp_args[3]) red;
 * \endcode
 *
 * Nearly all the values that support a string are accepted as a parameter:
 *
 * \li color
 * \li decimal_number
 * \li integer
 * \li percent
 * \li placeholder
 * \li string
 * \li url
 *
 * All of these may not always work exactly as expected.
 *
 * Note that integers and decimal numbers are converted to a string
 * including a minus sign when negative and their dimension if they
 * have one.
 *
 * So in other words, it is possible to create an identifier that
 * starts with a digit. This is possible in CSS with the backslash
 * as well (\\31 23 is the valid identifier "123" starting with the
 * character 1 writen as an escape character.)
 *
 * \subsection if_function if()
 *
 * \code{.scss}
 *      if(boolean-expression, expr-true, expr-false)
 * \endcode
 *
 * The \b if() function is an internal CSS Preprocessor functions that
 * takes exactly 3 parameters. The first parameter is expected to be
 * a boolean which resolves as either true or false.
 *
 * When the first parameter is true, the function returns its second
 * parameter as the result. When the first parameter is false, the
 * function returns its third parameter as the result.
 *
 * The true and false expressions do not need to represent the same
 * type of data.
 *
 * \subsection integer_function integer()
 *
 * \code{.scss}
 *      integer(expression)
 * \endcode
 *
 * This function is a \em cast that transforms its parameter in an
 * integer.
 *
 * The function is useful to force a decimal number in an integer.
 * This will compute the floor() of the decimal number when the
 * number is positive or zero and the ceil() of the decimal number
 * when the number is negative.
 *
 * \f[
 * result = \begin{cases}\lfloor expression \rfloor & , expression >= 0
 * \\ \lceil expression \rceil & , expression < 0\end{cases}
 * \f]
 *
 * Note that a percent value loses its dimension specification since
 * percent values must otherwise be decimal numbers. Also in most
 * cases percentages are numbers between 0 and 1 so the result is
 * likely going to be 0.
 *
 * The function also attempts to transform strings into numbers
 * returned as decimal numbers. Those strings may also include
 * a dimension:
 *
 * \code{.scss}
 *      @mixin my_set_unit($value, $unit) {
 *          @return integer(string($value) + string($unit));
 *      }
 * \endcode
 *
 * \subsection inspect_function inspect()
 *
 * \code{.scss}
 *      inspect(expression)
 * \endcode
 *
 * This function is mainly for debug purposes. It converts the expression
 * passed as a parameter into a string without any other attempt at
 * interpreting the parameter. This is very similar to using
 * \ref string_function "string()" except that strings will have
 * their quotes shown in the result.
 *
 * \subsection lightness_function lightness()
 *
 * \code{.scss}
 *      lightness(color)
 * \endcode
 *
 * Extract the lightness component of a color. Note that we keep our colors
 * as RGBA floats so extracting the lightness includes a step calculating
 * the HSL value out of which we return the lightness.
 *
 * \f[
 *  result = color_l
 * \f]
 *
 * \subsection log_function log()
 *
 * \code{.scss}
 *      log(number)
 * \endcode
 *
 * Calculate the natural (or neperian) logarithm value of number.
 *
 * There is no \b exp() function because you may just use the power operator
 * as in:
 *
 * \code{.scss}
 *      $_csspp_e ** number
 * \endcode
 *
 * \subsection max_function max()
 *
 * \code{.scss}
 *      max(number, number, ...)
 * \endcode
 *
 * Retrieve the largest value from a list of numbers. The list has to
 * have at least one number, there is no upper limit.
 *
 * All the numbers must have the same unit or no units.
 *
 * \subsection min_function min()
 *
 * \code{.scss}
 *      min(number, number, ...)
 * \endcode
 *
 * Retrieve the smallest value from a list of numbers. The list has to
 * have at least one number, there is no upper limit.
 *
 * All the numbers must have the same unit or no units.
 *
 * \subsection not_function not()
 *
 * \code{.scss}
 *      not(boolean)
 * \endcode
 *
 * The \b not() function returns true if the \p boolean expression represents
 * false and vice versa.
 *
 * \note
 * We do not offer the '!' operator because that's way too confusing with
 * the '!important' and other similar flags. Note that in CSS 3 (and most
 * certainly ealier versions too), the \b !important flag could be written
 * with spaces ('! important') and thus we cannot be sure whether it is a
 * flag or a boolean \b not. For that reason we decided to simply offer a
 * \b not() function.
 *
 * \subsection random_function random()
 *
 * \code{.scss}
 *      random(number)
 * \endcode
 *
 * Generate a really bad random value between 0.0 and 1.0 (1.0 excluded.)
 *
 * \subsection red_function red()
 *
 * \code{.scss}
 *      red(color)
 * \endcode
 *
 * The \b red() function retrieves the current red value of the
 * specified color.
 *
 * The color components are not clamped so the red value may be
 * out of range. However, the default range is from 0.0 to 255.0
 * inclusive. The value is returned as a decimal number.
 *
 * \subsection rgb_function rgb()
 *
 * \code{.scss}
 *      rgb(color)
 *        or
 *      rgb(red, green, blue)
 * \endcode
 *
 * The \b rgb() function force the alpha channel of a color to 1.0 (opaque)
 * or transforms three numbers in a color with its alpha channel set to 1.0.
 *
 * The color components are expected to be values from 0 to 255. They may
 * be integers or decimal numbers. The numbers can be out of range in
 * which case they will be clamped by the assembler whenever the output
 * is generated. This allows you to apply various mathematical functions
 * to your colors before the output gets generated.
 *
 * \subsection rgba_function rgba()
 *
 * \code{.scss}
 *      rgba(color, alpha)
 *        or
 *      rgba(red, green, blue, alpha)
 * \endcode
 *
 * The \b rgba() function force the alpha channel of a color to the
 * specified value or transforms four numbers in a color with its
 * alpha channel specified.
 *
 * The color components are expected to be values from 0 to 255 and
 * the alpha channel a number between 0.0 and 1.0. All the numbers
 * can be out of range. They get clamped only at the time they are
 * output by the assembler. This allows you to apply various mathematical
 * functions to your colors before the output gets generated.
 *
 * \subsection round_function round()
 *
 * \code{.scss}
 *      round(number)
 * \endcode
 *
 * Round the specified number to the nearest number. This function
 * has no effects on integers.
 *
 * \subsection saturation_function saturation()
 *
 * \code{.scss}
 *      saturation(color)
 * \endcode
 *
 * Extract the saturation component of a color. Note that we keep our colors
 * as RGBA floats so extracting the saturation includes a step calculating
 * the HSL value out of which we return the saturation.
 *
 * \f[
 *      result = color_s
 * \f]
 *
 * \subsection sign_function sign()
 *
 * \code{.scss}
 *      sign(number)
 * \endcode
 *
 * Extract the sign of a number. The result is -1 for negative numbers,
 * 0 for zero, and 1 for positive numbers.
 *
 * \f[
 * result = \begin{cases} -1 & , number < 0
 * \\ 0 & , number = 0
 * \\ 1 & , number > 0\end{cases}
 * \f]
 *
 * Note that the function returns 0 for the decimal number -0.0.
 *
 * \subsection sin_function sin()
 *
 * \code{.scss}
 *      sin(number)
 * \endcode
 *
 * Calculate the sine value of number.
 *
 * \subsection sqrt_function sqrt()
 *
 * \code{.scss}
 *      sqrt(number)
 * \endcode
 *
 * Calculate the square root value of \p number.
 *
 * If \p number has a dimension, it has to be squared. For example, "cm * cm"
 * (for \f$cm^2\f$) and the resulting value will be "cm". If a dimension is
 * defined but not squared, then an error results.
 *
 * \f[
 * result = \sqrt{number}
 * \f]
 *
 * \subsection string_function string()
 *
 * \code{.scss}
 *      string(expression)
 * \endcode
 *
 * This function is a \em cast that transforms its parameter in an
 * string.
 *
 * Nearly all the values that support a string are accepted as a parameter:
 *
 * \li color
 * \li decimal_number
 * \li integer
 * \li percent
 * \li placeholder
 * \li string
 * \li url
 *
 * All of these may not always work exactly as expected. If the input
 * is a string, no transformation is performed.
 *
 * Note that integers and decimal numbers are converted to a string
 * including a minus sign when negative and their dimension if they
 * have one.
 *
 * \subsection str_length_function str_length()
 *
 * \code{.scss}
 *      str_length(string)
 * \endcode
 *
 * The \b str_length() function returns the number of characters found
 * in the specified \em string parameter. The length is returned as an
 * integer.
 *
 * To compute the length of nearly any type, you may first want to
 * stringify the parameter as in:
 *
 * \code{.scss}
 *      str_length(string(expression))
 * \endcode
 *
 * The length is the number of characters. So if you have a UTF-8
 * string, the length may not be the number of bytes in the string.
 *
 * \subsection tan_function tan()
 *
 * \code{.scss}
 *      tan(number)
 * \endcode
 *
 * Calculate the tangent value of number.
 *
 * \subsection type_of_function type_of()
 *
 * \code{.scss}
 *      type_of(expression)
 * \endcode
 *
 * The \b type_of() function returns a string naming the type of the
 * specified expression. The types availabe in the CSS Preprocessor
 * are as follow:
 *
 * \li type_of(100px) => "integer"
 * \li type_of(3.5em) => "number"
 * \li type_of(25%) => "number"
 * \li type_of(solid) => "identifier"
 * \li type_of("Hello world!") => "string"
 * \li type_of(true) => "bool"
 * \li type_of(false) => "bool"
 * \li type_of(15 = 42) => "bool"
 * \li type_of(\#fff) => "color"
 * \li type_of(chocolate) => "color"
 * \li type_of(U+4??) => "unicode-range"
 * \li type_of((hello: "world", thank: "you")) => "map"
 * \li type_of((32px, 55px, 172px)) => "array"
 *
 * Note that this function does not tell you whether a number
 * is a specific dimension or is dimension less.
 *
 * The \ref unit_function function returns the dimension of a
 * number. For a percentage, unit() returns "%". For a dimension
 * less number, it returns an empty string.
 *
 * The \ref unitless_function function returns true if the
 * number has no unit (i.e. is a plain integer or decimal number).
 *
 * \subsection unique_id_function unique_id()
 *
 * \code{.scss}
 *      unique_id()
 *      or
 *      unique_id(identifier)
 * \endcode
 *
 * The \b unique_id() function generates a unique identifier. At this
 * point the identifier looks like: "_csspp_unique<number>". The
 * number is incremented by one each time the function is called.
 *
 * The identifier name may be changed by specifying a different
 * identifier or string as the first parameter of the command.
 * Note that the function uses a single counter so the number
 * continues to increment just the same whatever the string
 * passed in (or no string passed in). If you pass an empty
 * string, the default string (\c _csspp_unique) is used.
 *
 * \code
 *      unique_id()         // returns "_csspp_unique1"
 *      unique_id(my_id)    // returns "my_id2"
 *      unique_id('alpha')  // returns "alpha3"
 * \endcode
 *
 * \subsection unit_function unit()
 *
 * \code{.scss}
 *      unit(number)
 * \endcode
 *
 * The \b unit() function extracts the unit of a dimension. A number
 * without a unit returns the empty string. A percentage returns
 * the special dimension, "%".
 *
 * The function returns the current unit as is. So if you multiply two
 * dimensions with each others, the unit returned is the product of
 * their units.
 *
 * \code{.scss}
 *      unit(4px * 3px) = "px * px"
 * \endcode
 *
 * Note that the \ref type_of_function function tells you whether
 * a number is an "integer" or a decimal "number", including
 * percentages. However, it does not tell you the unit so you
 * cannot distinguish between various dimensions or percentages.
 *
 * \subsection variable_exists_function variable_exists()
 *
 * \code{.scss}
 *      variable_exists(name)
 * \endcode
 *
 * The \b variable_exists() function checks whether a variable with the
 * specified name was set prior to this statement.
 *
 * The parameter can be a string or an identifier representing the name
 * of the variable:
 *
 * \code{.scss}
 *      $var: 123px;
 *
 *      width: variable_exists(var) ? $var + 0 : 123;
 *
 *      height: variable_exists("var") ? $var + 0 : 123;
 * \endcode
 *
 * \note
 * The "... + 0" in the previous example is necessary because when
 * $var is not defined, the statement would become empty.
 *
 * \subsection missing_internal_functions Internal functions still missing
 *
 * The following is a brief list of internal functions we will be adding
 * at some point:
 *
 * \li call() -- call a function
 * \li comparable() -- check whether two items can be added, subtracted, compared
 * \li feature_exists() -- check whether a certain feature exists in CSS Preprocessor
 * \li keywords() -- retrieve the '...' parameters of a function as a map
 * \li length() -- return the number of items in a map or an array
 * \li map_has_key() -- return true if the map has the specified key
 * \li map_keys() -- retrieve the keys of a map as an array
 * \li map_values() -- retrieve the values of a map as an array
 * \li set_nth() -- replace specified parameter with a new value in an array or a map
 *
 * \section user_functions User Functions
 *
 * I have the intend to also define a set of user functions that extend
 * the functionality without having to change the internal code. There
 * is a brief list of what I am thinking we can write as user functions
 * once all or at least most of the internal functions are available and
 * the \@return functionality is available.
 *
 * Many of these functions are based on the functions shown in SASS:
 *
 * http://sass-lang.com/documentation/Sass/Script/Functions.html
 *
 * Note that many of the list and map based functions are not likely to
 * work like in SASS any time soon because we do not have the same
 * list concept that SASS has (we're close... we'll see whether we
 * can do that one day.)
 *
 * \subsection adjust_hue_function adjust_hue()
 *
 * \code{.scss}
 *      adjust_hue(color, adjustment)
 * \endcode
 *
 * The \b adjust_hue() function expects two parameters: a color and
 * an adjustment representing an angle. That number is added to the current
 * hue of the color. The adjustment may be a negative number.
 *
 * \f[
 *  result_h = color_h + adjustment
 * \f]
 *
 * The other components do not get modified, although since we save
 * colors in RGB, obviously, they may be affected slightly.
 *
 * Since the color is kept as RGB components, the computation uses the
 * get_hsl() and the set_hsl() color functions.
 *
 * \subsection complement_function complement()
 *
 * \code{.scss}
 *      complement(color)
 * \endcode
 *
 * The \b complement() function turns the hue of a color by 180&deg;.
 *
 * \f[
 *  result_h = color_h + \pi
 * \f]
 *
 * The other components do not get modified, although since we save
 * colors in RGB, obviously, they may be affected slightly.
 *
 * In SCSS, this is equivalent to:
 *
 * \code{.scss}
 *      adjust_hue(color, 180deg)
 * \endcode
 *
 * \subsection darken_function darken()
 *
 * \code{.scss}
 *      darken(color, adjustment)
 * \endcode
 *
 * The \b darken() function subtracts the specified \p adjustment from the
 * lightness parameter of \p color.
 *
 * \f[
 * result_l = color_l - adjustment
 * \f]
 *
 * The other components do not get modified, although since we save
 * colors in RGB, obviously, they may be affected slightly.
 *
 * \subsection desaturate_function desaturate()
 *
 * \code{.scss}
 *      desaturate(color, adjustment)
 * \endcode
 *
 * The \b desaturate() function subtracts the specified \p adjustment from
 * the saturation parameter of \p color.
 *
 * \f[
 * result_s = color_s - adjustment
 * \f]
 *
 * The other components do not get modified, although since we save
 * colors in RGB, obviously, they may be affected slightly.
 *
 * \subsection grayscale_function grayscale()
 *
 * \code{.scss}
 *      grayscale(color)
 * \endcode
 *
 * The \b grayscale() function removes all the saturation from a color,
 * which means the color becomes black, gray, or white.
 *
 * \f[
 *  result_s = 0
 * \f]
 *
 * The other components do not get modified, although since we save
 * colors in RGB, obviously, they may be affected slightly.
 *
 * \subsection invert_function invert()
 *
 * \code{.scss}
 *      invert(color)
 * \endcode
 *
 * The \b invert() function calculates the opposite component value. This
 * is (1.0 - component). It only affects the red, green, and blue components.
 *
 * \f[
 * \begin{cases} result_r = 1.0 - color_r
 * \\ result_g = 1.0 - color_g
 * \\ result_b = 1.0 - color_b\end{cases}
 * \f]
 *
 * \note
 * The colors are saved as floating point values from 0.0 to 1.0 (although
 * we do not clamp these by default so you may have negative numbers and
 * numbers larger than 1.0). This is similar to doing \f$255 - color_c\f$
 * if the components were unsigned bytes from 0 to 255.
 *
 * \subsection lighten_function lighten()
 *
 * \code{.scss}
 *      lighten(color, adjustment)
 * \endcode
 *
 * The \b lighten() function adds the specified \p adjustment to the
 * lightness parameter of \p color.
 *
 * \f[
 * result_l = color_l + adjustment
 * \f]
 *
 * The other components do not get modified, although since we save
 * colors in RGB, obviously, they may be affected slightly.
 *
 * \subsection mix_function mix()
 *
 * \code{.scss}
 *      mix(color1, color2, weigth: 0.5)
 * \endcode
 *
 * The \b mix() function adds two colors together using a weight.
 *
 * \f[
 * color_{result} = color_1 \, weight + color_2 \, (1 - weight)
 * \f]
 *
 * By default the weight is 0.5 which is equivalent to adding two
 * colors together and dividing by two:
 *
 * \code{.scss}
 *      mix($c1, $c2) = ($c1 + $c2) / 2.0
 * \endcode
 *
 * This means:
 *
 * \f[
 * \large color_{result} = \frac{color_1 + color_2}{2}
 * \f]
 *
 * If you want to mix more colors, you may write that in one statement
 * such as:
 *
 * \code{.scss}
 *      ($c1 * $w1 + $c2 * $w2 + $c3 * $w3 + $c4 * $w4) / ($w1 + $w2 + $w3 + $w4)
 * \endcode
 *
 * This means:
 *
 * \f[
 * \large color_{result} = \frac{\sum\limits_{i=0}^n color_{i} \, weight_{i}}{\sum\limits_{i=0}^n weight_{i}}
 * \f]
 *
 * Assuming you know that the total of all the weights is equal to one, the
 * division is not necessary.
 *
 * Note that RGB colors are actually square roots of the real (physical)
 * color. Therefore, the \ref mix_function function does not calculate a correct
 * mixing of colors. We keep it that way to be compatible with the function
 * in SASS, however.
 *
 * There is a talk about this on this page:
 *
 * http://scottsievert.com/blog/2015/04/23/image-sqrt/
 *
 * See also the XYZ color space (by CIE):
 *
 * https://en.wikipedia.org/wiki/CIE_1931_color_space
 *
 * The correct math would be to calculate the squares of the components,
 * multiply them by their respective weight, divide by the sum of the
 * weights, and finally compute the square root of that number:
 *
 * \f[
 * \large component_{result} = \sqrt\frac{compoment_{a}^{2} \, weight_{a} + component_{b}^{2} \, weight_{b}}{weight_{a} + weight_{b}}
 * \f]
 *
 * When the weights are 0.5, we find the special case of a \em perfect
 * mix:
 *
 * \f[
 * \large component_{result} = \sqrt\frac{compoment_{a}^{2} + component_{b}^{2}}{2}
 * \f]
 *
 * One could use the following code (once power and sqrt() apply to colors):
 *
 * \code
 *  @mixin physical_mix($color1, $color2, $weight: 0.5)
 *  {
 *    @return sqrt($color1 ** 2 * $weight + $color2 ** 2 * (1.0 - $weight));
 *  }
 * \endcode
 *
 * \subsection opacify_function opacify() or fade_in()
 *
 * \code{.scss}
 *      opacify(color, adjustment)
 *      fade_in(color, adjustment)
 * \endcode
 *
 * The \b opacify() function add the specified adjustment to the alpha
 * channel of a color:
 *
 * \f[
 * result_a = color_a + adjustment
 * \f]
 *
 * It is the same as calling \ref transparentize_function with \b -adjustment.
 *
 * \subsection opacity_function opacity()
 *
 * \code{.scss}
 *      opacity(color)
 * \endcode
 *
 * The \b opacity() function is an overload of the \ref alpha_function
 * function.
 *
 * \f[
 *  result = color_a
 * \f]
 *
 * \subsection percentage_function percentage()
 *
 * \code{.scss}
 *      percentage(number)
 * \endcode
 *
 * The \b percentage() function transforms a number in a percentage.
 *
 * The \ref set_unit_function function calls the \b percentage()
 * function when it is called with "%" as the unit:
 *
 * \code
 *      set_unit($number, "%")
 *  or
 *      percentage($number)
 * \endcode
 *
 * \subsection quote_function quote()
 *
 * \code{.scss}
 *      quote(identifier)
 * \endcode
 *
 * The \b quote() function transforms an \p identifier in a string.
 *
 * This function is based on the \ref string_function function so
 * any token that represents a string in an expression will be
 * transformed to a quoted string.
 *
 * \subsection remove_unit_function remove_unit()
 *
 * \code{.scss}
 *      remove_unit(number)
 * \endcode
 *
 * The \b remove_unit() function removes the unit of the number making
 * it a plain number instead of a dimension. If the number was already
 * a plain number, then nothing happens. If the number was an integer
 * it remains an integer.
 *
 * The function also removes the % of a percentage. Remember that a
 * percentage is a fraction. So <tt>remove_unit(30%)</tt> returns \c 0.3
 * and not 30.
 *
 * The \b remove_unit() function uses a trick: it divides the number
 * by "1<unit>" of the number.
 *
 * For example, if the number is in pixels (px), the operation would
 * look like this:
 *
 * \f[
 * result = { number \over 1px }
 * \f]
 *
 * \subsection saturate_function saturate()
 *
 * \code{.scss}
 *      saturate(color, adjustment)
 * \endcode
 *
 * The \b saturate() function adds the specified \p adjustment to the
 * saturation parameter of \p color and return the new color.
 * The \p adjustment is expected to be a percentage. It can be negative.
 *
 * \f[
 * result_s = color_s + adjustment
 * \f]
 *
 * The other components do not get modified, although since we save
 * colors in RGB, obviously, they may be affected slightly.
 *
 * \subsection set_unit_function set_unit()
 *
 * \code{.scss}
 *      set_unit(number, unit)
 * \endcode
 *
 * The \b set_unit() function replace the existing unit of number
 * with a new unit. The existing unit is removed using the
 * \ref remove_unit_function function. Then the new unit is added
 * to that plain number. The unit can be specified as a string
 * ("px") or directly as an identifier (em). If the number was
 * an integer it remains an integer.
 *
 * The \b set_unit() function uses a trick: it divides the number
 * by "1<existing-unit>" and multiply the number by "1<new-unit>".
 *
 * For example, if the new unit is 'em' and the old one was 'px',
 * the following expression is applied:
 *
 * \f[
 * result = { { number \over 1px } \times 1em }
 * \f]
 *
 * Note that in most cases the math is probably wrong. In this
 * example, 1em is probably something like 12px or so. So the
 * correct math would be to divide by 12px instead of 1px.
 *
 * You may achieve a better result with an operation such as
 * this one:
 *
 * \code
 *      $pixels: $number / 12px * 1em;
 * \endcode
 *
 * To define a number as a percentage, use "%". Although you may instead
 * use the \ref percentage_function function.
 *
 * \subsection transparentize_function transparentize() or fade_out()
 *
 * \code{.scss}
 *      transparentize(color, adjustment)
 *      fade_out(color, adjustment)
 * \endcode
 *
 * The \b transparentize() function subtract adjustment from the alpha
 * channel and returns the result.
 *
 * \f[
 * result_a = color_a - adjustment
 * \f]
 *
 * It is the same as calling \ref opacify_function with \b -adjustment.
 *
 * \subsection unitless_function unitless()
 *
 * \code{.scss}
 *      unitless(number)
 * \endcode
 *
 * The \b unitless() function returns true if \p number is
 * \em unit \em less
 * (i.e. is not a dimension, was not assigned a unit, and is not
 * a percentage either.)
 *
 * Note that the \ref type_of_function function tells you whether
 * a number is an "integer" or a decimal "number", including
 * percentages. However, it does not tell you the unit so you
 * cannot distinguish between various dimensions or percentages.
 *
 * \subsection unquote_function unquote()
 *
 * \code{.scss}
 *      unquote(string)
 * \endcode
 *
 * The \b unquote() function transforms a \p string in an identifier.
 *
 * This function is based on the \ref identifier_function function so
 * any token that represents a string in an expression will be transformed
 * to an identifier.
 *
 * \subsection missing_user_functions User functions still missing
 *
 * \li adjust_color() -- add the specified components to the corresponding color component
 * \li scale_color() -- fluidly scale the color
 * \li change_color() -- change one or more property of a color
 * \li ie_hex_str() -- convert color to Internet Explorer compatible color for a filter: ... field
 * \li str_insert() -- insert a string in another
 * \li str_index() -- find a string in another and get position
 * \li str_slice() -- retrieve part of a string
 * \li to_upper_case() -- transform string to all uppercase
 * \li to_lower_case() -- transform string to all lowercase
 * \li nth() -- return the nth element of an array or a map
 * \li join() -- concatenate an array or a map
 * \li append() -- add one value at the end of the array
 * \li index() -- search for a value in an array or map and return its position
 * \li zip() -- concatenate any number of lists into one
 * \li list_separator() -- return ',' because we only support such
 * \li map_get() -- return an item by name from a map
 * \li map_merge() -- merge two maps together
 * \li map_remove() -- remove items with the specified keys
 * \li mixin_exists() -- check whether a variable exists
 *
 * SASS also supports functions for selectors. We do not support expressions
 * in selectors, so these are probably not going to be supported any time
 * soon:
 *
 * \li selector_nest() -- nest selectors
 * \li selector_append() -- add selectors at the end of another
 * \li selector_extend() -- extend selectors a bit like \@extend
 * \li selector_replace() -- replace part of a selector with another selector
 * \li selector_unify() -- add a comma between two selectors
 * \li is_super_selector() -- check whether the second selector matches the first one to one
 * \li simple_selector() -- return a simple selector (?)
 * \li selector_parse() -- compile a selector in the same format used by &
 */

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// Also do: :syntax sync fromstart
// vim: ts=4 sw=4 et syntax=doxygen
