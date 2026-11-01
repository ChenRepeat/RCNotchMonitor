<div align="center">

# RCNotchMonitor

**Windows 螢幕瀏海硬體即時監控工具 (Dynamic Island / Notch Hardware Telemetry Monitor)**

[![Language](https://img.shields.io/badge/Language-C%2B%2B17-00599C?style=flat-square&logo=cplusplus)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%2F%2011%20(x64)-0078D6?style=flat-square&logo=windows)](https://www.microsoft.com/windows)
[![Graphics](https://img.shields.io/badge/Renderer-Direct2D%20%2B%20DirectWrite-4ADE80?style=flat-square)](#)
[![Footprint](https://img.shields.io/badge/Memory-~6.4%20MB%20RAM-FACC15?style=flat-square)](#)
[![License](https://img.shields.io/badge/License-MIT-C084FC?style=flat-square)](LICENSE)

一款以純原生 **C++17 + Win32 API + Direct2D + DirectWrite** 打造的極輕量級 Windows 頂部螢幕瀏海（Dynamic Island）硬體狀態監控工具。  
零外部執行期依賴（無須 `.NET Runtime`、`WebView2` 或 `Electron`），單一執行檔僅 **815 KB**，常駐記憶體僅約 **6.4 MB**。

</div>

---

## 📸 介面預覽 (Screenshots)

### 1. 收合狀態：雙行網格模式 (Dual-Row) vs 單行橫條模式 (Single-Row)

| 雙行網格模式 (Dual-Row Grid) | 單行橫條模式 (Single-Row Bar) |
| :---: | :---: |
| ![Dual-Row Collapsed](docs/images/notch_collapsed_dual.png) | ![Single-Row Collapsed](docs/images/notch_collapsed_single.png) |
| 適合緊湊型頂部瀏海，上下雙排整齊對齊 | 自動依據欄位數量撐開寬度，單排橫向展開 |

### 2. 滑鼠移入：動態島展開模式 (Expanded Dynamic Island)

<div align="center">
  <img src="docs/images/notch_expanded_island.png" alt="Expanded Dynamic Island" width="540" />
</div>

> 滑鼠移入頂部瀏海時，以 **60FPS Smoothstep 物理曲線**流暢展開為動態島面板，顯示處理器與顯示卡完整型號、時脈、功耗、VRAM 佔用率、前景 3D 幀率與底部快捷圖示按鈕（設定、單雙行切換、鎖定滑鼠穿透）。

### 3. 無邊框現代化設定面板 (Borderless Settings Window)

<div align="center">
  <img src="docs/images/settings_panel.png" alt="Settings Panel" width="600" />
</div>

### 4. 自訂硬體名稱與個人化主題配色 (Custom Labels & Theme)

<div align="center">
  <img src="docs/images/notch_custom_theme.png" alt="Custom Theme" width="420" />
</div>

---

## ✨ 核心功能與設計亮點

### 1. 原生硬體遙測引擎 (Zero-Bloat Telemetry Engine)
背景採用獨立低優先權採樣執行緒，完全不阻塞 UI 渲染，支援以下四大硬體指標：

| 監控項目 | 收合模式顯示 | 動態島展開附加資訊 | 底層資料來源 (Windows Native APIs) |
| :--- | :--- | :--- | :--- |
| **CPU 處理器** | 使用率 `%` + 溫度 `°C` | 完整型號、即時時脈 `GHz`、封裝功耗 `W` | `GetSystemTimes`、`CallNtPowerInformation`、LibreHardwareMonitor 感測橋接 / WMI |
| **GPU 顯示卡** | 使用率 `%` + 溫度 `°C` | 完整型號、VRAM 已用/總量 `GB`、功耗 `W` | 動態載入 `nvml.dll` (NVIDIA NVML API) + Windows GPU Engine 效能計數器備援 |
| **RAM 記憶體** | 記憶體使用率 `%` | 已用容量 / 總物理記憶體容量 `GB` | `GlobalMemoryStatusEx` |
| **FPS 幀率** | 即時幀率 (4 位數固定寬度) | 前景 3D 視窗狀態、螢幕更新率 `Hz` | Windows 原生 `ETW (Event Tracing for Windows)` 即時捕捉 DXGI / D3D Present 事件 |

### 2. 像素級排版與視覺細節 (4px Grid & Anti-Jitter Typography)
- **全專案 4px 倍數網格系統**：所有視窗尺寸、內距（左右 `20px` padding）、圓角、按鈕與滑桿步進皆嚴格遵循 `4px` 倍數設計規範。
- **固定 4 位數右對齊數值槽位**：使用率（`100%`）、FPS（`9999`）與溫度（`99°C`）皆採用預先度量的固定寬度槽位並靠右對齊，數值劇烈變動時**標題與版面絕對不會左右彈跳**。
- **高解析度 DPI 自動縮放**：自動偵測主螢幕解析度（如 1080p / 2K / 4K），依 $\sqrt{\text{ScreenW} / 1920}$ 計算基礎物理倍率，並將文字大小直接綁定瀏海縮放比例 `%` 同步無段縮放。
- **零閃爍滾輪無段縮放**：直接將滑鼠移至頂部瀏海滾動**滑鼠滾輪**，即可在 `50%` ~ `250%` 間即時無段調整大小；底層採用固定最大預分配透明畫布（`UpdateLayeredWindow`），縮放與展開過程絕不閃現獨立視窗邊框。
- **4 點旋轉排版切換動畫**：單／雙行切換按鈕採用自繪 4 圓點圖示，點擊時左側兩點向左上旋轉、右側兩點向右下旋轉，搭配 60FPS 補幀過渡。

### 3. 無邊框自訂設定視窗 (Custom-Framed Settings UI)
- **拖曳排序 (Drag-to-Reorder)**：按住每列右側的 6 點拖曳把手（自動切換為 `IDC_SIZEALL` 十字移動游標）即可上下拖曳調整 CPU / GPU / RAM / FPS 的顯示順序，其他列會以 **60FPS 補幀動畫**平滑讓位。
- **雙模式溫度閾值設定**：「警戒溫度」與「高溫溫度」除了可點擊上下箭頭快速微調外，亦可直接點擊數值框以鍵盤輸入任意溫度（`30°C` ~ `110°C`）。
- **黑白極簡預設與全自訂配色**：預設採用極簡黑底白字懸停風格（擺脫廉價螢光藍），並支援透過 Windows 調色盤自訂「底色」、「邊框」、「按鈕」、「懸停」以及每個監控項目的「標題色」與「數值色」。
- **開機管理員自啟**：內建 Windows 工作排程器（`Task Scheduler`）整合，開啟「開機自啟」後於登入時自動以最高權限背景啟動，免除每次開機彈出 UAC 提示。

---

## ⌨️ 快捷鍵與滑鼠操作

| 操作方式 | 觸發位置 | 功能說明 |
| :--- | :--- | :--- |
| **`Ctrl + Alt + S`** | 全域快捷鍵 | 隨時開啟／聚焦「設定」視窗 |
| **`Ctrl + Alt + N`** | 全域快捷鍵 | 切換「鎖定滑鼠穿透」（開啟後滑鼠點擊直接穿透瀏海至底層視窗） |
| **滾動滑鼠滾輪** | 頂部螢幕瀏海上方 | 以 `4%` 為單位即時放大／縮小螢幕瀏海尺寸（`50%` ~ `250%`） |
| **滑鼠移入 / 移出** | 頂部螢幕瀏海上方 | 自動展開動態島詳細硬體面板／移出自動收合（未鎖定穿透時） |
| **左鍵雙擊圖示** | 系統匣 (System Tray) | 開啟「設定」視窗 |
| **右鍵點擊圖示** | 系統匣 (System Tray) | 開啟快速選單（開啟設定、切換單雙行、切換穿透、結束程式） |

---

## 📂 專案目錄結構

```text
RCNotchMonitor/
├── .github/
│   └── workflows/
│       └── build-and-sign.yml   # GitHub Actions 自動雲端編譯、SignPath 數位簽章與 Release 發布流水線
├── docs/
│   └── images/                  # README 介面預覽截圖
├── src/
│   ├── main.cpp                 # 程式進入點、系統匣常駐、全域快捷鍵、單一實例鎖與自動化驗證 (--verify)
│   ├── config.h / config.cpp    # JSON 設定檔讀寫、4px 對齊正規化與預設黑白主題配置
│   ├── telemetry.h / .cpp       # CPU / GPU (NVML) / RAM / ETW FPS 背景硬體採樣引擎
│   ├── notch_window.h / .cpp    # Direct2D 頂部螢幕瀏海與動態島展開動畫渲染器
│   ├── settings_window.h / .cpp # Direct2D 無邊框設定視窗、拖曳排序動畫與文字輸入框控制器
│   ├── fa_icons.h               # 純幾何向量繪製之 Font Awesome 風格圖示庫與 4 點旋轉補幀渲染器
│   ├── sensor_bridge.cs         # 選用之硬體溫度感測橋接模組
│   ├── app.ico                  # 多解析度 (16x16 ~ 256x256) 應用程式圖示
│   └── app.rc                   # Windows 資源檔 (内嵌圖示與 VS_VERSION_INFO 發行者資訊)
├── build.ps1                    # 一鍵編譯腳本 (支援本機 w64devkit 與 GitHub Actions MinGW-w64)
├── .gitignore                   # Git 忽略規則
├── LICENSE                      # MIT 開源授權條款
└── README.md                    # 專案說明文件
```

---

## 🛠️ 從原始碼編譯 (Build from Source)

### 系統與編譯器需求
- **作業系統**：Windows 10 / Windows 11 (64-bit)
- **編譯器**：MinGW-w64 (`g++.exe` 支援 C++17 與 `windres.exe`)
  - 若於本機開發，可將 [w64devkit](https://github.com/skeeto/w64devkit) 解壓縮至 `deps\w64devkit`，或確保系統 `PATH` 中已安裝 MinGW-w64。

### 執行一鍵編譯
在 PowerShell 中切換至專案根目錄並執行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

編譯完成後，單一獨立執行檔會輸出至：
```text
dist\RCNotchMonitor.exe
```

### 執行自動化自我檢測與截圖生成
`RCNotchMonitor.exe` 內建無頭驗證模式（`--verify`），可自動測試所有感測器並輸出 5 張介面渲染圖與記憶體報告：

```powershell
.\dist\RCNotchMonitor.exe --verify .\verify_out | Out-Null
```

---

## ⚙️ 設定檔結構 (`config.json`)

程式會在執行檔同目錄下自動產生並讀取 `config.json`，所有像素與比例數值均對齊為 `4` 的倍數：

```json
{
  "configVersion": 4,
  "scalePercent": 100,
  "notchWidth": 260,
  "notchHeight": 36,
  "fontSizePt": 12,
  "opacityPercent": 96,
  "layoutMode": 0,
  "refreshIntervalMs": 1000,
  "bgColor": "0C0E14",
  "borderColor": "262936",
  "btnColor": "1E2026",
  "btnHoverColor": "F4F4F6",
  "tempWarnC": 75,
  "tempCritC": 85,
  "clickThroughLocked": false,
  "autoStartAdmin": false,
  "showDesktopFpsDash": true
}
```

---

## 🔐 Code Signing Policy

Free code signing provided by [SignPath.io](https://signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

- **Committers and Reviewers**: Project maintainers and contributors on GitHub.
- **Approvers**: Project owner releasing official versions via GitHub Actions workflow ([`.github/workflows/build-and-sign.yml`](.github/workflows/build-and-sign.yml)).
- **Privacy Policy**: This program (`RCNotchMonitor.exe`) does not collect, transmit, or store any personal information, usage analytics, or telemetry data over the network. All hardware sensor readings are processed strictly locally in volatile memory for real-time display, and user interface preferences are stored strictly locally in `config.json`.

---

## 📄 授權條款 (License)

本專案採用 [MIT License](LICENSE) 授權開源。歡迎自由 Fork、提交 Issue 或 Pull Request！
