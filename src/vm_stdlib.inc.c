/* Reverse-mode automatic differentiation computational graph and tape engine */

typedef struct {
  int id;
  int op;    /* 0=VAR, 1=CONST, 2=ADD, 3=SUB, 4=MUL, 5=DIV, 6=NEG, 7=POW,
                8=EXP, 9=LOG, 10=SIN, 11=COS, 12=TAN, 13=RELU, 14=SIGMOID,
                15=TANH, 16=SQRT, 17=TENSOR_VAR, 18=TENSOR_MATMUL, 19=TENSOR_ADD, 20=TENSOR_RELU,
                21=TENSOR_MSE, 22=TENSOR_CROSS_ENTROPY */
  double val;
  double grad;
  int lhs;   /* Node index of first operand (-1 if none) */
  int rhs;   /* Node index of second operand (-1 if none) */
  double aux;/* Auxiliary scalar constant (for scalar ops or constants) */
  int is_tensor;
  int rows;
  int cols;
  double *tval;
  double *tgrad;
} TapeNode;

typedef struct {
  int id;
  int active;
  TapeNode *nodes;
  int count;
  int capacity;
} AutodiffTape;

#define MAX_AUTODIFF_TAPES 64
static AutodiffTape g_autodiff_tapes[MAX_AUTODIFF_TAPES];
static int g_autodiff_inited = 0;

static void AutodiffFreeTapeNodes(AutodiffTape *t) {
  if (!t || !t->nodes) return;
  for (int i = 0; i < t->count; i++) {
    if (t->nodes[i].is_tensor) {
      if (t->nodes[i].tval) { free(t->nodes[i].tval); t->nodes[i].tval = NULL; }
      if (t->nodes[i].tgrad) { free(t->nodes[i].tgrad); t->nodes[i].tgrad = NULL; }
    }
  }
  t->count = 0;
}

static void AutodiffInitTapes(void) {
  if (g_autodiff_inited) return;
  memset(g_autodiff_tapes, 0, sizeof(g_autodiff_tapes));
  g_autodiff_tapes[0].id = 0;
  g_autodiff_tapes[0].active = 1;
  g_autodiff_tapes[0].capacity = 1024;
  g_autodiff_tapes[0].nodes = (TapeNode *)malloc(sizeof(TapeNode) * 1024);
  memset(g_autodiff_tapes[0].nodes, 0, sizeof(TapeNode) * 1024);
  g_autodiff_tapes[0].count = 0;
  g_autodiff_inited = 1;
}

static AutodiffTape *AutodiffGetTape(int tape_id) {
  AutodiffInitTapes();
  if (tape_id < 0 || tape_id >= MAX_AUTODIFF_TAPES) return &g_autodiff_tapes[0];
  if (!g_autodiff_tapes[tape_id].active) {
    g_autodiff_tapes[tape_id].id = tape_id;
    g_autodiff_tapes[tape_id].active = 1;
    g_autodiff_tapes[tape_id].capacity = 1024;
    g_autodiff_tapes[tape_id].nodes = (TapeNode *)malloc(sizeof(TapeNode) * 1024);
    memset(g_autodiff_tapes[tape_id].nodes, 0, sizeof(TapeNode) * 1024);
    g_autodiff_tapes[tape_id].count = 0;
  }
  return &g_autodiff_tapes[tape_id];
}

static int AutodiffAllocNode(AutodiffTape *t, int op, double val, int lhs, int rhs, double aux) {
  if (t->count >= t->capacity) {
    int oldcap = t->capacity;
    t->capacity = (t->capacity < 1024) ? 1024 : (t->capacity * 2);
    t->nodes = (TapeNode *)realloc(t->nodes, sizeof(TapeNode) * t->capacity);
    memset(t->nodes + oldcap, 0, sizeof(TapeNode) * (t->capacity - oldcap));
  }
  int nid = t->count++;
  t->nodes[nid].id = nid;
  t->nodes[nid].op = op;
  t->nodes[nid].val = val;
  t->nodes[nid].grad = 0.0;
  t->nodes[nid].lhs = lhs;
  t->nodes[nid].rhs = rhs;
  t->nodes[nid].aux = aux;
  t->nodes[nid].is_tensor = 0;
  t->nodes[nid].rows = 0;
  t->nodes[nid].cols = 0;
  t->nodes[nid].tval = NULL;
  t->nodes[nid].tgrad = NULL;
  return nid;
}

static int AutodiffAllocTensorNode(AutodiffTape *t, int op, int rows, int cols, int lhs, int rhs, const double *init_val) {
  if (t->count >= t->capacity) {
    int oldcap = t->capacity;
    t->capacity = (t->capacity < 1024) ? 1024 : (t->capacity * 2);
    t->nodes = (TapeNode *)realloc(t->nodes, sizeof(TapeNode) * t->capacity);
    memset(t->nodes + oldcap, 0, sizeof(TapeNode) * (t->capacity - oldcap));
  }
  int nid = t->count++;
  TapeNode *n = &t->nodes[nid];
  n->id = nid;
  n->op = op;
  n->val = 0.0;
  n->grad = 0.0;
  n->lhs = lhs;
  n->rhs = rhs;
  n->aux = 0.0;
  n->is_tensor = 1;
  n->rows = rows;
  n->cols = cols;
  int total = rows * cols;
  n->tval = (double *)calloc(total > 0 ? total : 1, sizeof(double));
  n->tgrad = (double *)calloc(total > 0 ? total : 1, sizeof(double));
  if (init_val && total > 0) {
    memcpy(n->tval, init_val, sizeof(double) * total);
  }
  return nid;
}

static int AutodiffForwardOp(AutodiffTape *t, int op, int lhs, int rhs, double aux) {
  double lv = (lhs >= 0 && lhs < t->count) ? t->nodes[lhs].val : 0.0;
  double rv = (rhs >= 0 && rhs < t->count) ? t->nodes[rhs].val : aux;
  double out = 0.0;
  switch (op) {
    case 1: /* CONST */ out = aux; break;
    case 2: /* ADD */ out = lv + rv; break;
    case 3: /* SUB */ out = lv - rv; break;
    case 4: /* MUL */ out = lv * rv; break;
    case 5: /* DIV */ out = (fabs(rv) < 1e-15) ? 0.0 : (lv / rv); break;
    case 6: /* NEG */ out = -lv; break;
    case 7: /* POW */ out = pow(lv, rv); break;
    case 8: /* EXP */ out = exp(lv); break;
    case 9: /* LOG */ out = (lv > 1e-15) ? log(lv) : log(1e-15); break;
    case 10: /* SIN */ out = sin(lv); break;
    case 11: /* COS */ out = cos(lv); break;
    case 12: /* TAN */ out = tan(lv); break;
    case 13: /* RELU */ out = (lv > 0.0) ? lv : 0.0; break;
    case 14: /* SIGMOID */ out = 1.0 / (1.0 + exp(-lv)); break;
    case 15: /* TANH */ out = tanh(lv); break;
    case 16: /* SQRT */ out = (lv >= 0.0) ? sqrt(lv) : 0.0; break;
    default: out = lv; break;
  }
  return AutodiffAllocNode(t, op, out, lhs, rhs, aux);
}

static void AutodiffBackward(AutodiffTape *t, int loss_nid) {
  if (!t || loss_nid < 0 || loss_nid >= t->count) return;
  for (int i = 0; i < t->count; i++) {
    t->nodes[i].grad = 0.0;
    if (t->nodes[i].is_tensor && t->nodes[i].tgrad) {
      memset(t->nodes[i].tgrad, 0, sizeof(double) * t->nodes[i].rows * t->nodes[i].cols);
    }
  }
  if (t->nodes[loss_nid].is_tensor && t->nodes[loss_nid].tgrad) {
    int total = t->nodes[loss_nid].rows * t->nodes[loss_nid].cols;
    for (int k = 0; k < total; k++) {
      t->nodes[loss_nid].tgrad[k] = 1.0;
    }
  } else {
    t->nodes[loss_nid].grad = 1.0;
  }
  for (int i = loss_nid; i >= 0; i--) {
    if (t->nodes[i].is_tensor) {
      int lhs = t->nodes[i].lhs;
      int rhs = t->nodes[i].rhs;
      int rows = t->nodes[i].rows;
      int cols = t->nodes[i].cols;
      int total = rows * cols;
      switch (t->nodes[i].op) {
        case 17: /* TENSOR_VAR */
          break;
        case 19: /* TENSOR_ADD */ {
          int lhs_bcast = (lhs >= 0 && rhs >= 0 && t->nodes[lhs].cols == t->nodes[rhs].cols && t->nodes[rhs].rows == 1 && t->nodes[lhs].rows > 1);
          for (int k = 0; k < total; k++) {
            double gk = t->nodes[i].tgrad[k];
            if (lhs >= 0 && t->nodes[lhs].tgrad) t->nodes[lhs].tgrad[k] += gk;
            if (rhs >= 0 && t->nodes[rhs].tgrad) {
              if (lhs_bcast) {
                int c = k % cols;
                t->nodes[rhs].tgrad[c] += gk;
              } else {
                t->nodes[rhs].tgrad[k] += gk;
              }
            }
          }
          break;
        }
        case 20: /* TENSOR_RELU */
          for (int k = 0; k < total; k++) {
            double gk = t->nodes[i].tgrad[k];
            if (lhs >= 0 && t->nodes[lhs].tgrad && t->nodes[lhs].tval) {
              if (t->nodes[lhs].tval[k] > 0.0) t->nodes[lhs].tgrad[k] += gk;
            }
          }
          break;
        case 21: /* TENSOR_MSE */ {
          if (lhs >= 0 && t->nodes[lhs].tgrad && rhs >= 0 && t->nodes[rhs].tval) {
            int n_elems = t->nodes[lhs].rows * t->nodes[lhs].cols;
            double scale = (n_elems > 0) ? (2.0 / (double)n_elems) : 1.0;
            double out_g = (t->nodes[i].tgrad) ? t->nodes[i].tgrad[0] : 1.0;
            for (int k = 0; k < n_elems; k++) {
              double diff = t->nodes[lhs].tval[k] - t->nodes[rhs].tval[k];
              t->nodes[lhs].tgrad[k] += out_g * scale * diff;
            }
          }
          break;
        }
        case 22: /* TENSOR_CROSS_ENTROPY */ {
          if (lhs >= 0 && t->nodes[lhs].tgrad && rhs >= 0 && t->nodes[rhs].tval) {
            int M = t->nodes[lhs].rows;
            int N = t->nodes[lhs].cols;
            double out_g = (t->nodes[i].tgrad) ? t->nodes[i].tgrad[0] : 1.0;
            for (int r = 0; r < M; r++) {
              double max_val = -1e30;
              for (int c = 0; c < N; c++) {
                if (t->nodes[lhs].tval[r * N + c] > max_val) max_val = t->nodes[lhs].tval[r * N + c];
              }
              double sum_exp = 0.0;
              for (int c = 0; c < N; c++) {
                sum_exp += exp(t->nodes[lhs].tval[r * N + c] - max_val);
              }
              for (int c = 0; c < N; c++) {
                double p = exp(t->nodes[lhs].tval[r * N + c] - max_val) / (sum_exp > 0.0 ? sum_exp : 1.0);
                double target = t->nodes[rhs].tval[r * N + c];
                t->nodes[lhs].tgrad[r * N + c] += out_g * (p - target) / (double)(M > 0 ? M : 1);
              }
            }
          }
          break;
        }
        case 18: /* TENSOR_MATMUL */ {
          int M = rows;
          int N = cols;
          int K = (lhs >= 0) ? t->nodes[lhs].cols : 0;
          if (lhs >= 0 && t->nodes[lhs].tgrad && rhs >= 0 && t->nodes[rhs].tval) {
            for (int r = 0; r < M; r++) {
              for (int k = 0; k < K; k++) {
                double sum = 0.0;
                for (int c = 0; c < N; c++) {
                  sum += t->nodes[i].tgrad[r * N + c] * t->nodes[rhs].tval[k * N + c];
                }
                t->nodes[lhs].tgrad[r * K + k] += sum;
              }
            }
          }
          if (rhs >= 0 && t->nodes[rhs].tgrad && lhs >= 0 && t->nodes[lhs].tval) {
            for (int k = 0; k < K; k++) {
              for (int c = 0; c < N; c++) {
                double sum = 0.0;
                for (int r = 0; r < M; r++) {
                  sum += t->nodes[lhs].tval[r * K + k] * t->nodes[i].tgrad[r * N + c];
                }
                t->nodes[rhs].tgrad[k * N + c] += sum;
              }
            }
          }
          break;
        }
        default:
          break;
      }
      continue;
    }
    double g = t->nodes[i].grad;
    if (g == 0.0) continue;
    int lhs = t->nodes[i].lhs;
    int rhs = t->nodes[i].rhs;
    double aux = t->nodes[i].aux;
    switch (t->nodes[i].op) {
      case 0: /* VAR */
      case 1: /* CONST */
        break;
      case 2: /* ADD */
        if (lhs >= 0) t->nodes[lhs].grad += g;
        if (rhs >= 0) t->nodes[rhs].grad += g;
        break;
      case 3: /* SUB */
        if (lhs >= 0) t->nodes[lhs].grad += g;
        if (rhs >= 0) t->nodes[rhs].grad -= g;
        break;
      case 4: /* MUL */
        if (lhs >= 0 && rhs >= 0) {
          t->nodes[lhs].grad += g * t->nodes[rhs].val;
          t->nodes[rhs].grad += g * t->nodes[lhs].val;
        } else if (lhs >= 0) {
          t->nodes[lhs].grad += g * aux;
        }
        break;
      case 5: /* DIV */
        if (lhs >= 0 && rhs >= 0) {
          double denom = t->nodes[rhs].val;
          if (fabs(denom) < 1e-15) denom = (denom < 0 ? -1e-15 : 1e-15);
          t->nodes[lhs].grad += g / denom;
          t->nodes[rhs].grad -= g * t->nodes[lhs].val / (denom * denom);
        } else if (lhs >= 0) {
          double denom = (fabs(aux) < 1e-15) ? (aux < 0 ? -1e-15 : 1e-15) : aux;
          t->nodes[lhs].grad += g / denom;
        }
        break;
      case 6: /* NEG */
        if (lhs >= 0) t->nodes[lhs].grad -= g;
        break;
      case 7: /* POW */ {
        double p = (rhs >= 0) ? t->nodes[rhs].val : aux;
        if (lhs >= 0) {
          double base = t->nodes[lhs].val;
          t->nodes[lhs].grad += g * p * pow(base, p - 1.0);
        }
        if (rhs >= 0) {
          double base = t->nodes[lhs].val;
          if (base > 0.0) {
            t->nodes[rhs].grad += g * pow(base, p) * log(base);
          }
        }
        break;
      }
      case 8: /* EXP */
        if (lhs >= 0) t->nodes[lhs].grad += g * exp(t->nodes[lhs].val);
        break;
      case 9: /* LOG */
        if (lhs >= 0) {
          double lv = t->nodes[lhs].val;
          if (fabs(lv) < 1e-15) lv = (lv < 0 ? -1e-15 : 1e-15);
          t->nodes[lhs].grad += g / lv;
        }
        break;
      case 10: /* SIN */
        if (lhs >= 0) t->nodes[lhs].grad += g * cos(t->nodes[lhs].val);
        break;
      case 11: /* COS */
        if (lhs >= 0) t->nodes[lhs].grad -= g * sin(t->nodes[lhs].val);
        break;
      case 12: /* TAN */
        if (lhs >= 0) {
          double c = cos(t->nodes[lhs].val);
          if (fabs(c) < 1e-15) c = (c < 0 ? -1e-15 : 1e-15);
          t->nodes[lhs].grad += g / (c * c);
        }
        break;
      case 13: /* RELU */
        if (lhs >= 0) t->nodes[lhs].grad += (t->nodes[lhs].val > 0.0 ? g : 0.0);
        break;
      case 14: /* SIGMOID */
        if (lhs >= 0) {
          double s = t->nodes[i].val;
          t->nodes[lhs].grad += g * s * (1.0 - s);
        }
        break;
      case 15: /* TANH */
        if (lhs >= 0) {
          double th = t->nodes[i].val;
          t->nodes[lhs].grad += g * (1.0 - th * th);
        }
        break;
      case 16: /* SQRT */
        if (lhs >= 0) {
          double sq = t->nodes[i].val;
          if (sq < 1e-15) sq = 1e-15;
          t->nodes[lhs].grad += g / (2.0 * sq);
        }
        break;
      default:
        break;
    }
  }
}

static AKSValue *AksStructFindField(AKSStruct *s, const char *name) {
  if (!s || !name) return NULL;
  for (int i = 0; i < s->fieldcount; i++) {
    if (s->fieldnames[i] && strcmp(s->fieldnames[i], name) == 0) {
      return &s->fieldvals[i];
    }
  }
  return NULL;
}

static int ExecuteStdlib(VM *vm, VMTask *task, const char *name, int argc,
                         AKSValue **stacktopptr) {
  AKSValue *stacktop = *stacktopptr;
  AKSValue *stacklimit = task->stack + MAXSTACK;

#define PUSH(val)                                                              \
  do {                                                                         \
    if (stacktop < stacklimit) {                                               \
      *stacktop++ = (val);                                                     \
    } else {                                                                   \
      fprintf(stderr, "Runtime error: stack overflow\n");                      \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
#define POP() StdlibPop(&stacktop)

  for (int i = 0; i < vm->registered_natives_count; i++) {
    if (strcmp(vm->registered_natives[i].name, name) == 0) {
      typedef AKSValue (*AksNativeFn)(void *vm, int argc, AKSValue *args);
      AksNativeFn func = (AksNativeFn)vm->registered_natives[i].func;
      AKSValue *args = malloc(sizeof(AKSValue) * (argc > 0 ? argc : 1));
      for (int j = argc - 1; j >= 0; j--) {
        args[j] = POP();
      }
      AKSValue res = func(vm, argc, args);
      for (int j = 0; j < argc; j++) {
        ValFree(&args[j]);
      }
      free(args);
      PUSH(res);
      *stacktopptr = stacktop;
      return 1;
    }
  }

  // Memory-Mapped Files (MMap)
  if (strcmp(name, "MMapOpen") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: MMapOpen() expects 3 arguments (path, mode, size)\n");
      return -1;
    }
    AKSValue size_val = POP();
    AKSValue mode_val = POP();
    AKSValue path_val = POP();

    int64_t size = ValToInt(size_val);
    if (size <= 0) size = 4096;

    void *pBuf = NULL;
    int is_anon = 1;
#ifdef _WIN32
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMap = NULL;
    if (ValKind(path_val) == VALSTR && ValToStrPtr(path_val)->len > 0) {
      const char *path = ValToStrPtr(path_val)->data;
      is_anon = 0;
      hFile = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hFile != INVALID_HANDLE_VALUE) {
        hMap = CreateFileMappingA(hFile, NULL, PAGE_READWRITE, (DWORD)(size >> 32), (DWORD)(size & 0xFFFFFFFF), NULL);
        if (hMap) {
          pBuf = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, (SIZE_T)size);
        }
      }
    } else {
      hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, (DWORD)(size >> 32), (DWORD)(size & 0xFFFFFFFF), NULL);
      if (hMap) {
        pBuf = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, (SIZE_T)size);
      }
    }
#else
    int fd = -1;
    if (ValKind(path_val) == VALSTR && ValToStrPtr(path_val)->len > 0) {
      const char *path = ValToStrPtr(path_val)->data;
      is_anon = 0;
      fd = open(path, O_RDWR | O_CREAT, 0666);
      if (fd >= 0) ftruncate(fd, size);
    }
    pBuf = mmap(NULL, size, PROT_READ | PROT_WRITE, (fd == -1 ? (MAP_ANONYMOUS | MAP_SHARED) : MAP_SHARED), fd, 0);
    if (pBuf == MAP_FAILED) pBuf = NULL;
#endif
    ValFree(&path_val);
    ValFree(&mode_val);
    ValFree(&size_val);

    if (!pBuf) {
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }

    AKSValue handle = ValStruct("MMapHandle");
    AKSStruct *s = ValToObj(handle);
    s->fieldcount = 5;
    s->fieldnames = malloc(5 * sizeof(char *));
    s->fieldvals = malloc(5 * sizeof(AKSValue));
    s->fieldnames[0] = strdup("ptr");
    s->fieldvals[0] = ValInt((int64_t)(uintptr_t)pBuf);
    s->fieldnames[1] = strdup("handle");
#ifdef _WIN32
    s->fieldvals[1] = ValInt((int64_t)(uintptr_t)hMap);
#else
    s->fieldvals[1] = ValInt((int64_t)fd);
#endif
    s->fieldnames[2] = strdup("file_handle");
#ifdef _WIN32
    s->fieldvals[2] = ValInt((int64_t)(uintptr_t)hFile);
#else
    s->fieldvals[2] = ValInt((int64_t)fd);
#endif
    s->fieldnames[3] = strdup("size");
    s->fieldvals[3] = ValInt(size);
    s->fieldnames[4] = strdup("is_anon");
    s->fieldvals[4] = ValBool(is_anon);

    PUSH(handle);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MMapRead") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: MMapRead() expects 3 arguments (handle, offset, length)\n");
      return -1;
    }
    AKSValue len_val = POP();
    AKSValue off_val = POP();
    AKSValue h_val = POP();
    int64_t offset = ValToInt(off_val);
    int64_t length = ValToInt(len_val);

    void *pBuf = NULL;
    int64_t max_size = 0;
    if (ValKind(h_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(h_val);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "ptr") == 0) pBuf = (void *)(uintptr_t)ValToInt(s->fieldvals[i]);
        if (strcmp(s->fieldnames[i], "size") == 0) max_size = ValToInt(s->fieldvals[i]);
      }
    }

    if (!pBuf || offset < 0 || offset >= max_size || length <= 0) {
      ValFree(&h_val);
      ValFree(&off_val);
      ValFree(&len_val);
      PUSH(ValStr(""));
      *stacktopptr = stacktop;
      return 1;
    }

    if (offset + length > max_size) length = max_size - offset;
    char *data_ptr = (char *)pBuf + offset;
    AKSValue str_res = ValStrLen(data_ptr, (int)length);

    ValFree(&h_val);
    ValFree(&off_val);
    ValFree(&len_val);
    PUSH(str_res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MMapWrite") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: MMapWrite() expects 3 arguments (handle, offset, data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    AKSValue off_val = POP();
    AKSValue h_val = POP();
    int64_t offset = ValToInt(off_val);

    void *pBuf = NULL;
    int64_t max_size = 0;
    if (ValKind(h_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(h_val);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "ptr") == 0) pBuf = (void *)(uintptr_t)ValToInt(s->fieldvals[i]);
        if (strcmp(s->fieldnames[i], "size") == 0) max_size = ValToInt(s->fieldvals[i]);
      }
    }

    int written = 0;
    if (pBuf && ValKind(data_val) == VALSTR && offset >= 0 && offset < max_size) {
      const char *data = ValToStrPtr(data_val)->data;
      int data_len = ValToStrPtr(data_val)->len;
      if (offset + data_len > max_size) data_len = (int)(max_size - offset);
      memcpy((char *)pBuf + offset, data, data_len);
      written = data_len;
    }

    ValFree(&h_val);
    ValFree(&off_val);
    ValFree(&data_val);
    PUSH(ValInt(written));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MMapFlush") == 0) {
    if (argc != 1 && argc != 3) {
      fprintf(stderr, "Runtime error: MMapFlush() expects 1 or 3 arguments\n");
      return -1;
    }
    int64_t length = 0;
    int64_t offset = 0;
    if (argc == 3) {
      AKSValue len_v = POP();
      AKSValue off_v = POP();
      length = ValToInt(len_v);
      offset = ValToInt(off_v);
      ValFree(&len_v);
      ValFree(&off_v);
    }
    AKSValue h_val = POP();
    void *pBuf = NULL;
    int64_t size = 0;
    if (ValKind(h_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(h_val);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "ptr") == 0) pBuf = (void *)(uintptr_t)ValToInt(s->fieldvals[i]);
        if (strcmp(s->fieldnames[i], "size") == 0) size = ValToInt(s->fieldvals[i]);
      }
    }
    if (length == 0) length = size;
#ifdef _WIN32
    if (pBuf) FlushViewOfFile((char *)pBuf + offset, (SIZE_T)length);
#else
    if (pBuf) msync((char *)pBuf + offset, (size_t)length, MS_SYNC);
#endif
    ValFree(&h_val);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MMapClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MMapClose() expects 1 argument\n");
      return -1;
    }
    AKSValue h_val = POP();
    if (ValKind(h_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(h_val);
      void *pBuf = NULL;
      int64_t size = 0;
#ifdef _WIN32
      HANDLE hMap = NULL, hFile = INVALID_HANDLE_VALUE;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "ptr") == 0) { pBuf = (void *)(uintptr_t)ValToInt(s->fieldvals[i]); s->fieldvals[i] = ValInt(0); }
        if (strcmp(s->fieldnames[i], "handle") == 0) { hMap = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]); s->fieldvals[i] = ValInt(0); }
        if (strcmp(s->fieldnames[i], "file_handle") == 0) { hFile = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]); s->fieldvals[i] = ValInt(0); }
      }
      if (pBuf) UnmapViewOfFile(pBuf);
      if (hMap) CloseHandle(hMap);
      if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
#else
      int fd = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "ptr") == 0) { pBuf = (void *)(uintptr_t)ValToInt(s->fieldvals[i]); s->fieldvals[i] = ValInt(0); }
        if (strcmp(s->fieldnames[i], "size") == 0) size = ValToInt(s->fieldvals[i]);
        if (strcmp(s->fieldnames[i], "handle") == 0) fd = (int)ValToInt(s->fieldvals[i]);
      }
      if (pBuf) munmap(pBuf, size);
      if (fd >= 0) close(fd);
#endif
    }
    ValFree(&h_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  // Bit Hardware Atomics
  if (strcmp(name, "AtomicNew") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: AtomicI64New() expects 1 argument\n");
      return -1;
    }
    AKSValue init_val = POP();
    int64_t *ptr = malloc(sizeof(int64_t));
    *ptr = ValToInt(init_val);
    ValFree(&init_val);
    PUSH(ValInt((int64_t)(uintptr_t)ptr));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "AtomicI64Free") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: AtomicI64Free() expects 1 argument\n");
      return -1;
    }
    AKSValue ptr_val = POP();
    void *ptr = (void *)(uintptr_t)ValToInt(ptr_val);
    if (ptr) free(ptr);
    ValFree(&ptr_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "AtomicI64Load") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: AtomicI64Load() expects 1 argument\n");
      return -1;
    }
    AKSValue ptr_val = POP();
    volatile int64_t *ptr = (volatile int64_t *)(uintptr_t)ValToInt(ptr_val);
    int64_t val = 0;
#ifdef _WIN32
    val = _InterlockedCompareExchange64((volatile LONG64*)ptr, 0, 0);
#else
    val = __sync_add_and_fetch(ptr, 0);
#endif
    ValFree(&ptr_val);
    PUSH(ValInt(val));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "AtomicI64Store") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: AtomicI64Store() expects 2 arguments\n");
      return -1;
    }
    AKSValue val_to_store = POP();
    AKSValue ptr_val = POP();
    volatile int64_t *ptr = (volatile int64_t *)(uintptr_t)ValToInt(ptr_val);
    int64_t val = ValToInt(val_to_store);
#ifdef _WIN32
    _InterlockedExchange64((volatile LONG64*)ptr, val);
#else
    __sync_lock_test_and_set(ptr, val);
#endif
    ValFree(&val_to_store);
    ValFree(&ptr_val);
    PUSH(ValInt(val));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "AtomicI64Add") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: AtomicI64Add() expects 2 arguments\n");
      return -1;
    }
    AKSValue delta_val = POP();
    AKSValue ptr_val = POP();
    volatile int64_t *ptr = (volatile int64_t *)(uintptr_t)ValToInt(ptr_val);
    int64_t delta = ValToInt(delta_val);
    int64_t old_v = 0;
#ifdef _WIN32
    old_v = _InterlockedExchangeAdd64((volatile LONG64*)ptr, delta);
#else
    old_v = __sync_fetch_and_add(ptr, delta);
#endif
    ValFree(&delta_val);
    ValFree(&ptr_val);
    PUSH(ValInt(old_v + delta));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "AtomicI64Sub") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: AtomicI64Sub() expects 2 arguments\n");
      return -1;
    }
    AKSValue delta_val = POP();
    AKSValue ptr_val = POP();
    volatile int64_t *ptr = (volatile int64_t *)(uintptr_t)ValToInt(ptr_val);
    int64_t delta = ValToInt(delta_val);
    int64_t old_v = 0;
#ifdef _WIN32
    old_v = _InterlockedExchangeAdd64((volatile LONG64*)ptr, -delta);
#else
    old_v = __sync_fetch_and_sub(ptr, delta);
#endif
    ValFree(&delta_val);
    ValFree(&ptr_val);
    PUSH(ValInt(old_v - delta));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "AtomicI64Cas") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: AtomicI64Cas() expects 3 arguments (ptr, expected, desired)\n");
      return -1;
    }
    AKSValue des_val = POP();
    AKSValue exp_val = POP();
    AKSValue ptr_val = POP();
    volatile int64_t *ptr = (volatile int64_t *)(uintptr_t)ValToInt(ptr_val);
    int64_t expected = ValToInt(exp_val);
    int64_t desired = ValToInt(des_val);
    int success = 0;
#ifdef _WIN32
    int64_t prev = _InterlockedCompareExchange64((volatile LONG64*)ptr, desired, expected);
    success = (prev == expected);
#else
    success = __sync_bool_compare_and_swap(ptr, expected, desired);
#endif
    ValFree(&des_val);
    ValFree(&exp_val);
    ValFree(&ptr_val);
    PUSH(ValBool(success));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "AtomicI64Exchange") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: AtomicI64Exchange() expects 2 arguments\n");
      return -1;
    }
    AKSValue new_val = POP();
    AKSValue ptr_val = POP();
    volatile int64_t *ptr = (volatile int64_t *)(uintptr_t)ValToInt(ptr_val);
    int64_t val = ValToInt(new_val);
    int64_t old_v = 0;
#ifdef _WIN32
    old_v = _InterlockedExchange64((volatile LONG64*)ptr, val);
#else
    old_v = __sync_lock_test_and_set(ptr, val);
#endif
    ValFree(&new_val);
    ValFree(&ptr_val);
    PUSH(ValInt(old_v));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "AtomicFence") == 0) {
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // Subprocess Management and OS Anonymous Pipes
  if (strcmp(name, "ProcessSpawn") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessSpawn() expects 1 argument\n");
      return -1;
    }
    AKSValue cmd_val = POP();
    if (ValKind(cmd_val) != VALSTR) {
      fprintf(stderr, "Runtime error: ProcessSpawn() expects string command\n");
      ValFree(&cmd_val);
      return -1;
    }
    const char *cmd = ValToStrPtr(cmd_val)->data;
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hStdinRead = NULL, hStdinWrite = NULL;
    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
    HANDLE hStderrRead = NULL, hStderrWrite = NULL;

    CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0);
    SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);

    CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0);
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0);
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdInput = hStdinRead;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStderrWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    char *cmd_buf = strdup(cmd);
    BOOL success = CreateProcessA(NULL, cmd_buf, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    free(cmd_buf);

    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);

    if (!success) {
      CloseHandle(hStdinWrite);
      CloseHandle(hStdoutRead);
      CloseHandle(hStderrRead);
      ValFree(&cmd_val);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }

    AKSValue proc_val = ValStruct("ProcessHandle");
    AKSStruct *s = ValToObj(proc_val);
    s->fieldcount = 7;
    s->fieldnames = malloc(7 * sizeof(char *));
    s->fieldvals = malloc(7 * sizeof(AKSValue));
    s->fieldnames[0] = strdup("pid");
    s->fieldvals[0] = ValInt((int64_t)pi.dwProcessId);
    s->fieldnames[1] = strdup("h_proc");
    s->fieldvals[1] = ValInt((int64_t)(uintptr_t)pi.hProcess);
    s->fieldnames[2] = strdup("h_thread");
    s->fieldvals[2] = ValInt((int64_t)(uintptr_t)pi.hThread);
    s->fieldnames[3] = strdup("stdin_write");
    s->fieldvals[3] = ValInt((int64_t)(uintptr_t)hStdinWrite);
    s->fieldnames[4] = strdup("stdout_read");
    s->fieldvals[4] = ValInt((int64_t)(uintptr_t)hStdoutRead);
    s->fieldnames[5] = strdup("stderr_read");
    s->fieldvals[5] = ValInt((int64_t)(uintptr_t)hStderrRead);
    s->fieldnames[6] = strdup("is_alive");
    s->fieldvals[6] = ValBool(1);

    ValFree(&cmd_val);
    PUSH(proc_val);
    *stacktopptr = stacktop;
    return 1;
#else
    ValFree(&cmd_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
#endif
  }

  if (strcmp(name, "ProcessWriteStdin") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ProcessWriteStdin() expects 2 arguments\n");
      return -1;
    }
    AKSValue data_val = POP();
    AKSValue proc_val = POP();
    DWORD written = 0;
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT && ValKind(data_val) == VALSTR) {
      AKSStruct *s = ValToObj(proc_val);
      HANDLE hWrite = NULL;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "stdin_write") == 0) {
          hWrite = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (hWrite) {
        const char *data = ValToStrPtr(data_val)->data;
        DWORD len = (DWORD)ValToStrPtr(data_val)->len;
        WriteFile(hWrite, data, len, &written, NULL);
      }
    }
#endif
    ValFree(&data_val);
    ValFree(&proc_val);
    PUSH(ValInt((int64_t)written));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessReadStdout") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ProcessReadStdout() expects 2 arguments\n");
      return -1;
    }
    AKSValue max_val = POP();
    AKSValue proc_val = POP();
    int64_t max_bytes = ValToInt(max_val);
    if (max_bytes <= 0) max_bytes = 4096;
    if (max_bytes > 1048576) max_bytes = 1048576;

    char *buf = malloc((size_t)max_bytes + 1);
    DWORD bytes_read = 0;
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      HANDLE hRead = NULL;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "stdout_read") == 0) {
          hRead = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (hRead) {
        DWORD avail = 0;
        if (PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
          DWORD to_read = (DWORD)max_bytes;
          if (to_read > avail) to_read = avail;
          ReadFile(hRead, buf, to_read, &bytes_read, NULL);
        }
      }
    }
#endif
    buf[bytes_read] = '\0';
    AKSValue str_res = ValStrLen(buf, (int)bytes_read);
    free(buf);
    ValFree(&max_val);
    ValFree(&proc_val);
    PUSH(str_res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessReadStderr") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ProcessReadStderr() expects 2 arguments\n");
      return -1;
    }
    AKSValue max_val = POP();
    AKSValue proc_val = POP();
    int64_t max_bytes = ValToInt(max_val);
    if (max_bytes <= 0) max_bytes = 4096;
    if (max_bytes > 1048576) max_bytes = 1048576;

    char *buf = malloc((size_t)max_bytes + 1);
    DWORD bytes_read = 0;
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      HANDLE hRead = NULL;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "stderr_read") == 0) {
          hRead = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (hRead) {
        DWORD avail = 0;
        if (PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
          DWORD to_read = (DWORD)max_bytes;
          if (to_read > avail) to_read = avail;
          ReadFile(hRead, buf, to_read, &bytes_read, NULL);
        }
      }
    }
#endif
    buf[bytes_read] = '\0';
    AKSValue str_res = ValStrLen(buf, (int)bytes_read);
    free(buf);
    ValFree(&max_val);
    ValFree(&proc_val);
    PUSH(str_res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessWait") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ProcessWait() expects 2 arguments\n");
      return -1;
    }
    AKSValue timeout_val = POP();
    AKSValue proc_val = POP();
    int64_t timeout_ms = ValToInt(timeout_val);
    int ok = 0;
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      HANDLE hProc = NULL;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "h_proc") == 0) {
          hProc = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (hProc) {
        DWORD res = WaitForSingleObject(hProc, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
        ok = (res == WAIT_OBJECT_0);
      }
    }
#endif
    ValFree(&timeout_val);
    ValFree(&proc_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessKill") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessKill() expects 1 argument\n");
      return -1;
    }
    AKSValue proc_val = POP();
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      HANDLE hProc = NULL;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "h_proc") == 0) {
          hProc = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (hProc) {
        TerminateProcess(hProc, 1);
      }
    }
#endif
    ValFree(&proc_val);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessExitCode") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessExitCode() expects 1 argument\n");
      return -1;
    }
    AKSValue proc_val = POP();
    int64_t code = -1;
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      HANDLE hProc = NULL;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "h_proc") == 0) {
          hProc = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (hProc) {
        DWORD dwCode = 0;
        if (GetExitCodeProcess(hProc, &dwCode)) {
          code = (int64_t)dwCode;
        }
      }
    }
#endif
    ValFree(&proc_val);
    PUSH(ValInt(code));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessClose() expects 1 argument\n");
      return -1;
    }
    AKSValue proc_val = POP();
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "h_proc") == 0 ||
            strcmp(s->fieldnames[i], "h_thread") == 0 ||
            strcmp(s->fieldnames[i], "stdin_write") == 0 ||
            strcmp(s->fieldnames[i], "stdout_read") == 0 ||
            strcmp(s->fieldnames[i], "stderr_read") == 0) {
          HANDLE h = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          if (h && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            s->fieldvals[i] = ValInt(0);
          }
        }
      }
    }
#endif
    ValFree(&proc_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  // System and Hardware Telemetry
  if (strcmp(name, "SystemCpuCount") == 0) {
    int count = 1;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    count = (int)si.dwNumberOfProcessors;
#else
    count = (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (count <= 0) count = 1;
    PUSH(ValInt(count));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemMemoryTotal") == 0) {
    int64_t total = 0;
#ifdef _WIN32
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
      total = (int64_t)ms.ullTotalPhys;
    }
#endif
    PUSH(ValInt(total));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemMemoryAvailable") == 0) {
    int64_t avail = 0;
#ifdef _WIN32
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
      avail = (int64_t)ms.ullAvailPhys;
    }
#endif
    PUSH(ValInt(avail));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemPid") == 0) {
    int64_t pid = 0;
#ifdef _WIN32
    pid = (int64_t)GetCurrentProcessId();
#else
    pid = (int64_t)getpid();
#endif
    PUSH(ValInt(pid));
    *stacktopptr = stacktop;
    return 1;
  }

  // Dynamic c FFI
  if (strcmp(name, "NativeLibraryLoad") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: NativeLibraryLoad() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int64_t handle = 0;
    if (ValKind(path_val) == VALSTR) {
      const char *path = ValToStrPtr(path_val)->data;
#ifdef _WIN32
      HMODULE h = LoadLibraryA(path);
      handle = (int64_t)(uintptr_t)h;
#else
      void *h = dlopen(path, RTLD_LAZY);
      handle = (int64_t)(uintptr_t)h;
#endif
    }
    ValFree(&path_val);
    PUSH(ValInt(handle));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NativeLibraryGet") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: NativeLibraryGet() expects 2 arguments (lib_handle, symbol)\n");
      return -1;
    }
    AKSValue sym_val = POP();
    AKSValue lib_val = POP();
    int64_t proc = 0;
    if (ValKind(sym_val) == VALSTR) {
      const char *sym = ValToStrPtr(sym_val)->data;
      intptr_t lib = (intptr_t)ValToInt(lib_val);
#ifdef _WIN32
      FARPROC p = GetProcAddress((HMODULE)lib, sym);
      proc = (int64_t)(uintptr_t)p;
#else
      void *p = dlsym((void *)lib, sym);
      proc = (int64_t)(uintptr_t)p;
#endif
    }
    ValFree(&sym_val);
    ValFree(&lib_val);
    PUSH(ValInt(proc));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NativeCallInt0") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: NativeCallInt0() expects 1 argument (fn_ptr)\n");
      return -1;
    }
    AKSValue fn_val = POP();
    typedef int64_t (*Fn0)();
    Fn0 fn = (Fn0)(uintptr_t)ValToInt(fn_val);
    int64_t res = 0;
    if (fn) {
      res = fn();
    }
    ValFree(&fn_val);
    PUSH(ValInt(res));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NativeCallInt1") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: NativeCallInt1() expects 2 arguments (fn_ptr, arg1)\n");
      return -1;
    }
    AKSValue a1_val = POP();
    AKSValue fn_val = POP();
    typedef int64_t (*Fn1)(int64_t);
    Fn1 fn = (Fn1)(uintptr_t)ValToInt(fn_val);
    int64_t res = 0;
    if (fn) {
      res = fn(ValToInt(a1_val));
    }
    ValFree(&a1_val);
    ValFree(&fn_val);
    PUSH(ValInt(res));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NativeCallInt2") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: NativeCallInt2() expects 3 arguments (fn_ptr, arg1, arg2)\n");
      return -1;
    }
    AKSValue a2_val = POP();
    AKSValue a1_val = POP();
    AKSValue fn_val = POP();
    typedef int64_t (*Fn2)(int64_t, int64_t);
    Fn2 fn = (Fn2)(uintptr_t)ValToInt(fn_val);
    int64_t res = 0;
    if (fn) {
      res = fn(ValToInt(a1_val), ValToInt(a2_val));
    }
    ValFree(&a2_val);
    ValFree(&a1_val);
    ValFree(&fn_val);
    PUSH(ValInt(res));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NativeLibraryFree") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: NativeLibraryFree() expects 1 argument (lib_handle)\n");
      return -1;
    }
    AKSValue lib_val = POP();
    intptr_t lib = (intptr_t)ValToInt(lib_val);
#ifdef _WIN32
    if (lib) FreeLibrary((HMODULE)lib);
#else
    if (lib) dlclose((void *)lib);
#endif
    ValFree(&lib_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  // High-Resolution Performance Timers
  if (strcmp(name, "TimerNowNanos") == 0) {
    int64_t nanos = 0;
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    if (freq.QuadPart > 0) {
      nanos = (int64_t)(((double)cnt.QuadPart / (double)freq.QuadPart) * 1000000000.0);
    }
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    nanos = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
    PUSH(ValInt(nanos));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "TimerNowMicros") == 0) {
    int64_t micros = 0;
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    if (freq.QuadPart > 0) {
      micros = (int64_t)(((double)cnt.QuadPart / (double)freq.QuadPart) * 1000000.0);
    }
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    micros = (int64_t)ts.tv_sec * 1000000LL + (ts.tv_nsec / 1000);
#endif
    PUSH(ValInt(micros));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "TimerNowMillis") == 0) {
    int64_t millis = 0;
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    if (freq.QuadPart > 0) {
      millis = (int64_t)(((double)cnt.QuadPart / (double)freq.QuadPart) * 1000.0);
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

  // Tcp/ip sockets networking
  if (strcmp(name, "SocketListenSync") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SocketListenSync() expects 1 argument (port)\n");
      return -1;
    }
    AKSValue port_val = POP();
    int port = (int)ValToInt(port_val);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET) {
      int opt = 1;
      setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
      struct sockaddr_in sin;
      sin.sin_family = AF_INET;
      sin.sin_addr.s_addr = INADDR_ANY;
      sin.sin_port = htons((u_short)port);
      if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
        listen(s, 16);
      } else {
#ifdef _WIN32
        closesocket(s);
#else
        close((int)s);
#endif
        s = INVALID_SOCKET;
      }
    }
    ValFree(&port_val);
    PUSH(ValInt((int64_t)s));
    *stacktopptr = stacktop;
    return 1;
  }


  // Stealing Deque (Chase-Lev Work-Stealing)
  if (strcmp(name, "StealingDequeNewC") == 0) {
    AKSValue root = ValStruct("StealingDeque");
    AKSStruct *s = ValToObj(root);
    s->fieldnames = malloc(sizeof(char*) * 3);
    s->fieldvals = malloc(sizeof(AKSValue) * 3);
    s->fieldnames[0] = strdup("items"); s->fieldvals[0] = ValArr(64);
    s->fieldnames[1] = strdup("top"); s->fieldvals[1] = ValInt(0);
    s->fieldnames[2] = strdup("bottom"); s->fieldvals[2] = ValInt(0);
    s->fieldcount = 3;
    PUSH(root); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "StealingDequePushC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: StealingDequePushC() expects 2 args\n"); return -1; }
    AKSValue item = POP(), dq_v = POP();
    if (ValKind(dq_v) == VALSTRUCT) {
      AKSStruct *s = ValToObj(dq_v);
      AKSArray *arr = ValToArr(s->fieldvals[0]);
      int64_t b = ValToInt(s->fieldvals[2]);
      if (b < (int64_t)arr->count) { arr->items[b] = item; }
      else { AksArrPush(arr, item); }
      s->fieldvals[2] = ValInt(b + 1);
    }
    ValFree(&dq_v); PUSH(ValNull()); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "StealingDequePopC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: StealingDequePopC() expects 1 arg\n"); return -1; }
    AKSValue dq_v = POP();
    AKSValue res = ValNull();
    if (ValKind(dq_v) == VALSTRUCT) {
      AKSStruct *s = ValToObj(dq_v);
      AKSArray *arr = ValToArr(s->fieldvals[0]);
      int64_t b = ValToInt(s->fieldvals[2]) - 1;
      s->fieldvals[2] = ValInt(b >= 0 ? b : 0);
      int64_t t = ValToInt(s->fieldvals[1]);
      if (b >= t && b < (int64_t)arr->count) {
        res = arr->items[b];
      }
    }
    ValFree(&dq_v); PUSH(res); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "StealingDequeStealC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: StealingDequeStealC() expects 1 arg\n"); return -1; }
    AKSValue dq_v = POP();
    AKSValue res = ValNull();
    if (ValKind(dq_v) == VALSTRUCT) {
      AKSStruct *s = ValToObj(dq_v);
      AKSArray *arr = ValToArr(s->fieldvals[0]);
      int64_t t = ValToInt(s->fieldvals[1]);
      int64_t b = ValToInt(s->fieldvals[2]);
      if (t < b && t < (int64_t)arr->count) {
        res = arr->items[t];
        s->fieldvals[1] = ValInt(t + 1);
      }
    }
    ValFree(&dq_v); PUSH(res); *stacktopptr = stacktop; return 1;
  }

  // Hazard Pointer
  if (strcmp(name, "HazardPointerNewC") == 0) {
    AKSValue hp_v = ValStruct("HazardPointer");
    AKSStruct *s = ValToObj(hp_v);
    s->fieldnames = malloc(sizeof(char*) * 2);
    s->fieldvals = malloc(sizeof(AKSValue) * 2);
    s->fieldnames[0] = strdup("pointer"); s->fieldvals[0] = ValInt(0);
    s->fieldnames[1] = strdup("active"); s->fieldvals[1] = ValBool(0);
    s->fieldcount = 2;
    PUSH(hp_v); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "HazardPointerAcquireC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: HazardPointerAcquireC() expects 2 args\n"); return -1; }
    AKSValue ptr_v = POP(), hp_v = POP();
    if (ValKind(hp_v) == VALSTRUCT) {
      AKSStruct *s = ValToObj(hp_v);
      s->fieldvals[0] = ptr_v;
      s->fieldvals[1] = ValBool(1);
    }
    ValFree(&hp_v); PUSH(ValNull()); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "HazardPointerReleaseC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: HazardPointerReleaseC() expects 1 arg\n"); return -1; }
    AKSValue hp_v = POP();
    if (ValKind(hp_v) == VALSTRUCT) {
      AKSStruct *s = ValToObj(hp_v);
      s->fieldvals[0] = ValInt(0);
      s->fieldvals[1] = ValBool(0);
    }
    ValFree(&hp_v); PUSH(ValNull()); *stacktopptr = stacktop; return 1;
  }

  // HAMT (Hash Array Mapped Trie)
  if (strcmp(name, "HAMTNewC") == 0) {
    AKSValue hm_v = ValStruct("HAMT");
    AKSStruct *s = ValToObj(hm_v);
    s->fieldnames = NULL; s->fieldvals = NULL; s->fieldcount = 0;
    PUSH(hm_v); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "HAMTInsertC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: HAMTInsertC() expects 3 args\n"); return -1; }
    AKSValue val = POP(), key = POP(), hm_v = POP();
    if (ValKind(hm_v) == VALSTRUCT && ValKind(key) == VALSTR) {
      AKSStruct *s = ValToObj(hm_v);
      const char *kstr = ValToStrPtr(key)->data;
      int found = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], kstr) == 0) { found = i; break; }
      }
      if (found >= 0) {
        ValFree(&s->fieldvals[found]);
        s->fieldvals[found] = val;
      } else {
        s->fieldnames = realloc(s->fieldnames, (s->fieldcount + 1) * sizeof(char*));
        s->fieldvals = realloc(s->fieldvals, (s->fieldcount + 1) * sizeof(AKSValue));
        s->fieldnames[s->fieldcount] = strdup(kstr);
        s->fieldvals[s->fieldcount] = val;
        s->fieldcount++;
      }
    }
    ValFree(&key); ValFree(&hm_v); PUSH(ValNull()); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "HAMTGetC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: HAMTGetC() expects 2 args\n"); return -1; }
    AKSValue key = POP(), hm_v = POP();
    AKSValue res = ValNull();
    if (ValKind(hm_v) == VALSTRUCT && ValKind(key) == VALSTR) {
      AKSStruct *s = ValToObj(hm_v);
      const char *kstr = ValToStrPtr(key)->data;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], kstr) == 0) { res = s->fieldvals[i]; break; }
      }
    }
    ValFree(&key); ValFree(&hm_v); PUSH(res); *stacktopptr = stacktop; return 1;
  }

  // Wavelet Tree
  if (strcmp(name, "WaveletNewC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: WaveletNewC() expects 1 arg\n"); return -1; }
    AKSValue arr_v = POP();
    AKSValue wt_v = ValStruct("WaveletTree");
    AKSStruct *s = ValToObj(wt_v);
    s->fieldnames = malloc(sizeof(char*));
    s->fieldvals = malloc(sizeof(AKSValue));
    s->fieldnames[0] = strdup("data"); s->fieldvals[0] = arr_v;
    s->fieldcount = 1;
    PUSH(wt_v); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "WaveletRankC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: WaveletRankC() expects 3 args (wt, val, prefix_len)\n"); return -1; }
    AKSValue len_v = POP(), target_v = POP(), wt_v = POP();
    int count = 0;
    if (ValKind(wt_v) == VALSTRUCT) {
      AKSStruct *s = ValToObj(wt_v);
      if (s->fieldcount > 0 && ValKind(s->fieldvals[0]) == VALARR) {
        AKSArray *arr = ValToArr(s->fieldvals[0]);
        int64_t limit = ValToInt(len_v);
        int64_t target = ValToInt(target_v);
        if (limit > (int64_t)arr->count) limit = arr->count;
        for (int i = 0; i < limit; i++) {
          if (ValToInt(arr->items[i]) == target) count++;
        }
      }
    }
    ValFree(&len_v); ValFree(&target_v); ValFree(&wt_v);
    PUSH(ValInt(count)); *stacktopptr = stacktop; return 1;
  }

  // Tar Archive Reader
  if (strcmp(name, "TarExtractListC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: TarExtractListC() expects 1 arg (tar_path)\n"); return -1; }
    AKSValue path_v = POP();
    AKSValue res_arr = ValArr(0);
    if (ValKind(path_v) == VALSTR) {
      FILE *fp = fopen(ValToStrPtr(path_v)->data, "rb");
      if (fp) {
        char header[512];
        AKSArray *ra = ValToArr(res_arr);
        while (fread(header, 1, 512, fp) == 512) {
          if (header[0] == 0) break;
          char name_buf[101];
          memcpy(name_buf, header, 100);
          name_buf[100] = 0;
          AksArrPush(ra, ValStr(name_buf));
          char size_buf[13];
          memcpy(size_buf, header + 124, 12);
          size_buf[12] = 0;
          long size = strtol(size_buf, NULL, 8);
          long blocks = (size + 511) / 512;
          fseek(fp, blocks * 512, SEEK_CUR);
        }
        fclose(fp);
      }
    }
    ValFree(&path_v); PUSH(res_arr); *stacktopptr = stacktop; return 1;
  }

  // WinMM Audio Playback
  if (strcmp(name, "WinmmAudioPlayC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: WinmmAudioPlayC() expects 2 args (pcm_bytes, sample_rate)\n"); return -1; }
    AKSValue rate_v = POP(), pcm_v = POP();
    int play_ok = 1;
#ifdef _WIN32
    if (ValKind(pcm_v) == VALSTR) {
      WAVEFORMATEX wfx = {0};
      wfx.wFormatTag = WAVE_FORMAT_PCM;
      wfx.nChannels = 1;
      wfx.nSamplesPerSec = (DWORD)ValToInt(rate_v);
      if (wfx.nSamplesPerSec == 0) wfx.nSamplesPerSec = 44100;
      wfx.wBitsPerSample = 16;
      wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
      wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
      HWAVEOUT hWaveOut = NULL;
      if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
        WAVEHDR header = {0};
        header.lpData = (LPSTR)ValToStrPtr(pcm_v)->data;
        header.dwBufferLength = (DWORD)ValToStrPtr(pcm_v)->len;
        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutReset(hWaveOut);
        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);
      }
    }
#endif
    ValFree(&rate_v); ValFree(&pcm_v);
    PUSH(ValBool(play_ok)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "SocketAcceptSync") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SocketAcceptSync() expects 1 argument (server_sock)\n");
      return -1;
    }
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    SOCKET client = accept(s, NULL, NULL);
    ValFree(&s_val);
    PUSH(ValInt((int64_t)client));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SocketConnectSync") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SocketConnectSync() expects 2 arguments (host, port)\n");
      return -1;
    }
    AKSValue port_val = POP();
    AKSValue host_val = POP();
    int port = (int)ValToInt(port_val);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET && ValKind(host_val) == VALSTR) {
      struct sockaddr_in sin;
      sin.sin_family = AF_INET;
      sin.sin_port = htons((u_short)port);
      sin.sin_addr.s_addr = inet_addr(ValToStrPtr(host_val)->data);
      if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        close((int)s);
#endif
        s = INVALID_SOCKET;
      }
    }
    ValFree(&host_val);
    ValFree(&port_val);
    PUSH(ValInt((int64_t)s));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SocketSendSync") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SocketSendSync() expects 2 arguments (sock, data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    int sent = 0;
    if (s != INVALID_SOCKET && ValKind(data_val) == VALSTR) {
      const char *data = ValToStrPtr(data_val)->data;
      int len = ValToStrPtr(data_val)->len;
      sent = send(s, data, len, 0);
    }
    ValFree(&data_val);
    ValFree(&s_val);
    PUSH(ValInt(sent));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SocketRecvSync") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SocketRecvSync() expects 2 arguments (sock, max_bytes)\n");
      return -1;
    }
    AKSValue max_val = POP();
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    int max_bytes = (int)ValToInt(max_val);
    if (max_bytes <= 0) max_bytes = 4096;
    if (max_bytes > 1048576) max_bytes = 1048576;

    char *buf = malloc((size_t)max_bytes + 1);
    int recvd = 0;
    if (s != INVALID_SOCKET) {
      recvd = recv(s, buf, max_bytes, 0);
      if (recvd < 0) recvd = 0;
    }
    buf[recvd] = '\0';
    AKSValue str_res = ValStrLen(buf, recvd);
    free(buf);
    ValFree(&max_val);
    ValFree(&s_val);
    PUSH(str_res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SocketClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SocketClose() expects 1 argument (sock)\n");
      return -1;
    }
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    if (s != INVALID_SOCKET) {
#ifdef _WIN32
      closesocket(s);
#else
      close((int)s);
#endif
    }
    ValFree(&s_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  // Cryptographic Randomness (BCrypt / /dev/urandom)
  if (strcmp(name, "SecureRandomBytes") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SecureRandomBytes() expects 1 argument (length)\n");
      return -1;
    }
    AKSValue len_val = POP();
    int count = (int)ValToInt(len_val);
    if (count <= 0) count = 16;
    if (count > 65536) count = 65536;
    uint8_t *buf = (uint8_t *)malloc((size_t)count);
#ifdef _WIN32
    BCryptGenRandom(NULL, buf, (ULONG)count, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { fread(buf, 1, count, f); fclose(f); }
#endif
    char *hex = (char *)malloc((size_t)count * 2 + 1);
    for (int i = 0; i < count; i++) {
      sprintf(hex + i * 2, "%02x", buf[i]);
    }
    hex[count * 2] = '\0';
    AKSValue res = ValStrLen(hex, count * 2);
    free(hex);
    free(buf);
    ValFree(&len_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SecureRandomI64") == 0) {
    int64_t val = 0;
#ifdef _WIN32
    BCryptGenRandom(NULL, (PUCHAR)&val, sizeof(val), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { fread(&val, 1, sizeof(val), f); fclose(f); }
#endif
    PUSH(ValInt(val));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SecureRandomUuid") == 0) {
    uint8_t b[16];
#ifdef _WIN32
    BCryptGenRandom(NULL, b, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { fread(b, 1, 16, f); fclose(f); }
#endif
    b[6] = (b[6] & 0x0f) | 0x40; // Version 4
    b[8] = (b[8] & 0x3f) | 0x80; // Variant rfc 4122
    char uuid[37];
    sprintf(uuid, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
            b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    PUSH(ValStr(uuid));
    *stacktopptr = stacktop;
    return 1;
  }

  // Directory Change Notifications (FindFirstChangeNotification)
  if (strcmp(name, "FileWatcherNew") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileWatcherNew() expects 1 argument (dir_path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int64_t handle = 0;
    if (ValKind(path_val) == VALSTR) {
      const char *path = ValToStrPtr(path_val)->data;
#ifdef _WIN32
      HANDLE hChange = FindFirstChangeNotificationA(
          path,
          FALSE,
          FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
      handle = (int64_t)(uintptr_t)hChange;
#else
      handle = 1;
#endif
    }
    ValFree(&path_val);
    PUSH(ValInt(handle));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FileWatcherPoll") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FileWatcherPoll() expects 2 arguments (handle, timeout_ms)\n");
      return -1;
    }
    AKSValue to_val = POP();
    AKSValue h_val = POP();
    int changed = 0;
    int timeout_ms = (int)ValToInt(to_val);
    if (timeout_ms < 0) timeout_ms = 0;
#ifdef _WIN32
    HANDLE hChange = (HANDLE)(uintptr_t)ValToInt(h_val);
    if (hChange != INVALID_HANDLE_VALUE && hChange != NULL) {
      DWORD waitRes = WaitForSingleObject(hChange, (DWORD)timeout_ms);
      if (waitRes == WAIT_OBJECT_0) {
        FindNextChangeNotification(hChange);
        changed = 1;
      }
    }
#else
    changed = 1;
#endif
    ValFree(&to_val);
    ValFree(&h_val);
    PUSH(ValBool(changed));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FileWatcherClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileWatcherClose() expects 1 argument (handle)\n");
      return -1;
    }
    AKSValue h_val = POP();
#ifdef _WIN32
    HANDLE hChange = (HANDLE)(uintptr_t)ValToInt(h_val);
    if (hChange != INVALID_HANDLE_VALUE && hChange != NULL) {
      FindCloseChangeNotification(hChange);
    }
#endif
    ValFree(&h_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  // AVX2 256-bit SIMD vector intrinsics
  if (strcmp(name, "SimdVec8fNew") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdVec8fNew() expects 1 argument (array8)\n");
      return -1;
    }
    AKSValue arr_val = POP();
    typedef struct { float v[8]; } AKSSimdVec8f;
    AKSSimdVec8f *sv = (AKSSimdVec8f *)malloc(sizeof(AKSSimdVec8f));
    for (int i = 0; i < 8; i++) {
      sv->v[i] = 0.0f;
    }
    if (ValKind(arr_val) == VALARR) {
      AKSArray *a = ValToArr(arr_val);
      int n = a->count < 8 ? a->count : 8;
      for (int i = 0; i < n; i++) {
        AKSValue el = a->items[i];
        double dv = (ValKind(el) == VALINT) ? (double)ValToInt(el) : ((ValKind(el) == VALFLOAT) ? ValToFloat(el) : 0.0);
        sv->v[i] = (float)dv;
      }
    }
    ValFree(&arr_val);
    PUSH(ValInt((int64_t)(uintptr_t)sv));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8fAdd") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec8fAdd() expects 2 arguments\n");
      return -1;
    }
    AKSValue v2_val = POP();
    AKSValue v1_val = POP();
    typedef struct { float v[8]; } AKSSimdVec8f;
    AKSSimdVec8f *p1 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v1_val);
    AKSSimdVec8f *p2 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v2_val);
    AKSSimdVec8f *res = (AKSSimdVec8f *)malloc(sizeof(AKSSimdVec8f));
    __m256 a = _mm256_loadu_ps(p1->v);
    __m256 b = _mm256_loadu_ps(p2->v);
    __m256 c = _mm256_add_ps(a, b);
    _mm256_storeu_ps(res->v, c);
    ValFree(&v2_val);
    ValFree(&v1_val);
    PUSH(ValInt((int64_t)(uintptr_t)res));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8fMul") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec8fMul() expects 2 arguments\n");
      return -1;
    }
    AKSValue v2_val = POP();
    AKSValue v1_val = POP();
    typedef struct { float v[8]; } AKSSimdVec8f;
    AKSSimdVec8f *p1 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v1_val);
    AKSSimdVec8f *p2 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v2_val);
    AKSSimdVec8f *res = (AKSSimdVec8f *)malloc(sizeof(AKSSimdVec8f));
    __m256 a = _mm256_loadu_ps(p1->v);
    __m256 b = _mm256_loadu_ps(p2->v);
    __m256 c = _mm256_mul_ps(a, b);
    _mm256_storeu_ps(res->v, c);
    ValFree(&v2_val);
    ValFree(&v1_val);
    PUSH(ValInt((int64_t)(uintptr_t)res));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8fFmadd") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SimdVec8fFmadd() expects 3 arguments (a, b, c)\n");
      return -1;
    }
    AKSValue v3_val = POP();
    AKSValue v2_val = POP();
    AKSValue v1_val = POP();
    typedef struct { float v[8]; } AKSSimdVec8f;
    AKSSimdVec8f *p1 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v1_val);
    AKSSimdVec8f *p2 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v2_val);
    AKSSimdVec8f *p3 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v3_val);
    AKSSimdVec8f *res = (AKSSimdVec8f *)malloc(sizeof(AKSSimdVec8f));
    __m256 a = _mm256_loadu_ps(p1->v);
    __m256 b = _mm256_loadu_ps(p2->v);
    __m256 c = _mm256_loadu_ps(p3->v);
    __m256 r = _mm256_fmadd_ps(a, b, c);
    _mm256_storeu_ps(res->v, r);
    ValFree(&v3_val);
    ValFree(&v2_val);
    ValFree(&v1_val);
    PUSH(ValInt((int64_t)(uintptr_t)res));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8fSum") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdVec8fSum() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    typedef struct { float v[8]; } AKSSimdVec8f;
    AKSSimdVec8f *p = (AKSSimdVec8f *)(uintptr_t)ValToInt(v_val);
    __m256 y = _mm256_loadu_ps(p->v);
    __m128 lo = _mm256_castps256_ps128(y);
    __m128 hi = _mm256_extractf128_ps(y, 1);
    __m128 s = _mm_add_ps(lo, hi);
    s = _mm_hadd_ps(s, s);
    s = _mm_hadd_ps(s, s);
    float total = _mm_cvtss_f32(s);
    ValFree(&v_val);
    PUSH(ValFloat((double)total));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8fMinElement") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdVec8fMinElement() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    typedef struct { float v[8]; } AKSSimdVec8f;
    AKSSimdVec8f *p = (AKSSimdVec8f *)(uintptr_t)ValToInt(v_val);
    float min_val = p->v[0];
    for (int i = 1; i < 8; i++) {
      if (p->v[i] < min_val) min_val = p->v[i];
    }
    ValFree(&v_val);
    PUSH(ValFloat((double)min_val));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8fMaxElement") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdVec8fMaxElement() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    typedef struct { float v[8]; } AKSSimdVec8f;
    AKSSimdVec8f *p = (AKSSimdVec8f *)(uintptr_t)ValToInt(v_val);
    float max_val = p->v[0];
    for (int i = 1; i < 8; i++) {
      if (p->v[i] > max_val) max_val = p->v[i];
    }
    ValFree(&v_val);
    PUSH(ValFloat((double)max_val));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8fNorm") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdVec8fNorm() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    typedef struct { float v[8]; } AKSSimdVec8f;
    AKSSimdVec8f *p = (AKSSimdVec8f *)(uintptr_t)ValToInt(v_val);
    float sum_sq = 0.0f;
    for (int i = 0; i < 8; i++) {
      sum_sq += p->v[i] * p->v[i];
    }
    float norm_val = sqrtf(sum_sq);
    ValFree(&v_val);
    PUSH(ValFloat((double)norm_val));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8fToArray") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdVec8fToArray() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    typedef struct { float v[8]; } AKSSimdVec8f;
    AKSSimdVec8f *p = (AKSSimdVec8f *)(uintptr_t)ValToInt(v_val);
    AKSValue res_arr = ValArr(8);
    AKSArray *arr = ValToArr(res_arr);
    for (int i = 0; i < 8; i++) {
      arr->items[arr->count++] = ValFloat((double)p->v[i]);
    }
    ValFree(&v_val);
    PUSH(res_arr);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8fFree") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdVec8fFree() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    void *p = (void *)(uintptr_t)ValToInt(v_val);
    if (p) free(p);
    ValFree(&v_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  // UDP Datagram Socket Networking
  if (strcmp(name, "UdpSocketBind") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: UdpSocketBind() expects 1 argument (port)\n");
      return -1;
    }
    AKSValue port_val = POP();
    int port = (int)ValToInt(port_val);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s != INVALID_SOCKET) {
      int opt = 1;
      setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
      struct sockaddr_in sin;
      memset(&sin, 0, sizeof(sin));
      sin.sin_family = AF_INET;
      sin.sin_addr.s_addr = INADDR_ANY;
      sin.sin_port = htons((u_short)port);
      if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        close((int)s);
#endif
        s = INVALID_SOCKET;
      }
    }
    ValFree(&port_val);
    PUSH(ValInt((int64_t)s));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "UdpSocketNew") == 0) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    PUSH(ValInt((int64_t)s));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "UdpSocketSendTo") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: UdpSocketSendTo() expects 4 arguments (sock, host, port, data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    AKSValue port_val = POP();
    AKSValue host_val = POP();
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    int sent = 0;
    if (s != INVALID_SOCKET && ValKind(host_val) == VALSTR && ValKind(data_val) == VALSTR) {
      struct sockaddr_in dest;
      memset(&dest, 0, sizeof(dest));
      dest.sin_family = AF_INET;
      dest.sin_port = htons((u_short)ValToInt(port_val));
      dest.sin_addr.s_addr = inet_addr(ValToStrPtr(host_val)->data);
      sent = sendto(s, ValToStrPtr(data_val)->data, ValToStrPtr(data_val)->len, 0, (struct sockaddr *)&dest, sizeof(dest));
    }
    ValFree(&data_val);
    ValFree(&port_val);
    ValFree(&host_val);
    ValFree(&s_val);
    PUSH(ValInt(sent));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "UdpSocketRecvFrom") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: UdpSocketRecvFrom() expects 2 arguments (sock, max_bytes)\n");
      return -1;
    }
    AKSValue max_val = POP();
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    int max_bytes = (int)ValToInt(max_val);
    if (max_bytes <= 0) max_bytes = 4096;
    if (max_bytes > 65536) max_bytes = 65536;
    char *buf = (char *)malloc((size_t)max_bytes + 1);
    struct sockaddr_in from;
    int fromlen = sizeof(from);
    int recvd = 0;
    if (s != INVALID_SOCKET) {
      recvd = recvfrom(s, buf, max_bytes, 0, (struct sockaddr *)&from, &fromlen);
      if (recvd < 0) recvd = 0;
    }
    buf[recvd] = '\0';
    AKSValue str_res = ValStrLen(buf, recvd);
    free(buf);
    ValFree(&max_val);
    ValFree(&s_val);
    PUSH(str_res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "UdpSocketClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: UdpSocketClose() expects 1 argument (sock)\n");
      return -1;
    }
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    if (s != INVALID_SOCKET) {
#ifdef _WIN32
      closesocket(s);
#else
      close((int)s);
#endif
    }
    ValFree(&s_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  // Linear Arena Allocator (VirtualAlloc)
  if (strcmp(name, "ArenaNew") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ArenaNew() expects 1 argument (capacity)\n");
      return -1;
    }
    AKSValue cap_val = POP();
    size_t cap = (size_t)ValToInt(cap_val);
    if (cap < 4096) cap = 4096;
    typedef struct { uint8_t *base; size_t capacity; size_t offset; } AKSArena;
    AKSArena *a = (AKSArena *)malloc(sizeof(AKSArena));
    a->capacity = cap;
    a->offset = 0;
#ifdef _WIN32
    a->base = (uint8_t *)VirtualAlloc(NULL, cap, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    a->base = (uint8_t *)mmap(NULL, cap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    ValFree(&cap_val);
    PUSH(ValInt((int64_t)(uintptr_t)a));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ArenaAlloc") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ArenaAlloc() expects 2 arguments (arena, size)\n");
      return -1;
    }
    AKSValue sz_val = POP();
    AKSValue a_val = POP();
    size_t sz = (size_t)ValToInt(sz_val);
    size_t aligned = (sz + 7) & ~7;
    typedef struct { uint8_t *base; size_t capacity; size_t offset; } AKSArena;
    AKSArena *a = (AKSArena *)(uintptr_t)ValToInt(a_val);
    int64_t ptr = 0;
    if (a && a->base && a->offset + aligned <= a->capacity) {
      ptr = (int64_t)(uintptr_t)(a->base + a->offset);
      a->offset += aligned;
    }
    ValFree(&sz_val);
    ValFree(&a_val);
    PUSH(ValInt(ptr));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ArenaReset") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ArenaReset() expects 1 argument (arena)\n");
      return -1;
    }
    AKSValue a_val = POP();
    typedef struct { uint8_t *base; size_t capacity; size_t offset; } AKSArena;
    AKSArena *a = (AKSArena *)(uintptr_t)ValToInt(a_val);
    if (a) {
      a->offset = 0;
    }
    ValFree(&a_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ArenaUsed") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ArenaUsed() expects 1 argument (arena)\n");
      return -1;
    }
    AKSValue a_val = POP();
    typedef struct { uint8_t *base; size_t capacity; size_t offset; } AKSArena;
    AKSArena *a = (AKSArena *)(uintptr_t)ValToInt(a_val);
    int64_t used = a ? (int64_t)a->offset : 0;
    ValFree(&a_val);
    PUSH(ValInt(used));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ArenaFree") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ArenaFree() expects 1 argument (arena)\n");
      return -1;
    }
    AKSValue a_val = POP();
    typedef struct { uint8_t *base; size_t capacity; size_t offset; } AKSArena;
    AKSArena *a = (AKSArena *)(uintptr_t)ValToInt(a_val);
    if (a) {
#ifdef _WIN32
      if (a->base) VirtualFree(a->base, 0, MEM_RELEASE);
#else
      if (a->base) munmap(a->base, a->capacity);
#endif
      free(a);
    }
    ValFree(&a_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  // Spsc Lock-Free Circular Queue (MemoryBarrier)
  if (strcmp(name, "LockFreeQueueNew") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: LockFreeQueueNew() expects 1 argument (capacity)\n");
      return -1;
    }
    AKSValue cap_val = POP();
    size_t cap = (size_t)ValToInt(cap_val);
    if (cap < 4) cap = 1024;
    typedef struct { int64_t *buffer; size_t capacity; volatile int64_t head; volatile int64_t tail; } AKSQueue;
    AKSQueue *q = (AKSQueue *)malloc(sizeof(AKSQueue));
    q->capacity = cap;
    q->buffer = (int64_t *)calloc(cap, sizeof(int64_t));
    q->head = 0;
    q->tail = 0;
    ValFree(&cap_val);
    PUSH(ValInt((int64_t)(uintptr_t)q));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "LockFreeQueuePush") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: LockFreeQueuePush() expects 2 arguments (queue, val)\n");
      return -1;
    }
    AKSValue val_obj = POP();
    AKSValue q_val = POP();
    int64_t val = ValToInt(val_obj);
    typedef struct { int64_t *buffer; size_t capacity; volatile int64_t head; volatile int64_t tail; } AKSQueue;
    AKSQueue *q = (AKSQueue *)(uintptr_t)ValToInt(q_val);
    int success = 0;
    if (q) {
      int64_t next_tail = (q->tail + 1) % q->capacity;
      if (next_tail != q->head) {
        q->buffer[q->tail] = val;
#ifdef _WIN32
        MemoryBarrier();
#else
        __sync_synchronize();
#endif
        q->tail = next_tail;
        success = 1;
      }
    }
    ValFree(&val_obj);
    ValFree(&q_val);
    PUSH(ValBool(success));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "LockFreeQueuePop") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: LockFreeQueuePop() expects 1 argument (queue)\n");
      return -1;
    }
    AKSValue q_val = POP();
    typedef struct { int64_t *buffer; size_t capacity; volatile int64_t head; volatile int64_t tail; } AKSQueue;
    AKSQueue *q = (AKSQueue *)(uintptr_t)ValToInt(q_val);
    AKSValue res = ValNull();
    if (q && q->head != q->tail) {
      int64_t val = q->buffer[q->head];
#ifdef _WIN32
      MemoryBarrier();
#else
      __sync_synchronize();
#endif
      q->head = (q->head + 1) % q->capacity;
      res = ValInt(val);
    }
    ValFree(&q_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "LockFreeQueueFree") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: LockFreeQueueFree() expects 1 argument (queue)\n");
      return -1;
    }
    AKSValue q_val = POP();
    typedef struct { int64_t *buffer; size_t capacity; volatile int64_t head; volatile int64_t tail; } AKSQueue;
    AKSQueue *q = (AKSQueue *)(uintptr_t)ValToInt(q_val);
    if (q) {
      if (q->buffer) free(q->buffer);
      free(q);
    }
    ValFree(&q_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  // Cryptographic Hashes & HMAC (BCrypt)
  if (strcmp(name, "CryptoSha256") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CryptoSha256() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    char hex[65];
    hex[0] = '\0';
    if (ValKind(data_val) == VALSTR) {
      AKSString *str = ValToStrPtr(data_val);
#ifdef _WIN32
      BCRYPT_ALG_HANDLE hAlg = NULL;
      BCRYPT_HASH_HANDLE hHash = NULL;
      if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0) {
        DWORD hashObjSize = 0, dataLen = 0;
        BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjSize, sizeof(hashObjSize), &dataLen, 0);
        PUCHAR pbHashObject = (PUCHAR)malloc(hashObjSize);
        uint8_t hashBuf[32];
        if (BCryptCreateHash(hAlg, &hHash, pbHashObject, hashObjSize, NULL, 0, 0) == 0) {
          BCryptHashData(hHash, (PUCHAR)str->data, (ULONG)str->len, 0);
          BCryptFinishHash(hHash, (PUCHAR)hashBuf, 32, 0);
          BCryptDestroyHash(hHash);
          for (int i = 0; i < 32; i++) {
            sprintf(hex + i * 2, "%02x", hashBuf[i]);
          }
          hex[64] = '\0';
        }
        free(pbHashObject);
        BCryptCloseAlgorithmProvider(hAlg, 0);
      }
#endif
    }
    ValFree(&data_val);
    PUSH(ValStr(hex));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptoSha512") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CryptoSha512() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    char hex[129];
    hex[0] = '\0';
    if (ValKind(data_val) == VALSTR) {
      AKSString *str = ValToStrPtr(data_val);
#ifdef _WIN32
      BCRYPT_ALG_HANDLE hAlg = NULL;
      BCRYPT_HASH_HANDLE hHash = NULL;
      if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA512_ALGORITHM, NULL, 0) == 0) {
        DWORD hashObjSize = 0, dataLen = 0;
        BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjSize, sizeof(hashObjSize), &dataLen, 0);
        PUCHAR pbHashObject = (PUCHAR)malloc(hashObjSize);
        uint8_t hashBuf[64];
        if (BCryptCreateHash(hAlg, &hHash, pbHashObject, hashObjSize, NULL, 0, 0) == 0) {
          BCryptHashData(hHash, (PUCHAR)str->data, (ULONG)str->len, 0);
          BCryptFinishHash(hHash, (PUCHAR)hashBuf, 64, 0);
          BCryptDestroyHash(hHash);
          for (int i = 0; i < 64; i++) {
            sprintf(hex + i * 2, "%02x", hashBuf[i]);
          }
          hex[128] = '\0';
        }
        free(pbHashObject);
        BCryptCloseAlgorithmProvider(hAlg, 0);
      }
#endif
    }
    ValFree(&data_val);
    PUSH(ValStr(hex));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptoHmacSha256") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoHmacSha256() expects 2 arguments (key, data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    AKSValue key_val = POP();
    char hex[65];
    hex[0] = '\0';
    if (ValKind(key_val) == VALSTR && ValKind(data_val) == VALSTR) {
      AKSString *keyStr = ValToStrPtr(key_val);
      AKSString *dataStr = ValToStrPtr(data_val);
#ifdef _WIN32
      BCRYPT_ALG_HANDLE hAlg = NULL;
      BCRYPT_HASH_HANDLE hHash = NULL;
      if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
        DWORD hashObjSize = 0, dataLen = 0;
        BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjSize, sizeof(hashObjSize), &dataLen, 0);
        PUCHAR pbHashObject = (PUCHAR)malloc(hashObjSize);
        uint8_t hashBuf[32];
        if (BCryptCreateHash(hAlg, &hHash, pbHashObject, hashObjSize, (PUCHAR)keyStr->data, (ULONG)keyStr->len, 0) == 0) {
          BCryptHashData(hHash, (PUCHAR)dataStr->data, (ULONG)dataStr->len, 0);
          BCryptFinishHash(hHash, (PUCHAR)hashBuf, 32, 0);
          BCryptDestroyHash(hHash);
          for (int i = 0; i < 32; i++) {
            sprintf(hex + i * 2, "%02x", hashBuf[i]);
          }
          hex[64] = '\0';
        }
        free(pbHashObject);
        BCryptCloseAlgorithmProvider(hAlg, 0);
      }
#endif
    }
    ValFree(&data_val);
    ValFree(&key_val);
    PUSH(ValStr(hex));
    *stacktopptr = stacktop;
    return 1;
  }

  // Data Compression (Win32 Compression API)
  if (strcmp(name, "CompressDeflate") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CompressDeflate() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    char *outHex = NULL;
    int outHexLen = 0;
    if (ValKind(data_val) == VALSTR) {
      AKSString *str = ValToStrPtr(data_val);
#ifdef _WIN32
      COMPRESSOR_HANDLE hCompressor = NULL;
      if (CreateCompressor(COMPRESS_ALGORITHM_MSZIP, NULL, &hCompressor)) {
        SIZE_T compSize = 0;
        Compress(hCompressor, str->data, (SIZE_T)str->len, NULL, 0, &compSize);
        if (compSize > 0) {
          uint8_t *compBuf = (uint8_t *)malloc(compSize);
          SIZE_T written = 0;
          if (Compress(hCompressor, str->data, (SIZE_T)str->len, compBuf, compSize, &written)) {
            outHex = (char *)malloc(written * 2 + 1);
            for (SIZE_T i = 0; i < written; i++) {
              sprintf(outHex + i * 2, "%02x", compBuf[i]);
            }
            outHex[written * 2] = '\0';
            outHexLen = (int)(written * 2);
          }
          free(compBuf);
        }
        CloseCompressor(hCompressor);
      }
#endif
    }
    ValFree(&data_val);
    AKSValue res = outHex ? ValStrLen(outHex, outHexLen) : ValStr("");
    if (outHex) free(outHex);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CompressInflate") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CompressInflate() expects 2 arguments (compressed_hex, original_size)\n");
      return -1;
    }
    AKSValue orig_size_val = POP();
    AKSValue hex_val = POP();
    char *decompStr = NULL;
    int decompStrLen = 0;
    if (ValKind(hex_val) == VALSTR) {
      AKSString *hexStr = ValToStrPtr(hex_val);
      int rawLen = hexStr->len / 2;
      uint8_t *rawBuf = (uint8_t *)malloc((size_t)(rawLen > 0 ? rawLen : 1));
      for (int i = 0; i < rawLen; i++) {
        unsigned int byteVal = 0;
        sscanf(hexStr->data + i * 2, "%02x", &byteVal);
        rawBuf[i] = (uint8_t)byteVal;
      }
#ifdef _WIN32
      DECOMPRESSOR_HANDLE hDecompressor = NULL;
      if (CreateDecompressor(COMPRESS_ALGORITHM_MSZIP, NULL, &hDecompressor)) {
        SIZE_T origSize = (SIZE_T)ValToInt(orig_size_val);
        if (origSize <= 0) origSize = 65536;
        uint8_t *decompBuf = (uint8_t *)malloc(origSize + 1);
        SIZE_T written = 0;
        if (Decompress(hDecompressor, rawBuf, (SIZE_T)rawLen, decompBuf, origSize, &written)) {
          decompBuf[written] = '\0';
          decompStr = (char *)decompBuf;
          decompStrLen = (int)written;
        } else {
          free(decompBuf);
        }
        CloseDecompressor(hDecompressor);
      }
#endif
      free(rawBuf);
    }
    ValFree(&orig_size_val);
    ValFree(&hex_val);
    AKSValue res = decompStr ? ValStrLen(decompStr, decompStrLen) : ValStr("");
    if (decompStr) free(decompStr);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  // Thread-Local Storage (Win32 TLS)
  if (strcmp(name, "ThreadLocalNew") == 0) {
    int64_t slot = 0;
#ifdef _WIN32
    DWORD tlsIdx = TlsAlloc();
    if (tlsIdx != TLS_OUT_OF_INDEXES) {
      slot = (int64_t)tlsIdx;
    }
#endif
    PUSH(ValInt(slot));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadLocalSet") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ThreadLocalSet() expects 2 arguments (slot, val)\n");
      return -1;
    }
    AKSValue val_obj = POP();
    AKSValue slot_val = POP();
    int ok = 0;
#ifdef _WIN32
    DWORD slot = (DWORD)ValToInt(slot_val);
    int64_t v = ValToInt(val_obj);
    if (TlsSetValue(slot, (LPVOID)(uintptr_t)v)) {
      ok = 1;
    }
#endif
    ValFree(&val_obj);
    ValFree(&slot_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadLocalGet") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadLocalGet() expects 1 argument (slot)\n");
      return -1;
    }
    AKSValue slot_val = POP();
    int64_t v = 0;
#ifdef _WIN32
    DWORD slot = (DWORD)ValToInt(slot_val);
    v = (int64_t)(uintptr_t)TlsGetValue(slot);
#endif
    ValFree(&slot_val);
    PUSH(ValInt(v));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadLocalFree") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadLocalFree() expects 1 argument (slot)\n");
      return -1;
    }
    AKSValue slot_val = POP();
#ifdef _WIN32
    DWORD slot = (DWORD)ValToInt(slot_val);
    TlsFree(slot);
#endif
    ValFree(&slot_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  // Fixed-Size Block Memory Pool Allocator
  if (strcmp(name, "MemoryPoolNew") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryPoolNew() expects 2 arguments (block_size, block_count)\n");
      return -1;
    }
    AKSValue count_val = POP();
    AKSValue size_val = POP();
    size_t blkSize = (size_t)ValToInt(size_val);
    size_t blkCount = (size_t)ValToInt(count_val);
    if (blkSize < 8) blkSize = 8;
    blkSize = (blkSize + 7) & ~7;
    if (blkCount < 1) blkCount = 64;

    typedef struct {
      uint8_t *slab;
      size_t blockSize;
      size_t blockCount;
      int32_t *freeList;
      int32_t freeTop;
    } AKSMemoryPool;

    AKSMemoryPool *pool = (AKSMemoryPool *)malloc(sizeof(AKSMemoryPool));
    pool->blockSize = blkSize;
    pool->blockCount = blkCount;
    pool->freeTop = (int32_t)blkCount;
    pool->freeList = (int32_t *)malloc(blkCount * sizeof(int32_t));
    for (size_t i = 0; i < blkCount; i++) {
      pool->freeList[i] = (int32_t)i;
    }
#ifdef _WIN32
    pool->slab = (uint8_t *)VirtualAlloc(NULL, blkSize * blkCount, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    pool->slab = (uint8_t *)mmap(NULL, blkSize * blkCount, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    ValFree(&count_val);
    ValFree(&size_val);
    PUSH(ValInt((int64_t)(uintptr_t)pool));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryPoolAlloc") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryPoolAlloc() expects 1 argument (pool)\n");
      return -1;
    }
    AKSValue p_val = POP();
    typedef struct {
      uint8_t *slab;
      size_t blockSize;
      size_t blockCount;
      int32_t *freeList;
      int32_t freeTop;
    } AKSMemoryPool;
    AKSMemoryPool *pool = (AKSMemoryPool *)(uintptr_t)ValToInt(p_val);
    int64_t ptr = 0;
    if (pool && pool->freeTop > 0) {
      int32_t idx = pool->freeList[--pool->freeTop];
      ptr = (int64_t)(uintptr_t)(pool->slab + (size_t)idx * pool->blockSize);
    }
    ValFree(&p_val);
    PUSH(ValInt(ptr));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryPoolFree") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryPoolFree() expects 2 arguments (pool, ptr)\n");
      return -1;
    }
    AKSValue ptr_val = POP();
    AKSValue p_val = POP();
    typedef struct {
      uint8_t *slab;
      size_t blockSize;
      size_t blockCount;
      int32_t *freeList;
      int32_t freeTop;
    } AKSMemoryPool;
    AKSMemoryPool *pool = (AKSMemoryPool *)(uintptr_t)ValToInt(p_val);
    uintptr_t ptr = (uintptr_t)ValToInt(ptr_val);
    if (pool && ptr >= (uintptr_t)pool->slab && ptr < (uintptr_t)(pool->slab + pool->blockCount * pool->blockSize)) {
      if ((size_t)pool->freeTop < pool->blockCount) {
        int32_t idx = (int32_t)((ptr - (uintptr_t)pool->slab) / pool->blockSize);
        pool->freeList[pool->freeTop++] = idx;
      }
    }
    ValFree(&ptr_val);
    ValFree(&p_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryPoolAvailable") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryPoolAvailable() expects 1 argument (pool)\n");
      return -1;
    }
    AKSValue p_val = POP();
    typedef struct {
      uint8_t *slab;
      size_t blockSize;
      size_t blockCount;
      int32_t *freeList;
      int32_t freeTop;
    } AKSMemoryPool;
    AKSMemoryPool *pool = (AKSMemoryPool *)(uintptr_t)ValToInt(p_val);
    int64_t avail = pool ? (int64_t)pool->freeTop : 0;
    ValFree(&p_val);
    PUSH(ValInt(avail));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryPoolDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryPoolDestroy() expects 1 argument (pool)\n");
      return -1;
    }
    AKSValue p_val = POP();
    typedef struct {
      uint8_t *slab;
      size_t blockSize;
      size_t blockCount;
      int32_t *freeList;
      int32_t freeTop;
    } AKSMemoryPool;
    AKSMemoryPool *pool = (AKSMemoryPool *)(uintptr_t)ValToInt(p_val);
    if (pool) {
#ifdef _WIN32
      if (pool->slab) VirtualFree(pool->slab, 0, MEM_RELEASE);
#else
      if (pool->slab) munmap(pool->slab, pool->blockSize * pool->blockCount);
#endif
      if (pool->freeList) free(pool->freeList);
      free(pool);
    }
    ValFree(&p_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  // Inter-Process Shared Memory Ring Buffer Queue (IPC)
  if (strcmp(name, "IpcQueueCreate") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: IpcQueueCreate() expects 2 arguments (name, capacity)\n");
      return -1;
    }
    AKSValue cap_val = POP();
    AKSValue name_val = POP();
    int cap = (int)ValToInt(cap_val);
    if (cap < 4) cap = 64;
    typedef struct {
      volatile uint32_t head;
      volatile uint32_t tail;
      uint32_t capacity;
      uint32_t entrySize;
    } AKSIpcHeader;
    typedef struct {
      void *hMap;
      void *pView;
      int isOwner;
      size_t totalSize;
    } AKSIpcQueue;

    AKSIpcQueue *q = (AKSIpcQueue *)malloc(sizeof(AKSIpcQueue));
    q->isOwner = 1;
    q->totalSize = sizeof(AKSIpcHeader) + (size_t)cap * 256;
    q->hMap = NULL;
    q->pView = NULL;

    if (ValKind(name_val) == VALSTR) {
      const char *qname = ValToStrPtr(name_val)->data;
#ifdef _WIN32
      q->hMap = (void *)CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)q->totalSize, qname);
      if (q->hMap) {
        q->pView = MapViewOfFile((HANDLE)q->hMap, FILE_MAP_ALL_ACCESS, 0, 0, q->totalSize);
        if (q->pView) {
          AKSIpcHeader *hdr = (AKSIpcHeader *)q->pView;
          hdr->head = 0;
          hdr->tail = 0;
          hdr->capacity = (uint32_t)cap;
          hdr->entrySize = 256;
        }
      }
#endif
    }
    ValFree(&cap_val);
    ValFree(&name_val);
    PUSH(ValInt((int64_t)(uintptr_t)q));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "IpcQueueOpen") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: IpcQueueOpen() expects 1 argument (name)\n");
      return -1;
    }
    AKSValue name_val = POP();
    typedef struct {
      void *hMap;
      void *pView;
      int isOwner;
      size_t totalSize;
    } AKSIpcQueue;

    AKSIpcQueue *q = (AKSIpcQueue *)malloc(sizeof(AKSIpcQueue));
    q->isOwner = 0;
    q->totalSize = 0;
    q->hMap = NULL;
    q->pView = NULL;

    if (ValKind(name_val) == VALSTR) {
      const char *qname = ValToStrPtr(name_val)->data;
#ifdef _WIN32
      q->hMap = (void *)OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, qname);
      if (q->hMap) {
        q->pView = MapViewOfFile((HANDLE)q->hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
      }
#endif
    }
    ValFree(&name_val);
    PUSH(ValInt((int64_t)(uintptr_t)q));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "IpcQueuePush") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: IpcQueuePush() expects 2 arguments (queue, msg)\n");
      return -1;
    }
    AKSValue msg_val = POP();
    AKSValue q_val = POP();
    typedef struct {
      volatile uint32_t head;
      volatile uint32_t tail;
      uint32_t capacity;
      uint32_t entrySize;
    } AKSIpcHeader;
    typedef struct {
      void *hMap;
      void *pView;
      int isOwner;
      size_t totalSize;
    } AKSIpcQueue;

    AKSIpcQueue *q = (AKSIpcQueue *)(uintptr_t)ValToInt(q_val);
    int ok = 0;
    if (q && q->pView && ValKind(msg_val) == VALSTR) {
      AKSIpcHeader *hdr = (AKSIpcHeader *)q->pView;
      uint32_t cap = hdr->capacity;
      if (hdr->tail - hdr->head < cap) {
        char *entryPtr = (char *)q->pView + sizeof(AKSIpcHeader) + (hdr->tail % cap) * 256;
        AKSString *str = ValToStrPtr(msg_val);
        int copyLen = str->len < 255 ? str->len : 255;
        memcpy(entryPtr, str->data, copyLen);
        entryPtr[copyLen] = '\0';
#ifdef _WIN32
        MemoryBarrier();
#else
        __sync_synchronize();
#endif
        hdr->tail++;
        ok = 1;
      }
    }
    ValFree(&msg_val);
    ValFree(&q_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "IpcQueuePop") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: IpcQueuePop() expects 1 argument (queue)\n");
      return -1;
    }
    AKSValue q_val = POP();
    typedef struct {
      volatile uint32_t head;
      volatile uint32_t tail;
      uint32_t capacity;
      uint32_t entrySize;
    } AKSIpcHeader;
    typedef struct {
      void *hMap;
      void *pView;
      int isOwner;
      size_t totalSize;
    } AKSIpcQueue;

    AKSIpcQueue *q = (AKSIpcQueue *)(uintptr_t)ValToInt(q_val);
    AKSValue res = ValNull();
    if (q && q->pView) {
      AKSIpcHeader *hdr = (AKSIpcHeader *)q->pView;
      if (hdr->head < hdr->tail) {
        char *entryPtr = (char *)q->pView + sizeof(AKSIpcHeader) + (hdr->head % hdr->capacity) * 256;
        res = ValStr(entryPtr);
#ifdef _WIN32
        MemoryBarrier();
#else
        __sync_synchronize();
#endif
        hdr->head++;
      }
    }
    ValFree(&q_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "IpcQueueClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: IpcQueueClose() expects 1 argument (queue)\n");
      return -1;
    }
    AKSValue q_val = POP();
    typedef struct {
      void *hMap;
      void *pView;
      int isOwner;
      size_t totalSize;
    } AKSIpcQueue;

    AKSIpcQueue *q = (AKSIpcQueue *)(uintptr_t)ValToInt(q_val);
    if (q) {
#ifdef _WIN32
      if (q->pView) UnmapViewOfFile(q->pView);
      if (q->hMap) CloseHandle((HANDLE)q->hMap);
#endif
      free(q);
    }
    ValFree(&q_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  // Process Environment Block & Working Directory
  if (strcmp(name, "EnvGet") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: EnvGet() expects 1 argument (key)\n");
      return -1;
    }
    AKSValue key_val = POP();
    AKSValue res = ValNull();
    if (ValKind(key_val) == VALSTR) {
      const char *key = ValToStrPtr(key_val)->data;
#ifdef _WIN32
      char buf[4096];
      DWORD len = GetEnvironmentVariableA(key, buf, sizeof(buf));
      if (len > 0 && len < sizeof(buf)) {
        res = ValStrLen(buf, (int)len);
      }
#else
      const char *val = getenv(key);
      if (val) res = ValStr(val);
#endif
    }
    ValFree(&key_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "EnvSet") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: EnvSet() expects 2 arguments (key, val)\n");
      return -1;
    }
    AKSValue val_obj = POP();
    AKSValue key_val = POP();
    int ok = 0;
    if (ValKind(key_val) == VALSTR) {
      const char *key = ValToStrPtr(key_val)->data;
      const char *val = (ValKind(val_obj) == VALSTR) ? ValToStrPtr(val_obj)->data : NULL;
#ifdef _WIN32
      ok = SetEnvironmentVariableA(key, val) ? 1 : 0;
#else
      if (val) setenv(key, val, 1); else unsetenv(key);
      ok = 1;
#endif
    }
    ValFree(&val_obj);
    ValFree(&key_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "EnvCwd") == 0) {
    char cwd[MAX_PATH];
    cwd[0] = '\0';
#ifdef _WIN32
    GetCurrentDirectoryA(sizeof(cwd), cwd);
#else
    if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
#endif
    PUSH(ValStr(cwd));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "EnvSetCwd") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: EnvSetCwd() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int ok = 0;
    if (ValKind(path_val) == VALSTR) {
      const char *path = ValToStrPtr(path_val)->data;
#ifdef _WIN32
      ok = SetCurrentDirectoryA(path) ? 1 : 0;
#else
      ok = (chdir(path) == 0) ? 1 : 0;
#endif
    }
    ValFree(&path_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware Bit Manipulation Intrinsics
  if (strcmp(name, "BitPopCount") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: BitPopCount() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    int64_t v = ValToInt(v_val);
    int64_t count = 0;
#ifdef _WIN32
    count = (int64_t)__popcnt64((uint64_t)v);
#else
    count = (int64_t)__builtin_popcountll((unsigned long long)v);
#endif
    ValFree(&v_val);
    PUSH(ValInt(count));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "BitClz") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: BitClz() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    uint64_t v = (uint64_t)ValToInt(v_val);
    int64_t clz = 64;
#ifdef _WIN32
    unsigned long idx = 0;
    if (_BitScanReverse64(&idx, v)) {
      clz = 63 - (int64_t)idx;
    }
#else
    if (v != 0) clz = (int64_t)__builtin_clzll((unsigned long long)v);
#endif
    ValFree(&v_val);
    PUSH(ValInt(clz));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "BitCtz") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: BitCtz() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    uint64_t v = (uint64_t)ValToInt(v_val);
    int64_t ctz = 64;
#ifdef _WIN32
    unsigned long idx = 0;
    if (_BitScanForward64(&idx, v)) {
      ctz = (int64_t)idx;
    }
#else
    if (v != 0) ctz = (int64_t)__builtin_ctzll((unsigned long long)v);
#endif
    ValFree(&v_val);
    PUSH(ValInt(ctz));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "BitRotl") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: BitRotl() expects 2 arguments (val, shift)\n");
      return -1;
    }
    AKSValue s_val = POP();
    AKSValue v_val = POP();
    uint64_t v = (uint64_t)ValToInt(v_val);
    int shift = (int)ValToInt(s_val);
    uint64_t res = 0;
#ifdef _WIN32
    res = _rotl64(v, shift);
#else
    shift = shift & 63;
    res = (v << shift) | (v >> (64 - shift));
#endif
    ValFree(&s_val);
    ValFree(&v_val);
    PUSH(ValInt((int64_t)res));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "BitRotr") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: BitRotr() expects 2 arguments (val, shift)\n");
      return -1;
    }
    AKSValue s_val = POP();
    AKSValue v_val = POP();
    uint64_t v = (uint64_t)ValToInt(v_val);
    int shift = (int)ValToInt(s_val);
    uint64_t res = 0;
#ifdef _WIN32
    res = _rotr64(v, shift);
#else
    shift = shift & 63;
    res = (v >> shift) | (v << (64 - shift));
#endif
    ValFree(&s_val);
    ValFree(&v_val);
    PUSH(ValInt((int64_t)res));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "BitByteSwap") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: BitByteSwap() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    uint32_t v = (uint32_t)ValToInt(v_val);
    uint32_t res = 0;
#ifdef _WIN32
    res = _byteswap_ulong(v);
#else
    res = __builtin_bswap32(v);
#endif
    ValFree(&v_val);
    PUSH(ValInt((int64_t)res));
    *stacktopptr = stacktop;
    return 1;
  }

  // Extended File Metadata & Operations
  if (strcmp(name, "FileSystemSize") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileSystemSize() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int64_t sz = -1;
    if (ValKind(path_val) == VALSTR) {
      const char *path = ValToStrPtr(path_val)->data;
#ifdef _WIN32
      WIN32_FILE_ATTRIBUTE_DATA fad;
      if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER li;
        li.LowPart = fad.nFileSizeLow;
        li.HighPart = fad.nFileSizeHigh;
        sz = (int64_t)li.QuadPart;
      }
#else
      struct stat st;
      if (stat(path, &st) == 0) sz = (int64_t)st.st_size;
#endif
    }
    ValFree(&path_val);
    PUSH(ValInt(sz));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FileSystemIsFile") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileSystemIsFile() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int isFile = 0;
    if (ValKind(path_val) == VALSTR) {
      const char *path = ValToStrPtr(path_val)->data;
#ifdef _WIN32
      DWORD attr = GetFileAttributesA(path);
      isFile = (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
#else
      struct stat st;
      if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) isFile = 1;
#endif
    }
    ValFree(&path_val);
    PUSH(ValBool(isFile));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FileSystemIsDir") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileSystemIsDir() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int isDir = 0;
    if (ValKind(path_val) == VALSTR) {
      const char *path = ValToStrPtr(path_val)->data;
#ifdef _WIN32
      DWORD attr = GetFileAttributesA(path);
      isDir = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
#else
      struct stat st;
      if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) isDir = 1;
#endif
    }
    ValFree(&path_val);
    PUSH(ValBool(isDir));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FileSystemModifiedTime") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileSystemModifiedTime() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int64_t mtime = 0;
    if (ValKind(path_val) == VALSTR) {
      const char *path = ValToStrPtr(path_val)->data;
#ifdef _WIN32
      WIN32_FILE_ATTRIBUTE_DATA fad;
      if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER uli;
        uli.LowPart = fad.ftLastWriteTime.dwLowDateTime;
        uli.HighPart = fad.ftLastWriteTime.dwHighDateTime;
        mtime = (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
      }
#else
      struct stat st;
      if (stat(path, &st) == 0) mtime = (int64_t)st.st_mtime * 1000LL;
#endif
    }
    ValFree(&path_val);
    PUSH(ValInt(mtime));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FileSystemCopy") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FileSystemCopy() expects 2 arguments (src, dst)\n");
      return -1;
    }
    AKSValue dst_val = POP();
    AKSValue src_val = POP();
    int ok = 0;
    if (ValKind(src_val) == VALSTR && ValKind(dst_val) == VALSTR) {
      const char *src = ValToStrPtr(src_val)->data;
      const char *dst = ValToStrPtr(dst_val)->data;
#ifdef _WIN32
      ok = CopyFileA(src, dst, FALSE) ? 1 : 0;
#endif
    }
    ValFree(&dst_val);
    ValFree(&src_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FileSystemWriteText") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FileSystemWriteText() expects 2 arguments (path, content)\n");
      return -1;
    }
    AKSValue content_val = POP();
    AKSValue path_val = POP();
    int ok = 0;
    if (ValKind(path_val) == VALSTR && ValKind(content_val) == VALSTR) {
      FILE *fp = fopen(ValToStrPtr(path_val)->data, "wb");
      if (fp) {
        fwrite(ValToStrPtr(content_val)->data, 1, ValToStrPtr(content_val)->len, fp);
        fclose(fp);
        ok = 1;
      }
    }
    ValFree(&content_val);
    ValFree(&path_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FileSystemReadText") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileSystemReadText() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    AKSValue res = ValNull();
    if (ValKind(path_val) == VALSTR) {
      FILE *fp = fopen(ValToStrPtr(path_val)->data, "rb");
      if (fp) {
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz >= 0) {
          char *buf = (char *)malloc((size_t)sz + 1);
          size_t rd = fread(buf, 1, (size_t)sz, fp);
          buf[rd] = '\0';
          res = ValStrLen(buf, (int)rd);
          free(buf);
        }
        fclose(fp);
      }
    }
    ValFree(&path_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FileSystemDelete") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileSystemDelete() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int ok = 0;
    if (ValKind(path_val) == VALSTR) {
      ok = (remove(ValToStrPtr(path_val)->data) == 0) ? 1 : 0;
    }
    ValFree(&path_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  // Authenticated AES-256-GCM Encryption & Decryption
  if (strcmp(name, "AesGcmEncrypt") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: AesGcmEncrypt() expects 4 arguments (key_hex, iv_hex, plaintext, auth_data)\n");
      return -1;
    }
    AKSValue auth_val = POP();
    AKSValue pt_val = POP();
    AKSValue iv_val = POP();
    AKSValue key_val = POP();
    char *outHex = NULL;
    if (ValKind(key_val) == VALSTR && ValKind(iv_val) == VALSTR && ValKind(pt_val) == VALSTR) {
      AKSString *keyStr = ValToStrPtr(key_val);
      AKSString *ivStr = ValToStrPtr(iv_val);
      AKSString *ptStr = ValToStrPtr(pt_val);
      const char *authData = (ValKind(auth_val) == VALSTR) ? ValToStrPtr(auth_val)->data : NULL;
      ULONG authDataLen = (ValKind(auth_val) == VALSTR) ? (ULONG)ValToStrPtr(auth_val)->len : 0;

      uint8_t keyBuf[32];
      int keyLen = keyStr->len / 2;
      if (keyLen > 32) keyLen = 32;
      memset(keyBuf, 0, sizeof(keyBuf));
      for (int i = 0; i < keyLen; i++) {
        unsigned int b = 0;
        sscanf(keyStr->data + i * 2, "%02x", &b);
        keyBuf[i] = (uint8_t)b;
      }

      uint8_t ivBuf[16];
      int ivLen = ivStr->len / 2;
      if (ivLen > 16) ivLen = 16;
      memset(ivBuf, 0, sizeof(ivBuf));
      for (int i = 0; i < ivLen; i++) {
        unsigned int b = 0;
        sscanf(ivStr->data + i * 2, "%02x", &b);
        ivBuf[i] = (uint8_t)b;
      }

#ifdef _WIN32
      BCRYPT_ALG_HANDLE hAlg = NULL;
      BCRYPT_KEY_HANDLE hKey = NULL;
      if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0) == 0) {
        BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
        if (BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, keyBuf, 32, 0) == 0) {
          uint8_t tagBuf[16];
          memset(tagBuf, 0, sizeof(tagBuf));
          BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
          BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
          authInfo.pbNonce = ivBuf;
          authInfo.cbNonce = (ULONG)ivLen;
          authInfo.pbAuthData = (PUCHAR)authData;
          authInfo.cbAuthData = authDataLen;
          authInfo.pbTag = tagBuf;
          authInfo.cbTag = sizeof(tagBuf);

          ULONG ptLen = (ULONG)ptStr->len;
          uint8_t *ctBuf = (uint8_t *)malloc(ptLen > 0 ? ptLen : 1);
          ULONG written = 0;
          if (BCryptEncrypt(hKey, (PUCHAR)ptStr->data, ptLen, &authInfo, NULL, 0, ctBuf, ptLen, &written, 0) == 0) {
            size_t totalBytes = (size_t)written + 16;
            outHex = (char *)malloc(totalBytes * 2 + 1);
            for (size_t i = 0; i < written; i++) {
              sprintf(outHex + i * 2, "%02x", ctBuf[i]);
            }
            for (size_t i = 0; i < 16; i++) {
              sprintf(outHex + (written + i) * 2, "%02x", tagBuf[i]);
            }
            outHex[totalBytes * 2] = '\0';
          }
          free(ctBuf);
          BCryptDestroyKey(hKey);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
      }
#endif
    }
    ValFree(&auth_val);
    ValFree(&pt_val);
    ValFree(&iv_val);
    ValFree(&key_val);
    AKSValue res = outHex ? ValStr(outHex) : ValNull();
    if (outHex) free(outHex);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "AesGcmDecrypt") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: AesGcmDecrypt() expects 4 arguments (key_hex, iv_hex, ciphertext_tag_hex, auth_data)\n");
      return -1;
    }
    AKSValue auth_val = POP();
    AKSValue ct_val = POP();
    AKSValue iv_val = POP();
    AKSValue key_val = POP();
    char *outPt = NULL;
    int outPtLen = 0;
    if (ValKind(key_val) == VALSTR && ValKind(iv_val) == VALSTR && ValKind(ct_val) == VALSTR) {
      AKSString *keyStr = ValToStrPtr(key_val);
      AKSString *ivStr = ValToStrPtr(iv_val);
      AKSString *ctStr = ValToStrPtr(ct_val);
      const char *authData = (ValKind(auth_val) == VALSTR) ? ValToStrPtr(auth_val)->data : NULL;
      ULONG authDataLen = (ValKind(auth_val) == VALSTR) ? (ULONG)ValToStrPtr(auth_val)->len : 0;

      uint8_t keyBuf[32];
      int keyLen = keyStr->len / 2;
      if (keyLen > 32) keyLen = 32;
      memset(keyBuf, 0, sizeof(keyBuf));
      for (int i = 0; i < keyLen; i++) {
        unsigned int b = 0;
        sscanf(keyStr->data + i * 2, "%02x", &b);
        keyBuf[i] = (uint8_t)b;
      }

      uint8_t ivBuf[16];
      int ivLen = ivStr->len / 2;
      if (ivLen > 16) ivLen = 16;
      memset(ivBuf, 0, sizeof(ivBuf));
      for (int i = 0; i < ivLen; i++) {
        unsigned int b = 0;
        sscanf(ivStr->data + i * 2, "%02x", &b);
        ivBuf[i] = (uint8_t)b;
      }

      int totalRawLen = ctStr->len / 2;
      if (totalRawLen >= 16) {
        int ctRawLen = totalRawLen - 16;
        uint8_t *ctBuf = (uint8_t *)malloc((size_t)(ctRawLen > 0 ? ctRawLen : 1));
        for (int i = 0; i < ctRawLen; i++) {
          unsigned int b = 0;
          sscanf(ctStr->data + i * 2, "%02x", &b);
          ctBuf[i] = (uint8_t)b;
        }
        uint8_t tagBuf[16];
        for (int i = 0; i < 16; i++) {
          unsigned int b = 0;
          sscanf(ctStr->data + (ctRawLen + i) * 2, "%02x", &b);
          tagBuf[i] = (uint8_t)b;
        }

#ifdef _WIN32
        BCRYPT_ALG_HANDLE hAlg = NULL;
        BCRYPT_KEY_HANDLE hKey = NULL;
        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0) == 0) {
          BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
          if (BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, keyBuf, 32, 0) == 0) {
            BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
            BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
            authInfo.pbNonce = ivBuf;
            authInfo.cbNonce = (ULONG)ivLen;
            authInfo.pbAuthData = (PUCHAR)authData;
            authInfo.cbAuthData = authDataLen;
            authInfo.pbTag = tagBuf;
            authInfo.cbTag = sizeof(tagBuf);

            uint8_t *ptBuf = (uint8_t *)malloc((size_t)(ctRawLen > 0 ? ctRawLen + 1 : 1));
            ULONG written = 0;
            if (BCryptDecrypt(hKey, ctBuf, (ULONG)ctRawLen, &authInfo, NULL, 0, ptBuf, (ULONG)ctRawLen, &written, 0) == 0) {
              ptBuf[written] = '\0';
              outPt = (char *)ptBuf;
              outPtLen = (int)written;
            } else {
              free(ptBuf);
            }
            BCryptDestroyKey(hKey);
          }
          BCryptCloseAlgorithmProvider(hAlg, 0);
        }
#endif
        free(ctBuf);
      }
    }
    ValFree(&auth_val);
    ValFree(&ct_val);
    ValFree(&iv_val);
    ValFree(&key_val);
    AKSValue res = outPt ? ValStrLen(outPt, outPtLen) : ValNull();
    if (outPt) free(outPt);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  // Virtual Memory Page Protection
  if (strcmp(name, "MemoryProtectSet") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: MemoryProtectSet() expects 3 arguments (ptr, size, mode)\n");
      return -1;
    }
    AKSValue mode_val = POP();
    AKSValue size_val = POP();
    AKSValue ptr_val = POP();
    int ok = 0;
    uintptr_t ptr = (uintptr_t)ValToInt(ptr_val);
    size_t size = (size_t)ValToInt(size_val);
    if (ptr != 0 && size > 0 && ValKind(mode_val) == VALSTR) {
      const char *mode = ValToStrPtr(mode_val)->data;
#ifdef _WIN32
      DWORD prot = PAGE_READWRITE;
      if (strcmp(mode, "ro") == 0) prot = PAGE_READONLY;
      else if (strcmp(mode, "none") == 0) prot = PAGE_NOACCESS;
      else if (strcmp(mode, "rwx") == 0) prot = PAGE_EXECUTE_READWRITE;
      DWORD oldProt = 0;
      ok = VirtualProtect((LPVOID)ptr, size, prot, &oldProt) ? 1 : 0;
#endif
    }
    ValFree(&mode_val);
    ValFree(&size_val);
    ValFree(&ptr_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  // Process Resource Telemetry
  if (strcmp(name, "ProcessWorkingSet") == 0) {
    int64_t bytes = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
      bytes = (int64_t)pmc.WorkingSetSize;
    }
#endif
    PUSH(ValInt(bytes));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessPeakWorkingSet") == 0) {
    int64_t bytes = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
      bytes = (int64_t)pmc.PeakWorkingSetSize;
    }
#endif
    PUSH(ValInt(bytes));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessCpuTime") == 0) {
    int64_t totalMs = 0;
#ifdef _WIN32
    FILETIME createTime, exitTime, kernelTime, userTime;
    if (GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime, &kernelTime, &userTime)) {
      ULARGE_INTEGER k, u;
      k.LowPart = kernelTime.dwLowDateTime;
      k.HighPart = kernelTime.dwHighDateTime;
      u.LowPart = userTime.dwLowDateTime;
      u.HighPart = userTime.dwHighDateTime;
      totalMs = (int64_t)((k.QuadPart + u.QuadPart) / 10000ULL);
    }
#endif
    PUSH(ValInt(totalMs));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Console Signal Handling
  if (strcmp(name, "OsSignalIgnoreBreak") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSignalIgnoreBreak() expects 1 argument (ignore_bool)\n");
      return -1;
    }
    AKSValue ign_val = POP();
    int ok = 0;
#ifdef _WIN32
    BOOL ignore = (BOOL)ValToBool(ign_val);
    ok = SetConsoleCtrlHandler(NULL, ignore) ? 1 : 0;
#endif
    ValFree(&ign_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "OsSignalGenerateInterrupt") == 0) {
    int ok = 0;
#ifdef _WIN32
    ok = GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0) ? 1 : 0;
#endif
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  // Universally Unique Identifiers (UUID v4)
  if (strcmp(name, "UuidGenerate") == 0) {
#ifdef _WIN32
    UUID uuid;
    UuidCreate(&uuid);
    RPC_CSTR str = NULL;
    if (UuidToStringA(&uuid, &str) == RPC_S_OK && str != NULL) {
      AKSValue res = ValStr((char *)str);
      RpcStringFreeA(&str);
      PUSH(res);
    } else {
      PUSH(ValNull());
    }
#else
    PUSH(ValNull());
#endif
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "UuidIsValid") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: UuidIsValid() expects 1 argument\n");
      return -1;
    }
    AKSValue s_val = POP();
    int isValid = 0;
    if (ValKind(s_val) == VALSTR) {
      const char *str = ValToStrPtr(s_val)->data;
#ifdef _WIN32
      UUID uuid;
      RPC_STATUS status = UuidFromStringA((RPC_CSTR)str, &uuid);
      isValid = (status == RPC_S_OK) ? 1 : 0;
#endif
    }
    ValFree(&s_val);
    PUSH(ValBool(isValid));
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware CPU Topology & Information
  if (strcmp(name, "CpuPhysicalCores") == 0) {
    int count = 1;
#ifdef _WIN32
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
    if (len > 0) {
      PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)malloc(len);
      if (buffer && GetLogicalProcessorInformationEx(RelationProcessorCore, buffer, &len)) {
        count = 0;
        DWORD offset = 0;
        while (offset < len) {
          PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((char *)buffer + offset);
          if (info->Relationship == RelationProcessorCore) count++;
          offset += info->Size;
        }
      }
      if (buffer) free(buffer);
    }
    if (count <= 0) count = 1;
#endif
    PUSH(ValInt((int64_t)count));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CpuLogicalCores") == 0) {
    int count = 1;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    count = (int)si.dwNumberOfProcessors;
#endif
    PUSH(ValInt((int64_t)count));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CpuPageSize") == 0) {
    int64_t pageSize = 4096;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    pageSize = (int64_t)si.dwPageSize;
#endif
    PUSH(ValInt(pageSize));
    *stacktopptr = stacktop;
    return 1;
  }

  // Inter-Process Named Pipes
  if (strcmp(name, "NamedPipeCreateServer") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: NamedPipeCreateServer() expects 1 argument (name)\n");
      return -1;
    }
    AKSValue name_val = POP();
    int64_t handleId = 0;
    if (ValKind(name_val) == VALSTR) {
      const char *pName = ValToStrPtr(name_val)->data;
#ifdef _WIN32
      HANDLE hPipe = CreateNamedPipeA(
          pName,
          PIPE_ACCESS_DUPLEX,
          PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT,
          1,
          4096,
          4096,
          0,
          NULL);
      if (hPipe != INVALID_HANDLE_VALUE) {
        handleId = (int64_t)(uintptr_t)hPipe;
      }
#endif
    }
    ValFree(&name_val);
    PUSH(ValInt(handleId));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NamedPipeCall") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: NamedPipeCall() expects 2 arguments (name, msg)\n");
      return -1;
    }
    AKSValue msg_val = POP();
    AKSValue name_val = POP();
    AKSValue res = ValNull();
    if (ValKind(name_val) == VALSTR && ValKind(msg_val) == VALSTR) {
      const char *pName = ValToStrPtr(name_val)->data;
      AKSString *msgStr = ValToStrPtr(msg_val);
#ifdef _WIN32
      char readBuf[4096];
      DWORD bytesRead = 0;
      BOOL ok = CallNamedPipeA(
          pName,
          (LPVOID)msgStr->data,
          (DWORD)msgStr->len,
          readBuf,
          sizeof(readBuf) - 1,
          &bytesRead,
          500);
      if (ok && bytesRead > 0) {
        readBuf[bytesRead] = '\0';
        res = ValStrLen(readBuf, (int)bytesRead);
      }
#endif
    }
    ValFree(&msg_val);
    ValFree(&name_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NamedPipeClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: NamedPipeClose() expects 1 argument (handle)\n");
      return -1;
    }
    AKSValue h_val = POP();
    int ok = 0;
#ifdef _WIN32
    HANDLE hPipe = (HANDLE)(uintptr_t)ValToInt(h_val);
    if (hPipe != NULL && hPipe != INVALID_HANDLE_VALUE) {
      ok = CloseHandle(hPipe) ? 1 : 0;
    }
#endif
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  // SIMD Vector Math Intrinsics
  if (strcmp(name, "FastMathSqrtVec8f") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FastMathSqrtVec8f() expects 1 argument (arr8)\n");
      return -1;
    }
    AKSValue arr_val = POP();
    AKSValue res = ValNull();
    if (ValKind(arr_val) == VALARR) {
      AKSArray *arr = ValToArr(arr_val);
      if (arr && arr->count >= 8) {
        float inFloats[8];
        for (int i = 0; i < 8; i++) {
          AKSValue el = arr->items[i];
          double dv = (ValKind(el) == VALINT) ? (double)ValToInt(el) : ((ValKind(el) == VALFLOAT) ? ValToFloat(el) : 0.0);
          inFloats[i] = (float)dv;
        }
        __m256 v = _mm256_loadu_ps(inFloats);
        __m256 r = _mm256_sqrt_ps(v);
        float outFloats[8];
        _mm256_storeu_ps(outFloats, r);
        res = ValArr(8);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 8; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)outFloats[i]);
        }
      }
    }
    ValFree(&arr_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastMathRsqrtVec8f") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FastMathRsqrtVec8f() expects 1 argument (arr8)\n");
      return -1;
    }
    AKSValue arr_val = POP();
    AKSValue res = ValNull();
    if (ValKind(arr_val) == VALARR) {
      AKSArray *arr = ValToArr(arr_val);
      if (arr && arr->count >= 8) {
        float inFloats[8];
        for (int i = 0; i < 8; i++) {
          AKSValue el = arr->items[i];
          double dv = (ValKind(el) == VALINT) ? (double)ValToInt(el) : ((ValKind(el) == VALFLOAT) ? ValToFloat(el) : 0.0);
          inFloats[i] = (float)dv;
        }
        __m256 v = _mm256_loadu_ps(inFloats);
        __m256 r = _mm256_rsqrt_ps(v);
        float outFloats[8];
        _mm256_storeu_ps(outFloats, r);
        res = ValArr(8);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 8; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)outFloats[i]);
        }
      }
    }
    ValFree(&arr_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastMathMinVec8f") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMathMinVec8f() expects 2 arguments (arr_a, arr_b)\n");
      return -1;
    }
    AKSValue b_val = POP();
    AKSValue a_val = POP();
    AKSValue res = ValNull();
    if (ValKind(a_val) == VALARR && ValKind(b_val) == VALARR) {
      AKSArray *arrA = ValToArr(a_val);
      AKSArray *arrB = ValToArr(b_val);
      if (arrA && arrB && arrA->count >= 8 && arrB->count >= 8) {
        float aFloats[8], bFloats[8];
        for (int i = 0; i < 8; i++) {
          AKSValue elA = arrA->items[i];
          AKSValue elB = arrB->items[i];
          double dvA = (ValKind(elA) == VALINT) ? (double)ValToInt(elA) : ((ValKind(elA) == VALFLOAT) ? ValToFloat(elA) : 0.0);
          double dvB = (ValKind(elB) == VALINT) ? (double)ValToInt(elB) : ((ValKind(elB) == VALFLOAT) ? ValToFloat(elB) : 0.0);
          aFloats[i] = (float)dvA;
          bFloats[i] = (float)dvB;
        }
        __m256 va = _mm256_loadu_ps(aFloats);
        __m256 vb = _mm256_loadu_ps(bFloats);
        __m256 vr = _mm256_min_ps(va, vb);
        float outFloats[8];
        _mm256_storeu_ps(outFloats, vr);
        res = ValArr(8);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 8; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)outFloats[i]);
        }
      }
    }
    ValFree(&b_val);
    ValFree(&a_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastMathMaxVec8f") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMathMaxVec8f() expects 2 arguments (arr_a, arr_b)\n");
      return -1;
    }
    AKSValue b_val = POP();
    AKSValue a_val = POP();
    AKSValue res = ValNull();
    if (ValKind(a_val) == VALARR && ValKind(b_val) == VALARR) {
      AKSArray *arrA = ValToArr(a_val);
      AKSArray *arrB = ValToArr(b_val);
      if (arrA && arrB && arrA->count >= 8 && arrB->count >= 8) {
        float aFloats[8], bFloats[8];
        for (int i = 0; i < 8; i++) {
          AKSValue elA = arrA->items[i];
          AKSValue elB = arrB->items[i];
          double dvA = (ValKind(elA) == VALINT) ? (double)ValToInt(elA) : ((ValKind(elA) == VALFLOAT) ? ValToFloat(elA) : 0.0);
          double dvB = (ValKind(elB) == VALINT) ? (double)ValToInt(elB) : ((ValKind(elB) == VALFLOAT) ? ValToFloat(elB) : 0.0);
          aFloats[i] = (float)dvA;
          bFloats[i] = (float)dvB;
        }
        __m256 va = _mm256_loadu_ps(aFloats);
        __m256 vb = _mm256_loadu_ps(bFloats);
        __m256 vr = _mm256_max_ps(va, vb);
        float outFloats[8];
        _mm256_storeu_ps(outFloats, vr);
        res = ValArr(8);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 8; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)outFloats[i]);
        }
      }
    }
    ValFree(&b_val);
    ValFree(&a_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  // Windows Registry Management
  if (strcmp(name, "RegistryGetString") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: RegistryGetString() expects 3 arguments (root, subkey, name)\n");
      return -1;
    }
    AKSValue n_val = POP();
    AKSValue k_val = POP();
    AKSValue r_val = POP();
    AKSValue res = ValNull();
    if (ValKind(r_val) == VALSTR && ValKind(k_val) == VALSTR && ValKind(n_val) == VALSTR) {
      const char *root = ValToStrPtr(r_val)->data;
      const char *subkey = ValToStrPtr(k_val)->data;
      const char *valName = ValToStrPtr(n_val)->data;
#ifdef _WIN32
      HKEY hRoot = HKEY_CURRENT_USER;
      if (strcmp(root, "HKLM") == 0) hRoot = HKEY_LOCAL_MACHINE;
      else if (strcmp(root, "HKCR") == 0) hRoot = HKEY_CLASSES_ROOT;
      HKEY hKey = NULL;
      if (RegOpenKeyExA(hRoot, subkey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buf[1024];
        DWORD sz = sizeof(buf);
        DWORD type = 0;
        if (RegQueryValueExA(hKey, valName, NULL, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS) {
          if (type == REG_SZ || type == REG_EXPAND_SZ) {
            res = ValStr(buf);
          }
        }
        RegCloseKey(hKey);
      }
#endif
    }
    ValFree(&n_val);
    ValFree(&k_val);
    ValFree(&r_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "RegistrySetString") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: RegistrySetString() expects 4 arguments (root, subkey, name, val)\n");
      return -1;
    }
    AKSValue v_val = POP();
    AKSValue n_val = POP();
    AKSValue k_val = POP();
    AKSValue r_val = POP();
    int ok = 0;
    if (ValKind(r_val) == VALSTR && ValKind(k_val) == VALSTR && ValKind(n_val) == VALSTR && ValKind(v_val) == VALSTR) {
      const char *root = ValToStrPtr(r_val)->data;
      const char *subkey = ValToStrPtr(k_val)->data;
      const char *valName = ValToStrPtr(n_val)->data;
      const char *valStr = ValToStrPtr(v_val)->data;
#ifdef _WIN32
      HKEY hRoot = HKEY_CURRENT_USER;
      if (strcmp(root, "HKLM") == 0) hRoot = HKEY_LOCAL_MACHINE;
      else if (strcmp(root, "HKCR") == 0) hRoot = HKEY_CLASSES_ROOT;
      HKEY hKey = NULL;
      if (RegCreateKeyExA(hRoot, subkey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        if (RegSetValueExA(hKey, valName, 0, REG_SZ, (const BYTE *)valStr, (DWORD)strlen(valStr) + 1) == ERROR_SUCCESS) {
          ok = 1;
        }
        RegCloseKey(hKey);
      }
#endif
    }
    ValFree(&v_val);
    ValFree(&n_val);
    ValFree(&k_val);
    ValFree(&r_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "RegistryGetDword") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: RegistryGetDword() expects 3 arguments (root, subkey, name)\n");
      return -1;
    }
    AKSValue n_val = POP();
    AKSValue k_val = POP();
    AKSValue r_val = POP();
    AKSValue res = ValNull();
    if (ValKind(r_val) == VALSTR && ValKind(k_val) == VALSTR && ValKind(n_val) == VALSTR) {
      const char *root = ValToStrPtr(r_val)->data;
      const char *subkey = ValToStrPtr(k_val)->data;
      const char *valName = ValToStrPtr(n_val)->data;
#ifdef _WIN32
      HKEY hRoot = HKEY_CURRENT_USER;
      if (strcmp(root, "HKLM") == 0) hRoot = HKEY_LOCAL_MACHINE;
      else if (strcmp(root, "HKCR") == 0) hRoot = HKEY_CLASSES_ROOT;
      HKEY hKey = NULL;
      if (RegOpenKeyExA(hRoot, subkey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwVal = 0;
        DWORD sz = sizeof(dwVal);
        DWORD type = 0;
        if (RegQueryValueExA(hKey, valName, NULL, &type, (LPBYTE)&dwVal, &sz) == ERROR_SUCCESS) {
          if (type == REG_DWORD) {
            res = ValInt((int64_t)dwVal);
          }
        }
        RegCloseKey(hKey);
      }
#endif
    }
    ValFree(&n_val);
    ValFree(&k_val);
    ValFree(&r_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "RegistrySetDword") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: RegistrySetDword() expects 4 arguments (root, subkey, name, val)\n");
      return -1;
    }
    AKSValue v_val = POP();
    AKSValue n_val = POP();
    AKSValue k_val = POP();
    AKSValue r_val = POP();
    int ok = 0;
    if (ValKind(r_val) == VALSTR && ValKind(k_val) == VALSTR && ValKind(n_val) == VALSTR) {
      const char *root = ValToStrPtr(r_val)->data;
      const char *subkey = ValToStrPtr(k_val)->data;
      const char *valName = ValToStrPtr(n_val)->data;
      DWORD dwVal = (DWORD)ValToInt(v_val);
#ifdef _WIN32
      HKEY hRoot = HKEY_CURRENT_USER;
      if (strcmp(root, "HKLM") == 0) hRoot = HKEY_LOCAL_MACHINE;
      else if (strcmp(root, "HKCR") == 0) hRoot = HKEY_CLASSES_ROOT;
      HKEY hKey = NULL;
      if (RegCreateKeyExA(hRoot, subkey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        if (RegSetValueExA(hKey, valName, 0, REG_DWORD, (const BYTE *)&dwVal, sizeof(dwVal)) == ERROR_SUCCESS) {
          ok = 1;
        }
        RegCloseKey(hKey);
      }
#endif
    }
    ValFree(&v_val);
    ValFree(&n_val);
    ValFree(&k_val);
    ValFree(&r_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "RegistryDeleteValue") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: RegistryDeleteValue() expects 3 arguments (root, subkey, name)\n");
      return -1;
    }
    AKSValue n_val = POP();
    AKSValue k_val = POP();
    AKSValue r_val = POP();
    int ok = 0;
    if (ValKind(r_val) == VALSTR && ValKind(k_val) == VALSTR && ValKind(n_val) == VALSTR) {
      const char *root = ValToStrPtr(r_val)->data;
      const char *subkey = ValToStrPtr(k_val)->data;
      const char *valName = ValToStrPtr(n_val)->data;
#ifdef _WIN32
      HKEY hRoot = HKEY_CURRENT_USER;
      if (strcmp(root, "HKLM") == 0) hRoot = HKEY_LOCAL_MACHINE;
      else if (strcmp(root, "HKCR") == 0) hRoot = HKEY_CLASSES_ROOT;
      HKEY hKey = NULL;
      if (RegOpenKeyExA(hRoot, subkey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        ok = (RegDeleteValueA(hKey, valName) == ERROR_SUCCESS) ? 1 : 0;
        RegCloseKey(hKey);
      }
#endif
    }
    ValFree(&n_val);
    ValFree(&k_val);
    ValFree(&r_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  // Console Buffer & Styling
  if (strcmp(name, "ConsoleSetColor") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ConsoleSetColor() expects 2 arguments (fg, bg)\n");
      return -1;
    }
    AKSValue bg_val = POP();
    AKSValue fg_val = POP();
    int ok = 0;
#ifdef _WIN32
    WORD fg = (WORD)ValToInt(fg_val);
    WORD bg = (WORD)ValToInt(bg_val);
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD attr = (WORD)(fg | (bg << 4));
    if (hConsole != INVALID_HANDLE_VALUE && SetConsoleTextAttribute(hConsole, attr)) {
      ok = 1;
    } else {
      HANDLE hCon = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
      if (hCon != INVALID_HANDLE_VALUE) {
        ok = SetConsoleTextAttribute(hCon, attr) ? 1 : 0;
        CloseHandle(hCon);
      } else {
        ok = 1;
      }
    }
#else
    ok = 1;
#endif
    ValFree(&bg_val);
    ValFree(&fg_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ConsoleResetColor") == 0) {
    int ok = 0;
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    if (hConsole != INVALID_HANDLE_VALUE && SetConsoleTextAttribute(hConsole, attr)) {
      ok = 1;
    } else {
      HANDLE hCon = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
      if (hCon != INVALID_HANDLE_VALUE) {
        ok = SetConsoleTextAttribute(hCon, attr) ? 1 : 0;
        CloseHandle(hCon);
      } else {
        ok = 1;
      }
    }
#else
    ok = 1;
#endif
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ConsoleSetCursorPos") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ConsoleSetCursorPos() expects 2 arguments (x, y)\n");
      return -1;
    }
    AKSValue y_val = POP();
    AKSValue x_val = POP();
    int ok = 0;
#ifdef _WIN32
    SHORT x = (SHORT)ValToInt(x_val);
    SHORT y = (SHORT)ValToInt(y_val);
    COORD pos = { x, y };
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE && SetConsoleCursorPosition(hConsole, pos)) {
      ok = 1;
    } else {
      HANDLE hCon = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
      if (hCon != INVALID_HANDLE_VALUE) {
        ok = SetConsoleCursorPosition(hCon, pos) ? 1 : 0;
        CloseHandle(hCon);
      } else {
        ok = 1;
      }
    }
#else
    ok = 1;
#endif
    ValFree(&y_val);
    ValFree(&x_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ConsoleGetSize") == 0) {
    AKSValue res = ValArr(2);
    AKSArray *arr = ValToArr(res);
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (hConsole != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hConsole, &csbi)) {
      int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
      int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
      arr->items[arr->count++] = ValInt(width);
      arr->items[arr->count++] = ValInt(height);
    } else {
      arr->items[arr->count++] = ValInt(80);
      arr->items[arr->count++] = ValInt(25);
    }
#else
    arr->items[arr->count++] = ValInt(80);
    arr->items[arr->count++] = ValInt(25);
#endif
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  // Storage Drive & Volume Telemetry
  if (strcmp(name, "DiskVolumeFreeSpace") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: DiskVolumeFreeSpace() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue p_val = POP();
    int64_t freeBytesVal = 0;
    if (ValKind(p_val) == VALSTR) {
      const char *path = ValToStrPtr(p_val)->data;
#ifdef _WIN32
      ULARGE_INTEGER freeBytes, totalBytes, totalFree;
      if (GetDiskFreeSpaceExA(path, &freeBytes, &totalBytes, &totalFree)) {
        freeBytesVal = (int64_t)freeBytes.QuadPart;
      }
#endif
    }
    ValFree(&p_val);
    PUSH(ValInt(freeBytesVal));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "DiskVolumeTotalSpace") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: DiskVolumeTotalSpace() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue p_val = POP();
    int64_t totalBytesVal = 0;
    if (ValKind(p_val) == VALSTR) {
      const char *path = ValToStrPtr(p_val)->data;
#ifdef _WIN32
      ULARGE_INTEGER freeBytes, totalBytes, totalFree;
      if (GetDiskFreeSpaceExA(path, &freeBytes, &totalBytes, &totalFree)) {
        totalBytesVal = (int64_t)totalBytes.QuadPart;
      }
#endif
    }
    ValFree(&p_val);
    PUSH(ValInt(totalBytesVal));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "DiskVolumeVolumeName") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: DiskVolumeVolumeName() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue p_val = POP();
    char volName[MAX_PATH + 1] = {0};
    if (ValKind(p_val) == VALSTR) {
      const char *path = ValToStrPtr(p_val)->data;
#ifdef _WIN32
      char fsName[MAX_PATH + 1] = {0};
      GetVolumeInformationA(path, volName, sizeof(volName), NULL, NULL, NULL, fsName, sizeof(fsName));
#endif
    }
    ValFree(&p_val);
    PUSH(ValStr(volName));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "DiskVolumeFileSystemName") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: DiskVolumeFileSystemName() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue p_val = POP();
    char fsName[MAX_PATH + 1] = {0};
    if (ValKind(p_val) == VALSTR) {
      const char *path = ValToStrPtr(p_val)->data;
#ifdef _WIN32
      char volName[MAX_PATH + 1] = {0};
      GetVolumeInformationA(path, volName, sizeof(volName), NULL, NULL, NULL, fsName, sizeof(fsName));
#endif
    }
    ValFree(&p_val);
    PUSH(ValStr(fsName));
    *stacktopptr = stacktop;
    return 1;
  }

  // Fast User-Mode Critical Section Locks
  if (strcmp(name, "ThreadSyncCreateMutex") == 0) {
    int64_t handleId = 0;
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)malloc(sizeof(CRITICAL_SECTION));
    if (cs) {
      InitializeCriticalSection(cs);
      handleId = (int64_t)(uintptr_t)cs;
    }
#endif
    PUSH(ValInt(handleId));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadSyncLock") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadSyncLock() expects 1 argument (mutex_id)\n");
      return -1;
    }
    AKSValue h_val = POP();
    int ok = 0;
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)(uintptr_t)ValToInt(h_val);
    if (cs) {
      EnterCriticalSection(cs);
      ok = 1;
    }
#endif
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadSyncUnlock") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadSyncUnlock() expects 1 argument (mutex_id)\n");
      return -1;
    }
    AKSValue h_val = POP();
    int ok = 0;
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)(uintptr_t)ValToInt(h_val);
    if (cs) {
      LeaveCriticalSection(cs);
      ok = 1;
    }
#endif
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadSyncDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadSyncDestroy() expects 1 argument (mutex_id)\n");
      return -1;
    }
    AKSValue h_val = POP();
    int ok = 0;
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)(uintptr_t)ValToInt(h_val);
    if (cs) {
      DeleteCriticalSection(cs);
      free(cs);
      ok = 1;
    }
#endif
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  // Binary Byte Stream Engine
  if (strcmp(name, "MemoryStreamNew") == 0) {
    int64_t cap = 256;
    if (argc == 1) {
      AKSValue c_val = POP();
      cap = ValToInt(c_val);
      if (cap < 16) cap = 16;
      ValFree(&c_val);
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)malloc(sizeof(AKSMemoryStreamObj));
    if (ms) {
      ms->buffer = (uint8_t *)malloc((size_t)cap);
      ms->position = 0;
      ms->length = 0;
      ms->capacity = cap;
    }
    PUSH(ValInt((int64_t)(uintptr_t)ms));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryStreamWriteByte") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryStreamWriteByte() expects 2 arguments (stream, byte)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue b_val = POP();
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int ok = 0;
    if (ms && ms->buffer) {
      if (ms->position + 1 > ms->capacity) {
        ms->capacity = (ms->capacity * 2) + 64;
        ms->buffer = (uint8_t *)realloc(ms->buffer, (size_t)ms->capacity);
      }
      ms->buffer[ms->position++] = (uint8_t)ValToInt(b_val);
      if (ms->position > ms->length) ms->length = ms->position;
      ok = 1;
    }
    ValFree(&b_val);
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryStreamWriteInt32") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryStreamWriteInt32() expects 2 arguments (stream, int32)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue v_val = POP();
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int ok = 0;
    if (ms && ms->buffer) {
      if (ms->position + 4 > ms->capacity) {
        ms->capacity = (ms->capacity * 2) + 64;
        ms->buffer = (uint8_t *)realloc(ms->buffer, (size_t)ms->capacity);
      }
      int32_t val32 = (int32_t)ValToInt(v_val);
      memcpy(ms->buffer + ms->position, &val32, 4);
      ms->position += 4;
      if (ms->position > ms->length) ms->length = ms->position;
      ok = 1;
    }
    ValFree(&v_val);
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryStreamWriteInt64") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryStreamWriteInt64() expects 2 arguments (stream, int64)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue v_val = POP();
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int ok = 0;
    if (ms && ms->buffer) {
      if (ms->position + 8 > ms->capacity) {
        ms->capacity = (ms->capacity * 2) + 64;
        ms->buffer = (uint8_t *)realloc(ms->buffer, (size_t)ms->capacity);
      }
      int64_t val64 = ValToInt(v_val);
      memcpy(ms->buffer + ms->position, &val64, 8);
      ms->position += 8;
      if (ms->position > ms->length) ms->length = ms->position;
      ok = 1;
    }
    ValFree(&v_val);
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryStreamWriteString") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryStreamWriteString() expects 2 arguments (stream, str)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue s_val = POP();
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int ok = 0;
    if (ms && ms->buffer && ValKind(s_val) == VALSTR) {
      AKSString *str = ValToStrPtr(s_val);
      if (ms->position + str->len > ms->capacity) {
        ms->capacity = (ms->capacity + str->len) * 2;
        ms->buffer = (uint8_t *)realloc(ms->buffer, (size_t)ms->capacity);
      }
      memcpy(ms->buffer + ms->position, str->data, (size_t)str->len);
      ms->position += str->len;
      if (ms->position > ms->length) ms->length = ms->position;
      ok = 1;
    }
    ValFree(&s_val);
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryStreamReadByte") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryStreamReadByte() expects 1 argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int64_t bVal = -1;
    if (ms && ms->buffer && ms->position < ms->length) {
      bVal = (int64_t)ms->buffer[ms->position++];
    }
    ValFree(&h_val);
    PUSH(ValInt(bVal));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryStreamReadInt32") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryStreamReadInt32() expects 1 argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int64_t resVal = 0;
    if (ms && ms->buffer && ms->position + 4 <= ms->length) {
      int32_t val32 = 0;
      memcpy(&val32, ms->buffer + ms->position, 4);
      ms->position += 4;
      resVal = (int64_t)val32;
    }
    ValFree(&h_val);
    PUSH(ValInt(resVal));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryStreamReadInt64") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryStreamReadInt64() expects 1 argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int64_t resVal = 0;
    if (ms && ms->buffer && ms->position + 8 <= ms->length) {
      memcpy(&resVal, ms->buffer + ms->position, 8);
      ms->position += 8;
    }
    ValFree(&h_val);
    PUSH(ValInt(resVal));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryStreamReadString") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryStreamReadString() expects 2 arguments (stream, len)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue l_val = POP();
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int64_t reqLen = ValToInt(l_val);
    AKSValue res = ValNull();
    if (ms && ms->buffer && reqLen > 0) {
      int64_t avail = ms->length - ms->position;
      if (reqLen > avail) reqLen = avail;
      if (reqLen > 0) {
        res = ValStrLen((const char *)(ms->buffer + ms->position), (int)reqLen);
        ms->position += reqLen;
      }
    }
    if (ValKind(res) == VALNULL) {
      res = ValStr("");
    }
    ValFree(&l_val);
    ValFree(&h_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryStreamSeek") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MemoryStreamSeek() expects 2 arguments (stream, pos)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue p_val = POP();
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int ok = 0;
    if (ms) {
      int64_t targetPos = ValToInt(p_val);
      if (targetPos < 0) targetPos = 0;
      if (targetPos > ms->length) targetPos = ms->length;
      ms->position = targetPos;
      ok = 1;
    }
    ValFree(&p_val);
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryStreamPosition") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryStreamPosition() expects 1 argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int64_t pos = ms ? ms->position : 0;
    ValFree(&h_val);
    PUSH(ValInt(pos));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryStreamLength") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryStreamLength() expects 1 argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int64_t len = ms ? ms->length : 0;
    ValFree(&h_val);
    PUSH(ValInt(len));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryStreamToHex") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryStreamToHex() expects 1 argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    AKSValue res = ValNull();
    if (ms && ms->buffer && ms->length > 0) {
      char *hexBuf = (char *)malloc((size_t)(ms->length * 2 + 1));
      if (hexBuf) {
        for (int64_t i = 0; i < ms->length; i++) {
          sprintf(hexBuf + i * 2, "%02x", (unsigned int)ms->buffer[i]);
        }
        hexBuf[ms->length * 2] = '\0';
        res = ValStrLen(hexBuf, (int)(ms->length * 2));
        free(hexBuf);
      }
    }
    if (ValKind(res) == VALNULL) {
      res = ValStr("");
    }
    ValFree(&h_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "MemoryStreamDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MemoryStreamDestroy() expects 1 argument (stream)\n");
      return -1;
    }
    typedef struct {
      uint8_t *buffer;
      int64_t position;
      int64_t length;
      int64_t capacity;
    } AKSMemoryStreamObj;
    AKSValue h_val = POP();
    AKSMemoryStreamObj *ms = (AKSMemoryStreamObj *)(uintptr_t)ValToInt(h_val);
    int ok = 0;
    if (ms) {
      if (ms->buffer) free(ms->buffer);
      free(ms);
      ok = 1;
    }
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }

  // System Power & Battery Telemetry
  if (strcmp(name, "SystemPowerAcStatus") == 0) {
    int64_t status = 255;
#ifdef _WIN32
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
      status = (int64_t)sps.ACLineStatus;
    }
#endif
    PUSH(ValInt(status));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemPowerBatteryPercent") == 0) {
    int64_t percent = 255;
#ifdef _WIN32
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
      percent = (int64_t)sps.BatteryLifePercent;
    }
#endif
    PUSH(ValInt(percent));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemPowerBatteryLifeTime") == 0) {
    int64_t lifeTime = -1;
#ifdef _WIN32
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
      lifeTime = (int64_t)(int32_t)sps.BatteryLifeTime;
    }
#endif
    PUSH(ValInt(lifeTime));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemPowerIsCharging") == 0) {
    int isCharging = 0;
#ifdef _WIN32
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
      isCharging = (sps.BatteryFlag & 8) ? 1 : 0;
    }
#endif
    PUSH(ValBool(isCharging));
    *stacktopptr = stacktop;
    return 1;
  }

  // Network Adapters & MAC Address Telemetry
  if (strcmp(name, "NetworkInterfacePrimaryIp") == 0) {
    char ipStr[64] = "127.0.0.1";
#ifdef _WIN32
    ULONG bufLen = sizeof(IP_ADAPTER_INFO);
    PIP_ADAPTER_INFO pAdapterInfo = (IP_ADAPTER_INFO *)malloc(bufLen);
    if (pAdapterInfo && GetAdaptersInfo(pAdapterInfo, &bufLen) == ERROR_BUFFER_OVERFLOW) {
      free(pAdapterInfo);
      pAdapterInfo = (IP_ADAPTER_INFO *)malloc(bufLen);
    }
    if (pAdapterInfo && GetAdaptersInfo(pAdapterInfo, &bufLen) == NO_ERROR) {
      PIP_ADAPTER_INFO pAdapter = pAdapterInfo;
      while (pAdapter) {
        if (pAdapter->IpAddressList.IpAddress.String[0] != '\0' &&
            strcmp(pAdapter->IpAddressList.IpAddress.String, "0.0.0.0") != 0) {
          strncpy(ipStr, pAdapter->IpAddressList.IpAddress.String, sizeof(ipStr) - 1);
          ipStr[sizeof(ipStr) - 1] = '\0';
          break;
        }
        pAdapter = pAdapter->Next;
      }
    }
    if (pAdapterInfo) free(pAdapterInfo);
#endif
    PUSH(ValStr(ipStr));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NetworkInterfacePrimaryMac") == 0) {
    char macStr[64] = "00:00:00:00:00:00";
#ifdef _WIN32
    ULONG bufLen = sizeof(IP_ADAPTER_INFO);
    PIP_ADAPTER_INFO pAdapterInfo = (IP_ADAPTER_INFO *)malloc(bufLen);
    if (pAdapterInfo && GetAdaptersInfo(pAdapterInfo, &bufLen) == ERROR_BUFFER_OVERFLOW) {
      free(pAdapterInfo);
      pAdapterInfo = (IP_ADAPTER_INFO *)malloc(bufLen);
    }
    if (pAdapterInfo && GetAdaptersInfo(pAdapterInfo, &bufLen) == NO_ERROR) {
      PIP_ADAPTER_INFO pAdapter = pAdapterInfo;
      while (pAdapter) {
        if (pAdapter->AddressLength >= 6) {
          sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
                  pAdapter->Address[0], pAdapter->Address[1], pAdapter->Address[2],
                  pAdapter->Address[3], pAdapter->Address[4], pAdapter->Address[5]);
          break;
        }
        pAdapter = pAdapter->Next;
      }
    }
    if (pAdapterInfo) free(pAdapterInfo);
#endif
    PUSH(ValStr(macStr));
    *stacktopptr = stacktop;
    return 1;
  }

  // SIMD 4x4 Matrix Mathematics Engine
  if (strcmp(name, "FastMatrixIdentity") == 0) {
    AKSValue res = ValArr(16);
    AKSArray *arr = ValToArr(res);
    for (int i = 0; i < 16; i++) {
      int r = i / 4;
      int c = i % 4;
      arr->items[arr->count++] = ValFloat((r == c) ? 1.0 : 0.0);
    }
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastMatrixMultiply") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMatrixMultiply() expects 2 arguments (mat4_a, mat4_b)\n");
      return -1;
    }
    AKSValue b_val = POP();
    AKSValue a_val = POP();
    AKSValue res = ValNull();
    if (ValKind(a_val) == VALARR && ValKind(b_val) == VALARR) {
      AKSArray *arrA = ValToArr(a_val);
      AKSArray *arrB = ValToArr(b_val);
      if (arrA && arrB && arrA->count >= 16 && arrB->count >= 16) {
        float a[16], b[16], c[16];
        for (int i = 0; i < 16; i++) {
          AKSValue elA = arrA->items[i];
          AKSValue elB = arrB->items[i];
          double dvA = (ValKind(elA) == VALINT) ? (double)ValToInt(elA) : ((ValKind(elA) == VALFLOAT) ? ValToFloat(elA) : 0.0);
          double dvB = (ValKind(elB) == VALINT) ? (double)ValToInt(elB) : ((ValKind(elB) == VALFLOAT) ? ValToFloat(elB) : 0.0);
          a[i] = (float)dvA;
          b[i] = (float)dvB;
        }
        for (int i = 0; i < 4; i++) {
          for (int j = 0; j < 4; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
              sum += a[i * 4 + k] * b[k * 4 + j];
            }
            c[i * 4 + j] = sum;
          }
        }
        res = ValArr(16);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 16; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)c[i]);
        }
      }
    }
    ValFree(&b_val);
    ValFree(&a_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastMatrixTransformVec4") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMatrixTransformVec4() expects 2 arguments (mat4, vec4)\n");
      return -1;
    }
    AKSValue v_val = POP();
    AKSValue m_val = POP();
    AKSValue res = ValNull();
    if (ValKind(m_val) == VALARR && ValKind(v_val) == VALARR) {
      AKSArray *arrM = ValToArr(m_val);
      AKSArray *arrV = ValToArr(v_val);
      if (arrM && arrV && arrM->count >= 16 && arrV->count >= 4) {
        float m[16], v[4], out[4];
        for (int i = 0; i < 16; i++) {
          AKSValue el = arrM->items[i];
          double dv = (ValKind(el) == VALINT) ? (double)ValToInt(el) : ((ValKind(el) == VALFLOAT) ? ValToFloat(el) : 0.0);
          m[i] = (float)dv;
        }
        for (int i = 0; i < 4; i++) {
          AKSValue el = arrV->items[i];
          double dv = (ValKind(el) == VALINT) ? (double)ValToInt(el) : ((ValKind(el) == VALFLOAT) ? ValToFloat(el) : 0.0);
          v[i] = (float)dv;
        }
        for (int i = 0; i < 4; i++) {
          out[i] = m[i * 4 + 0] * v[0] + m[i * 4 + 1] * v[1] + m[i * 4 + 2] * v[2] + m[i * 4 + 3] * v[3];
        }
        res = ValArr(4);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 4; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)out[i]);
        }
      }
    }
    ValFree(&v_val);
    ValFree(&m_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastMatrixTranspose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FastMatrixTranspose() expects 1 argument (mat4)\n");
      return -1;
    }
    AKSValue m_val = POP();
    AKSValue res = ValNull();
    if (ValKind(m_val) == VALARR) {
      AKSArray *arrM = ValToArr(m_val);
      if (arrM && arrM->count >= 16) {
        float m[16], t[16];
        for (int i = 0; i < 16; i++) {
          AKSValue el = arrM->items[i];
          double dv = (ValKind(el) == VALINT) ? (double)ValToInt(el) : ((ValKind(el) == VALFLOAT) ? ValToFloat(el) : 0.0);
          m[i] = (float)dv;
        }
        for (int i = 0; i < 4; i++) {
          for (int j = 0; j < 4; j++) {
            t[j * 4 + i] = m[i * 4 + j];
          }
        }
        res = ValArr(16);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 16; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)t[i]);
        }
      }
    }
    ValFree(&m_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }

  // Native OS Semaphore & Concurrency Counting
  if (strcmp(name, "OsSemaphoreCreate") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: OsSemaphoreCreate() expects 3 arguments (init_count, max_count, name)\n");
      return -1;
    }
    AKSValue NameVal = POP();
    AKSValue MaxVal = POP();
    AKSValue InitVal = POP();
    AKSValue ResVal = ValInt(0);
#ifdef _WIN32
    const char *NameStr = (ValKind(NameVal) == VALSTR) ? ValToStrPtr(NameVal)->data : NULL;
    LONG InitCount = (LONG)ValToInt(InitVal);
    LONG MaxCount = (LONG)ValToInt(MaxVal);
    HANDLE SemHandle = CreateSemaphoreA(NULL, InitCount, MaxCount, NameStr);
    ResVal = ValInt((int64_t)(uintptr_t)SemHandle);
#else
    ResVal = ValInt(1);
#endif
    ValFree(&InitVal);
    ValFree(&MaxVal);
    ValFree(&NameVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "OsSemaphoreAcquire") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: OsSemaphoreAcquire() expects 2 arguments (sem, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue SemVal = POP();
    AKSValue ResVal = ValBool(0);
#ifdef _WIN32
    HANDLE SemHandle = (HANDLE)(uintptr_t)ValToInt(SemVal);
    DWORD TimeoutMs = (DWORD)ValToInt(TimeoutVal);
    if (SemHandle && SemHandle != INVALID_HANDLE_VALUE) {
      DWORD WaitResult = WaitForSingleObject(SemHandle, TimeoutMs);
      ResVal = ValBool(WaitResult == WAIT_OBJECT_0 ? 1 : 0);
    }
#else
    ResVal = ValBool(1);
#endif
    ValFree(&SemVal);
    ValFree(&TimeoutVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "OsSemaphoreRelease") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: OsSemaphoreRelease() expects 2 arguments (sem, count)\n");
      return -1;
    }
    AKSValue CountVal = POP();
    AKSValue SemVal = POP();
    AKSValue ResVal = ValInt(-1);
#ifdef _WIN32
    HANDLE SemHandle = (HANDLE)(uintptr_t)ValToInt(SemVal);
    LONG RelCount = (LONG)ValToInt(CountVal);
    LONG PrevCount = 0;
    if (SemHandle && SemHandle != INVALID_HANDLE_VALUE) {
      if (ReleaseSemaphore(SemHandle, RelCount, &PrevCount)) {
        ResVal = ValInt((int64_t)PrevCount);
      }
    }
#else
    ResVal = ValInt(0);
#endif
    ValFree(&SemVal);
    ValFree(&CountVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "OsSemaphoreDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSemaphoreDestroy() expects 1 argument (sem)\n");
      return -1;
    }
    AKSValue SemVal = POP();
#ifdef _WIN32
    HANDLE SemHandle = (HANDLE)(uintptr_t)ValToInt(SemVal);
    if (SemHandle && SemHandle != INVALID_HANDLE_VALUE) {
      CloseHandle(SemHandle);
    }
#endif
    ValFree(&SemVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Memory Working Set & Page Fault Telemetry
  if (strcmp(name, "ProcessWorkingSetCurrent") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessWorkingSetCurrent() expects 1 argument (pid)\n");
      return -1;
    }
    AKSValue PidVal = POP();
    int64_t WorkingSetBytes = 0;
#ifdef _WIN32
    DWORD TargetPid = (DWORD)ValToInt(PidVal);
    HANDLE ProcHandle = (TargetPid == 0) ? GetCurrentProcess() : OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, TargetPid);
    if (ProcHandle) {
      PROCESS_MEMORY_COUNTERS Pmc;
      memset(&Pmc, 0, sizeof(Pmc));
      if (GetProcessMemoryInfo(ProcHandle, &Pmc, sizeof(Pmc))) {
        WorkingSetBytes = (int64_t)Pmc.WorkingSetSize;
      }
      if (TargetPid != 0) {
        CloseHandle(ProcHandle);
      }
    }
#endif
    ValFree(&PidVal);
    PUSH(ValInt(WorkingSetBytes));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessWorkingSetPeak") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessWorkingSetPeak() expects 1 argument (pid)\n");
      return -1;
    }
    AKSValue PidVal = POP();
    int64_t PeakWorkingSetBytes = 0;
#ifdef _WIN32
    DWORD TargetPid = (DWORD)ValToInt(PidVal);
    HANDLE ProcHandle = (TargetPid == 0) ? GetCurrentProcess() : OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, TargetPid);
    if (ProcHandle) {
      PROCESS_MEMORY_COUNTERS Pmc;
      memset(&Pmc, 0, sizeof(Pmc));
      if (GetProcessMemoryInfo(ProcHandle, &Pmc, sizeof(Pmc))) {
        PeakWorkingSetBytes = (int64_t)Pmc.PeakWorkingSetSize;
      }
      if (TargetPid != 0) {
        CloseHandle(ProcHandle);
      }
    }
#endif
    ValFree(&PidVal);
    PUSH(ValInt(PeakWorkingSetBytes));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessWorkingSetPageFaults") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessWorkingSetPageFaults() expects 1 argument (pid)\n");
      return -1;
    }
    AKSValue PidVal = POP();
    int64_t PageFaultCount = 0;
#ifdef _WIN32
    DWORD TargetPid = (DWORD)ValToInt(PidVal);
    HANDLE ProcHandle = (TargetPid == 0) ? GetCurrentProcess() : OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, TargetPid);
    if (ProcHandle) {
      PROCESS_MEMORY_COUNTERS Pmc;
      memset(&Pmc, 0, sizeof(Pmc));
      if (GetProcessMemoryInfo(ProcHandle, &Pmc, sizeof(Pmc))) {
        PageFaultCount = (int64_t)Pmc.PageFaultCount;
      }
      if (TargetPid != 0) {
        CloseHandle(ProcHandle);
      }
    }
#endif
    ValFree(&PidVal);
    PUSH(ValInt(PageFaultCount));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessWorkingSetTrim") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessWorkingSetTrim() expects 1 argument (pid)\n");
      return -1;
    }
    AKSValue PidVal = POP();
    BOOL TrimSuccess = FALSE;
#ifdef _WIN32
    DWORD TargetPid = (DWORD)ValToInt(PidVal);
    HANDLE ProcHandle = (TargetPid == 0) ? GetCurrentProcess() : OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION, FALSE, TargetPid);
    if (ProcHandle) {
      TrimSuccess = EmptyWorkingSet(ProcHandle);
      if (TargetPid != 0) {
        CloseHandle(ProcHandle);
      }
    }
#endif
    ValFree(&PidVal);
    PUSH(ValBool(TrimSuccess ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // High-Precision Monotonic Clock & Timestamp Counter
  if (strcmp(name, "HardwareTimestampRdtsc") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: HardwareTimestampRdtsc() expects 0 arguments\n");
      return -1;
    }
    uint64_t CycleCount = 0;
#ifdef _WIN32
    CycleCount = __rdtsc();
#endif
    PUSH(ValInt((int64_t)(CycleCount & 0x7FFFFFFFFFFFFFFFLL)));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "HardwareTimestampRdtscp") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: HardwareTimestampRdtscp() expects 0 arguments\n");
      return -1;
    }
    uint64_t CycleCount = 0;
    unsigned int CoreId = 0;
#ifdef _WIN32
    CycleCount = __rdtscp(&CoreId);
#endif
    AKSValue ResStruct = ValStruct("HardwareTimestampRdtscpResult");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("cycles");
    StructPtr->fieldvals[0] = ValInt((int64_t)(CycleCount & 0x7FFFFFFFFFFFFFFFLL));
    StructPtr->fieldnames[1] = strdup("core_id");
    StructPtr->fieldvals[1] = ValInt((int64_t)CoreId);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "HardwareTimestampTicksPerSecond") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: HardwareTimestampTicksPerSecond() expects 0 arguments\n");
      return -1;
    }
    int64_t TicksPerSec = 10000000;
#ifdef _WIN32
    LARGE_INTEGER Freq;
    if (QueryPerformanceFrequency(&Freq)) {
      TicksPerSec = (int64_t)Freq.QuadPart;
    }
#endif
    PUSH(ValInt(TicksPerSec));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "HardwareTimestampNanoseconds") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: HardwareTimestampNanoseconds() expects 0 arguments\n");
      return -1;
    }
    int64_t TotalNanoseconds = 0;
#ifdef _WIN32
    LARGE_INTEGER Freq, Counter;
    if (QueryPerformanceFrequency(&Freq) && QueryPerformanceCounter(&Counter) && Freq.QuadPart > 0) {
      TotalNanoseconds = (int64_t)((Counter.QuadPart / Freq.QuadPart) * 1000000000LL + ((Counter.QuadPart % Freq.QuadPart) * 1000000000LL) / Freq.QuadPart);
    }
#endif
    PUSH(ValInt(TotalNanoseconds));
    *stacktopptr = stacktop;
    return 1;
  }

  // Native Range-Based File Locking
  if (strcmp(name, "FileLockAcquireShared") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: FileLockAcquireShared() expects 4 arguments (path, offset, length, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue LenVal = POP();
    AKSValue OffVal = POP();
    AKSValue PathVal = POP();
    AKSValue ResVal = ValInt(0);
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR) {
      const char *FilePath = ValToStrPtr(PathVal)->data;
      HANDLE FileHandle = CreateFileA(FilePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (FileHandle != INVALID_HANDLE_VALUE) {
        OVERLAPPED OverlappedStruct;
        memset(&OverlappedStruct, 0, sizeof(OverlappedStruct));
        OverlappedStruct.Offset = (DWORD)ValToInt(OffVal);
        DWORD LockLength = (DWORD)ValToInt(LenVal);
        if (LockFileEx(FileHandle, 0, 0, LockLength, 0, &OverlappedStruct)) {
          ResVal = ValInt((int64_t)(uintptr_t)FileHandle);
        } else {
          CloseHandle(FileHandle);
        }
      }
    }
#endif
    ValFree(&PathVal);
    ValFree(&OffVal);
    ValFree(&LenVal);
    ValFree(&TimeoutVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FileLockAcquireExclusive") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: FileLockAcquireExclusive() expects 4 arguments (path, offset, length, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue LenVal = POP();
    AKSValue OffVal = POP();
    AKSValue PathVal = POP();
    AKSValue ResVal = ValInt(0);
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR) {
      const char *FilePath = ValToStrPtr(PathVal)->data;
      HANDLE FileHandle = CreateFileA(FilePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (FileHandle != INVALID_HANDLE_VALUE) {
        OVERLAPPED OverlappedStruct;
        memset(&OverlappedStruct, 0, sizeof(OverlappedStruct));
        OverlappedStruct.Offset = (DWORD)ValToInt(OffVal);
        DWORD LockLength = (DWORD)ValToInt(LenVal);
        if (LockFileEx(FileHandle, LOCKFILE_EXCLUSIVE_LOCK, 0, LockLength, 0, &OverlappedStruct)) {
          ResVal = ValInt((int64_t)(uintptr_t)FileHandle);
        } else {
          CloseHandle(FileHandle);
        }
      }
    }
#endif
    ValFree(&PathVal);
    ValFree(&OffVal);
    ValFree(&LenVal);
    ValFree(&TimeoutVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FileLockRelease") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: FileLockRelease() expects 3 arguments (handle, offset, length)\n");
      return -1;
    }
    AKSValue LenVal = POP();
    AKSValue OffVal = POP();
    AKSValue HandleVal = POP();
    BOOL UnlockOk = FALSE;
#ifdef _WIN32
    HANDLE FileHandle = (HANDLE)(uintptr_t)ValToInt(HandleVal);
    if (FileHandle && FileHandle != INVALID_HANDLE_VALUE) {
      OVERLAPPED OverlappedStruct;
      memset(&OverlappedStruct, 0, sizeof(OverlappedStruct));
      OverlappedStruct.Offset = (DWORD)ValToInt(OffVal);
      DWORD LockLength = (DWORD)ValToInt(LenVal);
      UnlockOk = UnlockFileEx(FileHandle, 0, LockLength, 0, &OverlappedStruct);
      CloseHandle(FileHandle);
    }
#endif
    ValFree(&HandleVal);
    ValFree(&OffVal);
    ValFree(&LenVal);
    PUSH(ValBool(UnlockOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FileLockIsLocked") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileLockIsLocked() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue PathVal = POP();
    BOOL IsFileLocked = FALSE;
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR) {
      const char *FilePath = ValToStrPtr(PathVal)->data;
      HANDLE FileHandle = CreateFileA(FilePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (FileHandle == INVALID_HANDLE_VALUE) {
        DWORD ErrorCode = GetLastError();
        if (ErrorCode == ERROR_SHARING_VIOLATION || ErrorCode == ERROR_LOCK_VIOLATION) {
          IsFileLocked = TRUE;
        }
      } else {
        CloseHandle(FileHandle);
      }
    }
#endif
    ValFree(&PathVal);
    PUSH(ValBool(IsFileLocked ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware CRC32-C & Fast Checksum Engine
  if (strcmp(name, "FastChecksumCrc32c") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FastChecksumCrc32c() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    uint32_t RunningCrc = 0xFFFFFFFF;
    if (ValKind(DataVal) == VALSTR) {
      const uint8_t *DataBytes = (const uint8_t *)ValToStrPtr(DataVal)->data;
      size_t DataLength = (size_t)ValToStrPtr(DataVal)->len;
#ifdef _WIN32
      while (DataLength >= 8) {
        RunningCrc = (uint32_t)_mm_crc32_u64(RunningCrc, *(const uint64_t *)DataBytes);
        DataBytes += 8;
        DataLength -= 8;
      }
      while (DataLength > 0) {
        RunningCrc = _mm_crc32_u8(RunningCrc, *DataBytes);
        DataBytes++;
        DataLength--;
      }
#endif
    }
    ValFree(&DataVal);
    PUSH(ValInt((int64_t)(RunningCrc ^ 0xFFFFFFFF)));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastChecksumCrc32cUpdate") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastChecksumCrc32cUpdate() expects 2 arguments (current_crc, data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    AKSValue CrcVal = POP();
    uint32_t RunningCrc = ((uint32_t)ValToInt(CrcVal)) ^ 0xFFFFFFFF;
    if (ValKind(DataVal) == VALSTR) {
      const uint8_t *DataBytes = (const uint8_t *)ValToStrPtr(DataVal)->data;
      size_t DataLength = (size_t)ValToStrPtr(DataVal)->len;
#ifdef _WIN32
      while (DataLength >= 8) {
        RunningCrc = (uint32_t)_mm_crc32_u64(RunningCrc, *(const uint64_t *)DataBytes);
        DataBytes += 8;
        DataLength -= 8;
      }
      while (DataLength > 0) {
        RunningCrc = _mm_crc32_u8(RunningCrc, *DataBytes);
        DataBytes++;
        DataLength--;
      }
#endif
    }
    ValFree(&CrcVal);
    ValFree(&DataVal);
    PUSH(ValInt((int64_t)(RunningCrc ^ 0xFFFFFFFF)));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastChecksumAdler32") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FastChecksumAdler32() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    uint32_t AdlerA = 1;
    uint32_t AdlerB = 0;
    if (ValKind(DataVal) == VALSTR) {
      const uint8_t *DataBytes = (const uint8_t *)ValToStrPtr(DataVal)->data;
      size_t DataLength = (size_t)ValToStrPtr(DataVal)->len;
      for (size_t Index = 0; Index < DataLength; Index++) {
        AdlerA = (AdlerA + DataBytes[Index]) % 65521;
        AdlerB = (AdlerB + AdlerA) % 65521;
      }
    }
    ValFree(&DataVal);
    uint32_t FinalAdler = (AdlerB << 16) | AdlerA;
    PUSH(ValInt((int64_t)FinalAdler));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastChecksumFnv1a64") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FastChecksumFnv1a64() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    uint64_t FnvHash = 14695981039346656037ULL;
    if (ValKind(DataVal) == VALSTR) {
      const uint8_t *DataBytes = (const uint8_t *)ValToStrPtr(DataVal)->data;
      size_t DataLength = (size_t)ValToStrPtr(DataVal)->len;
      for (size_t Index = 0; Index < DataLength; Index++) {
        FnvHash ^= (uint64_t)DataBytes[Index];
        FnvHash *= 1099511628211ULL;
      }
    }
    ValFree(&DataVal);
    PUSH(ValInt((int64_t)FnvHash));
    *stacktopptr = stacktop;
    return 1;
  }

  // Native OS Event Objects & Inter-Thread Signaling
  if (strcmp(name, "OsEventCreate") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: OsEventCreate() expects 3 arguments (manual_reset, init_state, name)\n");
      return -1;
    }
    AKSValue NameVal = POP();
    AKSValue InitVal = POP();
    AKSValue ManualVal = POP();
    AKSValue ResVal = ValInt(0);
#ifdef _WIN32
    const char *NameStr = (ValKind(NameVal) == VALSTR) ? ValToStrPtr(NameVal)->data : NULL;
    BOOL ManualReset = ValToBool(ManualVal) ? TRUE : FALSE;
    BOOL InitState = ValToBool(InitVal) ? TRUE : FALSE;
    HANDLE EventHandle = CreateEventA(NULL, ManualReset, InitState, NameStr);
    ResVal = ValInt((int64_t)(uintptr_t)EventHandle);
#else
    ResVal = ValInt(1);
#endif
    ValFree(&ManualVal);
    ValFree(&InitVal);
    ValFree(&NameVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "OsEventSet") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsEventSet() expects 1 argument (event)\n");
      return -1;
    }
    AKSValue EventVal = POP();
    BOOL SetOk = FALSE;
#ifdef _WIN32
    HANDLE EventHandle = (HANDLE)(uintptr_t)ValToInt(EventVal);
    if (EventHandle && EventHandle != INVALID_HANDLE_VALUE) {
      SetOk = SetEvent(EventHandle);
    }
#endif
    ValFree(&EventVal);
    PUSH(ValBool(SetOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "OsEventReset") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsEventReset() expects 1 argument (event)\n");
      return -1;
    }
    AKSValue EventVal = POP();
    BOOL ResetOk = FALSE;
#ifdef _WIN32
    HANDLE EventHandle = (HANDLE)(uintptr_t)ValToInt(EventVal);
    if (EventHandle && EventHandle != INVALID_HANDLE_VALUE) {
      ResetOk = ResetEvent(EventHandle);
    }
#endif
    ValFree(&EventVal);
    PUSH(ValBool(ResetOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "OsEventWait") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: OsEventWait() expects 2 arguments (event, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue EventVal = POP();
    AKSValue ResVal = ValBool(0);
#ifdef _WIN32
    HANDLE EventHandle = (HANDLE)(uintptr_t)ValToInt(EventVal);
    DWORD TimeoutMs = (DWORD)ValToInt(TimeoutVal);
    if (EventHandle && EventHandle != INVALID_HANDLE_VALUE) {
      DWORD WaitResult = WaitForSingleObject(EventHandle, TimeoutMs);
      ResVal = ValBool(WaitResult == WAIT_OBJECT_0 ? 1 : 0);
    }
#else
    ResVal = ValBool(1);
#endif
    ValFree(&EventVal);
    ValFree(&TimeoutVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "OsEventDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsEventDestroy() expects 1 argument (event)\n");
      return -1;
    }
    AKSValue EventVal = POP();
#ifdef _WIN32
    HANDLE EventHandle = (HANDLE)(uintptr_t)ValToInt(EventVal);
    if (EventHandle && EventHandle != INVALID_HANDLE_VALUE) {
      CloseHandle(EventHandle);
    }
#endif
    ValFree(&EventVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware On-Die Entropy & True Randomness Engine
  if (strcmp(name, "EntropyPoolHardwareSeed") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: EntropyPoolHardwareSeed() expects 0 arguments\n");
      return -1;
    }
    unsigned __int64 SeedVal = 0;
#ifdef _WIN32
    int SeedOk = _rdseed64_step(&SeedVal);
    if (!SeedOk) {
      _rdrand64_step(&SeedVal);
    }
#endif
    PUSH(ValInt((int64_t)SeedVal));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "EntropyPoolHardwareRandomInt64") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: EntropyPoolHardwareRandomInt64() expects 0 arguments\n");
      return -1;
    }
    unsigned __int64 RandVal = 0;
#ifdef _WIN32
    int RandOk = _rdrand64_step(&RandVal);
    if (!RandOk) {
      BCryptGenRandom(NULL, (PUCHAR)&RandVal, sizeof(RandVal), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    }
#endif
    PUSH(ValInt((int64_t)RandVal));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "EntropyPoolHardwareEntropyAvailable") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: EntropyPoolHardwareEntropyAvailable() expects 0 arguments\n");
      return -1;
    }
    BOOL HasRdrand = FALSE;
#ifdef _WIN32
    int CpuInfo[4];
    __cpuid(CpuInfo, 1);
    HasRdrand = (CpuInfo[2] & (1 << 30)) != 0;
#endif
    PUSH(ValBool(HasRdrand ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "EntropyPoolSystemEntropyBytes") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: EntropyPoolSystemEntropyBytes() expects 1 argument (count)\n");
      return -1;
    }
    AKSValue CountVal = POP();
    int ByteCount = (int)ValToInt(CountVal);
    if (ByteCount <= 0) ByteCount = 16;
    if (ByteCount > 1024) ByteCount = 1024;
    char HexOutput[2049] = {0};
#ifdef _WIN32
    uint8_t EntropyBuf[1024];
    if (BCryptGenRandom(NULL, EntropyBuf, (ULONG)ByteCount, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) {
      static const char HexChars[] = "0123456789abcdef";
      for (int Idx = 0; Idx < ByteCount; Idx++) {
        HexOutput[Idx * 2] = HexChars[(EntropyBuf[Idx] >> 4) & 0x0F];
        HexOutput[Idx * 2 + 1] = HexChars[EntropyBuf[Idx] & 0x0F];
      }
      HexOutput[ByteCount * 2] = '\0';
    }
#endif
    ValFree(&CountVal);
    PUSH(ValStr(HexOutput));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Thread Affinity & Priority Scheduling Control
  if (strcmp(name, "ThreadSchedulerSetAffinity") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadSchedulerSetAffinity() expects 1 argument (mask)\n");
      return -1;
    }
    AKSValue MaskVal = POP();
    int64_t PrevAffinity = 0;
#ifdef _WIN32
    DWORD_PTR AffinityMask = (DWORD_PTR)ValToInt(MaskVal);
    DWORD_PTR PrevMask = SetThreadAffinityMask(GetCurrentThread(), AffinityMask);
    PrevAffinity = (int64_t)PrevMask;
#endif
    ValFree(&MaskVal);
    PUSH(ValInt(PrevAffinity));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadSchedulerGetProcessAffinity") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ThreadSchedulerGetProcessAffinity() expects 0 arguments\n");
      return -1;
    }
    int64_t ProcessAffinity = 1;
#ifdef _WIN32
    DWORD_PTR ProcMask = 0, SysMask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &ProcMask, &SysMask)) {
      ProcessAffinity = (int64_t)ProcMask;
    }
#endif
    PUSH(ValInt(ProcessAffinity));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadSchedulerSetPriority") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadSchedulerSetPriority() expects 1 argument (priority)\n");
      return -1;
    }
    AKSValue PrioVal = POP();
    BOOL SetOk = FALSE;
#ifdef _WIN32
    int PriorityVal = (int)ValToInt(PrioVal);
    SetOk = SetThreadPriority(GetCurrentThread(), PriorityVal);
#endif
    ValFree(&PrioVal);
    PUSH(ValBool(SetOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadSchedulerGetPriority") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ThreadSchedulerGetPriority() expects 0 arguments\n");
      return -1;
    }
    int CurrentPriority = 0;
#ifdef _WIN32
    CurrentPriority = GetThreadPriority(GetCurrentThread());
#endif
    PUSH(ValInt(CurrentPriority));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS System Locale & High-Precision Calendar / Timezone Telemetry
  if (strcmp(name, "SystemLocaleCurrentLocaleName") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemLocaleCurrentLocaleName() expects 0 arguments\n");
      return -1;
    }
    char LocaleNameStr[128] = {0};
#ifdef _WIN32
    GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SNAME, LocaleNameStr, sizeof(LocaleNameStr));
    if (LocaleNameStr[0] == '\0') strcpy(LocaleNameStr, "en-US");
#else
    strcpy(LocaleNameStr, "en-US");
#endif
    PUSH(ValStr(LocaleNameStr));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemLocaleTimezoneName") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemLocaleTimezoneName() expects 0 arguments\n");
      return -1;
    }
    char TimezoneNameStr[128] = {0};
#ifdef _WIN32
    TIME_ZONE_INFORMATION TzInfo;
    memset(&TzInfo, 0, sizeof(TzInfo));
    GetTimeZoneInformation(&TzInfo);
    WideCharToMultiByte(CP_UTF8, 0, TzInfo.StandardName, -1, TimezoneNameStr, sizeof(TimezoneNameStr), NULL, NULL);
    if (TimezoneNameStr[0] == '\0') strcpy(TimezoneNameStr, "UTC");
#else
    strcpy(TimezoneNameStr, "UTC");
#endif
    PUSH(ValStr(TimezoneNameStr));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemLocaleTimezoneBiasMinutes") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemLocaleTimezoneBiasMinutes() expects 0 arguments\n");
      return -1;
    }
    int64_t BiasMinutes = 0;
#ifdef _WIN32
    TIME_ZONE_INFORMATION TzInfo;
    memset(&TzInfo, 0, sizeof(TzInfo));
    GetTimeZoneInformation(&TzInfo);
    BiasMinutes = (int64_t)TzInfo.Bias;
#endif
    PUSH(ValInt(BiasMinutes));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemLocaleIsDaylightSaving") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemLocaleIsDaylightSaving() expects 0 arguments\n");
      return -1;
    }
    BOOL IsDaylight = FALSE;
#ifdef _WIN32
    TIME_ZONE_INFORMATION TzInfo;
    memset(&TzInfo, 0, sizeof(TzInfo));
    DWORD TzResult = GetTimeZoneInformation(&TzInfo);
    IsDaylight = (TzResult == TIME_ZONE_ID_DAYLIGHT);
#endif
    PUSH(ValBool(IsDaylight ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Waitable Kernel Timers & Periodic Alarms
  if (strcmp(name, "WaitableTimerCreate") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: WaitableTimerCreate() expects 2 arguments (manual_reset, name)\n");
      return -1;
    }
    AKSValue NameVal = POP();
    AKSValue ManualVal = POP();
    AKSValue ResVal = ValInt(0);
#ifdef _WIN32
    const char *NameStr = (ValKind(NameVal) == VALSTR) ? ValToStrPtr(NameVal)->data : NULL;
    BOOL ManualReset = ValToBool(ManualVal) ? TRUE : FALSE;
    HANDLE TimerHandle = CreateWaitableTimerA(NULL, ManualReset, NameStr);
    ResVal = ValInt((int64_t)(uintptr_t)TimerHandle);
#else
    ResVal = ValInt(1);
#endif
    ValFree(&ManualVal);
    ValFree(&NameVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "WaitableTimerSet") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: WaitableTimerSet() expects 3 arguments (timer, due_ms, period_ms)\n");
      return -1;
    }
    AKSValue PeriodVal = POP();
    AKSValue DueVal = POP();
    AKSValue TimerVal = POP();
    BOOL SetOk = FALSE;
#ifdef _WIN32
    HANDLE TimerHandle = (HANDLE)(uintptr_t)ValToInt(TimerVal);
    int64_t DueMs = ValToInt(DueVal);
    LONG PeriodMs = (LONG)ValToInt(PeriodVal);
    if (TimerHandle && TimerHandle != INVALID_HANDLE_VALUE) {
      LARGE_INTEGER DueTime;
      DueTime.QuadPart = -10000LL * DueMs;
      SetOk = SetWaitableTimer(TimerHandle, &DueTime, PeriodMs, NULL, NULL, FALSE);
    }
#endif
    ValFree(&TimerVal);
    ValFree(&DueVal);
    ValFree(&PeriodVal);
    PUSH(ValBool(SetOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "WaitableTimerWait") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: WaitableTimerWait() expects 2 arguments (timer, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue TimerVal = POP();
    AKSValue ResVal = ValBool(0);
#ifdef _WIN32
    HANDLE TimerHandle = (HANDLE)(uintptr_t)ValToInt(TimerVal);
    DWORD TimeoutMs = (DWORD)ValToInt(TimeoutVal);
    if (TimerHandle && TimerHandle != INVALID_HANDLE_VALUE) {
      DWORD WaitResult = WaitForSingleObject(TimerHandle, TimeoutMs);
      ResVal = ValBool(WaitResult == WAIT_OBJECT_0 ? 1 : 0);
    }
#else
    ResVal = ValBool(1);
#endif
    ValFree(&TimerVal);
    ValFree(&TimeoutVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "WaitableTimerCancel") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WaitableTimerCancel() expects 1 argument (timer)\n");
      return -1;
    }
    AKSValue TimerVal = POP();
    BOOL CancelOk = FALSE;
#ifdef _WIN32
    HANDLE TimerHandle = (HANDLE)(uintptr_t)ValToInt(TimerVal);
    if (TimerHandle && TimerHandle != INVALID_HANDLE_VALUE) {
      CancelOk = CancelWaitableTimer(TimerHandle);
    }
#endif
    ValFree(&TimerVal);
    PUSH(ValBool(CancelOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "WaitableTimerDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WaitableTimerDestroy() expects 1 argument (timer)\n");
      return -1;
    }
    AKSValue TimerVal = POP();
#ifdef _WIN32
    HANDLE TimerHandle = (HANDLE)(uintptr_t)ValToInt(TimerVal);
    if (TimerHandle && TimerHandle != INVALID_HANDLE_VALUE) {
      CloseHandle(TimerHandle);
    }
#endif
    ValFree(&TimerVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // SIMD 3d vector geometry engine
  if (strcmp(name, "FastGeometryDot3") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastGeometryDot3() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    double DotResult = 0.0;
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      if (ArrA->count >= 3 && ArrB->count >= 3) {
        double A0 = (ValKind(ArrA->items[0]) == VALINT) ? (double)ValToInt(ArrA->items[0]) : ValToFloat(ArrA->items[0]);
        double A1 = (ValKind(ArrA->items[1]) == VALINT) ? (double)ValToInt(ArrA->items[1]) : ValToFloat(ArrA->items[1]);
        double A2 = (ValKind(ArrA->items[2]) == VALINT) ? (double)ValToInt(ArrA->items[2]) : ValToFloat(ArrA->items[2]);
        double B0 = (ValKind(ArrB->items[0]) == VALINT) ? (double)ValToInt(ArrB->items[0]) : ValToFloat(ArrB->items[0]);
        double B1 = (ValKind(ArrB->items[1]) == VALINT) ? (double)ValToInt(ArrB->items[1]) : ValToFloat(ArrB->items[1]);
        double B2 = (ValKind(ArrB->items[2]) == VALINT) ? (double)ValToInt(ArrB->items[2]) : ValToFloat(ArrB->items[2]);
        DotResult = A0 * B0 + A1 * B1 + A2 * B2;
      }
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ValFloat(DotResult));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastGeometryCross3") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastGeometryCross3() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(3);
    AKSArray *ResArr = ValToArr(ResArrVal);
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      if (ArrA->count >= 3 && ArrB->count >= 3) {
        double A0 = (ValKind(ArrA->items[0]) == VALINT) ? (double)ValToInt(ArrA->items[0]) : ValToFloat(ArrA->items[0]);
        double A1 = (ValKind(ArrA->items[1]) == VALINT) ? (double)ValToInt(ArrA->items[1]) : ValToFloat(ArrA->items[1]);
        double A2 = (ValKind(ArrA->items[2]) == VALINT) ? (double)ValToInt(ArrA->items[2]) : ValToFloat(ArrA->items[2]);
        double B0 = (ValKind(ArrB->items[0]) == VALINT) ? (double)ValToInt(ArrB->items[0]) : ValToFloat(ArrB->items[0]);
        double B1 = (ValKind(ArrB->items[1]) == VALINT) ? (double)ValToInt(ArrB->items[1]) : ValToFloat(ArrB->items[1]);
        double B2 = (ValKind(ArrB->items[2]) == VALINT) ? (double)ValToInt(ArrB->items[2]) : ValToFloat(ArrB->items[2]);
        ResArr->items[ResArr->count++] = ValFloat(A1 * B2 - A2 * B1);
        ResArr->items[ResArr->count++] = ValFloat(A2 * B0 - A0 * B2);
        ResArr->items[ResArr->count++] = ValFloat(A0 * B1 - A1 * B0);
      }
    }
    while (ResArr->count < 3) {
      ResArr->items[ResArr->count++] = ValFloat(0.0);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastGeometryNormalize3") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FastGeometryNormalize3() expects 1 argument (vec)\n");
      return -1;
    }
    AKSValue VecVal = POP();
    AKSValue ResArrVal = ValArr(3);
    AKSArray *ResArr = ValToArr(ResArrVal);
    if (ValKind(VecVal) == VALARR) {
      AKSArray *SrcArr = ValToArr(VecVal);
      if (SrcArr->count >= 3) {
        double V0 = (ValKind(SrcArr->items[0]) == VALINT) ? (double)ValToInt(SrcArr->items[0]) : ValToFloat(SrcArr->items[0]);
        double V1 = (ValKind(SrcArr->items[1]) == VALINT) ? (double)ValToInt(SrcArr->items[1]) : ValToFloat(SrcArr->items[1]);
        double V2 = (ValKind(SrcArr->items[2]) == VALINT) ? (double)ValToInt(SrcArr->items[2]) : ValToFloat(SrcArr->items[2]);
        double Magnitude = sqrt(V0 * V0 + V1 * V1 + V2 * V2);
        double InvMag = (Magnitude > 1e-12) ? (1.0 / Magnitude) : 0.0;
        ResArr->items[ResArr->count++] = ValFloat(V0 * InvMag);
        ResArr->items[ResArr->count++] = ValFloat(V1 * InvMag);
        ResArr->items[ResArr->count++] = ValFloat(V2 * InvMag);
      }
    }
    while (ResArr->count < 3) {
      ResArr->items[ResArr->count++] = ValFloat(0.0);
    }
    ValFree(&VecVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastGeometryDistance3") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastGeometryDistance3() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    double DistResult = 0.0;
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      if (ArrA->count >= 3 && ArrB->count >= 3) {
        double A0 = (ValKind(ArrA->items[0]) == VALINT) ? (double)ValToInt(ArrA->items[0]) : ValToFloat(ArrA->items[0]);
        double A1 = (ValKind(ArrA->items[1]) == VALINT) ? (double)ValToInt(ArrA->items[1]) : ValToFloat(ArrA->items[1]);
        double A2 = (ValKind(ArrA->items[2]) == VALINT) ? (double)ValToInt(ArrA->items[2]) : ValToFloat(ArrA->items[2]);
        double B0 = (ValKind(ArrB->items[0]) == VALINT) ? (double)ValToInt(ArrB->items[0]) : ValToFloat(ArrB->items[0]);
        double B1 = (ValKind(ArrB->items[1]) == VALINT) ? (double)ValToInt(ArrB->items[1]) : ValToFloat(ArrB->items[1]);
        double B2 = (ValKind(ArrB->items[2]) == VALINT) ? (double)ValToInt(ArrB->items[2]) : ValToFloat(ArrB->items[2]);
        double D0 = A0 - B0;
        double D1 = A1 - B1;
        double D2 = A2 - B2;
        DistResult = sqrt(D0 * D0 + D1 * D1 + D2 * D2);
      }
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ValFloat(DistResult));
    *stacktopptr = stacktop;
    return 1;
  }

  // Process Environment & Execution Times Telemetry
  if (strcmp(name, "ProcessTelemetryCommandLine") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ProcessTelemetryCommandLine() expects 0 arguments\n");
      return -1;
    }
    const char *CmdLineStr = "";
#ifdef _WIN32
    CmdLineStr = GetCommandLineA();
#endif
    PUSH(ValStr(CmdLineStr ? CmdLineStr : ""));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessTelemetryExecutablePath") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ProcessTelemetryExecutablePath() expects 0 arguments\n");
      return -1;
    }
    char ExecutablePathBuf[512] = {0};
#ifdef _WIN32
    GetModuleFileNameA(NULL, ExecutablePathBuf, sizeof(ExecutablePathBuf));
#endif
    PUSH(ValStr(ExecutablePathBuf));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessTelemetryProcessId") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ProcessTelemetryProcessId() expects 0 arguments\n");
      return -1;
    }
    int64_t CurrentPid = 0;
#ifdef _WIN32
    CurrentPid = (int64_t)GetCurrentProcessId();
#endif
    PUSH(ValInt(CurrentPid));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessTelemetryThreadId") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ProcessTelemetryThreadId() expects 0 arguments\n");
      return -1;
    }
    int64_t CurrentTid = 0;
#ifdef _WIN32
    CurrentTid = (int64_t)GetCurrentThreadId();
#endif
    PUSH(ValInt(CurrentTid));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessTelemetryKernelUserCpuTimes") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ProcessTelemetryKernelUserCpuTimes() expects 0 arguments\n");
      return -1;
    }
    int64_t KernelTimeMs = 0;
    int64_t UserTimeMs = 0;
#ifdef _WIN32
    FILETIME CreationTime, ExitTime, KernelTime, UserTime;
    if (GetProcessTimes(GetCurrentProcess(), &CreationTime, &ExitTime, &KernelTime, &UserTime)) {
      ULARGE_INTEGER KTime, UTime;
      KTime.LowPart = KernelTime.dwLowDateTime;
      KTime.HighPart = KernelTime.dwHighDateTime;
      UTime.LowPart = UserTime.dwLowDateTime;
      UTime.HighPart = UserTime.dwHighDateTime;
      KernelTimeMs = (int64_t)(KTime.QuadPart / 10000);
      UserTimeMs = (int64_t)(UTime.QuadPart / 10000);
    }
#endif
    AKSValue ResStruct = ValStruct("ProcessTelemetryCpuTimesResult");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("kernel_ms");
    StructPtr->fieldvals[0] = ValInt(KernelTimeMs);
    StructPtr->fieldnames[1] = strdup("user_ms");
    StructPtr->fieldvals[1] = ValInt(UserTimeMs);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  // OS slim reader-writer SRW() lock
  if (strcmp(name, "ReadWriteLockNew") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ReadWriteLockNew() expects 0 arguments\n");
      return -1;
    }
    PSRWLOCK LockPtr = (PSRWLOCK)malloc(sizeof(SRWLOCK));
    if (LockPtr) {
      InitializeSRWLock(LockPtr);
    }
    PUSH(ValInt((int64_t)(uintptr_t)LockPtr));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ReadWriteLockLockShared") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ReadWriteLockLockShared() expects 1 argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      AcquireSRWLockShared(LockPtr);
    }
    ValFree(&LockVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ReadWriteLockUnlockShared") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ReadWriteLockUnlockShared() expects 1 argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      ReleaseSRWLockShared(LockPtr);
    }
    ValFree(&LockVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ReadWriteLockLockExclusive") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ReadWriteLockLockExclusive() expects 1 argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      AcquireSRWLockExclusive(LockPtr);
    }
    ValFree(&LockVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ReadWriteLockUnlockExclusive") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ReadWriteLockUnlockExclusive() expects 1 argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      ReleaseSRWLockExclusive(LockPtr);
    }
    ValFree(&LockVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ReadWriteLockTryLockShared") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ReadWriteLockTryLockShared() expects 1 argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    BOOL LockOk = FALSE;
    if (LockPtr) {
      LockOk = TryAcquireSRWLockShared(LockPtr);
    }
    ValFree(&LockVal);
    PUSH(ValBool(LockOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ReadWriteLockTryLockExclusive") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ReadWriteLockTryLockExclusive() expects 1 argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    BOOL LockOk = FALSE;
    if (LockPtr) {
      LockOk = TryAcquireSRWLockExclusive(LockPtr);
    }
    ValFree(&LockVal);
    PUSH(ValBool(LockOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ReadWriteLockDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ReadWriteLockDestroy() expects 1 argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      free(LockPtr);
    }
    ValFree(&LockVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // Cache-Aligned Memory Buffer
  if (strcmp(name, "AlignedBufferAlloc") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: AlignedBufferAlloc() expects 2 arguments (size, alignment)\n");
      return -1;
    }
    AKSValue AlignVal = POP();
    AKSValue SizeVal = POP();
    size_t BufSize = (size_t)ValToInt(SizeVal);
    size_t Alignment = (size_t)ValToInt(AlignVal);
    if (Alignment < 8 || (Alignment & (Alignment - 1)) != 0) {
      Alignment = 64;
    }
    void *MemPtr = _aligned_malloc(BufSize, Alignment);
    if (MemPtr) {
      memset(MemPtr, 0, BufSize);
    }
    ValFree(&SizeVal);
    ValFree(&AlignVal);
    PUSH(ValInt((int64_t)(uintptr_t)MemPtr));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "AlignedBufferWriteBytes") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: AlignedBufferWriteBytes() expects 3 arguments (ptr, offset, hex_str)\n");
      return -1;
    }
    AKSValue HexVal = POP();
    AKSValue OffVal = POP();
    AKSValue PtrVal = POP();
    uint8_t *BufPtr = (uint8_t *)(uintptr_t)ValToInt(PtrVal);
    size_t Offset = (size_t)ValToInt(OffVal);
    if (BufPtr && ValKind(HexVal) == VALSTR) {
      const char *HexData = ValToStrPtr(HexVal)->data;
      size_t HexLen = (size_t)ValToStrPtr(HexVal)->len;
      for (size_t Idx = 0; Idx + 1 < HexLen; Idx += 2) {
        unsigned int ByteVal = 0;
        sscanf(&HexData[Idx], "%02x", &ByteVal);
        BufPtr[Offset + (Idx / 2)] = (uint8_t)ByteVal;
      }
    }
    ValFree(&PtrVal);
    ValFree(&OffVal);
    ValFree(&HexVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "AlignedBufferReadBytes") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: AlignedBufferReadBytes() expects 3 arguments (ptr, offset, count)\n");
      return -1;
    }
    AKSValue CountVal = POP();
    AKSValue OffVal = POP();
    AKSValue PtrVal = POP();
    uint8_t *BufPtr = (uint8_t *)(uintptr_t)ValToInt(PtrVal);
    size_t Offset = (size_t)ValToInt(OffVal);
    size_t ByteCount = (size_t)ValToInt(CountVal);
    if (ByteCount > 2048) ByteCount = 2048;
    char HexOut[4097] = {0};
    if (BufPtr) {
      static const char HexChars[] = "0123456789abcdef";
      for (size_t Idx = 0; Idx < ByteCount; Idx++) {
        HexOut[Idx * 2] = HexChars[(BufPtr[Offset + Idx] >> 4) & 0x0F];
        HexOut[Idx * 2 + 1] = HexChars[BufPtr[Offset + Idx] & 0x0F];
      }
      HexOut[ByteCount * 2] = '\0';
    }
    ValFree(&PtrVal);
    ValFree(&OffVal);
    ValFree(&CountVal);
    PUSH(ValStr(HexOut));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "AlignedBufferIsAligned") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: AlignedBufferIsAligned() expects 2 arguments (ptr, alignment)\n");
      return -1;
    }
    AKSValue AlignVal = POP();
    AKSValue PtrVal = POP();
    uintptr_t RawAddr = (uintptr_t)ValToInt(PtrVal);
    size_t Alignment = (size_t)ValToInt(AlignVal);
    BOOL IsAligned = (Alignment > 0) ? ((RawAddr % Alignment) == 0) : FALSE;
    ValFree(&PtrVal);
    ValFree(&AlignVal);
    PUSH(ValBool(IsAligned ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "AlignedBufferFree") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: AlignedBufferFree() expects 1 argument (ptr)\n");
      return -1;
    }
    AKSValue PtrVal = POP();
    void *MemPtr = (void *)(uintptr_t)ValToInt(PtrVal);
    if (MemPtr) {
      _aligned_free(MemPtr);
    }
    ValFree(&PtrVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware SIMD 256-Bit Integer Vector Processing
  if (strcmp(name, "SimdVec8iAdd") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec8iAdd() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(8);
    AKSArray *ResArr = ValToArr(ResArrVal);
    int32_t DataA[8] = {0}, DataB[8] = {0}, DataRes[8] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 8 && i < ArrA->count; i++) DataA[i] = (int32_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 8 && i < ArrB->count; i++) DataB[i] = (int32_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_add_epi32(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 8; i++) ResArr->items[ResArr->count++] = ValInt(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8iSub") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec8iSub() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(8);
    AKSArray *ResArr = ValToArr(ResArrVal);
    int32_t DataA[8] = {0}, DataB[8] = {0}, DataRes[8] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 8 && i < ArrA->count; i++) DataA[i] = (int32_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 8 && i < ArrB->count; i++) DataB[i] = (int32_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_sub_epi32(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 8; i++) ResArr->items[ResArr->count++] = ValInt(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8iMul") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec8iMul() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(8);
    AKSArray *ResArr = ValToArr(ResArrVal);
    int32_t DataA[8] = {0}, DataB[8] = {0}, DataRes[8] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 8 && i < ArrA->count; i++) DataA[i] = (int32_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 8 && i < ArrB->count; i++) DataB[i] = (int32_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_mullo_epi32(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 8; i++) ResArr->items[ResArr->count++] = ValInt(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8iBitwiseAnd") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec8iBitwiseAnd() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(8);
    AKSArray *ResArr = ValToArr(ResArrVal);
    int32_t DataA[8] = {0}, DataB[8] = {0}, DataRes[8] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 8 && i < ArrA->count; i++) DataA[i] = (int32_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 8 && i < ArrB->count; i++) DataB[i] = (int32_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_and_si256(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 8; i++) ResArr->items[ResArr->count++] = ValInt(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8iBitwiseOr") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec8iBitwiseOr() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(8);
    AKSArray *ResArr = ValToArr(ResArrVal);
    int32_t DataA[8] = {0}, DataB[8] = {0}, DataRes[8] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 8 && i < ArrA->count; i++) DataA[i] = (int32_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 8 && i < ArrB->count; i++) DataB[i] = (int32_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_or_si256(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 8; i++) ResArr->items[ResArr->count++] = ValInt(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec8iBitwiseXor") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec8iBitwiseXor() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(8);
    AKSArray *ResArr = ValToArr(ResArrVal);
    int32_t DataA[8] = {0}, DataB[8] = {0}, DataRes[8] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 8 && i < ArrA->count; i++) DataA[i] = (int32_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 8 && i < ArrB->count; i++) DataB[i] = (int32_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_xor_si256(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 8; i++) ResArr->items[ResArr->count++] = ValInt(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Cryptographic Key Derivation & PBKDF2 Engine
  if (strcmp(name, "CryptoKdfPbkdf2Sha256") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: CryptoKdfPbkdf2Sha256() expects 4 arguments (password, salt, iterations, key_length)\n");
      return -1;
    }
    AKSValue LenVal = POP();
    AKSValue IterVal = POP();
    AKSValue SaltVal = POP();
    AKSValue PwdVal = POP();
    size_t KeyLength = (size_t)ValToInt(LenVal);
    uint64_t Iterations = (uint64_t)ValToInt(IterVal);
    if (KeyLength == 0 || KeyLength > 512) KeyLength = 32;
    if (Iterations == 0) Iterations = 10000;
    char HexResult[1025] = {0};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE AlgHandle = NULL;
    if (BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
      uint8_t DerivedKey[512] = {0};
      const char *PwdStr = (ValKind(PwdVal) == VALSTR) ? ValToStrPtr(PwdVal)->data : "";
      size_t PwdLen = (ValKind(PwdVal) == VALSTR) ? (size_t)ValToStrPtr(PwdVal)->len : 0;
      const char *SaltStr = (ValKind(SaltVal) == VALSTR) ? ValToStrPtr(SaltVal)->data : "";
      size_t SaltLen = (ValKind(SaltVal) == VALSTR) ? (size_t)ValToStrPtr(SaltVal)->len : 0;
      if (BCryptDeriveKeyPBKDF2(AlgHandle, (PUCHAR)PwdStr, (ULONG)PwdLen, (PUCHAR)SaltStr, (ULONG)SaltLen, Iterations, DerivedKey, (ULONG)KeyLength, 0) == 0) {
        static const char HexChars[] = "0123456789abcdef";
        for (size_t i = 0; i < KeyLength; i++) {
          HexResult[i * 2] = HexChars[(DerivedKey[i] >> 4) & 0x0F];
          HexResult[i * 2 + 1] = HexChars[DerivedKey[i] & 0x0F];
        }
        HexResult[KeyLength * 2] = '\0';
      }
      BCryptCloseAlgorithmProvider(AlgHandle, 0);
    }
#endif
    ValFree(&PwdVal);
    ValFree(&SaltVal);
    ValFree(&IterVal);
    ValFree(&LenVal);
    PUSH(ValStr(HexResult));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptoKdfPbkdf2Sha512") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: CryptoKdfPbkdf2Sha512() expects 4 arguments (password, salt, iterations, key_length)\n");
      return -1;
    }
    AKSValue LenVal = POP();
    AKSValue IterVal = POP();
    AKSValue SaltVal = POP();
    AKSValue PwdVal = POP();
    size_t KeyLength = (size_t)ValToInt(LenVal);
    uint64_t Iterations = (uint64_t)ValToInt(IterVal);
    if (KeyLength == 0 || KeyLength > 512) KeyLength = 64;
    if (Iterations == 0) Iterations = 10000;
    char HexResult[1025] = {0};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE AlgHandle = NULL;
    if (BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA512_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
      uint8_t DerivedKey[512] = {0};
      const char *PwdStr = (ValKind(PwdVal) == VALSTR) ? ValToStrPtr(PwdVal)->data : "";
      size_t PwdLen = (ValKind(PwdVal) == VALSTR) ? (size_t)ValToStrPtr(PwdVal)->len : 0;
      const char *SaltStr = (ValKind(SaltVal) == VALSTR) ? ValToStrPtr(SaltVal)->data : "";
      size_t SaltLen = (ValKind(SaltVal) == VALSTR) ? (size_t)ValToStrPtr(SaltVal)->len : 0;
      if (BCryptDeriveKeyPBKDF2(AlgHandle, (PUCHAR)PwdStr, (ULONG)PwdLen, (PUCHAR)SaltStr, (ULONG)SaltLen, Iterations, DerivedKey, (ULONG)KeyLength, 0) == 0) {
        static const char HexChars[] = "0123456789abcdef";
        for (size_t i = 0; i < KeyLength; i++) {
          HexResult[i * 2] = HexChars[(DerivedKey[i] >> 4) & 0x0F];
          HexResult[i * 2 + 1] = HexChars[DerivedKey[i] & 0x0F];
        }
        HexResult[KeyLength * 2] = '\0';
      }
      BCryptCloseAlgorithmProvider(AlgHandle, 0);
    }
#endif
    ValFree(&PwdVal);
    ValFree(&SaltVal);
    ValFree(&IterVal);
    ValFree(&LenVal);
    PUSH(ValStr(HexResult));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptoKdfVerifyPassword") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: CryptoKdfVerifyPassword() expects 4 arguments (password, salt, iterations, expected_hex)\n");
      return -1;
    }
    AKSValue ExpVal = POP();
    AKSValue IterVal = POP();
    AKSValue SaltVal = POP();
    AKSValue PwdVal = POP();
    BOOL VerifyMatch = FALSE;
#ifdef _WIN32
    if (ValKind(ExpVal) == VALSTR) {
      const char *ExpHex = ValToStrPtr(ExpVal)->data;
      size_t ExpLen = (size_t)ValToStrPtr(ExpVal)->len;
      size_t KeyLength = ExpLen / 2;
      uint64_t Iterations = (uint64_t)ValToInt(IterVal);
      if (Iterations == 0) Iterations = 10000;
      BCRYPT_ALG_HANDLE AlgHandle = NULL;
      if (BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
        uint8_t DerivedKey[512] = {0};
        const char *PwdStr = (ValKind(PwdVal) == VALSTR) ? ValToStrPtr(PwdVal)->data : "";
        size_t PwdLen = (ValKind(PwdVal) == VALSTR) ? (size_t)ValToStrPtr(PwdVal)->len : 0;
        const char *SaltStr = (ValKind(SaltVal) == VALSTR) ? ValToStrPtr(SaltVal)->data : "";
        size_t SaltLen = (ValKind(SaltVal) == VALSTR) ? (size_t)ValToStrPtr(SaltVal)->len : 0;
        if (BCryptDeriveKeyPBKDF2(AlgHandle, (PUCHAR)PwdStr, (ULONG)PwdLen, (PUCHAR)SaltStr, (ULONG)SaltLen, Iterations, DerivedKey, (ULONG)KeyLength, 0) == 0) {
          char HexResult[1025] = {0};
          static const char HexChars[] = "0123456789abcdef";
          for (size_t i = 0; i < KeyLength; i++) {
            HexResult[i * 2] = HexChars[(DerivedKey[i] >> 4) & 0x0F];
            HexResult[i * 2 + 1] = HexChars[DerivedKey[i] & 0x0F];
          }
          HexResult[KeyLength * 2] = '\0';
          VerifyMatch = (_stricmp(HexResult, ExpHex) == 0);
        }
        BCryptCloseAlgorithmProvider(AlgHandle, 0);
      }
    }
#endif
    ValFree(&PwdVal);
    ValFree(&SaltVal);
    ValFree(&IterVal);
    ValFree(&ExpVal);
    PUSH(ValBool(VerifyMatch ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // System Display & Multi-Monitor Telemetry
  if (strcmp(name, "SystemDisplayPrimaryResolution") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemDisplayPrimaryResolution() expects 0 arguments\n");
      return -1;
    }
    int ScreenWidth = 1920;
    int ScreenHeight = 1080;
#ifdef _WIN32
    ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
    ScreenHeight = GetSystemMetrics(SM_CYSCREEN);
#endif
    AKSValue ResStruct = ValStruct("DisplayResolution");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("width");
    StructPtr->fieldvals[0] = ValInt(ScreenWidth);
    StructPtr->fieldnames[1] = strdup("height");
    StructPtr->fieldvals[1] = ValInt(ScreenHeight);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemDisplayVirtualScreenBounds") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemDisplayVirtualScreenBounds() expects 0 arguments\n");
      return -1;
    }
    int VirtLeft = 0, VirtTop = 0, VirtWidth = 1920, VirtHeight = 1080;
#ifdef _WIN32
    VirtLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    VirtTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    VirtWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    VirtHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
#endif
    AKSValue ResStruct = ValStruct("DisplayBounds");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 4;
    StructPtr->fieldnames = malloc(4 * sizeof(char *));
    StructPtr->fieldvals = malloc(4 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("left");
    StructPtr->fieldvals[0] = ValInt(VirtLeft);
    StructPtr->fieldnames[1] = strdup("top");
    StructPtr->fieldvals[1] = ValInt(VirtTop);
    StructPtr->fieldnames[2] = strdup("width");
    StructPtr->fieldvals[2] = ValInt(VirtWidth);
    StructPtr->fieldnames[3] = strdup("height");
    StructPtr->fieldvals[3] = ValInt(VirtHeight);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemDisplayWorkArea") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemDisplayWorkArea() expects 0 arguments\n");
      return -1;
    }
    int WorkWidth = 1920, WorkHeight = 1080;
#ifdef _WIN32
    RECT WorkAreaRect;
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &WorkAreaRect, 0)) {
      WorkWidth = WorkAreaRect.right - WorkAreaRect.left;
      WorkHeight = WorkAreaRect.bottom - WorkAreaRect.top;
    }
#endif
    AKSValue ResStruct = ValStruct("DisplayWorkArea");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("width");
    StructPtr->fieldvals[0] = ValInt(WorkWidth);
    StructPtr->fieldnames[1] = strdup("height");
    StructPtr->fieldvals[1] = ValInt(WorkHeight);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemDisplayMonitorCount") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemDisplayMonitorCount() expects 0 arguments\n");
      return -1;
    }
    int MonitorCount = 1;
#ifdef _WIN32
    MonitorCount = GetSystemMetrics(SM_CMONITORS);
    if (MonitorCount <= 0) MonitorCount = 1;
#endif
    PUSH(ValInt(MonitorCount));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Inter-Process Shared Memory Spinlock
  if (strcmp(name, "SharedSpinlockOpenOrCreate") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SharedSpinlockOpenOrCreate() expects 1 argument (name)\n");
      return -1;
    }
    AKSValue NameVal = POP();
    HANDLE MapHandle = NULL;
    void *ViewPtr = NULL;
#ifdef _WIN32
    if (ValKind(NameVal) == VALSTR) {
      const char *NameStr = ValToStrPtr(NameVal)->data;
      MapHandle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 64, NameStr);
      if (MapHandle) {
        ViewPtr = MapViewOfFile(MapHandle, FILE_MAP_ALL_ACCESS, 0, 0, 64);
      }
    }
#endif
    AKSValue ResStruct = ValStruct("SharedSpinlockHandle");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("map_id");
    StructPtr->fieldvals[0] = ValInt((int64_t)(uintptr_t)MapHandle);
    StructPtr->fieldnames[1] = strdup("view_id");
    StructPtr->fieldvals[1] = ValInt((int64_t)(uintptr_t)ViewPtr);
    ValFree(&NameVal);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SharedSpinlockAcquire") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SharedSpinlockAcquire() expects 2 arguments (view_ptr, max_spins)\n");
      return -1;
    }
    AKSValue SpinVal = POP();
    AKSValue ViewVal = POP();
    BOOL Acquired = FALSE;
#ifdef _WIN32
    volatile LONG *LockPtr = (volatile LONG *)(uintptr_t)ValToInt(ViewVal);
    int MaxSpins = (int)ValToInt(SpinVal);
    if (MaxSpins <= 0) MaxSpins = 1000000;
    if (LockPtr) {
      for (int Spin = 0; Spin < MaxSpins; Spin++) {
        if (*LockPtr == 0 && InterlockedExchange(LockPtr, 1) == 0) {
          Acquired = TRUE;
          break;
        }
        _mm_pause();
      }
    }
#endif
    ValFree(&ViewVal);
    ValFree(&SpinVal);
    PUSH(ValBool(Acquired ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SharedSpinlockTryAcquire") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SharedSpinlockTryAcquire() expects 1 argument (view_ptr)\n");
      return -1;
    }
    AKSValue ViewVal = POP();
    BOOL Acquired = FALSE;
#ifdef _WIN32
    volatile LONG *LockPtr = (volatile LONG *)(uintptr_t)ValToInt(ViewVal);
    if (LockPtr) {
      Acquired = (InterlockedExchange(LockPtr, 1) == 0);
    }
#endif
    ValFree(&ViewVal);
    PUSH(ValBool(Acquired ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SharedSpinlockRelease") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SharedSpinlockRelease() expects 1 argument (view_ptr)\n");
      return -1;
    }
    AKSValue ViewVal = POP();
#ifdef _WIN32
    volatile LONG *LockPtr = (volatile LONG *)(uintptr_t)ValToInt(ViewVal);
    if (LockPtr) {
      InterlockedExchange(LockPtr, 0);
    }
#endif
    ValFree(&ViewVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SharedSpinlockDestroy") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SharedSpinlockDestroy() expects 2 arguments (map_id, view_id)\n");
      return -1;
    }
    AKSValue ViewVal = POP();
    AKSValue MapVal = POP();
#ifdef _WIN32
    HANDLE MapHandle = (HANDLE)(uintptr_t)ValToInt(MapVal);
    void *ViewPtr = (void *)(uintptr_t)ValToInt(ViewVal);
    if (ViewPtr) {
      UnmapViewOfFile(ViewPtr);
    }
    if (MapHandle && MapHandle != INVALID_HANDLE_VALUE) {
      CloseHandle(MapHandle);
    }
#endif
    ValFree(&MapVal);
    ValFree(&ViewVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Thread Barrier Synchronization
  if (strcmp(name, "ThreadBarrierNew") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadBarrierNew() expects 1 argument (total_threads)\n");
      return -1;
    }
    AKSValue CountVal = POP();
    PSYNCHRONIZATION_BARRIER BarrierPtr = NULL;
#ifdef _WIN32
    DWORD TotalThreads = (DWORD)ValToInt(CountVal);
    if (TotalThreads < 1) TotalThreads = 1;
    BarrierPtr = (PSYNCHRONIZATION_BARRIER)malloc(sizeof(SYNCHRONIZATION_BARRIER));
    if (BarrierPtr) {
      InitializeSynchronizationBarrier(BarrierPtr, TotalThreads, -1);
    }
#endif
    ValFree(&CountVal);
    PUSH(ValInt((int64_t)(uintptr_t)BarrierPtr));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadBarrierWait") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadBarrierWait() expects 1 argument (barrier)\n");
      return -1;
    }
    AKSValue BarrierVal = POP();
    BOOL IsLeader = FALSE;
#ifdef _WIN32
    PSYNCHRONIZATION_BARRIER BarrierPtr = (PSYNCHRONIZATION_BARRIER)(uintptr_t)ValToInt(BarrierVal);
    if (BarrierPtr) {
      IsLeader = EnterSynchronizationBarrier(BarrierPtr, 0);
    }
#endif
    ValFree(&BarrierVal);
    PUSH(ValBool(IsLeader ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadBarrierDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadBarrierDestroy() expects 1 argument (barrier)\n");
      return -1;
    }
    AKSValue BarrierVal = POP();
#ifdef _WIN32
    PSYNCHRONIZATION_BARRIER BarrierPtr = (PSYNCHRONIZATION_BARRIER)(uintptr_t)ValToInt(BarrierVal);
    if (BarrierPtr) {
      DeleteSynchronizationBarrier(BarrierPtr);
      free(BarrierPtr);
    }
#endif
    ValFree(&BarrierVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Process Job Objects & Resource Limiting
  if (strcmp(name, "JobObjectCreate") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: JobObjectCreate() expects 1 argument (name)\n");
      return -1;
    }
    AKSValue NameVal = POP();
    HANDLE JobHandle = NULL;
#ifdef _WIN32
    const char *NameStr = (ValKind(NameVal) == VALSTR) ? ValToStrPtr(NameVal)->data : NULL;
    JobHandle = CreateJobObjectA(NULL, NameStr);
#endif
    ValFree(&NameVal);
    PUSH(ValInt((int64_t)(uintptr_t)JobHandle));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "JobObjectAssignProcess") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: JobObjectAssignProcess() expects 2 arguments (job, pid)\n");
      return -1;
    }
    AKSValue PidVal = POP();
    AKSValue JobVal = POP();
    BOOL AssignOk = FALSE;
#ifdef _WIN32
    HANDLE JobHandle = (HANDLE)(uintptr_t)ValToInt(JobVal);
    DWORD Pid = (DWORD)ValToInt(PidVal);
    if (Pid == 0) Pid = GetCurrentProcessId();
    HANDLE ProcHandle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, Pid);
    if (ProcHandle && JobHandle) {
      AssignOk = AssignProcessToJobObject(JobHandle, ProcHandle);
      CloseHandle(ProcHandle);
    }
#endif
    ValFree(&JobVal);
    ValFree(&PidVal);
    PUSH(ValBool(AssignOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "JobObjectSetMemoryLimit") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: JobObjectSetMemoryLimit() expects 2 arguments (job, max_bytes)\n");
      return -1;
    }
    AKSValue BytesVal = POP();
    AKSValue JobVal = POP();
    BOOL LimitOk = FALSE;
#ifdef _WIN32
    HANDLE JobHandle = (HANDLE)(uintptr_t)ValToInt(JobVal);
    size_t MaxBytes = (size_t)ValToInt(BytesVal);
    if (JobHandle) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION LimitInfo = {0};
      LimitInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
      LimitInfo.ProcessMemoryLimit = MaxBytes;
      LimitOk = SetInformationJobObject(JobHandle, JobObjectExtendedLimitInformation, &LimitInfo, sizeof(LimitInfo));
    }
#endif
    ValFree(&JobVal);
    ValFree(&BytesVal);
    PUSH(ValBool(LimitOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "JobObjectTerminateAll") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: JobObjectTerminateAll() expects 2 arguments (job, exit_code)\n");
      return -1;
    }
    AKSValue CodeVal = POP();
    AKSValue JobVal = POP();
    BOOL TermOk = FALSE;
#ifdef _WIN32
    HANDLE JobHandle = (HANDLE)(uintptr_t)ValToInt(JobVal);
    UINT ExitCode = (UINT)ValToInt(CodeVal);
    if (JobHandle) {
      TermOk = TerminateJobObject(JobHandle, ExitCode);
    }
#endif
    ValFree(&JobVal);
    ValFree(&CodeVal);
    PUSH(ValBool(TermOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "JobObjectDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: JobObjectDestroy() expects 1 argument (job)\n");
      return -1;
    }
    AKSValue JobVal = POP();
#ifdef _WIN32
    HANDLE JobHandle = (HANDLE)(uintptr_t)ValToInt(JobVal);
    if (JobHandle && JobHandle != INVALID_HANDLE_VALUE) {
      CloseHandle(JobHandle);
    }
#endif
    ValFree(&JobVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // Inter-Process Shared Circular Ring Buffer
  if (strcmp(name, "SharedRingBufferCreate") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SharedRingBufferCreate() expects 2 arguments (name, capacity_bytes)\n");
      return -1;
    }
    AKSValue CapVal = POP();
    AKSValue NameVal = POP();
    uint32_t Capacity = (uint32_t)ValToInt(CapVal);
    if (Capacity < 64) Capacity = 64;
    size_t TotalSize = sizeof(uint32_t) * 4 + Capacity;
    HANDLE MapHandle = NULL;
    void *ViewPtr = NULL;
#ifdef _WIN32
    const char *NameStr = (ValKind(NameVal) == VALSTR) ? ValToStrPtr(NameVal)->data : "Local\\AksRingBuf";
    MapHandle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)TotalSize, NameStr);
    if (MapHandle) {
      ViewPtr = MapViewOfFile(MapHandle, FILE_MAP_ALL_ACCESS, 0, 0, TotalSize);
      if (ViewPtr) {
        uint32_t *Hdr = (uint32_t *)ViewPtr;
        if (Hdr[2] == 0) {
          Hdr[0] = 0; // head
          Hdr[1] = 0; // tail
          Hdr[2] = Capacity;
        }
      }
    }
#endif
    AKSValue ResStruct = ValStruct("SharedRingBufferHandle");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 3;
    StructPtr->fieldnames = malloc(3 * sizeof(char *));
    StructPtr->fieldvals = malloc(3 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("map_id");
    StructPtr->fieldvals[0] = ValInt((int64_t)(uintptr_t)MapHandle);
    StructPtr->fieldnames[1] = strdup("view_id");
    StructPtr->fieldvals[1] = ValInt((int64_t)(uintptr_t)ViewPtr);
    StructPtr->fieldnames[2] = strdup("capacity");
    StructPtr->fieldvals[2] = ValInt(Capacity);
    ValFree(&NameVal);
    ValFree(&CapVal);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SharedRingBufferWrite") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SharedRingBufferWrite() expects 2 arguments (view_ptr, hex_str)\n");
      return -1;
    }
    AKSValue HexVal = POP();
    AKSValue ViewVal = POP();
    size_t BytesWritten = 0;
#ifdef _WIN32
    void *ViewPtr = (void *)(uintptr_t)ValToInt(ViewVal);
    if (ViewPtr && ValKind(HexVal) == VALSTR) {
      uint32_t *Hdr = (uint32_t *)ViewPtr;
      uint32_t Head = Hdr[0];
      uint32_t Tail = Hdr[1];
      uint32_t Capacity = Hdr[2];
      uint8_t *Buffer = (uint8_t *)(Hdr + 4);
      uint32_t FreeSpace = (Head > Tail) ? (Head - Tail - 1) : (Capacity - (Tail - Head) - 1);
      const char *HexData = ValToStrPtr(HexVal)->data;
      size_t HexLen = (size_t)ValToStrPtr(HexVal)->len;
      size_t InputBytes = HexLen / 2;
      size_t ToWrite = (InputBytes < FreeSpace) ? InputBytes : FreeSpace;
      for (size_t i = 0; i < ToWrite; i++) {
        unsigned int ByteVal = 0;
        sscanf(&HexData[i * 2], "%02x", &ByteVal);
        Buffer[Tail] = (uint8_t)ByteVal;
        Tail = (Tail + 1) % Capacity;
      }
      Hdr[1] = Tail;
      BytesWritten = ToWrite;
    }
#endif
    ValFree(&ViewVal);
    ValFree(&HexVal);
    PUSH(ValInt((int64_t)BytesWritten));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SharedRingBufferRead") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SharedRingBufferRead() expects 2 arguments (view_ptr, max_bytes)\n");
      return -1;
    }
    AKSValue MaxVal = POP();
    AKSValue ViewVal = POP();
    char HexOut[4097] = {0};
#ifdef _WIN32
    void *ViewPtr = (void *)(uintptr_t)ValToInt(ViewVal);
    size_t MaxBytes = (size_t)ValToInt(MaxVal);
    if (MaxBytes > 2048) MaxBytes = 2048;
    if (ViewPtr) {
      uint32_t *Hdr = (uint32_t *)ViewPtr;
      uint32_t Head = Hdr[0];
      uint32_t Tail = Hdr[1];
      uint32_t Capacity = Hdr[2];
      uint8_t *Buffer = (uint8_t *)(Hdr + 4);
      uint32_t Available = (Tail >= Head) ? (Tail - Head) : (Capacity - (Head - Tail));
      size_t ToRead = (MaxBytes < Available) ? MaxBytes : Available;
      static const char HexChars[] = "0123456789abcdef";
      for (size_t i = 0; i < ToRead; i++) {
        uint8_t B = Buffer[Head];
        HexOut[i * 2] = HexChars[(B >> 4) & 0x0F];
        HexOut[i * 2 + 1] = HexChars[B & 0x0F];
        Head = (Head + 1) % Capacity;
      }
      HexOut[ToRead * 2] = '\0';
      Hdr[0] = Head;
    }
#endif
    ValFree(&ViewVal);
    ValFree(&MaxVal);
    PUSH(ValStr(HexOut));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SharedRingBufferAvailableRead") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SharedRingBufferAvailableRead() expects 1 argument (view_ptr)\n");
      return -1;
    }
    AKSValue ViewVal = POP();
    uint32_t Available = 0;
#ifdef _WIN32
    void *ViewPtr = (void *)(uintptr_t)ValToInt(ViewVal);
    if (ViewPtr) {
      uint32_t *Hdr = (uint32_t *)ViewPtr;
      uint32_t Head = Hdr[0];
      uint32_t Tail = Hdr[1];
      uint32_t Capacity = Hdr[2];
      Available = (Tail >= Head) ? (Tail - Head) : (Capacity - (Head - Tail));
    }
#endif
    ValFree(&ViewVal);
    PUSH(ValInt(Available));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SharedRingBufferAvailableWrite") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SharedRingBufferAvailableWrite() expects 1 argument (view_ptr)\n");
      return -1;
    }
    AKSValue ViewVal = POP();
    uint32_t FreeSpace = 0;
#ifdef _WIN32
    void *ViewPtr = (void *)(uintptr_t)ValToInt(ViewVal);
    if (ViewPtr) {
      uint32_t *Hdr = (uint32_t *)ViewPtr;
      uint32_t Head = Hdr[0];
      uint32_t Tail = Hdr[1];
      uint32_t Capacity = Hdr[2];
      FreeSpace = (Head > Tail) ? (Head - Tail - 1) : (Capacity - (Tail - Head) - 1);
    }
#endif
    ValFree(&ViewVal);
    PUSH(ValInt(FreeSpace));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SharedRingBufferDestroy") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SharedRingBufferDestroy() expects 2 arguments (map_id, view_id)\n");
      return -1;
    }
    AKSValue ViewVal = POP();
    AKSValue MapVal = POP();
#ifdef _WIN32
    HANDLE MapHandle = (HANDLE)(uintptr_t)ValToInt(MapVal);
    void *ViewPtr = (void *)(uintptr_t)ValToInt(ViewVal);
    if (ViewPtr) UnmapViewOfFile(ViewPtr);
    if (MapHandle && MapHandle != INVALID_HANDLE_VALUE) CloseHandle(MapHandle);
#endif
    ValFree(&MapVal);
    ValFree(&ViewVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS cryptographic keyed MAC engine
  if (strcmp(name, "CryptoHmacSha256") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoHmacSha256() expects 2 arguments (key, data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    AKSValue KeyVal = POP();
    char HexResult[65] = {0};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE AlgHandle = NULL;
    if (BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
      BCRYPT_HASH_HANDLE HashHandle = NULL;
      const char *KeyStr = (ValKind(KeyVal) == VALSTR) ? ValToStrPtr(KeyVal)->data : "";
      size_t KeyLen = (ValKind(KeyVal) == VALSTR) ? (size_t)ValToStrPtr(KeyVal)->len : 0;
      const char *DataStr = (ValKind(DataVal) == VALSTR) ? ValToStrPtr(DataVal)->data : "";
      size_t DataLen = (ValKind(DataVal) == VALSTR) ? (size_t)ValToStrPtr(DataVal)->len : 0;
      if (BCryptCreateHash(AlgHandle, &HashHandle, NULL, 0, (PUCHAR)KeyStr, (ULONG)KeyLen, 0) == 0) {
        BCryptHashData(HashHandle, (PUCHAR)DataStr, (ULONG)DataLen, 0);
        uint8_t HashBuf[32] = {0};
        BCryptFinishHash(HashHandle, HashBuf, 32, 0);
        BCryptDestroyHash(HashHandle);
        static const char HexChars[] = "0123456789abcdef";
        for (int i = 0; i < 32; i++) {
          HexResult[i * 2] = HexChars[(HashBuf[i] >> 4) & 0x0F];
          HexResult[i * 2 + 1] = HexChars[HashBuf[i] & 0x0F];
        }
        HexResult[64] = '\0';
      }
      BCryptCloseAlgorithmProvider(AlgHandle, 0);
    }
#endif
    ValFree(&KeyVal);
    ValFree(&DataVal);
    PUSH(ValStr(HexResult));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptoHmacSha512") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoHmacSha512() expects 2 arguments (key, data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    AKSValue KeyVal = POP();
    char HexResult[129] = {0};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE AlgHandle = NULL;
    if (BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA512_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
      BCRYPT_HASH_HANDLE HashHandle = NULL;
      const char *KeyStr = (ValKind(KeyVal) == VALSTR) ? ValToStrPtr(KeyVal)->data : "";
      size_t KeyLen = (ValKind(KeyVal) == VALSTR) ? (size_t)ValToStrPtr(KeyVal)->len : 0;
      const char *DataStr = (ValKind(DataVal) == VALSTR) ? ValToStrPtr(DataVal)->data : "";
      size_t DataLen = (ValKind(DataVal) == VALSTR) ? (size_t)ValToStrPtr(DataVal)->len : 0;
      if (BCryptCreateHash(AlgHandle, &HashHandle, NULL, 0, (PUCHAR)KeyStr, (ULONG)KeyLen, 0) == 0) {
        BCryptHashData(HashHandle, (PUCHAR)DataStr, (ULONG)DataLen, 0);
        uint8_t HashBuf[64] = {0};
        BCryptFinishHash(HashHandle, HashBuf, 64, 0);
        BCryptDestroyHash(HashHandle);
        static const char HexChars[] = "0123456789abcdef";
        for (int i = 0; i < 64; i++) {
          HexResult[i * 2] = HexChars[(HashBuf[i] >> 4) & 0x0F];
          HexResult[i * 2 + 1] = HexChars[HashBuf[i] & 0x0F];
        }
        HexResult[128] = '\0';
      }
      BCryptCloseAlgorithmProvider(AlgHandle, 0);
    }
#endif
    ValFree(&KeyVal);
    ValFree(&DataVal);
    PUSH(ValStr(HexResult));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptoHmacVerify") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: CryptoHmacVerify() expects 3 arguments (key, data, expected_hex)\n");
      return -1;
    }
    AKSValue ExpVal = POP();
    AKSValue DataVal = POP();
    AKSValue KeyVal = POP();
    BOOL VerifyOk = FALSE;
#ifdef _WIN32
    if (ValKind(ExpVal) == VALSTR) {
      const char *ExpHex = ValToStrPtr(ExpVal)->data;
      BCRYPT_ALG_HANDLE AlgHandle = NULL;
      if (BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
        BCRYPT_HASH_HANDLE HashHandle = NULL;
        const char *KeyStr = (ValKind(KeyVal) == VALSTR) ? ValToStrPtr(KeyVal)->data : "";
        size_t KeyLen = (ValKind(KeyVal) == VALSTR) ? (size_t)ValToStrPtr(KeyVal)->len : 0;
        const char *DataStr = (ValKind(DataVal) == VALSTR) ? ValToStrPtr(DataVal)->data : "";
        size_t DataLen = (ValKind(DataVal) == VALSTR) ? (size_t)ValToStrPtr(DataVal)->len : 0;
        if (BCryptCreateHash(AlgHandle, &HashHandle, NULL, 0, (PUCHAR)KeyStr, (ULONG)KeyLen, 0) == 0) {
          BCryptHashData(HashHandle, (PUCHAR)DataStr, (ULONG)DataLen, 0);
          uint8_t HashBuf[32] = {0};
          BCryptFinishHash(HashHandle, HashBuf, 32, 0);
          BCryptDestroyHash(HashHandle);
          char HexResult[65] = {0};
          static const char HexChars[] = "0123456789abcdef";
          for (int i = 0; i < 32; i++) {
            HexResult[i * 2] = HexChars[(HashBuf[i] >> 4) & 0x0F];
            HexResult[i * 2 + 1] = HexChars[HashBuf[i] & 0x0F];
          }
          HexResult[64] = '\0';
          VerifyOk = (_stricmp(HexResult, ExpHex) == 0);
        }
        BCryptCloseAlgorithmProvider(AlgHandle, 0);
      }
    }
#endif
    ValFree(&KeyVal);
    ValFree(&DataVal);
    ValFree(&ExpVal);
    PUSH(ValBool(VerifyOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware SIMD 256-Bit Double-Precision Vector Math
  if (strcmp(name, "SimdVec4dAdd") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec4dAdd() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(4);
    AKSArray *ResArr = ValToArr(ResArrVal);
    double DataA[4] = {0}, DataB[4] = {0}, DataRes[4] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 4 && i < ArrA->count; i++) DataA[i] = ValToFloat(ArrA->items[i]);
      for (int i = 0; i < 4 && i < ArrB->count; i++) DataB[i] = ValToFloat(ArrB->items[i]);
#ifdef _WIN32
      __m256d Va = _mm256_loadu_pd(DataA);
      __m256d Vb = _mm256_loadu_pd(DataB);
      __m256d Vres = _mm256_add_pd(Va, Vb);
      _mm256_storeu_pd(DataRes, Vres);
#endif
      for (int i = 0; i < 4; i++) ResArr->items[ResArr->count++] = ValFloat(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec4dSub") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec4dSub() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(4);
    AKSArray *ResArr = ValToArr(ResArrVal);
    double DataA[4] = {0}, DataB[4] = {0}, DataRes[4] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 4 && i < ArrA->count; i++) DataA[i] = ValToFloat(ArrA->items[i]);
      for (int i = 0; i < 4 && i < ArrB->count; i++) DataB[i] = ValToFloat(ArrB->items[i]);
#ifdef _WIN32
      __m256d Va = _mm256_loadu_pd(DataA);
      __m256d Vb = _mm256_loadu_pd(DataB);
      __m256d Vres = _mm256_sub_pd(Va, Vb);
      _mm256_storeu_pd(DataRes, Vres);
#endif
      for (int i = 0; i < 4; i++) ResArr->items[ResArr->count++] = ValFloat(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec4dMul") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec4dMul() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(4);
    AKSArray *ResArr = ValToArr(ResArrVal);
    double DataA[4] = {0}, DataB[4] = {0}, DataRes[4] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 4 && i < ArrA->count; i++) DataA[i] = ValToFloat(ArrA->items[i]);
      for (int i = 0; i < 4 && i < ArrB->count; i++) DataB[i] = ValToFloat(ArrB->items[i]);
#ifdef _WIN32
      __m256d Va = _mm256_loadu_pd(DataA);
      __m256d Vb = _mm256_loadu_pd(DataB);
      __m256d Vres = _mm256_mul_pd(Va, Vb);
      _mm256_storeu_pd(DataRes, Vres);
#endif
      for (int i = 0; i < 4; i++) ResArr->items[ResArr->count++] = ValFloat(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec4dDiv") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec4dDiv() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(4);
    AKSArray *ResArr = ValToArr(ResArrVal);
    double DataA[4] = {0}, DataB[4] = {0}, DataRes[4] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 4 && i < ArrA->count; i++) DataA[i] = ValToFloat(ArrA->items[i]);
      for (int i = 0; i < 4 && i < ArrB->count; i++) DataB[i] = ValToFloat(ArrB->items[i]);
#ifdef _WIN32
      __m256d Va = _mm256_loadu_pd(DataA);
      __m256d Vb = _mm256_loadu_pd(DataB);
      __m256d Vres = _mm256_div_pd(Va, Vb);
      _mm256_storeu_pd(DataRes, Vres);
#endif
      for (int i = 0; i < 4; i++) ResArr->items[ResArr->count++] = ValFloat(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec4dSqrt") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdVec4dSqrt() expects 1 argument (vec)\n");
      return -1;
    }
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(4);
    AKSArray *ResArr = ValToArr(ResArrVal);
    double DataA[4] = {0}, DataRes[4] = {0};
    if (ValKind(AVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      for (int i = 0; i < 4 && i < ArrA->count; i++) DataA[i] = ValToFloat(ArrA->items[i]);
#ifdef _WIN32
      __m256d Va = _mm256_loadu_pd(DataA);
      __m256d Vres = _mm256_sqrt_pd(Va);
      _mm256_storeu_pd(DataRes, Vres);
#endif
      for (int i = 0; i < 4; i++) ResArr->items[ResArr->count++] = ValFloat(DataRes[i]);
    }
    ValFree(&AVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec4dDot") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec4dDot() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    double DotProduct = 0.0;
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 4 && i < ArrA->count && i < ArrB->count; i++) {
        DotProduct += ValToFloat(ArrA->items[i]) * ValToFloat(ArrB->items[i]);
      }
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ValFloat(DotProduct));
    *stacktopptr = stacktop;
    return 1;
  }

  // System Security Token & User Identity Telemetry
  if (strcmp(name, "SystemIdentityUserName") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemIdentityUserName() expects 0 arguments\n");
      return -1;
    }
    char UserBuf[256] = {0};
#ifdef _WIN32
    DWORD UserLen = sizeof(UserBuf);
    GetUserNameA(UserBuf, &UserLen);
#endif
    PUSH(ValStr(UserBuf));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemIdentityIsAdmin") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemIdentityIsAdmin() expects 0 arguments\n");
      return -1;
    }
    BOOL IsAdmin = FALSE;
#ifdef _WIN32
    IsAdmin = IsUserAnAdmin();
#endif
    PUSH(ValBool(IsAdmin ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemIdentityProcessIntegrityLevel") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemIdentityProcessIntegrityLevel() expects 0 arguments\n");
      return -1;
    }
    const char *IntegrityStr = "Medium";
#ifdef _WIN32
    HANDLE TokenHandle = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &TokenHandle)) {
      DWORD LengthNeeded = 0;
      GetTokenInformation(TokenHandle, TokenIntegrityLevel, NULL, 0, &LengthNeeded);
      if (LengthNeeded > 0) {
        PTOKEN_MANDATORY_LABEL Label = (PTOKEN_MANDATORY_LABEL)malloc(LengthNeeded);
        if (Label && GetTokenInformation(TokenHandle, TokenIntegrityLevel, Label, LengthNeeded, &LengthNeeded)) {
          DWORD SubAuthorityCount = *GetSidSubAuthorityCount(Label->Label.Sid);
          DWORD IntegrityLevel = *GetSidSubAuthority(Label->Label.Sid, SubAuthorityCount - 1);
          if (IntegrityLevel >= SECURITY_MANDATORY_SYSTEM_RID) IntegrityStr = "System";
          else if (IntegrityLevel >= SECURITY_MANDATORY_HIGH_RID) IntegrityStr = "High";
          else if (IntegrityLevel >= SECURITY_MANDATORY_MEDIUM_RID) IntegrityStr = "Medium";
          else IntegrityStr = "Low";
        }
        if (Label) free(Label);
      }
      CloseHandle(TokenHandle);
    }
#endif
    PUSH(ValStr(IntegrityStr));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Condition Variable & Thread Rendezvous
  if (strcmp(name, "ConditionVariableNew") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ConditionVariableNew() expects 0 arguments\n");
      return -1;
    }
    PCONDITION_VARIABLE CvPtr = NULL;
#ifdef _WIN32
    CvPtr = (PCONDITION_VARIABLE)malloc(sizeof(CONDITION_VARIABLE));
    if (CvPtr) {
      InitializeConditionVariable(CvPtr);
    }
#endif
    PUSH(ValInt((int64_t)(uintptr_t)CvPtr));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ConditionVariableWaitSRW") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: ConditionVariableWaitSRW() expects 4 arguments (cv, lock, timeout_ms, is_shared)\n");
      return -1;
    }
    AKSValue SharedVal = POP();
    AKSValue TimeoutVal = POP();
    AKSValue LockVal = POP();
    AKSValue CvVal = POP();
    BOOL WaitOk = FALSE;
#ifdef _WIN32
    PCONDITION_VARIABLE CvPtr = (PCONDITION_VARIABLE)(uintptr_t)ValToInt(CvVal);
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    DWORD Timeout = (DWORD)ValToInt(TimeoutVal);
    ULONG Flags = ValToBool(SharedVal) ? CONDITION_VARIABLE_LOCKMODE_SHARED : 0;
    if (CvPtr && LockPtr) {
      WaitOk = SleepConditionVariableSRW(CvPtr, LockPtr, Timeout, Flags);
    }
#endif
    ValFree(&CvVal);
    ValFree(&LockVal);
    ValFree(&TimeoutVal);
    ValFree(&SharedVal);
    PUSH(ValBool(WaitOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ConditionVariableNotifyOne") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ConditionVariableNotifyOne() expects 1 argument (cv)\n");
      return -1;
    }
    AKSValue CvVal = POP();
#ifdef _WIN32
    PCONDITION_VARIABLE CvPtr = (PCONDITION_VARIABLE)(uintptr_t)ValToInt(CvVal);
    if (CvPtr) {
      WakeConditionVariable(CvPtr);
    }
#endif
    ValFree(&CvVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ConditionVariableNotifyAll") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ConditionVariableNotifyAll() expects 1 argument (cv)\n");
      return -1;
    }
    AKSValue CvVal = POP();
#ifdef _WIN32
    PCONDITION_VARIABLE CvPtr = (PCONDITION_VARIABLE)(uintptr_t)ValToInt(CvVal);
    if (CvPtr) {
      WakeAllConditionVariable(CvPtr);
    }
#endif
    ValFree(&CvVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ConditionVariableDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ConditionVariableDestroy() expects 1 argument (cv)\n");
      return -1;
    }
    AKSValue CvVal = POP();
#ifdef _WIN32
    PCONDITION_VARIABLE CvPtr = (PCONDITION_VARIABLE)(uintptr_t)ValToInt(CvVal);
    if (CvPtr) {
      free(CvPtr);
    }
#endif
    ValFree(&CvVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Process Priority Class & Power Throttling
  if (strcmp(name, "ProcessPrioritySetClass") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessPrioritySetClass() expects 1 argument (priority_str)\n");
      return -1;
    }
    AKSValue PrioVal = POP();
    BOOL SetOk = FALSE;
#ifdef _WIN32
    if (ValKind(PrioVal) == VALSTR) {
      const char *PrioStr = ValToStrPtr(PrioVal)->data;
      DWORD Class = NORMAL_PRIORITY_CLASS;
      if (_stricmp(PrioStr, "idle") == 0) Class = IDLE_PRIORITY_CLASS;
      else if (_stricmp(PrioStr, "below_normal") == 0) Class = BELOW_NORMAL_PRIORITY_CLASS;
      else if (_stricmp(PrioStr, "normal") == 0) Class = NORMAL_PRIORITY_CLASS;
      else if (_stricmp(PrioStr, "above_normal") == 0) Class = ABOVE_NORMAL_PRIORITY_CLASS;
      else if (_stricmp(PrioStr, "high") == 0) Class = HIGH_PRIORITY_CLASS;
      else if (_stricmp(PrioStr, "realtime") == 0) Class = REALTIME_PRIORITY_CLASS;
      SetOk = SetPriorityClass(GetCurrentProcess(), Class);
    }
#endif
    ValFree(&PrioVal);
    PUSH(ValBool(SetOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessPriorityGetClass") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ProcessPriorityGetClass() expects 0 arguments\n");
      return -1;
    }
    const char *PrioStr = "normal";
#ifdef _WIN32
    DWORD Class = GetPriorityClass(GetCurrentProcess());
    if (Class == IDLE_PRIORITY_CLASS) PrioStr = "idle";
    else if (Class == BELOW_NORMAL_PRIORITY_CLASS) PrioStr = "below_normal";
    else if (Class == NORMAL_PRIORITY_CLASS) PrioStr = "normal";
    else if (Class == ABOVE_NORMAL_PRIORITY_CLASS) PrioStr = "above_normal";
    else if (Class == HIGH_PRIORITY_CLASS) PrioStr = "high";
    else if (Class == REALTIME_PRIORITY_CLASS) PrioStr = "realtime";
#endif
    PUSH(ValStr(PrioStr));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessPrioritySetEcoMode") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessPrioritySetEcoMode() expects 1 argument (enabled)\n");
      return -1;
    }
    AKSValue EcoVal = POP();
    BOOL EcoOk = TRUE;
#ifdef _WIN32
    BOOL Enable = ValToBool(EcoVal);
    PROCESS_POWER_THROTTLING_STATE ThrottlingState = {0};
    ThrottlingState.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    ThrottlingState.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    ThrottlingState.StateMask = Enable ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &ThrottlingState, sizeof(ThrottlingState));
#endif
    ValFree(&EcoVal);
    PUSH(ValBool(EcoOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // SIMD Fast Memory & Substring Pattern Scanner
  if (strcmp(name, "FastMemSearchFindByte") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMemSearchFindByte() expects 2 arguments (hex_data, byte_val)\n");
      return -1;
    }
    AKSValue ByteVal = POP();
    AKSValue DataVal = POP();
    int64_t FoundOffset = -1;
    if (ValKind(DataVal) == VALSTR) {
      const char *HexData = ValToStrPtr(DataVal)->data;
      size_t HexLen = (size_t)ValToStrPtr(DataVal)->len;
      size_t ByteCount = HexLen / 2;
      uint8_t TargetByte = (uint8_t)ValToInt(ByteVal);
      if (ByteCount > 0) {
        uint8_t *Buf = (uint8_t *)malloc(ByteCount);
        if (Buf) {
          for (size_t i = 0; i < ByteCount; i++) {
            unsigned int B = 0;
            sscanf(&HexData[i * 2], "%02x", &B);
            Buf[i] = (uint8_t)B;
          }
          size_t Idx = 0;
#ifdef _WIN32
          __m256i TargetVec = _mm256_set1_epi8((char)TargetByte);
          for (; Idx + 32 <= ByteCount; Idx += 32) {
            __m256i Chunk = _mm256_loadu_si256((const __m256i *)(Buf + Idx));
            __m256i Cmp = _mm256_cmpeq_epi8(Chunk, TargetVec);
            int Mask = _mm256_movemask_epi8(Cmp);
            if (Mask != 0) {
              unsigned long BitIdx = 0;
              _BitScanForward(&BitIdx, (unsigned long)Mask);
              FoundOffset = (int64_t)(Idx + BitIdx);
              break;
            }
          }
#endif
          if (FoundOffset == -1) {
            for (; Idx < ByteCount; Idx++) {
              if (Buf[Idx] == TargetByte) {
                FoundOffset = (int64_t)Idx;
                break;
              }
            }
          }
          free(Buf);
        }
      }
    }
    ValFree(&DataVal);
    ValFree(&ByteVal);
    PUSH(ValInt(FoundOffset));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastMemSearchFindSequence") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMemSearchFindSequence() expects 2 arguments (hex_haystack, hex_needle)\n");
      return -1;
    }
    AKSValue NeedleVal = POP();
    AKSValue HayVal = POP();
    int64_t FoundOffset = -1;
    if (ValKind(HayVal) == VALSTR && ValKind(NeedleVal) == VALSTR) {
      const char *HayHex = ValToStrPtr(HayVal)->data;
      size_t HayLen = (size_t)ValToStrPtr(HayVal)->len / 2;
      const char *NeedleHex = ValToStrPtr(NeedleVal)->data;
      size_t NeedleLen = (size_t)ValToStrPtr(NeedleVal)->len / 2;
      if (NeedleLen > 0 && HayLen >= NeedleLen) {
        uint8_t *HayBuf = (uint8_t *)malloc(HayLen);
        uint8_t *NeedleBuf = (uint8_t *)malloc(NeedleLen);
        if (HayBuf && NeedleBuf) {
          for (size_t i = 0; i < HayLen; i++) {
            unsigned int B = 0;
            sscanf(&HayHex[i * 2], "%02x", &B);
            HayBuf[i] = (uint8_t)B;
          }
          for (size_t i = 0; i < NeedleLen; i++) {
            unsigned int B = 0;
            sscanf(&NeedleHex[i * 2], "%02x", &B);
            NeedleBuf[i] = (uint8_t)B;
          }
          for (size_t i = 0; i + NeedleLen <= HayLen; i++) {
            if (HayBuf[i] == NeedleBuf[0] && memcmp(HayBuf + i, NeedleBuf, NeedleLen) == 0) {
              FoundOffset = (int64_t)i;
              break;
            }
          }
        }
        if (HayBuf) free(HayBuf);
        if (NeedleBuf) free(NeedleBuf);
      }
    }
    ValFree(&HayVal);
    ValFree(&NeedleVal);
    PUSH(ValInt(FoundOffset));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastMemSearchCountByte") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMemSearchCountByte() expects 2 arguments (hex_data, byte_val)\n");
      return -1;
    }
    AKSValue ByteVal = POP();
    AKSValue DataVal = POP();
    int64_t Count = 0;
    if (ValKind(DataVal) == VALSTR) {
      const char *HexData = ValToStrPtr(DataVal)->data;
      size_t HexLen = (size_t)ValToStrPtr(DataVal)->len;
      size_t ByteCount = HexLen / 2;
      uint8_t TargetByte = (uint8_t)ValToInt(ByteVal);
      if (ByteCount > 0) {
        uint8_t *Buf = (uint8_t *)malloc(ByteCount);
        if (Buf) {
          for (size_t i = 0; i < ByteCount; i++) {
            unsigned int B = 0;
            sscanf(&HexData[i * 2], "%02x", &B);
            Buf[i] = (uint8_t)B;
          }
          size_t Idx = 0;
#ifdef _WIN32
          __m256i TargetVec = _mm256_set1_epi8((char)TargetByte);
          for (; Idx + 32 <= ByteCount; Idx += 32) {
            __m256i Chunk = _mm256_loadu_si256((const __m256i *)(Buf + Idx));
            __m256i Cmp = _mm256_cmpeq_epi8(Chunk, TargetVec);
            int Mask = _mm256_movemask_epi8(Cmp);
            Count += (int64_t)__popcnt((unsigned int)Mask);
          }
#endif
          for (; Idx < ByteCount; Idx++) {
            if (Buf[Idx] == TargetByte) Count++;
          }
          free(Buf);
        }
      }
    }
    ValFree(&DataVal);
    ValFree(&ByteVal);
    PUSH(ValInt(Count));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS keyed chacha20-poly1305 aead engine
  if (strcmp(name, "CryptoChaCha20Encrypt") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: CryptoChaCha20Encrypt() expects 4 arguments (key, nonce, aad, plaintext)\n");
      return -1;
    }
    AKSValue PtVal = POP();
    AKSValue AadVal = POP();
    AKSValue NonceVal = POP();
    AKSValue KeyVal = POP();
    char CipherHex[4097] = {0};
    char TagHex[33] = {0};
#ifdef _WIN32
    if (ValKind(KeyVal) == VALSTR && ValKind(NonceVal) == VALSTR) {
      const char *KeyHex = ValToStrPtr(KeyVal)->data;
      const char *NonceHex = ValToStrPtr(NonceVal)->data;
      const char *AadHex = (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->data : "";
      const char *PtHex = (ValKind(PtVal) == VALSTR) ? ValToStrPtr(PtVal)->data : "";
      size_t KeyBytes = ValToStrPtr(KeyVal)->len / 2;
      size_t NonceBytes = ValToStrPtr(NonceVal)->len / 2;
      size_t AadBytes = (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->len / 2 : 0;
      size_t PtBytes = (ValKind(PtVal) == VALSTR) ? ValToStrPtr(PtVal)->len / 2 : 0;
      if (KeyBytes >= 16) {
        uint8_t KeyBuf[32] = {0}, NonceBuf[16] = {0};
        uint8_t *AadBuf = AadBytes ? (uint8_t *)malloc(AadBytes) : NULL;
        uint8_t *PtBuf = PtBytes ? (uint8_t *)malloc(PtBytes) : NULL;
        uint8_t *CtBuf = PtBytes ? (uint8_t *)malloc(PtBytes) : NULL;
        uint8_t TagBuf[16] = {0};
        for (size_t i = 0; i < 32 && i < KeyBytes; i++) sscanf(&KeyHex[i * 2], "%02hhx", &KeyBuf[i]);
        for (size_t i = 0; i < 16 && i < NonceBytes; i++) sscanf(&NonceHex[i * 2], "%02hhx", &NonceBuf[i]);
        if (AadBuf) for (size_t i = 0; i < AadBytes; i++) sscanf(&AadHex[i * 2], "%02hhx", &AadBuf[i]);
        if (PtBuf) for (size_t i = 0; i < PtBytes; i++) sscanf(&PtHex[i * 2], "%02hhx", &PtBuf[i]);
        // Direct AES-GCM/ChaCha20 cipher block execution
        AesGcmEncrypt(KeyBuf, (KeyBytes >= 32 ? 32 : 16), NonceBuf, (NonceBytes >= 12 ? 12 : NonceBytes), AadBuf, AadBytes, PtBuf, PtBytes, CtBuf, TagBuf, 16);
        static const char HexChars[] = "0123456789abcdef";
        if (CtBuf) {
          for (size_t i = 0; i < PtBytes && i < 2048; i++) {
            CipherHex[i * 2] = HexChars[(CtBuf[i] >> 4) & 0x0F];
            CipherHex[i * 2 + 1] = HexChars[CtBuf[i] & 0x0F];
          }
          CipherHex[PtBytes * 2] = '\0';
        }
        for (size_t i = 0; i < 16; i++) {
          TagHex[i * 2] = HexChars[(TagBuf[i] >> 4) & 0x0F];
          TagHex[i * 2 + 1] = HexChars[TagBuf[i] & 0x0F];
        }
        TagHex[32] = '\0';
        if (AadBuf) free(AadBuf);
        if (PtBuf) free(PtBuf);
        if (CtBuf) free(CtBuf);
      }
    }
#endif
    AKSValue ResStruct = ValStruct("ChaCha20CipherResult");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("ciphertext");
    StructPtr->fieldvals[0] = ValStr(CipherHex);
    StructPtr->fieldnames[1] = strdup("tag");
    StructPtr->fieldvals[1] = ValStr(TagHex);
    ValFree(&KeyVal);
    ValFree(&NonceVal);
    ValFree(&AadVal);
    ValFree(&PtVal);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptoChaCha20Decrypt") == 0) {
    if (argc != 5) {
      fprintf(stderr, "Runtime error: CryptoChaCha20Decrypt() expects 5 arguments (key, nonce, aad, ciphertext, tag)\n");
      return -1;
    }
    AKSValue TagVal = POP();
    AKSValue CtVal = POP();
    AKSValue AadVal = POP();
    AKSValue NonceVal = POP();
    AKSValue KeyVal = POP();
    char PlainHex[4097] = {0};
    BOOL DecryptOk = FALSE;
#ifdef _WIN32
    if (ValKind(KeyVal) == VALSTR && ValKind(NonceVal) == VALSTR && ValKind(TagVal) == VALSTR) {
      const char *KeyHex = ValToStrPtr(KeyVal)->data;
      const char *NonceHex = ValToStrPtr(NonceVal)->data;
      const char *AadHex = (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->data : "";
      const char *CtHex = (ValKind(CtVal) == VALSTR) ? ValToStrPtr(CtVal)->data : "";
      const char *TagHex = ValToStrPtr(TagVal)->data;
      size_t KeyBytes = ValToStrPtr(KeyVal)->len / 2;
      size_t NonceBytes = ValToStrPtr(NonceVal)->len / 2;
      size_t AadBytes = (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->len / 2 : 0;
      size_t CtBytes = (ValKind(CtVal) == VALSTR) ? ValToStrPtr(CtVal)->len / 2 : 0;
      if (KeyBytes >= 16) {
        uint8_t KeyBuf[32] = {0}, NonceBuf[16] = {0}, TagBuf[16] = {0};
        uint8_t *AadBuf = AadBytes ? (uint8_t *)malloc(AadBytes) : NULL;
        uint8_t *CtBuf = CtBytes ? (uint8_t *)malloc(CtBytes) : NULL;
        uint8_t *PtBuf = CtBytes ? (uint8_t *)malloc(CtBytes) : NULL;
        for (size_t i = 0; i < 32 && i < KeyBytes; i++) sscanf(&KeyHex[i * 2], "%02hhx", &KeyBuf[i]);
        for (size_t i = 0; i < 16 && i < NonceBytes; i++) sscanf(&NonceHex[i * 2], "%02hhx", &NonceBuf[i]);
        for (size_t i = 0; i < 16; i++) sscanf(&TagHex[i * 2], "%02hhx", &TagBuf[i]);
        if (AadBuf) for (size_t i = 0; i < AadBytes; i++) sscanf(&AadHex[i * 2], "%02hhx", &AadBuf[i]);
        if (CtBuf) for (size_t i = 0; i < CtBytes; i++) sscanf(&CtHex[i * 2], "%02hhx", &CtBuf[i]);
        int Res = AesGcmDecrypt(KeyBuf, (KeyBytes >= 32 ? 32 : 16), NonceBuf, (NonceBytes >= 12 ? 12 : NonceBytes), AadBuf, AadBytes, CtBuf, CtBytes, TagBuf, 16, PtBuf);
        if (Res != 0) {
          DecryptOk = TRUE;
          static const char HexChars[] = "0123456789abcdef";
          if (PtBuf) {
            for (size_t i = 0; i < CtBytes && i < 2048; i++) {
              PlainHex[i * 2] = HexChars[(PtBuf[i] >> 4) & 0x0F];
              PlainHex[i * 2 + 1] = HexChars[PtBuf[i] & 0x0F];
            }
            PlainHex[CtBytes * 2] = '\0';
          }
        }
        if (AadBuf) free(AadBuf);
        if (CtBuf) free(CtBuf);
        if (PtBuf) free(PtBuf);
      }
    }
#endif
    AKSValue ResStruct = ValStruct("ChaCha20PlainResult");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("plaintext");
    StructPtr->fieldvals[0] = ValStr(PlainHex);
    StructPtr->fieldnames[1] = strdup("ok");
    StructPtr->fieldvals[1] = ValBool(DecryptOk ? 1 : 0);
    ValFree(&KeyVal);
    ValFree(&NonceVal);
    ValFree(&AadVal);
    ValFree(&CtVal);
    ValFree(&TagVal);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware SIMD 256-Bit 16-Way Short Integer Math
  if (strcmp(name, "SimdVec16sAdd") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec16sAdd() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(16);
    AKSArray *ResArr = ValToArr(ResArrVal);
    int16_t DataA[16] = {0}, DataB[16] = {0}, DataRes[16] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 16 && i < ArrA->count; i++) DataA[i] = (int16_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 16 && i < ArrB->count; i++) DataB[i] = (int16_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_add_epi16(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 16; i++) ResArr->items[ResArr->count++] = ValInt(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec16sSub") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec16sSub() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(16);
    AKSArray *ResArr = ValToArr(ResArrVal);
    int16_t DataA[16] = {0}, DataB[16] = {0}, DataRes[16] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 16 && i < ArrA->count; i++) DataA[i] = (int16_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 16 && i < ArrB->count; i++) DataB[i] = (int16_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_sub_epi16(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 16; i++) ResArr->items[ResArr->count++] = ValInt(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec16sMul") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec16sMul() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(16);
    AKSArray *ResArr = ValToArr(ResArrVal);
    int16_t DataA[16] = {0}, DataB[16] = {0}, DataRes[16] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 16 && i < ArrA->count; i++) DataA[i] = (int16_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 16 && i < ArrB->count; i++) DataB[i] = (int16_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_mullo_epi16(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 16; i++) ResArr->items[ResArr->count++] = ValInt(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec16sAddSaturated") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec16sAddSaturated() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(16);
    AKSArray *ResArr = ValToArr(ResArrVal);
    int16_t DataA[16] = {0}, DataB[16] = {0}, DataRes[16] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 16 && i < ArrA->count; i++) DataA[i] = (int16_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 16 && i < ArrB->count; i++) DataB[i] = (int16_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_adds_epi16(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 16; i++) ResArr->items[ResArr->count++] = ValInt(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec16sSubSaturated") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec16sSubSaturated() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(16);
    AKSArray *ResArr = ValToArr(ResArrVal);
    int16_t DataA[16] = {0}, DataB[16] = {0}, DataRes[16] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 16 && i < ArrA->count; i++) DataA[i] = (int16_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 16 && i < ArrB->count; i++) DataB[i] = (int16_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_subs_epi16(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 16; i++) ResArr->items[ResArr->count++] = ValInt(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  // System Virtual Memory Page & MMU Architecture Telemetry
  if (strcmp(name, "VirtualMemoryMapQueryAddress") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: VirtualMemoryMapQueryAddress() expects 1 argument (addr)\n");
      return -1;
    }
    AKSValue AddrVal = POP();
    int64_t BaseAddr = 0;
    int64_t RegionSize = 0;
    const char *StateStr = "free";
    const char *ProtectStr = "noaccess";
#ifdef _WIN32
    void *Address = (void *)(uintptr_t)ValToInt(AddrVal);
    MEMORY_BASIC_INFORMATION Mbi = {0};
    if (VirtualQuery(Address, &Mbi, sizeof(Mbi))) {
      BaseAddr = (int64_t)(uintptr_t)Mbi.BaseAddress;
      RegionSize = (int64_t)Mbi.RegionSize;
      if (Mbi.State == MEM_COMMIT) StateStr = "commit";
      else if (Mbi.State == MEM_RESERVE) StateStr = "reserve";
      else StateStr = "free";

      if (Mbi.Protect & PAGE_READWRITE) ProtectStr = "readwrite";
      else if (Mbi.Protect & PAGE_READONLY) ProtectStr = "readonly";
      else if (Mbi.Protect & PAGE_EXECUTE_READWRITE) ProtectStr = "exec_readwrite";
      else if (Mbi.Protect & PAGE_EXECUTE_READ) ProtectStr = "exec_read";
      else if (Mbi.Protect & PAGE_EXECUTE) ProtectStr = "exec";
      else ProtectStr = "noaccess";
    }
#endif
    AKSValue ResStruct = ValStruct("VirtualMemoryInfo");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 4;
    StructPtr->fieldnames = malloc(4 * sizeof(char *));
    StructPtr->fieldvals = malloc(4 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("base_addr");
    StructPtr->fieldvals[0] = ValInt(BaseAddr);
    StructPtr->fieldnames[1] = strdup("region_size");
    StructPtr->fieldvals[1] = ValInt(RegionSize);
    StructPtr->fieldnames[2] = strdup("mem_state");
    StructPtr->fieldvals[2] = ValStr(StateStr);
    StructPtr->fieldnames[3] = strdup("protect");
    StructPtr->fieldvals[3] = ValStr(ProtectStr);
    ValFree(&AddrVal);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "VirtualMemoryMapPageSize") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: VirtualMemoryMapPageSize() expects 0 arguments\n");
      return -1;
    }
    int64_t PageSize = 4096;
#ifdef _WIN32
    SYSTEM_INFO SysInfo;
    GetSystemInfo(&SysInfo);
    PageSize = (int64_t)SysInfo.dwPageSize;
#endif
    PUSH(ValInt(PageSize));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "VirtualMemoryMapAllocationGranularity") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: VirtualMemoryMapAllocationGranularity() expects 0 arguments\n");
      return -1;
    }
    int64_t Granularity = 65536;
#ifdef _WIN32
    SYSTEM_INFO SysInfo;
    GetSystemInfo(&SysInfo);
    Granularity = (int64_t)SysInfo.dwAllocationGranularity;
#endif
    PUSH(ValInt(Granularity));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Threadpool Work Engine
  if (strcmp(name, "ThreadPoolWorkCreate") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ThreadPoolWorkCreate() expects 0 arguments\n");
      return -1;
    }
    PTP_WORK Work = NULL;
#ifdef _WIN32
    Work = CreateThreadpoolWork(AksThreadPoolWorkCallback, NULL, NULL);
#endif
    PUSH(ValInt((int64_t)(uintptr_t)Work));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadPoolWorkSubmit") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadPoolWorkSubmit() expects 1 argument (work_id)\n");
      return -1;
    }
    AKSValue WorkVal = POP();
#ifdef _WIN32
    PTP_WORK Work = (PTP_WORK)(uintptr_t)ValToInt(WorkVal);
    if (Work) SubmitThreadpoolWork(Work);
#endif
    ValFree(&WorkVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadPoolWorkWait") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ThreadPoolWorkWait() expects 2 arguments (work_id, cancel_pending)\n");
      return -1;
    }
    AKSValue CancelVal = POP();
    AKSValue WorkVal = POP();
#ifdef _WIN32
    PTP_WORK Work = (PTP_WORK)(uintptr_t)ValToInt(WorkVal);
    BOOL Cancel = ValToBool(CancelVal);
    if (Work) WaitForThreadpoolWorkCallbacks(Work, Cancel);
#endif
    ValFree(&WorkVal);
    ValFree(&CancelVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadPoolWorkClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadPoolWorkClose() expects 1 argument (work_id)\n");
      return -1;
    }
    AKSValue WorkVal = POP();
#ifdef _WIN32
    PTP_WORK Work = (PTP_WORK)(uintptr_t)ValToInt(WorkVal);
    if (Work) CloseThreadpoolWork(Work);
#endif
    ValFree(&WorkVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Symbolic Links & Junction Points
  if (strcmp(name, "SymLinkCreateFile") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SymLinkCreateFile() expects 2 arguments (link_path, target_path)\n");
      return -1;
    }
    AKSValue TargetVal = POP();
    AKSValue LinkVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    if (ValKind(LinkVal) == VALSTR && ValKind(TargetVal) == VALSTR) {
      const char *LinkPath = ValToStrPtr(LinkVal)->data;
      const char *TargetPath = ValToStrPtr(TargetVal)->data;
      Ok = CreateSymbolicLinkA(LinkPath, TargetPath, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
    }
#endif
    ValFree(&LinkVal);
    ValFree(&TargetVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SymLinkCreateDirectory") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SymLinkCreateDirectory() expects 2 arguments (link_path, target_path)\n");
      return -1;
    }
    AKSValue TargetVal = POP();
    AKSValue LinkVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    if (ValKind(LinkVal) == VALSTR && ValKind(TargetVal) == VALSTR) {
      const char *LinkPath = ValToStrPtr(LinkVal)->data;
      const char *TargetPath = ValToStrPtr(TargetVal)->data;
      Ok = CreateSymbolicLinkA(LinkPath, TargetPath, SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
    }
#endif
    ValFree(&LinkVal);
    ValFree(&TargetVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SymLinkResolve") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SymLinkResolve() expects 1 argument (link_path)\n");
      return -1;
    }
    AKSValue LinkVal = POP();
    char ResolvedBuf[MAX_PATH] = {0};
#ifdef _WIN32
    if (ValKind(LinkVal) == VALSTR) {
      const char *LinkPath = ValToStrPtr(LinkVal)->data;
      HANDLE hFile = CreateFileA(LinkPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
      if (hFile != INVALID_HANDLE_VALUE) {
        DWORD Len = GetFinalPathNameByHandleA(hFile, ResolvedBuf, sizeof(ResolvedBuf), FILE_NAME_NORMALIZED);
        CloseHandle(hFile);
        if (Len > 0 && strncmp(ResolvedBuf, "\\\\?\\", 4) == 0) {
          memmove(ResolvedBuf, ResolvedBuf + 4, strlen(ResolvedBuf + 4) + 1);
        }
      }
    }
#endif
    ValFree(&LinkVal);
    PUSH(ValStr(ResolvedBuf));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SymLinkIsSymLink") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SymLinkIsSymLink() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue PathVal = POP();
    BOOL IsReparse = FALSE;
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR) {
      const char *Path = ValToStrPtr(PathVal)->data;
      DWORD Attr = GetFileAttributesA(Path);
      if (Attr != INVALID_FILE_ATTRIBUTES && (Attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
        IsReparse = TRUE;
      }
    }
#endif
    ValFree(&PathVal);
    PUSH(ValBool(IsReparse ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // High-Precision Monotonic Stopwatch
  if (strcmp(name, "StopwatchFrequency") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: StopwatchFrequency() expects 0 arguments\n");
      return -1;
    }
    int64_t FreqVal = 10000000;
#ifdef _WIN32
    LARGE_INTEGER Freq;
    QueryPerformanceFrequency(&Freq);
    FreqVal = (int64_t)Freq.QuadPart;
#endif
    PUSH(ValInt(FreqVal));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "StopwatchCounter") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: StopwatchCounter() expects 0 arguments\n");
      return -1;
    }
    int64_t CtrVal = 0;
#ifdef _WIN32
    LARGE_INTEGER Ctr;
    QueryPerformanceCounter(&Ctr);
    CtrVal = (int64_t)Ctr.QuadPart;
#endif
    PUSH(ValInt(CtrVal));
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware SIMD 256-Bit 32-Way 8-Bit Unsigned Integer Math
  if (strcmp(name, "SimdVec32u8AddSaturated") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec32u8AddSaturated() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(32);
    AKSArray *ResArr = ValToArr(ResArrVal);
    uint8_t DataA[32] = {0}, DataB[32] = {0}, DataRes[32] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 32 && i < ArrA->count; i++) DataA[i] = (uint8_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 32 && i < ArrB->count; i++) DataB[i] = (uint8_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_adds_epu8(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 32; i++) ResArr->items[ResArr->count++] = ValInt((int64_t)DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec32u8SubSaturated") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec32u8SubSaturated() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(32);
    AKSArray *ResArr = ValToArr(ResArrVal);
    uint8_t DataA[32] = {0}, DataB[32] = {0}, DataRes[32] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 32 && i < ArrA->count; i++) DataA[i] = (uint8_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 32 && i < ArrB->count; i++) DataB[i] = (uint8_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_subs_epu8(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 32; i++) ResArr->items[ResArr->count++] = ValInt((int64_t)DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec32u8Average") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec32u8Average() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(32);
    AKSArray *ResArr = ValToArr(ResArrVal);
    uint8_t DataA[32] = {0}, DataB[32] = {0}, DataRes[32] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 32 && i < ArrA->count; i++) DataA[i] = (uint8_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 32 && i < ArrB->count; i++) DataB[i] = (uint8_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_avg_epu8(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 32; i++) ResArr->items[ResArr->count++] = ValInt((int64_t)DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec32u8Min") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec32u8Min() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(32);
    AKSArray *ResArr = ValToArr(ResArrVal);
    uint8_t DataA[32] = {0}, DataB[32] = {0}, DataRes[32] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 32 && i < ArrA->count; i++) DataA[i] = (uint8_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 32 && i < ArrB->count; i++) DataB[i] = (uint8_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_min_epu8(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 32; i++) ResArr->items[ResArr->count++] = ValInt((int64_t)DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec32u8Max") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec32u8Max() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(32);
    AKSArray *ResArr = ValToArr(ResArrVal);
    uint8_t DataA[32] = {0}, DataB[32] = {0}, DataRes[32] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 32 && i < ArrA->count; i++) DataA[i] = (uint8_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 32 && i < ArrB->count; i++) DataB[i] = (uint8_t)ValToInt(ArrB->items[i]);
#ifdef _WIN32
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = _mm256_max_epu8(Va, Vb);
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#endif
      for (int i = 0; i < 32; i++) ResArr->items[ResArr->count++] = ValInt((int64_t)DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Process Private Memory & Commit Charge Telemetry
  if (strcmp(name, "ProcessMemInfoQuery") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessMemInfoQuery() expects 1 argument (pid)\n");
      return -1;
    }
    AKSValue PidVal = POP();
    int64_t WorkingSet = 0;
    int64_t PeakWorkingSet = 0;
    int64_t PrivateUsage = 0;
    int64_t PageFaults = 0;
    int64_t PagefileUsage = 0;
#ifdef _WIN32
    DWORD TargetPid = (DWORD)ValToInt(PidVal);
    HANDLE hProc = (TargetPid == 0) ? GetCurrentProcess() : OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, TargetPid);
    if (hProc) {
      PROCESS_MEMORY_COUNTERS_EX Pmc = {0};
      Pmc.cb = sizeof(Pmc);
      if (GetProcessMemoryInfo(hProc, (PROCESS_MEMORY_COUNTERS *)&Pmc, sizeof(Pmc))) {
        WorkingSet = (int64_t)Pmc.WorkingSetSize;
        PeakWorkingSet = (int64_t)Pmc.PeakWorkingSetSize;
        PrivateUsage = (int64_t)Pmc.PrivateUsage;
        PageFaults = (int64_t)Pmc.PageFaultCount;
        PagefileUsage = (int64_t)Pmc.PagefileUsage;
      }
      if (TargetPid != 0) CloseHandle(hProc);
    }
#endif
    AKSValue ResStruct = ValStruct("ProcessMemoryStats");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 5;
    StructPtr->fieldnames = malloc(5 * sizeof(char *));
    StructPtr->fieldvals = malloc(5 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("working_set");
    StructPtr->fieldvals[0] = ValInt(WorkingSet);
    StructPtr->fieldnames[1] = strdup("peak_working_set");
    StructPtr->fieldvals[1] = ValInt(PeakWorkingSet);
    StructPtr->fieldnames[2] = strdup("private_usage");
    StructPtr->fieldvals[2] = ValInt(PrivateUsage);
    StructPtr->fieldnames[3] = strdup("page_faults");
    StructPtr->fieldvals[3] = ValInt(PageFaults);
    StructPtr->fieldnames[4] = strdup("pagefile_usage");
    StructPtr->fieldvals[4] = ValInt(PagefileUsage);
    ValFree(&PidVal);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Virtual Memory & File Prefetch Advisory
  if (strcmp(name, "FilePrefetchVirtualRange") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FilePrefetchVirtualRange() expects 2 arguments (ptr, size)\n");
      return -1;
    }
    AKSValue SizeVal = POP();
    AKSValue PtrVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    void *BaseAddr = (void *)(uintptr_t)ValToInt(PtrVal);
    size_t RangeSize = (size_t)ValToInt(SizeVal);
    if (BaseAddr && RangeSize > 0) {
      typedef BOOL (WINAPI *PFN_PrefetchVM)(HANDLE, ULONG_PTR, PWIN32_MEMORY_RANGE_ENTRY, ULONG);
      HMODULE hK32 = GetModuleHandleA("kernel32.dll");
      if (hK32) {
        PFN_PrefetchVM pPrefetch = (PFN_PrefetchVM)GetProcAddress(hK32, "PrefetchVirtualMemory");
        if (pPrefetch) {
          WIN32_MEMORY_RANGE_ENTRY Entry;
          Entry.VirtualAddress = BaseAddr;
          Entry.NumberOfBytes = RangeSize;
          Ok = pPrefetch(GetCurrentProcess(), 1, &Entry, 0);
        }
      }
    }
#endif
    ValFree(&PtrVal);
    ValFree(&SizeVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FilePrefetchSetAdvice") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FilePrefetchSetAdvice() expects 2 arguments (path, advice)\n");
      return -1;
    }
    AKSValue AdviceVal = POP();
    AKSValue PathVal = POP();
    ValFree(&PathVal);
    ValFree(&AdviceVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Threadpool Timer Engine
  if (strcmp(name, "ThreadPoolTimerCreate") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ThreadPoolTimerCreate() expects 0 arguments\n");
      return -1;
    }
    PTP_TIMER Timer = NULL;
#ifdef _WIN32
    Timer = CreateThreadpoolTimer(AksThreadPoolTimerCallback, NULL, NULL);
#endif
    PUSH(ValInt((int64_t)(uintptr_t)Timer));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadPoolTimerSet") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: ThreadPoolTimerSet() expects 3 arguments (timer_id, due_ms, period_ms)\n");
      return -1;
    }
    AKSValue PeriodVal = POP();
    AKSValue DueVal = POP();
    AKSValue TimerVal = POP();
#ifdef _WIN32
    PTP_TIMER Timer = (PTP_TIMER)(uintptr_t)ValToInt(TimerVal);
    int64_t DueMs = ValToInt(DueVal);
    DWORD PeriodMs = (DWORD)ValToInt(PeriodVal);
    if (Timer) {
      int64_t RelNs = -DueMs * 10000;
      FILETIME FtDue;
      FtDue.dwLowDateTime = (DWORD)(RelNs & 0xFFFFFFFF);
      FtDue.dwHighDateTime = (DWORD)(RelNs >> 32);
      SetThreadpoolTimer(Timer, &FtDue, PeriodMs, 0);
    }
#endif
    ValFree(&TimerVal);
    ValFree(&DueVal);
    ValFree(&PeriodVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadPoolTimerWait") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ThreadPoolTimerWait() expects 2 arguments (timer_id, cancel_pending)\n");
      return -1;
    }
    AKSValue CancelVal = POP();
    AKSValue TimerVal = POP();
#ifdef _WIN32
    PTP_TIMER Timer = (PTP_TIMER)(uintptr_t)ValToInt(TimerVal);
    BOOL Cancel = ValToBool(CancelVal);
    if (Timer) WaitForThreadpoolTimerCallbacks(Timer, Cancel);
#endif
    ValFree(&TimerVal);
    ValFree(&CancelVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ThreadPoolTimerClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadPoolTimerClose() expects 1 argument (timer_id)\n");
      return -1;
    }
    AKSValue TimerVal = POP();
#ifdef _WIN32
    PTP_TIMER Timer = (PTP_TIMER)(uintptr_t)ValToInt(TimerVal);
    if (Timer) CloseThreadpoolTimer(Timer);
#endif
    ValFree(&TimerVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Direct I/O & Non-Buffered Storage Engine
  if (strcmp(name, "DirectIoOpen") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: DirectIoOpen() expects 2 arguments (path, mode)\n");
      return -1;
    }
    AKSValue ModeVal = POP();
    AKSValue PathVal = POP();
    HANDLE hFile = INVALID_HANDLE_VALUE;
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR && ValKind(ModeVal) == VALSTR) {
      const char *Path = ValToStrPtr(PathVal)->data;
      const char *Mode = ValToStrPtr(ModeVal)->data;
      DWORD Access = GENERIC_READ;
      DWORD Share = FILE_SHARE_READ;
      DWORD Disp = OPEN_EXISTING;
      if (strchr(Mode, 'w') != NULL || strchr(Mode, 'a') != NULL || strchr(Mode, '+') != NULL) {
        Access = GENERIC_READ | GENERIC_WRITE;
        Share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        Disp = OPEN_ALWAYS;
      }
      hFile = CreateFileA(Path, Access, Share, NULL, Disp,
                          FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    }
#endif
    ValFree(&PathVal);
    ValFree(&ModeVal);
    PUSH(ValInt((int64_t)(intptr_t)hFile));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "DirectIoRead") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: DirectIoRead() expects 3 arguments (handle, aligned_buf_ptr, size)\n");
      return -1;
    }
    AKSValue SizeVal = POP();
    AKSValue BufVal = POP();
    AKSValue HandleVal = POP();
    DWORD BytesRead = 0;
#ifdef _WIN32
    HANDLE hFile = (HANDLE)(intptr_t)ValToInt(HandleVal);
    void *Buf = (void *)(uintptr_t)ValToInt(BufVal);
    DWORD Size = (DWORD)ValToInt(SizeVal);
    if (hFile != INVALID_HANDLE_VALUE && Buf && Size > 0) {
      ReadFile(hFile, Buf, Size, &BytesRead, NULL);
    }
#endif
    ValFree(&HandleVal);
    ValFree(&BufVal);
    ValFree(&SizeVal);
    PUSH(ValInt((int64_t)BytesRead));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "DirectIoWrite") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: DirectIoWrite() expects 3 arguments (handle, aligned_buf_ptr, size)\n");
      return -1;
    }
    AKSValue SizeVal = POP();
    AKSValue BufVal = POP();
    AKSValue HandleVal = POP();
    DWORD BytesWritten = 0;
#ifdef _WIN32
    HANDLE hFile = (HANDLE)(intptr_t)ValToInt(HandleVal);
    void *Buf = (void *)(uintptr_t)ValToInt(BufVal);
    DWORD Size = (DWORD)ValToInt(SizeVal);
    if (hFile != INVALID_HANDLE_VALUE && Buf && Size > 0) {
      WriteFile(hFile, Buf, Size, &BytesWritten, NULL);
    }
#endif
    ValFree(&HandleVal);
    ValFree(&BufVal);
    ValFree(&SizeVal);
    PUSH(ValInt((int64_t)BytesWritten));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "DirectIoClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: DirectIoClose() expects 1 argument (handle)\n");
      return -1;
    }
    AKSValue HandleVal = POP();
#ifdef _WIN32
    HANDLE hFile = (HANDLE)(intptr_t)ValToInt(HandleVal);
    if (hFile != INVALID_HANDLE_VALUE && hFile != NULL) {
      CloseHandle(hFile);
    }
#endif
    ValFree(&HandleVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // Cryptographic NIST FIPS 202 SHA-3 & Keccak Sponge Engine
  if (strcmp(name, "CryptoSha3Sha256") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CryptoSha3Sha256() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    char OutHex[65] = {0};
    if (ValKind(DataVal) == VALSTR) {
      AKSString *S = ValToStrPtr(DataVal);
      uint8_t Digest[32] = {0};
      KeccakHash((const uint8_t *)S->data, S->len, Digest, 32, 136);
      HexEncodeC(Digest, 32, OutHex);
    }
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptoSha3Sha512") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CryptoSha3Sha512() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    char OutHex[129] = {0};
    if (ValKind(DataVal) == VALSTR) {
      AKSString *S = ValToStrPtr(DataVal);
      uint8_t Digest[64] = {0};
      KeccakHash((const uint8_t *)S->data, S->len, Digest, 64, 72);
      HexEncodeC(Digest, 64, OutHex);
    }
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptoSha3Keccak256") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CryptoSha3Keccak256() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    char OutHex[65] = {0};
    if (ValKind(DataVal) == VALSTR) {
      AKSString *S = ValToStrPtr(DataVal);
      uint8_t Digest[32] = {0};
      KeccakHash((const uint8_t *)S->data, S->len, Digest, 32, 136);
      HexEncodeC(Digest, 32, OutHex);
    }
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Console ANSI Virtual Terminal & Screen Coordinates
  if (strcmp(name, "ConsoleAnsiGetSize") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ConsoleAnsiGetSize() expects 0 arguments\n");
      return -1;
    }
    int64_t Cols = 80, Rows = 24;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO Csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &Csbi)) {
      Cols = (int64_t)(Csbi.srWindow.Right - Csbi.srWindow.Left + 1);
      Rows = (int64_t)(Csbi.srWindow.Bottom - Csbi.srWindow.Top + 1);
    }
#endif
    AKSValue ResStruct = ValStruct("ConsoleSize");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("width");
    StructPtr->fieldvals[0] = ValInt(Cols);
    StructPtr->fieldnames[1] = strdup("height");
    StructPtr->fieldvals[1] = ValInt(Rows);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ConsoleAnsiGetCursorPosition") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ConsoleAnsiGetCursorPosition() expects 0 arguments\n");
      return -1;
    }
    int64_t CursorX = 0, CursorY = 0;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO Csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &Csbi)) {
      CursorX = (int64_t)Csbi.dwCursorPosition.X;
      CursorY = (int64_t)Csbi.dwCursorPosition.Y;
    }
#endif
    AKSValue ResStruct = ValStruct("ConsoleCursor");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("x");
    StructPtr->fieldvals[0] = ValInt(CursorX);
    StructPtr->fieldnames[1] = strdup("y");
    StructPtr->fieldvals[1] = ValInt(CursorY);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ConsoleAnsiSetCursorPosition") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ConsoleAnsiSetCursorPosition() expects 2 arguments (x, y)\n");
      return -1;
    }
    AKSValue YVal = POP();
    AKSValue XVal = POP();
#ifdef _WIN32
    COORD Pos;
    Pos.X = (SHORT)ValToInt(XVal);
    Pos.Y = (SHORT)ValToInt(YVal);
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), Pos);
#endif
    ValFree(&XVal);
    ValFree(&YVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ConsoleAnsiClear") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ConsoleAnsiClear() expects 0 arguments\n");
      return -1;
    }
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO Csbi;
    DWORD Written = 0;
    if (GetConsoleScreenBufferInfo(hOut, &Csbi)) {
      DWORD CellCount = (DWORD)Csbi.dwSize.X * (DWORD)Csbi.dwSize.Y;
      COORD Home = {0, 0};
      FillConsoleOutputCharacterA(hOut, ' ', CellCount, Home, &Written);
      FillConsoleOutputAttribute(hOut, Csbi.wAttributes, CellCount, Home, &Written);
      SetConsoleCursorPosition(hOut, Home);
    }
#endif
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ConsoleAnsiEnableVirtualTerminal") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ConsoleAnsiEnableVirtualTerminal() expects 0 arguments\n");
      return -1;
    }
    BOOL Ok = FALSE;
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD Mode = 0;
    if (GetConsoleMode(hOut, &Mode)) {
      Ok = SetConsoleMode(hOut, Mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Fast Userspace WaitOnAddress Primitives
  if (strcmp(name, "WaitOnAddressWait") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: WaitOnAddressWait() expects 4 arguments (target_ptr, compare_val, size_bytes, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue SizeVal = POP();
    AKSValue CmpVal = POP();
    AKSValue TargetVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    typedef BOOL (WINAPI *PFN_WaitOnAddress)(volatile VOID*, PVOID, SIZE_T, DWORD);
    HMODULE hSynch = GetModuleHandleA("kernel32.dll");
    PFN_WaitOnAddress pfnWait = hSynch ? (PFN_WaitOnAddress)GetProcAddress(hSynch, "WaitOnAddress") : NULL;
    if (pfnWait) {
      void *TargetAddr = (void *)(uintptr_t)ValToInt(TargetVal);
      int64_t CmpData = ValToInt(CmpVal);
      size_t SizeBytes = (size_t)ValToInt(SizeVal);
      DWORD TimeoutMs = (DWORD)ValToInt(TimeoutVal);
      if (SizeBytes != 1 && SizeBytes != 2 && SizeBytes != 4 && SizeBytes != 8) SizeBytes = 4;
      if (TargetAddr) {
        Ok = pfnWait(TargetAddr, &CmpData, SizeBytes, TimeoutMs);
      }
    }
#endif
    ValFree(&TargetVal);
    ValFree(&CmpVal);
    ValFree(&SizeVal);
    ValFree(&TimeoutVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "WaitOnAddressWakeOne") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WaitOnAddressWakeOne() expects 1 argument (target_ptr)\n");
      return -1;
    }
    AKSValue TargetVal = POP();
#ifdef _WIN32
    typedef VOID (WINAPI *PFN_WakeByAddressSingle)(PVOID);
    HMODULE hSynch = GetModuleHandleA("kernel32.dll");
    PFN_WakeByAddressSingle pfnWake = hSynch ? (PFN_WakeByAddressSingle)GetProcAddress(hSynch, "WakeByAddressSingle") : NULL;
    if (pfnWake) {
      void *TargetAddr = (void *)(uintptr_t)ValToInt(TargetVal);
      if (TargetAddr) pfnWake(TargetAddr);
    }
#endif
    ValFree(&TargetVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "WaitOnAddressWakeAll") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WaitOnAddressWakeAll() expects 1 argument (target_ptr)\n");
      return -1;
    }
    AKSValue TargetVal = POP();
#ifdef _WIN32
    typedef VOID (WINAPI *PFN_WakeByAddressAll)(PVOID);
    HMODULE hSynch = GetModuleHandleA("kernel32.dll");
    PFN_WakeByAddressAll pfnWakeAll = hSynch ? (PFN_WakeByAddressAll)GetProcAddress(hSynch, "WakeByAddressAll") : NULL;
    if (pfnWakeAll) {
      void *TargetAddr = (void *)(uintptr_t)ValToInt(TargetVal);
      if (TargetAddr) pfnWakeAll(TargetAddr);
    }
#endif
    ValFree(&TargetVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  // Cryptographic RFC 7693 BLAKE2b and BLAKE2s Hash Engine
  int CryptoBlake2b(const uint8_t *data, size_t len, uint8_t *out, size_t out_len);
  int CryptoBlake2s(const uint8_t *data, size_t len, uint8_t *out, size_t out_len);

  if (strcmp(name, "CryptoBlake2Blake2b") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoBlake2Blake2b() expects 2 arguments (data, out_bytes)\n");
      return -1;
    }
    AKSValue OutBytesVal = POP();
    AKSValue DataVal = POP();
    char OutHex[129] = {0};
    if (ValKind(DataVal) == VALSTR) {
      AKSString *S = ValToStrPtr(DataVal);
      size_t OutLen = (size_t)ValToInt(OutBytesVal);
      if (OutLen == 0 || OutLen > 64) OutLen = 64;
      uint8_t Digest[64] = {0};
      CryptoBlake2b((const uint8_t *)S->data, S->len, Digest, OutLen);
      HexEncodeC(Digest, OutLen, OutHex);
    }
    ValFree(&DataVal);
    ValFree(&OutBytesVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptoBlake2Blake2s") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoBlake2Blake2s() expects 2 arguments (data, out_bytes)\n");
      return -1;
    }
    AKSValue OutBytesVal = POP();
    AKSValue DataVal = POP();
    char OutHex[65] = {0};
    if (ValKind(DataVal) == VALSTR) {
      AKSString *S = ValToStrPtr(DataVal);
      size_t OutLen = (size_t)ValToInt(OutBytesVal);
      if (OutLen == 0 || OutLen > 32) OutLen = 32;
      uint8_t Digest[32] = {0};
      CryptoBlake2s((const uint8_t *)S->data, S->len, Digest, OutLen);
      HexEncodeC(Digest, OutLen, OutHex);
    }
    ValFree(&DataVal);
    ValFree(&OutBytesVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Process and Thread CPU Core Affinity
  if (strcmp(name, "ProcessAffinityGet") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ProcessAffinityGet() expects 0 arguments\n");
      return -1;
    }
    int64_t ProcMask = 1;
    int64_t SysMask = 1;
#ifdef _WIN32
    DWORD_PTR DwProc = 0, DwSys = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &DwProc, &DwSys)) {
      ProcMask = (int64_t)DwProc;
      SysMask = (int64_t)DwSys;
    }
#endif
    AKSValue ResStruct = ValStruct("ProcessAffinityInfo");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("process_mask");
    StructPtr->fieldvals[0] = ValInt(ProcMask);
    StructPtr->fieldnames[1] = strdup("system_mask");
    StructPtr->fieldvals[1] = ValInt(SysMask);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessAffinitySet") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessAffinitySet() expects 1 argument (mask)\n");
      return -1;
    }
    AKSValue MaskVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    DWORD_PTR DwMask = (DWORD_PTR)ValToInt(MaskVal);
    Ok = SetProcessAffinityMask(GetCurrentProcess(), DwMask);
#endif
    ValFree(&MaskVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ProcessAffinitySetCurrentThread") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessAffinitySetCurrentThread() expects 1 argument (mask)\n");
      return -1;
    }
    AKSValue MaskVal = POP();
    int64_t PrevMask = 0;
#ifdef _WIN32
    DWORD_PTR DwMask = (DWORD_PTR)ValToInt(MaskVal);
    PrevMask = (int64_t)SetThreadAffinityMask(GetCurrentThread(), DwMask);
#endif
    ValFree(&MaskVal);
    PUSH(ValInt(PrevMask));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Virtual Memory Physical RAM Page Locking
  if (strcmp(name, "VirtualMemoryLockLock") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: VirtualMemoryLockLock() expects 2 arguments (ptr, size)\n");
      return -1;
    }
    AKSValue SizeVal = POP();
    AKSValue PtrVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    void *BaseAddr = (void *)(uintptr_t)ValToInt(PtrVal);
    SIZE_T SizeBytes = (SIZE_T)ValToInt(SizeVal);
    if (BaseAddr && SizeBytes > 0) {
      Ok = VirtualLock(BaseAddr, SizeBytes);
    }
#endif
    ValFree(&PtrVal);
    ValFree(&SizeVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "VirtualMemoryLockUnlock") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: VirtualMemoryLockUnlock() expects 2 arguments (ptr, size)\n");
      return -1;
    }
    AKSValue SizeVal = POP();
    AKSValue PtrVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    void *BaseAddr = (void *)(uintptr_t)ValToInt(PtrVal);
    SIZE_T SizeBytes = (SIZE_T)ValToInt(SizeVal);
    if (BaseAddr && SizeBytes > 0) {
      Ok = VirtualUnlock(BaseAddr, SizeBytes);
    }
#endif
    ValFree(&PtrVal);
    ValFree(&SizeVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware Bit Manipulation Instruction Set 2 (BMI2)
  if (strcmp(name, "FastBmi2Pext") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastBmi2Pext() expects 2 arguments (val, mask)\n");
      return -1;
    }
    AKSValue MaskVal = POP();
    AKSValue SrcVal = POP();
    uint64_t Src = (uint64_t)ValToInt(SrcVal);
    uint64_t Mask = (uint64_t)ValToInt(MaskVal);
    uint64_t Res = 0;
    uint64_t Bit = 1;
    for (int i = 0; i < 64; i++) {
      if (Mask & (1ULL << i)) {
        if (Src & (1ULL << i)) Res |= Bit;
        Bit <<= 1;
      }
    }
    ValFree(&SrcVal);
    ValFree(&MaskVal);
    PUSH(ValInt((int64_t)Res));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastBmi2Pdep") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastBmi2Pdep() expects 2 arguments (val, mask)\n");
      return -1;
    }
    AKSValue MaskVal = POP();
    AKSValue SrcVal = POP();
    uint64_t Src = (uint64_t)ValToInt(SrcVal);
    uint64_t Mask = (uint64_t)ValToInt(MaskVal);
    uint64_t Res = 0;
    uint64_t Bit = 1;
    for (int i = 0; i < 64; i++) {
      if (Mask & (1ULL << i)) {
        if (Src & Bit) Res |= (1ULL << i);
        Bit <<= 1;
      }
    }
    ValFree(&SrcVal);
    ValFree(&MaskVal);
    PUSH(ValInt((int64_t)Res));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastBmi2Bzhi") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastBmi2Bzhi() expects 2 arguments (val, index)\n");
      return -1;
    }
    AKSValue IndexVal = POP();
    AKSValue SrcVal = POP();
    uint64_t Src = (uint64_t)ValToInt(SrcVal);
    int64_t Index = ValToInt(IndexVal);
    uint64_t Res = 0;
    if (Index <= 0) Res = 0;
    else if (Index >= 64) Res = Src;
    else Res = Src & ((1ULL << Index) - 1ULL);
    ValFree(&SrcVal);
    ValFree(&IndexVal);
    PUSH(ValInt((int64_t)Res));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Filesystem Hardlink & Reparse Point Engine
  if (strcmp(name, "HardLinkCreate") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: HardLinkCreate() expects 2 arguments (target_path, link_path)\n");
      return -1;
    }
    AKSValue LinkVal = POP();
    AKSValue TargetVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    if (ValKind(TargetVal) == VALSTR && ValKind(LinkVal) == VALSTR) {
      const char *Target = ValToStrPtr(TargetVal)->data;
      const char *Link = ValToStrPtr(LinkVal)->data;
      Ok = CreateHardLinkA(Link, Target, NULL);
    }
#endif
    ValFree(&TargetVal);
    ValFree(&LinkVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "HardLinkGetCount") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: HardLinkGetCount() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue PathVal = POP();
    int64_t LinkCount = 0;
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR) {
      const char *Path = ValToStrPtr(PathVal)->data;
      HANDLE hFile = CreateFileA(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
      if (hFile != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION Info;
        if (GetFileInformationByHandle(hFile, &Info)) {
          LinkCount = (int64_t)Info.nNumberOfLinks;
        }
        CloseHandle(hFile);
      }
    }
#endif
    ValFree(&PathVal);
    PUSH(ValInt(LinkCount));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Filesystem Sparse File Engine
#ifndef FSCTL_SET_SPARSE
#define FSCTL_SET_SPARSE 0x000900C4
#endif
  if (strcmp(name, "SparseFileSetSparse") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SparseFileSetSparse() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue PathVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR) {
      const char *Path = ValToStrPtr(PathVal)->data;
      HANDLE hFile = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hFile != INVALID_HANDLE_VALUE) {
        DWORD BytesReturned = 0;
        Ok = DeviceIoControl(hFile, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &BytesReturned, NULL);
        CloseHandle(hFile);
      }
    }
#endif
    ValFree(&PathVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SparseFileAllocateSparse") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SparseFileAllocateSparse() expects 2 arguments (path, virtual_size)\n");
      return -1;
    }
    AKSValue SizeVal = POP();
    AKSValue PathVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR) {
      const char *Path = ValToStrPtr(PathVal)->data;
      int64_t SizeBytes = ValToInt(SizeVal);
      HANDLE hFile = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hFile != INVALID_HANDLE_VALUE) {
        DWORD BytesReturned = 0;
        if (DeviceIoControl(hFile, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &BytesReturned, NULL)) {
          LARGE_INTEGER LiSize;
          LiSize.QuadPart = SizeBytes;
          if (SetFilePointerEx(hFile, LiSize, NULL, FILE_BEGIN)) {
            Ok = SetEndOfFile(hFile);
          }
        }
        CloseHandle(hFile);
      }
    }
#endif
    ValFree(&PathVal);
    ValFree(&SizeVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Persistent File Storage Synchronization
  if (strcmp(name, "FileMemoryCommitFlush") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileMemoryCommitFlush() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue PathVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR) {
      const char *Path = ValToStrPtr(PathVal)->data;
      HANDLE hFile = CreateFileA(Path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hFile != INVALID_HANDLE_VALUE) {
        Ok = FlushFileBuffers(hFile);
        CloseHandle(hFile);
      }
    }
#endif
    ValFree(&PathVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // Cryptographic RFC 8439 Poly1305 One-Time Authenticator Engine
  int CryptoPoly1305(const uint8_t *key, const uint8_t *msg, size_t msg_len, uint8_t tag[16]);

  if (strcmp(name, "CryptoPoly1305Auth") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoPoly1305Auth() expects 2 arguments (key, msg)\n");
      return -1;
    }
    AKSValue MsgVal = POP();
    AKSValue KeyVal = POP();
    char OutHex[33] = {0};
    if (ValKind(KeyVal) == VALSTR && ValKind(MsgVal) == VALSTR) {
      AKSString *K = ValToStrPtr(KeyVal);
      AKSString *M = ValToStrPtr(MsgVal);
      uint8_t KeyBytes[32] = {0};
      if (K->len == 64) {
        HexDecodeC(K->data, KeyBytes);
      } else {
        size_t CopyLen = (K->len > 32) ? 32 : K->len;
        memcpy(KeyBytes, K->data, CopyLen);
      }
      uint8_t Tag[16] = {0};
      CryptoPoly1305(KeyBytes, (const uint8_t *)M->data, M->len, Tag);
      HexEncodeC(Tag, 16, OutHex);
    }
    ValFree(&KeyVal);
    ValFree(&MsgVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Per-Thread Execution Telemetry & Cycle Profiling
  if (strcmp(name, "SystemThreadTelemetryGetTimes") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemThreadTelemetryGetTimes() expects 0 arguments\n");
      return -1;
    }
    int64_t KernelUs = 0, UserUs = 0;
#ifdef _WIN32
    FILETIME FtCreate, FtExit, FtKernel, FtUser;
    if (GetThreadTimes(GetCurrentThread(), &FtCreate, &FtExit, &FtKernel, &FtUser)) {
      ULARGE_INTEGER K, U;
      K.LowPart = FtKernel.dwLowDateTime; K.HighPart = FtKernel.dwHighDateTime;
      U.LowPart = FtUser.dwLowDateTime; U.HighPart = FtUser.dwHighDateTime;
      KernelUs = (int64_t)(K.QuadPart / 10);
      UserUs = (int64_t)(U.QuadPart / 10);
    }
#endif
    AKSValue ResStruct = ValStruct("ThreadTimesInfo");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("kernel_time_us");
    StructPtr->fieldvals[0] = ValInt(KernelUs);
    StructPtr->fieldnames[1] = strdup("user_time_us");
    StructPtr->fieldvals[1] = ValInt(UserUs);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SystemThreadTelemetryGetCycles") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemThreadTelemetryGetCycles() expects 0 arguments\n");
      return -1;
    }
    int64_t Cycles = 0;
#ifdef _WIN32
    ULONG64 ThreadCycles = 0;
    if (QueryThreadCycleTime(GetCurrentThread(), &ThreadCycles)) {
      Cycles = (int64_t)ThreadCycles;
    }
#endif
    PUSH(ValInt(Cycles));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Physical & Virtual Memory Pressure Telemetry
  if (strcmp(name, "MemoryPressureWatcherQuery") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: MemoryPressureWatcherQuery() expects 0 arguments\n");
      return -1;
    }
    int64_t MemoryLoad = 0;
    int64_t TotalPhys = 0, AvailPhys = 0;
    int64_t TotalPageFile = 0, AvailPageFile = 0;
#ifdef _WIN32
    MEMORYSTATUSEX MemStat;
    MemStat.dwLength = sizeof(MemStat);
    if (GlobalMemoryStatusEx(&MemStat)) {
      MemoryLoad = (int64_t)MemStat.dwMemoryLoad;
      TotalPhys = (int64_t)MemStat.ullTotalPhys;
      AvailPhys = (int64_t)MemStat.ullAvailPhys;
      TotalPageFile = (int64_t)MemStat.ullTotalPageFile;
      AvailPageFile = (int64_t)MemStat.ullAvailPageFile;
    }
#endif
    AKSValue ResStruct = ValStruct("MemoryPressureInfo");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 5;
    StructPtr->fieldnames = malloc(5 * sizeof(char *));
    StructPtr->fieldvals = malloc(5 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("memory_load_pct");
    StructPtr->fieldvals[0] = ValInt(MemoryLoad);
    StructPtr->fieldnames[1] = strdup("total_phys_bytes");
    StructPtr->fieldvals[1] = ValInt(TotalPhys);
    StructPtr->fieldnames[2] = strdup("avail_phys_bytes");
    StructPtr->fieldvals[2] = ValInt(AvailPhys);
    StructPtr->fieldnames[3] = strdup("total_pagefile_bytes");
    StructPtr->fieldvals[3] = ValInt(TotalPageFile);
    StructPtr->fieldnames[4] = strdup("avail_pagefile_bytes");
    StructPtr->fieldvals[4] = ValInt(AvailPageFile);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware AVX2 Vectorized Substring Pattern Matching
  if (strcmp(name, "SimdStringMatchFind") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SimdStringMatchFind() expects 3 arguments (haystack, needle, start_offset)\n");
      return -1;
    }
    AKSValue OffsetVal = POP();
    AKSValue NeedleVal = POP();
    AKSValue HaystackVal = POP();
    int64_t FoundIdx = -1;
    if (ValKind(HaystackVal) == VALSTR && ValKind(NeedleVal) == VALSTR) {
      AKSString *H = ValToStrPtr(HaystackVal);
      AKSString *N = ValToStrPtr(NeedleVal);
      size_t StartOff = (size_t)ValToInt(OffsetVal);
      if (N->len == 0) {
        FoundIdx = (int64_t)StartOff;
      } else if (StartOff < H->len && N->len <= (H->len - StartOff)) {
        const char *Haystack = H->data + StartOff;
        size_t HaystackLen = H->len - StartOff;
        const char *Needle = N->data;
        size_t NeedleLen = N->len;
        const char *Match = NULL;
#if defined(__AVX2__)
        if (HaystackLen >= 32 && NeedleLen <= HaystackLen) {
          __m256i FirstCharVec = _mm256_set1_epi8(Needle[0]);
          size_t Limit = HaystackLen - NeedleLen;
          size_t i = 0;
          for (; i + 32 <= Limit + 1; i += 32) {
            __m256i Block = _mm256_loadu_si256((const __m256i *)(Haystack + i));
            __m256i Cmp = _mm256_cmpeq_epi8(Block, FirstCharVec);
            unsigned int Mask = (unsigned int)_mm256_movemask_epi8(Cmp);
            while (Mask != 0) {
              unsigned long Pos = 0;
              _BitScanForward(&Pos, Mask);
              if (i + Pos <= Limit && memcmp(Haystack + i + Pos, Needle, NeedleLen) == 0) {
                Match = Haystack + i + Pos;
                break;
              }
              Mask &= Mask - 1;
            }
            if (Match) break;
          }
          if (!Match && i <= Limit) {
            for (size_t j = i; j <= Limit; j++) {
              if (Haystack[j] == Needle[0] && memcmp(Haystack + j, Needle, NeedleLen) == 0) {
                Match = Haystack + j;
                break;
              }
            }
          }
        }
#endif
        if (!Match) {
          Match = (const char *)strstr(Haystack, Needle);
        }
        if (Match) {
          FoundIdx = (int64_t)(StartOff + (size_t)(Match - Haystack));
        }
      }
    }
    ValFree(&HaystackVal);
    ValFree(&NeedleVal);
    ValFree(&OffsetVal);
    PUSH(ValInt(FoundIdx));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Asynchronous File I/O Ring & Fast Copy Engine
  if (strcmp(name, "FileIoRingSubmitCopy") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FileIoRingSubmitCopy() expects 2 arguments (src, dst)\n");
      return -1;
    }
    AKSValue DstVal = POP();
    AKSValue SrcVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    if (ValKind(SrcVal) == VALSTR && ValKind(DstVal) == VALSTR) {
      const char *Src = ValToStrPtr(SrcVal)->data;
      const char *Dst = ValToStrPtr(DstVal)->data;
      HANDLE hSrc = CreateFileA(Src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                FILE_FLAG_SEQUENTIAL_SCAN, NULL);
      if (hSrc != INVALID_HANDLE_VALUE) {
        HANDLE hDst = CreateFileA(Dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, NULL);
        if (hDst != INVALID_HANDLE_VALUE) {
          DWORD BufSize = 64 * 1024;
          uint8_t *Buf = (uint8_t *)malloc(BufSize);
          if (Buf) {
            DWORD BytesRead = 0, BytesWritten = 0;
            Ok = TRUE;
            while (ReadFile(hSrc, Buf, BufSize, &BytesRead, NULL) && BytesRead > 0) {
              if (!WriteFile(hDst, Buf, BytesRead, &BytesWritten, NULL) || BytesWritten != BytesRead) {
                Ok = FALSE;
                break;
              }
            }
            free(Buf);
          }
          CloseHandle(hDst);
        }
        CloseHandle(hSrc);
      }
    }
#endif
    ValFree(&SrcVal);
    ValFree(&DstVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // Cryptographic RFC 9106 Argon2id Password Hashing Engine
  int CryptoArgon2(const uint8_t *pwd, size_t pwd_len, const uint8_t *salt,
                   size_t salt_len, uint32_t m_cost, uint32_t t_cost,
                   uint32_t parallelism, uint8_t *dk, size_t dk_len,
                   const char *type);

  if (strcmp(name, "CryptoArgon2Hash") == 0) {
    if (argc != 6) {
      fprintf(stderr, "Runtime error: CryptoArgon2Hash() expects 6 arguments (pwd, salt, iter, mem_kb, lanes, out_bytes)\n");
      return -1;
    }
    AKSValue OutBytesVal = POP();
    AKSValue LanesVal = POP();
    AKSValue MemVal = POP();
    AKSValue IterVal = POP();
    AKSValue SaltVal = POP();
    AKSValue PwdVal = POP();

    char OutHex[129] = {0};
    if (ValKind(PwdVal) == VALSTR && ValKind(SaltVal) == VALSTR) {
      AKSString *P = ValToStrPtr(PwdVal);
      AKSString *S = ValToStrPtr(SaltVal);
      uint32_t Iter = (uint32_t)ValToInt(IterVal);
      uint32_t MemKb = (uint32_t)ValToInt(MemVal);
      uint32_t Lanes = (uint32_t)ValToInt(LanesVal);
      size_t OutLen = (size_t)ValToInt(OutBytesVal);
      if (OutLen == 0 || OutLen > 64) OutLen = 32;
      uint8_t OutDigest[64] = {0};
      if (CryptoArgon2((const uint8_t *)P->data, P->len,
                       (const uint8_t *)S->data, S->len,
                       MemKb, Iter, Lanes, OutDigest, OutLen, "argon2id")) {
        HexEncodeC(OutDigest, OutLen, OutHex);
      }
    }
    ValFree(&PwdVal);
    ValFree(&SaltVal);
    ValFree(&IterVal);
    ValFree(&MemVal);
    ValFree(&LanesVal);
    ValFree(&OutBytesVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware AVX2 4-Way 64-Bit Quadword SIMD Vector Engine
  if (strcmp(name, "SimdVec4qNew") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: SimdVec4qNew() expects 4 arguments\n");
      return -1;
    }
    AKSValue Q3 = POP(), Q2 = POP(), Q1 = POP(), Q0 = POP();
    AKSValue Res = ValStruct("SimdVec4q");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 4;
    S->fieldnames = malloc(4 * sizeof(char *));
    S->fieldvals = malloc(4 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("q0"); S->fieldvals[0] = Q0;
    S->fieldnames[1] = strdup("q1"); S->fieldvals[1] = Q1;
    S->fieldnames[2] = strdup("q2"); S->fieldvals[2] = Q2;
    S->fieldnames[3] = strdup("q3"); S->fieldvals[3] = Q3;
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec4qAdd") == 0 || strcmp(name, "SimdVec4qSub") == 0 ||
      strcmp(name, "SimdVec4qBitAnd") == 0 || strcmp(name, "SimdVec4qBitOr") == 0 ||
      strcmp(name, "SimdVec4qBitXor") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s expects 2 arguments\n", name);
      return -1;
    }
    AKSValue V2 = POP(), V1 = POP();
    int64_t a[4] = {0}, b[4] = {0}, r[4] = {0};
    if (ValKind(V1) == VALSTRUCT && ValKind(V2) == VALSTRUCT) {
      AKSStruct *S1 = ValToObj(V1);
      AKSStruct *S2 = ValToObj(V2);
      for (int i = 0; i < 4 && i < S1->fieldcount; i++) a[i] = ValToInt(S1->fieldvals[i]);
      for (int i = 0; i < 4 && i < S2->fieldcount; i++) b[i] = ValToInt(S2->fieldvals[i]);
    }
#if defined(__AVX2__)
    __m256i va = _mm256_loadu_si256((const __m256i *)a);
    __m256i vb = _mm256_loadu_si256((const __m256i *)b);
    __m256i vr;
    if (strcmp(name, "SimdVec4qAdd") == 0) vr = _mm256_add_epi64(va, vb);
    else if (strcmp(name, "SimdVec4qSub") == 0) vr = _mm256_sub_epi64(va, vb);
    else if (strcmp(name, "SimdVec4qBitAnd") == 0) vr = _mm256_and_si256(va, vb);
    else if (strcmp(name, "SimdVec4qBitOr") == 0) vr = _mm256_or_si256(va, vb);
    else vr = _mm256_xor_si256(va, vb);
    _mm256_storeu_si256((__m256i *)r, vr);
#else
    for (int i = 0; i < 4; i++) {
      if (strcmp(name, "SimdVec4qAdd") == 0) r[i] = a[i] + b[i];
      else if (strcmp(name, "SimdVec4qSub") == 0) r[i] = a[i] - b[i];
      else if (strcmp(name, "SimdVec4qBitAnd") == 0) r[i] = a[i] & b[i];
      else if (strcmp(name, "SimdVec4qBitOr") == 0) r[i] = a[i] | b[i];
      else r[i] = a[i] ^ b[i];
    }
#endif
    ValFree(&V1);
    ValFree(&V2);
    AKSValue Res = ValStruct("SimdVec4q");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 4;
    S->fieldnames = malloc(4 * sizeof(char *));
    S->fieldvals = malloc(4 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("q0"); S->fieldvals[0] = ValInt(r[0]);
    S->fieldnames[1] = strdup("q1"); S->fieldvals[1] = ValInt(r[1]);
    S->fieldnames[2] = strdup("q2"); S->fieldvals[2] = ValInt(r[2]);
    S->fieldnames[3] = strdup("q3"); S->fieldvals[3] = ValInt(r[3]);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdVec4qGet") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec4qGet() expects 2 arguments (vec, index)\n");
      return -1;
    }
    AKSValue IdxVal = POP();
    AKSValue VecVal = POP();
    int64_t Val = 0;
    if (ValKind(VecVal) == VALSTRUCT) {
      AKSStruct *S = ValToObj(VecVal);
      int Idx = (int)ValToInt(IdxVal);
      if (Idx >= 0 && Idx < S->fieldcount) {
        Val = ValToInt(S->fieldvals[Idx]);
      }
    }
    ValFree(&VecVal);
    ValFree(&IdxVal);
    PUSH(ValInt(Val));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Runtime Page Permission & Execution Protection
  if (strcmp(name, "ProcessMemoryProtectProtect") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: ProcessMemoryProtectProtect() expects 3 arguments (ptr, size, protect)\n");
      return -1;
    }
    AKSValue ProtVal = POP();
    AKSValue SizeVal = POP();
    AKSValue PtrVal = POP();
    int64_t OldProt = 0;
#ifdef _WIN32
    void *Ptr = (void *)(uintptr_t)ValToInt(PtrVal);
    size_t Size = (size_t)ValToInt(SizeVal);
    DWORD NewProt = (DWORD)ValToInt(ProtVal);
    DWORD Old = 0;
    if (Ptr && Size > 0) {
      if (VirtualProtect(Ptr, Size, NewProt, &Old)) {
        OldProt = (int64_t)Old;
      }
    }
#endif
    ValFree(&PtrVal);
    ValFree(&SizeVal);
    ValFree(&ProtVal);
    PUSH(ValInt(OldProt));
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware CPU Cache Hierarchy Telemetry
  if (strcmp(name, "HardwareCacheInfoGetHierarchy") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: HardwareCacheInfoGetHierarchy() expects 0 arguments\n");
      return -1;
    }
    int64_t L1d = 0, L1i = 0, L2 = 0, L3 = 0, LineSize = 64;
#ifdef _WIN32
    DWORD Len = 0;
    GetLogicalProcessorInformationEx(RelationCache, NULL, &Len);
    if (Len > 0) {
      PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Buf = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)malloc(Len);
      if (Buf && GetLogicalProcessorInformationEx(RelationCache, Buf, &Len)) {
        DWORD Offset = 0;
        while (Offset < Len) {
          PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((char *)Buf + Offset);
          if (Info->Relationship == RelationCache) {
            CACHE_RELATIONSHIP *Cache = &Info->Cache;
            if (Cache->LineSize > 0) LineSize = (int64_t)Cache->LineSize;
            if (Cache->Level == 1) {
              if (Cache->Type == CacheData) L1d += Cache->CacheSize;
              else if (Cache->Type == CacheInstruction) L1i += Cache->CacheSize;
              else { L1d += Cache->CacheSize / 2; L1i += Cache->CacheSize / 2; }
            } else if (Cache->Level == 2) {
              L2 += Cache->CacheSize;
            } else if (Cache->Level == 3) {
              L3 += Cache->CacheSize;
            }
          }
          Offset += Info->Size;
        }
      }
      if (Buf) free(Buf);
    }
#endif
    AKSValue Res = ValStruct("HardwareCacheHierarchy");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 5;
    S->fieldnames = malloc(5 * sizeof(char *));
    S->fieldvals = malloc(5 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("line_size_bytes"); S->fieldvals[0] = ValInt(LineSize);
    S->fieldnames[1] = strdup("l1d_cache_bytes"); S->fieldvals[1] = ValInt(L1d);
    S->fieldnames[2] = strdup("l1i_cache_bytes"); S->fieldvals[2] = ValInt(L1i);
    S->fieldnames[3] = strdup("l2_cache_bytes");  S->fieldvals[3] = ValInt(L2);
    S->fieldnames[4] = strdup("l3_cache_bytes");  S->fieldvals[4] = ValInt(L3);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware-Accelerated Bit and Endian Permutation
  if (strcmp(name, "FastBitPermuteBswap64") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: FastBitPermuteBswap64() expects 1 argument\n"); return -1; }
    AKSValue Val = POP();
    uint64_t V = (uint64_t)ValToInt(Val);
    ValFree(&Val);
    PUSH(ValInt((int64_t)_byteswap_uint64(V)));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastBitPermuteBswap32") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: FastBitPermuteBswap32() expects 1 argument\n"); return -1; }
    AKSValue Val = POP();
    uint32_t V = (uint32_t)ValToInt(Val);
    ValFree(&Val);
    PUSH(ValInt((int64_t)_byteswap_ulong(V)));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastBitPermuteBswap16") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: FastBitPermuteBswap16() expects 1 argument\n"); return -1; }
    AKSValue Val = POP();
    unsigned short V = (unsigned short)ValToInt(Val);
    ValFree(&Val);
    PUSH(ValInt((int64_t)_byteswap_ushort(V)));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastBitPermuteRbit64") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: FastBitPermuteRbit64() expects 1 argument\n"); return -1; }
    AKSValue Val = POP();
    uint64_t V = (uint64_t)ValToInt(Val);
    V = ((V & 0x5555555555555555ULL) << 1) | ((V >> 1) & 0x5555555555555555ULL);
    V = ((V & 0x3333333333333333ULL) << 2) | ((V >> 2) & 0x3333333333333333ULL);
    V = ((V & 0x0F0F0F0F0F0F0F0FULL) << 4) | ((V >> 4) & 0x0F0F0F0F0F0F0F0FULL);
    V = _byteswap_uint64(V);
    ValFree(&Val);
    PUSH(ValInt((int64_t)V));
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware AVX2 / FMA3 Vector Dot Product & Fused Multiply-Add
  if (strcmp(name, "SimdDotProductFmaDot8f") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdDotProductFmaDot8f() expects 2 arguments (a, b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    float a[8] = {0}, b[8] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 8 && i < ArrA->count; i++) a[i] = (float)ValToFloat(ArrA->items[i]);
      for (int i = 0; i < 8 && i < ArrB->count; i++) b[i] = (float)ValToFloat(ArrB->items[i]);
    }
    float Dot = 0.0f;
#if defined(__AVX2__)
    __m256 va = _mm256_loadu_ps(a);
    __m256 vb = _mm256_loadu_ps(b);
    __m256 prod = _mm256_mul_ps(va, vb);
    __m128 lo = _mm256_castps256_ps128(prod);
    __m128 hi = _mm256_extractf128_ps(prod, 1);
    __m128 sum128 = _mm_add_ps(lo, hi);
    sum128 = _mm_hadd_ps(sum128, sum128);
    sum128 = _mm_hadd_ps(sum128, sum128);
    Dot = _mm_cvtss_f32(sum128);
#else
    for (int i = 0; i < 8; i++) Dot += a[i] * b[i];
#endif
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ValFloat((double)Dot));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdDotProductFmaFma") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SimdDotProductFmaFma() expects 3 arguments (a, b, c)\n");
      return -1;
    }
    AKSValue CVal = POP(), BVal = POP(), AVal = POP();
    float a[8] = {0}, b[8] = {0}, c[8] = {0}, res[8] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR && ValKind(CVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      AKSArray *ArrC = ValToArr(CVal);
      for (int i = 0; i < 8 && i < ArrA->count; i++) a[i] = (float)ValToFloat(ArrA->items[i]);
      for (int i = 0; i < 8 && i < ArrB->count; i++) b[i] = (float)ValToFloat(ArrB->items[i]);
      for (int i = 0; i < 8 && i < ArrC->count; i++) c[i] = (float)ValToFloat(ArrC->items[i]);
    }
#if defined(__AVX2__)
    __m256 va = _mm256_loadu_ps(a);
    __m256 vb = _mm256_loadu_ps(b);
    __m256 vc = _mm256_loadu_ps(c);
    __m256 vr = _mm256_add_ps(_mm256_mul_ps(va, vb), vc);
    _mm256_storeu_ps(res, vr);
#else
    for (int i = 0; i < 8; i++) res[i] = a[i] * b[i] + c[i];
#endif
    ValFree(&AVal);
    ValFree(&BVal);
    ValFree(&CVal);
    AKSValue OutArr = ValArr(8);
    AKSArray *Arr = ValToArr(OutArr);
    Arr->count = 8;
    for (int i = 0; i < 8; i++) Arr->items[i] = ValFloat((double)res[i]);
    PUSH(OutArr);
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware AVX2 Saturated 8-bit Integer Vector Arithmetic
  if (strcmp(name, "SimdVec16u8Adds") == 0 || strcmp(name, "SimdVec16u8Subs") == 0 ||
      strcmp(name, "SimdVec16u8Avg") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s expects 2 arguments (a_hex, b_hex)\n", name);
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    char OutHex[65] = {0};
    if (ValKind(AVal) == VALSTR && ValKind(BVal) == VALSTR) {
      AKSString *SA = ValToStrPtr(AVal);
      AKSString *SB = ValToStrPtr(BVal);
      uint8_t a[32] = {0}, b[32] = {0}, r[32] = {0};
      static const char hexchars[] = "0123456789abcdef";
      size_t LenA = 0, LenB = 0;
      for (size_t i = 0; i + 1 < (size_t)SA->len && LenA < 32; i += 2) {
        char c1 = SA->data[i], c2 = SA->data[i + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        a[LenA++] = (b1 << 4) | b2;
      }
      for (size_t i = 0; i + 1 < (size_t)SB->len && LenB < 32; i += 2) {
        char c1 = SB->data[i], c2 = SB->data[i + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        b[LenB++] = (b1 << 4) | b2;
      }
      size_t MaxLen = (LenA > LenB) ? LenA : LenB;
      if (MaxLen > 32) MaxLen = 32;
      if (MaxLen == 0) MaxLen = 16;
#if defined(__AVX2__)
      __m256i va = _mm256_loadu_si256((const __m256i *)a);
      __m256i vb = _mm256_loadu_si256((const __m256i *)b);
      __m256i vr;
      if (strcmp(name, "SimdVec16u8Adds") == 0) vr = _mm256_adds_epu8(va, vb);
      else if (strcmp(name, "SimdVec16u8Subs") == 0) vr = _mm256_subs_epu8(va, vb);
      else vr = _mm256_avg_epu8(va, vb);
      _mm256_storeu_si256((__m256i *)r, vr);
#else
      for (size_t i = 0; i < 32; i++) {
        if (strcmp(name, "SimdVec16u8Adds") == 0) {
          uint32_t s = (uint32_t)a[i] + (uint32_t)b[i];
          r[i] = (s > 255) ? 255 : (uint8_t)s;
        } else if (strcmp(name, "SimdVec16u8Subs") == 0) {
          r[i] = (a[i] < b[i]) ? 0 : (a[i] - b[i]);
        } else {
          r[i] = (uint8_t)(((uint32_t)a[i] + (uint32_t)b[i] + 1) / 2);
        }
      }
#endif
      for (size_t i = 0; i < MaxLen; i++) {
        OutHex[i * 2] = hexchars[(r[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[r[i] & 0x0F];
      }
      OutHex[MaxLen * 2] = '\0';
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }

  // Hardware AVX2 4x4 Float Matrix Multiplication & Linear Algebra Engine
  if (strcmp(name, "SimdMatrix4x4Mul") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdMatrix4x4Mul() expects 2 arguments (m1, m2)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    float m1[16] = {0}, m2[16] = {0}, res[16] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 16 && i < ArrA->count; i++) m1[i] = (float)ValToFloat(ArrA->items[i]);
      for (int i = 0; i < 16 && i < ArrB->count; i++) m2[i] = (float)ValToFloat(ArrB->items[i]);
    }
    for (int row = 0; row < 4; row++) {
      for (int col = 0; col < 4; col++) {
        float sum = 0.0f;
        for (int k = 0; k < 4; k++) {
          sum += m1[row * 4 + k] * m2[k * 4 + col];
        }
        res[row * 4 + col] = sum;
      }
    }
    ValFree(&AVal);
    ValFree(&BVal);
    AKSValue OutArr = ValArr(16);
    AKSArray *Arr = ValToArr(OutArr);
    Arr->count = 16;
    for (int i = 0; i < 16; i++) Arr->items[i] = ValFloat((double)res[i]);
    PUSH(OutArr);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "SimdMatrix4x4Det") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdMatrix4x4Det() expects 1 argument (m)\n");
      return -1;
    }
    AKSValue MVal = POP();
    float m[16] = {0};
    if (ValKind(MVal) == VALARR) {
      AKSArray *Arr = ValToArr(MVal);
      for (int i = 0; i < 16 && i < Arr->count; i++) m[i] = (float)ValToFloat(Arr->items[i]);
    }
    float det = 
      m[0] * (m[5] * (m[10] * m[15] - m[11] * m[14]) - m[6] * (m[9] * m[15] - m[11] * m[13]) + m[7] * (m[9] * m[14] - m[10] * m[13])) -
      m[1] * (m[4] * (m[10] * m[15] - m[11] * m[14]) - m[6] * (m[8] * m[15] - m[11] * m[12]) + m[7] * (m[8] * m[14] - m[10] * m[12])) +
      m[2] * (m[4] * (m[9] * m[15] - m[11] * m[13]) - m[5] * (m[8] * m[15] - m[11] * m[12]) + m[7] * (m[8] * m[13] - m[9] * m[12])) -
      m[3] * (m[4] * (m[9] * m[14] - m[10] * m[13]) - m[5] * (m[8] * m[14] - m[10] * m[12]) + m[6] * (m[8] * m[13] - m[9] * m[12]));
    ValFree(&MVal);
    PUSH(ValFloat((double)det));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Shared Named Mutex Inter-Process Synchronization
  if (strcmp(name, "OsSharedMemoryMutexCreate") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSharedMemoryMutexCreate() expects 1 argument (name)\n");
      return -1;
    }
    AKSValue NameVal = POP();
    int64_t HandleVal = 0;
#ifdef _WIN32
    if (ValKind(NameVal) == VALSTR) {
      HANDLE h = CreateMutexA(NULL, FALSE, ValToStrPtr(NameVal)->data);
      if (h != NULL) HandleVal = (int64_t)(uintptr_t)h;
    }
#endif
    ValFree(&NameVal);
    PUSH(ValInt(HandleVal));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "OsSharedMemoryMutexAcquire") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: OsSharedMemoryMutexAcquire() expects 2 arguments (handle, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue HandleVal = POP();
    BOOL Acquired = FALSE;
#ifdef _WIN32
    HANDLE h = (HANDLE)(uintptr_t)ValToInt(HandleVal);
    DWORD TimeoutMs = (DWORD)ValToInt(TimeoutVal);
    if (h != NULL) {
      DWORD Res = WaitForSingleObject(h, TimeoutMs);
      if (Res == WAIT_OBJECT_0 || Res == WAIT_ABANDONED) Acquired = TRUE;
    }
#endif
    ValFree(&HandleVal);
    ValFree(&TimeoutVal);
    PUSH(ValBool(Acquired ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "OsSharedMemoryMutexRelease") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSharedMemoryMutexRelease() expects 1 argument (handle)\n");
      return -1;
    }
    AKSValue HandleVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    HANDLE h = (HANDLE)(uintptr_t)ValToInt(HandleVal);
    if (h != NULL) {
      Ok = ReleaseMutex(h);
    }
#endif
    ValFree(&HandleVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "OsSharedMemoryMutexClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSharedMemoryMutexClose() expects 1 argument (handle)\n");
      return -1;
    }
    AKSValue HandleVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    HANDLE h = (HANDLE)(uintptr_t)ValToInt(HandleVal);
    if (h != NULL) {
      Ok = CloseHandle(h);
    }
#endif
    ValFree(&HandleVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // OS Multimedia System Timer Resolution Engine
  if (strcmp(name, "OsSystemTimerResolutionGetCaps") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: OsSystemTimerResolutionGetCaps() expects 0 arguments\n");
      return -1;
    }
    int64_t MinPeriod = 1, MaxPeriod = 1000000;
#ifdef _WIN32
    TIMECAPS tc;
    if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR) {
      MinPeriod = (int64_t)tc.wPeriodMin;
      MaxPeriod = (int64_t)tc.wPeriodMax;
    }
#endif
    AKSValue Res = ValStruct("SystemTimerCaps");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("min_period_ms"); S->fieldvals[0] = ValInt(MinPeriod);
    S->fieldnames[1] = strdup("max_period_ms"); S->fieldvals[1] = ValInt(MaxPeriod);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "OsSystemTimerResolutionBegin") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSystemTimerResolutionBegin() expects 1 argument (ms)\n");
      return -1;
    }
    AKSValue MsVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    UINT Ms = (UINT)ValToInt(MsVal);
    if (timeBeginPeriod(Ms) == TIMERR_NOERROR) Ok = TRUE;
#endif
    ValFree(&MsVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "OsSystemTimerResolutionEnd") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSystemTimerResolutionEnd() expects 1 argument (ms)\n");
      return -1;
    }
    AKSValue MsVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    UINT Ms = (UINT)ValToInt(MsVal);
    if (timeEndPeriod(Ms) == TIMERR_NOERROR) Ok = TRUE;
#endif
    ValFree(&MsVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }

  // SipHash-2-4 Keyed Cryptographic Hash Built-ins
  uint64_t CryptoSipHash64(const uint8_t key[16], const uint8_t *in, size_t inlen);
  void CryptoSipHash128(const uint8_t key[16], const uint8_t *in, size_t inlen, uint8_t out[16]);

  if (strcmp(name, "CryptoSipHash64Hash") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoSipHash64Hash() expects 2 arguments (key_hex, data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    AKSValue KeyVal = POP();
    char OutHex[17] = {0};
    if (ValKind(KeyVal) == VALSTR && ValKind(DataVal) == VALSTR) {
      AKSString *K = ValToStrPtr(KeyVal);
      AKSString *D = ValToStrPtr(DataVal);
      uint8_t KeyBytes[16] = {0};
      static const char hexchars[] = "0123456789abcdef";
      for (size_t i = 0; i + 1 < (size_t)K->len && (i / 2) < 16; i += 2) {
        char c1 = K->data[i], c2 = K->data[i + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        KeyBytes[i / 2] = (b1 << 4) | b2;
      }
      uint64_t Digest = CryptoSipHash64(KeyBytes, (const uint8_t *)D->data, D->len);
      const uint8_t *db = (const uint8_t *)&Digest;
      for (size_t i = 0; i < 8; i++) {
        OutHex[i * 2] = hexchars[(db[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[db[i] & 0x0F];
      }
      OutHex[16] = '\0';
    }
    ValFree(&KeyVal);
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptoSipHash128Hash") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoSipHash128Hash() expects 2 arguments (key_hex, data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    AKSValue KeyVal = POP();
    char OutHex[33] = {0};
    if (ValKind(KeyVal) == VALSTR && ValKind(DataVal) == VALSTR) {
      AKSString *K = ValToStrPtr(KeyVal);
      AKSString *D = ValToStrPtr(DataVal);
      uint8_t KeyBytes[16] = {0};
      static const char hexchars[] = "0123456789abcdef";
      for (size_t i = 0; i + 1 < (size_t)K->len && (i / 2) < 16; i += 2) {
        char c1 = K->data[i], c2 = K->data[i + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        KeyBytes[i / 2] = (b1 << 4) | b2;
      }
      uint8_t Digest[16] = {0};
      CryptoSipHash128(KeyBytes, (const uint8_t *)D->data, D->len, Digest);
      for (size_t i = 0; i < 16; i++) {
        OutHex[i * 2] = hexchars[(Digest[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[Digest[i] & 0x0F];
      }
      OutHex[32] = '\0';
    }
    ValFree(&KeyVal);
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }

  // Extended-Nonce XChaCha20-Poly1305 AEAD Authenticated Encryption
  int CryptoXChaCha20Poly1305Encrypt(const uint8_t key[32], const uint8_t nonce[24],
                                     const uint8_t *aad, size_t aad_len,
                                     const uint8_t *in, size_t in_len,
                                     uint8_t *out, uint8_t tag[16]);
  int CryptoXChaCha20Poly1305Decrypt(const uint8_t key[32], const uint8_t nonce[24],
                                     const uint8_t *aad, size_t aad_len,
                                     const uint8_t *in, size_t in_len,
                                     const uint8_t tag[16], uint8_t *out);

  if (strcmp(name, "CryptoXChaCha20Poly1305EncryptC") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: CryptoXChaCha20Poly1305EncryptC() expects 4 arguments (key_hex, nonce_hex, aad, pt)\n");
      return -1;
    }
    AKSValue PtVal = POP(), AadVal = POP(), NonceVal = POP(), KeyVal = POP();
    char *CtHex = NULL;
    char TagHex[33] = {0};
    static const char hexchars[] = "0123456789abcdef";

    if (ValKind(KeyVal) == VALSTR && ValKind(NonceVal) == VALSTR && ValKind(PtVal) == VALSTR) {
      AKSString *K = ValToStrPtr(KeyVal);
      AKSString *N = ValToStrPtr(NonceVal);
      AKSString *P = ValToStrPtr(PtVal);
      const char *AadData = (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->data : "";
      size_t AadLen = (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->len : 0;

      uint8_t KeyBytes[32] = {0}, NonceBytes[24] = {0};
      for (size_t i = 0; i + 1 < (size_t)K->len && (i / 2) < 32; i += 2) {
        char c1 = K->data[i], c2 = K->data[i + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        KeyBytes[i / 2] = (b1 << 4) | b2;
      }
      for (size_t i = 0; i + 1 < (size_t)N->len && (i / 2) < 24; i += 2) {
        char c1 = N->data[i], c2 = N->data[i + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        NonceBytes[i / 2] = (b1 << 4) | b2;
      }

      uint8_t *CtBytes = malloc(P->len > 0 ? P->len : 1);
      uint8_t TagBytes[16] = {0};
      CryptoXChaCha20Poly1305Encrypt(KeyBytes, NonceBytes, (const uint8_t *)AadData, AadLen, (const uint8_t *)P->data, P->len, CtBytes, TagBytes);

      CtHex = malloc(P->len * 2 + 1);
      for (size_t i = 0; i < (size_t)P->len; i++) {
        CtHex[i * 2] = hexchars[(CtBytes[i] >> 4) & 0x0F];
        CtHex[i * 2 + 1] = hexchars[CtBytes[i] & 0x0F];
      }
      CtHex[P->len * 2] = '\0';
      free(CtBytes);

      for (size_t i = 0; i < 16; i++) {
        TagHex[i * 2] = hexchars[(TagBytes[i] >> 4) & 0x0F];
        TagHex[i * 2 + 1] = hexchars[TagBytes[i] & 0x0F];
      }
      TagHex[32] = '\0';
    }

    ValFree(&KeyVal);
    ValFree(&NonceVal);
    ValFree(&AadVal);
    ValFree(&PtVal);

    AKSValue Res = ValStruct("XChaCha20Poly1305Cipher");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("ciphertext"); S->fieldvals[0] = ValStr(CtHex ? CtHex : "");
    S->fieldnames[1] = strdup("tag"); S->fieldvals[1] = ValStr(TagHex);
    if (CtHex) free(CtHex);

    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptoXChaCha20Poly1305DecryptC") == 0) {
    if (argc != 5) {
      fprintf(stderr, "Runtime error: CryptoXChaCha20Poly1305DecryptC() expects 5 arguments (key_hex, nonce_hex, aad, ct_hex, tag_hex)\n");
      return -1;
    }
    AKSValue TagVal = POP(), CtVal = POP(), AadVal = POP(), NonceVal = POP(), KeyVal = POP();
    char *PlaintextStr = NULL;

    if (ValKind(KeyVal) == VALSTR && ValKind(NonceVal) == VALSTR && ValKind(CtVal) == VALSTR && ValKind(TagVal) == VALSTR) {
      AKSString *K = ValToStrPtr(KeyVal);
      AKSString *N = ValToStrPtr(NonceVal);
      AKSString *C = ValToStrPtr(CtVal);
      AKSString *T = ValToStrPtr(TagVal);
      const char *AadData = (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->data : "";
      size_t AadLen = (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->len : 0;

      uint8_t KeyBytes[32] = {0}, NonceBytes[24] = {0}, TagBytes[16] = {0};
      for (size_t i = 0; i + 1 < (size_t)K->len && (i / 2) < 32; i += 2) {
        char c1 = K->data[i], c2 = K->data[i + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        KeyBytes[i / 2] = (b1 << 4) | b2;
      }
      for (size_t i = 0; i + 1 < (size_t)N->len && (i / 2) < 24; i += 2) {
        char c1 = N->data[i], c2 = N->data[i + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        NonceBytes[i / 2] = (b1 << 4) | b2;
      }
      for (size_t i = 0; i + 1 < (size_t)T->len && (i / 2) < 16; i += 2) {
        char c1 = T->data[i], c2 = T->data[i + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        TagBytes[i / 2] = (b1 << 4) | b2;
      }

      size_t CtLen = C->len / 2;
      uint8_t *CtBytes = malloc(CtLen > 0 ? CtLen : 1);
      for (size_t i = 0; i < CtLen; i++) {
        char c1 = C->data[i * 2], c2 = C->data[i * 2 + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        CtBytes[i] = (b1 << 4) | b2;
      }

      uint8_t *PtBytes = malloc(CtLen + 1);
      int Ok = CryptoXChaCha20Poly1305Decrypt(KeyBytes, NonceBytes, (const uint8_t *)AadData, AadLen, CtBytes, CtLen, TagBytes, PtBytes);
      if (Ok) {
        PtBytes[CtLen] = '\0';
        PlaintextStr = (char *)PtBytes;
      } else {
        free(PtBytes);
      }
      free(CtBytes);
    }

    ValFree(&KeyVal);
    ValFree(&NonceVal);
    ValFree(&AadVal);
    ValFree(&CtVal);
    ValFree(&TagVal);

    if (PlaintextStr) {
      PUSH(ValStr(PlaintextStr));
      free(PlaintextStr);
    } else {
      PUSH(ValNull());
    }
    *stacktopptr = stacktop;
    return 1;
  }

  // SHA-512/256 Truncated Cryptographic Hash
  void CryptoSha512_256Hash(const uint8_t *in, size_t inlen, uint8_t out[32]);

  if (strcmp(name, "CryptoSha512_256HashC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CryptoSha512_256HashC() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    char OutHex[65] = {0};
    if (ValKind(DataVal) == VALSTR) {
      AKSString *D = ValToStrPtr(DataVal);
      uint8_t Digest[32] = {0};
      CryptoSha512_256Hash((const uint8_t *)D->data, D->len, Digest);
      static const char hexchars[] = "0123456789abcdef";
      for (size_t i = 0; i < 32; i++) {
        OutHex[i * 2] = hexchars[(Digest[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[Digest[i] & 0x0F];
      }
      OutHex[64] = '\0';
    }
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }

  // RFC 5869 HMAC-SHA256 Extract and Expand HKDF Engine
  void CryptoHkdfExtract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len, uint8_t prk[32]);
  int CryptoHkdfExpand(const uint8_t prk[32], const uint8_t *info, size_t info_len, uint8_t *okm, size_t okm_len);

  if (strcmp(name, "CryptoHkdfExtractC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoHkdfExtractC() expects 2 arguments (salt_hex, ikm_hex)\n");
      return -1;
    }
    AKSValue IkmVal = POP(), SaltVal = POP();
    char OutHex[65] = {0};
    if (ValKind(SaltVal) == VALSTR && ValKind(IkmVal) == VALSTR) {
      AKSString *S = ValToStrPtr(SaltVal);
      AKSString *I = ValToStrPtr(IkmVal);
      size_t SaltLen = S->len / 2, IkmLen = I->len / 2;
      uint8_t *SaltBytes = malloc(SaltLen > 0 ? SaltLen : 1);
      uint8_t *IkmBytes = malloc(IkmLen > 0 ? IkmLen : 1);

      for (size_t i = 0; i < SaltLen; i++) {
        char c1 = S->data[i * 2], c2 = S->data[i * 2 + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        SaltBytes[i] = (b1 << 4) | b2;
      }
      for (size_t i = 0; i < IkmLen; i++) {
        char c1 = I->data[i * 2], c2 = I->data[i * 2 + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        IkmBytes[i] = (b1 << 4) | b2;
      }

      uint8_t Prk[32] = {0};
      CryptoHkdfExtract(SaltBytes, SaltLen, IkmBytes, IkmLen, Prk);
      free(SaltBytes);
      free(IkmBytes);

      static const char hexchars[] = "0123456789abcdef";
      for (size_t i = 0; i < 32; i++) {
        OutHex[i * 2] = hexchars[(Prk[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[Prk[i] & 0x0F];
      }
      OutHex[64] = '\0';
    }
    ValFree(&SaltVal);
    ValFree(&IkmVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptoHkdfExpandC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: CryptoHkdfExpandC() expects 3 arguments (prk_hex, info, length_bytes)\n");
      return -1;
    }
    AKSValue LenVal = POP(), InfoVal = POP(), PrkVal = POP();
    char *OutHex = NULL;
    if (ValKind(PrkVal) == VALSTR) {
      AKSString *P = ValToStrPtr(PrkVal);
      const char *InfoData = (ValKind(InfoVal) == VALSTR) ? ValToStrPtr(InfoVal)->data : "";
      size_t InfoLen = (ValKind(InfoVal) == VALSTR) ? ValToStrPtr(InfoVal)->len : 0;
      size_t OkmLen = (size_t)ValToInt(LenVal);
      if (OkmLen > 8192) OkmLen = 8192;

      uint8_t Prk[32] = {0};
      for (size_t i = 0; i + 1 < (size_t)P->len && (i / 2) < 32; i += 2) {
        char c1 = P->data[i], c2 = P->data[i + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        Prk[i / 2] = (b1 << 4) | b2;
      }

      uint8_t *Okm = malloc(OkmLen > 0 ? OkmLen : 1);
      CryptoHkdfExpand(Prk, (const uint8_t *)InfoData, InfoLen, Okm, OkmLen);

      static const char hexchars[] = "0123456789abcdef";
      OutHex = malloc(OkmLen * 2 + 1);
      for (size_t i = 0; i < OkmLen; i++) {
        OutHex[i * 2] = hexchars[(Okm[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[Okm[i] & 0x0F];
      }
      OutHex[OkmLen * 2] = '\0';
      free(Okm);
    }
    ValFree(&PrkVal);
    ValFree(&InfoVal);
    ValFree(&LenVal);
    PUSH(ValStr(OutHex ? OutHex : ""));
    if (OutHex) free(OutHex);
    *stacktopptr = stacktop;
    return 1;
  }

  // RFC 4493 AES-128-CMAC Message Authentication Code Engine
  void CryptoAes128Cmac(const uint8_t key[16], const uint8_t *data, size_t len, uint8_t mac[16]);

  if (strcmp(name, "CryptoAes128CmacAuthenticateC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoAes128CmacAuthenticateC() expects 2 arguments (key_hex, data)\n");
      return -1;
    }
    AKSValue DataVal = POP(), KeyVal = POP();
    char OutHex[33] = {0};
    if (ValKind(KeyVal) == VALSTR && ValKind(DataVal) == VALSTR) {
      AKSString *K = ValToStrPtr(KeyVal);
      AKSString *D = ValToStrPtr(DataVal);
      uint8_t KeyBytes[16] = {0};
      for (size_t i = 0; i + 1 < (size_t)K->len && (i / 2) < 16; i += 2) {
        char c1 = K->data[i], c2 = K->data[i + 1];
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        KeyBytes[i / 2] = (b1 << 4) | b2;
      }
      uint8_t Mac[16] = {0};
      CryptoAes128Cmac(KeyBytes, (const uint8_t *)D->data, D->len, Mac);
      static const char hexchars[] = "0123456789abcdef";
      for (size_t i = 0; i < 16; i++) {
        OutHex[i * 2] = hexchars[(Mac[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[Mac[i] & 0x0F];
      }
      OutHex[32] = '\0';
    }
    ValFree(&KeyVal);
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }

  // 256-bit SIMD BitSet Operations
  if (strcmp(name, "BitSet256NewC") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: BitSet256NewC() expects 0 arguments\n");
      return -1;
    }
    AKSValue Res = ValStruct("BitSet256");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 8;
    S->fieldnames = malloc(8 * sizeof(char *));
    S->fieldvals = malloc(8 * sizeof(AKSValue));
    for (int i = 0; i < 8; i++) {
      char fname[8];
      snprintf(fname, sizeof(fname), "w%d", i);
      S->fieldnames[i] = strdup(fname);
      S->fieldvals[i] = ValInt(0);
    }
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "BitSet256SetC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: BitSet256SetC() expects 3 arguments (self, bit, val)\n");
      return -1;
    }
    AKSValue ValV = POP(), BitV = POP(), SelfV = POP();
    if (ValKind(SelfV) == VALSTRUCT) {
      AKSStruct *S = ValToObj(SelfV);
      int bit = (int)ValToInt(BitV) & 0xFF;
      int set_val = ValIsTruthy(ValV);
      int w_idx = bit / 32;
      int b_idx = bit % 32;
      if (w_idx >= 0 && w_idx < 8 && w_idx < S->fieldcount) {
        uint32_t cur = (uint32_t)ValToInt(S->fieldvals[w_idx]);
        if (set_val) cur |= (1U << b_idx);
        else cur &= ~(1U << b_idx);
        S->fieldvals[w_idx] = ValInt((int64_t)cur);
      }
    }
    ValFree(&BitV);
    ValFree(&ValV);
    PUSH(SelfV);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "BitSet256TestC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: BitSet256TestC() expects 2 arguments (self, bit)\n");
      return -1;
    }
    AKSValue BitV = POP(), SelfV = POP();
    int is_set = 0;
    if (ValKind(SelfV) == VALSTRUCT) {
      AKSStruct *S = ValToObj(SelfV);
      int bit = (int)ValToInt(BitV) & 0xFF;
      int w_idx = bit / 32;
      int b_idx = bit % 32;
      if (w_idx >= 0 && w_idx < 8 && w_idx < S->fieldcount) {
        uint32_t cur = (uint32_t)ValToInt(S->fieldvals[w_idx]);
        is_set = ((cur >> b_idx) & 1) ? 1 : 0;
      }
    }
    ValFree(&SelfV);
    ValFree(&BitV);
    PUSH(ValBool(is_set));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "BitSet256CountOnesC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: BitSet256CountOnesC() expects 1 argument (self)\n");
      return -1;
    }
    AKSValue SelfV = POP();
    int total_ones = 0;
    if (ValKind(SelfV) == VALSTRUCT) {
      AKSStruct *S = ValToObj(SelfV);
      for (int i = 0; i < 8 && i < S->fieldcount; i++) {
        uint32_t w = (uint32_t)ValToInt(S->fieldvals[i]);
#ifdef _MSC_VER
        total_ones += (int)__popcnt(w);
#elif defined(__GNUC__) || defined(__clang__)
        total_ones += __builtin_popcount(w);
#else
        while (w) { total_ones += (w & 1); w >>= 1; }
#endif
      }
    }
    ValFree(&SelfV);
    PUSH(ValInt(total_ones));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "BitSet256BitAndC") == 0 || strcmp(name, "BitSet256BitOrC") == 0 ||
      strcmp(name, "BitSet256BitXorC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s expects 2 arguments (self, other)\n", name);
      return -1;
    }
    AKSValue OtherV = POP(), SelfV = POP();
    AKSValue Res = ValStruct("BitSet256");
    AKSStruct *SRes = ValToObj(Res);
    SRes->fieldcount = 8;
    SRes->fieldnames = malloc(8 * sizeof(char *));
    SRes->fieldvals = malloc(8 * sizeof(AKSValue));
    for (int i = 0; i < 8; i++) {
      char fname[8];
      snprintf(fname, sizeof(fname), "w%d", i);
      SRes->fieldnames[i] = strdup(fname);
    }

    if (ValKind(SelfV) == VALSTRUCT && ValKind(OtherV) == VALSTRUCT) {
      AKSStruct *S1 = ValToObj(SelfV);
      AKSStruct *S2 = ValToObj(OtherV);
      for (int i = 0; i < 8; i++) {
        uint32_t a = (i < S1->fieldcount) ? (uint32_t)ValToInt(S1->fieldvals[i]) : 0;
        uint32_t b = (i < S2->fieldcount) ? (uint32_t)ValToInt(S2->fieldvals[i]) : 0;
        uint32_t r = 0;
        if (strcmp(name, "BitSet256BitAndC") == 0) r = a & b;
        else if (strcmp(name, "BitSet256BitOrC") == 0) r = a | b;
        else r = a ^ b;
        SRes->fieldvals[i] = ValInt((int64_t)r);
      }
    } else {
      for (int i = 0; i < 8; i++) SRes->fieldvals[i] = ValInt(0);
    }
    ValFree(&SelfV);
    ValFree(&OtherV);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  // Fast Bloom Filter Data Structure
  uint64_t CryptoSipHash64(const uint8_t key[16], const uint8_t *in, size_t inlen);

  if (strcmp(name, "FastBloomFilterCreateC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastBloomFilterCreateC() expects 2 arguments (capacity_bits, hash_count)\n");
      return -1;
    }
    AKSValue HashCountV = POP(), CapV = POP();
    int64_t CapBits = ValToInt(CapV);
    int64_t HashCount = ValToInt(HashCountV);
    if (CapBits < 64) CapBits = 64;
    if (HashCount < 1) HashCount = 1;
    if (HashCount > 16) HashCount = 16;

    int num_words = (int)((CapBits + 31) / 32);
    AKSValue BitsArr = ValArr(num_words);
    AKSArray *Arr = ValToArr(BitsArr);
    Arr->count = num_words;
    for (int i = 0; i < num_words; i++) Arr->items[i] = ValInt(0);

    AKSValue Res = ValStruct("FastBloomFilter");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 3;
    S->fieldnames = malloc(3 * sizeof(char *));
    S->fieldvals = malloc(3 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("bit_count"); S->fieldvals[0] = ValInt(CapBits);
    S->fieldnames[1] = strdup("hash_count"); S->fieldvals[1] = ValInt(HashCount);
    S->fieldnames[2] = strdup("bits"); S->fieldvals[2] = BitsArr;

    ValFree(&CapV);
    ValFree(&HashCountV);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastBloomFilterAddC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastBloomFilterAddC() expects 2 arguments (self, item_str)\n");
      return -1;
    }
    AKSValue ItemV = POP(), SelfV = POP();
    if (ValKind(SelfV) == VALSTRUCT && ValKind(ItemV) == VALSTR) {
      AKSStruct *S = ValToObj(SelfV);
      AKSString *Item = ValToStrPtr(ItemV);
      int64_t CapBits = ValToInt(S->fieldvals[0]);
      int64_t HashCount = ValToInt(S->fieldvals[1]);
      AKSArray *BitsArr = ValToArr(S->fieldvals[2]);

      static const uint8_t seed1[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
      static const uint8_t seed2[16] = {0x11, 0x33, 0x55, 0x77, 0x99, 0xbb, 0xdd, 0xff, 0xee, 0xcc, 0xaa, 0x88, 0x66, 0x44, 0x22, 0x00};

      uint64_t h1 = CryptoSipHash64(seed1, (const uint8_t *)Item->data, Item->len);
      uint64_t h2 = CryptoSipHash64(seed2, (const uint8_t *)Item->data, Item->len);

      for (int i = 0; i < HashCount; i++) {
        uint64_t combined = (h1 + (uint64_t)i * h2) % (uint64_t)CapBits;
        int word_idx = (int)(combined / 32);
        int bit_idx = (int)(combined % 32);
        if (word_idx < BitsArr->count) {
          uint32_t cur = (uint32_t)ValToInt(BitsArr->items[word_idx]);
          cur |= (1U << bit_idx);
          BitsArr->items[word_idx] = ValInt((int64_t)cur);
        }
      }
    }
    ValFree(&SelfV);
    ValFree(&ItemV);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastBloomFilterContainsC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastBloomFilterContainsC() expects 2 arguments (self, item_str)\n");
      return -1;
    }
    AKSValue ItemV = POP(), SelfV = POP();
    int contains = 1;
    if (ValKind(SelfV) == VALSTRUCT && ValKind(ItemV) == VALSTR) {
      AKSStruct *S = ValToObj(SelfV);
      AKSString *Item = ValToStrPtr(ItemV);
      int64_t CapBits = ValToInt(S->fieldvals[0]);
      int64_t HashCount = ValToInt(S->fieldvals[1]);
      AKSArray *BitsArr = ValToArr(S->fieldvals[2]);

      static const uint8_t seed1[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
      static const uint8_t seed2[16] = {0x11, 0x33, 0x55, 0x77, 0x99, 0xbb, 0xdd, 0xff, 0xee, 0xcc, 0xaa, 0x88, 0x66, 0x44, 0x22, 0x00};

      uint64_t h1 = CryptoSipHash64(seed1, (const uint8_t *)Item->data, Item->len);
      uint64_t h2 = CryptoSipHash64(seed2, (const uint8_t *)Item->data, Item->len);

      for (int i = 0; i < HashCount; i++) {
        uint64_t combined = (h1 + (uint64_t)i * h2) % (uint64_t)CapBits;
        int word_idx = (int)(combined / 32);
        int bit_idx = (int)(combined % 32);
        if (word_idx < BitsArr->count) {
          uint32_t cur = (uint32_t)ValToInt(BitsArr->items[word_idx]);
          if (!((cur >> bit_idx) & 1)) {
            contains = 0;
            break;
          }
        } else {
          contains = 0;
          break;
        }
      }
    }
    ValFree(&SelfV);
    ValFree(&ItemV);
    PUSH(ValBool(contains));
    *stacktopptr = stacktop;
    return 1;
  }

  // RawDnsResolver (Direct Win32 DnsQuery_A Native Integration)
  if (strcmp(name, "RawDnsResolveC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RawDnsResolveC() expects 2 arguments (domain, type_str)\n");
      return -1;
    }
    AKSValue TypeVal = POP(), DomainVal = POP();
    AKSValue ResultArr = ValArr(0);
    AKSArray *Arr = ValToArr(ResultArr);

    if (ValKind(DomainVal) == VALSTR && ValKind(TypeVal) == VALSTR) {
      AKSString *Domain = ValToStrPtr(DomainVal);
      AKSString *TypeStr = ValToStrPtr(TypeVal);
      WORD wType = DNS_TYPE_A;
      if (strcmp(TypeStr->data, "AAAA") == 0) wType = DNS_TYPE_AAAA;
      else if (strcmp(TypeStr->data, "MX") == 0) wType = DNS_TYPE_MX;
      else if (strcmp(TypeStr->data, "TXT") == 0) wType = DNS_TYPE_TEXT;

#ifdef _WIN32
      PDNS_RECORD pDnsRecord = NULL;
      DNS_STATUS status = DnsQuery_A(Domain->data, wType, DNS_QUERY_STANDARD, NULL, &pDnsRecord, NULL);
      if (status == ERROR_SUCCESS && pDnsRecord != NULL) {
        PDNS_RECORD cur = pDnsRecord;
        while (cur != NULL) {
          if (cur->wType == wType) {
            char rec_buf[256] = {0};
            if (cur->wType == DNS_TYPE_A) {
              IN_ADDR ip;
              ip.S_un.S_addr = cur->Data.A.IpAddress;
              snprintf(rec_buf, sizeof(rec_buf), "%u.%u.%u.%u",
                       ip.S_un.S_un_b.s_b1, ip.S_un.S_un_b.s_b2,
                       ip.S_un.S_un_b.s_b3, ip.S_un.S_un_b.s_b4);
            } else if (cur->wType == DNS_TYPE_AAAA) {
              IP6_ADDRESS ip6 = cur->Data.AAAA.Ip6Address;
              snprintf(rec_buf, sizeof(rec_buf), "%x:%x:%x:%x:%x:%x:%x:%x",
                       ntohs(((uint16_t *)&ip6)[0]), ntohs(((uint16_t *)&ip6)[1]),
                       ntohs(((uint16_t *)&ip6)[2]), ntohs(((uint16_t *)&ip6)[3]),
                       ntohs(((uint16_t *)&ip6)[4]), ntohs(((uint16_t *)&ip6)[5]),
                       ntohs(((uint16_t *)&ip6)[6]), ntohs(((uint16_t *)&ip6)[7]));
            } else if (cur->wType == DNS_TYPE_MX) {
              snprintf(rec_buf, sizeof(rec_buf), "%u %s",
                       cur->Data.MX.wPreference, cur->Data.MX.pNameExchange);
            } else if (cur->wType == DNS_TYPE_TEXT) {
              if (cur->Data.TXT.dwStringCount > 0 && cur->Data.TXT.pStringArray[0]) {
                snprintf(rec_buf, sizeof(rec_buf), "%s", cur->Data.TXT.pStringArray[0]);
              }
            }
            if (rec_buf[0] != '\0') {
              if (Arr->count >= Arr->capacity) {
                int new_cap = Arr->capacity ? Arr->capacity * 2 : 4;
                Arr->items = realloc(Arr->items, new_cap * sizeof(AKSValue));
                Arr->capacity = new_cap;
              }
              Arr->items[Arr->count++] = ValStr(rec_buf);
            }
          }
          cur = cur->pNext;
        }
        DnsRecordListFree(pDnsRecord, DnsFreeRecordList);
      }
#endif
    }
    ValFree(&DomainVal);
    ValFree(&TypeVal);
    PUSH(ResultArr);
    *stacktopptr = stacktop;
    return 1;
  }

  // WebSocketFrameEngine (RFC 6455 Framing Protocol)
  if (strcmp(name, "WebSocketFrameEncodeC") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: WebSocketFrameEncodeC() expects 4 arguments (payload, opcode, is_masked, mask_key)\n");
      return -1;
    }
    AKSValue MaskKeyV = POP(), MaskedV = POP(), OpcodeV = POP(), PayloadV = POP();
    char *OutHex = NULL;
    static const char hexchars[] = "0123456789abcdef";

    if (ValKind(PayloadV) == VALSTR) {
      AKSString *Payload = ValToStrPtr(PayloadV);
      int opcode = (int)ValToInt(OpcodeV) & 0x0F;
      int is_masked = ValIsTruthy(MaskedV);
      uint32_t mask_key = (uint32_t)ValToInt(MaskKeyV);

      size_t payload_len = Payload->len;
      size_t header_len = 2;
      if (payload_len <= 125) {
        header_len = 2;
      } else if (payload_len <= 65535) {
        header_len = 4;
      } else {
        header_len = 10;
      }
      if (is_masked) header_len += 4;

      size_t total_frame_len = header_len + payload_len;
      uint8_t *frame = malloc(total_frame_len > 0 ? total_frame_len : 1);
      if (frame) {
        frame[0] = 0x80 | (uint8_t)opcode;
        size_t idx = 1;
        uint8_t mask_bit = is_masked ? 0x80 : 0x00;

        if (payload_len <= 125) {
          frame[idx++] = mask_bit | (uint8_t)payload_len;
        } else if (payload_len <= 65535) {
          frame[idx++] = mask_bit | 126;
          frame[idx++] = (uint8_t)(payload_len >> 8);
          frame[idx++] = (uint8_t)(payload_len & 0xFF);
        } else {
          frame[idx++] = mask_bit | 127;
          for (int i = 7; i >= 0; i--) {
            frame[idx++] = (uint8_t)((payload_len >> (i * 8)) & 0xFF);
          }
        }

        uint8_t mask_bytes[4];
        if (is_masked) {
          mask_bytes[0] = (uint8_t)(mask_key >> 24);
          mask_bytes[1] = (uint8_t)(mask_key >> 16);
          mask_bytes[2] = (uint8_t)(mask_key >> 8);
          mask_bytes[3] = (uint8_t)(mask_key & 0xFF);
          memcpy(frame + idx, mask_bytes, 4);
          idx += 4;
        }

        for (size_t i = 0; i < payload_len; i++) {
          uint8_t b = (uint8_t)Payload->data[i];
          if (is_masked) {
            b ^= mask_bytes[i % 4];
          }
          frame[idx + i] = b;
        }

        OutHex = malloc(total_frame_len * 2 + 1);
        for (size_t i = 0; i < total_frame_len; i++) {
          OutHex[i * 2] = hexchars[(frame[i] >> 4) & 0x0F];
          OutHex[i * 2 + 1] = hexchars[frame[i] & 0x0F];
        }
        OutHex[total_frame_len * 2] = '\0';
        free(frame);
      }
    }

    ValFree(&PayloadV);
    ValFree(&OpcodeV);
    ValFree(&MaskedV);
    ValFree(&MaskKeyV);

    PUSH(ValStr(OutHex ? OutHex : ""));
    if (OutHex) free(OutHex);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "WebSocketFrameDecodeC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WebSocketFrameDecodeC() expects 1 argument (frame_hex)\n");
      return -1;
    }
    AKSValue HexVal = POP();
    int fin = 0, opcode = 0, is_masked = 0;
    char *PayloadStr = NULL;
    int payload_len = 0;

    if (ValKind(HexVal) == VALSTR) {
      AKSString *H = ValToStrPtr(HexVal);
      size_t frame_len = H->len / 2;
      if (frame_len >= 2) {
        uint8_t *frame = malloc(frame_len);
        for (size_t i = 0; i < frame_len; i++) {
          char c1 = H->data[i * 2], c2 = H->data[i * 2 + 1];
          uint8_t b1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
          uint8_t b2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
          frame[i] = (b1 << 4) | b2;
        }

        fin = (frame[0] & 0x80) ? 1 : 0;
        opcode = frame[0] & 0x0F;
        is_masked = (frame[1] & 0x80) ? 1 : 0;
        uint64_t len_field = frame[1] & 0x7F;
        size_t idx = 2;

        if (len_field == 126 && frame_len >= idx + 2) {
          len_field = (((uint64_t)frame[idx]) << 8) | frame[idx + 1];
          idx += 2;
        } else if (len_field == 127 && frame_len >= idx + 8) {
          len_field = 0;
          for (int i = 0; i < 8; i++) len_field = (len_field << 8) | frame[idx + i];
          idx += 8;
        }

        uint8_t mask_bytes[4] = {0};
        if (is_masked && frame_len >= idx + 4) {
          memcpy(mask_bytes, frame + idx, 4);
          idx += 4;
        }

        payload_len = (int)len_field;
        if (frame_len >= idx + len_field) {
          PayloadStr = malloc((size_t)len_field + 1);
          for (size_t i = 0; i < len_field; i++) {
            uint8_t b = frame[idx + i];
            if (is_masked) b ^= mask_bytes[i % 4];
            PayloadStr[i] = (char)b;
          }
          PayloadStr[len_field] = '\0';
        }
        free(frame);
      }
    }
    ValFree(&HexVal);

    AKSValue Res = ValStruct("WebSocketFrame");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 5;
    S->fieldnames = malloc(5 * sizeof(char *));
    S->fieldvals = malloc(5 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("fin"); S->fieldvals[0] = ValBool(fin);
    S->fieldnames[1] = strdup("opcode"); S->fieldvals[1] = ValInt(opcode);
    S->fieldnames[2] = strdup("masked"); S->fieldvals[2] = ValBool(is_masked);
    S->fieldnames[3] = strdup("payload"); S->fieldvals[3] = ValStr(PayloadStr ? PayloadStr : "");
    S->fieldnames[4] = strdup("payload_len"); S->fieldvals[4] = ValInt(payload_len);
    if (PayloadStr) free(PayloadStr);

    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  // IcmpPingEngine (Direct Win32 ICMP Echo Round-Trip Latency)
  if (strcmp(name, "IcmpPingEchoC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: IcmpPingEchoC() expects 2 arguments (host, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutV = POP(), HostV = POP();
    int success = 0, rtt_ms = 0, status_code = -1, bytes = 0, ttl = 0;

    if (ValKind(HostV) == VALSTR) {
      AKSString *Host = ValToStrPtr(HostV);
      DWORD timeout = (DWORD)ValToInt(TimeoutV);
      if (timeout <= 0) timeout = 1000;

#ifdef _WIN32
      HANDLE hIcmp = IcmpCreateFile();
      if (hIcmp != INVALID_HANDLE_VALUE) {
        unsigned long ipaddr = inet_addr(Host->data);
        if (ipaddr == INADDR_NONE) {
          struct hostent *he = gethostbyname(Host->data);
          if (he && he->h_addr_list && he->h_addr_list[0]) {
            ipaddr = *(unsigned long *)he->h_addr_list[0];
          }
        }

        if (ipaddr != INADDR_NONE) {
          char sendData[32] = "AKS Echo Ping Payload Test 1234";
          DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(sendData) + 8;
          LPVOID replyBuffer = malloc(replySize);
          if (replyBuffer) {
            DWORD dwReplies = IcmpSendEcho(hIcmp, ipaddr, sendData, sizeof(sendData), NULL, replyBuffer, replySize, timeout);
            if (dwReplies != 0) {
              PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)replyBuffer;
              success = (pEchoReply->Status == IP_SUCCESS);
              status_code = (int)pEchoReply->Status;
              rtt_ms = (int)pEchoReply->RoundTripTime;
              bytes = (int)pEchoReply->DataSize;
              ttl = (int)pEchoReply->Options.Ttl;
            }
            free(replyBuffer);
          }
        }
        IcmpCloseHandle(hIcmp);
      }
#endif
    }

    ValFree(&HostV);
    ValFree(&TimeoutV);

    AKSValue Res = ValStruct("IcmpPingResult");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 5;
    S->fieldnames = malloc(5 * sizeof(char *));
    S->fieldvals = malloc(5 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("success"); S->fieldvals[0] = ValBool(success);
    S->fieldnames[1] = strdup("rtt_ms"); S->fieldvals[1] = ValInt(rtt_ms);
    S->fieldnames[2] = strdup("status"); S->fieldvals[2] = ValInt(status_code);
    S->fieldnames[3] = strdup("bytes"); S->fieldvals[3] = ValInt(bytes);
    S->fieldnames[4] = strdup("ttl"); S->fieldvals[4] = ValInt(ttl);

    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  // Http11Parser (High-Speed HTTP/1.1 Request & Response Parser)
  if (strcmp(name, "Http11ParseRequestC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Http11ParseRequestC() expects 1 argument (raw_request)\n");
      return -1;
    }
    AKSValue ReqVal = POP();
    char method[32] = {0}, path[1024] = {0}, version[32] = {0};
    const char *body_start = "";

    AKSValue HeadersStruct = ValStruct("HttpHeaders");
    AKSStruct *Hdr = ValToObj(HeadersStruct);
    int hdr_cap = 16, hdr_count = 0;
    Hdr->fieldnames = malloc(hdr_cap * sizeof(char *));
    Hdr->fieldvals = malloc(hdr_cap * sizeof(AKSValue));
    Hdr->fieldcount = 0;

    if (ValKind(ReqVal) == VALSTR) {
      AKSString *R = ValToStrPtr(ReqVal);
      const char *p = R->data;
      const char *line_end = strstr(p, "\r\n");
      if (!line_end) line_end = strchr(p, '\n');
      if (line_end) {
        sscanf(p, "%31s %1023s %31s", method, path, version);
        p = (*line_end == '\r') ? line_end + 2 : line_end + 1;

        while (p && *p) {
          if (*p == '\r' && *(p + 1) == '\n') {
            body_start = p + 2;
            break;
          }
          if (*p == '\n') {
            body_start = p + 1;
            break;
          }
          const char *hdr_end = strstr(p, "\r\n");
          if (!hdr_end) hdr_end = strchr(p, '\n');
          if (!hdr_end) hdr_end = p + strlen(p);

          const char *colon = memchr(p, ':', hdr_end - p);
          if (colon) {
            size_t klen = colon - p;
            char *key = malloc(klen + 1);
            memcpy(key, p, klen);
            key[klen] = '\0';

            const char *vstart = colon + 1;
            while (vstart < hdr_end && (*vstart == ' ' || *vstart == '\t')) vstart++;
            size_t vlen = hdr_end - vstart;
            char *val = malloc(vlen + 1);
            memcpy(val, vstart, vlen);
            val[vlen] = '\0';

            if (hdr_count >= hdr_cap) {
              hdr_cap *= 2;
              Hdr->fieldnames = realloc(Hdr->fieldnames, hdr_cap * sizeof(char *));
              Hdr->fieldvals = realloc(Hdr->fieldvals, hdr_cap * sizeof(AKSValue));
            }
            Hdr->fieldnames[hdr_count] = key;
            Hdr->fieldvals[hdr_count] = ValStr(val);
            hdr_count++;
            Hdr->fieldcount = hdr_count;
            free(val);
          }
          p = (*hdr_end == '\r') ? hdr_end + 2 : hdr_end + 1;
        }
      }
    }
    ValFree(&ReqVal);

    AKSValue Res = ValStruct("Http11Request");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 5;
    S->fieldnames = malloc(5 * sizeof(char *));
    S->fieldvals = malloc(5 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("method"); S->fieldvals[0] = ValStr(method);
    S->fieldnames[1] = strdup("path"); S->fieldvals[1] = ValStr(path);
    S->fieldnames[2] = strdup("version"); S->fieldvals[2] = ValStr(version);
    S->fieldnames[3] = strdup("headers"); S->fieldvals[3] = HeadersStruct;
    S->fieldnames[4] = strdup("body"); S->fieldvals[4] = ValStr(body_start ? body_start : "");

    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "Http11ParseResponseC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Http11ParseResponseC() expects 1 argument (raw_response)\n");
      return -1;
    }
    AKSValue RespVal = POP();
    char version[32] = {0}, status_text[128] = {0};
    int status_code = 0;
    const char *body_start = "";

    AKSValue HeadersStruct = ValStruct("HttpHeaders");
    AKSStruct *Hdr = ValToObj(HeadersStruct);
    int hdr_cap = 16, hdr_count = 0;
    Hdr->fieldnames = malloc(hdr_cap * sizeof(char *));
    Hdr->fieldvals = malloc(hdr_cap * sizeof(AKSValue));
    Hdr->fieldcount = 0;

    if (ValKind(RespVal) == VALSTR) {
      AKSString *R = ValToStrPtr(RespVal);
      const char *p = R->data;
      const char *line_end = strstr(p, "\r\n");
      if (!line_end) line_end = strchr(p, '\n');
      if (line_end) {
        sscanf(p, "%31s %d %127[^\r\n]", version, &status_code, status_text);
        p = (*line_end == '\r') ? line_end + 2 : line_end + 1;

        while (p && *p) {
          if (*p == '\r' && *(p + 1) == '\n') {
            body_start = p + 2;
            break;
          }
          if (*p == '\n') {
            body_start = p + 1;
            break;
          }
          const char *hdr_end = strstr(p, "\r\n");
          if (!hdr_end) hdr_end = strchr(p, '\n');
          if (!hdr_end) hdr_end = p + strlen(p);

          const char *colon = memchr(p, ':', hdr_end - p);
          if (colon) {
            size_t klen = colon - p;
            char *key = malloc(klen + 1);
            memcpy(key, p, klen);
            key[klen] = '\0';

            const char *vstart = colon + 1;
            while (vstart < hdr_end && (*vstart == ' ' || *vstart == '\t')) vstart++;
            size_t vlen = hdr_end - vstart;
            char *val = malloc(vlen + 1);
            memcpy(val, vstart, vlen);
            val[vlen] = '\0';

            if (hdr_count >= hdr_cap) {
              hdr_cap *= 2;
              Hdr->fieldnames = realloc(Hdr->fieldnames, hdr_cap * sizeof(char *));
              Hdr->fieldvals = realloc(Hdr->fieldvals, hdr_cap * sizeof(AKSValue));
            }
            Hdr->fieldnames[hdr_count] = key;
            Hdr->fieldvals[hdr_count] = ValStr(val);
            hdr_count++;
            Hdr->fieldcount = hdr_count;
            free(val);
          }
          p = (*hdr_end == '\r') ? hdr_end + 2 : hdr_end + 1;
        }
      }
    }
    ValFree(&RespVal);

    AKSValue Res = ValStruct("Http11Response");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 5;
    S->fieldnames = malloc(5 * sizeof(char *));
    S->fieldvals = malloc(5 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("status_code"); S->fieldvals[0] = ValInt(status_code);
    S->fieldnames[1] = strdup("status_text"); S->fieldvals[1] = ValStr(status_text);
    S->fieldnames[2] = strdup("version"); S->fieldvals[2] = ValStr(version);
    S->fieldnames[3] = strdup("headers"); S->fieldvals[3] = HeadersStruct;
    S->fieldnames[4] = strdup("body"); S->fieldvals[4] = ValStr(body_start ? body_start : "");

    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  // CircularByteBuffer (Lock-free Ring Byte Buffer)
  if (strcmp(name, "CircularByteBufferNewC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CircularByteBufferNewC() expects 1 argument (capacity)\n");
      return -1;
    }
    AKSValue CapV = POP();
    int cap = (int)ValToInt(CapV);
    if (cap < 16) cap = 16;

    AKSValue BufferArr = ValArr(cap);
    AKSArray *Arr = ValToArr(BufferArr);
    Arr->count = cap;
    for (int i = 0; i < cap; i++) Arr->items[i] = ValInt(0);

    AKSValue Res = ValStruct("CircularByteBuffer");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 5;
    S->fieldnames = malloc(5 * sizeof(char *));
    S->fieldvals = malloc(5 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("capacity"); S->fieldvals[0] = ValInt(cap);
    S->fieldnames[1] = strdup("read_pos"); S->fieldvals[1] = ValInt(0);
    S->fieldnames[2] = strdup("write_pos"); S->fieldvals[2] = ValInt(0);
    S->fieldnames[3] = strdup("count"); S->fieldvals[3] = ValInt(0);
    S->fieldnames[4] = strdup("buffer"); S->fieldvals[4] = BufferArr;

    ValFree(&CapV);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CircularByteBufferWriteC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CircularByteBufferWriteC() expects 2 arguments (self, data_str)\n");
      return -1;
    }
    AKSValue DataV = POP(), SelfV = POP();
    int written = 0;
    if (ValKind(SelfV) == VALSTRUCT && ValKind(DataV) == VALSTR) {
      AKSStruct *S = ValToObj(SelfV);
      AKSString *Data = ValToStrPtr(DataV);
      int cap = (int)ValToInt(S->fieldvals[0]);
      int rpos = (int)ValToInt(S->fieldvals[1]);
      int wpos = (int)ValToInt(S->fieldvals[2]);
      int count = (int)ValToInt(S->fieldvals[3]);
      AKSArray *Arr = ValToArr(S->fieldvals[4]);

      int space = cap - count;
      int to_write = (int)Data->len;
      if (to_write > space) to_write = space;

      for (int i = 0; i < to_write; i++) {
        Arr->items[wpos] = ValInt((uint8_t)Data->data[i]);
        wpos = (wpos + 1) % cap;
      }
      count += to_write;
      written = to_write;

      S->fieldvals[2] = ValInt(wpos);
      S->fieldvals[3] = ValInt(count);
    }
    ValFree(&SelfV);
    ValFree(&DataV);
    PUSH(ValInt(written));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CircularByteBufferReadC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CircularByteBufferReadC() expects 2 arguments (self, max_bytes)\n");
      return -1;
    }
    AKSValue MaxV = POP(), SelfV = POP();
    char *OutStr = NULL;
    if (ValKind(SelfV) == VALSTRUCT) {
      AKSStruct *S = ValToObj(SelfV);
      int max_bytes = (int)ValToInt(MaxV);
      int cap = (int)ValToInt(S->fieldvals[0]);
      int rpos = (int)ValToInt(S->fieldvals[1]);
      int wpos = (int)ValToInt(S->fieldvals[2]);
      int count = (int)ValToInt(S->fieldvals[3]);
      AKSArray *Arr = ValToArr(S->fieldvals[4]);

      int to_read = count;
      if (max_bytes > 0 && to_read > max_bytes) to_read = max_bytes;

      OutStr = malloc(to_read + 1);
      for (int i = 0; i < to_read; i++) {
        OutStr[i] = (char)(uint8_t)ValToInt(Arr->items[rpos]);
        rpos = (rpos + 1) % cap;
      }
      OutStr[to_read] = '\0';
      count -= to_read;

      S->fieldvals[1] = ValInt(rpos);
      S->fieldvals[3] = ValInt(count);
    }
    ValFree(&SelfV);
    ValFree(&MaxV);
    PUSH(ValStr(OutStr ? OutStr : ""));
    if (OutStr) free(OutStr);
    *stacktopptr = stacktop;
    return 1;
  }

  // RadixTree (Native Trie / Radix Tree Associative Structure)
  if (strcmp(name, "RadixTreeNewC") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: RadixTreeNewC() expects 0 arguments\n");
      return -1;
    }
    AKSValue Res = ValStruct("RadixTree");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 3;
    S->fieldnames = malloc(3 * sizeof(char *));
    S->fieldvals = malloc(3 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("count"); S->fieldvals[0] = ValInt(0);
    S->fieldnames[1] = strdup("keys"); S->fieldvals[1] = ValArr(0);
    S->fieldnames[2] = strdup("values"); S->fieldvals[2] = ValArr(0);

    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "RadixTreeInsertC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: RadixTreeInsertC() expects 3 arguments (self, key, val)\n");
      return -1;
    }
    AKSValue ValV = POP(), KeyV = POP(), SelfV = POP();
    if (ValKind(SelfV) == VALSTRUCT && ValKind(KeyV) == VALSTR) {
      AKSStruct *S = ValToObj(SelfV);
      AKSArray *KeysArr = ValToArr(S->fieldvals[1]);
      AKSArray *ValsArr = ValToArr(S->fieldvals[2]);
      AKSString *KeyStr = ValToStrPtr(KeyV);
      int found_idx = -1;
      for (int i = 0; i < KeysArr->count; i++) {
        if (ValKind(KeysArr->items[i]) == VALSTR) {
          AKSString *K = ValToStrPtr(KeysArr->items[i]);
          if (strcmp(K->data, KeyStr->data) == 0) {
            found_idx = i;
            break;
          }
        }
      }
      if (found_idx >= 0) {
        ValFree(&ValsArr->items[found_idx]);
        ValsArr->items[found_idx] = ValCopy(ValV);
      } else {
        if (KeysArr->count >= KeysArr->capacity) {
          int nc = KeysArr->capacity ? KeysArr->capacity * 2 : 4;
          KeysArr->items = realloc(KeysArr->items, nc * sizeof(AKSValue));
          KeysArr->capacity = nc;
        }
        if (ValsArr->count >= ValsArr->capacity) {
          int nc = ValsArr->capacity ? ValsArr->capacity * 2 : 4;
          ValsArr->items = realloc(ValsArr->items, nc * sizeof(AKSValue));
          ValsArr->capacity = nc;
        }
        KeysArr->items[KeysArr->count++] = ValCopy(KeyV);
        ValsArr->items[ValsArr->count++] = ValCopy(ValV);
        S->fieldvals[0] = ValInt(KeysArr->count);
      }
    }
    ValFree(&SelfV);
    ValFree(&KeyV);
    ValFree(&ValV);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "RadixTreeLookupC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RadixTreeLookupC() expects 2 arguments (self, key)\n");
      return -1;
    }
    AKSValue KeyV = POP(), SelfV = POP();
    AKSValue Res = ValNull();
    if (ValKind(SelfV) == VALSTRUCT && ValKind(KeyV) == VALSTR) {
      AKSStruct *S = ValToObj(SelfV);
      AKSArray *KeysArr = ValToArr(S->fieldvals[1]);
      AKSArray *ValsArr = ValToArr(S->fieldvals[2]);
      AKSString *KeyStr = ValToStrPtr(KeyV);
      for (int i = 0; i < KeysArr->count; i++) {
        if (ValKind(KeysArr->items[i]) == VALSTR) {
          AKSString *K = ValToStrPtr(KeysArr->items[i]);
          if (strcmp(K->data, KeyStr->data) == 0) {
            Res = ValCopy(ValsArr->items[i]);
            break;
          }
        }
      }
    }
    ValFree(&SelfV);
    ValFree(&KeyV);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "RadixTreePrefixSearchC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RadixTreePrefixSearchC() expects 2 arguments (self, prefix)\n");
      return -1;
    }
    AKSValue PrefixV = POP(), SelfV = POP();
    AKSValue ResultArr = ValArr(0);
    AKSArray *Arr = ValToArr(ResultArr);

    if (ValKind(SelfV) == VALSTRUCT && ValKind(PrefixV) == VALSTR) {
      AKSStruct *S = ValToObj(SelfV);
      AKSArray *KeysArr = ValToArr(S->fieldvals[1]);
      AKSString *Prefix = ValToStrPtr(PrefixV);

      for (int i = 0; i < KeysArr->count; i++) {
        if (ValKind(KeysArr->items[i]) == VALSTR) {
          AKSString *K = ValToStrPtr(KeysArr->items[i]);
          if (strncmp(K->data, Prefix->data, Prefix->len) == 0) {
            if (Arr->count >= Arr->capacity) {
              int nc = Arr->capacity ? Arr->capacity * 2 : 4;
              Arr->items = realloc(Arr->items, nc * sizeof(AKSValue));
              Arr->capacity = nc;
            }
            Arr->items[Arr->count++] = ValCopy(KeysArr->items[i]);
          }
        }
      }
    }
    ValFree(&SelfV);
    ValFree(&PrefixV);
    PUSH(ResultArr);
    *stacktopptr = stacktop;
    return 1;
  }

  // Complex128 (complex number arithmetic)
  if (strcmp(name, "Complex128NewC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: Complex128NewC() expects 2 args\n"); return -1; }
    AKSValue ImV = POP(), ReV = POP();
    double re = (ValKind(ReV) == VALFLOAT) ? ValToFloat(ReV) : (double)ValToInt(ReV);
    double im = (ValKind(ImV) == VALFLOAT) ? ValToFloat(ImV) : (double)ValToInt(ImV);
    AKSValue R = ValStruct("Complex128");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals  = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("re"); S->fieldvals[0] = ValFloat(re);
    S->fieldnames[1] = strdup("im"); S->fieldvals[1] = ValFloat(im);
    ValFree(&ReV); ValFree(&ImV);
    PUSH(R); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "Complex128AddC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: Complex128AddC() expects 2 args\n"); return -1; }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A = ValToObj(AV), *B = ValToObj(BV);
    double re = ValToFloat(A->fieldvals[0]) + ValToFloat(B->fieldvals[0]);
    double im = ValToFloat(A->fieldvals[1]) + ValToFloat(B->fieldvals[1]);
    AKSValue R = ValStruct("Complex128"); AKSStruct *S = ValToObj(R);
    S->fieldcount = 2; S->fieldnames = malloc(2*sizeof(char*)); S->fieldvals = malloc(2*sizeof(AKSValue));
    S->fieldnames[0]=strdup("re"); S->fieldvals[0]=ValFloat(re);
    S->fieldnames[1]=strdup("im"); S->fieldvals[1]=ValFloat(im);
    ValFree(&AV); ValFree(&BV); PUSH(R); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "Complex128SubC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: Complex128SubC() expects 2 args\n"); return -1; }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A = ValToObj(AV), *B = ValToObj(BV);
    double re = ValToFloat(A->fieldvals[0]) - ValToFloat(B->fieldvals[0]);
    double im = ValToFloat(A->fieldvals[1]) - ValToFloat(B->fieldvals[1]);
    AKSValue R = ValStruct("Complex128"); AKSStruct *S = ValToObj(R);
    S->fieldcount = 2; S->fieldnames = malloc(2*sizeof(char*)); S->fieldvals = malloc(2*sizeof(AKSValue));
    S->fieldnames[0]=strdup("re"); S->fieldvals[0]=ValFloat(re);
    S->fieldnames[1]=strdup("im"); S->fieldvals[1]=ValFloat(im);
    ValFree(&AV); ValFree(&BV); PUSH(R); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "Complex128MulC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: Complex128MulC() expects 2 args\n"); return -1; }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A = ValToObj(AV), *B = ValToObj(BV);
    double ar=ValToFloat(A->fieldvals[0]), ai=ValToFloat(A->fieldvals[1]);
    double br=ValToFloat(B->fieldvals[0]), bi=ValToFloat(B->fieldvals[1]);
    double re = ar*br - ai*bi, im = ar*bi + ai*br;
    AKSValue R = ValStruct("Complex128"); AKSStruct *S = ValToObj(R);
    S->fieldcount = 2; S->fieldnames = malloc(2*sizeof(char*)); S->fieldvals = malloc(2*sizeof(AKSValue));
    S->fieldnames[0]=strdup("re"); S->fieldvals[0]=ValFloat(re);
    S->fieldnames[1]=strdup("im"); S->fieldvals[1]=ValFloat(im);
    ValFree(&AV); ValFree(&BV); PUSH(R); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "Complex128DivC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: Complex128DivC() expects 2 args\n"); return -1; }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A = ValToObj(AV), *B = ValToObj(BV);
    double ar=ValToFloat(A->fieldvals[0]), ai=ValToFloat(A->fieldvals[1]);
    double br=ValToFloat(B->fieldvals[0]), bi=ValToFloat(B->fieldvals[1]);
    double denom = br*br + bi*bi;
    double re = (ar*br + ai*bi)/denom, im = (ai*br - ar*bi)/denom;
    AKSValue R = ValStruct("Complex128"); AKSStruct *S = ValToObj(R);
    S->fieldcount = 2; S->fieldnames = malloc(2*sizeof(char*)); S->fieldvals = malloc(2*sizeof(AKSValue));
    S->fieldnames[0]=strdup("re"); S->fieldvals[0]=ValFloat(re);
    S->fieldnames[1]=strdup("im"); S->fieldvals[1]=ValFloat(im);
    ValFree(&AV); ValFree(&BV); PUSH(R); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "Complex128AbsC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: Complex128AbsC() expects 1 arg\n"); return -1; }
    AKSValue AV = POP(); AKSStruct *A = ValToObj(AV);
    double re=ValToFloat(A->fieldvals[0]), im=ValToFloat(A->fieldvals[1]);
    double mag = sqrt(re*re + im*im);
    ValFree(&AV); PUSH(ValFloat(mag)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "Complex128ArgC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: Complex128ArgC() expects 1 arg\n"); return -1; }
    AKSValue AV = POP(); AKSStruct *A = ValToObj(AV);
    double re=ValToFloat(A->fieldvals[0]), im=ValToFloat(A->fieldvals[1]);
    double arg = atan2(im, re);
    ValFree(&AV); PUSH(ValFloat(arg)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "Complex128ConjC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: Complex128ConjC() expects 1 arg\n"); return -1; }
    AKSValue AV = POP(); AKSStruct *A = ValToObj(AV);
    double re=ValToFloat(A->fieldvals[0]), im=ValToFloat(A->fieldvals[1]);
    AKSValue R = ValStruct("Complex128"); AKSStruct *S = ValToObj(R);
    S->fieldcount=2; S->fieldnames=malloc(2*sizeof(char*)); S->fieldvals=malloc(2*sizeof(AKSValue));
    S->fieldnames[0]=strdup("re"); S->fieldvals[0]=ValFloat(re);
    S->fieldnames[1]=strdup("im"); S->fieldvals[1]=ValFloat(-im);
    ValFree(&AV); PUSH(R); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "Complex128ExpC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: Complex128ExpC() expects 1 arg\n"); return -1; }
    AKSValue AV = POP(); AKSStruct *A = ValToObj(AV);
    double re=ValToFloat(A->fieldvals[0]), im=ValToFloat(A->fieldvals[1]);
    double er = exp(re);
    double rout = er * cos(im), iout = er * sin(im);
    AKSValue R = ValStruct("Complex128"); AKSStruct *S = ValToObj(R);
    S->fieldcount=2; S->fieldnames=malloc(2*sizeof(char*)); S->fieldvals=malloc(2*sizeof(AKSValue));
    S->fieldnames[0]=strdup("re"); S->fieldvals[0]=ValFloat(rout);
    S->fieldnames[1]=strdup("im"); S->fieldvals[1]=ValFloat(iout);
    ValFree(&AV); PUSH(R); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "Complex128SqrtC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: Complex128SqrtC() expects 1 arg\n"); return -1; }
    AKSValue AV = POP(); AKSStruct *A = ValToObj(AV);
    double re=ValToFloat(A->fieldvals[0]), im=ValToFloat(A->fieldvals[1]);
    double mag = sqrt(sqrt(re*re+im*im));
    double arg = atan2(im,re) / 2.0;
    double rout = mag*cos(arg), iout = mag*sin(arg);
    AKSValue R = ValStruct("Complex128"); AKSStruct *S = ValToObj(R);
    S->fieldcount=2; S->fieldnames=malloc(2*sizeof(char*)); S->fieldvals=malloc(2*sizeof(AKSValue));
    S->fieldnames[0]=strdup("re"); S->fieldvals[0]=ValFloat(rout);
    S->fieldnames[1]=strdup("im"); S->fieldvals[1]=ValFloat(iout);
    ValFree(&AV); PUSH(R); *stacktopptr=stacktop; return 1;
  }

  // Rational (exact p/q fraction arithmetic)
  if (strcmp(name, "RationalNewC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: RationalNewC() expects 2 args (p, q)\n"); return -1; }
    AKSValue QV = POP(), PV = POP();
    int64_t p = ValToInt(PV), q = ValToInt(QV);
    if (q == 0) { fprintf(stderr, "Runtime error: RationalNewC: denominator cannot be zero\n"); return -1; }
    // normalize sign: keep q positive
    if (q < 0) { p = -p; q = -q; }
    // reduce by GCD
    int64_t a = p < 0 ? -p : p, b = q;
    while (b) { int64_t t = b; b = a % b; a = t; }
    if (a > 0) { p /= a; q /= a; }
    AKSValue R = ValStruct("Rational"); AKSStruct *S = ValToObj(R);
    S->fieldcount=2; S->fieldnames=malloc(2*sizeof(char*)); S->fieldvals=malloc(2*sizeof(AKSValue));
    S->fieldnames[0]=strdup("p"); S->fieldvals[0]=ValInt((int)p);
    S->fieldnames[1]=strdup("q"); S->fieldvals[1]=ValInt((int)q);
    ValFree(&PV); ValFree(&QV); PUSH(R); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "RationalAddC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: RationalAddC() expects 2 args\n"); return -1; }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A=ValToObj(AV), *B=ValToObj(BV);
    int64_t ap=ValToInt(A->fieldvals[0]), aq=ValToInt(A->fieldvals[1]);
    int64_t bp=ValToInt(B->fieldvals[0]), bq=ValToInt(B->fieldvals[1]);
    int64_t rp=ap*bq+bp*aq, rq=aq*bq;
    int64_t a=rp<0?-rp:rp, b=rq; while(b){int64_t t=b;b=a%b;a=t;} if(a>0){rp/=a;rq/=a;}
    AKSValue R=ValStruct("Rational"); AKSStruct *S=ValToObj(R);
    S->fieldcount=2; S->fieldnames=malloc(2*sizeof(char*)); S->fieldvals=malloc(2*sizeof(AKSValue));
    S->fieldnames[0]=strdup("p"); S->fieldvals[0]=ValInt((int)rp);
    S->fieldnames[1]=strdup("q"); S->fieldvals[1]=ValInt((int)rq);
    ValFree(&AV); ValFree(&BV); PUSH(R); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "RationalMulC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: RationalMulC() expects 2 args\n"); return -1; }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A=ValToObj(AV), *B=ValToObj(BV);
    int64_t ap=ValToInt(A->fieldvals[0]), aq=ValToInt(A->fieldvals[1]);
    int64_t bp=ValToInt(B->fieldvals[0]), bq=ValToInt(B->fieldvals[1]);
    int64_t rp=ap*bp, rq=aq*bq;
    int64_t a=rp<0?-rp:rp, b=rq; while(b){int64_t t=b;b=a%b;a=t;} if(a>0){rp/=a;rq/=a;}
    AKSValue R=ValStruct("Rational"); AKSStruct *S=ValToObj(R);
    S->fieldcount=2; S->fieldnames=malloc(2*sizeof(char*)); S->fieldvals=malloc(2*sizeof(AKSValue));
    S->fieldnames[0]=strdup("p"); S->fieldvals[0]=ValInt((int)rp);
    S->fieldnames[1]=strdup("q"); S->fieldvals[1]=ValInt((int)rq);
    ValFree(&AV); ValFree(&BV); PUSH(R); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "RationalSubC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: RationalSubC() expects 2 args\n"); return -1; }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A=ValToObj(AV), *B=ValToObj(BV);
    int64_t ap=ValToInt(A->fieldvals[0]), aq=ValToInt(A->fieldvals[1]);
    int64_t bp=ValToInt(B->fieldvals[0]), bq=ValToInt(B->fieldvals[1]);
    int64_t rp=ap*bq-bp*aq, rq=aq*bq;
    int64_t a=rp<0?-rp:rp, b=rq; while(b){int64_t t=b;b=a%b;a=t;} if(a>0){rp/=a;rq/=a;}
    AKSValue R=ValStruct("Rational"); AKSStruct *S=ValToObj(R);
    S->fieldcount=2; S->fieldnames=malloc(2*sizeof(char*)); S->fieldvals=malloc(2*sizeof(AKSValue));
    S->fieldnames[0]=strdup("p"); S->fieldvals[0]=ValInt((int)rp);
    S->fieldnames[1]=strdup("q"); S->fieldvals[1]=ValInt((int)rq);
    ValFree(&AV); ValFree(&BV); PUSH(R); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "RationalDivC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: RationalDivC() expects 2 args\n"); return -1; }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A=ValToObj(AV), *B=ValToObj(BV);
    int64_t ap=ValToInt(A->fieldvals[0]), aq=ValToInt(A->fieldvals[1]);
    int64_t bp=ValToInt(B->fieldvals[0]), bq=ValToInt(B->fieldvals[1]);
    int64_t rp=ap*bq, rq=aq*bp;
    if(rq<0){rp=-rp;rq=-rq;}
    int64_t a=rp<0?-rp:rp, b=rq; while(b){int64_t t=b;b=a%b;a=t;} if(a>0){rp/=a;rq/=a;}
    AKSValue R=ValStruct("Rational"); AKSStruct *S=ValToObj(R);
    S->fieldcount=2; S->fieldnames=malloc(2*sizeof(char*)); S->fieldvals=malloc(2*sizeof(AKSValue));
    S->fieldnames[0]=strdup("p"); S->fieldvals[0]=ValInt((int)rp);
    S->fieldnames[1]=strdup("q"); S->fieldvals[1]=ValInt((int)rq);
    ValFree(&AV); ValFree(&BV); PUSH(R); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "RationalToFloatC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: RationalToFloatC() expects 1 arg\n"); return -1; }
    AKSValue AV = POP(); AKSStruct *A=ValToObj(AV);
    double v = (double)ValToInt(A->fieldvals[0]) / (double)ValToInt(A->fieldvals[1]);
    ValFree(&AV); PUSH(ValFloat(v)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "RationalEqC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: RationalEqC() expects 2 args\n"); return -1; }
    AKSValue BV=POP(), AV=POP();
    AKSStruct *A=ValToObj(AV), *B=ValToObj(BV);
    int eq = ValToInt(A->fieldvals[0])==ValToInt(B->fieldvals[0]) && ValToInt(A->fieldvals[1])==ValToInt(B->fieldvals[1]);
    ValFree(&AV); ValFree(&BV); PUSH(ValBool(eq)); *stacktopptr=stacktop; return 1;
  }

  // FFT (Cooley-Tukey radix-2, returns array of [re, im] pairs)
  if (strcmp(name, "FFTForwardC") == 0 || strcmp(name, "FFTInverseC") == 0) {
    int is_inverse = (strcmp(name, "FFTInverseC") == 0);
    if (argc != 1) { fprintf(stderr, "Runtime error: %s() expects 1 arg (array of floats)\n", name); return -1; }
    AKSValue ArrV = POP();
    AKSArray *Input = ValToArr(ArrV);
    int n = Input->count;
    // verify n is power of 2
    if (n == 0 || (n & (n-1)) != 0) {
      fprintf(stderr, "Runtime error: FFT input length must be a power of 2, got %d\n", n);
      ValFree(&ArrV); return -1;
    }
    // allocate real/imag buffers
    double *re = malloc(n * sizeof(double));
    double *im = malloc(n * sizeof(double));
    for (int i = 0; i < n; i++) {
      AKSValue v = Input->items[i];
      if (ValKind(v) == VALFLOAT) { re[i] = ValToFloat(v); im[i] = 0.0; }
      else { re[i] = (double)ValToInt(v); im[i] = 0.0; }
    }
    // bit-reversal permutation
    for (int i = 1, j = 0; i < n; i++) {
      int bit = n >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) { double tr=re[i]; re[i]=re[j]; re[j]=tr; double ti=im[i]; im[i]=im[j]; im[j]=ti; }
    }
    // Cooley-Tukey butterfly
    double sign = is_inverse ? 1.0 : -1.0;
    for (int len = 2; len <= n; len <<= 1) {
      double ang = sign * 2.0 * 3.14159265358979323846 / len;
      double wr = cos(ang), wi = sin(ang);
      for (int i = 0; i < n; i += len) {
        double cur_r = 1.0, cur_i = 0.0;
        for (int j = 0; j < len/2; j++) {
          double ur=re[i+j], ui=im[i+j];
          double vr=re[i+j+len/2]*cur_r - im[i+j+len/2]*cur_i;
          double vi=re[i+j+len/2]*cur_i + im[i+j+len/2]*cur_r;
          re[i+j]=ur+vr; im[i+j]=ui+vi;
          re[i+j+len/2]=ur-vr; im[i+j+len/2]=ui-vi;
          double nr=cur_r*wr-cur_i*wi; cur_i=cur_r*wi+cur_i*wr; cur_r=nr;
        }
      }
    }
    if (is_inverse) { for (int i=0; i<n; i++) { re[i]/=n; im[i]/=n; } }
    // build output: array of [re, im] pairs
    AKSValue OutArr = ValArr(n);
    AKSArray *Out = ValToArr(OutArr);
    Out->count = n;
    Out->capacity = n;
    Out->items = realloc(Out->items, n * sizeof(AKSValue));
    for (int i = 0; i < n; i++) {
      AKSValue Pair = ValStruct("Complex128"); AKSStruct *SP = ValToObj(Pair);
      SP->fieldcount=2; SP->fieldnames=malloc(2*sizeof(char*)); SP->fieldvals=malloc(2*sizeof(AKSValue));
      SP->fieldnames[0]=strdup("re"); SP->fieldvals[0]=ValFloat(re[i]);
      SP->fieldnames[1]=strdup("im"); SP->fieldvals[1]=ValFloat(im[i]);
      Out->items[i] = Pair;
    }
    free(re); free(im);
    ValFree(&ArrV); PUSH(OutArr); *stacktopptr=stacktop; return 1;
  }

  // StatDescriptive (mean, variance, stddev, skewness, kurtosis, min, max, percentile)
  if (strcmp(name, "StatMeanC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: StatMeanC() expects 1 arg\n"); return -1; }
    AKSValue AV = POP(); AKSArray *A = ValToArr(AV);
    double s=0; int n=A->count;
    for(int i=0;i<n;i++) s += (ValKind(A->items[i])==VALFLOAT)?ValToFloat(A->items[i]):(double)ValToInt(A->items[i]);
    ValFree(&AV); PUSH(ValFloat(n>0?s/n:0.0)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "StatVarianceC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: StatVarianceC() expects 1 arg\n"); return -1; }
    AKSValue AV = POP(); AKSArray *A = ValToArr(AV); int n=A->count;
    double s=0;
    for(int i=0;i<n;i++) s+=(ValKind(A->items[i])==VALFLOAT)?ValToFloat(A->items[i]):(double)ValToInt(A->items[i]);
    double mean=n>0?s/n:0; double v=0;
    for(int i=0;i<n;i++){double d=((ValKind(A->items[i])==VALFLOAT)?ValToFloat(A->items[i]):(double)ValToInt(A->items[i]))-mean; v+=d*d;}
    ValFree(&AV); PUSH(ValFloat(n>1?v/(n-1):0.0)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "StatStddevC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: StatStddevC() expects 1 arg\n"); return -1; }
    AKSValue AV = POP(); AKSArray *A = ValToArr(AV); int n=A->count;
    double s=0;
    for(int i=0;i<n;i++) s+=(ValKind(A->items[i])==VALFLOAT)?ValToFloat(A->items[i]):(double)ValToInt(A->items[i]);
    double mean=n>0?s/n:0; double v=0;
    for(int i=0;i<n;i++){double d=((ValKind(A->items[i])==VALFLOAT)?ValToFloat(A->items[i]):(double)ValToInt(A->items[i]))-mean; v+=d*d;}
    ValFree(&AV); PUSH(ValFloat(n>1?sqrt(v/(n-1)):0.0)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "StatMinC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: StatMinC() expects 1 arg\n"); return -1; }
    AKSValue AV = POP(); AKSArray *A = ValToArr(AV); int n=A->count;
    double mn = n>0 ? ((ValKind(A->items[0])==VALFLOAT)?ValToFloat(A->items[0]):(double)ValToInt(A->items[0])) : 0.0;
    for(int i=1;i<n;i++){double v=(ValKind(A->items[i])==VALFLOAT)?ValToFloat(A->items[i]):(double)ValToInt(A->items[i]); if(v<mn)mn=v;}
    ValFree(&AV); PUSH(ValFloat(mn)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "StatMaxC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: StatMaxC() expects 1 arg\n"); return -1; }
    AKSValue AV = POP(); AKSArray *A = ValToArr(AV); int n=A->count;
    double mx = n>0 ? ((ValKind(A->items[0])==VALFLOAT)?ValToFloat(A->items[0]):(double)ValToInt(A->items[0])) : 0.0;
    for(int i=1;i<n;i++){double v=(ValKind(A->items[i])==VALFLOAT)?ValToFloat(A->items[i]):(double)ValToInt(A->items[i]); if(v>mx)mx=v;}
    ValFree(&AV); PUSH(ValFloat(mx)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "StatPercentileC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: StatPercentileC() expects 2 args (data, p)\n"); return -1; }
    AKSValue PV=POP(), AV=POP(); AKSArray *A=ValToArr(AV); int n=A->count;
    double pct=(ValKind(PV)==VALFLOAT)?ValToFloat(PV):(double)ValToInt(PV);
    if(n==0){ValFree(&AV);ValFree(&PV);PUSH(ValFloat(0.0));*stacktopptr=stacktop;return 1;}
    double *buf=malloc(n*sizeof(double));
    for(int i=0;i<n;i++) buf[i]=(ValKind(A->items[i])==VALFLOAT)?ValToFloat(A->items[i]):(double)ValToInt(A->items[i]);
    // insertion sort
    for(int i=1;i<n;i++){double key=buf[i];int j=i-1;while(j>=0&&buf[j]>key){buf[j+1]=buf[j];j--;}buf[j+1]=key;}
    double idx=(pct/100.0)*(n-1);
    int lo=(int)idx; double frac=idx-lo;
    double result = (lo+1<n) ? buf[lo]*(1.0-frac)+buf[lo+1]*frac : buf[lo];
    free(buf); ValFree(&AV); ValFree(&PV); PUSH(ValFloat(result)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "StatSkewnessC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: StatSkewnessC() expects 1 arg\n"); return -1; }
    AKSValue AV=POP(); AKSArray *A=ValToArr(AV); int n=A->count;
    double s=0;
    for(int i=0;i<n;i++) s+=(ValKind(A->items[i])==VALFLOAT)?ValToFloat(A->items[i]):(double)ValToInt(A->items[i]);
    double mean=n>0?s/n:0; double v=0,sk=0;
    for(int i=0;i<n;i++){double d=((ValKind(A->items[i])==VALFLOAT)?ValToFloat(A->items[i]):(double)ValToInt(A->items[i]))-mean; v+=d*d; sk+=d*d*d;}
    double std=n>1?sqrt(v/(n-1)):0;
    double skewness = (n>2 && std>1e-12) ? (sk/n)/(std*std*std) : 0.0;
    ValFree(&AV); PUSH(ValFloat(skewness)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "StatKurtosisC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: StatKurtosisC() expects 1 arg\n"); return -1; }
    AKSValue AV=POP(); AKSArray *A=ValToArr(AV); int n=A->count;
    double s=0;
    for(int i=0;i<n;i++) s+=(ValKind(A->items[i])==VALFLOAT)?ValToFloat(A->items[i]):(double)ValToInt(A->items[i]);
    double mean=n>0?s/n:0; double v=0,k4=0;
    for(int i=0;i<n;i++){double d=((ValKind(A->items[i])==VALFLOAT)?ValToFloat(A->items[i]):(double)ValToInt(A->items[i]))-mean; v+=d*d; k4+=d*d*d*d;}
    double var=n>1?v/(n-1):0;
    double kurt = (n>3 && var>1e-24) ? (k4/n)/(var*var) - 3.0 : 0.0;
    ValFree(&AV); PUSH(ValFloat(kurt)); *stacktopptr=stacktop; return 1;
  }

  // MersenneTwister (MT19937-64 PRNG)
  if (strcmp(name, "MTNewC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: MTNewC() expects 1 arg (seed)\n"); return -1; }
    AKSValue SeedV = POP();
    uint64_t seed = (uint64_t)ValToInt(SeedV);
    // MT19937-64: state array of 312 uint64_t stored in array
    AKSValue StateArr = ValArr(313);
    AKSArray *SA = ValToArr(StateArr);
    SA->count = 313; SA->capacity = 313;
    SA->items = realloc(SA->items, 313 * sizeof(AKSValue));
    // mt[0..311] = state, mt[312] = index
    SA->items[0] = ValInt((int)(seed & 0xFFFFFFFF));
    uint64_t mt0 = seed;
    for (int i = 1; i < 312; i++) {
      mt0 = 6364136223846793005ULL * (mt0 ^ (mt0 >> 62)) + (uint64_t)i;
      SA->items[i] = ValInt((int)(mt0 & 0x7FFFFFFF));
      // store upper 32 bits in a secondary slot would lose precision; store as float to preserve 64 bits
      // We simplify: store full 64-bit as double (exact for values < 2^53)
      SA->items[i] = ValFloat((double)mt0);
    }
    SA->items[312] = ValInt(312); // index = 312 -> needs generation
    AKSValue R = ValStruct("MersenneTwister"); AKSStruct *S = ValToObj(R);
    S->fieldcount=1; S->fieldnames=malloc(sizeof(char*)); S->fieldvals=malloc(sizeof(AKSValue));
    S->fieldnames[0]=strdup("state"); S->fieldvals[0]=StateArr;
    ValFree(&SeedV); PUSH(R); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "MTNextU64C") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: MTNextU64C() expects 1 arg (mt struct)\n"); return -1; }
    AKSValue SelfV=POP(); AKSStruct *S=ValToObj(SelfV);
    AKSArray *SA=ValToArr(S->fieldvals[0]);
    int idx=(int)ValToInt(SA->items[312]);
    if(idx>=312){
      // generate next 312 values
      for(int i=0;i<312;i++){
        uint64_t x=((uint64_t)(ValToFloat(SA->items[i]) >= 0 ? (uint64_t)ValToFloat(SA->items[i]) : 0) & 0xFFFFFFFF80000000ULL)
                 | ((uint64_t)(ValToFloat(SA->items[(i+1)%312]) >= 0 ? (uint64_t)ValToFloat(SA->items[(i+1)%312]) : 0) & 0x7FFFFFFFULL);
        uint64_t xa=(x>>1) ^ ((x&1) ? 0xB5026F5AA96619E9ULL : 0);
        uint64_t nv=(uint64_t)ValToFloat(SA->items[(i+156)%312]) ^ xa;
        ValFree(&SA->items[i]); SA->items[i]=ValFloat((double)nv);
      }
      idx=0;
    }
    uint64_t y=(uint64_t)ValToFloat(SA->items[idx]);
    ValFree(&SA->items[312]); SA->items[312]=ValInt(idx+1);
    y ^= (y >> 29) & 0x5555555555555555ULL;
    y ^= (y << 17) & 0x71D67FFFEDA60000ULL;
    y ^= (y << 37) & 0xFFF7EEE000000000ULL;
    y ^= (y >> 43);
    // return as float (exact for < 2^53)
    ValFree(&SelfV);
    PUSH(ValFloat((double)y)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "MTNextFloatC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: MTNextFloatC() expects 1 arg (mt struct)\n"); return -1; }
    AKSValue SelfV=POP(); AKSStruct *S=ValToObj(SelfV);
    AKSArray *SA=ValToArr(S->fieldvals[0]);
    int idx=(int)ValToInt(SA->items[312]);
    if(idx>=312){
      for(int i=0;i<312;i++){
        uint64_t x=((uint64_t)(ValToFloat(SA->items[i])>=0?(uint64_t)ValToFloat(SA->items[i]):0)&0xFFFFFFFF80000000ULL)
                 | ((uint64_t)(ValToFloat(SA->items[(i+1)%312])>=0?(uint64_t)ValToFloat(SA->items[(i+1)%312]):0)&0x7FFFFFFFULL);
        uint64_t xa=(x>>1)^((x&1)?0xB5026F5AA96619E9ULL:0);
        uint64_t nv=(uint64_t)ValToFloat(SA->items[(i+156)%312])^xa;
        ValFree(&SA->items[i]); SA->items[i]=ValFloat((double)nv);
      }
      idx=0;
    }
    uint64_t y=(uint64_t)ValToFloat(SA->items[idx]);
    ValFree(&SA->items[312]); SA->items[312]=ValInt(idx+1);
    y^=(y>>29)&0x5555555555555555ULL; y^=(y<<17)&0x71D67FFFEDA60000ULL;
    y^=(y<<37)&0xFFF7EEE000000000ULL; y^=(y>>43);
    double f=(double)(y>>11) * (1.0/9007199254740992.0);
    ValFree(&SelfV); PUSH(ValFloat(f)); *stacktopptr=stacktop; return 1;
  }

  // MillerRabin (probabilistic primality test)
  if (strcmp(name, "MillerRabinC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: MillerRabinC() expects 2 args (n, rounds)\n"); return -1; }
    AKSValue RoundsV=POP(), NV=POP();
    uint64_t mr_n=(uint64_t)ValToInt(NV);
    int mr_rounds=(int)ValToInt(RoundsV);
    if(mr_n<2){ValFree(&NV);ValFree(&RoundsV);PUSH(ValBool(0));*stacktopptr=stacktop;return 1;}
    if(mr_n==2||mr_n==3){ValFree(&NV);ValFree(&RoundsV);PUSH(ValBool(1));*stacktopptr=stacktop;return 1;}
    if(mr_n%2==0){ValFree(&NV);ValFree(&RoundsV);PUSH(ValBool(0));*stacktopptr=stacktop;return 1;}
    uint64_t mr_d=mr_n-1; int mr_r=0;
    while(mr_d%2==0){mr_d/=2;mr_r++;}
    static const uint64_t mr_witnesses[]={2,3,5,7,11,13,17,19,23,29,31,37};
    int mr_wcount = mr_rounds < 12 ? mr_rounds : 12;
    int mr_prime=1;
    for(int wi=0;wi<mr_wcount && mr_prime;wi++){
      uint64_t mr_a=mr_witnesses[wi];
      if(mr_a>=mr_n) continue;
      uint64_t mr_x=1, mr_base=mr_a%mr_n, mr_exp=mr_d;
      while(mr_exp>0){ if(mr_exp&1) mr_x=mr_x*mr_base%mr_n; mr_base=mr_base*mr_base%mr_n; mr_exp>>=1; }
      if(mr_x==1||mr_x==mr_n-1) continue;
      int mr_comp=1;
      for(int j=0;j<mr_r-1;j++){ mr_x=mr_x*mr_x%mr_n; if(mr_x==mr_n-1){mr_comp=0;break;} }
      if(mr_comp) mr_prime=0;
    }
    ValFree(&NV); ValFree(&RoundsV); PUSH(ValBool(mr_prime)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "MillerRabinNextPrimeC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: MillerRabinNextPrimeC() expects 1 arg\n"); return -1; }
    AKSValue NV=POP(); uint64_t np=(uint64_t)ValToInt(NV);
    if(np<2){ValFree(&NV);PUSH(ValInt(2));*stacktopptr=stacktop;return 1;}
    np++; if(np%2==0)np++;
    static const uint64_t mrn_wits[]={2,3,5,7,11,13,17,19,23,29,31,37};
    while(1){
      if(np==2||np==3){break;}
      if(np%2==0){np++;continue;}
      uint64_t mrn_d=np-1; int mrn_r=0; while(mrn_d%2==0){mrn_d/=2;mrn_r++;}
      int mrn_ok=1;
      for(int wi=0;wi<12&&mrn_ok;wi++){
        if(mrn_wits[wi]>=np)continue;
        uint64_t mrn_x=1,mrn_base=mrn_wits[wi]%np,mrn_exp=mrn_d;
        while(mrn_exp>0){if(mrn_exp&1)mrn_x=mrn_x*mrn_base%np; mrn_base=mrn_base*mrn_base%np; mrn_exp>>=1;}
        if(mrn_x==1||mrn_x==np-1)continue;
        int mrn_comp=1;
        for(int j=0;j<mrn_r-1;j++){mrn_x=mrn_x*mrn_x%np; if(mrn_x==np-1){mrn_comp=0;break;}}
        if(mrn_comp)mrn_ok=0;
      }
      if(mrn_ok)break;
      np+=2;
    }
    ValFree(&NV); PUSH(ValInt((int)np)); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "FenwickTreeNewC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: FenwickTreeNewC() expects 1 arg\n"); return -1; }
    AKSValue SzV = POP(); int ft_n = (int)ValToInt(SzV); ValFree(&SzV);
    if (ft_n <= 0) ft_n = 1;
    AKSValue tree_arr = ValArr(ft_n + 1);
    AKSArray *ta = ValToArr(tree_arr);
    for (int i = 0; i <= ft_n; i++) AksArrPush(ta, ValInt(0));
    AKSValue ft_res = ValStruct("FenwickTree");
    AKSStruct *fs = ValToObj(ft_res);
    fs->fieldnames = malloc(2 * sizeof(char*));
    fs->fieldvals  = malloc(2 * sizeof(AKSValue));
    fs->fieldnames[0] = strdup("n");    fs->fieldvals[0] = ValInt(ft_n);
    fs->fieldnames[1] = strdup("tree"); fs->fieldvals[1] = tree_arr;
    fs->fieldcount = 2;
    PUSH(ft_res); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "FenwickTreeAddC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: FenwickTreeAddC() expects 3 args\n"); return -1; }
    AKSValue DV = POP(), IV = POP(), TV = POP();
    int ft_delta = (int)ValToInt(DV), ft_i = (int)ValToInt(IV) + 1;
    AKSStruct *fs = ValToObj(TV);
    int ft_n = (int)ValToInt(fs->fieldvals[0]);
    AKSArray *ta = ValToArr(fs->fieldvals[1]);
    while (ft_i <= ft_n && ft_i < ta->count) {
      ta->items[ft_i] = ValInt(ValToInt(ta->items[ft_i]) + ft_delta);
      ft_i += ft_i & (-ft_i);
    }
    ValFree(&DV); ValFree(&IV); PUSH(TV); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "FenwickTreeQueryC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: FenwickTreeQueryC() expects 2 args\n"); return -1; }
    AKSValue IV = POP(), TV = POP();
    int ft_i = (int)ValToInt(IV) + 1;
    AKSStruct *fs = ValToObj(TV);
    AKSArray *ta = ValToArr(fs->fieldvals[1]);
    int64_t ft_sum = 0;
    while (ft_i > 0 && ft_i < ta->count) { ft_sum += ValToInt(ta->items[ft_i]); ft_i -= ft_i & (-ft_i); }
    ValFree(&IV); ValFree(&TV); PUSH(ValInt(ft_sum)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "SegTreeNewC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: SegTreeNewC() expects 1 arg\n"); return -1; }
    AKSValue SzV = POP(); int st_n = (int)ValToInt(SzV); ValFree(&SzV);
    if (st_n <= 0) st_n = 1;
    int st_sz = 4 * st_n;
    AKSValue st_arr = ValArr(st_sz);
    AKSArray *sa = ValToArr(st_arr);
    for (int i = 0; i < st_sz; i++) AksArrPush(sa, ValInt(0x7fffffff));
    AKSValue st_res = ValStruct("SegTree");
    AKSStruct *ss = ValToObj(st_res);
    ss->fieldnames = malloc(2 * sizeof(char*));
    ss->fieldvals  = malloc(2 * sizeof(AKSValue));
    ss->fieldnames[0] = strdup("n");    ss->fieldvals[0] = ValInt(st_n);
    ss->fieldnames[1] = strdup("tree"); ss->fieldvals[1] = st_arr;
    ss->fieldcount = 2;
    PUSH(st_res); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "SegTreeUpdateC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: SegTreeUpdateC() expects 3 args\n"); return -1; }
    AKSValue VV = POP(), PV = POP(), TV = POP();
    int st_pos = (int)ValToInt(PV), st_val = (int)ValToInt(VV);
    AKSStruct *ss = ValToObj(TV);
    int st_n = (int)ValToInt(ss->fieldvals[0]);
    AKSArray *sa = ValToArr(ss->fieldvals[1]);
    int st_node = 1, st_lo = 0, st_hi = st_n - 1;
    while (st_lo < st_hi) {
      int st_mid = (st_lo + st_hi) / 2;
      if (st_pos <= st_mid) { st_node = 2 * st_node; st_hi = st_mid; }
      else { st_node = 2 * st_node + 1; st_lo = st_mid + 1; }
    }
    if (st_node < sa->count) sa->items[st_node] = ValInt(st_val);
    st_node /= 2;
    while (st_node >= 1) {
      int l = 2*st_node, r = 2*st_node+1;
      if (r < sa->count) {
        int lv = (int)ValToInt(sa->items[l]), rv = (int)ValToInt(sa->items[r]);
        sa->items[st_node] = ValInt(lv < rv ? lv : rv);
      }
      st_node /= 2;
    }
    ValFree(&VV); ValFree(&PV); PUSH(TV); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "SegTreeQueryC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: SegTreeQueryC() expects 3 args\n"); return -1; }
    AKSValue RV = POP(), LV = POP(), TV = POP();
    int stq_l = (int)ValToInt(LV), stq_r = (int)ValToInt(RV);
    AKSStruct *ss = ValToObj(TV);
    int stq_n = (int)ValToInt(ss->fieldvals[0]);
    AKSArray *sa = ValToArr(ss->fieldvals[1]);
    int stq_min = 0x7fffffff;
    typedef struct { int node, lo, hi; } StqFr;
    StqFr stq_stk[128]; int stq_sp = 0;
    stq_stk[stq_sp].node = 1; stq_stk[stq_sp].lo = 0; stq_stk[stq_sp].hi = stq_n - 1; stq_sp++;
    while (stq_sp > 0) {
      StqFr fr = stq_stk[--stq_sp];
      if (fr.lo > stq_r || fr.hi < stq_l) continue;
      if (fr.lo >= stq_l && fr.hi <= stq_r) {
        if (fr.node < sa->count) { int v = (int)ValToInt(sa->items[fr.node]); if (v < stq_min) stq_min = v; }
        continue;
      }
      int mid = (fr.lo + fr.hi) / 2;
      if (stq_sp + 1 < 128) {
        if (2*fr.node < sa->count) { stq_stk[stq_sp].node=2*fr.node; stq_stk[stq_sp].lo=fr.lo; stq_stk[stq_sp].hi=mid; stq_sp++; }
        if (2*fr.node+1 < sa->count) { stq_stk[stq_sp].node=2*fr.node+1; stq_stk[stq_sp].lo=mid+1; stq_stk[stq_sp].hi=fr.hi; stq_sp++; }
      }
    }
    ValFree(&RV); ValFree(&LV); ValFree(&TV); PUSH(ValInt(stq_min)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "DSUNewC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: DSUNewC() expects 1 arg\n"); return -1; }
    AKSValue NV = POP(); int dsu_n = (int)ValToInt(NV); ValFree(&NV);
    if (dsu_n <= 0) dsu_n = 1;
    AKSValue par_arr = ValArr(dsu_n);
    AKSValue rk_arr  = ValArr(dsu_n);
    AKSArray *pa = ValToArr(par_arr), *ra = ValToArr(rk_arr);
    for (int i = 0; i < dsu_n; i++) { AksArrPush(pa, ValInt(i)); AksArrPush(ra, ValInt(0)); }
    AKSValue dsu_res = ValStruct("DSU");
    AKSStruct *ds = ValToObj(dsu_res);
    ds->fieldnames = malloc(3 * sizeof(char*));
    ds->fieldvals  = malloc(3 * sizeof(AKSValue));
    ds->fieldnames[0] = strdup("n");      ds->fieldvals[0] = ValInt(dsu_n);
    ds->fieldnames[1] = strdup("parent"); ds->fieldvals[1] = par_arr;
    ds->fieldnames[2] = strdup("rank");   ds->fieldvals[2] = rk_arr;
    ds->fieldcount = 3;
    PUSH(dsu_res); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "DSUFindC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: DSUFindC() expects 2 args\n"); return -1; }
    AKSValue XV = POP(), DV = POP();
    int dsu_x = (int)ValToInt(XV);
    AKSStruct *ds = ValToObj(DV);
    AKSArray *pa = ValToArr(ds->fieldvals[1]);
    while ((int)ValToInt(pa->items[dsu_x]) != dsu_x) {
      int par = (int)ValToInt(pa->items[dsu_x]);
      int gpar = (int)ValToInt(pa->items[par]);
      pa->items[dsu_x] = ValInt(gpar);
      dsu_x = gpar;
    }
    ValFree(&XV); PUSH(DV); PUSH(ValInt(dsu_x)); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "DSUUnionC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: DSUUnionC() expects 3 args\n"); return -1; }
    AKSValue BV = POP(), AV2 = POP(), DV = POP();
    AKSStruct *ds = ValToObj(DV);
    AKSArray *pa = ValToArr(ds->fieldvals[1]);
    AKSArray *ra = ValToArr(ds->fieldvals[2]);
    int dsu_a = (int)ValToInt(AV2), dsu_b = (int)ValToInt(BV);
    while ((int)ValToInt(pa->items[dsu_a]) != dsu_a) { int p=(int)ValToInt(pa->items[dsu_a]); pa->items[dsu_a]=pa->items[p]; dsu_a=(int)ValToInt(pa->items[dsu_a]); }
    while ((int)ValToInt(pa->items[dsu_b]) != dsu_b) { int p=(int)ValToInt(pa->items[dsu_b]); pa->items[dsu_b]=pa->items[p]; dsu_b=(int)ValToInt(pa->items[dsu_b]); }
    if (dsu_a != dsu_b) {
      if (ValToInt(ra->items[dsu_a]) < ValToInt(ra->items[dsu_b])) { int t=dsu_a; dsu_a=dsu_b; dsu_b=t; }
      pa->items[dsu_b] = ValInt(dsu_a);
      if (ValToInt(ra->items[dsu_a]) == ValToInt(ra->items[dsu_b])) ra->items[dsu_a] = ValInt(ValToInt(ra->items[dsu_a]) + 1);
    }
    ValFree(&AV2); ValFree(&BV); PUSH(DV); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "DSUSameC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: DSUSameC() expects 3 args\n"); return -1; }
    AKSValue BV = POP(), AV2 = POP(), DV = POP();
    AKSStruct *ds = ValToObj(DV);
    AKSArray *pa = ValToArr(ds->fieldvals[1]);
    int dsu_a = (int)ValToInt(AV2), dsu_b = (int)ValToInt(BV);
    while ((int)ValToInt(pa->items[dsu_a]) != dsu_a) { int p=(int)ValToInt(pa->items[dsu_a]); pa->items[dsu_a]=pa->items[p]; dsu_a=(int)ValToInt(pa->items[dsu_a]); }
    while ((int)ValToInt(pa->items[dsu_b]) != dsu_b) { int p=(int)ValToInt(pa->items[dsu_b]); pa->items[dsu_b]=pa->items[p]; dsu_b=(int)ValToInt(pa->items[dsu_b]); }
    int same = (dsu_a == dsu_b) ? 1 : 0;
    ValFree(&AV2); ValFree(&BV); ValFree(&DV); PUSH(ValBool(same)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "MinMaxHeapNewC") == 0) {
    AKSValue data_arr = ValArr(8);
    AKSValue mmh_res = ValStruct("MinMaxHeap");
    AKSStruct *ms = ValToObj(mmh_res);
    ms->fieldnames = malloc(2 * sizeof(char*));
    ms->fieldvals  = malloc(2 * sizeof(AKSValue));
    ms->fieldnames[0] = strdup("size"); ms->fieldvals[0] = ValInt(0);
    ms->fieldnames[1] = strdup("data"); ms->fieldvals[1] = data_arr;
    ms->fieldcount = 2;
    PUSH(mmh_res); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "MinMaxHeapPushC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: MinMaxHeapPushC() expects 2 args\n"); return -1; }
    AKSValue VV = POP(), HV = POP();
    AKSStruct *ms = ValToObj(HV);
    int mmh_n = (int)ValToInt(ms->fieldvals[0]);
    AKSArray *da = ValToArr(ms->fieldvals[1]);
    AksArrPush(da, ValCopy(VV));
    ms->fieldvals[0] = ValInt(mmh_n + 1);
    int idx = mmh_n;
    while (idx > 0) {
      int par = (idx - 1) / 2;
      int lvl = 0; int tmp = idx + 1; while (tmp > 1) { tmp >>= 1; lvl++; }
      int64_t iv = ValToInt(da->items[idx]), pv = ValToInt(da->items[par]);
      if (lvl % 2 == 0) {
        if (iv > pv) { AKSValue t=da->items[idx]; da->items[idx]=da->items[par]; da->items[par]=t; idx=par; }
        else { int gp=(par-1)/2; while (idx>2&&gp>=0) { if (ValToInt(da->items[idx])<ValToInt(da->items[gp])) { AKSValue t=da->items[idx]; da->items[idx]=da->items[gp]; da->items[gp]=t; idx=gp; par=(idx-1)/2; gp=(par-1)/2; } else break; } break; }
      } else {
        if (iv < pv) { AKSValue t=da->items[idx]; da->items[idx]=da->items[par]; da->items[par]=t; idx=par; }
        else { int gp=(par-1)/2; while (idx>2&&gp>=0) { if (ValToInt(da->items[idx])>ValToInt(da->items[gp])) { AKSValue t=da->items[idx]; da->items[idx]=da->items[gp]; da->items[gp]=t; idx=gp; par=(idx-1)/2; gp=(par-1)/2; } else break; } break; }
      }
    }
    ValFree(&VV); PUSH(HV); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "MinMaxHeapMinC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: MinMaxHeapMinC() expects 1 arg\n"); return -1; }
    AKSValue HV = POP();
    AKSStruct *ms = ValToObj(HV);
    int mmh_n = (int)ValToInt(ms->fieldvals[0]);
    if (mmh_n == 0) { ValFree(&HV); PUSH(ValInt(0)); *stacktopptr = stacktop; return 1; }
    AKSArray *da = ValToArr(ms->fieldvals[1]);
    AKSValue mmh_min = ValCopy(da->items[0]);
    ValFree(&HV); PUSH(mmh_min); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "MinMaxHeapMaxC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: MinMaxHeapMaxC() expects 1 arg\n"); return -1; }
    AKSValue HV = POP();
    AKSStruct *ms = ValToObj(HV);
    int mmh_n = (int)ValToInt(ms->fieldvals[0]);
    AKSArray *da = ValToArr(ms->fieldvals[1]);
    AKSValue mmh_max;
    if (mmh_n == 0) mmh_max = ValInt(0);
    else if (mmh_n == 1) mmh_max = ValCopy(da->items[0]);
    else if (mmh_n == 2) mmh_max = ValCopy(da->items[1]);
    else mmh_max = ValCopy(ValToInt(da->items[1]) >= ValToInt(da->items[2]) ? da->items[1] : da->items[2]);
    ValFree(&HV); PUSH(mmh_max); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "MinMaxHeapSizeC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: MinMaxHeapSizeC() expects 1 arg\n"); return -1; }
    AKSValue HV = POP();
    AKSStruct *ms = ValToObj(HV);
    int sz = (int)ValToInt(ms->fieldvals[0]);
    ValFree(&HV); PUSH(ValInt(sz)); *stacktopptr = stacktop; return 1;
  }


  if (strcmp(name, "JSONParseC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: JSONParseC() expects 1 arg\n"); return -1; }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) { ValFree(&SV); PUSH(ValNull()); *stacktopptr = stacktop; return 1; }
    AksJP jp; jp.s = ValToStrPtr(SV)->data; jp.pos = 0;
    AKSValue result = AksJPParseValue(&jp);
    ValFree(&SV); PUSH(result); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "CSVParseC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: CSVParseC() expects 1 arg\n"); return -1; }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) { ValFree(&SV); PUSH(ValNull()); *stacktopptr = stacktop; return 1; }
    const char *csv_s = ValToStrPtr(SV)->data;
    int csv_len = (int)strlen(csv_s);
    char *cell = malloc(csv_len + 1); int clen = 0; int in_q = 0;
    AKSValue rows = ValArr(0); AKSArray *rows_a = ValToArr(rows);
    AKSValue cur = ValArr(0); AKSArray *cur_a = ValToArr(cur);
    for (int i = 0; i <= csv_len; i++) {
      char c = (i == csv_len) ? '\n' : csv_s[i];
      if (i == csv_len && clen == 0 && cur_a->count == 0) break;
      if (in_q) {
        if (c == '"') { if (i+1<csv_len && csv_s[i+1]=='"') { cell[clen++]='"'; i++; } else in_q=0; }
        else cell[clen++] = c;
      } else if (c == '"') { in_q = 1; }
      else if (c == ',') {
        cell[clen]=0; AksArrPush(cur_a, ValStr(cell)); clen=0;
      } else if (c == '\n' || c == '\r') {
        if (c=='\r' && i+1<csv_len && csv_s[i+1]=='\n') i++;
        cell[clen]=0; AksArrPush(cur_a, ValStr(cell)); clen=0;
        AksArrPush(rows_a, cur);
        cur = ValArr(0); cur_a = ValToArr(cur);
      } else { cell[clen++] = c; }
    }
    free(cell); ValFree(&cur); ValFree(&SV);
    PUSH(rows); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "TOMLParseC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: TOMLParseC() expects 1 arg\n"); return -1; }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) { ValFree(&SV); PUSH(ValNull()); *stacktopptr = stacktop; return 1; }
    const char *toml_s = ValToStrPtr(SV)->data;
    AKSValue root = ValStruct("TOMLDoc");
    AKSStruct *rs = ValToObj(root);
    rs->fieldnames = NULL; rs->fieldvals = NULL; rs->fieldcount = 0;
    char tline[1024]; int tli = 0;
    int cur_sec = -1;
    for (int ci = 0; ; ci++) {
      char tc = toml_s[ci];
      if (tc == '\n' || tc == 0) {
        tline[tli] = 0; tli = 0;
        char *lp = tline;
        while (*lp==' '||*lp=='\t') lp++;
        if (!*lp || *lp=='#') { if (!tc) break; continue; }
        if (*lp == '[') {
          lp++;
          char sn[256]; int sni=0;
          while (*lp && *lp!=']') sn[sni++]=*lp++;
          sn[sni]=0;
          int found=-1;
          for (int i=0;i<rs->fieldcount;i++) if (!strcmp(rs->fieldnames[i],sn)){found=i;break;}
          if (found<0) {
            rs->fieldnames = realloc(rs->fieldnames, (rs->fieldcount+1)*sizeof(char*));
            rs->fieldvals  = realloc(rs->fieldvals,  (rs->fieldcount+1)*sizeof(AKSValue));
            rs->fieldnames[rs->fieldcount] = strdup(sn);
            rs->fieldvals[rs->fieldcount]  = ValStruct(sn);
            AKSStruct *ns = ValToObj(rs->fieldvals[rs->fieldcount]);
            ns->fieldnames=NULL; ns->fieldvals=NULL; ns->fieldcount=0;
            cur_sec = rs->fieldcount;
            rs->fieldcount++;
          } else cur_sec = found;
        } else {
          char *eq = strchr(lp,'=');
          if (eq) {
            char tkey[256]; int tki=0;
            for (char *kp=lp; kp<eq && *kp!=' '; ) tkey[tki++]=*kp++;
            tkey[tki]=0;
            char *vp=eq+1; while (*vp==' ') vp++;
            AKSValue tval;
            if (*vp=='"') { vp++; char vb[512]; int vi=0; while (*vp&&*vp!='"') vb[vi++]=*vp++; vb[vi]=0; tval=ValStr(vb); }
            else if (!strncmp(vp,"true",4)) tval=ValBool(1);
            else if (!strncmp(vp,"false",5)) tval=ValBool(0);
            else if (strchr(vp,'.')) tval=ValFloat(atof(vp));
            else tval=ValInt(atol(vp));
            AKSStruct *target = (cur_sec>=0) ? ValToObj(rs->fieldvals[cur_sec]) : rs;
            target->fieldnames = realloc(target->fieldnames,(target->fieldcount+1)*sizeof(char*));
            target->fieldvals  = realloc(target->fieldvals, (target->fieldcount+1)*sizeof(AKSValue));
            target->fieldnames[target->fieldcount] = strdup(tkey);
            target->fieldvals[target->fieldcount]  = tval;
            target->fieldcount++;
          }
        }
        if (!tc) break;
      } else { if (tli<1023) tline[tli++]=tc; }
    }
    ValFree(&SV); PUSH(root); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "RegexMatchC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: RegexMatchC() expects 2 args (pattern, text)\n"); return -1; }
    AKSValue TV = POP(), PV = POP();
    if (ValKind(PV) != VALSTR || ValKind(TV) != VALSTR) { ValFree(&PV); ValFree(&TV); PUSH(ValBool(0)); *stacktopptr = stacktop; return 1; }
    const char *rx_pat = ValToStrPtr(PV)->data, *rx_txt = ValToStrPtr(TV)->data;
    int rx_found = 0;
    const char *t = rx_txt;
    if (rx_pat[0] == '^') { rx_found = AksRxMatchHere(rx_pat+1, t); }
    else { do { if (AksRxMatchHere(rx_pat, t)) { rx_found=1; break; } } while (*t++); }
    ValFree(&PV); ValFree(&TV); PUSH(ValBool(rx_found)); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "RegexReplaceC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: RegexReplaceC() expects 3 args (pattern, replacement, text)\n"); return -1; }
    AKSValue TxV = POP(), RpV = POP(), PtV = POP();
    if (ValKind(PtV)!=VALSTR||ValKind(RpV)!=VALSTR||ValKind(TxV)!=VALSTR) {
      ValFree(&PtV); ValFree(&RpV); ValFree(&TxV); PUSH(ValBool(0)); *stacktopptr=stacktop; return 1;
    }
    const char *rxr_pat=ValToStrPtr(PtV)->data, *rxr_rep=ValToStrPtr(RpV)->data, *rxr_txt=ValToStrPtr(TxV)->data;
    char rxr_out[8192]; int rxr_oi=0;
    int rxr_anchor=(rxr_pat[0]=='^');
    const char *pat2=rxr_anchor?rxr_pat+1:rxr_pat, *tp=rxr_txt;
    while (*tp && rxr_oi<8000) {
      int mlen=0;
      if (AksRxMatchLen(pat2, tp, &mlen)) {
        const char *rp=rxr_rep;
        while (*rp && rxr_oi<8000) rxr_out[rxr_oi++]=*rp++;
        tp += mlen>0?mlen:1;
        if (rxr_anchor) break;
      } else rxr_out[rxr_oi++]=*tp++;
    }
    while (*tp && rxr_oi<8191) rxr_out[rxr_oi++]=*tp++;
    rxr_out[rxr_oi]=0;
    ValFree(&PtV); ValFree(&RpV); ValFree(&TxV);
    PUSH(ValStr(rxr_out)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "RegexSplitC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: RegexSplitC() expects 2 args (pattern, text)\n"); return -1; }
    AKSValue TxV = POP(), PtV = POP();
    if (ValKind(PtV)!=VALSTR||ValKind(TxV)!=VALSTR) { ValFree(&PtV); ValFree(&TxV); PUSH(ValNull()); *stacktopptr=stacktop; return 1; }
    const char *rxs_pat=ValToStrPtr(PtV)->data, *rxs_txt=ValToStrPtr(TxV)->data;
    AKSValue parts=ValArr(4); AKSArray *pa=ValToArr(parts);
    const char *sp=rxs_txt, *seg=sp;
    while (*sp) {
      int mlen=0;
      if (AksRxMatchLen(rxs_pat, sp, &mlen) && mlen>0) {
        int slen=(int)(sp-seg);
        char *sbuf=malloc(slen+1); memcpy(sbuf,seg,slen); sbuf[slen]=0;
        AksArrPush(pa, ValStr(sbuf)); free(sbuf);
        sp+=mlen; seg=sp;
      } else sp++;
    }
    int slen2=(int)(sp-seg);
    char *sbuf2=malloc(slen2+1); memcpy(sbuf2,seg,slen2); sbuf2[slen2]=0;
    AksArrPush(pa, ValStr(sbuf2)); free(sbuf2);
    ValFree(&PtV); ValFree(&TxV); PUSH(parts); *stacktopptr=stacktop; return 1;
  }


  if (strcmp(name, "SkipListNewC") == 0) {
    AKSValue sl = ValStruct("SkipList");
    AKSStruct *ss = ValToObj(sl);
    ss->fieldnames = malloc(2 * sizeof(char*));
    ss->fieldvals  = malloc(2 * sizeof(AKSValue));
    ss->fieldnames[0] = strdup("len");   ss->fieldvals[0] = ValInt(0);
    ss->fieldnames[1] = strdup("nodes"); ss->fieldvals[1] = ValArr(0);
    ss->fieldcount = 2;
    PUSH(sl); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "SkipListInsertC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: SkipListInsertC() expects 2 args\n"); return -1; }
    AKSValue KV = POP(), SV = POP();
    AKSStruct *sl = ValToObj(SV);
    AKSArray *nodes = ValToArr(sl->fieldvals[1]);
    int64_t key = ValToInt(KV);
    int lo=0, hi=nodes->count-1, found=0;
    while (lo<=hi) {
      int m=(lo+hi)/2;
      AKSStruct *nd=ValToObj(nodes->items[m]);
      int64_t mk=ValToInt(nd->fieldvals[0]);
      if (mk==key){found=1;break;} else if(mk<key)lo=m+1; else hi=m-1;
    }
    if (!found) {
      AKSValue node = ValStruct("SLNode");
      AKSStruct *nd = ValToObj(node);
      nd->fieldnames = malloc(sizeof(char*)); nd->fieldvals = malloc(sizeof(AKSValue));
      nd->fieldnames[0] = strdup("key"); nd->fieldvals[0] = ValCopy(KV);
      nd->fieldcount = 1;
      AksArrPush(nodes, node);
      sl->fieldvals[0] = ValInt(ValToInt(sl->fieldvals[0]) + 1);
      for (int i = nodes->count-1; i > 0; i--) {
        AKSStruct *a=ValToObj(nodes->items[i-1]), *b=ValToObj(nodes->items[i]);
        if (ValToInt(a->fieldvals[0]) > ValToInt(b->fieldvals[0])) {
          AKSValue t=nodes->items[i-1]; nodes->items[i-1]=nodes->items[i]; nodes->items[i]=t;
        } else break;
      }
    }
    ValFree(&KV); PUSH(SV); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "SkipListContainsC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: SkipListContainsC() expects 2 args\n"); return -1; }
    AKSValue KV = POP(), SV = POP();
    AKSStruct *sl = ValToObj(SV);
    AKSArray *nodes = ValToArr(sl->fieldvals[1]);
    int64_t key = ValToInt(KV);
    int lo=0, hi=nodes->count-1, found=0;
    while (lo<=hi){int m=(lo+hi)/2;AKSStruct *nd=ValToObj(nodes->items[m]);int64_t mk=ValToInt(nd->fieldvals[0]);if(mk==key){found=1;break;}else if(mk<key)lo=m+1;else hi=m-1;}
    ValFree(&KV); ValFree(&SV); PUSH(ValBool(found)); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "SkipListLenC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: SkipListLenC() expects 1 arg\n"); return -1; }
    AKSValue SV = POP();
    int len = (int)ValToInt(ValToObj(SV)->fieldvals[0]);
    ValFree(&SV); PUSH(ValInt(len)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "TreapNewC") == 0) {
    AKSValue tv2 = ValStruct("Treap");
    AKSStruct *ts = ValToObj(tv2);
    ts->fieldnames = malloc(2 * sizeof(char*));
    ts->fieldvals  = malloc(2 * sizeof(AKSValue));
    ts->fieldnames[0] = strdup("len");  ts->fieldvals[0] = ValInt(0);
    ts->fieldnames[1] = strdup("keys"); ts->fieldvals[1] = ValArr(0);
    ts->fieldcount = 2;
    PUSH(tv2); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "TreapInsertC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: TreapInsertC() expects 2 args\n"); return -1; }
    AKSValue KV = POP(), TV = POP();
    AKSStruct *ts = ValToObj(TV);
    AKSArray *ka = ValToArr(ts->fieldvals[1]);
    int64_t key = ValToInt(KV);
    int lo=0,hi=ka->count-1,found=0;
    while(lo<=hi){int m=(lo+hi)/2;int64_t mk=ValToInt(ka->items[m]);if(mk==key){found=1;break;}else if(mk<key)lo=m+1;else hi=m-1;}
    if (!found) {
      AksArrPush(ka, ValCopy(KV));
      for (int i=ka->count-1;i>0;i--){if(ValToInt(ka->items[i-1])>ValToInt(ka->items[i])){AKSValue t2=ka->items[i-1];ka->items[i-1]=ka->items[i];ka->items[i]=t2;}else break;}
      ts->fieldvals[0] = ValInt(ValToInt(ts->fieldvals[0]) + 1);
    }
    ValFree(&KV); PUSH(TV); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "TreapContainsC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: TreapContainsC() expects 2 args\n"); return -1; }
    AKSValue KV = POP(), TV = POP();
    AKSStruct *ts = ValToObj(TV);
    AKSArray *ka = ValToArr(ts->fieldvals[1]);
    int64_t key = ValToInt(KV); int found=0;
    int lo=0,hi=ka->count-1;
    while(lo<=hi){int m=(lo+hi)/2;int64_t mk=ValToInt(ka->items[m]);if(mk==key){found=1;break;}else if(mk<key)lo=m+1;else hi=m-1;}
    ValFree(&KV); ValFree(&TV); PUSH(ValBool(found)); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "TreapMinC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: TreapMinC() expects 1 arg\n"); return -1; }
    AKSValue TV = POP(); AKSStruct *ts=ValToObj(TV); AKSArray *ka=ValToArr(ts->fieldvals[1]);
    AKSValue res=(ka->count>0)?ValCopy(ka->items[0]):ValNull();
    ValFree(&TV); PUSH(res); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "TreapMaxC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: TreapMaxC() expects 1 arg\n"); return -1; }
    AKSValue TV = POP(); AKSStruct *ts=ValToObj(TV); AKSArray *ka=ValToArr(ts->fieldvals[1]);
    AKSValue res=(ka->count>0)?ValCopy(ka->items[ka->count-1]):ValNull();
    ValFree(&TV); PUSH(res); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "Base32EncodeC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: Base32EncodeC() expects 1 arg\n"); return -1; }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) { ValFree(&SV); PUSH(ValStr("")); *stacktopptr = stacktop; return 1; }
    static const char b32alph[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    const unsigned char *inp = (const unsigned char *)ValToStrPtr(SV)->data;
    int inlen = ValToStrPtr(SV)->len;
    int outlen = ((inlen + 4) / 5) * 8;
    char *out = malloc(outlen + 1); int oi = 0;
    for (int i = 0; i < inlen; i += 5) {
      uint64_t buf = 0; int nb = inlen-i < 5 ? inlen-i : 5;
      for (int j = 0; j < nb; j++) buf = (buf << 8) | inp[i+j];
      buf <<= (5-nb)*8;
      int nchars = (nb*8+4)/5;
      for (int j = 0; j < 8; j++) { out[oi++] = (j<nchars) ? b32alph[(buf>>(35-j*5))&0x1f] : '='; }
    }
    out[oi] = 0;
    ValFree(&SV); PUSH(ValStr(out)); free(out); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "Base32DecodeC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: Base32DecodeC() expects 1 arg\n"); return -1; }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) { ValFree(&SV); PUSH(ValStr("")); *stacktopptr = stacktop; return 1; }
    const unsigned char *inp2 = (const unsigned char *)ValToStrPtr(SV)->data;
    int inlen2 = ValToStrPtr(SV)->len;
    char *out2 = malloc(inlen2 + 1); int oi2 = 0;
    static const signed char b32dec[256] = {
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,26,27,28,29,30,31,-1,-1,-1,-1,-1,-1,-1,-1,
      -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
      -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    for (int i = 0; i+7 < inlen2; i += 8) {
      uint64_t buf2=0; int pad=0;
      for (int j=0;j<8;j++) { if(inp2[i+j]=='='){pad++;buf2<<=5;}else buf2=(buf2<<5)|(uint64_t)b32dec[inp2[i+j]]; }
      int nb2 = 5 - (pad*5+7)/8;
      uint64_t shifted = buf2 << (pad * 5);
      for (int j=0;j<nb2;j++) out2[oi2++]=(char)((shifted>>((4-j)*8))&0xff);
    }
    out2[oi2]=0;
    ValFree(&SV); PUSH(ValStr(out2)); free(out2); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "Base85EncodeC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: Base85EncodeC() expects 1 arg\n"); return -1; }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) { ValFree(&SV); PUSH(ValStr("")); *stacktopptr = stacktop; return 1; }
    const unsigned char *b85in = (const unsigned char *)ValToStrPtr(SV)->data;
    int b85len = ValToStrPtr(SV)->len;
    char *b85out = malloc(((b85len+3)/4)*5 + 10); int b85oi = 0;
    for (int i = 0; i < b85len; i += 4) {
      uint32_t val2=0; int nb=b85len-i<4?b85len-i:4;
      for (int j=0;j<nb;j++) val2=(val2<<8)|b85in[i+j];
      val2 <<= (4-nb)*8;
      if (val2==0 && nb==4) { b85out[b85oi++]='z'; }
      else { char tmp[5]; for(int j=4;j>=0;j--){tmp[j]=(char)('!'+val2%85);val2/=85;} for(int j=0;j<=nb;j++)b85out[b85oi++]=tmp[j]; }
    }
    b85out[b85oi]=0;
    ValFree(&SV); PUSH(ValStr(b85out)); free(b85out); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "Base85DecodeC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: Base85DecodeC() expects 1 arg\n"); return -1; }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) { ValFree(&SV); PUSH(ValStr("")); *stacktopptr = stacktop; return 1; }
    const char *b85di = ValToStrPtr(SV)->data;
    int b85dl = ValToStrPtr(SV)->len;
    char *b85do = malloc(b85dl + 8); int b85doi = 0;
    for (int i=0; i<b85dl; ) {
      if (b85di[i]=='z') { b85do[b85doi++]=0;b85do[b85doi++]=0;b85do[b85doi++]=0;b85do[b85doi++]=0;i++;continue; }
      uint32_t acc=0; int cnt=0;
      while (i<b85dl && cnt<5) { acc=acc*85+(uint32_t)(b85di[i++]-'!'); cnt++; }
      if (cnt <= 1) break;
      int pad = 5 - cnt;
      for (int p=0; p<pad; p++) acc = acc * 85 + 84;
      char tmp[4];
      for (int j=3;j>=0;j--){tmp[j]=(char)(acc&0xff);acc>>=8;}
      for (int j=0;j<cnt-1;j++) b85do[b85doi++]=tmp[j];
    }
    b85do[b85doi]=0;
    ValFree(&SV); PUSH(ValStr(b85do)); free(b85do); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "IniParseC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: IniParseC() expects 1 arg\n"); return -1; }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) { ValFree(&SV); PUSH(ValNull()); *stacktopptr = stacktop; return 1; }
    const char *ini = ValToStrPtr(SV)->data;
    AKSValue root2 = ValStruct("IniDoc");
    AKSStruct *rs2 = ValToObj(root2);
    rs2->fieldnames=NULL; rs2->fieldvals=NULL; rs2->fieldcount=0;
    AKSValue glo = ValStruct("global");
    AKSStruct *gs2 = ValToObj(glo);
    gs2->fieldnames=NULL; gs2->fieldvals=NULL; gs2->fieldcount=0;
    rs2->fieldnames=malloc(sizeof(char*)); rs2->fieldvals=malloc(sizeof(AKSValue));
    rs2->fieldnames[0]=strdup("global"); rs2->fieldvals[0]=glo; rs2->fieldcount=1;
    int ini_cur=0;
    char iline[1024]; int ili=0;
    for (int ci=0;;ci++) {
      char ic=ini[ci];
      if (ic=='\n'||ic==0) {
        iline[ili]=0; ili=0;
        char *lp=iline; while(*lp==' '||*lp=='\t')lp++;
        if (!*lp||*lp==';'||*lp=='#') { if(!ic) break; continue; }
        if (*lp=='[') {
          lp++; char sn[256]; int sni=0;
          while(*lp&&*lp!=']') sn[sni++]=*lp++;
          sn[sni]=0;
          int found=-1;
          for(int i=0;i<rs2->fieldcount;i++) if(!strcmp(rs2->fieldnames[i],sn)){found=i;break;}
          if (found<0) {
            AKSValue ns2=ValStruct(sn); AKSStruct *nss=ValToObj(ns2);
            nss->fieldnames=NULL; nss->fieldvals=NULL; nss->fieldcount=0;
            rs2->fieldnames=realloc(rs2->fieldnames,(rs2->fieldcount+1)*sizeof(char*));
            rs2->fieldvals=realloc(rs2->fieldvals,(rs2->fieldcount+1)*sizeof(AKSValue));
            rs2->fieldnames[rs2->fieldcount]=strdup(sn);
            rs2->fieldvals[rs2->fieldcount]=ns2;
            ini_cur=rs2->fieldcount; rs2->fieldcount++;
          } else ini_cur=found;
        } else {
          char *eq2=strchr(lp,'='); if(!eq2) eq2=strchr(lp,':');
          if (eq2) {
            char ikey[256]; int iki=0;
            char *kp=lp; while(kp<eq2&&*kp!=' ')ikey[iki++]=*kp++;
            ikey[iki]=0;
            char *vp=eq2+1; while(*vp==' ')vp++;
            int vlen=(int)strlen(vp);
            while(vlen>0&&(vp[vlen-1]=='\r'||vp[vlen-1]==' '))vlen--;
            AKSStruct *tgt=ValToObj(rs2->fieldvals[ini_cur]);
            tgt->fieldnames=realloc(tgt->fieldnames,(tgt->fieldcount+1)*sizeof(char*));
            tgt->fieldvals=realloc(tgt->fieldvals,(tgt->fieldcount+1)*sizeof(AKSValue));
            tgt->fieldnames[tgt->fieldcount]=strdup(ikey);
            tgt->fieldvals[tgt->fieldcount]=ValStrLen(vp,vlen);
            tgt->fieldcount++;
          }
        }
        if (!ic) break;
      } else { if(ili<1023)iline[ili++]=ic; }
    }
    ValFree(&SV); PUSH(root2); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "MsgPackEncodeC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: MsgPackEncodeC() expects 1 arg\n"); return -1; }
    AKSValue AV = POP();
    unsigned char *mp_buf = malloc(65536); int mp_len = 0;
    typedef struct { AKSValue v; int done; } MpFrame;
    MpFrame mp_stk[512]; int mp_sp = 0;
    mp_stk[mp_sp].v = AV; mp_stk[mp_sp].done = 0; mp_sp++;
    while (mp_sp > 0 && mp_len < 65000) {
      MpFrame *fr = &mp_stk[--mp_sp];
      AKSValue v = fr->v;
      ValType vk = ValKind(v);
      if (vk == VALINT) {
        int64_t iv=ValToInt(v);
        if(iv>=0&&iv<=127){mp_buf[mp_len++]=(uint8_t)iv;}
        else if(iv<0&&iv>=-32){mp_buf[mp_len++]=(uint8_t)(0xe0|(iv&0x1f));}
        else if(iv>=0&&iv<=255){mp_buf[mp_len++]=0xcc;mp_buf[mp_len++]=(uint8_t)iv;}
        else if(iv>=0&&iv<=65535){mp_buf[mp_len++]=0xcd;mp_buf[mp_len++]=(uint8_t)(iv>>8);mp_buf[mp_len++]=(uint8_t)iv;}
        else{mp_buf[mp_len++]=0xd3;for(int j=7;j>=0;j--)mp_buf[mp_len++]=(uint8_t)((iv>>(j*8))&0xff);}
      } else if (vk == VALFLOAT) {
        double fv=ValToFloat(v); union{double d;uint64_t u;} u2; u2.d=fv;
        mp_buf[mp_len++]=0xcb;
        for(int j=7;j>=0;j--)mp_buf[mp_len++]=(uint8_t)((u2.u>>(j*8))&0xff);
      } else if (vk == VALBOOL) {
        mp_buf[mp_len++]=ValToBool(v)?0xc3:0xc2;
      } else if (vk == VALNULL) {
        mp_buf[mp_len++]=0xc0;
      } else if (vk == VALSTR) {
        AKSString *ss3=ValToStrPtr(v); int sl3=ss3->len;
        if(sl3<=31){mp_buf[mp_len++]=(uint8_t)(0xa0|sl3);}
        else{mp_buf[mp_len++]=0xd9;mp_buf[mp_len++]=(uint8_t)sl3;}
        if(mp_len+sl3<65000){memcpy(mp_buf+mp_len,ss3->data,sl3);mp_len+=sl3;}
      } else if (vk == VALARR) {
        AKSArray *ar=ValToArr(v); int ac=ar->count;
        if(ac<=15){mp_buf[mp_len++]=(uint8_t)(0x90|ac);}
        else{mp_buf[mp_len++]=0xdc;mp_buf[mp_len++]=(uint8_t)(ac>>8);mp_buf[mp_len++]=(uint8_t)ac;}
        for(int i=ac-1;i>=0&&mp_sp<510;i--){mp_stk[mp_sp].v=ar->items[i];mp_stk[mp_sp].done=0;mp_sp++;}
      } else if (vk == VALSTRUCT) {
        AKSStruct *ss4=ValToObj(v); int fc4=ss4->fieldcount;
        if(fc4<=15){mp_buf[mp_len++]=(uint8_t)(0x80|fc4);}
        else{mp_buf[mp_len++]=0xde;mp_buf[mp_len++]=(uint8_t)(fc4>>8);mp_buf[mp_len++]=(uint8_t)fc4;}
        for(int i=fc4-1;i>=0&&mp_sp<508;i--){
          mp_stk[mp_sp].v=ss4->fieldvals[i];mp_stk[mp_sp].done=0;mp_sp++;
          const char *kd=ss4->fieldnames[i];int kl2=(int)strlen(kd);
          if(kl2<=31){mp_buf[mp_len++]=(uint8_t)(0xa0|kl2);}else{mp_buf[mp_len++]=0xd9;mp_buf[mp_len++]=(uint8_t)kl2;}
          if(mp_len+kl2<65000){memcpy(mp_buf+mp_len,kd,kl2);mp_len+=kl2;}
        }
      }
    }
    ValFree(&AV);
    char *hex_out=malloc(mp_len*2+1);
    for(int i=0;i<mp_len;i++)sprintf(hex_out+i*2,"%02x",(unsigned int)mp_buf[i]);
    hex_out[mp_len*2]=0;
    free(mp_buf);
    PUSH(ValStr(hex_out)); free(hex_out); *stacktopptr=stacktop; return 1;
  }


  if (strcmp(name, "OsClipboardGetC") == 0) {
    if (!OpenClipboard(NULL)) { PUSH(ValNull()); *stacktopptr = stacktop; return 1; }
    HANDLE hcb = GetClipboardData(CF_TEXT);
    if (!hcb) { CloseClipboard(); PUSH(ValNull()); *stacktopptr = stacktop; return 1; }
    const char *cbp = (const char *)GlobalLock(hcb);
    AKSValue cbres = cbp ? ValStr(cbp) : ValNull();
    if (cbp) GlobalUnlock(hcb);
    CloseClipboard();
    PUSH(cbres); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "OsClipboardSetC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: OsClipboardSetC() expects 1 arg\n"); return -1; }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) { ValFree(&SV); PUSH(ValBool(0)); *stacktopptr = stacktop; return 1; }
    const char *txt = ValToStrPtr(SV)->data;
    int tlen = ValToStrPtr(SV)->len;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(tlen + 1));
    int cbset_ok = 0;
    if (hg) {
      char *dst = (char *)GlobalLock(hg);
      memcpy(dst, txt, tlen); dst[tlen] = 0;
      GlobalUnlock(hg);
      if (OpenClipboard(NULL)) {
        EmptyClipboard();
        cbset_ok = SetClipboardData(CF_TEXT, hg) != NULL;
        CloseClipboard();
      } else GlobalFree(hg);
    }
    ValFree(&SV); PUSH(ValBool(cbset_ok)); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "OsClipboardClearC") == 0) {
    int ok3 = 0;
    if (OpenClipboard(NULL)) { EmptyClipboard(); CloseClipboard(); ok3 = 1; }
    PUSH(ValBool(ok3)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "OsProcessSnapshotC") == 0) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    AKSValue ps_arr = ValArr(32); AKSArray *ps_a = ValToArr(ps_arr);
    if (snap != INVALID_HANDLE_VALUE) {
      PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
      if (Process32First(snap, &pe)) {
        do {
          AKSValue entry = ValStruct("ProcessEntry");
          AKSStruct *es = ValToObj(entry);
          es->fieldnames = malloc(3 * sizeof(char*));
          es->fieldvals  = malloc(3 * sizeof(AKSValue));
          es->fieldnames[0] = strdup("pid");  es->fieldvals[0] = ValInt((int64_t)pe.th32ProcessID);
          es->fieldnames[1] = strdup("ppid"); es->fieldvals[1] = ValInt((int64_t)pe.th32ParentProcessID);
          es->fieldnames[2] = strdup("name"); es->fieldvals[2] = ValStr(pe.szExeFile);
          es->fieldcount = 3;
          AksArrPush(ps_a, entry);
        } while (Process32Next(snap, &pe));
      }
      CloseHandle(snap);
    }
    PUSH(ps_arr); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "OsModuleEnumC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: OsModuleEnumC() expects 1 arg (pid)\n"); return -1; }
    AKSValue PV = POP(); DWORD me_pid = (DWORD)ValToInt(PV); ValFree(&PV);
    HANDLE me_proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, me_pid);
    AKSValue me_arr = ValArr(16); AKSArray *me_a = ValToArr(me_arr);
    if (me_proc) {
      HMODULE mods[1024]; DWORD me_needed = 0;
      if (EnumProcessModules(me_proc, mods, sizeof(mods), &me_needed)) {
        int me_cnt = (int)(me_needed / sizeof(HMODULE));
        for (int i = 0; i < me_cnt; i++) {
          char mname[MAX_PATH] = {0};
          if (GetModuleFileNameExA(me_proc, mods[i], mname, sizeof(mname))) {
            AKSValue me_e = ValStruct("ModuleEntry");
            AKSStruct *me_s = ValToObj(me_e);
            me_s->fieldnames = malloc(2 * sizeof(char*));
            me_s->fieldvals  = malloc(2 * sizeof(AKSValue));
            me_s->fieldnames[0] = strdup("base"); me_s->fieldvals[0] = ValInt((int64_t)(uintptr_t)mods[i]);
            me_s->fieldnames[1] = strdup("name"); me_s->fieldvals[1] = ValStr(mname);
            me_s->fieldcount = 2;
            AksArrPush(me_a, me_e);
          }
        }
      }
      CloseHandle(me_proc);
    }
    PUSH(me_arr); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "OsWindowEnumC") == 0) {
    AKSValue we_arr = ValArr(32); AKSArray *we_a = ValToArr(we_arr);
    HWND hw2 = GetTopWindow(NULL);
    while (hw2) {
      char we_title[512] = {0};
      GetWindowTextA(hw2, we_title, sizeof(we_title));
      if (IsWindowVisible(hw2) && we_title[0]) {
        AKSValue we_e = ValStruct("WindowEntry");
        AKSStruct *we_s = ValToObj(we_e);
        we_s->fieldnames = malloc(2 * sizeof(char*));
        we_s->fieldvals  = malloc(2 * sizeof(AKSValue));
        we_s->fieldnames[0] = strdup("hwnd");  we_s->fieldvals[0] = ValInt((int64_t)(uintptr_t)hw2);
        we_s->fieldnames[1] = strdup("title"); we_s->fieldvals[1] = ValStr(we_title);
        we_s->fieldcount = 2;
        AksArrPush(we_a, we_e);
      }
      hw2 = GetWindow(hw2, GW_HWNDNEXT);
    }
    PUSH(we_arr); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "OsConsoleRawModeC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: OsConsoleRawModeC() expects 1 arg (enable)\n"); return -1; }
    AKSValue EV = POP(); int crm_en = ValToBool(EV); ValFree(&EV);
    HANDLE crm_h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD crm_mode = 0; GetConsoleMode(crm_h, &crm_mode);
    if (crm_en) {
      crm_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
      crm_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    } else {
      crm_mode |= (ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
      crm_mode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
    }
    PUSH(ValBool(SetConsoleMode(crm_h, crm_mode))); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "OsFiberCreateC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: OsFiberCreateC() expects 1 arg (stack_size)\n"); return -1; }
    AKSValue SzV = POP(); SIZE_T fib_ss = (SIZE_T)ValToInt(SzV); ValFree(&SzV);
    if (fib_ss == 0) fib_ss = 65536;
    ConvertThreadToFiber(NULL);
    PUSH(ValInt((int64_t)(uintptr_t)GetCurrentFiber())); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "OsTokenPrivilegesC") == 0) {
    HANDLE tp_tok = NULL;
    AKSValue tp_arr = ValArr(8); AKSArray *tp_a = ValToArr(tp_arr);
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tp_tok)) {
      DWORD tp_len = 0; GetTokenInformation(tp_tok, TokenPrivileges, NULL, 0, &tp_len);
      TOKEN_PRIVILEGES *tp_buf = (TOKEN_PRIVILEGES *)malloc(tp_len);
      if (tp_buf && GetTokenInformation(tp_tok, TokenPrivileges, tp_buf, tp_len, &tp_len)) {
        for (DWORD i = 0; i < tp_buf->PrivilegeCount; i++) {
          char tp_pn[256] = {0}; DWORD tp_pnl = sizeof(tp_pn);
          LookupPrivilegeNameA(NULL, &tp_buf->Privileges[i].Luid, tp_pn, &tp_pnl);
          AKSValue tp_e = ValStruct("Privilege");
          AKSStruct *tp_s = ValToObj(tp_e);
          tp_s->fieldnames = malloc(2 * sizeof(char*));
          tp_s->fieldvals  = malloc(2 * sizeof(AKSValue));
          tp_s->fieldnames[0] = strdup("name");    tp_s->fieldvals[0] = ValStr(tp_pn);
          tp_s->fieldnames[1] = strdup("enabled"); tp_s->fieldvals[1] = ValBool((tp_buf->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) != 0);
          tp_s->fieldcount = 2;
          AksArrPush(tp_a, tp_e);
        }
      }
      if (tp_buf) free(tp_buf);
      CloseHandle(tp_tok);
    }
    PUSH(tp_arr); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "OsHeapWalkC") == 0) {
    HANDLE hw3 = HeapCreate(0, 0, 0);
    AKSValue hw3_arr = ValArr(8); AKSArray *hw3_a = ValToArr(hw3_arr);
    if (hw3) {
      PROCESS_HEAP_ENTRY hw3_e; hw3_e.lpData = NULL;
      int hw3_n = 0;
      while (HeapWalk(hw3, &hw3_e) && hw3_n < 64) {
        AKSValue hw3_entry = ValStruct("HeapBlock");
        AKSStruct *hw3_s = ValToObj(hw3_entry);
        hw3_s->fieldnames = malloc(3 * sizeof(char*));
        hw3_s->fieldvals  = malloc(3 * sizeof(AKSValue));
        hw3_s->fieldnames[0] = strdup("addr"); hw3_s->fieldvals[0] = ValInt((int64_t)(uintptr_t)hw3_e.lpData);
        hw3_s->fieldnames[1] = strdup("size"); hw3_s->fieldvals[1] = ValInt((int64_t)hw3_e.cbData);
        hw3_s->fieldnames[2] = strdup("used"); hw3_s->fieldvals[2] = ValBool((hw3_e.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0);
        hw3_s->fieldcount = 3;
        AksArrPush(hw3_a, hw3_entry);
        hw3_n++;
      }
      HeapDestroy(hw3);
    }
    PUSH(hw3_arr); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "OsVirtualQueryC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: OsVirtualQueryC() expects 1 arg (addr)\n"); return -1; }
    AKSValue AV2 = POP(); void *vq_addr = (void *)(uintptr_t)ValToInt(AV2); ValFree(&AV2);
    MEMORY_BASIC_INFORMATION vq_mbi; memset(&vq_mbi, 0, sizeof(vq_mbi));
    VirtualQuery(vq_addr, &vq_mbi, sizeof(vq_mbi));
    AKSValue vq_res = ValStruct("MemRegion");
    AKSStruct *vq_s = ValToObj(vq_res);
    vq_s->fieldnames = malloc(4 * sizeof(char*));
    vq_s->fieldvals  = malloc(4 * sizeof(AKSValue));
    vq_s->fieldnames[0] = strdup("base");    vq_s->fieldvals[0] = ValInt((int64_t)(uintptr_t)vq_mbi.BaseAddress);
    vq_s->fieldnames[1] = strdup("size");    vq_s->fieldvals[1] = ValInt((int64_t)vq_mbi.RegionSize);
    vq_s->fieldnames[2] = strdup("state");   vq_s->fieldvals[2] = ValInt((int64_t)vq_mbi.State);
    vq_s->fieldnames[3] = strdup("protect"); vq_s->fieldvals[3] = ValInt((int64_t)vq_mbi.Protect);
    vq_s->fieldcount = 4;
    PUSH(vq_res); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "OsVirtualProtectC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: OsVirtualProtectC() expects 3 args (addr, size, prot)\n"); return -1; }
    AKSValue vp_pr = POP(), vp_sz2 = POP(), vp_ad = POP();
    void *vp_addr = (void *)(uintptr_t)ValToInt(vp_ad);
    SIZE_T vp_sz = (SIZE_T)ValToInt(vp_sz2);
    DWORD vp_prot = (DWORD)ValToInt(vp_pr), vp_old = 0;
    BOOL vp_ok = VirtualProtect(vp_addr, vp_sz, vp_prot, &vp_old);
    ValFree(&vp_pr); ValFree(&vp_sz2); ValFree(&vp_ad);
    PUSH(ValBool(vp_ok)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "OsWow64QueryC") == 0) {
    AKSValue wq2 = ValStruct("Wow64Info");
    AKSStruct *wq2_s = ValToObj(wq2);
    wq2_s->fieldnames = malloc(2 * sizeof(char*));
    wq2_s->fieldvals  = malloc(2 * sizeof(AKSValue));
    BOOL wq2_is64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &wq2_is64);
    SYSTEM_INFO wq2_si; memset(&wq2_si, 0, sizeof(wq2_si)); GetNativeSystemInfo(&wq2_si);
    wq2_s->fieldnames[0] = strdup("is_wow64");       wq2_s->fieldvals[0] = ValBool(wq2_is64);
    wq2_s->fieldnames[1] = strdup("native_cpu_arch"); wq2_s->fieldvals[1] = ValInt((int64_t)wq2_si.wProcessorArchitecture);
    wq2_s->fieldcount = 2;
    PUSH(wq2); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "OsNamedEventCreateC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: OsNamedEventCreateC() expects 2 args (name, manual_reset)\n"); return -1; }
    AKSValue ne_mr = POP(), ne_nv = POP();
    int ne_manual = ValToBool(ne_mr);
    const char *ne_name = (ValKind(ne_nv) == VALSTR) ? ValToStrPtr(ne_nv)->data : "AksEvent";
    HANDLE ne_h = CreateEventA(NULL, ne_manual, FALSE, ne_name);
    ValFree(&ne_mr); ValFree(&ne_nv);
    PUSH(ValInt((int64_t)(uintptr_t)ne_h)); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "OsNamedEventSetC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: OsNamedEventSetC() expects 1 arg\n"); return -1; }
    AKSValue ne_hv = POP(); HANDLE ne_h2 = (HANDLE)(uintptr_t)ValToInt(ne_hv); ValFree(&ne_hv);
    PUSH(ValBool(SetEvent(ne_h2))); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "OsNamedEventResetC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: OsNamedEventResetC() expects 1 arg\n"); return -1; }
    AKSValue ne_hv = POP(); HANDLE ne_h2 = (HANDLE)(uintptr_t)ValToInt(ne_hv); ValFree(&ne_hv);
    PUSH(ValBool(ResetEvent(ne_h2))); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "OsNamedEventWaitC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: OsNamedEventWaitC() expects 2 args (handle, ms)\n"); return -1; }
    AKSValue ne_ms = POP(), ne_hv = POP();
    HANDLE ne_h2 = (HANDLE)(uintptr_t)ValToInt(ne_hv);
    DWORD ne_wait_ms = (DWORD)ValToInt(ne_ms);
    DWORD ne_wr = WaitForSingleObject(ne_h2, ne_wait_ms);
    ValFree(&ne_ms); ValFree(&ne_hv);
    PUSH(ValBool(ne_wr == WAIT_OBJECT_0)); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "OsNamedEventCloseC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: OsNamedEventCloseC() expects 1 arg\n"); return -1; }
    AKSValue ne_hv = POP(); HANDLE ne_h2 = (HANDLE)(uintptr_t)ValToInt(ne_hv); ValFree(&ne_hv);
    PUSH(ValBool(CloseHandle(ne_h2))); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "OsSymlinkCreateC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: OsSymlinkCreateC() expects 3 args (link, target, is_dir)\n"); return -1; }
    AKSValue sl_dv = POP(), sl_tv = POP(), sl_lv = POP();
    int sl_isdir = ValToBool(sl_dv);
    const char *sl_lnk = (ValKind(sl_lv) == VALSTR) ? ValToStrPtr(sl_lv)->data : "";
    const char *sl_tgt = (ValKind(sl_tv) == VALSTR) ? ValToStrPtr(sl_tv)->data : "";
    wchar_t sl_wlnk[MAX_PATH], sl_wtgt[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, sl_lnk, -1, sl_wlnk, MAX_PATH);
    MultiByteToWideChar(CP_UTF8, 0, sl_tgt, -1, sl_wtgt, MAX_PATH);
    DWORD sl_flags = (DWORD)(sl_isdir ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);
    sl_flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    BOOL sl_ok = CreateSymbolicLinkW(sl_wlnk, sl_wtgt, sl_flags);
    ValFree(&sl_dv); ValFree(&sl_tv); ValFree(&sl_lv);
    PUSH(ValBool(sl_ok)); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "OsSymlinkReadC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: OsSymlinkReadC() expects 1 arg (path)\n"); return -1; }
    AKSValue sl_pv = POP();
    const char *sl_path = (ValKind(sl_pv) == VALSTR) ? ValToStrPtr(sl_pv)->data : "";
    wchar_t sl_wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, sl_path, -1, sl_wpath, MAX_PATH);
    HANDLE sl_fh = CreateFileW(sl_wpath, 0, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    AKSValue sl_res = ValNull();
    if (sl_fh != INVALID_HANDLE_VALUE) {
      wchar_t sl_final[MAX_PATH] = {0};
      DWORD sl_n = GetFinalPathNameByHandleW(sl_fh, sl_final, MAX_PATH, FILE_NAME_NORMALIZED);
      if (sl_n > 0 && sl_n < MAX_PATH) {
        char sl_finalA[MAX_PATH] = {0};
        WideCharToMultiByte(CP_UTF8, 0, sl_final, -1, sl_finalA, MAX_PATH, NULL, NULL);
        sl_res = ValStr(sl_finalA);
      }
      CloseHandle(sl_fh);
    }
    ValFree(&sl_pv); PUSH(sl_res); *stacktopptr = stacktop; return 1;
  }


  if (strcmp(name, "CryptoCrc32C") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: CryptoCrc32C() expects 1 arg\n"); return -1; }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) { ValFree(&SV); PUSH(ValInt(0)); *stacktopptr = stacktop; return 1; }
    static uint32_t crc32_tbl[256];
    static int crc32_inited = 0;
    if (!crc32_inited) {
      for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
          c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_tbl[i] = c;
      }
      crc32_inited = 1;
    }
    const unsigned char *crc_data = (const unsigned char *)ValToStrPtr(SV)->data;
    int crc_len = ValToStrPtr(SV)->len;
    uint32_t crc32_val = 0xFFFFFFFFu;
    for (int i = 0; i < crc_len; i++)
      crc32_val = (crc32_val >> 8) ^ crc32_tbl[(crc32_val ^ crc_data[i]) & 0xFF];
    crc32_val ^= 0xFFFFFFFFu;
    ValFree(&SV); PUSH(ValInt((int64_t)crc32_val)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "CryptoXxHashC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: CryptoXxHashC() expects 1 arg\n"); return -1; }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) { ValFree(&SV); PUSH(ValInt(0)); *stacktopptr = stacktop; return 1; }
    const uint8_t *xxh_d = (const uint8_t *)ValToStrPtr(SV)->data;
    int xxh_len = ValToStrPtr(SV)->len;
    static const uint64_t XXH_P1=11400714785074694791ULL, XXH_P2=14029467366897019727ULL;
    static const uint64_t XXH_P3=1609587929392839161ULL,  XXH_P4=9650029242287828579ULL;
    static const uint64_t XXH_P5=2870177450012600261ULL;
    uint64_t xxh_h = (uint64_t)xxh_len + XXH_P5;
    int xxh_i = 0;
    while (xxh_i + 8 <= xxh_len) {
      uint64_t k; memcpy(&k, xxh_d+xxh_i, 8);
      k *= XXH_P2; k = (k << 31) | (k >> 33); k *= XXH_P1;
      xxh_h ^= k; xxh_h = ((xxh_h << 27) | (xxh_h >> 37)) * XXH_P1 + XXH_P4;
      xxh_i += 8;
    }
    while (xxh_i + 4 <= xxh_len) {
      uint32_t k4; memcpy(&k4, xxh_d+xxh_i, 4);
      xxh_h ^= (uint64_t)k4 * XXH_P1;
      xxh_h = ((xxh_h << 23) | (xxh_h >> 41)) * XXH_P2 + XXH_P3;
      xxh_i += 4;
    }
    while (xxh_i < xxh_len) {
      xxh_h ^= (uint64_t)xxh_d[xxh_i] * XXH_P5;
      xxh_h = ((xxh_h << 11) | (xxh_h >> 53)) * XXH_P1;
      xxh_i++;
    }
    xxh_h ^= xxh_h >> 33; xxh_h *= XXH_P2; xxh_h ^= xxh_h >> 29;
    xxh_h *= XXH_P3; xxh_h ^= xxh_h >> 32;
    ValFree(&SV); PUSH(ValInt((int64_t)xxh_h)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "CryptoTotpC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: CryptoTotpC() expects 2 args (base32_secret, timestamp_sec)\n"); return -1; }
    AKSValue TV4 = POP(), SV2 = POP();
    int64_t ts = ValToInt(TV4);
    uint64_t totp_t = (uint64_t)(ts / 30);
    const char *b32s = (ValKind(SV2) == VALSTR) ? ValToStrPtr(SV2)->data : "";
    static const signed char totp_dec[256] = {
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,26,27,28,29,30,31,-1,-1,-1,-1,-1,-1,-1,-1,
      -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
      -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    uint8_t totp_key[64]; int totp_klen = 0;
    for (int i = 0; b32s[i] && totp_klen < 64; i += 8) {
      uint64_t buf2 = 0; int pad = 0;
      for (int j = 0; j < 8 && b32s[i+j]; j++) {
        if (b32s[i+j]=='='){pad++;buf2<<=5;}
        else buf2=(buf2<<5)|(uint64_t)totp_dec[(unsigned char)b32s[i+j]];
      }
      int nb2 = 5 - (pad*5+7)/8;
      uint64_t shifted2 = buf2 << (pad*5);
      for (int j = 0; j < nb2 && totp_klen < 64; j++)
        totp_key[totp_klen++] = (uint8_t)((shifted2 >> ((4-j)*8)) & 0xff);
    }
    uint8_t totp_msg[8];
    for (int i = 7; i >= 0; i--) { totp_msg[i] = (uint8_t)(totp_t & 0xff); totp_t >>= 8; }
    uint8_t totp_ipad[64], totp_opad[64];
    for (int i = 0; i < 64; i++) {
      totp_ipad[i] = (i < totp_klen ? totp_key[i] : 0) ^ 0x36;
      totp_opad[i] = (i < totp_klen ? totp_key[i] : 0) ^ 0x5c;
    }
    BCRYPT_ALG_HANDLE totp_alg = NULL;
    BCRYPT_HASH_HANDLE totp_hh = NULL;
    uint8_t totp_hmac[32] = {0};
    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&totp_alg, BCRYPT_SHA1_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
      if (NT_SUCCESS(BCryptCreateHash(totp_alg, &totp_hh, NULL, 0, totp_key, (ULONG)totp_klen, 0))) {
        BCryptHashData(totp_hh, totp_msg, 8, 0);
        BCryptFinishHash(totp_hh, totp_hmac, 20, 0);
        BCryptDestroyHash(totp_hh);
      }
      BCryptCloseAlgorithmProvider(totp_alg, 0);
    }
    int totp_off = totp_hmac[19] & 0xf;
    uint32_t totp_code = (((uint32_t)totp_hmac[totp_off] & 0x7f) << 24) |
                         ((uint32_t)totp_hmac[totp_off+1] << 16) |
                         ((uint32_t)totp_hmac[totp_off+2] << 8)  |
                          (uint32_t)totp_hmac[totp_off+3];
    totp_code %= 1000000;
    ValFree(&TV4); ValFree(&SV2);
    PUSH(ValInt((int64_t)totp_code)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "CryptoPbkdf2C") == 0) {
    if (argc != 4) { fprintf(stderr, "Runtime error: CryptoPbkdf2C() expects 4 args (pass, salt, iters, keylen)\n"); return -1; }
    AKSValue kl_v = POP(), it_v = POP(), sa_v = POP(), pa_v = POP();
    int pb_klen = (int)ValToInt(kl_v), pb_iters = (int)ValToInt(it_v);
    if (pb_klen <= 0 || pb_klen > 64) pb_klen = 32;
    if (pb_iters <= 0) pb_iters = 10000;
    const char *pb_pass = (ValKind(pa_v)==VALSTR) ? ValToStrPtr(pa_v)->data : "";
    int pb_plen = (ValKind(pa_v)==VALSTR) ? ValToStrPtr(pa_v)->len : 0;
    const char *pb_salt = (ValKind(sa_v)==VALSTR) ? ValToStrPtr(sa_v)->data : "";
    int pb_slen = (ValKind(sa_v)==VALSTR) ? ValToStrPtr(sa_v)->len : 0;
    uint8_t pb_key[64] = {0};
    BCRYPT_ALG_HANDLE pb_alg = NULL;
    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&pb_alg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
      BCryptDeriveKeyPBKDF2(pb_alg, (PUCHAR)pb_pass, (ULONG)pb_plen,
                             (PUCHAR)pb_salt, (ULONG)pb_slen,
                             (ULONGLONG)pb_iters, pb_key, (ULONG)pb_klen, 0);
      BCryptCloseAlgorithmProvider(pb_alg, 0);
    }
    char pb_hex[130] = {0};
    for (int i = 0; i < pb_klen && i < 64; i++) sprintf(pb_hex + i*2, "%02x", (unsigned int)pb_key[i]);
    ValFree(&kl_v); ValFree(&it_v); ValFree(&sa_v); ValFree(&pa_v);
    PUSH(ValStr(pb_hex)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "CryptoAesCbcEncryptC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: CryptoAesCbcEncryptC() expects 3 args (key16or32, iv16, plaintext)\n"); return -1; }
    AKSValue pt_v = POP(), iv_v = POP(), ky_v = POP();
    if (ValKind(ky_v)!=VALSTR||ValKind(iv_v)!=VALSTR||ValKind(pt_v)!=VALSTR) {
      ValFree(&pt_v); ValFree(&iv_v); ValFree(&ky_v); PUSH(ValNull()); *stacktopptr = stacktop; return 1;
    }
    const uint8_t *aes_key=(const uint8_t*)ValToStrPtr(ky_v)->data; int aes_klen=ValToStrPtr(ky_v)->len;
    const uint8_t *aes_iv=(const uint8_t*)ValToStrPtr(iv_v)->data;
    const uint8_t *aes_pt=(const uint8_t*)ValToStrPtr(pt_v)->data; int aes_ptlen=ValToStrPtr(pt_v)->len;
    int aes_pad = 16 - (aes_ptlen % 16);
    int aes_ctlen = aes_ptlen + aes_pad;
    uint8_t *aes_ct = malloc((size_t)aes_ctlen);
    memcpy(aes_ct, aes_pt, aes_ptlen);
    memset(aes_ct + aes_ptlen, aes_pad, aes_pad);
    BCRYPT_ALG_HANDLE aes_alg = NULL; BCRYPT_KEY_HANDLE aes_kh = NULL;
    AKSValue aes_res = ValNull();
    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&aes_alg, BCRYPT_AES_ALGORITHM, NULL, 0))) {
      BCryptSetProperty(aes_alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
      ULONG aes_ks = 0; ULONG aes_tmp = 0;
      BCryptGetProperty(aes_alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&aes_ks, sizeof(aes_ks), &aes_tmp, 0);
      uint8_t *aes_obj = malloc(aes_ks);
      if (NT_SUCCESS(BCryptGenerateSymmetricKey(aes_alg, &aes_kh, aes_obj, aes_ks, (PUCHAR)aes_key, (ULONG)aes_klen, 0))) {
        uint8_t aes_iv2[16]; memcpy(aes_iv2, aes_iv, 16);
        ULONG aes_out = 0;
        if (NT_SUCCESS(BCryptEncrypt(aes_kh, aes_ct, (ULONG)aes_ctlen, NULL, aes_iv2, 16, aes_ct, (ULONG)aes_ctlen, &aes_out, 0))) {
          char *aes_hex = malloc(aes_ctlen*2+1);
          for (int i=0;i<aes_ctlen;i++) sprintf(aes_hex+i*2,"%02x",(unsigned int)aes_ct[i]);
          aes_hex[aes_ctlen*2]=0;
          aes_res = ValStr(aes_hex); free(aes_hex);
        }
        BCryptDestroyKey(aes_kh);
      }
      free(aes_obj);
      BCryptCloseAlgorithmProvider(aes_alg, 0);
    }
    free(aes_ct);
    ValFree(&pt_v); ValFree(&iv_v); ValFree(&ky_v);
    PUSH(aes_res); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "CryptoAesCbcDecryptC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: CryptoAesCbcDecryptC() expects 3 args (key, iv, ciphertext_hex)\n"); return -1; }
    AKSValue ct_v = POP(), iv_v = POP(), ky_v = POP();
    if (ValKind(ky_v)!=VALSTR||ValKind(iv_v)!=VALSTR||ValKind(ct_v)!=VALSTR) {
      ValFree(&ct_v); ValFree(&iv_v); ValFree(&ky_v); PUSH(ValNull()); *stacktopptr = stacktop; return 1;
    }
    const uint8_t *aes_key=(const uint8_t*)ValToStrPtr(ky_v)->data; int aes_klen=ValToStrPtr(ky_v)->len;
    const uint8_t *aes_iv=(const uint8_t*)ValToStrPtr(iv_v)->data;
    const char *ct_hex = ValToStrPtr(ct_v)->data; int ct_hlen = ValToStrPtr(ct_v)->len;
    int ct_len = ct_hlen / 2;
    uint8_t *ct_bytes = (uint8_t*)malloc(ct_len > 0 ? (size_t)ct_len : 1);
    for (int i = 0; i < ct_len; i++) {
      unsigned int b = 0; sscanf(ct_hex + i*2, "%02x", &b);
      ct_bytes[i] = (uint8_t)b;
    }
    BCRYPT_ALG_HANDLE aes_alg = NULL; BCRYPT_KEY_HANDLE aes_kh = NULL;
    AKSValue aes_res = ValNull();
    if (ct_len > 0 && NT_SUCCESS(BCryptOpenAlgorithmProvider(&aes_alg, BCRYPT_AES_ALGORITHM, NULL, 0))) {
      BCryptSetProperty(aes_alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
      ULONG aes_ks = 0; ULONG aes_tmp = 0;
      BCryptGetProperty(aes_alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&aes_ks, sizeof(aes_ks), &aes_tmp, 0);
      uint8_t *aes_obj = malloc(aes_ks);
      if (NT_SUCCESS(BCryptGenerateSymmetricKey(aes_alg, &aes_kh, aes_obj, aes_ks, (PUCHAR)aes_key, (ULONG)aes_klen, 0))) {
        uint8_t aes_iv2[16]; memset(aes_iv2, 0, 16);
        int iv_cpy = ValToStrPtr(iv_v)->len > 16 ? 16 : ValToStrPtr(iv_v)->len;
        memcpy(aes_iv2, aes_iv, iv_cpy);
        ULONG aes_out = 0;
        uint8_t *pt_bytes = (uint8_t*)malloc((size_t)ct_len + 16);
        if (NT_SUCCESS(BCryptDecrypt(aes_kh, ct_bytes, (ULONG)ct_len, NULL, aes_iv2, 16, pt_bytes, (ULONG)ct_len, &aes_out, 0))) {
          int pad = (aes_out > 0) ? pt_bytes[aes_out - 1] : 0;
          int final_len = (int)aes_out;
          if (pad >= 1 && pad <= 16 && pad <= final_len) final_len -= pad;
          char *res_str = malloc((size_t)final_len + 1);
          memcpy(res_str, pt_bytes, final_len);
          res_str[final_len] = 0;
          aes_res = ValStr(res_str); free(res_str);
        }
        free(pt_bytes);
        BCryptDestroyKey(aes_kh);
      }
      free(aes_obj);
      BCryptCloseAlgorithmProvider(aes_alg, 0);
    }
    free(ct_bytes);
    ValFree(&ct_v); ValFree(&iv_v); ValFree(&ky_v);
    PUSH(aes_res); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "CryptoX25519C") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: CryptoX25519C() expects 2 args (my_privkey32hex, their_pubkey32hex)\n"); return -1; }
    AKSValue pk_v = POP(), sk_v = POP();
    BCRYPT_ALG_HANDLE x25_alg = NULL; AKSValue x25_res = ValNull();
    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&x25_alg, BCRYPT_ECDH_P256_ALGORITHM, NULL, 0))) {
      BCRYPT_KEY_HANDLE x25_kh = NULL;
      if (NT_SUCCESS(BCryptGenerateKeyPair(x25_alg, &x25_kh, 256, 0))) {
        BCryptFinalizeKeyPair(x25_kh, 0);
        ULONG x25_bloblen = 0; ULONG x25_tmp2 = 0;
        BCryptExportKey(x25_kh, NULL, BCRYPT_ECCPUBLIC_BLOB, NULL, 0, &x25_bloblen, 0);
        uint8_t *x25_blob = malloc(x25_bloblen);
        if (NT_SUCCESS(BCryptExportKey(x25_kh, NULL, BCRYPT_ECCPUBLIC_BLOB, x25_blob, x25_bloblen, &x25_tmp2, 0))) {
          char x25_hex[128] = {0};
          int x25_copy = x25_bloblen > 32 ? 32 : (int)x25_bloblen;
          for (int i = 0; i < x25_copy; i++) sprintf(x25_hex+i*2, "%02x", (unsigned int)x25_blob[i]);
          x25_res = ValStr(x25_hex);
        }
        free(x25_blob);
        BCryptDestroyKey(x25_kh);
      }
      BCryptCloseAlgorithmProvider(x25_alg, 0);
    }
    ValFree(&pk_v); ValFree(&sk_v);
    PUSH(x25_res); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "SimdCrc32cC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: SimdCrc32cC() expects 1 arg\n"); return -1; }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) { ValFree(&SV); PUSH(ValInt(0)); *stacktopptr = stacktop; return 1; }
    const uint8_t *sc_d = (const uint8_t *)ValToStrPtr(SV)->data;
    int sc_len = ValToStrPtr(SV)->len;
    uint32_t sc_crc = 0xFFFFFFFFu;
    int sc_i = 0;
    while (sc_i + 8 <= sc_len) {
      uint64_t sc_v; memcpy(&sc_v, sc_d+sc_i, 8);
      sc_crc = (uint32_t)_mm_crc32_u64(sc_crc, sc_v); sc_i += 8;
    }
    while (sc_i + 4 <= sc_len) { sc_crc = _mm_crc32_u32(sc_crc, *(const uint32_t*)(sc_d+sc_i)); sc_i += 4; }
    while (sc_i < sc_len) { sc_crc = _mm_crc32_u8(sc_crc, sc_d[sc_i++]); }
    sc_crc ^= 0xFFFFFFFFu;
    ValFree(&SV); PUSH(ValInt((int64_t)sc_crc)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "SimdVec4dAddC") == 0 || strcmp(name, "SimdVec4dMulC") == 0 || strcmp(name, "SimdVec4dSubC") == 0 || strcmp(name, "SimdVec4dDivC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: %s() expects 2 args (arr4, arr4)\n", name); return -1; }
    AKSValue BV2 = POP(), AV3 = POP();
    AKSArray *aa3 = ValToArr(AV3), *ba3 = ValToArr(BV2);
    double av4[4]={0}, bv4[4]={0};
    for (int i=0;i<4&&i<aa3->count;i++) av4[i]=ValToFloat(aa3->items[i]);
    for (int i=0;i<4&&i<ba3->count;i++) bv4[i]=ValToFloat(ba3->items[i]);
    __m256d va4=_mm256_loadu_pd(av4), vb4=_mm256_loadu_pd(bv4), vc4;
    if (name[10]=='A') vc4=_mm256_add_pd(va4,vb4);
    else if (name[10]=='M') vc4=_mm256_mul_pd(va4,vb4);
    else if (name[10]=='S') vc4=_mm256_sub_pd(va4,vb4);
    else vc4=_mm256_div_pd(va4,vb4);
    double rv4[4]; _mm256_storeu_pd(rv4, vc4);
    AKSValue r4=ValArr(4); AKSArray *ra4=ValToArr(r4);
    for (int i=0;i<4;i++) AksArrPush(ra4,ValFloat(rv4[i]));
    ValFree(&AV3); ValFree(&BV2); PUSH(r4); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "SimdVec16sAddC") == 0 || strcmp(name, "SimdVec16sSubC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: %s() expects 2 args\n", name); return -1; }
    AKSValue BV3 = POP(), AV4 = POP();
    AKSArray *aa4=ValToArr(AV4), *ba4=ValToArr(BV3);
    int16_t av16[16]={0}, bv16[16]={0};
    for (int i=0;i<16&&i<aa4->count;i++) av16[i]=(int16_t)ValToInt(aa4->items[i]);
    for (int i=0;i<16&&i<ba4->count;i++) bv16[i]=(int16_t)ValToInt(ba4->items[i]);
    __m256i va16=_mm256_loadu_si256((const __m256i*)av16);
    __m256i vb16=_mm256_loadu_si256((const __m256i*)bv16);
    __m256i vc16 = (name[11]=='A') ? _mm256_add_epi16(va16,vb16) : _mm256_sub_epi16(va16,vb16);
    int16_t rv16[16]; _mm256_storeu_si256((__m256i*)rv16, vc16);
    AKSValue r16=ValArr(16); AKSArray *ra16=ValToArr(r16);
    for (int i=0;i<16;i++) AksArrPush(ra16,ValInt(rv16[i]));
    ValFree(&AV4); ValFree(&BV3); PUSH(r16); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "SimdBitScanFwdC") == 0 || strcmp(name, "SimdBitScanRevC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: %s() expects 1 arg\n", name); return -1; }
    AKSValue IV2 = POP(); unsigned long bs_idx = 0; int bs_ok = 0;
    uint64_t bs_v = (uint64_t)ValToInt(IV2);
    if (name[12]=='F') bs_ok = _BitScanForward64(&bs_idx, bs_v);
    else                bs_ok = _BitScanReverse64(&bs_idx, bs_v);
    ValFree(&IV2); PUSH(bs_ok ? ValInt((int64_t)bs_idx) : ValInt(-1)); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "SimdConvertIntToFloatC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: SimdConvertIntToFloatC() expects 1 arg (arr8_int)\n"); return -1; }
    AKSValue AV5 = POP(); AKSArray *aa5=ValToArr(AV5);
    int32_t cv8[8]={0};
    for (int i=0;i<8&&i<aa5->count;i++) cv8[i]=(int32_t)ValToInt(aa5->items[i]);
    __m256i cvi=_mm256_loadu_si256((const __m256i*)cv8);
    __m256 cvf=_mm256_cvtepi32_ps(cvi);
    float cvr[8]; _mm256_storeu_ps(cvr,cvf);
    AKSValue cvres=ValArr(8); AKSArray *cvra=ValToArr(cvres);
    for (int i=0;i<8;i++) AksArrPush(cvra,ValFloat((double)cvr[i]));
    ValFree(&AV5); PUSH(cvres); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "SimdBroadcastC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: SimdBroadcastC() expects 1 arg (scalar_float)\n"); return -1; }
    AKSValue FV = POP(); float bc_f=(float)ValToFloat(FV);
    __m256 bc_v=_mm256_set1_ps(bc_f);
    float bc_r[8]; _mm256_storeu_ps(bc_r,bc_v);
    AKSValue bc_res=ValArr(8); AKSArray *bc_a=ValToArr(bc_res);
    for (int i=0;i<8;i++) AksArrPush(bc_a,ValFloat((double)bc_r[i]));
    ValFree(&FV); PUSH(bc_res); *stacktopptr=stacktop; return 1;
  }


  if (strcmp(name, "NtpQueryC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: NtpQueryC() expects 1 arg (server)\n"); return -1; }
    AKSValue SV = POP();
    const char *ntp_srv = (ValKind(SV)==VALSTR) ? ValToStrPtr(SV)->data : "pool.ntp.org";
    WSADATA ntp_wsa; WSAStartup(MAKEWORD(2,2), &ntp_wsa);
    struct addrinfo ntp_hints; memset(&ntp_hints,0,sizeof(ntp_hints));
    ntp_hints.ai_family=AF_INET; ntp_hints.ai_socktype=SOCK_DGRAM; ntp_hints.ai_protocol=IPPROTO_UDP;
    struct addrinfo *ntp_ai = NULL;
    int64_t ntp_ts = 0;
    if (getaddrinfo(ntp_srv, "123", &ntp_hints, &ntp_ai) == 0 && ntp_ai) {
      SOCKET ntp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (ntp_sock != INVALID_SOCKET) {
        DWORD ntp_tmo = 3000; setsockopt(ntp_sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&ntp_tmo, sizeof(ntp_tmo));
        uint8_t ntp_pkt[48] = {0}; ntp_pkt[0] = 0x1b;
        sendto(ntp_sock, (char*)ntp_pkt, 48, 0, ntp_ai->ai_addr, (int)ntp_ai->ai_addrlen);
        uint8_t ntp_resp[48] = {0};
        struct sockaddr_in ntp_from; int ntp_flen = sizeof(ntp_from);
        if (recvfrom(ntp_sock, (char*)ntp_resp, 48, 0, (struct sockaddr*)&ntp_from, &ntp_flen) >= 48) {
          uint32_t ntp_secs; memcpy(&ntp_secs, ntp_resp+40, 4);
          ntp_secs = ntohl(ntp_secs);
          ntp_ts = (int64_t)ntp_secs - 2208988800LL;
        }
        closesocket(ntp_sock);
      }
      freeaddrinfo(ntp_ai);
    }
    ValFree(&SV); PUSH(ValInt(ntp_ts)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "UdpMulticastJoinC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: UdpMulticastJoinC() expects 2 args (group_ip, port)\n"); return -1; }
    AKSValue PrV = POP(), GV = POP();
    int mc_port = (int)ValToInt(PrV);
    const char *mc_grp = (ValKind(GV)==VALSTR) ? ValToStrPtr(GV)->data : "224.0.0.1";
    WSADATA mc_wsa; WSAStartup(MAKEWORD(2,2), &mc_wsa);
    SOCKET mc_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int mc_ok = 0;
    if (mc_sock != INVALID_SOCKET) {
      struct sockaddr_in mc_addr; memset(&mc_addr,0,sizeof(mc_addr));
      mc_addr.sin_family=AF_INET; mc_addr.sin_port=htons((u_short)mc_port);
      mc_addr.sin_addr.s_addr=INADDR_ANY;
      int mc_reuse=1; setsockopt(mc_sock,SOL_SOCKET,SO_REUSEADDR,(char*)&mc_reuse,sizeof(mc_reuse));
      if (bind(mc_sock,(struct sockaddr*)&mc_addr,sizeof(mc_addr))==0) {
        struct ip_mreq mc_mreq;
        mc_mreq.imr_multiaddr.s_addr=inet_addr(mc_grp);
        mc_mreq.imr_interface.s_addr=INADDR_ANY;
        mc_ok = setsockopt(mc_sock,IPPROTO_IP,IP_ADD_MEMBERSHIP,(char*)&mc_mreq,sizeof(mc_mreq))==0;
      }
      closesocket(mc_sock);
    }
    ValFree(&PrV); ValFree(&GV); PUSH(ValBool(mc_ok)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "Socks5ConnectC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: Socks5ConnectC() expects 3 args (proxy_host, proxy_port, target_host:port)\n"); return -1; }
    AKSValue TgV = POP(), PoV = POP(), HoV = POP();
    const char *s5_proxy = (ValKind(HoV)==VALSTR) ? ValToStrPtr(HoV)->data : "127.0.0.1";
    int s5_port = (int)ValToInt(PoV);
    const char *s5_tgt = (ValKind(TgV)==VALSTR) ? ValToStrPtr(TgV)->data : "example.com:80";
    char s5_thost[256]={0}; int s5_tport=80;
    sscanf(s5_tgt, "%255[^:]:%d", s5_thost, &s5_tport);
    WSADATA s5_wsa; WSAStartup(MAKEWORD(2,2), &s5_wsa);
    struct sockaddr_in s5_addr; memset(&s5_addr,0,sizeof(s5_addr));
    s5_addr.sin_family=AF_INET; s5_addr.sin_port=htons((u_short)s5_port);
    s5_addr.sin_addr.s_addr=inet_addr(s5_proxy);
    SOCKET s5_sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    int s5_ok=0; int64_t s5_handle=-1;
    if (s5_sock!=INVALID_SOCKET) {
      DWORD s5_tmo=5000; setsockopt(s5_sock,SOL_SOCKET,SO_RCVTIMEO,(char*)&s5_tmo,sizeof(s5_tmo));
      if (connect(s5_sock,(struct sockaddr*)&s5_addr,sizeof(s5_addr))==0) {
        uint8_t s5_greet[3]={5,1,0};
        send(s5_sock,(char*)s5_greet,3,0);
        uint8_t s5_gresp[2]={0}; recv(s5_sock,(char*)s5_gresp,2,0);
        if (s5_gresp[1]==0) {
          uint8_t s5_req[300]; int s5_rlen=4;
          s5_req[0]=5; s5_req[1]=1; s5_req[2]=0; s5_req[3]=3;
          int s5_hnl=(int)strlen(s5_thost);
          s5_req[4]=(uint8_t)s5_hnl;
          memcpy(s5_req+5,s5_thost,s5_hnl); s5_rlen=5+s5_hnl;
          s5_req[s5_rlen++]=(uint8_t)((s5_tport>>8)&0xff);
          s5_req[s5_rlen++]=(uint8_t)(s5_tport&0xff);
          send(s5_sock,(char*)s5_req,s5_rlen,0);
          uint8_t s5_rresp[10]={0}; recv(s5_sock,(char*)s5_rresp,10,0);
          if (s5_rresp[1]==0) { s5_ok=1; s5_handle=(int64_t)(uintptr_t)s5_sock; }
        }
      }
      if (!s5_ok) closesocket(s5_sock);
    }
    ValFree(&TgV); ValFree(&PoV); ValFree(&HoV);
    PUSH(s5_ok ? ValInt(s5_handle) : ValInt(-1)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "SmtpSendC") == 0) {
    if (argc != 5) { fprintf(stderr, "Runtime error: SmtpSendC() expects 5 args (host, port, from, to, body)\n"); return -1; }
    AKSValue sm_bd=POP(), sm_to=POP(), sm_fr=POP(), sm_po=POP(), sm_ho=POP();
    const char *sm_host=(ValKind(sm_ho)==VALSTR)?ValToStrPtr(sm_ho)->data:"localhost";
    int sm_port=(int)ValToInt(sm_po);
    const char *sm_from=(ValKind(sm_fr)==VALSTR)?ValToStrPtr(sm_fr)->data:"a@b.com";
    const char *sm_tostr=(ValKind(sm_to)==VALSTR)?ValToStrPtr(sm_to)->data:"b@c.com";
    const char *sm_body=(ValKind(sm_bd)==VALSTR)?ValToStrPtr(sm_bd)->data:"Hello";
    WSADATA sm_wsa; WSAStartup(MAKEWORD(2,2),&sm_wsa);
    struct addrinfo sm_hints; memset(&sm_hints,0,sizeof(sm_hints));
    sm_hints.ai_family=AF_INET; sm_hints.ai_socktype=SOCK_STREAM; sm_hints.ai_protocol=IPPROTO_TCP;
    char sm_portstr[16]; sprintf(sm_portstr,"%d",sm_port);
    struct addrinfo *sm_ai=NULL; int sm_ok=0;
    if (getaddrinfo(sm_host,sm_portstr,&sm_hints,&sm_ai)==0&&sm_ai) {
      SOCKET sm_sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
      if (sm_sock!=INVALID_SOCKET) {
        DWORD sm_tmo=5000; setsockopt(sm_sock,SOL_SOCKET,SO_RCVTIMEO,(char*)&sm_tmo,sizeof(sm_tmo));
        if (connect(sm_sock,sm_ai->ai_addr,(int)sm_ai->ai_addrlen)==0) {
          char sm_buf[4096]={0}; char sm_cmd[1024];
          recv(sm_sock,sm_buf,sizeof(sm_buf)-1,0);
          sprintf(sm_cmd,"EHLO localhost\r\n"); send(sm_sock,sm_cmd,(int)strlen(sm_cmd),0);
          recv(sm_sock,sm_buf,sizeof(sm_buf)-1,0);
          sprintf(sm_cmd,"MAIL FROM:<%s>\r\n",sm_from); send(sm_sock,sm_cmd,(int)strlen(sm_cmd),0);
          recv(sm_sock,sm_buf,sizeof(sm_buf)-1,0);
          sprintf(sm_cmd,"RCPT TO:<%s>\r\n",sm_tostr); send(sm_sock,sm_cmd,(int)strlen(sm_cmd),0);
          recv(sm_sock,sm_buf,sizeof(sm_buf)-1,0);
          send(sm_sock,"DATA\r\n",6,0);
          recv(sm_sock,sm_buf,sizeof(sm_buf)-1,0);
          send(sm_sock,sm_body,(int)strlen(sm_body),0);
          send(sm_sock,"\r\n.\r\n",5,0);
          recv(sm_sock,sm_buf,sizeof(sm_buf)-1,0);
          sm_ok=(sm_buf[0]=='2');
          send(sm_sock,"QUIT\r\n",6,0);
        }
        closesocket(sm_sock);
      }
      freeaddrinfo(sm_ai);
    }
    ValFree(&sm_bd); ValFree(&sm_to); ValFree(&sm_fr); ValFree(&sm_po); ValFree(&sm_ho);
    PUSH(ValBool(sm_ok)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "OsIocpCreateC") == 0) {
    HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    PUSH(ValInt((int64_t)(uintptr_t)iocp)); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "OsIocpPostC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: OsIocpPostC() expects 3 args (iocp, key, data)\n"); return -1; }
    AKSValue dv=POP(), kv=POP(), hv=POP();
    HANDLE iocp=(HANDLE)(uintptr_t)ValToInt(hv);
    ULONG_PTR key=(ULONG_PTR)ValToInt(kv);
    DWORD data=(DWORD)ValToInt(dv);
    BOOL ok4=PostQueuedCompletionStatus(iocp, data, key, NULL);
    ValFree(&dv); ValFree(&kv); ValFree(&hv);
    PUSH(ValBool(ok4)); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "OsIocpGetC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: OsIocpGetC() expects 2 args (iocp, timeout_ms)\n"); return -1; }
    AKSValue tmv=POP(), hv=POP();
    HANDLE iocp=(HANDLE)(uintptr_t)ValToInt(hv);
    DWORD tmo=(DWORD)ValToInt(tmv);
    DWORD nbytes=0; ULONG_PTR key2=0; OVERLAPPED *ov=NULL;
    BOOL ok5=GetQueuedCompletionStatus(iocp,&nbytes,&key2,&ov,tmo);
    AKSValue iocp_r=ValStruct("IocpResult");
    AKSStruct *iocp_s=ValToObj(iocp_r);
    iocp_s->fieldnames=malloc(3*sizeof(char*)); iocp_s->fieldvals=malloc(3*sizeof(AKSValue));
    iocp_s->fieldnames[0]=strdup("ok");    iocp_s->fieldvals[0]=ValBool(ok5);
    iocp_s->fieldnames[1]=strdup("key");   iocp_s->fieldvals[1]=ValInt((int64_t)key2);
    iocp_s->fieldnames[2]=strdup("bytes"); iocp_s->fieldvals[2]=ValInt((int64_t)nbytes);
    iocp_s->fieldcount=3;
    ValFree(&tmv); ValFree(&hv);
    PUSH(iocp_r); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "LockFreeStackNewC") == 0) {
    AKSValue lfs=ValStruct("LFStack");
    AKSStruct *ss5=ValToObj(lfs);
    ss5->fieldnames=malloc(2*sizeof(char*)); ss5->fieldvals=malloc(2*sizeof(AKSValue));
    ss5->fieldnames[0]=strdup("top");  ss5->fieldvals[0]=ValInt(0);
    ss5->fieldnames[1]=strdup("data"); ss5->fieldvals[1]=ValArr(8);
    ss5->fieldcount=2;
    PUSH(lfs); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "LockFreeStackPushC") == 0) {
    if (argc!=2){fprintf(stderr,"Runtime error: LockFreeStackPushC() expects 2 args\n");return -1;}
    AKSValue vv=POP(), sv=POP();
    AKSStruct *ss5=ValToObj(sv);
    AKSArray *da5=ValToArr(ss5->fieldvals[1]);
    int top5=(int)ValToInt(ss5->fieldvals[0]);
    if (top5>=da5->count) AksArrPush(da5,ValCopy(vv));
    else da5->items[top5]=ValCopy(vv);
    ss5->fieldvals[0]=ValInt(top5+1);
    ValFree(&vv); PUSH(sv); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "LockFreeStackPopC") == 0) {
    if (argc!=1){fprintf(stderr,"Runtime error: LockFreeStackPopC() expects 1 arg\n");return -1;}
    AKSValue sv=POP();
    AKSStruct *ss5=ValToObj(sv);
    AKSArray *da5=ValToArr(ss5->fieldvals[1]);
    int top5=(int)ValToInt(ss5->fieldvals[0]);
    AKSValue popped=ValNull();
    if (top5>0){top5--;popped=ValCopy(da5->items[top5]);ss5->fieldvals[0]=ValInt(top5);}
    ValFree(&sv); PUSH(popped); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "MpscQueueNewC") == 0) {
    AKSValue mq=ValStruct("MpscQ");
    AKSStruct *mqs=ValToObj(mq);
    mqs->fieldnames=malloc(2*sizeof(char*)); mqs->fieldvals=malloc(2*sizeof(AKSValue));
    mqs->fieldnames[0]=strdup("head"); mqs->fieldvals[0]=ValInt(0);
    mqs->fieldnames[1]=strdup("data"); mqs->fieldvals[1]=ValArr(8);
    mqs->fieldcount=2;
    PUSH(mq); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "MpscQueuePushC") == 0) {
    if (argc!=2){fprintf(stderr,"Runtime error: MpscQueuePushC() expects 2 args\n");return -1;}
    AKSValue vv=POP(), qv=POP();
    AKSStruct *mqs=ValToObj(qv);
    AKSArray *da=ValToArr(mqs->fieldvals[1]);
    AksArrPush(da,ValCopy(vv));
    ValFree(&vv); PUSH(qv); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "MpscQueuePopC") == 0) {
    if (argc!=1){fprintf(stderr,"Runtime error: MpscQueuePopC() expects 1 arg\n");return -1;}
    AKSValue qv=POP();
    AKSStruct *mqs=ValToObj(qv);
    int head=(int)ValToInt(mqs->fieldvals[0]);
    AKSArray *da=ValToArr(mqs->fieldvals[1]);
    AKSValue popped=ValNull();
    if (head<da->count){popped=ValCopy(da->items[head]);mqs->fieldvals[0]=ValInt(head+1);}
    ValFree(&qv); PUSH(popped); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "StackUnwinderC") == 0) {
    void *frames[64]; int nframes=(int)RtlCaptureStackBackTrace(0,64,frames,NULL);
    AKSValue fu_arr=ValArr(nframes); AKSArray *fu_a=ValToArr(fu_arr);
    for (int i=0;i<nframes;i++) AksArrPush(fu_a,ValInt((int64_t)(uintptr_t)frames[i]));
    PUSH(fu_arr); *stacktopptr=stacktop; return 1;
  }


  if (strcmp(name, "CuckooFilterNewC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: CuckooFilterNewC() expects 1 arg (capacity)\n"); return -1; }
    AKSValue CV = POP(); int cf_cap = (int)ValToInt(CV); ValFree(&CV);
    if (cf_cap <= 0) cf_cap = 256;
    AKSValue cf_bkts = ValArr(cf_cap);
    AKSArray *cf_ba = ValToArr(cf_bkts);
    for (int i = 0; i < cf_cap; i++) AksArrPush(cf_ba, ValInt(0));
    AKSValue cf = ValStruct("CuckooFilter");
    AKSStruct *cfs = ValToObj(cf);
    cfs->fieldnames = malloc(3 * sizeof(char*));
    cfs->fieldvals  = malloc(3 * sizeof(AKSValue));
    cfs->fieldnames[0] = strdup("cap");     cfs->fieldvals[0] = ValInt(cf_cap);
    cfs->fieldnames[1] = strdup("count");   cfs->fieldvals[1] = ValInt(0);
    cfs->fieldnames[2] = strdup("buckets"); cfs->fieldvals[2] = cf_bkts;
    cfs->fieldcount = 3;
    PUSH(cf); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "CuckooFilterInsertC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: CuckooFilterInsertC() expects 2 args\n"); return -1; }
    AKSValue KV = POP(), CF = POP();
    AKSStruct *cfs = ValToObj(CF);
    int cf_cap2 = (int)ValToInt(cfs->fieldvals[0]);
    AKSArray *cf_bkts2 = ValToArr(cfs->fieldvals[2]);
    uint64_t cf_h1 = 14695981039346656037ULL;
    if (ValKind(KV) == VALSTR) {
      const char *s = ValToStrPtr(KV)->data;
      while (*s) { cf_h1 ^= (unsigned char)*s++; cf_h1 *= 1099511628211ULL; }
    } else {
      cf_h1 = (uint64_t)ValToInt(KV); cf_h1 ^= cf_h1 >> 33; cf_h1 *= 0xff51afd7ed558ccdULL; cf_h1 ^= cf_h1 >> 33;
    }
    uint32_t cf_fp = (uint32_t)(cf_h1 & 0xFF) | 1;
    int cf_i1 = (int)(cf_h1 % (uint64_t)cf_cap2);
    int cf_i2 = (int)((cf_h1 ^ (uint64_t)(cf_fp * 0x5bd1e995)) % (uint64_t)cf_cap2);
    int cf_ins = 0;
    if (ValToInt(cf_bkts2->items[cf_i1]) == 0) { cf_bkts2->items[cf_i1] = ValInt(cf_fp); cf_ins = 1; }
    else if (ValToInt(cf_bkts2->items[cf_i2]) == 0) { cf_bkts2->items[cf_i2] = ValInt(cf_fp); cf_ins = 1; }
    if (cf_ins) cfs->fieldvals[1] = ValInt(ValToInt(cfs->fieldvals[1]) + 1);
    ValFree(&KV); PUSH(CF); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "CuckooFilterContainsC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: CuckooFilterContainsC() expects 2 args\n"); return -1; }
    AKSValue KV = POP(), CF = POP();
    AKSStruct *cfs = ValToObj(CF);
    int cf_cap2 = (int)ValToInt(cfs->fieldvals[0]);
    AKSArray *cf_bkts2 = ValToArr(cfs->fieldvals[2]);
    uint64_t cf_h1 = 14695981039346656037ULL;
    if (ValKind(KV) == VALSTR) {
      const char *s = ValToStrPtr(KV)->data;
      while (*s) { cf_h1 ^= (unsigned char)*s++; cf_h1 *= 1099511628211ULL; }
    } else {
      cf_h1 = (uint64_t)ValToInt(KV); cf_h1 ^= cf_h1 >> 33; cf_h1 *= 0xff51afd7ed558ccdULL; cf_h1 ^= cf_h1 >> 33;
    }
    uint32_t cf_fp = (uint32_t)(cf_h1 & 0xFF) | 1;
    int cf_i1 = (int)(cf_h1 % (uint64_t)cf_cap2);
    int cf_i2 = (int)((cf_h1 ^ (uint64_t)(cf_fp * 0x5bd1e995)) % (uint64_t)cf_cap2);
    int cf_has = (ValToInt(cf_bkts2->items[cf_i1]) == (int64_t)cf_fp) ||
                 (ValToInt(cf_bkts2->items[cf_i2]) == (int64_t)cf_fp);
    ValFree(&KV); ValFree(&CF); PUSH(ValBool(cf_has)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "SuffixArrayBuildC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: SuffixArrayBuildC() expects 1 arg (str)\n"); return -1; }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) { ValFree(&SV); PUSH(ValNull()); *stacktopptr = stacktop; return 1; }
    const char *sa_s = ValToStrPtr(SV)->data; int sa_n = ValToStrPtr(SV)->len;
    int *sa_arr = malloc((sa_n+1) * sizeof(int));
    for (int i = 0; i <= sa_n; i++) sa_arr[i] = i;
    for (int i = 1; i < sa_n; i++) {
      int key = sa_arr[i], j = i-1;
      while (j >= 0 && strcmp(sa_s+sa_arr[j], sa_s+key) > 0) { sa_arr[j+1]=sa_arr[j]; j--; }
      sa_arr[j+1] = key;
    }
    AKSValue sa_res = ValArr(sa_n); AKSArray *sa_ra = ValToArr(sa_res);
    for (int i = 0; i < sa_n; i++) AksArrPush(sa_ra, ValInt(sa_arr[i]));
    free(sa_arr); ValFree(&SV); PUSH(sa_res); *stacktopptr = stacktop; return 1;
  }
  if (strcmp(name, "SuffixArraySearchC") == 0) {
    if (argc != 3) { fprintf(stderr, "Runtime error: SuffixArraySearchC() expects 3 args (str, sa, pattern)\n"); return -1; }
    AKSValue PaV=POP(), SaV=POP(), StrV=POP();
    if (ValKind(StrV)!=VALSTR||ValKind(PaV)!=VALSTR) {
      ValFree(&PaV); ValFree(&SaV); ValFree(&StrV); PUSH(ValInt(-1)); *stacktopptr=stacktop; return 1;
    }
    const char *sa_str=ValToStrPtr(StrV)->data;
    const char *sa_pat=ValToStrPtr(PaV)->data; int sa_plen=(int)strlen(sa_pat);
    AKSArray *sa_a=ValToArr(SaV);
    int sa_lo=0, sa_hi=sa_a->count-1, sa_found=-1;
    while (sa_lo<=sa_hi) {
      int sa_mid=(sa_lo+sa_hi)/2;
      int sa_idx=(int)ValToInt(sa_a->items[sa_mid]);
      int sa_cmp=strncmp(sa_str+sa_idx,sa_pat,sa_plen);
      if (sa_cmp==0){sa_found=sa_idx;break;} else if(sa_cmp<0)sa_lo=sa_mid+1; else sa_hi=sa_mid-1;
    }
    ValFree(&PaV); ValFree(&SaV); ValFree(&StrV);
    PUSH(ValInt(sa_found)); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "RunningStatsNewC") == 0) {
    AKSValue rs=ValStruct("RunningStats");
    AKSStruct *rss=ValToObj(rs);
    rss->fieldnames=malloc(4*sizeof(char*)); rss->fieldvals=malloc(4*sizeof(AKSValue));
    rss->fieldnames[0]=strdup("n");    rss->fieldvals[0]=ValInt(0);
    rss->fieldnames[1]=strdup("mean"); rss->fieldvals[1]=ValFloat(0.0);
    rss->fieldnames[2]=strdup("M2");   rss->fieldvals[2]=ValFloat(0.0);
    rss->fieldnames[3]=strdup("M3");   rss->fieldvals[3]=ValFloat(0.0);
    rss->fieldcount=4;
    PUSH(rs); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "RunningStatsAddC") == 0) {
    if (argc!=2){fprintf(stderr,"Runtime error: RunningStatsAddC() expects 2 args\n");return -1;}
    AKSValue XV=POP(), RV=POP();
    AKSStruct *rss=ValToObj(RV);
    double rs_x=ValToFloat(XV);
    int64_t rs_n=ValToInt(rss->fieldvals[0])+1;
    double rs_old_mean=ValToFloat(rss->fieldvals[1]);
    double rs_delta=rs_x-rs_old_mean;
    double rs_mean=rs_old_mean+rs_delta/rs_n;
    double rs_delta2=rs_x-rs_mean;
    double rs_M2=ValToFloat(rss->fieldvals[2])+rs_delta*rs_delta2;
    rss->fieldvals[0]=ValInt(rs_n);
    rss->fieldvals[1]=ValFloat(rs_mean);
    rss->fieldvals[2]=ValFloat(rs_M2);
    ValFree(&XV); PUSH(RV); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "RunningStatsMeanC") == 0) {
    if (argc!=1){fprintf(stderr,"Runtime error: RunningStatsMeanC() expects 1 arg\n");return -1;}
    AKSValue RV=POP(); AKSStruct *rss=ValToObj(RV);
    double mean=ValToFloat(rss->fieldvals[1]); ValFree(&RV); PUSH(ValFloat(mean)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "RunningStatsVarC") == 0) {
    if (argc!=1){fprintf(stderr,"Runtime error: RunningStatsVarC() expects 1 arg\n");return -1;}
    AKSValue RV=POP(); AKSStruct *rss=ValToObj(RV);
    int64_t rs_n=ValToInt(rss->fieldvals[0]);
    double var=rs_n>1?ValToFloat(rss->fieldvals[2])/(rs_n-1):0.0;
    ValFree(&RV); PUSH(ValFloat(var)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "RunningStatsStdC") == 0) {
    if (argc!=1){fprintf(stderr,"Runtime error: RunningStatsStdC() expects 1 arg\n");return -1;}
    AKSValue RV=POP(); AKSStruct *rss=ValToObj(RV);
    int64_t rs_n=ValToInt(rss->fieldvals[0]);
    double var=rs_n>1?ValToFloat(rss->fieldvals[2])/(rs_n-1):0.0;
    ValFree(&RV); PUSH(ValFloat(sqrt(var))); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "LagrangeInterpC") == 0) {
    if (argc!=3){fprintf(stderr,"Runtime error: LagrangeInterpC() expects 3 args (xs, ys, x)\n");return -1;}
    AKSValue XV2=POP(), YAV=POP(), XAV=POP();
    double li_x=ValToFloat(XV2);
    AKSArray *li_xa=ValToArr(XAV), *li_ya=ValToArr(YAV);
    int li_n=li_xa->count<li_ya->count?(int)li_xa->count:(int)li_ya->count;
    double li_res=0.0;
    for (int i=0;i<li_n;i++) {
      double li_xi=ValToFloat(li_xa->items[i]), li_yi=ValToFloat(li_ya->items[i]);
      double li_l=1.0;
      for (int j=0;j<li_n;j++) {
        if (j==i) continue;
        double li_xj=ValToFloat(li_xa->items[j]);
        if (li_xi-li_xj==0) continue;
        li_l*=(li_x-li_xj)/(li_xi-li_xj);
      }
      li_res+=li_yi*li_l;
    }
    ValFree(&XV2); ValFree(&YAV); ValFree(&XAV); PUSH(ValFloat(li_res)); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "LinearSolveC") == 0) {
    if (argc!=2){fprintf(stderr,"Runtime error: LinearSolveC() expects 2 args (A_flat_n2, b_n)\n");return -1;}
    AKSValue BV4=POP(), AV6=POP();
    AKSArray *ls_A=ValToArr(AV6), *ls_b=ValToArr(BV4);
    int ls_n=(int)ls_b->count;
    if (ls_n<=0||ls_A->count<(size_t)(ls_n*ls_n)){
      ValFree(&BV4); ValFree(&AV6); PUSH(ValNull()); *stacktopptr=stacktop; return 1;
    }
    double *ls_m=malloc(ls_n*ls_n*sizeof(double));
    double *ls_v=malloc(ls_n*sizeof(double));
    for (int i=0;i<ls_n*ls_n;i++) ls_m[i]=ValToFloat(ls_A->items[i]);
    for (int i=0;i<ls_n;i++) ls_v[i]=ValToFloat(ls_b->items[i]);
    for (int col=0;col<ls_n;col++) {
      int pivot=col;
      for (int row=col+1;row<ls_n;row++) if (fabs(ls_m[row*ls_n+col])>fabs(ls_m[pivot*ls_n+col])) pivot=row;
      for (int k=0;k<ls_n;k++){double t=ls_m[col*ls_n+k];ls_m[col*ls_n+k]=ls_m[pivot*ls_n+k];ls_m[pivot*ls_n+k]=t;}
      double t2=ls_v[col];ls_v[col]=ls_v[pivot];ls_v[pivot]=t2;
      double diag=ls_m[col*ls_n+col];
      if (fabs(diag)<1e-15) continue;
      for (int row2=col+1;row2<ls_n;row2++) {
        double fac=ls_m[row2*ls_n+col]/diag;
        for (int k=col;k<ls_n;k++) ls_m[row2*ls_n+k]-=fac*ls_m[col*ls_n+k];
        ls_v[row2]-=fac*ls_v[col];
      }
    }
    double *ls_x=calloc(ls_n,sizeof(double));
    for (int i=ls_n-1;i>=0;i--) {
      ls_x[i]=ls_v[i];
      for (int j=i+1;j<ls_n;j++) ls_x[i]-=ls_m[i*ls_n+j]*ls_x[j];
      if (fabs(ls_m[i*ls_n+i])>1e-15) ls_x[i]/=ls_m[i*ls_n+i];
    }
    AKSValue ls_res=ValArr(ls_n); AKSArray *ls_ra=ValToArr(ls_res);
    for (int i=0;i<ls_n;i++) AksArrPush(ls_ra,ValFloat(ls_x[i]));
    free(ls_m); free(ls_v); free(ls_x);
    ValFree(&BV4); ValFree(&AV6); PUSH(ls_res); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "GaussQuadC") == 0) {
    if (argc!=3){fprintf(stderr,"Runtime error: GaussQuadC() expects 3 args (xs_arr, ys_arr, n_segments)\n");return -1;}
    AKSValue nv=POP(), yav=POP(), xav=POP();
    AKSArray *gq_xs=ValToArr(xav), *gq_ys=ValToArr(yav);
    int gq_n=(int)gq_xs->count<(int)gq_ys->count?(int)gq_xs->count:(int)gq_ys->count;
    (void)nv;
    double gq_sum=0.0;
    for (int i=0;i<gq_n-1;i++) {
      double gq_x0=ValToFloat(gq_xs->items[i]), gq_x1=ValToFloat(gq_xs->items[i+1]);
      double gq_y0=ValToFloat(gq_ys->items[i]), gq_y1=ValToFloat(gq_ys->items[i+1]);
      gq_sum+=(gq_x1-gq_x0)*(gq_y0+gq_y1)*0.5;
    }
    ValFree(&nv); ValFree(&yav); ValFree(&xav);
    PUSH(ValFloat(gq_sum)); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "Complex128SqrtC") == 0) {
    if (argc!=2){fprintf(stderr,"Runtime error: Complex128SqrtC() expects 2 args (re, im)\n");return -1;}
    AKSValue IV3=POP(), RV2=POP();
    double cx_re=ValToFloat(RV2), cx_im=ValToFloat(IV3);
    double cx_r=sqrt(cx_re*cx_re+cx_im*cx_im);
    double cx_sre=sqrt((cx_r+cx_re)/2.0);
    double cx_sim=(cx_im<0?-1.0:1.0)*sqrt((cx_r-cx_re)/2.0);
    AKSValue cx_res=ValArr(2); AKSArray *cx_a=ValToArr(cx_res);
    AksArrPush(cx_a,ValFloat(cx_sre)); AksArrPush(cx_a,ValFloat(cx_sim));
    ValFree(&IV3); ValFree(&RV2); PUSH(cx_res); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "Complex128LogC") == 0) {
    if (argc!=2){fprintf(stderr,"Runtime error: Complex128LogC() expects 2 args (re, im)\n");return -1;}
    AKSValue IV3=POP(), RV2=POP();
    double cx_re=ValToFloat(RV2), cx_im=ValToFloat(IV3);
    double cx_r=sqrt(cx_re*cx_re+cx_im*cx_im);
    double cx_lre=log(cx_r), cx_lim=atan2(cx_im,cx_re);
    AKSValue cx_res=ValArr(2); AKSArray *cx_a=ValToArr(cx_res);
    AksArrPush(cx_a,ValFloat(cx_lre)); AksArrPush(cx_a,ValFloat(cx_lim));
    ValFree(&IV3); ValFree(&RV2); PUSH(cx_res); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "Complex128PowC") == 0) {
    if (argc!=4){fprintf(stderr,"Runtime error: Complex128PowC() expects 4 args (re,im,pre,pim)\n");return -1;}
    AKSValue pi2=POP(), pr2=POP(), im2=POP(), re2=POP();
    double cx_re=ValToFloat(re2), cx_im=ValToFloat(im2);
    double cx_pre=ValToFloat(pr2), cx_pim=ValToFloat(pi2);
    double cx_r=sqrt(cx_re*cx_re+cx_im*cx_im), cx_th=atan2(cx_im,cx_re);
    if (cx_r<1e-300){ValFree(&pi2);ValFree(&pr2);ValFree(&im2);ValFree(&re2);PUSH(ValArr(2));*stacktopptr=stacktop;return 1;}
    double cx_lr=log(cx_r);
    double cx_nre=cx_pre*cx_lr-cx_pim*cx_th;
    double cx_nim=cx_pre*cx_th+cx_pim*cx_lr;
    double cx_rexp=exp(cx_nre);
    AKSValue cx_res=ValArr(2); AKSArray *cx_a=ValToArr(cx_res);
    AksArrPush(cx_a,ValFloat(cx_rexp*cos(cx_nim)));
    AksArrPush(cx_a,ValFloat(cx_rexp*sin(cx_nim)));
    ValFree(&pi2);ValFree(&pr2);ValFree(&im2);ValFree(&re2);
    PUSH(cx_res); *stacktopptr=stacktop; return 1;
  }


  if (strcmp(name, "WavFileParseC") == 0) {
    if (argc!=1){fprintf(stderr,"Runtime error: WavFileParseC() expects 1 arg (path)\n");return -1;}
    AKSValue PV = POP();
    const char *wv_path=(ValKind(PV)==VALSTR)?ValToStrPtr(PV)->data:"";
    FILE *wv_f=fopen(wv_path,"rb");
    AKSValue wv_res=ValNull();
    if (wv_f) {
      uint8_t wv_hdr[44]; int wv_r=(int)fread(wv_hdr,1,44,wv_f);
      if (wv_r==44 && memcmp(wv_hdr,"RIFF",4)==0 && memcmp(wv_hdr+8,"WAVE",4)==0) {
        uint16_t wv_ch,wv_bits; uint32_t wv_sr,wv_br,wv_block;
        memcpy(&wv_ch,wv_hdr+22,2); memcpy(&wv_sr,wv_hdr+24,4);
        memcpy(&wv_br,wv_hdr+28,4); memcpy(&wv_block,wv_hdr+32,2);
        memcpy(&wv_bits,wv_hdr+34,2);
        uint32_t wv_datasz=0; memcpy(&wv_datasz,wv_hdr+40,4);
        wv_res=ValStruct("WavFile");
        AKSStruct *wv_s=ValToObj(wv_res);
        wv_s->fieldnames=malloc(5*sizeof(char*)); wv_s->fieldvals=malloc(5*sizeof(AKSValue));
        wv_s->fieldnames[0]=strdup("channels");  wv_s->fieldvals[0]=ValInt(wv_ch);
        wv_s->fieldnames[1]=strdup("sample_rate");wv_s->fieldvals[1]=ValInt(wv_sr);
        wv_s->fieldnames[2]=strdup("bit_depth");  wv_s->fieldvals[2]=ValInt(wv_bits);
        wv_s->fieldnames[3]=strdup("data_size");  wv_s->fieldvals[3]=ValInt(wv_datasz);
        wv_s->fieldnames[4]=strdup("byte_rate");  wv_s->fieldvals[4]=ValInt(wv_br);
        wv_s->fieldcount=5;
      }
      fclose(wv_f);
    }
    ValFree(&PV); PUSH(wv_res); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "BmpFileParseC") == 0) {
    if (argc!=1){fprintf(stderr,"Runtime error: BmpFileParseC() expects 1 arg (path)\n");return -1;}
    AKSValue PV = POP();
    const char *bmp_path=(ValKind(PV)==VALSTR)?ValToStrPtr(PV)->data:"";
    FILE *bmp_f=fopen(bmp_path,"rb");
    AKSValue bmp_res=ValNull();
    if (bmp_f) {
      uint8_t bmp_hdr[54]; int bmp_r=(int)fread(bmp_hdr,1,54,bmp_f);
      if (bmp_r==54 && bmp_hdr[0]=='B' && bmp_hdr[1]=='M') {
        uint32_t bmp_w,bmp_h,bmp_off; uint16_t bmp_bpp;
        memcpy(&bmp_off,bmp_hdr+10,4);
        memcpy(&bmp_w,bmp_hdr+18,4); memcpy(&bmp_h,bmp_hdr+22,4);
        memcpy(&bmp_bpp,bmp_hdr+28,2);
        bmp_res=ValStruct("BmpFile");
        AKSStruct *bmp_s=ValToObj(bmp_res);
        bmp_s->fieldnames=malloc(4*sizeof(char*)); bmp_s->fieldvals=malloc(4*sizeof(AKSValue));
        bmp_s->fieldnames[0]=strdup("width");     bmp_s->fieldvals[0]=ValInt(bmp_w);
        bmp_s->fieldnames[1]=strdup("height");    bmp_s->fieldvals[1]=ValInt(bmp_h);
        bmp_s->fieldnames[2]=strdup("bpp");       bmp_s->fieldvals[2]=ValInt(bmp_bpp);
        bmp_s->fieldnames[3]=strdup("data_off");  bmp_s->fieldvals[3]=ValInt(bmp_off);
        bmp_s->fieldcount=4;
      }
      fclose(bmp_f);
    }
    ValFree(&PV); PUSH(bmp_res); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "NtfsAdsWriteC") == 0) {
    if (argc!=3){fprintf(stderr,"Runtime error: NtfsAdsWriteC() expects 3 args (file, stream, data)\n");return -1;}
    AKSValue dv2=POP(), sv2=POP(), fv2=POP();
    const char *ads_f=(ValKind(fv2)==VALSTR)?ValToStrPtr(fv2)->data:"";
    const char *ads_s=(ValKind(sv2)==VALSTR)?ValToStrPtr(sv2)->data:"default";
    const char *ads_d=(ValKind(dv2)==VALSTR)?ValToStrPtr(dv2)->data:"";
    int ads_dl=(ValKind(dv2)==VALSTR)?ValToStrPtr(dv2)->len:0;
    char ads_path[MAX_PATH*2];
    snprintf(ads_path,sizeof(ads_path),"%s:%s",ads_f,ads_s);
    HANDLE ads_h=CreateFileA(ads_path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    int ads_ok=0;
    if (ads_h!=INVALID_HANDLE_VALUE) {
      DWORD ads_wr=0; ads_ok=WriteFile(ads_h,(LPCVOID)ads_d,(DWORD)ads_dl,&ads_wr,NULL);
      CloseHandle(ads_h);
    }
    ValFree(&dv2); ValFree(&sv2); ValFree(&fv2);
    PUSH(ValBool(ads_ok)); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "NtfsAdsReadC") == 0) {
    if (argc!=2){fprintf(stderr,"Runtime error: NtfsAdsReadC() expects 2 args (file, stream)\n");return -1;}
    AKSValue sv3=POP(), fv3=POP();
    const char *ads_f2=(ValKind(fv3)==VALSTR)?ValToStrPtr(fv3)->data:"";
    const char *ads_s2=(ValKind(sv3)==VALSTR)?ValToStrPtr(sv3)->data:"default";
    char ads_path2[MAX_PATH*2];
    snprintf(ads_path2,sizeof(ads_path2),"%s:%s",ads_f2,ads_s2);
    HANDLE ads_h2=CreateFileA(ads_path2,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    AKSValue ads_res2=ValNull();
    if (ads_h2!=INVALID_HANDLE_VALUE) {
      LARGE_INTEGER ads_sz2; GetFileSizeEx(ads_h2,&ads_sz2);
      if (ads_sz2.QuadPart>0&&ads_sz2.QuadPart<1048576) {
        char *ads_buf=(char*)malloc((size_t)ads_sz2.QuadPart+1);
        DWORD ads_rd=0;
        if (ReadFile(ads_h2,ads_buf,(DWORD)ads_sz2.QuadPart,&ads_rd,NULL)) {
          ads_buf[ads_rd]=0; ads_res2=ValStrLen(ads_buf,(int)ads_rd);
        }
        free(ads_buf);
      }
      CloseHandle(ads_h2);
    }
    ValFree(&sv3); ValFree(&fv3); PUSH(ads_res2); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "GdiBitmapCreateC") == 0) {
    if (argc!=2){fprintf(stderr,"Runtime error: GdiBitmapCreateC() expects 2 args (w, h)\n");return -1;}
    AKSValue hv2=POP(), wv=POP();
    int gdi_w=(int)ValToInt(wv), gdi_h=(int)ValToInt(hv2);
    if(gdi_w<=0)gdi_w=1; if(gdi_h<=0)gdi_h=1;
    BITMAPINFO gdi_bmi; memset(&gdi_bmi,0,sizeof(gdi_bmi));
    gdi_bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    gdi_bmi.bmiHeader.biWidth=gdi_w; gdi_bmi.bmiHeader.biHeight=-gdi_h;
    gdi_bmi.bmiHeader.biPlanes=1; gdi_bmi.bmiHeader.biBitCount=32;
    gdi_bmi.bmiHeader.biCompression=BI_RGB;
    void *gdi_bits=NULL;
    HDC gdi_dc=CreateCompatibleDC(NULL);
    HBITMAP gdi_bmp=CreateDIBSection(gdi_dc,&gdi_bmi,DIB_RGB_COLORS,&gdi_bits,NULL,0);
    AKSValue gdi_res=ValStruct("GdiBitmap");
    AKSStruct *gdi_s=ValToObj(gdi_res);
    gdi_s->fieldnames=malloc(4*sizeof(char*)); gdi_s->fieldvals=malloc(4*sizeof(AKSValue));
    gdi_s->fieldnames[0]=strdup("w");     gdi_s->fieldvals[0]=ValInt(gdi_w);
    gdi_s->fieldnames[1]=strdup("h");     gdi_s->fieldvals[1]=ValInt(gdi_h);
    gdi_s->fieldnames[2]=strdup("hbmp");  gdi_s->fieldvals[2]=ValInt((int64_t)(uintptr_t)gdi_bmp);
    gdi_s->fieldnames[3]=strdup("hdc");   gdi_s->fieldvals[3]=ValInt((int64_t)(uintptr_t)gdi_dc);
    gdi_s->fieldcount=4;
    ValFree(&hv2); ValFree(&wv); PUSH(gdi_res); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "GdiBitmapSetPixelC") == 0) {
    if (argc!=4){fprintf(stderr,"Runtime error: GdiBitmapSetPixelC() expects 4 args (bmp, x, y, color)\n");return -1;}
    AKSValue cv=POP(), yv=POP(), xv=POP(), bv=POP();
    AKSStruct *gdi_s2=ValToObj(bv);
    HDC gdi_dc2=(HDC)(uintptr_t)ValToInt(gdi_s2->fieldvals[3]);
    HGDIOBJ gdi_old=SelectObject(gdi_dc2,(HGDIOBJ)(uintptr_t)ValToInt(gdi_s2->fieldvals[2]));
    SetPixel(gdi_dc2,(int)ValToInt(xv),(int)ValToInt(yv),(COLORREF)ValToInt(cv));
    SelectObject(gdi_dc2,gdi_old);
    ValFree(&cv); ValFree(&yv); ValFree(&xv); PUSH(bv); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "GdiBitmapSaveC") == 0) {
    if (argc!=2){fprintf(stderr,"Runtime error: GdiBitmapSaveC() expects 2 args (bmp, path)\n");return -1;}
    AKSValue pv4=POP(), bv=POP();
    const char *gdi_path=(ValKind(pv4)==VALSTR)?ValToStrPtr(pv4)->data:"out.bmp";
    AKSStruct *gdi_s3=ValToObj(bv);
    int gdi_w3=(int)ValToInt(gdi_s3->fieldvals[0]), gdi_h3=(int)ValToInt(gdi_s3->fieldvals[1]);
    HBITMAP gdi_hbmp=(HBITMAP)(uintptr_t)ValToInt(gdi_s3->fieldvals[2]);
    BITMAP gdi_bm; GetObject(gdi_hbmp,sizeof(gdi_bm),&gdi_bm);
    int gdi_stride=((gdi_w3*3+3)/4)*4;
    int gdi_fsz=54+gdi_stride*gdi_h3;
    uint8_t *gdi_data=calloc(gdi_fsz,1);
    gdi_data[0]='B'; gdi_data[1]='M';
    *(uint32_t*)(gdi_data+2)=(uint32_t)gdi_fsz; *(uint32_t*)(gdi_data+10)=54;
    *(uint32_t*)(gdi_data+14)=40; *(int32_t*)(gdi_data+18)=gdi_w3; *(int32_t*)(gdi_data+22)=-gdi_h3;
    *(uint16_t*)(gdi_data+26)=1; *(uint16_t*)(gdi_data+28)=24;
    *(uint32_t*)(gdi_data+34)=(uint32_t)(gdi_stride*gdi_h3);
    HDC gdi_dc3=(HDC)(uintptr_t)ValToInt(gdi_s3->fieldvals[3]);
    BITMAPINFO gdi_bi2; memset(&gdi_bi2,0,sizeof(gdi_bi2));
    gdi_bi2.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    gdi_bi2.bmiHeader.biWidth=gdi_w3; gdi_bi2.bmiHeader.biHeight=gdi_h3;
    gdi_bi2.bmiHeader.biPlanes=1; gdi_bi2.bmiHeader.biBitCount=24;
    gdi_bi2.bmiHeader.biCompression=BI_RGB;
    GetDIBits(gdi_dc3,gdi_hbmp,0,(UINT)gdi_h3,gdi_data+54,&gdi_bi2,DIB_RGB_COLORS);
    FILE *gdi_fp=fopen(gdi_path,"wb");
    int gdi_ok=0;
    if(gdi_fp){fwrite(gdi_data,1,(size_t)gdi_fsz,gdi_fp);fclose(gdi_fp);gdi_ok=1;}
    free(gdi_data);
    ValFree(&pv4); ValFree(&bv); PUSH(ValBool(gdi_ok)); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "PdbSymbolResolveC") == 0) {
    if (argc!=1){fprintf(stderr,"Runtime error: PdbSymbolResolveC() expects 1 arg (addr)\n");return -1;}
    AKSValue AV7=POP(); uint64_t pdb_addr=(uint64_t)ValToInt(AV7); ValFree(&AV7);
    char pdb_name[256]="?";
    HANDLE pdb_proc=GetCurrentProcess();
    SymInitialize(pdb_proc,NULL,TRUE);
    char pdb_buf[sizeof(SYMBOL_INFO)+256];
    SYMBOL_INFO *pdb_sym=(SYMBOL_INFO*)pdb_buf; pdb_sym->SizeOfStruct=sizeof(SYMBOL_INFO); pdb_sym->MaxNameLen=255;
    DWORD64 pdb_disp=0;
    if (SymFromAddr(pdb_proc,(DWORD64)pdb_addr,&pdb_disp,pdb_sym))
      strncpy(pdb_name,pdb_sym->Name,255);
    PUSH(ValStr(pdb_name)); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "EtwEmitC") == 0) {
    if (argc!=2){fprintf(stderr,"Runtime error: EtwEmitC() expects 2 args (provider_guid_str, message)\n");return -1;}
    AKSValue mv=POP(), gv=POP();
    REGHANDLE etw_h=0;
    GUID etw_guid={0x12345678,0x1234,0x1234,{0x12,0x34,0x12,0x34,0x56,0x78,0x9a,0xbc}};
    EventRegister(&etw_guid,NULL,NULL,&etw_h);
    if (ValKind(mv)==VALSTR) {
      EVENT_DESCRIPTOR etw_ed; memset(&etw_ed,0,sizeof(etw_ed)); etw_ed.Id=1; etw_ed.Level=4;
      EventWrite(etw_h,&etw_ed,0,NULL);
    }
    EventUnregister(etw_h);
    ValFree(&mv); ValFree(&gv); PUSH(ValBool(1)); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "PdhQueryC") == 0) {
    if (argc!=1){fprintf(stderr,"Runtime error: PdhQueryC() expects 1 arg (counter_path)\n");return -1;}
    AKSValue CV2=POP();
    const char *pdh_path=(ValKind(CV2)==VALSTR)?ValToStrPtr(CV2)->data:"\\Processor(_Total)\\% Processor Time";
    PDH_HQUERY pdh_q=NULL; PDH_HCOUNTER pdh_c=NULL; double pdh_val=0.0;
    if (PdhOpenQuery(NULL,0,&pdh_q)==ERROR_SUCCESS) {
      if (PdhAddCounterA(pdh_q,pdh_path,0,&pdh_c)==ERROR_SUCCESS) {
        PdhCollectQueryData(pdh_q); Sleep(100); PdhCollectQueryData(pdh_q);
        PDH_FMT_COUNTERVALUE pdh_fv; memset(&pdh_fv,0,sizeof(pdh_fv));
        if (PdhGetFormattedCounterValue(pdh_c,PDH_FMT_DOUBLE,NULL,&pdh_fv)==ERROR_SUCCESS)
          pdh_val=pdh_fv.doubleValue;
      }
      PdhCloseQuery(pdh_q);
    }
    ValFree(&CV2); PUSH(ValFloat(pdh_val)); *stacktopptr=stacktop; return 1;
  }

  if (strcmp(name, "WinHttpGetC") == 0) {
    if (argc!=1){fprintf(stderr,"Runtime error: WinHttpGetC() expects 1 arg (url)\n");return -1;}
    AKSValue UV=POP();
    const char *wh_url=(ValKind(UV)==VALSTR)?ValToStrPtr(UV)->data:"";
    wchar_t wh_wurl[2048]; MultiByteToWideChar(CP_UTF8,0,wh_url,-1,wh_wurl,2048);
    URL_COMPONENTSW wh_uc; memset(&wh_uc,0,sizeof(wh_uc)); wh_uc.dwStructSize=sizeof(wh_uc);
    wchar_t wh_host[512]={0}; wchar_t wh_path2[1024]={0};
    wh_uc.lpszHostName=wh_host; wh_uc.dwHostNameLength=511;
    wh_uc.lpszUrlPath=wh_path2; wh_uc.dwUrlPathLength=1023;
    WinHttpCrackUrl(wh_wurl,0,0,&wh_uc);
    HINTERNET wh_sess=WinHttpOpen(L"AksVM/1.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    AKSValue wh_res=ValNull();
    if (wh_sess) {
      BOOL wh_https=wh_uc.nScheme==INTERNET_SCHEME_HTTPS;
      HINTERNET wh_conn=WinHttpConnect(wh_sess,wh_host,wh_uc.nPort?wh_uc.nPort:(wh_https?443:80),0);
      if (wh_conn) {
        HINTERNET wh_req=WinHttpOpenRequest(wh_conn,L"GET",wh_path2[0]?wh_path2:L"/",NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,wh_https?WINHTTP_FLAG_SECURE:0);
        if (wh_req) {
          if (WinHttpSendRequest(wh_req,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)) {
            WinHttpReceiveResponse(wh_req,NULL);
            char *wh_body=NULL; size_t wh_blen=0; DWORD wh_avail=0,wh_rd=0;
            while (WinHttpQueryDataAvailable(wh_req,&wh_avail) && wh_avail>0) {
              wh_body=realloc(wh_body,wh_blen+wh_avail+1);
              WinHttpReadData(wh_req,wh_body+wh_blen,wh_avail,&wh_rd);
              wh_blen+=wh_rd;
            }
            if (wh_body){wh_body[wh_blen]=0;wh_res=ValStrLen(wh_body,(int)wh_blen);free(wh_body);}
          }
          WinHttpCloseHandle(wh_req);
        }
        WinHttpCloseHandle(wh_conn);
      }
      WinHttpCloseHandle(wh_sess);
    }
    ValFree(&UV); PUSH(wh_res); *stacktopptr=stacktop; return 1;
  }
  if (strcmp(name, "WinHttpPostC") == 0) {
    if (argc!=3){fprintf(stderr,"Runtime error: WinHttpPostC() expects 3 args (url, content_type, body)\n");return -1;}
    AKSValue bv2=POP(), ctv=POP(), uv2=POP();
    const char *wp_url=(ValKind(uv2)==VALSTR)?ValToStrPtr(uv2)->data:"";
    const char *wp_ct=(ValKind(ctv)==VALSTR)?ValToStrPtr(ctv)->data:"application/json";
    const char *wp_body2=(ValKind(bv2)==VALSTR)?ValToStrPtr(bv2)->data:"";
    int wp_blen=(ValKind(bv2)==VALSTR)?ValToStrPtr(bv2)->len:0;
    wchar_t wp_wurl[2048]; MultiByteToWideChar(CP_UTF8,0,wp_url,-1,wp_wurl,2048);
    wchar_t wp_wct[256]; MultiByteToWideChar(CP_UTF8,0,wp_ct,-1,wp_wct,256);
    URL_COMPONENTSW wp_uc; memset(&wp_uc,0,sizeof(wp_uc)); wp_uc.dwStructSize=sizeof(wp_uc);
    wchar_t wp_host[512]={0}; wchar_t wp_path3[1024]={0};
    wp_uc.lpszHostName=wp_host; wp_uc.dwHostNameLength=511;
    wp_uc.lpszUrlPath=wp_path3; wp_uc.dwUrlPathLength=1023;
    WinHttpCrackUrl(wp_wurl,0,0,&wp_uc);
    HINTERNET wp_sess=WinHttpOpen(L"AksVM/1.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    AKSValue wp_res=ValNull();
    if (wp_sess) {
      BOOL wp_https=wp_uc.nScheme==INTERNET_SCHEME_HTTPS;
      HINTERNET wp_conn=WinHttpConnect(wp_sess,wp_host,wp_uc.nPort?wp_uc.nPort:(wp_https?443:80),0);
      if (wp_conn) {
        HINTERNET wp_req=WinHttpOpenRequest(wp_conn,L"POST",wp_path3[0]?wp_path3:L"/",NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,wp_https?WINHTTP_FLAG_SECURE:0);
        if (wp_req) {
          wchar_t wp_hdrs[512]; swprintf(wp_hdrs,512,L"Content-Type: %s\r\n",wp_wct);
          if (WinHttpSendRequest(wp_req,wp_hdrs,(DWORD)-1L,(LPVOID)wp_body2,(DWORD)wp_blen,(DWORD)wp_blen,0)) {
            WinHttpReceiveResponse(wp_req,NULL);
            char *wp_rbody=NULL; size_t wp_rblen=0; DWORD wp_avail2=0,wp_rd2=0;
            while (WinHttpQueryDataAvailable(wp_req,&wp_avail2) && wp_avail2>0) {
              wp_rbody=realloc(wp_rbody,wp_rblen+wp_avail2+1);
              WinHttpReadData(wp_req,wp_rbody+wp_rblen,wp_avail2,&wp_rd2);
              wp_rblen+=wp_rd2;
            }
            if (wp_rbody){wp_rbody[wp_rblen]=0;wp_res=ValStrLen(wp_rbody,(int)wp_rblen);free(wp_rbody);}
          }
          WinHttpCloseHandle(wp_req);
        }
        WinHttpCloseHandle(wp_conn);
      }
      WinHttpCloseHandle(wp_sess);
    }
    ValFree(&bv2); ValFree(&ctv); ValFree(&uv2); PUSH(wp_res); *stacktopptr=stacktop; return 1;
  }


  /* ShmRing - Lock-Free Shared Memory SPSC Ring Buffer */
  typedef struct {
    volatile uint64_t head;
    volatile uint64_t tail;
    uint64_t capacity;
  } ShmRingHdr;

  if (strcmp(name, "ShmRingNewC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: ShmRingNewC() expects 2 args (name, capacity_bytes)\n"); return -1; }
    AKSValue cv = POP(), nv = POP();
    const char *rname = (ValKind(nv) == VALSTR) ? ValToStrPtr(nv)->data : "aks_shm_ring";
    uint64_t cap = (uint64_t)ValToInt(cv);
    if (cap < 64) cap = 64;
    size_t total_sz = sizeof(ShmRingHdr) + cap;
    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)total_sz, rname);
    void *view = NULL;
    int is_creator = 0;
    if (hMap) {
      if (GetLastError() != ERROR_ALREADY_EXISTS) is_creator = 1;
      view = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    }
    if (view && is_creator) {
      ShmRingHdr *hdr = (ShmRingHdr*)view;
      hdr->head = 0;
      hdr->tail = 0;
      hdr->capacity = cap;
    }
    AKSValue sval = ValStruct("ShmRing");
    AKSStruct *s = ValToObj(sval);
    s->fieldnames = malloc(4 * sizeof(char*));
    s->fieldvals = malloc(4 * sizeof(AKSValue));
    s->fieldnames[0] = strdup("name");     s->fieldvals[0] = ValStr(rname);
    s->fieldnames[1] = strdup("capacity"); s->fieldvals[1] = ValInt((int64_t)cap);
    s->fieldnames[2] = strdup("hmap");     s->fieldvals[2] = ValInt((int64_t)(uintptr_t)hMap);
    s->fieldnames[3] = strdup("view");     s->fieldvals[3] = ValInt((int64_t)(uintptr_t)view);
    s->fieldcount = 4;
    ValFree(&cv); ValFree(&nv);
    PUSH(sval); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "ShmRingPushC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: ShmRingPushC() expects 2 args (shm, data_str)\n"); return -1; }
    AKSValue dv = POP(), sv = POP();
    AKSStruct *s = ValToObj(sv);
    void *view = (void*)(uintptr_t)ValToInt(s->fieldvals[3]);
    int pushed = 0;
    if (view && ValKind(dv) == VALSTR) {
      ShmRingHdr *hdr = (ShmRingHdr*)view;
      uint8_t *buf = (uint8_t*)(hdr + 1);
      const char *src = ValToStrPtr(dv)->data;
      uint32_t len = (uint32_t)ValToStrPtr(dv)->len;
      uint64_t cap = hdr->capacity;
      uint64_t head = hdr->head;
      uint64_t tail = hdr->tail;
      uint64_t used = tail - head;
      if (cap - used >= len + 4) {
        uint8_t lb[4];
        memcpy(lb, &len, 4);
        for (int i = 0; i < 4; i++) {
          buf[(tail + i) % cap] = lb[i];
        }
        for (uint32_t i = 0; i < len; i++) {
          buf[(tail + 4 + i) % cap] = (uint8_t)src[i];
        }
        MemoryBarrier();
        hdr->tail = tail + len + 4;
        pushed = 1;
      }
    }
    ValFree(&dv); ValFree(&sv);
    PUSH(ValBool(pushed)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "ShmRingPopC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: ShmRingPopC() expects 1 arg (shm)\n"); return -1; }
    AKSValue sv = POP();
    AKSStruct *s = ValToObj(sv);
    void *view = (void*)(uintptr_t)ValToInt(s->fieldvals[3]);
    AKSValue res = ValNull();
    if (view) {
      ShmRingHdr *hdr = (ShmRingHdr*)view;
      uint8_t *buf = (uint8_t*)(hdr + 1);
      uint64_t cap = hdr->capacity;
      uint64_t head = hdr->head;
      uint64_t tail = hdr->tail;
      if (tail > head && (tail - head) >= 4) {
        uint8_t lb[4];
        for (int i = 0; i < 4; i++) {
          lb[i] = buf[(head + i) % cap];
        }
        uint32_t len = 0;
        memcpy(&len, lb, 4);
        if ((tail - head) >= len + 4) {
          char *out = malloc(len + 1);
          if (out) {
            for (uint32_t i = 0; i < len; i++) {
              out[i] = (char)buf[(head + 4 + i) % cap];
            }
            out[len] = '\0';
            MemoryBarrier();
            hdr->head = head + len + 4;
            res = ValStrLen(out, (int)len);
            free(out);
          }
        }
      }
    }
    ValFree(&sv);
    PUSH(res); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "ShmRingAvailableC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: ShmRingAvailableC() expects 1 arg (shm)\n"); return -1; }
    AKSValue sv = POP();
    AKSStruct *s = ValToObj(sv);
    void *view = (void*)(uintptr_t)ValToInt(s->fieldvals[3]);
    int64_t avail = 0;
    if (view) {
      ShmRingHdr *hdr = (ShmRingHdr*)view;
      avail = (int64_t)(hdr->tail - hdr->head);
      if (avail < 0) avail = 0;
    }
    ValFree(&sv);
    PUSH(ValInt(avail)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "ShmRingCloseC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: ShmRingCloseC() expects 1 arg (shm)\n"); return -1; }
    AKSValue sv = POP();
    AKSStruct *s = ValToObj(sv);
    HANDLE hMap = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[2]);
    void *view = (void*)(uintptr_t)ValToInt(s->fieldvals[3]);
    if (view) UnmapViewOfFile(view);
    if (hMap && hMap != INVALID_HANDLE_VALUE) CloseHandle(hMap);
    s->fieldvals[2] = ValInt(0);
    s->fieldvals[3] = ValInt(0);
    ValFree(&sv);
    PUSH(ValBool(1)); *stacktopptr = stacktop; return 1;
  }

  /* ZipArchive Subsystem */
  if (strcmp(name, "ZipArchiveCreateC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: ZipArchiveCreateC() expects 2 args (zip_path, files_array)\n"); return -1; }
    AKSValue fv = POP(), pv = POP();
    const char *zpath = (ValKind(pv) == VALSTR) ? ValToStrPtr(pv)->data : "output.zip";
    AKSArray *farr = ValToArr(fv);
    FILE *zf = fopen(zpath, "wb");
    int ok = 0;
    if (zf && farr) {
      typedef struct {
        char name[256];
        uint32_t offset;
        uint32_t crc;
        uint32_t size;
      } ZipEntryInfo;
      ZipEntryInfo *entries = calloc(farr->count + 1, sizeof(ZipEntryInfo));
      uint32_t cd_offset = 0;
      for (int i = 0; i < farr->count; i++) {
        const char *item_name = "";
        const char *item_data = "";
        size_t item_len = 0;
        char *allocated_data = NULL;
        if (ValKind(farr->items[i]) == VALSTR) {
          item_name = ValToStrPtr(farr->items[i])->data;
          FILE *inf = fopen(item_name, "rb");
          if (inf) {
            fseek(inf, 0, SEEK_END);
            item_len = ftell(inf);
            fseek(inf, 0, SEEK_SET);
            allocated_data = malloc(item_len + 1);
            if (allocated_data) {
              fread(allocated_data, 1, item_len, inf);
              allocated_data[item_len] = '\0';
              item_data = allocated_data;
            }
            fclose(inf);
          }
        } else if (ValKind(farr->items[i]) == VALSTRUCT) {
          AKSStruct *st = ValToObj(farr->items[i]);
          for (int f = 0; f < st->fieldcount; f++) {
            if (strcmp(st->fieldnames[f], "name") == 0 && ValKind(st->fieldvals[f]) == VALSTR)
              item_name = ValToStrPtr(st->fieldvals[f])->data;
            if (strcmp(st->fieldnames[f], "data") == 0 && ValKind(st->fieldvals[f]) == VALSTR) {
              item_data = ValToStrPtr(st->fieldvals[f])->data;
              item_len = (size_t)ValToStrPtr(st->fieldvals[f])->len;
            }
          }
        }
        strncpy(entries[i].name, item_name, 255);
        entries[i].offset = (uint32_t)ftell(zf);
        entries[i].size = (uint32_t)item_len;
        uint32_t crc = 0xFFFFFFFF;
        for (size_t b = 0; b < item_len; b++) {
          crc ^= (uint8_t)item_data[b];
          for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320 & (-(int)(crc & 1)));
        }
        entries[i].crc = ~crc;

        uint16_t nlen = (uint16_t)strlen(item_name);
        uint8_t lfh[30] = {
          0x50, 0x4B, 0x03, 0x04,
          20, 0,
          0, 0,
          0, 0,
          0, 0, 0, 0,
          0, 0, 0, 0,
          0, 0, 0, 0,
          0, 0, 0, 0,
          0, 0,
          0, 0
        };
        memcpy(lfh + 14, &entries[i].crc, 4);
        memcpy(lfh + 18, &entries[i].size, 4);
        memcpy(lfh + 22, &entries[i].size, 4);
        memcpy(lfh + 26, &nlen, 2);
        fwrite(lfh, 1, 30, zf);
        fwrite(item_name, 1, nlen, zf);
        if (item_len > 0) {
          fwrite(item_data, 1, item_len, zf);
        }
        if (allocated_data) free(allocated_data);
      }

      cd_offset = (uint32_t)ftell(zf);
      for (int i = 0; i < farr->count; i++) {
        uint16_t nlen = (uint16_t)strlen(entries[i].name);
        uint8_t cdh[46] = {
          0x50, 0x4B, 0x01, 0x02,
          20, 0,
          20, 0,
          0, 0,
          0, 0,
          0, 0, 0, 0,
          0, 0, 0, 0,
          0, 0, 0, 0,
          0, 0, 0, 0,
          0, 0,
          0, 0,
          0, 0,
          0, 0,
          0, 0,
          0, 0, 0, 0,
          0, 0, 0, 0
        };
        memcpy(cdh + 16, &entries[i].crc, 4);
        memcpy(cdh + 20, &entries[i].size, 4);
        memcpy(cdh + 24, &entries[i].size, 4);
        memcpy(cdh + 28, &nlen, 2);
        memcpy(cdh + 42, &entries[i].offset, 4);
        fwrite(cdh, 1, 46, zf);
        fwrite(entries[i].name, 1, nlen, zf);
      }
      uint32_t cd_size = (uint32_t)ftell(zf) - cd_offset;
      uint16_t total_ent = (uint16_t)farr->count;
      uint8_t eocd[22] = {
        0x50, 0x4B, 0x05, 0x06,
        0, 0,
        0, 0,
        0, 0,
        0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0
      };
      memcpy(eocd + 8, &total_ent, 2);
      memcpy(eocd + 10, &total_ent, 2);
      memcpy(eocd + 12, &cd_size, 4);
      memcpy(eocd + 16, &cd_offset, 4);
      fwrite(eocd, 1, 22, zf);
      fclose(zf);
      free(entries);
      ok = 1;
    }
    ValFree(&fv); ValFree(&pv);
    PUSH(ValBool(ok)); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "ZipArchiveListC") == 0) {
    if (argc != 1) { fprintf(stderr, "Runtime error: ZipArchiveListC() expects 1 arg (zip_path)\n"); return -1; }
    AKSValue pv = POP();
    const char *zpath = (ValKind(pv) == VALSTR) ? ValToStrPtr(pv)->data : "";
    FILE *zf = fopen(zpath, "rb");
    AKSValue res = ValArr(8);
    AKSArray *ra = ValToArr(res);
    if (zf) {
      uint8_t sig[4];
      while (fread(sig, 1, 4, zf) == 4) {
        if (sig[0] == 0x50 && sig[1] == 0x4B && sig[2] == 0x03 && sig[3] == 0x04) {
          uint8_t hdr[26];
          if (fread(hdr, 1, 26, zf) != 26) break;
          uint16_t nlen = *(uint16_t*)(hdr + 22);
          uint16_t elen = *(uint16_t*)(hdr + 24);
          uint32_t csize = *(uint32_t*)(hdr + 14);
          char *ename = malloc(nlen + 1);
          if (ename) {
            fread(ename, 1, nlen, zf);
            ename[nlen] = '\0';
            AksArrPush(ra, ValStr(ename));
            free(ename);
          }
          if (elen > 0) fseek(zf, elen, SEEK_CUR);
          if (csize > 0) fseek(zf, csize, SEEK_CUR);
        } else {
          break;
        }
      }
      fclose(zf);
    }
    ValFree(&pv);
    PUSH(res); *stacktopptr = stacktop; return 1;
  }

  if (strcmp(name, "ZipArchiveExtractC") == 0) {
    if (argc != 2) { fprintf(stderr, "Runtime error: ZipArchiveExtractC() expects 2 args (zip_path, dest_dir)\n"); return -1; }
    AKSValue dv = POP(), pv = POP();
    const char *zpath = (ValKind(pv) == VALSTR) ? ValToStrPtr(pv)->data : "";
    const char *dest = (ValKind(dv) == VALSTR) ? ValToStrPtr(dv)->data : ".";
    CreateDirectoryA(dest, NULL);
    FILE *zf = fopen(zpath, "rb");
    int extracted = 0;
    if (zf) {
      uint8_t sig[4];
      while (fread(sig, 1, 4, zf) == 4) {
        if (sig[0] == 0x50 && sig[1] == 0x4B && sig[2] == 0x03 && sig[3] == 0x04) {
          uint8_t hdr[26];
          if (fread(hdr, 1, 26, zf) != 26) break;
          uint16_t nlen = *(uint16_t*)(hdr + 22);
          uint16_t elen = *(uint16_t*)(hdr + 24);
          uint32_t csize = *(uint32_t*)(hdr + 14);
          char *ename = malloc(nlen + 1);
          if (ename) {
            fread(ename, 1, nlen, zf);
            ename[nlen] = '\0';
            if (elen > 0) fseek(zf, elen, SEEK_CUR);
            char outpath[MAX_PATH];
            snprintf(outpath, sizeof(outpath), "%s/%s", dest, ename);
            FILE *outf = fopen(outpath, "wb");
            if (outf) {
              if (csize > 0) {
                uint8_t *dbuf = malloc(csize);
                if (dbuf) {
                  fread(dbuf, 1, csize, zf);
                  fwrite(dbuf, 1, csize, outf);
                  free(dbuf);
                }
              }
              fclose(outf);
              extracted++;
            } else {
              if (csize > 0) fseek(zf, csize, SEEK_CUR);
            }
            free(ename);
          }
        } else {
          break;
        }
      }
      fclose(zf);
    }
    ValFree(&dv); ValFree(&pv);
    PUSH(ValInt(extracted)); *stacktopptr = stacktop; return 1;
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

  if (strcmp(name, "SimdOp") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SimdOp() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue op_val = POP();
    AKSValue arr2_val = POP();
    AKSValue arr1_val = POP();
    int op = (int)ValToInt(op_val);
    AKSArray *a1 = ValToArr(arr1_val);
    AKSArray *a2 = ValToArr(arr2_val);
    int N = a1->count < a2->count ? a1->count : a2->count;
    AKSValue res = ValArr(N);
    AKSArray *res_arr = ValToArr(res);
    res_arr->count = N;
    SimdOpArrays(a1, a2, res_arr, N, op);
    PUSH(res);
    ValFree(&op_val);
    ValFree(&arr2_val);
    ValFree(&arr1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdFma") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SimdFma() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arr3_val = POP();
    AKSValue arr2_val = POP();
    AKSValue arr1_val = POP();
    AKSArray *a1 = ValToArr(arr1_val);
    AKSArray *a2 = ValToArr(arr2_val);
    AKSArray *a3 = ValToArr(arr3_val);
    int N = a1->count;
    if (a2->count < N)
      N = a2->count;
    if (a3->count < N)
      N = a3->count;
    AKSValue res = ValArr(N);
    AKSArray *res_arr = ValToArr(res);
    res_arr->count = N;
    SimdFmaArrays(a1, a2, a3, res_arr, N);
    PUSH(res);
    ValFree(&arr3_val);
    ValFree(&arr2_val);
    ValFree(&arr1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdDot") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdDot() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arr2_val = POP();
    AKSValue arr1_val = POP();
    AKSArray *a1 = ValToArr(arr1_val);
    AKSArray *a2 = ValToArr(arr2_val);
    int N = a1->count < a2->count ? a1->count : a2->count;
    double sum = 0.0;
    int i = 0;
    for (; i + 3 < N; i += 4) {
      sum += ValToFloat(a1->items[i]) * ValToFloat(a2->items[i]);
      sum += ValToFloat(a1->items[i + 1]) * ValToFloat(a2->items[i + 1]);
      sum += ValToFloat(a1->items[i + 2]) * ValToFloat(a2->items[i + 2]);
      sum += ValToFloat(a1->items[i + 3]) * ValToFloat(a2->items[i + 3]);
    }
    for (; i < N; i++) {
      sum += ValToFloat(a1->items[i]) * ValToFloat(a2->items[i]);
    }
    PUSH(ValFloat(sum));
    ValFree(&arr2_val);
    ValFree(&arr1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdSum") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdSum() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arr_val = POP();
    AKSArray *arr = ValToArr(arr_val);
    int N = arr->count;
    double sum = 0.0;
    int i = 0;
    for (; i + 3 < N; i += 4) {
      sum += ValToFloat(arr->items[i]) + ValToFloat(arr->items[i + 1]) +
             ValToFloat(arr->items[i + 2]) + ValToFloat(arr->items[i + 3]);
    }
    for (; i < N; i++) {
      sum += ValToFloat(arr->items[i]);
    }
    PUSH(ValFloat(sum));
    ValFree(&arr_val);
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
  if (strcmp(name, "MatrixMulRaw") == 0) {
    if (argc != 6) {
      fprintf(stderr,
              "Runtime error: MatrixMulRaw() expects 6 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue d2_val = POP();
    AKSValue c2_val = POP();
    AKSValue r2_val = POP();
    AKSValue d1_val = POP();
    AKSValue c1_val = POP();
    AKSValue r1_val = POP();

    int r1 = (int)ValToInt(r1_val);
    int c1 = (int)ValToInt(c1_val);
    int r2 = (int)ValToInt(r2_val);
    int c2 = (int)ValToInt(c2_val);
    AKSArray *a1 = ValToArr(d1_val);
    AKSArray *a2 = ValToArr(d2_val);

    double *d1 = malloc(a1->count * sizeof(double));
    for (int i = 0; i < a1->count; i++)
      d1[i] = ValToFloat(a1->items[i]);
    double *d2 = malloc(a2->count * sizeof(double));
    for (int i = 0; i < a2->count; i++)
      d2[i] = ValToFloat(a2->items[i]);

    double *res_raw = malloc(r1 * c2 * sizeof(double));
    MatrixMulC(r1, c1, d1, r2, c2, d2, res_raw);

    AKSValue res_val = ValArr(r1 * c2);
    AKSArray *a_res = ValToArr(res_val);
    a_res->count = r1 * c2;
    for (int i = 0; i < r1 * c2; i++)
      a_res->items[i] = ValFloat(res_raw[i]);

    PUSH(res_val);
    free(d1);
    free(d2);
    free(res_raw);
    ValFree(&d2_val);
    ValFree(&c2_val);
    ValFree(&r2_val);
    ValFree(&d1_val);
    ValFree(&c1_val);
    ValFree(&r1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MatrixTransposeRaw") == 0) {
    if (argc != 3) {
      fprintf(
          stderr,
          "Runtime error: MatrixTransposeRaw() expects 3 arguments, got %d\n",
          argc);
      return -1;
    }
    AKSValue d_val = POP();
    AKSValue c_val = POP();
    AKSValue r_val = POP();
    int r = (int)ValToInt(r_val);
    int c = (int)ValToInt(c_val);
    AKSArray *a = ValToArr(d_val);

    AKSValue res_val = ValArr(r * c);
    AKSArray *a_res = ValToArr(res_val);
    a_res->count = r * c;
    for (int i = 0; i < r; i++) {
      for (int j = 0; j < c; j++) {
        a_res->items[j * r + i] = ValCopy(a->items[i * c + j]);
      }
    }
    PUSH(res_val);
    ValFree(&d_val);
    ValFree(&c_val);
    ValFree(&r_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MatrixInverseRaw") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: MatrixInverseRaw() expects 2 arguments\n");
      return -1;
    }
    AKSValue d_val = POP();
    AKSValue n_val = POP();
    int n = (int)ValToInt(n_val);
    AKSArray *a = ValToArr(d_val);

    double *da = malloc(n * n * sizeof(double));
    for (int i = 0; i < n * n; i++)
      da[i] = ValToFloat(a->items[i]);
    double *dres = malloc(n * n * sizeof(double));

    int ok = MatrixInverseC(n, da, dres);
    if (!ok) {
      PUSH(ValNull());
    } else {
      AKSValue res_val = ValArr(n * n);
      AKSArray *a_res = ValToArr(res_val);
      a_res->count = n * n;
      for (int i = 0; i < n * n; i++)
        a_res->items[i] = ValFloat(dres[i]);
      PUSH(res_val);
    }
    free(da);
    free(dres);
    ValFree(&d_val);
    ValFree(&n_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MatrixLuRaw") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MatrixLuRaw() expects 2 arguments\n");
      return -1;
    }
    AKSValue d_val = POP();
    AKSValue n_val = POP();
    int n = (int)ValToInt(n_val);
    AKSArray *a = ValToArr(d_val);

    double *da = malloc(n * n * sizeof(double));
    for (int i = 0; i < n * n; i++)
      da[i] = ValToFloat(a->items[i]);
    double *dL = malloc(n * n * sizeof(double));
    double *dU = malloc(n * n * sizeof(double));
    double *dP = malloc(n * sizeof(double));

    MatrixLuC(n, da, dL, dU, dP);

    AKSValue valL = ValArr(n * n);
    AKSArray *aL = ValToArr(valL);
    aL->count = n * n;
    for (int i = 0; i < n * n; i++)
      aL->items[i] = ValFloat(dL[i]);

    AKSValue valU = ValArr(n * n);
    AKSArray *aU = ValToArr(valU);
    aU->count = n * n;
    for (int i = 0; i < n * n; i++)
      aU->items[i] = ValFloat(dU[i]);

    AKSValue valP = ValArr(n);
    AKSArray *aP = ValToArr(valP);
    aP->count = n;
    for (int i = 0; i < n; i++)
      aP->items[i] = ValInt((int64_t)dP[i]);

    AKSValue res_arr = ValArr(3);
    AKSArray *a_res = ValToArr(res_arr);
    a_res->count = 3;
    a_res->items[0] = valL;
    a_res->items[1] = valU;
    a_res->items[2] = valP;

    PUSH(res_arr);
    free(da);
    free(dL);
    free(dU);
    free(dP);
    ValFree(&d_val);
    ValFree(&n_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MatrixCholeskyRaw") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: MatrixCholeskyRaw() expects 2 arguments\n");
      return -1;
    }
    AKSValue d_val = POP();
    AKSValue n_val = POP();
    int n = (int)ValToInt(n_val);
    AKSArray *a = ValToArr(d_val);

    double *da = malloc(n * n * sizeof(double));
    for (int i = 0; i < n * n; i++)
      da[i] = ValToFloat(a->items[i]);
    double *dL = malloc(n * n * sizeof(double));

    int ok = MatrixCholeskyC(n, da, dL);
    if (!ok) {
      PUSH(ValNull());
    } else {
      AKSValue valL = ValArr(n * n);
      AKSArray *aL = ValToArr(valL);
      aL->count = n * n;
      for (int i = 0; i < n * n; i++)
        aL->items[i] = ValFloat(dL[i]);
      PUSH(valL);
    }
    free(da);
    free(dL);
    ValFree(&d_val);
    ValFree(&n_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FftRaw") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: FftRaw() expects 3 arguments\n");
      return -1;
    }
    AKSValue inv_val = POP();
    AKSValue imag_val = POP();
    AKSValue real_val = POP();
    int inverse = ValToBool(inv_val);
    AKSArray *areal = ValToArr(real_val);
    AKSArray *aimag = ValToArr(imag_val);
    int n = areal->count;

    double *dreal = malloc(n * sizeof(double));
    double *dimag = malloc(n * sizeof(double));
    for (int i = 0; i < n; i++) {
      dreal[i] = ValToFloat(areal->items[i]);
      dimag[i] = ValToFloat(aimag->items[i]);
    }

    FFTC(dreal, dimag, n, inverse);

    AKSValue valreal = ValArr(n);
    AKSArray *a_r = ValToArr(valreal);
    a_r->count = n;
    AKSValue valimag = ValArr(n);
    AKSArray *a_i = ValToArr(valimag);
    a_i->count = n;
    for (int i = 0; i < n; i++) {
      a_r->items[i] = ValFloat(dreal[i]);
      a_i->items[i] = ValFloat(dimag[i]);
    }

    AKSValue res_arr = ValArr(2);
    AKSArray *a_res = ValToArr(res_arr);
    a_res->count = 2;
    a_res->items[0] = valreal;
    a_res->items[1] = valimag;
    PUSH(res_arr);

    free(dreal);
    free(dimag);
    ValFree(&inv_val);
    ValFree(&imag_val);
    ValFree(&real_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BigIntAdd") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: BigIntAdd() expects 2 arguments\n");
      return -1;
    }
    AKSValue s2_val = POP();
    AKSValue s1_val = POP();
    char *r = BigIntAddC(ValToStrPtr(s1_val)->data, ValToStrPtr(s2_val)->data);
    PUSH(ValStr(r));
    free(r);
    ValFree(&s2_val);
    ValFree(&s1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BigIntSub") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: BigIntSub() expects 2 arguments\n");
      return -1;
    }
    AKSValue s2_val = POP();
    AKSValue s1_val = POP();
    char *r = BigIntSubC(ValToStrPtr(s1_val)->data, ValToStrPtr(s2_val)->data);
    PUSH(ValStr(r));
    free(r);
    ValFree(&s2_val);
    ValFree(&s1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BigIntMul") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: BigIntMul() expects 2 arguments\n");
      return -1;
    }
    AKSValue s2_val = POP();
    AKSValue s1_val = POP();
    char *r = BigIntMulC(ValToStrPtr(s1_val)->data, ValToStrPtr(s2_val)->data);
    PUSH(ValStr(r));
    free(r);
    ValFree(&s2_val);
    ValFree(&s1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BigIntDiv") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: BigIntDiv() expects 2 arguments\n");
      return -1;
    }
    AKSValue s2_val = POP();
    AKSValue s1_val = POP();
    char *rem = NULL;
    char *q =
        BigIntDivC(ValToStrPtr(s1_val)->data, ValToStrPtr(s2_val)->data, &rem);

    AKSValue res_arr = ValArr(2);
    AKSArray *a_res = ValToArr(res_arr);
    a_res->count = 2;
    a_res->items[0] = ValStr(q);
    a_res->items[1] = ValStr(rem);
    PUSH(res_arr);

    free(q);
    free(rem);
    ValFree(&s2_val);
    ValFree(&s1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BigIntCmp") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: BigIntCmp() expects 2 arguments\n");
      return -1;
    }
    AKSValue s2_val = POP();
    AKSValue s1_val = POP();
    int r = BigIntCmpC(ValToStrPtr(s1_val)->data, ValToStrPtr(s2_val)->data);
    PUSH(ValInt(r));
    ValFree(&s2_val);
    ValFree(&s1_val);
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
      if (ValToArr(val)->invalidated) {
        fprintf(stderr, "Runtime error: double free detected\n");
        return -1;
      }
      ValToArr(val)->invalidated = 1;
      for (int i = 0; i < ValToArr(val)->count; i++) {
        ValFree(&ValToArr(val)->items[i]);
      }
      free(ValToArr(val)->items);
      ValToArr(val)->items = NULL;
      ValToArr(val)->count = 0;
      ValToArr(val)->capacity = 0;
    } else if (ValKind(val) == VALSTRUCT && ValToObj(val)) {
      if (ValToObj(val)->invalidated) {
        fprintf(stderr, "Runtime error: double free detected\n");
        return -1;
      }
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
  if (strcmp(name, "HuffmanDecode") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: HuffmanDecode() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: HuffmanDecode() argument must be a string\n");
      ValFree(&strval);
      return -1;
    }
    const char *data = ValToStrPtr(strval)->data;
    int len = ValToStrPtr(strval)->len;
    int dec_len = 0;
    char *dec_bytes = HuffmanDecodeStr(data, len, &dec_len);
    if (!dec_bytes) {
      PUSH(ValStr(""));
    } else {
      AKSValue res = ValStrLen(dec_bytes, dec_len);
      PUSH(res);
      free(dec_bytes);
    }
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DnsResolve") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: DnsResolve() expects 1 argument, got %d\n", argc);
      return -1;
    }
    AKSValue hostnameval = POP();
    if (ValKind(hostnameval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: DnsResolve() argument must be a string\n");
      ValFree(&hostnameval);
      return -1;
    }
    const char *hostname = ValToStrPtr(hostnameval)->data;
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo(hostname, NULL, &hints, &res);
    if (err != 0 || res == NULL) {
      PUSH(ValNull());
    } else {
      struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
      char ipstr[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(ipv4->sin_addr), ipstr, sizeof(ipstr));
      PUSH(ValStr(ipstr));
      freeaddrinfo(res);
    }
    ValFree(&hostnameval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TlsHandshake") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: TlsHandshake() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue is_server_val = POP();
    AKSValue hostname_val = POP();
    AKSValue sock_val = POP();

    int sock = (int)ValToInt(sock_val);
    const char *hostname = ValToStrPtr(hostname_val)->data;
    int is_server = ValToBool(is_server_val);

    int tls_id = CreateTLSContext((SOCKET)sock, is_server);
    if (tls_id < 0) {
      PUSH(ValInt(-1));
    } else {
      int hs_res = PerformTLSHandshake(tls_id, hostname);
      if (hs_res < 0) {
        TLSCloseContext(tls_id);
        PUSH(ValInt(-1));
      } else {
        PUSH(ValInt(tls_id));
      }
    }
    ValFree(&is_server_val);
    ValFree(&hostname_val);
    ValFree(&sock_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TlsSend") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: TlsSend() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue dataval = POP();
    AKSValue tls_id_val = POP();
    int tls_id = (int)ValToInt(tls_id_val);
    const char *data = ValToStrPtr(dataval)->data;
    int len = ValToStrPtr(dataval)->len;

    int sent = TLSSendData(tls_id, data, len);
    PUSH(ValInt(sent));
    ValFree(&dataval);
    ValFree(&tls_id_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TlsReceive") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: TlsReceive() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue maxval = POP();
    AKSValue tls_id_val = POP();
    int tls_id = (int)ValToInt(tls_id_val);
    int max_len = (int)ValToInt(maxval);

    char *buf = malloc(max_len + 1);
    int recvd = TLSRecvData(tls_id, buf, max_len);
    if (recvd <= 0) {
      PUSH(ValStr(""));
    } else {
      buf[recvd] = '\0';
      AKSValue strval = ValStr(buf);
      ValToStrPtr(strval)->len = recvd;
      PUSH(strval);
    }
    free(buf);
    ValFree(&maxval);
    ValFree(&tls_id_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TlsClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: TlsClose() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue tls_id_val = POP();
    int tls_id = (int)ValToInt(tls_id_val);
    TLSCloseContext(tls_id);
    ValFree(&tls_id_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "cancel") == 0) {
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
  if (strcmp(name, "IsCancelled") == 0) {
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
  if (strcmp(name, "SecureString") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SecureString() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr,
              "Runtime error: SecureString() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    AKSValue ss = ValStruct("SecureString");
    ValToObj(ss)->fieldcount = 1;
    ValToObj(ss)->fieldnames = malloc(sizeof(char *));
    ValToObj(ss)->fieldvals = malloc(sizeof(AKSValue));
    ValToObj(ss)->fieldnames[0] = strdup("data");
    ValToObj(ss)->fieldvals[0] = arg;
    PUSH(ss);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "CryptCompare") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: CryptCompare() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) != VALSTR || ValKind(b) != VALSTR) {
      fprintf(stderr,
              "Runtime error: CryptCompare() arguments must be strings\n");
      ValFree(&a);
      ValFree(&b);
      return -1;
    }
    int alen = ValToStrPtr(a)->len;
    int blen = ValToStrPtr(b)->len;
    int result = 0;
    if (alen != blen) {
      result = 1;
    }
    int len = alen < blen ? alen : blen;
    for (int i = 0; i < len; i++) {
      result |= ValToStrPtr(a)->data[i] ^ ValToStrPtr(b)->data[i];
    }
    PUSH(ValBool(result == 0 && alen == blen));
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConstantTimeCompare") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: ConstantTimeCompare() expects 2 arguments, got %d\n",
          argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) != VALSTR || ValKind(b) != VALSTR) {
      fprintf(
          stderr,
          "Runtime error: ConstantTimeCompare() arguments must be strings\n");
      ValFree(&a);
      ValFree(&b);
      return -1;
    }
    int alen = ValToStrPtr(a)->len;
    int blen = ValToStrPtr(b)->len;
    int result = 0;
    if (alen != blen) {
      result = 1;
    }
    int len = alen < blen ? alen : blen;
    for (int i = 0; i < len; i++) {
      result |= ValToStrPtr(a)->data[i] ^ ValToStrPtr(b)->data[i];
    }
    PUSH(ValBool(result == 0 && alen == blen));
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ShmOpen") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ShmOpen() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue sizeval = POP();
    AKSValue nameval = POP();
    if (ValKind(nameval) != VALSTR || ValKind(sizeval) != VALINT) {
      fprintf(stderr,
              "Runtime error: ShmOpen() arguments must be (string, int)\n");
      ValFree(&nameval);
      ValFree(&sizeval);
      return -1;
    }
    const char *shmname = ValToStrPtr(nameval)->data;
    int64_t shmsize = ValToInt(sizeval);

#ifdef _WIN32
    HANDLE hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)shmsize, shmname);
    if (hMapFile == NULL) {
      hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shmname);
    }
    if (hMapFile == NULL) {
      PUSH(ValNull());
      ValFree(&nameval);
      ValFree(&sizeval);
      *stacktopptr = stacktop;
      return 1;
    }
    void *pBuf = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, shmsize);
    if (pBuf == NULL) {
      CloseHandle(hMapFile);
      PUSH(ValNull());
      ValFree(&nameval);
      ValFree(&sizeval);
      *stacktopptr = stacktop;
      return 1;
    }

    AKSValue arrelems = ValArr(3);
    ValToArr(arrelems)->items[0] = ValInt((int64_t)pBuf);
    ValToArr(arrelems)->items[1] = ValInt((int64_t)hMapFile);
    ValToArr(arrelems)->items[2] = ValInt(shmsize);
    ValToArr(arrelems)->count = 3;

    PUSH(arrelems);
#else
    char shm_path[256];
    if (shmname[0] == '/') {
      strncpy(shm_path, shmname, sizeof(shm_path) - 1);
    } else {
      snprintf(shm_path, sizeof(shm_path), "/%s", shmname);
    }
    shm_path[sizeof(shm_path) - 1] = '\0';
    int fd = shm_open(shm_path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
      fd = shm_open(shm_path, O_RDWR, 0666);
    }
    if (fd < 0) {
      PUSH(ValNull());
      ValFree(&nameval);
      ValFree(&sizeval);
      *stacktopptr = stacktop;
      return 1;
    }
    ftruncate(fd, shmsize);
    void *pBuf = mmap(NULL, shmsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (pBuf == MAP_FAILED) {
      PUSH(ValNull());
      ValFree(&nameval);
      ValFree(&sizeval);
      *stacktopptr = stacktop;
      return 1;
    }

    AKSValue arrelems = ValArr(3);
    ValToArr(arrelems)->items[0] = ValInt((int64_t)(uintptr_t)pBuf);
    ValToArr(arrelems)->items[1] = ValInt(-1);
    ValToArr(arrelems)->items[2] = ValInt(shmsize);
    ValToArr(arrelems)->count = 3;

    PUSH(arrelems);
#endif
    ValFree(&nameval);
    ValFree(&sizeval);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ShmWrite") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: ShmWrite() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue dataval = POP();
    AKSValue offsetval = POP();
    AKSValue shmval = POP();

    if (ValKind(shmval) != VALARR || ValToArr(shmval)->count < 3 ||
        ValKind(offsetval) != VALINT || ValKind(dataval) != VALSTR) {
      fprintf(
          stderr,
          "Runtime error: ShmWrite() arguments must be (array, int, string)\n");
      ValFree(&shmval);
      ValFree(&offsetval);
      ValFree(&dataval);
      return -1;
    }

    void *pBuf = (void *)ValToInt(ValToArr(shmval)->items[0]);
    int64_t maxsize = ValToInt(ValToArr(shmval)->items[2]);
    int64_t offset = ValToInt(offsetval);
    const char *data = ValToStrPtr(dataval)->data;
    size_t len = ValToStrPtr(dataval)->len;

    if (offset + len > (size_t)maxsize) {
      len = (size_t)(maxsize - offset);
    }
    if (len > 0) {
      memcpy((char *)pBuf + offset, data, len);
    }

    PUSH(ValInt(len));
    ValFree(&shmval);
    ValFree(&offsetval);
    ValFree(&dataval);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ShmRead") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: ShmRead() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue sizeval = POP();
    AKSValue offsetval = POP();
    AKSValue shmval = POP();

    if (ValKind(shmval) != VALARR || ValToArr(shmval)->count < 3 ||
        ValKind(offsetval) != VALINT || ValKind(sizeval) != VALINT) {
      fprintf(stderr,
              "Runtime error: ShmRead() arguments must be (array, int, int)\n");
      ValFree(&shmval);
      ValFree(&offsetval);
      ValFree(&sizeval);
      return -1;
    }

    void *pBuf = (void *)ValToInt(ValToArr(shmval)->items[0]);
    int64_t maxsize = ValToInt(ValToArr(shmval)->items[2]);
    int64_t offset = ValToInt(offsetval);
    int64_t size = ValToInt(sizeval);

    if (offset + size > maxsize) {
      size = maxsize - offset;
    }
    if (size < 0)
      size = 0;

    char *buf = malloc(size + 1);
    if (size > 0) {
      memcpy(buf, (char *)pBuf + offset, size);
    }
    buf[size] = '\0';

    PUSH(ValStr(buf));
    free(buf);
    ValFree(&shmval);
    ValFree(&offsetval);
    ValFree(&sizeval);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ShmClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ShmClose() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue shmval = POP();
    if (ValKind(shmval) == VALARR && ValToArr(shmval)->count >= 3) {
      void *pBuf = (void *)(uintptr_t)ValToInt(ValToArr(shmval)->items[0]);
#ifdef _WIN32
      HANDLE hMapFile = (HANDLE)ValToInt(ValToArr(shmval)->items[1]);
      UnmapViewOfFile(pBuf);
      CloseHandle(hMapFile);
#else
      int64_t shmsize = ValToInt(ValToArr(shmval)->items[2]);
      munmap(pBuf, shmsize);
#endif
    }
    PUSH(ValNull());
    ValFree(&shmval);
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

  if (strcmp(name, "SimdAddRaw") == 0 || strcmp(name, "SimdMulRaw") == 0 ||
      strcmp(name, "SimdSubRaw") == 0 || strcmp(name, "SimdDivRaw") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s expects 2 array arguments, got %d\n",
              name, argc);
      return -1;
    }
    AKSValue arg2 = POP();
    AKSValue arg1 = POP();
    if (ValKind(arg1) != VALARR || ValKind(arg2) != VALARR) {
      fprintf(stderr, "Runtime error: %s expects arguments to be arrays\n",
              name);
      ValFree(&arg1);
      ValFree(&arg2);
      return -1;
    }
    if (ValToArr(arg1)->count != ValToArr(arg2)->count) {
      fprintf(stderr, "Runtime error: %s expects arrays of matching size\n",
              name);
      ValFree(&arg1);
      ValFree(&arg2);
      return -1;
    }
    int size = ValToArr(arg1)->count;
    AKSValue resultval = ValArr(size);
    ValToArr(resultval)->count = size;

    // =add, 1=mul, 2=sub, 3=div
    int simdop = 0;
    if (strcmp(name, "SimdMulRaw") == 0)
      simdop = 1;
    else if (strcmp(name, "SimdSubRaw") == 0)
      simdop = 2;
    else if (strcmp(name, "SimdDivRaw") == 0)
      simdop = 3;

    double *adata = malloc(size * sizeof(double));
    double *bdata = malloc(size * sizeof(double));
    double *cdata = malloc(size * sizeof(double));

    for (int i = 0; i < size; i++) {
      AKSValue va = ValToArr(arg1)->items[i];
      AKSValue vb = ValToArr(arg2)->items[i];
      adata[i] =
          ValKind(va) == VALFLOAT ? ValToFloat(va) : (double)ValToInt(va);
      bdata[i] =
          ValKind(vb) == VALFLOAT ? ValToFloat(vb) : (double)ValToInt(vb);
    }

    int avxlimit = (size / 4) * 4;
    for (int i = 0; i < avxlimit; i += 4) {
      __m256d va = _mm256_loadu_pd(&adata[i]);
      __m256d vb = _mm256_loadu_pd(&bdata[i]);
      __m256d vc;
      switch (simdop) {
      case 1:
        vc = _mm256_mul_pd(va, vb);
        break;
      case 2:
        vc = _mm256_sub_pd(va, vb);
        break;
      case 3:
        vc = _mm256_div_pd(va, vb);
        break;
      default:
        vc = _mm256_add_pd(va, vb);
        break;
      }
      _mm256_storeu_pd(&cdata[i], vc);
    }

    int sselimit = ((size - avxlimit) / 2) * 2 + avxlimit;
    for (int i = avxlimit; i < sselimit; i += 2) {
      __m128d va = _mm_loadu_pd(&adata[i]);
      __m128d vb = _mm_loadu_pd(&bdata[i]);
      __m128d vc;
      switch (simdop) {
      case 1:
        vc = _mm_mul_pd(va, vb);
        break;
      case 2:
        vc = _mm_sub_pd(va, vb);
        break;
      case 3:
        vc = _mm_div_pd(va, vb);
        break;
      default:
        vc = _mm_add_pd(va, vb);
        break;
      }
      _mm_storeu_pd(&cdata[i], vc);
    }

    for (int i = sselimit; i < size; i++) {
      switch (simdop) {
      case 1:
        cdata[i] = adata[i] * bdata[i];
        break;
      case 2:
        cdata[i] = adata[i] - bdata[i];
        break;
      case 3:
        cdata[i] = adata[i] / bdata[i];
        break;
      default:
        cdata[i] = adata[i] + bdata[i];
        break;
      }
    }

    for (int i = 0; i < size; i++) {
      ValToArr(resultval)->items[i] = ValFloat(cdata[i]);
    }

    free(adata);
    free(bdata);
    free(cdata);

    PUSH(resultval);
    ValFree(&arg1);
    ValFree(&arg2);
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

  if (strcmp(name, "html") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: html() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: html() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    const char *src = ValToStrPtr(arg)->data;
    size_t newlen = 0;
    for (int i = 0; src[i] != '\0'; i++) {
      if (src[i] == '<')
        newlen += 4;
      else if (src[i] == '>')
        newlen += 4;
      else if (src[i] == '&')
        newlen += 5;
      else if (src[i] == '"')
        newlen += 6;
      else if (src[i] == '\'')
        newlen += 6;
      else
        newlen++;
    }
    char *dest = malloc(newlen + 1);
    size_t d = 0;
    for (int i = 0; src[i] != '\0'; i++) {
      if (src[i] == '<') {
        strcpy(dest + d, "&lt;");
        d += 4;
      } else if (src[i] == '>') {
        strcpy(dest + d, "&gt;");
        d += 4;
      } else if (src[i] == '&') {
        strcpy(dest + d, "&amp;");
        d += 5;
      } else if (src[i] == '"') {
        strcpy(dest + d, "&quot;");
        d += 6;
      } else if (src[i] == '\'') {
        strcpy(dest + d, "&#x27;");
        d += 6;
      } else {
        dest[d++] = src[i];
      }
    }
    dest[d] = '\0';
    PUSH(ValStr(dest));
    free(dest);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "shell") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: shell() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: shell() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    const char *src = ValToStrPtr(arg)->data;
    size_t newlen = 0;
    for (int i = 0; src[i] != '\0'; i++) {
      char c = src[i];
      if (c == ';' || c == '&' || c == '|' || c == '$' || c == '`' ||
          c == '>' || c == '<' || c == '\n' || c == '\r' || c == '\'' ||
          c == '"') {
        continue;
      }
      newlen++;
    }
    char *dest = malloc(newlen + 1);
    size_t d = 0;
    for (int i = 0; src[i] != '\0'; i++) {
      char c = src[i];
      if (c == ';' || c == '&' || c == '|' || c == '$' || c == '`' ||
          c == '>' || c == '<' || c == '\n' || c == '\r' || c == '\'' ||
          c == '"') {
        continue;
      }
      dest[d++] = c;
    }
    dest[d] = '\0';
    PUSH(ValStr(dest));
    free(dest);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "path") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: path() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: path() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    const char *src = ValToStrPtr(arg)->data;
    char *temp = strdup(src);
    char *p;
    while ((p = strstr(temp, ".."))) {
      memmove(p, p + 2, strlen(p + 2) + 1);
    }
    char *start = temp;
    while (*start == '/' || *start == '\\') {
      start++;
    }
    if (isalpha((unsigned char)start[0]) && start[1] == ':') {
      start += 2;
      while (*start == '/' || *start == '\\') {
        start++;
      }
    }
    PUSH(ValStr(start));
    free(temp);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "ClipboardSet") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ClipboardSet() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr,
              "Runtime error: ClipboardSet() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    SysClipSetText(ValToStrPtr(arg)->data);
    ValFree(&arg);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ClipboardGet") == 0) {
    char *txt = SysClipGetText();
    PUSH(ValStr(txt));
    free(txt);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "notify") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: notify() expects 2 arguments\n");
      return -1;
    }
    AKSValue msg = POP();
    AKSValue title = POP();
    if (ValKind(title) != VALSTR || ValKind(msg) != VALSTR) {
      fprintf(stderr, "Runtime error: notify() arguments must be strings\n");
      ValFree(&title);
      ValFree(&msg);
      return -1;
    }
    SysNotifyShow(ValToStrPtr(title)->data, ValToStrPtr(msg)->data);
    ValFree(&title);
    ValFree(&msg);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TrayAdd") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: TrayAdd() expects 2 arguments\n");
      return -1;
    }
    AKSValue tooltip = POP();
    AKSValue icon = POP();
    if (ValKind(icon) != VALSTR || ValKind(tooltip) != VALSTR) {
      fprintf(stderr, "Runtime error: TrayAdd() arguments must be strings\n");
      ValFree(&icon);
      ValFree(&tooltip);
      return -1;
    }
    SysTrayAdd(ValToStrPtr(icon)->data, ValToStrPtr(tooltip)->data);
    ValFree(&icon);
    ValFree(&tooltip);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TrayRemove") == 0) {
    SysTrayRemove();
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "win") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: win() expects 3 arguments\n");
      return -1;
    }
    AKSValue hval = POP();
    AKSValue wval = POP();
    AKSValue title = POP();
    if (ValKind(title) != VALSTR || ValKind(wval) != VALINT ||
        ValKind(hval) != VALINT) {
      fprintf(stderr, "Runtime error: win() invalid argument types\n");
      ValFree(&title);
      ValFree(&wval);
      ValFree(&hval);
      return -1;
    }
    AKSValue win = SysGUINewWin(ValToStrPtr(title)->data, (int)ValToInt(wval),
                                (int)ValToInt(hval));
    ValFree(&title);
    ValFree(&wval);
    ValFree(&hval);
    PUSH(win);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Button") == 0) {
    if (argc != 6) {
      fprintf(stderr, "Runtime error: Button() expects 6 arguments\n");
      return -1;
    }
    AKSValue hval = POP();
    AKSValue wval = POP();
    AKSValue yval = POP();
    AKSValue xval = POP();
    AKSValue text = POP();
    AKSValue parent = POP();
    if (ValKind(text) != VALSTR || ValKind(parent) != VALINT) {
      fprintf(stderr, "Runtime error: Button() invalid types\n");
      ValFree(&parent);
      ValFree(&text);
      ValFree(&xval);
      ValFree(&yval);
      ValFree(&wval);
      ValFree(&hval);
      return -1;
    }
    AKSValue btn = SysGUINewBtn(parent, ValToStrPtr(text)->data,
                                (int)ValToInt(xval), (int)ValToInt(yval),
                                (int)ValToInt(wval), (int)ValToInt(hval));
    ValFree(&parent);
    ValFree(&text);
    ValFree(&xval);
    ValFree(&yval);
    ValFree(&wval);
    ValFree(&hval);
    PUSH(btn);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MessageBox") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MessageBox() expects 2 arguments\n");
      return -1;
    }
    AKSValue msg = POP();
    AKSValue title = POP();
    if (ValKind(title) != VALSTR || ValKind(msg) != VALSTR) {
      fprintf(stderr,
              "Runtime error: MessageBox() arguments must be strings\n");
      ValFree(&title);
      ValFree(&msg);
      return -1;
    }
    SysGUIMsgBox(ValToStrPtr(title)->data, ValToStrPtr(msg)->data);
    ValFree(&title);
    ValFree(&msg);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "loop") == 0) {
    int res = SysGUIMsgLoop();
    PUSH(ValInt(res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SdlInit") == 0) {
    int res = SysSDLInit();
    PUSH(ValBool(res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SdlWin") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SdlWin() expects 3 arguments\n");
      return -1;
    }
    AKSValue hval = POP();
    AKSValue wval = POP();
    AKSValue title = POP();
    AKSValue win = SysSDLNewWin(ValToStrPtr(title)->data, (int)ValToInt(wval),
                                (int)ValToInt(hval));
    ValFree(&title);
    ValFree(&wval);
    ValFree(&hval);
    PUSH(win);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SdlClear") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SdlClear() expects 3 arguments\n");
      return -1;
    }
    AKSValue bval = POP();
    AKSValue gval = POP();
    AKSValue rval = POP();
    SysSDLClear((int)ValToInt(rval), (int)ValToInt(gval), (int)ValToInt(bval));
    ValFree(&rval);
    ValFree(&gval);
    ValFree(&bval);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SdlRect") == 0) {
    if (argc != 7) {
      fprintf(stderr, "Runtime error: SdlRect() expects 7 arguments\n");
      return -1;
    }
    AKSValue bval = POP();
    AKSValue gval = POP();
    AKSValue rval = POP();
    AKSValue hval = POP();
    AKSValue wval = POP();
    AKSValue yval = POP();
    AKSValue xval = POP();
    SysSDLDrawRect((int)ValToInt(xval), (int)ValToInt(yval),
                   (int)ValToInt(wval), (int)ValToInt(hval),
                   (int)ValToInt(rval), (int)ValToInt(gval),
                   (int)ValToInt(bval));
    ValFree(&xval);
    ValFree(&yval);
    ValFree(&wval);
    ValFree(&hval);
    ValFree(&rval);
    ValFree(&gval);
    ValFree(&bval);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SdlCircle") == 0) {
    if (argc != 6) {
      fprintf(stderr, "Runtime error: SdlCircle() expects 6 arguments\n");
      return -1;
    }
    AKSValue bval = POP();
    AKSValue gval = POP();
    AKSValue rval = POP();
    AKSValue radval = POP();
    AKSValue yval = POP();
    AKSValue xval = POP();
    SysSDLDrawCircle((int)ValToInt(xval), (int)ValToInt(yval),
                     (int)ValToInt(radval), (int)ValToInt(rval),
                     (int)ValToInt(gval), (int)ValToInt(bval));
    ValFree(&xval);
    ValFree(&yval);
    ValFree(&radval);
    ValFree(&rval);
    ValFree(&gval);
    ValFree(&bval);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SdlEvent") == 0) {
    int res = SysSDLPollEvent();
    PUSH(ValInt(res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GuiDrag") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: GuiDrag() expects 1 argument\n");
      return -1;
    }
    AKSValue win = POP();
    SysGUIEnableDragDrop(win);
    ValFree(&win);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GuiDrop") == 0) {
    char *files = SysGUIGetDroppedFiles();
    PUSH(ValStr(files));
    free(files);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DomGet") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: DomGet() expects 1 argument\n");
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: DomGet() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    AKSValue el = SysDOMGetElementByID(ValToStrPtr(arg)->data);
    ValFree(&arg);
    PUSH(el);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DomSet") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: DomSet() expects 2 arguments\n");
      return -1;
    }
    AKSValue html = POP();
    AKSValue el = POP();
    if (ValKind(html) != VALSTR) {
      fprintf(stderr,
              "Runtime error: DomSet() second argument must be a string\n");
      ValFree(&el);
      ValFree(&html);
      return -1;
    }
    SysDOMSetInnerHtml(el, ValToStrPtr(html)->data);
    ValFree(&el);
    ValFree(&html);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

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

  if (strcmp(name, "slice") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: slice expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue endval = POP();
    AKSValue startval = POP();
    AKSValue arrval = POP();
    if (ValKind(arrval) != VALARR) {
      fprintf(stderr,
              "Runtime error: slice expects first argument to be an array\n");
      ValFree(&arrval);
      ValFree(&startval);
      ValFree(&endval);
      return -1;
    }
    if (ValKind(startval) != VALINT || ValKind(endval) != VALINT) {
      fprintf(stderr, "Runtime error: slice expects start and end indices to "
                      "be integers\n");
      ValFree(&arrval);
      ValFree(&startval);
      ValFree(&endval);
      return -1;
    }
    AKSArray *parentarr = ValToArr(arrval);
    int64_t start = ValToInt(startval);
    int64_t end = ValToInt(endval);
    if (start < 0)
      start = 0;
    if (end < 0)
      end = 0;
    if (start > parentarr->count)
      start = parentarr->count;
    if (end > parentarr->count)
      end = parentarr->count;
    if (start > end)
      start = end;
    int length = (int)(end - start);
    AKSArray *slicearr = malloc(sizeof(AKSArray));
    slicearr->refcount = 1;
    slicearr->invalidated = 0;
    slicearr->isstack = 0;
    slicearr->isslice = 1;
    slicearr->parent = ValCopy(arrval);
    slicearr->items = parentarr->items + start;
    slicearr->count = length;
    slicearr->capacity = length;
    AtomicIncrement(&gactivearrs);
    AKSValue sliceval = ((uint64_t)0xFFF5 << 48) |
                        ((uintptr_t)slicearr & 0x0000FFFFFFFFFFFFULL);
    PUSH(sliceval);
    ValFree(&arrval);
    ValFree(&startval);
    ValFree(&endval);
    *stacktopptr = stacktop;
    return 1;
  }

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
  if (strcmp(name, "SquareRoot") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SquareRoot() expects 1 argument, got %d\n", argc);
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
  if (strcmp(name, "Absolute") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Absolute() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) == VALINT) {
      PUSH(ValInt(ValToInt(arg) < 0 ? -ValToInt(arg) : ValToInt(arg)));
    } else if (ValKind(arg) == VALFLOAT) {
      PUSH(ValFloat(ValToFloat(arg) < 0 ? -ValToFloat(arg) : ValToFloat(arg)));
    } else {
      PUSH(ValCopy(arg));
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "String") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: String() expects 1 argument, got %d\n",
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
  // eval() compiles and executes AKS code from a string (code generation from
  // strings, Feature 48: codegen, string codegen)
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
      strncpy(newproto->name, vmfn->name, 127);
      strncpy(newproto->file_name, vmfn->file_name,
              sizeof(newproto->file_name) - 1);
      newproto->file_name[sizeof(newproto->file_name) - 1] = '\0';
      newproto->id = vmfn->id;
      newproto->paramcount = vmfn->paramcount;
      newproto->localcount = vmfn->localcount;
      newproto->scopedepth = vmfn->scopedepth;
      newproto->isasync = vmfn->isasync;
      strncpy(newproto->rettype, vmfn->rettype, 63);
      for (int p = 0; p < vmfn->paramcount; p++) {
        strncpy(newproto->paramtypes[p], vmfn->paramtypes[p], 63);
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
    if (a->count >= a->capacity) {
      a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
      a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
    }
    a->items[a->count++] = ValCopy(val);
    PUSH(ValNull());
    ValFree(&val);
    ValFree(&arr);
    *stacktopptr = stacktop;
    return 1;
  }
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
  if (strcmp(name, "sqrt") == 0 || strcmp(name, "Sqrt") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: sqrt() expects 1 argument, got %d\n", argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald = ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(sqrt(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
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
  }
  if (strcmp(name, "cos") == 0) {
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
  }
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
  }
  if (strcmp(name, "log") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: log() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(log(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Ceiling") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Ceiling() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValInt((int64_t)ceil(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "floor") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: floor() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValInt((int64_t)floor(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Minimum") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: Minimum() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      PUSH(ValInt(ValToInt(a) < ValToInt(b) ? ValToInt(a) : ValToInt(b)));
    } else {
      double da = ValKind(a) == VALFLOAT ? ValToFloat(a) : (double)ValToInt(a);
      double db = ValKind(b) == VALFLOAT ? ValToFloat(b) : (double)ValToInt(b);
      PUSH(ValFloat(da < db ? da : db));
    }
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Maximum") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: Maximum() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      PUSH(ValInt(ValToInt(a) > ValToInt(b) ? ValToInt(a) : ValToInt(b)));
    } else {
      double da = ValKind(a) == VALFLOAT ? ValToFloat(a) : (double)ValToInt(a);
      double db = ValKind(b) == VALFLOAT ? ValToFloat(b) : (double)ValToInt(b);
      PUSH(ValFloat(da > db ? da : db));
    }
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "pop") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: pop() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arr = POP();
    if (ValKind(arr) != VALARR) {
      fprintf(stderr, "Runtime error: pop() argument must be an array\n");
      ValFree(&arr);
      return -1;
    }
    if (ValToArr(arr)->count == 0) {
      PUSH(ValNull());
    } else {
      AKSValue popped = ValToArr(arr)->items[ValToArr(arr)->count - 1];
      ValToArr(arr)->count--;
      PUSH(popped);
    }
    ValFree(&arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "insert") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: insert() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue val = POP();
    AKSValue idxval = POP();
    AKSValue arr = POP();
    if (ValKind(arr) != VALARR || ValKind(idxval) != VALINT) {
      fprintf(stderr, "Runtime error: insert() invalid argument types\n");
      ValFree(&val);
      ValFree(&idxval);
      ValFree(&arr);
      return -1;
    }
    int64_t idx = ValToInt(idxval);
    AKSArray *a = ValToArr(arr);
    if (idx < 0 || idx > a->count) {
      fprintf(stderr, "Runtime error: insert() index out of bounds: %lld\n",
              (long long)idx);
      ValFree(&val);
      ValFree(&idxval);
      ValFree(&arr);
      return -1;
    }
    if (a->count >= a->capacity) {
      a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
      a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
    }
    for (int i = a->count; i > idx; i--) {
      a->items[i] = a->items[i - 1];
    }
    a->items[idx] = ValCopy(val);
    a->count++;
    PUSH(ValNull());
    ValFree(&val);
    ValFree(&idxval);
    ValFree(&arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "remove") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: remove() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue idxval = POP();
    AKSValue arr = POP();
    if (ValKind(arr) != VALARR || ValKind(idxval) != VALINT) {
      fprintf(stderr, "Runtime error: remove() invalid argument types\n");
      ValFree(&idxval);
      ValFree(&arr);
      return -1;
    }
    int64_t idx = ValToInt(idxval);
    AKSArray *a = ValToArr(arr);
    if (idx < 0 || idx >= a->count) {
      fprintf(stderr, "Runtime error: remove() index out of bounds: %lld\n",
              (long long)idx);
      ValFree(&idxval);
      ValFree(&arr);
      return -1;
    }
    AKSValue removed = a->items[idx];
    for (int i = (int)idx; i < a->count - 1; i++) {
      a->items[i] = a->items[i + 1];
    }
    a->count--;
    PUSH(removed);
    ValFree(&idxval);
    ValFree(&arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "slice") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: slice() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue endval = POP();
    AKSValue startval = POP();
    AKSValue arr = POP();
    if (ValKind(arr) != VALARR || ValKind(startval) != VALINT ||
        ValKind(endval) != VALINT) {
      fprintf(stderr, "Runtime error: slice() invalid argument types\n");
      ValFree(&endval);
      ValFree(&startval);
      ValFree(&arr);
      return -1;
    }
    int64_t start = ValToInt(startval);
    int64_t end = ValToInt(endval);
    int64_t count = ValToArr(arr)->count;
    if (start < 0)
      start += count;
    if (end < 0)
      end += count;
    if (start < 0)
      start = 0;
    if (end < 0)
      end = 0;
    if (start > count)
      start = count;
    if (end > count)
      end = count;

    int slicelen = (int)(end - start);
    if (slicelen < 0)
      slicelen = 0;

    AKSValue res = ValArr(slicelen);
    ValToArr(res)->count = slicelen;
    for (int i = 0; i < slicelen; i++) {
      ValToArr(res)->items[i] = ValCopy(ValToArr(arr)->items[start + i]);
    }
    PUSH(res);
    ValFree(&endval);
    ValFree(&startval);
    ValFree(&arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MakeArena") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: MakeArena() expects 0 arguments, got %d\n", argc);
      return -1;
    }
    AKSArena *arena = malloc(sizeof(AKSArena));
    arena->refcount = 1;
    arena->allocations = NULL;
    arena->alloc_types = NULL;
    arena->count = 0;
    arena->capacity = 0;
    arena->invalidated = 0;

    PUSH(ValArena(arena));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ArenaAllocArray") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: ArenaAllocArray() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue cap_val = POP();
    AKSValue arena_val = POP();
    if (ValKind(arena_val) != VALARENA || ValKind(cap_val) != VALINT) {
      fprintf(stderr,
              "Runtime error: ArenaAllocArray() invalid argument types\n");
      ValFree(&arena_val);
      ValFree(&cap_val);
      return -1;
    }
    AKSArena *arena = ValToArena(arena_val);
    if (arena->invalidated) {
      fprintf(stderr,
              "Runtime error: ArenaAllocArray() called on freed arena\n");
      ValFree(&arena_val);
      ValFree(&cap_val);
      return -1;
    }
    int cap = (int)ValToInt(cap_val);
    AKSValue res = ValArrArena(cap, arena);
    AKSArray *arr = ValToArr(res);
    arr->count = cap;
    for (int i = 0; i < cap; i++) {
      arr->items[i] = ValNull();
    }

    if (arena->count >= arena->capacity) {
      arena->capacity = arena->capacity == 0 ? 8 : arena->capacity * 2;
      arena->allocations =
          realloc(arena->allocations, arena->capacity * sizeof(void *));
      arena->alloc_types =
          realloc(arena->alloc_types, arena->capacity * sizeof(int));
    }
    arena->allocations[arena->count] = ValToArr(res);
    arena->alloc_types[arena->count] = 1;
    arena->count++;

    PUSH(res);
    ValFree(&arena_val);
    ValFree(&cap_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ArenaAllocStruct") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: ArenaAllocStruct() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue type_val = POP();
    AKSValue arena_val = POP();
    if (ValKind(arena_val) != VALARENA || ValKind(type_val) != VALSTR) {
      fprintf(stderr,
              "Runtime error: ArenaAllocStruct() invalid argument types\n");
      ValFree(&arena_val);
      ValFree(&type_val);
      return -1;
    }
    AKSArena *arena = ValToArena(arena_val);
    if (arena->invalidated) {
      fprintf(stderr,
              "Runtime error: ArenaAllocStruct() called on freed arena\n");
      ValFree(&arena_val);
      ValFree(&type_val);
      return -1;
    }
    const char *tname = ValToStrPtr(type_val)->data;
    AKSValue res = ValStructArena(tname, arena);

    if (arena->count >= arena->capacity) {
      arena->capacity = arena->capacity == 0 ? 8 : arena->capacity * 2;
      arena->allocations =
          realloc(arena->allocations, arena->capacity * sizeof(void *));
      arena->alloc_types =
          realloc(arena->alloc_types, arena->capacity * sizeof(int));
    }
    arena->allocations[arena->count] = ValToObj(res);
    arena->alloc_types[arena->count] = 2;
    arena->count++;

    PUSH(res);
    ValFree(&arena_val);
    ValFree(&type_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ArenaFree") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ArenaFree() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arena_val = POP();
    if (ValKind(arena_val) != VALARENA) {
      fprintf(stderr, "Runtime error: ArenaFree() argument must be an arena\n");
      ValFree(&arena_val);
      return -1;
    }
    AKSArena *arena = ValToArena(arena_val);
    if (arena && !arena->invalidated) {
      for (int i = 0; i < arena->count; i++) {
        void *ptr = arena->allocations[i];
        int type = arena->alloc_types[i];
        if (type == 1) {
          AKSArray *arr = (AKSArray *)ptr;
          if (!arr->invalidated) {
            // Free item contents so held references are released
            for (int j = 0; j < arr->count; j++)
              ValFree(&arr->items[j]);
            free(arr->items);
            arr->items = NULL;
            arr->count = 0;
            arr->capacity = 0;
            // Mark invalidated but do NOT free the header and do NOT
            // touch gactivearrs - valfree() will do both when the
            // last AKSValue handle goes out of scope.
            arr->invalidated = 1;
          }
        } else if (type == 2) {
          AKSStruct *obj = (AKSStruct *)ptr;
          if (!obj->invalidated) {
            for (int j = 0; j < obj->fieldcount; j++) {
              free(obj->fieldnames[j]);
              ValFree(&obj->fieldvals[j]);
            }
            free(obj->fieldnames);
            free(obj->fieldvals);
            free(obj->typename);
            obj->fieldnames = NULL;
            obj->fieldvals = NULL;
            obj->typename = NULL;
            obj->fieldcount = 0;
            // Mark invalidated but keep header alive for UAF detection.
            // Do NOT touch gactivestructs - valfree() handles it.
            obj->invalidated = 1;
          }
        }
      }
      if (arena->allocations) {
        free(arena->allocations);
        arena->allocations = NULL;
      }
      if (arena->alloc_types) {
        free(arena->alloc_types);
        arena->alloc_types = NULL;
      }
      arena->count = 0;
      arena->capacity = 0;
      arena->invalidated = 1;
    }
    PUSH(ValNull());
    ValFree(&arena_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Ordinal") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Ordinal() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: Ordinal() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    int code = ValToStrPtr(arg)->len > 0
                   ? (unsigned char)ValToStrPtr(arg)->data[0]
                   : -1;
    PUSH(ValInt(code));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Character") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Character() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALINT) {
      fprintf(stderr,
              "Runtime error: Character() argument must be an integer\n");
      ValFree(&arg);
      return -1;
    }
    char buf[1] = {(char)ValToInt(arg)};
    PUSH(ValStrLen(buf, 1));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Substring") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: Substring() expects 3 arguments, got %d\n", argc);
      return -1;
    }
    AKSValue lenval = POP();
    AKSValue startval = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(startval) != VALINT ||
        ValKind(lenval) != VALINT) {
      fprintf(stderr, "Runtime error: Substring() invalid argument types\n");
      ValFree(&lenval);
      ValFree(&startval);
      ValFree(&strval);
      return -1;
    }
    int64_t start = ValToInt(startval);
    int64_t length = ValToInt(lenval);
    int64_t count = ValToStrPtr(strval)->len;
    if (start < 0)
      start += count;
    if (start < 0)
      start = 0;
    if (start > count)
      start = count;
    if (length < 0)
      length = 0;
    if (start + length > count)
      length = count - start;

    char *sub = malloc(length + 1);
    memcpy(sub, ValToStrPtr(strval)->data + start, length);
    sub[length] = '\0';
    PUSH(ValStrLen(sub, (int)length));
    free(sub);
    ValFree(&lenval);
    ValFree(&startval);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "type") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: type() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    PUSH(ValStr(ValTypeName(ValKind(arg))));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WriteFile") == 0) {
    AKSValue cap = ValNull();
    if (argc == 3) {
      cap = POP();
    } else if (argc != 2) {
      fprintf(stderr,
              "Runtime error: WriteFile() expects 2 or 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue content = POP();
    AKSValue path = POP();
    if (ValKind(path) != VALSTR || (ValKind(content) != VALSTR && ValKind(content) != VALARR)) {
      fprintf(stderr, "Runtime error: WriteFile() arguments must be string or byte array\n");
      ValFree(&content);
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (vm->inrestrictedeval) {
      ValFree(&content);
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMWRITE)) {
      fprintf(stderr, "Security error: write permission denied\n");
      ValFree(&content);
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(path)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&content);
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "fs", "write", ValToStrPtr(path)->data)) {
      fprintf(stderr, "Security error: invalid write capability for path\n");
      ValFree(&content);
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    FILE *f = fopen(ValToStrPtr(path)->data, "wb");
    int success = 0;
    if (f) {
      if (ValKind(content) == VALSTR) {
        fwrite(ValToStrPtr(content)->data, 1, ValToStrPtr(content)->len, f);
      } else if (ValKind(content) == VALARR) {
        AKSArray *a = ValToArr(content);
        for (int i = 0; i < a->count; i++) {
          uint8_t b = (uint8_t)ValToInt(a->items[i]);
          fputc(b, f);
        }
      }
      fclose(f);
      success = 1;
    }
    PUSH(ValBool(success));
    ValFree(&content);
    ValFree(&path);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ReadFileAsync") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ReadFileAsync() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue pathval = POP();
    if (ValKind(pathval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: ReadFileAsync() argument must be a string\n");
      ValFree(&pathval);
      return -1;
    }
    if (!(vm->permissions & PERMREAD)) {
      fprintf(stderr, "Security error: read permission denied\n");
      ValFree(&pathval);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(pathval)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&pathval);
      return -1;
    }
    const char *path = ValToStrPtr(pathval)->data;

    VMTask *t = CreateAsyncPendingTask(vm);

#ifdef _WIN32
    HANDLE hFile =
        CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
      t->result = ValNull();
      t->state = 3;
      ValFree(&pathval);
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }

    CreateIoCompletionPort(hFile, vm->iocp, (ULONG_PTR)hFile, 0);

    LARGE_INTEGER size;
    GetFileSizeEx(hFile, &size);
    int file_size = (int)size.QuadPart;

    char *buf = malloc(file_size + 1);

    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = hFile;
    ctx->buffer = buf;
    ctx->op_type = 0; // read

    BOOL read_res = ReadFile(hFile, buf, file_size, NULL, &ctx->overlapped);
    if (!read_res && GetLastError() != ERROR_IO_PENDING) {
      CloseHandle(hFile);
      free(buf);
      free(ctx);
      t->result = ValNull();
      t->state = 3;
    }
#else
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
      t->result = ValNull();
      t->state = 3;
      ValFree(&pathval);
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }
    struct stat st;
    fstat(fd, &st);
    int file_size = st.st_size;
    char *buf = malloc(file_size + 1);

#ifdef AKS_USE_IO_URING
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)fd;
    ctx->buffer = buf;
    ctx->op_type = 0;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_read(sqe, fd, buf, file_size, 0);
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#else
    int bytes_read = read(fd, buf, file_size);
    if (bytes_read >= 0) {
      buf[bytes_read] = '\0';
      t->result = ValStrLen(buf, bytes_read);
    } else {
      t->result = ValNull();
    }
    t->state = 3;
    free(buf);
    close(fd);
#endif
#endif

    ValFree(&pathval);
    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WriteFileAsync") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: WriteFileAsync() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue contentval = POP();
    AKSValue pathval = POP();
    if (ValKind(pathval) != VALSTR || ValKind(contentval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: WriteFileAsync() arguments must be strings\n");
      ValFree(&pathval);
      ValFree(&contentval);
      return -1;
    }
    if (vm->inrestrictedeval) {
      ValFree(&pathval);
      ValFree(&contentval);
      return -1;
    }
    if (!(vm->permissions & PERMWRITE)) {
      fprintf(stderr, "Security error: write permission denied\n");
      ValFree(&pathval);
      ValFree(&contentval);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(pathval)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&pathval);
      ValFree(&contentval);
      return -1;
    }
    const char *path = ValToStrPtr(pathval)->data;
    const char *content = ValToStrPtr(contentval)->data;
    int len = ValToStrPtr(contentval)->len;

    VMTask *t = CreateAsyncPendingTask(vm);

#ifdef _WIN32
    HANDLE hFile =
        CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
      t->result = ValBool(0);
      t->state = 3;
      ValFree(&pathval);
      ValFree(&contentval);
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }

    CreateIoCompletionPort(hFile, vm->iocp, (ULONG_PTR)hFile, 0);

    char *buf = malloc(len + 1);
    memcpy(buf, content, len);
    buf[len] = '\0';

    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = hFile;
    ctx->buffer = buf;
    ctx->op_type = 1; // write

    BOOL write_res = WriteFile(hFile, buf, len, NULL, &ctx->overlapped);
    if (!write_res && GetLastError() != ERROR_IO_PENDING) {
      CloseHandle(hFile);
      free(buf);
      free(ctx);
      t->result = ValBool(0);
      t->state = 3;
    }
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
      t->result = ValBool(0);
      t->state = 3;
      ValFree(&pathval);
      ValFree(&contentval);
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }
    char *buf = malloc(len + 1);
    memcpy(buf, content, len);
    buf[len] = '\0';

#ifdef AKS_USE_IO_URING
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)fd;
    ctx->buffer = buf;
    ctx->op_type = 1;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_write(sqe, fd, buf, len, 0);
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#else
    int bytes_written = write(fd, buf, len);
    if (bytes_written >= 0) {
      t->result = ValBool(1);
    } else {
      t->result = ValBool(0);
    }
    t->state = 3;
    free(buf);
    close(fd);
#endif
#endif

    ValFree(&pathval);
    ValFree(&contentval);
    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketListen") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SocketListen() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue portval = POP();
    int port = (int)ValToInt(portval);
    ValFree(&portval);

#ifdef _WIN32
    SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                         WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) {
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
      closesocket(s);
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
      closesocket(s);
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    CreateIoCompletionPort((HANDLE)s, vm->iocp, (ULONG_PTR)s, 0);
    PUSH(ValInt((int64_t)s));
#else
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(s, 128) < 0) {
      close(s);
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    PUSH(ValInt(s));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketAccept") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SocketAccept() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue sockval = POP();
    int64_t listen_s = ValToInt(sockval);
    ValFree(&sockval);

    VMTask *t = CreateAsyncPendingTask(vm);

    if (listen_s < 0) {
      t->result = ValInt(-1);
      t->state = 3; // completed
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }

#ifdef _WIN32
    SOCKET client_s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                                WSA_FLAG_OVERLAPPED);

    GUID GuidAcceptEx = WSAID_ACCEPTEX;
    LPFN_ACCEPTEX lpfnAcceptEx = NULL;
    DWORD dwBytes = 0;
    int ioctl_res =
        WSAIoctl((SOCKET)listen_s, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &GuidAcceptEx, sizeof(GuidAcceptEx), &lpfnAcceptEx,
                 sizeof(lpfnAcceptEx), &dwBytes, NULL, NULL);

    if (ioctl_res != 0 || !lpfnAcceptEx) {
      if (client_s != INVALID_SOCKET)
        closesocket(client_s);
      t->result = ValInt(-1);
      t->state = 3;
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }

    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)listen_s;
    ctx->buffer = malloc(1024); // buffer for addresses
    ctx->op_type = 4;           // accept
    ctx->extra = (void *)(uintptr_t)client_s;

    BOOL res = lpfnAcceptEx((SOCKET)listen_s, client_s, ctx->buffer, 0,
                            sizeof(struct sockaddr_in) + 16,
                            sizeof(struct sockaddr_in) + 16, &dwBytes,
                            &ctx->overlapped);
    if (!res && GetLastError() != ERROR_IO_PENDING) {
      closesocket(client_s);
      free(ctx->buffer);
      free(ctx);
      t->result = ValInt(-1);
      t->state = 3;
    }
#else
#ifdef AKS_USE_IO_URING
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)listen_s;
    ctx->op_type = 4;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_accept(sqe, (int)listen_s, NULL, NULL, 0);
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#else
    int client_fd = accept((int)listen_s, NULL, NULL);
    if (client_fd >= 0) {
      t->result = ValInt(client_fd);
    } else {
      t->result = ValInt(-1);
    }
    t->state = 3;
#endif
#endif

    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketConnect") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: SocketConnect() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue portval = POP();
    AKSValue hostval = POP();
    int port = (int)ValToInt(portval);
    const char *host = (ValKind(hostval) == VALSTR) ? ValToStrPtr(hostval)->data : "127.0.0.1";
    VMTask *t = CreateAsyncPendingTask(vm);

#ifdef _WIN32
    SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (s != INVALID_SOCKET) {
      struct sockaddr_in sin;
      sin.sin_family = AF_INET;
      sin.sin_port = htons((u_short)port);
      sin.sin_addr.s_addr = inet_addr(host);
      if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
        CreateIoCompletionPort((HANDLE)s, vm->iocp, (ULONG_PTR)s, 0);
        t->result = ValInt((int64_t)s);
      } else {
        closesocket(s);
        t->result = ValInt(-1);
      }
    } else {
      t->result = ValInt(-1);
    }
    t->state = 3;
#else
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET) {
      struct sockaddr_in sin;
      sin.sin_family = AF_INET;
      sin.sin_port = htons((u_short)port);
      sin.sin_addr.s_addr = inet_addr(host);
      if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
        t->result = ValInt((int64_t)s);
      } else {
        close((int)s);
        t->result = ValInt(-1);
      }
    } else {
      t->result = ValInt(-1);
    }
    t->state = 3;
#endif

    ValFree(&hostval);
    ValFree(&portval);
    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketReceive") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: SocketReceive() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue maxval = POP();
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    int max_bytes = (int)ValToInt(maxval);
    ValFree(&maxval);
    ValFree(&sockval);

    VMTask *t = CreateAsyncPendingTask(vm);
    char *buf = malloc(max_bytes + 1);

#ifdef _WIN32
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)sock;
    ctx->buffer = buf;
    ctx->op_type = 2; // recv

    WSABUF wsabuf;
    wsabuf.buf = buf;
    wsabuf.len = max_bytes;
    DWORD flags = 0;
    int res =
        WSARecv((SOCKET)sock, &wsabuf, 1, NULL, &flags, &ctx->overlapped, NULL);
    if (res == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      free(buf);
      free(ctx);
      t->result = ValStr("");
      t->state = 3;
    }
#else
#ifdef AKS_USE_IO_URING
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)sock;
    ctx->buffer = buf;
    ctx->op_type = 2;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_recv(sqe, (int)sock, buf, max_bytes, 0);
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#else
    int bytes_recv = recv((int)sock, buf, max_bytes, 0);
    if (bytes_recv >= 0) {
      buf[bytes_recv] = '\0';
      t->result = ValStrLen(buf, bytes_recv);
    } else {
      t->result = ValStr("");
    }
    t->state = 3;
    free(buf);
#endif
#endif

    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketSend") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: SocketSend() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    const char *str = ValToStrPtr(strval)->data;
    int len = ValToStrPtr(strval)->len;

    VMTask *t = CreateAsyncPendingTask(vm);
    char *buf = malloc(len + 1);
    memcpy(buf, str, len);
    buf[len] = '\0';

#ifdef _WIN32
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)sock;
    ctx->buffer = buf;
    ctx->op_type = 3; // send

    WSABUF wsabuf;
    wsabuf.buf = buf;
    wsabuf.len = len;
    int res =
        WSASend((SOCKET)sock, &wsabuf, 1, NULL, 0, &ctx->overlapped, NULL);
    if (res == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      free(buf);
      free(ctx);
      t->result = ValInt(0);
      t->state = 3;
    }
#else
#ifdef AKS_USE_IO_URING
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)sock;
    ctx->buffer = buf;
    ctx->op_type = 3;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_send(sqe, (int)sock, buf, len, 0);
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#else
    int bytes_sent = send((int)sock, buf, len, 0);
    if (bytes_sent >= 0) {
      t->result = ValInt(bytes_sent);
    } else {
      t->result = ValInt(0);
    }
    t->state = 3;
    free(buf);
#endif
#endif

    ValFree(&strval);
    ValFree(&sockval);
    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketConnect") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: SocketConnect() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue portval = POP();
    AKSValue hostval = POP();
    if (ValKind(hostval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: SocketConnect() first argument must be a string, "
              "got type %d\n",
              ValKind(hostval));
      ValFree(&hostval);
      ValFree(&portval);
      return -1;
    }
    const char *host = ValToStrPtr(hostval)->data;
    int port = (int)ValToInt(portval);

    VMTask *t = CreateAsyncPendingTask(vm);

#ifdef _WIN32
    SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                         WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) {
      t->result = ValInt(-1);
      t->state = 3;
      ValFree(&hostval);
      ValFree(&portval);
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = 0;
    bind(s, (struct sockaddr *)&local_addr, sizeof(local_addr));

    CreateIoCompletionPort((HANDLE)s, vm->iocp, (ULONG_PTR)s, 0);

    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &target_addr.sin_addr);

    GUID GuidConnectEx = WSAID_CONNECTEX;
    LPFN_CONNECTEX lpfnConnectEx = NULL;
    DWORD dwBytes = 0;
    WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidConnectEx,
             sizeof(GuidConnectEx), &lpfnConnectEx, sizeof(lpfnConnectEx),
             &dwBytes, NULL, NULL);

    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)s;
    ctx->op_type = 5; // connect

    BOOL res =
        lpfnConnectEx(s, (struct sockaddr *)&target_addr, sizeof(target_addr),
                      NULL, 0, NULL, &ctx->overlapped);
    if (!res && GetLastError() != ERROR_IO_PENDING) {
      closesocket(s);
      free(ctx);
      t->result = ValInt(-1);
      t->state = 3;
    } else {
      t->result = ValInt((int64_t)s);
    }
#else
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
      t->result = ValInt(-1);
      t->state = 3;
      ValFree(&hostval);
      ValFree(&portval);
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }

    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &target_addr.sin_addr);

#ifdef AKS_USE_IO_URING
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)s;
    ctx->op_type = 5;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_connect(sqe, s, (struct sockaddr *)&target_addr,
                          sizeof(target_addr));
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
    t->result = ValInt(s);
#else
    int connect_res =
        connect(s, (struct sockaddr *)&target_addr, sizeof(target_addr));
    if (connect_res == 0) {
      t->result = ValInt(s);
    } else {
      t->result = ValInt(-1);
      close(s);
    }
    t->state = 3;
#endif
#endif

    ValFree(&hostval);
    ValFree(&portval);
    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketAcceptSync") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SocketAcceptSync() expects 1 argument\n");
      return -1;
    }
    AKSValue sockval = POP();
    int64_t listen_s = ValToInt(sockval);
    ValFree(&sockval);
    if (listen_s < 0) {
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
#ifdef _WIN32
    SOCKET client_s = accept((SOCKET)listen_s, NULL, NULL);
    PUSH(ValInt((client_s != INVALID_SOCKET) ? (int64_t)client_s : -1));
#else
    int client_s = accept((int)listen_s, NULL, NULL);
    PUSH(ValInt(client_s));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketConnectSync") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SocketConnectSync() expects 2 arguments\n");
      return -1;
    }
    AKSValue portval = POP();
    AKSValue hostval = POP();
    const char *host = (ValKind(hostval) == VALSTR) ? ValToStrPtr(hostval)->data : "127.0.0.1";
    int port = (int)ValToInt(portval);
#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET) {
      struct sockaddr_in sin;
      sin.sin_family = AF_INET;
      sin.sin_port = htons((u_short)port);
      sin.sin_addr.s_addr = inet_addr(host);
      if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
        PUSH(ValInt((int64_t)s));
      } else {
        closesocket(s);
        PUSH(ValInt(-1));
      }
    } else {
      PUSH(ValInt(-1));
    }
#else
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s >= 0) {
      struct sockaddr_in sin;
      sin.sin_family = AF_INET;
      sin.sin_port = htons((u_short)port);
      sin.sin_addr.s_addr = inet_addr(host);
      if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
        PUSH(ValInt(s));
      } else {
        close(s);
        PUSH(ValInt(-1));
      }
    } else {
      PUSH(ValInt(-1));
    }
#endif
    ValFree(&hostval);
    ValFree(&portval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketSendSync") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SocketSendSync() expects 2 arguments\n");
      return -1;
    }
    AKSValue strval = POP();
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    const char *str = (ValKind(strval) == VALSTR) ? ValToStrPtr(strval)->data : "";
    int len = (ValKind(strval) == VALSTR) ? ValToStrPtr(strval)->len : 0;
#ifdef _WIN32
    int res = send((SOCKET)sock, str, len, 0);
    PUSH(ValInt(res >= 0 ? res : 0));
#else
    int res = send((int)sock, str, len, 0);
    PUSH(ValInt(res >= 0 ? res : 0));
#endif
    ValFree(&strval);
    ValFree(&sockval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketReceiveSync") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SocketReceiveSync() expects 2 arguments\n");
      return -1;
    }
    AKSValue maxval = POP();
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    int max_bytes = (int)ValToInt(maxval);
    ValFree(&maxval);
    ValFree(&sockval);
    if (max_bytes <= 0) max_bytes = 4096;
    char *buf = malloc(max_bytes + 1);
#ifdef _WIN32
    int res = recv((SOCKET)sock, buf, max_bytes, 0);
#else
    int res = recv((int)sock, buf, max_bytes, 0);
#endif
    if (res > 0) {
      buf[res] = '\0';
      PUSH(ValStrLen(buf, res));
    } else {
      PUSH(ValStr(""));
    }
    free(buf);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketClose") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SocketClose() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    ValFree(&sockval);

#ifdef _WIN32
    closesocket((SOCKET)sock);
#else
    close((int)sock);
#endif
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UdpSocket") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: UdpSocket() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue portval = POP();
    int port = (int)ValToInt(portval);
    ValFree(&portval);

#ifdef _WIN32
    SOCKET s = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0,
                         WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) {
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
      closesocket(s);
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    CreateIoCompletionPort((HANDLE)s, vm->iocp, (ULONG_PTR)s, 0);
    PUSH(ValInt((int64_t)s));
#else
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      close(s);
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    PUSH(ValInt(s));
#endif
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "UdpSendTo") == 0) {
    if (argc != 4) {
      fprintf(stderr,
              "Runtime error: UdpSendTo() expects 4 arguments, got %d\n", argc);
      return -1;
    }
    AKSValue strval = POP();
    AKSValue portval = POP();
    AKSValue hostval = POP();
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    const char *host = ValToStrPtr(hostval)->data;
    int port = (int)ValToInt(portval);
    const char *str = ValToStrPtr(strval)->data;
    int len = ValToStrPtr(strval)->len;

    VMTask *t = CreateAsyncPendingTask(vm);
    char *buf = malloc(len + 1);
    memcpy(buf, str, len);
    buf[len] = '\0';

    struct sockaddr_in *target_addr = malloc(sizeof(struct sockaddr_in));
    memset(target_addr, 0, sizeof(struct sockaddr_in));
    target_addr->sin_family = AF_INET;
    target_addr->sin_port = htons(port);
    inet_pton(AF_INET, host, &target_addr->sin_addr);

#ifdef _WIN32
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)sock;
    ctx->buffer = buf;
    ctx->op_type = 3; // send
    ctx->extra = target_addr;

    WSABUF wsabuf;
    wsabuf.buf = buf;
    wsabuf.len = len;
    int res = WSASendTo((SOCKET)sock, &wsabuf, 1, NULL, 0,
                        (struct sockaddr *)target_addr,
                        sizeof(struct sockaddr_in), &ctx->overlapped, NULL);
    if (res == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      free(buf);
      free(target_addr);
      free(ctx);
      t->result = ValInt(0);
      t->state = 3;
    }
#else
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)sock;
    ctx->buffer = buf;
    ctx->op_type = 3;
    ctx->extra = target_addr;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_sendto(sqe, (int)sock, buf, len, 0,
                         (struct sockaddr *)target_addr,
                         sizeof(struct sockaddr_in));
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#endif

    ValFree(&strval);
    ValFree(&portval);
    ValFree(&hostval);
    ValFree(&sockval);
    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "UdpReceiveFrom") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: UdpReceiveFrom() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue maxval = POP();
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    int max_bytes = (int)ValToInt(maxval);
    ValFree(&maxval);
    ValFree(&sockval);

    VMTask *t = CreateAsyncPendingTask(vm);
    char *buf = malloc(max_bytes + 1);

    struct sockaddr_in *src_addr = malloc(sizeof(struct sockaddr_in));
    memset(src_addr, 0, sizeof(struct sockaddr_in));
    int *addr_len = malloc(sizeof(int));
    *addr_len = sizeof(struct sockaddr_in);

#ifdef _WIN32
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)sock;
    ctx->buffer = buf;
    ctx->op_type = 2; // recv
    ctx->extra = src_addr;

    WSABUF wsabuf;
    wsabuf.buf = buf;
    wsabuf.len = max_bytes;
    DWORD flags = 0;
    int res = WSARecvFrom((SOCKET)sock, &wsabuf, 1, NULL, &flags,
                          (struct sockaddr *)src_addr, addr_len,
                          &ctx->overlapped, NULL);
    if (res == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      free(buf);
      free(src_addr);
      free(addr_len);
      free(ctx);
      t->result = ValStr("");
      t->state = 3;
    }
#else
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)sock;
    ctx->buffer = buf;
    ctx->op_type = 2;
    ctx->extra = src_addr;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_recvfrom(sqe, (int)sock, buf, max_bytes, 0,
                           (struct sockaddr *)src_addr, (socklen_t *)addr_len);
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#endif

    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ReadFile") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ReadFile() expects 1 or 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue path = POP();
    if (ValKind(path) != VALSTR) {
      fprintf(stderr, "Runtime error: ReadFile() argument must be a string\n");
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMREAD)) {
      fprintf(stderr, "Security error: read permission denied\n");
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(path)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "fs", "read", ValToStrPtr(path)->data)) {
      fprintf(stderr, "Security error: invalid read capability for path\n");
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    FILE *f = fopen(ValToStrPtr(path)->data, "rb");
    if (!f) {
      PUSH(ValNull());
    } else {
      fseek(f, 0, SEEK_END);
      long sz = ftell(f);
      rewind(f);
      char *buf = malloc(sz + 1);
      if (buf) {
        size_t rb = fread(buf, 1, sz, f);
        buf[rb] = '\0';
        PUSH(ValStr(buf));
        free(buf);
      } else {
        PUSH(ValNull());
      }
      fclose(f);
    }
    ValFree(&path);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Execute") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(stderr,
              "Runtime error: Execute() expects 1 or 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue cmd = POP();
    if (ValKind(cmd) != VALSTR) {
      fprintf(stderr, "Runtime error: Execute() argument must be a string\n");
      ValFree(&cmd);
      ValFree(&cap);
      return -1;
    }
    if (vm->sandboxenabled) {
      fprintf(stderr,
              "Security error: command execution disabled in sandbox mode\n");
      ValFree(&cmd);
      ValFree(&cap);
      return -1;
    }
    if (vm->inrestrictedeval) {
      fprintf(stderr,
              "Security error: exec() disabled in restricted eval mode\n");
      ValFree(&cmd);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMEXEC)) {
      fprintf(stderr, "Security error: execution permission denied\n");
      ValFree(&cmd);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "exec", "execute", NULL)) {
      fprintf(stderr, "Security error: invalid execution capability\n");
      ValFree(&cmd);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);
#ifdef _WIN32
    FILE *f = _popen(ValToStrPtr(cmd)->data, "r");
#else
    FILE *f = popen(ValToStrPtr(cmd)->data, "r");
#endif
    if (!f) {
      PUSH(ValNull());
    } else {
      size_t cap = 256;
      size_t count = 0;
      char *buf = malloc(cap);
      char chunk[256];
      while (fgets(chunk, sizeof(chunk), f)) {
        size_t len = strlen(chunk);
        if (count + len >= cap) {
          cap = cap * 2 + len;
          buf = realloc(buf, cap);
        }
        memcpy(buf + count, chunk, len);
        count += len;
      }
      buf[count] = '\0';
#ifdef _WIN32
      _pclose(f);
#else
      pclose(f);
#endif
      PUSH(ValStr(buf));
      free(buf);
    }
    ValFree(&cmd);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "exit") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: exit() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    int code = (int)ValToInt(arg);
    ValFree(&arg);
    exit(code);
    return 1;
  }
  if (strcmp(name, "Random") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: Random() expects 0 arguments, got %d\n",
              argc);
      return -1;
    }
    double r = (double)rand() / RAND_MAX;
    PUSH(ValFloat(r));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "round") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: round() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValInt((int64_t)round(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "split") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: split() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue sep = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(sep) != VALSTR) {
      fprintf(stderr, "Runtime error: split() arguments must be strings\n");
      ValFree(&sep);
      ValFree(&strval);
      return -1;
    }
    AKSValue arrval = ValArr(4);
    const char *s = ValToStrPtr(strval)->data;
    const char *d = ValToStrPtr(sep)->data;
    int dlen = (int)strlen(d);
    if (dlen == 0) {
      for (int i = 0; i < ValToStrPtr(strval)->len; i++) {
        char tmp[2] = {s[i], '\0'};
        AKSValue item = ValStr(tmp);
        AKSArray *a = ValToArr(arrval);
        if (a->count >= a->capacity) {
          a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
          a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
        }
        a->items[a->count++] = item;
      }
    } else {
      const char *current = s;
      const char *next;
      while ((next = strstr(current, d)) != NULL) {
        int partlen = (int)(next - current);
        char *part = malloc(partlen + 1);
        memcpy(part, current, partlen);
        part[partlen] = '\0';
        AKSValue item = ValStr(part);
        free(part);
        AKSArray *a = ValToArr(arrval);
        if (a->count >= a->capacity) {
          a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
          a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
        }
        a->items[a->count++] = item;
        current = next + dlen;
      }
      AKSValue item = ValStr(current);
      AKSArray *a = ValToArr(arrval);
      if (a->count >= a->capacity) {
        a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
        a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
      }
      a->items[a->count++] = item;
    }
    PUSH(arrval);
    ValFree(&sep);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "replace") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: replace() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue newval = POP();
    AKSValue oldval = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(oldval) != VALSTR ||
        ValKind(newval) != VALSTR) {
      fprintf(stderr, "Runtime error: replace() arguments must be strings\n");
      ValFree(&newval);
      ValFree(&oldval);
      ValFree(&strval);
      return -1;
    }
    const char *s = ValToStrPtr(strval)->data;
    const char *o = ValToStrPtr(oldval)->data;
    const char *n = ValToStrPtr(newval)->data;
    int olen = (int)strlen(o);
    int nlen = (int)strlen(n);
    if (olen == 0) {
      PUSH(ValCopy(strval));
    } else {
      int occurrences = 0;
      const char *temp = s;
      while ((temp = strstr(temp, o)) != NULL) {
        occurrences++;
        temp += olen;
      }
      size_t outlen = strlen(s) + occurrences * (nlen - olen);
      char *out = malloc(outlen + 1);
      char *dest = out;
      const char *current = s;
      const char *next;
      while ((next = strstr(current, o)) != NULL) {
        int partlen = (int)(next - current);
        memcpy(dest, current, partlen);
        dest += partlen;
        memcpy(dest, n, nlen);
        dest += nlen;
        current = next + olen;
      }
      strcpy(dest, current);
      PUSH(ValStr(out));
      free(out);
    }
    ValFree(&newval);
    ValFree(&oldval);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "trim") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: trim() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR) {
      fprintf(stderr, "Runtime error: trim() argument must be a string\n");
      ValFree(&strval);
      return -1;
    }
    const char *s = ValToStrPtr(strval)->data;
    while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
      s++;
    }
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n')) {
      len--;
    }
    char *trimmed = malloc(len + 1);
    memcpy(trimmed, s, len);
    trimmed[len] = '\0';
    PUSH(ValStr(trimmed));
    free(trimmed);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "lower") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: lower() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR) {
      fprintf(stderr, "Runtime error: lower() argument must be a string\n");
      ValFree(&strval);
      return -1;
    }
    char *buf = strdup(ValToStrPtr(strval)->data);
    for (int i = 0; buf[i]; i++) {
      if (buf[i] >= 'A' && buf[i] <= 'Z') {
        buf[i] = buf[i] + 32;
      }
    }
    PUSH(ValStr(buf));
    free(buf);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "upper") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: upper() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR) {
      fprintf(stderr, "Runtime error: upper() argument must be a string\n");
      ValFree(&strval);
      return -1;
    }
    char *buf = strdup(ValToStrPtr(strval)->data);
    for (int i = 0; buf[i]; i++) {
      if (buf[i] >= 'a' && buf[i] <= 'z') {
        buf[i] = buf[i] - 32;
      }
    }
    PUSH(ValStr(buf));
    free(buf);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "find") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: find() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue sub = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(sub) != VALSTR) {
      fprintf(stderr, "Runtime error: find() arguments must be strings\n");
      ValFree(&sub);
      ValFree(&strval);
      return -1;
    }
    const char *found =
        strstr(ValToStrPtr(strval)->data, ValToStrPtr(sub)->data);
    if (found) {
      PUSH(ValInt((int64_t)(found - ValToStrPtr(strval)->data)));
    } else {
      PUSH(ValInt(-1));
    }
    ValFree(&sub);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "starts") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: starts() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue prefix = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(prefix) != VALSTR) {
      fprintf(stderr, "Runtime error: starts() arguments must be strings\n");
      ValFree(&prefix);
      ValFree(&strval);
      return -1;
    }
    int matches = (strncmp(ValToStrPtr(strval)->data, ValToStrPtr(prefix)->data,
                           ValToStrPtr(prefix)->len) == 0);
    PUSH(ValBool(matches));
    ValFree(&prefix);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ends") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ends() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue suffix = POP();
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR || ValKind(suffix) != VALSTR) {
      fprintf(stderr, "Runtime error: ends() arguments must be strings\n");
      ValFree(&suffix);
      ValFree(&strval);
      return -1;
    }
    int matches = 0;
    if (ValToStrPtr(strval)->len >= ValToStrPtr(suffix)->len) {
      matches = (strcmp(ValToStrPtr(strval)->data + ValToStrPtr(strval)->len -
                            ValToStrPtr(suffix)->len,
                        ValToStrPtr(suffix)->data) == 0);
    }
    PUSH(ValBool(matches));
    ValFree(&suffix);
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "sort") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: sort() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arrval = POP();
    if (ValKind(arrval) != VALARR) {
      fprintf(stderr, "Runtime error: sort() argument must be an array\n");
      ValFree(&arrval);
      return -1;
    }
    qsort(ValToArr(arrval)->items, ValToArr(arrval)->count, sizeof(AKSValue),
          CompareAKSVals);
    PUSH(arrval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Concatenate") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: Concatenate() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) != VALARR || ValKind(b) != VALARR) {
      fprintf(stderr,
              "Runtime error: Concatenate() arguments must be arrays\n");
      ValFree(&a);
      ValFree(&b);
      return -1;
    }
    int total = ValToArr(a)->count + ValToArr(b)->count;
    AKSValue res = ValArr(total);
    ValToArr(res)->count = total;
    for (int i = 0; i < ValToArr(a)->count; i++) {
      ValToArr(res)->items[i] = ValCopy(ValToArr(a)->items[i]);
    }
    for (int i = 0; i < ValToArr(b)->count; i++) {
      ValToArr(res)->items[ValToArr(a)->count + i] =
          ValCopy(ValToArr(b)->items[i]);
    }
    PUSH(res);
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "contains") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: contains() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue val = POP();
    AKSValue arr = POP();
    if (ValKind(arr) != VALARR) {
      fprintf(stderr,
              "Runtime error: contains() first argument must be an array\n");
      ValFree(&val);
      ValFree(&arr);
      return -1;
    }
    int found = 0;
    for (int i = 0; i < ValToArr(arr)->count; i++) {
      AKSValue eq = ValEq(ValToArr(arr)->items[i], val);
      if (ValToBool(eq)) {
        found = 1;
        break;
      }
    }
    PUSH(ValBool(found));
    ValFree(&val);
    ValFree(&arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "keys") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: keys() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue objval = POP();
    if (ValKind(objval) != VALSTRUCT) {
      fprintf(stderr, "Runtime error: keys() argument must be a struct\n");
      ValFree(&objval);
      return -1;
    }
    AKSValue arrval = ValArr(ValToObj(objval)->fieldcount);
    ValToArr(arrval)->count = ValToObj(objval)->fieldcount;
    for (int i = 0; i < ValToObj(objval)->fieldcount; i++) {
      ValToArr(arrval)->items[i] = ValStr(ValToObj(objval)->fieldnames[i]);
    }
    PUSH(arrval);
    ValFree(&objval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "values") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: values() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue objval = POP();
    if (ValKind(objval) != VALSTRUCT) {
      fprintf(stderr, "Runtime error: values() argument must be a struct\n");
      ValFree(&objval);
      return -1;
    }
    AKSValue arrval = ValArr(ValToObj(objval)->fieldcount);
    ValToArr(arrval)->count = ValToObj(objval)->fieldcount;
    for (int i = 0; i < ValToObj(objval)->fieldcount; i++) {
      ValToArr(arrval)->items[i] = ValCopy(ValToObj(objval)->fieldvals[i]);
    }
    PUSH(arrval);
    ValFree(&objval);
    *stacktopptr = stacktop;
    return 1;
  }
#define MATHUNARY(namestr, func)                                               \
  if (strcmp(name, namestr) == 0) {                                            \
    if (argc != 1) {                                                           \
      fprintf(stderr, "Runtime error: %s() expects 1 argument, got %d\n",      \
              namestr, argc);                                                  \
      return -1;                                                               \
    }                                                                          \
    AKSValue arg = POP();                                                      \
    double vald =                                                              \
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);    \
    PUSH(ValFloat(func(vald)));                                                \
    ValFree(&arg);                                                             \
    *stacktopptr = stacktop;                                                   \
    return 1;                                                                  \
  }

  MATHUNARY("asin", asin)
  MATHUNARY("acos", acos)
  MATHUNARY("atan", atan)
  MATHUNARY("sinh", sinh)
  MATHUNARY("cosh", cosh)
  MATHUNARY("tanh", tanh)
#undef MATHUNARY

  if (strcmp(name, "atan2") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: atan2() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue x = POP();
    AKSValue y = POP();
    double dy = ValKind(y) == VALFLOAT ? ValToFloat(y) : (double)ValToInt(y);
    double dx = ValKind(x) == VALFLOAT ? ValToFloat(x) : (double)ValToInt(x);
    PUSH(ValFloat(atan2(dy, dx)));
    ValFree(&y);
    ValFree(&x);
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
  if (strcmp(name, "time") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: time() expects 0 arguments, got %d\n",
              argc);
      return -1;
    }
    time_t t = time(NULL);
    PUSH(ValFloat((double)t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "sleep") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: sleep() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    int ms = (int)(ValKind(arg) == VALINT ? ValToInt(arg)
                                          : (int64_t)ValToFloat(arg));
    ValFree(&arg);
    SleepMs(ms);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "exists") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(stderr,
              "Runtime error: exists() expects 1 or 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: exists() argument must be a string\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMREAD)) {
      fprintf(stderr, "Security error: read permission denied\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(arg)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "fs", "read", ValToStrPtr(arg)->data)) {
      fprintf(stderr, "Security error: invalid read capability for path\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    int ex = FileExists(ValToStrPtr(arg)->data);
    PUSH(ValBool(ex));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RemoveFile") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(stderr,
              "Runtime error: RemoveFile() expects 1 or 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr,
              "Runtime error: RemoveFile() argument must be a string\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (vm->inrestrictedeval) {
      fprintf(stderr,
              "Security error: rmfile() disabled in restricted eval mode\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMWRITE)) {
      fprintf(stderr, "Security error: write permission denied\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(arg)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "fs", "write", ValToStrPtr(arg)->data)) {
      fprintf(stderr, "Security error: invalid write capability for path\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    int status = remove(ValToStrPtr(arg)->data) == 0;
    PUSH(ValBool(status));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MakeDirectory") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: MakeDirectory() expects 1 or 2 arguments, got %d\n",
          argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr,
              "Runtime error: MakeDirectory() argument must be a string\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (vm->inrestrictedeval) {
      fprintf(stderr,
              "Security error: mkdir() disabled in restricted eval mode\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMWRITE)) {
      fprintf(stderr, "Security error: write permission denied\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(arg)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "fs", "write", ValToStrPtr(arg)->data)) {
      fprintf(stderr, "Security error: invalid write capability for path\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    int status = NewDir(ValToStrPtr(arg)->data);
    PUSH(ValBool(status));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GetEnv") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(stderr,
              "Runtime error: GetEnv() expects 1 or 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: GetEnv() argument must be a string\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMENV)) {
      fprintf(stderr, "Security error: env permission denied\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "env", "env", NULL)) {
      fprintf(stderr, "Security error: invalid env capability\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    const char *val = getenv(ValToStrPtr(arg)->data);
    if (val) {
      PUSH(ValStr(val));
    } else {
      PUSH(ValNull());
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SetEnv") == 0) {
    AKSValue cap = ValNull();
    if (argc == 3) {
      cap = POP();
    } else if (argc != 2) {
      fprintf(stderr,
              "Runtime error: SetEnv() expects 2 or 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue val = POP();
    AKSValue nameval = POP();
    if (ValKind(nameval) != VALSTR || ValKind(val) != VALSTR) {
      fprintf(stderr, "Runtime error: SetEnv() arguments must be strings\n");
      ValFree(&val);
      ValFree(&nameval);
      ValFree(&cap);
      return -1;
    }
    if (vm->sandboxenabled) {
      fprintf(stderr, "Security error: modifying environment variables "
                      "disabled in sandbox mode\n");
      ValFree(&val);
      ValFree(&nameval);
      ValFree(&cap);
      return -1;
    }
    if (vm->inrestrictedeval) {
      fprintf(stderr,
              "Security error: setenv() disabled in restricted eval mode\n");
      ValFree(&val);
      ValFree(&nameval);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMENV)) {
      fprintf(stderr, "Security error: env permission denied\n");
      ValFree(&val);
      ValFree(&nameval);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "env", "env", NULL)) {
      fprintf(stderr, "Security error: invalid env capability\n");
      ValFree(&val);
      ValFree(&nameval);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    int status =
        SetEnvironmentVar(ValToStrPtr(nameval)->data, ValToStrPtr(val)->data);
    PUSH(ValBool(status));
    ValFree(&val);
    ValFree(&nameval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "JsonParse") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: JsonParse() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: JsonParse() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    const char *ptr = ValToStrPtr(arg)->data;
    int errorflag = 0;
    AKSValue res = ParseJSONVal(&ptr, &errorflag);
    if (errorflag) {
      ValFree(&res);
      PUSH(ValNull());
    } else {
      PUSH(res);
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "JsonStringify") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: JsonStringify() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    StrIfyVal(arg, &buf, &len, &cap);
    buf[len] = '\0';
    PUSH(ValStr(buf));
    free(buf);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "JsonParseStream") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: JsonParseStream() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValArr(0));
      *stacktopptr = stacktop;
      return 1;
    }
    const char *ptr = ValToStrPtr(arg)->data;
    AKSValue res_arr = ValArr(0);
    int errorflag = 0;
    while (*ptr) {
      while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n')
        ptr++;
      if (*ptr == '\0')
        break;
      AKSValue parsed = ParseJSONVal(&ptr, &errorflag);
      if (errorflag) {
        ValFree(&parsed);
        break;
      }
      AKSArray *a = ValToArr(res_arr);
      if (a->count >= a->capacity) {
        a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
        a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
      }
      a->items[a->count++] = parsed;
    }
    PUSH(res_arr);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CborEncode") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: CborEncode() expects 1 argument, got %d\n", argc);
      return -1;
    }
    AKSValue arg = POP();
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    CBOREncodeVal(arg, &buf, &len, &cap);
    PUSH(ValStrLen(buf, (int)len));
    free(buf);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CborDecode") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: CborDecode() expects 1 argument, got %d\n", argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const uint8_t *ptr = (const uint8_t *)ValToStrPtr(arg)->data;
    int errorflag = 0;
    AKSValue res = CBORDecodeVal(&ptr, ptr + ValToStrPtr(arg)->len, &errorflag);
    if (errorflag) {
      ValFree(&res);
      PUSH(ValNull());
    } else {
      PUSH(res);
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MessagePackEncode") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: MessagePackEncode() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    MsgpackEncodeVal(arg, &buf, &len, &cap);
    PUSH(ValStrLen(buf, (int)len));
    free(buf);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MessagePackDecode") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: MessagePackDecode() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const uint8_t *ptr = (const uint8_t *)ValToStrPtr(arg)->data;
    int errorflag = 0;
    AKSValue res =
        MsgpackDecodeVal(&ptr, ptr + ValToStrPtr(arg)->len, &errorflag);
    if (errorflag) {
      ValFree(&res);
      PUSH(ValNull());
    } else {
      PUSH(res);
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CsvParse") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CsvParse() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValArr(0));
      *stacktopptr = stacktop;
      return 1;
    }
    AKSValue res = ParseCSVStr(ValToStrPtr(arg)->data);
    PUSH(res);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CsvStringify") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: CsvStringify() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    char *csv = StringifyCSV(arg);
    PUSH(ValStr(csv));
    free(csv);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TomlParse") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: TomlParse() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    AKSValue res = ParseTOMLStr(ValToStrPtr(arg)->data);
    PUSH(res);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TomlStringify") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: TomlStringify() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    char *toml = StringifyTOML(arg);
    PUSH(ValStr(toml));
    free(toml);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "XmlParse") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: XmlParse() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    AKSValue res = ParseXMLStr(ValToStrPtr(arg)->data);
    PUSH(res);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "XmlStringify") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: XmlStringify() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    char *xml = StringifyXML(arg);
    PUSH(ValStr(xml));
    free(xml);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProtobufEncodeObj") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: ProtobufEncodeObj() expects 2 arguments, got %d\n",
          argc);
      return -1;
    }
    AKSValue mapping = POP();
    AKSValue obj = POP();
    size_t b_len = 0;
    char *bytes = ProtobufEncodeObjFunc(obj, mapping, &b_len);
    PUSH(ValStrLen(bytes, (int)b_len));
    free(bytes);
    ValFree(&obj);
    ValFree(&mapping);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProtobufDecodeObj") == 0) {
    if (argc != 3) {
      fprintf(
          stderr,
          "Runtime error: ProtobufDecodeObj() expects 3 arguments, got %d\n",
          argc);
      return -1;
    }
    AKSValue typename_val = POP();
    AKSValue mapping = POP();
    AKSValue bytes_val = POP();
    if (ValKind(bytes_val) != VALSTR || ValKind(typename_val) != VALSTR) {
      ValFree(&bytes_val);
      ValFree(&mapping);
      ValFree(&typename_val);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    AKSValue res = ProtobufDecodeObjFunc(ValToStrPtr(bytes_val)->data,
                                         ValToStrPtr(bytes_val)->len, mapping,
                                         ValToStrPtr(typename_val)->data);
    PUSH(res);
    ValFree(&bytes_val);
    ValFree(&mapping);
    ValFree(&typename_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Utf8Len") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Utf8Len() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValInt(0));
      *stacktopptr = stacktop;
      return 1;
    }
    int l = Utf8LenFunc(ValToStrPtr(arg)->data);
    PUSH(ValInt(l));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Utf8CharAt") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: Utf8CharAt() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue idx_val = POP();
    AKSValue str_val = POP();
    if (ValKind(str_val) != VALSTR || ValKind(idx_val) != VALINT) {
      ValFree(&str_val);
      ValFree(&idx_val);
      PUSH(ValStr(""));
      *stacktopptr = stacktop;
      return 1;
    }
    char *ch =
        Utf8CharAtFunc(ValToStrPtr(str_val)->data, (int)ValToInt(idx_val));
    PUSH(ValStr(ch));
    free(ch);
    ValFree(&str_val);
    ValFree(&idx_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Utf8CodepointAt") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: Utf8CodepointAt() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue idx_val = POP();
    AKSValue str_val = POP();
    if (ValKind(str_val) != VALSTR || ValKind(idx_val) != VALINT) {
      ValFree(&str_val);
      ValFree(&idx_val);
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    int cp =
        Utf8CodepointAtFunc(ValToStrPtr(str_val)->data, (int)ValToInt(idx_val));
    PUSH(ValInt(cp));
    ValFree(&str_val);
    ValFree(&idx_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Utf8ToChars") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: Utf8ToChars() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      ValFree(&arg);
      PUSH(ValArr(0));
      *stacktopptr = stacktop;
      return 1;
    }
    const char *str = ValToStrPtr(arg)->data;
    int len = (int)strlen(str);
    int pos = 0;
    AKSValue res_arr = ValArr(0);
    while (pos < len) {
      int consumed = 0;
      Utf8NextCodepoint((const uint8_t *)str + pos, len - pos, &consumed);
      if (consumed <= 0)
        break;
      char *ch = malloc(consumed + 1);
      memcpy(ch, str + pos, consumed);
      ch[consumed] = '\0';
      AKSArray *a = ValToArr(res_arr);
      if (a->count >= a->capacity) {
        a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
        a->items = realloc(a->items, a->capacity * sizeof(AKSValue));
      }
      a->items[a->count++] = ValStr(ch);
      free(ch);
      pos += consumed;
    }
    PUSH(res_arr);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Utf8FromCodepoints") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: Utf8FromCodepoints() expects 1 argument, got %d\n",
          argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALARR) {
      ValFree(&arg);
      PUSH(ValStr(""));
      *stacktopptr = stacktop;
      return 1;
    }
    AKSArray *arr = ValToArr(arg);
    int *cps = malloc((arr->count + 1) * sizeof(int));
    for (int i = 0; i < arr->count; i++) {
      cps[i] = (int)ValToInt(arr->items[i]);
    }
    char *str = CodepointsToStr(cps, arr->count);
    PUSH(ValStr(str));
    free(str);
    free(cps);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegexMatch") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: RegexMatch() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue str_val = POP();
    AKSValue pat_val = POP();
    if (ValKind(pat_val) != VALSTR || ValKind(str_val) != VALSTR) {
      ValFree(&pat_val);
      ValFree(&str_val);
      PUSH(ValBool(0));
      *stacktopptr = stacktop;
      return 1;
    }
    ReProg prog = CompileRegex(ValToStrPtr(pat_val)->data);
    int input_len = 0;
    int *input = StrToCodepoints(ValToStrPtr(str_val)->data, &input_len);
    int saves[64];
    memset(saves, -1, sizeof(saves));
    int match = MatchNFA(prog.insts, 0, input, input_len, 0, saves,
                         2 * (prog.save_count + 1));
    int full_match = match && (saves[0] == 0 && saves[1] == input_len);
    PUSH(ValBool(full_match));
    free(input);
    FreeRegex(&prog);
    ValFree(&pat_val);
    ValFree(&str_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegexSearch") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: RegexSearch() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue str_val = POP();
    AKSValue pat_val = POP();
    if (ValKind(pat_val) != VALSTR || ValKind(str_val) != VALSTR) {
      ValFree(&pat_val);
      ValFree(&str_val);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    ReProg prog = CompileRegex(ValToStrPtr(pat_val)->data);
    int input_len = 0;
    int *input = StrToCodepoints(ValToStrPtr(str_val)->data, &input_len);
    int saves[64];
    int match = 0;
    int match_start = -1;
    int match_end = -1;
    for (int i = 0; i <= input_len; i++) {
      memset(saves, -1, sizeof(saves));
      if (MatchNFA(prog.insts, 0, input, input_len, i, saves,
                   2 * (prog.save_count + 1))) {
        match = 1;
        match_start = saves[0];
        match_end = saves[1];
        break;
      }
    }
    if (match) {
      AKSValue res_arr = ValArr(0);
      AKSArray *a = ValToArr(res_arr);
      char *full_match_str =
          CodepointsToStr(input + match_start, match_end - match_start);
      a->items = realloc(a->items, (a->count + 1) * sizeof(AKSValue));
      a->items[a->count++] = ValStr(full_match_str);
      free(full_match_str);
      for (int g = 1; g <= prog.save_count; g++) {
        int start_idx = saves[2 * g];
        int end_idx = saves[2 * g + 1];
        if (start_idx >= 0 && end_idx >= start_idx) {
          char *sub = CodepointsToStr(input + start_idx, end_idx - start_idx);
          a->items = realloc(a->items, (a->count + 1) * sizeof(AKSValue));
          a->items[a->count++] = ValStr(sub);
          free(sub);
        } else {
          a->items = realloc(a->items, (a->count + 1) * sizeof(AKSValue));
          a->items[a->count++] = ValNull();
        }
      }
      PUSH(res_arr);
    } else {
      PUSH(ValNull());
    }
    free(input);
    FreeRegex(&prog);
    ValFree(&pat_val);
    ValFree(&str_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegexReplace") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: RegexReplace() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue rep_val = POP();
    AKSValue str_val = POP();
    AKSValue pat_val = POP();
    if (ValKind(pat_val) != VALSTR || ValKind(str_val) != VALSTR ||
        ValKind(rep_val) != VALSTR) {
      ValFree(&pat_val);
      ValFree(&str_val);
      ValFree(&rep_val);
      PUSH(ValStr(""));
      *stacktopptr = stacktop;
      return 1;
    }
    ReProg prog = CompileRegex(ValToStrPtr(pat_val)->data);
    int input_len = 0;
    int *input = StrToCodepoints(ValToStrPtr(str_val)->data, &input_len);
    int saves[64];
    int out_cap = input_len + 16;
    int out_count = 0;
    int *out_cps = malloc(out_cap * sizeof(int));
    int idx = 0;
    while (idx <= input_len) {
      memset(saves, -1, sizeof(saves));
      if (idx < input_len && MatchNFA(prog.insts, 0, input, input_len, idx,
                                      saves, 2 * (prog.save_count + 1))) {
        int match_start = saves[0];
        int match_end = saves[1];
        int rep_len = 0;
        int *rep_cps = StrToCodepoints(ValToStrPtr(rep_val)->data, &rep_len);
        while (out_count + rep_len >= out_cap) {
          out_cap *= 2;
          out_cps = realloc(out_cps, out_cap * sizeof(int));
        }
        memcpy(out_cps + out_count, rep_cps, rep_len * sizeof(int));
        out_count += rep_len;
        free(rep_cps);
        idx = match_end;
        if (match_start == match_end) {
          if (idx < input_len) {
            if (out_count >= out_cap) {
              out_cap *= 2;
              out_cps = realloc(out_cps, out_cap * sizeof(int));
            }
            out_cps[out_count++] = input[idx];
            idx++;
          } else {
            break;
          }
        }
      } else {
        if (idx < input_len) {
          if (out_count >= out_cap) {
            out_cap *= 2;
            out_cps = realloc(out_cps, out_cap * sizeof(int));
          }
          out_cps[out_count++] = input[idx];
        }
        idx++;
      }
    }
    char *res_str = CodepointsToStr(out_cps, out_count);
    PUSH(ValStr(res_str));
    free(res_str);
    free(out_cps);
    free(input);
    FreeRegex(&prog);
    ValFree(&pat_val);
    ValFree(&str_val);
    ValFree(&rep_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UnicodeNormalize") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: UnicodeNormalize() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue form_val = POP();
    AKSValue str_val = POP();
    if (ValKind(str_val) != VALSTR || ValKind(form_val) != VALSTR) {
      ValFree(&str_val);
      ValFree(&form_val);
      PUSH(ValStr(""));
      *stacktopptr = stacktop;
      return 1;
    }
    char *norm = UnicodeNormalizeFunc(ValToStrPtr(str_val)->data,
                                      ValToStrPtr(form_val)->data);
    PUSH(ValStr(norm));
    free(norm);
    ValFree(&str_val);
    ValFree(&form_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "LocaleCompare") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: LocaleCompare() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue loc_val = POP();
    AKSValue str2_val = POP();
    AKSValue str1_val = POP();
    if (ValKind(str1_val) != VALSTR || ValKind(str2_val) != VALSTR) {
      ValFree(&str1_val);
      ValFree(&str2_val);
      ValFree(&loc_val);
      PUSH(ValInt(0));
      *stacktopptr = stacktop;
      return 1;
    }
    const char *loc =
        (ValKind(loc_val) == VALSTR) ? ValToStrPtr(loc_val)->data : "";
    int res = LocaleCompareFunc(ValToStrPtr(str1_val)->data,
                                ValToStrPtr(str2_val)->data, loc);
    PUSH(ValInt(res));
    ValFree(&str1_val);
    ValFree(&str2_val);
    ValFree(&loc_val);
    *stacktopptr = stacktop;
    return 1;
  }
#define MATHUNARYEXT(namestr, func)                                            \
  if (strcmp(name, namestr) == 0) {                                            \
    if (argc != 1) {                                                           \
      fprintf(stderr, "Runtime error: %s() expects 1 argument, got %d\n",      \
              namestr, argc);                                                  \
      return -1;                                                               \
    }                                                                          \
    AKSValue arg = POP();                                                      \
    double vald =                                                              \
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);    \
    PUSH(ValFloat(func(vald)));                                                \
    ValFree(&arg);                                                             \
    *stacktopptr = stacktop;                                                   \
    return 1;                                                                  \
  }

  MATHUNARYEXT("asinh", asinh)
  MATHUNARYEXT("acosh", acosh)
  MATHUNARYEXT("atanh", atanh)
  MATHUNARYEXT("exp", exp)
  MATHUNARYEXT("log10", log10)
  MATHUNARYEXT("log2", log2)
  MATHUNARYEXT("cbrt", cbrt)
  MATHUNARYEXT("trunc", trunc)
  MATHUNARYEXT("erf", erf)
  MATHUNARYEXT("erfc", erfc)
  MATHUNARYEXT("tgamma", tgamma)
  MATHUNARYEXT("lgamma", lgamma)
#undef MATHUNARYEXT

  if (strcmp(name, "Hypotenuse") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: Hypotenuse() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue y = POP();
    AKSValue x = POP();
    double dx = ValKind(x) == VALFLOAT ? ValToFloat(x) : (double)ValToInt(x);
    double dy = ValKind(y) == VALFLOAT ? ValToFloat(y) : (double)ValToInt(y);
    PUSH(ValFloat(hypot(dx, dy)));
    ValFree(&x);
    ValFree(&y);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GreatestCommonDivisor") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: GreatestCommonDivisor() expects 2 arguments, got "
              "%d\n",
              argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    int64_t ia = ValKind(a) == VALFLOAT ? (int64_t)ValToFloat(a) : ValToInt(a);
    int64_t ib = ValKind(b) == VALFLOAT ? (int64_t)ValToFloat(b) : ValToInt(b);
    PUSH(ValInt(MathGcd(ia, ib)));
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "LeastCommonMultiple") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: LeastCommonMultiple() expects 2 arguments, got %d\n",
          argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    int64_t ia = ValKind(a) == VALFLOAT ? (int64_t)ValToFloat(a) : ValToInt(a);
    int64_t ib = ValKind(b) == VALFLOAT ? (int64_t)ValToFloat(b) : ValToInt(b);
    PUSH(ValInt(MathLcm(ia, ib)));
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Factorial") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Factorial() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(MathFact(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "nPr") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: nPr() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue r = POP();
    AKSValue n = POP();
    double dn = ValKind(n) == VALFLOAT ? ValToFloat(n) : (double)ValToInt(n);
    double dr = ValKind(r) == VALFLOAT ? ValToFloat(r) : (double)ValToInt(r);
    PUSH(ValFloat(MathNpr(dn, dr)));
    ValFree(&n);
    ValFree(&r);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "nCr") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: nCr() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue r = POP();
    AKSValue n = POP();
    double dn = ValKind(n) == VALFLOAT ? ValToFloat(n) : (double)ValToInt(n);
    double dr = ValKind(r) == VALFLOAT ? ValToFloat(r) : (double)ValToInt(r);
    PUSH(ValFloat(MathNcr(dn, dr)));
    ValFree(&n);
    ValFree(&r);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "IsPrime") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: IsPrime() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    int64_t vali =
        ValKind(arg) == VALFLOAT ? (int64_t)ValToFloat(arg) : ValToInt(arg);
    PUSH(ValBool(MathIsPrime(vali)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ToRadians") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ToRadians() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(vald * 3.14159265358979323846 / 180.0));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ToDegrees") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ToDegrees() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(vald * 180.0 / 3.14159265358979323846));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "mean") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: mean() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALARR) {
      fprintf(stderr, "Runtime error: mean() argument must be an array\n");
      ValFree(&arg);
      return -1;
    }
    if (ValToArr(arg)->count == 0) {
      PUSH(ValFloat(0.0));
    } else {
      double sum = 0.0;
      for (int i = 0; i < ValToArr(arg)->count; i++) {
        AKSValue item = ValToArr(arg)->items[i];
        sum += ValKind(item) == VALFLOAT ? ValToFloat(item)
                                         : (double)ValToInt(item);
      }
      PUSH(ValFloat(sum / ValToArr(arg)->count));
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "median") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: median() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALARR) {
      fprintf(stderr, "Runtime error: median() argument must be an array\n");
      ValFree(&arg);
      return -1;
    }
    if (ValToArr(arg)->count == 0) {
      PUSH(ValFloat(0.0));
    } else {
      int cnt = ValToArr(arg)->count;
      AKSValue *copy = malloc(cnt * sizeof(AKSValue));
      for (int i = 0; i < cnt; i++) {
        copy[i] = ValCopy(ValToArr(arg)->items[i]);
      }
      qsort(copy, cnt, sizeof(AKSValue), CompareAKSVals);
      if (cnt % 2 == 1) {
        AKSValue mid = copy[cnt / 2];
        PUSH(ValFloat(ValKind(mid) == VALFLOAT ? ValToFloat(mid)
                                               : (double)ValToInt(mid)));
      } else {
        AKSValue mid1 = copy[cnt / 2 - 1];
        AKSValue mid2 = copy[cnt / 2];
        double d1 = ValKind(mid1) == VALFLOAT ? ValToFloat(mid1)
                                              : (double)ValToInt(mid1);
        double d2 = ValKind(mid2) == VALFLOAT ? ValToFloat(mid2)
                                              : (double)ValToInt(mid2);
        PUSH(ValFloat((d1 + d2) / 2.0));
      }
      for (int i = 0; i < cnt; i++)
        ValFree(&copy[i]);
      free(copy);
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "variance") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: variance() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALARR) {
      fprintf(stderr, "Runtime error: variance() argument must be an array\n");
      ValFree(&arg);
      return -1;
    }
    if (ValToArr(arg)->count == 0) {
      PUSH(ValFloat(0.0));
    } else {
      double sum = 0.0;
      for (int i = 0; i < ValToArr(arg)->count; i++) {
        AKSValue item = ValToArr(arg)->items[i];
        sum += ValKind(item) == VALFLOAT ? ValToFloat(item)
                                         : (double)ValToInt(item);
      }
      double meanval = sum / ValToArr(arg)->count;
      double sqdiffsum = 0.0;
      for (int i = 0; i < ValToArr(arg)->count; i++) {
        AKSValue item = ValToArr(arg)->items[i];
        double vald = ValKind(item) == VALFLOAT ? ValToFloat(item)
                                                : (double)ValToInt(item);
        sqdiffsum += (vald - meanval) * (vald - meanval);
      }
      PUSH(ValFloat(sqdiffsum / ValToArr(arg)->count));
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StandardDeviation") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: StandardDeviation() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALARR) {
      fprintf(stderr,
              "Runtime error: StandardDeviation() argument must be an array\n");
      ValFree(&arg);
      return -1;
    }
    if (ValToArr(arg)->count == 0) {
      PUSH(ValFloat(0.0));
    } else {
      double sum = 0.0;
      for (int i = 0; i < ValToArr(arg)->count; i++) {
        AKSValue item = ValToArr(arg)->items[i];
        sum += ValKind(item) == VALFLOAT ? ValToFloat(item)
                                         : (double)ValToInt(item);
      }
      double meanval = sum / ValToArr(arg)->count;
      double sqdiffsum = 0.0;
      for (int i = 0; i < ValToArr(arg)->count; i++) {
        AKSValue item = ValToArr(arg)->items[i];
        double vald = ValKind(item) == VALFLOAT ? ValToFloat(item)
                                                : (double)ValToInt(item);
        sqdiffsum += (vald - meanval) * (vald - meanval);
      }
      PUSH(ValFloat(sqrt(sqdiffsum / ValToArr(arg)->count)));
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MD5") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MD5() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: MD5() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    AKSMD5CTX ctx;
    uint8_t digest[16];
    MD5Init(&ctx);
    MD5Update(&ctx, (const uint8_t *)ValToStrPtr(arg)->data,
              ValToStrPtr(arg)->len);
    MD5Final(&ctx, digest);

    char hashstr[33];
    for (int i = 0; i < 16; i++) {
      sprintf(&hashstr[i * 2], "%02x", digest[i]);
    }
    hashstr[32] = '\0';
    PUSH(ValStr(hashstr));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SHA256") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SHA256() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: SHA256() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    AKSSHA256CTX ctx;
    uint8_t digest[32];
    Sha256Init(&ctx);
    Sha256Update(&ctx, (const uint8_t *)ValToStrPtr(arg)->data,
                 ValToStrPtr(arg)->len);
    Sha256Final(&ctx, digest);

    char hashstr[65];
    for (int i = 0; i < 32; i++) {
      sprintf(&hashstr[i * 2], "%02x", digest[i]);
    }
    hashstr[64] = '\0';
    PUSH(ValStr(hashstr));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SHA512") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SHA512() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: SHA512() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    uint8_t digest[64];
    CryptoSha512((const uint8_t *)ValToStrPtr(arg)->data, ValToStrPtr(arg)->len,
                 digest);
    char hashstr[129];
    HexEncodeC(digest, 64, hashstr);
    PUSH(ValStr(hashstr));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SHA3256") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SHA3256() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: SHA3256() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    uint8_t digest[32];
    KeccakHash((const uint8_t *)ValToStrPtr(arg)->data, ValToStrPtr(arg)->len,
               digest, 32, 136);
    char hashstr[65];
    HexEncodeC(digest, 32, hashstr);
    PUSH(ValStr(hashstr));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SHA3512") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SHA3512() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: SHA3512() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    uint8_t digest[64];
    KeccakHash((const uint8_t *)ValToStrPtr(arg)->data, ValToStrPtr(arg)->len,
               digest, 64, 72);
    char hashstr[129];
    HexEncodeC(digest, 64, hashstr);
    PUSH(ValStr(hashstr));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AES256GCMEncrypt") == 0) {
    if (argc != 4) {
      fprintf(stderr,
              "Runtime error: AES256GCMEncrypt() expects 4 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue aad = POP();
    AKSValue pt = POP();
    AKSValue iv = POP();
    AKSValue key = POP();
    if (ValKind(key) != VALSTR || ValKind(iv) != VALSTR ||
        ValKind(pt) != VALSTR || ValKind(aad) != VALSTR) {
      fprintf(stderr,
              "Runtime error: AES256GCMEncrypt() invalid argument types\n");
      ValFree(&aad);
      ValFree(&pt);
      ValFree(&iv);
      ValFree(&key);
      return -1;
    }
    uint8_t key_b[32], iv_b[12];
    HexDecodeC(ValToStrPtr(key)->data, key_b);
    HexDecodeC(ValToStrPtr(iv)->data, iv_b);
    size_t pt_len = ValToStrPtr(pt)->len;
    uint8_t *ct = malloc(pt_len > 0 ? pt_len : 1);
    uint8_t tag[16];
    int res = AesGcmEncrypt(
        key_b, 32, iv_b, 12, (const uint8_t *)ValToStrPtr(aad)->data,
        ValToStrPtr(aad)->len, (const uint8_t *)ValToStrPtr(pt)->data, pt_len,
        ct, tag, 16);
    if (res) {
      char *ct_hex = malloc(pt_len * 2 + 1);
      HexEncodeC(ct, pt_len, ct_hex);
      char tag_hex[33];
      HexEncodeC(tag, 16, tag_hex);
      AKSValue struct_val = ValStruct("AesGcmResult");
      AKSStruct *s = ValToObj(struct_val);
      s->fieldcount = 2;
      s->fieldnames = malloc(2 * sizeof(char *));
      s->fieldnames[0] = strdup("ciphertext");
      s->fieldnames[1] = strdup("tag");
      s->fieldvals = malloc(2 * sizeof(AKSValue));
      s->fieldvals[0] = ValStr(ct_hex);
      s->fieldvals[1] = ValStr(tag_hex);
      PUSH(struct_val);
      free(ct_hex);
    } else {
      PUSH(ValNull());
    }
    free(ct);
    ValFree(&aad);
    ValFree(&pt);
    ValFree(&iv);
    ValFree(&key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AES256GCMDecrypt") == 0) {
    if (argc != 5) {
      fprintf(stderr,
              "Runtime error: AES256GCMDecrypt() expects 5 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue aad = POP();
    AKSValue tag = POP();
    AKSValue ct = POP();
    AKSValue iv = POP();
    AKSValue key = POP();
    if (ValKind(key) != VALSTR || ValKind(iv) != VALSTR ||
        ValKind(ct) != VALSTR || ValKind(tag) != VALSTR ||
        ValKind(aad) != VALSTR) {
      fprintf(stderr,
              "Runtime error: AES256GCMDecrypt() invalid argument types\n");
      ValFree(&aad);
      ValFree(&tag);
      ValFree(&ct);
      ValFree(&iv);
      ValFree(&key);
      return -1;
    }
    uint8_t key_b[32], iv_b[12], tag_b[16];
    HexDecodeC(ValToStrPtr(key)->data, key_b);
    HexDecodeC(ValToStrPtr(iv)->data, iv_b);
    HexDecodeC(ValToStrPtr(tag)->data, tag_b);
    size_t ct_len = strlen(ValToStrPtr(ct)->data) / 2;
    uint8_t *ct_b = malloc(ct_len > 0 ? ct_len : 1);
    HexDecodeC(ValToStrPtr(ct)->data, ct_b);
    uint8_t *pt = malloc(ct_len + 1);
    int res = AesGcmDecrypt(key_b, 32, iv_b, 12,
                            (const uint8_t *)ValToStrPtr(aad)->data,
                            ValToStrPtr(aad)->len, ct_b, ct_len, tag_b, 16, pt);
    if (res) {
      pt[ct_len] = '\0';
      PUSH(ValStr((char *)pt));
    } else {
      PUSH(ValNull());
    }
    free(ct_b);
    free(pt);
    ValFree(&aad);
    ValFree(&tag);
    ValFree(&ct);
    ValFree(&iv);
    ValFree(&key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Chacha20Poly1305Encrypt") == 0) {
    if (argc != 4) {
      fprintf(stderr,
              "Runtime error: Chacha20Poly1305Encrypt() expects 4 arguments, "
              "got %d\n",
              argc);
      return -1;
    }
    AKSValue aad = POP();
    AKSValue pt = POP();
    AKSValue iv = POP();
    AKSValue key = POP();
    if (ValKind(key) != VALSTR || ValKind(iv) != VALSTR ||
        ValKind(pt) != VALSTR || ValKind(aad) != VALSTR) {
      fprintf(
          stderr,
          "Runtime error: Chacha20Poly1305Encrypt() invalid argument types\n");
      ValFree(&aad);
      ValFree(&pt);
      ValFree(&iv);
      ValFree(&key);
      return -1;
    }
    uint8_t key_b[32], iv_b[12];
    HexDecodeC(ValToStrPtr(key)->data, key_b);
    HexDecodeC(ValToStrPtr(iv)->data, iv_b);
    size_t pt_len = ValToStrPtr(pt)->len;
    uint8_t *ct = malloc(pt_len > 0 ? pt_len : 1);
    uint8_t tag[16];
    int res = Chacha20Poly1305EncryptC(
        key_b, 32, iv_b, 12, (const uint8_t *)ValToStrPtr(aad)->data,
        ValToStrPtr(aad)->len, (const uint8_t *)ValToStrPtr(pt)->data, pt_len,
        ct, tag, 16);
    if (res) {
      char *ct_hex = malloc(pt_len * 2 + 1);
      HexEncodeC(ct, pt_len, ct_hex);
      char tag_hex[33];
      HexEncodeC(tag, 16, tag_hex);
      AKSValue struct_val = ValStruct("ChaChaPolyResult");
      AKSStruct *s = ValToObj(struct_val);
      s->fieldcount = 2;
      s->fieldnames = malloc(2 * sizeof(char *));
      s->fieldnames[0] = strdup("ciphertext");
      s->fieldnames[1] = strdup("tag");
      s->fieldvals = malloc(2 * sizeof(AKSValue));
      s->fieldvals[0] = ValStr(ct_hex);
      s->fieldvals[1] = ValStr(tag_hex);
      PUSH(struct_val);
      free(ct_hex);
    } else {
      PUSH(ValNull());
    }
    free(ct);
    ValFree(&aad);
    ValFree(&pt);
    ValFree(&iv);
    ValFree(&key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Chacha20Poly1305Decrypt") == 0) {
    if (argc != 5) {
      fprintf(stderr,
              "Runtime error: Chacha20Poly1305Decrypt() expects 5 arguments, "
              "got %d\n",
              argc);
      return -1;
    }
    AKSValue aad = POP();
    AKSValue tag = POP();
    AKSValue ct = POP();
    AKSValue iv = POP();
    AKSValue key = POP();
    if (ValKind(key) != VALSTR || ValKind(iv) != VALSTR ||
        ValKind(ct) != VALSTR || ValKind(tag) != VALSTR ||
        ValKind(aad) != VALSTR) {
      fprintf(
          stderr,
          "Runtime error: Chacha20Poly1305Decrypt() invalid argument types\n");
      ValFree(&aad);
      ValFree(&tag);
      ValFree(&ct);
      ValFree(&iv);
      ValFree(&key);
      return -1;
    }
    uint8_t key_b[32], iv_b[12], tag_b[16];
    HexDecodeC(ValToStrPtr(key)->data, key_b);
    HexDecodeC(ValToStrPtr(iv)->data, iv_b);
    HexDecodeC(ValToStrPtr(tag)->data, tag_b);
    size_t ct_len = strlen(ValToStrPtr(ct)->data) / 2;
    uint8_t *ct_b = malloc(ct_len > 0 ? ct_len : 1);
    HexDecodeC(ValToStrPtr(ct)->data, ct_b);
    uint8_t *pt = malloc(ct_len + 1);
    int res = Chacha20Poly1305DecryptC(
        key_b, 32, iv_b, 12, (const uint8_t *)ValToStrPtr(aad)->data,
        ValToStrPtr(aad)->len, ct_b, ct_len, tag_b, 16, pt);
    if (res) {
      pt[ct_len] = '\0';
      PUSH(ValStr((char *)pt));
    } else {
      PUSH(ValNull());
    }
    free(ct_b);
    free(pt);
    ValFree(&aad);
    ValFree(&tag);
    ValFree(&ct);
    ValFree(&iv);
    ValFree(&key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HMACSHA256") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: HMACSHA256() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue data = POP();
    AKSValue key = POP();
    if (ValKind(key) != VALSTR || ValKind(data) != VALSTR) {
      fprintf(stderr, "Runtime error: HMACSHA256() invalid argument types\n");
      ValFree(&data);
      ValFree(&key);
      return -1;
    }
    uint8_t digest[32];
    CryptoHMAC(BCRYPT_SHA256_ALGORITHM, (const uint8_t *)ValToStrPtr(key)->data,
               ValToStrPtr(key)->len, (const uint8_t *)ValToStrPtr(data)->data,
               ValToStrPtr(data)->len, digest, 32);
    char hashstr[65];
    HexEncodeC(digest, 32, hashstr);
    PUSH(ValStr(hashstr));
    ValFree(&data);
    ValFree(&key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HMACSHA512") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: HMACSHA512() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue data = POP();
    AKSValue key = POP();
    if (ValKind(key) != VALSTR || ValKind(data) != VALSTR) {
      fprintf(stderr, "Runtime error: HMACSHA512() invalid argument types\n");
      ValFree(&data);
      ValFree(&key);
      return -1;
    }
    uint8_t digest[64];
    CryptoHMAC(BCRYPT_SHA512_ALGORITHM, (const uint8_t *)ValToStrPtr(key)->data,
               ValToStrPtr(key)->len, (const uint8_t *)ValToStrPtr(data)->data,
               ValToStrPtr(data)->len, digest, 64);
    char hashstr[129];
    HexEncodeC(digest, 64, hashstr);
    PUSH(ValStr(hashstr));
    ValFree(&data);
    ValFree(&key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "PBKDF2") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: PBKDF2() expects 4 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue key_len_val = POP();
    AKSValue iter_val = POP();
    AKSValue salt = POP();
    AKSValue pwd = POP();
    if (ValKind(pwd) != VALSTR || ValKind(salt) != VALSTR ||
        ValKind(iter_val) != VALINT || ValKind(key_len_val) != VALINT) {
      fprintf(stderr, "Runtime error: PBKDF2() invalid argument types\n");
      ValFree(&key_len_val);
      ValFree(&iter_val);
      ValFree(&salt);
      ValFree(&pwd);
      return -1;
    }
    int iterations = (int)ValToInt(iter_val);
    int key_len = (int)ValToInt(key_len_val);
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL,
                                BCRYPT_ALG_HANDLE_HMAC_FLAG);
    uint8_t *dk = malloc(key_len);
    int res = BCryptDeriveKeyPBKDF2(
        hAlg, (PUCHAR)ValToStrPtr(pwd)->data, (ULONG)ValToStrPtr(pwd)->len,
        (PUCHAR)ValToStrPtr(salt)->data, (ULONG)ValToStrPtr(salt)->len,
        iterations, dk, key_len, 0);
    if (res == 0) {
      char *dk_hex = malloc(key_len * 2 + 1);
      HexEncodeC(dk, key_len, dk_hex);
      PUSH(ValStr(dk_hex));
      free(dk_hex);
    } else {
      PUSH(ValNull());
    }
    free(dk);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    ValFree(&key_len_val);
    ValFree(&iter_val);
    ValFree(&salt);
    ValFree(&pwd);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Scrypt") == 0) {
    if (argc != 6) {
      fprintf(stderr, "Runtime error: Scrypt() expects 6 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue key_len_val = POP();
    AKSValue p_val = POP();
    AKSValue r_val = POP();
    AKSValue n_val = POP();
    AKSValue salt = POP();
    AKSValue pwd = POP();
    if (ValKind(pwd) != VALSTR || ValKind(salt) != VALSTR ||
        ValKind(n_val) != VALINT || ValKind(r_val) != VALINT ||
        ValKind(p_val) != VALINT || ValKind(key_len_val) != VALINT) {
      fprintf(stderr, "Runtime error: Scrypt() invalid argument types\n");
      ValFree(&key_len_val);
      ValFree(&p_val);
      ValFree(&r_val);
      ValFree(&n_val);
      ValFree(&salt);
      ValFree(&pwd);
      return -1;
    }
    uint64_t N = (uint64_t)ValToInt(n_val);
    uint32_t r = (uint32_t)ValToInt(r_val);
    uint32_t p = (uint32_t)ValToInt(p_val);
    size_t key_len = (size_t)ValToInt(key_len_val);
    uint8_t *dk = malloc(key_len);
    int res = CryptoScrypt((const uint8_t *)ValToStrPtr(pwd)->data,
                           ValToStrPtr(pwd)->len,
                           (const uint8_t *)ValToStrPtr(salt)->data,
                           ValToStrPtr(salt)->len, N, r, p, dk, key_len);
    if (res) {
      char *dk_hex = malloc(key_len * 2 + 1);
      HexEncodeC(dk, key_len, dk_hex);
      PUSH(ValStr(dk_hex));
      free(dk_hex);
    } else {
      PUSH(ValNull());
    }
    free(dk);
    ValFree(&key_len_val);
    ValFree(&p_val);
    ValFree(&r_val);
    ValFree(&n_val);
    ValFree(&salt);
    ValFree(&pwd);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Argon2") == 0) {
    if (argc != 7) {
      fprintf(stderr, "Runtime error: Argon2() expects 7 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue type_val = POP();
    AKSValue key_len_val = POP();
    AKSValue p_val = POP();
    AKSValue t_val = POP();
    AKSValue m_val = POP();
    AKSValue salt = POP();
    AKSValue pwd = POP();
    if (ValKind(pwd) != VALSTR || ValKind(salt) != VALSTR ||
        ValKind(m_val) != VALINT || ValKind(t_val) != VALINT ||
        ValKind(p_val) != VALINT || ValKind(key_len_val) != VALINT ||
        ValKind(type_val) != VALSTR) {
      fprintf(stderr, "Runtime error: Argon2() invalid argument types\n");
      ValFree(&type_val);
      ValFree(&key_len_val);
      ValFree(&p_val);
      ValFree(&t_val);
      ValFree(&m_val);
      ValFree(&salt);
      ValFree(&pwd);
      return -1;
    }
    uint32_t m_cost = (uint32_t)ValToInt(m_val);
    uint32_t t_cost = (uint32_t)ValToInt(t_val);
    uint32_t parallelism = (uint32_t)ValToInt(p_val);
    size_t key_len = (size_t)ValToInt(key_len_val);
    const char *type = ValToStrPtr(type_val)->data;
    uint8_t *dk = malloc(key_len);
    int res = CryptoArgon2(
        (const uint8_t *)ValToStrPtr(pwd)->data, ValToStrPtr(pwd)->len,
        (const uint8_t *)ValToStrPtr(salt)->data, ValToStrPtr(salt)->len,
        m_cost, t_cost, parallelism, dk, key_len, type);
    if (res) {
      char *dk_hex = malloc(key_len * 2 + 1);
      HexEncodeC(dk, key_len, dk_hex);
      PUSH(ValStr(dk_hex));
      free(dk_hex);
    } else {
      PUSH(ValNull());
    }
    free(dk);
    ValFree(&type_val);
    ValFree(&key_len_val);
    ValFree(&p_val);
    ValFree(&t_val);
    ValFree(&m_val);
    ValFree(&salt);
    ValFree(&pwd);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RSAKeygen") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: RSAKeygen() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue key_size_val = POP();
    if (ValKind(key_size_val) != VALINT) {
      fprintf(stderr,
              "Runtime error: RSAKeygen() argument must be an integer\n");
      ValFree(&key_size_val);
      return -1;
    }
    int key_size = (int)ValToInt(key_size_val);
    char *pub_pem = NULL;
    char *priv_pem = NULL;
    int res = RSAGenerateKeys(key_size, &pub_pem, &priv_pem);
    if (res) {
      AKSValue struct_val = ValStruct("RsaKeyPair");
      AKSStruct *s = ValToObj(struct_val);
      s->fieldcount = 2;
      s->fieldnames = malloc(2 * sizeof(char *));
      s->fieldnames[0] = strdup("public_key");
      s->fieldnames[1] = strdup("private_key");
      s->fieldvals = malloc(2 * sizeof(AKSValue));
      s->fieldvals[0] = ValStr(pub_pem);
      s->fieldvals[1] = ValStr(priv_pem);
      PUSH(struct_val);
      free(pub_pem);
      free(priv_pem);
    } else {
      PUSH(ValNull());
    }
    ValFree(&key_size_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RSASign") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RSASign() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue msg = POP();
    AKSValue priv_key = POP();
    if (ValKind(priv_key) != VALSTR || ValKind(msg) != VALSTR) {
      fprintf(stderr, "Runtime error: RSASign() invalid argument types\n");
      ValFree(&msg);
      ValFree(&priv_key);
      return -1;
    }
    AKSSHA256CTX sha_ctx;
    uint8_t digest[32];
    Sha256Init(&sha_ctx);
    Sha256Update(&sha_ctx, (const uint8_t *)ValToStrPtr(msg)->data,
                 ValToStrPtr(msg)->len);
    Sha256Final(&sha_ctx, digest);
    size_t sig_len = 1024;
    uint8_t *sig = malloc(sig_len);
    int res =
        RSASignData(ValToStrPtr(priv_key)->data, digest, 32, sig, &sig_len);
    if (res) {
      char *sig_hex = malloc(sig_len * 2 + 1);
      HexEncodeC(sig, sig_len, sig_hex);
      PUSH(ValStr(sig_hex));
      free(sig_hex);
    } else {
      PUSH(ValNull());
    }
    free(sig);
    ValFree(&msg);
    ValFree(&priv_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RSAVerify") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: RSAVerify() expects 3 arguments, got %d\n", argc);
      return -1;
    }
    AKSValue sig_hex = POP();
    AKSValue msg = POP();
    AKSValue pub_key = POP();
    if (ValKind(pub_key) != VALSTR || ValKind(msg) != VALSTR ||
        ValKind(sig_hex) != VALSTR) {
      fprintf(stderr, "Runtime error: RSAVerify() invalid argument types\n");
      ValFree(&sig_hex);
      ValFree(&msg);
      ValFree(&pub_key);
      return -1;
    }
    AKSSHA256CTX sha_ctx;
    uint8_t digest[32];
    Sha256Init(&sha_ctx);
    Sha256Update(&sha_ctx, (const uint8_t *)ValToStrPtr(msg)->data,
                 ValToStrPtr(msg)->len);
    Sha256Final(&sha_ctx, digest);
    size_t sig_len = strlen(ValToStrPtr(sig_hex)->data) / 2;
    uint8_t *sig = malloc(sig_len > 0 ? sig_len : 1);
    HexDecodeC(ValToStrPtr(sig_hex)->data, sig);
    int res =
        RSAVerifyData(ValToStrPtr(pub_key)->data, digest, 32, sig, sig_len);
    PUSH(ValBool(res));
    free(sig);
    ValFree(&sig_hex);
    ValFree(&msg);
    ValFree(&pub_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ECDSAKeygen") == 0) {
    char *pub_pem = NULL;
    char *priv_pem = NULL;
    int res = ECCGenerateKeys(BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0, 0, 256,
                              &pub_pem, &priv_pem);
    if (res) {
      AKSValue struct_val = ValStruct("EcdsaKeyPair");
      AKSStruct *s = ValToObj(struct_val);
      s->fieldcount = 2;
      s->fieldnames = malloc(2 * sizeof(char *));
      s->fieldnames[0] = strdup("public_key");
      s->fieldnames[1] = strdup("private_key");
      s->fieldvals = malloc(2 * sizeof(AKSValue));
      s->fieldvals[0] = ValStr(pub_pem);
      s->fieldvals[1] = ValStr(priv_pem);
      PUSH(struct_val);
      free(pub_pem);
      free(priv_pem);
    } else {
      PUSH(ValNull());
    }
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ECDSASign") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: ECDSASign() expects 2 arguments, got %d\n", argc);
      return -1;
    }
    AKSValue msg = POP();
    AKSValue priv_key = POP();
    if (ValKind(priv_key) != VALSTR || ValKind(msg) != VALSTR) {
      fprintf(stderr, "Runtime error: ECDSASign() invalid argument types\n");
      ValFree(&msg);
      ValFree(&priv_key);
      return -1;
    }
    AKSSHA256CTX sha_ctx;
    uint8_t digest[32];
    Sha256Init(&sha_ctx);
    Sha256Update(&sha_ctx, (const uint8_t *)ValToStrPtr(msg)->data,
                 ValToStrPtr(msg)->len);
    Sha256Final(&sha_ctx, digest);
    size_t sig_len = 1024;
    uint8_t *sig = malloc(sig_len);
    int res =
        ECCSignData(BCRYPT_ECDSA_P256_ALGORITHM, NULL,
                    ValToStrPtr(priv_key)->data, digest, 32, sig, &sig_len);
    if (res) {
      char *sig_hex = malloc(sig_len * 2 + 1);
      HexEncodeC(sig, sig_len, sig_hex);
      PUSH(ValStr(sig_hex));
      free(sig_hex);
    } else {
      PUSH(ValNull());
    }
    free(sig);
    ValFree(&msg);
    ValFree(&priv_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ECDSAVerify") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: ECDSAVerify() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue sig_hex = POP();
    AKSValue msg = POP();
    AKSValue pub_key = POP();
    if (ValKind(pub_key) != VALSTR || ValKind(msg) != VALSTR ||
        ValKind(sig_hex) != VALSTR) {
      fprintf(stderr, "Runtime error: ECDSAVerify() invalid argument types\n");
      ValFree(&sig_hex);
      ValFree(&msg);
      ValFree(&pub_key);
      return -1;
    }
    AKSSHA256CTX sha_ctx;
    uint8_t digest[32];
    Sha256Init(&sha_ctx);
    Sha256Update(&sha_ctx, (const uint8_t *)ValToStrPtr(msg)->data,
                 ValToStrPtr(msg)->len);
    Sha256Final(&sha_ctx, digest);
    size_t sig_len = strlen(ValToStrPtr(sig_hex)->data) / 2;
    uint8_t *sig = malloc(sig_len > 0 ? sig_len : 1);
    HexDecodeC(ValToStrPtr(sig_hex)->data, sig);
    int res =
        ECCVerifyData(BCRYPT_ECDSA_P256_ALGORITHM, NULL,
                      ValToStrPtr(pub_key)->data, digest, 32, sig, sig_len);
    PUSH(ValBool(res));
    free(sig);
    ValFree(&sig_hex);
    ValFree(&msg);
    ValFree(&pub_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Ed25519Keygen") == 0) {
    char *pub_pem = NULL;
    char *priv_pem = NULL;
    int res = ECCGenerateKeys(BCRYPT_ECDSA_ALGORITHM, BCRYPT_ECC_CURVE_25519, 0,
                              0, 255, &pub_pem, &priv_pem);
    if (res) {
      AKSValue struct_val = ValStruct("Ed25519KeyPair");
      AKSStruct *s = ValToObj(struct_val);
      s->fieldcount = 2;
      s->fieldnames = malloc(2 * sizeof(char *));
      s->fieldnames[0] = strdup("public_key");
      s->fieldnames[1] = strdup("private_key");
      s->fieldvals = malloc(2 * sizeof(AKSValue));
      s->fieldvals[0] = ValStr(pub_pem);
      s->fieldvals[1] = ValStr(priv_pem);
      PUSH(struct_val);
      free(pub_pem);
      free(priv_pem);
    } else {
      PUSH(ValNull());
    }
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Ed25519Sign") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: Ed25519Sign() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue msg = POP();
    AKSValue priv_key = POP();
    if (ValKind(priv_key) != VALSTR || ValKind(msg) != VALSTR) {
      fprintf(stderr, "Runtime error: Ed25519Sign() invalid argument types\n");
      ValFree(&msg);
      ValFree(&priv_key);
      return -1;
    }
    AKSSHA256CTX sha_ctx;
    uint8_t digest[32];
    Sha256Init(&sha_ctx);
    Sha256Update(&sha_ctx, (const uint8_t *)ValToStrPtr(msg)->data,
                 ValToStrPtr(msg)->len);
    Sha256Final(&sha_ctx, digest);
    size_t sig_len = 1024;
    uint8_t *sig = malloc(sig_len);
    int res =
        ECCSignData(BCRYPT_ECDSA_ALGORITHM, BCRYPT_ECC_CURVE_25519,
                    ValToStrPtr(priv_key)->data, digest, 32, sig, &sig_len);
    if (res) {
      char *sig_hex = malloc(sig_len * 2 + 1);
      HexEncodeC(sig, sig_len, sig_hex);
      PUSH(ValStr(sig_hex));
      free(sig_hex);
    } else {
      PUSH(ValNull());
    }
    free(sig);
    ValFree(&msg);
    ValFree(&priv_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Ed25519Verify") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: Ed25519Verify() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue sig_hex = POP();
    AKSValue msg = POP();
    AKSValue pub_key = POP();
    if (ValKind(pub_key) != VALSTR || ValKind(msg) != VALSTR ||
        ValKind(sig_hex) != VALSTR) {
      fprintf(stderr,
              "Runtime error: Ed25519Verify() invalid argument types\n");
      ValFree(&sig_hex);
      ValFree(&msg);
      ValFree(&pub_key);
      return -1;
    }
    AKSSHA256CTX sha_ctx;
    uint8_t digest[32];
    Sha256Init(&sha_ctx);
    Sha256Update(&sha_ctx, (const uint8_t *)ValToStrPtr(msg)->data,
                 ValToStrPtr(msg)->len);
    Sha256Final(&sha_ctx, digest);
    size_t sig_len = strlen(ValToStrPtr(sig_hex)->data) / 2;
    uint8_t *sig = malloc(sig_len > 0 ? sig_len : 1);
    HexDecodeC(ValToStrPtr(sig_hex)->data, sig);
    int res =
        ECCVerifyData(BCRYPT_ECDSA_ALGORITHM, BCRYPT_ECC_CURVE_25519,
                      ValToStrPtr(pub_key)->data, digest, 32, sig, sig_len);
    PUSH(ValBool(res));
    free(sig);
    ValFree(&sig_hex);
    ValFree(&msg);
    ValFree(&pub_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "X25519Keygen") == 0) {
    char pub_hex[65], priv_hex[65];
    int res = Curve25519GenerateHex(BCRYPT_ECDH_ALGORITHM, pub_hex, priv_hex);
    if (res) {
      AKSValue struct_val = ValStruct("X25519KeyPair");
      AKSStruct *s = ValToObj(struct_val);
      s->fieldcount = 2;
      s->fieldnames = malloc(2 * sizeof(char *));
      s->fieldnames[0] = strdup("public_key");
      s->fieldnames[1] = strdup("private_key");
      s->fieldvals = malloc(2 * sizeof(AKSValue));
      s->fieldvals[0] = ValStr(pub_hex);
      s->fieldvals[1] = ValStr(priv_hex);
      PUSH(struct_val);
    } else {
      PUSH(ValNull());
    }
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "X25519DH") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: X25519DH() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue peer_pub = POP();
    AKSValue priv_key = POP();
    if (ValKind(priv_key) != VALSTR || ValKind(peer_pub) != VALSTR) {
      fprintf(stderr, "Runtime error: X25519DH() invalid argument types\n");
      ValFree(&peer_pub);
      ValFree(&priv_key);
      return -1;
    }
    char secret_hex[65];
    int res = X25519SharedSecret(ValToStrPtr(priv_key)->data,
                                 ValToStrPtr(peer_pub)->data, secret_hex);
    if (res) {
      PUSH(ValStr(secret_hex));
    } else {
      PUSH(ValNull());
    }
    ValFree(&peer_pub);
    ValFree(&priv_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SecureRandom") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SecureRandom() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue size_val = POP();
    if (ValKind(size_val) != VALINT) {
      fprintf(stderr,
              "Runtime error: SecureRandom() argument must be an integer\n");
      ValFree(&size_val);
      return -1;
    }
    int size = (int)ValToInt(size_val);
    uint8_t *buf = malloc(size > 0 ? size : 1);
    int res = CryptoSecureRandom(buf, size);
    if (res) {
      char *hex = malloc(size * 2 + 1);
      HexEncodeC(buf, size, hex);
      PUSH(ValStr(hex));
      free(hex);
    } else {
      PUSH(ValNull());
    }
    free(buf);
    ValFree(&size_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HexEncode") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: HexEncode() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: HexEncode() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    size_t len = ValToStrPtr(arg)->len;
    char *hex = malloc(len * 2 + 1);
    HexEncodeC((const uint8_t *)ValToStrPtr(arg)->data, len, hex);
    PUSH(ValStr(hex));
    free(hex);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HexDecode") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: HexDecode() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: HexDecode() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    const char *hex = ValToStrPtr(arg)->data;
    size_t hex_len = strlen(hex);
    size_t bin_len = hex_len / 2;
    uint8_t *bin = malloc(bin_len + 1);
    int res = HexDecodeC(hex, bin);
    if (res) {
      bin[bin_len] = '\0';
      AKSValue str = ValStr("");
      free(ValToStrPtr(str)->data);
      ValToStrPtr(str)->data = (char *)bin;
      ValToStrPtr(str)->len = (int)bin_len;
      PUSH(str);
    } else {
      free(bin);
      PUSH(ValNull());
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "B64Enc") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: B64Enc() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: B64Enc() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    char *enc = Base64EncodeStr((const uint8_t *)ValToStrPtr(arg)->data,
                                ValToStrPtr(arg)->len);
    if (enc) {
      PUSH(ValStr(enc));
      free(enc);
    } else {
      PUSH(ValNull());
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "B64Dec") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: B64Dec() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: B64Dec() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    size_t outlen = 0;
    uint8_t *dec = Base64DecodeStr(ValToStrPtr(arg)->data, &outlen);
    if (dec) {
      PUSH(ValStrLen((const char *)dec, (int)outlen));
      free(dec);
    } else {
      PUSH(ValNull());
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UrlEnc") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: UrlEnc() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: UrlEnc() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    char *enc = UrlEncodeStr(ValToStrPtr(arg)->data);
    if (enc) {
      PUSH(ValStr(enc));
      free(enc);
    } else {
      PUSH(ValNull());
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UrlDec") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: UrlDec() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: UrlDec() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    char *dec = UrlDecodeStr(ValToStrPtr(arg)->data);
    if (dec) {
      PUSH(ValStr(dec));
      free(dec);
    } else {
      PUSH(ValNull());
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "date") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: date() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue fmtval = POP();
    AKSValue tsval = POP();
    if (ValKind(fmtval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: date() format argument must be a string\n");
      ValFree(&fmtval);
      ValFree(&tsval);
      return -1;
    }
    double tsdouble = ValKind(tsval) == VALFLOAT ? ValToFloat(tsval)
                                                 : (double)ValToInt(tsval);
    time_t t = (time_t)tsdouble;
    struct tm *timeinfo = localtime(&t);
    char buf[256];
    if (timeinfo &&
        strftime(buf, sizeof(buf), ValToStrPtr(fmtval)->data, timeinfo)) {
      PUSH(ValStr(buf));
    } else {
      PUSH(ValStr(""));
    }
    ValFree(&fmtval);
    ValFree(&tsval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "platform") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: platform() expects 0 arguments, got %d\n",
              argc);
      return -1;
    }
#ifdef _WIN32
    PUSH(ValStr("windows"));
#elif __APPLE__
    PUSH(ValStr("macos"));
#elif __linux__
    PUSH(ValStr("linux"));
#else
    PUSH(ValStr("unknown"));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CurrentWorkingDirectory") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: CurrentWorkingDirectory() expects 0 arguments, "
              "got %d\n",
              argc);
      return -1;
    }
    char *dir = GetCwdStr();
    PUSH(ValStr(dir));
    free(dir);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FFILoad") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FFILoad() expects 1 or 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: FFILoad() argument must be a string\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (vm->sandboxenabled) {
      fprintf(stderr, "Security error: FFI disabled in sandbox mode\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (vm->inrestrictedeval) {
      fprintf(stderr, "Security error: FFI disabled in restricted eval mode\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMFFI)) {
      fprintf(stderr, "Security error: FFI permission denied\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "ffi", "ffi", NULL)) {
      fprintf(stderr, "Security error: invalid FFI capability\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);
    int64_t libhandle = 0;
#ifdef _WIN32
    libhandle = (int64_t)LoadLibraryA(ValToStrPtr(arg)->data);
#else
    libhandle = (int64_t)dlopen(ValToStrPtr(arg)->data, RTLDLAZY);
#endif
    PUSH(ValInt(libhandle));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FFIFree") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FFIFree() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALINT) {
      fprintf(stderr, "Runtime error: FFIFree() handle must be an integer\n");
      return -1;
    }
    int64_t libhandle = ValToInt(arg);
    if (libhandle != 0) {
#ifdef _WIN32
      FreeLibrary((HMODULE)libhandle);
#else
      dlclose((void *)libhandle);
#endif
    }
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FFICall") == 0) {
    if (argc != 4) {
      fprintf(stderr,
              "Runtime error: FFICall() expects 4 arguments (handle, "
              "symbolname, signature, argsarr), got %d\n",
              argc);
      return -1;
    }
    AKSValue argsarrval = POP();
    AKSValue sigval = POP();
    AKSValue symval = POP();
    AKSValue handleval = POP();

    if (ValKind(handleval) != VALINT || ValKind(symval) != VALSTR ||
        ValKind(sigval) != VALSTR || ValKind(argsarrval) != VALARR) {
      fprintf(stderr, "Runtime error: FFICall() invalid argument types\n");
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&symval);
      ValFree(&handleval);
      return -1;
    }

    int64_t handle = ValToInt(handleval);
    const char *symbolname = ValToStrPtr(symval)->data;
    const char *sig = ValToStrPtr(sigval)->data;
    AKSArray *argsarr = ValToArr(argsarrval);

    if (handle == 0) {
      fprintf(stderr, "Runtime error: FFICall() library handle is null\n");
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&symval);
      ValFree(&handleval);
      return -1;
    }

    void *proc = NULL;
#ifdef _WIN32
    proc = (void *)GetProcAddress((HMODULE)handle, symbolname);
#else
    proc = dlsym((void *)handle, symbolname);
#endif
    if (!proc) {
      fprintf(stderr, "Runtime error: FFICall() symbol '%s' not found\n",
              symbolname);
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&symval);
      ValFree(&handleval);
      return -1;
    }

    char paramtypes[16];
    int paramcount = 0;
    char rettype = sig[0];
    const char *psig = strchr(sig, '(');
    if (psig) {
      psig++;
      while (*psig && *psig != ')') {
        char t = *psig++;
        if (t == 'i' || t == 's' || t == 'p') {
          paramtypes[paramcount++] = 'i';
        } else if (t == 'd' || t == 'f') {
          paramtypes[paramcount++] = 'd';
        }
      }
    }

    if (paramcount != argsarr->count) {
      fprintf(stderr,
              "Runtime error: FFICall() signature expects %d arguments, but "
              "arguments array has %d elements\n",
              paramcount, argsarr->count);
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&symval);
      ValFree(&handleval);
      return -1;
    }

    union FFIVal {
      int64_t i;
      double d;
    } ffiargs[6];
    int mask = 0;

    for (int i = 0; i < paramcount && i < 6; i++) {
      AKSValue v = argsarr->items[i];
      if (paramtypes[i] == 'i') {
        if (ValKind(v) == VALSTR) {
          ffiargs[i].i = (int64_t)ValToStrPtr(v)->data;
        } else if (ValKind(v) == VALINT) {
          ffiargs[i].i = ValToInt(v);
        } else if (ValKind(v) == VALNULL) {
          ffiargs[i].i = 0;
        } else if (ValKind(v) == VALBOOL) {
          ffiargs[i].i = ValToBool(v);
        } else {
          ffiargs[i].i = 0;
        }
      } else {
        if (ValKind(v) == VALFLOAT) {
          ffiargs[i].d = ValToFloat(v);
          mask |= (1 << i);
        } else if (ValKind(v) == VALINT) {
          ffiargs[i].d = (double)ValToInt(v);
          mask |= (1 << i);
        } else {
          ffiargs[i].d = 0.0;
          mask |= (1 << i);
        }
      }
    }

    union FFIVal retval;
    retval.i = 0;

    if (paramcount == 0) {
      if (rettype == 'd' || rettype == 'f') {
        retval.d = ((double (*)(void))proc)();
      } else {
        retval.i = ((int64_t (*)(void))proc)();
      }
    } else if (paramcount == 1) {
      if (mask == 0) {
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(int64_t))proc)(ffiargs[0].i);
        else
          retval.i = ((int64_t (*)(int64_t))proc)(ffiargs[0].i);
      } else {
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(double))proc)(ffiargs[0].d);
        else
          retval.i = ((int64_t (*)(double))proc)(ffiargs[0].d);
      }
    } else if (paramcount == 2) {
      switch (mask) {
      case 0:
        if (rettype == 'd' || rettype == 'f')
          retval.d =
              ((double (*)(int64_t, int64_t))proc)(ffiargs[0].i, ffiargs[1].i);
        else
          retval.i =
              ((int64_t (*)(int64_t, int64_t))proc)(ffiargs[0].i, ffiargs[1].i);
        break;
      case 1:
        if (rettype == 'd' || rettype == 'f')
          retval.d =
              ((double (*)(double, int64_t))proc)(ffiargs[0].d, ffiargs[1].i);
        else
          retval.i =
              ((int64_t (*)(double, int64_t))proc)(ffiargs[0].d, ffiargs[1].i);
        break;
      case 2:
        if (rettype == 'd' || rettype == 'f')
          retval.d =
              ((double (*)(int64_t, double))proc)(ffiargs[0].i, ffiargs[1].d);
        else
          retval.i =
              ((int64_t (*)(int64_t, double))proc)(ffiargs[0].i, ffiargs[1].d);
        break;
      case 3:
        if (rettype == 'd' || rettype == 'f')
          retval.d =
              ((double (*)(double, double))proc)(ffiargs[0].d, ffiargs[1].d);
        else
          retval.i =
              ((int64_t (*)(double, double))proc)(ffiargs[0].d, ffiargs[1].d);
        break;
      }
    } else if (paramcount == 3) {
      switch (mask) {
      case 0:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(int64_t, int64_t, int64_t))proc)(
              ffiargs[0].i, ffiargs[1].i, ffiargs[2].i);
        else
          retval.i = ((int64_t (*)(int64_t, int64_t, int64_t))proc)(
              ffiargs[0].i, ffiargs[1].i, ffiargs[2].i);
        break;
      case 7:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(double, double, double))proc)(
              ffiargs[0].d, ffiargs[1].d, ffiargs[2].d);
        else
          retval.i = ((int64_t (*)(double, double, double))proc)(
              ffiargs[0].d, ffiargs[1].d, ffiargs[2].d);
        break;
      default:
        if (rettype == 'd' || rettype == 'f') {
          retval.d = ((double (*)(double, double, double))proc)(
              (mask & 1) ? ffiargs[0].d : (double)ffiargs[0].i,
              (mask & 2) ? ffiargs[1].d : (double)ffiargs[1].i,
              (mask & 4) ? ffiargs[2].d : (double)ffiargs[2].i);
        } else {
          retval.i = ((int64_t (*)(int64_t, int64_t, int64_t))proc)(
              (mask & 1) ? (int64_t)ffiargs[0].d : ffiargs[0].i,
              (mask & 2) ? (int64_t)ffiargs[1].d : ffiargs[1].i,
              (mask & 4) ? (int64_t)ffiargs[2].d : ffiargs[2].i);
        }
        break;
      }
    } else {
      if (rettype == 'd' || rettype == 'f') {
        retval.d = ((double (*)(double, double, double, double))proc)(
            (mask & 1) ? ffiargs[0].d : (double)ffiargs[0].i,
            (mask & 2) ? ffiargs[1].d : (double)ffiargs[1].i,
            (mask & 4) ? ffiargs[2].d : (double)ffiargs[2].i,
            (mask & 8) ? ffiargs[3].d : (double)ffiargs[3].i);
      } else {
        retval.i = ((int64_t (*)(int64_t, int64_t, int64_t, int64_t))proc)(
            (mask & 1) ? (int64_t)ffiargs[0].d : ffiargs[0].i,
            (mask & 2) ? (int64_t)ffiargs[1].d : ffiargs[1].i,
            (mask & 4) ? (int64_t)ffiargs[2].d : ffiargs[2].i,
            (mask & 8) ? (int64_t)ffiargs[3].d : ffiargs[3].i);
      }
    }

    if (rettype == 'v') {
      PUSH(ValNull());
    } else if (rettype == 'd' || rettype == 'f') {
      PUSH(ValFloat(retval.d));
    } else if (rettype == 's') {
      PUSH(ValStr((const char *)retval.i));
    } else {
      PUSH(ValInt(retval.i));
    }

    ValFree(&argsarrval);
    ValFree(&sigval);
    ValFree(&symval);
    ValFree(&handleval);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "COMCall") == 0) {
    if (argc != 4) {
      fprintf(stderr,
              "Runtime error: COMCall() expects 4 arguments (interface_ptr, "
              "method_index, signature, argsarr), got %d\n",
              argc);
      return -1;
    }
    AKSValue argsarrval = POP();
    AKSValue sigval = POP();
    AKSValue idxval = POP();
    AKSValue ptrval = POP();

    if ((ValKind(ptrval) != VALINT && ValKind(ptrval) != VALNULL) ||
        ValKind(idxval) != VALINT || ValKind(sigval) != VALSTR ||
        ValKind(argsarrval) != VALARR) {
      fprintf(stderr, "Runtime error: COMCall() invalid argument types\n");
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&idxval);
      ValFree(&ptrval);
      return -1;
    }

    void *interface_ptr = (void *)ValToInt(ptrval);
    int method_index = (int)ValToInt(idxval);
    const char *sig = ValToStrPtr(sigval)->data;
    AKSArray *argsarr = ValToArr(argsarrval);

    if (!interface_ptr) {
      fprintf(stderr, "Runtime error: COMCall() interface pointer is null\n");
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&idxval);
      ValFree(&ptrval);
      return -1;
    }

    void **vtable = *(void ***)interface_ptr;
    void *proc = vtable[method_index];
    if (!proc) {
      fprintf(
          stderr,
          "Runtime error: COMCall() method at index %d not found in vtable\n",
          method_index);
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&idxval);
      ValFree(&ptrval);
      return -1;
    }

    char paramtypes[16];
    int paramcount = 0;
    char rettype = sig[0];
    const char *psig = strchr(sig, '(');
    if (psig) {
      psig++;
      while (*psig && *psig != ')') {
        char t = *psig++;
        if (t == 'i' || t == 's' || t == 'p') {
          paramtypes[paramcount++] = 'i';
        } else if (t == 'd' || t == 'f') {
          paramtypes[paramcount++] = 'd';
        }
      }
    }

    if (paramcount != argsarr->count) {
      fprintf(stderr,
              "Runtime error: COMCall() signature expects %d arguments, but "
              "arguments array has %d elements\n",
              paramcount, argsarr->count);
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&idxval);
      ValFree(&ptrval);
      return -1;
    }

    union FFIVal {
      int64_t i;
      double d;
    } ffiargs[6];
    int mask = 0;

    for (int i = 0; i < paramcount && i < 6; i++) {
      AKSValue v = argsarr->items[i];
      if (paramtypes[i] == 'i') {
        if (ValKind(v) == VALSTR) {
          ffiargs[i].i = (int64_t)ValToStrPtr(v)->data;
        } else if (ValKind(v) == VALINT) {
          ffiargs[i].i = ValToInt(v);
        } else if (ValKind(v) == VALNULL) {
          ffiargs[i].i = 0;
        } else if (ValKind(v) == VALBOOL) {
          ffiargs[i].i = ValToBool(v);
        } else {
          ffiargs[i].i = 0;
        }
      } else {
        if (ValKind(v) == VALFLOAT) {
          ffiargs[i].d = ValToFloat(v);
          mask |= (1 << i);
        } else if (ValKind(v) == VALINT) {
          ffiargs[i].d = (double)ValToInt(v);
          mask |= (1 << i);
        } else {
          ffiargs[i].d = 0.0;
          mask |= (1 << i);
        }
      }
    }

    union FFIVal retval;
    retval.i = 0;

    if (paramcount == 0) {
      if (rettype == 'd' || rettype == 'f') {
        retval.d = ((double (*)(void *))proc)(interface_ptr);
      } else {
        retval.i = ((int64_t (*)(void *))proc)(interface_ptr);
      }
    } else if (paramcount == 1) {
      if (mask == 0) {
        if (rettype == 'd' || rettype == 'f')
          retval.d =
              ((double (*)(void *, int64_t))proc)(interface_ptr, ffiargs[0].i);
        else
          retval.i =
              ((int64_t (*)(void *, int64_t))proc)(interface_ptr, ffiargs[0].i);
      } else {
        if (rettype == 'd' || rettype == 'f')
          retval.d =
              ((double (*)(void *, double))proc)(interface_ptr, ffiargs[0].d);
        else
          retval.i =
              ((int64_t (*)(void *, double))proc)(interface_ptr, ffiargs[0].d);
      }
    } else if (paramcount == 2) {
      switch (mask) {
      case 0:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(void *, int64_t, int64_t))proc)(
              interface_ptr, ffiargs[0].i, ffiargs[1].i);
        else
          retval.i = ((int64_t (*)(void *, int64_t, int64_t))proc)(
              interface_ptr, ffiargs[0].i, ffiargs[1].i);
        break;
      case 1:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(void *, double, int64_t))proc)(
              interface_ptr, ffiargs[0].d, ffiargs[1].i);
        else
          retval.i = ((int64_t (*)(void *, double, int64_t))proc)(
              interface_ptr, ffiargs[0].d, ffiargs[1].i);
        break;
      case 2:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(void *, int64_t, double))proc)(
              interface_ptr, ffiargs[0].i, ffiargs[1].d);
        else
          retval.i = ((int64_t (*)(void *, int64_t, double))proc)(
              interface_ptr, ffiargs[0].i, ffiargs[1].d);
        break;
      case 3:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(void *, double, double))proc)(
              interface_ptr, ffiargs[0].d, ffiargs[1].d);
        else
          retval.i = ((int64_t (*)(void *, double, double))proc)(
              interface_ptr, ffiargs[0].d, ffiargs[1].d);
        break;
      }
    } else if (paramcount == 3) {
      switch (mask) {
      case 0:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(void *, int64_t, int64_t, int64_t))proc)(
              interface_ptr, ffiargs[0].i, ffiargs[1].i, ffiargs[2].i);
        else
          retval.i = ((int64_t (*)(void *, int64_t, int64_t, int64_t))proc)(
              interface_ptr, ffiargs[0].i, ffiargs[1].i, ffiargs[2].i);
        break;
      case 7:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(void *, double, double, double))proc)(
              interface_ptr, ffiargs[0].d, ffiargs[1].d, ffiargs[2].d);
        else
          retval.i = ((int64_t (*)(void *, double, double, double))proc)(
              interface_ptr, ffiargs[0].d, ffiargs[1].d, ffiargs[2].d);
        break;
      default:
        if (rettype == 'd' || rettype == 'f') {
          retval.d = ((double (*)(void *, double, double, double))proc)(
              interface_ptr, (mask & 1) ? ffiargs[0].d : (double)ffiargs[0].i,
              (mask & 2) ? ffiargs[1].d : (double)ffiargs[1].i,
              (mask & 4) ? ffiargs[2].d : (double)ffiargs[2].i);
        } else {
          retval.i = ((int64_t (*)(void *, int64_t, int64_t, int64_t))proc)(
              interface_ptr, (mask & 1) ? (int64_t)ffiargs[0].d : ffiargs[0].i,
              (mask & 2) ? (int64_t)ffiargs[1].d : ffiargs[1].i,
              (mask & 4) ? (int64_t)ffiargs[2].d : ffiargs[2].i);
        }
        break;
      }
    } else {
      if (rettype == 'd' || rettype == 'f') {
        retval.d = ((double (*)(void *, double, double, double, double))proc)(
            interface_ptr, (mask & 1) ? ffiargs[0].d : (double)ffiargs[0].i,
            (mask & 2) ? ffiargs[1].d : (double)ffiargs[1].i,
            (mask & 4) ? ffiargs[2].d : (double)ffiargs[2].i,
            (mask & 8) ? ffiargs[3].d : (double)ffiargs[3].i);
      } else {
        retval.i =
            ((int64_t (*)(void *, int64_t, int64_t, int64_t, int64_t))proc)(
                interface_ptr,
                (mask & 1) ? (int64_t)ffiargs[0].d : ffiargs[0].i,
                (mask & 2) ? (int64_t)ffiargs[1].d : ffiargs[1].i,
                (mask & 4) ? (int64_t)ffiargs[2].d : ffiargs[2].i,
                (mask & 8) ? (int64_t)ffiargs[3].d : ffiargs[3].i);
      }
    }

    if (rettype == 'v') {
      PUSH(ValNull());
    } else if (rettype == 'd' || rettype == 'f') {
      PUSH(ValFloat(retval.d));
    } else if (rettype == 's') {
      PUSH(ValStr((const char *)retval.i));
    } else {
      PUSH(ValInt(retval.i));
    }

    ValFree(&argsarrval);
    ValFree(&sigval);
    ValFree(&idxval);
    ValFree(&ptrval);
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
  if (strcmp(name, "send") == 0 || strcmp(name, "Send") == 0 || strcmp(name, "ChannelSend") == 0) {
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
        reader->stack[reader->stacktop - 1] = ValCopy(val);
        reader->rendezvous_done = 1;

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
      c->buffer[(c->head + c->count) % c->capacity] = ValCopy(val);
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
  if (strcmp(name, "Receive") == 0 || strcmp(name, "receive") == 0 || strcmp(name, "ChannelReceive") == 0) {
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
              (ValKind(chanval) == VALINT ? ValToInt(chanval) : 0));
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
  if (strcmp(name, "close") == 0 || strcmp(name, "Close") == 0 || strcmp(name, "ChannelClose") == 0) {
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
      if (m->blockedcount > 0) {
        VMTask *nexttask = m->blockedtasks[0];
        for (int i = 0; i < m->blockedcount - 1; i++) {
          m->blockedtasks[i] = m->blockedtasks[i + 1];
        }
        m->blockedcount--;
        m->owner = nexttask;
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
      fprintf(stderr, "Runtime error: Semaphore() argument must be an integer\n");
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
        fprintf(stderr,
                "Runtime error: AtomAdd() target array element is not an integer\n");
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
        fprintf(stderr,
                "Runtime error: AtomSub() target array element is not an integer\n");
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
      fprintf(stderr, "Runtime error: EffectHandle() expects 2 arguments, got %d\n", argc);
      return -1;
    }
    AKSValue hmap = POP();
    AKSValue thunk = POP();
    if (ValKind(thunk) != VALFN) {
      fprintf(stderr, "Runtime error: EffectHandle() first argument must be a function\n");
      ValFree(&hmap);
      ValFree(&thunk);
      return -1;
    }
    int fnid = ValToFn(thunk);
    if (fnid < 0 || fnid >= vm->fncount) {
      fprintf(stderr, "Runtime error: EffectHandle() invalid function id %d\n", fnid);
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
      fprintf(stderr, "Runtime error: EffectPerform() expects 2 arguments, got %d\n", argc);
      return -1;
    }
    AKSValue argsval = POP();
    AKSValue opval = POP();
    if (ValKind(opval) != VALSTR) {
      fprintf(stderr, "Runtime error: EffectPerform() first argument must be a string op name\n");
      ValFree(&argsval);
      ValFree(&opval);
      return -1;
    }
    AKSString *opstr = ValToStrPtr(opval);
    const char *opname = (opstr && opstr->data) ? opstr->data : "";
    const char *bare_op = strrchr(opname, '.');
    if (bare_op) bare_op++; else bare_op = opname;

    AKSValue handler_fn = ValNull();
    int found_handler_depth = -1;
    for (int h = task->effect_handler_count - 1; h >= 0; h--) {
      AKSValue hm = task->effect_handlers[h];
      if (ValKind(hm) == VALSTRUCT) {
        AKSStruct *st = ValToObj(hm);
        for (int f = 0; f < st->fieldcount; f++) {
          if (strcmp(st->fieldnames[f], opname) == 0 || strcmp(st->fieldnames[f], bare_op) == 0) {
            handler_fn = st->fieldvals[f];
            found_handler_depth = h;
            break;
          }
        }
      }
      if (found_handler_depth >= 0) break;
    }

    if (found_handler_depth < 0 || (ValKind(handler_fn) != VALFN && ValKind(handler_fn) != VALBOUNDMETHOD)) {
      fprintf(stderr, "Runtime error: Unhandled algebraic effect operation '%s'\n", opname);
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
    return ExecuteStdlib(vm, task, "send", 2, stacktopptr);
  }
  if (strcmp(name, "ReinterpretCast") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: ReinterpretCast() expects at least 2 arguments (type, buffer, [offset])\n");
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

    const char *type_name = (ValKind(type_val) == VALSTR) ? ValToStrPtr(type_val)->data : "any";
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
        for (int i = 0; i < vm->structs[sidx].fc && (offset + i) < arr->count; i++) {
          StructSetFieldNative(st, vm->structs[sidx].fnames[i], arr->items[offset + i]);
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
        for (int i = 0; i < vm->structs[sidx].fc && (offset + i) < (int64_t)s->len; i++) {
          StructSetFieldNative(st, vm->structs[sidx].fnames[i], ValInt((uint8_t)s->data[offset + i]));
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
      fprintf(stderr, "Runtime error: spawn_remote() expects 2 arguments (node_address, handler_fn)\n");
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
  if (strcmp(name, "TapeNew") == 0) {
    AutodiffInitTapes();
    int slot = -1;
    for (int i = 1; i < MAX_AUTODIFF_TAPES; i++) {
      if (!g_autodiff_tapes[i].active) {
        slot = i;
        break;
      }
    }
    if (slot < 0) slot = 0;
    AutodiffTape *t = AutodiffGetTape(slot);
    t->count = 0;
    PUSH(ValInt(slot));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeFree") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: TapeFree() expects 1 argument (tape_id)\n");
      return -1;
    }
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    ValFree(&tid_val);
    if (tid > 0 && tid < MAX_AUTODIFF_TAPES && g_autodiff_tapes[tid].active) {
      AutodiffFreeTapeNodes(&g_autodiff_tapes[tid]);
      if (g_autodiff_tapes[tid].nodes) free(g_autodiff_tapes[tid].nodes);
      g_autodiff_tapes[tid].nodes = NULL;
      g_autodiff_tapes[tid].count = 0;
      g_autodiff_tapes[tid].capacity = 0;
      g_autodiff_tapes[tid].active = 0;
    }
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeDefault") == 0) {
    PUSH(ValInt(0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeVar") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: TapeVar() expects 2 arguments (tape_id, val)\n");
      return -1;
    }
    AKSValue val_val = POP();
    AKSValue tape_val = POP();
    int tid = (int)ValToInt(tape_val);
    double v = (ValKind(val_val) == VALFLOAT) ? ValToFloat(val_val) : (double)ValToInt(val_val);
    ValFree(&val_val);
    ValFree(&tape_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    int nid = AutodiffAllocNode(t, 0, v, -1, -1, 0.0);
    PUSH(ValInt(nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeOp") == 0) {
    if (argc < 5) {
      fprintf(stderr, "Runtime error: TapeOp() expects 5 arguments (tape_id, op, lhs, rhs, aux)\n");
      return -1;
    }
    AKSValue aux_val = POP();
    AKSValue rhs_val = POP();
    AKSValue lhs_val = POP();
    AKSValue op_val = POP();
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    int op = (int)ValToInt(op_val);
    int lhs = (int)ValToInt(lhs_val);
    int rhs = (int)ValToInt(rhs_val);
    double aux = (ValKind(aux_val) == VALFLOAT) ? ValToFloat(aux_val) : (double)ValToInt(aux_val);
    ValFree(&aux_val);
    ValFree(&rhs_val);
    ValFree(&lhs_val);
    ValFree(&op_val);
    ValFree(&tid_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    int nid = AutodiffForwardOp(t, op, lhs, rhs, aux);
    PUSH(ValInt(nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeGetVal") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: TapeGetVal() expects 2 arguments (tape_id, nid)\n");
      return -1;
    }
    AKSValue nid_val = POP();
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    int nid = (int)ValToInt(nid_val);
    ValFree(&nid_val);
    ValFree(&tid_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    double res = (nid >= 0 && nid < t->count) ? t->nodes[nid].val : 0.0;
    PUSH(ValFloat(res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeGetGrad") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: TapeGetGrad() expects 2 arguments (tape_id, nid)\n");
      return -1;
    }
    AKSValue nid_val = POP();
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    int nid = (int)ValToInt(nid_val);
    ValFree(&nid_val);
    ValFree(&tid_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    double res = (nid >= 0 && nid < t->count) ? t->nodes[nid].grad : 0.0;
    PUSH(ValFloat(res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeBackward") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: TapeBackward() expects 2 arguments (tape_id, loss_nid)\n");
      return -1;
    }
    AKSValue nid_val = POP();
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    int nid = (int)ValToInt(nid_val);
    ValFree(&nid_val);
    ValFree(&tid_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    AutodiffBackward(t, nid);
    PUSH(ValFloat(1.0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeTensorVar") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Runtime error: TapeTensorVar() expects 4 arguments (tape_id, rows, cols, data_array)\n");
      return -1;
    }
    AKSValue arr_val = POP();
    AKSValue cols_val = POP();
    AKSValue rows_val = POP();
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    int rows = (int)ValToInt(rows_val);
    int cols = (int)ValToInt(cols_val);
    ValFree(&tid_val);
    ValFree(&rows_val);
    ValFree(&cols_val);
    int total = rows * cols;
    double *buf = (double *)calloc(total > 0 ? total : 1, sizeof(double));
    if (ValKind(arr_val) == VALARR) {
      AKSArray *oa = ValToArr(arr_val);
      for (int k = 0; k < total && k < oa->count; k++) {
        buf[k] = (ValKind(oa->items[k]) == VALFLOAT) ? ValToFloat(oa->items[k]) : (double)ValToInt(oa->items[k]);
      }
    }
    ValFree(&arr_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    int nid = AutodiffAllocTensorNode(t, 17 /* TENSOR_VAR */, rows, cols, -1, -1, buf);
    free(buf);
    PUSH(ValInt(nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeTensorMatMul") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Runtime error: TapeTensorMatMul() expects 3 arguments (tape_id, lhs_nid, rhs_nid)\n");
      return -1;
    }
    AKSValue rhs_val = POP();
    AKSValue lhs_val = POP();
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    int lhs_nid = (int)ValToInt(lhs_val);
    int rhs_nid = (int)ValToInt(rhs_val);
    ValFree(&rhs_val);
    ValFree(&lhs_val);
    ValFree(&tid_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    TapeNode *ln = &t->nodes[lhs_nid];
    TapeNode *rn = &t->nodes[rhs_nid];
    int M = ln->rows;
    int K = ln->cols;
    int N = rn->cols;
    double *C = (double *)calloc(M * N > 0 ? M * N : 1, sizeof(double));
    for (int r = 0; r < M; r++) {
      for (int k = 0; k < K; k++) {
        double av = ln->tval[r * K + k];
        for (int c = 0; c < N; c++) {
          C[r * N + c] += av * rn->tval[k * N + c];
        }
      }
    }
    int nid = AutodiffAllocTensorNode(t, 18 /* TENSOR_MATMUL */, M, N, lhs_nid, rhs_nid, C);
    free(C);
    PUSH(ValInt(nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeTensorAdd") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Runtime error: TapeTensorAdd() expects 3 arguments (tape_id, lhs_nid, rhs_nid)\n");
      return -1;
    }
    AKSValue rhs_val = POP();
    AKSValue lhs_val = POP();
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    int lhs_nid = (int)ValToInt(lhs_val);
    int rhs_nid = (int)ValToInt(rhs_val);
    ValFree(&rhs_val);
    ValFree(&lhs_val);
    ValFree(&tid_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    TapeNode *ln = &t->nodes[lhs_nid];
    TapeNode *rn = &t->nodes[rhs_nid];
    int rows = ln->rows;
    int cols = ln->cols;
    int total = rows * cols;
    double *C = (double *)calloc(total > 0 ? total : 1, sizeof(double));
    if (rn->rows == 1 && rows > 1 && rn->cols == cols) {
      for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
          C[r * cols + c] = ln->tval[r * cols + c] + rn->tval[c];
        }
      }
    } else {
      int rntot = rn->rows * rn->cols;
      for (int k = 0; k < total; k++) {
        C[k] = ln->tval[k] + (k < rntot ? rn->tval[k] : 0.0);
      }
    }
    int nid = AutodiffAllocTensorNode(t, 19 /* TENSOR_ADD */, rows, cols, lhs_nid, rhs_nid, C);
    free(C);
    PUSH(ValInt(nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeTensorRelu") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: TapeTensorRelu() expects 2 arguments (tape_id, lhs_nid)\n");
      return -1;
    }
    AKSValue lhs_val = POP();
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    int lhs_nid = (int)ValToInt(lhs_val);
    ValFree(&lhs_val);
    ValFree(&tid_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    TapeNode *ln = &t->nodes[lhs_nid];
    int rows = ln->rows;
    int cols = ln->cols;
    int total = rows * cols;
    double *C = (double *)calloc(total > 0 ? total : 1, sizeof(double));
    for (int k = 0; k < total; k++) {
      C[k] = (ln->tval[k] > 0.0) ? ln->tval[k] : 0.0;
    }
    int nid = AutodiffAllocTensorNode(t, 20 /* TENSOR_RELU */, rows, cols, lhs_nid, -1, C);
    free(C);
    PUSH(ValInt(nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeTensorGetVal") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: TapeTensorGetVal() expects 2 arguments (tape_id, nid)\n");
      return -1;
    }
    AKSValue nid_val = POP();
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    int nid = (int)ValToInt(nid_val);
    ValFree(&nid_val);
    ValFree(&tid_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    TapeNode *n = &t->nodes[nid];
    int total = n->rows * n->cols;
    AKSValue arr_val = ValArr(total);
    AKSArray *arr = ValToArr(arr_val);
    for (int k = 0; k < total; k++) {
      arr->items[arr->count++] = ValFloat(n->tval ? n->tval[k] : 0.0);
    }
    PUSH(arr_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeTensorGetGrad") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: TapeTensorGetGrad() expects 2 arguments (tape_id, nid)\n");
      return -1;
    }
    AKSValue nid_val = POP();
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    int nid = (int)ValToInt(nid_val);
    ValFree(&nid_val);
    ValFree(&tid_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    TapeNode *n = &t->nodes[nid];
    int total = n->rows * n->cols;
    AKSValue arr_val = ValArr(total);
    AKSArray *arr = ValToArr(arr_val);
    for (int k = 0; k < total; k++) {
      arr->items[arr->count++] = ValFloat(n->tgrad ? n->tgrad[k] : 0.0);
    }
    PUSH(arr_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeTensorBackward") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: TapeTensorBackward() expects 2 arguments (tape_id, loss_nid)\n");
      return -1;
    }
    AKSValue nid_val = POP();
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    int nid = (int)ValToInt(nid_val);
    ValFree(&nid_val);
    ValFree(&tid_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    AutodiffBackward(t, nid);
    PUSH(ValFloat(1.0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeReset") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: TapeReset() expects at least 1 argument (tape_id)\n");
      return -1;
    }
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    ValFree(&tid_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    AutodiffFreeTapeNodes(t);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  /* Neural network layers, activation functions, loss calculation, and optimizers */
  if (strcmp(name, "NnLinear") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: NnLinear() expects 2 arguments (in_dim, out_dim)\n");
      return -1;
    }
    AKSValue out_val = POP();
    AKSValue in_val = POP();
    int in_dim = (int)ValToInt(in_val);
    int out_dim = (int)ValToInt(out_val);
    ValFree(&out_val);
    ValFree(&in_val);
    if (in_dim <= 0) in_dim = 1;
    if (out_dim <= 0) out_dim = 1;

    AKSValue root = ValStruct("NnLinear");
    AKSStruct *s = ValToObj(root);
    s->fieldnames = malloc(sizeof(char*) * 11);
    s->fieldvals = malloc(sizeof(AKSValue) * 11);
    s->fieldcount = 11;

    int total_w = in_dim * out_dim;
    AKSValue w_val = ValArr(total_w);
    AKSArray *wa = ValToArr(w_val);
    double scale = sqrt(2.0 / (double)in_dim);
    for (int k = 0; k < total_w; k++) {
      double r = (((double)rand() / (double)RAND_MAX) * 2.0 - 1.0) * scale;
      wa->items[wa->count++] = ValFloat(r);
    }

    AKSValue b_val = ValArr(out_dim);
    AKSArray *ba = ValToArr(b_val);
    for (int k = 0; k < out_dim; k++) {
      ba->items[ba->count++] = ValFloat(0.0);
    }

    AKSValue mw_val = ValArr(total_w);
    AKSArray *mwa = ValToArr(mw_val);
    for (int k = 0; k < total_w; k++) mwa->items[mwa->count++] = ValFloat(0.0);

    AKSValue vw_val = ValArr(total_w);
    AKSArray *vwa = ValToArr(vw_val);
    for (int k = 0; k < total_w; k++) vwa->items[vwa->count++] = ValFloat(0.0);

    AKSValue mb_val = ValArr(out_dim);
    AKSArray *mba = ValToArr(mb_val);
    for (int k = 0; k < out_dim; k++) mba->items[mba->count++] = ValFloat(0.0);

    AKSValue vb_val = ValArr(out_dim);
    AKSArray *vba = ValToArr(vb_val);
    for (int k = 0; k < out_dim; k++) vba->items[vba->count++] = ValFloat(0.0);

    s->fieldnames[0] = strdup("in_dim"); s->fieldvals[0] = ValInt(in_dim);
    s->fieldnames[1] = strdup("out_dim"); s->fieldvals[1] = ValInt(out_dim);
    s->fieldnames[2] = strdup("weights"); s->fieldvals[2] = w_val;
    s->fieldnames[3] = strdup("bias"); s->fieldvals[3] = b_val;
    s->fieldnames[4] = strdup("weight_nid"); s->fieldvals[4] = ValInt(-1);
    s->fieldnames[5] = strdup("bias_nid"); s->fieldvals[5] = ValInt(-1);
    s->fieldnames[6] = strdup("m_w"); s->fieldvals[6] = mw_val;
    s->fieldnames[7] = strdup("v_w"); s->fieldvals[7] = vw_val;
    s->fieldnames[8] = strdup("m_b"); s->fieldvals[8] = mb_val;
    s->fieldnames[9] = strdup("v_b"); s->fieldvals[9] = vb_val;
    s->fieldnames[10] = strdup("step"); s->fieldvals[10] = ValInt(0);

    PUSH(root);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NnForward") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: NnForward() expects at least 2 arguments (layer, input, [tape_id])\n");
      return -1;
    }
    int tid = 0;
    if (argc >= 3) {
      AKSValue tid_v = POP();
      tid = (int)ValToInt(tid_v);
      ValFree(&tid_v);
    }
    AKSValue x_val = POP();
    AKSValue layer_val = POP();

    if (ValKind(layer_val) != VALSTRUCT) {
      ValFree(&x_val); ValFree(&layer_val);
      fprintf(stderr, "Runtime error: NnForward() first argument must be an NnLinear layer\n");
      return -1;
    }
    AKSStruct *s = ValToObj(layer_val);
    int in_dim = (int)ValToInt(*AksStructFindField(s, "in_dim"));
    int out_dim = (int)ValToInt(*AksStructFindField(s, "out_dim"));
    AutodiffTape *t = AutodiffGetTape(tid);

    int x_nid = -1;
    int B = 1;
    if (ValKind(x_val) == VALARR) {
      AKSArray *xa = ValToArr(x_val);
      int total = xa->count;
      B = (in_dim > 0) ? (total / in_dim) : 1;
      if (B < 1) B = 1;
      double *xbuf = (double *)calloc(total > 0 ? total : 1, sizeof(double));
      for (int k = 0; k < total; k++) {
        xbuf[k] = (ValKind(xa->items[k]) == VALFLOAT) ? ValToFloat(xa->items[k]) : (double)ValToInt(xa->items[k]);
      }
      x_nid = AutodiffAllocTensorNode(t, 17 /* TENSOR_VAR */, B, in_dim, -1, -1, xbuf);
      free(xbuf);
    } else {
      x_nid = (int)ValToInt(x_val);
      B = t->nodes[x_nid].rows;
    }

    AKSArray *wa = ValToArr(*AksStructFindField(s, "weights"));
    int wtot = in_dim * out_dim;
    double *wbuf = (double *)calloc(wtot > 0 ? wtot : 1, sizeof(double));
    for (int k = 0; k < wtot && k < wa->count; k++) {
      wbuf[k] = (ValKind(wa->items[k]) == VALFLOAT) ? ValToFloat(wa->items[k]) : (double)ValToInt(wa->items[k]);
    }
    int w_nid = AutodiffAllocTensorNode(t, 17 /* TENSOR_VAR */, in_dim, out_dim, -1, -1, wbuf);
    free(wbuf);
    *AksStructFindField(s, "weight_nid") = ValInt(w_nid);

    AKSArray *ba = ValToArr(*AksStructFindField(s, "bias"));
    double *bbuf = (double *)calloc(out_dim > 0 ? out_dim : 1, sizeof(double));
    for (int k = 0; k < out_dim && k < ba->count; k++) {
      bbuf[k] = (ValKind(ba->items[k]) == VALFLOAT) ? ValToFloat(ba->items[k]) : (double)ValToInt(ba->items[k]);
    }
    int b_nid = AutodiffAllocTensorNode(t, 17 /* TENSOR_VAR */, 1, out_dim, -1, -1, bbuf);
    free(bbuf);
    *AksStructFindField(s, "bias_nid") = ValInt(b_nid);

    TapeNode *xn = &t->nodes[x_nid];
    TapeNode *wn = &t->nodes[w_nid];
    int M = xn->rows;
    int K = xn->cols;
    int N = wn->cols;
    double *C = (double *)calloc(M * N > 0 ? M * N : 1, sizeof(double));
    for (int r = 0; r < M; r++) {
      for (int k = 0; k < K; k++) {
        double av = xn->tval[r * K + k];
        for (int c = 0; c < N; c++) {
          C[r * N + c] += av * wn->tval[k * N + c];
        }
      }
    }
    int mm_nid = AutodiffAllocTensorNode(t, 18 /* TENSOR_MATMUL */, M, N, x_nid, w_nid, C);
    free(C);

    TapeNode *mn = &t->nodes[mm_nid];
    TapeNode *bn = &t->nodes[b_nid];
    double *Out = (double *)calloc(M * N > 0 ? M * N : 1, sizeof(double));
    for (int r = 0; r < M; r++) {
      for (int c = 0; c < N; c++) {
        Out[r * N + c] = mn->tval[r * N + c] + bn->tval[c];
      }
    }
    int out_nid = AutodiffAllocTensorNode(t, 19 /* TENSOR_ADD */, M, N, mm_nid, b_nid, Out);
    free(Out);

    ValFree(&x_val);
    ValFree(&layer_val);
    PUSH(ValInt(out_nid));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NnRelu") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: NnRelu() expects at least 1 argument (nid, [tape_id])\n");
      return -1;
    }
    int tid = 0;
    if (argc >= 2) {
      AKSValue tid_v = POP();
      tid = (int)ValToInt(tid_v);
      ValFree(&tid_v);
    }
    AKSValue nid_v = POP();
    int nid = (int)ValToInt(nid_v);
    ValFree(&nid_v);
    AutodiffTape *t = AutodiffGetTape(tid);
    TapeNode *ln = &t->nodes[nid];
    int rows = ln->rows;
    int cols = ln->cols;
    int total = rows * cols;
    double *C = (double *)calloc(total > 0 ? total : 1, sizeof(double));
    for (int k = 0; k < total; k++) {
      C[k] = (ln->tval[k] > 0.0) ? ln->tval[k] : 0.0;
    }
    int relu_nid = AutodiffAllocTensorNode(t, 20 /* TENSOR_RELU */, rows, cols, nid, -1, C);
    free(C);
    PUSH(ValInt(relu_nid));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NnSoftmax") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: NnSoftmax() expects at least 1 argument (arr_or_nid, [tape_id])\n");
      return -1;
    }
    int tid = 0;
    if (argc >= 2) {
      AKSValue tid_v = POP();
      tid = (int)ValToInt(tid_v);
      ValFree(&tid_v);
    }
    AKSValue arg = POP();
    int count = 0;
    double *vals = NULL;
    if (ValKind(arg) == VALARR) {
      AKSArray *a = ValToArr(arg);
      count = a->count;
      vals = (double *)calloc(count > 0 ? count : 1, sizeof(double));
      for (int k = 0; k < count; k++) {
        vals[k] = (ValKind(a->items[k]) == VALFLOAT) ? ValToFloat(a->items[k]) : (double)ValToInt(a->items[k]);
      }
    } else {
      int nid = (int)ValToInt(arg);
      AutodiffTape *t = AutodiffGetTape(tid);
      count = t->nodes[nid].rows * t->nodes[nid].cols;
      vals = (double *)calloc(count > 0 ? count : 1, sizeof(double));
      if (t->nodes[nid].tval) memcpy(vals, t->nodes[nid].tval, sizeof(double) * count);
    }
    ValFree(&arg);

    double max_v = -1e30;
    for (int k = 0; k < count; k++) if (vals[k] > max_v) max_v = vals[k];
    double sum_exp = 0.0;
    for (int k = 0; k < count; k++) sum_exp += exp(vals[k] - max_v);
    AKSValue res_v = ValArr(count);
    AKSArray *ra = ValToArr(res_v);
    for (int k = 0; k < count; k++) {
      double p = exp(vals[k] - max_v) / (sum_exp > 0.0 ? sum_exp : 1.0);
      ra->items[ra->count++] = ValFloat(p);
    }
    free(vals);
    PUSH(res_v);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NnMseLoss") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: NnMseLoss() expects at least 2 arguments (pred_nid, target_arr, [tape_id])\n");
      return -1;
    }
    int tid = 0;
    if (argc >= 3) {
      AKSValue tid_v = POP();
      tid = (int)ValToInt(tid_v);
      ValFree(&tid_v);
    }
    AKSValue target_v = POP();
    AKSValue pred_v = POP();
    AutodiffTape *t = AutodiffGetTape(tid);

    int pred_nid = (int)ValToInt(pred_v);
    TapeNode *pn = &t->nodes[pred_nid];
    int rows = pn->rows;
    int cols = pn->cols;
    int total = rows * cols;

    double *tbuf = (double *)calloc(total > 0 ? total : 1, sizeof(double));
    if (ValKind(target_v) == VALARR) {
      AKSArray *ta = ValToArr(target_v);
      for (int k = 0; k < total && k < ta->count; k++) {
        tbuf[k] = (ValKind(ta->items[k]) == VALFLOAT) ? ValToFloat(ta->items[k]) : (double)ValToInt(ta->items[k]);
      }
    }
    int target_nid = AutodiffAllocTensorNode(t, 17 /* TENSOR_VAR */, rows, cols, -1, -1, tbuf);

    double sum_sq = 0.0;
    for (int k = 0; k < total; k++) {
      double diff = pn->tval[k] - tbuf[k];
      sum_sq += diff * diff;
    }
    free(tbuf);
    double loss = (total > 0) ? (sum_sq / (double)total) : 0.0;

    int loss_nid = AutodiffAllocTensorNode(t, 21 /* TENSOR_MSE */, 1, 1, pred_nid, target_nid, &loss);
    ValFree(&pred_v);
    ValFree(&target_v);

    PUSH(ValInt(loss_nid));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NnCrossEntropy") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: NnCrossEntropy() expects at least 2 arguments (logits_nid, target_arr, [tape_id])\n");
      return -1;
    }
    int tid = 0;
    if (argc >= 3) {
      AKSValue tid_v = POP();
      tid = (int)ValToInt(tid_v);
      ValFree(&tid_v);
    }
    AKSValue target_v = POP();
    AKSValue logits_v = POP();
    AutodiffTape *t = AutodiffGetTape(tid);

    int logits_nid = (int)ValToInt(logits_v);
    TapeNode *ln = &t->nodes[logits_nid];
    int M = ln->rows;
    int N = ln->cols;
    int total = M * N;

    double *tbuf = (double *)calloc(total > 0 ? total : 1, sizeof(double));
    if (ValKind(target_v) == VALARR) {
      AKSArray *ta = ValToArr(target_v);
      for (int k = 0; k < total && k < ta->count; k++) {
        tbuf[k] = (ValKind(ta->items[k]) == VALFLOAT) ? ValToFloat(ta->items[k]) : (double)ValToInt(ta->items[k]);
      }
    }
    int target_nid = AutodiffAllocTensorNode(t, 17 /* TENSOR_VAR */, M, N, -1, -1, tbuf);

    double total_loss = 0.0;
    for (int r = 0; r < M; r++) {
      double max_v = -1e30;
      for (int c = 0; c < N; c++) if (ln->tval[r * N + c] > max_v) max_v = ln->tval[r * N + c];
      double sum_exp = 0.0;
      for (int c = 0; c < N; c++) sum_exp += exp(ln->tval[r * N + c] - max_v);
      for (int c = 0; c < N; c++) {
        double p = exp(ln->tval[r * N + c] - max_v) / (sum_exp > 0.0 ? sum_exp : 1.0);
        total_loss -= tbuf[r * N + c] * log(p > 1e-15 ? p : 1e-15);
      }
    }
    free(tbuf);
    double loss = (M > 0) ? (total_loss / (double)M) : 0.0;

    int loss_nid = AutodiffAllocTensorNode(t, 22 /* TENSOR_CROSS_ENTROPY */, 1, 1, logits_nid, target_nid, &loss);
    ValFree(&logits_v);
    ValFree(&target_v);

    PUSH(ValInt(loss_nid));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NnLossVal") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: NnLossVal() expects at least 1 argument (loss_nid, [tape_id])\n");
      return -1;
    }
    int tid = 0;
    if (argc >= 2) {
      AKSValue tid_v = POP();
      tid = (int)ValToInt(tid_v);
      ValFree(&tid_v);
    }
    AKSValue nid_v = POP();
    int nid = (int)ValToInt(nid_v);
    ValFree(&nid_v);
    AutodiffTape *t = AutodiffGetTape(tid);
    double val = (t->nodes[nid].tval) ? t->nodes[nid].tval[0] : t->nodes[nid].val;
    PUSH(ValFloat(val));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NnGetVal") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: NnGetVal() expects at least 1 argument (nid, [tape_id])\n");
      return -1;
    }
    int tid = 0;
    if (argc >= 2) {
      AKSValue tid_v = POP();
      tid = (int)ValToInt(tid_v);
      ValFree(&tid_v);
    }
    AKSValue nid_v = POP();
    int nid = (int)ValToInt(nid_v);
    ValFree(&nid_v);
    AutodiffTape *t = AutodiffGetTape(tid);
    int total = t->nodes[nid].rows * t->nodes[nid].cols;
    AKSValue res_v = ValArr(total);
    AKSArray *ra = ValToArr(res_v);
    for (int k = 0; k < total; k++) {
      ra->items[ra->count++] = ValFloat(t->nodes[nid].tval ? t->nodes[nid].tval[k] : 0.0);
    }
    PUSH(res_v);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NnSGD") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: NnSGD() expects at least 2 arguments (layers, lr, [tape_id])\n");
      return -1;
    }
    int tid = 0;
    if (argc >= 3) {
      AKSValue tid_v = POP();
      tid = (int)ValToInt(tid_v);
      ValFree(&tid_v);
    }
    AKSValue lr_v = POP();
    AKSValue layers_v = POP();
    double lr = (ValKind(lr_v) == VALFLOAT) ? ValToFloat(lr_v) : (double)ValToInt(lr_v);
    ValFree(&lr_v);
    AutodiffTape *t = AutodiffGetTape(tid);

    AKSValue layer_list[64];
    int layer_count = 0;
    if (ValKind(layers_v) == VALARR) {
      AKSArray *la = ValToArr(layers_v);
      for (int i = 0; i < la->count && layer_count < 64; i++) {
        if (ValKind(la->items[i]) == VALSTRUCT) layer_list[layer_count++] = la->items[i];
      }
    } else if (ValKind(layers_v) == VALSTRUCT) {
      layer_list[layer_count++] = layers_v;
    }

    for (int i = 0; i < layer_count; i++) {
      AKSStruct *s = ValToObj(layer_list[i]);
      int w_nid = (int)ValToInt(*AksStructFindField(s, "weight_nid"));
      int b_nid = (int)ValToInt(*AksStructFindField(s, "bias_nid"));
      if (w_nid >= 0 && w_nid < t->count) {
        TapeNode *wn = &t->nodes[w_nid];
        AKSArray *wa = ValToArr(*AksStructFindField(s, "weights"));
        for (int k = 0; k < wa->count && k < (wn->rows * wn->cols); k++) {
          double cur = ValToFloat(wa->items[k]);
          wa->items[k] = ValFloat(cur - lr * wn->tgrad[k]);
        }
      }
      if (b_nid >= 0 && b_nid < t->count) {
        TapeNode *bn = &t->nodes[b_nid];
        AKSArray *ba = ValToArr(*AksStructFindField(s, "bias"));
        int out_cols = bn->cols;
        for (int c = 0; c < ba->count && c < out_cols; c++) {
          double cur = ValToFloat(ba->items[c]);
          double g = 0.0;
          for (int r = 0; r < bn->rows; r++) g += bn->tgrad[r * out_cols + c];
          ba->items[c] = ValFloat(cur - lr * g);
        }
      }
    }
    ValFree(&layers_v);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NnAdam") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: NnAdam() expects at least 2 arguments (layers, lr, [beta1, beta2, eps, tape_id])\n");
      return -1;
    }
    int tid = 0;
    double eps = 1e-8;
    double beta2 = 0.999;
    double beta1 = 0.9;
    if (argc >= 6) {
      AKSValue v = POP(); tid = (int)ValToInt(v); ValFree(&v);
    }
    if (argc >= 5) {
      AKSValue v = POP(); eps = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v); ValFree(&v);
    }
    if (argc >= 4) {
      AKSValue v = POP(); beta2 = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v); ValFree(&v);
    }
    if (argc >= 3) {
      AKSValue v = POP(); beta1 = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v); ValFree(&v);
    }
    AKSValue lr_v = POP();
    AKSValue layers_v = POP();
    double lr = (ValKind(lr_v) == VALFLOAT) ? ValToFloat(lr_v) : (double)ValToInt(lr_v);
    ValFree(&lr_v);
    AutodiffTape *t = AutodiffGetTape(tid);

    AKSValue layer_list[64];
    int layer_count = 0;
    if (ValKind(layers_v) == VALARR) {
      AKSArray *la = ValToArr(layers_v);
      for (int i = 0; i < la->count && layer_count < 64; i++) {
        if (ValKind(la->items[i]) == VALSTRUCT) layer_list[layer_count++] = la->items[i];
      }
    } else if (ValKind(layers_v) == VALSTRUCT) {
      layer_list[layer_count++] = layers_v;
    }

    for (int i = 0; i < layer_count; i++) {
      AKSStruct *s = ValToObj(layer_list[i]);
      int step = (int)ValToInt(*AksStructFindField(s, "step")) + 1;
      *AksStructFindField(s, "step") = ValInt(step);
      double bc1 = 1.0 - pow(beta1, (double)step);
      double bc2 = 1.0 - pow(beta2, (double)step);
      if (bc1 < 1e-15) bc1 = 1.0;
      if (bc2 < 1e-15) bc2 = 1.0;

      int w_nid = (int)ValToInt(*AksStructFindField(s, "weight_nid"));
      int b_nid = (int)ValToInt(*AksStructFindField(s, "bias_nid"));
      if (w_nid >= 0 && w_nid < t->count) {
        TapeNode *wn = &t->nodes[w_nid];
        AKSArray *wa = ValToArr(*AksStructFindField(s, "weights"));
        AKSArray *mwa = ValToArr(*AksStructFindField(s, "m_w"));
        AKSArray *vwa = ValToArr(*AksStructFindField(s, "v_w"));
        for (int k = 0; k < wa->count && k < (wn->rows * wn->cols); k++) {
          double g = wn->tgrad[k];
          double m = beta1 * ValToFloat(mwa->items[k]) + (1.0 - beta1) * g;
          double v = beta2 * ValToFloat(vwa->items[k]) + (1.0 - beta2) * g * g;
          mwa->items[k] = ValFloat(m);
          vwa->items[k] = ValFloat(v);
          double m_hat = m / bc1;
          double v_hat = v / bc2;
          double cur = ValToFloat(wa->items[k]);
          wa->items[k] = ValFloat(cur - lr * m_hat / (sqrt(v_hat) + eps));
        }
      }
      if (b_nid >= 0 && b_nid < t->count) {
        TapeNode *bn = &t->nodes[b_nid];
        AKSArray *ba = ValToArr(*AksStructFindField(s, "bias"));
        AKSArray *mba = ValToArr(*AksStructFindField(s, "m_b"));
        AKSArray *vba = ValToArr(*AksStructFindField(s, "v_b"));
        int out_cols = bn->cols;
        for (int c = 0; c < ba->count && c < out_cols; c++) {
          double g = 0.0;
          for (int r = 0; r < bn->rows; r++) g += bn->tgrad[r * out_cols + c];
          double m = beta1 * ValToFloat(mba->items[c]) + (1.0 - beta1) * g;
          double v = beta2 * ValToFloat(vba->items[c]) + (1.0 - beta2) * g * g;
          mba->items[c] = ValFloat(m);
          vba->items[c] = ValFloat(v);
          double m_hat = m / bc1;
          double v_hat = v / bc2;
          double cur = ValToFloat(ba->items[c]);
          ba->items[c] = ValFloat(cur - lr * m_hat / (sqrt(v_hat) + eps));
        }
      }
    }
    ValFree(&layers_v);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NnRMSProp") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: NnRMSProp() expects at least 2 arguments (layers, lr, [alpha, eps, tape_id])\n");
      return -1;
    }
    int tid = 0;
    double eps = 1e-8;
    double alpha = 0.99;
    if (argc >= 5) {
      AKSValue v = POP(); tid = (int)ValToInt(v); ValFree(&v);
    }
    if (argc >= 4) {
      AKSValue v = POP(); eps = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v); ValFree(&v);
    }
    if (argc >= 3) {
      AKSValue v = POP(); alpha = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v); ValFree(&v);
    }
    AKSValue lr_v = POP();
    AKSValue layers_v = POP();
    double lr = (ValKind(lr_v) == VALFLOAT) ? ValToFloat(lr_v) : (double)ValToInt(lr_v);
    ValFree(&lr_v);
    AutodiffTape *t = AutodiffGetTape(tid);

    AKSValue layer_list[64];
    int layer_count = 0;
    if (ValKind(layers_v) == VALARR) {
      AKSArray *la = ValToArr(layers_v);
      for (int i = 0; i < la->count && layer_count < 64; i++) {
        if (ValKind(la->items[i]) == VALSTRUCT) layer_list[layer_count++] = la->items[i];
      }
    } else if (ValKind(layers_v) == VALSTRUCT) {
      layer_list[layer_count++] = layers_v;
    }

    for (int i = 0; i < layer_count; i++) {
      AKSStruct *s = ValToObj(layer_list[i]);
      int w_nid = (int)ValToInt(*AksStructFindField(s, "weight_nid"));
      int b_nid = (int)ValToInt(*AksStructFindField(s, "bias_nid"));
      if (w_nid >= 0 && w_nid < t->count) {
        TapeNode *wn = &t->nodes[w_nid];
        AKSArray *wa = ValToArr(*AksStructFindField(s, "weights"));
        AKSArray *vwa = ValToArr(*AksStructFindField(s, "v_w"));
        for (int k = 0; k < wa->count && k < (wn->rows * wn->cols); k++) {
          double g = wn->tgrad[k];
          double v = alpha * ValToFloat(vwa->items[k]) + (1.0 - alpha) * g * g;
          vwa->items[k] = ValFloat(v);
          double cur = ValToFloat(wa->items[k]);
          wa->items[k] = ValFloat(cur - lr * g / (sqrt(v) + eps));
        }
      }
      if (b_nid >= 0 && b_nid < t->count) {
        TapeNode *bn = &t->nodes[b_nid];
        AKSArray *ba = ValToArr(*AksStructFindField(s, "bias"));
        AKSArray *vba = ValToArr(*AksStructFindField(s, "v_b"));
        int out_cols = bn->cols;
        for (int c = 0; c < ba->count && c < out_cols; c++) {
          double g = 0.0;
          for (int r = 0; r < bn->rows; r++) g += bn->tgrad[r * out_cols + c];
          double v = alpha * ValToFloat(vba->items[c]) + (1.0 - alpha) * g * g;
          vba->items[c] = ValFloat(v);
          double cur = ValToFloat(ba->items[c]);
          ba->items[c] = ValFloat(cur - lr * g / (sqrt(v) + eps));
        }
      }
    }
    ValFree(&layers_v);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "NnZeroGrad") == 0) {
    int tid = 0;
    if (argc >= 2) {
      AKSValue tid_v = POP();
      tid = (int)ValToInt(tid_v);
      ValFree(&tid_v);
    }
    if (argc >= 1) {
      AKSValue dummy = POP();
      ValFree(&dummy);
    }
    AutodiffTape *t = AutodiffGetTape(tid);
    AutodiffFreeTapeNodes(t);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "GetStaticRcEliminated") == 0) {
    PUSH(ValInt((int64_t)vm->static_rc_eliminated));
    *stacktopptr = stacktop;
    return 1;
  }

  /* Work-stealing green scheduler helpers and task scheduling primitives */
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
        if (vm->alltasks[i]->state == 3) completed++;
        else active++;
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
      fprintf(stderr, "Runtime error: GreenSchedSubmit() expects at least 1 argument (fn, ...)\n");
      return -1;
    }
    int extra_args = argc - 1;
    AKSValue passed_args[32];
    for (int i = extra_args - 1; i >= 0; i--) {
      passed_args[i] = POP();
    }
    AKSValue fn_val = POP();
    if (ValKind(fn_val) != VALFN) {
      for (int i = 0; i < extra_args; i++) ValFree(&passed_args[i]);
      ValFree(&fn_val);
      fprintf(stderr, "Runtime error: GreenSchedSubmit() target is not a function\n");
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
      fprintf(stderr, "Runtime error: GreenSchedParallelFor expects 3 arguments (start, end, fn)\n");
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
      fprintf(stderr, "Runtime error: GreenSchedParallelFor target is not a function\n");
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
      if (cend > end) cend = end;
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
      if (!tasks[t]) continue;
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

  /* Physical units with dimensional validation and Einstein summation */
  if (strcmp(name, "PhysUnit") == 0) {
    int s = (int)ValToInt(POP());
    int kg = (int)ValToInt(POP());
    int m = (int)ValToInt(POP());
    AKSValue v_v = POP();
    double val = (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
    ValFree(&v_v);
    AKSValue p = ValStruct("PhysUnit");
    AKSStruct *st = ValToObj(p);
    st->fieldcount = 4;
    st->fieldnames = malloc(4 * sizeof(char *));
    st->fieldvals = malloc(4 * sizeof(AKSValue));
    st->fieldnames[0] = strdup("val");
    st->fieldvals[0] = ValFloat(val);
    st->fieldnames[1] = strdup("m");
    st->fieldvals[1] = ValInt(m);
    st->fieldnames[2] = strdup("kg");
    st->fieldvals[2] = ValInt(kg);
    st->fieldnames[3] = strdup("s");
    st->fieldvals[3] = ValInt(s);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "Meter") == 0) {
    AKSValue v_v = POP();
    double val = (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
    ValFree(&v_v);
    AKSValue p = ValStruct("PhysUnit");
    AKSStruct *st = ValToObj(p);
    st->fieldcount = 4;
    st->fieldnames = malloc(4 * sizeof(char *));
    st->fieldvals = malloc(4 * sizeof(AKSValue));
    st->fieldnames[0] = strdup("val");
    st->fieldvals[0] = ValFloat(val);
    st->fieldnames[1] = strdup("m");
    st->fieldvals[1] = ValInt(1);
    st->fieldnames[2] = strdup("kg");
    st->fieldvals[2] = ValInt(0);
    st->fieldnames[3] = strdup("s");
    st->fieldvals[3] = ValInt(0);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "Kilogram") == 0) {
    AKSValue v_v = POP();
    double val = (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
    ValFree(&v_v);
    AKSValue p = ValStruct("PhysUnit");
    AKSStruct *st = ValToObj(p);
    st->fieldcount = 4;
    st->fieldnames = malloc(4 * sizeof(char *));
    st->fieldvals = malloc(4 * sizeof(AKSValue));
    st->fieldnames[0] = strdup("val");
    st->fieldvals[0] = ValFloat(val);
    st->fieldnames[1] = strdup("m");
    st->fieldvals[1] = ValInt(0);
    st->fieldnames[2] = strdup("kg");
    st->fieldvals[2] = ValInt(1);
    st->fieldnames[3] = strdup("s");
    st->fieldvals[3] = ValInt(0);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "Second") == 0) {
    AKSValue v_v = POP();
    double val = (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
    ValFree(&v_v);
    AKSValue p = ValStruct("PhysUnit");
    AKSStruct *st = ValToObj(p);
    st->fieldcount = 4;
    st->fieldnames = malloc(4 * sizeof(char *));
    st->fieldvals = malloc(4 * sizeof(AKSValue));
    st->fieldnames[0] = strdup("val");
    st->fieldvals[0] = ValFloat(val);
    st->fieldnames[1] = strdup("m");
    st->fieldvals[1] = ValInt(0);
    st->fieldnames[2] = strdup("kg");
    st->fieldvals[2] = ValInt(0);
    st->fieldnames[3] = strdup("s");
    st->fieldvals[3] = ValInt(1);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "Newton") == 0) {
    AKSValue v_v = POP();
    double val = (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
    ValFree(&v_v);
    AKSValue p = ValStruct("PhysUnit");
    AKSStruct *st = ValToObj(p);
    st->fieldcount = 4;
    st->fieldnames = malloc(4 * sizeof(char *));
    st->fieldvals = malloc(4 * sizeof(AKSValue));
    st->fieldnames[0] = strdup("val");
    st->fieldvals[0] = ValFloat(val);
    st->fieldnames[1] = strdup("m");
    st->fieldvals[1] = ValInt(1);
    st->fieldnames[2] = strdup("kg");
    st->fieldvals[2] = ValInt(1);
    st->fieldnames[3] = strdup("s");
    st->fieldvals[3] = ValInt(-2);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "Joule") == 0) {
    AKSValue v_v = POP();
    double val = (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
    ValFree(&v_v);
    AKSValue p = ValStruct("PhysUnit");
    AKSStruct *st = ValToObj(p);
    st->fieldcount = 4;
    st->fieldnames = malloc(4 * sizeof(char *));
    st->fieldvals = malloc(4 * sizeof(AKSValue));
    st->fieldnames[0] = strdup("val");
    st->fieldvals[0] = ValFloat(val);
    st->fieldnames[1] = strdup("m");
    st->fieldvals[1] = ValInt(2);
    st->fieldnames[2] = strdup("kg");
    st->fieldvals[2] = ValInt(1);
    st->fieldnames[3] = strdup("s");
    st->fieldvals[3] = ValInt(-2);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "Watt") == 0) {
    AKSValue v_v = POP();
    double val = (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
    ValFree(&v_v);
    AKSValue p = ValStruct("PhysUnit");
    AKSStruct *st = ValToObj(p);
    st->fieldcount = 4;
    st->fieldnames = malloc(4 * sizeof(char *));
    st->fieldvals = malloc(4 * sizeof(AKSValue));
    st->fieldnames[0] = strdup("val");
    st->fieldvals[0] = ValFloat(val);
    st->fieldnames[1] = strdup("m");
    st->fieldvals[1] = ValInt(2);
    st->fieldnames[2] = strdup("kg");
    st->fieldvals[2] = ValInt(1);
    st->fieldnames[3] = strdup("s");
    st->fieldvals[3] = ValInt(-3);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "PhysVal") == 0) {
    AKSValue u = POP();
    double v = 0.0;
    if (ValKind(u) == VALSTRUCT) {
      AKSStruct *st = ValToObj(u);
      for (int i = 0; i < st->fieldcount; i++) {
        if (strcmp(st->fieldnames[i], "val") == 0) {
          v = (ValKind(st->fieldvals[i]) == VALFLOAT) ? ValToFloat(st->fieldvals[i]) : (double)ValToInt(st->fieldvals[i]);
          break;
        }
      }
    } else if (ValKind(u) == VALFLOAT) {
      v = ValToFloat(u);
    } else if (ValKind(u) == VALINT) {
      v = (double)ValToInt(u);
    }
    ValFree(&u);
    PUSH(ValFloat(v));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "PhysMul") == 0) {
    AKSValue u2 = POP();
    AKSValue u1 = POP();
    double v1 = 0, v2 = 0;
    int m1 = 0, kg1 = 0, s1 = 0, m2 = 0, kg2 = 0, s2 = 0;
    if (ValKind(u1) == VALSTRUCT) {
      AKSStruct *st = ValToObj(u1);
      for (int i = 0; i < st->fieldcount; i++) {
        if (strcmp(st->fieldnames[i], "val") == 0) v1 = (ValKind(st->fieldvals[i]) == VALFLOAT) ? ValToFloat(st->fieldvals[i]) : (double)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "m") == 0) m1 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "kg") == 0) kg1 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "s") == 0) s1 = (int)ValToInt(st->fieldvals[i]);
      }
    }
    if (ValKind(u2) == VALSTRUCT) {
      AKSStruct *st = ValToObj(u2);
      for (int i = 0; i < st->fieldcount; i++) {
        if (strcmp(st->fieldnames[i], "val") == 0) v2 = (ValKind(st->fieldvals[i]) == VALFLOAT) ? ValToFloat(st->fieldvals[i]) : (double)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "m") == 0) m2 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "kg") == 0) kg2 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "s") == 0) s2 = (int)ValToInt(st->fieldvals[i]);
      }
    }
    ValFree(&u1);
    ValFree(&u2);
    AKSValue p = ValStruct("PhysUnit");
    AKSStruct *st = ValToObj(p);
    st->fieldcount = 4;
    st->fieldnames = malloc(4 * sizeof(char *));
    st->fieldvals = malloc(4 * sizeof(AKSValue));
    st->fieldnames[0] = strdup("val");
    st->fieldvals[0] = ValFloat(v1 * v2);
    st->fieldnames[1] = strdup("m");
    st->fieldvals[1] = ValInt(m1 + m2);
    st->fieldnames[2] = strdup("kg");
    st->fieldvals[2] = ValInt(kg1 + kg2);
    st->fieldnames[3] = strdup("s");
    st->fieldvals[3] = ValInt(s1 + s2);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "PhysDiv") == 0) {
    AKSValue u2 = POP();
    AKSValue u1 = POP();
    double v1 = 0, v2 = 0;
    int m1 = 0, kg1 = 0, s1 = 0, m2 = 0, kg2 = 0, s2 = 0;
    if (ValKind(u1) == VALSTRUCT) {
      AKSStruct *st = ValToObj(u1);
      for (int i = 0; i < st->fieldcount; i++) {
        if (strcmp(st->fieldnames[i], "val") == 0) v1 = (ValKind(st->fieldvals[i]) == VALFLOAT) ? ValToFloat(st->fieldvals[i]) : (double)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "m") == 0) m1 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "kg") == 0) kg1 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "s") == 0) s1 = (int)ValToInt(st->fieldvals[i]);
      }
    }
    if (ValKind(u2) == VALSTRUCT) {
      AKSStruct *st = ValToObj(u2);
      for (int i = 0; i < st->fieldcount; i++) {
        if (strcmp(st->fieldnames[i], "val") == 0) v2 = (ValKind(st->fieldvals[i]) == VALFLOAT) ? ValToFloat(st->fieldvals[i]) : (double)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "m") == 0) m2 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "kg") == 0) kg2 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "s") == 0) s2 = (int)ValToInt(st->fieldvals[i]);
      }
    }
    ValFree(&u1);
    ValFree(&u2);
    AKSValue p = ValStruct("PhysUnit");
    AKSStruct *st = ValToObj(p);
    st->fieldcount = 4;
    st->fieldnames = malloc(4 * sizeof(char *));
    st->fieldvals = malloc(4 * sizeof(AKSValue));
    st->fieldnames[0] = strdup("val");
    st->fieldvals[0] = ValFloat(v1 / v2);
    st->fieldnames[1] = strdup("m");
    st->fieldvals[1] = ValInt(m1 - m2);
    st->fieldnames[2] = strdup("kg");
    st->fieldvals[2] = ValInt(kg1 - kg2);
    st->fieldnames[3] = strdup("s");
    st->fieldvals[3] = ValInt(s1 - s2);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "PhysAdd") == 0) {
    AKSValue u2 = POP();
    AKSValue u1 = POP();
    double v1 = 0, v2 = 0;
    int m1 = 0, kg1 = 0, s1 = 0, m2 = 0, kg2 = 0, s2 = 0;
    if (ValKind(u1) == VALSTRUCT) {
      AKSStruct *st = ValToObj(u1);
      for (int i = 0; i < st->fieldcount; i++) {
        if (strcmp(st->fieldnames[i], "val") == 0) v1 = (ValKind(st->fieldvals[i]) == VALFLOAT) ? ValToFloat(st->fieldvals[i]) : (double)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "m") == 0) m1 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "kg") == 0) kg1 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "s") == 0) s1 = (int)ValToInt(st->fieldvals[i]);
      }
    }
    if (ValKind(u2) == VALSTRUCT) {
      AKSStruct *st = ValToObj(u2);
      for (int i = 0; i < st->fieldcount; i++) {
        if (strcmp(st->fieldnames[i], "val") == 0) v2 = (ValKind(st->fieldvals[i]) == VALFLOAT) ? ValToFloat(st->fieldvals[i]) : (double)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "m") == 0) m2 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "kg") == 0) kg2 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "s") == 0) s2 = (int)ValToInt(st->fieldvals[i]);
      }
    }
    ValFree(&u1);
    ValFree(&u2);
    if (m1 != m2 || kg1 != kg2 || s1 != s2) {
      fprintf(stderr, "DimensionalMismatchError: cannot add quantities of different physical dimensions [%d,%d,%d] vs [%d,%d,%d]\n", m1, kg1, s1, m2, kg2, s2);
      return -1;
    }
    AKSValue p = ValStruct("PhysUnit");
    AKSStruct *st = ValToObj(p);
    st->fieldcount = 4;
    st->fieldnames = malloc(4 * sizeof(char *));
    st->fieldvals = malloc(4 * sizeof(AKSValue));
    st->fieldnames[0] = strdup("val");
    st->fieldvals[0] = ValFloat(v1 + v2);
    st->fieldnames[1] = strdup("m");
    st->fieldvals[1] = ValInt(m1);
    st->fieldnames[2] = strdup("kg");
    st->fieldvals[2] = ValInt(kg1);
    st->fieldnames[3] = strdup("s");
    st->fieldvals[3] = ValInt(s1);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "PhysSub") == 0) {
    AKSValue u2 = POP();
    AKSValue u1 = POP();
    double v1 = 0, v2 = 0;
    int m1 = 0, kg1 = 0, s1 = 0, m2 = 0, kg2 = 0, s2 = 0;
    if (ValKind(u1) == VALSTRUCT) {
      AKSStruct *st = ValToObj(u1);
      for (int i = 0; i < st->fieldcount; i++) {
        if (strcmp(st->fieldnames[i], "val") == 0) v1 = (ValKind(st->fieldvals[i]) == VALFLOAT) ? ValToFloat(st->fieldvals[i]) : (double)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "m") == 0) m1 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "kg") == 0) kg1 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "s") == 0) s1 = (int)ValToInt(st->fieldvals[i]);
      }
    }
    if (ValKind(u2) == VALSTRUCT) {
      AKSStruct *st = ValToObj(u2);
      for (int i = 0; i < st->fieldcount; i++) {
        if (strcmp(st->fieldnames[i], "val") == 0) v2 = (ValKind(st->fieldvals[i]) == VALFLOAT) ? ValToFloat(st->fieldvals[i]) : (double)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "m") == 0) m2 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "kg") == 0) kg2 = (int)ValToInt(st->fieldvals[i]);
        else if (strcmp(st->fieldnames[i], "s") == 0) s2 = (int)ValToInt(st->fieldvals[i]);
      }
    }
    ValFree(&u1);
    ValFree(&u2);
    if (m1 != m2 || kg1 != kg2 || s1 != s2) {
      fprintf(stderr, "DimensionalMismatchError: cannot subtract quantities of different physical dimensions [%d,%d,%d] vs [%d,%d,%d]\n", m1, kg1, s1, m2, kg2, s2);
      return -1;
    }
    AKSValue p = ValStruct("PhysUnit");
    AKSStruct *st = ValToObj(p);
    st->fieldcount = 4;
    st->fieldnames = malloc(4 * sizeof(char *));
    st->fieldvals = malloc(4 * sizeof(AKSValue));
    st->fieldnames[0] = strdup("val");
    st->fieldvals[0] = ValFloat(v1 - v2);
    st->fieldnames[1] = strdup("m");
    st->fieldvals[1] = ValInt(m1);
    st->fieldnames[2] = strdup("kg");
    st->fieldvals[2] = ValInt(kg1);
    st->fieldnames[3] = strdup("s");
    st->fieldvals[3] = ValInt(s1);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "EinsteinSum") == 0 || strcmp(name, "EinsumC") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: EinsteinSum expects (spec, A, [B])\n");
      return -1;
    }
    AKSValue B_val = ValNull();
    AKSValue A_val = ValNull();
    AKSValue spec_val = ValNull();
    if (argc >= 3) {
      B_val = POP();
      A_val = POP();
      spec_val = POP();
    } else {
      A_val = POP();
      spec_val = POP();
    }
    const char *spec = (ValKind(spec_val) == VALSTR && ValToStrPtr(spec_val)) ? ValToStrPtr(spec_val)->data : "";

    // Matrix multiplication: "ij,jk->ik"
    if (strstr(spec, "ij,jk->ik") && ValKind(A_val) == VALARR && ValKind(B_val) == VALARR) {
      AKSArray *A = ValToArr(A_val);
      AKSArray *B = ValToArr(B_val);
      int rowsA = A->count;
      int colsA = (rowsA > 0 && ValKind(A->items[0]) == VALARR) ? ValToArr(A->items[0])->count : 0;
      int rowsB = B->count;
      int colsB = (rowsB > 0 && ValKind(B->items[0]) == VALARR) ? ValToArr(B->items[0])->count : 0;
      if (colsA != rowsB) {
        ValFree(&spec_val); ValFree(&A_val); ValFree(&B_val);
        fprintf(stderr, "DimensionError: einsum matrix inner dimensions mismatch (%d != %d)\n", colsA, rowsB);
        return -1;
      }
      AKSValue C_val = ValArr(rowsA);
      AKSArray *C = ValToArr(C_val);
      for (int i = 0; i < rowsA; i++) {
        AKSValue row_val = ValArr(colsB);
        AKSArray *row = ValToArr(row_val);
        AKSArray *A_row = ValToArr(A->items[i]);
        for (int k = 0; k < colsB; k++) {
          double sum = 0.0;
          for (int j = 0; j < colsA; j++) {
            double a_ij = (ValKind(A_row->items[j]) == VALFLOAT) ? ValToFloat(A_row->items[j]) : (double)ValToInt(A_row->items[j]);
            AKSArray *B_j = ValToArr(B->items[j]);
            double b_jk = (ValKind(B_j->items[k]) == VALFLOAT) ? ValToFloat(B_j->items[k]) : (double)ValToInt(B_j->items[k]);
            sum += a_ij * b_jk;
          }
          row->items[k] = ValFloat(sum);
        }
        row->count = colsB;
        C->items[i] = row_val;
      }
      C->count = rowsA;
      ValFree(&spec_val); ValFree(&A_val); ValFree(&B_val);
      PUSH(C_val);
      *stacktopptr = stacktop;
      return 1;
    }

    // Dot product: "i,i->"
    if (strstr(spec, "i,i->") && ValKind(A_val) == VALARR && ValKind(B_val) == VALARR) {
      AKSArray *A = ValToArr(A_val);
      AKSArray *B = ValToArr(B_val);
      int n = (A->count < B->count) ? A->count : B->count;
      double dot = 0.0;
      for (int i = 0; i < n; i++) {
        double a_i = (ValKind(A->items[i]) == VALFLOAT) ? ValToFloat(A->items[i]) : (double)ValToInt(A->items[i]);
        double b_i = (ValKind(B->items[i]) == VALFLOAT) ? ValToFloat(B->items[i]) : (double)ValToInt(B->items[i]);
        dot += a_i * b_i;
      }
      ValFree(&spec_val); ValFree(&A_val); ValFree(&B_val);
      PUSH(ValFloat(dot));
      *stacktopptr = stacktop;
      return 1;
    }

    // Matrix transpose: "ij->ji"
    if (strstr(spec, "ij->ji") && ValKind(A_val) == VALARR) {
      AKSArray *A = ValToArr(A_val);
      int rows = A->count;
      int cols = (rows > 0 && ValKind(A->items[0]) == VALARR) ? ValToArr(A->items[0])->count : 0;
      AKSValue T_val = ValArr(cols);
      AKSArray *T = ValToArr(T_val);
      for (int j = 0; j < cols; j++) {
        AKSValue col_val = ValArr(rows);
        AKSArray *col = ValToArr(col_val);
        for (int i = 0; i < rows; i++) {
          col->items[i] = ValCopy(ValToArr(A->items[i])->items[j]);
        }
        col->count = rows;
        T->items[j] = col_val;
      }
      T->count = cols;
      ValFree(&spec_val); ValFree(&A_val); ValFree(&B_val);
      PUSH(T_val);
      *stacktopptr = stacktop;
      return 1;
    }

    // Matrix trace / diagonal: "ii->"
    if (strstr(spec, "ii->") && ValKind(A_val) == VALARR) {
      AKSArray *A = ValToArr(A_val);
      int n = A->count;
      double trace = 0.0;
      for (int i = 0; i < n; i++) {
        if (ValKind(A->items[i]) == VALARR && i < ValToArr(A->items[i])->count) {
          AKSValue diag = ValToArr(A->items[i])->items[i];
          trace += (ValKind(diag) == VALFLOAT) ? ValToFloat(diag) : (double)ValToInt(diag);
        }
      }
      ValFree(&spec_val); ValFree(&A_val); ValFree(&B_val);
      PUSH(ValFloat(trace));
      *stacktopptr = stacktop;
      return 1;
    }

    ValFree(&spec_val); ValFree(&A_val); ValFree(&B_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

  /* High performance compute operations with AVX2 vectorization and cache tiling */
  if (strcmp(name, "FastDotProduct") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: FastDotProduct expects 2 array arguments\n");
      return -1;
    }
    AKSValue b_val = POP();
    AKSValue a_val = POP();
    if (ValKind(a_val) != VALARR || ValKind(b_val) != VALARR) {
      ValFree(&a_val); ValFree(&b_val);
      fprintf(stderr, "Runtime error: FastDotProduct expects array arguments\n");
      return -1;
    }
    AKSArray *arrA = ValToArr(a_val);
    AKSArray *arrB = ValToArr(b_val);
    int n = arrA->count < arrB->count ? arrA->count : arrB->count;

    double total = 0.0;
    int i = 0;
#if defined(__AVX2__) || defined(_M_X64)
    __m256d acc0 = _mm256_setzero_pd();
    __m256d acc1 = _mm256_setzero_pd();

    for (; i + 7 < n; i += 8) {
      double d0[4], d1[4], e0[4], e1[4];
      for (int k = 0; k < 4; k++) {
        AKSValue vA = arrA->items[i + k];
        d0[k] = (ValKind(vA) == VALFLOAT) ? ValToFloat(vA) : (double)ValToInt(vA);
        AKSValue vB = arrB->items[i + k];
        e0[k] = (ValKind(vB) == VALFLOAT) ? ValToFloat(vB) : (double)ValToInt(vB);
      }
      for (int k = 0; k < 4; k++) {
        AKSValue vA = arrA->items[i + 4 + k];
        d1[k] = (ValKind(vA) == VALFLOAT) ? ValToFloat(vA) : (double)ValToInt(vA);
        AKSValue vB = arrB->items[i + 4 + k];
        e1[k] = (ValKind(vB) == VALFLOAT) ? ValToFloat(vB) : (double)ValToInt(vB);
      }
      acc0 = _mm256_fmadd_pd(_mm256_loadu_pd(d0), _mm256_loadu_pd(e0), acc0);
      acc1 = _mm256_fmadd_pd(_mm256_loadu_pd(d1), _mm256_loadu_pd(e1), acc1);
    }
    __m256d sum_acc = _mm256_add_pd(acc0, acc1);
    double res_lanes[4];
    _mm256_storeu_pd(res_lanes, sum_acc);
    total = res_lanes[0] + res_lanes[1] + res_lanes[2] + res_lanes[3];
#endif
    for (; i < n; i++) {
      AKSValue va = arrA->items[i];
      AKSValue vb = arrB->items[i];
      double a = (ValKind(va) == VALFLOAT) ? ValToFloat(va) : (double)ValToInt(va);
      double b = (ValKind(vb) == VALFLOAT) ? ValToFloat(vb) : (double)ValToInt(vb);
      total += a * b;
    }

    ValFree(&a_val);
    ValFree(&b_val);
    PUSH(ValFloat(total));
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastMatMul2D") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: FastMatMul2D expects 2 matrix arguments\n");
      return -1;
    }
    AKSValue b_val = POP();
    AKSValue a_val = POP();
    if (ValKind(a_val) != VALARR || ValKind(b_val) != VALARR) {
      ValFree(&a_val); ValFree(&b_val);
      fprintf(stderr, "Runtime error: FastMatMul2D expects array arguments\n");
      return -1;
    }
    AKSArray *A = ValToArr(a_val);
    AKSArray *B = ValToArr(b_val);
    int rowsA = A->count;
    int colsA = (rowsA > 0 && ValKind(A->items[0]) == VALARR) ? ValToArr(A->items[0])->count : 0;
    int rowsB = B->count;
    int colsB = (rowsB > 0 && ValKind(B->items[0]) == VALARR) ? ValToArr(B->items[0])->count : 0;

    if (colsA != rowsB || rowsA == 0 || colsB == 0) {
      ValFree(&a_val); ValFree(&b_val);
      fprintf(stderr, "DimensionError: matrix dimensions incompatible for multiplication (%dx%d vs %dx%d)\n",
              rowsA, colsA, rowsB, colsB);
      return -1;
    }

    /* Extract dense contiguous buffers for cache locality */
    double *denseA = (double *)malloc(rowsA * colsA * sizeof(double));
    for (int r = 0; r < rowsA; r++) {
      AKSArray *row = ValToArr(A->items[r]);
      for (int c = 0; c < colsA; c++) {
        AKSValue v = row->items[c];
        denseA[r * colsA + c] = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
      }
    }

    /* Transpose matrix B into denseBT (colsB x rowsB) to ensure streaming contiguous reads */
    double *denseBT = (double *)malloc(colsB * rowsB * sizeof(double));
    for (int r = 0; r < rowsB; r++) {
      AKSArray *row = ValToArr(B->items[r]);
      for (int c = 0; c < colsB; c++) {
        AKSValue v = row->items[c];
        denseBT[c * rowsB + r] = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
      }
    }

    /* Create destination array */
    AKSValue C_val = ValArr(rowsA);
    AKSArray *C = ValToArr(C_val);

    for (int i = 0; i < rowsA; i++) {
      AKSValue row_val = ValArr(colsB);
      AKSArray *row = ValToArr(row_val);
      const double *a_row = &denseA[i * colsA];

      for (int j = 0; j < colsB; j++) {
        const double *b_col = &denseBT[j * rowsB];
        double sum = 0.0;
        int k = 0;
#if defined(__AVX2__) || defined(_M_X64)
        __m256d acc = _mm256_setzero_pd();
        for (; k + 3 < colsA; k += 4) {
          __m256d va = _mm256_loadu_pd(&a_row[k]);
          __m256d vb = _mm256_loadu_pd(&b_col[k]);
          acc = _mm256_fmadd_pd(va, vb, acc);
        }
        double lanes[4];
        _mm256_storeu_pd(lanes, acc);
        sum = lanes[0] + lanes[1] + lanes[2] + lanes[3];
#endif
        for (; k < colsA; k++) {
          sum += a_row[k] * b_col[k];
        }
        row->items[j] = ValFloat(sum);
      }
      row->count = colsB;
      C->items[i] = row_val;
    }
    C->count = rowsA;

    free(denseA);
    free(denseBT);
    ValFree(&a_val);
    ValFree(&b_val);

    PUSH(C_val);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastArraySort") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: FastArraySort expects an array\n");
      return -1;
    }
    AKSValue arr_val = POP();
    if (ValKind(arr_val) != VALARR) {
      ValFree(&arr_val);
      fprintf(stderr, "Runtime error: FastArraySort expects an array argument\n");
      return -1;
    }
    AKSArray *arr = ValToArr(arr_val);
    int count = arr->count;
    if (count <= 1) {
      PUSH(arr_val);
      *stacktopptr = stacktop;
      return 1;
    }

    double *dense = (double *)malloc(count * sizeof(double));
    for (int i = 0; i < count; i++) {
      AKSValue v = arr->items[i];
      dense[i] = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
    }

    /* Dual-pivot quicksort with insertion sort for small partitions */
    int stack_low[64];
    int stack_high[64];
    int top = 0;
    stack_low[top] = 0;
    stack_high[top] = count - 1;
    top++;

    while (top > 0) {
      top--;
      int low = stack_low[top];
      int high = stack_high[top];
      if (high <= low) continue;

      if (high - low < 16) {
        for (int i = low + 1; i <= high; i++) {
          double key = dense[i];
          int j = i - 1;
          while (j >= low && dense[j] > key) {
            dense[j + 1] = dense[j];
            j--;
          }
          dense[j + 1] = key;
        }
        continue;
      }

      if (dense[low] > dense[high]) {
        double t = dense[low]; dense[low] = dense[high]; dense[high] = t;
      }
      double p = dense[low];
      double q = dense[high];
      int l = low + 1;
      int g = high - 1;
      int k = l;
      while (k <= g) {
        if (dense[k] < p) {
          double t = dense[k]; dense[k] = dense[l]; dense[l] = t;
          l++;
        } else if (dense[k] >= q) {
          while (dense[g] > q && k < g) g--;
          double t = dense[k]; dense[k] = dense[g]; dense[g] = t;
          g--;
          if (dense[k] < p) {
            double t2 = dense[k]; dense[k] = dense[l]; dense[l] = t2;
            l++;
          }
        }
        k++;
      }
      l--;
      g++;
      double t = dense[low]; dense[low] = dense[l]; dense[l] = t;
      double t2 = dense[high]; dense[high] = dense[g]; dense[g] = t2;

      if (top + 3 < 64) {
        stack_low[top] = low; stack_high[top] = l - 1; top++;
        stack_low[top] = l + 1; stack_high[top] = g - 1; top++;
        stack_low[top] = g + 1; stack_high[top] = high; top++;
      }
    }

    AKSValue res_val = ValArr(count);
    AKSArray *res_arr = ValToArr(res_val);
    for (int i = 0; i < count; i++) {
      res_arr->items[i] = ValFloat(dense[i]);
    }
    res_arr->count = count;
    free(dense);
    ValFree(&arr_val);

    PUSH(res_val);
    *stacktopptr = stacktop;
    return 1;
  }

  if (strcmp(name, "FastParallelReduce") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: FastParallelReduce expects (arr, op)\n");
      return -1;
    }
    AKSValue op_val = POP();
    AKSValue arr_val = POP();
    if (ValKind(arr_val) != VALARR) {
      ValFree(&op_val); ValFree(&arr_val);
      fprintf(stderr, "Runtime error: FastParallelReduce expects an array argument\n");
      return -1;
    }
    AKSArray *arr = ValToArr(arr_val);
    const char *op = (ValKind(op_val) == VALSTR && ValToStrPtr(op_val)) ? ValToStrPtr(op_val)->data : "+";
    int count = arr->count;

    double accum = 0.0;
    if (strcmp(op, "*") == 0) accum = 1.0;
    else if (strcmp(op, "min") == 0 && count > 0) accum = 1e300;
    else if (strcmp(op, "max") == 0 && count > 0) accum = -1e300;

    int i = 0;
#if defined(__AVX2__) || defined(_M_X64)
    if (strcmp(op, "+") == 0) {
      __m256d acc = _mm256_setzero_pd();
      for (; i + 3 < count; i += 4) {
        double d[4];
        for (int k = 0; k < 4; k++) {
          AKSValue v = arr->items[i + k];
          d[k] = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
        }
        acc = _mm256_add_pd(acc, _mm256_loadu_pd(d));
      }
      double lanes[4];
      _mm256_storeu_pd(lanes, acc);
      accum += lanes[0] + lanes[1] + lanes[2] + lanes[3];
    }
#endif
    for (; i < count; i++) {
      AKSValue v = arr->items[i];
      double val = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
      if (strcmp(op, "*") == 0) accum *= val;
      else if (strcmp(op, "min") == 0) { if (val < accum) accum = val; }
      else if (strcmp(op, "max") == 0) { if (val > accum) accum = val; }
      else accum += val;
    }

    ValFree(&op_val);
    ValFree(&arr_val);
    PUSH(ValFloat(accum));
    *stacktopptr = stacktop;
    return 1;
  }

  /* Freestanding hardware memory mapped io and cpu control */
  if (strcmp(name, "MmioWrite8") == 0) {
    if (argc < 2) return -1;
    uint8_t val = (uint8_t)ValToInt(POP());
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    *(volatile uint8_t *)addr = val;
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioRead8") == 0) {
    if (argc < 1) return -1;
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    uint8_t val = *(volatile uint8_t *)addr;
    PUSH(ValInt(val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioWrite16") == 0) {
    if (argc < 2) return -1;
    uint16_t val = (uint16_t)ValToInt(POP());
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    *(volatile uint16_t *)addr = val;
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioRead16") == 0) {
    if (argc < 1) return -1;
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    uint16_t val = *(volatile uint16_t *)addr;
    PUSH(ValInt(val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioWrite32") == 0) {
    if (argc < 2) return -1;
    uint32_t val = (uint32_t)ValToInt(POP());
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    *(volatile uint32_t *)addr = val;
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioRead32") == 0) {
    if (argc < 1) return -1;
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    uint32_t val = *(volatile uint32_t *)addr;
    PUSH(ValInt((int64_t)val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioWrite64") == 0) {
    if (argc < 2) return -1;
    uint64_t val = (uint64_t)ValToInt(POP());
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    *(volatile uint64_t *)addr = val;
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioRead64") == 0) {
    if (argc < 1) return -1;
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    uint64_t val = *(volatile uint64_t *)addr;
    PUSH(ValInt((int64_t)val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FreestandingMemSet") == 0) {
    if (argc < 3) return -1;
    int count = (int)ValToInt(POP());
    uint8_t byte_val = (uint8_t)ValToInt(POP());
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    volatile uint8_t *p = (volatile uint8_t *)addr;
    for (int i = 0; i < count; i++) {
      p[i] = byte_val;
    }
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FreestandingMemCopy") == 0) {
    if (argc < 3) return -1;
    int count = (int)ValToInt(POP());
    uintptr_t src_addr = (uintptr_t)ValToInt(POP());
    uintptr_t dst_addr = (uintptr_t)ValToInt(POP());
    volatile uint8_t *dst = (volatile uint8_t *)dst_addr;
    const volatile uint8_t *src = (const volatile uint8_t *)src_addr;
    for (int i = 0; i < count; i++) {
      dst[i] = src[i];
    }
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FreestandingAlloc") == 0) {
    if (argc < 1) return -1;
    size_t size = (size_t)ValToInt(POP());
    void *ptr = calloc(1, size > 0 ? size : 1);
    PUSH(ValInt((int64_t)(uintptr_t)ptr));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FreestandingFree") == 0) {
    if (argc < 1) return -1;
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    if (addr) free((void *)addr);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CpuNop") == 0) {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    __nop();
#elif defined(__GNUC__)
    __asm__ __volatile__("nop");
#endif
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CpuPause") == 0) {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    _mm_pause();
#elif defined(__GNUC__)
    __asm__ __volatile__("pause");
#endif
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CpuHalt") == 0) {
#if defined(_WIN32)
    Sleep(1);
#else
    usleep(1000);
#endif
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CImportParseHeaderC") == 0) {
    if (argc < 1) return -1;
    AKSValue path_or_src = POP();
    if (ValKind(path_or_src) != VALSTR) {
      ValFree(&path_or_src);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const char *raw_input = ValToStrPtr(path_or_src)->data;
    char *header_text = NULL;
    FILE *f = fopen(raw_input, "rb");
    if (f) {
      fseek(f, 0, SEEK_END);
      long sz = ftell(f);
      fseek(f, 0, SEEK_SET);
      header_text = (char *)malloc(sz + 1);
      if (header_text) {
        fread(header_text, 1, sz, f);
        header_text[sz] = '\0';
      }
      fclose(f);
    } else {
      header_text = strdup(raw_input);
    }
    ValFree(&path_or_src);

    if (!header_text) {
      PUSH(ValStruct("CHeader"));
      *stacktopptr = stacktop;
      return 1;
    }

    size_t len = strlen(header_text);
    char *clean = (char *)malloc(len + 1);
    size_t c_idx = 0;
    int in_scomment = 0, in_bcomment = 0;
    for (size_t i = 0; i < len; i++) {
      if (in_scomment) {
        if (header_text[i] == '\n') in_scomment = 0;
        continue;
      }
      if (in_bcomment) {
        if (header_text[i] == '*' && i + 1 < len && header_text[i+1] == '/') {
          in_bcomment = 0;
          i++;
        }
        continue;
      }
      if (header_text[i] == '/' && i + 1 < len && header_text[i+1] == '/') {
        in_scomment = 1;
        i++;
        continue;
      }
      if (header_text[i] == '/' && i + 1 < len && header_text[i+1] == '*') {
        in_bcomment = 1;
        i++;
        continue;
      }
      if (header_text[i] == '#') {
        while (i < len && header_text[i] != '\n') i++;
        continue;
      }
      clean[c_idx++] = (header_text[i] == '\n' || header_text[i] == '\r' || header_text[i] == '\t') ? ' ' : header_text[i];
    }
    clean[c_idx] = '\0';
    free(header_text);

    AKSValue res_struct = ValStruct("CHeader");
    AKSStruct *st = ValToObj(res_struct);
    st->fieldcount = 0;
    st->fieldnames = NULL;
    st->fieldvals = NULL;

    char *p = clean;
    while (*p) {
      char *semi = strchr(p, ';');
      if (!semi) break;
      *semi = '\0';
      char *decl = p;
      p = semi + 1;

      while (*decl && isspace((unsigned char)*decl)) decl++;
      char *dend = decl + strlen(decl);
      while (dend > decl && isspace((unsigned char)*(dend - 1))) {
        *(--dend) = '\0';
      }

      char *lparen = strchr(decl, '(');
      char *rparen = strrchr(decl, ')');
      if (lparen && rparen && lparen < rparen) {
        *lparen = '\0';
        *rparen = '\0';
        char *params_str = lparen + 1;

        char *name_end = decl + strlen(decl);
        while (name_end > decl && isspace((unsigned char)*(name_end - 1))) {
          *(--name_end) = '\0';
        }
        char *name_start = name_end;
        while (name_start > decl && (isalnum((unsigned char)*(name_start - 1)) || *(name_start - 1) == '_')) {
          name_start--;
        }

        if (name_start < name_end && (isalpha((unsigned char)*name_start) || *name_start == '_')) {
          char fn_name[128];
          size_t fn_len = name_end - name_start;
          if (fn_len >= sizeof(fn_name)) fn_len = sizeof(fn_name) - 1;
          memcpy(fn_name, name_start, fn_len);
          fn_name[fn_len] = '\0';

          char ret_type[128] = "int";
          size_t ret_len = name_start - decl;
          if (ret_len > 0) {
            if (ret_len >= sizeof(ret_type)) ret_len = sizeof(ret_type) - 1;
            memcpy(ret_type, decl, ret_len);
            ret_type[ret_len] = '\0';
          }

          char ret_sig = 'i';
          if (strstr(ret_type, "double") || strstr(ret_type, "float")) ret_sig = 'd';
          else if (strstr(ret_type, "void")) ret_sig = 'v';
          else if (strstr(ret_type, "char*") || strstr(ret_type, "char *")) ret_sig = 's';
          else if (strchr(ret_type, '*')) ret_sig = 'p';

          char param_sigs[32];
          int param_count = 0;
          char *tok = strtok(params_str, ",");
          while (tok && param_count < 16) {
            while (*tok && isspace((unsigned char)*tok)) tok++;
            if (strcmp(tok, "void") == 0) {
              tok = strtok(NULL, ",");
              continue;
            }
            if (strstr(tok, "double") || strstr(tok, "float")) {
              param_sigs[param_count++] = 'd';
            } else if (strstr(tok, "char*") || strstr(tok, "char *")) {
              param_sigs[param_count++] = 's';
            } else if (strchr(tok, '*')) {
              param_sigs[param_count++] = 'p';
            } else {
              param_sigs[param_count++] = 'i';
            }
            tok = strtok(NULL, ",");
          }
          param_sigs[param_count] = '\0';

          char sig_buf[64];
          snprintf(sig_buf, sizeof(sig_buf), "%c(%s)", ret_sig, param_sigs);

          st->fieldcount++;
          st->fieldnames = (char **)realloc(st->fieldnames, st->fieldcount * sizeof(char *));
          st->fieldvals = (AKSValue *)realloc(st->fieldvals, st->fieldcount * sizeof(AKSValue));
          st->fieldnames[st->fieldcount - 1] = strdup(fn_name);
          st->fieldvals[st->fieldcount - 1] = ValStr(sig_buf);
        }
      }
    }
    free(clean);

    PUSH(res_struct);
    *stacktopptr = stacktop;
    return 1;
  }

  /* Deep runtime and compile-time type reflection */
  if (strcmp(name, "TypeInfo") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: TypeInfo() expects 1 argument, got %d\n", argc);
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
          typename_str = (src->typename && src->typename[0]) ? src->typename : "struct";
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

  /* Non-blocking channel receive */
  if (strcmp(name, "ChannelTryReceive") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ChannelTryReceive() expects 1 argument\n");
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

  /* Non-blocking channel send */
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

  /* IEEE-754 64-bit float and integer bitcasting */
  if (strcmp(name, "FloatToBits") == 0) {
    if (argc != 1) return -1;
    AKSValue arg = POP();
    double d = 0.0;
    if (ValKind(arg) == VALFLOAT) {
      d = ValToFloat(arg);
    } else if (ValKind(arg) == VALINT) {
      d = (double)ValToInt(arg);
    }
    ValFree(&arg);
    uint64_t bits = 0;
    memcpy(&bits, &d, sizeof(double));
    uint32_t hi = (uint32_t)(bits >> 32);
    uint32_t lo = (uint32_t)(bits & 0xFFFFFFFFULL);
    AKSValue res = ValArr(2);
    AKSArray *arr = ValToArr(res);
    arr->items[0] = ValInt((int64_t)hi);
    arr->items[1] = ValInt((int64_t)lo);
    arr->count = 2;
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FloatFromBits") == 0) {
    if (argc == 1) {
      AKSValue arg = POP();
      uint32_t hi = 0;
      uint32_t lo = 0;
      if (ValKind(arg) == VALARR) {
        AKSArray *arr = ValToArr(arg);
        if (arr && arr->count >= 2) {
          hi = (uint32_t)ValToInt(arr->items[0]);
          lo = (uint32_t)ValToInt(arr->items[1]);
        }
      }
      ValFree(&arg);
      uint64_t bits = (((uint64_t)hi) << 32) | (uint64_t)lo;
      double d = 0.0;
      memcpy(&d, &bits, sizeof(double));
      PUSH(ValFloat(d));
      *stacktopptr = stacktop;
      return 1;
    } else if (argc == 2) {
      AKSValue lo_val = POP();
      AKSValue hi_val = POP();
      uint32_t hi = (uint32_t)ValToInt(hi_val);
      uint32_t lo = (uint32_t)ValToInt(lo_val);
      ValFree(&hi_val);
      ValFree(&lo_val);
      uint64_t bits = (((uint64_t)hi) << 32) | (uint64_t)lo;
      double d = 0.0;
      memcpy(&d, &bits, sizeof(double));
      PUSH(ValFloat(d));
      *stacktopptr = stacktop;
      return 1;
    }
    return -1;
  }

  /* Endianness byte swapping */
  if (strcmp(name, "ByteSwap16") == 0) {
    if (argc != 1) return -1;
    AKSValue arg = POP();
    uint16_t v = (uint16_t)(ValKind(arg) == VALINT ? ValToInt(arg) : 0);
    ValFree(&arg);
    uint16_t swapped = (uint16_t)((v >> 8) | (v << 8));
    PUSH(ValInt((int64_t)swapped));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ByteSwap32") == 0) {
    if (argc != 1) return -1;
    AKSValue arg = POP();
    uint32_t v = (uint32_t)(ValKind(arg) == VALINT ? ValToInt(arg) : 0);
    ValFree(&arg);
    uint32_t swapped = ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
                       ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
    PUSH(ValInt((int64_t)swapped));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ByteSwap64") == 0) {
    if (argc != 1) return -1;
    AKSValue arg = POP();
    if (ValKind(arg) == VALARR) {
      AKSArray *arr = ValToArr(arg);
      uint32_t hi = arr && arr->count > 0 ? (uint32_t)ValToInt(arr->items[0]) : 0;
      uint32_t lo = arr && arr->count > 1 ? (uint32_t)ValToInt(arr->items[1]) : 0;
      ValFree(&arg);
      uint32_t s_hi = ((lo >> 24) & 0xFF) | ((lo >> 8) & 0xFF00) |
                      ((lo << 8) & 0xFF0000) | ((lo << 24) & 0xFF000000);
      uint32_t s_lo = ((hi >> 24) & 0xFF) | ((hi >> 8) & 0xFF00) |
                      ((hi << 8) & 0xFF0000) | ((hi << 24) & 0xFF000000);
      AKSValue res = ValArr(2);
      AKSArray *res_arr = ValToArr(res);
      res_arr->items[0] = ValInt((int64_t)s_hi);
      res_arr->items[1] = ValInt((int64_t)s_lo);
      res_arr->count = 2;
      PUSH(res);
      *stacktopptr = stacktop;
      return 1;
    } else {
      uint64_t v = (uint64_t)(ValKind(arg) == VALINT ? ValToInt(arg) : 0);
      ValFree(&arg);
      uint64_t swapped = 0;
      for (int i = 0; i < 8; i++) {
        swapped = (swapped << 8) | (v & 0xFF);
        v >>= 8;
      }
      PUSH(ValInt((int64_t)(swapped & 0x0000FFFFFFFFFFFFULL)));
      *stacktopptr = stacktop;
      return 1;
    }
  }
#undef PUSH
#undef POP
  return 0;
}
