#include "Layout.h"

#include <wx/datetime.h>

#include <map>
#include <set>

wxString formatTime(long long t) {
    const wxDateTime dt((time_t)t);
    if (!dt.IsValid()) {
        return wxT("?");
    }
    return dt.Format(wxT("%Y-%m-%d %H:%M"));
}

void assignLanes(std::vector<Commit>& commits) {
    std::map<wxString, int> laneOf;   // oid -> lane whose tip is that commit
    std::vector<wxString> lanes;      // lane -> tip oid (empty = free)
    std::set<int> freeLanes;

    for (auto& c : commits) {
        int lane;
        auto it = laneOf.find(c.oid);
        if (it != laneOf.end()) {
            lane = it->second;
            laneOf.erase(it);
        } else {
            auto fi = freeLanes.begin();
            if (fi != freeLanes.end()) {
                lane = *fi;
                freeLanes.erase(fi);
            } else {
                lane = (int)lanes.size();
                lanes.push_back(wxEmptyString);
            }
        }
        c.lane = lane;

        bool extended = false;
        for (size_t i = 0; i < c.parents.size(); ++i) {
            const wxString& p = c.parents[i];
            if (p.empty()) {
                continue;
            }
            if (laneOf.find(p) != laneOf.end()) {
                continue;
            }
            if (i == 0) {
                laneOf[p] = lane;
                lanes[lane] = p;
                extended = true;
            } else {
                auto fi = freeLanes.begin();
                int pl;
                if (fi != freeLanes.end()) {
                    pl = *fi;
                    freeLanes.erase(fi);
                } else {
                    pl = (int)lanes.size();
                    lanes.push_back(wxEmptyString);
                }
                laneOf[p] = pl;
                lanes[pl] = p;
            }
        }
        if (!extended) {
            lanes[lane] = wxEmptyString;
            freeLanes.insert(lane);
        }
    }
}

std::vector<Edge> buildEdges(const std::vector<Commit>& commits) {
    std::map<wxString, std::pair<int, long long>> pos;
    for (const auto& c : commits) {
        pos[c.oid] = { c.lane, c.time };
    }

    std::vector<Edge> edges;
    for (const auto& c : commits) {
        for (const auto& p : c.parents) {
            auto it = pos.find(p);
            if (it == pos.end()) {
                continue;
            }
            Edge e;
            e.fromLane = c.lane;
            e.toLane = it->second.first;
            e.fromTime = c.time;
            e.toTime = it->second.second;
            edges.push_back(e);
        }
    }
    return edges;
}
