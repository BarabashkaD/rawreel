# Open Recent — design

Status: approved by the user, rev 2 (2026-11-11). Rev 2 applies the optimistic and the pessimistic
review of rev 1 and the lead's rulings (see "Review notes").

## 1. Intent

rawreel has no memory of what was opened. To look at the same clip again the user goes through
File > Open... and the folders again, or types the URI again. Open Recent lists the last opened
sources in the File menu; choosing one opens it again.

## 2. Requirements (user, 2026-11-11, binding)

1. File > Open Recent lists what was opened recently; choosing an entry opens the same text again,
   as if it were typed again.
2. An entry is the full path of a file, or the URI string as it was opened. Nothing else is stored
   (no format choice, no position). A raw file that needed the Format dialog asks again.
3. Every source counts: files and every URI kind rawreel opens.
4. Newest first; opening an entry again moves it to the top; no duplicates.
5. No checks while the list is shown. When an open fails because the file is missing or not
   accessible, its entry is removed (§6).
6. No privacy setting, no clear command, no Clear Menu item on any OS.
7. Size: default 5, range 1–10, set in the Settings window, stored in rawreel.json.
8. rawreel-cli never adds entries, has no history command, and ignores the history keys.
   `rawreel-gui <file>` is a GUI open and adds the entry.
9. The menu follows each OS's usual form.
10. macOS system integration (NSDocumentController, the Dock menu, Apple menu > Recent Items) is
    out of scope.
11. The list is stored in the same rawreel.json as the settings, also with `--config` and the
    portable file; the file may be rewritten on each successful open.
12. rawreel.json can be edited outside the app, between runs and while it runs; wrong values load
    safely with a defined result and a log warning (§8).
13. The size is the real limit of what is stored: lowering it deletes the extra entries for good;
    raising it later brings nothing back.

Also agreed: a failed or cancelled open never adds an entry; the same behaviour on all three OSes.

## 3. The menu

The submenu `Open Recent` sits right after Open URI... in the File menu on every OS. It has no
key and no mnemonic (no File menu label has one today).

| | macOS | Windows | Linux (GTK) |
|---|---|---|---|
| Entry label | `<short>` | `&1 <short>` … `&9 <short>`, `1&0 <short>` | `&1. <short>` … `&10. <short>` (GTK3's recent menu form `_N. name`) |
| `<short>` of a file entry | the file name | the full text, cut | the file name |
| `<short>` of any other entry | the full text, cut | the same | the same |
| Entry help | the full text | the same | the same |
| Empty list | the submenu item is disabled | the same | the same |

- **File entry**: the entry parses (`dvv::parseUri`) with the scheme `file` (a bare path or a
  `file:` URI). Its file name is `streamName()` (core/stream_name.h): the base name without the
  query.
- **Cut**: in the middle to 60 characters (code points) with `...`, the form of the Format
  dialog's title.
- **Same label**: when two entries would get the same `<short>`, those entries show their full
  text, not cut. Entries are distinct texts, so these labels differ.
- **Text**: every `&` of the text is written `&&`. Entries hold no control characters (§4), so a
  label never holds a TAB (wx would read the rest as an accelerator).
- **Access keys**: Windows' guidelines ask for numeric access keys on dynamic items such as
  recently used files (Windows UX guide, Menus and Keyboard). wxOSX strips mnemonics, and
  Apple's Open Recent shows names only.
- **Help text**: wx shows a highlighted item's help in the status bar's first field and restores it
  afterwards (`wxFrameBase::DoGiveHelp`, src/common/framecmn.cpp), on all three ports. That is
  where the full text of a short label shows.
- **Ids**: `RECENT_ID_BASE = wxID_HIGHEST + 80`, ten ids up to +89. In use today: +60 and +61 (the
  infobar buttons), +70 to +78 (the Log pane's context menu, src/gui/log_panel.cpp) and +100 on
  (the commands). The submenu is not a command of the command table.
- **Updates**: every change of the list rebuilds the submenu's items and enables or disables the
  submenu item. Nothing is read while the menu is open.

### 3.1 Why not wxFileHistory

The agreed plan was to build on `wxFileHistory` where it fits. Read against wx 3.3.4, it fits no
part:

- On wxGTK `AddFileToHistory` also adds the file to GTK's recent list (src/gtk/filehistory.cpp),
  which is OS integration (requirement 10), and a selftest would write to the user's list.
- `wxFileHistoryBase` treats every entry as a file name: it compares through `wxFileName`
  normalisation (absolute, case, dots, tilde), which mangles `lavfi:` and `synthetic:` entries,
  and its label cuts at the last `/` (src/common/filehistorycmn.cpp).
- The label is one fixed form, `&<n> <path>`, built in a helper no subclass can change, so macOS
  cannot drop the numbers.
- The maximum count is set only in the constructor, and Load/Save use `wxConfigBase` keys, not
  rawreel.json.

So the list is a small wx-free class in `src/core` (unit tested), and the GUI builds the submenu
with `wxMenu::AppendSubMenu` and plain items.

## 4. What an entry is

Each route passes its entry to `MainFrame::openUri` explicitly, beside the text it opens. The
entry is UTF-8 (paths through `dvv::media::utf8FromPath`).

| Route | Opens | Entry |
|---|---|---|
| File > Open..., a drop, macOS Finder / Dock (all through `openFiles`) | `fileUri(path)` of the first file | the path as given (absolute); its `file:` URI (`fileUri`) when the path contains `?` |
| `rawreel-gui <text>`, File > Open URI... (`cleanUriText`) | the text | a bare path (`hasScheme()` false): its path part made absolute by joining it to the working folder (no `..` resolved; not `std::filesystem::absolute`, which resolves `..` on Windows), a query kept as typed; a text with a scheme: as given |
| Open Recent | the entry | the entry |
| A Format dialog's OK (§5) | the text with the choice applied | the entry of the open that asked |
| View > Format..., Restart GPU | — | none |

- A bare path's `?` starts a query (dvv/base/uri.h), so a file path with `?` is kept as its
  `file:` URI to read back as the same file.
- **Valid entry**: valid UTF-8, no control characters (below U+0020, and U+007F), at most 4096
  bytes. Anything else is not recorded and leaves one Log info line
  `not added to Open Recent: <reason>`. This keeps labels well formed, keeps the saved text equal
  to the one in memory (the writer would replace invalid UTF-8), and keeps ten entries far below
  the 1 MiB config limit.
- **Same entry**: equal texts, byte for byte. A bare path and a `file:` URI of the same file, or two
  spellings of one path on a case-insensitive file system, are two entries.
- **Order**: newest first. Adding an entry that is there moves it to the top; when the list is
  full, the oldest goes.

## 5. When an entry is recorded

Only after a successful open. An open is asynchronous: `openUri` calls `Session::closeStreams()`
and `Session::open()`, and the outcome arrives later as a Session event (`streamsChanged` with a
stream, `formatNeeded` or `error`). Events reach the frame through `CallAfter`, so an earlier
open's events can arrive after a later `openUri`.

- **Requested URI.** `StreamInfo` gains `requestedUri`: `dvv::formatUri` of the request the open
  was given, before the provider resolved it (`resolvedUri` adds the detected keys). The Session
  fills it in `attachSource` from the open job's request. `formatNeeded` already carries the
  request.
- **Pending entry.** `openUri(slot, text, start, entry)` keeps `{entry, formatUri(parsed text)}` as
  the frame's pending entry, replacing any earlier one. A text that does not parse starts no open
  and leaves the pending entry as it is.
- **Success**: a `streamsChanged` whose stream's `requestedUri` equals the pending one records the
  entry (§4), saves the list when it changed (§7) and clears the pending entry. A stream of an
  older open has another URI and records nothing.
- **Format needed**: a `formatNeeded` whose request matches moves the entry into the queued dialog
  (`PendingFormat` gains `entry`). OK → `openWithChoice` → `openUri` with that entry. Cancel →
  nothing.
- **Error**: records nothing and leaves the pending entry in place. An `Errc::Io` error starts the
  removal check (§6). When the error offers the infobar's Format... button (D4b), the pending
  entry is kept with `format_error_uri_` and goes with that dialog's reopen; `dismissMessage()`
  clears both.
- **Close stream** clears the pending entry.
- An open that fails only because the GPU could not be started keeps its request for Restart GPU
  (`last_stream_`); when the restart shows it, the URI matches and the entry is recorded.

## 6. Removal after a failed open

After any open fails with `Errc::Io`, the pending entry is checked when its text is in the list.
The error carries no system code (`'<path>' could not be opened: <reason>`), so the cause is
decided by one look at the file:

- The file: the parsed path of the entry (`dvv::parseUri(entry)->path`, without the query) when the
  scheme is `file`. Any other scheme is never checked or removed.
- The check is wx-free core code (`std::filesystem::status`, then an open for reading):
  **missing** (not found, also a missing folder on the way), **not accessible** (the status query
  fails, not a regular file, or it cannot be opened for reading), else **readable**.
- It runs on the App's io executor, not the GUI thread (a dead network share can block it). The
  result comes back through `CallAfter`; nothing happens when the main window is closing then.
- Missing or not accessible, and the entry still in the list: the entry is removed, the list
  saved, and one Log line follows the open's own warning: `"<entry>" removed from Open Recent:
  <missing | not accessible>`.
- Readable, or another error code: the entry stays.

## 7. Storage

Two keys in `gui`, in the same rawreel.json:

```json
{ "gui": { "showLogPane": false,
           "recentSize": 5,
           "recent": ["/data/clips/clip_1920x1080_v210.yuv", "lavfi:testsrc2=size=1280x720"] } }
```

- `gui.recentSize`: a whole number from 1 to 10; default 5. `gui.recent`: an array of strings,
  newest first; default empty.
- **Owner.** The App owns the list (loaded with the config); the frame builds the submenu from it.
- **Load.** `parseConfig` does not read the history keys (it reads only the keys it knows,
  core/config.cpp), so rawreel-cli ignores them and the writer's check of the file never refuses a
  save because of them. `LoadedConfig` keeps the text it read; rawreel-gui passes it to a separate
  core function, `parseRecent`, which gives the size, the entries and the §8.3 warnings. The GUI
  logs each warning as `config '<path>': <warning>`, then one info line
  `config '<path>': gui.recentSize 5, gui.recent 3 entries`.
- **Writer.** `ConfigValue::value` gains `std::int64_t` and `std::vector<std::string>`. The writer
  also refuses a merged text over `CONFIG_MAX_BYTES` (Errc::Format, the loader's "too large"), so
  no save can produce a file the next start ignores. It already re-reads the file at save time
  and sets only the given keys.
- **Saves of the list.** A new `bool App::saveHistory(std::span<const ConfigValue> values)` writes
  `gui.recent` after an add or a removal that changed the list (reopening the top entry saves
  nothing). Like `saveSettings` it does nothing unless this process owns the settings. A failure
  shows no dialog, logs `config '<path>' not saved: <reason>` on the run's first history failure
  only (its own flag), and never sets `save_failed_`, so a later Settings failure still shows its
  dialog. The list stays in effect for the run. `App::saveSettings` is unchanged.
- **Size change** is a Settings change (§9): `saveSettings` with `gui.recentSize` and, when the list
  was cut, `gui.recent`.
- Selftests use their own config in a temporary folder (`App::startLogAndConfig`).

## 8. Outside edits and wrong values

### 8.1 Edits between runs

The file is read once at start; wrong values load as in §8.3. The cleaned list is written back only
with the next save, never at start.

### 8.2 Edits while rawreel runs

- **Other keys** are kept: each save re-reads the file and sets only its own keys. They apply at
  the next start (as today).
- **`gui.recent`**: the app's list wins. The next history save writes the whole array from memory.
  Without such a save, the outside edit stays and loads at the next start.
- **`gui.recentSize`** is written only by Settings, so an outside edit stays in the file; this run
  keeps its size, and the next start uses the file's value.
- **A file made invalid** (malformed JSON, a wrong value of another key): the writer refuses and
  leaves the file unchanged; the save failure is logged as in §7; the list is written by the first
  save after the file is valid again.

### 8.3 Wrong values on load

Each case loads; the rest of the config is used as loaded. Warnings are `DVV_LOG_WARN`. A bad value
is shown as compact JSON (`dvv::json::dump`), as the other keys show theirs. The rules run in this
order: size, list shape, entries, length.

| Input | Result | Warning |
|---|---|---|
| no `gui.recentSize` | size 5 | none |
| `gui.recentSize` not a whole number from 1 to 10 (`"5"`, `true`, `null`, `5.5`, `1e30`, `0`, `11`) | size 5 | `gui.recentSize: expected a whole number from 1 to 10, not "5"; using 5` |
| no `gui.recent` | empty list | none |
| `gui.recent` not an array (`"x"`, `3`, `null`, `{}`) | empty list | `gui.recent: expected an array of strings, not "x"; the list is empty` |
| entries that are not strings, empty strings, invalid entries (§4), duplicates | dropped (of duplicates the first, newest, stays) | one line: `gui.recent: ignored 2 entries that are not strings, 1 empty, 1 invalid, 1 duplicate` (only the kinds that occur) |
| more entries than the size (after the drops) | the first `size` kept | `gui.recent: 7 entries, more than gui.recentSize 5; the first 5 kept` |
| entries that are no valid path or URI (`"clip.yuv"`, `"bad:"`) | kept | none; a reopen fails like the typed text |
| `gui` not an object, malformed JSON, invalid UTF-8, a wrong value of another key, a file over 1 MiB | the whole file is ignored (today's rule and warning); empty list, size 5 | `config '<path>' ignored: <reason>; using the defaults` |

## 9. Settings control

A row on the Files page, above the Settings file line: `Items in Open Recent:` and a pop-up
(`wxChoice`, 1 to 10) on every OS, the control the Log page's Level already uses.

- A choice is one committed value, so the list is cut and saved once per change, never per step
  of a spin arrow.
- It follows the page's existing model (src/gui/settings.cpp): macOS and GTK apply and save each
  change at once (`ShouldApplyChangesImmediately`); Windows applies and saves on OK with the
  page's other values.
- Lowering the size cuts the list for good (menu and file); raising it brings nothing back
  (requirement 13). `TransferDataToWindow` shows the size in effect.

## 10. rawreel-cli

No change. It loads rawreel.json through `loadConfig`, which ignores the history keys.

## 11. Tests

Core unit tests (doctest, wx-free):

- Entry text per route: a path; a path with `?` becomes its `file:` URI; a relative bare path made
  absolute with `..` kept; a bare path with a query; a URI kept as typed; UTF-8 names. Invalid
  entries: bad UTF-8, a control character, 4097 bytes.
- The list: add on top, move to the top, no duplicates (exact text), the oldest dropped when full,
  remove, resize down cuts and up brings nothing back, "changed" false when the top is re-added.
- Labels per OS style: file name, full text and the 60-character cut (on UTF-8 text); a clash
  gives the full text uncut; `&1`…`&9`, `1&0` (Windows), `&1.`…`&10.` (Linux), none (macOS);
  `&` doubled.
- `parseRecent`: every row of §8.3 with the exact warning texts; a valid file with bad history
  values still gives the other keys through `parseConfig`.
- The removal check: missing file, missing folder, a folder instead of a file, a readable file, an
  unreadable file (POSIX, skipped as root); a non-file URI is never checked; a bare path with a
  query checks the path without it.
- Session: a successful open's `StreamInfo::requestedUri` is the request's `formatUri` while
  `resolvedUri` carries the detected keys.
- Config writer: an integer and a string array are written; other and unknown keys stay; an edit
  between load and save is kept; bad history values never refuse a save; a merged text over 1 MiB
  is refused and the file left unchanged.
- CLI: a config with valid and invalid history keys loads without a warning.

Selftest steps, in the `player` scenario (it has a temporary folder; `smoke` also runs in the
package tests and stays unchanged). Each checks its effect in a later event-loop turn; menu items
are chosen by injecting `wxEVT_MENU` with the entry's id; the Format dialog through its hook. The
existing selftest callers of `openUri` pass no entry unless the step checks the list.

- The submenu is labelled `Open Recent` and lists what the scenario's earlier opens recorded, as
  the config file's `gui.recent` has it, with the per-OS labels and the full text as help.
- A temporary file, then a `synthetic:` URI: both on top, newest first, in the menu and the file.
- Choosing the second entry reopens it and moves it to the top; no duplicate.
- A failed open (a missing file) and a cancelled Format dialog add nothing.
- A raw file without a size: Format dialog OK opens it; the entry is its path without the choice.
- Settings: size 1 cuts the menu and the file to one entry and saves `gui.recentSize`; size 2
  brings nothing back.
- Deleting that last listed file and choosing it: the open fails, the entry leaves the menu and
  the file, and the submenu item is disabled.

Manual checks per OS: the submenu's place and labels, the access keys on Windows and Linux, the
status bar help while an entry is highlighted, the Settings control.

## 12. Docs

README: File > Open Recent, the Settings row, and the two keys in the config section. Facts only.

## 13. Risks

- **Event matching.** Recording depends on the Session filling `requestedUri` from the open's
  request; the Session test pins it.
- **A save per open.** Each successful open that changes the list rewrites rawreel.json (accepted
  in requirement 11): a short synchronous write with a flush, as the Settings saves do.
- **Broad "not accessible".** A file held open exclusively by another program (Windows) or a
  disconnected network drive counts as not accessible and is removed.
- **A slow check.** On a dead network share the removal check holds one io thread until the OS
  gives up, as the failed open did.
- **Lost outside edits of `gui.recent`** by the next history save (§8.2).
- **Two instances.** Only the settings owner saves; a second rawreel-gui's list lives for its run.

## 14. Decisions to confirm

1. **Removal on any failed open, not only from the menu** (lead's call; widens requirement 5).
   After any open fails with `Errc::Io`, its entry is removed when it is in the list and its file
   is missing or not accessible; the check runs off the GUI thread on the parsed path.
2. **No wxFileHistory** (writer's call; changes an agreed point). §3.1: on wxGTK it writes to GTK's
   recent list, it treats URIs as file names, its labels cannot follow each OS, and it has no size
   setter.
3. **Recording matches the open by its requested URI** (our call). `StreamInfo` gains
   `requestedUri`, a small Session change; this replaces rev 1's arming on the first empty
   `streamsChanged`, which could record a later failed open. A side effect: an open that failed
   only because the GPU could not start is recorded when Restart GPU then shows it.
4. **Size control: a `wxChoice` 1–10 on every OS** (our call, under the lead's "apply on commit"),
   applied the way the page applies its other values (at once on macOS and GTK, on OK on Windows).
   One selection cuts for good. The alternative, a spin control on Windows and Linux, would need
   a separate commit point.
5. **Labels** (lead's call): `Open Recent` on every OS; Linux `&N. name`; Windows `&N` and the full
   text cut to 60 characters; macOS names only; non-file URIs always their full text, cut; two
   entries with the same label show their full text.
6. **A failed history save** (lead's call): no dialog, one log line per run, a new
   `App::saveHistory` with its own flag; `saveSettings` and `save_failed_` unchanged.
7. **Exact text comparison** (lead's call): no Windows case or slash rule; a bare path and a
   `file:` URI of one file are two entries.
8. **Entry limits** (lead's call, extended): at most 4096 bytes, valid UTF-8, no control
   characters, else not recorded with a log line; the writer refuses any save over 1 MiB.
9. **The app's list wins for `gui.recent`; `gui.recentSize` is written only by Settings** (writer's
   call).
10. **Wrong size → the default 5, not clamped** (writer's call), like the other keys; `11` gives 5.
11. **The history keys are parsed apart from `parseConfig`** (writer's call), so a bad history
    value never drops the rest of the config, never blocks a save, and rawreel-cli never sees it.
12. **Entry text rules** (writer's call): relative bare paths made absolute without resolving
    `..`; a query kept; a path containing `?` stored as its `file:` URI; a `file:` URI as typed.
13. **The Format dialog route records the original text**; View > Format... and Restart GPU add
    nothing, except as in decision 3 (writer's call).
14. **No save when the list did not change** (our call), e.g. reopening the top entry.
15. **Placement** right after Open URI... on every OS (writer's call); classic Windows apps put the
    recent list above Exit.

## 15. Review notes

Optimistic review (O) and pessimistic review (P) of rev 1.

| Finding | Outcome |
|---|---|
| O1, P1 ids clash with the Log pane's menu | applied: +80..+89; a grep of src/ shows +70..+78 (log_panel.cpp), +60, +61 and +100 on in use |
| O2, P3 `saveSettings` always shows a dialog | applied: new `App::saveHistory`, logged once per run, own flag (§7) |
| O3 `openFiles` opens `fileUri(path)`, not the path | applied: every route passes its entry explicitly (§4) |
| O4 drop the "from Open Recent" flag | applied (lead's ruling; §6) |
| O5 how a non-string bad value is shown | applied: compact JSON via `dvv::json::dump`, as the other keys; the 40-character cut rejected, the other keys do not cut |
| O6 which spelling survives the Windows rule | moot: the Windows rule is dropped (decision 7) |
| O7, P15 U+202A on Windows labels | rejected: rawreel sets no RTL layout, and in a left-to-right menu the embedding changes nothing |
| O8 one `wxChoice` on every OS | applied: also fixes P4 (§9) |
| O9 drop the selftest's outside-edit step | applied: the core writer tests cover it |
| O10 keep "wrong size → 5" | kept (decision 10) |
| O questions (typed open of a missing listed file; Windows submenu label) | answered by the lead: removed too (decision 1); `Open Recent` everywhere (decision 5) |
| P2 arming can record the wrong entry | applied: matching by the requested URI, no arming (§5); Close stream clears the pending entry; `dismissMessage()` clears the entry kept for the Format... button. It does not clear the in-flight pending entry: `onStackChanged` and `onStreamsChanged` call `dismissMessage()` during an open, so that would lose every entry opened after a device loss or with a message shown |
| P4 instant apply and a permanent cut on a spin button | applied: `wxChoice`, one save per committed value; keeping 10 entries in memory rejected by requirement 13 |
| P5a the check blocks the GUI thread | applied: io executor, result by `CallAfter` (§6) |
| P5b a bare path's query is checked as part of the path | applied: the parsed path |
| P6 clash labels undefined for non-file entries | applied: full text on a clash, non-file entries always full text (§3) |
| P7 GTK's label form is `_N. name` | applied (§3) |
| P8 lexical `..` resolution can name another file | applied: joined to the working folder, `..` kept (`std::filesystem::absolute` resolves `..` on MSVC) |
| P9 no size cap | applied: 4096-byte entries, and the writer refuses a merged text over 1 MiB |
| P10 Unicode case, APFS, scope of the Windows rule | rejected: exact text (lead's ruling); two entries for one file are harmless |
| P11 TAB and invalid UTF-8 in labels | applied: such entries are not recorded and are dropped on load (§4, §8.3) |
| P12 GPU start failure never recorded | applied: falls out of the URI match (§5) |
| P13 selftest details | applied: `player` scenario, no write of `gui.showLogPane`, `openUri` callers pass an entry or none |
| P14 skip the save when nothing changed | applied (decision 14) |
| P16 `NormalizeFileName` also uses dots and tilde | applied in §3.1 |
| P questions (undoable cut; dialog on history save; bare path vs `file:` URI) | answered: requirement 13 (for good); never (decision 6); two entries (decision 7) |
| P "which waited already" (§13 of rev 1) | applied: removed; the open ran on the io executor, not the GUI thread |
