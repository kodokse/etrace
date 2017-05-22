#pragma once

struct ColorInfo
{
  int count;
  int index;
  COLORREF bgColor;
  COLORREF txtColor;
};

class ColumnContext
{
public:
  ColumnContext(HWND fWnd = nullptr, float r = 0.0f);
  void SetFilterText(const wchar_t *txt, int len);
  std::wstring GetFilterText() const;
public:
  HWND filterWindow;
  float sizeRatio;
  std::map<std::wstring, ColorInfo> columnColor;
private:
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
  void InvalidateView(DWORD flags);
  int ColumnCount() const;
  bool IsFilterMessage(int controlId) const;
  void UpdateFilterText(int controlId);
private:
  bool FileOpenDialog(LPCWSTR filter, const std::function<void(const fs::path &)> &fileHandler, DWORD flags = 0);
private:
  HINSTANCE programInstance_;
  HWND mainWindow_;
  HWND listView_;
  DWORD itemCounter_;
  int selectedColumn_;
  etl::FormatDatabase fmtDb_;
  std::unique_ptr<etl::TraceEnumerator> logTrace_;
  etl::PdbFileManager pdbManager_;
  std::vector<std::unique_ptr<ColumnContext>> columns_;
  COLORREF customColors_[16];
  std::wstring windowTitle_;
};

