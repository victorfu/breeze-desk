# BreezeDesk AI Agent Instructions

## Mandatory Windows build and test preflight

Every AI agent that configures, builds, or tests BreezeDesk on Windows must use an x64 Visual Studio
development environment. A fresh shell or tool invocation must not rely on environment variables inherited
from an earlier invocation.

1. Run `call scripts\setup-msvc.bat` in the same `cmd.exe` process that will invoke CMake, the compiler,
   CTest, or a test executable.
2. Require `where cl.exe` to succeed. `setup-msvc.bat` also verifies that both the host and target
   architecture are x64. Stop immediately if this preflight fails.
3. Prefer `scripts\build.bat` and `scripts\run-tests.bat` for the default Debug tree; both repeat the
   mandatory setup themselves.
4. Before launching CTest or any Qt test executable directly, make the matching Debug Qt runtime and
   Windows platform plugin visible in that same process. For a fully built tree, the preferred approach is:

   ```bat
   call scripts\setup-msvc.bat || exit /b 1
   set "BREEZEDESK_AGENT_BUILD=%CD%\build\debug"
   call scripts\deploy-debug.bat "%BREEZEDESK_AGENT_BUILD%" || exit /b 1
   set "PATH=%BREEZEDESK_AGENT_BUILD%;%BREEZEDESK_AGENT_BUILD%\platforms;%PATH%"
   set "QT_PLUGIN_PATH=%BREEZEDESK_AGENT_BUILD%"
   set "QT_QPA_PLATFORM_PLUGIN_PATH=%BREEZEDESK_AGENT_BUILD%\platforms"
   set "QT_QPA_PLATFORM=windows"
   where cl.exe || exit /b 1
   where Qt6Cored.dll || exit /b 1
   where Qt6Testd.dll || exit /b 1
   where qwindowsd.dll || exit /b 1
   ```

   An alternate build tree may replace `build\debug`, but all paths and DLLs must come from the same Qt
   kit and build configuration.
5. Do not start a test executable when any preflight check fails. In particular, never launch it merely to
   see whether Windows reports a missing DLL; that creates blocking system-error dialogs.
6. When composing one `cmd.exe /d /v:on /c` command, append the existing path with `!PATH!`, not `%PATH%`,
   so expansion occurs after `setup-msvc.bat` has initialized Visual Studio.
7. Initialize the Visual Studio environment once in a parent process and let parallel tests inherit it. Do
   not call `setup-msvc.bat` concurrently from multiple child shells.
8. Use the `windows` Qt platform for deployed Debug tests. Do not force `offscreen` unless the matching
   offscreen platform plugin has been explicitly deployed and verified.

If a user-owned BreezeDesk process is locking the normal Debug binaries, use a separate build tree. Do not
terminate the user's application without permission.
