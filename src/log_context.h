#pragma once

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
  void InvalidateView();
  int ColumnCount() const;
  bool IsFilterMessage(int controlId) const;
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
  std::vector<ColumnContext> columns_;
  COLORREF customColors_[16];
};

