#pragma once

#include "GitData.h"

void assignLanes(std::vector<Commit>& commits);
std::vector<Edge> buildEdges(const std::vector<Commit>& commits);
