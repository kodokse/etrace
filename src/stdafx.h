// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <ampp/ampp.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <regex>
#include <map>

namespace fs = std::tr2::sys;

#include <ampp/etl/pdb_file.h>
#include <ampp/etl/pdb_provider.h>
#include <ampp/etl/time_util.h>
#include <ampp/etl/trace_enumerator.h>
#include <ampp/etl/guid_util.h>

// TODO: reference additional headers your program requires here
