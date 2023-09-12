
<p align="center">
<img alt="csspp" title="CSS Preprocessor -- a CSS compiler and compressor"
src="https://raw.githubusercontent.com/m2osw/csspp/master/doc/images/csspp-logo.png" width="256" height="141"/>
</p>

# CSS Preprocessor

The CSS language is actually pretty flat and rather cumbersome to maintain.
Many preprocessors have been created, but none in modern C++ supporting
CSS 3 and the preliminary CSS 4 languages. CSS Preprocessor offers that
functionality as a command line tool and a C++ library.

The main features of CSS Preprocessor are:

  * Nested rules
  * Nested fields
  * Variables
  * User defined functions
  * `@mixin` to create advanced / complex variables and functions
  * Conditional compilation
  * Strong validation of your CSS code (rules, field names, field data)
  * Minify or beautify CSS code


# Dependencies

The `CMakeLists.txt` file depends on the `snapCMakeModules`.

The `csspp` command line tool makes use of the `advgetopt` C++ library to
handle the command line argument.

The library requires at least C++14 (it may still work on C++11).

To generate the documentation, you will need to have Doxygen. That should
not be required (if you have problems with that, let me know.) If you
get Doxygen, having the dot tool is a good idea to get all the graphs
generated.


# Compile the library and `csspp` command line tool

The `INSTALL.md` in the root directory tells you how to generate the
distribution directory (or `dev/INSTALL.md` in the standalone `csspp` project).

We will be looking at making this simpler with time... for now, the
environment is a bit convoluted.


# Documentation

The [documentation](http://csspp.org/documentation/csspp-doc-1.0/ "CSS Preprocessor Documentation")
is available online. A copy can be downloaded from SourceForge.net.


# Bugs

Submit bug reports and patches on
[github](https://github.com/m2osw/csspp/issues).


_This file is part of the [snapcpp project](https://snapwebsites.org/)._
