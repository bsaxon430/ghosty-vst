// GHOST FX - Poltergeist 2's SPECTRAL rack (4 bays + WARP) as an effect, for any sound in FL / Ableton.
// It opens the .ghostfx files GHOST's SEND TO FL writes, so a lane's FX come along with its sound.
#include "FxPanel.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace ghost
{
class FxProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    FxProcessor() : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true).withOutput ("Out", juce::AudioChannelSet::stereo(), true))
    { startTimerHz (20); }
    ~FxProcessor() override { stopTimer(); }
    sfx::SpectralFx fx;
    juce::SpinLock lock;
    double sr = 48000.0;
    juce::String loadedName;

    void prepareToPlay (double rate, int) override
    {
        sr = rate;
        { const juce::SpinLock::ScopedLockType g (lock); fx.activate (sr); }
        setLatencySamples ((int) fx.latency());
    }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainInputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        double bpm = 120.0;
        if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto t = pos->getBpm()) bpm = *t;
        const juce::SpinLock::ScopedTryLockType g (lock);
        if (! g.isLocked() || b.getNumChannels() < 2) return;
        fx.process (b.getWritePointer (0), b.getWritePointer (1), (uint32_t) b.getNumSamples(), bpm);
    }
    void changed()
    {
        fx.serviceMainThread();
        if (fx.takeRestartRequest() || fx.latencyWanted() != fx.latency())
        {
            { const juce::SpinLock::ScopedLockType g (lock); fx.activate (sr); }
            setLatencySamples ((int) fx.latency());
        }
    }
    bool loadFile (const juce::File& f)
    {
        const auto text = f.loadFileAsString().toStdString();
        if (text.empty() || ! fx.fromState (text)) return false;
        loadedName = f.getFileNameWithoutExtension();
        { const juce::SpinLock::ScopedLockType g (lock); fx.activate (sr); }
        setLatencySamples ((int) fx.latency());
        updateHostDisplay();
        return true;
    }
    void timerCallback() override
    {
        if (fx.takeMainThreadRequest()) fx.serviceMainThread();
        if (fx.takeRestartRequest()) { { const juce::SpinLock::ScopedLockType g (lock); fx.activate (sr); } setLatencySamples ((int) fx.latency()); }
    }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "GHOST FX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& d) override
    {
        juce::ValueTree t ("GHOSTFX"); t.setProperty ("fx", juce::String (fx.toState()), nullptr); t.setProperty ("name", loadedName, nullptr);
        juce::MemoryOutputStream mo (d, false); t.writeToStream (mo);
    }
    void setStateInformation (const void* data, int size) override
    {
        auto t = juce::ValueTree::readFromData (data, (size_t) size);
        if (! t.hasType ("GHOSTFX")) return;
        loadedName = t["name"].toString();
        if (! fx.fromState (t["fx"].toString().toStdString())) fx.resetToDefaults();
        { const juce::SpinLock::ScopedLockType g (lock); fx.activate (sr); }
        setLatencySamples ((int) fx.latency());
    }
};

class FxEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget
{
public:
    explicit FxEditor (FxProcessor& p) : AudioProcessorEditor (p), proc (p), panel (p.fx, "GHOST FX", [&p] { p.changed(); })
    {
        setLookAndFeel (&lnf);
        addAndMakeVisible (panel);
        addAndMakeVisible (load);
        load.setTooltip ("Open a .ghostfx (GHOST's SEND TO FL writes one per lane in its Sounds folder) - or drop it here");
        load.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Open GHOST FX", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("GhostBlock/GHOST/Exports"), "*.ghostfx");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().existsAsFile()) { proc.loadFile (fc.getResult()); panel.rebuild(); panel.sync(); }
            });
        };
        setSize (1000, 680);
    }
    ~FxEditor() override { setLookAndFeel (nullptr); }
    void resized() override
    {
        panel.setBounds (getLocalBounds());
        load.setBounds (getWidth() - 18 - 80, 12, 80, 28);
    }
    bool isInterestedInFileDrag (const juce::StringArray& f) override { return f.size() == 1 && f[0].endsWithIgnoreCase (".ghostfx"); }
    void filesDropped (const juce::StringArray& f, int, int) override { if (proc.loadFile (juce::File (f[0]))) { panel.rebuild(); panel.sync(); } }
private:
    FxProcessor& proc;
    look::Look lnf;
    FxPanel panel;
    juce::TextButton load { "OPEN" };
    std::unique_ptr<juce::FileChooser> chooser;
};

juce::AudioProcessorEditor* FxProcessor::createEditor() { return new FxEditor (*this); }
} // namespace ghost

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ghost::FxProcessor(); }
