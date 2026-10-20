#pragma once
#include <JuceHeader.h>

namespace sid::presets
{
// Control/message-thread API only. Both AU and VST3 use the same directory;
// the audio callback never constructs or accesses this library.
class UserPresetLibrary
{
public:
    struct Entry { juce::String name; juce::File file; };
    explicit UserPresetLibrary (juce::File directory = defaultDirectory()) : root (directory) {}

    static juce::File defaultDirectory()
    {
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
#if JUCE_MAC
        base = base.getChildFile ("Application Support");
#endif
        return base.getChildFile ("Music-Prod/PumpCurve/User Presets");
    }

    static juce::Result validateName (const juce::String& name)
    {
        if (name.isEmpty() || name.length() > 80 || name != name.trim()
            || name == "." || name == ".." || name.endsWithChar ('.'))
            return juce::Result::fail ("Use a name of 1–80 characters without leading/trailing spaces or a trailing dot.");
        for (auto c : name)
            if (c < 32 || c == 127 || juce::String ("/\\:*?\"<>|").containsChar (c))
                return juce::Result::fail ("The preset name contains a reserved filename character.");
        return juce::Result::ok();
    }

    static juce::String foldedName (const juce::String& name);

    juce::Array<Entry> list() const
    {
        juce::Array<Entry> entries;
        for (const auto& file : root.findChildFiles (juce::File::findFiles, false, "*.pumpcurvepreset"))
        {
            juce::String name;
            juce::ValueTree state;
            if (read (file, name, state).wasOk())
                entries.add ({ name, file }); // corrupt files never enter the menu
        }
        std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
        { return a.name.compareIgnoreCase (b.name) < 0; });
        return entries;
    }

    juce::Result read (const juce::File& file, juce::String& name, juce::ValueTree& state) const
    {
        if (file.getParentDirectory() != root || file.isSymbolicLink()
            || ! file.existsAsFile() || file.getSize() <= 0 || file.getSize() > 1024 * 1024)
            return juce::Result::fail ("The preset file is missing, outside the library, or too large.");
        juce::MemoryBlock bytes;
        if (! file.loadFileAsData (bytes))
            return juce::Result::fail ("Could not read the preset file.");
        const auto wrapper = juce::ValueTree::readFromData (bytes.getData(), bytes.getSize());
        if (! wrapper.hasType ("PUMPCURVE_USER_PRESET")
            || ! wrapper.getProperty ("formatVersion").isInt()
            || (int) wrapper.getProperty ("formatVersion") != 1
            || wrapper.getNumChildren() != 1 || ! wrapper.getChild (0).hasType ("PARAMS"))
            return juce::Result::fail ("The preset is corrupt or uses an unsupported file format.");
        name = wrapper.getProperty ("name").toString();
        auto result = validateName (name);
        if (result.failed()) return result;
        state = wrapper.getChild (0).createCopy();
        return juce::Result::ok();
    }

    juce::Result save (const juce::String& name, const juce::ValueTree& state, juce::File& savedFile) const
    {
        auto valid = validateName (name);
        if (valid.failed()) return valid;
        if (! state.hasType ("PARAMS")) return juce::Result::fail ("No sound state to save.");
        if (auto result = root.createDirectory(); result.failed()) return result;
        juce::InterProcessLock lock ("PumpCurve-UserPresets-" + juce::String::toHexString (root.getFullPathName().hashCode64()));
        if (! lock.enter (100)) return juce::Result::fail ("Another instance is saving a preset. Please try again.");
        struct Unlock { juce::InterProcessLock& lock; ~Unlock() { lock.exit(); } } unlock { lock };
        for (const auto& entry : list())
            if (foldedName (entry.name) == foldedName (name))
                return juce::Result::fail ("A preset with that name already exists. Choose a different name.");
        // Hashing the case-folded display name avoids path traversal, reserved
        // Windows basenames and case-insensitive filesystem collisions.
        const auto target = root.getChildFile (juce::String::toHexString (foldedName (name).hashCode64()) + ".pumpcurvepreset");
        if (target.exists()) return juce::Result::fail ("A preset file with that name already exists. Nothing was overwritten.");
        juce::ValueTree wrapper ("PUMPCURVE_USER_PRESET");
        wrapper.setProperty ("formatVersion", 1, nullptr);
        wrapper.setProperty ("name", name, nullptr);
        wrapper.addChild (state.createCopy(), -1, nullptr);
        juce::TemporaryFile temporary (target);
        {
            juce::FileOutputStream stream (temporary.getFile());
            if (stream.failedToOpen()) return juce::Result::fail ("The preset directory is not writable.");
            wrapper.writeToStream (stream);
            stream.flush();
            if (stream.getStatus().failed()) return stream.getStatus();
        }
        if (! temporary.overwriteTargetFileWithTemporary())
            return juce::Result::fail ("Could not commit the preset file. The sound has not been changed.");
        savedFile = target;
        return juce::Result::ok();
    }
    const juce::File& directory() const { return root; }
private:
    juce::File root;
};
}
