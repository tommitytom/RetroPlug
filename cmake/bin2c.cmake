# Convert a binary file into a C source defining an external-linkage byte array
# and its length (so it can be linked into multiple translation units, unlike
# bin2h.cmake's `static const` header). Mirrors the UI's generated bundle_data.c.
# Invoke via `cmake -P` with:
#   -DINPUT=<path>  -DSYMBOL=<c-identifier>  -DOUTPUT=<path>
# Produces:  const unsigned char ${SYMBOL}[];  const unsigned int ${SYMBOL}_len;
#
# The bytes are formatted by two regular expressions over the file's whole hex string rather than one string(APPEND) a
# byte: an append copies the string it grows, so a per-byte loop is quadratic and a pack embedding megabytes would stall
# a build. An empty file is refused here as the manifest reader refuses it, since `X[0] = {}` is no C and an empty array
# would be a resource a product carries and cannot read.

file(READ "${INPUT}" HEX HEX)
string(LENGTH "${HEX}" HEX_LEN)
math(EXPR BYTE_COUNT "${HEX_LEN} / 2")
if(BYTE_COUNT EQUAL 0)
    message(FATAL_ERROR "bin2c.cmake: ${INPUT} is empty; an array of no bytes is no C")
endif()

string(TOUPPER "${HEX}" HEX)
string(REGEX REPLACE "([0-9A-F][0-9A-F])" "0x\\1," BYTES "${HEX}")
# Twelve bytes a line, each line indented, and the line's own indent is the run this strips at the end.
string(REGEX REPLACE "((0x[0-9A-F][0-9A-F],){12})" "\\1\n    " LINES "${BYTES}")
set(BODY "    ${LINES}")
if(BODY MATCHES "\n    $")
    string(LENGTH "${BODY}" BODY_LEN)
    math(EXPR BODY_LEN "${BODY_LEN} - 4")
    string(SUBSTRING "${BODY}" 0 ${BODY_LEN} BODY)
endif()
if(NOT BODY MATCHES "\n$")
    string(APPEND BODY "\n")
endif()

file(WRITE "${OUTPUT}"
"// auto-generated from ${INPUT} by cmake/bin2c.cmake - do not edit
const unsigned int ${SYMBOL}_len = ${BYTE_COUNT};
const unsigned char ${SYMBOL}[${BYTE_COUNT}] = {
${BODY}};
")
