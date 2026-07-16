# Convert a binary file into a C source defining an external-linkage byte array
# and its length (so it can be linked into multiple translation units, unlike
# bin2h.cmake's `static const` header). Mirrors the UI's generated bundle_data.c.
# Invoke via `cmake -P` with:
#   -DINPUT=<path>  -DSYMBOL=<c-identifier>  -DOUTPUT=<path>
# Produces:  const unsigned char ${SYMBOL}[];  const unsigned int ${SYMBOL}_len;

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
"// auto-generated from ${INPUT} by cmake/bin2c.cmake — do not edit
const unsigned int ${SYMBOL}_len = ${BYTE_COUNT};
const unsigned char ${SYMBOL}[${BYTE_COUNT}] = {
${BODY}};
")
