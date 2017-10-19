#include "stdafx.h"
#include "log_context.h"
#include "win_util.h"
#include "resource.h"

#define IDC_TRACELIST 201
#define IDC_FIRSTFILTER 202
#define FILTER_HEIGHT 25

LRESULT CALLBACK SubClassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{

static wchar_t *columnNames[] = {L"ID", L"LOG", L"PROCESS", L"THREAD", L"FILE", L"FUNCTION", L"TIMESTAMP", L"MESSAGE"};

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
  int brackets = 0;
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
      if (last != L'\\')
      {
        if (ch == L'[')
        {
          ++brackets;
        }
        if (ch == L']')
        {
          --brackets;
        }
      }
    }
    last = ch;
  }
  return (bsCount & 1) != 1 && brackets == 0;
}

bool UseWhiteText(COLORREF c)
{
  return __max(GetRValue(c), __max(GetGValue(c), GetBValue(c))) < 0x80 || (GetRValue(c) < 0x10 && GetGValue(c) < 0x10);
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

} // private namespace

////////////////////////

RowContext::RowContext()
  : colorFade(1.0f)
  , lastFilterCount(-1)
  , groupId(0)
{
}

///////////////////////

ColumnContext::ColumnContext(HWND fWnd, float ratio, LogContext *owner)
  : filterWindow(fWnd)
  , sizeRatio(ratio)
  , longestTextLength_(0)
  , owner_(owner)
  , orgProc_(nullptr)
{
}

bool ColumnContext::SetFilterText(const wchar_t *txt, int len)
{
  std::wstring newFilt(txt, len);
  bool rv = false;
  std::lock_guard<std::mutex> l(filterTextLock_);
  rv = filterText_ != newFilt;
  filterText_.swap(newFilt);
  return rv;
}

std::wstring ColumnContext::GetFilterText() const
{
  std::lock_guard<std::mutex> l(filterTextLock_);
  return filterText_;
}


///////////////////////////

LogContext::LogContext(HINSTANCE programInstance)
  : programInstance_(programInstance)
  , mainWindow_(nullptr)
  , listView_(nullptr)
  , itemCounter_(0)
  , selectedColumn_(-1)
  , filterId_(0)
  , groupCounter_(0)
  , currentMatchingLine_(std::numeric_limits<size_t>::max())
{
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

float LogContext::GetFilterLineFade(size_t line)
{
  for (auto &&flt : colorFilters_)
  {
    for (int c = 0; c < _countof(columnNames); ++c)
    {
      auto item = ColumnToDataItem(c);
      if (item != etl::TraceEventDataItem::MAX_ITEM)
      {
        auto &&text = logTrace_->GetItemValue(line, item);
        if (flt.first(c, text))
        {
          return flt.second;
        }
      }
    }
  }
  return 1.0f;
}

bool LogContext::ResetViewNoInvalidate()
{
  bool invalidate = selectedColumn_ >= 0;
  selectedColumn_ = -1;
  currentMatchingLine_ = -1;
  for (auto &c : columns_)
  {
    SetWindowTextW(c->filterWindow, L"");
    if (c->SetFilterText(L"", 0))
    {
      invalidate = true;
    }
  }
  return invalidate;
}

void LogContext::InitializeRowContext(size_t line)
{
  auto md = logTrace_->GetItemMetadata(line);
  if (!md)
  {
    std::lock_guard<std::mutex> l(rowInfoLock_);
    rowInfo_.push_back(std::make_unique<RowContext>());
    logTrace_->SetItemMetadata(line, rowInfo_.back().get());
  }
}

RowContext *LogContext::GetRowContext(size_t line)
{
  return reinterpret_cast<RowContext *>(logTrace_->GetItemMetadata(line));
}

const RowContext *LogContext::GetRowContext(size_t line) const
{
  return reinterpret_cast<const RowContext *>(logTrace_->GetItemMetadata(line));
}

std::function<bool(size_t*n)> LogContext::SelectedLinesEnumerator() const
{
  return std::function<bool(size_t*n)>([this](size_t *n)
  {
    int pos = *n == std::numeric_limits<size_t>::max() ? -1 : *n;
    pos = ListView_GetNextItem(listView_, pos, LVNI_SELECTED);
    if (pos >= 0)
    {
      *n = pos;
      return true;
    }
    return false;
  });
}

void LogContext::ApplyFilters()
{
  if(logTrace_)
  {
    logTrace_->ApplyFilters();
    ++filterId_;
  }
}

bool LogContext::AddPdbProvider(const fs::path &pdbPath)
{
  pdbPaths_.push_back(pdbPath);
  fmtDb_.AddProvider(etl::PdbProvider(pdbManager_, pdbPath));
  return true;
}

bool LogContext::InitializeLiveSession(const std::wstring &sessionName)
{
  sessionName_ = sessionName;
  logTrace_ = std::make_unique<etl::LiveTraceEnumerator>(fmtDb_, sessionName);
  windowTitle_ = sessionName + L" - LIVE ETRACE";
  return true;
}

bool LogContext::LoadEventLogFile(const fs::path &etlPath)
{
  if (etlPath.extension() == ".etl")
  {
    logTrace_ = std::make_unique<etl::LogfileEnumerator>(fmtDb_, etlPath);
  }
  else
  {
    logTrace_ = std::make_unique<etl::TxtfileEnumerator>(fmtDb_, etlPath);
  }
  windowTitle_ = etlPath.filename().wstring() + L" - ETRACE LOG";
  return true;
}

bool LogContext::InitTraceList()
{
  RECT r;
  GetClientRect(mainWindow_, &r);
  HWND hWnd = CreateWindowExW(0,
                              WC_LISTVIEW,
                              L"",
                              WS_BORDER | WS_VISIBLE | WS_TABSTOP | WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA,
                              r.left,
                              r.top + FILTER_HEIGHT,
                              Width(r),
                              Height(r) - FILTER_HEIGHT,
                              mainWindow_,
                              reinterpret_cast<HMENU>(IDC_TRACELIST),
                              programInstance_,
                              nullptr);

  if(!hWnd)
  {
    return false;
  }

  listView_ = hWnd;
  SetPropW(listView_, L"this", this);
  orgListViewProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(listView_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ListViewSubClassProc)));

  ListView_SetExtendedListViewStyleEx(hWnd, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);

  const int colCount = _countof(columnNames);

  const int colWidth = Width(r) / colCount;
  const float ratio = static_cast<float>(colWidth) / Width(r);

  int x = r.left;
  for(int i = 0; i < colCount; ++i)
  {
    HWND cbWnd = CreateWindowExW(0,
                                 WC_COMBOBOX,
                                 L"",
                                 WS_BORDER | WS_VISIBLE | WS_TABSTOP | WS_CHILD | CBS_DROPDOWN | CBS_HASSTRINGS,
                                 x,
                                 r.top,
                                 colWidth,
                                 FILTER_HEIGHT,
                                 mainWindow_,
                                 reinterpret_cast<HMENU>(IDC_FIRSTFILTER + i),
                                 programInstance_,
                                 nullptr);
    x += colWidth;
    //
    columns_.push_back(std::make_unique<ColumnContext>(cbWnd, ratio, this));
    POINT pt = { 0, 0 };
    HWND hwndEdit;
    // find first window within the combo box that isn't the combo box itself
    do
    {
      ++pt.x;
      ++pt.y;
      hwndEdit = ChildWindowFromPoint(cbWnd, pt);
    } while (hwndEdit == cbWnd);
    SetPropW(hwndEdit, L"this", columns_.back().get());
    columns_.back()->orgProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwndEdit,
                                                                            GWLP_WNDPROC,
                                                                            reinterpret_cast<LONG_PTR>(&ColumnContext::SubClassProc)));
    //
    LVCOLUMN col;
    ZeroMemory(&col, sizeof(col));
    col.pszText = columnNames[i];
    col.mask = LVCF_TEXT;
    ListView_InsertColumn(listView_, i, &col);
    ListView_SetColumnWidth(listView_, i, colWidth);
  }

  return TRUE;
}

void LogContext::RecalculateRatios(NMHEADER *hdr)
{
  RECT r;
  GetWindowRect(listView_, &r);
  const int w = Width(r);
  const int minW = w / 25;
  int maxW = w;
  for(size_t i = 0; i < columns_.size(); ++i)
  {
    auto colW = i == hdr->iItem && hdr->pitem ? hdr->pitem->cxy : ListView_GetColumnWidth(listView_, i);
    colW = __max(colW, minW);
    colW = __min(colW, maxW);
    maxW -= colW;
    columns_[i]->sizeRatio = static_cast<float>(colW) / w;
  }
}

void LogContext::RepositionControls()
{
  RECT r;
  RECT or;
  RECT nr;
  GetClientRect(mainWindow_, &r);
  GetWindowRect(listView_, &or);
  SetWindowPos(listView_, nullptr, r.left, r.top + FILTER_HEIGHT, Width(r), Height(r) - FILTER_HEIGHT, SWP_NOZORDER);
  GetWindowRect(listView_, &nr);
  const int colCount = _countof(columnNames);
  const int ow = Width(or);
  const int nw = Width(nr);
  int x = r.left;
  for(int i = 0; i < colCount; ++i)
  {
    auto newColW = static_cast<int>(columns_[i]->sizeRatio * nw);
    ListView_SetColumnWidth(listView_, i, newColW);
    SetWindowPos(columns_[i]->filterWindow, nullptr, x, r.top, newColW, FILTER_HEIGHT, SWP_NOZORDER);
    x += newColW;
  }
}

bool LogContext::FilterColumn(etl::TraceEventDataItem item, const std::wstring &txt)
{
  bool rv = true;
  int column = DataItemToColumn(item);
  if(column < 0)
  {
    return true;
  }
  auto res = columns_[column]->GetFilterText();
  if(ValidRegEx(res))
  {
    std::wregex r(res, std::regex_constants::icase);
    rv = std::regex_search(txt, r);
  }
  return rv;
}

bool LogContext::FilterColumnOnlyMatch(etl::TraceEventDataItem item, const std::wstring &txt) const
{
  bool rv = false;
  int column = DataItemToColumn(item);
  if (column < 0)
  {
    return false;
  }
  auto res = columns_[column]->GetFilterText();
  if (ValidRegEx(res))
  {
    std::wregex r(res, std::regex_constants::icase);
    rv = std::regex_search(txt, r);
  }
  return rv;
}

void LogContext::ResetView()
{
  if(ResetViewNoInvalidate())
  {
    needRedraw_ = true;
    ApplyFilters();
  }
}

bool LogContext::FileOpenDialog(LPCWSTR filter, const std::function<bool(const fs::path &)> &fileHandler, DWORD flags)
{
  std::wstring fileBuf;
  fileBuf.resize(512);
  OPENFILENAMEW ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = mainWindow_;
  ofn.Flags = flags | OFN_ENABLESIZING | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_LONGNAMES;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = &fileBuf[0];
  ofn.nMaxFile = fileBuf.size();
  if(!GetOpenFileNameW(&ofn))
  {
    return false;
  }
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
        if (!fileHandler(dir / name))
        {
          return false;
        }
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
    fileBuf.resize(firstNul);
    return fileHandler(fileBuf);
  }
  return true;
}

bool LogContext::FileSaveDialog(LPCWSTR filter, const std::function<bool(const fs::path &)> &fileHandler, DWORD flags, const std::wstring &suggestedFileName)
{
  std::wstring fileBuf = suggestedFileName;
  fileBuf.resize(512);
  OPENFILENAMEW ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = mainWindow_;
  ofn.Flags = flags | OFN_ENABLESIZING | OFN_EXPLORER | OFN_LONGNAMES;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = &fileBuf[0];
  ofn.nMaxFile = fileBuf.size();
  if (!GetSaveFileNameW(&ofn))
  {
    return false;
  }
  auto firstNul = fileBuf.find_first_of(L'\0');
  if (firstNul < ofn.nFileOffset)
  {
    fs::path dir = &fileBuf[0];
    auto offset = ofn.nFileOffset;
    while (offset < fileBuf.length())
    {
      std::wstring name = &fileBuf[offset];
      if (!name.empty())
      {
        if (!fileHandler(dir / name))
        {
          return false;
        }
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
    fileBuf.resize(firstNul);
    return fileHandler(fileBuf);
  }
  return true;
}

bool LogContext::LoadPdbFromDialog()
{
  if(FileOpenDialog(L"Program Database Files\0*.pdb\0\0",
                    [this](const fs::path &filePath)
                    {
                      fmtDb_.AddProvider(etl::PdbProvider(pdbManager_, filePath));
                      return true;
                    }, OFN_ALLOWMULTISELECT))
  {
    InvalidateView(LVSICF_NOSCROLL);
    return true;
  }
  return false;
}

bool LogContext::ExportFromDialogAll()
{
  size_t count = 0;
  return ExportFromDialog([this, &count](size_t *n)
  {
    if (count < logTrace_->GetItemCount())
    {
      *n = count++;
      return true;
    }
    return false;
  }, true);
}

bool LogContext::ExportFromDialogSelected()
{
  return ExportFromDialog(SelectedLinesEnumerator(), false);
}

template <class StringT>
bool CopyToClipboard(HWND owner, const StringT &txt)
{
  using CharT = typename StringT::value_type;
  if (!OpenClipboard(owner))
  {
    return false;
  }
  EmptyClipboard();
  auto size = txt.length() * sizeof(CharT);
  auto hmem = GlobalAlloc(GMEM_MOVEABLE, size + sizeof(CharT));
  if (!hmem)
  {
    CloseClipboard();
    return false;
  }
  auto dst = GlobalLock(hmem);
  memcpy(dst, txt.c_str(), size);
  ((CharT *)dst)[txt.length()] = 0;
  GlobalUnlock(hmem);
  SetClipboardData(sizeof(CharT) == 1 ? CF_TEXT : CF_UNICODETEXT, hmem);
  CloseClipboard();
  return true;
}

bool LogContext::CopySelected()
{
  std::wstring allLines;
  ExtractTextLines(SelectedLinesEnumerator(),
                   [&allLines](const std::wstring &txt)
                   {
                     allLines += txt;
                     return true;
                   }, false);
  return !allLines.empty() && CopyToClipboard(mainWindow_, allLines);
}

bool LogContext::ExportFromDialog(const std::function<bool (size_t *n)> &enumerator, bool includeHeader)
{
  if (FileSaveDialog(L"Text File (*.txt;*.log)\0*.txt;*.log\0\0",
    [this, enumerator, includeHeader](const fs::path &filePath)
  {
    if (!fs::exists(filePath.parent_path()))
    {
      fs::create_directories(filePath.parent_path());
    }
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;
    auto fh = _wfopen(filePath.c_str(), L"wb");
    if (!fh)
    {
      return false;
    }
    const bool rv = ExtractTextLines(enumerator, [&converter, fh](const std::wstring &txt)
    {
      std::string tmp = converter.to_bytes(txt);
      return fwrite(tmp.c_str(), 1, tmp.length(), fh) == tmp.length();
    }, includeHeader);
    fclose(fh);
    return rv;
  }, 0, sessionName_ + L".log"))
  {
    InvalidateView(LVSICF_NOSCROLL);
    return true;
  }
  return false;
}

bool LogContext::ExtractTextLines(const std::function<bool(size_t *n)> &enumerator,
                                  const std::function<bool(const std::wstring &txt)> &output,
                                  bool includeHeader)
{
  if (includeHeader)
  {
    std::wstring txt;
    for (int c = 0; c < ColumnCount(); ++c)
    {
      if (!txt.empty())
      {
        txt += L'\t';
      }
      txt += columnNames[c];
    }
    etl::TrimR(txt) += L"\r\n";
    output(txt);
  }
  size_t i = std::numeric_limits<size_t>::max();
  while (enumerator(&i))
  {
    std::wstring txt;
    for (int c = 0; c < ColumnCount(); ++c)
    {
      if (!txt.empty())
      {
        txt += L'\t';
      }
      txt += logTrace_->GetItemValue(i, ColumnToDataItem(c));
    }
    etl::TrimR(txt) += L"\r\n";
    if (!output(txt))
    {
      return false;
    }
  }
  return true;
}

bool LogContext::LoadEtlFromDialog()
{
  if(FileOpenDialog(L"Event Trace Log Files\0*.etl;*.log\0\0",
                    [this](const fs::path &filePath)
                    {
                      return LoadEventLogFile(filePath);
                    }))
  {
    InvalidateView(0);
    return true;
  }
  return false;
}

void LogContext::SetMainWindow(HWND hWnd)
{
  mainWindow_ = hWnd;
}

bool LogContext::BeginTrace()
{
  if (!logTrace_)
  {
    return false;
  }
  for (int column = 0; column < ColumnCount(); ++column)
  {
    colorFilters_.push_back(std::make_pair([this, column](int col, const std::wstring &val) -> bool {
      return col == column && !val.empty() ? !FilterColumn(ColumnToDataItem(col), val) : false;
    }, 0.37f));
  }
  logTrace_->SetCountCallback([this](size_t itemCount)
  {
    if (itemCount > 0)
    {
      const auto lineNo = itemCount - 1;
      InitializeRowContext(lineNo);
      size_t totalMaxLength = 0;
      for (int i = 1; i < _countof(columnNames); ++i)
      {
        auto item = ColumnToDataItem(i);
        if (item != etl::TraceEventDataItem::MAX_ITEM)
        {
          auto &&s = logTrace_->GetItemValue(lineNo, item);
          auto &procCol = columns_[i]->columnColor[s];
          columns_[i]->longestTextLength_ = __max(columns_[i]->longestTextLength_, s.length());
          totalMaxLength += columns_[i]->longestTextLength_;
          if (procCol.count++ == 0)
          {
            procCol.index = columns_[i]->columnColor.size();
            procCol.bgColor = 0;
          }
        }
      }
      if (totalMaxLength > 0)
      {
        auto maxL = totalMaxLength;
        for (int i = 0; i < _countof(columnNames); ++i)
        {
          auto l = __max(10, columns_[i]->longestTextLength_);
          l = __min(l, maxL);
          maxL -= l;
          columns_[i]->sizeRatio = static_cast<float>(l) / totalMaxLength;
        }
        RepositionControls();
      }
    }
    DWORD flags = LVSICF_NOINVALIDATEALL;
    if (needRedraw_)
    {
      flags = 0;
      needRedraw_ = false;
    }
    ListView_SetItemCountEx(listView_, itemCount, LVSICF_NOSCROLL | flags);
  });
  SetWindowTextW(mainWindow_, windowTitle_.c_str());
  bool rv = logTrace_->Start();
  if (!comPort_.empty())
  {
    rv = StartCom();
  }
  return rv;
}

void LogContext::SetComPort(const std::wstring &comPort)
{
  comPort_ = comPort;
}

void LogContext::StopCom()
{
  runComThread_ = false;
  if (comThread_ && comThread_->joinable())
  {
    comThread_->join();
  }
}

void LogContext::GotoNextMatch()
{
  GotoMatch(1);
}

void LogContext::GotoPreviousMatch()
{
  GotoMatch(-1);
}

void LogContext::ReloadAllPdbs()
{
  // despite its name AddProvider will not blindly add more and more providers but rather
  // add those that doesn't exist...
  for (auto &&p : pdbPaths_)
  {
    fmtDb_.AddProvider(etl::PdbProvider(pdbManager_, p));
  }
}

void LogContext::GotoMatch(int dir)
{
  auto ri = currentMatchingLine_ + dir;
  size_t testCount = 0;
  bool found = false;
  do
  {
    if (ri >= static_cast<int>(logTrace_->GetItemCount()))
    {
      ri = 0;
    }
    if (ri < 0)
    {
      ri = static_cast<int>(logTrace_->GetItemCount()) - 1;
    }
    for (auto eti = etl::TraceEventDataItem::TraceIndex; eti < etl::TraceEventDataItem::MAX_ITEM; ++eti)
    {
      if (FilterColumnOnlyMatch(eti, logTrace_->GetItemValue(ri, eti)))
      {
        found = true;
        break;
      }
    }
    if (!found)
    {
      ri += dir;
      ++testCount;
    }
  } while (!found && testCount < logTrace_->GetItemCount());
  //
  if (found)
  {
    const auto old = currentMatchingLine_;
    currentMatchingLine_ = ri;
    ListView_EnsureVisible(listView_, currentMatchingLine_, FALSE);
    const auto topIndex = ListView_GetTopIndex(listView_);
    RECT topRect = {0};
    ListView_GetItemRect(listView_, topIndex, &topRect, LVIR_BOUNDS);
    RECT foundRect = {0};
    ListView_GetItemRect(listView_, currentMatchingLine_, &foundRect, LVIR_BOUNDS);
    const int dy = foundRect.top - topRect.top;
    ListView_Scroll(listView_, 0, dy);
    ListView_Update(listView_, currentMatchingLine_);
    ListView_Update(listView_, old);
  }
}

bool LogContext::StartCom()
{
  enum class ComState {Connecting, RequestData, WaitForData, ConsumeData};
  comThread_ = std::make_unique<std::thread>([this]()
  {
    runComThread_ = true;
    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    char tmp[2000];
    DWORD bytesRead = 0;
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;
    ComState state = ComState::Connecting;
    HANDLE h = INVALID_HANDLE_VALUE;
    while (runComThread_)
    {
      if (state == ComState::Connecting)
      {
        if (h != INVALID_HANDLE_VALUE)
        {
          CloseHandle(h);
        }
        h = CreateFileW(comPort_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h != INVALID_HANDLE_VALUE)
        {
          DCB dcb = { 0 };
          if (GetCommState(h, &dcb))
          {
            dcb.BaudRate = CBR_19200;
            if (SetCommState(h, &dcb))
            {
              state = ComState::RequestData;
            }
          }
        }
        if (state == ComState::Connecting)
        {
          Sleep(500);
        }
      }
      if (state == ComState::RequestData)
      {
        if (ReadFile(h, tmp, sizeof(tmp), &bytesRead, &ov))
        {
          state = ComState::ConsumeData;
        }
        else if (GetLastError() == ERROR_IO_PENDING)
        {
          state = ComState::WaitForData;
        }
        else
        {
          std::wstringstream ss;
          ss << L"ReadFile ERROR: " << GetLastError();
          InsertText(ss.str());
          state = ComState::Connecting;
        }
      }
      if (state == ComState::WaitForData)
      {
        auto wait = WaitForSingleObject(ov.hEvent, 500);
        if (wait == 0)
        {
          if (GetOverlappedResult(h, &ov, &bytesRead, FALSE))
          {
            state = ComState::ConsumeData;
          }
          else
          {
            std::wstringstream ss;
            ss << L"GetOverlappedResult ERROR: " << GetLastError();
            InsertText(ss.str());
            state = ComState::Connecting;
          }
        }
      }
      if(state == ComState::ConsumeData)
      {
        std::wstring str = converter.from_bytes(tmp, tmp + bytesRead);
        auto sv = etl::Split(str, L'\n');
        auto timeStamp = etl::GetCurrentLocalFileTime();
        std::map<etl::TraceEventDataItem, std::wstring> txtMap{ {etl::TraceEventDataItem::ModuleName, comPort_.substr(4)} };
        for (auto &&s : sv)
        {
          etl::TrimR(s);
          if (s.length() > 0)
          {
            txtMap[etl::TraceEventDataItem::Message] = s;
            InsertText(timeStamp, txtMap);
          }
        }
        state = ComState::RequestData;
      }
    }
    if (h != INVALID_HANDLE_VALUE)
    {
      CloseHandle(h);
    }
  });
  return true;
}

void LogContext::EndTrace()
{
  runComThread_ = false;
  if (comThread_ && comThread_->joinable())
  {
    comThread_->join();
  }
  if(logTrace_)
  {
    logTrace_->Stop();
  }
}

void LogContext::ActivateColumn(NMLISTVIEW * listViewInfo)
{
  if(!logTrace_)
  {
    return;
  }
  auto &colInfo = columns_[listViewInfo->iSubItem]->columnColor;
  if (selectedColumn_ != listViewInfo->iSubItem)
  {
    for (auto &cc : colInfo)
    {
      if (cc.second.bgColor == 0)
      {
        cc.second.bgColor = GenerateColor(cc.second.index, colInfo.size());
        if (UseWhiteText(cc.second.bgColor))
        {
          cc.second.txtColor = RGB(0xFF, 0xFF, 0xFF);
        }
      }
    }
    selectedColumn_ = listViewInfo->iSubItem;
  }
  else
  {
    selectedColumn_ = -1;
  }
  InvalidateView(LVSICF_NOSCROLL);
}

void LogContext::SetItemText(NMLVDISPINFOW *plvdi)
{
  if(!logTrace_)
  {
    return;
  }
  plvdi->item.pszText = const_cast<LPWSTR>(logTrace_->GetItemValue(plvdi->item.iItem, ColumnToDataItem(plvdi->item.iSubItem), nullptr));
}

bool LogContext::SetItemColorFromColumn(NMLVCUSTOMDRAW *lvd)
{
  if (selectedColumn_ < 0)
  {
    return false;
  }
  ColorInfo ci = { 0 };
  if (selectedColumn_ == 0)
  {
    auto rc = GetRowContext(lvd->nmcd.dwItemSpec);
    if (!rc)
    {
      return false;
    }
    if (rc->groupId <= 0)
    {
      return false;
    }
    ci = columns_[selectedColumn_]->columnColor[std::to_wstring(rc->groupId)];
  }
  else
  {
    auto &&s = logTrace_->GetItemValue(lvd->nmcd.dwItemSpec, ColumnToDataItem(selectedColumn_));
    if (s.empty())
    {
      return false;
    }
    ci = columns_[selectedColumn_]->columnColor[s];
  }
  if (ci.bgColor == 0)
  {
    ci.bgColor = GenerateColor(ci.index, columns_[selectedColumn_]->columnColor.size());
    if (UseWhiteText(ci.bgColor))
    {
      ci.txtColor = RGB(0xFF, 0xFF, 0xFF);
    }
  }
  lvd->clrTextBk = ci.bgColor;
  lvd->clrText = ci.txtColor;
  return true;
}

void LogContext::SetItemColors(NMLVCUSTOMDRAW *lvd)
{
  if(!logTrace_)
  {
    return;
  }
  lvd->clrTextBk = RGB(0xFF, 0xFF, 0xFF);
  lvd->clrText = RGB(0, 0, 0);
  SetItemColorFromColumn(lvd);
  auto colPair = reinterpret_cast<RowContext *>(logTrace_->GetItemMetadata(lvd->nmcd.dwItemSpec));
  if (!colPair)
  {
    InitializeRowContext(lvd->nmcd.dwItemSpec);
    colPair = reinterpret_cast<RowContext *>(logTrace_->GetItemMetadata(lvd->nmcd.dwItemSpec));
  }
  if (currentMatchingLine_ == lvd->nmcd.dwItemSpec)
  {
    //lvd->clrTextBk = RGB(0x77, 0xFF, 0xFF);
    std::swap(lvd->clrText, lvd->clrTextBk);
  }
  if (colPair->lastFilterCount != filterId_)
  {
    colPair->colorFade = GetFilterLineFade(lvd->nmcd.dwItemSpec);
  }
  lvd->clrTextBk = ColorLerp(0x00FFFFFF, lvd->clrTextBk, colPair->colorFade);
  lvd->clrText = ColorLerp(0x00FFFFFF, lvd->clrText, colPair->colorFade);
}

void LogContext::HandleContextMenu()
{
  if(!logTrace_)
  {
    return;
  }
  //
  int selIdx = ListView_GetSelectionMark(listView_);
  if(selIdx < 0)
  {
    return;
  }
  const auto selCount = ListView_GetSelectedCount(listView_);
  ColorInfo *ci = nullptr;
  if (selectedColumn_ == 0 || selCount > 1)
  {
    // create group
    //const auto groupId = ++groupCounter_;
    std::optional<int> groupId;
    auto enumerator = SelectedLinesEnumerator();
    size_t n = std::numeric_limits<size_t>::max();
    // reuse a group id if there is one within the selection
    while (enumerator(&n))
    {
      auto rc = GetRowContext(n);
      if (rc)
      {
        if (rc->groupId > 0)
        {
          groupId = rc->groupId;
          break;
        }
      }
    }
    // create a new otherwise
    if (!groupId)
    {
      groupId = ++groupCounter_;
    }
    n = std::numeric_limits<size_t>::max();
    while (enumerator(&n))
    {
      auto rc = GetRowContext(n);
      if (rc)
      {
        rc->groupId = *groupId;
      }
    }
    ci = &columns_[0]->columnColor[std::to_wstring(*groupId)];
    if (ci->bgColor == 0)
    {
      ci->index = columns_[0]->columnColor.size();
      ci->bgColor = GenerateColor(ci->index, columns_[0]->columnColor.size());
      if (UseWhiteText(ci->bgColor))
      {
        ci->txtColor = RGB(0xFF, 0xFF, 0xFF);
      }
      else
      {
        ci->txtColor = 0;
      }
    }
  }
  else
  {
    if (selectedColumn_ < 0)
    {
      return;
    }
    auto &&s = logTrace_->GetItemValue(selIdx, ColumnToDataItem(selectedColumn_));
    if (s.empty())
    {
      return;
    }
    ci = &columns_[selectedColumn_]->columnColor[s];
  }
  CHOOSECOLORW color;
  ZeroMemory(&color, sizeof(color));
  color.lStructSize = sizeof(color);
  color.hwndOwner = mainWindow_;
  color.lpCustColors = customColors_;
  color.Flags = CC_ANYCOLOR;
  color.rgbResult = ci->bgColor;
  color.Flags |= CC_RGBINIT;
  if(!ChooseColorW(&color))
  {
    return;
  }
  ci->bgColor = color.rgbResult;
  if(UseWhiteText(ci->bgColor))
  {
    ci->txtColor = RGB(0xFF, 0xFF, 0xFF);
  }
  else
  {
    ci->txtColor = 0;
  }
  InvalidateView(LVSICF_NOSCROLL);
}

void LogContext::InvalidateView(DWORD flags)
{
  if(!logTrace_)
  {
    return;
  }
  ListView_SetItemCountEx(listView_, logTrace_->GetItemCount(), flags);
}

int LogContext::ColumnCount() const
{
  return static_cast<int>(columns_.size());
}

bool LogContext::IsFilterMessage(int controlId) const
{
  return controlId >= IDC_FIRSTFILTER && controlId < IDC_FIRSTFILTER + ColumnCount();
}

void LogContext::UpdateFilterText(int controlId)
{
  if (!IsFilterMessage(controlId))
  {
    return;
  }
  wchar_t retxt[100];
  const int column = controlId - IDC_FIRSTFILTER;
  const int l = ComboBox_GetText(columns_[column]->filterWindow, retxt, _countof(retxt));
  if (columns_[column]->SetFilterText(retxt, l))
  {
    needRedraw_ = true;
  }
}

void LogContext::ClearTrace()
{
  logTrace_->RemoveAllItems();
  for (auto &c : columns_)
  {
    c->columnColor.clear();
  }
  ResetViewNoInvalidate();
  InvalidateView(0);
}

void LogContext::InsertText(const std::wstring &t)
{
  std::map<etl::TraceEventDataItem, std::wstring> tm;
  tm[etl::TraceEventDataItem::Message] = t;
  InsertText(etl::GetCurrentLocalFileTime(), tm);
}

void LogContext::InsertText(const FILETIME &ft, const std::map<etl::TraceEventDataItem, std::wstring> &t)
{
  if (!logTrace_)
  {
    return;
  }
  needRedraw_ = true;
  logTrace_->InjectItem(ft, [&t](etl::TraceEventDataItem item)
  {
    auto it = t.find(item);
    return it != t.end() ? it->second : L"";
  });
}

///////

LRESULT CALLBACK ColumnContext::SubClassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  auto self = reinterpret_cast<ColumnContext *>(GetPropW(hwnd, L"this"));
  if (!self)
  {
    return 0;
  }
  switch (msg)
  {
  case WM_KEYDOWN:
    switch (wParam)
    {
    case VK_TAB:
      //SendMessage(hwndMain, WM_TAB, 0, 0);
      return 0;
    case VK_ESCAPE:
      self->owner_->ResetView();
      //SendMessage(hwndMain, WM_ESC, 0, 0);
      return 0;
    case VK_RETURN:
      //SendMessage(hwndMain, WM_ENTER, 0, 0);
      return 0;
    }
    break;
  case WM_COMMAND:
  {
    int wmId = LOWORD(wParam);
    switch (wmId)
    {
    case ID_EDIT_FINDNEXT:
      self->owner_->GotoNextMatch();
      break;
    case ID_EDIT_FINDPREVIOUS:
      self->owner_->GotoPreviousMatch();
      break;
    }
  }
  break;
  case WM_KEYUP:
  case WM_CHAR:
    switch (wParam)
    {
    case VK_TAB:
    case VK_ESCAPE:
    case VK_RETURN:
      return 0;
    }
  }

  //  Call the original window procedure for default processing. 
  return CallWindowProcW(self->orgProc_, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK LogContext::ListViewSubClassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  auto self = reinterpret_cast<LogContext *>(GetPropW(hwnd, L"this"));
  if (!self)
  {
    return 0;
  }
  switch (msg)
  {
  case WM_KEYDOWN:
    switch (wParam)
    {
    case VK_TAB:
      //SendMessage(hwndMain, WM_TAB, 0, 0);
      return 0;
    case VK_ESCAPE:
      self->ResetView();
      //SendMessage(hwndMain, WM_ESC, 0, 0);
      return 0;
    case VK_RETURN:
      //SendMessage(hwndMain, WM_ENTER, 0, 0);
      return 0;
    }
    break;
  case WM_COMMAND:
  {
    int wmId = LOWORD(wParam);
    switch (wmId)
    {
    case ID_EDIT_COPY:
      self->CopySelected();
      break;
    case ID_EDIT_FINDNEXT:
      self->GotoNextMatch();
      break;
    case ID_EDIT_FINDPREVIOUS:
      self->GotoPreviousMatch();
      break;
    }
  }
  break;
  case WM_KEYUP:
  case WM_CHAR:
    switch (wParam)
    {
    case VK_TAB:
    case VK_ESCAPE:
    case VK_RETURN:
      return 0;
    }
  }

  //  Call the original window procedure for default processing. 
  return CallWindowProcW(self->orgListViewProc_, hwnd, msg, wParam, lParam);
}

