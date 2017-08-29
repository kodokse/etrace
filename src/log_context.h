#pragma once

struct ColorInfo
{
  int count;
  int index;
  COLORREF bgColor;
  COLORREF txtColor;
};

using ColorFadeFilterList = std::list<std::pair<std::function<bool(int column, const std::wstring &value)>, float>>;

class LogContext;

struct RowContext
{
  bool selected;
  float colorFade;
  int lastFilterCount;
  int groupId;
  RowContext();
};

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
  bool ExportFromDialogAll();
  bool ExportFromDialogSelected();
  bool CopySelected();
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
  void SetComPort(const std::wstring &comPort);
  bool StartCom();
  void StopCom();
  //
private:
  bool ExportFromDialog(const std::function<bool(size_t *n)> &enumerator, bool includeHeader);
  bool ExtractTextLines(const std::function<bool(size_t *n)> &enumerator, const std::function<bool(const std::wstring &txt)> &output, bool includeHeader);
  bool FileOpenDialog(LPCWSTR filter, const std::function<bool(const fs::path &)> &fileHandler, DWORD flags = 0);
  bool FileSaveDialog(LPCWSTR filter, const std::function<bool(const fs::path &)> &fileHandler, DWORD flags = 0, const std::wstring &suggestedFileName = L"");
  bool SetItemColorFromColumn(NMLVCUSTOMDRAW *lvd);
  float GetFilterLineFade(size_t line);
  bool ResetViewNoInvalidate();
  void InitializeRowContext(size_t line);
  RowContext *GetRowContext(size_t line);
  const RowContext *GetRowContext(size_t line) const;
  std::function<bool(size_t *n)> SelectedLinesEnumerator() const;
  static LRESULT CALLBACK ListViewSubClassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  void InsertText(const std::wstring &t);
  void InsertText(const std::map<etl::TraceEventDataItem, std::wstring> &t);
private:
  HINSTANCE programInstance_;
  WNDPROC orgListViewProc_;
  HWND mainWindow_;
  HWND listView_;
  DWORD itemCounter_;
  int selectedColumn_;
  bool needRedraw_;
  etl::FormatDatabase fmtDb_;
  std::unique_ptr<etl::TraceEnumerator> logTrace_;
  etl::PdbFileManager pdbManager_;
  std::vector<std::unique_ptr<ColumnContext>> columns_;
  std::vector<std::unique_ptr<RowContext>> rowInfo_;
  ColorFadeFilterList colorFilters_;
  COLORREF customColors_[16];
  std::wstring windowTitle_;
  std::wstring sessionName_;
  int filterId_;
  std::mutex rowInfoLock_;
  int groupCounter_;
  std::wstring comPort_;
  std::unique_ptr<std::thread> comThread_;
  bool runComThread_;
};

