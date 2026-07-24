# UX 改進 Spec:借鑑 MacWhisper / Notta 的五項改進

日期:2026-07-24
狀態:待確認
來源:MacWhisper(macwhisper.com、docs.macwhisper.com)與 Notta(notta.ai)的 UI/UX 競品分析

## 背景與目標

競品分析找出五項對 BreezeDesk 價值最高、且不改變核心行為的 UX 改進。
本 spec 逐項定義行為、落點檔案、測試契約與驗收條件。

程式碼現況考察後的重要修正:三項基礎設施**已經存在**,各項範圍比表面看起來小 ——

- 視窗級拖放已可用:[Main.qml:131](../../../src/qml/Main.qml) 的 `DropArea` 已呼叫 `vm.importUrls(drop.urls)`,缺的只是**拖曳中的視覺回饋**。
- 播放跟隨已可用:`TranscriptViewModel.activePlaybackIndex` + RecordingPage 的 autoScroll 已實作,S3 純屬視覺層。
- 閱讀模式的資料層已可用:`TranscriptViewModel::fullText()`([TranscriptViewModel.h:74](../../../include/breezedesk/ui/TranscriptViewModel.h))已存在。

## 範圍

| # | 項目 | 性質 | 預估規模 |
|---|---|---|---|
| S1 | 拖放視覺回饋 + 空狀態拖放目標 | 純 QML | 小 |
| S2 | 佇列彙總列(含預估剩餘時間) | C++ VM + QML | 中 |
| S3 | 逐字稿版式節奏(Notta 式) | 純 QML | 小 |
| S4 | 逐字稿閱讀模式 | QML + VM 重用 | 小-中 |
| S5 | 隱私提示 + 快捷鍵一覽 | QML + 小重構 | 小 |

**非目標**:speaker diarization 與色票、多格式匯出 chips、會議偵測、watched folders、逐字稿搜尋高亮(S4 v1 不含)。

## 全域約束(每項皆適用)

1. 顏色/間距/字級一律經 `SemanticTokens`;components/controls/dialogs/pages 禁用 `PrimitiveTokens`(lint 測試 `ordinaryComponentsDoNotUsePrimitiveTokens` 強制)。
2. 既有 166 個 `objectName` 不得改名;新增互動元素一律給穩定 `objectName` 並列入本 spec。
3. 所有新字串用 `qsTr()`,完成後跑 `scripts/update-translations.sh` 補 zh_TW。
4. 每一項完成時:`cmake --build --preset debug-runtime-off` 0 warning、`QT_QPA_PLATFORM=offscreen ctest` 26 套件全綠。
5. 動效使用既有 motion token(`animationFast/Normal/Slow` + `easeStandard/Exit`)。

---

## S1 拖放視覺回饋 + 空狀態拖放目標

### 現況
- [Main.qml:131](../../../src/qml/Main.qml):`DropArea` 只有 `onDropped`,拖曳懸停時畫面零回饋。
- [LibraryPage.qml:152](../../../src/qml/pages/LibraryPage.qml):EmptyState 已有「Choose Files」按鈕與隱私文案,但看不出可以拖放。

### 行為規格
**S1a 全視窗拖曳覆蓋層**
- 當 `DropArea.containsDrag && drag.hasUrls` 時,顯示全視窗覆蓋層:`scrim` 底 + 中央一張 `surfaceRaised` 圓角卡(`radiusLg`、`AppShadow level 3`),內容:file-input 圖示 + 主文字「放開以匯入」+ 次行顯示檔案數(`qsTr("%n 個檔案", "", drag.urls.length)`)。
- 進出場:opacity fade,`animationFast` + `easeStandard/easeExit`。
- 覆蓋層 `visible` 僅由 containsDrag 驅動,不可攔截事件(`enabled: false` 的純視覺層,放在 DropArea 之上、z 高於頁面)。
- **不得干擾內部拖曳**:佇列重排的拖曳(JobProgress 內部 DropArea)沒有 urls;覆蓋層條件必須含 `drag.hasUrls`。

**S1b 空狀態升級為拖放目標**
- EmptyState 新增可選屬性 `property bool dropTarget: false`;為 true 時圖示與文案外包一層虛線框(1px `borderStrong`、`radiusLg`、內距 `spacingXl`)。QML 無原生虛線邊框,用 `Shape`/`ShapePath`(`strokeStyle: ShapePath.DashLine`)或 Canvas 實作,顏色走 token。
- LibraryPage 的空狀態(非搜尋情境)設 `dropTarget: true`,description 追加一行快捷鍵提示:macOS 顯示「或按 ⌘O 選擇檔案」,其他平台「或按 Ctrl+O 選擇檔案」(以 `Qt.platform.os === "osx"` 分支)。

### 新 objectName
- `importDropOverlay`(覆蓋層根)
- `importDropOverlayCard`(中央卡)
- `emptyStateDropFrame`(虛線框)

### 測試
- tst_QmlSmoke 新 case:載入 Main.qml,`importDropOverlay` 存在且預設 `visible === false`。
- EmptyState fixture:`dropTarget: true` 時 `emptyStateDropFrame` 可見,false 時不存在/不可見。
- (拖曳模擬在 offscreen 環境不可靠,不做 drag 整合測試;`importUrls` 已有既有覆蓋。)

### 驗收
拖檔案進視窗任一處 → 覆蓋層浮現並顯示數量;放開 → 匯入照舊;佇列內部重排拖曳不觸發覆蓋層;Library 空狀態能看出「可拖放 + 可按快捷鍵」兩條路徑。

---

## S2 佇列彙總列(含預估剩餘時間)

### 現況
- [QueuePage.qml](../../../src/qml/pages/QueuePage.qml):PageHeader + EmptyState + ListView,無彙總資訊。
- [JobListModel.h](../../../include/breezedesk/ui/JobListModel.h) 已有 `ProgressRole/StateRole/QueuePositionRole/CurrentChunkRole/TotalChunksRole`。
- 媒體時長在 `RecordingListModel::durationMs`;job 目前不帶時長 —— VM 需經 recordingId 從 repository 解析(JobQueueViewModel 建構時是否已持有 repository 參照,實作時確認;若無,由 ApplicationViewModel 注入)。

### 行為規格
**S2a JobQueueViewModel 新增唯讀屬性**
```
Q_PROPERTY(int waitingCount ...)        // 排隊中(不含運行中)
Q_PROPERTY(qint64 queuedDurationMs ...) // 運行中剩餘媒體時長 + 所有排隊 job 的媒體總時長
Q_PROPERTY(qint64 etaMs ...)            // 預估剩餘,未知時 -1
```

**S2b ETA 演算法**(在 VM 內、可單元測試的純函式)
- 即時倍率 `factor = 已處理媒體毫秒 / 實際牆鐘毫秒`,以 EWMA(α = 0.3)平滑;樣本不足(牆鐘 < 10s)視為未知。
- `etaMs = queuedDurationMs / factor`;`pauseAfterCurrent` 開啟時只計運行中 job 的剩餘。
- 任何 job 缺媒體時長 → `queuedDurationMs` 仍加總已知者,ETA 照算但 UI 加「約」;全部未知 → etaMs = -1。
- 運行中 job 的「已處理媒體毫秒」以 `progress × durationMs` 近似(progress 已存在)。

**S2c UI**
- PageHeader 之下、列表之上插入一列 caption 級彙總(`textMuted`,`spacingSm` 上下距):
  `「3 個等待中 · 總長 42:18 · 預估剩餘約 18 分鐘」`
- 顯示規則:佇列空 → 整列隱藏;etaMs < 0 → 該欄顯示「預估中…」;etaMs < 60s → 「不到 1 分鐘」;其餘以分鐘取整。`pauseAfterCurrent` 開啟 → 尾註「(當前作業後暫停)」。
- 時長格式重用 `UiText.timecode`。

### 新 objectName
- `queueSummaryRow`、`queueSummaryEta`

### 測試
- 新 C++ 測試(tests/Qml/tst_JobQueueViewModel 擴充或新檔):EWMA 收斂、樣本不足回 -1、pauseAfterCurrent 只計當前、缺時長的部分加總。
- tst_QmlSmoke:佇列有 fixture job 時 `queueSummaryRow` 可見且含等待數;清空後隱藏。

### 驗收
排入 2 個以上 job 時,彙總列即時更新;ETA 在轉錄開始約 10 秒後從「預估中…」轉為數字,且隨進度收斂而不跳動(EWMA 平滑)。

---

## S3 逐字稿版式節奏(Notta 式)

### 現況
[SegmentEditor.qml](../../../src/qml/components/SegmentEditor.qml) 已有左側固定時間欄(`timeColumnWidth ≥ 80`)+ 右側文字;選取態為 `accentMuted` 整列填色(測試有斷言,不可改);播放跟隨高亮與 autoScroll 已存在([RecordingPage.qml:749](../../../src/qml/pages/RecordingPage.qml))。

### 行為規格(全為視覺層,不動結構與資料)
1. **行距**:段落文字加 `lineHeight: SemanticTokens.lineHeightNormal`(1.5)+ `ProportionalHeight`。
2. **時間欄層級**:起始 TimeCode 維持 `text` 色;結束 TimeCode 降為 `textMuted` + `captionSize`(若現況已如此則不動)。
3. **hover 態**:非選取列 hover 時底色 `surfaceHover`,`animationFast` 過渡(現況 transparent → 無 hover 回饋)。
4. **播放列標示**:`activePlaybackIndex` 對應列在左緣加 3px 圓角 accent 豎條(仿 Toast severity strip;選取填色照舊,兩者可疊加)。
5. **段間距**:RecordingPage 的 segment ListView `spacing` 統一為 `spacingXs`(實作時核對現值,只在不一致時調整)。

### 新 objectName
- `segmentPlaybackRail`

### 測試
- tst_QmlSmoke 既有 SegmentEditor 斷言(選取色、radius 0 等)必須維持綠燈。
- 新增:設 `activePlaybackIndex` 後 `segmentPlaybackRail` 可見且色值等於 `SemanticTokens.accent`。

### 驗收
播放時能一眼區分「正在唸的列」(accent 豎條)與「我點選的列」(accentMuted 填色);長段文字行距舒展;26 套件全綠。

---

## S4 逐字稿閱讀模式

### 現況
`fullText()` 已存在;RecordingPage 逐字稿區已有工具列(搜尋、低信心過濾等)。

### 行為規格
- 逐字稿工具列加一組雙態切換「編輯｜閱讀」:兩顆 AppButton 樣式的互斥 toggle(選中者 `primary`),不新造 segmented control。
- **閱讀檢視**:唯讀 `TextArea`(可選字、⌘A/⌘C),內容 `transcript.fullText()`;字級 `bodySize`、`lineHeight 1.6`、內容最大寬 `68 * bodySize * 0.6` 近似 68ch 並水平置中(閱讀專注版式);背景 `surface`。
- **內容更新**:進入閱讀模式時取一次 `fullText()`;停留期間監聽 `segmentCountChanged` 與 `dirtyChanged` 再取(轉錄進行中新段落會流入)。
- 模式為 session 內狀態(RecordingPage property),不持久化;預設「編輯」。
- 編輯操作(split/merge 等快捷鍵)在閱讀模式一律不可觸發:切換時將 segment 列表 `visible: false`,快捷鍵 handler 檢查模式。
- 搜尋框在閱讀模式 v1 直接停用(disabled + tooltip「閱讀模式不支援搜尋」),不做高亮。

### 新 objectName
- `transcriptViewModeSwitch`、`transcriptViewModeEdit`、`transcriptViewModeRead`、`transcriptReadingView`

### 測試
- tst_QmlSmoke:切到閱讀 → `transcriptReadingView` 可見、其 `text` 等於 `fullText()`、segment 列表隱藏;切回編輯 → 反向成立;fixture 編輯一段文字後閱讀內容跟著變。

### 驗收
「轉完拿去貼」流程:開啟錄音 → 點「閱讀」→ ⌘A ⌘C 貼到外部,格式為乾淨連續文字;來回切換不丟編輯狀態、不重置列表捲動位置。

---

## S5 隱私提示 + 快捷鍵一覽

### S5a 隱私提示(佇列頁)
- 現況:隱私文案只在 Library 空狀態出現;等待轉錄的人看不到。
- 規格:QueuePage 的 PageHeader 之下加一行 caption/`textMuted`(S5a 先於 S2 實作;S2 落地時將此行併入彙總列尾端,objectName 不變):
  `qsTr("所有轉錄皆在本機完成,檔案不會離開你的電腦。")`
  僅在佇列非空時顯示(空佇列時 Library 空狀態已承擔此敘事)。
- objectName:`queuePrivacyNote`。
- 測試:佇列有 job 時可見。

### S5b 快捷鍵一覽(單一事實來源重構)
- 現況:[ShortcutRegistry.qml](../../../src/qml/controls/ShortcutRegistry.qml) 硬編 12 個 `Shortcut`,無任何 UI 呈現。
- 規格:
  1. Registry 重構為資料驅動:`readonly property var entries` 陣列,每項 `{ sequences: [...], macSequences: [...], labelKey, signalName }`;以 `Instantiator` + `Shortcut` 生成,行為與現況完全等價(**現有 12 組鍵位一個不變**)。
  2. 新增 `Shortcut`:`Ctrl+/` 與 `Meta+/` → `shortcutsTriggered` 信號。
  3. 新對話框 `dialogs/ShortcutsDialog.qml`(基於既有 Dialog):標題「鍵盤快捷鍵」,逐列「功能名稱 ····· 鍵帽」;鍵帽用新迷你元件 `components/KeyCap.qml`(`surfaceMuted` 底、1px `border`、`radiusSm`、`fixedFontFamily` caption 字);macOS 顯示 ⌘/⇧ 符號,其他平台顯示 Ctrl+Shift 文字。
  4. 入口:快捷鍵 `⌘/`;SettingsPage 關於區塊加一顆 AppLinkButton「鍵盤快捷鍵…」。
- objectName:`shortcutsDialog`、`shortcutsDialogList`。
- 測試:
  - registry 的 `entries.length` 與 Instantiator 生成的 Shortcut 數一致(防資料/行為漂移)。
  - 既有快捷鍵行為測試(若有)維持綠燈;⌘O 等代表性鍵位在重構後仍觸發對應信號。
  - 開啟 `shortcutsDialog` 後列數 === `entries.length + 1`(含 ⌘/ 自身)。

### 驗收
按 `⌘/` 彈出完整快捷鍵表,內容與實際行為永遠同步(同一份資料);重構前後所有既有快捷鍵行為不變。

---

## 建議實作順序

| 順序 | 項目 | 理由 |
|---|---|---|
| 1 | S5a | 一行字,先拿信任分 |
| 2 | S1 | 純 QML、首次體驗回報最大 |
| 3 | S3 | 純視覺,與 S1 同批 review |
| 4 | S2 | 唯一動 C++ 的項目,測試面最大 |
| 5 | S4 | 依賴 S3 的版式結論 |
| 6 | S5b | 含 Registry 重構,風險獨立,放最後單獨 review |

每項獨立可交付、獨立 review;任一項砍掉不影響其他項。

## 開放問題(實作前需拍板)

1. **S2 ETA 的顯示粒度**:分鐘取整是否夠?或需要「< 5 分鐘 / 約 X 分鐘 / 約 X 小時」三段式?
2. **S4 閱讀模式是否要帶時間戳的變體**(MacWhisper 的 Transcript 模式有 timestamps 開關)— v1 先純文字,確認是否列入 v2。
3. **S5b 鍵帽符號**:zh_TW 環境下 macOS 慣例是符號(⌘O);Windows 顯示「Ctrl+O」— 是否接受兩平台視覺不一致(建議接受,各隨平台慣例)。
