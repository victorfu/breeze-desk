# BreezeDesk repository audit：剩餘修復開發規格

Status: Planned 2026-07-30. 本文件只規範已驗證的既有缺陷與測試債務，不包含新功能。

Baseline: `main` at `dc8b065` (`Preserve source timeline during audio normalization`).

## 目標與範圍

完成全 repository 稽核後仍未處理的 13 個有效問題，並修復一組會遮蔽真實回歸結果的
QML 測試治具問題。每一項都必須先建立可在修正前失敗的回歸測試，再以獨立 commit
完成；不得把多項無關修正包在同一個 commit。

本規格不加入新的產品能力、不重新設計視覺風格，也不處理已完成的項目。下列工作已在
baseline 以前完成，因此不應再次實作：CLI durable chunk plan 的 mutation-before-validation、
managed import shutdown orphan、永久刪除 outbox、受限制的 cache cleanup、媒體 stream 選擇，
以及 MP4 音訊時間軸正規化。

## 全域實作規則

### G1. 一項缺陷、一個 commit

每個 `R1`–`R13` 與 `T1` 都是獨立工作單位。順序為：重現、加入回歸測試、最小修正、
執行驗證、檢查 diff、commit，然後才開始下一項。若實作時發現原判斷無法重現，應停止該項
並更新本規格的證據，不得為了符合清單而提交推測性修改。

### G2. Windows build/test preflight

所有 Windows configure、build、CTest 與 Qt test executable 都必須遵守 repository 根目錄
`AGENTS.md`。每個新的 shell invocation 必須在同一個 `cmd.exe` process 先執行：

```bat
call scripts\setup-msvc.bat || exit /b 1
where cl.exe || exit /b 1
```

直接執行 CTest 或 Qt test executable 前，還必須在同一 process 部署並驗證相同 Debug kit。
下列片段必須由 `cmd.exe /d /v:on` 執行，因此 append 既有 PATH 時使用 `!PATH!`：

```bat
set "BREEZEDESK_AGENT_BUILD=%CD%\build\debug"
call scripts\deploy-debug.bat "%BREEZEDESK_AGENT_BUILD%" || exit /b 1
set "PATH=%BREEZEDESK_AGENT_BUILD%;%BREEZEDESK_AGENT_BUILD%\platforms;!PATH!"
set "QT_PLUGIN_PATH=%BREEZEDESK_AGENT_BUILD%"
set "QT_QPA_PLATFORM_PLUGIN_PATH=%BREEZEDESK_AGENT_BUILD%\platforms"
set "QT_QPA_PLATFORM=windows"
where Qt6Cored.dll || exit /b 1
where Qt6Testd.dll || exit /b 1
where qwindowsd.dll || exit /b 1
```

任一檢查失敗時不得啟動測試 executable，以免再產生 missing-DLL blocking dialog。平行測試只能
繼承同一個已初始化的 parent environment，不得同時呼叫多份 `setup-msvc.bat`。

### G3. 錯誤處理與資料一致性

- repository mutation 必須是 transactionally durable 才能更新 UI；若採 optimistic UI，則必須有
  完整 snapshot rollback 與可見的 rejection message。
- async callback 必須以 recording ID、job ID、owner token 或 generation token 驗證結果仍屬於目前
  context。
- database migration 必須保留 checksum、backup、transaction、integrity-check 與 FTS5-disabled
  fallback 路徑。
- 不得用放寬 production validation 的方式讓舊測試通過；應修正不合法的測試 fixture。

## 剩餘工作總覽

| ID | Priority | 問題 | 主要範圍 |
| --- | --- | --- | --- |
| R1 | P2 | GUI hash/ffprobe background work 無法取消 | app/audio |
| R2 | P2 | 播放選取與 loop 狀態跨 recording 殘留 | UI/player |
| R3 | P1 | Notes 未儲存草稿會被 job metadata refresh 覆寫 | UI/detail |
| R4 | P1 | filtered list model 被當成 recording authoritative lookup | UI/repository |
| R5 | P1 | 同一 recording 可建立多個 pending/executing transcription jobs | jobs/database |
| R6 | P2 | mutation 後未重新套用 active query、sort、review filter | UI/library |
| R7 | P2 | title uniqueness 只檢查目前 filtered subset | UI/repository |
| R8 | P2 | Library search 會在沒有提示下過濾 Trash | UI/library |
| R9 | P1 | transcript read failure 會保留上一份 segments | UI/transcript |
| R10 | P2 | JobQueue optimistic command rejection 後未 rollback | UI/jobs |
| R11 | P1 | 編輯舊 transcript 未阻擋同 recording 的另一個 active writer | transcript/jobs |
| R12 | P1 | legacy permanent delete 可能留下 orphan search plaintext | database/search |
| R13 | P2 | Library LIKE search 會命中非 active transcript 歷史文字 | database/search |
| T1 | Test | QML Smoke 有 4 個已知 fixture/DPI failure | tests/QML |

## R1. 讓 GUI preprocessing background work 可取消

### 已驗證現況

`TranscriptionCoordinator::verifySourceMedia()`、`inspectMedia()` 與
`verifyNormalizedSource()` 透過 `QtConcurrent::run()` 執行 source hash 或 ffprobe，但沒有把
cancellation flag 傳入。`FileHash::sha256()` 與 `FFprobeService::inspect()` 已有 cancellation-aware
介面，因此缺口在 coordinator lifecycle。取消 job、失去 lease 或 shutdown 後，callback 雖會因
job/owner 不符而忽略結果，磁碟掃描或 ffprobe process 仍會繼續執行。

主要檔案：

- `src/app/TranscriptionCoordinator.cpp`
- `include/breezedesk/app/TranscriptionCoordinator.h`
- `include/breezedesk/core/FileHash.h`
- `include/breezedesk/audio/FFprobeService.h`

### 必要行為

- 每個 active preprocessing pipeline 持有一個 shared cancellation flag，hash、ffprobe、第二次
  source verification 都使用同一個 job-scoped identity，或使用明確交接且同樣受 lifecycle 控制的 flag。
- `cancel()`、`shutdown()`、`clearActive()`、lease-loss handoff 與 coordinator destruction 都先要求取消。
- cancellation result 不得被記錄成 `SourceFileMissing` 或 `UnsupportedMedia`；durable user cancellation
  仍由既有 cancellation checkpoint 決定最終狀態。
- destruction 必須安全 drain 尚未完成的 watcher/future，不得讓 callback 存取已銷毀的 coordinator，
  也不得遺留 ffprobe child process。

### 回歸測試與驗收

- 在 `tests/Jobs/tst_TranscriptionCoordinator.cpp` 以可阻塞的 hash/metadata seam 驗證 cancel、lease loss
  與 shutdown 都會設定 cancellation，且不會繼續到下一 pipeline stage。
- 在 Audio/Core tests 保留 `FileHash` 與 `FFprobeService` cancellation 的直接測試。
- 測試必須有 bounded timeout，不使用超大實體檔案製造 timing-dependent race。
- cancellation 完成後沒有錯誤 dialog、沒有錯誤 terminal transition、沒有 orphan child process。

## R2. 切換 recording 時重設播放互動狀態

### 已驗證現況

`PlayerViewModel::setSource()` 只設定 `QMediaPlayer` source；`selectionStart`、`selectionEnd` 與
`loopSelection` 沒有重設。`ApplicationViewModel::openRecording()` 會清 waveform peaks，但新 recording
可能繼承上一份 selection，並在播放位置進入舊 selection end 時跳回舊 start。

主要檔案：`src/ui/PlayerViewModel.cpp`、`src/ui/ApplicationViewModel.cpp`、
`src/qml/pages/RecordingPage.qml`。

### 必要行為

- recording identity 改變時，position、selection start/end、loop selection 與 recording-specific
  waveform context 必須回到初始值。
- 兩個 recording 即使引用同一個 source URL，也視為不同 context。
- 單純 metadata refresh 或對同一 recording 重設相同 source 不應造成意外跳播。
- property change signal 只在值實際改變時發出，重設順序不得觸發 loop seek。

### 回歸測試與驗收

- 在 Player/ApplicationViewModel tests 設定 A 的非零 selection 與 loop，切到 B 後驗證全部歸零。
- 加入「不同 recording、相同 URL」與「同 recording metadata refresh」案例。
- QML waveform selection、loop button 與 player position 顯示同步，無一 frame 的舊選取狀態。

## R3. 保護 Notes 未儲存草稿

### 已驗證現況

`RecordingPage.qml` 的 Notes `TextArea` 直接綁定 `root.detail.notes`，只在失去 focus 時提交。
`LibraryViewModel::setRecordingJobStatus()` 會發出 `recordingMetadataChanged`，而
`ApplicationViewModel` 收到後立即以 `m_library.details()` 呼叫 `RecordingDetailViewModel::setDetails()`。
因此使用者仍在輸入、尚未 blur 的文字可能被背景 job progress refresh 覆寫。

### 必要行為

- Notes 必須有 recording-scoped draft 與 dirty 狀態；背景 metadata refresh 只能 merge 非 Notes 欄位，
  不得覆寫 dirty draft。
- focus loss、切換 recording、離開 Recording page 與正常 application shutdown 前都要嘗試提交。
- repository 拒絕更新時，UI 保留 recording-scoped draft 與 dirty/unsaved 狀態、顯示 actionable error
  並允許重試；只有使用者明確 discard/reload 才恢復 durable Notes，不得默默丟掉輸入。
- 切換 recording 時不得把 A 的 draft 套到 B。

主要檔案：`src/qml/pages/RecordingPage.qml`、`src/ui/RecordingDetailViewModel.cpp`、
`src/ui/ApplicationViewModel.cpp`、`src/ui/LibraryViewModel.cpp`。

### 回歸測試與驗收

- focus Notes、輸入草稿、模擬 job progress；草稿保持不變，blur 後 repository 收到完整文字。
- 模擬 `updateNotes` 失敗；草稿與 dirty 狀態保留，使用者看得到錯誤且可重試，不被誤標成功。
- A 有 dirty draft 時切到 B，必須先成功提交 A；失敗則留在 A。

## R4. 將 recording lookup 與 visible list 分離

### 已驗證現況

repository mode 下，`LibraryViewModel::refresh()` 只把目前 search/review query 的結果放進
`m_source`，但 `LibraryViewModel::details(id)` 仍回傳 `m_source.recording(id)`。
`ApplicationViewModel::requestTranscription()`、`enqueueTranscription()`、model download completion、
reveal/export/refresh 等 command 因而把「目前不可見」誤判成「不存在」。最明顯 race 是等待 model
下載期間改變 search，下載完成後 pending recording 無法 enqueue。

### 必要行為

- repository mode 的 ID lookup 必須直接由 repository 或獨立 authoritative cache 取得；visible model
  只負責目前 query 的畫面資料。
- command 執行前重新驗證 recording 仍存在、deleted 狀態與 source 狀態，不以 row visibility 判斷。
- pending model-download queue 保存 recording ID；completion 逐一 authoritative revalidate，已刪除項目
  安靜移除或顯示一次明確訊息，不阻擋其他項目。
- repository error 與 not-found 必須分開呈現。

主要檔案：`src/ui/LibraryViewModel.cpp`、`include/breezedesk/ui/LibraryViewModel.h`、
`src/ui/ApplicationViewModel.cpp`、`include/breezedesk/database/IRecordingRepository.h`。

### 回歸測試與驗收

- import recording、要求 transcription、在 model download 完成前用 search 隱藏它；完成後仍 enqueue 正確 ID。
- review filter、Trash visibility 與 pagination 下的 ID lookup 同樣有效。
- 不存在或已永久刪除的 ID 不 enqueue，且不會誤用另一列資料。

## R5. 同一 recording 僅允許一個 pending/executing job

### 已驗證現況

`ApplicationViewModel::enqueueTranscription()` 每次配置新 UUID 並同步 emit；
`TranscriptionCoordinator::enqueue()` 直接 `createQueued()`。目前沒有 recording-scoped active-job
uniqueness，快速雙擊、兩個入口或另一 process 可建立重複 queued/running jobs。

### 必要行為

- 同一 recording 在任一時間最多只能有一個 pending/executing job。此集合明確為 `Queued`、
  所有 running stages 與 `Cancelling`；`Completed`、`Failed`、`Cancelled`、`Interrupted` 不算 active，
  但 retry/resume 也必須在 transition 前套用同一條規則。
- 檢查與 create/transition 必須在同一個 `BEGIN IMMEDIATE` transaction，確保不同 process 不會 race。
- 第二次要求不得建立新 job/chunks/events；UI 應聚焦或保留既有 job，並顯示「已在 queue/running」訊息。
- Recording card 的 Transcribe action 對所有 active states 都 disabled，不只顯示字串
  `Transcribing` 時 disabled；UI disable 只是 UX，database invariant 才是 correctness boundary。

主要檔案：`src/jobs/SqliteJobRepository.cpp`、`src/jobs/JobQueue.cpp`、
`src/app/TranscriptionCoordinator.cpp`、`src/ui/ApplicationViewModel.cpp`、
`src/qml/components/RecordingCard.qml`。

### 回歸測試與驗收

- double invocation 只留下單一 job；第二次回傳既有狀態或明確 rejection。
- 兩個 `DatabaseManager`/repository connection 同時 enqueue，仍只有一個 active job。
- 另一 active job 存在時 retry/resume 被拒絕且原 job state 不變。
- terminal job 存在不妨礙新 transcription。

## R6. mutation 後重新套用 active query、sort 與 review filter

### 已驗證現況

repository mode 的 search/review/sort 在 `refresh()` 時由 SQL 決定，proxy query 則被清空。
`rename()`、`setTags()`、`setReviewState()`、`setNotes()` 成功後只直接修改 `m_source`。因此 recording
即使已不符合 search/review 仍留在畫面，Title sort 下 rename 後也可能停在錯誤位置。

### 必要行為

- 所有會影響目前 query membership 或 sort key 的 durable mutation 成功後，共用同一個
  `refreshPreservingContext()` 路徑。
- refresh 必須保留仍存在且仍可見的 selected recording；若它不再符合 filter，清除 list selection，
  但不得意外關閉正在檢視的 recording detail。
- refresh 失敗時保留已知已 commit 的 local values、顯示 refresh error 並安排可控 retry；不得宣稱
  mutation 失敗（因為資料庫 mutation 已成功），也不得假設同一個失敗中的 repository 可立即再讀。
- non-repository/in-memory test mode 保留 dynamic proxy 行為。

### 回歸測試與驗收

- TitleAZ/TitleZA 下 rename 後順序正確。
- 依 Notes/Tag search 顯示的 recording 移除 matching text 後立即消失。
- Reviewed/Unreviewed filter 下切換狀態後 membership 正確。
- selection 與 active detail 不會跳到另一筆 recording。

## R7. title uniqueness 必須查 authoritative dataset

### 已驗證現況

import、managed import 與 rename 使用 `m_source.availableTitle()`；`m_source` 在 active search/review
下只含 filtered subset。被 filter 隱藏的同名 recording 不會被看見，因此可產生重複 title。

### 必要行為

- repository mode 的 unique-title allocation 與 create/update 在同一個 immediate transaction 完成，
  比對為 case-insensitive，並沿用現有 `Title`、`Title (2)`、`Title (3)` 規則。
- deleted recordings 也參與 uniqueness，避免 restore 後碰撞。
- rename 排除自身 ID；trim 後空白 title 仍拒絕。
- in-memory model 可保留 `RecordingListModel::availableTitle()`，但 production repository path 不得依賴它。

主要檔案：`src/ui/LibraryViewModel.cpp`、`src/ui/RecordingListModel.cpp`、
`src/database/SqliteRecordingRepository.cpp`、`include/breezedesk/database/IRecordingRepository.h`。

### 回歸測試與驗收

- 用 search 隱藏既有 `Meeting` 後再 import/rename，結果為 `Meeting (2)`。
- 大小寫不同與 Trash 中同名資料也會配置 suffix。
- 兩個 connection 同時要求相同 title 不會留下 duplicate。

## R8. Library search 不得隱性過濾 Trash

### 已驗證現況

`LibraryViewModel::setSearchText()` 在 repository mode 清空兩個 proxy query，再以 searchText refresh
同一份 source model。SQL 回傳只包含 match 的 active 與 deleted recording；Trash page 本身沒有 search
欄位或 active-filter 提示，因此可能顯示 `Trash is empty`，即使資料庫仍有被刪除項目。

### 必要行為

- Library search/review filter 只作用於 Library；目前沒有 Trash search UI，因此 Trash 永遠顯示全部
  deleted recordings。
- repository refresh 應使用兩個獨立 query/result set（或等價的獨立 source），不要讓 Library result
  取代 Trash authoritative set。
- `trashEmpty` 僅代表資料庫沒有 deleted recordings，不受 Library search/review 影響。
- sort mode 是否共用可維持既有行為，但必須在兩個 result set 都穩定一致。

### 回歸測試與驗收

- Trash 建立兩筆資料，Library search 只命中其中一筆或完全不命中；進入 Trash 仍看到兩筆。
- review filter、search debounce 與 repeated navigation 後 `trashEmpty` 仍正確。
- restore/permanent delete 後兩個 model 都更新且沒有 duplicate row。

## R9. transcript load failure 不得顯示 stale segments

### 已驗證現況

`ApplicationViewModel::reloadActiveTranscript()` 在 `segmentsForJob()` 失敗時只顯示 toast 並 return，
沒有清除 `TranscriptViewModel`。先開啟 A，再於載入 B 時遇到 database read error，畫面會在 B 的標題下
保留 A 的 transcript。

### 必要行為

- recording identity 改變時先建立明確 loading context；只有 matching context 的成功結果可 replace。
- read failure 顯示 error 並呈現 empty/error state，絕不在 B recording 下保留 A 的 segments。
- canonical active job 已正式切換後，載入新 canonical transcript 失敗不得把舊 job segments 偽裝成新 job。
  但同一 recording 的 retranscription 執行期間，既有設計可繼續顯示舊 canonical transcript；同一
  canonical context 的 transient reload failure 也可保留 last-known-good，只要顯示錯誤且不誤標 saved。
- 清除 stale view 前仍須遵守既有 dirty draft commit gate；不得因 B 載入失敗而丟失 A 的未儲存編輯。
- 未來若改成 async load，callback 必須檢查 recording ID、job ID 與 generation token。

### 回歸測試與驗收

- 先載入 A，對 B 注入 `segmentsForJob` failure；active ID 是 B，segments 為空且 toast 正確，沒有 A 文字。
- A dirty save 失敗時，不允許切換到 B，也不清除 A。
- 同一 canonical context 的 reload failure 可保留 last-known-good，但不得把 model 標成已成功儲存。

## R10. JobQueue command 必須 persist-first 或完整 rollback

### 已驗證現況

`JobQueueViewModel::cancel()`、`retry()`、`resume()` 與 `reorder()` 會先修改 `JobListModel`，再 emit request。
coordinator/repository 拒絕後沒有 rollback acknowledgment，UI 因而可能顯示未曾 durable 的 state/order。
`remove()` 與 `clearCompleted()` 已採 confirm-after-success，不屬於此 optimistic mutation 缺陷。

### 必要行為

- 優先統一為 persist-first：ViewModel 送 command，不先改 durable-looking state/order；成功後由 coordinator
  publish/confirm 更新 model。
- 若保留 optimistic interaction，必須保存完整 row/order snapshot，以 command ID 對應 success/rejection，
  rejection 原子 rollback 並發出 `commandRejected`。
- 重複 click 在 command pending 時不得產生第二個 mutation。
- repository error 必須顯示 actionable toast，不能只留下靜默無效操作；採 persist-first 時可沿用
  coordinator error path，不強制新增 `commandRejected`。

主要檔案：`src/ui/JobQueueViewModel.cpp`、`src/ui/JobListModel.cpp`、
`src/app/TranscriptionCoordinator.cpp`、`src/app/main.cpp`。

### 回歸測試與驗收

- 對 cancel/retry/resume/reorder 各注入 repository rejection；row state、progress、queue order 與 running ID
  全部維持 durable snapshot，並只顯示一次 error。
- 成功 path 不出現 transient、尚未持久化卻看似 durable 的 state；最終 state/order 與 repository 一致。
- remove/clear 的既有 confirm-after-success tests 保持通過。

## R11. ownerless transcript edit 必須檢查 recording-level writer

### 已驗證現況

`validateOwnerlessEdit()` 只驗證被編輯的 `jobId` state 與該 job 的 lease。若 recording 的 active transcript
仍是舊 completed job，但同一 recording 的另一個 transcription job 正由其他 process 持有 lease，使用者仍可
編輯舊 transcript；新 job 完成後會切換 `active_job_id`，single-transcript cleanup 還可能刪除剛編輯的
舊 segments。

### 必要行為

- ownerless replace/save/delete 在同一 immediate transaction 檢查同 recording 是否有另一個 active writer。
- active writer 至少包含 `JobStateMachine::isRunning()`/`Cancelling` 的 job，以及同 recording 上仍有效的
  execution lease；abandoned running state 在 recovery 前也不得視為安全可編輯。
- owner-token worker path 繼續使用 `executionLeaseTransaction(jobId, ownerToken)`，不得被 ownerless 規則取代。
- rejection 不修改 segments、revision history、search index 或 active_job_id。

主要檔案：`src/transcript/SqliteTranscriptRepository.cpp`、`tests/Transcript/tst_Transcript.cpp`。

### 回歸測試與驗收

- recording 有舊 completed active transcript，另一 job 為 Preparing/Transcribing 且持有 lease；對舊 job 的
  save-all、save-one、delete、replace 全部拒絕且資料完全不變。
- writer 進入 `Failed`、`Cancelled` 或 `Interrupted` 且 lease release 後，仍存在的舊 transcript 可正常編輯；
  不以會觸發 single-transcript cleanup 的 `Completed` 當此測試前提。
- 不同 recording 的 active writer 不阻擋此 recording。

## R12. migration 清除 legacy orphan search plaintext

### 已驗證現況

目前最新 schema version 為 11。現行永久刪除流程會同步維護 search index，但舊版資料庫可能已存在
recording row 被刪除、`search_index` FTS virtual table 仍保留 title/transcript plaintext 的狀態。FTS table
沒有 foreign-key cascade；只修正未來 delete 無法清除既有 orphan。

### 必要行為

- 新增「下一個可用 schema version」migration；不得在實作前硬編碼一定是 v12，因為其他先完成的修正
  可能先占用版本。
- transaction 內清空並從仍存在的 recordings 重建 `search_index_fallback`。必須依 `sqlite_master`
  檢查實體 `search_index`，不能只相信 `database_features.fts5`：feature flag disabled 時舊 FTS virtual table
  仍可能存在。存在且可用時清空重建；若此 build 無法安全使用它，則 drop，總之不得保留 plaintext。
  transcript 欄位只取 `recordings.active_job_id` 對應的 canonical segments。
- migration history name/checksum、upgrade backup 與 feature flag 必須符合現有 DatabaseManager 規則。
- FTS5 unavailable、已有 orphan fallback row、空資料庫與重複啟動都要安全。

主要檔案：`src/database/DatabaseManager.cpp`、`src/database/DatabaseSearchService.cpp`、
`tests/Database/tst_Database.cpp`。

### 回歸測試與驗收

- 建立 pre-migration DB，植入 live canonical row、orphan FTS/fallback row 與 hidden historical segments；
  upgrade 後 orphan 消失，live canonical text 正確，hidden text 不被重建。
- FTS5 與 forced-fallback 兩條路徑皆測試。
- schema version、migration checksum/history、backup 與 integrity check 正確。

## R13. Library search 只搜尋目前可見的 canonical transcript

### 已驗證現況

`SqliteRecordingRepository::list()` 的 search LIKE subquery 只限制
`transcript_segments.recording_id=r.id`，沒有限制 `job_id=r.active_job_id`。failed、cancelled、interrupted
等 non-active job 的歷史文字因此可讓 recording 出現在 Library；legacy database 若仍留有舊 completed
segments 也會受影響。打開 recording 後卻找不到該命中文字。
`DatabaseSearchService::rebuildRecording()` 與 search result location 已採 active job，兩條 search path 語意不一致。

### 必要行為

- Library query 的 transcript predicate 僅匹配 `s.job_id=r.active_job_id`。
- `active_job_id IS NULL` 時不搜尋任何 transcript segments，只搜尋 title/notes/tags。
- original/edited text 的 matching/display 語意一致；edited text 非空時，以 display text 為準，避免原文中已被
  使用者修正掉的字仍命中。FTS/fallback rebuild 也使用相同 expression。
- count query、paged rows query、FTS5 與 fallback LIKE 必須回傳相同 recording membership。

主要檔案：`src/database/SqliteRecordingRepository.cpp`、
`src/database/DatabaseSearchService.cpp`、`tests/Library/tst_LibraryWorkflows.cpp`。

### 回歸測試與驗收

- seed active completed transcript 與同 recording 的 failed/cancelled/interrupted historical transcripts；
  只有 active display text 的 terms 會命中。
- active_job_id null、edited text 取代 original text、deleted recording 與 pagination count 都有案例。
- FTS5 enabled 與 fallback mode membership 相同。

## T1. 修復 QML Smoke 的既有測試債務

這組工作是測試基線修復，不代表新發現的 production UI bug。完整 `Qml.Smoke` 在 baseline 有四個失敗：

1. `brandIconRendersAtNativeWindowsSizes` 在 high-DPI Windows 上 fixture 預期 `16x16`，實際 logical/device
   pixel 尺寸為 `36x36`。測試應明確區分 source native pixels、logical size 與 device pixel ratio，或在固定
   DPR render target 驗證；不得為了測試改壞實際 icon scaling。
2. `viewModelCommandsHaveObservableState`
3. `sameRecordingCanBeOpenedAgainAfterReturningToLibrary`
4. `libraryStateSurvivesViewModelRecreation`

後三項以任意 bytes 加上 media extension 假裝可匯入檔案。`ffc0da8` 加入正確的 pre-import media probe 後，
這些 fixture 應被拒絕。測試必須改用共用 helper 產生最小、有效 WAV（或 repository stub），不得放寬
production media validation。

### 驗收

- `Qml.Smoke` 在 Windows 100%、125%、150% 或可用的等價 DPR 測試環境不因 hard-coded pixel size 失敗。
- 三個 import tests 使用合法 deterministic media fixture，且保留各自原本要驗證的 state/persistence 行為。
- 完整 suite 無 Qt missing-DLL dialog、無 skipped test 掩蓋 failure。

## 建議實作順序

1. `T1`：先恢復可信任的 QML baseline，避免後續 UI 修正被既有 failure 淹沒。
2. `R11`、`R5`、`R12`、`R13`：先處理資料競爭、資料洩漏與 search integrity。
3. `R4`、`R9`、`R3`：修正 authoritative context 與使用者草稿/顯示資料安全。
4. `R10`、`R6`、`R7`、`R8`：統一 UI command 與 Library query consistency。
5. `R1`、`R2`：完成 lifecycle cancellation 與播放 context reset。

若 migration 版本被較早的工作占用，後續 migration 一律改用當時的下一個連續版本；不得重寫已發佈
migration checksum。

## 每個 commit 的 Definition of Done

- 修正前有 deterministic regression test，可證明問題不是 false positive。
- production change 僅涵蓋該 ID，不混入新功能或順手重構。
- targeted test 通過；UI/database/job 變更另跑其對應 suite。
- 執行 repository 定義的短測試集合；影響 QML 時跑完整 `Qml.Smoke`。
- 所有測試使用本文件 G2 的 x64 MSVC + matching Debug Qt preflight。
- `git diff --check` 無錯，工作樹只含預期檔案。
- commit message 描述使用者可觀察的 invariant，而不是籠統的 `fix tests`。

全部 13 項與 T1 完成後，才重新進行一次 full repository scan；新發現必須另外驗證與立項，不得無限擴張
本規格。
