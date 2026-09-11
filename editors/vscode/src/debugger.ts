import * as vscode from 'vscode';
import * as cp from 'child_process';
import * as net from 'net';

export class AksDebugAdapterDescriptorFactory implements vscode.DebugAdapterDescriptorFactory {
    private dapProcess?: cp.ChildProcess;

    async createDebugAdapterDescriptor(session: vscode.DebugSession): Promise<vscode.DebugAdapterDescriptor> {
        const config = vscode.workspace.getConfiguration('aks');
        const aksPath = config.get<string>('executablePath') || 'aks';
        const program = session.configuration.program;

        if (!program) {
            throw new Error("No AKS program specified for debugging ('program' configuration property missing).");
        }

        // Allocate a free TCP port
        const port = await this.getFreePort(4711);

        // Spawn 'aks dap <file> <port>'
        const args = ['dap', program, String(port)];
        this.dapProcess = cp.spawn(aksPath, args, {
            cwd: vscode.workspace.workspaceFolders?.[0]?.uri.fsPath || undefined,
            shell: false
        });

        this.dapProcess.stderr?.on('data', (data) => {
            console.error(`[AKS DAP] ${data}`);
        });

        // Wait a short moment for the DAP server to bind the socket
        await new Promise((resolve) => setTimeout(resolve, 500));

        return new vscode.DebugAdapterServer(port);
    }

    dispose() {
        if (this.dapProcess && !this.dapProcess.killed) {
            this.dapProcess.kill();
        }
    }

    private getFreePort(defaultPort: number): Promise<number> {
        return new Promise((resolve) => {
            const srv = net.createServer();
            srv.listen(defaultPort, '127.0.0.1', () => {
                const address = srv.address() as net.AddressInfo;
                const port = address.port;
                srv.close(() => resolve(port));
            });
            srv.on('error', () => {
                const altSrv = net.createServer();
                altSrv.listen(0, '127.0.0.1', () => {
                    const address = altSrv.address() as net.AddressInfo;
                    const port = address.port;
                    altSrv.close(() => resolve(port));
                });
            });
        });
    }
}
