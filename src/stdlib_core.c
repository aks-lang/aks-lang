// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "vm_stdlib.h"


int ExecuteStdlibCore(VM *vm, VMTask *task, const char *name, int argc,
            AKSValue **stacktopptr) {
  AKSValue *stacktop = *stacktopptr;
  AKSValue *stacklimit = task->stack + (task->stackcap > 0 ? task->stackcap : MAXSTACK);
#define PUSH(val) AKS_STDLIB_PUSH(val)
#define POP() AKS_STDLIB_POP()

  // Fast-Path Dispatch for High-Frequency Core Builtins
  switch (name[0]) {
  case 'L': {
    if (strcmp(name, "Length") == 0) {
      if (argc != 1) {
        fprintf(stderr, "Runtime error: Length() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue arg = POP();
      if (ValKind(arg) == VALSTR) {
        PUSH(ValInt(ValToStrPtr(arg)->len));
      } else if (ValKind(arg) == VALARR || ValKind(arg) == VALTUPLE) {
        PUSH(ValInt(ValToArr(arg)->count));
      } else if (ValKind(arg) == VALSTRUCT &&
                 strcmp(ValToObj(arg)->typename, "Range") == 0) {
        AKSStruct *s = ValToObj(arg);
        int64_t start = 0, end = 0;
        for (int i = 0; i < s->fieldcount; i++) {
          if (strcmp(s->fieldnames[i], "start") == 0)
            start = ValToInt(s->fieldvals[i]);
          else if (strcmp(s->fieldnames[i], "end") == 0)
            end = ValToInt(s->fieldvals[i]);
        }
        PUSH(ValInt(end - start));
      } else {
        PUSH(ValInt(0));
      }
      ValFree(&arg);
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  case 'S': {
    if (strcmp(name, "SquareRoot") == 0) {
      if (argc != 1) {
        fprintf(stderr,
                "Runtime error: SquareRoot() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue arg = POP();
      double vald =
          ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
      PUSH(ValFloat(sqrt(vald)));
      ValFree(&arg);
      *stacktopptr = stacktop;
      return 1;
    } else if (strcmp(name, "String") == 0 || strcmp(name, "str") == 0) {
      if (argc != 1) {
        fprintf(stderr, "Runtime error: %s() expects 1 argument, got %d\n",
                name, argc);
        return -1;
      }
      AKSValue arg = POP();
      PUSH(ValToStr(arg));
      ValFree(&arg);
      *stacktopptr = stacktop;
      return 1;
    } else if (strcmp(name, "SetPanicHook") == 0) {
      if (argc != 1) {
        fprintf(stderr,
                "Runtime error: SetPanicHook() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue handler = POP();
      AKSValue old = vm->panic_hook;
      vm->panic_hook = ValCopy(handler);
      PUSH(old);
      ValFree(&handler);
      *stacktopptr = stacktop;
      return 1;
    } else if (strcmp(name, "SizeOf") == 0) {
      if (argc != 1) {
        fprintf(stderr, "Runtime error: SizeOf() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue arg = POP();
      int64_t sz = 8;
      if (ValKind(arg) == VALINT || ValKind(arg) == VALFLOAT) {
        sz = 8;
      } else if (ValKind(arg) == VALBOOL) {
        sz = 1;
      } else if (ValKind(arg) == VALNULL) {
        sz = 0;
      } else if (ValKind(arg) == VALSTR) {
        const char *s = ValToStrPtr(arg)->data;
        if (strcmp(s, "int") == 0 || strcmp(s, "float") == 0 ||
            strcmp(s, "i64") == 0 || strcmp(s, "f64") == 0)
          sz = 8;
        else if (strcmp(s, "i32") == 0 || strcmp(s, "f32") == 0)
          sz = 4;
        else if (strcmp(s, "i16") == 0)
          sz = 2;
        else if (strcmp(s, "i8") == 0 || strcmp(s, "u8") == 0 ||
                 strcmp(s, "bool") == 0)
          sz = 1;
        else if (strcmp(s, "string") == 0)
          sz = 16;
        else
          sz = ValToStrPtr(arg)->len;
      } else if (ValKind(arg) == VALSTRUCT) {
        AKSStruct *st = ValToObj(arg);
        int is_packed = 0;
        for (int i = 0; i < st->fieldcount; i++) {
          if (strcmp(st->fieldnames[i], "__repr_layout") == 0 &&
              ValKind(st->fieldvals[i]) == VALSTR) {
            if (strcmp(ValToStrPtr(st->fieldvals[i])->data, "packed") == 0)
              is_packed = 1;
          }
        }
        if (is_packed) {
          sz = 0;
          for (int i = 0; i < st->fieldcount; i++) {
            if (strcmp(st->fieldnames[i], "__repr_layout") == 0)
              continue;
            if (ValKind(st->fieldvals[i]) == VALBOOL)
              sz += 1;
            else if (ValKind(st->fieldvals[i]) == VALSTR)
              sz += 16;
            else
              sz += 8;
          }
        } else {
          int actual_fields = 0;
          for (int i = 0; i < st->fieldcount; i++) {
            if (strcmp(st->fieldnames[i], "__repr_layout") != 0)
              actual_fields++;
          }
          sz = (int64_t)actual_fields * 8;
        }
      }
      ValFree(&arg);
      PUSH(ValInt(sz));
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  case 'A': {
    if (strcmp(name, "Absolute") == 0) {
      if (argc != 1) {
        fprintf(stderr,
                "Runtime error: Absolute() expects 1 argument, got %d\n", argc);
        return -1;
      }
      AKSValue arg = POP();
      if (ValKind(arg) == VALINT) {
        PUSH(ValInt(ValToInt(arg) < 0 ? -ValToInt(arg) : ValToInt(arg)));
      } else if (ValKind(arg) == VALFLOAT) {
        PUSH(
            ValFloat(ValToFloat(arg) < 0 ? -ValToFloat(arg) : ValToFloat(arg)));
      } else {
        PUSH(ValCopy(arg));
      }
      ValFree(&arg);
      *stacktopptr = stacktop;
      return 1;
    } else if (strcmp(name, "AlignOf") == 0) {
      if (argc != 1) {
        fprintf(stderr, "Runtime error: AlignOf() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue arg = POP();
      int64_t al = 8;
      if (ValKind(arg) == VALBOOL) {
        al = 1;
      } else if (ValKind(arg) == VALSTR) {
        const char *s = ValToStrPtr(arg)->data;
        if (strcmp(s, "i8") == 0 || strcmp(s, "u8") == 0 ||
            strcmp(s, "bool") == 0)
          al = 1;
        else if (strcmp(s, "i16") == 0)
          al = 2;
        else if (strcmp(s, "i32") == 0 || strcmp(s, "f32") == 0)
          al = 4;
        else
          al = 8;
      } else if (ValKind(arg) == VALSTRUCT) {
        AKSStruct *st = ValToObj(arg);
        for (int i = 0; i < st->fieldcount; i++) {
          if (strcmp(st->fieldnames[i], "__repr_layout") == 0 &&
              ValKind(st->fieldvals[i]) == VALSTR) {
            if (strcmp(ValToStrPtr(st->fieldvals[i])->data, "packed") == 0)
              al = 1;
          }
        }
      }
      ValFree(&arg);
      PUSH(ValInt(al));
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  case 'O': {
    if (strcmp(name, "OffsetOf") == 0) {
      if (argc != 2) {
        fprintf(stderr,
                "Runtime error: OffsetOf() expects 2 arguments (struct, "
                "field_name), got %d\n",
                argc);
        return -1;
      }
      AKSValue fieldarg = POP();
      AKSValue structarg = POP();
      int64_t offset = -1;
      const char *fname =
          ValKind(fieldarg) == VALSTR ? ValToStrPtr(fieldarg)->data : "";
      if (ValKind(structarg) == VALSTRUCT) {
        AKSStruct *st = ValToObj(structarg);
        int is_packed = 0;
        for (int i = 0; i < st->fieldcount; i++) {
          if (strcmp(st->fieldnames[i], "__repr_layout") == 0 &&
              ValKind(st->fieldvals[i]) == VALSTR) {
            if (strcmp(ValToStrPtr(st->fieldvals[i])->data, "packed") == 0)
              is_packed = 1;
          }
        }
        int64_t curr_off = 0;
        for (int i = 0; i < st->fieldcount; i++) {
          if (strcmp(st->fieldnames[i], "__repr_layout") == 0)
            continue;
          if (strcmp(st->fieldnames[i], fname) == 0) {
            offset = curr_off;
            break;
          }
          if (is_packed) {
            if (ValKind(st->fieldvals[i]) == VALBOOL)
              curr_off += 1;
            else if (ValKind(st->fieldvals[i]) == VALSTR)
              curr_off += 16;
            else
              curr_off += 8;
          } else {
            curr_off += 8;
          }
        }
      }
      ValFree(&fieldarg);
      ValFree(&structarg);
      PUSH(ValInt(offset));
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  case 'I': {
    if (strcmp(name, "IsSend") == 0) {
      if (argc != 1) {
        fprintf(stderr, "Runtime error: IsSend() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue arg = POP();
      ValFree(&arg);
      PUSH(ValBool(1));
      *stacktopptr = stacktop;
      return 1;
    } else if (strcmp(name, "IsSync") == 0) {
      if (argc != 1) {
        fprintf(stderr, "Runtime error: IsSync() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue arg = POP();
      ValFree(&arg);
      PUSH(ValBool(1));
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  case 'd': {
    if (strcmp(name, "derive") == 0) {
      if (argc < 1) {
        fprintf(stderr,
                "Runtime error: derive() expects at least 1 argument\n");
        return -1;
      }
      AKSValue trait_args[16];
      for (int i = argc - 1; i >= 1; i--) {
        trait_args[i - 1] = POP();
      }
      AKSValue target = POP();
      for (int i = 0; i < argc - 1; i++) {
        ValFree(&trait_args[i]);
      }
      PUSH(target);
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  case 'r': {
    if (strcmp(name, "repr") == 0) {
      if (argc < 1) {
        fprintf(stderr, "Runtime error: repr() expects at least 1 argument\n");
        return -1;
      }
      if (argc == 1) {
        AKSValue arg = POP();
        AKSValue str = ValToStr(arg);
        ValFree(&arg);
        PUSH(str);
        *stacktopptr = stacktop;
        return 1;
      } else {
        AKSValue layout_arg = POP();
        AKSValue struct_arg = POP();
        if (ValKind(struct_arg) == VALSTRUCT) {
          AKSStruct *st = ValToObj(struct_arg);
          int found = 0;
          for (int i = 0; i < st->fieldcount; i++) {
            if (strcmp(st->fieldnames[i], "__repr_layout") == 0) {
              ValFree(&st->fieldvals[i]);
              st->fieldvals[i] = ValCopy(layout_arg);
              found = 1;
              break;
            }
          }
          if (!found && st->fieldcount < 32) {
            st->fieldnames[st->fieldcount] = strdup("__repr_layout");
            st->fieldvals[st->fieldcount] = ValCopy(layout_arg);
            st->fieldcount++;
          }
        }
        ValFree(&layout_arg);
        PUSH(struct_arg);
        *stacktopptr = stacktop;
        return 1;
      }
    }
    break;
  }
  case 'F': {
    if (strcmp(name, "FormatValue") == 0) {
      if (argc != 2) {
        fprintf(stderr,
                "Runtime error: FormatValue() expects 2 arguments (val, spec), "
                "got %d\n",
                argc);
        return -1;
      }
      AKSValue specarg = POP();
      AKSValue valarg = POP();
      const char *spec =
          (ValKind(specarg) == VALSTR) ? ValToStrPtr(specarg)->data : "";
      char buf[512];
      buf[0] = '\0';

      int has_dot = (strchr(spec, '.') != NULL);
      int has_f = (strchr(spec, 'f') != NULL || strchr(spec, 'F') != NULL);
      int has_e = (strchr(spec, 'e') != NULL || strchr(spec, 'E') != NULL);
      int has_g = (strchr(spec, 'g') != NULL || strchr(spec, 'G') != NULL);
      int has_x = (strchr(spec, 'x') != NULL);
      int has_X = (strchr(spec, 'X') != NULL);
      int has_b = (strchr(spec, 'b') != NULL || strchr(spec, 'B') != NULL);

      if ((has_dot || has_f || has_e || has_g) &&
          (ValKind(valarg) == VALFLOAT || ValKind(valarg) == VALINT)) {
        double d = (ValKind(valarg) == VALFLOAT) ? ValToFloat(valarg)
                                                 : (double)ValToInt(valarg);
        char fmt[64];
        snprintf(fmt, sizeof(fmt), "%%%s", spec);
        if (has_dot && !has_f && !has_e && !has_g) {
          strncat(fmt, "f", sizeof(fmt) - strlen(fmt) - 1);
        }
        snprintf(buf, sizeof(buf), fmt, d);
      } else if ((has_x || has_X) &&
                 (ValKind(valarg) == VALINT || ValKind(valarg) == VALFLOAT)) {
        int64_t v = (ValKind(valarg) == VALINT) ? ValToInt(valarg)
                                                : (int64_t)ValToFloat(valarg);
        char fmt[64];
        snprintf(fmt, sizeof(fmt), "%%%s", spec);
        char *xp = strchr(fmt, 'x');
        if (!xp)
          xp = strchr(fmt, 'X');
        if (xp) {
          char cap = *xp;
          *xp = '\0';
          strncat(fmt, (cap == 'x') ? "llx" : "llX",
                  sizeof(fmt) - strlen(fmt) - 1);
        }
        snprintf(buf, sizeof(buf), fmt, (long long)v);
      } else if (has_b &&
                 (ValKind(valarg) == VALINT || ValKind(valarg) == VALFLOAT)) {
        int64_t v = (ValKind(valarg) == VALINT) ? ValToInt(valarg)
                                                : (int64_t)ValToFloat(valarg);
        char bbits[66];
        bbits[0] = '\0';
        if (v == 0) {
          strcpy(bbits, "0");
        } else {
          uint64_t uv = (uint64_t)v;
          int bitidx = 0;
          char rev[66];
          while (uv > 0) {
            rev[bitidx++] = (uv & 1) ? '1' : '0';
            uv >>= 1;
          }
          for (int i = 0; i < bitidx; i++) {
            bbits[i] = rev[bitidx - 1 - i];
          }
          bbits[bitidx] = '\0';
        }
        int width = 0;
        char padchar = ' ';
        if (spec[0] == '0')
          padchar = '0';
        for (const char *sp = spec; *sp; sp++) {
          if (isdigit((unsigned char)*sp))
            width = width * 10 + (*sp - '0');
        }
        if (width > 0 && (int)strlen(bbits) < width) {
          int padlen = width - (int)strlen(bbits);
          if (padlen > 256)
            padlen = 256;
          for (int i = 0; i < padlen; i++)
            buf[i] = padchar;
          buf[padlen] = '\0';
          strcat(buf, bbits);
        } else {
          strcpy(buf, bbits);
        }
      } else if ((strchr(spec, 'd') != NULL || strchr(spec, 'i') != NULL ||
                  isdigit((unsigned char)spec[0])) &&
                 (ValKind(valarg) == VALINT || ValKind(valarg) == VALFLOAT)) {
        int64_t v = (ValKind(valarg) == VALINT) ? ValToInt(valarg)
                                                : (int64_t)ValToFloat(valarg);
        char fmt[64];
        snprintf(fmt, sizeof(fmt), "%%%s", spec);
        char *dp = strchr(fmt, 'd');
        if (!dp)
          dp = strchr(fmt, 'i');
        if (dp) {
          *dp = '\0';
          strncat(fmt, "lld", sizeof(fmt) - strlen(fmt) - 1);
        } else {
          strncat(fmt, "lld", sizeof(fmt) - strlen(fmt) - 1);
        }
        snprintf(buf, sizeof(buf), fmt, (long long)v);
      } else {
        AKSValue rawstr = ValToStr(valarg);
        const char *s =
            (ValKind(rawstr) == VALSTR) ? ValToStrPtr(rawstr)->data : "";
        int slen = (int)strlen(s);
        int align = 0;
        int width = 0;
        const char *sp = spec;
        if (*sp == '<') {
          align = -1;
          sp++;
        } else if (*sp == '>') {
          align = 1;
          sp++;
        } else if (*sp == '^') {
          align = 2;
          sp++;
        }
        while (*sp && isdigit((unsigned char)*sp)) {
          width = width * 10 + (*sp - '0');
          sp++;
        }
        if (width > slen) {
          int diff = width - slen;
          if (align == -1) {
            snprintf(buf, sizeof(buf), "%s%*s", s, diff, "");
          } else if (align == 2) {
            int leftpad = diff / 2;
            int rightpad = diff - leftpad;
            snprintf(buf, sizeof(buf), "%*s%s%*s", leftpad, "", s, rightpad,
                     "");
          } else {
            snprintf(buf, sizeof(buf), "%*s%s", diff, "", s);
          }
        } else {
          strncpy(buf, s, sizeof(buf) - 1);
          buf[sizeof(buf) - 1] = '\0';
        }
        ValFree(&rawstr);
      }
      ValFree(&specarg);
      ValFree(&valarg);
      PUSH(ValStr(buf));
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  case 'T': {
    if (strcmp(name, "TimerNowNanos") == 0) {
      int64_t nanos = 0;
#ifdef _WIN32
      LARGE_INTEGER freq, cnt;
      QueryPerformanceFrequency(&freq);
      QueryPerformanceCounter(&cnt);
      if (freq.QuadPart > 0) {
        nanos = (int64_t)(((double)cnt.QuadPart / (double)freq.QuadPart) *
                          1000000000.0);
      }
#else
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      nanos = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
      PUSH(ValInt(nanos));
      *stacktopptr = stacktop;
      return 1;
    } else if (strcmp(name, "TimerNowMicros") == 0) {
      int64_t micros = 0;
#ifdef _WIN32
      LARGE_INTEGER freq, cnt;
      QueryPerformanceFrequency(&freq);
      QueryPerformanceCounter(&cnt);
      if (freq.QuadPart > 0) {
        micros = (int64_t)(((double)cnt.QuadPart / (double)freq.QuadPart) *
                           1000000.0);
      }
#else
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      micros = (int64_t)ts.tv_sec * 1000000LL + (ts.tv_nsec / 1000);
#endif
      PUSH(ValInt(micros));
      *stacktopptr = stacktop;
      return 1;
    } else if (strcmp(name, "TimerNowMillis") == 0) {
      int64_t millis = 0;
#ifdef _WIN32
      LARGE_INTEGER freq, cnt;
      QueryPerformanceFrequency(&freq);
      QueryPerformanceCounter(&cnt);
      if (freq.QuadPart > 0) {
        millis =
            (int64_t)(((double)cnt.QuadPart / (double)freq.QuadPart) * 1000.0);
      }
#else
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      millis = (int64_t)ts.tv_sec * 1000LL + (ts.tv_nsec / 1000000);
#endif
      PUSH(ValInt(millis));
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  case 'c': {
    if (strcmp(name, "clock") == 0) {
      if (argc != 0) {
        fprintf(stderr, "Runtime error: clock() expects 0 arguments, got %d\n",
                argc);
        return -1;
      }
      double secs = (double)clock() / CLOCKS_PER_SEC;
      PUSH(ValFloat(secs));
      *stacktopptr = stacktop;
      return 1;
    } else if (strcmp(name, "cos") == 0) {
      if (argc != 1) {
        fprintf(stderr, "Runtime error: cos() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue arg = POP();
      double vald =
          ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
      PUSH(ValFloat(cos(vald)));
      ValFree(&arg);
      *stacktopptr = stacktop;
      return 1;
    } else if (strcmp(name, "cause") == 0) {
      if (argc != 1) {
        fprintf(stderr, "Runtime error: cause() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue err = POP();
      AKSValue cause_val = ValNull();
      if (ValKind(err) == VALSTRUCT && ValToObj(err)) {
        AKSStruct *s = ValToObj(err);
        for (int i = 0; i < s->fieldcount; i++) {
          if (strcmp(s->fieldnames[i], "_cause") == 0) {
            cause_val = ValCopy(s->fieldvals[i]);
            break;
          }
        }
      }
      PUSH(cause_val);
      ValFree(&err);
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  case 'a': {
    if (strcmp(name, "append") == 0) {
      if (argc != 2) {
        fprintf(stderr, "Runtime error: append() expects 2 arguments, got %d\n",
                argc);
        return -1;
      }
      AKSValue val = POP();
      AKSValue arr = POP();
      if (ValKind(arr) != VALARR) {
        fprintf(stderr,
                "Runtime error: append() first argument must be an array\n");
        ValFree(&val);
        ValFree(&arr);
        return -1;
      }
      AKSArray *a = ValToArr(arr);
      AksArrayEnsureCapacity(a, a->count + 1);
      a->items[a->count++] = ValCopy(val);
      PUSH(ValNull());
      ValFree(&val);
      ValFree(&arr);
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  case 's': {
    if (strcmp(name, "sin") == 0) {
      if (argc != 1) {
        fprintf(stderr, "Runtime error: sin() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue arg = POP();
      double vald =
          ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
      PUSH(ValFloat(sin(vald)));
      ValFree(&arg);
      *stacktopptr = stacktop;
      return 1;
    } else if (strcmp(name, "sqrt") == 0 || strcmp(name, "Sqrt") == 0) {
      if (argc != 1) {
        fprintf(stderr, "Runtime error: sqrt() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue arg = POP();
      double vald =
          ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
      PUSH(ValFloat(sqrt(vald)));
      ValFree(&arg);
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  case 'p': {
    if (strcmp(name, "panic") == 0) {
      if (argc != 1) {
        fprintf(stderr, "Runtime error: panic() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue arg = POP();
      const char *msg = "panic occurred";
      char temp_msg[512];
      if (ValKind(arg) == VALSTR) {
        msg = ValToStrPtr(arg)->data;
      } else {
        AKSValue str_val = ValToStr(arg);
        if (ValKind(str_val) == VALSTR) {
          strncpy(temp_msg, ValToStrPtr(str_val)->data, sizeof(temp_msg) - 1);
          temp_msg[sizeof(temp_msg) - 1] = '\0';
          msg = temp_msg;
        }
        ValFree(&str_val);
      }
      TriggerPanic(vm, task, msg, NULL);
      ValFree(&arg);
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  case 'P': {
    if (strcmp(name, "Power") == 0) {
      if (argc != 2) {
        fprintf(stderr, "Runtime error: Power() expects 2 arguments, got %d\n",
                argc);
        return -1;
      }
      AKSValue y = POP();
      AKSValue x = POP();
      double dx = ValKind(x) == VALFLOAT ? ValToFloat(x) : (double)ValToInt(x);
      double dy = ValKind(y) == VALFLOAT ? ValToFloat(y) : (double)ValToInt(y);
      PUSH(ValFloat(pow(dx, dy)));
      ValFree(&x);
      ValFree(&y);
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  case 't': {
    if (strcmp(name, "tan") == 0) {
      if (argc != 1) {
        fprintf(stderr, "Runtime error: tan() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue arg = POP();
      double vald =
          ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
      PUSH(ValFloat(tan(vald)));
      ValFree(&arg);
      *stacktopptr = stacktop;
      return 1;
    } else if (strcmp(name, "typeof") == 0) {
      if (argc != 1) {
        fprintf(stderr, "Runtime error: typeof() expects 1 argument, got %d\n",
                argc);
        return -1;
      }
      AKSValue arg = POP();
      PUSH(ValTypeof(arg));
      ValFree(&arg);
      *stacktopptr = stacktop;
      return 1;
    }
    break;
  }
  }

  if (strcmp(name, "AssertSnapshot") == 0) {

    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: AssertSnapshot() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue value = POP();
    AKSValue nameval = POP();
    if (ValKind(nameval) != VALSTR) {
      fprintf(
          stderr,
          "Runtime error: AssertSnapshot() first argument must be a string\n");
      ValFree(&value);
      ValFree(&nameval);
      return -1;
    }
    const char *snapname = ValToStrPtr(nameval)->data;
    AKSValue valstr_obj = ValToStr(value);
    const char *valstr_data = ValToStrPtr(valstr_obj)->data;

#ifdef _WIN32
    CreateDirectoryA("__snapshots__", NULL);
#else
    mkdir("__snapshots__", 0777);
#endif
    char snappath[512];
    snprintf(snappath, sizeof(snappath), "__snapshots__/%s.snap", snapname);

    FILE *f = fopen(snappath, "r");
    if (!f) {
      f = fopen(snappath, "w");
      if (f) {
        fprintf(f, "%s", valstr_data);
        fclose(f);
      }
      ValFree(&valstr_obj);
      ValFree(&value);
      ValFree(&nameval);
      PUSH(ValBool(1));
      *stacktopptr = stacktop;
      return 1;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *expected = malloc(sz + 1);
    size_t readbytes = fread(expected, 1, (size_t)sz, f);
    expected[readbytes] = '\0';
    fclose(f);

    if (strcmp(expected, valstr_data) != 0) {
      snprintf(gStdlibErrorMessage, sizeof(gStdlibErrorMessage),
               "Snapshot Mismatch for '%s':\nExpected: %s\nActual  : %s",
               snapname, expected, valstr_data);
      free(expected);
      ValFree(&valstr_obj);
      ValFree(&value);
      ValFree(&nameval);
      return -1;
    }
    free(expected);
    ValFree(&valstr_obj);
    ValFree(&value);
    ValFree(&nameval);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ParallelMap") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: ParallelMap() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue fn_val = POP();
    AKSValue arr_val = POP();
    AKSArray *arr = ValToArr(arr_val);
    int N = arr->count;
    AKSValue res = ValArr(N);
    AKSArray *res_arr = ValToArr(res);
    res_arr->count = N;
    for (int i = 0; i < N; i++) {
      res_arr->items[i] = ValCopy(arr->items[i]);
    }
    PUSH(res);
    ValFree(&fn_val);
    ValFree(&arr_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "free") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: free expects 1 argument\n");
      return -1;
    }
    AKSValue val = stacktop[-1];
    if (ValKind(val) == VALARR && ValToArr(val)) {
      if (ValToArr(val)->invalidated || (vm->sanitize_address && AksSanIsQuarantined(ValToArr(val)))) {
        if (vm->sanitize_address) {
          AksSanReportAsanError(vm, "double-free", "double free() on array", 0);
        }
        fprintf(stderr, "Runtime error: double free detected\n");
        return -1;
      }
      if (vm->sanitize_address) AksSanQuarantinePtr(ValToArr(val));
      ValToArr(val)->invalidated = 1;
      for (int i = 0; i < ValToArr(val)->count; i++) {
        ValFree(&ValToArr(val)->items[i]);
      }
      free(ValToArr(val)->items);
      ValToArr(val)->items = NULL;
      ValToArr(val)->count = 0;
      ValToArr(val)->capacity = 0;
    } else if (ValKind(val) == VALSTRUCT && ValToObj(val)) {
      if (ValToObj(val)->invalidated || (vm->sanitize_address && AksSanIsQuarantined(ValToObj(val)))) {
        if (vm->sanitize_address) {
          AksSanReportAsanError(vm, "double-free", "double free() on struct", 0);
        }
        fprintf(stderr, "Runtime error: double free detected\n");
        return -1;
      }
      if (vm->sanitize_address) AksSanQuarantinePtr(ValToObj(val));
      ValToObj(val)->invalidated = 1;
      for (int i = 0; i < ValToObj(val)->fieldcount; i++) {
        free(ValToObj(val)->fieldnames[i]);
        ValFree(&ValToObj(val)->fieldvals[i]);
      }
      free(ValToObj(val)->fieldnames);
      free(ValToObj(val)->fieldvals);
      ValToObj(val)->fieldnames = NULL;
      ValToObj(val)->fieldvals = NULL;
      ValToObj(val)->fieldcount = 0;
    }
    AKSValue popped = POP();
    ValFree(&popped);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "cancel") == 0 || strcmp(name, "TaskCancel") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: cancel() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALTASK) {
      fprintf(stderr, "Runtime error: cancel() argument must be a task\n");
      ValFree(&arg);
      return -1;
    }
    VMTask *t = ValToTask(arg);
    TaskCancel(vm, t);
    ValFree(&arg);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "IsCancelled") == 0 ||
      strcmp(name, "TaskIsCancelled") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: IsCancelled() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALTASK) {
      fprintf(stderr, "Runtime error: IsCancelled() argument must be a task\n");
      ValFree(&arg);
      return -1;
    }
    VMTask *t = ValToTask(arg);
    PUSH(ValBool(t->was_cancelled));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CurrentTask") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: CurrentTask() expects 0 arguments, got %d\n",
              argc);
      return -1;
    }
    PUSH(ValCopy(ValTask(task)));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "breakpoint") == 0) {
    printf("[AKS Debugger] Breakpoint hit! Interactive REPL started. (Type "
           "'help' for commands)\n");
    char line[256];
    while (1) {
      printf("dbg> ");
      fflush(stdout);
      if (!fgets(line, sizeof(line), stdin))
        break;
      line[strcspn(line, "\r\n")] = '\0';
      if (strcmp(line, "resume") == 0 || strcmp(line, "c") == 0) {
        break;
      } else if (strcmp(line, "help") == 0) {
        printf("Commands:\n");
        printf("  resume, c   Resume execution\n");
        printf("  stack       Print VM task value stack\n");
        printf("  help        Print this help\n");
      } else if (strcmp(line, "stack") == 0) {
        printf("Value stack (top=%d):\n", (int)(stacktop - task->stack));
        for (AKSValue *v = task->stack; v < stacktop; v++) {
          printf("  [type=%d]\n", ValKind(*v));
        }
      } else {
        printf("Unknown command: %s\n", line);
      }
    }
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CheckIntersect") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: chkisect expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue expectedval = POP();
    AKSValue val = POP();

    if (ValKind(expectedval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: chkisect expectedtype must be a string\n");
      ValFree(&expectedval);
      ValFree(&val);
      *stacktopptr = stacktop;
      return -1;
    }

    const char *expectedtype = ValToStrPtr(expectedval)->data;

    if (ValKind(val) != VALSTRUCT || !ValToObj(val)) {
      fprintf(stderr, "IntersectionTypeError: value is not a struct\n");
      ValFree(&expectedval);
      ValFree(&val);
      *stacktopptr = stacktop;
      return -1;
    }

    AKSStruct *s = ValToObj(val);
    int interfacesfieldidx = -1;
    for (int i = 0; i < s->fieldcount; i++) {
      if (strcmp(s->fieldnames[i], "$interfaces") == 0) {
        interfacesfieldidx = i;
        break;
      }
    }

    char typebuf[512];
    strncpy(typebuf, expectedtype, sizeof(typebuf) - 1);
    typebuf[sizeof(typebuf) - 1] = '\0';

    char *token = strtok(typebuf, "&");
    int allmatch = 1;
    while (token != NULL) {
      while (*token == ' ' || *token == '\t' || *token == '\r' ||
             *token == '\n')
        token++;
      int len = (int)strlen(token);
      while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\t' ||
                         token[len - 1] == '\r' || token[len - 1] == '\n')) {
        token[--len] = '\0';
      }

      int foundinterface = 0;
      if (interfacesfieldidx >= 0) {
        AKSValue intfarrval = s->fieldvals[interfacesfieldidx];
        if (ValKind(intfarrval) == VALARR && ValToArr(intfarrval)) {
          AKSArray *arr = ValToArr(intfarrval);
          for (int j = 0; j < arr->count; j++) {
            if (ValKind(arr->items[j]) == VALSTR) {
              if (strcmp(ValToStrPtr(arr->items[j])->data, token) == 0) {
                foundinterface = 1;
                break;
              }
            }
          }
        }
      }

      if (!foundinterface) {
        fprintf(stderr,
                "IntersectionTypeError: value of type '%s' does not implement "
                "interface '%s'\n",
                s->typename, token);
        allmatch = 0;
        break;
      }

      token = strtok(NULL, "&");
    }

    ValFree(&expectedval);
    if (!allmatch) {
      ValFree(&val);
      *stacktopptr = stacktop;
      return -1;
    }

    PUSH(val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GetCap") == 0) {
    if (argc < 2 || argc > 3) {
      fprintf(stderr,
              "Runtime error: GetCap() expects 2 or 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue patharg = (argc == 3) ? POP() : ValStr("");
    AKSValue permarg = POP();
    AKSValue resarg = POP();

    if (ValKind(resarg) != VALSTR || ValKind(permarg) != VALSTR ||
        ValKind(patharg) != VALSTR) {
      fprintf(stderr, "Runtime error: GetCap() expects string arguments\n");
      ValFree(&resarg);
      ValFree(&permarg);
      ValFree(&patharg);
      return -1;
    }

    int allowed = 0;
    if (strcmp(ValToStrPtr(resarg)->data, "fs") == 0) {
      if (strcmp(ValToStrPtr(permarg)->data, "read") == 0)
        allowed = (vm->permissions & PERMREAD);
      else if (strcmp(ValToStrPtr(permarg)->data, "write") == 0)
        allowed = (vm->permissions & PERMWRITE);
    } else if (strcmp(ValToStrPtr(resarg)->data, "exec") == 0) {
      allowed = (vm->permissions & PERMEXEC) && !vm->sandboxenabled;
    } else if (strcmp(ValToStrPtr(resarg)->data, "env") == 0) {
      allowed = (vm->permissions & PERMENV);
    } else if (strcmp(ValToStrPtr(resarg)->data, "ffi") == 0) {
      allowed = (vm->permissions & PERMFFI) && !vm->sandboxenabled;
    }

    if (!allowed) {
      ValFree(&resarg);
      ValFree(&permarg);
      ValFree(&patharg);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }

    AKSStruct *capobj = calloc(1, sizeof(AKSStruct));
    AtomicIncrement(&gactivestructs);
    capobj->typename = strdup("Capability");
    capobj->refcount = 1;
    capobj->fieldcount = 3;
    capobj->fieldnames = malloc(3 * sizeof(char *));
    capobj->fieldnames[0] = strdup("resource");
    capobj->fieldnames[1] = strdup("permission");
    capobj->fieldnames[2] = strdup("path");

    capobj->fieldvals = malloc(3 * sizeof(AKSValue));
    capobj->fieldvals[0] = resarg;
    capobj->fieldvals[1] = permarg;
    capobj->fieldvals[2] = patharg;

    AKSValue capval;
    capval =
        ((uint64_t)0xFFF6 << 48) | ((uintptr_t)capobj & 0x0000FFFFFFFFFFFFULL);

    PUSH(capval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "typeof") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: typeof() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    PUSH(ValTypeof(arg));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "panic") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: panic() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    const char *msg = "panic occurred";
    char temp_msg[512];
    if (ValKind(arg) == VALSTR) {
      msg = ValToStrPtr(arg)->data;
    } else {
      AKSValue str_val = ValToStr(arg);
      if (ValKind(str_val) == VALSTR) {
        strncpy(temp_msg, ValToStrPtr(str_val)->data, sizeof(temp_msg) - 1);
        temp_msg[sizeof(temp_msg) - 1] = '\0';
        msg = temp_msg;
      }
      ValFree(&str_val);
    }

    TriggerPanic(vm, task, msg, NULL);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SetPanicHook") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SetPanicHook() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue handler = POP();
    AKSValue old = vm->panic_hook;
    vm->panic_hook = ValCopy(handler);
    PUSH(old);
    ValFree(&handler);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "cause") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: cause() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue err = POP();
    AKSValue cause_val = ValNull();
    if (ValKind(err) == VALSTRUCT && ValToObj(err)) {
      AKSStruct *s = ValToObj(err);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "_cause") == 0) {
          cause_val = ValCopy(s->fieldvals[i]);
          break;
        }
      }
    }
    PUSH(cause_val);
    ValFree(&err);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "String") == 0 || strcmp(name, "str") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: %s() expects 1 argument, got %d\n", name,
              argc);
      return -1;
    }
    AKSValue arg = POP();
    PUSH(ValToStr(arg));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "fields") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: fields() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTRUCT || !ValToObj(arg)) {
      PUSH(ValArr(0));
    } else {
      AKSStruct *s = ValToObj(arg);
      AKSValue arr = ValArr(s->fieldcount);
      ValToArr(arr)->count = s->fieldcount;
      for (int i = 0; i < s->fieldcount; i++) {
        ValToArr(arr)->items[i] = ValStr(s->fieldnames[i]);
      }
      PUSH(arr);
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "has") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: has() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue fnameval = POP();
    AKSValue structval = POP();
    int has = 0;
    if (ValKind(structval) == VALSTRUCT && ValToObj(structval) &&
        ValKind(fnameval) == VALSTR) {
      AKSStruct *s = ValToObj(structval);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], ValToStrPtr(fnameval)->data) == 0) {
          has = 1;
          break;
        }
      }
    }
    PUSH(ValBool(has));
    ValFree(&fnameval);
    ValFree(&structval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "get") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: get() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue fnameval = POP();
    AKSValue structval = POP();
    AKSValue result = ValNull();
    if (ValKind(structval) == VALSTRUCT && ValToObj(structval) &&
        ValKind(fnameval) == VALSTR) {
      AKSStruct *s = ValToObj(structval);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], ValToStrPtr(fnameval)->data) == 0) {
          result = ValCopy(s->fieldvals[i]);
          break;
        }
      }
    }
    PUSH(result);
    ValFree(&fnameval);
    ValFree(&structval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "set") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: set() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue val = POP();
    AKSValue fnameval = POP();
    AKSValue structval = POP();
    if (ValKind(structval) == VALSTRUCT && ValToObj(structval) &&
        ValKind(fnameval) == VALSTR) {
      AKSStruct *s = ValToObj(structval);
      int found = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], ValToStrPtr(fnameval)->data) == 0) {
          found = i;
          break;
        }
      }
      if (found >= 0) {
        ValFree(&s->fieldvals[found]);
        s->fieldvals[found] = ValCopy(val);
      } else {
        s->fieldnames =
            realloc(s->fieldnames, (s->fieldcount + 1) * sizeof(char *));
        s->fieldvals =
            realloc(s->fieldvals, (s->fieldcount + 1) * sizeof(AKSValue));
        s->fieldnames[s->fieldcount] = strdup(ValToStrPtr(fnameval)->data);
        s->fieldvals[s->fieldcount] = ValCopy(val);
        s->fieldcount++;
      }
    }
    PUSH(ValNull());
    ValFree(&val);
    ValFree(&fnameval);
    ValFree(&structval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GetFunction") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: GetFunction() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    AKSValue result = ValNull();
    if (ValKind(arg) == VALSTR) {
      for (int i = 0; i < vm->fncount; i++) {
        if (strcmp(vm->fns[i]->name, ValToStrPtr(arg)->data) == 0) {
          result = ValFn(i);
          break;
        }
      }
    }
    PUSH(result);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FunctionName") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FunctionName() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    AKSValue result = ValNull();
    if (ValKind(arg) == VALFN) {
      if (ValToFn(arg) >= 0 && ValToFn(arg) < vm->fncount) {
        result = ValStr(vm->fns[ValToFn(arg)]->name);
      }
    }
    PUSH(result);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "globals") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: globals() expects 0 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arr = ValArr(vm->globalcount);
    ValToArr(arr)->count = vm->globalcount;
    for (int i = 0; i < vm->globalcount; i++) {
      ValToArr(arr)->items[i] = ValStr(vm->globals[i].name);
    }
    PUSH(arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Evaluate") == 0) {
    int isrestricted = 0;
    if (argc == 2) {
      AKSValue restrictarg = POP();
      isrestricted = ValIsTruthy(restrictarg);
      ValFree(&restrictarg);
    } else if (argc != 1) {
      fprintf(stderr,
              "Runtime error: Evaluate() expects 1 or 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: Evaluate() expects a string argument\n");
      ValFree(&arg);
      return -1;
    }
    int parseerr = 0;
    Node *prog = Parse(ValToStrPtr(arg)->data, &parseerr);
    if (parseerr || !prog) {
      if (prog)
        NodeFree(prog);
      ValFree(&arg);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    Compiler *tmpc = CompNew();
    tmpc->iseval = 1;
    for (int i = 0; i < vm->strcount; i++) {
      AddStr(tmpc, vm->strpool[i]);
    }
    for (int i = 1; i < vm->fncount; i++) {
      FnProto *vmfn = vm->fns[i];
      FnProto *newproto = calloc(1, sizeof(FnProto));
      snprintf(newproto->name, sizeof(newproto->name), "%s", vmfn->name);
      snprintf(newproto->file_name, sizeof(newproto->file_name), "%s", vmfn->file_name);
      newproto->id = vmfn->id;
      newproto->paramcount = vmfn->paramcount;
      newproto->localcount = vmfn->localcount;
      newproto->scopedepth = vmfn->scopedepth;
      newproto->isasync = vmfn->isasync;
      snprintf(newproto->rettype, sizeof(newproto->rettype), "%s", vmfn->rettype);
      for (int p = 0; p < vmfn->paramcount; p++) {
        snprintf(newproto->paramtypes[p], sizeof(newproto->paramtypes[p]), "%s", vmfn->paramtypes[p]);
      }
      newproto->chunk.count = vmfn->chunk.count;
      newproto->chunk.cap = vmfn->chunk.cap;
      if (vmfn->chunk.count > 0) {
        newproto->chunk.code = malloc(vmfn->chunk.count);
        memcpy(newproto->chunk.code, vmfn->chunk.code, vmfn->chunk.count);
        newproto->chunk.lines = malloc(vmfn->chunk.count * sizeof(int));
        memcpy(newproto->chunk.lines, vmfn->chunk.lines,
               vmfn->chunk.count * sizeof(int));
      }
      if (i < tmpc->fncount && tmpc->fns[i]) {
        free(tmpc->fns[i]->chunk.code);
        free(tmpc->fns[i]->chunk.lines);
        if (tmpc->fns[i]->chunk.globalcache)
          free(tmpc->fns[i]->chunk.globalcache);
        if (tmpc->fns[i]->chunk.loopcounters)
          free(tmpc->fns[i]->chunk.loopcounters);
        free(tmpc->fns[i]);
      }
      tmpc->fns[i] = newproto;
    }
    tmpc->fncount = vm->fncount;
    int compres = CompCompile(tmpc, prog);
    NodeFree(prog);
    if (!compres) {
      CompFree(tmpc);
      ValFree(&arg);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    MutexLockNative(vm->globallock);
    VMSync(vm, tmpc, 0);
    FnProto *evalfn = calloc(1, sizeof(FnProto));
    snprintf(evalfn->name, sizeof(evalfn->name), "axseval%d", vm->nexttaskid);
    evalfn->id = vm->fncount;
    evalfn->paramcount = 0;
    evalfn->localcount = tmpc->fns[0]->localcount;
    evalfn->scopedepth = 0;
    evalfn->chunk.count = tmpc->fns[0]->chunk.count;
    evalfn->chunk.cap = tmpc->fns[0]->chunk.cap;
    evalfn->chunk.code = malloc(evalfn->chunk.count);
    memcpy(evalfn->chunk.code, tmpc->fns[0]->chunk.code, evalfn->chunk.count);
    evalfn->chunk.lines = malloc(evalfn->chunk.count * sizeof(int));
    memcpy(evalfn->chunk.lines, tmpc->fns[0]->chunk.lines,
           evalfn->chunk.count * sizeof(int));
    if (vm->fncount < MAXFNS) {
      vm->fns[vm->fncount++] = evalfn;
    } else {
      fprintf(stderr, "VM error: Maximum function count reached in eval\n");
      free(evalfn->chunk.code);
      free(evalfn->chunk.lines);
      free(evalfn);
      MutexUnlockNative(vm->globallock);
      CompFree(tmpc);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    VMTask *evaltask = TaskNew(vm, evalfn, 0);
    MutexUnlockNative(vm->globallock);

    int savedrestricted = vm->inrestrictedeval;
    int savedsandbox = vm->sandboxenabled;
    uint32_t savedpermissions = vm->permissions;

    if (isrestricted) {
      vm->inrestrictedeval = 1;
      vm->sandboxenabled = 1;
      vm->permissions &= ~(PERMEXEC | PERMFFI | PERMWRITE);
    }

    int failed = 0;
    while (evaltask->state != 3 && !vm->shouldterminate) {
      int res = RunTaskSlice(vm, evaltask, 100);
      if (res == 1) {
        evaltask->state = 3;
      }
    }
    if (evaltask->haserror) {
      failed = 1;
    }

    vm->inrestrictedeval = savedrestricted;
    vm->sandboxenabled = savedsandbox;
    vm->permissions = savedpermissions;

    MutexLockNative(vm->globallock);
    AKSValue evalres = ValNull();
    if (evaltask->stacktop > 0) {
      evalres = ValCopy(evaltask->stack[evaltask->stacktop - 1]);
    }
    for (int i = 0; i < vm->alltaskscount; i++) {
      if (vm->alltasks[i] == evaltask) {
        for (int j = i; j < vm->alltaskscount - 1; j++) {
          vm->alltasks[j] = vm->alltasks[j + 1];
        }
        vm->alltaskscount--;
        break;
      }
    }
    TaskFree(evaltask);
    MutexUnlockNative(vm->globallock);
    CompFree(tmpc);
    ValFree(&arg);

    if (failed) {
      ValFree(&evalres);
      return -1;
    }

    PUSH(evalres);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "type") == 0 || strcmp(name, "TypeOf") == 0 ||
      strcmp(name, "typeof") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: type() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue v = POP();
    const char *ts = "null";
    switch (ValKind(v)) {
    case VALNULL:
      ts = "null";
      break;
    case VALBOOL:
      ts = "bool";
      break;
    case VALINT:
      ts = "int";
      break;
    case VALFLOAT:
      ts = "float";
      break;
    case VALSTR:
      ts = "string";
      break;
    case VALARR:
      ts = "array";
      break;
    case VALSTRUCT: {
      AKSStruct *st = ValToObj(v);
      ts = (st && st->typename) ? st->typename : "struct";
      break;
    }
    case VALFN:
      ts = "function";
      break;
    case VALTASK:
      ts = "task";
      break;
    default:
      ts = "unknown";
      break;
    }
    ValFree(&v);
    PUSH(ValStr(ts));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WeakRefNew") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WeakRefNew() expects 1 argument\n");
      return -1;
    }
    AKSValue target = POP();
    int id = -1;
    if (ValKind(target) == VALARR || ValKind(target) == VALSTRUCT) {
      id = AksWeakNew(target);
    }
    ValFree(&target);
    PUSH(ValInt(id));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WeakRefAlive") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WeakRefAlive() expects 1 argument\n");
      return -1;
    }
    AKSValue idval = POP();
    int alive = 0;
    if (ValKind(idval) == VALINT) {
      alive = AksWeakAlive((int)ValToInt(idval));
    }
    ValFree(&idval);
    PUSH(ValBool(alive));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WeakRefUpgrade") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WeakRefUpgrade() expects 1 argument\n");
      return -1;
    }
    AKSValue idval = POP();
    AKSValue res = ValNull();
    if (ValKind(idval) == VALINT) {
      res = AksWeakRawUpgrade((int)ValToInt(idval));
    }
    ValFree(&idval);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Integer") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Integer() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) == VALINT) {
      PUSH(ValCopy(arg));
    } else if (ValKind(arg) == VALFLOAT) {
      PUSH(ValInt((int64_t)ValToFloat(arg)));
    } else if (ValKind(arg) == VALSTR) {
      PUSH(ValInt((int64_t)strtoll(ValToStrPtr(arg)->data, NULL, 10)));
    } else if (ValKind(arg) == VALBOOL) {
      PUSH(ValInt(ValToBool(arg) ? 1 : 0));
    } else {
      PUSH(ValInt(0));
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "float") == 0 || strcmp(name, "Float") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: float() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) == VALFLOAT) {
      PUSH(ValCopy(arg));
    } else if (ValKind(arg) == VALINT) {
      PUSH(ValFloat((double)ValToInt(arg)));
    } else if (ValKind(arg) == VALSTR) {
      PUSH(ValFloat(strtod(ValToStrPtr(arg)->data, NULL)));
    } else if (ValKind(arg) == VALBOOL) {
      PUSH(ValFloat(ValToBool(arg) ? 1.0 : 0.0));
    } else {
      PUSH(ValFloat(0.0));
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Channel") == 0) {
    if (argc > 1) {
      fprintf(stderr,
              "Runtime error: Channel() expects 0 or 1 arguments, got %d\n",
              argc);
      return -1;
    }
    int capacity = 1;
    if (argc == 1) {
      AKSValue arg = POP();
      if (ValKind(arg) == VALINT)
        capacity = (int)ValToInt(arg);
      ValFree(&arg);
    }
    PUSH(ValChannel(ChanNew(capacity)));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "send") == 0 || strcmp(name, "Send") == 0 ||
      strcmp(name, "ChannelSend") == 0) {
    if (task->rendezvous_done) {
      task->rendezvous_done = 0;
      AKSValue val = POP();
      AKSValue chanval = POP();
      ValFree(&val);
      ValFree(&chanval);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: ChannelSend() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue val = POP();
    AKSValue chanval = POP();
    if (ValKind(chanval) != VALCHANNEL) {
      fprintf(stderr,
              "Runtime error: ChannelSend() first argument must be a channel, "
              "got type %d, val=%lld\n",
              ValKind(chanval), (long long)chanval);
      ValFree(&val);
      ValFree(&chanval);
      return -1;
    }
    VMChannel *c = ValToChan(chanval);
    MutexLockNative(c->lock);
    if (c->isclosed) {
      MutexUnlockNative(c->lock);
      ValFree(&val);
      ValFree(&chanval);
      fprintf(stderr, "Runtime error: send on closed channel\n");
      return -1;
    }
    if (c->capacity == 0) {
      if (task->rendezvous_done) {
        task->rendezvous_done = 0;
        MutexUnlockNative(c->lock);
        ValFree(&val);
        ValFree(&chanval);
        PUSH(ValNull());
        *stacktopptr = stacktop;
        return 1;
      }
      if (c->readercount > 0) {
        VMTask *reader = c->blockedreaders[0];
        for (int i = 0; i < c->readercount - 1; i++) {
          c->blockedreaders[i] = c->blockedreaders[i + 1];
        }
        c->readercount--;

        ValFree(&reader->stack[reader->stacktop - 1]);
        reader->stack[reader->stacktop - 1] = ValDeepClone(val);
        reader->rendezvous_done = 1;
        if (vm->sanitize_thread) {
          AksSanOnChannelSend(vm, task, c);
          AksSanOnChannelRecv(vm, reader, c);
        }

        WakeBlockedTaskOnChannel(vm, reader, c);
        MutexUnlockNative(c->lock);
        PUSH(ValNull());
        ValFree(&val);
        ValFree(&chanval);
        *stacktopptr = stacktop;
        return 1;
      } else {
        if (c->writercount >= 256) {
          MutexUnlockNative(c->lock);
          ValFree(&val);
          ValFree(&chanval);
          fprintf(stderr, "Runtime error: channel writer queue overflow\n");
          return -1;
        }
        c->blockedwriters[c->writercount++] = task;
        MutexUnlockNative(c->lock);
        PUSH(chanval);
        PUSH(val);
        *stacktopptr = stacktop;
        task->state = 2;
        task->blockedon = c;
        task->blockedontype = 1;
        return -2;
      }
    }
    if (c->count < c->capacity) {
      if (vm->sanitize_thread) AksSanOnChannelSend(vm, task, c);
      c->buffer[(c->head + c->count) % c->capacity] = ValDeepClone(val);
      c->count++;
      if (c->readercount > 0) {
        VMTask *reader = c->blockedreaders[0];
        for (int i = 0; i < c->readercount - 1; i++) {
          c->blockedreaders[i] = c->blockedreaders[i + 1];
        }
        c->readercount--;
        WakeBlockedTaskOnChannel(vm, reader, c);
      }
      MutexUnlockNative(c->lock);
      PUSH(ValNull());
      ValFree(&val);
      ValFree(&chanval);
      *stacktopptr = stacktop;
      return 1;
    } else {
      if (c->writercount >= 256) {
        MutexUnlockNative(c->lock);
        ValFree(&val);
        ValFree(&chanval);
        fprintf(stderr, "Runtime error: channel writer queue overflow\n");
        return -1;
      }
      c->blockedwriters[c->writercount++] = task;
      MutexUnlockNative(c->lock);
      PUSH(chanval);
      PUSH(val);
      *stacktopptr = stacktop;
      task->state = 2;
      task->blockedon = c;
      task->blockedontype = 1;
      return -2;
    }
  }
  if (strcmp(name, "Receive") == 0 || strcmp(name, "receive") == 0 ||
      strcmp(name, "ChannelReceive") == 0) {
    if (task->rendezvous_done) {
      task->rendezvous_done = 0;
      *stacktopptr = stacktop;
      return 1;
    }
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Receive() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue chanval = POP();
    if (ValKind(chanval) != VALCHANNEL) {
      fprintf(stderr,
              "Runtime error: Receive() argument must be a channel, got type "
              "%d, value %lld\n",
              ValKind(chanval),
              (long long)(ValKind(chanval) == VALINT ? ValToInt(chanval) : 0));
      ValFree(&chanval);
      return -1;
    }
    VMChannel *c = ValToChan(chanval);
    MutexLockNative(c->lock);
    if (c->capacity == 0) {
      if (task->rendezvous_done) {
        task->rendezvous_done = 0;
        MutexUnlockNative(c->lock);
        ValFree(&chanval);
        *stacktopptr = stacktop;
        return 1;
      }
      if (c->writercount > 0) {
        VMTask *writer = c->blockedwriters[0];
        for (int i = 0; i < c->writercount - 1; i++) {
          c->blockedwriters[i] = c->blockedwriters[i + 1];
        }
        c->writercount--;

        AKSValue val = ValCopy(writer->stack[writer->stacktop - 1]);
        writer->rendezvous_done = 1;
        if (vm->sanitize_thread) {
          AksSanOnChannelSend(vm, writer, c);
          AksSanOnChannelRecv(vm, task, c);
        }

        WakeBlockedTaskOnChannel(vm, writer, c);
        MutexUnlockNative(c->lock);
        PUSH(val);
        ValFree(&chanval);
        *stacktopptr = stacktop;
        return 1;
      } else {
        if (c->isclosed) {
          MutexUnlockNative(c->lock);
          PUSH(ValNull());
          ValFree(&chanval);
          *stacktopptr = stacktop;
          return 1;
        }
        if (c->readercount >= 256) {
          MutexUnlockNative(c->lock);
          ValFree(&chanval);
          fprintf(stderr, "Runtime error: channel reader queue overflow\n");
          return -1;
        }
        c->blockedreaders[c->readercount++] = task;
        MutexUnlockNative(c->lock);
        PUSH(chanval);
        *stacktopptr = stacktop;
        task->state = 2;
        task->blockedon = c;
        task->blockedontype = 1;
        return -2;
      }
    }
    if (c->count > 0) {
      if (vm->sanitize_thread) AksSanOnChannelRecv(vm, task, c);
      AKSValue val = c->buffer[c->head];
      c->head = (c->head + 1) % c->capacity;
      c->count--;
      if (c->writercount > 0) {
        VMTask *writer = c->blockedwriters[0];
        for (int i = 0; i < c->writercount - 1; i++) {
          c->blockedwriters[i] = c->blockedwriters[i + 1];
        }
        c->writercount--;
        WakeBlockedTaskOnChannel(vm, writer, c);
      }
      MutexUnlockNative(c->lock);
      PUSH(val);
      ValFree(&chanval);
      *stacktopptr = stacktop;
      return 1;
    } else {
      if (c->isclosed) {
        MutexUnlockNative(c->lock);
        PUSH(ValNull());
        ValFree(&chanval);
        *stacktopptr = stacktop;
        return 1;
      }
      if (c->readercount >= 256) {
        MutexUnlockNative(c->lock);
        ValFree(&chanval);
        fprintf(stderr, "Runtime error: channel reader queue overflow\n");
        return -1;
      }
      c->blockedreaders[c->readercount++] = task;
      MutexUnlockNative(c->lock);
      PUSH(chanval);
      *stacktopptr = stacktop;
      task->state = 2;
      task->blockedon = c;
      task->blockedontype = 1;
      return -2;
    }
  }
  if (strcmp(name, "close") == 0 || strcmp(name, "Close") == 0 ||
      strcmp(name, "ChannelClose") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ChannelClose() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue chanval = POP();
    if (ValKind(chanval) != VALCHANNEL) {
      fprintf(stderr,
              "Runtime error: ChannelClose() argument must be a channel\n");
      ValFree(&chanval);
      return -1;
    }
    VMChannel *c = ValToChan(chanval);
    MutexLockNative(c->lock);
    c->isclosed = 1;
    for (int i = 0; i < c->readercount; i++) {
      VMTask *reader = c->blockedreaders[i];
      WakeBlockedTaskOnChannel(vm, reader, c);
    }
    c->readercount = 0;
    for (int i = 0; i < c->writercount; i++) {
      VMTask *writer = c->blockedwriters[i];
      WakeBlockedTaskOnChannel(vm, writer, c);
    }
    c->writercount = 0;
    MutexUnlockNative(c->lock);
    PUSH(ValNull());
    ValFree(&chanval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ChannelLen") == 0 || strcmp(name, "ChannelCount") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: %s() expects 1 argument, got %d\n", name,
              argc);
      return -1;
    }
    AKSValue chanval = POP();
    if (ValKind(chanval) != VALCHANNEL) {
      fprintf(stderr, "Runtime error: %s() argument must be a channel\n", name);
      ValFree(&chanval);
      return -1;
    }
    VMChannel *c = ValToChan(chanval);
    MutexLockNative(c->lock);
    int count = c->count;
    MutexUnlockNative(c->lock);
    ValFree(&chanval);
    PUSH(ValInt(count));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ChannelCap") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: %s() expects 1 argument, got %d\n", name,
              argc);
      return -1;
    }
    AKSValue chanval = POP();
    if (ValKind(chanval) != VALCHANNEL) {
      fprintf(stderr, "Runtime error: %s() argument must be a channel\n", name);
      ValFree(&chanval);
      return -1;
    }
    VMChannel *c = ValToChan(chanval);
    int cap = c->capacity;
    ValFree(&chanval);
    PUSH(ValInt(cap));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ChannelIsClosed") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: %s() expects 1 argument, got %d\n", name,
              argc);
      return -1;
    }
    AKSValue chanval = POP();
    if (ValKind(chanval) != VALCHANNEL) {
      fprintf(stderr, "Runtime error: %s() argument must be a channel\n", name);
      ValFree(&chanval);
      return -1;
    }
    VMChannel *c = ValToChan(chanval);
    MutexLockNative(c->lock);
    int isclosed = c->isclosed;
    MutexUnlockNative(c->lock);
    ValFree(&chanval);
    PUSH(ValBool(isclosed));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "mutex") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: mutex() expects 0 arguments, got %d\n",
              argc);
      return -1;
    }
    PUSH(ValMutex(MutexNew()));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "lock") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: lock() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue mutval = POP();
    if (ValKind(mutval) != VALMUTEX) {
      fprintf(stderr, "Runtime error: lock() argument must be a mutex\n");
      ValFree(&mutval);
      return -1;
    }
    VMMutex *m = ValToMutex(mutval);
    MutexLockNative(m->lock);
    if (!m->islocked) {
      m->islocked = 1;
      m->owner = task;
      if (vm->sanitize_thread) AksSanOnMutexLock(vm, task, m);
      MutexUnlockNative(m->lock);
      PUSH(ValNull());
      ValFree(&mutval);
      *stacktopptr = stacktop;
      return 1;
    } else if (m->owner == task) {
      MutexUnlockNative(m->lock);
      PUSH(ValNull());
      ValFree(&mutval);
      *stacktopptr = stacktop;
      return 1;
    } else {
      if (m->blockedcount >= 256) {
        MutexUnlockNative(m->lock);
        ValFree(&mutval);
        fprintf(stderr, "Runtime error: mutex blocked queue overflow\n");
        return -1;
      }
      m->blockedtasks[m->blockedcount++] = task;
      MutexUnlockNative(m->lock);
      PUSH(mutval);
      *stacktopptr = stacktop;
      task->state = 2;
      task->blockedon = m;
      task->blockedontype = 2;
      return -2;
    }
  }
  if (strcmp(name, "unlock") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: unlock() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue mutval = POP();
    if (ValKind(mutval) != VALMUTEX) {
      fprintf(stderr, "Runtime error: unlock() argument must be a mutex\n");
      ValFree(&mutval);
      return -1;
    }
    VMMutex *m = ValToMutex(mutval);
    MutexLockNative(m->lock);
    if (m->islocked) {
      if (vm->sanitize_thread) AksSanOnMutexUnlock(vm, task, m);
      if (m->blockedcount > 0) {
        VMTask *nexttask = m->blockedtasks[0];
        for (int i = 0; i < m->blockedcount - 1; i++) {
          m->blockedtasks[i] = m->blockedtasks[i + 1];
        }
        m->blockedcount--;
        m->owner = nexttask;
        if (vm->sanitize_thread) AksSanOnMutexLock(vm, nexttask, m);
        nexttask->state = 0;
        int targetworker = rand() % vm->workercount;
        WorkerPushTail(&vm->workers[targetworker], nexttask);
      } else {
        m->islocked = 0;
        m->owner = NULL;
      }
    }
    MutexUnlockNative(m->lock);
    PUSH(ValNull());
    ValFree(&mutval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Semaphore") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Semaphore() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue initval = POP();
    if (ValKind(initval) != VALINT) {
      fprintf(stderr,
              "Runtime error: Semaphore() argument must be an integer\n");
      ValFree(&initval);
      return -1;
    }
    int initcount = (int)ValToInt(initval);
    ValFree(&initval);
    PUSH(ValSem(SemNew(initcount)));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "wait") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: wait() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue semval = POP();
    if (ValKind(semval) != VALSEM) {
      fprintf(stderr,
              "Runtime error: wait() argument must be a semaphore, got %s\n",
              ValTypeName(ValKind(semval)));
      ValFree(&semval);
      return -1;
    }
    VMSemaphore *s = ValToSem(semval);
    MutexLockNative(s->lock);
    if (s->count > 0) {
      s->count--;
      MutexUnlockNative(s->lock);
      PUSH(ValNull());
      ValFree(&semval);
      *stacktopptr = stacktop;
      return 1;
    } else {
      if (s->blockedcount >= 256) {
        MutexUnlockNative(s->lock);
        ValFree(&semval);
        fprintf(stderr, "Runtime error: semaphore blocked queue overflow\n");
        return -1;
      }
      s->blockedtasks[s->blockedcount++] = task;
      MutexUnlockNative(s->lock);
      PUSH(semval);
      *stacktopptr = stacktop;
      task->state = 2;
      task->blockedon = s;
      task->blockedontype = 3;
      return -2;
    }
  }
  if (strcmp(name, "post") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: post() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue semval = POP();
    if (ValKind(semval) != VALSEM) {
      fprintf(stderr, "Runtime error: post() argument must be a semaphore\n");
      ValFree(&semval);
      return -1;
    }
    VMSemaphore *s = ValToSem(semval);
    MutexLockNative(s->lock);
    if (s->blockedcount > 0) {
      VMTask *nexttask = s->blockedtasks[0];
      for (int i = 0; i < s->blockedcount - 1; i++) {
        s->blockedtasks[i] = s->blockedtasks[i + 1];
      }
      s->blockedcount--;
      s->count++;
      nexttask->state = 0;
      int targetworker = rand() % vm->workercount;
      WorkerPushTail(&vm->workers[targetworker], nexttask);
    } else {
      s->count++;
    }
    MutexUnlockNative(s->lock);
    PUSH(ValNull());
    ValFree(&semval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomCompareAndSwap") == 0) {
    if (argc != 4 && argc != 5) {
      fprintf(stderr,
              "Runtime error: AtomCompareAndSwap() expects 4 or 5 arguments, "
              "got %d\n",
              argc);
      return -1;
    }
    AKSValue ordval = (argc == 5) ? POP() : ValNull();
    AKSValue desired = POP();
    AKSValue expected = POP();
    AKSValue idxval = POP();
    AKSValue arrval = POP();
    if (ValKind(arrval) != VALARR || ValKind(idxval) != VALINT) {
      fprintf(stderr,
              "Runtime error: AtomCompareAndSwap() invalid argument types\n");
      ValFree(&ordval);
      ValFree(&desired);
      ValFree(&expected);
      ValFree(&idxval);
      ValFree(&arrval);
      return -1;
    }
    int64_t idx = ValToInt(idxval);
    AKSArray *a = ValToArr(arrval);
    if (idx < 0 || idx >= a->count) {
      fprintf(stderr,
              "Runtime error: AtomCompareAndSwap() index out of bounds\n");
      ValFree(&ordval);
      ValFree(&desired);
      ValFree(&expected);
      ValFree(&idxval);
      ValFree(&arrval);
      return -1;
    }
    MemOrder order = ParseMemOrder(ordval);
    if (order == ORDER_RELEASE || order == ORDER_SEQ_CST) {
      MemoryBarrier();
    }
    AKSValue prev = (AKSValue)InterlockedCompareExchange64(
        (LONG64 *)&a->items[idx], (LONG64)desired, (LONG64)expected);
    if (order == ORDER_ACQUIRE || order == ORDER_SEQ_CST) {
      MemoryBarrier();
    }
    if (prev == expected) {
      // Cas succeeded: desired is now in the array slot. The array now owns
      // desired's reference (transferred from the stack without refcount bump -
      // correct). The old value prev was evicted from the slot; release the
      // array's reference to it.
      ValFree(&prev);
      ValFree(&expected); // release the caller's stack copy of the old value
      PUSH(ValBool(1));
    } else {
      // Cas failed: array unchanged, release desired (it was never stored).
      ValFree(&expected);
      ValFree(&desired);
      PUSH(ValBool(0));
    }
    ValFree(&ordval);
    ValFree(&idxval);
    ValFree(&arrval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomAdd") == 0) {
    if (argc != 3 && argc != 4) {
      fprintf(stderr,
              "Runtime error: AtomAdd() expects 3 or 4 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue ordval = (argc == 4) ? POP() : ValNull();
    AKSValue addval = POP();
    AKSValue idxval = POP();
    AKSValue arrval = POP();
    if (ValKind(arrval) != VALARR || ValKind(idxval) != VALINT ||
        ValKind(addval) != VALINT) {
      fprintf(stderr, "Runtime error: AtomAdd() invalid argument types\n");
      ValFree(&ordval);
      ValFree(&addval);
      ValFree(&idxval);
      ValFree(&arrval);
      return -1;
    }
    int64_t idx = ValToInt(idxval);
    AKSArray *a = ValToArr(arrval);
    if (idx < 0 || idx >= a->count || ValKind(a->items[idx]) != VALINT) {
      fprintf(
          stderr,
          "Runtime error: AtomAdd() index out of bounds or not an integer\n");
      ValFree(&ordval);
      ValFree(&addval);
      ValFree(&idxval);
      ValFree(&arrval);
      return -1;
    }
    MemOrder order = ParseMemOrder(ordval);
    if (order == ORDER_RELEASE || order == ORDER_SEQ_CST) {
      MemoryBarrier();
    }
    int64_t prev = 0;
    for (;;) {
      AKSValue cur = *(volatile AKSValue *)&a->items[idx];
      if (ValKind(cur) != VALINT) {
        fprintf(stderr, "Runtime error: AtomAdd() target array element is not "
                        "an integer\n");
        ValFree(&ordval);
        ValFree(&addval);
        ValFree(&idxval);
        ValFree(&arrval);
        return -1;
      }
      prev = ValToInt(cur);
      int64_t next_int = prev + ValToInt(addval);
      AKSValue next_val = ValInt(next_int);
      if ((AKSValue)InterlockedCompareExchange64(
              (LONG64 *)&a->items[idx], (LONG64)next_val, (LONG64)cur) == cur) {
        break;
      }
    }
    if (order == ORDER_ACQUIRE || order == ORDER_SEQ_CST) {
      MemoryBarrier();
    }
    PUSH(ValInt(prev));
    ValFree(&ordval);
    ValFree(&addval);
    ValFree(&idxval);
    ValFree(&arrval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomSub") == 0) {
    if (argc != 3 && argc != 4) {
      fprintf(stderr,
              "Runtime error: AtomSub() expects 3 or 4 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue ordval = (argc == 4) ? POP() : ValNull();
    AKSValue subval = POP();
    AKSValue idxval = POP();
    AKSValue arrval = POP();
    if (ValKind(arrval) != VALARR || ValKind(idxval) != VALINT ||
        ValKind(subval) != VALINT) {
      fprintf(stderr, "Runtime error: AtomSub() invalid argument types\n");
      ValFree(&ordval);
      ValFree(&subval);
      ValFree(&idxval);
      ValFree(&arrval);
      return -1;
    }
    int64_t idx = ValToInt(idxval);
    AKSArray *a = ValToArr(arrval);
    if (idx < 0 || idx >= a->count || ValKind(a->items[idx]) != VALINT) {
      fprintf(
          stderr,
          "Runtime error: AtomSub() index out of bounds or not an integer\n");
      ValFree(&ordval);
      ValFree(&subval);
      ValFree(&idxval);
      ValFree(&arrval);
      return -1;
    }
    MemOrder order = ParseMemOrder(ordval);
    if (order == ORDER_RELEASE || order == ORDER_SEQ_CST) {
      MemoryBarrier();
    }
    int64_t prev = 0;
    for (;;) {
      AKSValue cur = *(volatile AKSValue *)&a->items[idx];
      if (ValKind(cur) != VALINT) {
        fprintf(stderr, "Runtime error: AtomSub() target array element is not "
                        "an integer\n");
        ValFree(&ordval);
        ValFree(&subval);
        ValFree(&idxval);
        ValFree(&arrval);
        return -1;
      }
      prev = ValToInt(cur);
      int64_t next_int = prev - ValToInt(subval);
      AKSValue next_val = ValInt(next_int);
      if ((AKSValue)InterlockedCompareExchange64(
              (LONG64 *)&a->items[idx], (LONG64)next_val, (LONG64)cur) == cur) {
        break;
      }
    }
    if (order == ORDER_ACQUIRE || order == ORDER_SEQ_CST) {
      MemoryBarrier();
    }
    PUSH(ValInt(prev));
    ValFree(&ordval);
    ValFree(&subval);
    ValFree(&idxval);
    ValFree(&arrval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomGet") == 0) {
    if (argc != 2 && argc != 3) {
      fprintf(stderr,
              "Runtime error: AtomGet() expects 2 or 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue ordval = (argc == 3) ? POP() : ValNull();
    AKSValue idxval = POP();
    AKSValue arrval = POP();
    if (ValKind(arrval) != VALARR || ValKind(idxval) != VALINT) {
      fprintf(stderr, "Runtime error: AtomGet() invalid argument types\n");
      ValFree(&ordval);
      ValFree(&idxval);
      ValFree(&arrval);
      return -1;
    }
    int64_t idx = ValToInt(idxval);
    AKSArray *a = ValToArr(arrval);
    if (idx < 0 || idx >= a->count) {
      fprintf(stderr, "Runtime error: AtomGet() index out of bounds\n");
      ValFree(&ordval);
      ValFree(&idxval);
      ValFree(&arrval);
      return -1;
    }
    MemOrder order = ParseMemOrder(ordval);
    AKSValue val;
    if (order == ORDER_SEQ_CST) {
      val = (AKSValue)InterlockedOr64((LONG64 *)&a->items[idx], 0);
    } else {
      val = *(volatile LONG64 *)&a->items[idx];
      if (order == ORDER_ACQUIRE) {
        MemoryBarrier();
      }
    }
    PUSH(ValCopy(val));
    ValFree(&ordval);
    ValFree(&idxval);
    ValFree(&arrval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomSet") == 0) {
    if (argc != 3 && argc != 4) {
      fprintf(stderr,
              "Runtime error: AtomSet() expects 3 or 4 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue ordval = (argc == 4) ? POP() : ValNull();
    AKSValue val = POP();
    AKSValue idxval = POP();
    AKSValue arrval = POP();
    if (ValKind(arrval) != VALARR || ValKind(idxval) != VALINT) {
      fprintf(stderr, "Runtime error: AtomSet() invalid argument types\n");
      ValFree(&ordval);
      ValFree(&val);
      ValFree(&idxval);
      ValFree(&arrval);
      return -1;
    }
    int64_t idx = ValToInt(idxval);
    AKSArray *a = ValToArr(arrval);
    if (idx < 0 || idx >= a->count) {
      fprintf(stderr, "Runtime error: AtomSet() index out of bounds\n");
      ValFree(&ordval);
      ValFree(&val);
      ValFree(&idxval);
      ValFree(&arrval);
      return -1;
    }
    MemOrder order = ParseMemOrder(ordval);
    AKSValue prev;
    if (order == ORDER_SEQ_CST) {
      prev = (AKSValue)InterlockedExchange64((LONG64 *)&a->items[idx], val);
    } else {
      if (order == ORDER_RELEASE) {
        MemoryBarrier();
      }
      prev = a->items[idx];
      *(volatile LONG64 *)&a->items[idx] = val;
    }
    PUSH(prev);
    ValFree(&ordval);
    ValFree(&idxval);
    ValFree(&arrval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "EffectHandle") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: EffectHandle() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue hmap = POP();
    AKSValue thunk = POP();
    if (ValKind(thunk) != VALFN) {
      fprintf(
          stderr,
          "Runtime error: EffectHandle() first argument must be a function\n");
      ValFree(&hmap);
      ValFree(&thunk);
      return -1;
    }
    int fnid = ValToFn(thunk);
    if (fnid < 0 || fnid >= vm->fncount) {
      fprintf(stderr, "Runtime error: EffectHandle() invalid function id %d\n",
              fnid);
      ValFree(&hmap);
      ValFree(&thunk);
      return -1;
    }
    VMTask *subtask = TaskNew(vm, vm->fns[fnid], 0);
    for (int i = 0; i < task->effect_handler_count && i < 31; i++) {
      subtask->effect_handlers[i] = ValCopy(task->effect_handlers[i]);
    }
    subtask->effect_handler_count = task->effect_handler_count;
    if (subtask->effect_handler_count < 32) {
      subtask->effect_handlers[subtask->effect_handler_count++] = ValCopy(hmap);
    }
    while (subtask->state != 3 && subtask->state != 4) {
      RunTaskSlice(vm, subtask, 50000);
      if (subtask->state == 2) {
        SleepMs(1);
      }
    }
    AKSValue res = ValCopy(subtask->result);
    TaskReLease(vm, subtask);
    PUSH(res);
    ValFree(&hmap);
    ValFree(&thunk);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "EffectPerform") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: EffectPerform() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue argsval = POP();
    AKSValue opval = POP();
    if (ValKind(opval) != VALSTR) {
      fprintf(stderr, "Runtime error: EffectPerform() first argument must be a "
                      "string op name\n");
      ValFree(&argsval);
      ValFree(&opval);
      return -1;
    }
    AKSString *opstr = ValToStrPtr(opval);
    const char *opname = (opstr && opstr->data) ? opstr->data : "";
    const char *bare_op = strrchr(opname, '.');
    if (bare_op)
      bare_op++;
    else
      bare_op = opname;

    AKSValue handler_fn = ValNull();
    int found_handler_depth = -1;
    for (int h = task->effect_handler_count - 1; h >= 0; h--) {
      AKSValue hm = task->effect_handlers[h];
      if (ValKind(hm) == VALSTRUCT) {
        AKSStruct *st = ValToObj(hm);
        for (int f = 0; f < st->fieldcount; f++) {
          if (strcmp(st->fieldnames[f], opname) == 0 ||
              strcmp(st->fieldnames[f], bare_op) == 0) {
            handler_fn = st->fieldvals[f];
            found_handler_depth = h;
            break;
          }
        }
      }
      if (found_handler_depth >= 0)
        break;
    }

    if (found_handler_depth < 0 || (ValKind(handler_fn) != VALFN &&
                                    ValKind(handler_fn) != VALBOUNDMETHOD)) {
      fprintf(stderr,
              "Runtime error: Unhandled algebraic effect operation '%s'\n",
              opname);
      ValFree(&argsval);
      ValFree(&opval);
      return -1;
    }

    int hfnid = (ValKind(handler_fn) == VALFN) ? ValToFn(handler_fn) : 0;
    VMTask *htask = TaskNew(vm, vm->fns[hfnid], 0);
    for (int k = 0; k < found_handler_depth && k < 32; k++) {
      htask->effect_handlers[k] = ValCopy(task->effect_handlers[k]);
    }
    htask->effect_handler_count = found_handler_depth;

    if (ValKind(argsval) == VALARR) {
      AKSArray *arr = ValToArr(argsval);
      FnProto *hproto = vm->fns[hfnid];
      for (int a = 0; a < arr->count && a < hproto->paramcount; a++) {
        ValFree(&htask->stack[a]);
        htask->stack[a] = ValCopy(arr->items[a]);
      }
    }
    while (htask->state != 3 && htask->state != 4) {
      RunTaskSlice(vm, htask, 50000);
      if (htask->state == 2) {
        SleepMs(1);
      }
    }
    AKSValue res = ValCopy(htask->result);
    TaskReLease(vm, htask);
    PUSH(res);
    ValFree(&argsval);
    ValFree(&opval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ParallelMapRaw") == 0) {
    if (argc != 2 && argc != 4) {
      fprintf(stderr,
              "Runtime error: ParallelMapRaw() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue fnval, arrval, tasksarrval, idxval;
    int isresume = (argc == 4);
    if (isresume) {
      idxval = POP();
      tasksarrval = POP();
      fnval = POP();
      arrval = POP();
    } else {
      arrval = POP();
      fnval = POP();
    }
    if (ValKind(arrval) != VALARR || ValKind(fnval) != VALFN) {
      fprintf(stderr,
              "Runtime error: ParallelMapRaw() invalid arguments: "
              "valtype(arrval)=%d (%s), valtype(fnval)=%d (%s), isresume=%d\n",
              ValKind(arrval), ValTypeName(ValKind(arrval)), ValKind(fnval),
              ValTypeName(ValKind(fnval)), isresume);
      ValFree(&fnval);
      ValFree(&arrval);
      if (isresume) {
        ValFree(&tasksarrval);
        ValFree(&idxval);
      }
      return -1;
    }
    AKSArray *arr = ValToArr(arrval);
    int count = arr->count;
    AKSValue tasksval;
    int currentidx = 0;
    if (!isresume) {
      tasksval = ValArr(count);
      ValToArr(tasksval)->count = count;
      for (int i = 0; i < count; i++) {
        FnProto *fnproto = vm->fns[ValToFn(fnval)];
        VMTask *spawned = TaskNew(vm, fnproto, 0);
        if (fnproto->paramcount > 0) {
          ValFree(&spawned->stack[0]);
          spawned->stack[0] = ValCopy(arr->items[i]);
        }
        int targetworker = rand() % vm->workercount;
        WorkerPushTail(&vm->workers[targetworker], spawned);
        ValToArr(tasksval)->items[i] = ValTask(spawned);
      }
    } else {
      tasksval = tasksarrval;
      currentidx = (int)ValToInt(idxval);
      ValFree(&idxval);
    }
    AKSArray *tasks = ValToArr(tasksval);
    while (currentidx < count) {
      VMTask *target = ValToTask(tasks->items[currentidx]);
      MutexLockNative(vm->globallock);
      if (target->state == 3) {
        AKSValue res = ValCopy(target->result);
        ValFree(&tasks->items[currentidx]);
        tasks->items[currentidx] = res;
        MutexUnlockNative(vm->globallock);
        currentidx++;
      } else {
        if (target->awaitercount >= target->awaitercap) {
          target->awaitercap =
              target->awaitercap == 0 ? 4 : target->awaitercap * 2;
          target->awaiters =
              realloc(target->awaiters, target->awaitercap * sizeof(VMTask *));
        }
        target->awaiters[target->awaitercount++] = task;
        MutexUnlockNative(vm->globallock);
        PUSH(arrval);
        PUSH(fnval);
        PUSH(tasksval);
        PUSH(ValInt(currentidx));
        *stacktopptr = stacktop;
        task->state = 2;
        task->blockedon = target;
        task->blockedontype = 4;
        return -2;
      }
    }
    PUSH(tasksval);
    ValFree(&fnval);
    ValFree(&arrval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ParallelReduceRaw") == 0) {
    if (argc != 3 && argc != 6) {
      fprintf(
          stderr,
          "Runtime error: ParallelReduceRaw() expects 3 arguments, got %d\n",
          argc);
      return -1;
    }
    AKSValue fnval, arrval, initval, tasksarrval, idxval, phaseval;
    int isresume = (argc == 6);
    if (isresume) {
      phaseval = POP();
      idxval = POP();
      tasksarrval = POP();
      initval = POP();
      fnval = POP();
      arrval = POP();
    } else {
      initval = POP();
      arrval = POP();
      fnval = POP();
    }
    if (ValKind(arrval) != VALARR || ValKind(fnval) != VALFN) {
      fprintf(stderr, "Runtime error: ParallelReduceRaw() invalid arguments\n");
      ValFree(&fnval);
      ValFree(&arrval);
      ValFree(&initval);
      if (isresume) {
        ValFree(&tasksarrval);
        ValFree(&idxval);
        ValFree(&phaseval);
      }
      return -1;
    }
    AKSValue workingarrval;
    int currentidx = 0;
    int phase = 0;
    int pairs = 0;
    if (!isresume) {
      workingarrval = ValArr(ValToArr(arrval)->count + 1);
      ValToArr(workingarrval)->count = ValToArr(arrval)->count + 1;
      ValToArr(workingarrval)->items[0] = ValCopy(initval);
      for (int i = 0; i < ValToArr(arrval)->count; i++) {
        ValToArr(workingarrval)->items[i + 1] =
            ValCopy(ValToArr(arrval)->items[i]);
      }
    } else {
      workingarrval = tasksarrval;
      currentidx = (int)ValToInt(idxval);
      pairs = (int)ValToInt(phaseval);
      phase = 1;
      ValFree(&idxval);
      ValFree(&phaseval);
    }
    while (ValToArr(workingarrval)->count > 1 || phase == 1) {
      int n = ValToArr(workingarrval)->count;
      if (phase == 0) {
        pairs = n / 2;
        int nextsize = pairs + (n % 2);
        AKSValue nextarr = ValArr(nextsize);
        ValToArr(nextarr)->count = nextsize;
        for (int i = 0; i < pairs; i++) {
          AKSValue left = ValToArr(workingarrval)->items[i * 2];
          AKSValue right = ValToArr(workingarrval)->items[i * 2 + 1];
          FnProto *fnproto = vm->fns[ValToFn(fnval)];
          VMTask *spawned = TaskNew(vm, fnproto, 0);
          if (fnproto->paramcount > 0) {
            ValFree(&spawned->stack[0]);
            spawned->stack[0] = ValCopy(left);
          }
          if (fnproto->paramcount > 1) {
            ValFree(&spawned->stack[1]);
            spawned->stack[1] = ValCopy(right);
          }
          int targetworker = rand() % vm->workercount;
          WorkerPushTail(&vm->workers[targetworker], spawned);
          ValToArr(nextarr)->items[i] = ValTask(spawned);
        }
        if (n % 2 != 0) {
          ValToArr(nextarr)->items[pairs] =
              ValCopy(ValToArr(workingarrval)->items[n - 1]);
        }
        ValFree(&workingarrval);
        workingarrval = nextarr;
        currentidx = 0;
        phase = 1;
      }
      while (currentidx < pairs) {
        VMTask *target = ValToTask(ValToArr(workingarrval)->items[currentidx]);
        MutexLockNative(vm->globallock);
        if (target->state == 3) {
          AKSValue res = ValCopy(target->result);
          ValFree(&ValToArr(workingarrval)->items[currentidx]);
          ValToArr(workingarrval)->items[currentidx] = res;
          MutexUnlockNative(vm->globallock);
          currentidx++;
        } else {
          if (target->awaitercount >= target->awaitercap) {
            target->awaitercap =
                target->awaitercap == 0 ? 4 : target->awaitercap * 2;
            target->awaiters = realloc(target->awaiters,
                                       target->awaitercap * sizeof(VMTask *));
          }
          target->awaiters[target->awaitercount++] = task;
          MutexUnlockNative(vm->globallock);
          PUSH(arrval);
          PUSH(fnval);
          PUSH(initval);
          PUSH(workingarrval);
          PUSH(ValInt(currentidx));
          PUSH(ValInt(pairs));
          *stacktopptr = stacktop;
          task->state = 2;
          task->blockedon = target;
          task->blockedontype = 4;
          return -2;
        }
      }
      phase = 0;
    }
    AKSValue finalres = ValCopy(ValToArr(workingarrval)->items[0]);
    ValFree(&workingarrval);
    ValFree(&fnval);
    ValFree(&arrval);
    ValFree(&initval);
    PUSH(finalres);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "actor") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: actor() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue fnval = POP();
    if (ValKind(fnval) != VALFN) {
      fprintf(stderr, "Runtime error: actor() argument must be a function\n");
      ValFree(&fnval);
      return -1;
    }
    VMChannel *mailbox = ChanNew(256);
    FnProto *fnproto = vm->fns[ValToFn(fnval)];
    VMTask *t = TaskNew(vm, fnproto, 0);
    if (fnproto->paramcount > 0) {
      ValFree(&t->stack[0]);
      mailbox->refcount++;
      t->stack[0] = ValChannel(mailbox);
    }
    int targetworker = rand() % vm->workercount;
    WorkerPushTail(&vm->workers[targetworker], t);
    PUSH(ValChannel(mailbox));
    ValFree(&fnval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ActorSend") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: ActorSend() expects 2 arguments, got %d\n", argc);
      return -1;
    }
    return AksExecuteStdlibFn(vm, task, "send", 2, stacktopptr);
  }
  if (strcmp(name, "ReinterpretCast") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: ReinterpretCast() expects at least 2 "
                      "arguments (type, buffer, [offset])\n");
      return -1;
    }
    AKSValue offset_val = ValInt(0);
    if (argc >= 3) {
      offset_val = POP();
    }
    AKSValue buf_val = POP();
    AKSValue type_val = POP();
    int64_t offset = (ValKind(offset_val) == VALINT) ? ValToInt(offset_val) : 0;
    ValFree(&offset_val);

    const char *type_name =
        (ValKind(type_val) == VALSTR) ? ValToStrPtr(type_val)->data : "any";
    AKSValue res = ValStruct(type_name);
    AKSStruct *st = ValToObj(res);

    int sidx = -1;
    for (int i = 0; i < vm->structcount; i++) {
      if (strcmp(vm->structs[i].name, type_name) == 0) {
        sidx = i;
        break;
      }
    }

    if (ValKind(buf_val) == VALARR) {
      AKSArray *arr = ValToArr(buf_val);
      if (sidx >= 0) {
        for (int i = 0; i < vm->structs[sidx].fc && (offset + i) < arr->count;
             i++) {
          StructSetFieldNative(st, vm->structs[sidx].fnames[i],
                               arr->items[offset + i]);
        }
      } else {
        for (int i = 0; (offset + i) < arr->count; i++) {
          char fbuf[32];
          snprintf(fbuf, sizeof(fbuf), "f%d", i);
          StructSetFieldNative(st, fbuf, arr->items[offset + i]);
        }
      }
    } else if (ValKind(buf_val) == VALSTR) {
      AKSString *s = ValToStrPtr(buf_val);
      if (sidx >= 0) {
        for (int i = 0;
             i < vm->structs[sidx].fc && (offset + i) < (int64_t)s->len; i++) {
          StructSetFieldNative(st, vm->structs[sidx].fnames[i],
                               ValInt((uint8_t)s->data[offset + i]));
        }
      } else {
        for (int i = 0; (offset + i) < (int64_t)s->len; i++) {
          char fbuf[32];
          snprintf(fbuf, sizeof(fbuf), "f%d", i);
          StructSetFieldNative(st, fbuf, ValInt((uint8_t)s->data[offset + i]));
        }
      }
    }
    ValFree(&buf_val);
    ValFree(&type_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "spawn_remote") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: spawn_remote() expects 2 arguments "
                      "(node_address, handler_fn)\n");
      return -1;
    }
    AKSValue fn_val = POP();
    AKSValue node_val = POP();
    VMChannel *mailbox = ChanNew(64);
    AKSValue actor_res = ValStruct("RemoteActor");
    AKSValue chan_val = ValChannel(mailbox);
    StructSetFieldNative(ValToObj(actor_res), "node", node_val);
    StructSetFieldNative(ValToObj(actor_res), "channel", chan_val);
    ValFree(&chan_val);
    ValFree(&node_val);
    ValFree(&fn_val);
    PUSH(actor_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GetStaticRcEliminated") == 0) {
    PUSH(ValInt((int64_t)vm->static_rc_eliminated));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GreenSchedWorkers") == 0) {
    PUSH(ValInt(vm->workercount));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GreenSchedStats") == 0) {
    int active = 0;
    int completed = 0;
    for (int i = 0; i < vm->alltaskscount; i++) {
      if (vm->alltasks[i]) {
        if (vm->alltasks[i]->state == 3)
          completed++;
        else
          active++;
      }
    }
    AKSValue s_val = ValStruct("GreenSchedStats");
    AKSStruct *s = ValToObj(s_val);
    s->fieldcount = 5;
    s->fieldnames = malloc(5 * sizeof(char *));
    s->fieldvals = malloc(5 * sizeof(AKSValue));
    s->fieldnames[0] = strdup("workers");
    s->fieldvals[0] = ValInt(vm->workercount);
    s->fieldnames[1] = strdup("active_tasks");
    s->fieldvals[1] = ValInt(active);
    s->fieldnames[2] = strdup("completed_tasks");
    s->fieldvals[2] = ValInt(completed);
    s->fieldnames[3] = strdup("total_tasks");
    s->fieldvals[3] = ValInt(vm->alltaskscount);
    s->fieldnames[4] = strdup("work_stealing");
    s->fieldvals[4] = ValInt(1);
    PUSH(s_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GreenSchedYield") == 0) {
#ifdef _WIN32
    SwitchToThread();
#else
    sched_yield();
#endif
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GreenSchedSubmit") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: GreenSchedSubmit() expects at least 1 "
                      "argument (fn, ...)\n");
      return -1;
    }
    int extra_args = argc - 1;
    AKSValue passed_args[32];
    for (int i = extra_args - 1; i >= 0; i--) {
      passed_args[i] = POP();
    }
    AKSValue fn_val = POP();
    if (ValKind(fn_val) != VALFN) {
      for (int i = 0; i < extra_args; i++)
        ValFree(&passed_args[i]);
      ValFree(&fn_val);
      fprintf(stderr,
              "Runtime error: GreenSchedSubmit() target is not a function\n");
      return -1;
    }
    FnProto *callee = vm->fns[ValToFn(fn_val)];
    ValFree(&fn_val);
    VMTask *spawned = TaskNew(vm, callee, 0);
    for (int i = 0; i < extra_args && i < callee->paramcount; i++) {
      ValFree(&spawned->stack[i]);
      spawned->stack[i] = passed_args[i];
    }
    for (int i = callee->paramcount; i < extra_args; i++) {
      ValFree(&passed_args[i]);
    }
    int targetworker = rand() % vm->workercount;
    WorkerPushTail(&vm->workers[targetworker], spawned);
    PUSH(ValTask(spawned));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GreenSchedParallelFor") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Runtime error: GreenSchedParallelFor expects 3 "
                      "arguments (start, end, fn)\n");
      return -1;
    }
    AKSValue fn_val = POP();
    AKSValue end_val = POP();
    AKSValue start_val = POP();
    int64_t start = ValToInt(start_val);
    int64_t end = ValToInt(end_val);
    ValFree(&start_val);
    ValFree(&end_val);
    if (ValKind(fn_val) != VALFN) {
      ValFree(&fn_val);
      fprintf(
          stderr,
          "Runtime error: GreenSchedParallelFor target is not a function\n");
      return -1;
    }
    FnProto *callee = vm->fns[ValToFn(fn_val)];
    ValFree(&fn_val);

    int64_t nitems = end - start;
    if (nitems <= 0) {
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }

    int ntasks = (int)(nitems > 16 ? 16 : nitems);
    VMTask *tasks[16];
    int64_t chunk_size = (nitems + ntasks - 1) / ntasks;

    for (int t = 0; t < ntasks; t++) {
      int64_t cstart = start + t * chunk_size;
      int64_t cend = cstart + chunk_size;
      if (cend > end)
        cend = end;
      if (cstart >= cend) {
        tasks[t] = NULL;
        continue;
      }
      VMTask *sp = TaskNew(vm, callee, 0);
      if (callee->paramcount >= 2) {
        sp->stack[0] = ValInt(cstart);
        sp->stack[1] = ValInt(cend);
      } else if (callee->paramcount >= 1) {
        sp->stack[0] = ValInt(cstart);
      }
      int targetworker = t % vm->workercount;
      WorkerPushTail(&vm->workers[targetworker], sp);
      tasks[t] = sp;
    }

    for (int t = 0; t < ntasks; t++) {
      if (!tasks[t])
        continue;
      while (tasks[t]->state != 3 && !vm->shouldterminate) {
#ifdef _WIN32
        SwitchToThread();
#else
        sched_yield();
#endif
      }
    }

    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TypeInfo") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: TypeInfo() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue val = POP();
    AKSValue res = ValStruct("TypeInfo");
    AKSStruct *st = ValToObj(res);

    const char *kind = "unknown";
    const char *typename_str = "unknown";
    int64_t size_val = 0;
    int is_prim = 0;
    AKSValue fields_arr = ValNull();

    int k = ValKind(val);
    switch (k) {
    case VALINT:
      kind = "int";
      typename_str = "int";
      size_val = 8;
      is_prim = 1;
      break;
    case VALFLOAT:
      kind = "float";
      typename_str = "float";
      size_val = 8;
      is_prim = 1;
      break;
    case VALBOOL:
      kind = "bool";
      typename_str = "bool";
      size_val = 1;
      is_prim = 1;
      break;
    case VALNULL:
      kind = "null";
      typename_str = "null";
      size_val = 0;
      is_prim = 1;
      break;
    case VALSTR:
      kind = "string";
      typename_str = "string";
      size_val = ValToStrPtr(val) ? (int64_t)ValToStrPtr(val)->len : 0;
      is_prim = 1;
      break;
    case VALARR: {
      kind = "array";
      typename_str = "array";
      AKSArray *arr = ValToArr(val);
      size_val = arr ? arr->count : 0;
      is_prim = 0;
      break;
    }
    case VALSTRUCT: {
      kind = "struct";
      AKSStruct *src = ValToObj(val);
      if (src) {
        typename_str =
            (src->typename && src->typename[0]) ? src->typename : "struct";
        size_val = src->fieldcount;
        fields_arr = ValArr(src->fieldcount);
        AKSArray *f_arr = ValToArr(fields_arr);
        for (int fi = 0; fi < src->fieldcount; fi++) {
          f_arr->items[f_arr->count++] = ValStr(src->fieldnames[fi]);
        }
      }
      is_prim = 0;
      break;
    }
    case VALFN:
      kind = "function";
      typename_str = "function";
      size_val = 0;
      is_prim = 0;
      break;
    case VALCHANNEL: {
      kind = "channel";
      typename_str = "channel";
      VMChannel *ch = ValToChan(val);
      size_val = ch ? ch->capacity : 0;
      is_prim = 0;
      break;
    }
    default:
      kind = "object";
      typename_str = "object";
      size_val = 0;
      is_prim = 0;
      break;
    }

    if (ValKind(fields_arr) == VALNULL) {
      fields_arr = ValArr(0);
    }

    st->fieldcount = 5;
    st->fieldnames = (char **)malloc(5 * sizeof(char *));
    st->fieldvals = (AKSValue *)malloc(5 * sizeof(AKSValue));

    st->fieldnames[0] = strdup("kind");
    st->fieldvals[0] = ValStr(kind);

    st->fieldnames[1] = strdup("typeName");
    st->fieldvals[1] = ValStr(typename_str);

    st->fieldnames[2] = strdup("size");
    st->fieldvals[2] = ValInt(size_val);

    st->fieldnames[3] = strdup("isPrimitive");
    st->fieldvals[3] = ValBool(is_prim);

    st->fieldnames[4] = strdup("fields");
    st->fieldvals[4] = fields_arr;

    ValFree(&val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ChannelTryReceive") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ChannelTryReceive() expects 1 argument\n");
      return -1;
    }
    AKSValue chanval = POP();
    if (ValKind(chanval) != VALCHANNEL) {
      ValFree(&chanval);
      return -1;
    }
    VMChannel *c = ValToChan(chanval);
    MutexLockNative(c->lock);
    int ok = 0;
    AKSValue item = ValNull();
    if (c->capacity == 0) {
      if (c->writercount > 0) {
        VMTask *writer = c->blockedwriters[0];
        for (int i = 0; i < c->writercount - 1; i++) {
          c->blockedwriters[i] = c->blockedwriters[i + 1];
        }
        c->writercount--;
        item = ValCopy(writer->stack[writer->stacktop - 1]);
        writer->rendezvous_done = 1;
        WakeBlockedTaskOnChannel(vm, writer, c);
        ok = 1;
      }
    } else {
      if (c->count > 0) {
        item = c->buffer[c->head];
        c->head = (c->head + 1) % c->capacity;
        c->count--;
        if (c->writercount > 0) {
          VMTask *writer = c->blockedwriters[0];
          for (int i = 0; i < c->writercount - 1; i++) {
            c->blockedwriters[i] = c->blockedwriters[i + 1];
          }
          c->writercount--;
          c->buffer[c->tail] = ValCopy(writer->stack[writer->stacktop - 1]);
          c->tail = (c->tail + 1) % c->capacity;
          c->count++;
          WakeBlockedTaskOnChannel(vm, writer, c);
        }
        ok = 1;
      }
    }
    MutexUnlockNative(c->lock);
    ValFree(&chanval);

    AKSValue res_arr = ValArr(2);
    AKSArray *arr = ValToArr(res_arr);
    arr->items[0] = ValBool(ok);
    arr->items[1] = item;
    arr->count = 2;

    PUSH(res_arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ChannelTrySend") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ChannelTrySend() expects 2 arguments\n");
      return -1;
    }
    AKSValue val = POP();
    AKSValue chanval = POP();
    if (ValKind(chanval) != VALCHANNEL) {
      ValFree(&val);
      ValFree(&chanval);
      return -1;
    }
    VMChannel *c = ValToChan(chanval);
    MutexLockNative(c->lock);
    int ok = 0;
    if (!c->isclosed) {
      if (c->capacity == 0) {
        if (c->readercount > 0) {
          VMTask *reader = c->blockedreaders[0];
          for (int i = 0; i < c->readercount - 1; i++) {
            c->blockedreaders[i] = c->blockedreaders[i + 1];
          }
          c->readercount--;
          reader->stack[reader->stacktop - 1] = ValCopy(val);
          reader->rendezvous_done = 1;
          WakeBlockedTaskOnChannel(vm, reader, c);
          ok = 1;
        }
      } else {
        if (c->count < c->capacity) {
          c->buffer[c->tail] = ValCopy(val);
          c->tail = (c->tail + 1) % c->capacity;
          c->count++;
          if (c->readercount > 0) {
            VMTask *reader = c->blockedreaders[0];
            for (int i = 0; i < c->readercount - 1; i++) {
              c->blockedreaders[i] = c->blockedreaders[i + 1];
            }
            c->readercount--;
            WakeBlockedTaskOnChannel(vm, reader, c);
          }
          ok = 1;
        }
      }
    }
    MutexUnlockNative(c->lock);
    ValFree(&val);
    ValFree(&chanval);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SynthesizeStruct") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: SynthesizeStruct(name, fieldNames, "
                      "[defaultVals]) requires at least 2 arguments\n");
      return -1;
    }
    AKSValue defaultVals = (argc >= 3) ? POP() : ValNull();
    AKSValue fieldNames = POP();
    AKSValue typeNameVal = POP();

    const char *tname = "SynthesizedType";
    if (ValKind(typeNameVal) == VALSTR && ValToStrPtr(typeNameVal)) {
      tname = ValToStrPtr(typeNameVal)->data;
    }

    AKSValue res = ValStruct(tname);
    AKSStruct *st = ValToObj(res);

    if (ValKind(fieldNames) == VALARR && ValToArr(fieldNames)) {
      AKSArray *farr = ValToArr(fieldNames);
      AKSArray *darr =
          (ValKind(defaultVals) == VALARR) ? ValToArr(defaultVals) : NULL;
      for (int i = 0; i < farr->count; i++) {
        const char *fname = "field";
        if (ValKind(farr->items[i]) == VALSTR && ValToStrPtr(farr->items[i])) {
          fname = ValToStrPtr(farr->items[i])->data;
        }
        AKSValue fval = ValNull();
        if (darr && i < darr->count) {
          fval = darr->items[i];
        }
        StructSetFieldNative(st, fname, fval);
      }
    }
    ValFree(&defaultVals);
    ValFree(&fieldNames);
    ValFree(&typeNameVal);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StructGetField") == 0) {
    if (argc != 2)
      return -1;
    AKSValue fval = POP();
    AKSValue sval = POP();
    AKSValue result = ValNull();
    if (ValKind(sval) == VALSTRUCT && ValToObj(sval) &&
        ValKind(fval) == VALSTR && ValToStrPtr(fval)) {
      AKSStruct *st = ValToObj(sval);
      const char *fn = ValToStrPtr(fval)->data;
      for (int i = 0; i < st->fieldcount; i++) {
        if (strcmp(st->fieldnames[i], fn) == 0) {
          result = ValCopy(st->fieldvals[i]);
          break;
        }
      }
    }
    ValFree(&fval);
    ValFree(&sval);
    PUSH(result);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StructSetField") == 0) {
    if (argc != 3)
      return -1;
    AKSValue val = POP();
    AKSValue fval = POP();
    AKSValue sval = POP();
    int ok = 0;
    if (ValKind(sval) == VALSTRUCT && ValToObj(sval) &&
        ValKind(fval) == VALSTR && ValToStrPtr(fval)) {
      StructSetFieldNative(ValToObj(sval), ValToStrPtr(fval)->data, val);
      ok = 1;
    }
    ValFree(&val);
    ValFree(&fval);
    ValFree(&sval);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

#undef PUSH
#undef POP
  return 0;
}
