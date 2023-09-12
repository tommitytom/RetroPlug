#!/bin/sh
#
# See the snapcmakemodules project for details about this script
#     https://github.com/m2osw/snapcmakemodules

if test -x ../../cmake/scripts/mk
then
	export TEST_OPTIONS="--scripts scripts --version-script ../../BUILD/Debug/contrib/csspp/scripts --show-errors"
	../../cmake/scripts/mk $*
else
	echo "error: could not locate the cmake mk script"
	exit 1
fi

#if test "$1" = "-t"
#then
#	make -C ../../../BUILD/contrib/csspp/
#	shift
#	TEST="$1"
#	if test -n "$TEST"
#	then
#		shift
#		echo run with \"$TEST\"
#	fi
#	../../../BUILD/contrib/csspp/tests/csspp_tests --scripts scripts --version-script ../../../BUILD/contrib/csspp/scripts "$TEST" $*
#elif test "$1" = "-c"
#then
#	if test -z "$2"
#	then
#		echo "the -c option requires a second option with the name of the tag"
#		exit 1
#	fi
#	make -C ../../../BUILD/contrib/csspp install
#	../../../BUILD/contrib/csspp/tests/csspp_tests --scripts ../../../BUILD/dist/lib/csspp/scripts --show-errors "[$2]" 2>&1 | less
#fi
