#include "GitRepository.h"

#include <wx/arrstr.h>
#include <wx/process.h>
#include <wx/stream.h>
#include <wx/utils.h>
#include <wx/wfstream.h>

#include <string>
#include <vector>

namespace {

wxString quoteArg(const wxString& s) {
    wxString q = wxT("\"");
    for (size_t i = 0; i < s.Len(); ++i) {
        if (s[i] == wxT('"')) {
            q += wxT("\\\"");
        } else if (s[i] == wxT('\\')) {
            q += wxT("\\\\");
        } else {
            q += s[i];
        }
    }
    q += wxT("\"");
    return q;
}

wxString fromUtf8(const std::string& s) {
    return wxString::FromUTF8(s.c_str());
}

const wxString EMPTY_TREE = wxT("4b825dc642cb6eb9a060e54bf8d69288fbee4904");

void parseNameStatus(const wxString& raw, std::vector<FileChange>& files) {
    const std::string s = raw.utf8_string();
    size_t start = 0;
    while (start < s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            nl = s.size();
        }
        std::string line = s.substr(start, nl - start);
        start = nl + 1;
        if (line.empty()) {
            continue;
        }

        std::vector<std::string> f;
        size_t b = 0;
        while (b < line.size()) {
            size_t e = line.find('\t', b);
            if (e == std::string::npos) {
                e = line.size();
            }
            f.push_back(line.substr(b, e - b));
            b = e + 1;
        }
        if (f.size() < 2) {
            continue;
        }

        FileChange fc;
        fc.status = fromUtf8(f[0]);
        if (f.size() >= 3) {
            fc.oldPath = fromUtf8(f[1]);
            fc.path = fromUtf8(f[2]);
        } else {
            fc.path = fromUtf8(f[1]);
        }
        files.push_back(fc);
    }
}

void parseFileList(const wxString& raw, std::vector<wxString>& files) {
    const std::string s = raw.utf8_string();
    size_t start = 0;
    while (start < s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            nl = s.size();
        }
        std::string line = s.substr(start, nl - start);
        start = nl + 1;
        if (line.empty()) {
            continue;
        }
        files.push_back(fromUtf8(line));
    }
}

bool parseLog(const wxString& raw, std::vector<Commit>& commits) {
    const std::string s = raw.utf8_string();
    size_t start = 0;
    while (start < s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            nl = s.size();
        }
        std::string line = s.substr(start, nl - start);
        start = nl + 1;
        if (line.empty()) {
            continue;
        }

        std::vector<std::string> f;
        size_t b = 0;
        while (b < line.size()) {
            size_t e = line.find('\t', b);
            if (e == std::string::npos) {
                e = line.size();
            }
            f.push_back(line.substr(b, e - b));
            b = e + 1;
        }
        if (f.size() < 6) {
            continue;
        }

        Commit c;
        c.oid = fromUtf8(f[0]);

        std::string ps = f[1];
        size_t b2 = 0;
        while (b2 < ps.size()) {
            size_t e2 = ps.find(' ', b2);
            if (e2 == std::string::npos) {
                e2 = ps.size();
            }
            std::string p = ps.substr(b2, e2 - b2);
            if (!p.empty()) {
                c.parents.push_back(fromUtf8(p));
            }
            b2 = e2 + 1;
        }

        c.author = fromUtf8(f[2]);
        c.email = fromUtf8(f[3]);
        wxString timeStr = fromUtf8(f[4]);
        if (!timeStr.ToLongLong(&c.time)) {
            c.time = 0;
        }
        c.subject = fromUtf8(f[5]);
        if (c.subject.IsEmpty()) {
            c.subject = wxT("(no subject)");
        }
        commits.push_back(c);
    }
    return true;
}

void parseRefs(const wxString& raw, std::vector<GitRef>& refs) {
    const std::string s = raw.utf8_string();
    size_t start = 0;
    while (start < s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            nl = s.size();
        }
        std::string line = s.substr(start, nl - start);
        start = nl + 1;
        if (line.empty()) {
            continue;
        }

        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) {
            continue;
        }
        size_t t2 = line.find('\t', t1 + 1);

        std::string refname = line.substr(0, t1);
        std::string objectname = line.substr(t1 + 1, (t2 == std::string::npos) ? std::string::npos : t2 - t1 - 1);
        std::string peeled;
        if (t2 != std::string::npos) {
            peeled = line.substr(t2 + 1);
        }
        std::string oid = peeled.empty() ? objectname : peeled;

        GitRef r;
        r.name = fromUtf8(refname);
        r.oid = fromUtf8(oid);

        if (refname.compare(0, 11, "refs/heads/") == 0) {
            r.kind = GitRef::Branch;
            r.name = r.name.Mid(11);
        } else if (refname.compare(0, 13, "refs/remotes/") == 0) {
            r.kind = GitRef::RemoteBranch;
            r.name = r.name.Mid(13);
        } else if (refname.compare(0, 10, "refs/tags/") == 0) {
            r.kind = GitRef::Tag;
            r.name = r.name.Mid(10);
        } else {
            continue;
        }
        refs.push_back(r);
    }
}

}  // namespace

long GitRepository::runGit(const wxString& path, const std::vector<wxString>& args, wxString& out) {
    wxString cmd = wxT("git -C ") + quoteArg(path);
    for (const auto& a : args) {
        cmd += wxT(" ") + quoteArg(a);
    }

    wxArrayString output;
    wxArrayString err;
    long rc = wxExecute(cmd, output, err, wxEXEC_SYNC);
    out = wxJoin(output, wxT('\n'));
    return rc;
}

bool GitRepository::inWorkTree(const wxString& path) {
    wxString out;
    if (runGit(path, { wxT("rev-parse"), wxT("--is-inside-work-tree") }, out) != 0) {
        return false;
    }
    return out.Upper().Trim().Trim(false) == wxT("TRUE");
}

bool GitRepository::changedFiles(const wxString& path, const wxString& oid,
                                 const std::vector<wxString>& parents,
                                 std::vector<FileChange>& files, wxString& error) {
    files.clear();
    const wxString base = parents.empty() ? EMPTY_TREE : parents[0];

    wxString out;
    long rc = runGit(path, { wxT("diff"), wxT("--name-status"), wxT("-M"), base, oid }, out);
    if (rc != 0) {
        error = wxT("git diff failed (exit code ");
        error << rc << wxT(")");
        return false;
    }
    parseNameStatus(out, files);
    return true;
}

bool GitRepository::fileDiff(const wxString& path, const wxString& oid,
                             const std::vector<wxString>& parents, const wxString& file,
                             wxString& diff, wxString& error) {
    const wxString base = parents.empty() ? EMPTY_TREE : parents[0];

    wxString out;
    long rc = runGit(path, { wxT("diff"), base, oid, wxT("--"), file }, out);
    if (rc != 0) {
        error = wxT("git diff failed (exit code ");
        error << rc << wxT(")");
        return false;
    }
    diff = out;
    return true;
}

bool GitRepository::changedFilesBetween(const wxString& path, const wxString& oidA, const wxString& oidB,
                                        std::vector<FileChange>& files, wxString& error) {
    files.clear();

    wxString out;
    long rc = runGit(path, { wxT("diff"), wxT("--name-status"), wxT("-M"), oidA, oidB }, out);
    if (rc != 0) {
        error = wxT("git diff failed (exit code ");
        error << rc << wxT(")");
        return false;
    }
    parseNameStatus(out, files);
    return true;
}

bool GitRepository::fileDiffBetween(const wxString& path, const wxString& oidA, const wxString& oidB,
                                    const wxString& file, wxString& diff, wxString& error) {
    wxString out;
    long rc = runGit(path, { wxT("diff"), oidA, oidB, wxT("--"), file }, out);
    if (rc != 0) {
        error = wxT("git diff failed (exit code ");
        error << rc << wxT(")");
        return false;
    }
    diff = out;
    return true;
}

bool GitRepository::listTree(const wxString& path, const wxString& oid,
                             std::vector<wxString>& files, wxString& error) {
    files.clear();

    wxString out;
    long rc = runGit(path, { wxT("ls-tree"), wxT("-r"), wxT("--name-only"), oid }, out);
    if (rc != 0) {
        error = wxT("git ls-tree failed (exit code ");
        error << rc << wxT(")");
        return false;
    }
    parseFileList(out, files);
    return true;
}

bool GitRepository::exportFile(const wxString& path, const wxString& oid, const wxString& file,
                               const wxString& outputPath, wxString& error) {
    const wxString cmd = wxT("git -C ") + quoteArg(path) + wxT(" show ") +
                         quoteArg(oid + wxT(":") + file);

    wxProcess process;
    process.Redirect();
    const long rc = wxExecute(cmd, wxEXEC_SYNC, &process);
    if (rc != 0) {
        error = wxT("git show failed (exit code ");
        error << rc << wxT(")");
        return false;
    }

    wxInputStream* in = process.GetInputStream();
    if (!in) {
        error = wxT("Failed to read git output");
        return false;
    }

    wxFileOutputStream out(outputPath);
    if (!out.IsOk()) {
        error = wxT("Failed to open destination file");
        return false;
    }
    out.Write(*in);
    if (out.GetLastError() != wxSTREAM_NO_ERROR) {
        error = wxT("Failed to write destination file");
        return false;
    }
    return true;
}

bool GitRepository::load(const wxString& path, std::vector<Commit>& commits,
                         std::vector<GitRef>& refs, wxString& error) {
    commits.clear();
    refs.clear();

    wxString logOut;
    long rc = runGit(path, {
                               wxT("log"), wxT("--all"), wxT("--topo-order"), wxT("--parents"),
                               wxT("--format=%H%x09%P%x09%an%x09%ae%x09%ct%x09%s%n"),
                           },
                     logOut);
    if (rc != 0) {
        if (logOut.IsEmpty()) {
            return true;  // empty repository, nothing to show
        }
        error = wxT("git log failed (exit code ");
        error << rc << wxT(")");
        return false;
    }
    parseLog(logOut, commits);

    wxString refOut;
    rc = runGit(path, {
                          wxT("for-each-ref"),
                          wxT("--format=%(refname)\t%(objectname)\t%(*objectname)"),
                          wxT("refs/heads"), wxT("refs/remotes"), wxT("refs/tags"),
                      },
                refOut);
    if (rc != 0) {
        error = wxT("git for-each-ref failed (exit code ");
        error << rc << wxT(")");
        return false;
    }
    parseRefs(refOut, refs);

    wxString headOut;
    if (runGit(path, { wxT("rev-parse"), wxT("HEAD") }, headOut) == 0) {
        wxString oid = headOut.Trim().Trim(false);
        if (!oid.IsEmpty()) {
            GitRef r;
            r.kind = GitRef::Head;
            r.name = wxT("HEAD");
            r.oid = oid;
            refs.push_back(r);
        }
    }

    return true;
}
