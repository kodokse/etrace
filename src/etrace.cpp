// WTLView.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "etrace.h"

#define IDC_TRACELIST 201
#define IDC_FIRSTFILTER 202
#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE g_hInstance;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

struct ColorInfo
{
  int count;
  int index;
  COLORREF bgColor;
  COLORREF txtColor;
};

struct ColumnContext
{
  HWND filterWindow;
  float sizeRatio;
  std::map<std::wstring, ColorInfo> columnColor;
};

struct ColorFilter
{
  std::wregex re;
  DWORD col;
  COLORREF bgColor;
  COLORREF txtColor;
  ColorFilter(const std::wstring &r, DWORD c, COLORREF bg, COLORREF fg)
    : re(r)
    , col(c)
    , bgColor(bg)
    , txtColor(fg)
  {}
};

struct WindowContext
{
  HWND mainWindow;
  HWND listView;
  DWORD itemCounter;
  int selectedColumn;
  etl::FormatDatabase fmtDb;
  std::unique_ptr<etl::TraceEnumerator> logTrace;
  etl::PdbFileManager pdbManager;
  std::vector<ColumnContext> columns;
  COLORREF customColors[16];
};

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
        state = CmdLineState::Select;
        break;
      case L'\"':
        state = CmdLineState::InString;
        break;
      case L' ':
        if(current_key != result.end() && !current_value.empty())
        {
          current_key->second.push_back(std::move(current_value));
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
  if(current_key != result.end() && !current_value.empty())
  {
    current_key->second.push_back(std::move(current_value));
  }
  return true;
}

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int, WindowContext *context);
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

  WindowContext context {nullptr, nullptr, 0, -1};

  CommandLineMap cmdLineMap;
  cmdLineMap[L"pdb"];
  auto &sessionNameVec = cmdLineMap[L"live"];
  auto &logVec = cmdLineMap[L"log"];
  ParseCommandLine(lpCmdLine, cmdLineMap);

  for(auto &&pdbs : cmdLineMap[L"pdb"])
  {
    context.fmtDb.AddProvider(etl::PdbProvider(context.pdbManager, pdbs));
  }
  if(!sessionNameVec.empty())
  {
    context.logTrace = std::make_unique<etl::LiveTraceEnumerator>(context.fmtDb, sessionNameVec.front());
  }
  else if(!logVec.empty())
  {
    context.logTrace = std::make_unique<etl::LogfileEnumerator>(context.fmtDb, logVec.front());
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
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow, WindowContext *context)
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

inline int Width(const RECT &r)
{
  return r.right - r.left;
}

inline int Height(const RECT &r)
{
  return r.bottom - r.top;
}

#define FILTER_HEIGHT 25

static wchar_t *columnNames[] = {L"ID", L"LOG", L"PROCESS", L"THREAD", L"FILE", L"FUNCTION", L"TIMESTAMP", L"MESSAGE"};

BOOL InitTraceList(WindowContext *context)
{
  RECT r;
  GetClientRect(context->mainWindow, &r);
  HWND hWnd = CreateWindowExW(0, WC_LISTVIEW, L"", WS_BORDER | WS_VISIBLE | WS_TABSTOP | WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA,
                            r.left, r.top + FILTER_HEIGHT, Width(r), Height(r) - FILTER_HEIGHT, context->mainWindow, reinterpret_cast<HMENU>(IDC_TRACELIST), g_hInstance, nullptr);

  if(!hWnd)
  {
    return FALSE;
  }

  context->listView = hWnd;

  ListView_SetExtendedListViewStyleEx(hWnd, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);

  const int colCount = _countof(columnNames);

  const int colWidth = Width(r) / colCount;
  const float ratio = static_cast<float>(colWidth) / Width(r);

  int x = r.left;
  for(int i = 0; i < colCount; ++i)
  {
    HWND cbWnd = CreateWindowExW(0, WC_COMBOBOX, L"", WS_BORDER | WS_VISIBLE | WS_TABSTOP | WS_CHILD | CBS_DROPDOWN | CBS_HASSTRINGS,
                            x, r.top, colWidth, FILTER_HEIGHT, context->mainWindow, reinterpret_cast<HMENU>(IDC_FIRSTFILTER + i), g_hInstance, nullptr);
    x += colWidth;
    //
    context->columns.push_back(ColumnContext {cbWnd, ratio});
    //
    LVCOLUMN col;
    ZeroMemory(&col, sizeof(col));
    col.pszText = columnNames[i];
    col.mask = LVCF_TEXT;
    ListView_InsertColumn(hWnd, i, &col);
    ListView_SetColumnWidth(hWnd, i, colWidth);
  }

  return TRUE;
}

void RecalculateRatios(WindowContext *context, NMHEADER *hdr)
{
  RECT r;
  GetWindowRect(context->listView, &r);
  const int w = Width(r);
  for(size_t i = 0; i < context->columns.size(); ++i)
  {
    auto colW = i == hdr->iItem && hdr->pitem ? hdr->pitem->cxy : ListView_GetColumnWidth(context->listView, i);
    context->columns[i].sizeRatio = static_cast<float>(colW) / w;
  }
}

void RepositionControls(WindowContext *context)
{
  RECT r;
  RECT or;
  RECT nr;
  GetClientRect(context->mainWindow, &r);
  GetWindowRect(context->listView, &or);
  SetWindowPos(context->listView, nullptr, r.left, r.top + FILTER_HEIGHT, Width(r), Height(r) - FILTER_HEIGHT, SWP_NOZORDER);
  GetWindowRect(context->listView, &nr);
  const int colCount = _countof(columnNames);
  const int ow = Width(or);
  const int nw = Width(nr);
  int x = r.left;
  for(int i = 0; i < colCount; ++i)
  {
    auto newColW = static_cast<int>(context->columns[i].sizeRatio * nw);
    ListView_SetColumnWidth(context->listView, i, newColW);
    SetWindowPos(context->columns[i].filterWindow, nullptr, x, r.top, newColW, FILTER_HEIGHT, SWP_NOZORDER);
    x += newColW;
  }
}

etl::TraceEventDataItem ColumnToDataItem(int column)
{
  static const etl::TraceEventDataItem columns[] = {
    etl::TraceEventDataItem::TraceIndex,
    etl::TraceEventDataItem::ModuleName,
    etl::TraceEventDataItem::ProcessId,
    etl::TraceEventDataItem::ThreadId,
    etl::TraceEventDataItem::SourceFile,
    etl::TraceEventDataItem::Function,
    etl::TraceEventDataItem::TimeStamp,
    etl::TraceEventDataItem::Message,
  };
  if(column >= 0 && column < _countof(columns))
  {
    return columns[column];
  }
  return etl::TraceEventDataItem::MAX_ITEM;
}

int DataItemToColumn(etl::TraceEventDataItem item)
{
  static const std::map<etl::TraceEventDataItem, int> columns = {
    {etl::TraceEventDataItem::TraceIndex, 0},
    {etl::TraceEventDataItem::ModuleName, 1},
    {etl::TraceEventDataItem::ProcessId, 2},
    {etl::TraceEventDataItem::ThreadId, 3},
    {etl::TraceEventDataItem::SourceFile, 4},
    {etl::TraceEventDataItem::Function, 5},
    {etl::TraceEventDataItem::TimeStamp, 6},
    {etl::TraceEventDataItem::Message, 7},
  };
  auto it = columns.find(item);
  if(it != columns.end())
  {
    return it->second;
  }
  return -1;
}

bool ValidRegEx(const std::wstring &txt)
{
  if(txt.empty())
  {
    return false;
  }
  wchar_t last = 0;
  int bsCount = 0;
  for(auto ch : txt)
  {
    if(last == L'\\' && ch >= L'0' && ch <= L'9')
    {
      return false;
    }
    if(ch == L'\\')
    {
      ++bsCount;
    }
    else
    {
      bsCount = 0;
    }
    last = ch;
  }
  return bsCount != 1;
}

bool FilterColumn(WindowContext *context, etl::TraceEventDataItem item, const std::wstring &txt)
{
  bool rv = true;
  wchar_t retxt[100];
  int column = DataItemToColumn(item);
  if(column < 0)
  {
    return true;
  }
  int l = ComboBox_GetText(context->columns[column].filterWindow, retxt, _countof(retxt));
  std::wstring res(retxt, l);
  if(ValidRegEx(res))
  {
    std::wregex r(res, std::regex_constants::icase);
    rv = std::regex_search(txt, r);
  }
  return rv;
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

COLORREF GenerateColor(int current, int unique_element_count)
{
  static const COLORREF colors[] = {
    RGB(0xFF, 0x00, 0x00), RGB(0x00, 0xFF, 0x00), RGB(0x00, 0x00, 0xFF), RGB(0xFF, 0xFF, 0x00),
    RGB(0xFF, 0x00, 0xFF), RGB(0x00, 0xFF, 0xFF), RGB(0xFF, 0xFF, 0xFF), RGB(0xFF, 0xFF, 0x88),
    RGB(0xFF, 0x88, 0x88), RGB(0x88, 0xFF, 0x88), RGB(0x88, 0x88, 0xFF),
  };
  //const COLORREF halfColor = 0x55555555;
  //// 16 red, green, blue, yellow, purple, teal, grey
  //const int grades = __min(255, __max(1, unique_element_count / _countof(colors)));
  //const int c = current % _countof(colors);
  //const double gradient = static_cast<double>(current) / _countof(colors);
  //return ColorLerp(colors[c], halfColor, gradient / grades);
  return colors[current % _countof(colors)];
}

bool UseWhiteText(COLORREF c)
{
  return __max(GetRValue(c), __max(GetGValue(c), GetBValue(c))) < 0x80 || (GetRValue(c) < 0x10 && GetGValue(c) < 0x10);
}

void ResetView(WindowContext *context)
{
  bool invalidate = context->selectedColumn >= 0;
  context->selectedColumn = -1;
  for(auto &c : context->columns)
  {
    if(GetWindowTextLengthW(c.filterWindow) > 0)
    {
      invalidate = true;
    }
    SetWindowTextW(c.filterWindow, L"");
  }
  if(invalidate)
  {
    context->logTrace->ApplyFilters();
  }
}

bool LoadPdbFromDialog(WindowContext *context)
{
  std::wstring fileBuf;
  fileBuf.resize(512);
  OPENFILENAMEW ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = context->mainWindow;
  ofn.Flags = OFN_ALLOWMULTISELECT | OFN_ENABLESIZING | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_LONGNAMES;
  ofn.lpstrFilter = L"Program Database Files\0*.pdb\0\0";
  ofn.lpstrFile = &fileBuf[0];
  ofn.nMaxFile = fileBuf.size();
  if(GetOpenFileNameW(&ofn))
  {
    auto firstNul = fileBuf.find_first_of(L'\0');
    if(firstNul < ofn.nFileOffset)
    {
      fs::path dir = &fileBuf[0];
      auto offset = ofn.nFileOffset;
      while(offset < fileBuf.length())
      {
        std::wstring name = &fileBuf[offset];
        if(!name.empty())
        {
          context->fmtDb.AddProvider(etl::PdbProvider(context->pdbManager, dir / name));
          offset += name.length() + 1;
        }
        else
        {
          break;
        }
      }
    }
    else
    {
      context->fmtDb.AddProvider(etl::PdbProvider(context->pdbManager, fileBuf));
    }
    ListView_SetItemCountEx(context->listView, context->logTrace->GetItemCount(), LVSICF_NOSCROLL);
    return true;
  }
  return false;
}

void HandleKeyDown(WindowContext *context, WORD virtualKey)
{
  switch(virtualKey)
  {
  case VK_ESCAPE:
    ResetView(context);
    break;
  case VK_F5:
    LoadPdbFromDialog(context);
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
    auto context = reinterpret_cast<WindowContext *>(info->lpCreateParams);
    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
    context->mainWindow = hWnd;
    InitTraceList(context);
    //
    for(etl::TraceEventDataItem colItem = etl::TraceEventDataItem::TraceIndex; colItem < etl::TraceEventDataItem::MAX_ITEM; ++colItem)
    {
      context->logTrace->AddFilter([context, colItem](etl::TraceEventDataItem item, const std::wstring &txt)
      {
        return item == colItem ? FilterColumn(context, item, txt) : true;
      });
    }
    context->logTrace->SetCountCallback([context](size_t itemCount)
    {
      if(itemCount > 0)
      {
        for(int i = 0; i < _countof(columnNames); ++i)
        {
          auto item = ColumnToDataItem(i);
          if(item != etl::TraceEventDataItem::MAX_ITEM)
          {
            auto &&s = context->logTrace->GetItemValue(itemCount - 1, item);
            auto &procCol = context->columns[i].columnColor[s];
            if(procCol.count++ == 0)
            {
              procCol.index = context->columns[i].columnColor.size();
              procCol.bgColor = 0;
            }
          }
        }
      }
      ListView_SetItemCountEx(context->listView, itemCount, LVSICF_NOSCROLL);
    });
    context->logTrace->Start();
    //
  }
  break;
  case WM_NOTIFY:
  {
    auto context = reinterpret_cast<WindowContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    auto nm = reinterpret_cast<NMHDR *>(lParam);
    if(nm->idFrom != 201)
    {
      int a = 32;
    }
    switch(nm->code)
    {
    case HDN_ENDTRACK:
      RecalculateRatios(context, reinterpret_cast<NMHEADER *>(lParam));
      RepositionControls(context);
      break;
    case HDN_DIVIDERDBLCLICK:
      RecalculateRatios(context, reinterpret_cast<NMHEADER *>(lParam));
      RepositionControls(context);
      break;
    case LVN_COLUMNCLICK:
    {
      auto listView = reinterpret_cast<NMLISTVIEW *>(lParam);
      auto &colInfo = context->columns[listView->iSubItem].columnColor;
      bool redraw = context->selectedColumn != listView->iSubItem;
      for(auto &cc : colInfo)
      {
        if(cc.second.bgColor == 0)
        {
          cc.second.bgColor = GenerateColor(cc.second.index, colInfo.size());
          if(UseWhiteText(cc.second.bgColor))
          {
            cc.second.txtColor = RGB(0xFF, 0xFF, 0xFF);
          }
          redraw = true;
        }
      }
      context->selectedColumn = listView->iSubItem;
      if(redraw)
      {
        ListView_SetItemCountEx(context->listView, context->logTrace->GetItemCount(), LVSICF_NOSCROLL);
      }
    }
    break;
    case LVN_GETDISPINFO:
    {
      NMLVDISPINFOW *plvdi = reinterpret_cast<NMLVDISPINFOW *>(lParam);
      plvdi->item.pszText = const_cast<LPWSTR>(context->logTrace->GetItemValue(plvdi->item.iItem, ColumnToDataItem(plvdi->item.iSubItem), nullptr));
      return TRUE;
    }
    break;
    case NM_CUSTOMDRAW:
    {
      auto lvd = reinterpret_cast<NMLVCUSTOMDRAW *>(lParam);
      if(lvd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
      {
        if(context->selectedColumn >= 0)
        {
          auto &&s = context->logTrace->GetItemValue(lvd->nmcd.dwItemSpec, ColumnToDataItem(context->selectedColumn));
          if(!s.empty())
          {
            auto &ci = context->columns[context->selectedColumn].columnColor[s];
            lvd->clrTextBk = ci.bgColor;
            lvd->clrText = ci.txtColor;
          }
        }
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
    auto context = reinterpret_cast<WindowContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    int selIdx = ListView_GetSelectionMark(context->listView);
    if(selIdx >= 0)
    {
      auto &&s = context->logTrace->GetItemValue(selIdx, ColumnToDataItem(context->selectedColumn));
      if(!s.empty())
      {
        auto &ci = context->columns[context->selectedColumn].columnColor[s];
        CHOOSECOLORW color;
        ZeroMemory(&color, sizeof(color));
        color.lStructSize = sizeof(color);
        color.hwndOwner = hWnd;
        color.lpCustColors = context->customColors;
        color.Flags = CC_ANYCOLOR;
        color.rgbResult = ci.bgColor;
        color.Flags |= CC_RGBINIT;
        if(ChooseColorW(&color))
        {
          ci.bgColor = color.rgbResult;
          if(UseWhiteText(ci.bgColor))
          {
            ci.txtColor = RGB(0xFF, 0xFF, 0xFF);
          }
          ListView_SetItemCountEx(context->listView, context->logTrace->GetItemCount(), LVSICF_NOSCROLL);
        }
      }
    }
  }
  break;
  case WM_COMMAND:
  {
    auto context = reinterpret_cast<WindowContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    int wmId = LOWORD(wParam);
    if(HIWORD(wParam) != CBN_SELENDCANCEL && HIWORD(wParam) != CBN_KILLFOCUS && HIWORD(wParam) != CBN_SETFOCUS)
    {
      int a = 42;
    }
    if(wmId >= IDC_FIRSTFILTER && wmId < IDC_FIRSTFILTER + _countof(columnNames))
    {
      switch(HIWORD(wParam))
      {
      case CBN_EDITCHANGE:
      case CBN_SELCHANGE:
        context->logTrace->ApplyFilters();
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
        DestroyWindow(hWnd);
        break;
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
    HandleKeyDown(reinterpret_cast<WindowContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA)), wParam);
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
    RepositionControls(reinterpret_cast<WindowContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA)));
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
