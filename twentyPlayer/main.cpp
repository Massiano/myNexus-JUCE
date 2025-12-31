#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// EDIT THIS PATH TO YOUR NEXUS VST3
// ============================================================================
#define NEXUS_PATH "C:/Program Files/Common Files/VST3/Nexus.vst3"

// ============================================================================
// Melody definition: {noteNumber, startBeat, durationBeats, velocity}
// ============================================================================
struct Note { int pitch; double startBeat; double duration; float velocity; };

static const Note melody[] = {
    {60, 0.0, 0.5, 0.8f},   // C4
    {64, 0.5, 0.5, 0.8f},   // E4
    {67, 1.0, 0.5, 0.8f},   // G4
    {72, 1.5, 1.0, 0.9f},   // C5
    {67, 2.5, 0.5, 0.7f},   // G4
    {64, 3.0, 0.5, 0.7f},   // E4
    {60, 3.5, 1.5, 0.8f},   // C4
};
static const int melodySize = sizeof(melody) / sizeof(melody[0]);

// ============================================================================
// Audio callback that feeds MIDI to the plugin
// ============================================================================
class PluginHost : public juce::AudioIODeviceCallback
{
public:
    PluginHost(juce::AudioPluginInstance* p, double sr)
        : plugin(p), sampleRate(sr)
    {
        bpm = 120.0;
        samplesPerBeat = (sampleRate * 60.0) / bpm;
        positionInSamples = 0;
    }

    void audioDeviceIOCallbackWithContext(const float* const* /*in*/,
                                          int /*numIn*/,
                                          float* const* out,
                                          int numOut,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        juce::MidiBuffer midiBuffer;

        double blockStartBeat = positionInSamples / samplesPerBeat;
        double blockEndBeat = (positionInSamples + numSamples) / samplesPerBeat;

        // Loop melody every 5 beats
        double loopLength = 5.0;
        double loopStartBeat = std::fmod(blockStartBeat, loopLength);
        double loopEndBeat = std::fmod(blockEndBeat, loopLength);

        // Handle wrap-around
        bool wrapped = loopEndBeat < loopStartBeat;

        for (int i = 0; i < melodySize; ++i)
        {
            const Note& n = melody[i];

            // Note on
            double noteOnBeat = n.startBeat;
            if ((!wrapped && noteOnBeat >= loopStartBeat && noteOnBeat < loopEndBeat) ||
                (wrapped && (noteOnBeat >= loopStartBeat || noteOnBeat < loopEndBeat)))
            {
                double beatOffset = noteOnBeat - loopStartBeat;
                if (beatOffset < 0) beatOffset += loopLength;
                int sampleOffset = static_cast<int>(beatOffset * samplesPerBeat);
                if (sampleOffset >= 0 && sampleOffset < numSamples)
                {
                    midiBuffer.addEvent(juce::MidiMessage::noteOn(1, n.pitch, n.velocity), sampleOffset);
                }
            }

            // Note off
            double noteOffBeat = n.startBeat + n.duration;
            if (noteOffBeat >= loopLength) noteOffBeat -= loopLength;
            if ((!wrapped && noteOffBeat >= loopStartBeat && noteOffBeat < loopEndBeat) ||
                (wrapped && (noteOffBeat >= loopStartBeat || noteOffBeat < loopEndBeat)))
            {
                double beatOffset = noteOffBeat - loopStartBeat;
                if (beatOffset < 0) beatOffset += loopLength;
                int sampleOffset = static_cast<int>(beatOffset * samplesPerBeat);
                if (sampleOffset >= 0 && sampleOffset < numSamples)
                {
                    midiBuffer.addEvent(juce::MidiMessage::noteOff(1, n.pitch), sampleOffset);
                }
            }
        }

        juce::AudioBuffer<float> buffer(numOut, numSamples);
        buffer.clear();

        plugin->processBlock(buffer, midiBuffer);

        for (int ch = 0; ch < numOut; ++ch)
        {
            if (out[ch] != nullptr)
                std::memcpy(out[ch], buffer.getReadPointer(ch), sizeof(float) * numSamples);
        }

        positionInSamples += numSamples;
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        sampleRate = device->getCurrentSampleRate();
        samplesPerBeat = (sampleRate * 60.0) / bpm;
        plugin->prepareToPlay(sampleRate, device->getCurrentBufferSizeSamples());
    }

    void audioDeviceStopped() override
    {
        plugin->releaseResources();
    }

private:
    juce::AudioPluginInstance* plugin;
    double sampleRate;
    double bpm;
    double samplesPerBeat;
    int64_t positionInSamples;
};

// ============================================================================
// Main window showing plugin GUI
// ============================================================================
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow(juce::AudioProcessorEditor* ed)
        : DocumentWindow("Nexus", juce::Colours::black, DocumentWindow::allButtons),
          editor(ed)
    {
        setContentOwned(editor, true);
        setResizable(true, false);
        centreWithSize(editor->getWidth(), editor->getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    juce::AudioProcessorEditor* editor;
};

// ============================================================================
// Application
// ============================================================================
class MyHostApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "MyHost"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        formatManager.addDefaultFormats();

        juce::OwnedArray<juce::PluginDescription> descs;
        juce::VST3PluginFormat vst3;
        vst3.findAllTypesForFile(descs, NEXUS_PATH);

        if (descs.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Error",
                "Could not find Nexus at:\n" NEXUS_PATH);
            quit();
            return;
        }

        juce::String error;
        plugin = formatManager.createPluginInstance(
            *descs[0], 44100.0, 512, error);

        if (!plugin)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Error",
                "Failed to load plugin:\n" + error);
            quit();
            return;
        }

        // Setup audio device
        deviceManager.initialiseWithDefaultDevices(0, 2);
        host = std::make_unique<PluginHost>(plugin.get(), 44100.0);
        deviceManager.addAudioCallback(host.get());

        // Show plugin GUI
        if (auto* editor = plugin->createEditor())
        {
            window = std::make_unique<PluginWindow>(editor);
        }
    }

    void shutdown() override
    {
        deviceManager.removeAudioCallback(host.get());
        window.reset();
        host.reset();
        plugin.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    juce::AudioPluginFormatManager formatManager;
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<PluginHost> host;
    std::unique_ptr<PluginWindow> window;
};

START_JUCE_APPLICATION(MyHostApp)
