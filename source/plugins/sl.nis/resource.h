#include "versions.h"
#include "../../../_artifacts/gitVersion.h"

#define STR1(a) #a
#define STR(a) STR1(a)

#define VER_FILEVERSION             VERSION_MAJOR,VERSION_MINOR,VERSION_PATCH,0
#define VER_FILEVERSION_STR         STR(VER_FILEVERSION)

#define VER_PRODUCTVERSION          VERSION_MAJOR,VERSION_MINOR,VERSION_PATCH,0
#define VER_PRODUCTVERSION_STR      STR(VER_PRODUCTVERSION)

#define VER_COMPANYNAME_STR         "MooreThreads"
#define VER_FILEDESCRIPTION_STR     "SL.IMGUI PLUGIN"
#define VER_INTERNALNAME_STR        "SL.IMGUI"
#define VER_LEGALCOPYRIGHT_STR      "Copyright 2023 MooreThreads CORP"
#define VER_LEGALTRADEMARKS1_STR    "All Rights Reserved"
#define VER_LEGALTRADEMARKS2_STR    VER_LEGALTRADEMARKS1_STR
#define VER_ORIGINALFILENAME_STR    GIT_LAST_COMMIT
#define VER_PRODUCTNAME_STR         "MooreThreads STREAMLINE " BUILD_CONFIG_INFO

#define VER_COMPANYDOMAIN_STR       "www.mthreads.com"
