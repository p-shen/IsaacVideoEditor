// Standalone replacement helper: no application or Qt libraries may be loaded here.
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/stdio.h>
#endif
#endif

namespace {
namespace fs = std::filesystem;
constexpr const char *readyText = "IsaacVideoEditor update 1\n";
constexpr const char *startedText = "IsaacVideoEditor started 1\n";

void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

bool within(const fs::path &child, const fs::path &parent) {
    auto c = child.begin();
    for (auto p = parent.begin(); p != parent.end(); ++p, ++c)
        if (c == child.end()) return false;
#ifdef _WIN32
        else if (CompareStringOrdinal(c->c_str(), -1, p->c_str(), -1, TRUE) != CSTR_EQUAL) return false;
#else
        else if (*c != *p) return false;
#endif
    return true;
}

struct FileIdentity {
    unsigned long long device = 0, file = 0;
    explicit FileIdentity(const fs::path &path) {
#ifdef _WIN32
        HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        require(handle != INVALID_HANDLE_VALUE, "Could not identify an update path.");
        BY_HANDLE_FILE_INFORMATION info {};
        const bool success = GetFileInformationByHandle(handle, &info);
        CloseHandle(handle);
        require(success, "Could not identify an update path.");
        device = info.dwVolumeSerialNumber;
        file = (static_cast<unsigned long long>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
#else
        struct stat info {};
        require(lstat(path.c_str(), &info) == 0, "Could not identify an update path.");
        device = info.st_dev;
        file = info.st_ino;
#endif
    }
    bool matches(const fs::path &path) const {
        try {
            const FileIdentity now(path);
            return device == now.device && file == now.file;
        } catch (...) { return false; }
    }
};

void canonicalPath(const fs::path &path) {
    require(path.is_absolute() && path != path.root_path(), "Update paths must be absolute, canonical, and outside the filesystem root.");
    for (const auto &part : path)
        require(part != "." && part != "..", "Update paths must not contain dot components.");
    fs::path current = path.root_path();
    for (const auto &part : path.relative_path()) {
        current /= part;
        require(!fs::is_symlink(fs::symlink_status(current)), "Update paths must not use symbolic links.");
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(current.c_str());
        require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                "Update paths must not use junctions or reparse points.");
#endif
    }
    require(fs::canonical(path) == path, "Update paths must be canonical, without aliases.");
}

void regularFile(const fs::path &path, const std::string &description) {
    require(fs::is_regular_file(fs::symlink_status(path)), description + " is missing or is not a regular file.");
}

#if defined(__APPLE__) || defined(_WIN32)
void validateTree(const fs::path &root) {
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
        const auto status = entry.symlink_status();
        if (fs::is_symlink(status)) {
#ifdef __APPLE__
            require(within(entry.path(), root / "Contents" / "Frameworks") &&
                    within(fs::canonical(entry.path()), root), "A framework symbolic link escapes the application payload.");
#else
            throw std::runtime_error("Application payload must not contain symbolic links.");
#endif
        } else {
            require(fs::is_directory(status) || fs::is_regular_file(status), "Application payload contains a special file.");
#ifdef _WIN32
            const DWORD attributes = GetFileAttributesW(entry.path().c_str());
            require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                    "Application payload must not contain reparse points.");
#endif
        }
    }
}
#endif

#ifdef _WIN32
struct PathLess {
    bool operator()(const fs::path &left, const fs::path &right) const {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }
};
using Manifest = std::set<fs::path, PathLess>;

Manifest packageManifest(const fs::path &app) {
    regularFile(app / "isaac-package-files.txt", "Portable package manifest");
    std::ifstream stream(app / "isaac-package-files.txt", std::ios::binary);
    Manifest files;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        require(!line.empty() && line.find('\0') == std::string::npos && line.find('\\') == std::string::npos &&
                line.find(':') == std::string::npos, "Portable package manifest contains an invalid path.");
        auto relative = fs::u8path(line);
        relative.make_preferred();
        require(relative.is_relative() && !relative.has_root_path(), "Portable package manifest contains an absolute path.");
        for (const auto &part : relative)
            require(part != "." && part != "..", "Portable package manifest contains a dot component.");
        require(files.insert(relative).second, "Portable package manifest contains duplicate paths.");
        regularFile(app / relative, "Portable package file");
    }
    require(stream.eof() && files.count(".isaac-portable") && files.count("isaac-package-files.txt") &&
            files.count("IsaacVideoEditor.exe"), "Portable package manifest is incomplete.");
    return files;
}

void preserveUserFiles(const fs::path &current, const fs::path &replacement, Manifest &preserved) {
    const auto shipped = packageManifest(current);
    for (const auto &entry : fs::recursive_directory_iterator(current)) {
        if (!entry.is_regular_file()) continue;
        auto relative = entry.path().lexically_relative(current);
        relative.make_preferred();
        if (shipped.count(relative)) continue;
        const auto destination = replacement / relative;
        require(!fs::exists(fs::symlink_status(destination)) ||
                (preserved.count(relative) && fs::is_regular_file(fs::symlink_status(destination))),
                "A new package file conflicts with a user file; move the user file out of the portable application folder and retry.");
        fs::create_directories(destination.parent_path());
        fs::copy_file(entry.path(), destination, fs::copy_options::overwrite_existing);
        preserved.insert(relative);
    }
}
#endif

fs::path mainExecutable(const fs::path &app, const fs::path &relative) {
    return relative == "." ? app : app / relative;
}

void validateApp(const fs::path &app, const fs::path &relative) {
    canonicalPath(app);
#if defined(__APPLE__) || defined(_WIN32)
    require(fs::is_directory(app), "Expected an application directory.");
    validateTree(app);
#ifdef _WIN32
    regularFile(app / ".isaac-portable", "Portable installation marker");
    packageManifest(app);
#endif
#else
    regularFile(app, "AppImage payload");
#endif
    const auto executable = mainExecutable(app, relative);
    canonicalPath(executable);
    regularFile(executable, "Application executable");
#ifndef _WIN32
    require(access(executable.c_str(), X_OK) == 0, "Application executable does not have execute permission.");
#endif
}

fs::path helperPath() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    require(size > 0 && size < buffer.size(), "Could not locate update helper.");
    return fs::canonical(fs::path(std::wstring(buffer.data(), size)));
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    require(_NSGetExecutablePath(buffer.data(), &size) == 0, "Could not locate update helper.");
    return fs::canonical(buffer.data());
#else
    return fs::canonical("/proc/self/exe");
#endif
}

void validatePreparedDirectory(const fs::path &prepared) {
    canonicalPath(prepared);
    require(fs::is_directory(prepared), "Prepared update is not a directory.");
    const auto temp = fs::canonical(fs::temp_directory_path());
    bool temporary = within(prepared, temp) && prepared != temp;
#ifdef __APPLE__
    temporary = temporary || (within(prepared, "/private/tmp") && prepared != "/private/tmp");
#endif
    require(temporary, "Prepared update must be in a private operating-system temporary directory.");
#ifndef _WIN32
    struct stat info {};
    require(lstat(prepared.c_str(), &info) == 0 && info.st_uid == geteuid() && (info.st_mode & 0077) == 0,
            "Prepared update directory must be private to the current user.");
#endif
}

void validatePrepared(const fs::path &prepared) {
    const auto ready = prepared / "ready";
    regularFile(ready, "Update ready marker");
    require(fs::file_size(ready) == std::char_traits<char>::length(readyText), "Update ready marker is invalid.");
    std::ifstream marker(ready, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(marker)), std::istreambuf_iterator<char>());
    require(contents == readyText, "Update ready marker is invalid.");
    require(!fs::exists(fs::symlink_status(prepared / "armed")) && !fs::exists(fs::symlink_status(prepared / "started")) &&
            !fs::exists(fs::symlink_status(prepared / "error.txt")), "Prepared update has already been used.");
}

void writeMarker(const fs::path &path, const std::string &message) {
    require(!fs::exists(fs::symlink_status(path)), "Update diagnostic path already exists.");
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(file != INVALID_HANDLE_VALUE, "Could not write update status.");
    DWORD written = 0;
    const bool success = WriteFile(file, message.data(), static_cast<DWORD>(message.size()), &written, nullptr) && written == message.size();
    CloseHandle(file);
    require(success, "Could not write update status.");
#else
    int file = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    require(file >= 0, "Could not write update status.");
    size_t offset = 0;
    while (offset < message.size()) {
        const ssize_t written = write(file, message.data() + offset, message.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { close(file); throw std::runtime_error("Could not write update status."); }
        offset += static_cast<size_t>(written);
    }
    require(close(file) == 0, "Could not close update status.");
#endif
}

fs::path makeStage(const fs::path &parent) {
    std::random_device random;
    for (int attempt = 0; attempt != 100; ++attempt) {
        const auto path = parent / (".isaac-update-" + std::to_string(random()) + "-" + std::to_string(random()));
#ifdef _WIN32
        if (CreateDirectoryW(path.c_str(), nullptr)) return path;
        require(GetLastError() == ERROR_ALREADY_EXISTS, "Could not create replacement staging directory beside the application.");
#else
        if (mkdir(path.c_str(), 0700) == 0) return path;
        require(errno == EEXIST, "Could not create replacement staging directory beside the application.");
#endif
    }
    throw std::runtime_error("Could not allocate replacement staging directory.");
}

void renameExclusive(const fs::path &from, const fs::path &to) {
#ifdef _WIN32
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH)) return;
    const std::error_code error(GetLastError(), std::system_category());
#elif defined(__APPLE__)
    if (renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0) return;
    const std::error_code error(errno, std::generic_category());
#else
    if (renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0) return;
    const std::error_code error(errno, std::generic_category());
#endif
    throw fs::filesystem_error("Could not move an update path without replacing existing files", from, to, error);
}

class ParentProcess {
public:
    explicit ParentProcess(const fs::path &text) {
        const std::string digits = text.string();
        require(!digits.empty() && digits.find_first_not_of("0123456789") == std::string::npos, "Invalid parent process ID.");
        unsigned long long value = 0;
        try { value = std::stoull(digits); } catch (...) { throw std::runtime_error("Invalid parent process ID."); }
#ifdef _WIN32
        require(value > 1 && value <= std::numeric_limits<DWORD>::max() && value != GetCurrentProcessId(), "Invalid parent process ID.");
        handle_ = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(value));
        require(handle_ != nullptr, "Could not open the application process for update.");
#else
        require(value > 1 && value <= static_cast<unsigned long long>(std::numeric_limits<pid_t>::max()) &&
                value != static_cast<unsigned long long>(getpid()), "Invalid parent process ID.");
        pid_ = static_cast<pid_t>(value);
        require(kill(pid_, 0) == 0, "Could not find the application process for update.");
#endif
    }
    ~ParentProcess() {
#ifdef _WIN32
        if (handle_) CloseHandle(handle_);
#endif
    }
    void wait() const {
#ifdef _WIN32
        require(WaitForSingleObject(handle_, 90000) == WAIT_OBJECT_0, "Timed out waiting for the application to close; the installed app was not changed.");
#else
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (kill(pid_, 0) == 0 || errno != ESRCH) {
            require(std::chrono::steady_clock::now() < deadline, "Timed out waiting for the application to close; the installed app was not changed.");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
#endif
    }
private:
#ifdef _WIN32
    HANDLE handle_ = nullptr;
#else
    pid_t pid_ = 0;
#endif
};

bool acknowledged(const fs::path &path, const FileIdentity &directory) {
    try {
        if (!directory.matches(path.parent_path()) || !fs::is_regular_file(fs::symlink_status(path)) ||
            fs::file_size(path) != std::char_traits<char>::length(startedText)) return false;
        std::ifstream marker(path, std::ios::binary);
        const std::string contents((std::istreambuf_iterator<char>(marker)), std::istreambuf_iterator<char>());
        return contents == startedText && directory.matches(path.parent_path());
    } catch (...) { return false; }
}

bool launch(const fs::path &executable, bool updateFailed, const fs::path &started = {}, const FileIdentity *directory = nullptr) {
    if (!started.empty()) {
        try {
            canonicalPath(started.parent_path());
            if (!directory || !directory->matches(started.parent_path()) || fs::exists(fs::symlink_status(started))) return false;
        } catch (...) { return false; }
    }
#ifdef _WIN32
    std::wstring command = L"\"" + executable.native() + L"\"";
    if (updateFailed) command += L" --update-failed";
    if (!started.empty()) command += L" --update-started \"" + started.native() + L"\"";
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process {};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        executable.parent_path().c_str(), &startup, &process)) return false;
    CloseHandle(process.hThread);
    bool success = started.empty();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!success) {
        if (acknowledged(started, *directory)) { success = true; break; }
        if (WaitForSingleObject(process.hProcess, 100) != WAIT_TIMEOUT || std::chrono::steady_clock::now() >= deadline) break;
    }
    if (!success) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    }
    CloseHandle(process.hProcess);
    return success;
#else
    std::vector<std::string> runtimeVariables;
    for (char **item = environ; *item; ++item) {
        const std::string entry(*item);
        const std::string name = entry.substr(0, entry.find('='));
        if (name == "APPIMAGE" || name == "APPDIR" || name == "ARGV0" || name == "OWD" || name == "LD_LIBRARY_PATH" || name == "LD_PRELOAD" ||
            name.rfind("QT_", 0) == 0 || name.rfind("QML", 0) == 0 ||
            name.rfind("MLT_", 0) == 0 || name.rfind("DYLD_", 0) == 0) runtimeVariables.push_back(name);
    }
    int resultPipe[2];
    if (pipe(resultPipe) != 0) return false;
    if (fcntl(resultPipe[1], F_SETFD, FD_CLOEXEC) != 0) { close(resultPipe[0]); close(resultPipe[1]); return false; }
    const pid_t child = fork();
    if (child == 0) {
        close(resultPipe[0]);
        for (const auto &name : runtimeVariables) unsetenv(name.c_str());
        const char *arguments[] = {executable.c_str(), updateFailed ? "--update-failed" :
                                  (!started.empty() ? "--update-started" : nullptr),
                                  !started.empty() ? started.c_str() : nullptr, nullptr};
        execv(executable.c_str(), const_cast<char *const *>(arguments));
        const int error = errno;
        const ssize_t ignored = write(resultPipe[1], &error, sizeof(error));
        (void)ignored;
        _exit(127);
    }
    close(resultPipe[1]);
    if (child < 0) { close(resultPipe[0]); return false; }
    int error = 0;
    ssize_t count;
    do { count = read(resultPipe[0], &error, sizeof(error)); } while (count < 0 && errno == EINTR);
    close(resultPipe[0]);
    if (count != 0) { while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {} return false; }
    if (started.empty()) return true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        if (acknowledged(started, *directory)) return true;
        const pid_t result = waitpid(child, nullptr, WNOHANG);
        if (result == child || (result < 0 && errno != EINTR)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    kill(child, SIGTERM);
    const auto stopDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < stopDeadline) {
        const pid_t result = waitpid(child, nullptr, WNOHANG);
        if (result == child || (result < 0 && errno != EINTR)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    kill(child, SIGKILL);
    while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
    return false;
#endif
}

int install(const std::vector<fs::path> &arguments) {
    fs::path prepared, stage, target;
    std::optional<FileIdentity> preparedIdentity, stageIdentity, originalIdentity, originalExecutableIdentity, replacementIdentity;
    bool safeDiagnostic = false;
    bool originalMoved = false;
    bool replacementMoved = false;
    bool parentExited = false;
#ifdef _WIN32
    Manifest preservedUserFiles;
#endif
    try {
        require(arguments.size() == 6 && arguments[1] == "--install", "Usage: IsaacVideoEditorUpdater --install parentPid preparedDir targetPath relativeExecutable");
        prepared = arguments[3];
        // Never write an error through an unvalidated path supplied on the command line.
        validatePreparedDirectory(prepared);
        preparedIdentity.emplace(prepared);
        safeDiagnostic = true;
        validatePrepared(prepared);
        ParentProcess parent(arguments[2]);
        target = arguments[4];
        const auto relative = arguments[5];
#ifdef __APPLE__
        require(relative == "Contents/MacOS/IsaacVideoEditor" && target.extension() == ".app", "Expected an IsaacVideoEditor application bundle.");
#elif defined(_WIN32)
        require(relative == L"IsaacVideoEditor.exe", "Expected the portable IsaacVideoEditor executable.");
#else
        require(relative == "." && target.extension() == ".AppImage", "Expected an IsaacVideoEditor AppImage.");
#endif
        canonicalPath(target);
        require(target.parent_path() != target.root_path(), "Cannot replace an application directly in the filesystem root.");
        const auto helper = helperPath();
        validatePreparedDirectory(helper.parent_path());
        require(!within(prepared, target) && !within(target, prepared) && !within(helper, prepared) && !within(helper, target),
                "Update source, installation, and helper must be separate paths.");
        const auto payload = prepared / "payload";
        validateApp(target, relative);
        validateApp(payload, relative);
        require(!fs::equivalent(mainExecutable(target, relative), mainExecutable(payload, relative)),
                "The update executable must not alias the installed application.");
        originalIdentity.emplace(target);
        originalExecutableIdentity.emplace(mainExecutable(target, relative));
        const auto originalTime = fs::last_write_time(mainExecutable(target, relative));
        stage = makeStage(target.parent_path());
        stageIdentity.emplace(stage);
        fs::copy(payload, stage / "payload", fs::copy_options::recursive | fs::copy_options::copy_symlinks);
#ifdef _WIN32
        preserveUserFiles(target, stage / "payload", preservedUserFiles);
#endif
        validateApp(stage / "payload", relative);
        replacementIdentity.emplace(stage / "payload");
        writeMarker(prepared / "armed", readyText);
        parent.wait();
        parentExited = true;
        canonicalPath(prepared);
        canonicalPath(stage);
        require(preparedIdentity->matches(prepared) && stageIdentity->matches(stage), "An update working directory changed while waiting.");
        validateApp(target, relative);
        require(originalIdentity->matches(target) && originalExecutableIdentity->matches(mainExecutable(target, relative)) &&
                fs::last_write_time(mainExecutable(target, relative)) == originalTime, "The installed application changed while waiting to update.");
#ifdef _WIN32
        // Closing the app can save projects after the initial staging copy.
        preserveUserFiles(target, stage / "payload", preservedUserFiles);
#endif
        validateApp(stage / "payload", relative);
        require(replacementIdentity->matches(stage / "payload"), "The staged application changed while waiting to update.");
        renameExclusive(target, stage / "previous");
        originalMoved = true;
        renameExclusive(stage / "payload", target);
        replacementMoved = true;
        require(launch(mainExecutable(target, relative), false, prepared / "started", &*preparedIdentity),
                "Could not launch the updated application or confirm successful startup.");
        originalMoved = false; // The replacement is running; cleanup must never roll it back.
        std::error_code cleanup;
        if (stageIdentity->matches(stage)) fs::remove_all(stage, cleanup);
        if (preparedIdentity->matches(prepared)) fs::remove_all(prepared, cleanup);
        return 0;
    } catch (const std::exception &error) {
        std::string message = error.what();
        if (originalMoved) {
            try {
                canonicalPath(stage);
                require(stageIdentity->matches(stage) && originalIdentity->matches(stage / "previous"), "The original backup path changed during update.");
                if (replacementMoved) {
                    // Preserve the failed replacement without deleting anything at the installation path.
                    require(replacementIdentity->matches(target), "The replacement app path changed during update.");
                    renameExclusive(target, stage / "failed");
                }
                require(!fs::exists(fs::symlink_status(target)), "Installation path is occupied; original app remains in " + (stage / "previous").u8string());
                renameExclusive(stage / "previous", target);
                originalMoved = false;
                if (!launch(mainExecutable(target, arguments[5]), true)) message += " The original app was restored but could not be relaunched.";
            } catch (const std::exception &recovery) {
                message += " Recovery needs attention: ";
                message += recovery.what();
                message += " Original backup: " + (stage / "previous").u8string();
            }
        } else if (parentExited && !replacementMoved && originalIdentity && originalIdentity->matches(target) &&
            originalExecutableIdentity && originalExecutableIdentity->matches(mainExecutable(target, arguments[5]))) {
            try {
                validateApp(target, arguments[5]);
                if (!launch(mainExecutable(target, arguments[5]), true)) message += " The unchanged original app could not be relaunched.";
            } catch (const std::exception &recovery) { message += " Original app recovery failed: " + std::string(recovery.what()); }
        }
        if (safeDiagnostic) {
            try {
                canonicalPath(prepared);
                if (preparedIdentity->matches(prepared)) writeMarker(prepared / "error.txt", message + "\n");
            } catch (...) {}
        }
        if (stageIdentity && stageIdentity->matches(stage) && !originalMoved) {
            std::error_code cleanup;
            fs::remove_all(stage, cleanup);
        }
        return 1;
    }
}
}

#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    LPWSTR *values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!values) return 1;
    std::vector<fs::path> arguments(values, values + count);
    LocalFree(values);
    return install(arguments);
}
#else
int main(int argc, char **argv) {
    return install(std::vector<fs::path>(argv, argv + argc));
}
#endif
