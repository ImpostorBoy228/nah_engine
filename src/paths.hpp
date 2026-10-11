#pragma once

#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>

// resolve asset/script paths relative to the executable, not the cwd, so the
// game runs from a launcher or desktop with any working directory. linux only
// (/proc/self/exe), which is fine: the engine is wayland only.
namespace paths { // BLACKBOX

    // writes "<exe_dir>/<relative>" into out, absolute paths pass through
    inline const char* resolve(const char* relative, char* out, size_t size) {
        if (relative == NULL) {
            out[0] = '\0';
            return out;
        }
        if (relative[0] == '/') {
            snprintf(out, size, "%s", relative);
            return out;
        }

        char exe[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n <= 0) {
            // no /proc, fall back to the cwd
            snprintf(out, size, "%s", relative);
            return out;
        }
        exe[n] = '\0';

        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", exe);
        char* base = dirname(dir);

        // drop a leading "./" so we do not build dir/./file
        if (relative[0] == '.' && relative[1] == '/')
            relative += 2;

        snprintf(out, size, "%s/%s", base, relative);
        return out;
    }

} // namespace paths
