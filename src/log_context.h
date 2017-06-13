#pragma once

struct ColorInfo
{
  int count;
  int index;
  COLORREF bgColor;
  COLORREF txtColor;
};

using FilterList = std::list<std::pair<std::function<bool(int column, const std::wstring &value)>, ColorInfo>>;

class LogContext;

class ColumnContext
{
public:
  ColumnContext(HWND fWnd = nullptr, float r = 0.0f, LogContext *owner = nullptr);
  bool SetFilterText(const wchar_t *txt, int len);
  std::wstring GetFilterText() const;
public:
  HWND filterWindow;
  float sizeRatio;
  std::map<std::wstring, ColorInfo> columnColor;
  size_t longestTextLength_;
  WNDPROC orgProc_;
  //
  static LRESULT CALLBACK SubClassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
private:
  LogContext *owner_;
  std::wstring filterText_;
  mutable std::mutex filterTextLock_;
};

class LogContext
{
public:
  LogContext(HINSTANCE programInstance);
  void ApplyFilters();
  bool AddPdbProvider(const fs::path &pdbPath);
  bool InitializeLiveSession(const std::wstring &sessionName);
  bool LoadEventLogFile(const fs::path &etlPath);
  bool InitTraceList();
  void RecalculateRatios(NMHEADER *hdr);
  void RepositionControls();
  bool FilterColumn(etl::TraceEventDataItem item, const std::wstring &txt);
  void ResetView();
  bool LoadPdbFromDialog();
  bool LoadEtlFromDialog();
  void SetMainWindow(HWND hWnd);
  bool BeginTrace();
  void EndTrace();
  void ActivateColumn(NMLISTVIEW *listViewInfo);
  void SetItemText(NMLVDISPINFOW *plvdi);
  void SetItemColors(NMLVCUSTOMDRAW *lvd);
  void HandleContextMenu();
  void InvalidateView(DWORD flags = 0);
  int ColumnCount() const;
  bool IsFilterMessage(int controlId) const;
  void UpdateFilterText(int controlId);
  void ClearTrace();
private:
  bool FileOpenDialog(LPCWSTR filter, const std::function<void(const fs::path &)> &fileHandler, DWORD flags = 0);
  bool SetItemColorFromColumn(NMLVCUSTOMDRAW *lvd);
  bool ColorFilterLine(size_t line);
  bool ResetViewNoInvalidate();
private:
  HINSTANCE programInstance_;
  HWND mainWindow_;
  HWND listView_;
  DWORD itemCounter_;
  int selectedColumn_;
  bool needRedraw_;
  etl::FormatDatabase fmtDb_;
  std::unique_ptr<etl::TraceEnumerator> logTrace_;
  etl::PdbFileManager pdbManager_;
  std::vector<std::unique_ptr<ColumnContext>> columns_;
  std::vector<std::unique_ptr<std::pair<COLORREF, COLORREF>>> filterRowColors_;
  FilterList colorFilters_;
  COLORREF customColors_[16];
  std::wstring windowTitle_;
};

