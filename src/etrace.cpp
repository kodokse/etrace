// WTLView.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "etrace.h"
#include "log_context.h"

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE g_hInstance;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

enum class CmdLineState {Initial, Select, Matching, InString};

using CommandLineMap = std::map<std::wstring, std::vector<std::wstring>>;

bool ParseCommandLine(LPWSTR cmdLine, CommandLineMap &result)
{
  CommandLineMap::iterator current_key = result.end();
  size_t current_key_index = 0;
  CmdLineState state = CmdLineState::Initial;
  auto len = wcslen(cmdLine);
  std::wstring current_value;
  size_t i = 0;
  size_t backtrack = 0;
  while(i < len)
  {
    if(state == CmdLineState::Initial)
    {
      switch(cmdLine[i])
      {
      case L'-':
        if (current_key != result.end())
        {
            current_key->second.push_back(std::move(current_value));
            current_key = result.end();
        }
        state = CmdLineState::Select;
        break;
      case L'\"':
        state = CmdLineState::InString;
        break;
      case L' ':
        if(current_key != result.end())
        {
          current_key->second.push_back(std::move(current_value));
          current_key = result.end();
        }
        break;
      default:
        current_value += cmdLine[i];
        break;
      }
    }
    else if(state == CmdLineState::Select)
    {
      for(auto k = result.begin(); k != result.end(); ++k)
      {
        if(cmdLine[i] == k->first[0])
        {
          current_key = k;
          current_key_index = 1;
          backtrack = i;
          state = CmdLineState::Matching;
          break;
        }
      }
    }
    else if(state == CmdLineState::Matching)
    {
      if(current_key_index >= current_key->first.length())
      {
        state = CmdLineState::Initial;
      }
      else if(cmdLine[i] != current_key->first[current_key_index++])
      {
        ++current_key;
        if(current_key == result.end())
        {
          // invalid key
          return false;
        }
        i = backtrack;
        current_key_index = 1;
      }
    }
    else if(state == CmdLineState::InString)
    {
      if(cmdLine[i] == L'\"')
      {
        if(current_key != result.end())
        {
          current_key->second.push_back(std::move(current_value));
          current_key = result.end();
        }
        state = CmdLineState::Initial;
      }
      else
      {
        current_value += cmdLine[i];
      }
    }
    ++i;
  }
  if(current_key != result.end())
  {
    current_key->second.push_back(std::move(current_value));
  }
  return true;
}

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int, LogContext *context);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR    lpCmdLine,
                      _In_ int       nCmdShow)
{
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

  // TODO: Place code here.

  // Initialize global strings
  LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
  LoadStringW(hInstance, IDC_ETRACE, szWindowClass, MAX_LOADSTRING);
  MyRegisterClass(hInstance);

  LogContext context(hInstance);

  CommandLineMap cmdLineMap;
  cmdLineMap[L"pdb"];
  auto &sessionNameVec = cmdLineMap[L"live"];
  auto &logVec = cmdLineMap[L"log"];
  ParseCommandLine(lpCmdLine, cmdLineMap);

  for(auto &&pdbs : cmdLineMap[L"pdb"])
  {
    context.AddPdbProvider(pdbs);
  }
  if(!sessionNameVec.empty())
  {
    context.InitializeLiveSession(sessionNameVec.front().empty() ? etl::NewGuidAsString() : sessionNameVec.front());
  }
  else if(!logVec.empty())
  {
    context.LoadEventLogFile(logVec.front());
  }

  // Perform application initialization:
  if(!InitInstance(hInstance, nCmdShow, &context))
  {
    return FALSE;
  }

  HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ETRACE));

  MSG msg;

  // Main message loop:
  while(GetMessage(&msg, nullptr, 0, 0))
  {
    if(!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) // && !IsDialogMessage(msg.hwnd, &msg))
    //if(msg.hwnd == NULL || !IsDialogMessage(msg.hwnd, &msg))
    {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  return (int)msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
  WNDCLASSEXW wcex;

  wcex.cbSize = sizeof(WNDCLASSEX);

  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = WndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ETRACE));
  wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_ETRACE);
  wcex.lpszClassName = szWindowClass;
  wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

  return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow, LogContext *context)
{
  g_hInstance = hInstance; // Store instance handle in our global variable

  HWND hWnd = CreateWindowExW(WS_EX_CONTROLPARENT, szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, context);

  if(!hWnd)
  {
    return FALSE;
  }

  ShowWindow(hWnd, nCmdShow);
  UpdateWindow(hWnd);

  return TRUE;
}

template <typename T>
inline T lerp(T a, T b, double t)
{
  return static_cast<T>((1.0 - t) * a + b * t);
}

inline COLORREF ColorLerp(COLORREF c1, COLORREF c2, double t)
{
  return RGB(lerp(GetRValue(c1), GetRValue(c2), t),
             lerp(GetGValue(c1), GetGValue(c2), t),
             lerp(GetBValue(c1), GetBValue(c2), t));
}

void HandleKeyDown(LogContext *context, WORD virtualKey)
{
  switch(virtualKey)
  {
  case VK_ESCAPE:
    context->ResetView();
    break;
  case VK_F5:
    context->LoadPdbFromDialog();
    break;
  }
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch(message)
  {
  case WM_CREATE:
  {
    auto info = reinterpret_cast<LPCREATESTRUCT>(lParam);
    auto context = reinterpret_cast<LogContext *>(info->lpCreateParams);
    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
    context->SetMainWindow(hWnd);
    context->InitTraceList();
    //
    context->BeginTrace();
  }
  break;
  case WM_NOTIFY:
  {
    auto context = reinterpret_cast<LogContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    auto nm = reinterpret_cast<NMHDR *>(lParam);
    switch(nm->code)
    {
    case HDN_ENDTRACK:
      context->RecalculateRatios(reinterpret_cast<NMHEADER *>(lParam));
      context->RepositionControls();
      break;
    case HDN_DIVIDERDBLCLICK:
      context->RecalculateRatios(reinterpret_cast<NMHEADER *>(lParam));
      context->RepositionControls();
      break;
    case LVN_COLUMNCLICK:
    {
      auto listView = reinterpret_cast<NMLISTVIEW *>(lParam);
      context->ActivateColumn(listView);
    }
    break;
    case LVN_GETDISPINFO:
    {
      NMLVDISPINFOW *plvdi = reinterpret_cast<NMLVDISPINFOW *>(lParam);
      context->SetItemText(plvdi);
      return TRUE;
    }
    break;
    case NM_CUSTOMDRAW:
    {
      auto lvd = reinterpret_cast<NMLVCUSTOMDRAW *>(lParam);
      if(lvd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
      {
        context->SetItemColors(lvd);
      }
      return CDRF_NOTIFYITEMDRAW;
    }
    break;
    case LVN_KEYDOWN:
      HandleKeyDown(context, reinterpret_cast<NMLVKEYDOWN *>(lParam)->wVKey);
      break;
    }
  }
  break;
  case WM_CONTEXTMENU:
  {
    auto context = reinterpret_cast<LogContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    context->HandleContextMenu();
  }
  break;
  case WM_COMMAND:
  {
    auto context = reinterpret_cast<LogContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    int wmId = LOWORD(wParam);
    if(context->IsFilterMessage(wmId))
    {
      switch(HIWORD(wParam))
      {
      case CBN_EDITCHANGE:
      case CBN_SELCHANGE:
        context->UpdateFilterText(wmId);
        context->ApplyFilters();
        break;
      }
    }
    else
    {
      // Parse the menu selections:
      switch(wmId)
      {
      case IDM_ABOUT:
        DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
        break;
      case IDM_EXIT:
        context->EndTrace();
        DestroyWindow(hWnd);
        break;
      case ID_FILE_OPEN:
        context->EndTrace();
        if(context->LoadEtlFromDialog())
        {
          context->BeginTrace();
        }
        break;
      case ID_FILE_NEW:
        context->EndTrace();
        if(context->InitializeLiveSession(etl::NewGuidAsString()))
        {
          context->BeginTrace();
        }
        break;
      case ID_FILE_LOADPDBS:
        context->LoadPdbFromDialog();
        break;
      default:
        return DefWindowProc(hWnd, message, wParam, lParam);
      }
    }
  }
  break;
  //case WM_GETDLGCODE:
  //  return DLGC_WANTTAB | DLGC_W;
  case WM_KEYDOWN:
    HandleKeyDown(reinterpret_cast<LogContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA)), wParam);
    break;
  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    // TODO: Add any drawing code that uses hdc here...
    EndPaint(hWnd, &ps);
  }
  break;
  case WM_SIZE:
    reinterpret_cast<LogContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA))->RepositionControls();
    break;
  case WM_CLOSE:
    reinterpret_cast<LogContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA))->EndTrace();
    DestroyWindow(hWnd);
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
  UNREFERENCED_PARAMETER(lParam);
  switch(message)
  {
  case WM_INITDIALOG:
    return (INT_PTR)TRUE;

  case WM_COMMAND:
    if(LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
    {
      EndDialog(hDlg, LOWORD(wParam));
      return (INT_PTR)TRUE;
    }
    break;
  }
  return (INT_PTR)FALSE;
}
