// Definitive VST3 bus validation for the built SideChain.vst3 (read-only).
// Loads the plugin binary, obtains IComponent, queries every audio bus:
// name, channel count, bus type (kMain/kAux), default-active flag.
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

using namespace Steinberg;
using namespace Vst;

typedef IPluginFactory* (*GetFactoryProc)();

// The SDK headers declare the interface iids (static const TUID ..._iid);
// the out-of-line FUID definitions must exist exactly once, per funknown.h.
namespace Steinberg { namespace Vst {
const FUID IComponent::iid (IComponent_iid);
}}

int failures = 0;
static void check (bool ok, const char* what)
{
    printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++failures;
}

int main (int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] :
        "/Users/martin/Documents/SideChain/SideChain/Builds/MacOSX/build/Release/SideChain.vst3/Contents/MacOS/SideChain";

    void* handle = dlopen (path, RTLD_NOW | RTLD_LOCAL);
    if (! handle) { printf ("FATAL: dlopen: %s\n", dlerror()); return 1; }

    auto getFactory = (GetFactoryProc) dlsym (handle, "GetPluginFactory");
    if (! getFactory) { printf ("FATAL: GetPluginFactory not found\n"); return 1; }

    IPluginFactory* factory = getFactory();
    if (! factory) { printf ("FATAL: factory is null\n"); return 1; }

    printf ("VST3 factory loaded. Classes:\n");
    PClassInfo ci;
    IComponent* component = nullptr;
    const int32 numClasses = factory->countClasses();
    for (int32 i = 0; i < numClasses; ++i)
    {
        if (factory->getClassInfo (i, &ci) != kResultOk) break;
        char n8[128] = {0};
        for (int k = 0; k < 127 && ci.name[k]; ++k) n8[k] = (char) ci.name[k];
        printf ("  - %s (%s)\n", n8, ci.category);
        if (strcmp (ci.category, "Audio Module Class") == 0 && component == nullptr)
        {
            if (factory->createInstance (ci.cid, IComponent::iid, (void**) &component) != kResultOk)
                component = nullptr;
        }
    }
    check (component != nullptr, "IComponent instantiated");
    if (! component) return 1;

    const int32 numIn  = component->getBusCount (kAudio, kInput);
    const int32 numOut = component->getBusCount (kAudio, kOutput);
    printf ("Audio input buses: %d   Audio output buses: %d\n", (int) numIn, (int) numOut);    // 0.4.0 internal trigger: NO auxiliary input any more - one main stereo
    // input only. (The 0.3.x topology had a second kAux "Sidechain" bus.)
    check (numIn == 1, "VST3 exposes exactly 1 audio input bus (main; aux removed in 0.4.0)");
    check (numOut == 1, "VST3 exposes exactly 1 audio output bus");

    for (int32 dir = 0; dir <= 1; ++dir)
    {
        const int32 n = component->getBusCount (kAudio, dir ? kOutput : kInput);
        for (int32 b = 0; b < n; ++b)
        {
            BusInfo bi {};
            if (component->getBusInfo (kAudio, dir ? kOutput : kInput, b, bi) != kResultOk) continue;
            char name8[128] = {0};
            for (int k = 0; k < 127 && bi.name[k]; ++k) name8[k] = (char) bi.name[k];
            printf ("  %s bus %d: name='%s' channels=%d type=%s defaultActive=%s\n",
                    dir == 0 ? "Input " : "Output", (int) b, name8, (int) bi.channelCount,
                    bi.busType == kAux ? "kAux (sidechain)" : "kMain",
                    (bi.flags & BusInfo::kDefaultActive) ? "yes" : "no");

            if (dir == 0 && b == 0)
                check (bi.busType == kMain && bi.channelCount == 2,
                       "input bus 0 is kMain stereo ('Input')");
            if (dir == 0 && b == 1)
            {
                // 0.4.0: unreachable for the shipped plugin (no aux bus),
                // kept as a guard in case an auxiliary input ever returns.
                check (bi.busType == kAux && bi.channelCount == 2,
                       "input bus 1 would be kAux stereo ('Sidechain')");
                check (! (bi.flags & BusInfo::kDefaultActive),
                       "sidechain bus would NOT be active by default");
            }
            if (dir == 1 && b == 0)
                check (bi.busType == kMain && bi.channelCount == 2,
                       "output bus 0 is kMain stereo ('Output')");
        }
    }

    component->release();
    factory->release();
    dlclose (handle);

    printf ("\n%s (%d failure(s))\n", failures == 0 ? "VST3 BUS CHECK PASSED" : "VST3 BUS CHECK FAILED", failures);
    return failures == 0 ? 0 : 1;
}
