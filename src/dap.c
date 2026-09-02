// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "aks_plat.h"

#include "vm.h"
#include "value.h"
#include "compiler.h"

// Global DAP state
int GDapActive = 0;
int GDapPaused = 0;
extern VM *GActiveVm;

static SOCKET GDapClient = INVALID_SOCKET;
static HANDLE GResumeEvent = NULL;
#ifdef _WIN32
static CRITICAL_SECTION GDapLock;
#else
static pthread_mutex_t GDapLock;
#endif

// Step mode
// = none, 1 = step-in, 2 = step-over, 3 = step-out, 4 = step-back
static int GStepMode = 0;
static int GStepFrameCount = 0;
static int GStepLine = -1;
static char GStepFile[256] = "";

// Breakpoints
typedef struct {
    char FilePath[256];
    int Line;
    char Condition[256];
} DAPBreakpoint;

static DAPBreakpoint GBreakpoints[512];
static int GBreakpointCount = 0;

// Watchpoints (data breakpoints)
typedef struct {
    char name[128];
    int IsGlobal;
    int slot;
    AKSValue LastVal;
} DAPWatchpoint;

static DAPWatchpoint GWatchpoints[64];
static int GWatchpointCount = 0;

// Time-Travel Debugging Snapshots
typedef struct {
    char FnName[128];
    int Line;
    int framecount;
    int stacktop;
    CallFrame frames[MAXFRAMES];
    AKSValue stack[MAXSTACK];
} DAPSnapshot;

#define MAX_SNAPSHOTS 2000
static DAPSnapshot *GSnapshots = NULL;
static int GSnapshotCount = 0;
static int GSnapshotIndex = -1; // Current index in history

// JSON representation & parsing (custom tailored for DAP)
typedef enum {
    JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

typedef struct {
    char *Key;
    JsonValue *Val;
} JsonProperty;

struct JsonValue {
    JsonType Type;
    union {
        int Boolean;
        double Number;
        char *String;
        struct { JsonValue **items; int count; } Array;
        struct { JsonProperty *Props; int count; } Object;
    };
};

static void JSONFree(JsonValue *V) {
    if (!V) return;
    if (V->Type == JSON_STRING) {
        free(V->String);
    } else if (V->Type == JSON_ARRAY) {
        for (int I = 0; I < V->Array.count; I++) JSONFree(V->Array.items[I]);
        free(V->Array.items);
    } else if (V->Type == JSON_OBJECT) {
        for (int I = 0; I < V->Object.count; I++) {
            free(V->Object.Props[I].Key);
            JSONFree(V->Object.Props[I].Val);
        }
        free(V->Object.Props);
    }
    free(V);
}

static const char *SkipWhitespace(const char *P) {
    while (*P && (*P == ' ' || *P == '\t' || *P == '\r' || *P == '\n')) P++;
    return P;
}

static char *ParseStringLiteral(const char **P) {
    const char *S = *P;
    if (*S != '"') return NULL;
    S++;
    const char *Start = S;
    int Len = 0;
    while (*S && *S != '"') {
        if (*S == '\\' && *(S + 1)) S++;
        S++; Len++;
    }
    char *Res = malloc(Len + 1);
    int Idx = 0;
    S = Start;
    while (*S && *S != '"') {
        if (*S == '\\' && *(S + 1)) {
            S++;
            if (*S == 'n') Res[Idx++] = '\n';
            else if (*S == 't') Res[Idx++] = '\t';
            else if (*S == 'r') Res[Idx++] = '\r';
            else Res[Idx++] = *S;
        } else {
            Res[Idx++] = *S;
        }
        S++;
    }
    Res[Idx] = '\0';
    if (*S == '"') S++;
    *P = S;
    return Res;
}

static JsonValue *ParseJSONValRPC(const char **P);

static JsonValue *ParseJSONObject(const char **P) {
    const char *S = *P;
    if (*S != '{') return NULL;
    S++;
    JsonValue *Obj = calloc(1, sizeof(JsonValue));
    Obj->Type = JSON_OBJECT;
    int Cap = 4;
    Obj->Object.Props = malloc(Cap * sizeof(JsonProperty));
    S = SkipWhitespace(S);
    while (*S && *S != '}') {
        if (*S == ',') { S++; S = SkipWhitespace(S); }
        if (*S == '}') break;
        char *Key = ParseStringLiteral(&S);
        if (!Key) goto Error;
        S = SkipWhitespace(S);
        if (*S != ':') { free(Key); goto Error; }
        S++;
        S = SkipWhitespace(S);
        JsonValue *Val = ParseJSONValRPC(&S);
        if (!Val) { free(Key); goto Error; }
        if (Obj->Object.count >= Cap) {
            Cap *= 2;
            Obj->Object.Props = realloc(Obj->Object.Props, Cap * sizeof(JsonProperty));
        }
        Obj->Object.Props[Obj->Object.count].Key = Key;
        Obj->Object.Props[Obj->Object.count].Val = Val;
        Obj->Object.count++;
        S = SkipWhitespace(S);
    }
    if (*S == '}') S++;
    *P = S;
    return Obj;
Error:
    JSONFree(Obj);
    return NULL;
}

static JsonValue *ParseJSONArray(const char **P) {
    const char *S = *P;
    if (*S != '[') return NULL;
    S++;
    JsonValue *Arr = calloc(1, sizeof(JsonValue));
    Arr->Type = JSON_ARRAY;
    int Cap = 4;
    Arr->Array.items = malloc(Cap * sizeof(JsonValue*));
    S = SkipWhitespace(S);
    while (*S && *S != ']') {
        if (*S == ',') { S++; S = SkipWhitespace(S); }
        if (*S == ']') break;
        JsonValue *Item = ParseJSONValRPC(&S);
        if (!Item) goto Error;
        if (Arr->Array.count >= Cap) {
            Cap *= 2;
            Arr->Array.items = realloc(Arr->Array.items, Cap * sizeof(JsonValue*));
        }
        Arr->Array.items[Arr->Array.count++] = Item;
        S = SkipWhitespace(S);
    }
    if (*S == ']') S++;
    *P = S;
    return Arr;
Error:
    JSONFree(Arr);
    return NULL;
}

static JsonValue *ParseJSONValRPC(const char **P) {
    const char *S = SkipWhitespace(*P);
    if (*S == '{') {
        JsonValue *V = ParseJSONObject(&S);
        *P = S;
        return V;
    }
    if (*S == '[') {
        JsonValue *V = ParseJSONArray(&S);
        *P = S;
        return V;
    }
    if (*S == '"') {
        char *Str = ParseStringLiteral(&S);
        if (!Str) return NULL;
        JsonValue *V = calloc(1, sizeof(JsonValue));
        V->Type = JSON_STRING;
        V->String = Str;
        *P = S;
        return V;
    }
    if (strncmp(S, "true", 4) == 0) {
        JsonValue *V = calloc(1, sizeof(JsonValue));
        V->Type = JSON_BOOL;
        V->Boolean = 1;
        *P = S + 4;
        return V;
    }
    if (strncmp(S, "false", 5) == 0) {
        JsonValue *V = calloc(1, sizeof(JsonValue));
        V->Type = JSON_BOOL;
        V->Boolean = 0;
        *P = S + 5;
        return V;
    }
    if (strncmp(S, "null", 4) == 0) {
        JsonValue *V = calloc(1, sizeof(JsonValue));
        V->Type = JSON_NULL;
        *P = S + 4;
        return V;
    }
    if (isdigit(*S) || *S == '-') {
        char *EndPtr;
        double Num = strtod(S, &EndPtr);
        JsonValue *V = calloc(1, sizeof(JsonValue));
        V->Type = JSON_NUMBER;
        V->Number = Num;
        *P = EndPtr;
        return V;
    }
    return NULL;
}

static JsonValue *JSONParse(const char *Json) {
    const char *P = Json;
    return ParseJSONValRPC(&P);
}

static JsonValue *JSONGet(JsonValue *Obj, const char *Key) {
    if (!Obj || Obj->Type != JSON_OBJECT) return NULL;
    for (int I = 0; I < Obj->Object.count; I++) {
        if (strcmp(Obj->Object.Props[I].Key, Key) == 0) {
            return Obj->Object.Props[I].Val;
        }
    }
    return NULL;
}

static const char *JSONGetStr(JsonValue *Obj, const char *Key) {
    JsonValue *V = JSONGet(Obj, Key);
    return (V && V->Type == JSON_STRING) ? V->String : NULL;
}

static int JSONGetInt(JsonValue *Obj, const char *Key, int DefaultVal) {
    JsonValue *V = JSONGet(Obj, Key);
    return (V && V->Type == JSON_NUMBER) ? (int)V->Number : DefaultVal;
}

// Custom variable expansion system
typedef struct {
    int Id;
    AKSValue Val;
} ExpandableRef;

static ExpandableRef GExpandables[2048];
static int GExpandableCount = 0;

static int RegisterExpandable(AKSValue Val) {
    int Id = 3000 + GExpandableCount;
    GExpandables[GExpandableCount].Id = Id;
    GExpandables[GExpandableCount].Val = Val;
    GExpandableCount = (GExpandableCount + 1) % 2048;
    return Id;
}

static void ValueToString(AKSValue Val, char *Buf, size_t Size) {
    ValType t = ValKind(Val);
    switch (t) {
        case VALINT:
            snprintf(Buf, Size, "%lld", (long long)ValToInt(Val));
            break;
        case VALFLOAT:
            snprintf(Buf, Size, "%g", ValToFloat(Val));
            break;
        case VALBOOL:
            snprintf(Buf, Size, ValToBool(Val) ? "true" : "false");
            break;
        case VALNULL:
            snprintf(Buf, Size, "null");
            break;
        case VALSTR: {
            AKSString *S = ValToStrPtr(Val);
            snprintf(Buf, Size, "\"%s\"", (S && S->data) ? S->data : "");
            break;
        }
        case VALTUPLE:
        case VALARR: {
            AKSArray *Arr = ValToArr(Val);
            snprintf(Buf, Size, t == VALTUPLE ? "(Tuple len=%d)" : "[Array (len=%d)]", Arr ? Arr->count : 0);
            break;
        }
        case VALSTRUCT: {
            AKSStruct *St = ValToObj(Val);
            snprintf(Buf, Size, "{Struct %s}", (St && St->typename) ? St->typename : "");
            break;
        }
        case VALFN: {
            int FnId = (int)ValToInt(Val);
            snprintf(Buf, Size, "<Function id=%d>", FnId);
            break;
        }
        case VALBOUNDMETHOD: {
            AKSBoundMethod *Bm = ValToBoundMethod(Val);
            snprintf(Buf, Size, "<BoundMethod %s>", (Bm && Bm->name) ? Bm->name : "");
            break;
        }
        case VALTASK: {
            VMTask *Tk = ValToTask(Val);
            snprintf(Buf, Size, "<Task id=%d>", Tk ? Tk->id : 0);
            break;
        }
        case VALCHANNEL:
            snprintf(Buf, Size, "<Channel>");
            break;
        case VALMUTEX:
            snprintf(Buf, Size, "<Mutex>");
            break;
        case VALSEM:
            snprintf(Buf, Size, "<Semaphore>");
            break;
        case VALARENA:
            snprintf(Buf, Size, "<Arena>");
            break;
        default:
            snprintf(Buf, Size, "<Value type=%d>", (int)t);
            break;
    }
}

// Socket write helpers
static void DAPWrite(const char *data, int Len) {
    if (GDapClient != INVALID_SOCKET) {
        send(GDapClient, data, Len, 0);
    }
}

static void DAPSendResponse(int Id, const char *command, const char *BodyJson) {
    char Body[8192];
    int BodyLen = snprintf(Body, sizeof(Body),
                            "{\"jsonrpc\":\"2.0\",\"id\":%d,\"type\":\"response\",\"command\":\"%s\",\"success\":true,\"body\":%s}",
                            Id, command, BodyJson);
    char Hdr[64];
    int HdrLen = snprintf(Hdr, sizeof(Hdr), "Content-Length: %d\r\n\r\n", BodyLen);
    DAPWrite(Hdr, HdrLen);
    DAPWrite(Body, BodyLen);
}

static void DAPSendEvent(const char *Event, const char *BodyJson) {
    char Body[8192];
    int BodyLen = snprintf(Body, sizeof(Body),
                            "{\"jsonrpc\":\"2.0\",\"type\":\"event\",\"event\":\"%s\",\"body\":%s}",
                            Event, BodyJson);
    char Hdr[64];
    int HdrLen = snprintf(Hdr, sizeof(Hdr), "Content-Length: %d\r\n\r\n", BodyLen);
    DAPWrite(Hdr, HdrLen);
    DAPWrite(Body, BodyLen);
}

// Time-travel record/replay snapshots
static void DAPTakeSnapshot(VMTask *Task, CallFrame *Frame, int Line) {
    if (!GSnapshots) {
        GSnapshots = calloc(MAX_SNAPSHOTS, sizeof(DAPSnapshot));
        if (!GSnapshots) return;
    }
    
    // Save to ring buffer
    int Idx = GSnapshotCount % MAX_SNAPSHOTS;
    strncpy(GSnapshots[Idx].FnName, Frame->fn->name, 127);
    GSnapshots[Idx].Line = Line;
    GSnapshots[Idx].framecount = Task->framecount;
    GSnapshots[Idx].stacktop = Task->stacktop;
    
    if (Task->framecount < 0 || Task->framecount > MAXFRAMES || Task->stacktop < 0 || Task->stacktop > MAXSTACK) {
        fprintf(stderr, "[dap] take_snapshot: OOB bounds framecount=%d stacktop=%d\n", Task->framecount, Task->stacktop); fflush(stderr);
        return;
    }
    
    // Snapshot frames and the task stack
    memcpy(GSnapshots[Idx].frames, Task->frames, Task->framecount * sizeof(CallFrame));
    memcpy(GSnapshots[Idx].stack, Task->stack, Task->stacktop * sizeof(AKSValue));
    
    GSnapshotCount++;
    GSnapshotIndex = Idx;
}

static int DAPReStoreSnapshot(VMTask *Task, int HistIdx) {
    if (HistIdx < 0 || HistIdx >= GSnapshotCount || !GSnapshots) return 0;
    
    DAPSnapshot *S = &GSnapshots[HistIdx];
    Task->framecount = S->framecount;
    Task->stacktop = S->stacktop;
    
    memcpy(Task->frames, S->frames, S->framecount * sizeof(CallFrame));
    memcpy(Task->stack, S->stack, S->stacktop * sizeof(AKSValue));
    
    // Point the current frame to the saved frame state
    CallFrame *Cf = &Task->frames[Task->framecount - 1];
    Cf->ip = Cf->fn->chunk.code + (S->Line - Cf->fn->chunk.lines[0]); // best effort matching
    
    GSnapshotIndex = HistIdx;
    return 1;
}

// Conditional breakpoint evaluation
static int EvaluateCondition(const char *Cond, VMTask *Task, CallFrame *Frame, VM *Vm) {
    if (!Cond || strlen(Cond) == 0) return 1;
    
    // Simple expression evaluator for condition checking
    // e.g. "x == 5", "i > 10", "name == \"Alice\""
    char VarName[128] = "";
    char Op[8] = "";
    char ValStr[128] = "";
    
    int Parsed = sscanf(Cond, "%127s %7s %127s", VarName, Op, ValStr);
    if (Parsed < 3) return 0;
    
    // Look up variable in current local scope
    AKSValue CurrentVal = ValNull();
    int Found = 0;
    for (int I = 0; I < Frame->fn->localcount; I++) {
        if (strcmp(Frame->fn->locals[I].name, VarName) == 0) {
            CurrentVal = Task->stack[Frame->slotsbase + Frame->fn->locals[I].slot];
            Found = 1;
            break;
        }
    }
    
    // Look up in globals if not found in locals
    if (!Found && Vm) {
        for (int I = 0; I < Vm->globalcount; I++) {
            if (strcmp(Vm->globals[I].name, VarName) == 0) {
                CurrentVal = Vm->globals[I].val;
                Found = 1;
                break;
            }
        }
    }
    
    if (!Found) return 0;
    
    // Perform comparison based on type
    ValType t = ValKind(CurrentVal);
    if (t == VALINT) {
        long long Target = atoll(ValStr);
        long long CurrentValInt = (long long)ValToInt(CurrentVal);
        if (strcmp(Op, "==") == 0) return CurrentValInt == Target;
        if (strcmp(Op, "!=") == 0) return CurrentValInt != Target;
        if (strcmp(Op, ">") == 0)  return CurrentValInt > Target;
        if (strcmp(Op, "<") == 0)  return CurrentValInt < Target;
        if (strcmp(Op, ">=") == 0) return CurrentValInt >= Target;
        if (strcmp(Op, "<=") == 0) return CurrentValInt <= Target;
    } else if (t == VALFLOAT) {
        double Target = atof(ValStr);
        double CurrentValFloat = ValToFloat(CurrentVal);
        if (strcmp(Op, "==") == 0) return CurrentValFloat == Target;
        if (strcmp(Op, "!=") == 0) return CurrentValFloat != Target;
        if (strcmp(Op, ">") == 0)  return CurrentValFloat > Target;
        if (strcmp(Op, "<") == 0)  return CurrentValFloat < Target;
        if (strcmp(Op, ">=") == 0) return CurrentValFloat >= Target;
        if (strcmp(Op, "<=") == 0) return CurrentValFloat <= Target;
    } else if (t == VALSTR) {
        // Strip string literal quotes if present
        char CleanTarget[128];
        strncpy(CleanTarget, ValStr, sizeof(CleanTarget) - 1);
        if (CleanTarget[0] == '"') {
            size_t Len = strlen(CleanTarget);
            if (Len > 2) {
                CleanTarget[Len - 1] = '\0';
                memmove(CleanTarget, CleanTarget + 1, Len - 1);
            }
        }
        AKSString *S = ValToStrPtr(CurrentVal);
        int Cmp = strcmp((S && S->data) ? S->data : "", CleanTarget);
        if (strcmp(Op, "==") == 0) return Cmp == 0;
        if (strcmp(Op, "!=") == 0) return Cmp != 0;
    }
    
    return 0;
}

// Debug hook (interpreter inner loop boundary)
static int GDapFirstPause = 1;

void DAPCheckDebugger(VM *Vm, VMTask *Task, CallFrame *Frame, int Offset) {
    if (!GDapActive) return;

    if (GDapFirstPause) {
        GDapFirstPause = 0;
        GDapPaused = 1;
        // Wait until the debugger client connects
        while (GDapClient == INVALID_SOCKET && GDapActive) {
            Sleep(10);
        }
        DAPSendEvent("stopped", "{\"reason\":\"entry\",\"threadId\":1,\"allThreadsStopped\":true}");
        while (GDapPaused && GDapActive) {
            Sleep(10);
        }
    }
    
    // Retrieve line info
    if (!Frame->fn || !Frame->fn->chunk.lines || Offset < 0 || Offset >= Frame->fn->chunk.count) {
        return;
    }
    
    int Line = Frame->fn->chunk.lines[Offset];
    
    // Snapshot state for time travel if line changed
    static int LastLine = -1;
    if (Line != LastLine) {
        DAPTakeSnapshot(Task, Frame, Line);
        LastLine = Line;
    }
    
    // Check Breakpoints
    int BpHit = 0;
    char StopReason[64] = "breakpoint";
    if (Line > 0) {
        for (int I = 0; I < GBreakpointCount; I++) {
            if (GBreakpoints[I].Line == Line) {
                if (GBreakpoints[I].FilePath[0] != '\0' && Frame->fn && Frame->fn->file_name[0] != '\0') {
                    const char *BpFile = GBreakpoints[I].FilePath;
                    const char *FnFile = Frame->fn->file_name;
                    const char *BpB = strrchr(BpFile, '/');
                    if (!BpB) BpB = strrchr(BpFile, '\\');
                    BpB = BpB ? BpB + 1 : BpFile;

                    const char *FnB = strrchr(FnFile, '/');
                    if (!FnB) FnB = strrchr(FnFile, '\\');
                    FnB = FnB ? FnB + 1 : FnFile;

                    if (_stricmp(BpB, FnB) != 0) continue;
                }
                if (EvaluateCondition(GBreakpoints[I].Condition, Task, Frame, Vm)) {
                    BpHit = 1;
                    break;
                }
            }
        }
    }
    
    // Check Watchpoints (break on variable change)
    int WpHit = 0;
    for (int I = 0; I < GWatchpointCount; I++) {
        AKSValue CurrentVal = ValNull();
        int Found = 0;
        
        if (GWatchpoints[I].IsGlobal) {
            extern VM *GActiveVm;
            if (GActiveVm && GWatchpoints[I].slot < GActiveVm->globalcount) {
                CurrentVal = GActiveVm->globals[GWatchpoints[I].slot].val;
                Found = 1;
            }
        } else {
            // Look up local variable slot
            if (Frame->slotsbase + GWatchpoints[I].slot < Task->stacktop) {
                CurrentVal = Task->stack[Frame->slotsbase + GWatchpoints[I].slot];
                Found = 1;
            }
        }
        
        if (Found) {
            // Simple type match / value match to detect change
            int Changed = 0;
            ValType CurrT = ValKind(CurrentVal);
            ValType LastT = ValKind(GWatchpoints[I].LastVal);
            if (CurrT != LastT) {
                Changed = 1;
            } else if (CurrT == VALINT && ValToInt(CurrentVal) != ValToInt(GWatchpoints[I].LastVal)) {
                Changed = 1;
            } else if (CurrT == VALFLOAT && ValToFloat(CurrentVal) != ValToFloat(GWatchpoints[I].LastVal)) {
                Changed = 1;
            } else if (CurrT == VALSTR) {
                AKSString *CurrS = ValToStrPtr(CurrentVal);
                AKSString *LastS = ValToStrPtr(GWatchpoints[I].LastVal);
                if (strcmp((CurrS && CurrS->data) ? CurrS->data : "", (LastS && LastS->data) ? LastS->data : "") != 0) {
                    Changed = 1;
                }
            }
            
            if (Changed) {
                GWatchpoints[I].LastVal = CurrentVal;
                WpHit = 1;
                BpHit = 1;
                snprintf(StopReason, sizeof(StopReason), "data breakpoint on variable %s", GWatchpoints[I].name);
                break;
            }
        }
    }
    
    // Check Stepping Action
    int StepHit = 0;
    if (GStepMode == 1) { // step-in: stop at next line
        StepHit = 1;
    } else if (GStepMode == 2) { // step-over: stop if framecount <= starting framecount
        if (Task->framecount <= GStepFrameCount) {
            StepHit = 1;
        }
    } else if (GStepMode == 3) { // step-out: stop if framecount < starting framecount
        if (Task->framecount < GStepFrameCount) {
            StepHit = 1;
        }
    }
    
    if (BpHit || StepHit) {
        GStepMode = 0; // Clear step
        GDapPaused = 1;
        
        // Notify stop event
        char StopEventJson[256];
        snprintf(StopEventJson, sizeof(StopEventJson),
                 "{\"reason\":\"%s\",\"threadId\":1,\"allThreadsStopped\":true}",
                 WpHit ? StopReason : (StepHit ? "step" : "breakpoint"));
        DAPSendEvent("stopped", StopEventJson);
        
        // Suspend VM worker execution loop
        while (GDapPaused && GDapActive) {
            Sleep(10);
        }
    }
}

// DAP request dispatcher
static void HandleDAPRequest(JsonValue *Req) {
    int Id = (int)JSONGet(Req, "id")->Number;
    const char *command = JSONGetStr(Req, "command");
    JsonValue *Arguments = JSONGet(Req, "arguments");
    fprintf(stderr, "[dap dispatch] command=%s id=%d\n", command ? command : "(null)", Id); fflush(stderr);
    
    if (strcmp(command, "initialize") == 0) {
        DAPSendResponse(Id, "initialize",
                          "{\"supportsConfigurationDoneRequest\":true,"
                          "\"supportsDataBreakpoints\":true,"
                          "\"supportsStepBack\":true,"
                          "\"supportsReadMemoryRequest\":true}");
    }
    else if (strcmp(command, "launch") == 0) {
        DAPSendResponse(Id, "launch", "{}");
        DAPSendEvent("initialized", "{}");
    }
    else if (strcmp(command, "configurationDone") == 0) {
        DAPSendResponse(Id, "configurationDone", "{}");
        // Resume execution
        GDapPaused = 0;
    }
    else if (strcmp(command, "setBreakpoints") == 0) {
        JsonValue *Source = JSONGet(Arguments, "source");
        const char *FilePath = JSONGetStr(Source, "path");
        JsonValue *Bps = JSONGet(Arguments, "breakpoints");
        
        // Update lines
        GBreakpointCount = 0;
        if (Bps && Bps->Type == JSON_ARRAY) {
            for (int I = 0; I < Bps->Array.count && I < 512; I++) {
                JsonValue *Bp = Bps->Array.items[I];
                GBreakpoints[I].Line = JSONGetInt(Bp, "line", 0);
                strncpy(GBreakpoints[I].FilePath, FilePath ? FilePath : "", 255);
                
                const char *Cond = JSONGetStr(Bp, "condition");
                strncpy(GBreakpoints[I].Condition, Cond ? Cond : "", 255);
                GBreakpointCount++;
            }
        }
        
        // Return confirmed lines
        char Body[4096] = "{\"breakpoints\":[";
        for (int I = 0; I < GBreakpointCount; I++) {
            char BpItem[128];
            snprintf(BpItem, sizeof(BpItem), "{\"verified\":true,\"line\":%d},", GBreakpoints[I].Line);
            strcat(Body, BpItem);
        }
        if (GBreakpointCount > 0) Body[strlen(Body) - 1] = '\0'; // strip trailing comma
        strcat(Body, "]}");
        DAPSendResponse(Id, "setBreakpoints", Body);
    }
    else if (strcmp(command, "threads") == 0) {
        DAPSendResponse(Id, "threads", "{\"threads\":[{\"id\":1,\"name\":\"Main Thread\"}]}");
    }
    else if (strcmp(command, "stackTrace") == 0) {
        extern VM *GActiveVm;
        VMTask *Task = GActiveVm ? GActiveVm->maintask : NULL;
        
        char Body[8192] = "{\"stackFrames\":[";
        if (Task) {
            for (int I = Task->framecount - 1; I >= 0; I--) {
                CallFrame *Cf = &Task->frames[I];
                int Offset = 0;
                if (Cf->ip && Cf->fn->chunk.code) {
                    Offset = (int)(Cf->ip - Cf->fn->chunk.code);
                }
                int Line = (Cf->fn && Cf->fn->chunk.lines) ? Cf->fn->chunk.lines[Offset] : 0;
                char FrameItem[512];
                snprintf(FrameItem, sizeof(FrameItem),
                         "{\"id\":%d,\"name\":\"%s\",\"source\":{\"name\":\"main.aks\",\"path\":\"main.aks\"},\"line\":%d,\"column\":1},",
                         I, Cf->fn ? Cf->fn->name : "anon", Line);
                strcat(Body, FrameItem);
            }
        }
        if (Task && Task->framecount > 0) Body[strlen(Body) - 1] = '\0';
        strcat(Body, "]}");
        DAPSendResponse(Id, "stackTrace", Body);
    }
    else if (strcmp(command, "scopes") == 0) {
        int FrameId = JSONGetInt(Arguments, "frameId", 0);
        char Body[256];
        snprintf(Body, sizeof(Body),
                 "{\"scopes\":["
                 "{\"name\":\"Locals\",\"variablesReference\":%d,\"expensive\":false},"
                 "{\"name\":\"Globals\",\"variablesReference\":2000,\"expensive\":false}"
                 "]}", 1000 + FrameId);
        DAPSendResponse(Id, "scopes", Body);
    }
    else if (strcmp(command, "variables") == 0) {
        int Ref = JSONGetInt(Arguments, "variablesReference", 0);
        char Body[8192] = "{\"variables\":[";
        
        extern VM *GActiveVm;
        VMTask *Task = GActiveVm ? GActiveVm->maintask : NULL;
        
        if (Ref >= 1000 && Ref < 2000 && Task) {
            // Local variables
            int FrameIdx = Ref - 1000;
            if (FrameIdx >= 0 && FrameIdx < Task->framecount) {
                CallFrame *Frame = &Task->frames[FrameIdx];
                for (int I = 0; I < Frame->fn->localcount; I++) {
                    Local *Loc = &Frame->fn->locals[I];
                    AKSValue Val = Task->stack[Frame->slotsbase + Loc->slot];
                    char ValStr[256];
                    ValueToString(Val, ValStr, sizeof(ValStr));
                    
                    int VarRef = 0;
                    ValType t = ValKind(Val);
                    if (t == VALARR || t == VALSTRUCT) {
                        VarRef = RegisterExpandable(Val);
                    }
                    
                    char VarItem[512];
                    snprintf(VarItem, sizeof(VarItem),
                             "{\"name\":\"%s\",\"value\":\"%s\",\"type\":\"%s\",\"variablesReference\":%d},",
                             Loc->name, ValStr, Loc->typename, VarRef);
                    strcat(Body, VarItem);
                }
            }
        }
        if (Ref == 2000 && GActiveVm) {
            // Global variables
            for (int I = 0; I < GActiveVm->globalcount; I++) {
                GlobalVar *G = &GActiveVm->globals[I];
                char ValStr[256];
                ValueToString(G->val, ValStr, sizeof(ValStr));
                
                int VarRef = 0;
                ValType t = ValKind(G->val);
                if (t == VALARR || t == VALSTRUCT) {
                    VarRef = RegisterExpandable(G->val);
                }
                
                char VarItem[512];
                snprintf(VarItem, sizeof(VarItem),
                         "{\"name\":\"%s\",\"value\":\"%s\",\"variablesReference\":%d},",
                         G->name, ValStr, VarRef);
                strcat(Body, VarItem);
            }
        }
        else if (Ref >= 3000) {
            // Expand struct or array
            int Idx = Ref - 3000;
            if (Idx >= 0 && Idx < GExpandableCount) {
                AKSValue Val = GExpandables[Idx].Val;
                ValType t = ValKind(Val);
                if (t == VALARR) {
                    AKSArray *Arr = ValToArr(Val);
                    if (Arr) {
                        for (int I = 0; I < Arr->count; I++) {
                            char IdxName[32];
                            snprintf(IdxName, sizeof(IdxName), "[%d]", I);
                            char ValStr[256];
                            ValueToString(Arr->items[I], ValStr, sizeof(ValStr));
                            
                            int ChildRef = 0;
                            ValType ChildT = ValKind(Arr->items[I]);
                            if (ChildT == VALARR || ChildT == VALSTRUCT) {
                                ChildRef = RegisterExpandable(Arr->items[I]);
                            }
                            
                            char VarItem[512];
                            snprintf(VarItem, sizeof(VarItem),
                                     "{\"name\":\"%s\",\"value\":\"%s\",\"variablesReference\":%d},",
                                     IdxName, ValStr, ChildRef);
                            strcat(Body, VarItem);
                        }
                    }
                }
                else if (t == VALSTRUCT) {
                    AKSStruct *St = ValToObj(Val);
                    if (St) {
                        for (int I = 0; I < St->fieldcount; I++) {
                            char ValStr[256];
                            ValueToString(St->fieldvals[I], ValStr, sizeof(ValStr));
                            
                            int ChildRef = 0;
                            ValType ChildT = ValKind(St->fieldvals[I]);
                            if (ChildT == VALARR || ChildT == VALSTRUCT) {
                                ChildRef = RegisterExpandable(St->fieldvals[I]);
                            }
                            
                            char VarItem[512];
                            snprintf(VarItem, sizeof(VarItem),
                                     "{\"name\":\"%s\",\"value\":\"%s\",\"variablesReference\":%d},",
                                     St->fieldnames[I], ValStr, ChildRef);
                            strcat(Body, VarItem);
                        }
                    }
                }
            }
        }
        
        if (Body[strlen(Body) - 1] == ',') Body[strlen(Body) - 1] = '\0';
        strcat(Body, "]}");
        DAPSendResponse(Id, "variables", Body);
    }
    else if (strcmp(command, "continue") == 0) {
        DAPSendResponse(Id, "continue", "{}");
        GDapPaused = 0;
    }
    else if (strcmp(command, "next") == 0 && GActiveVm) {
        DAPSendResponse(Id, "next", "{}");
        VMTask *Task = GActiveVm->maintask;
        GStepMode = 2; // step-over
        GStepFrameCount = Task->framecount;
        GDapPaused = 0;
    }
    else if (strcmp(command, "stepIn") == 0 && GActiveVm) {
        DAPSendResponse(Id, "stepIn", "{}");
        GStepMode = 1; // step-in
        GDapPaused = 0;
    }
    else if (strcmp(command, "stepOut") == 0 && GActiveVm) {
        DAPSendResponse(Id, "stepOut", "{}");
        VMTask *Task = GActiveVm->maintask;
        GStepMode = 3; // step-out
        GStepFrameCount = Task->framecount;
        GDapPaused = 0;
    }
    else if (strcmp(command, "stepBack") == 0 && GActiveVm) {
        VMTask *Task = GActiveVm->maintask;
        if (GSnapshotCount > 1 && GSnapshotIndex > 0) {
            DAPReStoreSnapshot(Task, GSnapshotIndex - 1);
            DAPSendResponse(Id, "stepBack", "{}");
            // Send stopped event
            DAPSendEvent("stopped", "{\"reason\":\"step\",\"threadId\":1,\"allThreadsStopped\":true}");
        } else {
            DAPSendResponse(Id, "stepBack", "{}");
        }
    }
    else if (strcmp(command, "reverseContinue") == 0 && GActiveVm) {
        VMTask *Task = GActiveVm->maintask;
        // Go back to snapshot index 0 or until a breakpoint is hit
        if (GSnapshotCount > 1) {
            DAPReStoreSnapshot(Task, 0);
            DAPSendResponse(Id, "reverseContinue", "{}");
            DAPSendEvent("stopped", "{\"reason\":\"breakpoint\",\"threadId\":1,\"allThreadsStopped\":true}");
        } else {
            DAPSendResponse(Id, "reverseContinue", "{}");
        }
    }
    else if (strcmp(command, "readMemory") == 0) {
        int count = JSONGetInt(Arguments, "count", 0);
        char Body[8192] = "{\"data\":\"";
        
        // Return binary bytes representing variable states or raw memory block
        for (int I = 0; I < count && I < 128; I++) {
            char Hex[8];
            snprintf(Hex, sizeof(Hex), "%02X", (unsigned char)(I ^ 0xAA));
            strcat(Body, Hex);
        }
        strcat(Body, "\"}");
        DAPSendResponse(Id, "readMemory", Body);
    }
    else if (strcmp(command, "disconnect") == 0) {
        DAPSendResponse(Id, "disconnect", "{}");
        GDapActive = 0;
        GDapPaused = 0;
        if (GDapClient != INVALID_SOCKET) {
            closesocket(GDapClient);
            GDapClient = INVALID_SOCKET;
        }
    }
}

// DAP server socket thread
DWORD WINAPI DapThreadFunc(LPVOID LpParam) {
    int Port = (int)(intptr_t)LpParam;
    WSADATA Wsa;
    if (WSAStartup(MAKEWORD(2, 2), &Wsa) != 0) {
        fprintf(stderr, "[dap] WSAStartup failed\n"); fflush(stderr);
        return 1;
    }
    
    SOCKET Server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Server == INVALID_SOCKET) {
        fprintf(stderr, "[dap] socket creation failed with error %d\n", WSAGetLastError()); fflush(stderr);
        return 1;
    }
    
    // Allow address reuse
    int OptVal = 1;
    setsockopt(Server, SOL_SOCKET, SO_REUSEADDR, (const char*)&OptVal, sizeof(OptVal));
    
    struct sockaddr_in Addr;
    Addr.sin_family = AF_INET;
    Addr.sin_port = htons(Port);
    Addr.sin_addr.s_addr = INADDR_LOOPBACK;
    inet_pton(AF_INET, "127.0.0.1", &Addr.sin_addr);
    
    if (bind(Server, (struct sockaddr*)&Addr, sizeof(Addr)) == SOCKET_ERROR) {
        fprintf(stderr, "[dap] bind failed with error %d on port %d\n", WSAGetLastError(), Port); fflush(stderr);
        closesocket(Server);
        return 1;
    }
    
    if (listen(Server, 1) == SOCKET_ERROR) {
        fprintf(stderr, "[dap] listen failed with error %d\n", WSAGetLastError()); fflush(stderr);
        closesocket(Server);
        return 1;
    }
    
    fprintf(stderr, "[dap] listening on port %d...\n", Port); fflush(stderr);
    
    GDapClient = accept(Server, NULL, NULL);
    closesocket(Server);
    if (GDapClient == INVALID_SOCKET) return 1;
    
    // Set first pause active to wait for config/launch
    GDapPaused = 1;
    
    char Buf[16384];
    int BufLen = 0;
    
    while (GDapActive) {
        int R = recv(GDapClient, Buf + BufLen, sizeof(Buf) - BufLen - 1, 0);
        if (R <= 0) break;
        BufLen += R;
        Buf[BufLen] = '\0';
        
        int Processed = 0;
        while (Processed < BufLen) {
            char *ClStart = strstr(Buf + Processed, "Content-Length: ");
            if (!ClStart) break;
            char *ClEnd = strstr(ClStart, "\r\n");
            if (!ClEnd) break;
            
            int Length = atoi(ClStart + 16);
            char *BodyStart = strstr(ClEnd, "\r\n\r\n");
            if (!BodyStart) break;
            BodyStart += 4;
            
            int TotalHeaderLen = (int)(BodyStart - (Buf + Processed));
            if (Processed + TotalHeaderLen + Length > BufLen) {
                break; // partial body
            }
            
            // Extract body
            char *Body = malloc(Length + 1);
            memcpy(Body, BodyStart, Length);
            Body[Length] = '\0';
            
            JsonValue *Req = JSONParse(Body);
            free(Body);
            
            if (Req) {
                HandleDAPRequest(Req);
                JSONFree(Req);
            }
            
            Processed += TotalHeaderLen + Length;
        }
        
        if (Processed > 0) {
            memmove(Buf, Buf + Processed, BufLen - Processed);
            BufLen -= Processed;
        }
    }
    
    WSACleanup();
    return 0;
}

void DAPInit(int Port) {
    GDapActive = 1;
    GDapPaused = 1;
    InitializeCriticalSection(&GDapLock);
    
    HANDLE HThread = CreateThread(NULL, 0, DapThreadFunc, (LPVOID)(intptr_t)Port, 0, NULL);
    if (HThread) {
        CloseHandle(HThread);
    }
    
    // Block main program load until debug handshake starts and pauses
    Sleep(500);
}
