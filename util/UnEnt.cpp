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
 total_size_ = 0;
}
UnEnt::~UnEnt() {

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
    }
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        std::string currentPath = archive_entry_pathname(entry);
        std::string fullDestPath = destinationPath + "/" + currentPath;

        // 경로 문자열의 마지막이 '/'로 끝나면 디렉토리로 간주하고,
        // 실제 파일 시스템 경로에서 마지막 '/'를 제거합니다 (디렉토리 생성 시).
        if (!currentPath.empty() && currentPath.back() == '/') {
            // libarchive가 AE_IFDIR로 타입을 잘 반환하므로, 이 조건은 중복일 수 있습니다.
            // archive_entry_filetype(entry) == AE_IFDIR 로 확인하는 것이 더 정확합니다.
            if (archive_entry_filetype(entry) == AE_IFDIR) {
                 try {
 if (!std::filesystem::exists(fullDestPath)) {
 std::filesystem::create_directories(fullDestPath);
 }
                } catch (const std::filesystem::filesystem_error& e) {
 EntLog("Failed to create directory " + fullDestPath + ": " + e.what());
                }
            }
        } else if (archive_entry_filetype(entry) == AE_IFREG) { // 일반 파일인 경우
            // 파일의 상위 디렉토리가 존재하지 않으면 생성
            std::filesystem::path p(fullDestPath);
            if (p.has_parent_path()) {
                try {
                    std::filesystem::create_directories(p.parent_path());
                } catch (const std::filesystem::filesystem_error& e) {
 EntLog("Failed to create parent directory for " + fullDestPath + ": " + e.what());
                }
            }

            std::ofstream outFile(fullDestPath, std::ios::binary);
            if (!outFile.is_open()) {
 EntLog("Failed to create file: " + fullDestPath);
 archive_read_data_skip(a); // 현재 파일 데이터 건너뛰기
                continue;
            }

            while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                outFile.write(static_cast<const char*>(buff), size);
                current_extracted_size += size;
                if (progressCallback) progressCallback(current_extracted_size, total_size_);
            }
            outFile.close();
        }
        // 심볼릭 링크, 하드 링크 등 다른 타입의 엔트리 처리도 필요하다면 여기에 추가
         archive_entry_set_perm(entry, 0755); // 예시: 권한 설정 (필요한 경우)
 archive_read_extract(a, entry, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS); // 더 고수준의 추출 함수 사용
    }

 if (a)
 r = archive_read_free(a);
    if (r != ARCHIVE_OK) {
        // throw std::runtime_error("Failed to free archive resources.");
        return false; // 또는 예외
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
