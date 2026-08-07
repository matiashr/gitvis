#include "GraphCanvas.h"

#include <wx/cursor.h>
#include <wx/datetime.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/font.h>
#include <wx/image.h>
#include <wx/menu.h>
#include <wx/tooltip.h>

#include <algorithm>
#include <cmath>
#include <map>

wxDEFINE_EVENT(wxEVT_COMMIT_SELECTED, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_DIFF_REQUESTED, wxCommandEvent);

namespace {

const double LANE_SPACING = 42.0;
const double TIME_SCALE = 0.02;
const double RADIUS = 5.0;

// Caps how much vertical space a single gap between chronologically adjacent
// commits can consume, so a few long idle stretches don't squeeze the rest
// of the history into an unreadable sliver at the default (fit-to-window) zoom.
const double GAP_CAP_SECONDS = 86400.0;

const wxColour BG(0x16, 0x1b, 0x22);
const wxColour GRID(0x25, 0x2c, 0x36);
const wxColour TXT_DIM(0x9a, 0xa3, 0xad);
const wxColour SEL(0xf5, 0xc2, 0x11);
const wxColour COMPARE(0xff, 0x6a, 0x00);

const wxColour PALETTE[] = {
    wxColour(0x56, 0x8a, 0xf0), wxColour(0x9a, 0xed, 0x70), wxColour(0xff, 0x85, 0x7f),
    wxColour(0xf5, 0xa6, 0x23), wxColour(0xb0, 0x6a, 0xf5), wxColour(0xef, 0xd9, 0x6b),
    wxColour(0x69, 0xc8, 0xe6), wxColour(0xff, 0x96, 0xc6), wxColour(0x8f, 0xe6, 0xd8),
    wxColour(0x7d, 0xd9, 0x5f), wxColour(0xcd, 0x9b, 0x1d), wxColour(0xc0, 0xc0, 0xc0),
};
const int PALETTE_N = (int)(sizeof(PALETTE) / sizeof(PALETTE[0]));

wxColour LaneColour(int lane) {
    return PALETTE[lane % PALETTE_N];
}

wxColour RefColour(GitRef::Kind kind) {
    switch (kind) {
        case GitRef::Branch:
            return wxColour(0x2d, 0xa4, 0x4e);
        case GitRef::RemoteBranch:
            return wxColour(0x57, 0x60, 0x6a);
        case GitRef::Tag:
            return wxColour(0xbf, 0x87, 0x00);
        case GitRef::Head:
            return wxColour(0x89, 0x57, 0xe5);
    }
    return wxColour(0x57, 0x60, 0x6a);
}

}  // namespace

GraphCanvas::GraphCanvas(wxWindow* parent, wxWindowID id)
    : wxPanel(parent, id) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetCursor(wxCursor(wxCURSOR_HAND));
    Bind(wxEVT_PAINT, &GraphCanvas::OnPaint, this);
    Bind(wxEVT_SIZE, &GraphCanvas::OnSize, this);
    Bind(wxEVT_MOUSEWHEEL, &GraphCanvas::OnMouseWheel, this);
    Bind(wxEVT_MOTION, &GraphCanvas::OnMouseMove, this);
    Bind(wxEVT_LEFT_DOWN, &GraphCanvas::OnMouseDown, this);
    Bind(wxEVT_LEFT_UP, &GraphCanvas::OnMouseUp, this);
    Bind(wxEVT_LEAVE_WINDOW, &GraphCanvas::OnMouseLeave, this);
    Bind(wxEVT_RIGHT_DOWN, &GraphCanvas::OnMouseRightDown, this);
    Bind(wxEVT_MENU, &GraphCanvas::OnDiffMenuClick, this, ID_DIFF_MENU);
}

void GraphCanvas::SetData(const std::vector<Commit>& commits, const std::vector<Edge>& edges,
                          const std::vector<GitRef>& refs, long long minTime, long long maxTime) {
    m_commits = commits;
    m_edges = edges;
    m_refs = refs;
    m_minTime = minTime;
    m_maxTime = maxTime;
    m_selectedOid.clear();
    m_compareOids.clear();
    if (m_maxTime == m_minTime) {
        m_maxTime = m_minTime + 1;
    }
    BuildTimeAxis();
    m_fitPending = false;
    FitView(GetClientSize());
    Refresh();
}

void GraphCanvas::BuildTimeAxis() {
    std::vector<long long> times;
    times.reserve(m_commits.size() + 2);
    times.push_back(m_minTime);
    times.push_back(m_maxTime);
    for (const auto& c : m_commits) {
        times.push_back(c.time);
    }
    std::sort(times.begin(), times.end(), std::greater<long long>());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    m_timeAxis.clear();
    m_timeAxis.reserve(times.size());
    if (times.empty()) {
        return;
    }

    double y = 0.0;
    m_timeAxis.emplace_back(times[0], y);
    for (size_t i = 1; i < times.size(); ++i) {
        const double gap = (double)(times[i - 1] - times[i]);
        y += std::min(gap, GAP_CAP_SECONDS) * TIME_SCALE;
        m_timeAxis.emplace_back(times[i], y);
    }
}

double GraphCanvas::TimeToY(long long t) const {
    if (m_timeAxis.empty()) {
        return 0.0;
    }
    if (t >= m_timeAxis.front().first) {
        return m_timeAxis.front().second;
    }
    if (t <= m_timeAxis.back().first) {
        return m_timeAxis.back().second;
    }

    size_t lo = 0, hi = m_timeAxis.size() - 1;
    while (lo + 1 < hi) {
        const size_t mid = (lo + hi) / 2;
        if (m_timeAxis[mid].first >= t) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    const auto& a = m_timeAxis[lo];  // time >= t
    const auto& b = m_timeAxis[hi];  // time <= t
    if (a.first == b.first) {
        return a.second;
    }
    const double frac = (double)(a.first - t) / (double)(a.first - b.first);
    return a.second + frac * (b.second - a.second);
}

void GraphCanvas::ResetView() {
    m_fitPending = false;
    FitView(GetClientSize());
    Refresh();
}

bool GraphCanvas::SaveImage(const wxString& path, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    wxBitmap bmp(width, height);
    wxMemoryDC dc(bmp);
    FitView(wxSize(width, height));
    DrawGraph(dc, wxSize(width, height));
    dc.SelectObject(wxNullBitmap);
    wxImage img = bmp.ConvertToImage();
    return img.SaveFile(path, wxBITMAP_TYPE_PNG);
}

void GraphCanvas::OnPaint(wxPaintEvent&) {
    wxBufferedPaintDC dc(this);
    DrawGraph(dc, GetClientSize());
}

void GraphCanvas::OnSize(wxSizeEvent&) {
    if (m_fitPending) {
        FitView(GetClientSize());
    }
    Refresh();
}

void GraphCanvas::OnMouseWheel(wxMouseEvent& evt) {
    const int delta = evt.GetWheelRotation() / evt.GetWheelDelta();

    if (evt.ControlDown()) {
        const wxPoint m = evt.GetPosition();
        double k = delta > 0 ? 1.2 : 1.0 / 1.2;
        double nscale = m_scale * k;
        nscale = std::max(1e-6, std::min(nscale, 100.0));

        const double wx_ = m.x / m_scale + m_offX;
        const double wy_ = m.y / m_scale + m_offY;
        m_scale = nscale;
        m_offX = wx_ - m.x / m_scale;
        m_offY = wy_ - m.y / m_scale;
        Refresh();
        return;
    }

    const double step = 40.0 / m_scale;
    if (evt.ShiftDown()) {
        m_offX -= delta * step;
    } else {
        m_offY += delta * step;
    }
    Refresh();
}

void GraphCanvas::OnMouseMove(wxMouseEvent& evt) {
    if (m_dragging) {
        const wxPoint pos = evt.GetPosition();
        const wxPoint delta = pos - m_lastMouse;
        m_lastMouse = pos;
        if (delta.x != 0 || delta.y != 0) {
            m_panned = true;
        }
        m_offX -= delta.x / m_scale;
        m_offY -= delta.y / m_scale;
        Refresh();
        return;
    }

    const Commit* c = HitTest(evt.GetPosition());
    if (c) {
        wxString tip = c->subject.Left(80);
        tip << wxT("\n") << formatTime(c->time) << wxT("\nby ") << c->author << wxT("\n") << c->oid;
        SetToolTip(tip);
    } else {
        UnsetToolTip();
    }
}

void GraphCanvas::OnMouseDown(wxMouseEvent& evt) {
    if (evt.GetButton() != wxMOUSE_BTN_LEFT) {
        return;
    }
    if (evt.ControlDown()) {
        const Commit* c = HitTest(evt.GetPosition());
        if (c) {
            ToggleCompareSelection(c->oid);
            Refresh();
        }
        return;
    }
    m_dragging = true;
    m_panned = false;
    m_lastMouse = evt.GetPosition();
    CaptureMouse();
}

void GraphCanvas::OnMouseRightDown(wxMouseEvent& evt) {
    wxMenu menu;
    wxMenuItem* diffItem = menu.Append(ID_DIFF_MENU, wxT("Diff Selected Commits"));
    diffItem->Enable(m_compareOids.size() == 2);
    PopupMenu(&menu, evt.GetPosition());
}

void GraphCanvas::OnDiffMenuClick(wxCommandEvent&) {
    if (m_compareOids.size() != 2) {
        return;
    }
    wxCommandEvent e(wxEVT_DIFF_REQUESTED, GetId());
    e.SetString(m_compareOids[0] + wxT("|") + m_compareOids[1]);
    e.SetEventObject(this);
    wxPostEvent(this, e);
}

void GraphCanvas::ToggleCompareSelection(const wxString& oid) {
    auto it = std::find(m_compareOids.begin(), m_compareOids.end(), oid);
    if (it != m_compareOids.end()) {
        m_compareOids.erase(it);
        return;
    }
    m_compareOids.push_back(oid);
    if (m_compareOids.size() > 2) {
        m_compareOids.erase(m_compareOids.begin());
    }
}

void GraphCanvas::OnMouseUp(wxMouseEvent& evt) {
    if (!m_dragging) {
        return;
    }
    m_dragging = false;
    if (HasCapture()) {
        ReleaseMouse();
    }
    if (!m_panned) {
        const Commit* c = HitTest(evt.GetPosition());
        m_selectedOid = c ? c->oid : wxString();

        wxCommandEvent e(wxEVT_COMMIT_SELECTED, GetId());
        e.SetString(m_selectedOid);
        e.SetEventObject(this);
        wxPostEvent(this, e);
        Refresh();
    }
}

void GraphCanvas::OnMouseLeave(wxMouseEvent&) {
    if (m_dragging && HasCapture()) {
        ReleaseMouse();
    }
    m_dragging = false;
}

void GraphCanvas::FitView(const wxSize& size) {
    if (m_commits.empty()) {
        m_scale = 1.0;
        m_offX = 0.0;
        m_offY = 0.0;
        return;
    }

    if (size.GetWidth() <= 0 || size.GetHeight() <= 0) {
        m_fitPending = true;
        return;
    }

    double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18;
    for (const Commit& c : m_commits) {
        const double x = c.lane * LANE_SPACING;
        const double y = TimeToY(c.time);
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }

    double w = maxX - minX;
    if (w <= 0) {
        w = LANE_SPACING;
    }
    double h = maxY - minY;
    if (h <= 0) {
        h = 1.0;
    }

    const double sx = (double)(size.GetWidth() - 90) / w;
    const double sy = (double)(size.GetHeight() - 90) / h;
    m_scale = std::min(sx, sy);
    if (m_scale <= 0) {
        m_scale = 1.0;
    }

    const double cx = (minX + maxX) / 2.0;
    const double cy = (minY + maxY) / 2.0;
    m_offX = cx - size.GetWidth() / (2.0 * m_scale);
    m_offY = cy - size.GetHeight() / (2.0 * m_scale);
}

wxPoint GraphCanvas::W2S(double x, double y) const {
    const long LIMIT = 1000000;
    long sx = (long)std::lround((x - m_offX) * m_scale);
    long sy = (long)std::lround((y - m_offY) * m_scale);
    sx = std::max(-LIMIT, std::min(sx, LIMIT));
    sy = std::max(-LIMIT, std::min(sy, LIMIT));
    return wxPoint(sx, sy);
}

const Commit* GraphCanvas::FindCommit(const wxString& oid) const {
    for (const auto& c : m_commits) {
        if (c.oid == oid) {
            return &c;
        }
    }
    return nullptr;
}

const Commit* GraphCanvas::HitTest(const wxPoint& sp) const {
    const Commit* hit = nullptr;
    const double r = std::max(1.5, std::min(RADIUS * m_scale, 24.0));
    double best = r + 4.0;
    for (const auto& c : m_commits) {
        const wxPoint p = W2S(c.lane * LANE_SPACING, TimeToY(c.time));
        const double dx = p.x - sp.x;
        const double dy = p.y - sp.y;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d < best) {
            best = d;
            hit = &c;
        }
    }
    return hit;
}

void GraphCanvas::DrawGraph(wxDC& dc, const wxSize& size) {
    dc.SetBrush(wxBrush(BG));
    dc.SetPen(wxPen(BG));
    dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());

    if (m_commits.empty()) {
        dc.SetTextForeground(TXT_DIM);
        dc.SetFont(wxFont(wxSize(14, 14), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dc.DrawText(wxT("Open a git repository to view its history timeline"),
                    30, size.GetHeight() / 2 - 10);
        return;
    }

    DrawTimeGrid(dc, size);

    for (const Edge& e : m_edges) {
        const double fx = e.fromLane * LANE_SPACING;
        const double fy = TimeToY(e.fromTime);
        const double tx = e.toLane * LANE_SPACING;
        const double ty = TimeToY(e.toTime);

        dc.SetPen(wxPen(LaneColour(e.fromLane), 2));
        if (e.fromLane == e.toLane) {
            dc.DrawLine(W2S(fx, fy), W2S(tx, ty));
        } else {
            const double mx = (fx + tx) / 2.0;
            wxPoint pts[4] = { W2S(fx, fy), W2S(mx, fy), W2S(mx, ty), W2S(tx, ty) };
            dc.DrawLines(4, pts);
        }
    }

    const double r = std::max(1.5, std::min(RADIUS * m_scale, 24.0));
    for (const Commit& c : m_commits) {
        const wxPoint p = W2S(c.lane * LANE_SPACING, TimeToY(c.time));
        if (p.x < -20 || p.y < -20 || p.x > size.GetWidth() + 20 || p.y > size.GetHeight() + 20) {
            continue;
        }
        const wxColour col = LaneColour(c.lane);

        if (c.oid == m_selectedOid) {
            dc.SetPen(wxPen(SEL, 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawCircle(p, r + 4);
        }

        const auto cmpIt = std::find(m_compareOids.begin(), m_compareOids.end(), c.oid);
        if (cmpIt != m_compareOids.end()) {
            dc.SetPen(wxPen(COMPARE, 2, wxPENSTYLE_SHORT_DASH));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawCircle(p, r + 7);

            const int order = (int)(cmpIt - m_compareOids.begin()) + 1;
            const wxString label = wxString::Format(wxT("%d"), order);
            dc.SetFont(wxFont(wxSize(9, 9), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
            const wxSize ext = dc.GetTextExtent(label);
            const wxPoint badge(p.x + (int)(r + 7), p.y - (int)(r + 7));
            dc.SetPen(wxPen(COMPARE));
            dc.SetBrush(wxBrush(COMPARE));
            dc.DrawCircle(badge, ext.GetHeight() / 2 + 2);
            dc.SetTextForeground(*wxBLACK);
            dc.DrawText(label, badge.x - ext.GetWidth() / 2, badge.y - ext.GetHeight() / 2);
        }

        dc.SetPen(wxPen(col, 2));
        dc.SetBrush(wxBrush(col));
        dc.DrawCircle(p, c.isMerge() ? r + 2 : r);
    }

    dc.SetFont(wxFont(wxSize(10, 10), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    std::map<wxString, int> labelStack;
    for (const GitRef& r : m_refs) {
        const Commit* c = FindCommit(r.oid);
        if (!c) {
            continue;
        }
        const wxPoint p = W2S(c->lane * LANE_SPACING, TimeToY(c->time));
        if (p.x < -80 || p.x > size.GetWidth() + 80 || p.y < -80 || p.y > size.GetHeight() + 80) {
            continue;
        }

        const wxColour bg = RefColour(r.kind);
        const wxSize ext = dc.GetTextExtent(r.name);
        const int w = ext.GetWidth() + 10;
        const int h = ext.GetHeight() + 4;
        const int offset = labelStack[r.oid] * (h + 2);
        int x = p.x + 8;
        int y = p.y - h - 2 - offset;
        if (x + w > size.GetWidth()) {
            x = p.x - w - 8;
        }
        if (y < 0) {
            y = p.y + 2 + offset;
        }

        dc.SetPen(wxPen(bg));
        dc.SetBrush(wxBrush(bg));
        dc.DrawRoundedRectangle(x, y, w, h, 3);
        dc.SetTextForeground(*wxWHITE);
        dc.DrawText(r.name, x + 5, y + 2);
        ++labelStack[r.oid];
    }
}

void GraphCanvas::DrawTimeGrid(wxDC& dc, const wxSize& size) {
    const long long range = m_maxTime - m_minTime;
    if (range <= 0 || m_timeAxis.empty()) {
        return;
    }
    const double rangePx = m_timeAxis.back().second * m_scale;
    if (rangePx <= 0) {
        return;
    }

    static const long long steps[] = {
        3600LL, 6LL * 3600, 86400LL, 3LL * 86400, 7LL * 86400, 30LL * 86400,
        90LL * 86400, 365LL * 86400, 2LL * 365 * 86400, 5LL * 365 * 86400,
        10LL * 365 * 86400, 20LL * 365 * 86400, 50LL * 365 * 86400, 100LL * 365 * 86400,
    };
    long long step = 86400;
    for (long long s : steps) {
        const double pxPerTick = rangePx * (double)s / (double)range;
        if (pxPerTick >= 120.0) {
            step = s;
        } else {
            break;
        }
    }

    long long t = (m_minTime / step) * step;
    if (t < m_minTime) {
        t += step;
    }

    dc.SetPen(wxPen(GRID, 1));
    dc.SetFont(wxFont(wxSize(10, 10), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    for (; t <= m_maxTime; t += step) {
        const wxPoint p = W2S(0, TimeToY(t));
        if (p.y < 0 || p.y > size.GetHeight()) {
            continue;
        }
        dc.DrawLine(0, p.y, size.GetWidth(), p.y);
        dc.SetTextForeground(TXT_DIM);
        const wxDateTime dt((time_t)t);
        if (dt.IsValid()) {
            dc.DrawText(dt.Format(wxT("%Y-%m-%d")), 6, p.y + 2);
        }
    }
}
