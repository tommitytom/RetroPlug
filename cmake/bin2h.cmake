# Convert a binary file into a C header containing a static byte array and
# its length. Invoke via `cmake -P` with:
#   -DINPUT=<path>  -DSYMBOL=<c-identifier>  -DOUTPUT=<path>

file(READ "${INPUT}" HEX HEX)
string(LENGTH "${HEX}" HEX_LEN)
math(EXPR BYTE_COUNT "${HEX_LEN} / 2")

set(BODY "")
set(COL 0)
set(POS 0)
while(POS LESS HEX_LEN)
    string(SUBSTRING "${HEX}" ${POS} 2 BYTE)
    string(TOUPPER "${BYTE}" BYTE)
    if(COL EQUAL 0)
        set(BODY "${BODY}    ")
    endif()
    set(BODY "${BODY}0x${BYTE},")
    math(EXPR POS "${POS} + 2")
    math(EXPR COL "${COL} + 1")
    if(COL EQUAL 12)
        set(BODY "${BODY}\n")
        set(COL 0)
    endif()
endwhile()
if(NOT COL EQUAL 0)
    set(BODY "${BODY}\n")
endif()

file(WRITE "${OUTPUT}"
"// auto-generated from ${INPUT} by cmake/bin2h.cmake — do not edit
#pragma once
#include <stddef.h>

static const size_t ${SYMBOL}_len = ${BYTE_COUNT};
static const unsigned char ${SYMBOL}[${BYTE_COUNT}] = {
${BODY}};
")
