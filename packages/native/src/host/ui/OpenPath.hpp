#pragma once

#include <string>

#if defined(_WIN32)
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <sys/wait.h>
#  include <unistd.h>
#endif

/** Reveal a file or folder in the OS file manager.
 *
 *  Both hosts had their own copy, and both built a SHELL COMMAND by string concatenation with only
 *  double-quotes around the path:
 *
 *      std::system(("xdg-open \"" + path + "\" &").c_str());
 *
 *  Inside double quotes `sh` still expands `$(...)` and backticks, so a path containing either escapes
 *  the quoting and runs. Today the only caller is Settings -> Open Settings Folder, which passes the
 *  host's own config dir, so nothing reachable is attacker-controlled - but the seam is generic, the
 *  next caller to pass a project or ROM path inherits it silently, and there is no reason to keep a
 *  shell in the picture at all. This takes the path as an ARGUMENT, so no quoting exists to escape.
 *
 *  It also settles a divergence: the plugin backgrounded the macOS call with `&`, SDL did not, so an
 *  "Open Settings Folder" on a Mac standalone blocked the UI thread in waitpid while LaunchServices
 *  started. Backgrounding is correct and is what both do now - via a double fork, so the grandchild is
 *  reparented and no zombie is left for a host that never reaps.
 */
inline void openPathInFileManager(const std::string& path) {
    if (path.empty()) return;

#if defined(_WIN32)
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (n <= 0) return;
    std::wstring wide(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), n);
    // ShellExecuteW takes the path as a parameter, so there is no command line to quote.
    ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
#  if defined(__APPLE__)
    const char* opener = "open";
#  else
    const char* opener = "xdg-open";
#  endif
    const pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        // Double fork: the middle child exits at once so the parent's waitpid returns immediately and the
        // grandchild - the actual opener - is reparented rather than left as a zombie.
        if (fork() == 0) {
            execlp(opener, opener, path.c_str(), static_cast<char*>(nullptr));
            _exit(127); // exec failed; nothing sensible to report from here
        }
        _exit(0);
    }
    int status = 0;
    (void)waitpid(pid, &status, 0);
#endif
}
