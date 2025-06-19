#include "UnEnt.h"
#include <stdexcept>
#include <fstream> // For std::ofstream
#include <iostream> // For std::cerr
#include <filesystem> // For std::filesystem::create_directories (C++17)
#include <archive.h>
#include <archive_entry.h>
#include <functional>

// UnEnt.h에 FileEntry 구조체는 여전히 유용할 수 있습니다 (예: UI에 목록 표시).
// 하지만 압축 해제 자체에는 필수는 아닙니다.

// decompAndReturnFileTree 함수 대신, 압축을 직접 푸는 함수를 만들 수 있습니다.
// 예를 들어:
// static bool extractArchive(const std::string& archivePath, const std::string& destinationPath);

// 아래는 decompAndReturnFileTree 함수를 수정하여 직접 압축 해제하는 예시입니다.
// 실제로는 별도의 extractArchive 함수를 만드는 것이 더 명확할 수 있습니다.

// 이 함수는 이제 파일 트리를 반환하는 대신, 지정된 경로에 압축을 풉니다.
// 성공 여부를 bool로 반환하거나, 예외를 통해 오류를 알릴 수 있습니다.
UnEnt::UnEnt() : logger_("UnEnt.log"){
    EntLog("UnEnt Ready");
    total_size_ = 0;
}
UnEnt::~UnEnt() {
    EntLog("UnEnt Shutdown");
}
bool UnEnt::extractArchiveTo(const std::string& archivePath, const std::string& destinationPath, std::function<void(double,double)> progressCallback) {
    archive *a = nullptr;
    archive_entry *entry;
    int r;
    const void *buff;
    size_t size;
#if ARCHIVE_VERSION_NUMBER >= 3000000
    la_int64_t offset;
#else
    off_t offset;
#endif
    double current_extracted_size = 0;

    a = archive_read_new();
    archive_read_support_filter_gzip(a);
    archive_read_support_format_tar(a);

    r = archive_read_open_filename(a, archivePath.c_str(), 10240);
    if (r != ARCHIVE_OK) {
        std::string errMsg = "Failed to open archive '" + archivePath + "': " + archive_error_string(a);
        if (a)
            archive_read_free(a);
        throw std::runtime_error(errMsg);
    }

    // Calculate total size for progress
    archive *a_size = archive_read_new();
    archive_read_support_filter_gzip(a_size);
    archive_read_support_format_tar(a_size);
    if (archive_read_open_filename(a_size, archivePath.c_str(), 10240) == ARCHIVE_OK) {
        while (archive_read_next_header(a_size, &entry) == ARCHIVE_OK) {
            total_size_ += archive_entry_size(entry);
        }
        archive_read_free(a_size);
    } else {
        // 파일 크기 계산 실패 시, total_size_가 0으로 유지되어 진행률 계산에 문제 발생 가능
        // 필요시 오류 처리 또는 기본값 설정
        EntLog("Warning: Could not open archive to calculate total size. Progress may not be accurate.");
    }

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        std::string currentPath = archive_entry_pathname(entry);
        std::string fullDestPath = destinationPath + "/" + currentPath;

        if (archive_entry_filetype(entry) == AE_IFDIR) {
            try {
                if (!std::filesystem::exists(fullDestPath)) {
                    std::filesystem::create_directories(fullDestPath);
                }
            } catch (const std::filesystem::filesystem_error& e) {
                EntLog("Failed to create directory " + fullDestPath + ": " + e.what());
            }
        } else if (archive_entry_filetype(entry) == AE_IFREG) {
            std::filesystem::path p(fullDestPath);
            if (p.has_parent_path()) {
                try {
                    if (!std::filesystem::exists(p.parent_path())) { // 부모 디렉토리 존재 여부 확인 추가
                        std::filesystem::create_directories(p.parent_path());
                    }
                } catch (const std::filesystem::filesystem_error& e) {
                    EntLog("Failed to create parent directory for " + fullDestPath + ": " + e.what());
                }
            }

            std::ofstream outFile(fullDestPath, std::ios::binary);
            if (!outFile.is_open()) {
                EntLog("Failed to create file: " + fullDestPath);
                archive_read_data_skip(a);
                continue;
            }

            while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                outFile.write(static_cast<const char*>(buff), size);
                current_extracted_size += size;
                if (progressCallback) progressCallback(current_extracted_size, total_size_);

                // 수정된 로그 출력 부분
                std::ostringstream oss;
                double percentage = 0.0;
                if (total_size_ > 0) {
                    percentage = (current_extracted_size / total_size_) * 100.0;
                }
                oss << "Decompression Progress: " << std::fixed << std::setprecision(2) << percentage
                    << "% (" << static_cast<long long>(current_extracted_size)
                    << "/" << static_cast<long long>(total_size_)
                    << ") Current block size: " << size << ", offset: " << offset;
                EntLog(oss.str());
            }
            outFile.close();
        }
        // archive_entry_set_perm(entry, 0755); // 이 줄은 archive_read_extract 사용 시 중복될 수 있음
        // archive_read_extract(a, entry, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);
        // 위 extract 함수는 파일 데이터를 자동으로 처리하므로, 수동으로 data_block을 읽는 로직과 함께 사용하면 안 됩니다.
        // 여기서는 수동으로 데이터를 쓰고 있으므로, archive_read_extract는 주석 처리하거나 제거해야 합니다.
        // 만약 archive_read_extract를 사용한다면, 위의 파일 쓰기 루프는 필요 없습니다.
        // 현재 코드는 수동으로 파일을 쓰고 있으므로, archive_read_extract는 주석 처리된 상태로 두거나 삭제합니다.
    }

    if (a)
        r = archive_read_free(a);
    if (r != ARCHIVE_OK) {
        return false;
    }
    return true;
}
void UnEnt::EntLog(std::string s) const {
    std::string TAG = "[UnEntry] ";
    std::string lm = TAG + s;
    printf(lm.c_str());
    printf("\n");
    logger_.log(lm);
}
