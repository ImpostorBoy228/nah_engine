#include "rgfw_config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// RGFW_WAYLAND hard-defines RGFW_DEBUG, which printf()s every internal step
// hijack RGFW_PRINTF, the only sink it uses, and drop the "RGFW I..." spam
// every message is followed by a bare RGFW_PRINTF("\n"), so print it here
static void rgfw_log(const char* fmt, ...) {
    if (fmt[0] == '\n' && fmt[1] == '\0')
        return;

    if (strncmp(fmt, "RGFW I", 6) == 0)
        return;

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