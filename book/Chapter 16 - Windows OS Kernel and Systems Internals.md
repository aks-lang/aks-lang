# Chapter 16: Windows OS Kernel & Low-Level Systems Integration

Low-level systems programming on Windows requires direct access to Win32 and NT kernel facilities. AKS provides bare-metal integration with the Windows operating system: clipboard management, Toolhelp32 process snapshots, PSAPI module enumeration, desktop window traversal, console raw mode, security token privileges, private heap walking, virtual memory management, named synchronization events, Event Tracing for Windows (ETW), PDH performance counters, NTFS Alternate Data Streams (ADS), and Windows Job Objects.

---

## 16.1 Windows Clipboard Operations

Interacts directly with the Windows clipboard subsystem using `OpenClipboard`, global memory allocation, and locking:

```aks
let sensitive_token = "SECURE_AUTH_TOKEN_2026_X99";

// Write string to Windows clipboard
let write_ok = OsClipboardSetC(sensitive_token);
print("Clipboard Write Success: " + String(write_ok));

// Read string from Windows clipboard
let retrieved_text = OsClipboardGetC();
print("Retrieved from Clipboard: " + retrieved_text);
```

---

## 16.2 Toolhelp32 & PSAPI Process Introspection

### 16.2.1 Process Snapshot Enumeration (`CreateToolhelp32Snapshot`)

Traverses active system processes, extracting Process IDs (PIDs), parent PIDs, thread counts, and executable names:

```aks
let processes = OsProcessSnapshotC();

print("Active System Processes (" + String(Length(processes)) + " detected):");
for (proc in processes) {
    if (proc.pid < 100) {
        print("PID: " + String(proc.pid) + "\tThreads: " + String(proc.threads) + "\tName: " + proc.exe_file);
    }
}
```

### 16.2.2 PSAPI Loaded DLL Module Enumeration

Enumerates loaded dynamic-link libraries for any target process:

```aks
let modules = OsModuleEnumC(SystemPid()); // Query current process modules

print("Loaded Modules in AKS Runtime:");
for (mod in modules) {
    print("Module: " + mod);
}
```

### 16.2.3 Desktop Window Traversal (`EnumWindows`)

Enumerates visible top-level desktop windows:

```aks
let window_list = OsWindowEnumC();
for (win in window_list) {
    if (Length(win.title) > 0) {
        print("Window: " + win.title + " (PID: " + String(win.pid) + ")");
    }
}
```

---

## 16.3 Console Raw Mode & Virtual Terminal Sequences

Enables ANSI virtual terminal processing and raw single-keypress capturing without waiting for newline buffer flushes:

```aks
// Enable raw keyboard mode (1 = raw, 0 = cooked)
let prev_mode = OsConsoleRawModeC(1);
print("Press any key to continue (Raw mode active)...");

// Restore standard line-buffered mode
OsConsoleRawModeC(prev_mode);
```

---

## 16.4 Process Token Privileges (`AdjustTokenPrivileges`)

Inspects process token privileges for system diagnostics and administrative tasks:

```aks
let privileges = OsTokenPrivilegesC();
print("Process Token Privileges Count: " + String(Length(privileges)));
for (priv in privileges) {
    print("Privilege: " + priv.name + " Enabled: " + String(priv.enabled));
}
```

---

## 16.5 Private Heap Walking & Virtual Memory Management

### 16.5.1 Heap Walking (`HeapWalk`)

Inspects internal chunks, allocation sizes, and overhead flags of the default process heap:

```aks
let heap_blocks = OsHeapWalkC();
print("Heap Block Count: " + String(Length(heap_blocks)));
if (Length(heap_blocks) > 0) {
    print("First Block Size: " + String(heap_blocks[0].size) + " bytes, Busy: " + String(heap_blocks[0].is_busy));
}
```

### 16.5.2 Virtual Memory Protection (`VirtualProtect` & `VirtualQuery`)

Modifies page-level memory access permissions:

```aks
// Query page memory attributes for virtual address 0
let page_info = OsVirtualQueryC(0);
print("Page State: " + String(page_info.state) + ", Region Size: " + String(page_info.region_size) + " bytes");

// Wow64 CPU Architecture Query
let w64 = OsWow64QueryC();
print("Native CPU Architecture: " + String(w64.native_cpu_arch));
```

---

## 16.6 Inter-Process Named Synchronization Events

Facilitates IPC synchronization between independent operating system processes using Windows Named Events:

```aks
let event_name = "Global\\AksDeploymentBarrierEvent";

// Create named event (1 = manual reset, 0 = auto reset)
let event_handle = OsNamedEventCreateC(event_name, 1);

// Process A signals the event:
OsNamedEventSetC(event_handle);

// Process B waits on the event (timeout in ms):
let signaled = OsNamedEventWaitC(event_handle, 100);
print("Event Signaled: " + String(signaled));

OsNamedEventCloseC(event_handle);
```

---

## 16.7 NTFS Alternate Data Streams (ADS)

NTFS allows associating multiple hidden data streams with a single file metadata entry:

```aks
let file_path = "document.txt";
let stream_name = "security_checksum";
let stream_data = "CRC32:98FA001B";

// Write hidden NTFS stream (document.txt:security_checksum)
NtfsAdsWriteC(file_path, stream_name, stream_data);

// Read stream data
let read_back = NtfsAdsReadC(file_path, stream_name);
print("Read from NTFS Alternate Stream: " + read_back);
```

---

## 16.8 Event Tracing for Windows (ETW) & PDH Counters

### 16.8.1 Emitting ETW Telemetry Events

Publishes high-speed structured telemetry directly to the Windows Event Tracing subsystem:

```aks
let provider_guid = "{B8679A1B-56D2-4754-8B1E-149B0B7F89CD}";
let event_name = "ServiceHealthCheckCompleted";

EtwEmitC(provider_guid, event_name);
print("Telemetry event dispatched to ETW channel.");
```

### 16.8.2 Windows PDH Performance Counters

Queries hardware and kernel performance metrics in real time:

```aks
let cpu_load = PdhQueryC("\\Processor(_Total)\\% Processor Time");
print("Current CPU Utilization: " + String(cpu_load) + "%");
```

---

## 16.9 Systems Programming Primitives & `unsafe` Blocks

For low-level kernel drivers, embedded development, and bare-metal performance, AKS provides first-class systems programming primitives:

### 16.9.1 The `unsafe` Block

Within an `unsafe` block, borrow checker rules and safety constraints are relaxed, allowing manual pointer operations, raw FFI, and hardware manipulation:

```aks
unsafe {
    // Low-level pointer arithmetic and raw hardware access
    let raw_addr = 0x00400000;
    // Unrestricted memory operations...
}
```

### 16.9.2 Memory Layout Introspection

AKS provides exact compile-time and runtime introspection into struct memory layout, byte sizes, and alignment requirements matching the target C ABI:

```aks
struct NetworkPacket {
    header: int;
    length: int;
    checksum: int;
}

// Compute byte sizes and alignment
let packet_size = SizeOf("NetworkPacket");
let packet_align = AlignOf("NetworkPacket");
let payload_offset = OffsetOf("NetworkPacket", "checksum");

print(f"Size: {packet_size} bytes, Align: {packet_align}, Checksum Offset: {payload_offset}");
```

### 16.9.3 Struct Memory Layout Control (`repr`)

The `repr()` attribute configures struct field packing and ABI alignment:

```aks
repr("packed")
struct PackedHeader {
    flags: int;
    seq: int;
}
```

### 16.9.4 Concurrency Safety Trait Bounds (`IsSend`, `IsSync`)

To guarantee data-race freedom across thread and fiber task boundaries, AKS enforces `Send` and `Sync` bounds:

- `IsSend(val)`: Verifies that ownership of `val` can be transferred safely across `spawn` boundaries without shared mutable references.
- `IsSync(val)`: Verifies that references to `val` can be concurrently accessed by multiple threads.

```aks
let data = [1, 2, 3];
assert(IsSend(data) == true);
assert(IsSync(data) == true);
```

---

## 16.10 Summary & Next Steps

In this chapter, we explored:
1. Windows clipboard reads and writes (`OsClipboardSetC`, `OsClipboardGetC`).
2. Toolhelp32 process snapshots (`OsProcessSnapshotC`) and PSAPI loaded DLL enumeration (`OsModuleEnumC`).
3. Desktop window traversal (`OsWindowEnumC`) and console raw mode (`OsConsoleRawModeC`).
4. Querying process token privileges (`OsTokenPrivilegesC`).
5. Private heap walking (`OsHeapWalkC`) and `VirtualQuery` page attributes.
6. Named cross-process synchronization events (`OsNamedEventCreateC`, `OsNamedEventSetC`, `OsNamedEventWaitC`).
7. NTFS Alternate Data Streams (ADS).
8. ETW event tracing and PDH performance counters.
9. Systems programming primitives: `unsafe { ... }` blocks, memory layout introspection (`SizeOf`, `AlignOf`, `OffsetOf`), `repr()` packing, and `IsSend`/`IsSync` trait bounds.

Proceed to [Chapter 17: Media, Graphics & Audio Subsystems](Chapter%2017%20-%20Media,%20Graphics%20and%20Audio%20Subsystems.md) to explore WAV audio parsing, GDI bitmap graphics, and WinMM low-latency PCM playback.
