#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <JuceHeader.h>
#include <cstdio>
#include <cmath>

// Only JUCE host code is linked, never the product processor/editor. AU
// registration is process-local, preserves aufx/SdCh/Musc and does not install.
static int checks = 0, failures = 0;
static void check (bool ok, const char* text)
{ ++checks; std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", text); if (! ok) ++failures; }
static std::unique_ptr<juce::AudioPluginInstance> load (const juce::String& kind, const juce::String& bundle)
{
    juce::String error; juce::PluginDescription description;
    std::unique_ptr<juce::AudioPluginFormat> format;
    if (kind == "AU")
    {
        auto* handle = dlopen ((bundle + "/Contents/MacOS/SideChain").toRawUTF8(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) { std::printf ("dlopen: %s\n", dlerror()); return {}; }
        auto factory = reinterpret_cast<AudioComponentFactoryFunction> (dlsym (handle, "SideChainAUFactory"));
        AudioComponentDescription desc {};
        desc.componentType = 'aufx'; desc.componentSubType = 'SdCh'; desc.componentManufacturer = 'Musc';
        const auto component = factory != nullptr ? AudioComponentRegister (&desc, CFSTR ("Music-Prod: PumpCurve"), 1280, factory) : nullptr;
        check (component != nullptr, "fresh AU factory registered in this process only");
        if (component == nullptr) return {};
        description.fileOrIdentifier = "AudioUnit:Effects/aufx,SdCh,Musc";
        format = std::make_unique<juce::AudioUnitPluginFormat>();
    }
    else
    {
        format = std::make_unique<juce::VST3PluginFormat>();
        juce::OwnedArray<juce::PluginDescription> found; format->findAllTypesForFile (found, bundle);
        if (found.isEmpty()) return {};
        description = *found[0];
    }
    auto instance = format->createInstanceFromDescription (description, 48000, 256, error);
    if (! instance) std::printf ("Load error: %s\n", error.toRawUTF8());
    return instance;
}
static juce::ValueTree extract (const juce::MemoryBlock& native, const juce::String& kind)
{
    juce::MemoryBlock raw;
    if (kind == "AU")
    {
        auto data = CFDataCreate (nullptr, (const UInt8*) native.getData(), (CFIndex) native.getSize());
        auto plist = CFPropertyListCreateWithData (nullptr, data, kCFPropertyListImmutable, nullptr, nullptr); CFRelease (data);
        if (plist == nullptr || CFGetTypeID (plist) != CFDictionaryGetTypeID()) { if (plist) CFRelease (plist); return {}; }
        const auto count = CFDictionaryGetCount ((CFDictionaryRef) plist);
        std::vector<const void*> values ((std::size_t) count);
        CFDictionaryGetKeysAndValues ((CFDictionaryRef) plist, nullptr, values.data());
        for (auto value : values)
            if (CFGetTypeID (value) == CFDataGetTypeID())
            {
                auto tree = juce::ValueTree::readFromData (CFDataGetBytePtr ((CFDataRef) value), (std::size_t) CFDataGetLength ((CFDataRef) value));
                if (tree.hasType ("PARAMS")) { CFRelease (plist); return tree; }
            }
        CFRelease (plist); return {};
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (native.getData(), (int) native.getSize());
    if (! xml || ! xml->getChildByName ("IComponent")) return {};
    raw.fromBase64Encoding (xml->getChildByName ("IComponent")->getAllSubText());
    // JUCE can prefix a VstW/VST2-compatible state header. Locate the PARAMS
    // root and let ValueTree consume the sound state, ignoring wrapper suffix.
    for (std::size_t offset = 0; offset + 7 < raw.getSize(); ++offset)
        if (std::memcmp ((const char*) raw.getData() + offset, "PARAMS\0", 7) == 0)
            return juce::ValueTree::readFromData ((const char*) raw.getData() + offset, raw.getSize() - offset);
    return {};
}
static bool apply (juce::AudioPluginInstance& instance, const juce::ValueTree& sound, const juce::String& kind)
{
    juce::MemoryBlock raw; { juce::MemoryOutputStream stream (raw, false); sound.writeToStream (stream); }
    if (kind == "VST3")
    {
        juce::XmlElement state ("VST3PluginState"); state.createNewChildElement ("IComponent")->addTextElement (raw.toBase64Encoding());
        juce::MemoryBlock native; juce::AudioProcessor::copyXmlToBinary (state, native);
        instance.setStateInformation (native.getData(), (int) native.getSize()); return true;
    }
    juce::MemoryBlock native; instance.getStateInformation (native);
    auto data = CFDataCreate (nullptr, (const UInt8*) native.getData(), (CFIndex) native.getSize());
    auto plist = CFPropertyListCreateWithData (nullptr, data, kCFPropertyListMutableContainers, nullptr, nullptr); CFRelease (data);
    if (plist == nullptr || CFGetTypeID (plist) != CFDictionaryGetTypeID()) { if (plist) CFRelease (plist); return false; }
    const auto count = CFDictionaryGetCount ((CFDictionaryRef) plist);
    std::vector<const void*> keys ((std::size_t) count), values ((std::size_t) count);
    CFDictionaryGetKeysAndValues ((CFDictionaryRef) plist, keys.data(), values.data());
    bool replaced = false;
    for (CFIndex i = 0; i < count; ++i)
        if (CFGetTypeID (values[(std::size_t) i]) == CFDataGetTypeID())
        {
            auto value = (CFDataRef) values[(std::size_t) i];
            if (juce::ValueTree::readFromData (CFDataGetBytePtr (value), (std::size_t) CFDataGetLength (value)).hasType ("PARAMS"))
            {
                auto replacement = CFDataCreate (nullptr, (const UInt8*) raw.getData(), (CFIndex) raw.getSize());
                CFDictionarySetValue ((CFMutableDictionaryRef) plist, keys[(std::size_t) i], replacement); CFRelease (replacement); replaced = true; break;
            }
        }
    auto serialized = CFPropertyListCreateData (nullptr, plist, kCFPropertyListBinaryFormat_v1_0, 0, nullptr); CFRelease (plist);
    if (serialized == nullptr) return false;
    instance.setStateInformation (CFDataGetBytePtr (serialized), (int) CFDataGetLength (serialized)); CFRelease (serialized);
    return replaced;
}
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc != 5) { std::printf ("Usage: probe AU|VST3 bundle save|load preset-file\n"); return 2; }
    const juce::String kind (argv[1]), action (argv[3]); const juce::File file (argv[4]);
    auto instance = load (kind, argv[2]); check (instance != nullptr, "actual release wrapper instantiated");
    if (! instance) return 1;
    juce::PluginDescription desc; instance->fillInPluginDescription (desc);
    std::printf ("Actual product: %s version %s format %s\n", desc.name.toRawUTF8(), desc.version.toRawUTF8(), desc.pluginFormatName.toRawUTF8());
    check (desc.name.contains ("PumpCurve") && desc.version == "0.5.1", "exact wrapper is PumpCurve 0.5.1");
    juce::MemoryBlock native; instance->getStateInformation (native);
    auto initial = extract (native, kind); check (initial.hasType ("PARAMS"), "actual wrapper exposes established PARAMS sound state");
    if (! initial.isValid()) return 1;
    juce::ValueTree expected;
    if (action == "save")
    {
        expected = initial.createCopy();
        for (int i = 0; i < expected.getNumChildren(); ++i)
        {
            auto child = expected.getChild (i); const auto id = child.getProperty ("id").toString();
            if (id == "sidechainAmount") child.setProperty ("value", 82.7, nullptr);
            if (id == "duckLength") child.setProperty ("value", 612.345, nullptr);
            if (id == "release") child.setProperty ("value", 333.123, nullptr);
            if (id == "sidechainOffset") child.setProperty ("value", -17.0, nullptr);
            if (id == "smooth") child.setProperty ("value", 73.4, nullptr);
            if (id == "transportGate") child.setProperty ("value", 0.0, nullptr);
        }
        auto curve = expected.getChildWithName ("PUMPCURVE");
        if (curve.getNumChildren() > 2) curve.getChild (1).setProperty ("y", 0.17, nullptr);
        check (apply (*instance, expected, kind), "native wrapper restores edited complete sound");
        instance->getStateInformation (native); expected = extract (native, kind);
        juce::ValueTree wrapper ("PUMPCURVE_USER_PRESET"); wrapper.setProperty ("formatVersion", 1, nullptr);
        wrapper.setProperty ("name", "Format acceptance", nullptr); wrapper.addChild (expected.createCopy(), -1, nullptr);
        juce::FileOutputStream stream (file); check (! stream.failedToOpen(), "persistent preset fixture writable");
        wrapper.writeToStream (stream); stream.flush(); check (stream.getStatus().wasOk(), "real wrapper sound saved in shared versioned preset format");
    }
    else
    {
        juce::MemoryBlock bytes; file.loadFileAsData (bytes);
        const auto wrapper = juce::ValueTree::readFromData (bytes.getData(), bytes.getSize());
        check (wrapper.hasType ("PUMPCURVE_USER_PRESET") && (int) wrapper.getProperty ("formatVersion") == 1, "new process reads versioned persisted user preset");
        expected = wrapper.getChildWithName ("PARAMS").createCopy();
        check (expected.isValid() && apply (*instance, expected, kind), "native wrapper restores cross-format user preset sound");
    }
    instance->getStateInformation (native); const auto actual = extract (native, kind);
    check (actual.isEquivalentTo (expected), "all sound parameters and exact curve positions survive actual wrapper round-trip");
    instance->prepareToPlay (48000, 256); bool ducks = false; juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 256);
    for (int block = 0; block < 180; ++block)
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) buffer.setSample (c, i, 0.5f);
        instance->processBlock (buffer, midi);
        for (int i = 0; i < 256; ++i) ducks |= buffer.getSample (0, i) > 0 && buffer.getSample (0, i) < 0.45f;
    }
    check (ducks, "restored sound processes real audio with gate OFF while stopped");
    instance->releaseResources(); instance.reset();
    std::printf ("%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
