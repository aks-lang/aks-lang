const path = require('path');
const addonPath = path.resolve(__dirname, '../aks_node.node');
console.log('Loading addon from:', addonPath);
const addon = require(addonPath);

console.log('Exported functions:', Object.keys(addon));

const sourceCode = `
fn greet(name) {
    print("Hello from AKS Node.js native addon, " + name + "!");
}
greet("JS Interop");
`;

console.log('Running AKS code from Node...');
const res = addon.runAKS(sourceCode);
console.log('AKS run returned:', res);
if (res === 0) {
    console.log('PASS: AKS executed successfully from Node.js!');
} else {
    console.log('FAIL: AKS execution failed!');
    process.exit(1);
}
