#!/bin/sh -e
#

URL=csspp.org

while test -n "$1"
do
	case "$1" in
	"--help"|"-h")
		echo "Usage: $0 <opts>"
		echo "where <opts> is:"
		echo "    --help | -h     print out this help screen"
		echo "    --url <URL>     change the default destination URL"
		exit 1
		;;

	"--url")
		shift
		URL="$1"
		shift
		;;

	*)
		echo "error: unknown option \"${1}\"."
		exit 1
		;;

	esac
done

echo "info: uploading website (will fail unless you are me)."
echo "info: you must build once to get the index.html file."
echo

# TODO: review how to handle this path, it won't work for everyone like this...
scp ../../BUILD/Debug/contrib/csspp/doc/front-page.html ${URL}:/var/www/csspp/public_html/index.html

scp doc/favicon.ico ${URL}:/var/www/csspp/public_html/favicon.ico
scp doc/images/csspp-logo.png ${URL}:/var/www/csspp/public_html/images/csspp-logo.png
scp doc/images/open-source-initiative-logo.png ${URL}:/var/www/csspp/public_html/images/open-source-initiative-logo.png

if test -d ../../BUILD/Debug/dist/share/doc/csspp/html
then
	. dev/version
	scp -r ../../BUILD/Debug/dist/share/doc/csspp/html ${URL}:/var/www/csspp/public_html/documentation/csspp-doc-${VERSION}
else
	echo "warning: documentation not found, it won't be uploaded."
fi

