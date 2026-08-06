#include "MainFrame.h"

#include "GitRepository.h"
#include "GraphCanvas.h"
#include "Layout.h"

#include <wx/artprov.h>
#include <wx/dirdlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
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
    m_details = new wxTextCtrl(m_splitter, wxID_ANY, wxEmptyString,
                               wxDefaultPosition, wxDefaultSize,
                               wxTE_MULTILINE | wxTE_READONLY);
    m_details->SetFont(wxFont(wxSize(10, 10), wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    m_splitter->SplitVertically(m_canvas, m_details, 820);
    m_splitter->SetSashGravity(0.75);
    m_splitter->SetMinimumPaneSize(150);

    m_status = CreateStatusBar(2);
    m_status->SetStatusText(wxT("Ready"));

    m_canvas->Bind(wxEVT_COMMIT_SELECTED, &MainFrame::OnCommitSelected, this);
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

void MainFrame::OnCommitSelected(wxCommandEvent& evt) {
    const wxString oid = evt.GetString();
    if (oid.IsEmpty()) {
        m_details->SetValue(wxT("No commit selected.\n\nClick a node to inspect it."));
        return;
    }

    const Commit* c = nullptr;
    for (const auto& cm : m_commits) {
        if (cm.oid == oid) {
            c = &cm;
            break;
        }
    }
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
}
