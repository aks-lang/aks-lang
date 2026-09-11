import * as vscode from 'vscode';
import * as path from 'path';
import * as cp from 'child_process';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions
} from 'vscode-languageclient/node';
import { AksDebugAdapterDescriptorFactory } from './debugger';

let client: LanguageClient | undefined;
let outputChannel: vscode.OutputChannel;
let debugAdapterFactory: AksDebugAdapterDescriptorFactory | undefined;

export function activate(context: vscode.ExtensionContext) {
    outputChannel = vscode.window.createOutputChannel('AKS Language Support');
    outputChannel.appendLine('AKS Language Extension activating...');

    // 1. Initialize and Start Language Server Protocol (LSP) Client
    startLanguageClient(context);

    // 2. Register Debug Adapter Protocol (DAP) Factory
    debugAdapterFactory = new AksDebugAdapterDescriptorFactory();
    context.subscriptions.push(
        vscode.debug.registerDebugAdapterDescriptorFactory('aks-dap', debugAdapterFactory)
    );

    // 3. Register Editor Commands
    context.subscriptions.push(
        vscode.commands.registerCommand('aks.runFile', () => runCurrentFile()),
        vscode.commands.registerCommand('aks.formatDocument', () => formatCurrentDocument()),
        vscode.commands.registerCommand('aks.restartServer', async () => {
            outputChannel.appendLine('Restarting AKS Language Server...');
            if (client) {
                await client.stop();
            }
            startLanguageClient(context);
            vscode.window.showInformationMessage('AKS Language Server restarted.');
        })
    );

    outputChannel.appendLine('AKS Language Extension successfully activated.');
}

function startLanguageClient(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('aks');
    const serverExecutable = config.get<string>('languageServerPath') || 'aksls';
    const fallbackExecutable = config.get<string>('executablePath') || 'aks';

    const serverOptions: ServerOptions = {
        run: {
            command: serverExecutable,
            args: []
        },
        debug: {
            command: serverExecutable,
            args: []
        }
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'aks' }],
        outputChannel: outputChannel,
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.aks')
        }
    };

    try {
        client = new LanguageClient(
            'aksLanguageServer',
            'AKS Language Server',
            serverOptions,
            clientOptions
        );

        client.start().catch((err) => {
            outputChannel.appendLine(`Failed to start language server '${serverExecutable}': ${err}`);
            outputChannel.appendLine(`Attempting fallback to '${fallbackExecutable} lsp'...`);
            
            // Try fallback to 'aks lsp'
            const fallbackServerOptions: ServerOptions = {
                command: fallbackExecutable,
                args: ['lsp']
            };
            client = new LanguageClient(
                'aksLanguageServerFallback',
                'AKS Language Server (Fallback)',
                fallbackServerOptions,
                clientOptions
            );
            client.start().catch((fallbackErr) => {
                outputChannel.appendLine(`Fallback also failed: ${fallbackErr}`);
            });
        });
    } catch (e) {
        outputChannel.appendLine(`Error initializing LanguageClient: ${e}`);
    }
}

function runCurrentFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showErrorMessage('No active AKS file to run.');
        return;
    }

    const document = editor.document;
    if (document.languageId !== 'aks') {
        vscode.window.showWarningMessage('The active file is not an AKS source file (.aks).');
        return;
    }

    if (document.isDirty) {
        document.save();
    }

    const config = vscode.workspace.getConfiguration('aks');
    const aksPath = config.get<string>('executablePath') || 'aks';
    const filePath = document.fileName;

    const terminalName = 'AKS Runner';
    let terminal = vscode.window.terminals.find((t) => t.name === terminalName);
    if (!terminal) {
        terminal = vscode.window.createTerminal(terminalName);
    }

    terminal.show();
    terminal.sendText(`${aksPath} run "${filePath}"`);
}

function formatCurrentDocument() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        return;
    }

    const document = editor.document;
    if (document.languageId !== 'aks') {
        return;
    }

    if (document.isDirty) {
        document.save();
    }

    const config = vscode.workspace.getConfiguration('aks');
    const aksPath = config.get<string>('executablePath') || 'aks';
    const filePath = document.fileName;

    cp.exec(`"${aksPath}" fmt "${filePath}"`, (err, stdout, stderr) => {
        if (err) {
            vscode.window.showErrorMessage(`AKS Formatting error: ${stderr || err.message}`);
        } else {
            vscode.window.setStatusBarMessage('AKS Document formatted successfully.', 3000);
        }
    });
}

export function deactivate(): Thenable<void> | undefined {
    if (debugAdapterFactory) {
        debugAdapterFactory.dispose();
    }
    if (!client) {
        return undefined;
    }
    return client.stop();
}
