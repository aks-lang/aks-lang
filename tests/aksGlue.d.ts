// Automatically generated TypeScript declarations by aks wasm-bind

export class AKSRuntime {
    constructor(wasmInstance: any);
    runSource(source: string): number;
}

export function instantiate(wasmModuleOrBuffer: any, imports?: {
    print?: (msg: string) => void;
}): Promise<AKSRuntime>;
