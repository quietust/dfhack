#pragma once
namespace DFHack {
    namespace Version {
        const char *dfhack_version();
        const char *df_version();
        const char *dfhack_release();
    }
}

#ifndef NO_DFHACK_VERSION_MACROS
    #define DF_VERSION (DFHack::Version::df_version())
    #define DFHACK_RELEASE (DFHack::Version::dfhack_release())
    #define DFHACK_VERSION (DFHack::Version::dfhack_version())
#endif
