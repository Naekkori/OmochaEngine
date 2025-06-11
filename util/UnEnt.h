// UnEnt.h (또는 FileEntry를 정의할 적절한 헤더)
#pragma once
#ifndef UNENT_H
#define UNENT_H

#include <functional>
#include <string>
#include <util/Logger.h>
// 파일/디렉터리 항목을 위한 구조체
struct FileEntry {
    std::string path;
    bool is_directory;
    uint64_t size;
};

class UnEnt {
    double total_size_;
    mutable SimpleLogger logger_;
public:
    UnEnt();

    ~UnEnt();

    bool extractArchiveTo(const std::string &archivePath, const std::string &destinationPath, std::function<void(double, double)>
                          progressCallback);

    void EntLog(std::string s) const;
};

#endif