#include "App.h"

#include "GitRepository.h"
#include "GraphCanvas.h"
#include "Layout.h"
#include "MainFrame.h"

#include <wx/image.h>
#include <wx/utils.h>
#include <cstdio>

namespace {

bool RenderToFile(const wxString& file, const wxString& repo) {
    std::vector<Commit> commits;
    std::vector<GitRef> refs;
    wxString err;
    if (!GitRepository::load(repo, commits, refs, err)) {
        fprintf(stderr, "gitvis: failed to read repository: %s\n", err.utf8_str().data());
        return false;
    }

    assignLanes(commits);
    std::vector<Edge> edges = buildEdges(commits);

    long long minT = 0, maxT = 0;
    for (size_t i = 0; i < commits.size(); ++i) {
        if (i == 0) {
            minT = maxT = commits[i].time;
            continue;
        }
        if (commits[i].time < minT) {
            minT = commits[i].time;
        }
        if (commits[i].time > maxT) {
            maxT = commits[i].time;
        }
    }
    if (maxT == minT) {
        maxT = minT + 1;
    }

    GraphCanvas canvas(nullptr, wxID_ANY);
    canvas.SetData(commits, edges, refs, minT, maxT);
    bool ok = canvas.SaveImage(file, 1200, 720);
    return ok;
}

}  // namespace

bool App::OnInit() {
    SetAppName(wxT("gitvis"));
    wxInitAllImageHandlers();

    wxString renderPath;
    wxString repoPath;
    for (int i = 1; i < argc; ++i) {
        const wxString a = argv[i];
        if (a == wxT("--render") && i + 1 < argc) {
            renderPath = argv[++i];
        } else if (a.StartsWith(wxT("--render="))) {
            renderPath = a.AfterFirst(wxT('='));
        } else if (a == wxT("--repo") && i + 1 < argc) {
            repoPath = argv[++i];
        } else if (a.StartsWith(wxT("--repo="))) {
            repoPath = a.AfterFirst(wxT('='));
        }
    }

    if (!renderPath.IsEmpty()) {
        const wxString repo = repoPath.IsEmpty() ? wxGetCwd() : repoPath;
        return RenderToFile(renderPath, repo);
    }

    auto* frame = new MainFrame(wxT("gitvis - git timeline"), wxSize(1200, 760));
    frame->Show(true);
    return true;
}
