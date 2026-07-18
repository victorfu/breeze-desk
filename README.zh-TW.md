<p align="center">
  <img src="resources/icons/breezedesk.png" alt="BreezeDesk logo" width="128">
</p>

<h1 align="center">BreezeDesk</h1>

<p align="center"><b>完全離線的長錄音轉錄工作站 — 音訊與逐字稿永遠留在你的電腦上。</b></p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-2e7d32" alt="License: MIT"></a>
  <img src="https://img.shields.io/badge/platform-macOS%2014%2B%20%7C%20Windows%2010%2F11-4c7fb0" alt="Platform: macOS 14+ | Windows 10/11">
  <img src="https://img.shields.io/badge/Qt-6.8%2B-41cd52" alt="Qt 6.8+">
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599c" alt="C++17">
</p>

<p align="center"><a href="README.md">English</a> | <b>繁體中文</b></p>

---

## 🚀 BreezeDesk 是什麼?

BreezeDesk 是完全離線的長錄音轉錄與逐字稿管理桌面程式。它可匯入音訊/影片、以麥克風錄音,
使用 LGPL FFmpeg sidecar 正規化媒體,再交給獨立的原生 Qt Worker;Worker 直接連結 whisper.cpp
C API,不執行 `whisper-cli`,也沒有雲端 ASR、Python runtime 或遙測。音訊與逐字稿不會離開
電腦,唯一可能的網路活動是使用者主動下載模型,以及選擇啟用的更新檢查。

正式平台為 macOS 14+ Apple Silicon 與 Windows 10 22H2+/Windows 11 x64;預設針對台灣華語、
中文夾英文,以及軟體工程/產品會議。

## ✨ 主要特色

- 🛡️ **崩潰隔離的轉錄引擎** — 轉錄在獨立的 `breezedesk-asr-worker` 行程中執行,原生層崩潰
  不會拖垮主程式或你的資料庫。
- ⏳ **為數小時的長錄音而生** — VAD 對齊分塊、partial segments 即時保存、取消、重試、中斷後
  resume,以及確定性的重疊去重。
- 🗂️ **完整的逐字稿 Library** — SQLite 儲存、revisions 與編輯、標籤、搜尋、Trash、Glossary
  profiles、prompt token 預算與可稽核的 alias replacement。
- 🎨 **精緻的 QML 介面** — 語意化 Design Tokens、System/Light/Dark 主題、中英文介面、鍵盤
  操作、波形與播放器同步、無障礙控制項。
- 🧠 **可驗證的模型管理** — Q5/Q8/自訂 GGML 模型,下載可暫停續傳,載入前驗證真實位元組
  大小與 SHA-256。
- ⌨️ **可腳本化的 CLI** — `breezedesk-cli` 與 GUI 共用資料庫、模型、Worker 與匯出器,提供
  穩定 exit codes 與 `--json` 輸出,便於自動化。
- 📤 **六種匯出格式** — TXT、Markdown、SRT、VTT、JSON、CSV;原子寫入確保匯出失敗不會毀損
  既有的有效檔案。
- 📦 **原生安裝包** — macOS 與 Windows 套件,可選用 Sparkle/WinSparkle 更新檢查。

## 🎯 適合誰?

- 在台灣以華語開會、對話中自然夾雜英文工程與產品術語的團隊。
- 需要轉錄訪談、課程、Podcast 或數小時錄音,卻不想把檔案上傳到任何雲端服務的人。
- 錄音內容機密、必須完全留在本機的工作環境。

## 💻 技術架構

| 層級 | 實作 |
| --- | --- |
| UI | Qt 6.8+ Quick/QML(Release 基準 6.10.1)搭配 C++17 核心 |
| ASR | 固定 immutable commit 的 whisper.cpp,由 Worker 行程以函式庫方式連結 |
| 加速 | macOS 使用 Metal + Accelerate;Windows 提供 Vulkan、CUDA 或 CPU Worker 版本 |
| 媒體 | LGPL FFmpeg sidecar 負責探測與正規化;Qt Multimedia 負責錄音與播放 |
| 儲存 | SQLite 保存 Library、jobs、chunk checkpoints、revisions、Glossary 與稽核資料 |
| 更新 | 可選用的 Sparkle(macOS)/WinSparkle(Windows)更新檢查 |

## 🧠 模型

模型不放入 repository 或 installer。Models 頁(或 `breezedesk-cli models download`)只會從
immutable revision 下載,支援暫停/續傳,並在檔案交給 Worker 前驗證真實位元組大小與
SHA-256;模型授權與來源在程式內隨時可見,也可匯入本機的 whisper.cpp GGML `.bin` 作為
自訂模型。

| 模型 | 量化 | 大小 | 用途 |
| --- | --- | ---: | --- |
| Breeze-ASR-25 Q5(建議) | Q5_K | 約 1.0 GB | Apple Silicon 與 8 GB 記憶體機器的預設選擇 |
| Breeze-ASR-25 Q8 | Q8_0 | 約 1.6 GB | 記憶體較充裕時的高品質選項 |
| Silero VAD 6.2.0 | F32 | <1 MB | 讓長錄音分塊邊界落在靜音處 |

## 🛠 開發者指南

### macOS 建置

安裝 CMake、Ninja、Qt 6.8+(Release 基準為 6.10.1)、Xcode Command Line Tools,以及相容
LGPL 的 FFmpeg 開發 sidecar,然後執行:

```sh
./scripts/build.sh
./scripts/run-tests.sh
./scripts/build-and-run.sh
```

開發時可指定本機、已固定 revision 的 whisper.cpp:

```sh
BREEZEDESK_WHISPER_CPP_SOURCE_DIR=/path/to/whisper.cpp ./scripts/build.sh
```

### Windows 建置

使用 Visual Studio 2022 Build Tools、Windows SDK、CMake、Ninja 與 Qt 6.8+:

```bat
scripts\build.bat
scripts\run-tests.bat
scripts\build-and-run.bat
```

一般 Windows 建置預設使用 CPU 版 whisper.cpp Worker，因此不需要額外安裝 Vulkan 或 CUDA
SDK。開發時請一律使用 `scripts\build-and-run.bat` 啟動；CMake 的原始輸出並不是可獨立執行
的完整目錄，可能會因缺少 `Qt6Networkd.dll` 等 Debug Qt DLL 而失敗。此腳本會找出目前 Qt
kit 對應的 `windeployqt.exe`，並部署 GUI、CLI 與 ASR Worker 合併後所需的完整 runtime。
若只要部署而不啟動，請執行：

```bat
scripts\deploy-debug.bat
```

若 Qt 安裝在非標準位置，請先將 `BREEZEDESK_WINDEPLOYQT` 設為相同 Qt kit 的
`windeployqt.exe`。請勿從其他 build 或 Qt 版本手動複製部分 DLL。

`debug-runtime-off` preset 只供通訊協定與 UI 測試使用，刻意不具備轉錄能力；請勿將該
目錄的執行檔用於一般開發或散佈。

### 手動 CMake

```sh
cmake -S . -B build/manual -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel
ctest --test-dir build/manual --output-on-failure
```

`BREEZEDESK_ENABLE_WHISPER=OFF` 可在不建置原生 runtime 的情況下執行協定/領域測試;正式
安裝包一律啟用。GGML 後端是 configure 時期的選擇,因此 Windows 安裝包分別建置 Vulkan 與
CUDA 兩種 Worker。

### 發行安裝包

本機未簽章套件與 CI 使用同一組部署腳本:

```sh
export BREEZEDESK_FFMPEG_DIR="$(packaging/macos/build-ffmpeg-lgpl.sh)"
packaging/macos/package.sh
```

```powershell
$env:BREEZEDESK_FFMPEG_DIR = packaging/windows/build-ffmpeg-lgpl.ps1 | Select-Object -Last 1
cmd /c packaging\windows\package.bat Universal --msix
cmd /c packaging\windows\package.bat CUDA
```

簽章完全由環境變數控制;本機不提供憑證仍可建立 unsigned package。簽章、notarization、
update feed 與產物名稱請見[發行打包文件](docs/developer/release-packaging.md)。

## 📖 文件

- **使用說明** — 從[快速開始](docs/user/getting-started.md)出發,涵蓋
  [匯入媒體](docs/user/importing-media.md)、[錄音](docs/user/recording.md)、
  [轉錄](docs/user/transcription.md)、[編輯](docs/user/editing.md)、
  [Glossary](docs/user/glossary.md)、[匯出](docs/user/exporting.md)、
  [模型](docs/user/models.md)、[隱私](docs/user/privacy.md)、
  [疑難排解](docs/user/troubleshooting.md)與 [CLI](docs/user/cli.md)。
- **開發者文件** — 先讀[架構](docs/developer/architecture.md);建置、測試、資料庫、Worker
  協定與打包文件在 [docs/developer](docs/developer),設計決策紀錄在 [docs/adr](docs/adr)。
- **專案** — [CHANGELOG](CHANGELOG.md)、[CONTRIBUTING](CONTRIBUTING.md)、
  [SECURITY](SECURITY.md)。

## 📄 授權

BreezeDesk 採 [MIT 授權](LICENSE)。Qt 以動態連結部署並遵循 LGPL;whisper.cpp 為 MIT;
Breeze-ASR-25 權重為 Apache-2.0;發行用 FFmpeg 明確停用 GPL/nonfree 元件。完整第三方授權
請見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
