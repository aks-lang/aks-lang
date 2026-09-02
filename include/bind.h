#ifndef AKS_BIND_H
#define AKS_BIND_H

int AKSBindHeaders(const char *header_path, const char *output_path, int is_cpp, const char *libname);
int AKSGenerateWasmGlue(const char *wit_path, const char *js_path, const char *ts_path);

#endif // Aks_bind_h
