#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

static void send_lsp_msg(HANDLE hWr, int id, const char *method, const char *params) {
    char body[2048];
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
    DWORD written;
    WriteFile(hWr, hdr, (DWORD)hdr_len, &written, NULL);
    WriteFile(hWr, body, (DWORD)body_len, &written, NULL);
}

static int test_lsp(void) {
    printf("Running aksls LSP Automated Tests (Native C)...\n"); fflush(stdout);
    
    HANDLE hChildStd_IN_Rd = NULL;
    HANDLE hChildStd_IN_Wr = NULL;
    HANDLE hChildStd_OUT_Rd = NULL;
    HANDLE hChildStd_OUT_Wr = NULL;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

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

    send_lsp_msg(hChildStd_IN_Wr, 1, "initialize", "{}");
    send_lsp_msg(hChildStd_IN_Wr, 2, "textDocument/didOpen", "{\"textDocument\":{\"uri\":\"file:///test.aks\",\"languageId\":\"aks\",\"version\":1,\"text\":\"let a = 10;\"}}");
    send_lsp_msg(hChildStd_IN_Wr, 3, "textDocument/hover", "{\"textDocument\":{\"uri\":\"file:///test.aks\"},\"position\":{\"line\":0,\"character\":4}}");
    // Test borrow checker multi-span diagnostic publication
    send_lsp_msg(hChildStd_IN_Wr, 0, "textDocument/didOpen", "{\"textDocument\":{\"uri\":\"file:///borrow_err.aks\",\"languageId\":\"aks\",\"version\":1,\"text\":\"let x = \\\"str\\\";\\nlet y = x;\\nprint(x);\"}}");
    // Test quick-fix code actions
    send_lsp_msg(hChildStd_IN_Wr, 4, "textDocument/codeAction", "{\"textDocument\":{\"uri\":\"file:///borrow_err.aks\"},\"range\":{\"start\":{\"line\":2,\"character\":0},\"end\":{\"line\":2,\"character\":8}},\"context\":{\"diagnostics\":[{\"code\":\"E0382\",\"message\":\"use of moved value: `x`\"}]}}");
    send_lsp_msg(hChildStd_IN_Wr, 5, "shutdown", "{}");
    send_lsp_msg(hChildStd_IN_Wr, 0, "exit", NULL);
    CloseHandle(hChildStd_IN_Wr);

    char full_output[65536];
    full_output[0] = '\0';
    int total_bytes = 0;
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

    int got_init_response = (strstr(full_output, "capabilities") != NULL || strstr(full_output, "\"id\":1") != NULL);
    int got_diag = (strstr(full_output, "publishDiagnostics") != NULL && strstr(full_output, "E0382") != NULL);
    int got_related = (strstr(full_output, "relatedInformation") != NULL);
    int got_code_action = (strstr(full_output, "quickfix") != NULL && strstr(full_output, "Borrow value") != NULL);

    if (got_init_response && got_diag && got_related && got_code_action) {
        printf("  LSP initialize capabilities & response: PASS\n");
        printf("  LSP multi-span borrow diagnostics (E0382): PASS\n");
        printf("  LSP DiagnosticRelatedInformation move spans: PASS\n");
        printf("  LSP Quick-Fix Code Actions (Borrow / Clone): PASS\n");
        printf("All LSP TESTS PASSED SUCCESSFULLY!\n");
        return 0;
    } else {
        printf("FAIL: LSP response missing expected fields:\n");
        printf("  init=%d, diag=%d, related=%d, codeAction=%d\n", got_init_response, got_diag, got_related, got_code_action);
        return 1;
    }
}

static int test_dap(void) {
    printf("Running aksls DAP Automated Tests (Native C)...\n"); fflush(stdout);

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

    Sleep(800);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_port = htons(4711);

    int conn = connect(sock, (struct sockaddr *)&server, sizeof(server));
    if (conn < 0) {
        printf("FAIL: Could not connect to DAP server on 127.0.0.1:4711\n");
        closesocket(sock);
        WSACleanup();
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    const char *init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"type\":\"request\",\"command\":\"initialize\"}";
    char buf[512];
    snprintf(buf, sizeof(buf), "Content-Length: %d\r\n\r\n%s", (int)strlen(init_req), init_req);
    send(sock, buf, (int)strlen(buf), 0);

    char recv_buf[2048];
    int passed = 0;
    for (int attempt = 0; attempt < 5; attempt++) {
        int r = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);
        if (r <= 0) break;
        recv_buf[r] = '\0';
        if (strstr(recv_buf, "initialize") != NULL || 
            strstr(recv_buf, "stopped") != NULL || 
            strstr(recv_buf, "capabilities") != NULL) {
            passed = 1;
            break;
        }
    }

    const char *disc_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"type\":\"request\",\"command\":\"disconnect\"}";
    snprintf(buf, sizeof(buf), "Content-Length: %d\r\n\r\n%s", (int)strlen(disc_req), disc_req);
    send(sock, buf, (int)strlen(buf), 0);

    closesocket(sock);
    WSACleanup();

    WaitForSingleObject(pi.hProcess, 3000);
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (passed) {
        printf("  DAP initialize & socket protocol: PASS\n");
        printf("All DAP TESTS PASSED SUCCESSFULLY!\n");
        return 0;
    } else {
        printf("FAIL: Invalid or missing DAP response\n");
        return 1;
    }
}

int main(void) {
    if (test_lsp() != 0) return 1;
    if (test_dap() != 0) return 1;
    printf("\nAll Editor Protocol Tests Passed Successfully!\n");
    return 0;
}
