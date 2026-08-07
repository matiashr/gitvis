#include "MainFrame.h"

#include "GitRepository.h"
#include "GraphCanvas.h"
#include "Layout.h"

#include <wx/artprov.h>
#include <wx/dirdlg.h>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statusbr.h>
#include <wx/textctrl.h>
#include <wx/toolbar.h>
#include <wx/utils.h>

MainFrame::MainFrame(const wxString& title, const wxSize& size)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, size) {
    wxMenu* fileMenu = new wxMenu;
    fileMenu->Append(wxID_OPEN, wxT("&Open Repository...\tCtrl-O"));
    fileMenu->Append(ID_REFRESH, wxT("&Refresh\tF5"));
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, wxT("E&xit\tCtrl-Q"));

    wxMenu* viewMenu = new wxMenu;
    viewMenu->Append(ID_FIT, wxT("&Fit to Window\tCtrl-0"));

    wxMenuBar* menubar = new wxMenuBar;
    menubar->Append(fileMenu, wxT("&File"));
    menubar->Append(viewMenu, wxT("&View"));
    SetMenuBar(menubar);

    m_toolbar = CreateToolBar();
    m_toolbar->AddTool(wxID_OPEN, wxT("Open"),
                       wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_TOOLBAR),
                       wxT("Open repository"));
    m_toolbar->AddTool(ID_REFRESH, wxT("Refresh"),
                       wxArtProvider::GetBitmap(wxART_REDO, wxART_TOOLBAR),
                       wxT("Reload repository"));
    m_toolbar->Realize();

    m_splitter = new wxSplitterWindow(this, wxID_ANY);
    m_canvas = new GraphCanvas(m_splitter, wxID_ANY);

    wxPanel* rightPanel = new wxPanel(m_splitter, wxID_ANY);
    const wxFont monoFont(wxSize(10, 10), wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);

    m_details = new wxTextCtrl(rightPanel, wxID_ANY, wxEmptyString,
                               wxDefaultPosition, wxDefaultSize,
                               wxTE_MULTILINE | wxTE_READONLY);
    m_details->SetFont(monoFont);
    m_details->SetMinSize(wxSize(-1, 140));

    m_rightSplitter = new wxSplitterWindow(rightPanel, wxID_ANY);
    m_fileList = new wxListBox(m_rightSplitter, wxID_ANY);
    m_diffView = new wxTextCtrl(m_rightSplitter, wxID_ANY, wxEmptyString,
                                wxDefaultPosition, wxDefaultSize,
                                wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    m_diffView->SetFont(monoFont);
    m_rightSplitter->SplitVertically(m_fileList, m_diffView, 220);
    m_rightSplitter->SetSashGravity(0.3);
    m_rightSplitter->SetMinimumPaneSize(100);

    wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
    rightSizer->Add(m_details, 0, wxEXPAND);
    rightSizer->Add(m_rightSplitter, 1, wxEXPAND);
    rightPanel->SetSizer(rightSizer);

    m_splitter->SplitVertically(m_canvas, rightPanel, 820);
    m_splitter->SetSashGravity(0.75);
    m_splitter->SetMinimumPaneSize(150);

    m_status = CreateStatusBar(2);
    m_status->SetStatusText(wxT("Ready"));

    m_canvas->Bind(wxEVT_COMMIT_SELECTED, &MainFrame::OnCommitSelected, this);
    m_canvas->Bind(wxEVT_DIFF_REQUESTED, &MainFrame::OnDiffRequested, this);
    m_fileList->Bind(wxEVT_LISTBOX, &MainFrame::OnFileSelected, this);
    Bind(wxEVT_MENU, &MainFrame::OnOpenRepo, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnRefresh, this, ID_REFRESH);
    Bind(wxEVT_MENU, &MainFrame::OnFit, this, ID_FIT);
    Bind(wxEVT_MENU, &MainFrame::OnQuit, this, wxID_EXIT);

    wxString cwd = wxGetCwd();
    if (GitRepository::inWorkTree(cwd)) {
        OpenPath(cwd);
    } else {
        m_details->SetValue(wxT("The current directory is not a git work tree.\n\n"
                                "Use File > Open Repository or the Open button to choose a repository."));
        m_status->SetStatusText(wxT("No repository open"));
    }
}

void MainFrame::OnOpenRepo(wxCommandEvent&) {
    wxDirDialog dlg(this, wxT("Choose a git repository"),
                    m_repoPath.IsEmpty() ? wxGetCwd() : m_repoPath,
                    wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        OpenPath(dlg.GetPath());
    }
}

void MainFrame::OnRefresh(wxCommandEvent&) {
    if (m_repoPath.IsEmpty()) {
        wxMessageBox(wxT("No repository open yet."), wxT("gitvis"), wxOK | wxICON_INFORMATION, this);
        return;
    }
    OpenPath(m_repoPath);
}

void MainFrame::OnFit(wxCommandEvent&) {
    m_canvas->ResetView();
}

void MainFrame::OnQuit(wxCommandEvent&) {
    Close(true);
}

void MainFrame::OpenPath(const wxString& path) {
    wxString err;
    if (!GitRepository::load(path, m_commits, m_refs, err)) {
        wxMessageBox(wxT("Failed to read repository:\n") + err, wxT("gitvis"), wxOK | wxICON_ERROR, this);
        return;
    }
    m_repoPath = path;
    BuildGraph();

    m_currentOid.clear();
    m_currentParents.clear();
    m_currentFiles.clear();
    m_compareMode = false;
    m_compareBase.clear();
    m_compareTarget.clear();
    m_fileList->Clear();
    m_diffView->Clear();
    m_details->SetValue(wxT("No commit selected.\n\nClick a node to inspect it."));

    m_status->SetStatusText(wxT("Repository: ") + path);
    m_status->SetStatusText(wxString::Format(wxT("%ld commits, %ld refs"),
                                             (long)m_commits.size(), (long)m_refs.size()),
                            1);
}

void MainFrame::BuildGraph() {
    assignLanes(m_commits);
    m_edges = buildEdges(m_commits);

    long long minT = 0, maxT = 0;
    for (size_t i = 0; i < m_commits.size(); ++i) {
        if (i == 0) {
            minT = maxT = m_commits[i].time;
            continue;
        }
        if (m_commits[i].time < minT) {
            minT = m_commits[i].time;
        }
        if (m_commits[i].time > maxT) {
            maxT = m_commits[i].time;
        }
    }
    if (maxT == minT) {
        maxT = minT + 1;
    }

    m_canvas->SetData(m_commits, m_edges, m_refs, minT, maxT);
}

const Commit* MainFrame::FindCommit(const wxString& oid) const {
    for (const auto& cm : m_commits) {
        if (cm.oid == oid) {
            return &cm;
        }
    }
    return nullptr;
}

void MainFrame::OnCommitSelected(wxCommandEvent& evt) {
    const wxString oid = evt.GetString();
    m_currentOid.clear();
    m_currentParents.clear();
    m_currentFiles.clear();
    m_compareMode = false;
    m_fileList->Clear();
    m_diffView->Clear();

    if (oid.IsEmpty()) {
        m_details->SetValue(wxT("No commit selected.\n\nClick a node to inspect it."));
        return;
    }

    const Commit* c = FindCommit(oid);
    if (!c) {
        return;
    }

    wxString txt;
    txt << wxT("OID      ") << c->oid << wxT("\n");
    txt << wxT("Subject  ") << c->subject << wxT("\n");
    txt << wxT("Author   ") << c->author << wxT(" <") << c->email << wxT(">\n");
    txt << wxT("Date     ") << formatTime(c->time) << wxT("\n");
    txt << wxT("Parents  ");
    for (const auto& p : c->parents) {
        txt << p.Left(7) << wxT(" ");
    }
    txt << wxT("\n");

    txt << wxT("Refs     ");
    bool first = true;
    bool any = false;
    for (const auto& r : m_refs) {
        if (r.oid == oid) {
            if (!first) {
                txt << wxT(", ");
            }
            txt << r.name;
            first = false;
            any = true;
        }
    }
    if (!any) {
        txt << wxT("(none)");
    }
    txt << wxT("\n");

    m_details->SetValue(txt);

    m_currentOid = oid;
    m_currentParents = c->parents;

    wxString err;
    if (!GitRepository::changedFiles(m_repoPath, oid, c->parents, m_currentFiles, err)) {
        m_diffView->SetValue(wxT("Failed to list changed files:\n") + err);
        return;
    }

    for (const auto& fc : m_currentFiles) {
        wxString label = fc.status.Left(1) + wxT("  ");
        if (!fc.oldPath.IsEmpty()) {
            label << fc.oldPath << wxT(" -> ") << fc.path;
        } else {
            label << fc.path;
        }
        m_fileList->Append(label);
    }
}

void MainFrame::OnFileSelected(wxCommandEvent& evt) {
    const int sel = evt.GetSelection();
    if (sel < 0 || (size_t)sel >= m_currentFiles.size()) {
        return;
    }
    if (!m_compareMode && m_currentOid.IsEmpty()) {
        return;
    }

    const FileChange& fc = m_currentFiles[sel];
    wxString diff, err;
    bool ok;
    if (m_compareMode) {
        ok = GitRepository::fileDiffBetween(m_repoPath, m_compareBase, m_compareTarget, fc.path, diff, err);
    } else {
        ok = GitRepository::fileDiff(m_repoPath, m_currentOid, m_currentParents, fc.path, diff, err);
    }
    if (!ok) {
        m_diffView->SetValue(wxT("Failed to load diff:\n") + err);
        return;
    }
    m_diffView->SetValue(diff);
}

void MainFrame::OnDiffRequested(wxCommandEvent& evt) {
    const wxString s = evt.GetString();
    const int sep = s.Find(wxT('|'));
    if (sep == wxNOT_FOUND) {
        return;
    }
    const wxString oidA = s.Left(sep);
    const wxString oidB = s.Mid(sep + 1);

    m_currentOid.clear();
    m_currentParents.clear();
    m_currentFiles.clear();
    m_fileList->Clear();
    m_diffView->Clear();
    m_compareMode = true;
    m_compareBase = oidA;
    m_compareTarget = oidB;

    const Commit* a = FindCommit(oidA);
    const Commit* b = FindCommit(oidB);

    wxString txt;
    txt << wxT("Diff\n");
    txt << wxT("A  ") << oidA.Left(10);
    if (a) {
        txt << wxT("  ") << a->subject;
    }
    txt << wxT("\n");
    txt << wxT("B  ") << oidB.Left(10);
    if (b) {
        txt << wxT("  ") << b->subject;
    }
    txt << wxT("\n");
    m_details->SetValue(txt);

    wxString err;
    if (!GitRepository::changedFilesBetween(m_repoPath, oidA, oidB, m_currentFiles, err)) {
        m_diffView->SetValue(wxT("Failed to list changed files:\n") + err);
        return;
    }

    for (const auto& fc : m_currentFiles) {
        wxString label = fc.status.Left(1) + wxT("  ");
        if (!fc.oldPath.IsEmpty()) {
            label << fc.oldPath << wxT(" -> ") << fc.path;
        } else {
            label << fc.path;
        }
        m_fileList->Append(label);
    }
}
