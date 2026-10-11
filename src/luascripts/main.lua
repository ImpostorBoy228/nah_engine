local ffi = require("ffi")
ffi.cdef[[
    typedef struct GachiMan GachiMan;
    struct GachiMan { int cumCounter; char* name; };
    void gachicum(GachiMan* g, int c);
    GachiMan gachicreate(const char* name);
    void gachiprint(GachiMan* g);
]]

local bo = ffi.C.gachicreate("Bo Sinn")
ffi.C.gachicum(bo,666)
ffi.C.gachiprint(bo)
