#pragma once

#include "AppPaths.h"
#include "DownloadQueue.h"

#include <vector>

class DownloadQueueStore {
public:
    static std::vector<DownloadTaskSnapshot> Load(const AppPaths& paths);
    static void Save(const AppPaths& paths, const std::vector<DownloadTaskSnapshot>& tasks);
};
