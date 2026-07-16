/*
 * Portable host port of SameBoy's BootROMs/pb12.c logo compressor.
 *
 * The upstream tool (deps/sameboy/BootROMs/pb12.c) is written for POSIX
 * toolchains: it includes <unistd.h>, uses read()/write() on STDIN/STDOUT,
 * ssize_t, and void* pointer arithmetic — none of which MSVC accepts. Rather
 * than patch the pristine SameBoy submodule, RetroPlug builds this drop-in
 * replacement on Windows (see cmake/sameboy_bootroms.cmake). It is byte-for-byte
 * equivalent: same pb12 stream, read from stdin and written to stdout, but via
 * portable <stdio.h> with stdin/stdout forced to binary mode on Windows so the
 * CRT does not translate CRLF in the binary stream.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif

static void opts(uint8_t byte, uint8_t *options)
{
    *(options++) = byte | ((byte << 1) & 0xff);
    *(options++) = byte & (byte << 1);
    *(options++) = byte | ((byte >> 1) & 0xff);
    *(options++) = byte & (byte >> 1);
}

static void write_all(const void *buf, size_t count)
{
    if (count && fwrite(buf, 1, count, stdout) != count) {
        fprintf(stderr, "write");
        exit(1);
    }
}

int main(void)
{
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    static uint8_t source[0x4000];
    size_t size = fread(source, 1, sizeof(source), stdin);
    unsigned pos = 0;
    assert(size <= 0x4000);
    while (size && source[size - 1] == 0) {
        size--;
    }

    uint8_t literals[8];
    size_t literals_size = 0;
    unsigned bits = 0;
    unsigned control = 0;
    unsigned prev[2] = {-1, -1}; // Unsigned to allow "not set" values

    while (true) {

        uint8_t byte = 0;
        if (pos == size){
            if (bits == 0) break;
        }
        else {
            byte = source[pos++];
        }

        if (byte == prev[0] || byte == prev[1]) {
            bits += 2;
            control <<= 1;
            control |= 1;
            control <<= 1;
            if (byte == prev[1]) {
                control |= 1;
            }
        }
        else {
            bits += 2;
            control <<= 2;
            uint8_t options[4];
            opts(prev[1], options);
            bool found = false;
            for (unsigned i = 0; i < 4; i++) {
                if (options[i] == byte) {
                    // 01 = modify
                    control |= 1;

                    bits += 2;
                    control <<= 2;
                    control |= i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                literals[literals_size++] = byte;
            }
        }

        prev[0] = prev[1];
        prev[1] = byte;
        if (bits >= 8) {
            uint8_t outctl = control >> (bits - 8);
            assert(outctl != 1); // 1 is reserved as the end byte
            write_all(&outctl, 1);
            write_all(literals, literals_size);
            bits -= 8;
            control &= (1 << bits) - 1;
            literals_size = 0;
        }
    }
    uint8_t end_byte = 1;
    write_all(&end_byte, 1);

    return 0;
}
