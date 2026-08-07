#pragma once

#include "GitData.h"

#include <wx/frame.h>

class GraphCanvas;
class wxListBox;
class wxNotebook;
class wxSplitterWindow;
class wxStatusBar;
class wxStyledTextCtrl;
class wxTextCtrl;
class wxToolBar;

class MainFrame : public wxFrame {
public:
    MainFrame(const wxString& title, const wxSize& size);

private:
    void OnOpenRepo(wxCommandEvent&);
    void OnRefresh(wxCommandEvent&);
    void OnFit(wxCommandEvent&);
    void OnCommitSelected(wxCommandEvent&);
    void OnFileSelected(wxCommandEvent&);
    void OnDiffRequested(wxCommandEvent&);
    void OnTreeFileRightClick(wxMouseEvent&);
    void OnExportFile(wxCommandEvent&);
    void OnQuit(wxCommandEvent&);

    void OpenPath(const wxString& path);
    void BuildGraph();
    void SetDiffText(const wxString& diff);
    const Commit* FindCommit(const wxString& oid) const;

    enum {
        ID_REFRESH = wxID_HIGHEST + 1,
        ID_FIT,
        ID_EXPORT_FILE,
    };

    wxToolBar* m_toolbar = nullptr;
    wxSplitterWindow* m_splitter = nullptr;
    wxSplitterWindow* m_rightSplitter = nullptr;
    GraphCanvas* m_canvas = nullptr;
    wxTextCtrl* m_details = nullptr;
    wxNotebook* m_fileNotebook = nullptr;
    wxListBox* m_fileList = nullptr;
    wxListBox* m_treeFileList = nullptr;
    wxStyledTextCtrl* m_diffView = nullptr;
    wxStatusBar* m_status = nullptr;

    wxString m_repoPath;
    std::vector<Commit> m_commits;
    std::vector<GitRef> m_refs;
    std::vector<Edge> m_edges;

    wxString m_currentOid;
    std::vector<wxString> m_currentParents;
    std::vector<FileChange> m_currentFiles;
    std::vector<wxString> m_currentTreeFiles;

    bool m_compareMode = false;
    wxString m_compareBase;
    wxString m_compareTarget;
};
