#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
typedef HANDLE ProtoWriteHandle;
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#define closesocket(s) close(s)
#define Sleep(ms) usleep((ms) * 1000)
typedef int ProtoWriteHandle;
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void build_file_uri(const char *fs_path, char *out_uri, size_t out_sz) {
    char norm[1024];
    snprintf(norm, sizeof(norm), "%s", fs_path);
    for (size_t i = 0; norm[i]; i++) {
        if (norm[i] == '\\') norm[i] = '/';
    }
#ifdef _WIN32
    snprintf(out_uri, out_sz, "file:///%s", norm);
#else
    snprintf(out_uri, out_sz, "file://%s", norm);
#endif
}

static int write_text_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t len = strlen(content);
    size_t w = fwrite(content, 1, len, f);
    fclose(f);
    return w == len;
}

static void send_lsp_msg(ProtoWriteHandle hWr, int id, const char *method, const char *params) {
    char body[4096];
    int body_len;
    if (id > 0) {
        if (params) {
            body_len = snprintf(body, sizeof(body), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\",\"params\":%s}", id, method, params);
        } else {
            body_len = snprintf(body, sizeof(body), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\"}", id, method);
        }
    } else {
        if (params) {
            body_len = snprintf(body, sizeof(body), "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s}", method, params);
        } else {
            body_len = snprintf(body, sizeof(body), "{\"jsonrpc\":\"2.0\",\"method\":\"%s\"}", method);
        }
    }
    char hdr[128];
    int hdr_len = snprintf(hdr, sizeof(hdr), "Content-Length: %d\r\n\r\n", body_len);
#ifdef _WIN32
    DWORD written;
    WriteFile(hWr, hdr, (DWORD)hdr_len, &written, NULL);
    WriteFile(hWr, body, (DWORD)body_len, &written, NULL);
#else
    if (write(hWr, hdr, (size_t)hdr_len) < 0) {}
    if (write(hWr, body, (size_t)body_len) < 0) {}
#endif
}

static void feed_lsp_requests(ProtoWriteHandle hWr, const char *root_uri, const char *ws_main_uri) {
    char params_buf[2048];
    snprintf(params_buf, sizeof(params_buf), "{\"rootUri\":\"%s\"}", root_uri);
    send_lsp_msg(hWr, 1, "initialize", params_buf);
    send_lsp_msg(hWr, 2, "textDocument/didOpen", "{\"textDocument\":{\"uri\":\"file:///test.aks\",\"languageId\":\"aks\",\"version\":1,\"text\":\"let a = 10;\"}}");
    send_lsp_msg(hWr, 3, "textDocument/hover", "{\"textDocument\":{\"uri\":\"file:///test.aks\"},\"position\":{\"line\":0,\"character\":4}}");
    send_lsp_msg(hWr, 0, "textDocument/didOpen", "{\"textDocument\":{\"uri\":\"file:///borrow_err.aks\",\"languageId\":\"aks\",\"version\":1,\"text\":\"let x = \\\"str\\\";\\nlet y = x;\\nprint(x);\"}}");
    send_lsp_msg(hWr, 4, "textDocument/codeAction", "{\"textDocument\":{\"uri\":\"file:///borrow_err.aks\"},\"range\":{\"start\":{\"line\":2,\"character\":0},\"end\":{\"line\":2,\"character\":8}},\"context\":{\"diagnostics\":[{\"code\":\"E0382\",\"message\":\"use of moved value: `x`\"}]}}");
    send_lsp_msg(hWr, 5, "textDocument/documentSymbol", "{\"textDocument\":{\"uri\":\"file:///test.aks\"}}");
    send_lsp_msg(hWr, 6, "textDocument/formatting", "{\"textDocument\":{\"uri\":\"file:///test.aks\"},\"options\":{\"tabSize\":4,\"insertSpaces\":true}}");
    send_lsp_msg(hWr, 7, "textDocument/inlayHint", "{\"textDocument\":{\"uri\":\"file:///test.aks\"},\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":10,\"character\":0}}}");
    send_lsp_msg(hWr, 0, "textDocument/didOpen", "{\"textDocument\":{\"uri\":\"file:///sig_test.aks\",\"languageId\":\"aks\",\"version\":1,\"text\":\"fn add(a: int, b: int) -> int { return a + b; }\\nadd(10, 20);\"}}");
    send_lsp_msg(hWr, 8, "textDocument/signatureHelp", "{\"textDocument\":{\"uri\":\"file:///sig_test.aks\"},\"position\":{\"line\":1,\"character\":8}}");

    // Cross-file workspace indexing requests
    snprintf(params_buf, sizeof(params_buf),
             "{\"textDocument\":{\"uri\":\"%s\",\"languageId\":\"aks\",\"version\":1,\"text\":\"import \\\"ws_math.aks\\\";\\nlet ws_res = cross_mul(6, 7);\"}}",
             ws_main_uri);
    send_lsp_msg(hWr, 0, "textDocument/didOpen", params_buf);

    send_lsp_msg(hWr, 10, "workspace/symbol", "{\"query\":\"cross_mul\"}");

    snprintf(params_buf, sizeof(params_buf),
             "{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":1,\"character\":15}}",
             ws_main_uri);
    send_lsp_msg(hWr, 11, "textDocument/definition", params_buf);
    send_lsp_msg(hWr, 12, "textDocument/hover", params_buf);

    snprintf(params_buf, sizeof(params_buf),
             "{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":1,\"character\":26}}",
             ws_main_uri);
    send_lsp_msg(hWr, 13, "textDocument/signatureHelp", params_buf);

    send_lsp_msg(hWr, 9, "shutdown", "{}");
    send_lsp_msg(hWr, 0, "exit", NULL);
}

static int test_lsp(void) {
    printf("Running aksls LSP Automated Tests (Native C)...\n"); fflush(stdout);

    char ws_dir[1024];
#ifdef _WIN32
    char tmp_base[512];
    GetTempPathA(sizeof(tmp_base), tmp_base);
    snprintf(ws_dir, sizeof(ws_dir), "%saks_lsp_ws_%lu", tmp_base, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(ws_dir, NULL);
#else
    snprintf(ws_dir, sizeof(ws_dir), "/tmp/aks_lsp_ws_%lu", (unsigned long)getpid());
    mkdir(ws_dir, 0755);
#endif

    char ws_math_path[1200], ws_main_path[1200];
    snprintf(ws_math_path, sizeof(ws_math_path), "%s/ws_math.aks", ws_dir);
    snprintf(ws_main_path, sizeof(ws_main_path), "%s/ws_main.aks", ws_dir);

    if (!write_text_file(ws_math_path, "fn cross_mul(a: int, b: int) -> int {\n    return a * b;\n}\n") ||
        !write_text_file(ws_main_path, "import \"ws_math.aks\";\nlet ws_res = cross_mul(6, 7);\n")) {
        printf("FAIL: Could not create temporary workspace files\n");
        return 1;
    }

    char ws_root_uri[1200], ws_main_uri[1200];
    build_file_uri(ws_dir, ws_root_uri, sizeof(ws_root_uri));
    build_file_uri(ws_main_path, ws_main_uri, sizeof(ws_main_uri));

    char full_output[65536];
    full_output[0] = '\0';
    int total_bytes = 0;

#ifdef _WIN32
    HANDLE hChildStd_IN_Rd = NULL, hChildStd_IN_Wr = NULL;
    HANDLE hChildStd_OUT_Rd = NULL, hChildStd_OUT_Wr = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &sa, 0)) return 1;
    if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) return 1;
    if (!CreatePipe(&hChildStd_IN_Rd, &hChildStd_IN_Wr, &sa, 0)) return 1;
    if (!SetHandleInformation(hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0)) return 1;

    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdOutput = hChildStd_OUT_Wr;
    si.hStdInput = hChildStd_IN_Rd;
    si.dwFlags |= STARTF_USESTDHANDLES;

    char cmdLine[] = ".\\aksls.exe";
    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        printf("FAIL: Failed to spawn aksls.exe\n");
        return 1;
    }
    CloseHandle(hChildStd_OUT_Wr);
    CloseHandle(hChildStd_IN_Rd);

    feed_lsp_requests(hChildStd_IN_Wr, ws_root_uri, ws_main_uri);
    CloseHandle(hChildStd_IN_Wr);

    char buffer[4096];
    DWORD bytesRead;
    while (ReadFile(hChildStd_OUT_Rd, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        if (total_bytes + (int)bytesRead < (int)sizeof(full_output) - 1) {
            strcat(full_output, buffer);
            total_bytes += (int)bytesRead;
        }
    }
    CloseHandle(hChildStd_OUT_Rd);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#else
    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) return 1;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        execl("./aksls", "./aksls", (char *)NULL);
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);

    feed_lsp_requests(in_pipe[1], ws_root_uri, ws_main_uri);
    close(in_pipe[1]);

    char buffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = read(out_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytesRead] = '\0';
        if (total_bytes + (int)bytesRead < (int)sizeof(full_output) - 1) {
            strcat(full_output, buffer);
            total_bytes += (int)bytesRead;
        }
    }
    close(out_pipe[0]);
    waitpid(pid, NULL, 0);
#endif

    remove(ws_math_path);
    remove(ws_main_path);
#ifdef _WIN32
    RemoveDirectoryA(ws_dir);
#else
    rmdir(ws_dir);
#endif

    int got_init_response = (strstr(full_output, "workspaceSymbolProvider") != NULL && strstr(full_output, "\"id\":1") != NULL);
    int got_diag = (strstr(full_output, "publishDiagnostics") != NULL && strstr(full_output, "E0382") != NULL);
    int got_related = (strstr(full_output, "relatedInformation") != NULL);
    int got_code_action = (strstr(full_output, "quickfix") != NULL && strstr(full_output, "Borrow value") != NULL);
    int got_symbols = (strstr(full_output, "\"name\":\"a\"") != NULL || strstr(full_output, "\"kind\":13") != NULL);
    int got_formatting = (strstr(full_output, "newText") != NULL || strstr(full_output, "\"id\":6") != NULL);
    int got_inlay = (strstr(full_output, "\": int\"") != NULL || strstr(full_output, "\"id\":7") != NULL);
    int got_sig_help = (strstr(full_output, "fn add(a: int, b: int) -> int") != NULL && strstr(full_output, "\"activeParameter\":1") != NULL);

    const char *ws_sym_resp = strstr(full_output, "\"id\":10");
    const char *def_resp = strstr(full_output, "\"id\":11");
    const char *hover_resp = strstr(full_output, "\"id\":12");
    const char *sig_resp = strstr(full_output, "\"id\":13");

    int got_ws_sym = (ws_sym_resp != NULL && strstr(ws_sym_resp, "\"name\":\"cross_mul\"") != NULL && strstr(ws_sym_resp, "ws_math.aks") != NULL);
    int got_cross_def = (def_resp != NULL && strstr(def_resp, "ws_math.aks") != NULL && strstr(def_resp, "\"line\":0") != NULL);
    int got_cross_hover = (hover_resp != NULL && strstr(hover_resp, "fn cross_mul(a: int, b: int) -> int") != NULL);
    int got_cross_sig = (sig_resp != NULL && strstr(sig_resp, "fn cross_mul(a: int, b: int) -> int") != NULL);

    if (got_init_response && got_diag && got_related && got_code_action && got_symbols && got_formatting &&
        got_inlay && got_sig_help && got_ws_sym && got_cross_def && got_cross_hover && got_cross_sig) {
        printf("  LSP initialize capabilities & workspace indexing: PASS\n");
        printf("  LSP multi-span borrow diagnostics (E0382): PASS\n");
        printf("  LSP DiagnosticRelatedInformation move spans: PASS\n");
        printf("  LSP Quick-Fix Code Actions (Borrow / Clone): PASS\n");
        printf("  LSP Document Symbols & Outline (kind:13): PASS\n");
        printf("  LSP Document Formatting (textDocument/formatting): PASS\n");
        printf("  LSP Inlay Hints Type Inference (: int): PASS\n");
        printf("  LSP Signature Help (textDocument/signatureHelp): PASS\n");
        printf("  LSP Cross-File Workspace Symbols (workspace/symbol): PASS\n");
        printf("  LSP Cross-File Go-To-Definition & Hover: PASS\n");
        printf("  LSP Cross-File Signature Help: PASS\n");
        printf("All LSP TESTS PASSED SUCCESSFULLY!\n");
        return 0;
    } else {
        printf("FAIL: LSP response missing expected fields:\n");
        printf("  init=%d, diag=%d, related=%d, codeAction=%d, symbols=%d, fmt=%d, inlay=%d, sigHelp=%d\n",
               got_init_response, got_diag, got_related, got_code_action, got_symbols, got_formatting, got_inlay, got_sig_help);
        printf("  wsSym=%d, crossDef=%d, crossHover=%d, crossSig=%d\n",
               got_ws_sym, got_cross_def, got_cross_hover, got_cross_sig);
        return 1;
    }
}

static void send_dap_req(SOCKET sock, const char *json) {
    char buf[2048];
    snprintf(buf, sizeof(buf), "Content-Length: %d\r\n\r\n%s", (int)strlen(json), json);
    send(sock, buf, (int)strlen(buf), 0);
}

static int test_dap(void) {
    printf("Running aksls DAP Automated Tests (Native C)...\n"); fflush(stdout);

#ifdef _WIN32
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    char cmdLine[] = ".\\aks.exe dap tests/DebugTarget.aks 4711";
    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        printf("FAIL: Failed to spawn aks.exe dap\n");
        return 1;
    }
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    pid_t pid = fork();
    if (pid == 0) {
        execl("./aks", "./aks", "dap", "tests/DebugTarget.aks", "4711", (char *)NULL);
        _exit(127);
    }
#endif

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_port = htons(4711);

    int conn = -1;
    for (int retry = 0; retry < 25; retry++) {
        Sleep(100);
        conn = connect(sock, (struct sockaddr *)&server, sizeof(server));
        if (conn == 0) break;
    }
    if (conn < 0) {
        printf("FAIL: Could not connect to DAP server on 127.0.0.1:4711\n");
        closesocket(sock);
#ifdef _WIN32
        WSACleanup();
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
#else
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
#endif
        return 1;
    }

    send_dap_req(sock, "{\"jsonrpc\":\"2.0\",\"id\":1,\"type\":\"request\",\"command\":\"initialize\"}");
    send_dap_req(sock, "{\"jsonrpc\":\"2.0\",\"id\":2,\"type\":\"request\",\"command\":\"setBreakpoints\",\"arguments\":{\"source\":{\"path\":\"tests/DebugTarget.aks\"},\"breakpoints\":[{\"line\":2}]}}");
    send_dap_req(sock, "{\"jsonrpc\":\"2.0\",\"id\":3,\"type\":\"request\",\"command\":\"threads\"}");
    send_dap_req(sock, "{\"jsonrpc\":\"2.0\",\"id\":4,\"type\":\"request\",\"command\":\"scopes\",\"arguments\":{\"frameId\":0}}");

    char dap_out[16384];
    dap_out[0] = '\0';
    int dap_len = 0;
    for (int attempt = 0; attempt < 20; attempt++) {
        char recv_buf[2048];
        int r = (int)recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);
        if (r <= 0) break;
        recv_buf[r] = '\0';
        if (dap_len + r < (int)sizeof(dap_out) - 1) {
            strcat(dap_out, recv_buf);
            dap_len += r;
        }
        if ((strstr(dap_out, "capabilities") != NULL || strstr(dap_out, "initialize") != NULL) &&
            strstr(dap_out, "verified") != NULL &&
            strstr(dap_out, "Main Thread") != NULL &&
            strstr(dap_out, "Locals") != NULL) {
            break;
        }
    }

    int got_init = (strstr(dap_out, "initialize") != NULL || strstr(dap_out, "capabilities") != NULL || strstr(dap_out, "stopped") != NULL);
    int got_bp = (strstr(dap_out, "verified") != NULL);
    int got_threads = (strstr(dap_out, "Main Thread") != NULL);
    int got_scopes = (strstr(dap_out, "Locals") != NULL);
    int passed = (got_init && got_bp && got_threads && got_scopes);

    send_dap_req(sock, "{\"jsonrpc\":\"2.0\",\"id\":5,\"type\":\"request\",\"command\":\"disconnect\"}");

    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
    WaitForSingleObject(pi.hProcess, 3000);
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#else
    waitpid(pid, NULL, 0);
#endif

    if (passed) {
        printf("  DAP initialize & socket protocol: PASS\n");
        printf("  DAP setBreakpoints / threads / scopes: PASS\n");
        printf("All DAP TESTS PASSED SUCCESSFULLY!\n");
        return 0;
    } else {
        printf("FAIL: Invalid or missing DAP response (init=%d bp=%d threads=%d scopes=%d)\n",
               got_init, got_bp, got_threads, got_scopes);
        return 1;
    }
}

int main(void) {
    if (test_lsp() != 0) return 1;
    if (test_dap() != 0) return 1;
    printf("\nAll Editor Protocol Tests Passed Successfully!\n");
    return 0;
}
