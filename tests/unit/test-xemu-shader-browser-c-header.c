#include "../../ui/xui/shader-browser-session-provider.hh"

int main(void)
{
    XemuShaderBrowserShaderRecord record = {0};
    XemuShaderBrowserObservation observation = {0};
    XemuShaderBrowserPerformanceSession session = {0};
    XemuShaderBrowserExternalArtifact artifact = {0};
    (void)record;
    (void)observation;
    (void)session;
    (void)artifact;
    return XEMU_SHADER_BROWSER_HASH_BYTES == 12 ? 0 : 1;
}
