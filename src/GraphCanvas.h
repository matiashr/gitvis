#pragma once

#include "GitData.h"

#include <wx/panel.h>

wxDECLARE_EVENT(wxEVT_COMMIT_SELECTED, wxCommandEvent);

class GraphCanvas : public wxPanel {
public:
    GraphCanvas(wxWindow* parent, wxWindowID id);

    void SetData(const std::vector<Commit>& commits, const std::vector<Edge>& edges,
                 const std::vector<GitRef>& refs, long long minTime, long long maxTime);
    void ResetView();
    bool SaveImage(const wxString& path, int width, int height);
    wxString SelectedCommit() const { return m_selectedOid; }

private:
    void OnPaint(wxPaintEvent&);
    void OnSize(wxSizeEvent&);
    void OnMouseWheel(wxMouseEvent&);
    void OnMouseMove(wxMouseEvent&);
    void OnMouseDown(wxMouseEvent&);
    void OnMouseUp(wxMouseEvent&);
    void OnMouseLeave(wxMouseEvent&);

    void DrawGraph(wxDC& dc, const wxSize& size);
    void DrawTimeGrid(wxDC& dc, const wxSize& size);
    void FitView(const wxSize& size);
    wxPoint W2S(double x, double y) const;
    const Commit* HitTest(const wxPoint& sp) const;
    const Commit* FindCommit(const wxString& oid) const;

    std::vector<Commit> m_commits;
    std::vector<Edge> m_edges;
    std::vector<GitRef> m_refs;
    long long m_minTime = 0;
    long long m_maxTime = 0;

    double m_scale = 1.0;
    double m_offX = 0.0;
    double m_offY = 0.0;
    bool m_fitPending = true;
    bool m_dragging = false;
    bool m_panned = false;
    wxPoint m_lastMouse;
    wxString m_selectedOid;
};
