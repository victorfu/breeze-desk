# BreezeDesk

BreezeDesk 是完全離線的長錄音轉錄與逐字稿管理桌面程式。它可匯入音訊／影片、以麥克風
錄音，使用 LGPL FFmpeg sidecar 正規化媒體，再交給獨立的原生 Qt Worker；Worker 直接連結
whisper.cpp C API，不執行 `whisper-cli`。音訊與逐字稿不會離開電腦，唯一可能的網路活動是
使用者主動下載模型，以及選擇啟用的更新檢查。

正式平台為 macOS 14+ Apple Silicon，以及 Windows 10 22H2+/Windows 11 x64；預設針對台灣
華語、中文夾英文，以及軟體工程／產品會議。

## 主要功能

- GUI 崩潰隔離的 `breezedesk-asr-worker`，不含雲端 ASR、Python runtime 或遙測。
- 長錄音 VAD 分塊、即時保存 partial segments、取消、重試、Interrupted resume 與重疊去重。
- SQLite Library、逐字稿 revisions、搜尋、標籤、Trash、Glossary profiles、prompt token 預算
  與可撤銷的 alias replacement audit。
- QML Design Tokens、System/Light/Dark、中英文、鍵盤操作、波形與播放器同步。
- Q5/Q8/自訂 GGML 模型、可恢復與 checksum 驗證的下載、CLI、多格式匯出與原生安裝包。

## macOS 建置

安裝 CMake、Ninja、Qt 6.8+（Release 基準為 6.10.1）及 Xcode Command Line Tools：

```sh
./scripts/build.sh
./scripts/run-tests.sh
./scripts/build-and-run.sh
```

可指定本機、已固定 revision 的 whisper.cpp：

```sh
BREEZEDESK_WHISPER_CPP_SOURCE_DIR=/path/to/whisper.cpp ./scripts/build.sh
```

## Windows 建置

使用 Visual Studio 2022 Build Tools、Windows SDK、CMake、Ninja 與 Qt 6.8+：

```bat
scripts\build.bat
scripts\run-tests.bat
scripts\build-and-run.bat
```

## 手動 CMake

```sh
cmake -S . -B build/manual -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel
ctest --test-dir build/manual --output-on-failure
```

模型不放入 repository 或 installer。Models 頁只會從 immutable revision 下載，並在載入前
驗證真實大小與 SHA-256。BreezeDesk 採 MIT；Qt 動態部署遵循 LGPL；whisper.cpp 為 MIT；
發行用 FFmpeg 明確停用 GPL/nonfree。詳細授權見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 發行安裝包

本機未簽章套件與 CI 使用同一組部署腳本：

```sh
export BREEZEDESK_FFMPEG_DIR="$(packaging/macos/build-ffmpeg-lgpl.sh)"
packaging/macos/package.sh
```

```powershell
$env:BREEZEDESK_FFMPEG_DIR = packaging/windows/build-ffmpeg-lgpl.ps1 | Select-Object -Last 1
cmd /c packaging\windows\package.bat Universal --msix
cmd /c packaging\windows\package.bat CUDA
```

簽章完全由環境變數控制；本機不提供憑證仍可建立 unsigned package。簽章、notarization、
update feed 與產物名稱請見 [發行打包文件](docs/developer/release-packaging.md)。

使用說明從 [快速開始](docs/user/getting-started.md) 開始；開發者先讀
[架構](docs/developer/architecture.md)。
