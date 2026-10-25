# Metasiberia crash diagnostics

These scripts observe the canonical Windows Qt client only. They do not launch or stop it, connect to LSL, enable classification, or change the server.

## WER LocalDumps

`configure_metasiberia_wer_localdumps.ps1` is read-only unless invoked with `-Apply`. It checks the current per-application WER values, AeDebug state, and free space before any write. Applying the plan requires an elevated PowerShell session and writes only the `gui_client.exe` child key:

```text
HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\gui_client.exe
  DumpFolder  REG_EXPAND_SZ  %LOCALAPPDATA%\Metasiberia\CrashDumps
  DumpCount   REG_DWORD     3
  DumpType    REG_DWORD     2
```

No global LocalDumps values, other application's subkeys, or AeDebug values are changed. The script refuses conflicting per-application values, automatic AeDebug mode, or free space below the threshold. `-WhatIf` is supported. Microsoft documents that LocalDumps requires administrator configuration, supports per-application keys, and does not collect a dump when automatic application debugging is enabled.

The 12 GiB free-space floor is a guard, not a guarantee: previous normal starts reached about 2.5 GiB of private committed memory. It budgets approximately four dumps at that observed size plus margin (three retained files and a new dump being written). The actual full dump may be larger. The currently observed C: free space was about 85.75 GiB. Full dumps contain process memory and must stay in the user's local profile unless separately approved for sharing.

## Five-cycle monitor

After WER configuration is applied and verified, start the monitor before manually launching the client. Use `-Cycles 1` for the next single launch, or change it to `5` to monitor five launches:

```powershell
& 'C:\Program Files\PowerShell\7\pwsh.exe' -NoProfile -ExecutionPolicy Bypass -File 'C:\programming\substrata_neurocontrol_integration\scripts\diagnostics\monitor_metasiberia_lifecycle.ps1' -Cycles 1
```

The monitor refuses to start unless the app-specific WER values match, the dump folder exists, automatic AeDebug is not enabled, and the configured free-space floor is met. It watches only the exact `gui_client.exe` path. It records PID and process creation time, per-run resource samples, per-run log provenance, Windows Application Error/WER/Application Hang records, and any new dump file metadata. It never launches or terminates Metasiberia.

Each session is stored under:

```text
%LOCALAPPDATA%\Metasiberia\LifecycleDiagnostics\<UTC-session-id>\
```

Each `cycle-XX-pid-YYYYY` folder receives its own `client-log.txt` only after the shared application `log.txt` changes after that process's creation time and a fresh `Lifecycle: window construction started` marker can be associated with the run. Fresh appended text before that marker is kept as a fresh delta. Unchanged stale content or an unassociated rewrite is not copied into the run journal; `cycle.json` records `log_capture_status` and the baseline hash. A startup crash before the client creates a fresh log therefore has an explicitly empty/not-produced client log while the process metadata and WER report/dump remain available.

After each process exit, the monitor keeps that cycle open for up to 45 seconds while polling for a new WER dump. If a dump appears, it waits until its path, size, and write time remain stable for two seconds before recording it. This lets WER's post-crash collection finish after the process disappears; the monitor continues polling for later user-started cycles during the wait.

The session-level `wer-dumps.json` also inventories new or changed dump files independently of process detection. If a startup crash is so early that polling never observes the process, wait until the dump appears in `CrashDumps`, then press Ctrl+C in the monitor console. Its `finally` block saves `wer-dumps.json` and `windows-application-events.txt`; the Windows event records contain the process ID and exception details. Ctrl+C stops only the monitor.

`cycle.json` includes any new `.dmp`/`.mdmp` files found in the dump folder after process exit. Dumps are inventoried by path, size, and last-write time; the monitor does not read, copy, hash, or upload dump contents.

If the window disappears but `gui_client.exe` stays alive, do not end it. Use Task Manager → **Details** → right-click the matching `gui_client.exe` PID → **Create dump file**; save the exact path shown before taking any further action. If a WER/JIT dialog appears, leave it open until the configured dump file is present.

## Offline dump analysis

WinDbg/CDB was not found in the standard Windows SDK debugger locations during preparation; `vsjitdebugger.exe` is registered but is not an offline dump analyzer. No debugging package will be installed without approval. If WinDbg becomes available, open the full dump and run the prepared command file:

```text
scripts\diagnostics\metasiberia_dump_analysis.wds
```

It captures `!analyze -v`, exception record/context, faulting instruction, faulting and all-thread stacks, module paths/ranges, memory-region summaries, private executable regions, and heap summary into a WinDbg timestamped log in `CrashDumps`. Then compare the failing instruction/API entrypoint only against the exact matching executable or DLL image. `!chkimg -d <module>` is an optional comparison after the exact image and symbols are available; mismatches alone do not establish who patched the code or why.

Prior investigation reported anonymous/private executable memory and possible API-entry modifications, but did not tie them to the failing instruction, identify an owner, or produce a resolved call stack. Treat them as leads to correlate with the next dump, not as a confirmed cause. Do not assign blame to Qt, CEF, LSL, an overlay, or another component without that correlation.
