#pragma once

#include "GitData.h"

#include <wx/string.h>

#include <vector>

class GitRepository {
public:
    static bool inWorkTree(const wxString& path);
    static bool load(const wxString& path, std::vector<Commit>& commits,
                     std::vector<GitRef>& refs, wxString& error);

private:
    static long runGit(const wxString& path, const std::vector<wxString>& args, wxString& out);
};
