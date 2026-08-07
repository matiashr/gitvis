#pragma once

#include "GitData.h"

#include <wx/string.h>

#include <vector>

class GitRepository {
public:
    static bool inWorkTree(const wxString& path);
    static bool load(const wxString& path, std::vector<Commit>& commits,
                     std::vector<GitRef>& refs, wxString& error);
    static bool changedFiles(const wxString& path, const wxString& oid,
                             const std::vector<wxString>& parents,
                             std::vector<FileChange>& files, wxString& error);
    static bool fileDiff(const wxString& path, const wxString& oid,
                         const std::vector<wxString>& parents, const wxString& file,
                         wxString& diff, wxString& error);
    static bool changedFilesBetween(const wxString& path, const wxString& oidA, const wxString& oidB,
                                    std::vector<FileChange>& files, wxString& error);
    static bool fileDiffBetween(const wxString& path, const wxString& oidA, const wxString& oidB,
                                const wxString& file, wxString& diff, wxString& error);

private:
    static long runGit(const wxString& path, const std::vector<wxString>& args, wxString& out);
};
