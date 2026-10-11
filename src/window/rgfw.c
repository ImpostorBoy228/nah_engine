#include "rgfw_config.h"

#include <stdarg.h>
#include <stdio.h>

/* RGFW_WAYLAND hard-defines RGFW_DEBUG, which printf()s every internal step.
   RGFW_PRINTF is the only sink it uses, so hijack it and drop the info spam
   while keeping errors and warnings on stderr. Every message is followed by a
   bare RGFW_PRINTF("\n"), so print the line break here instead. */
static void rgfw_log(const char* fmt, ...) {
    if (fmt[0] == '\n' && fmt[1] == '\0')
        return;

    if (fmt[0] == 'R' && fmt[1] == 'G' && fmt[2] == 'F' && fmt[3] == 'W'
        && fmt[4] == ' ' && fmt[5] == 'I') {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

#define RGFW_PRINTF(...) rgfw_log(__VA_ARGS__)

#define RGFW_IMPLEMENTATION
#define RGFW_EXPORT
#include "RGFW.h"