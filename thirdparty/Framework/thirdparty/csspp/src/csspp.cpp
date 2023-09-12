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

/** \file
 * \brief Implementation of the CSS Preprocessor command line tool.
 * \tableofcontents
 *
 * This tool can be used as a verification, compilation, and compression
 * tool depending on your needs.
 *
 * The Snap! Websites environment uses the tool for verification when
 * generating a layout. Later a Snap! Website plugin compresses the various
 * files. That way the website system includes the original file and not
 * just the minimized version.
 *
 * \section csspp-options Command Line Options
 *
 * The following are the options currently supported by csspp:
 *
 * \subsection arguments --args or -a -- specifying arguments
 *
 * The SCSS scripts expect some variables to be set. Some of these variables
 * can be set on the command line with the --args option. The arguments are
 * added to an array that can be accessed as the variable $_csspp_args.
 *
 * \code
 *      // command line
 *      csspp --args red -- my-file.scss
 *
 *      // reference to the command line argument
 *      .flowers
 *      {
 *          border: 1px solid rgb(identifier($_csspp_args[1]));
 *      }
 * \endcode
 *
 * \warning
 * This example does not work yet because I did not yet implement the
 * rgb() internal function to transform input in a COLOR token. However,
 * I intend to work on the colors soonish and thus it could be fully
 * functional by the time you read the example.
 *
 * At this time there is no other way to access command line arguments.
 *
 * There is no $_csspp_args[0] since arrays in SCSS start at 1. This
 * also means you do not (yet) have access to the name of the program
 * compiling the code.
 *
 * Multiple arguments can be specified one after another:
 *
 * \code
 *      csspp --args red green blue -- my-file.css
 * \endcode
 *
 * \subsection debug --debug or -d -- show all messages, including @debug messages
 *
 * When specified, the error output is setup to output everything,
 * including fatal errors, errors, warnings, informational messages,
 * and debug messages.
 *
 * \subsection help --help or -h -- show the available command line options
 *
 * The --help command line option can be used to request that the csspp
 * print out the complete list of supported command line options in
 * stdout.
 *
 * The tool then quits immediately.
 *
 * \subsection include -I -- specify paths to include files
 *
 * Specify paths to user defined directories that include SCSS scripts
 * one can include using the @import command.
 *
 * By default the system looks for system defined scripts (i.e. the
 * default validation, version, and other similar scripts) under
 * the following directory:
 *
 * \code
 *      /usr/lib/csspp/scripts
 * \endcode
 *
 * The system scripts (initialization, closure, version) appear under
 * a sub-directory named "system".
 *
 * The validation scripts (field names, pseudo names, etc.) appear
 * under a sub-directory named "validation".
 *
 * There are no specific rules for where include files will be found.
 * The @import can use a full path or a local path. When a local path
 * is used, then all the specified -I paths are prepended until a
 * file matches. The first match is used.
 *
 * You may specify any number of include paths one after another. You
 * must specify -I only once:
 *
 * \code
 *      csspp ... -I my-scripts alfred-scripts extension-scripts ...
 * \endcode
 *
 * \subsection no_logo --no-logo -- hide the "logo"
 *
 * This option prevents the "logo" comment from being added at the end
 * of the output.
 *
 * \subsection output --output or -o -- specify the output
 *
 * This option may be used to specify a filename used to save the
 * output of the compiler. By default the output is written to
 * stdout.
 *
 * You may explicitly use '-' to write the output to stdout.
 *
 * \code
 *      csspp --output file.css my-script.scss
 * \endcode
 *
 * \subsection precision --precision or -p -- specify the precision to use with decimal number
 *
 * The output is written as consice as possible. Only that can cause problems
 * with decimal numbers getting written with less precision than you need.
 *
 * By default decimal numbers are written with 3 decimal numbers after the
 * decimal point. You may use the --precision command line option to change
 * that default to another value.
 *
 * \code
 *      csspp ... --precision 5 ...
 * \endcode
 *
 * Note that numbers such as 3.5 are not written with ending zeroes (i.e.
 * 3.50000) even if you increase precision.
 *
 * \warning
 * The percent numbers, which are also decimal numbers, do not take this
 * value in account. All percent numbers are always written with 2 decimal
 * digits after the decimal point. We may change that behavior in the
 * future if someone sees a need for it.
 *
 * \subsection quiet --quiet or -q -- make the output as quite as possible
 *
 * By default csspp prints out all messages except debug messages.
 *
 * This option also turns off informational and warning messages. So in
 * effect all that's left are error and fatal error messages.
 *
 * Note that if you used the --Werror command line options, warning
 * are transformed to errors and thus they get printed in your output
 * anyway.
 *
 * \subsection style --style or -s -- define the output style
 *
 * By default the csspp compiler is expected to compress your CSS data
 * as much as possible (i.e. it removes non-required spaces, delete empty
 * rules, avoid new lines, etc.)
 *
 * The --style options let choose a different output style than the
 * compressed style:
 *
 * \li --style compressed -- this is the default, it outputs files as
 * compressed as possible
 * \li --style tidy -- this option writes one rule per line, each rule is
 * as compressed as possible
 * \li --compact -- this option writes one declaration per line, making it
 * a lot easier to edit if you were to do such a thing; this output is
 * already quite gentle on humans and can easily be used for debug purposes
 * \li expanded -- this option prints everything as neatly as possible
 * for human consumption; the output uses many newlines and indentation
 * for declarations
 *
 * The best to see how each style really looks like is for you to test
 * with a large existing CSS file and check the output of csspp against
 * that file.
 *
 * For example, you could use the \c expanded format before reading a
 * file you found on a website as in:
 *
 * \code
 *      csspp --style expanded compressed.css
 * \endcode
 *
 * \subsection version --version -- print out the version and exit
 *
 * This command line option prints out the version of the csspp compiler
 * in stdout and then exits.
 *
 * \subsection warnings-to-errors --Werror -- transform warnings into errors
 *
 * The --Werror requests the compiler to generate errors whenever
 * a warning message was to be printed. This also has the side effect
 * of incrementing the error counter by one each time a warning is
 * found. Note that as a result the warning counter will always
 * remains zero nin this case.
 *
 * \note
 * You may want to note that this option uses two dashes (--) to specify.
 * With GNU C/C++, the command line accepts -Werror, with a single dash.
 *
 * \subsection command-line-filenames Input files
 *
 * Other parameters specified on the command line, or parameters defined
 * after a "--", are taken as .scss filenames. The "--" is mandatory if
 * you have a preceeding argument that accepts multiple values like the
 * --args and -I options.
 *
 * \code
 *      // no need for "--" in this case:
 *      csspp -I scripts -p 2 my-script.scss
 *
 *      // "--" required in this case:
 *      csspp -p 2 -I scripts -- my-script.scss
 * \endcode
 */

// csspp
//
#include    <csspp/assembler.h>
#include    <csspp/compiler.h>
#include    <csspp/exception.h>
#include    <csspp/parser.h>


// advgetopt
//
#include    <advgetopt/advgetopt.h>
#include    <advgetopt/exception.h>


// boost
//
#include    <boost/preprocessor/stringize.hpp>


// C++
//
#include    <cstdlib>
#include    <fstream>
#include    <iostream>


// C
//
#include    <unistd.h>


// last include
//
#include    <snapdev/poison.h>



namespace
{

void free_char(char * ptr)
{
    free(ptr);
}

// TODO: add support for configuration files & the variable

constexpr advgetopt::option g_options[] =
{
    advgetopt::define_option(
          advgetopt::Name("args")
        , advgetopt::ShortName('a')
        , advgetopt::Flags(advgetopt::command_flags<
                  advgetopt::GETOPT_FLAG_REQUIRED
                , advgetopt::GETOPT_FLAG_MULTIPLE>())
        , nullptr
        , "define values in the $_csspp_args variable map"
        , nullptr
    ),
    advgetopt::define_option(
          advgetopt::Name("debug")
        , advgetopt::ShortName('d')
        , advgetopt::Flags(advgetopt::standalone_command_flags<>())
        , advgetopt::Help("show all messages, including @debug messages")
    ),
    advgetopt::define_option(
          advgetopt::Name("include")
        , advgetopt::ShortName('I')
        , advgetopt::Flags(advgetopt::command_flags<
                  advgetopt::GETOPT_FLAG_MULTIPLE>())
        , advgetopt::Help("specify a path to various user defined CSS files; \"-\" to clear the list (i.e. \"-I -\")")
    ),
    advgetopt::define_option(
          advgetopt::Name("no-logo")
        , advgetopt::ShortName('\0')
        , advgetopt::Flags(advgetopt::standalone_command_flags<>())
        , advgetopt::Help("prevent the \"logo\" from appearing in the output file")
    ),
    advgetopt::define_option(
          advgetopt::Name("empty-on-undefined-variable")
        , advgetopt::ShortName('\0')
        , advgetopt::Flags(advgetopt::standalone_command_flags<>())
        , advgetopt::Help("if accessing an undefined variable, return an empty string, otherwise generate an error")
    ),
    advgetopt::define_option(
          advgetopt::Name("output")
        , advgetopt::ShortName('o')
        , advgetopt::Flags(advgetopt::standalone_command_flags<
                  advgetopt::GETOPT_FLAG_SHOW_USAGE_ON_ERROR>())
        , advgetopt::DefaultValue("out.css")
        , advgetopt::Help("save the results in the specified file")
    ),
    advgetopt::define_option(
          advgetopt::Name("precision")
        , advgetopt::ShortName('p')
        , advgetopt::Flags(advgetopt::standalone_command_flags<>())
        , advgetopt::Help("define the number of digits to use after the decimal point, defaults to 3; note that for percent values, the precision is always 2.")
    ),
    advgetopt::define_option(
          advgetopt::Name("quiet")
        , advgetopt::ShortName('q')
        , advgetopt::Flags(advgetopt::standalone_command_flags<>())
        , advgetopt::Help("suppress @info and @warning messages")
    ),
    advgetopt::define_option(
          advgetopt::Name("style")
        , advgetopt::ShortName('s')
        , advgetopt::Flags(advgetopt::command_flags<advgetopt::GETOPT_FLAG_REQUIRED>())
        , advgetopt::Help("output style: compressed, tidy, compact, expanded")
    ),
    advgetopt::define_option(
          advgetopt::Name("Werror")
        , advgetopt::Flags(advgetopt::standalone_command_flags<>())
        , advgetopt::Help("make warnings count as errors")
    ),
    advgetopt::define_option(
          advgetopt::Name("--")
        , advgetopt::Flags(advgetopt::command_flags<
                  advgetopt::GETOPT_FLAG_MULTIPLE
                , advgetopt::GETOPT_FLAG_DEFAULT_OPTION
                , advgetopt::GETOPT_FLAG_SHOW_USAGE_ON_ERROR>())
        , advgetopt::Help("[file.css ...]; use stdin if no filename specified")
    ),
    advgetopt::end_options()
};

// TODO: once we have stdc++20, remove all defaults
#pragma GCC diagnostic ignored "-Wpedantic"
advgetopt::options_environment const g_options_environment =
{
    .f_project_name = "csspp",
    .f_group_name = nullptr,
    .f_options = g_options,
    .f_options_files_directory = nullptr,
    .f_environment_variable_name = "CSSPPFLAGS",
    .f_environment_variable_intro = nullptr,
    .f_section_variables_name = nullptr,
    .f_configuration_files = nullptr,
    .f_configuration_filename = nullptr,
    .f_configuration_directories = nullptr,
    .f_environment_flags = advgetopt::GETOPT_ENVIRONMENT_FLAG_PROCESS_SYSTEM_PARAMETERS,
    .f_help_header = "Usage: %p [-<opt>] [file.css ...] [-o out.css]\n"
                     "where -<opt> is one or more of:",
    .f_help_footer = "%c",
    .f_version = CSSPP_VERSION,
    .f_license = "GNU GPL v2",
    .f_copyright = "Copyright (c) 2015-"
                   BOOST_PP_STRINGIZE(UTC_BUILD_YEAR)
                   " by Made to Order Software Corporation -- All Rights Reserved",
    //.f_build_date = UTC_BUILD_DATE,
    //.f_build_time = UTC_BUILD_TIME
};



class pp
{
public:
                                        pp(int argc, char * argv[]);

    int                                 compile();

private:
    std::shared_ptr<advgetopt::getopt>  f_opt;
    int                                 f_precision = 3;
};

pp::pp(int argc, char * argv[])
    : f_opt(new advgetopt::getopt(g_options_environment, argc, argv))
{
    if(f_opt->is_defined("quiet"))
    {
        csspp::error::instance().set_hide_all(true);
    }

    if(f_opt->is_defined("debug"))
    {
        csspp::error::instance().set_show_debug(true);
    }

    if(f_opt->is_defined("Werror"))
    {
        csspp::error::instance().set_count_warnings_as_errors(true);
    }

    if(f_opt->is_defined("precision"))
    {
        f_precision = f_opt->get_long("precision");
    }
}

int pp::compile()
{
    csspp::lexer::pointer_t l;
    csspp::position::pointer_t pos;
    std::unique_ptr<std::stringstream> ss;

    csspp::safe_precision_t safe_precision(f_precision);

    if(f_opt->is_defined("--"))
    {
        // one or more filename specified
        int const arg_count(f_opt->size("--"));
        if(arg_count == 1
        && f_opt->get_string("--") == "-")
        {
            // user asked for stdin
            pos.reset(new csspp::position("-"));
            l.reset(new csspp::lexer(std::cin, *pos));
        }
        else
        {
            std::unique_ptr<char, void (*)(char *)> cwd(get_current_dir_name(), free_char);
            ss.reset(new std::stringstream);
            pos.reset(new csspp::position("csspp.css"));
            for(int idx(0); idx < arg_count; ++idx)
            {
                // full paths so the -I have no effects on those files
                std::string filename(f_opt->get_string("--", idx));
                if(filename.empty())
                {
                    csspp::error::instance() << *pos
                            << "You cannot include a file with an empty name."
                            << csspp::error_mode_t::ERROR_WARNING;
                    return 1;
                }
                if(filename == "-")
                {
                    csspp::error::instance() << *pos
                            << "You cannot currently mix files and stdin. You may use @import \"filename\"; in your stdin data though."
                            << csspp::error_mode_t::ERROR_WARNING;
                    return 1;
                }
                if(filename[0] == '/')
                {
                    // already absolute
                    *ss << "@import \"" << filename << "\";\n";
                }
                else
                {
                    // make absolute so we do not need to have a "." path
                    *ss << "@import \"" << cwd.get() << "/" << filename << "\";\n";
                }
            }
            l.reset(new csspp::lexer(*ss, *pos));
        }
    }
    else
    {
        // default to stdin
        pos.reset(new csspp::position("-"));
        l.reset(new csspp::lexer(std::cin, *pos));
    }

    // run the lexer and parser
    csspp::error_happened_t error_tracker;
    csspp::parser p(l);
    csspp::node::pointer_t root(p.stylesheet());
    if(error_tracker.error_happened())
    {
        return 1;
    }

    csspp::node::pointer_t csspp_args(new csspp::node(csspp::node_type_t::LIST, root->get_position()));
    csspp::node::pointer_t args_var(new csspp::node(csspp::node_type_t::VARIABLE, root->get_position()));
    args_var->set_string("_csspp_args");
    csspp::node::pointer_t wrapper(new csspp::node(csspp::node_type_t::LIST, root->get_position()));
    csspp::node::pointer_t array(new csspp::node(csspp::node_type_t::ARRAY, root->get_position()));
    wrapper->add_child(array);
    csspp_args->add_child(args_var);
    csspp_args->add_child(wrapper);
    if(f_opt->is_defined("args"))
    {
        int const count(f_opt->size("args"));
        for(int idx(0); idx < count; ++idx)
        {
            csspp::node::pointer_t arg(new csspp::node(csspp::node_type_t::STRING, root->get_position()));
            arg->set_string(f_opt->get_string("args", idx));
            array->add_child(arg);
        }
    }
    root->set_variable("_csspp_args", csspp_args);

    // run the compiler
    csspp::compiler c;
    c.set_root(root);
    c.set_date_time_variables(time(nullptr));

    // add paths to the compiler (i.e. for the user and system @imports)
    if(f_opt->is_defined("I"))
    {
        int const count(f_opt->size("I"));
        for(int idx(0); idx < count; ++idx)
        {
            std::string const path(f_opt->get_string("I", idx));
            if(path == "-")
            {
                c.clear_paths();
            }
            else
            {
                c.add_path(path);
            }
        }
    }

    if(f_opt->is_defined("no-logo"))
    {
        c.set_no_logo();
    }

    if(f_opt->is_defined("empty-on-undefined-variable"))
    {
        c.set_empty_on_undefined_variable(true);
    }

    c.compile(false);
    if(error_tracker.error_happened())
    {
        return 1;
    }

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";
    csspp::output_mode_t output_mode(csspp::output_mode_t::COMPRESSED);
    if(f_opt->is_defined("style"))
    {
        std::string const mode(f_opt->get_string("style"));
        if(mode == "compressed")
        {
            output_mode = csspp::output_mode_t::COMPRESSED;
        }
        else if(mode == "tidy")
        {
            output_mode = csspp::output_mode_t::TIDY;
        }
        else if(mode == "compact")
        {
            output_mode = csspp::output_mode_t::COMPACT;
        }
        else if(mode == "expanded")
        {
            output_mode = csspp::output_mode_t::EXPANDED;
        }
        else
        {
            csspp::error::instance() << root->get_position()
                    << "The output mode \""
                    << mode
                    << "\" is not supported. Try one of: compressed, tidy, compact, expanded instead."
                    << csspp::error_mode_t::ERROR_WARNING;
            return 1;
        }
    }

    std::ostream * out;
    if(f_opt->is_defined("output")
    && f_opt->get_string("output") != "-")
    {
        out = new std::ofstream(f_opt->get_string("output"));
    }
    else
    {
        out = &std::cout;
    }
    csspp::assembler a(*out);
    a.output(c.get_root(), output_mode);
    if(f_opt->is_defined("output")
    && f_opt->get_string("output") != "-")
    {
        delete out;
    }
    if(error_tracker.error_happened())
    {
        // this should be rare as the assembler generally does not generate
        // errors (it may throw though.)
        return 1;
    }

    return 0;
}

} // no name namespace

int main(int argc, char *argv[])
{
    try
    {
        pp preprocessor(argc, argv);
        return preprocessor.compile();
    }
    catch(advgetopt::getopt_exit const & except)
    {
        return except.code();
    }
    catch(csspp::csspp_exception_exit const & e)
    {
        // something went wrong in the library
        return e.exit_code();
    }
    catch(csspp::csspp_exception_logic const & e)
    {
        std::cerr << "fatal error: a logic exception, which should NEVER occur, occurred: " << e.what() << std::endl;
        exit(1);
    }
    catch(csspp::csspp_exception_overflow const & e)
    {
        std::cerr << "fatal error: an overflow exception occurred: " << e.what() << std::endl;
        exit(1);
    }
    catch(csspp::csspp_exception_runtime const & e)
    {
        std::cerr << "fatal error: a runtime exception occurred: " << e.what() << std::endl;
        exit(1);
    }
    catch(advgetopt::getopt_undefined const & e)
    {
        std::cerr << "fatal error: an undefined exception occurred because of your command line: " << e.what() << std::endl;
        exit(1);
    }
    catch(advgetopt::getopt_invalid const & e)
    {
        std::cerr << "fatal error: there is an error on your command line, an exception occurred: " << e.what() << std::endl;
        exit(1);
    }
    catch(advgetopt::getopt_invalid_default const & e)
    {
        std::cerr << "fatal error: there is an error on your command line, you used a parameter without a value and there is no default. The exception says: " << e.what() << std::endl;
        exit(1);
    }
}

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
