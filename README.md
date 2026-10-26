# QLaunch

**English** | [中文](#中文)

A small, fast program launcher for Windows with **two levels of tabs**.

<!-- Screenshot: replace with docs/images/main.png when it is added
![QLaunch](docs/images/main.png)
-->

- One small exe, no runtime to install. Portable: settings and data live next to the exe.
- Hidden until you need it: move the mouse into a screen corner or press a hotkey. It hides again when the mouse leaves or the window loses focus (pin it to keep it open).
- Every page (tab) can have sub-pages: keep everyday programs on the page, the rest on its sub-pages.
- Drag files, folders and shortcuts from Explorer onto the window to add buttons. System items such as This PC, Control Panel and Recycle Bin work too. Drag buttons to move or swap them, or onto another tab.
- Resize the window by its border and the grid gains or loses rows and columns; buttons keep their place.
- A built-in **System** page: This PC, Control Panel, lock, sleep, restart, shut down, Task Manager, Registry Editor and more.
- Skins: Dark / Light colour schemes plus a dozen image skins (dark and light), live preview in Settings.
- Multi-language (`lang/*.ini`); English and Simplified Chinese included.

## Download

Get the latest zip from [Releases](../../releases), unpack it anywhere and run `QLaunch.exe`. On first run the window appears once and a message tells you how to open it again.

To start it with Windows, tick **Start with Windows** in Settings (no administrator rights needed). Don't run QLaunch itself as administrator: Windows then blocks drag and drop from Explorer. To run a single program elevated, right-click its button and choose **Run as administrator**.

## Using it

### Pages and sub-pages

<!-- Screenshot: docs/images/subpages.png
![Sub-pages](docs/images/subpages.png)
-->

Sub-tabs share the tab bar with the top-level tabs, so the window never changes size:

- Click a tab to show its buttons. A tab marked `›` has sub-pages; clicking it turns the bar into `‹ Page` followed by its sub-tabs.
- Click or hover over a sub-tab to show its buttons (the hover delay is in Settings, the same as for top-level tabs).
- Go back with a click on `‹ Page`, a right-click on it or on the blank tab bar, `Esc` / `Backspace`, or the mouse back button.
- `Esc` on the top-level tabs hides the window.
- While dragging a button, hover over a tab to switch to it and drop the button there. Hover over `‹ Page` to get back to the top-level tabs.

### Context menus

- **Button**: Open, Run as administrator, Open file location, Properties, Delete. On an empty cell: Add button.
- **Tab**: Rename, New page, New sub-page, Move back, Move forward, Delete.
- **Caption bar and blank areas**: the main menu with global actions: New page, Keep open, Refresh icons, Settings, Exit.

### System page

<!-- Screenshot: docs/images/system.png
![System page](docs/images/system.png)
-->

Created as the first page on first run (and once after upgrading from a version without it; it isn't recreated after you delete it). Four groups, each starting on a new row:

1. This PC, Control Panel, Recycle Bin, Downloads, Documents, Network, Empty Recycle Bin, Search
2. Lock, Sleep, Sign out, Restart, Shut down, Show desktop, Turn off display
3. Task Manager, Run, Windows Settings, Command Prompt, Notepad, Calculator, PowerShell
4. Device Manager, Services, Registry Editor, Environment Variables, Programs and Features, System

Some of these are commands QLaunch runs itself. Their file is written as `ql:<name>`, and the buttons can be copied to any page:

| File | Action |
|---|---|
| `ql:Lock` | Lock the PC |
| `ql:Sleep` | Sleep (asks first) |
| `ql:SignOut` | Sign out (asks first) |
| `ql:Restart` | Restart (asks first) |
| `ql:Shutdown` | Shut down (asks first) |
| `ql:MonitorOff` | Turn off the display |
| `ql:EmptyRecycleBin` | Empty the Recycle Bin |
| `ql:Search` | Windows search (Win+S) |
| `ql:Run` | Run dialog (Win+R) |
| `ql:ShowDesktop` | Show desktop (Win+D) |

Confirmation boxes appear at the mouse position. When you switch the language in Settings, built-in pages and buttons you haven't renamed are renamed to the new language.

### Settings

Hot corner and delay, hotkey, where the window opens, language, skin, tab position (left / top / right / bottom), button and tab sizes, icon size, tab hover delay, separate fonts for tabs and buttons, and Start with Windows. Changing the skin or a font previews it on the main window at once; Cancel restores the previous look.

## Skins

<!-- Screenshot: docs/images/skins.png
![Skins](docs/images/skins.png)
-->

Pick a skin in Settings. A skin is a folder or zip file in the `skins` folder next to the exe (`skins\Name.zip` or `skins\Name\skin.xml`) holding a `skin.xml` and PNG images. When you switch skins, the skin's recommended tab position and sizes are applied.

Supported: frame and background (9-slice stretch or tile), close and pin buttons (anchored), caption, tab area and tabs, button area and buttons in all states, text colour with shadow or outline, translucent windows (alpha channel and clipping colour). Sub-tabs use the same images as top-level tabs. The font face always comes from Settings; size and weight come from the skin.

The bundled skins (Voyage, Sakura, Glacier and others) are generated by [`tools/make_skin.py`](tools/make_skin.py). Pick a palette on [Realtime Colors](https://www.realtimecolors.com) and hand the link to the script to get a new skin of the same shape:

```bat
python tools\make_skin.py MySkin "https://www.realtimecolors.com/?colors=ecf0e8-060705-c1cbb1-464365-a37ca8"
```

The five colours are text, background, primary, secondary and accent: buttons use the secondary colour, hovering moves towards the primary, and the selected tab and the pin use the accent. See the top of the script for details.

## Files

Everything lives next to the exe, so the whole folder can be copied anywhere:

| File | Contents |
|---|---|
| `QLaunch.ini` | Settings |
| `data.ini` | Pages and buttons |
| `icons.dat` | Icon cache; delete it to extract all icons again |
| `lang/*.ini` | Language files |
| `skins/` | Skins |
| `icons/*.ico` | Icons of the built-in commands. A relative icon path on a button starts at the exe folder |

In `data.ini` the section name is the position, counted from 0: `[P03]` is page 3, `[P03.R01C05]` is its button at row 1, column 5, `[P03.S01]` is its sub-page 1, and `[P03.S01.R01C05]` is a button on that sub-page.

To translate QLaunch, copy `lang/en.ini` to `lang/<code>.ini` (for example `ja.ini`) and translate the text after each `=`.

## Building

Requires Visual Studio 2019 or 2022 (Community or Build Tools) with **Desktop development with C++**, including **C++ CMake tools for Windows** (CMake and Ninja).

**VS Code**: open the folder and install the recommended extensions (CMake Tools, C/C++). Pick the `x64-debug` or `x64-release` preset in the status bar, then press `F7` or `Ctrl+Shift+B` to build, or `F5` to build and debug. CMake Tools sets up the Visual Studio environment itself.

**Command line**, in an *x64 Native Tools Command Prompt*:

```bat
cmake --preset x64-release
cmake --build --preset x64-release
```

The exe goes to `out\bin\Debug` or `out\bin\Release`, and everything under `dist\` (languages, skins, icons) is copied next to it. Intermediate files go to `out\temp`.

The app icon and the built-in command icons are generated by [`tools/make_icons.py`](tools/make_icons.py). Both scripts need Python 3.8+ and Pillow (`pip install pillow`).

### Source layout

```
src/
  main.cpp      entry point: single instance, startup, message loop
  app.h         settings + data + icon cache
  core/         ini, strings, paths, RAII handles
  model/        pages and buttons, settings
  trigger/      hot corner (low-level mouse hook) and global hotkey
  icon/         icon extraction (background thread) and the icons.dat cache
  skin/         canvas, skin interface, colour skin, image skins (zip, XML, PNG)
  ui/           layout, main window, dialogs, drag and drop
  launch/       starting programs, built-in commands and the System page
  i18n/         language files
res/            app icon, manifest, dialog resources
dist/           files copied next to the exe: lang/, skins/, icons/
tools/          scripts that generate the bundled skins and icons
```

To keep the exe small and memory use low: static CRT, no exceptions or RTTI, no UI framework. Shell icons are extracted on a background thread and cached, later shows read only the cache. While hidden, the window frees icons, canvas and skin images and trims its working set.

## License

[MIT](LICENSE)

---

<a id="中文"></a>

# QLaunch

[English](#qlaunch) | **中文**

一个小巧、快速的 Windows 程序启动器，支持**二级标签**。

<!-- 截图：添加 docs/images/main.png 后取消注释
![QLaunch](docs/images/main.png)
-->

- 单个小 exe，不用装运行库。便携：设置和数据都在 exe 旁边。
- 平时隐藏：鼠标移到屏幕角落或按快捷键唤出；鼠标离开窗口或窗口失去焦点就自动隐藏（点图钉固定后不隐藏）。
- 每个页面（标签）下都可以建二级页面：常用程序放一级页面，不常用的放二级页面。
- 从资源管理器把文件、文件夹、快捷方式拖进窗口就能添加按钮，此电脑、控制面板、回收站这类系统项目也可以。按钮可以拖动换位置，也可以拖到别的标签上。
- 拖动窗口边框调整行列数，按钮留在原位。
- 内置“系统”页：此电脑、控制面板、锁定、睡眠、重启、关机、任务管理器、注册表编辑器等。
- 外观：Dark / Light 配色和十几个图片主题（深色、浅色都有），设置里可即时预览。
- 多语言（`lang/*.ini`），自带英文和简体中文。

## 下载

在 [Releases](../../releases) 下载最新的 zip，解压到任意位置，运行 `QLaunch.exe`。第一次运行时窗口会显示一次，并弹框告诉你以后怎么打开它。

需要开机自动启动，在设置里勾选“开机自动启动”即可，不需要管理员权限。不要用管理员身份运行 QLaunch 本身，否则 Windows 会拦住从资源管理器的拖放。个别程序需要管理员权限时，在它的按钮上右键选“以管理员身份运行”。

## 使用

### 页面和二级页面

<!-- 截图：docs/images/subpages.png
![二级页面](docs/images/subpages.png)
-->

二级标签和一级标签共用同一个标签栏，窗口大小始终不变：

- 点标签显示它的按钮。带 `›` 的标签有二级页面，点它后标签栏换成 `‹ 页面名` 加它的二级标签。
- 点二级标签或鼠标悬停在上面，显示该子页的按钮（悬停延时在设置里调，和一级标签相同）。
- 返回一级标签：点 `‹ 页面名`、右键它或标签栏空白处、按 `Esc` / `Backspace`、鼠标侧键“后退”。
- 在一级标签栏按 `Esc` 隐藏窗口。
- 拖动按钮时悬停在标签上会切换过去，松开就放进该页面；悬停在 `‹ 页面名` 上会回到一级标签。

### 右键菜单

- **程序按钮**：打开、以管理员身份运行、打开所在位置、属性、删除。空格子上：添加按钮。
- **标签**：重命名、新建页面、新建子页、前移、后移、删除。
- **标题栏和空白处**：主菜单，放全局操作：新建页面、保持显示、刷新图标、设置、退出。

### 系统页

<!-- 截图：docs/images/system.png
![系统页](docs/images/system.png)
-->

第一次运行时建在第一页（从没有系统页的旧版本升级后也会补建一次，删掉后不再重建）。分四组，每组从新的一行开始：

1. 此电脑、控制面板、回收站、下载、文档、网络、清空回收站、搜索
2. 锁定、睡眠、注销、重启、关机、显示桌面、关闭显示器
3. 任务管理器、运行、Windows 设置、命令提示符、记事本、计算器、PowerShell
4. 设备管理器、服务、注册表编辑器、环境变量、程序和功能、系统

其中一部分是 QLaunch 自己执行的命令，按钮的文件写成 `ql:名字`，可以复制到任何页面：

| 文件 | 作用 |
|---|---|
| `ql:Lock` | 锁定电脑 |
| `ql:Sleep` | 睡眠（先确认） |
| `ql:SignOut` | 注销（先确认） |
| `ql:Restart` | 重启（先确认） |
| `ql:Shutdown` | 关机（先确认） |
| `ql:MonitorOff` | 关闭显示器 |
| `ql:EmptyRecycleBin` | 清空回收站 |
| `ql:Search` | Windows 搜索（Win+S） |
| `ql:Run` | 运行对话框（Win+R） |
| `ql:ShowDesktop` | 显示桌面（Win+D） |

确认框出现在鼠标位置。在设置里切换语言时，没改过名的内置页面和按钮会跟着换成新语言。

### 设置

热角和延时、快捷键、窗口弹出位置、语言、主题、标签位置（左 / 上 / 右 / 下）、按钮和标签尺寸、图标大小、标签悬停延时、标签和按钮各自的字体、开机自动启动。切换主题或字体时主窗口马上显示效果，点取消恢复原样。

## 主题

<!-- 截图：docs/images/skins.png
![主题](docs/images/skins.png)
-->

在设置里选择主题。主题是 exe 旁边 `skins` 文件夹里的一个文件夹或 zip（`skins\名字.zip` 或 `skins\名字\skin.xml`），里面是 `skin.xml` 和 PNG 图片。切换主题时会套用主题推荐的标签位置和尺寸。

支持的内容：边框和背景（九宫格拉伸或平铺）、关闭和固定按钮（锚点定位）、标题、标签区和标签、按钮区和按钮的各种状态、文字颜色和阴影/描边、半透明窗口（alpha 通道和透明色）。二级标签和一级标签用同样的图。字体名始终用设置里的，字号和粗细用主题的。

自带的主题（Voyage、Sakura、Glacier 等）由 [`tools/make_skin.py`](tools/make_skin.py) 生成。在 [Realtime Colors](https://www.realtimecolors.com) 上选好配色，把链接交给脚本，就能生成一个同样造型的新主题：

```bat
python tools\make_skin.py MySkin "https://www.realtimecolors.com/?colors=ecf0e8-060705-c1cbb1-464365-a37ca8"
```

五个颜色依次是文字、背景、主色、辅色、强调色：按钮用辅色，悬停时往主色靠，选中的标签和图钉用强调色。详细用法见脚本开头的说明。

## 文件

所有文件都在 exe 旁边，整个文件夹可以直接拷走使用：

| 文件 | 内容 |
|---|---|
| `QLaunch.ini` | 设置 |
| `data.ini` | 页面和按钮 |
| `icons.dat` | 图标缓存，删除后会重新提取 |
| `lang/*.ini` | 语言包 |
| `skins/` | 主题 |
| `icons/*.ico` | 内置命令的图标。按钮的图标写相对路径时从 exe 所在目录算 |

`data.ini` 的节名就是位置（都从 0 数）：`[P03]` 是第 3 个页面，`[P03.R01C05]` 是它第 1 行第 5 列的按钮，`[P03.S01]` 是它的第 1 个二级页面，`[P03.S01.R01C05]` 是二级页面里的按钮。

翻译：把 `lang/en.ini` 复制成 `lang/<语言代码>.ini`（例如 `ja.ini`），翻译每行 `=` 后面的文字即可。

## 编译

需要 Visual Studio 2019 或 2022（Community 或 Build Tools）的“使用 C++ 的桌面开发”，并勾选其中的“用于 Windows 的 C++ CMake 工具”（带 CMake 和 Ninja）。

**VS Code**：打开本目录，安装推荐的扩展（CMake Tools、C/C++）。在状态栏选 `x64-debug` 或 `x64-release` 预设，按 `F7` 或 `Ctrl+Shift+B` 编译，`F5` 编译并调试。CMake Tools 会自己设置好 Visual Studio 的编译环境。

**命令行**，在“x64 Native Tools Command Prompt”里：

```bat
cmake --preset x64-release
cmake --build --preset x64-release
```

exe 输出在 `out\bin\Debug` 或 `out\bin\Release`，`dist\` 下的所有内容（语言包、主题、图标）会自动复制过去；中间文件在 `out\temp`。

程序图标和内置命令的图标由 [`tools/make_icons.py`](tools/make_icons.py) 生成。两个脚本都需要 Python 3.8+ 和 Pillow（`pip install pillow`）。

### 代码结构

```
src/
  main.cpp      入口：单实例、启动、消息循环
  app.h         设置 + 数据 + 图标缓存
  core/         ini、字符串、路径、RAII 句柄
  model/        页面和按钮、设置
  trigger/      热角（低级鼠标钩子）和全局快捷键
  icon/         图标提取（后台线程）和 icons.dat 缓存
  skin/         画布、皮肤接口、配色皮肤、图片主题（zip、XML、PNG）
  ui/           布局、主窗口、对话框、拖放
  launch/       启动程序、内置命令和系统页
  i18n/         语言包
res/            程序图标、清单、对话框资源
dist/           复制到 exe 旁边的文件：lang/、skins/、icons/
tools/          生成自带主题和图标的脚本
```

为了让 exe 小、内存占用少：静态链接 CRT，不用异常和 RTTI，不用任何 UI 框架；Shell 图标在后台线程提取并缓存，之后只读缓存；窗口隐藏时释放图标、画布和主题图片，并把工作集换出。

## 许可

[MIT](LICENSE)
