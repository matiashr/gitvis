#pragma once

#include <wx/string.h>
#include <vector>

struct GitRef {
    enum Kind { Branch, RemoteBranch, Tag, Head };
    Kind kind = Branch;
    wxString name;
    wxString oid;
};

struct Commit {
    wxString oid;
    std::vector<wxString> parents;
    wxString author;
    wxString email;
    wxString subject;
    long long time = 0;
    int lane = 0;

    bool isMerge() const { return parents.size() > 1; }
};

struct Edge {
    int fromLane = 0;
    int toLane = 0;
    long long fromTime = 0;
    long long toTime = 0;
};

struct FileChange {
    wxString status;
    wxString path;
    wxString oldPath;
};

wxString formatTime(long long t);
