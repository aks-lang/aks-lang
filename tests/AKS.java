public class aks {
    static {
        // Load the compiled aks_jni.dll
        System.loadLibrary("aks_jni");
    }

    // Declare the native method matching the signature Java_AKS_run
    public static native int run(String script);

    public static void main(String[] args) {
        System.out.println("Java loaded aks_jni successfully!");
        String script = "fn greet(name) { print(\"Hello from AKS JNI bridge, \" + name + \"!\"); } greet(\"Java\");";
        System.out.println("Running AKS code from Java...");
        int res = run(script);
        System.out.println("AKS execution returned: " + res);
        if (res == 0) {
            System.out.println("PASS: AKS executed successfully from Java!");
        } else {
            System.out.println("FAIL: AKS execution failed!");
            System.exit(1);
        }
    }
}
