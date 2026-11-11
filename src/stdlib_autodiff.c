// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "vm_stdlib.h"

/* Reverse-mode automatic differentiation computational graph and tape engine */

typedef struct {
  int id;
  int op; /* 0=VAR, 1=CONST, 2=ADD, 3=SUB, 4=MUL, 5=DIV, 6=NEG, 7=POW,
             8=EXP, 9=LOG, 10=SIN, 11=COS, 12=TAN, 13=RELU, 14=SIGMOID,
             15=TANH, 16=SQRT, 17=TENSOR_VAR, 18=TENSOR_MATMUL, 19=TENSOR_ADD,
             20=TENSOR_RELU, 21=TENSOR_MSE, 22=TENSOR_CROSS_ENTROPY */
  double val;
  double grad;
  int lhs;    /* Node index of first operand (-1 if none) */
  int rhs;    /* Node index of second operand (-1 if none) */
  double aux; /* Auxiliary scalar constant (for scalar ops or constants) */
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
  if (!t || !t->nodes)
    return;
  for (int i = 0; i < t->count; i++) {
    if (t->nodes[i].is_tensor) {
      if (t->nodes[i].tval) {
        free(t->nodes[i].tval);
        t->nodes[i].tval = NULL;
      }
      if (t->nodes[i].tgrad) {
        free(t->nodes[i].tgrad);
        t->nodes[i].tgrad = NULL;
      }
    }
  }
  t->count = 0;
}

static void AutodiffInitTapes(void) {
  if (g_autodiff_inited)
    return;
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
  if (tape_id < 0 || tape_id >= MAX_AUTODIFF_TAPES)
    return &g_autodiff_tapes[0];
  if (!g_autodiff_tapes[tape_id].active) {
    g_autodiff_tapes[tape_id].id = tape_id;
    g_autodiff_tapes[tape_id].active = 1;
    g_autodiff_tapes[tape_id].capacity = 1024;
    g_autodiff_tapes[tape_id].nodes =
        (TapeNode *)malloc(sizeof(TapeNode) * 1024);
    memset(g_autodiff_tapes[tape_id].nodes, 0, sizeof(TapeNode) * 1024);
    g_autodiff_tapes[tape_id].count = 0;
  }
  return &g_autodiff_tapes[tape_id];
}

static int AutodiffAllocNode(AutodiffTape *t, int op, double val, int lhs,
                             int rhs, double aux) {
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

static int AutodiffAllocTensorNode(AutodiffTape *t, int op, int rows, int cols,
                                   int lhs, int rhs, const double *init_val) {
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

static int AutodiffForwardOp(AutodiffTape *t, int op, int lhs, int rhs,
                             double aux) {
  double lv = (lhs >= 0 && lhs < t->count) ? t->nodes[lhs].val : 0.0;
  double rv = (rhs >= 0 && rhs < t->count) ? t->nodes[rhs].val : aux;
  double out = 0.0;
  switch (op) {
  case 1: /* CONST */
    out = aux;
    break;
  case 2: /* ADD */
    out = lv + rv;
    break;
  case 3: /* SUB */
    out = lv - rv;
    break;
  case 4: /* MUL */
    out = lv * rv;
    break;
  case 5: /* DIV */
    out = (fabs(rv) < 1e-15) ? 0.0 : (lv / rv);
    break;
  case 6: /* NEG */
    out = -lv;
    break;
  case 7: /* POW */
    out = pow(lv, rv);
    break;
  case 8: /* EXP */
    out = exp(lv);
    break;
  case 9: /* LOG */
    out = (lv > 1e-15) ? log(lv) : log(1e-15);
    break;
  case 10: /* SIN */
    out = sin(lv);
    break;
  case 11: /* COS */
    out = cos(lv);
    break;
  case 12: /* TAN */
    out = tan(lv);
    break;
  case 13: /* RELU */
    out = (lv > 0.0) ? lv : 0.0;
    break;
  case 14: /* SIGMOID */
    out = 1.0 / (1.0 + exp(-lv));
    break;
  case 15: /* TANH */
    out = tanh(lv);
    break;
  case 16: /* SQRT */
    out = (lv >= 0.0) ? sqrt(lv) : 0.0;
    break;
  default:
    out = lv;
    break;
  }
  return AutodiffAllocNode(t, op, out, lhs, rhs, aux);
}

static void AutodiffBackward(AutodiffTape *t, int loss_nid) {
  if (!t || loss_nid < 0 || loss_nid >= t->count)
    return;
  for (int i = 0; i < t->count; i++) {
    t->nodes[i].grad = 0.0;
    if (t->nodes[i].is_tensor && t->nodes[i].tgrad) {
      memset(t->nodes[i].tgrad, 0,
             sizeof(double) * t->nodes[i].rows * t->nodes[i].cols);
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
        int lhs_bcast =
            (lhs >= 0 && rhs >= 0 && t->nodes[lhs].cols == t->nodes[rhs].cols &&
             t->nodes[rhs].rows == 1 && t->nodes[lhs].rows > 1);
        for (int k = 0; k < total; k++) {
          double gk = t->nodes[i].tgrad[k];
          if (lhs >= 0 && t->nodes[lhs].tgrad)
            t->nodes[lhs].tgrad[k] += gk;
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
            if (t->nodes[lhs].tval[k] > 0.0)
              t->nodes[lhs].tgrad[k] += gk;
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
              if (t->nodes[lhs].tval[r * N + c] > max_val)
                max_val = t->nodes[lhs].tval[r * N + c];
            }
            double sum_exp = 0.0;
            for (int c = 0; c < N; c++) {
              sum_exp += exp(t->nodes[lhs].tval[r * N + c] - max_val);
            }
            for (int c = 0; c < N; c++) {
              double p = exp(t->nodes[lhs].tval[r * N + c] - max_val) /
                         (sum_exp > 0.0 ? sum_exp : 1.0);
              double target = t->nodes[rhs].tval[r * N + c];
              t->nodes[lhs].tgrad[r * N + c] +=
                  out_g * (p - target) / (double)(M > 0 ? M : 1);
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
                sum += t->nodes[i].tgrad[r * N + c] *
                       t->nodes[rhs].tval[k * N + c];
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
                sum += t->nodes[lhs].tval[r * K + k] *
                       t->nodes[i].tgrad[r * N + c];
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
    if (g == 0.0)
      continue;
    int lhs = t->nodes[i].lhs;
    int rhs = t->nodes[i].rhs;
    double aux = t->nodes[i].aux;
    switch (t->nodes[i].op) {
    case 0: /* VAR */
    case 1: /* CONST */
      break;
    case 2: /* ADD */
      if (lhs >= 0)
        t->nodes[lhs].grad += g;
      if (rhs >= 0)
        t->nodes[rhs].grad += g;
      break;
    case 3: /* SUB */
      if (lhs >= 0)
        t->nodes[lhs].grad += g;
      if (rhs >= 0)
        t->nodes[rhs].grad -= g;
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
        if (fabs(denom) < 1e-15)
          denom = (denom < 0 ? -1e-15 : 1e-15);
        t->nodes[lhs].grad += g / denom;
        t->nodes[rhs].grad -= g * t->nodes[lhs].val / (denom * denom);
      } else if (lhs >= 0) {
        double denom = (fabs(aux) < 1e-15) ? (aux < 0 ? -1e-15 : 1e-15) : aux;
        t->nodes[lhs].grad += g / denom;
      }
      break;
    case 6: /* NEG */
      if (lhs >= 0)
        t->nodes[lhs].grad -= g;
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
      if (lhs >= 0)
        t->nodes[lhs].grad += g * exp(t->nodes[lhs].val);
      break;
    case 9: /* LOG */
      if (lhs >= 0) {
        double lv = t->nodes[lhs].val;
        if (fabs(lv) < 1e-15)
          lv = (lv < 0 ? -1e-15 : 1e-15);
        t->nodes[lhs].grad += g / lv;
      }
      break;
    case 10: /* SIN */
      if (lhs >= 0)
        t->nodes[lhs].grad += g * cos(t->nodes[lhs].val);
      break;
    case 11: /* COS */
      if (lhs >= 0)
        t->nodes[lhs].grad -= g * sin(t->nodes[lhs].val);
      break;
    case 12: /* TAN */
      if (lhs >= 0) {
        double c = cos(t->nodes[lhs].val);
        if (fabs(c) < 1e-15)
          c = (c < 0 ? -1e-15 : 1e-15);
        t->nodes[lhs].grad += g / (c * c);
      }
      break;
    case 13: /* RELU */
      if (lhs >= 0)
        t->nodes[lhs].grad += (t->nodes[lhs].val > 0.0 ? g : 0.0);
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
        if (sq < 1e-15)
          sq = 1e-15;
        t->nodes[lhs].grad += g / (2.0 * sq);
      }
      break;
    default:
      break;
    }
  }
}


static AKSValue MakePhysUnit(double val, int m, int kg, int s) {
  AKSValue p = ValStruct("PhysUnit");
  AKSStruct *st = ValToObj(p);
  st->fieldcount = 8;
  st->fieldnames = malloc(8 * sizeof(char *));
  st->fieldvals = malloc(8 * sizeof(AKSValue));
  st->fieldnames[0] = strdup("val");
  st->fieldvals[0] = ValFloat(val);
  st->fieldnames[1] = strdup("magnitude");
  st->fieldvals[1] = ValFloat(val);
  st->fieldnames[2] = strdup("m");
  st->fieldvals[2] = ValInt(m);
  st->fieldnames[3] = strdup("kg");
  st->fieldvals[3] = ValInt(kg);
  st->fieldnames[4] = strdup("s");
  st->fieldvals[4] = ValInt(s);

  AKSValue u = ValStruct("UnitDim");
  AKSStruct *ust = ValToObj(u);
  ust->fieldcount = 7;
  ust->fieldnames = malloc(7 * sizeof(char *));
  ust->fieldvals = malloc(7 * sizeof(AKSValue));
  ust->fieldnames[0] = strdup("m");
  ust->fieldvals[0] = ValInt(m);
  ust->fieldnames[1] = strdup("kg");
  ust->fieldvals[1] = ValInt(kg);
  ust->fieldnames[2] = strdup("s");
  ust->fieldvals[2] = ValInt(s);
  ust->fieldnames[3] = strdup("a");
  ust->fieldvals[3] = ValInt(0);
  ust->fieldnames[4] = strdup("k");
  ust->fieldvals[4] = ValInt(0);
  ust->fieldnames[5] = strdup("mol");
  ust->fieldvals[5] = ValInt(0);
  ust->fieldnames[6] = strdup("cd");
  ust->fieldvals[6] = ValInt(0);

  st->fieldnames[5] = strdup("unit");
  st->fieldvals[5] = u;
  st->fieldnames[6] = strdup("dim");
  st->fieldvals[6] = ValCopy(u);
  st->fieldnames[7] = strdup("name");
  st->fieldvals[7] = ValStr("PhysUnit");
  return p;
}

static void PhysExtractUnit(AKSValue u, double *out_val, int *out_m,
                            int *out_kg, int *out_s) {
  double v = 0.0;
  int m = 0, kg = 0, s = 0;
  if (ValKind(u) == VALSTRUCT) {
    AKSStruct *st = ValToObj(u);
    for (int i = 0; i < st->fieldcount; i++) {
      if (st->fieldnames[i]) {
        if (strcmp(st->fieldnames[i], "val") == 0 ||
            strcmp(st->fieldnames[i], "magnitude") == 0) {
          v = (ValKind(st->fieldvals[i]) == VALFLOAT)
                  ? ValToFloat(st->fieldvals[i])
                  : (double)ValToInt(st->fieldvals[i]);
        } else if (strcmp(st->fieldnames[i], "m") == 0) {
          m = (int)ValToInt(st->fieldvals[i]);
        } else if (strcmp(st->fieldnames[i], "kg") == 0) {
          kg = (int)ValToInt(st->fieldvals[i]);
        } else if (strcmp(st->fieldnames[i], "s") == 0) {
          s = (int)ValToInt(st->fieldvals[i]);
        } else if ((strcmp(st->fieldnames[i], "unit") == 0 ||
                    strcmp(st->fieldnames[i], "dim") == 0) &&
                   ValKind(st->fieldvals[i]) == VALSTRUCT) {
          AKSStruct *ust = ValToObj(st->fieldvals[i]);
          for (int j = 0; j < ust->fieldcount; j++) {
            if (ust->fieldnames[j]) {
              if (strcmp(ust->fieldnames[j], "m") == 0 && m == 0)
                m = (int)ValToInt(ust->fieldvals[j]);
              else if (strcmp(ust->fieldnames[j], "kg") == 0 && kg == 0)
                kg = (int)ValToInt(ust->fieldvals[j]);
              else if (strcmp(ust->fieldnames[j], "s") == 0 && s == 0)
                s = (int)ValToInt(ust->fieldvals[j]);
            }
          }
        }
      }
    }
  } else if (ValKind(u) == VALFLOAT) {
    v = ValToFloat(u);
  } else if (ValKind(u) == VALINT) {
    v = (double)ValToInt(u);
  }
  *out_val = v;
  *out_m = m;
  *out_kg = kg;
  *out_s = s;
}
int ExecuteStdlibAutodiff(VM *vm, VMTask *task, const char *name, int argc,
            AKSValue **stacktopptr) {
  AKSValue *stacktop = *stacktopptr;
  AKSValue *stacklimit = task->stack + (task->stackcap > 0 ? task->stackcap : MAXSTACK);
#define PUSH(val) AKS_STDLIB_PUSH(val)
#define POP() AKS_STDLIB_POP()

  if (strcmp(name, "TapeNew") == 0) {
    AutodiffInitTapes();
    int slot = -1;
    for (int i = 1; i < MAX_AUTODIFF_TAPES; i++) {
      if (!g_autodiff_tapes[i].active) {
        slot = i;
        break;
      }
    }
    if (slot < 0)
      slot = 0;
    AutodiffTape *t = AutodiffGetTape(slot);
    t->count = 0;
    PUSH(ValInt(slot));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeFree") == 0) {
    if (argc < 1) {
      fprintf(stderr,
              "Runtime error: TapeFree() expects 1 argument (tape_id)\n");
      return -1;
    }
    AKSValue tid_val = POP();
    int tid = (int)ValToInt(tid_val);
    ValFree(&tid_val);
    if (tid > 0 && tid < MAX_AUTODIFF_TAPES && g_autodiff_tapes[tid].active) {
      AutodiffFreeTapeNodes(&g_autodiff_tapes[tid]);
      if (g_autodiff_tapes[tid].nodes)
        free(g_autodiff_tapes[tid].nodes);
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
      fprintf(stderr,
              "Runtime error: TapeVar() expects 2 arguments (tape_id, val)\n");
      return -1;
    }
    AKSValue val_val = POP();
    AKSValue tape_val = POP();
    int tid = (int)ValToInt(tape_val);
    double v = (ValKind(val_val) == VALFLOAT) ? ValToFloat(val_val)
                                              : (double)ValToInt(val_val);
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
      fprintf(stderr, "Runtime error: TapeOp() expects 5 arguments (tape_id, "
                      "op, lhs, rhs, aux)\n");
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
    double aux = (ValKind(aux_val) == VALFLOAT) ? ValToFloat(aux_val)
                                                : (double)ValToInt(aux_val);
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
      fprintf(
          stderr,
          "Runtime error: TapeGetVal() expects 2 arguments (tape_id, nid)\n");
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
      fprintf(
          stderr,
          "Runtime error: TapeGetGrad() expects 2 arguments (tape_id, nid)\n");
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
      fprintf(stderr, "Runtime error: TapeBackward() expects 2 arguments "
                      "(tape_id, loss_nid)\n");
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
      fprintf(stderr, "Runtime error: TapeTensorVar() expects 4 arguments "
                      "(tape_id, rows, cols, data_array)\n");
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
        buf[k] = (ValKind(oa->items[k]) == VALFLOAT)
                     ? ValToFloat(oa->items[k])
                     : (double)ValToInt(oa->items[k]);
      }
    }
    ValFree(&arr_val);
    AutodiffTape *t = AutodiffGetTape(tid);
    int nid = AutodiffAllocTensorNode(t, 17 /* TENSOR_VAR */, rows, cols, -1,
                                      -1, buf);
    free(buf);
    PUSH(ValInt(nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeTensorMatMul") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Runtime error: TapeTensorMatMul() expects 3 arguments "
                      "(tape_id, lhs_nid, rhs_nid)\n");
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
    int nid = AutodiffAllocTensorNode(t, 18 /* TENSOR_MATMUL */, M, N, lhs_nid,
                                      rhs_nid, C);
    free(C);
    PUSH(ValInt(nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeTensorAdd") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Runtime error: TapeTensorAdd() expects 3 arguments "
                      "(tape_id, lhs_nid, rhs_nid)\n");
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
    int nid = AutodiffAllocTensorNode(t, 19 /* TENSOR_ADD */, rows, cols,
                                      lhs_nid, rhs_nid, C);
    free(C);
    PUSH(ValInt(nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeTensorRelu") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: TapeTensorRelu() expects 2 arguments "
                      "(tape_id, lhs_nid)\n");
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
    int nid = AutodiffAllocTensorNode(t, 20 /* TENSOR_RELU */, rows, cols,
                                      lhs_nid, -1, C);
    free(C);
    PUSH(ValInt(nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TapeTensorGetVal") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: TapeTensorGetVal() expects 2 arguments "
                      "(tape_id, nid)\n");
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
      fprintf(stderr, "Runtime error: TapeTensorGetGrad() expects 2 arguments "
                      "(tape_id, nid)\n");
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
      fprintf(stderr, "Runtime error: TapeTensorBackward() expects 2 arguments "
                      "(tape_id, loss_nid)\n");
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
      fprintf(
          stderr,
          "Runtime error: TapeReset() expects at least 1 argument (tape_id)\n");
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
  if (strcmp(name, "NnLinear") == 0) {
    if (argc < 2) {
      fprintf(
          stderr,
          "Runtime error: NnLinear() expects 2 arguments (in_dim, out_dim)\n");
      return -1;
    }
    AKSValue out_val = POP();
    AKSValue in_val = POP();
    int in_dim = (int)ValToInt(in_val);
    int out_dim = (int)ValToInt(out_val);
    ValFree(&out_val);
    ValFree(&in_val);
    if (in_dim <= 0)
      in_dim = 1;
    if (out_dim <= 0)
      out_dim = 1;

    AKSValue root = ValStruct("NnLinear");
    AKSStruct *s = ValToObj(root);
    s->fieldnames = malloc(sizeof(char *) * 11);
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
    for (int k = 0; k < total_w; k++)
      mwa->items[mwa->count++] = ValFloat(0.0);

    AKSValue vw_val = ValArr(total_w);
    AKSArray *vwa = ValToArr(vw_val);
    for (int k = 0; k < total_w; k++)
      vwa->items[vwa->count++] = ValFloat(0.0);

    AKSValue mb_val = ValArr(out_dim);
    AKSArray *mba = ValToArr(mb_val);
    for (int k = 0; k < out_dim; k++)
      mba->items[mba->count++] = ValFloat(0.0);

    AKSValue vb_val = ValArr(out_dim);
    AKSArray *vba = ValToArr(vb_val);
    for (int k = 0; k < out_dim; k++)
      vba->items[vba->count++] = ValFloat(0.0);

    s->fieldnames[0] = strdup("in_dim");
    s->fieldvals[0] = ValInt(in_dim);
    s->fieldnames[1] = strdup("out_dim");
    s->fieldvals[1] = ValInt(out_dim);
    s->fieldnames[2] = strdup("weights");
    s->fieldvals[2] = w_val;
    s->fieldnames[3] = strdup("bias");
    s->fieldvals[3] = b_val;
    s->fieldnames[4] = strdup("weight_nid");
    s->fieldvals[4] = ValInt(-1);
    s->fieldnames[5] = strdup("bias_nid");
    s->fieldvals[5] = ValInt(-1);
    s->fieldnames[6] = strdup("m_w");
    s->fieldvals[6] = mw_val;
    s->fieldnames[7] = strdup("v_w");
    s->fieldvals[7] = vw_val;
    s->fieldnames[8] = strdup("m_b");
    s->fieldvals[8] = mb_val;
    s->fieldnames[9] = strdup("v_b");
    s->fieldvals[9] = vb_val;
    s->fieldnames[10] = strdup("step");
    s->fieldvals[10] = ValInt(0);

    PUSH(root);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NnForward") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: NnForward() expects at least 2 arguments "
                      "(layer, input, [tape_id])\n");
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
      ValFree(&x_val);
      ValFree(&layer_val);
      fprintf(stderr, "Runtime error: NnForward() first argument must be an "
                      "NnLinear layer\n");
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
      if (B < 1)
        B = 1;
      double *xbuf = (double *)calloc(total > 0 ? total : 1, sizeof(double));
      for (int k = 0; k < total; k++) {
        xbuf[k] = (ValKind(xa->items[k]) == VALFLOAT)
                      ? ValToFloat(xa->items[k])
                      : (double)ValToInt(xa->items[k]);
      }
      x_nid = AutodiffAllocTensorNode(t, 17 /* TENSOR_VAR */, B, in_dim, -1, -1,
                                      xbuf);
      free(xbuf);
    } else {
      x_nid = (int)ValToInt(x_val);
      B = t->nodes[x_nid].rows;
    }

    AKSArray *wa = ValToArr(*AksStructFindField(s, "weights"));
    int wtot = in_dim * out_dim;
    double *wbuf = (double *)calloc(wtot > 0 ? wtot : 1, sizeof(double));
    for (int k = 0; k < wtot && k < wa->count; k++) {
      wbuf[k] = (ValKind(wa->items[k]) == VALFLOAT)
                    ? ValToFloat(wa->items[k])
                    : (double)ValToInt(wa->items[k]);
    }
    int w_nid = AutodiffAllocTensorNode(t, 17 /* TENSOR_VAR */, in_dim, out_dim,
                                        -1, -1, wbuf);
    free(wbuf);
    *AksStructFindField(s, "weight_nid") = ValInt(w_nid);

    AKSArray *ba = ValToArr(*AksStructFindField(s, "bias"));
    double *bbuf = (double *)calloc(out_dim > 0 ? out_dim : 1, sizeof(double));
    for (int k = 0; k < out_dim && k < ba->count; k++) {
      bbuf[k] = (ValKind(ba->items[k]) == VALFLOAT)
                    ? ValToFloat(ba->items[k])
                    : (double)ValToInt(ba->items[k]);
    }
    int b_nid = AutodiffAllocTensorNode(t, 17 /* TENSOR_VAR */, 1, out_dim, -1,
                                        -1, bbuf);
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
    int mm_nid = AutodiffAllocTensorNode(t, 18 /* TENSOR_MATMUL */, M, N, x_nid,
                                         w_nid, C);
    free(C);

    TapeNode *mn = &t->nodes[mm_nid];
    TapeNode *bn = &t->nodes[b_nid];
    double *Out = (double *)calloc(M * N > 0 ? M * N : 1, sizeof(double));
    for (int r = 0; r < M; r++) {
      for (int c = 0; c < N; c++) {
        Out[r * N + c] = mn->tval[r * N + c] + bn->tval[c];
      }
    }
    int out_nid = AutodiffAllocTensorNode(t, 19 /* TENSOR_ADD */, M, N, mm_nid,
                                          b_nid, Out);
    free(Out);

    ValFree(&x_val);
    ValFree(&layer_val);
    PUSH(ValInt(out_nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NnRelu") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: NnRelu() expects at least 1 argument "
                      "(nid, [tape_id])\n");
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
    int relu_nid = AutodiffAllocTensorNode(t, 20 /* TENSOR_RELU */, rows, cols,
                                           nid, -1, C);
    free(C);
    PUSH(ValInt(relu_nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NnSoftmax") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: NnSoftmax() expects at least 1 argument "
                      "(arr_or_nid, [tape_id])\n");
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
        vals[k] = (ValKind(a->items[k]) == VALFLOAT)
                      ? ValToFloat(a->items[k])
                      : (double)ValToInt(a->items[k]);
      }
    } else {
      int nid = (int)ValToInt(arg);
      AutodiffTape *t = AutodiffGetTape(tid);
      count = t->nodes[nid].rows * t->nodes[nid].cols;
      vals = (double *)calloc(count > 0 ? count : 1, sizeof(double));
      if (t->nodes[nid].tval)
        memcpy(vals, t->nodes[nid].tval, sizeof(double) * count);
    }
    ValFree(&arg);

    double max_v = -1e30;
    for (int k = 0; k < count; k++)
      if (vals[k] > max_v)
        max_v = vals[k];
    double sum_exp = 0.0;
    for (int k = 0; k < count; k++)
      sum_exp += exp(vals[k] - max_v);
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
      fprintf(stderr, "Runtime error: NnMseLoss() expects at least 2 arguments "
                      "(pred_nid, target_arr, [tape_id])\n");
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
        tbuf[k] = (ValKind(ta->items[k]) == VALFLOAT)
                      ? ValToFloat(ta->items[k])
                      : (double)ValToInt(ta->items[k]);
      }
    }
    int target_nid = AutodiffAllocTensorNode(t, 17 /* TENSOR_VAR */, rows, cols,
                                             -1, -1, tbuf);

    double sum_sq = 0.0;
    for (int k = 0; k < total; k++) {
      double diff = pn->tval[k] - tbuf[k];
      sum_sq += diff * diff;
    }
    free(tbuf);
    double loss = (total > 0) ? (sum_sq / (double)total) : 0.0;

    int loss_nid = AutodiffAllocTensorNode(t, 21 /* TENSOR_MSE */, 1, 1,
                                           pred_nid, target_nid, &loss);
    ValFree(&pred_v);
    ValFree(&target_v);

    PUSH(ValInt(loss_nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NnCrossEntropy") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: NnCrossEntropy() expects at least 2 "
                      "arguments (logits_nid, target_arr, [tape_id])\n");
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
        tbuf[k] = (ValKind(ta->items[k]) == VALFLOAT)
                      ? ValToFloat(ta->items[k])
                      : (double)ValToInt(ta->items[k]);
      }
    }
    int target_nid =
        AutodiffAllocTensorNode(t, 17 /* TENSOR_VAR */, M, N, -1, -1, tbuf);

    double total_loss = 0.0;
    for (int r = 0; r < M; r++) {
      double max_v = -1e30;
      for (int c = 0; c < N; c++)
        if (ln->tval[r * N + c] > max_v)
          max_v = ln->tval[r * N + c];
      double sum_exp = 0.0;
      for (int c = 0; c < N; c++)
        sum_exp += exp(ln->tval[r * N + c] - max_v);
      for (int c = 0; c < N; c++) {
        double p =
            exp(ln->tval[r * N + c] - max_v) / (sum_exp > 0.0 ? sum_exp : 1.0);
        total_loss -= tbuf[r * N + c] * log(p > 1e-15 ? p : 1e-15);
      }
    }
    free(tbuf);
    double loss = (M > 0) ? (total_loss / (double)M) : 0.0;

    int loss_nid = AutodiffAllocTensorNode(t, 22 /* TENSOR_CROSS_ENTROPY */, 1,
                                           1, logits_nid, target_nid, &loss);
    ValFree(&logits_v);
    ValFree(&target_v);

    PUSH(ValInt(loss_nid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NnLossVal") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: NnLossVal() expects at least 1 argument "
                      "(loss_nid, [tape_id])\n");
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
    double val =
        (t->nodes[nid].tval) ? t->nodes[nid].tval[0] : t->nodes[nid].val;
    PUSH(ValFloat(val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NnGetVal") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: NnGetVal() expects at least 1 argument "
                      "(nid, [tape_id])\n");
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
      ra->items[ra->count++] =
          ValFloat(t->nodes[nid].tval ? t->nodes[nid].tval[k] : 0.0);
    }
    PUSH(res_v);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NnSGD") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: NnSGD() expects at least 2 arguments "
                      "(layers, lr, [tape_id])\n");
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
    double lr =
        (ValKind(lr_v) == VALFLOAT) ? ValToFloat(lr_v) : (double)ValToInt(lr_v);
    ValFree(&lr_v);
    AutodiffTape *t = AutodiffGetTape(tid);

    AKSValue layer_list[64];
    int layer_count = 0;
    if (ValKind(layers_v) == VALARR) {
      AKSArray *la = ValToArr(layers_v);
      for (int i = 0; i < la->count && layer_count < 64; i++) {
        if (ValKind(la->items[i]) == VALSTRUCT)
          layer_list[layer_count++] = la->items[i];
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
          for (int r = 0; r < bn->rows; r++)
            g += bn->tgrad[r * out_cols + c];
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
      fprintf(stderr, "Runtime error: NnAdam() expects at least 2 arguments "
                      "(layers, lr, [beta1, beta2, eps, tape_id])\n");
      return -1;
    }
    int tid = 0;
    double eps = 1e-8;
    double beta2 = 0.999;
    double beta1 = 0.9;
    if (argc >= 6) {
      AKSValue v = POP();
      tid = (int)ValToInt(v);
      ValFree(&v);
    }
    if (argc >= 5) {
      AKSValue v = POP();
      eps = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
      ValFree(&v);
    }
    if (argc >= 4) {
      AKSValue v = POP();
      beta2 = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
      ValFree(&v);
    }
    if (argc >= 3) {
      AKSValue v = POP();
      beta1 = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
      ValFree(&v);
    }
    AKSValue lr_v = POP();
    AKSValue layers_v = POP();
    double lr =
        (ValKind(lr_v) == VALFLOAT) ? ValToFloat(lr_v) : (double)ValToInt(lr_v);
    ValFree(&lr_v);
    AutodiffTape *t = AutodiffGetTape(tid);

    AKSValue layer_list[64];
    int layer_count = 0;
    if (ValKind(layers_v) == VALARR) {
      AKSArray *la = ValToArr(layers_v);
      for (int i = 0; i < la->count && layer_count < 64; i++) {
        if (ValKind(la->items[i]) == VALSTRUCT)
          layer_list[layer_count++] = la->items[i];
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
      if (bc1 < 1e-15)
        bc1 = 1.0;
      if (bc2 < 1e-15)
        bc2 = 1.0;

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
          for (int r = 0; r < bn->rows; r++)
            g += bn->tgrad[r * out_cols + c];
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
      fprintf(stderr, "Runtime error: NnRMSProp() expects at least 2 arguments "
                      "(layers, lr, [alpha, eps, tape_id])\n");
      return -1;
    }
    int tid = 0;
    double eps = 1e-8;
    double alpha = 0.99;
    if (argc >= 5) {
      AKSValue v = POP();
      tid = (int)ValToInt(v);
      ValFree(&v);
    }
    if (argc >= 4) {
      AKSValue v = POP();
      eps = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
      ValFree(&v);
    }
    if (argc >= 3) {
      AKSValue v = POP();
      alpha = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
      ValFree(&v);
    }
    AKSValue lr_v = POP();
    AKSValue layers_v = POP();
    double lr =
        (ValKind(lr_v) == VALFLOAT) ? ValToFloat(lr_v) : (double)ValToInt(lr_v);
    ValFree(&lr_v);
    AutodiffTape *t = AutodiffGetTape(tid);

    AKSValue layer_list[64];
    int layer_count = 0;
    if (ValKind(layers_v) == VALARR) {
      AKSArray *la = ValToArr(layers_v);
      for (int i = 0; i < la->count && layer_count < 64; i++) {
        if (ValKind(la->items[i]) == VALSTRUCT)
          layer_list[layer_count++] = la->items[i];
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
          for (int r = 0; r < bn->rows; r++)
            g += bn->tgrad[r * out_cols + c];
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
  if (strcmp(name, "PhysUnit") == 0) {
    int s = (argc >= 4) ? (int)ValToInt(POP()) : 0;
    int kg = (argc >= 3) ? (int)ValToInt(POP()) : 0;
    int m = (argc >= 2) ? (int)ValToInt(POP()) : 0;
    AKSValue v_v = (argc >= 1) ? POP() : ValFloat(0.0);
    double val =
        (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
    ValFree(&v_v);
    AKSValue p = MakePhysUnit(val, m, kg, s);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Meter") == 0) {
    AKSValue v_v = POP();
    double val =
        (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
    ValFree(&v_v);
    AKSValue p = MakePhysUnit(val, 1, 0, 0);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Kilogram") == 0) {
    AKSValue v_v = POP();
    double val =
        (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
    ValFree(&v_v);
    AKSValue p = MakePhysUnit(val, 0, 1, 0);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Second") == 0) {
    AKSValue v_v = POP();
    double val =
        (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
    ValFree(&v_v);
    AKSValue p = MakePhysUnit(val, 0, 0, 1);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Newton") == 0) {
    if (argc >= 2) {
      AKSValue u2 = POP();
      AKSValue u1 = POP();
      double v1 = 0, v2 = 0;
      int m1 = 0, kg1 = 0, s1 = 0, m2 = 0, kg2 = 0, s2 = 0;
      PhysExtractUnit(u1, &v1, &m1, &kg1, &s1);
      PhysExtractUnit(u2, &v2, &m2, &kg2, &s2);
      ValFree(&u1);
      ValFree(&u2);
      AKSValue p = MakePhysUnit(v1 * v2, m1 + m2, kg1 + kg2, s1 + s2);
      PUSH(p);
      *stacktopptr = stacktop;
      return 1;
    } else {
      AKSValue v_v = POP();
      double val =
          (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
      ValFree(&v_v);
      AKSValue p = MakePhysUnit(val, 1, 1, -2);
      PUSH(p);
      *stacktopptr = stacktop;
      return 1;
    }
  }
  if (strcmp(name, "Joule") == 0) {
    if (argc >= 2) {
      AKSValue u2 = POP();
      AKSValue u1 = POP();
      double v1 = 0, v2 = 0;
      int m1 = 0, kg1 = 0, s1 = 0, m2 = 0, kg2 = 0, s2 = 0;
      PhysExtractUnit(u1, &v1, &m1, &kg1, &s1);
      PhysExtractUnit(u2, &v2, &m2, &kg2, &s2);
      ValFree(&u1);
      ValFree(&u2);
      AKSValue p = MakePhysUnit(v1 * v2, m1 + m2, kg1 + kg2, s1 + s2);
      PUSH(p);
      *stacktopptr = stacktop;
      return 1;
    } else {
      AKSValue v_v = POP();
      double val =
          (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
      ValFree(&v_v);
      AKSValue p = MakePhysUnit(val, 2, 1, -2);
      PUSH(p);
      *stacktopptr = stacktop;
      return 1;
    }
  }
  if (strcmp(name, "Watt") == 0) {
    if (argc >= 2) {
      AKSValue u2 = POP();
      AKSValue u1 = POP();
      double v1 = 0, v2 = 0;
      int m1 = 0, kg1 = 0, s1 = 0, m2 = 0, kg2 = 0, s2 = 0;
      PhysExtractUnit(u1, &v1, &m1, &kg1, &s1);
      PhysExtractUnit(u2, &v2, &m2, &kg2, &s2);
      ValFree(&u1);
      ValFree(&u2);
      AKSValue p = MakePhysUnit(v1 / v2, m1 - m2, kg1 - kg2, s1 - s2);
      PUSH(p);
      *stacktopptr = stacktop;
      return 1;
    } else {
      AKSValue v_v = POP();
      double val =
          (ValKind(v_v) == VALFLOAT) ? ValToFloat(v_v) : (double)ValToInt(v_v);
      ValFree(&v_v);
      AKSValue p = MakePhysUnit(val, 2, 1, -3);
      PUSH(p);
      *stacktopptr = stacktop;
      return 1;
    }
  }
  if (strcmp(name, "PhysVal") == 0) {
    AKSValue u = POP();
    double v = 0.0;
    int m = 0, kg = 0, s = 0;
    PhysExtractUnit(u, &v, &m, &kg, &s);
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
    PhysExtractUnit(u1, &v1, &m1, &kg1, &s1);
    PhysExtractUnit(u2, &v2, &m2, &kg2, &s2);
    ValFree(&u1);
    ValFree(&u2);
    AKSValue p = MakePhysUnit(v1 * v2, m1 + m2, kg1 + kg2, s1 + s2);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "PhysDiv") == 0) {
    AKSValue u2 = POP();
    AKSValue u1 = POP();
    double v1 = 0, v2 = 0;
    int m1 = 0, kg1 = 0, s1 = 0, m2 = 0, kg2 = 0, s2 = 0;
    PhysExtractUnit(u1, &v1, &m1, &kg1, &s1);
    PhysExtractUnit(u2, &v2, &m2, &kg2, &s2);
    ValFree(&u1);
    ValFree(&u2);
    AKSValue p = MakePhysUnit(v1 / v2, m1 - m2, kg1 - kg2, s1 - s2);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "PhysAdd") == 0) {
    AKSValue u2 = POP();
    AKSValue u1 = POP();
    double v1 = 0, v2 = 0;
    int m1 = 0, kg1 = 0, s1 = 0, m2 = 0, kg2 = 0, s2 = 0;
    PhysExtractUnit(u1, &v1, &m1, &kg1, &s1);
    PhysExtractUnit(u2, &v2, &m2, &kg2, &s2);
    ValFree(&u1);
    ValFree(&u2);
    if (m1 != m2 || kg1 != kg2 || s1 != s2) {
      fprintf(stderr,
              "DimensionalMismatchError: cannot add quantities of different "
              "physical dimensions [%d,%d,%d] vs [%d,%d,%d]\n",
              m1, kg1, s1, m2, kg2, s2);
      return -1;
    }
    AKSValue p = MakePhysUnit(v1 + v2, m1, kg1, s1);
    PUSH(p);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "PhysSub") == 0) {
    AKSValue u2 = POP();
    AKSValue u1 = POP();
    double v1 = 0, v2 = 0;
    int m1 = 0, kg1 = 0, s1 = 0, m2 = 0, kg2 = 0, s2 = 0;
    PhysExtractUnit(u1, &v1, &m1, &kg1, &s1);
    PhysExtractUnit(u2, &v2, &m2, &kg2, &s2);
    ValFree(&u1);
    ValFree(&u2);
    if (m1 != m2 || kg1 != kg2 || s1 != s2) {
      fprintf(stderr,
              "DimensionalMismatchError: cannot subtract quantities of "
              "different physical dimensions [%d,%d,%d] vs [%d,%d,%d]\n",
              m1, kg1, s1, m2, kg2, s2);
      return -1;
    }
    AKSValue p = MakePhysUnit(v1 - v2, m1, kg1, s1);
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
    const char *spec = (ValKind(spec_val) == VALSTR && ValToStrPtr(spec_val))
                           ? ValToStrPtr(spec_val)->data
                           : "";

    // Matrix multiplication: "ij,jk->ik"
    if (strstr(spec, "ij,jk->ik") && ValKind(A_val) == VALARR &&
        ValKind(B_val) == VALARR) {
      AKSArray *A = ValToArr(A_val);
      AKSArray *B = ValToArr(B_val);
      int rowsA = A->count;
      int colsA = (rowsA > 0 && ValKind(A->items[0]) == VALARR)
                      ? ValToArr(A->items[0])->count
                      : 0;
      int rowsB = B->count;
      int colsB = (rowsB > 0 && ValKind(B->items[0]) == VALARR)
                      ? ValToArr(B->items[0])->count
                      : 0;
      if (colsA != rowsB) {
        ValFree(&spec_val);
        ValFree(&A_val);
        ValFree(&B_val);
        fprintf(stderr,
                "DimensionError: einsum matrix inner dimensions mismatch (%d "
                "!= %d)\n",
                colsA, rowsB);
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
            double a_ij = (ValKind(A_row->items[j]) == VALFLOAT)
                              ? ValToFloat(A_row->items[j])
                              : (double)ValToInt(A_row->items[j]);
            AKSArray *B_j = ValToArr(B->items[j]);
            double b_jk = (ValKind(B_j->items[k]) == VALFLOAT)
                              ? ValToFloat(B_j->items[k])
                              : (double)ValToInt(B_j->items[k]);
            sum += a_ij * b_jk;
          }
          row->items[k] = ValFloat(sum);
        }
        row->count = colsB;
        C->items[i] = row_val;
      }
      C->count = rowsA;
      ValFree(&spec_val);
      ValFree(&A_val);
      ValFree(&B_val);
      PUSH(C_val);
      *stacktopptr = stacktop;
      return 1;
    }

    // Dot product: "i,i->"
    if (strstr(spec, "i,i->") && ValKind(A_val) == VALARR &&
        ValKind(B_val) == VALARR) {
      AKSArray *A = ValToArr(A_val);
      AKSArray *B = ValToArr(B_val);
      int n = (A->count < B->count) ? A->count : B->count;
      double dot = 0.0;
      for (int i = 0; i < n; i++) {
        double a_i = (ValKind(A->items[i]) == VALFLOAT)
                         ? ValToFloat(A->items[i])
                         : (double)ValToInt(A->items[i]);
        double b_i = (ValKind(B->items[i]) == VALFLOAT)
                         ? ValToFloat(B->items[i])
                         : (double)ValToInt(B->items[i]);
        dot += a_i * b_i;
      }
      ValFree(&spec_val);
      ValFree(&A_val);
      ValFree(&B_val);
      PUSH(ValFloat(dot));
      *stacktopptr = stacktop;
      return 1;
    }

    // Matrix transpose: "ij->ji"
    if (strstr(spec, "ij->ji") && ValKind(A_val) == VALARR) {
      AKSArray *A = ValToArr(A_val);
      int rows = A->count;
      int cols = (rows > 0 && ValKind(A->items[0]) == VALARR)
                     ? ValToArr(A->items[0])->count
                     : 0;
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
      ValFree(&spec_val);
      ValFree(&A_val);
      ValFree(&B_val);
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
        if (ValKind(A->items[i]) == VALARR &&
            i < ValToArr(A->items[i])->count) {
          AKSValue diag = ValToArr(A->items[i])->items[i];
          trace += (ValKind(diag) == VALFLOAT) ? ValToFloat(diag)
                                               : (double)ValToInt(diag);
        }
      }
      ValFree(&spec_val);
      ValFree(&A_val);
      ValFree(&B_val);
      PUSH(ValFloat(trace));
      *stacktopptr = stacktop;
      return 1;
    }

    ValFree(&spec_val);
    ValFree(&A_val);
    ValFree(&B_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }

#undef PUSH
#undef POP
  return 0;
}
