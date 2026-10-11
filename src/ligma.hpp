#pragma once
#include <cstdio>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "luajit.h"
}

class Ligma {
private:
    lua_State* L;
public:
    Ligma() {
        L = luaL_newstate();
        if (L) luaL_openlibs(L);
    }

    void ligma_execute(const char* script) {
        if (luaL_dofile(L, script) != 0) {
            fprintf(stderr, "lua: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }

    ~Ligma() {
        if (L) lua_close(L);
    }

    Ligma(const Ligma&) = delete;
    Ligma& operator=(const Ligma&) = delete;
};

struct GachiMan {
    int cumCounter;
    char* name;
};

extern "C" {
    GachiMan gachicreate(char* name) {
        return GachiMan {0,name};
    }
    void gachicum(GachiMan* g, int c) {
        g->cumCounter+=c;
    }
    void gachiprint(GachiMan* g) {
        printf("%s\n%d\n",g->name,g->cumCounter);
    }
}
