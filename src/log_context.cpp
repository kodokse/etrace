#include "stdafx.h"
#include "log_context.h"
#include "win_util.h"

#define IDC_TRACELIST 201
#define IDC_FIRSTFILTER 202
#define FILTER_HEIGHT 25

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

ColumnContext::ColumnContext(HWND fWnd, float ratio)
  : filterWindow(fWnd)
  , sizeRatio(ratio)
{
}

void ColumnContext::SetFilterText(const wchar_t *txt, int len)
{
  std::lock_guard<std::mutex> l(filterTextLock_);
  filterText_.assign(txt, len);
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
{
}

void LogContext::ApplyFilters()
{
  if(logTrace_)
  {
    logTrace_->ApplyFilters();
  }
}

bool LogContext::AddPdbProvider(const fs::path &pdbPath)
{
  fmtDb_.AddProvider(etl::PdbProvider(pdbManager_, pdbPath));
  return true;
}

bool LogContext::InitializeLiveSession(const std::wstring &sessionName)
{
  logTrace_ = std::make_unique<etl::LiveTraceEnumerator>(fmtDb_, sessionName);
  windowTitle_ = sessionName + L" - LIVE ETRACE";
  return true;
}

bool LogContext::LoadEventLogFile(const fs::path &etlPath)
{
  logTrace_ = std::make_unique<etl::LogfileEnumerator>(fmtDb_, etlPath);
  windowTitle_ = etlPath.filename().wstring() + L" - ETRACE LOG";
  return true;
}

bool LogContext::InitTraceList()
{
  RECT r;
  GetClientRect(mainWindow_, &r);
  HWND hWnd = CreateWindowExW(0, WC_LISTVIEW, L"", WS_BORDER | WS_VISIBLE | WS_TABSTOP | WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA,
                            r.left, r.top + FILTER_HEIGHT, Width(r), Height(r) - FILTER_HEIGHT, mainWindow_, reinterpret_cast<HMENU>(IDC_TRACELIST), programInstance_, nullptr);

  if(!hWnd)
  {
    return false;
  }

  listView_ = hWnd;

  ListView_SetExtendedListViewStyleEx(hWnd, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);

  const int colCount = _countof(columnNames);

  const int colWidth = Width(r) / colCount;
  const float ratio = static_cast<float>(colWidth) / Width(r);

  int x = r.left;
  for(int i = 0; i < colCount; ++i)
  {
    HWND cbWnd = CreateWindowExW(0, WC_COMBOBOX, L"", WS_BORDER | WS_VISIBLE | WS_TABSTOP | WS_CHILD | CBS_DROPDOWN | CBS_HASSTRINGS,
                            x, r.top, colWidth, FILTER_HEIGHT, mainWindow_, reinterpret_cast<HMENU>(IDC_FIRSTFILTER + i), programInstance_, nullptr);
    x += colWidth;
    //
    columns_.push_back(std::make_unique<ColumnContext>(cbWnd, ratio));
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
  for(size_t i = 0; i < columns_.size(); ++i)
  {
    auto colW = i == hdr->iItem && hdr->pitem ? hdr->pitem->cxy : ListView_GetColumnWidth(listView_, i);
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
  wchar_t retxt[100];
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

void LogContext::ResetView()
{
  bool invalidate = selectedColumn_ >= 0;
  selectedColumn_ = -1;
  for(auto &c : columns_)
  {
    if(GetWindowTextLengthW(c->filterWindow) > 0)
    {
      invalidate = true;
    }
    SetWindowTextW(c->filterWindow, L"");
  }
  if(invalidate)
  {
    ApplyFilters();
  }
}

bool LogContext::FileOpenDialog(LPCWSTR filter, const std::function<void(const fs::path &)> &fileHandler, DWORD flags)
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
        fileHandler(dir / name);
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
    fileHandler(fileBuf);
  }
  return true;
}

bool LogContext::LoadPdbFromDialog()
{
  if(FileOpenDialog(L"Program Database Files\0*.pdb\0\0",
                    [this](const fs::path &filePath)
                    {
                      fmtDb_.AddProvider(etl::PdbProvider(pdbManager_, filePath));
                    }, OFN_ALLOWMULTISELECT))
  {
    InvalidateView(LVSICF_NOSCROLL);
    return true;
  }
  return false;
}

bool LogContext::LoadEtlFromDialog()
{
  if(FileOpenDialog(L"Event Trace Log Files\0*.etl\0\0",
                    [this](const fs::path &filePath)
                    {
                      LoadEventLogFile(filePath);
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
  if(!logTrace_)
  {
    return false;
  }
  for(etl::TraceEventDataItem colItem = etl::TraceEventDataItem::TraceIndex; colItem < etl::TraceEventDataItem::MAX_ITEM; ++colItem)
  {
    logTrace_->AddFilter([this, colItem](etl::TraceEventDataItem item, const std::wstring &txt)
    {
      return item == colItem ? FilterColumn(item, txt) : true;
    });
  }
  logTrace_->SetCountCallback([this](size_t itemCount)
  {
    if(itemCount > 0)
    {
      for(int i = 0; i < _countof(columnNames); ++i)
      {
        auto item = ColumnToDataItem(i);
        if(item != etl::TraceEventDataItem::MAX_ITEM)
        {
          auto &&s = logTrace_->GetItemValue(itemCount - 1, item);
          auto &procCol = columns_[i]->columnColor[s];
          if(procCol.count++ == 0)
          {
            procCol.index = columns_[i]->columnColor.size();
            procCol.bgColor = 0;
          }
        }
      }
    }
    ListView_SetItemCountEx(listView_, itemCount, LVSICF_NOINVALIDATEALL);
  });
  SetWindowTextW(mainWindow_, windowTitle_.c_str());
  return logTrace_->Start();
}

void LogContext::EndTrace()
{
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
  bool redraw = selectedColumn_ != listViewInfo->iSubItem;
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
  selectedColumn_ = listViewInfo->iSubItem;
  if(redraw)
  {
    InvalidateView(LVSICF_NOSCROLL);
  }
}

void LogContext::SetItemText(NMLVDISPINFOW *plvdi)
{
  if(!logTrace_)
  {
    return;
  }
  plvdi->item.pszText = const_cast<LPWSTR>(logTrace_->GetItemValue(plvdi->item.iItem, ColumnToDataItem(plvdi->item.iSubItem), nullptr));
}

void LogContext::SetItemColors(NMLVCUSTOMDRAW * lvd)
{
  if(selectedColumn_ < 0)
  {
    return;
  }
  if(!logTrace_)
  {
    return;
  }
  auto &&s = logTrace_->GetItemValue(lvd->nmcd.dwItemSpec, ColumnToDataItem(selectedColumn_));
  if(s.empty())
  {
    return;
  }
  auto &ci = columns_[selectedColumn_]->columnColor[s];
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
}

void LogContext::HandleContextMenu()
{
  if(!logTrace_)
  {
    return;
  }
  int selIdx = ListView_GetSelectionMark(listView_);
  if(selIdx < 0)
  {
    return;
  }
  auto &&s = logTrace_->GetItemValue(selIdx, ColumnToDataItem(selectedColumn_));
  if(s.empty())
  {
    return;
  }
  auto &ci = columns_[selectedColumn_]->columnColor[s];
  CHOOSECOLORW color;
  ZeroMemory(&color, sizeof(color));
  color.lStructSize = sizeof(color);
  color.hwndOwner = mainWindow_;
  color.lpCustColors = customColors_;
  color.Flags = CC_ANYCOLOR;
  color.rgbResult = ci.bgColor;
  color.Flags |= CC_RGBINIT;
  if(!ChooseColorW(&color))
  {
    return;
  }
  ci.bgColor = color.rgbResult;
  if(UseWhiteText(ci.bgColor))
  {
    ci.txtColor = RGB(0xFF, 0xFF, 0xFF);
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
  columns_[column]->SetFilterText(retxt, l);
}
