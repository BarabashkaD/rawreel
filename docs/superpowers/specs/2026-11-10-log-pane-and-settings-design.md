# Log Pane and Settings Window: Design

Status: revision 2, 2026-11-10, for review.
- It folds in a builder check and an optimistic and a pessimistic review; their findings are
  resolved in the text, and the calls are listed in §11.
- It replaces the plan draft `docs/superpowers/plans/2026-11-10-log-pane-hide.md`, which covered
  only §2 and §3; that plan is rewritten from this design.

## 1. Intent

Today:
- The Log pane is always on screen, fed by a 100 ms timer.
- It holds plain text that can only be selected by dragging.
- The log file and the system log have no settings.
- The one existing setting (`io.fileReads`) can only be changed by editing `rawreel.json` by hand.

This design:
- hides the Log pane by default and remembers whether the user wants it;
- feeds it without a timer;
- makes its lines selectable, copyable and savable;
- adds a Settings window for the log file, the system log and the file reads mode;
- saves every setting in `rawreel.json`;
- follows each OS's conventions (the project's native-per-OS rule: per-OS differences beat
  consistency).

It implements two items of the logger design's §10
(`docs/superpowers/specs/2026-11-09-dvv-logger-design.md`): its first bullet, and the
`SystemLogSink` part of its last bullet. The other §10 items (message review, crash wiring,
`Logger::start()` in the GUI) are not part of this design.

### Goals

- G1. The Log pane is hidden unless the user asked for it; its state is kept between runs.
- G2. No timer and no polling feeds the pane.
- G3. Whole lines can be selected (one, a range, or several), copied and saved; the whole log can
  be saved too.
- G4. A Settings window, opened the way each OS expects, edits the log file, the system log and the
  file reads mode.
- G5. Every setting, and the Log pane's state, is saved in `rawreel.json` without losing anything
  else in that file.

### Non-goals

- Filtering or searching the log, levels per sink in the GUI, colours per level.
- A setting for the log file's name, size or number of backups.
- A level choice for the log file or the system log.
- Remembering the window's size, the docks' layout or the Info pane's state.
- `Logger::start()` (asynchronous mode) in rawreel-gui.
- Any change to rawreel-cli's logging: it keeps its stderr sink.
- A single-instance rule: several rawreel-gui processes may run at once (§6.3).

### Anti-goals (a failure even if it works)

- A setting that is lost, or a config file that is damaged by a save: hand edits, unknown keys,
  links, permissions and an invalid file must all survive.
- A Settings window that feels foreign on its OS (an OK button on macOS, an instant-apply dialog on
  Windows).
- A Log pane that wakes up the app when nothing is logged.
- A log line lost because a setting changed.
- Selftests that touch the user's real `rawreel.json`, `rawreel.log`, system log or clipboard.
- A dialog that blocks quitting, logging out or a selftest.

## 2. The Log pane: shown, hidden and remembered

- **At start:** the pane starts as `gui.showLogPane` says (§7). When the key or the file is
  missing, it starts hidden.
- **The menu item** comes after "Restart GPU and reopen", with no key:
  - macOS: **View → Show Log** / **View → Hide Log**, a title that changes (the macOS convention
    for showing and hiding a part of a window);
  - Windows and Linux: **View → Log**, a check item.

  It follows the pane whichever way it was shown or hidden (`wxEVT_UPDATE_UI`).
- **Close button:** the pane's caption gets one (the Info pane keeps none).
- **Help → Diagnostics** shows the pane after it writes its lines.
- **Height:** within a run, the pane keeps the height the user gave it across hide and show. Each
  run starts at its default height.
- **Never automatic:** a warning or error never opens the pane by itself.
- **The status bar notice** is a fifth field that says how many warnings and errors were logged
  since the pane was last shown:
  - errors come first, singular for 1: `"1 warning in the log"`, `"2 errors, 3 warnings in the
    log"`;
  - it is empty while the pane is shown, and when there are none;
  - it counts lines logged before the main window existed (for example a config warning);
  - a left click on it shows the pane: wx has no click event per field, so this is a
    `wxEVT_LEFT_UP` on the status bar inside the field's rect;
  - its keyboard path is the View menu item.
- **Saving the state:** `gui.showLogPane` is written once, when the main window closes. This
  happens only when the pane's state then differs from the value read at start (so an untouched
  setting creates no file), and only by the settings owner (§6.3). It is window state, so it lives
  on the View menu, not in the Settings window.

## 3. The Log pane without a timer

### 3.1 The store

A wx-free log store in `src/core` replaces `LogPanelSink` and is the Logger's sink for the pane.

It keeps the last 5000 records, each with:
- its sequence number, counted from the start of the run;
- its level, time, message and source (`file.cpp:42`);
- the formatted line the Logger passed to `write()`.

It counts Warn and Error records, and has a "pending" flag.

`write()`:
- runs under the Logger's lock, on any thread;
- stores the record, dropping the oldest past 5000;
- updates the counts;
- if a notify function is set and nothing is pending, sets pending and calls the notify function.

`write()` must not log. The notify function is called under the store's lock and must only post
(`CallAfter`), never log.

`take(since)`, a single call under the store's lock:
- returns the records with a sequence number of `since` or more;
- returns the counts;
- returns a **gap flag**, true when records from `since` on were already dropped (more than 5000
  arrived since the last take);
- clears pending.

Taking and clearing in one call means a record written between them cannot be left behind
without a notify.

### 3.2 The GUI side

The main window's notify function posts to the GUI thread. The posted handler:
1. Calls `take(next)`, appends the records to a **GUI-thread mirror** (at most 5000 records; reset
   to the taken records when the gap flag is set), and sets `next` past the last one.
2. Updates the status bar notice.
3. Only while the pane is shown, applies the batch to the list control (§4.1).

The controls read only the mirror, never the store, so painting takes no lock and rows cannot
shift between a refresh and a paint. When the pane is shown, the control is rebuilt from the
mirror.

### 3.3 Rules

- **No logging in the refresh path:** the posted handler and the control update must not log. A
  line logged there posts another refresh, forever.
- **Lock order** is always Logger → store. GUI code never holds the store's lock while it does
  anything else.
- **Local time** is formatted on the GUI thread, outside any lock, with
  `localtime_r`/`localtime_s` (no `std::chrono::current_zone()`, which throws on systems without
  time zone data).
- **Shutdown:** the main window clears the store's notify function first thing in its destructor.
  Events already posted die with the window (`~wxEvtHandler`).
- **Logger mode:** rawreel stays in synchronous logger mode, so the signal comes from `write()`,
  not `batchEnd()`.

## 4. Selecting, copying and saving lines

### 4.1 The list

The pane becomes a virtual list with one row per record and multiple selection. The control is
chosen per OS for the native one (confirmed by the user):
- Windows: `wxListCtrl` in virtual report mode, a native ListView;
- macOS and Linux: `wxDataViewCtrl` with a virtual list model (native on both).

Both read the same mirror, so only a thin adapter differs.

**Columns:**
- **Time (local)**: `HH:MM:SS.mmm`;
- **Level**: `INFO`, `WARN`, ...;
- **Message**: a message of several lines (for example the GPU info JSON) shows its first line
  followed by " …", while copy and save carry the whole message;
- **Source**: `file.cpp:42`.

Fonts and row heights are the control's defaults.

**A refresh is one batch: d records dropped at the front and a appended.** For the batch:
- The selection, the selection anchor (the start of a Shift range), the focused row and the top
  visible row are remembered by sequence number before the batch and restored after it:
  - on Windows: `SetItemCount`, then shift the selection states by d and set the selection mark;
  - on macOS and Linux: reset the model, then restore by sequence number.
- Rows dropped by the cap leave the selection.
- **Tail behaviour:** if the last row was visible before the batch, the last row is visible
  after it. Otherwise the top visible record stays at the top, so dropping rows at the front does
  not move what the user is reading.
- No step deletes rows one by one (that is O(n²) on GTK with a large selection).

**Keys:** when the list has the focus it keeps every key, as an editable text field does today
(D17's char hook):
- Up, Down, Page Up, Page Down, Home and End move;
- Shift extends the selection;
- Space and Shift+Left/Right do not reach playback.

Playback keys work again once the focus leaves the list. `commandForKey`'s "text focus" input
becomes "the focus keeps its keys". It is true for an editable text entry, and for any window
whose parent chain contains the Log pane.

### 4.2 The Edit menu

A new **Edit** menu, between File and Playback, on every OS:

| Item | id | macOS | Windows | Linux |
|---|---|---|---|---|
| Undo | `wxID_UNDO` | ⌘Z | Ctrl+Z | Ctrl+Z |
| Redo | `wxID_REDO` | ⇧⌘Z | Ctrl+Y | Shift+Ctrl+Z |
| — | | | | |
| Cut | `wxID_CUT` | ⌘X | Ctrl+X | Ctrl+X |
| Copy | `wxID_COPY` | ⌘C | Ctrl+C | Ctrl+C |
| Paste | `wxID_PASTE` | ⌘V | Ctrl+V | Ctrl+V |
| Delete | `wxID_CLEAR` | none | none | none |
| — | | | | |
| Select All | `wxID_SELECTALL` | ⌘A | Ctrl+A | Ctrl+A |
| — (Linux only) | | | | |
| Preferences… (Linux only, §5.1) | `wxID_PREFERENCES` | | | Ctrl+, |

Delete uses `wxID_CLEAR`, which is what wxOSX maps to the native `delete:`. It has no key, so the
Delete key stays with the focused control on every OS.

How the items reach the focused control:
- **macOS:** wxOSX maps Cut, Copy, Paste, Delete and Select All (`wxID_CLEAR` for Delete) to the
  native `cut:`, `copy:`, `paste:`, `delete:` and `selectAll:`. These go down the responder chain,
  so they work and enable natively in the text fields of the main window, of modal dialogs (Open
  URI…) and of the Settings window. When nothing in the chain handles one (for example the Log
  list on `copy:`), it comes back as a wx menu event, and the main window maps it as below.
  wxOSX does not map Undo and Redo: they must still work in text fields, and the builder verifies
  how (the wx forwarding, or a small Cocoa helper that sends `undo:`/`redo:` to the first
  responder).
- **Windows and Linux:** menu events go to the main window, not to the focused control. The main
  window handles `wxEVT_MENU` and `wxEVT_UPDATE_UI` for these ids by looking at `FindFocus()` and
  calling the action directly, never by re-sending the event (which would climb back to the main
  window and recurse):
  - an editable `wxTextEntry`: `Undo`, `Redo`, `Cut`, `Copy`, `Paste`, removing the selection,
    `SelectAll`, each enabled by its `Can…`;
  - a read-only text control (the Info pane): Copy and Select All;
  - the Log list: Copy (the selected rows, enabled when a row is selected) and Select All;
  - anything else: disabled.

  A modal dialog disables the main menu on these OSes, and its fields handle Ctrl+C/V natively.

### 4.3 Copy and save

- **File → Save Log…** saves every record in the mirror.
- **File → Save Selected Lines…** saves the selected rows; it is disabled when no row is selected.
- **The Log list's context menu** (right-click, or Ctrl-click on macOS): Copy, Select All, a
  separator, Save Selected Lines…, Save Log….
- Neither save item has a key (rawreel has no documents, so Ctrl+S stays free).

The text that is copied or saved:
- one record per line, exactly as `rawreel.log` writes it (`formatLogRecord`:
  `2026-10-20T12:34:56.789Z INFO  message [file.cpp:42]`, UTC);
- a multi-line message keeps its lines;
- UTF-8, `\n` line ends, a final newline.

It is built by one wx-free function. Copy puts it on the clipboard as text (wx converts line ends
for the Windows clipboard). On Linux, `wxTheClipboard->Flush()` at exit keeps copied text after
rawreel quits.

"Save Log" saves the records the pane holds (the last 5000 of this run). The log file remains the
complete record.

**The selection is snapshotted before the dialog opens.** The records that are saved are the ones
selected when the command ran, even if new records drop them from the mirror while the dialog is
open.

### 4.4 The save dialog

- The native save dialog (`wxFileDialog`, save mode, overwrite prompt).
- The default name is `rawreel-log-YYYYMMDD-HHMMSS.txt`, in the local time of the save.
- Filters:
  - macOS: one type, plain text (`.txt`), with no format popup;
  - Windows and Linux: "Text files (*.txt;*.log)" and "All files".
- It starts in the folder of the previous log save in this run, else the system default.
- A target that is the live `rawreel.log` or one of its backups is refused, with a message: a file
  equivalent to one of them (`std::filesystem::equivalent`), or one of their names in the log
  folder in use, whether it exists yet or not (a rotation would overwrite it; the name compares
  without case on Windows and macOS).
- A failed write shows a native error message with the path and the reason, and logs one warning.

## 5. The Settings window

### 5.1 Opening and behaviour

`wxPreferencesEditor`, which gives each OS its convention:

| | macOS | Windows | Linux (GTK) |
|---|---|---|---|
| Menu item | **rawreel → Settings…** (⌘,), in the app menu | **File → Settings…** (Ctrl+,) | **Edit → Preferences…** (Ctrl+,) |
| Window | toolbar with page icons; the title follows the page | modal dialog with tabs, titled "Settings" | dialog with tabs, titled "Preferences" |
| Applying | each change at once, no buttons | **OK / Cancel**; OK applies | each change at once, Close button |

- The item uses `wxID_PREFERENCES`; wxOSX moves it to the app menu and keeps its label.
- Only one Settings window exists at a time; asking again raises it.
- **Lifetime:**
  - The main window owns the editor.
  - `MainFrame::onClose` calls `Dismiss()` before it shuts the Session down.
  - The page handlers do nothing once the main window is closing.
- **Showing values:**
  - The pages load their values when created and in `TransferDataToWindow`; macOS calls that on
    every show.
  - On GTK the window is only hidden between shows, so the pages also reload on `wxEVT_SHOW`.
- **Windows, OK:**
  - The editor's OK button runs `Validate()` and then `TransferDataFromWindow()` over all pages
    (validation is recursive in wx 3.3).
  - The Log page's `Validate()` opens a changed log folder (§5.3) without switching to it yet. If
    that fails, a native error message gives the path and the reason, the dialog stays open with
    every value as the user left it, and nothing is applied on any page.
  - `TransferDataFromWindow()` then applies every changed value and saves (§5.4).
  - Cancel applies nothing.
- **⌘W on macOS:** the Settings window is modeless and has no menu of its own, so ⌘W would reach
  File → Close stream. While the Settings window is the key window (it has the focus or is the
  active window), ⌘W closes it instead (the macOS convention); Close stream acts only on the main
  window. Other windows (a floating Info or Log pane) keep File → Close stream.
- **Page icons** (macOS only; wxOSX requires one per page): the SF Symbols `doc.text` (Log) and
  `folder` (Files). They come from a new helper in `native_controls_cocoa.mm`, which has no
  `NSImage` code yet. The builder checks them in light and dark appearance.

### 5.2 Pages

**Log**
- **Write a log file**: a checkbox (`log.file.enabled`, default on).
- **Folder**: a read-only field with the folder in use. When the value is `""` it shows the
  default folder as its placeholder. Next to it:
  - **Choose…**: the native folder dialog. Only its result is applied, so nothing happens while
    the user types or browses.
  - **Use Default**: sets the value back to `""`. It is disabled while the value is already `""`.
    Choosing the default folder's own path also stores `""`.
  - **Show in Finder** (macOS), **Show in Explorer** (Windows), **Open Folder** (Linux): opens the
    folder in use in the OS file manager, with `rawreel.log` selected where the OS can do that
    (`activateFileViewerSelectingURLs` on macOS, `explorer /select,` on Windows). It is disabled
    when the folder does not exist.

  When the folder in use is not the saved one (§5.3, at start), a line under the field says "Not
  available, writing to <folder in use>".
- **System log**: a checkbox (`log.system.enabled`, **default off**), labelled per OS:
  - macOS: **Send warnings and errors to the system log (Console)**;
  - Linux: **Send warnings and errors to the system log (journal/syslog)**;
  - Windows: **Send warnings and errors to the debugger output**, because `SystemLogSink` uses
    `OutputDebugStringW` there, which is not the Event Log.

**Files**
- **File reads**: three radio buttons, **Automatic**, **Unbuffered (no OS cache)** and **Buffered
  (OS cache)** (`io.fileReads`: `auto`, `unbuffered`, `buffered`). Help text: "Applies to files
  opened after the change, and to reopens."
- At the bottom: **Settings file:** the path of the config file in use (§6.1). In a second copy
  (§6.3), the read-only line goes here too.

### 5.3 Applying a setting

**The sinks are added once, at start, and never removed:**
- the store;
- the stderr sink (`gui/main.cpp`, as today);
- a **log file switch**: an App-owned forwarding sink that holds at most one `RotatingFileSink`
  behind its own mutex (lock order Logger → switch);
- one `SystemLogSink("rawreel")`.

Turning things on and off never touches the Logger's sink list, so no record is lost while a
setting changes, and this keeps working after a later `Logger::start()`:
- **System log on/off:** `setSinkLevel(system_sink, Warn)` or `Off`. The Logger allows this at any
  time, and it never creates a second `SystemLogSink`: on Linux a second one's `openlog` would be
  undone by the first one's `closelog`.
- **Log file off:** the switch drops its file sink, which is destroyed outside both locks.
- **Log file on, or a new folder:**
  1. The folder is prepared: if it does not exist, only its last component is created, and only
     when its parent exists. This way a folder on an unmounted volume does not get a fake tree.
     The default folder (the user data folder) is the exception: it is created with its parents,
     as today, so a fresh account still gets a log.
  2. A new `RotatingFileSink` opens `rawreel.log` there, outside the locks, with the same rotation
     as today (`LOG_FILE_BYTES`, `LOG_FILE_BACKUPS`).
  3. Only then does the old file get one info line, `log file moves to '<new path>'`.
  4. The switch swaps in the new sink, and the old one is destroyed outside the locks.

  The old files stay where they were. If step 1 or 2 fails, the old file stays in use and a
  native error message gives the path and the reason. On macOS and Linux the field then keeps the
  old value; on Windows the failure is caught by OK's `Validate()`.
- **Use Default** while the default folder is already in use (after a fallback, see "At start"
  below) changes the saved value only; it does not reopen the file. Folders are compared with
  `std::filesystem::equivalent`.
- **File reads:** the Session's mode becomes an atomic, because worker threads read it at every
  open. A change applies to every later open, including Restart GPU and reopen and a lost-device
  reopen of the current stream.

**At start**, `App::OnInit`:
1. adds the store, the stderr sink, the empty log file switch and the system sink (level `Off`);
2. reads the config (§6), whose lines go to the store and stderr;
3. sets the system sink's level from `log.system.enabled`;
4. opens the log file when `log.file.enabled`. At this first open, the switch first writes into
   the new file the lines the store already holds, so no config warning is missing from the file.

If the saved folder cannot be used (deleted, or on an unplugged drive):
- rawreel prepares it as in step 1 above;
- if that fails, it writes to the default folder and logs one warning, `log folder '<saved>'
  cannot be used: <reason>; writing to '<default>'`;
- the saved value stays in the config, and the Log page shows the "Not available" line;
- if the default folder fails too, there is no log file, as today.

### 5.4 Saving

Only the settings owner saves (§6.3).
- On macOS and Linux each applied change is saved at once; on Windows, OK saves every changed
  value.
- `gui.showLogPane` is saved at close (§2).
- A failed save logs one warning, `config '<path>' not saved: <reason>`. The setting stays applied
  for the session.
- The first failure in a run also shows a native error message, except:
  - when the main window is closing: no dialog then (it would block quitting and logging out);
  - in a selftest, where every dialog goes through a hook.

## 6. The config file

### 6.1 Which file

The default config file, for both executables:
- **macOS, executable inside an app bundle:** `~/Library/Application Support/rawreel/rawreel.json`,
  as today.
- **Anywhere else** (Windows, Linux, and macOS outside a bundle):
  - `rawreel.json` next to the executable **if that file exists** (a portable setup, as today);
  - otherwise `rawreel.json` in the user data folder (`%APPDATA%\rawreel\` on Windows,
    `~/.local/share/rawreel/` or `$XDG_DATA_HOME/rawreel/` on Linux, `~/Library/Application
    Support/rawreel/` on macOS). This is the folder `rawreel.log` already uses by default.

Without this, unpacking a new version into a new versioned folder would lose the settings, and an
install in Program Files or /opt could never save.

`--config <file>` still names another file. Its path is made absolute against the working folder
at start, so later saves go to the same file.

### 6.2 Keys

All keys are optional, and the file stays optional:

```json
{
  "io":  { "fileReads": "auto" },
  "gui": { "showLogPane": false },
  "log": {
    "file":   { "enabled": true, "folder": "" },
    "system": { "enabled": false }
  }
}
```

| Key | Values | Default | Used by |
|---|---|---|---|
| `io.fileReads` | `auto`, `unbuffered`, `buffered` | `auto` | both executables |
| `gui.showLogPane` | `true`, `false` | `false` | rawreel-gui |
| `log.file.enabled` | `true`, `false` | `true` | rawreel-gui |
| `log.file.folder` | an absolute folder path, UTF-8; `""` = the default | `""` | rawreel-gui |
| `log.system.enabled` | `true`, `false` | `false` | rawreel-gui |

**Reading** keeps today's rules, with one shared parser for both executables (C12):
- A wrong type or a bad value means one warning and the defaults for the whole file. The messages
  name the key, e.g. `gui.showLogPane: expected true or false, not "yes"`.
- A relative `log.file.folder` is a bad value.
- Unknown keys are ignored.

So a typo in a `gui` or `log` key also makes rawreel-cli use the defaults. A value that only a
newer rawreel knows makes an older one ignore the file and refuse to save into it. The README
states both facts.

### 6.3 Who may write: the settings owner

Several rawreel-gui processes may run at once (to compare two clips). Only the first one owns the
settings:
- **The claim:** at start, rawreel-gui claims the config file through a small claim of its own in
  `src/core`, keyed by the file's canonical path:
  - on Windows, a named mutex whose name is derived from that path;
  - on macOS and Linux, an exclusive `flock` on `<config>.lock` next to the config file. A second
    claim conflicts even inside the same process, which `wxSingleInstanceChecker` does not do on
    Unix.

  The OS releases the claim when the process ends, so a crash leaves no stale owner. The lock
  file may stay behind; it is empty and harmless. When the config's folder cannot be written, the
  claim fails as "not the owner", which is right, because such a file cannot be saved anyway.
- **A second copy** (the claim failed) runs normally but never writes the config:
  - its Settings window works, and its changes apply to that process only;
  - a line at the top of each page says "Another rawreel window owns the settings. Changes here
    apply to this window only and are not saved.";
  - its Log pane state is not saved at close.
- **Ownership** is decided at start and does not pass on when the owner quits.
- **rawreel-cli** never claims and never writes.

### 6.4 Writing

One wx-free function in `src/core` merges a set of key values into the file as it is at that
moment (the user may have edited it while the app ran):

1. **Find the target.** If the path is a symbolic link, the target is the file it points to
   (resolved), so the link stays a link.
2. **Refuse what must not be written.**
   - It refuses a file the loader would reject: invalid JSON, a top level that is not an object, a
     parent of a set key that is not an object, or a bad value in any key. Such a file is left
     exactly as it is, the save fails with the loader's reason, and the user's choices then hold
     for this session only (C13).
   - It refuses a read-only file ("read-only").
   - The same 1 MiB limit applies as for the loader.
3. **Merge.** It sets only the given keys, creating objects on the way. Every other key stays,
   unknown ones too. The file's key order is kept (`nlohmann::ordered_json` in the writer), with a
   two-space indent and a final newline.
4. **Write safely.**
   - It writes a uniquely named temporary file in the target's folder,
     `<name>.<pid>.<random>.tmp`, created exclusively, with the target's permission bits on POSIX.
   - It flushes the file to disk (`fsync`; `F_FULLFSYNC` on macOS).
   - It replaces the target: `rename` on POSIX; `ReplaceFileW` on Windows, which keeps the file's
     attributes and ACL.
   - On failure, the temporary file is removed.
5. **A missing file** is created, together with its folder: the user data folder exists normally;
   only its last component is created.

The function itself never logs and never retries; the caller reports a failure (§5.4). On Windows,
a file held open without delete sharing (an editor, a sync client, an antivirus scan) makes the
save fail with that reason.

## 7. Selftests and tests

### 7.1 Selftest isolation

A `--selftest` run without `--config` uses its own config folder, which `App::OnInit` creates
before it reads the config: `<temp>/rawreel-selftest-config-<pid>-<random>/`, where `<random>` is
16 hex digits. It is created exclusively (an existing path, a symbolic link included, is never
reused; a few new random names are tried), with mode 0700 on POSIX, and so is its `logs/`. It is
separate from the smoke scenario's scratch folder, which a step deletes mid-run. It holds:
- a `rawreel.json` with `log.file.folder` set to a `logs/` folder inside it;
- `log.system.enabled` false.

So a selftest never touches the user's config, log file or system log: when its `logs/` cannot
be opened, it runs without a log file instead of falling back to the default folder. The folder
is removed in
`OnExit` after the log file is closed. A selftest given `--config` starts its own scratch config as a copy of that file and never writes the original.

Every dialog and every OS hand-off in a selftest goes through a hook, so nothing blocks and
nothing reaches the desktop:
- the save dialog;
- the folder dialog;
- error messages;
- Show in Finder/Explorer;
- the clipboard: a hook captures the copied text, so the developer's clipboard is untouched.

On Windows, where the Settings dialog is modal, the selftest drives its pages through a hook
installed when the editor creates them. It ends the dialog with OK through that hook, so no
selftest step runs nested inside the modal loop.

Steps that compare log text lengths compare sequence numbers instead, because the store is capped.

Every test, not only the selftests, runs with `HOME`, `XDG_DATA_HOME` and `APPDATA` pointing into
a folder of the build tree (`DVV_TEST_ENVIRONMENT`). The new default config location (§6.1) then
never makes a test read or write the developer's real user data folder.

### 7.2 Unit tests (`rawreel_core_tests`, wx-free)

- **The log store:**
  - order and sequence numbers;
  - the 5000 cap and the gap flag;
  - the counts;
  - notify once per pending, and `take` clearing it;
  - a notify set after records were written;
  - a notify cleared;
  - writes from several threads.
- **Text functions:**
  - the notice text;
  - the copy/save text of records;
  - the default save-file name.
- **The batch-to-selection mapping** (wx-free): given the selected sequence numbers, the anchor,
  the top row, d and a, the new row indices.
- **The config:**
  - the default path rule of §6.1 (portable file present or not, app bundle);
  - parse of every new key and its bad values, and a relative folder;
  - the writer:
    - creating the file;
    - keeping other and unknown keys and the key order;
    - re-reading the file;
    - leaving an invalid file or a file with a bad value unchanged;
    - refusing a read-only file;
    - keeping a symlink a symlink, and keeping the permission bits on POSIX;
    - leaving no temporary file behind.

### 7.3 GUI selftests (smoke scenario)

**The pane:**
- hidden at start;
- the notice after a warning;
- shown through the View item, with its check mark or title;
- hidden through the close button's path;
- a burst of more than 5000 records while hidden;
- Diagnostics shows it.

**The list:**
- the Log list keeps its keys (Space does not start playback);
- Select All and Copy (the clipboard hook gets the expected text);
- Save Log and Save Selected Lines through the dialog hook, into the scratch folder;
- a refused save over the live `rawreel.log`.

**The Edit menu:** Copy and Select All in the Info pane.

**Settings:**
- opening the Settings window;
- changing the log folder: the new `logs2/` gets `rawreel.log`, and the old file has the
  "moves to" line;
- turning the system log on and off;
- changing the file reads mode;
- on macOS, ⌘W with the Settings window in front closes it, and the stream stays open.

The scratch `rawreel.json` holds every new value.

**Closing:** `gui.showLogPane` is written at close, which is checked in `OnExit`, because the step
runner has stopped by then.

**The second copy:** the claim is unit-tested in core: with two claims on one path, the second
fails; after the first is released, a new one succeeds. The selftest checks that the app owns its
scratch config. A real second process is not started.

## 8. README

The rawreel-gui section covers:
- the Log pane: hidden by default, the View item per OS, and the choice is kept;
- the status bar notice;
- the Edit menu;
- Save Log / Save Selected Lines;
- the Settings window, and where it is on each OS;
- the second-copy rule.

The Configuration section covers:
- the new default location rule (§6.1), and the keys and table from §6.2;
- that rawreel-gui writes the file: merging, keeping other keys and the key order, keeping links
  and permissions;
- the "not saved" warning;
- that a bad value in any key makes both executables use the defaults;
- that an older rawreel ignores a file holding a value only a newer one knows.

All of it states facts only.

## 9. Left to the builder

- The adapter code of the two list controls (within §4.1's rules).
- How Undo/Redo reach text fields on macOS (§4.2).
- How the selftest hooks are installed (§7.1).
- Task order and commit boundaries.

## 10. Known limits

- A second copy's changes are not saved, even after the owner quits (§6.3).
- On Windows, a config file held open without delete sharing cannot be saved until it is closed.
- The system log gets warnings as they are, file and folder names included (on macOS dvv-base logs
  them as public). For a raw-video player this is accepted (user, 2026-11-10).
- The DataView list (macOS, Linux) does not restore the selection anchor after a refresh (wx has no
  API for it).
- ⌘W closes only the Settings window; other windows (a floating Info or Log pane) keep File → Close
  stream (§5.1).
- A save by rename on macOS and Linux (§6.4) keeps the permission bits but not ACLs, extended
  attributes or the owner.

## 11. Calls made by the designer (check these)

- C1. A list with columns (Time (local), Level, Message, Source) instead of a plain text line per
  row.
- C2. A per-OS control: `wxListCtrl` on Windows, `wxDataViewCtrl` on macOS and Linux. Confirmed by
  the user on 2026-11-10 ("keep per OS").
- C3. No keys for Save Log… / Save Selected Lines…; Settings on ⌘, / Ctrl+,.
- C4. The system log is off by default on every OS, so nothing changes for current users. It takes
  Warn and Error only, with no level setting. The default was confirmed by the user on
  2026-11-10.
- C5. A config file that cannot be read is never overwritten by a save.
- C6. "Save Log" saves the pane's records (at most 5000), not the log file.
- C7. Settings are saved at once on macOS and Linux, and on OK on Windows; the Log pane's state is
  saved at close.
- C8. The full standard Edit menu on every OS, with `wxID_CLEAR` for Delete and no key for it.
- C9. The focused Log list keeps every key; playback keys do not reach the player then.
- C10. On Windows, a log folder that fails on OK keeps the dialog open and applies nothing.
- C11. A saved log folder that cannot be used at start falls back to the default folder, with a
  warning.
- C12. One shared parser: rawreel-cli also rejects a file with a bad `gui`/`log` value.
- C13. A file with a bad value is never saved into; settings then hold for the session only.
- C14. **Use Default** resets the log folder to `""`.
- C15. macOS shows "Show Log"/"Hide Log"; Windows and Linux show a "Log" check item.
- C16. Linux puts Preferences in the Edit menu; Windows puts Settings in the File menu.
- C17. Only the first rawreel-gui process saves settings (the user's rule); ownership does not
  pass on.
- C18. The config file goes to the user data folder unless a portable `rawreel.json` exists next
  to the executable (the user approved this recommendation).
