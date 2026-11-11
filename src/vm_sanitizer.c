// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "vm_stdlib.h"

volatile int gAksAsanActive = 0;

#define SAN_QUARANTINE_CAP 8192
#define SAN_VC_SLOTS 64
#define SAN_MAX_TASKS 512
#define SAN_MAX_SYNCS 512
#define SAN_MEM_BUCKETS 4096
#define SAN_MAX_LOCKS 8

typedef struct {
  void *ptr;
} SanQuarantineEntry;

static uintptr_t g_asan_q[SAN_QUARANTINE_CAP];
static int g_asan_q_head = 0;

static inline uint32_t SanHashPtr(uintptr_t p) {
  uint64_t x = (uint64_t)p;
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  return (uint32_t)x;
}

void AksSanQuarantinePtr(void *ptr) {
  if (!gAksAsanActive || !ptr)
    return;
  uintptr_t u = (uintptr_t)ptr;
  /* Guarded benignly via atomic index swap / overwrite in ring */
  int idx = g_asan_q_head;
  g_asan_q_head = (idx + 1) & (SAN_QUARANTINE_CAP - 1);
  g_asan_q[idx] = u;
}

void AksSanUnquarantinePtr(void *ptr) {
  if (!gAksAsanActive || !ptr)
    return;
  uintptr_t u = (uintptr_t)ptr;
  for (int i = 0; i < SAN_QUARANTINE_CAP; i++) {
    if (g_asan_q[i] == u) {
      g_asan_q[i] = 0;
    }
  }
}

int AksSanIsQuarantined(const void *ptr) {
  if (!gAksAsanActive || !ptr)
    return 0;
  uintptr_t u = (uintptr_t)ptr;
  for (int i = 0; i < SAN_QUARANTINE_CAP; i++) {
    if (g_asan_q[i] == u)
      return 1;
  }
  return 0;
}

typedef struct {
  VMTask *task;
  uint32_t vc[SAN_VC_SLOTS];
  void *held_locks[SAN_MAX_LOCKS];
  int held_count;
  int in_use;
} SanTaskInfo;

typedef struct {
  void *sync_ptr;
  uint32_t vc[SAN_VC_SLOTS];
  int in_use;
} SanSyncInfo;

typedef struct {
  uintptr_t addr;
  uint16_t writer_slot;
  uint32_t writer_epoch;
  void *writer_locks[SAN_MAX_LOCKS];
  int writer_nlocks;
  uint8_t writer_valid;
  uint16_t reader_slot;
  uint32_t reader_epoch;
  void *reader_locks[SAN_MAX_LOCKS];
  int reader_nlocks;
  uint8_t reader_valid;
  uint8_t reported;
} SanMemSlot;

typedef struct {
  SanTaskInfo tasks[SAN_MAX_TASKS];
  int next_task_slot;
  SanSyncInfo syncs[SAN_MAX_SYNCS];
  SanMemSlot mem[SAN_MEM_BUCKETS];
  int asan_errors;
  int tsan_errors;
} VmSanitizerState;

static int SanGetTaskSlotLocked(VmSanitizerState *s, VMTask *task) {
  if (!task)
    return 0;
  for (int i = 0; i < SAN_MAX_TASKS; i++) {
    if (s->tasks[i].in_use && s->tasks[i].task == task)
      return i;
  }
  int slot = s->next_task_slot % SAN_MAX_TASKS;
  s->next_task_slot++;
  memset(&s->tasks[slot], 0, sizeof(SanTaskInfo));
  s->tasks[slot].task = task;
  s->tasks[slot].in_use = 1;
  s->tasks[slot].vc[slot % SAN_VC_SLOTS] = 1;
  return slot;
}

static SanSyncInfo *SanGetSyncLocked(VmSanitizerState *s, void *ptr) {
  if (!ptr)
    return NULL;
  for (int i = 0; i < SAN_MAX_SYNCS; i++) {
    if (s->syncs[i].in_use && s->syncs[i].sync_ptr == ptr)
      return &s->syncs[i];
  }
  for (int i = 0; i < SAN_MAX_SYNCS; i++) {
    if (!s->syncs[i].in_use) {
      memset(&s->syncs[i], 0, sizeof(SanSyncInfo));
      s->syncs[i].in_use = 1;
      s->syncs[i].sync_ptr = ptr;
      return &s->syncs[i];
    }
  }
  return NULL;
}

static void SanMergeVc(uint32_t *dst, const uint32_t *src) {
  for (int i = 0; i < SAN_VC_SLOTS; i++) {
    if (src[i] > dst[i])
      dst[i] = src[i];
  }
}

static int SanHasCommonLock(void *const *a, int na, void *const *b, int nb) {
  for (int i = 0; i < na; i++) {
    for (int j = 0; j < nb; j++) {
      if (a[i] && a[i] == b[j])
        return 1;
    }
  }
  return 0;
}

void AksSanInitVm(VM *vm) {
  if (!vm)
    return;
  if (vm->sanitize_address) {
    gAksAsanActive = 1;
    vm->no_bounds_check = 0;
  }
  if (!vm->sanitize_address && !vm->sanitize_thread)
    return;
  vm->san_lock = MutexNewNative();
  vm->san_state = calloc(1, sizeof(VmSanitizerState));
  vm->san_error_count = 0;
}

void AksSanFreeVm(VM *vm) {
  if (!vm)
    return;
  if (vm->san_state) {
    free(vm->san_state);
    vm->san_state = NULL;
  }
  if (vm->san_lock) {
    MutexFreeNative(vm->san_lock);
    vm->san_lock = NULL;
  }
  if (vm->sanitize_address) {
    memset(g_asan_q, 0, sizeof(g_asan_q));
    g_asan_q_head = 0;
  }
}

void AksSanReportAsanError(VM *vm, const char *kind, const char *detail,
                           int line) {
  if (!vm)
    return;
  fprintf(stderr, "[AddressSanitizer] ERROR: %s at line %d: %s\n",
          kind ? kind : "memory-error", line, detail ? detail : "");
  vm->san_error_count++;
  VmSanitizerState *s = (VmSanitizerState *)vm->san_state;
  if (s) {
    if (vm->san_lock) MutexLockNative(vm->san_lock);
    s->asan_errors++;
    if (vm->san_lock) MutexUnlockNative(vm->san_lock);
  }
}

int AksSanCheckObjAsan(VM *vm, AKSValue v, const char *opname, int line) {
  if (!vm || !vm->sanitize_address)
    return 1;
  ValType k = ValKind(v);
  void *ptr = NULL;
  if (k == VALARR || k == VALTUPLE)
    ptr = ValToArr(v);
  else if (k == VALSTRUCT)
    ptr = ValToObj(v);
  else if (k == VALSTR)
    ptr = ValToStrPtr(v);
  if (ptr && AksSanIsQuarantined(ptr)) {
    char msg[256];
    snprintf(msg, sizeof(msg), "%s attempted on freed/quarantined heap object %p",
             opname ? opname : "access", ptr);
    AksSanReportAsanError(vm, "heap-use-after-free", msg, line);
    return 0;
  }
  if (ValIsInvalidated(v)) {
    char msg[256];
    snprintf(msg, sizeof(msg), "%s attempted on invalidated/freed object %p",
             opname ? opname : "access", ptr);
    AksSanReportAsanError(vm, "heap-use-after-free", msg, line);
    return 0;
  }
  return 1;
}

void AksSanOnSpawn(VM *vm, VMTask *parent, VMTask *child) {
  if (!vm || !vm->sanitize_thread || !vm->san_state || !vm->san_lock)
    return;
  VmSanitizerState *s = (VmSanitizerState *)vm->san_state;
  MutexLockNative(vm->san_lock);
  int ps = SanGetTaskSlotLocked(s, parent);
  int cs = SanGetTaskSlotLocked(s, child);
  SanMergeVc(s->tasks[cs].vc, s->tasks[ps].vc);
  s->tasks[ps].vc[ps % SAN_VC_SLOTS]++;
  s->tasks[cs].vc[cs % SAN_VC_SLOTS]++;
  MutexUnlockNative(vm->san_lock);
}

void AksSanOnAwait(VM *vm, VMTask *waiter, VMTask *target) {
  if (!vm || !vm->sanitize_thread || !vm->san_state || !vm->san_lock)
    return;
  VmSanitizerState *s = (VmSanitizerState *)vm->san_state;
  MutexLockNative(vm->san_lock);
  int ws = SanGetTaskSlotLocked(s, waiter);
  int ts = SanGetTaskSlotLocked(s, target);
  SanMergeVc(s->tasks[ws].vc, s->tasks[ts].vc);
  s->tasks[ws].vc[ws % SAN_VC_SLOTS]++;
  MutexUnlockNative(vm->san_lock);
}

void AksSanOnMutexLock(VM *vm, VMTask *task, void *mutex_ptr) {
  if (!vm || !vm->sanitize_thread || !vm->san_state || !vm->san_lock)
    return;
  VmSanitizerState *s = (VmSanitizerState *)vm->san_state;
  MutexLockNative(vm->san_lock);
  int ts = SanGetTaskSlotLocked(s, task);
  SanTaskInfo *ti = &s->tasks[ts];
  SanSyncInfo *sy = SanGetSyncLocked(s, mutex_ptr);
  if (sy) {
    SanMergeVc(ti->vc, sy->vc);
  }
  int already = 0;
  for (int i = 0; i < ti->held_count; i++) {
    if (ti->held_locks[i] == mutex_ptr) {
      already = 1;
      break;
    }
  }
  if (!already && ti->held_count < SAN_MAX_LOCKS) {
    ti->held_locks[ti->held_count++] = mutex_ptr;
  }
  MutexUnlockNative(vm->san_lock);
}

void AksSanOnMutexUnlock(VM *vm, VMTask *task, void *mutex_ptr) {
  if (!vm || !vm->sanitize_thread || !vm->san_state || !vm->san_lock)
    return;
  VmSanitizerState *s = (VmSanitizerState *)vm->san_state;
  MutexLockNative(vm->san_lock);
  int ts = SanGetTaskSlotLocked(s, task);
  SanTaskInfo *ti = &s->tasks[ts];
  ti->vc[ts % SAN_VC_SLOTS]++;
  SanSyncInfo *sy = SanGetSyncLocked(s, mutex_ptr);
  if (sy) {
    SanMergeVc(sy->vc, ti->vc);
  }
  for (int i = 0; i < ti->held_count; i++) {
    if (ti->held_locks[i] == mutex_ptr) {
      for (int j = i; j < ti->held_count - 1; j++) {
        ti->held_locks[j] = ti->held_locks[j + 1];
      }
      ti->held_count--;
      break;
    }
  }
  MutexUnlockNative(vm->san_lock);
}

void AksSanOnChannelSend(VM *vm, VMTask *sender, void *chan_ptr) {
  if (!vm || !vm->sanitize_thread || !vm->san_state || !vm->san_lock)
    return;
  VmSanitizerState *s = (VmSanitizerState *)vm->san_state;
  MutexLockNative(vm->san_lock);
  int ts = SanGetTaskSlotLocked(s, sender);
  SanTaskInfo *ti = &s->tasks[ts];
  ti->vc[ts % SAN_VC_SLOTS]++;
  SanSyncInfo *sy = SanGetSyncLocked(s, chan_ptr);
  if (sy) {
    SanMergeVc(sy->vc, ti->vc);
  }
  MutexUnlockNative(vm->san_lock);
}

void AksSanOnChannelRecv(VM *vm, VMTask *receiver, void *chan_ptr) {
  if (!vm || !vm->sanitize_thread || !vm->san_state || !vm->san_lock)
    return;
  VmSanitizerState *s = (VmSanitizerState *)vm->san_state;
  MutexLockNative(vm->san_lock);
  int ts = SanGetTaskSlotLocked(s, receiver);
  SanTaskInfo *ti = &s->tasks[ts];
  SanSyncInfo *sy = SanGetSyncLocked(s, chan_ptr);
  if (sy) {
    SanMergeVc(ti->vc, sy->vc);
  }
  ti->vc[ts % SAN_VC_SLOTS]++;
  MutexUnlockNative(vm->san_lock);
}

void AksSanRecordAccess(VM *vm, VMTask *task, uintptr_t obj_addr,
                        const char *kind, int is_write, int line) {
  if (!vm || !vm->sanitize_thread || !vm->san_state || !vm->san_lock ||
      !obj_addr)
    return;
  VmSanitizerState *s = (VmSanitizerState *)vm->san_state;
  MutexLockNative(vm->san_lock);
  int ts = SanGetTaskSlotLocked(s, task);
  SanTaskInfo *ti = &s->tasks[ts];
  uint32_t cur_epoch = ti->vc[ts % SAN_VC_SLOTS];

  uint32_t h = SanHashPtr(obj_addr) & (SAN_MEM_BUCKETS - 1);
  SanMemSlot *slot = NULL;
  for (int probe = 0; probe < 16; probe++) {
    uint32_t idx = (h + (uint32_t)probe) & (SAN_MEM_BUCKETS - 1);
    if (s->mem[idx].addr == obj_addr || s->mem[idx].addr == 0) {
      slot = &s->mem[idx];
      break;
    }
  }
  if (!slot) {
    slot = &s->mem[h];
    memset(slot, 0, sizeof(SanMemSlot));
  }
  if (slot->addr != obj_addr) {
    memset(slot, 0, sizeof(SanMemSlot));
    slot->addr = obj_addr;
  }

  int conflict = 0;
  uint16_t other_tid = 0;
  const char *other_op = "write";
  if (slot->writer_valid && slot->writer_slot != (uint16_t)ts) {
    if (slot->writer_epoch > ti->vc[slot->writer_slot % SAN_VC_SLOTS] &&
        !SanHasCommonLock(ti->held_locks, ti->held_count, slot->writer_locks,
                          slot->writer_nlocks)) {
      conflict = 1;
      other_tid = slot->writer_slot;
      other_op = "write";
    }
  }
  if (!conflict && is_write && slot->reader_valid &&
      slot->reader_slot != (uint16_t)ts) {
    if (slot->reader_epoch > ti->vc[slot->reader_slot % SAN_VC_SLOTS] &&
        !SanHasCommonLock(ti->held_locks, ti->held_count, slot->reader_locks,
                          slot->reader_nlocks)) {
      conflict = 1;
      other_tid = slot->reader_slot;
      other_op = "read";
    }
  }

  if (conflict && !slot->reported) {
    slot->reported = 1;
    s->tsan_errors++;
    vm->san_error_count++;
    fprintf(stderr,
            "[ThreadSanitizer] WARNING: data race on %s (0x%llx) at line %d: "
            "%s by task#%d conflicts with %s by task#%u\n",
            kind ? kind : "object", (unsigned long long)obj_addr, line,
            is_write ? "write" : "read", ts, other_op, (unsigned)other_tid);
  }

  if (is_write) {
    slot->writer_valid = 1;
    slot->writer_slot = (uint16_t)ts;
    slot->writer_epoch = cur_epoch;
    slot->writer_nlocks = ti->held_count;
    for (int i = 0; i < ti->held_count && i < SAN_MAX_LOCKS; i++)
      slot->writer_locks[i] = ti->held_locks[i];
  } else {
    slot->reader_valid = 1;
    slot->reader_slot = (uint16_t)ts;
    slot->reader_epoch = cur_epoch;
    slot->reader_nlocks = ti->held_count;
    for (int i = 0; i < ti->held_count && i < SAN_MAX_LOCKS; i++)
      slot->reader_locks[i] = ti->held_locks[i];
  }
  MutexUnlockNative(vm->san_lock);
}

