#pragma once
// Ghost Graph -- DRAG: the song split into its parts (Kick, Snare, Hi-hat, Bass, Lead ...), each one a WAV and a MIDI
// you drag straight into the DAW, plus the whole song (the mix as a WAV, every part in one MIDI) and all the parts at
// once. The files are kept in Music / Ghost Graph / Exports, one folder each time the page opens, so the DAW can find
// them later. MIDI is written at once; the WAVs render on another thread (each handle lights up when its file is done).
//
// Included by GraphEditor.h after ggui (the looks and the fonts).

#include <juce_audio_formats/juce_audio_formats.h>
#include "GraphProcessor.h"
#include "GraphStems.h"

class DragChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    juce::String text;
    juce::StringArray files;
    bool ready = false;
    float progress = 0.f;   // while a WAV renders

    DragChip() { setRepaintsOnMouseActivity (true); }
    void setReady (bool r) { ready = r; setMouseCursor (r ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor); repaint(); }

    void paint (juce::Graphics& g) override
    {
        using namespace ggui;
        const auto r = getLocalBounds().toFloat().reduced (1.5f);
        const bool lcd = th().lcd;
        const juce::Colour ink = th().amber;
        const float a = ready ? 1.0f : 0.3f;   // not ready yet: greyed out
        if (ready && isMouseOver()) { g.setColour (ink.withAlpha (0.16f)); g.fillRoundedRectangle (r, lcd ? 2.f : 5.f); }
        if (! ready && progress > 0.f)
        {
            g.setColour (ink.withAlpha (0.14f));
            g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jlimit (0.f, 1.f, progress)), lcd ? 2.f : 5.f);
        }
        g.setColour (ink.withAlpha (a));
        g.drawRoundedRectangle (r, lcd ? 2.f : 5.f, lcd ? 2.f : 1.2f);
        // a little grip: three dots, so it reads as something to drag
        const float gx = r.getX() + 9.f, gy = r.getCentreY();
        for (int i = -1; i <= 1; ++i) g.fillEllipse (gx - 1.5f, gy + i * 4.5f - 1.5f, 3.f, 3.f);
        g.setFont (serif (14.f));
        g.drawText (ready ? text : (progress > 0.f ? text : juce::String::fromUTF8 ("\xe2\x80\xa6")), r.withTrimmedLeft (14.f), juce::Justification::centred);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! ready || dragging || e.getDistanceFromDragStart() < 4 || files.isEmpty()) return;
        dragging = true;
        juce::Component::SafePointer<DragChip> safe (this);
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this, [safe] { if (safe != nullptr) safe->dragging = false; });
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (ready && ! dragging && e.getDistanceFromDragStart() < 4 && ! files.isEmpty())
            juce::File (files[0]).revealToUser();   // a click shows the file in Finder
        dragging = false;
    }
private:
    bool dragging = false;
};

class ExportPanel : public juce::Component, private juce::Timer
{
public:
    explicit ExportPanel (GraphProcessor& p) : proc (p)
    {
        rowsHolder.owner = this;
        setWantsKeyboardFocus (true);
        doneBtn.setButtonText ("DONE");
        doneBtn.onClick = [this] { close(); };
        addAndMakeVisible (doneBtn);
        folderBtn.setButtonText ("SHOW FILES");
        folderBtn.setTooltip ("Open the folder these files are saved in");
        folderBtn.onClick = [this] { if (folder.isDirectory()) folder.revealToUser(); };
        addAndMakeVisible (folderBtn);
        list.setViewedComponent (&rowsHolder, false);
        list.setScrollBarsShown (true, false);
        addAndMakeVisible (list);
        for (auto* c : { &mixWav, &mixMid, &allWav, &allMid }) addAndMakeVisible (c);
        mixWav.text = "WAV";  mixWav.setTooltip ("The whole song as you hear it: drag it into your DAW (click: show the file)");
        mixMid.text = "MIDI"; mixMid.setTooltip ("Every part in one MIDI file, a track each (drums on channel 10)");
        allWav.text = "WAVS"; allWav.setTooltip ("Every part's WAV at once: drop them in and each part gets its own track");
        allMid.text = "MIDIS"; allMid.setTooltip ("Every part's MIDI at once");
    }
    ~ExportPanel() override { stopRender(); }

    std::function<void()> onClose;
    const ggui::Theme* theme = &ggui::theme (0);

    // ---- open: look at the song as it is now, write the MIDI, start rendering the WAVs
    void open()
    {
        stopRender();
        rows.clear();
        rowsHolder.removeAllChildren();
        const int n = proc.getNumScenes();
        scenes.clear();
        for (int i = 0; i < n; ++i) scenes.push_back (proc.getScene (i));
        auto par = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
        const float r = par ("rate");
        plan = gg::planSong (scenes, par ("chain") > 0.5f, (int) par ("scene") - 1, r < 0.5f ? 0.5 : (r < 1.5f ? 1.0 : 2.0), (int) std::lround (par ("tune")), par ("kit") > 0.5f);
        bpm = juce::jlimit (20.0, 400.0, proc.displayBpm.load());
        stems = gg::findStems (scenes, plan);
        tail = par ("space") > 0.001f ? 2.5 : 0.25;

        auto root = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Ghost Graph").getChildFile ("Exports");
        if (auto* env = std::getenv ("GG_EXPORT_DIR")) root = juce::File (juce::String (env));   // the checks write somewhere else
        folder = root.getChildFile ("Ghost Graph " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S"));
        folder.createDirectory();

        // MIDI, right away
        std::vector<std::pair<const gg::Stem*, std::vector<gg::StemNote>>> all;
        for (auto& s : stems) all.push_back ({ &s, gg::stemNotes (scenes, plan, s) });
        for (size_t i = 0; i < stems.size(); ++i)
        {
            auto row = std::make_unique<Row>();
            row->name = juce::String::fromUTF8 (stems[i].name.c_str());
            row->detail = describe (stems[i], all[i].second);
            const juce::String file = fileName (stems[i].name);
            row->wavFile = folder.getChildFile (file + ".wav");
            row->midFile = folder.getChildFile (file + ".mid");
            writeMidi (row->midFile, { all[i] });
            row->wav.text = "WAV";  row->wav.files.add (row->wavFile.getFullPathName());
            row->mid.text = "MIDI"; row->mid.files.add (row->midFile.getFullPathName());
            row->wav.setTooltip ("Just the " + row->name + ", as a WAV: drag it into your DAW (click: show the file)");
            row->mid.setTooltip ("Just the " + row->name + ", as MIDI: drag it onto an instrument (click: show the file)");
            row->mid.setReady (row->midFile.existsAsFile());
            rowsHolder.addAndMakeVisible (row->wav);
            rowsHolder.addAndMakeVisible (row->mid);
            rows.push_back (std::move (row));
        }
        mixWavFile = folder.getChildFile ("Whole song.wav");
        mixMidFile = folder.getChildFile ("Whole song.mid");
        writeMidi (mixMidFile, all);
        mixWav.files = { mixWavFile.getFullPathName() };
        mixMid.files = { mixMidFile.getFullPathName() };
        mixMid.setReady (mixMidFile.existsAsFile());
        allWav.files.clear(); allMid.files.clear();
        for (auto& r2 : rows) { allWav.files.add (r2->wavFile.getFullPathName()); allMid.files.add (r2->midFile.getFullPathName()); }
        allMid.setReady (! rows.empty());
        for (auto* c : { &mixWav, &allWav }) { c->setReady (false); c->progress = 0.f; }

        // WAVs, on another thread: the whole song first, then each part
        renderCopy = proc.makeRenderCopy();
        jobs.clear();
        jobs.push_back ({ {}, mixWavFile });
        for (size_t i = 0; i < stems.size(); ++i) jobs.push_back ({ gg::stemMask (stems[i], (int) scenes.size()), rows[i]->wavFile });
        done.clear();
        for (size_t j = 0; j < jobs.size(); ++j) done.push_back (std::make_unique<std::atomic<int>> (0));
        cancel = false;
        current = 0; currentProgress = 0.f;
        worker = std::make_unique<Worker> (*this);
        worker->startThread();
        startTimerHz (15);
        resized();
        repaint();
    }

    void close()
    {
        stopRender();
        setVisible (false);
        if (onClose) onClose();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { close(); return true; }
        return false;
    }

    // tests: wait for every WAV (true when all are written)
    bool waitForRenders (int ms)
    {
        const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        while (juce::Time::getMillisecondCounter() < until)
        {
            bool all = ! done.empty();
            for (auto& d : done) all = all && d->load() != 0;
            if (all) { timerCallback(); return true; }
            juce::Thread::sleep (20);
        }
        return false;
    }
    const std::vector<gg::Stem>& getStems() const { return stems; }
    juce::File getFolder() const { return folder; }
    double getBpm() const { return bpm; }

    void paint (juce::Graphics& g) override
    {
        using namespace ggui;
        curTheme() = theme;
        g.fillAll (th().paper);
        g.setColour (th().amber);
        g.setFont (serif (30.f, true));
        g.drawText ("Drag out", titleArea, juce::Justification::centredLeft);
        g.setFont (serif (16.f, true));
        g.setColour (th().amber.withAlpha (0.75f));
        const int secs = (int) std::lround (plan.beats * 60.0 / bpm);
        const juce::String info = juce::String (juce::roundToInt (bpm)) + " BPM" + dot() + juce::String ((int) plan.scenes.size())
                                + (plan.scenes.size() == 1 ? " scene" : " scenes") + dot() + juce::String (plan.beats, 0) + " beats ("
                                + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2) + ")" + dot()
                                + juce::String ((int) stems.size()) + ((int) stems.size() == 1 ? " part" : " parts");
        g.drawText (info, infoArea, juce::Justification::centredLeft);
        g.setColour (th().amber.withAlpha (0.55f));
        g.drawText ("Drag a WAV or a MIDI into your DAW. Click one to see the file.", hintArea, juce::Justification::centredLeft);

        // the column heads and the bottom rows
        g.setColour (th().amber.withAlpha (0.25f));
        g.fillRect (list.getX(), list.getY() - 1, list.getWidth(), 1);
        g.fillRect (list.getX(), list.getBottom() + 4, list.getWidth(), 1);
        g.setColour (th().amber);
        g.setFont (serif (19.f));
        g.drawText ("Whole song", wholeArea, juce::Justification::centredLeft);
        g.drawText ("All parts", allArea, juce::Justification::centredLeft);
        g.setFont (serif (13.f, true));
        g.setColour (th().amber.withAlpha (0.6f));
        g.drawText ("the mix, as you hear it (with TONE, SPACE, VOLUME)", wholeArea.withTrimmedLeft (130), juce::Justification::centredLeft);
        g.drawText ("every part at once, a track each", allArea.withTrimmedLeft (130), juce::Justification::centredLeft);
        if (stems.empty())
        {
            g.setFont (serif (17.f, true));
            g.drawText ("Nothing plays in this song yet.", list.getBounds(), juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (juce::jmax (24, getWidth() / 22), juce::jmax (18, getHeight() / 30));
        auto top = r.removeFromTop (44);
        doneBtn.setBounds (top.removeFromRight (84).withSizeKeepingCentre (84, 28));
        top.removeFromRight (8);
        folderBtn.setBounds (top.removeFromRight (116).withSizeKeepingCentre (116, 28));
        titleArea = top;
        infoArea = r.removeFromTop (24);
        hintArea = r.removeFromTop (22);
        r.removeFromTop (10);
        auto bottom = r.removeFromBottom (88);
        list.setBounds (r);
        bottom.removeFromTop (12);
        wholeArea = bottom.removeFromTop (38);
        allArea = bottom.removeFromTop (38);
        auto chips = [] (juce::Rectangle<int> a, DragChip& w, DragChip& m)
        {
            auto c = a.removeFromRight (196);
            w.setBounds (c.removeFromLeft (94).reduced (2, 4));
            c.removeFromLeft (8);
            m.setBounds (c.removeFromLeft (94).reduced (2, 4));
        };
        chips (wholeArea, mixWav, mixMid);
        chips (allArea, allWav, allMid);
        const int rowH = 40;
        rowsHolder.setSize (list.getWidth() - list.getScrollBarThickness(), juce::jmax (list.getHeight(), (int) rows.size() * rowH));
        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto rr = juce::Rectangle<int> (0, (int) i * rowH, rowsHolder.getWidth(), rowH);
            rows[i]->area = rr;
            auto c = rr.removeFromRight (196);
            rows[i]->wav.setBounds (c.removeFromLeft (94).reduced (2, 5));
            c.removeFromLeft (8);
            rows[i]->mid.setBounds (c.removeFromLeft (94).reduced (2, 5));
        }
        rowsHolder.repaint();
    }

private:
    GraphProcessor& proc;
    struct Row { juce::String name, detail; juce::File wavFile, midFile; DragChip wav, mid; juce::Rectangle<int> area; };

    // the list of parts: name and what it is on the left, its WAV and MIDI handles on the right
    struct RowsHolder : public juce::Component
    {
        ExportPanel* owner = nullptr;
        void paint (juce::Graphics& g) override
        {
            using namespace ggui;
            for (size_t i = 0; i < owner->rows.size(); ++i)
            {
                const auto& row = *owner->rows[i];
                auto a = row.area.withTrimmedRight (210);
                if (i % 2 == 1) { g.setColour (th().amber.withAlpha (0.05f)); g.fillRect (row.area); }
                g.setColour (th().amber);
                g.setFont (serif (19.f));
                g.drawText (row.name, a.removeFromLeft (juce::jmin (220, a.getWidth() / 2)).withTrimmedLeft (8), juce::Justification::centredLeft);
                g.setColour (th().amber.withAlpha (0.6f));
                g.setFont (serif (14.f, true));
                g.drawText (row.detail, a, juce::Justification::centredLeft);
            }
        }
    } rowsHolder;
    juce::Viewport list;
    juce::TextButton doneBtn, folderBtn;
    DragChip mixWav, mixMid, allWav, allMid;
    juce::Rectangle<int> titleArea, infoArea, hintArea, wholeArea, allArea;

    std::vector<gg::Scene> scenes;
    gg::SongPlan plan;
    std::vector<gg::Stem> stems;
    std::vector<std::unique_ptr<Row>> rows;
    juce::File folder, mixWavFile, mixMidFile;
    double bpm = 120, tail = 0.25;

    // ---- the WAV renders
    struct Job { std::vector<uint16_t> mask; juce::File file; };
    std::vector<Job> jobs;
    std::vector<std::unique_ptr<std::atomic<int>>> done;
    std::atomic<bool> cancel { false };
    std::atomic<int> current { 0 };
    std::atomic<float> currentProgress { 0.f };
    std::unique_ptr<GraphProcessor> renderCopy;

    struct Worker : public juce::Thread
    {
        explicit Worker (ExportPanel& o) : juce::Thread ("Ghost Graph DRAG"), owner (o) {}
        void run() override
        {
            const double sr = 48000.0;
            const double secs = owner.plan.beats * 60.0 / owner.bpm + owner.tail;
            for (size_t j = 0; j < owner.jobs.size() && ! threadShouldExit() && ! owner.cancel; ++j)
            {
                owner.current = (int) j;
                juce::AudioBuffer<float> buf;
                owner.renderCopy->renderOffline (buf, owner.bpm, secs, sr, owner.jobs[j].mask, &owner.cancel);
                if (owner.cancel) return;
                auto& f = owner.jobs[j].file;
                f.deleteFile();
                if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
                {
                    juce::WavAudioFormat wav;
                    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0)))
                    {
                        os.release();   // the writer owns it now
                        w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
                    }
                }
                *owner.done[j] = 1;
            }
        }
        ExportPanel& owner;
    };
    std::unique_ptr<Worker> worker;

    void stopRender()
    {
        stopTimer();
        cancel = true;
        if (worker != nullptr) { worker->stopThread (4000); worker.reset(); }
        renderCopy.reset();
    }

    void timerCallback() override
    {
        bool all = true;
        for (size_t j = 0; j < done.size(); ++j)
        {
            const bool ok = done[j]->load() != 0 && jobs[j].file.existsAsFile();
            all = all && done[j]->load() != 0;
            DragChip* c = j == 0 ? &mixWav : &rows[j - 1]->wav;
            if (ok != c->ready) c->setReady (ok);
            c->progress = (int) j == current.load() && ! ok ? 0.5f : 0.f;
            c->repaint();
        }
        if (all != allWav.ready) allWav.setReady (all);
        if (all) stopTimer();
    }

    static juce::String fileName (const std::string& name)
    {
        juce::String s = juce::String::fromUTF8 (name.c_str()).replace (juce::String::fromUTF8 (" \xc2\xb7 "), " - ");
        return juce::File::createLegalFileName (s);
    }

    static juce::String scenesText (const std::vector<int>& ks)
    {
        juce::String out;
        for (size_t i = 0; i < ks.size();)
        {
            size_t j = i;
            while (j + 1 < ks.size() && ks[j + 1] == ks[j] + 1) ++j;
            if (out.isNotEmpty()) out << ", ";
            out << juce::String (ks[i] + 1);
            if (j > i) out << juce::String::fromUTF8 ("\xe2\x80\x93") << juce::String (ks[j] + 1);
            i = j + 1;
        }
        return out;
    }
    juce::String describe (const gg::Stem& s, const std::vector<gg::StemNote>& notes) const
    {
        std::vector<int> ks;
        for (auto& p : s.parts) if (std::find (ks.begin(), ks.end(), p.scene) == ks.end()) ks.push_back (p.scene);
        std::sort (ks.begin(), ks.end());
        juce::String d = (ks.size() == 1 ? "scene " : "scenes ") + scenesText (ks);
        d << dot() << juce::String ((int) notes.size()) << (s.drum ? (notes.size() == 1 ? " hit" : " hits") : (notes.size() == 1 ? " note" : " notes"));
        if (s.drum) d << dot() << "MIDI note " << s.gm;
        else if (! notes.empty())
        {
            int lo = 127, hi = 0;
            for (auto& n : notes) { lo = std::min (lo, n.note); hi = std::max (hi, n.note); }
            d << dot() << juce::MidiMessage::getMidiNoteName (lo, true, true, 3)
              << juce::String::fromUTF8 (" \xe2\x80\x93 ") << juce::MidiMessage::getMidiNoteName (hi, true, true, 3);
        }
        return d;
    }

    void writeMidi (const juce::File& f, const std::vector<std::pair<const gg::Stem*, std::vector<gg::StemNote>>>& parts) const
    {
        juce::MidiFile mf;
        const int tpq = 960;
        mf.setTicksPerQuarterNote (tpq);
        juce::MidiMessageSequence meta;
        meta.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)), 0);
        meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
        meta.addEvent (juce::MidiMessage::textMetaEvent (3, "Ghost Graph"), 0);
        mf.addTrack (meta);
        for (auto& pr : parts)
        {
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::textMetaEvent (3, juce::String::fromUTF8 (pr.first->name.c_str())), 0);
            const int ch = pr.first->drum ? 10 : 1;
            for (auto& n : pr.second)
            {
                const double on = n.start * tpq, off = (n.start + n.length) * tpq;
                seq.addEvent (juce::MidiMessage::noteOn (ch, n.note, (juce::uint8) n.velocity), on);
                seq.addEvent (juce::MidiMessage::noteOff (ch, n.note), juce::jmax (on + 1.0, off - 1.0));
            }
            seq.sort();
            seq.updateMatchedPairs();
            mf.addTrack (seq);
        }
        f.deleteFile();
        juce::FileOutputStream os (f);
        if (os.openedOk()) mf.writeTo (os, 1);
    }

    static juce::String dot() { return juce::String::fromUTF8 ("   \xc2\xb7   "); }
};

// ---- the tempo, on the main page: what it plays at, and whether it follows the DAW
// Drag it up or down (or scroll) to change it, double-click to type it. With SYNC on in a DAW it shows the DAW's tempo
// and can't be changed here (greyed out); the standalone app has no DAW to follow, so its SYNC is greyed out.
class TempoBox : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    const ggui::Theme* theme = &ggui::theme (0);
    explicit TempoBox (GraphProcessor& p) : proc (p)
    {
        syncBtn.setButtonText ("SYNC");
        syncBtn.setClickingTogglesState (true);
        addAndMakeVisible (syncBtn);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "sync", syncBtn);
        syncBtn.setEnabled (! proc.isStandalone());
        syncBtn.setTooltip (proc.isStandalone() ? "SYNC follows a DAW's tempo and play button (there is no DAW in the standalone app)"
                                                : "Follow the DAW: its tempo, and play when it plays (off: set your own tempo here)");
        typed.setJustification (juce::Justification::centred);
        typed.onReturnKey = [this] { commitTyped(); };
        typed.onFocusLost = [this] { commitTyped(); };
        typed.onEscapeKey = [this] { typed.setVisible (false); };
        addChildComponent (typed);
        startTimerHz (10);
    }

    bool following() const { return proc.apvts.getRawParameterValue ("sync")->load() > 0.5f && ! proc.isStandalone(); }
    double shown() const
    {
        if (following() && proc.displaySeenHost.load()) return proc.displayBpm.load();
        return proc.apvts.getRawParameterValue ("tempo")->load();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace ggui;
        curTheme() = theme;
        const bool lcd = th().lcd;
        const bool fixed = following();
        auto r = numArea.toFloat();
        g.setColour (th().amber.withAlpha (fixed ? 0.45f : 1.0f));   // following the DAW: nothing to change here
        const double b = shown();
        const juce::String num = std::abs (b - std::round (b)) < 0.05 ? juce::String (juce::roundToInt (b)) : juce::String (b, 1);
        const float h = r.getHeight();
        g.setFont (serif (h * (lcd ? 0.66f : 0.78f), true));
        const juce::String small = fixed ? "DAW BPM" : "BPM";
        juce::GlyphArrangement ga; ga.addLineOfText (g.getCurrentFont(), num, 0, 0);
        const float nw = ga.getBoundingBox (0, -1, true).getWidth();
        const juce::Font sf = serif (h * 0.38f);
        juce::GlyphArrangement gb; gb.addLineOfText (sf, small, 0, 0);
        const float sw = gb.getBoundingBox (0, -1, true).getWidth();
        const float x0 = r.getRight() - nw - sw - 6.f;
        if (! typed.isVisible()) g.drawText (num, juce::Rectangle<float> (x0, r.getY(), nw + 2.f, h), juce::Justification::centredLeft);
        g.setFont (sf);
        g.drawText (small, juce::Rectangle<float> (r.getRight() - sw - 1.f, r.getY(), sw + 2.f, h * 0.78f), juce::Justification::bottomLeft);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        syncBtn.setBounds (r.removeFromRight (juce::jmin (58, r.getWidth() / 3)).withSizeKeepingCentre (juce::jmin (58, r.getWidth() / 3), juce::jmin (24, r.getHeight())));
        r.removeFromRight (8);
        numArea = r;
        typed.setBounds (r.withTrimmedLeft (r.getWidth() / 3).withSizeKeepingCentre (r.getWidth() * 2 / 3, juce::jmin (r.getHeight(), 30)));
    }
    void mouseDown (const juce::MouseEvent&) override { startTempo = proc.apvts.getRawParameterValue ("tempo")->load(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (following() || ! numArea.contains (e.getMouseDownPosition())) return;
        const float step = e.mods.isAltDown() ? 0.1f : 0.25f;   // pixels -> BPM
        set (startTempo - e.getDistanceFromDragStartY() * step);
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (following()) return;
        set (proc.apvts.getRawParameterValue ("tempo")->load() + (w.deltaY > 0 ? 1.f : -1.f));
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (following() || ! numArea.contains (e.getPosition())) return;
        typed.setText (juce::String (juce::roundToInt (proc.apvts.getRawParameterValue ("tempo")->load())), false);
        typed.setVisible (true);
        typed.grabKeyboardFocus();
        typed.selectAll();
        repaint();
    }
    void applyLookColours()
    {
        typed.setColour (juce::TextEditor::textColourId, ggui::th().amber);
        typed.setColour (juce::TextEditor::backgroundColourId, ggui::th().paper);
        typed.setColour (juce::TextEditor::outlineColourId, ggui::th().amber.withAlpha (0.6f));
        typed.applyColourToAllText (ggui::th().amber);
    }

private:
    GraphProcessor& proc;
    juce::TextButton syncBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    juce::TextEditor typed;
    juce::Rectangle<int> numArea;
    float startTempo = 120.f;
    double lastShown = -1; bool lastFollow = false;

    void set (float bpm)
    {
        if (auto* p = proc.apvts.getParameter ("tempo")) p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (40.f, 220.f, bpm)));
        repaint();
    }
    void commitTyped()
    {
        if (! typed.isVisible()) return;
        const double v = typed.getText().retainCharacters ("0123456789.").getDoubleValue();
        typed.setVisible (false);
        if (v > 0) set ((float) v);
        repaint();
    }
    void timerCallback() override
    {
        const double s = shown();
        const bool f = following();
        setMouseCursor (f ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
        setTooltip (f ? "The DAW's tempo (SYNC is on). Turn SYNC off to set your own." : "The tempo: drag up or down, scroll, or double-click to type it (40 to 220)");
        if (std::abs (s - lastShown) > 0.01 || f != lastFollow) { lastShown = s; lastFollow = f; repaint(); }
    }
};

// ---- the key, on the main page: which note is home and which scale. Click it to choose: the root moves the song (it
// changes TUNE), the scale moves its notes step for step (minor -> major raises the 3rd, 6th and 7th). "New songs in
// this key" makes GENERATE and EXPERIMENT write in it. With no tune to listen to (only drums) it shows "no key" and
// only "Listen again" does anything (the rest is greyed out).
class KeyBox : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    const ggui::Theme* theme = &ggui::theme (0);
    std::function<void()> onChanged;   // after a choice (the editor repaints, the graph stays as written)

    explicit KeyBox (GraphProcessor& p) : proc (p)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("The key: click to choose the home note and the scale (and whether new songs use it)");
        startTimerHz (6);
    }

    juce::String text() const
    {
        const int r = proc.shownRoot(), s = proc.shownScale();
        if (r < 0 || s < 0) return "no key";
        return juce::String::fromUTF8 (gg::rootName (r)) + " " + juce::String::fromUTF8 (gg::scaleName (s));
    }

    void paint (juce::Graphics& g) override
    {
        using namespace ggui;
        curTheme() = theme;
        auto r = getLocalBounds().toFloat();
        const float h = r.getHeight();
        const juce::Font small = serif (h * 0.38f);
        g.setFont (small);
        g.setColour (th().amber.withAlpha (0.6f));
        juce::GlyphArrangement gk; gk.addLineOfText (small, "KEY", 0, 0);
        const float kw = gk.getBoundingBox (0, -1, true).getWidth() + 6.f;
        g.drawText ("KEY", r.removeFromLeft (kw), juce::Justification::centredLeft);
        // the key itself, as big as fits
        const juce::String t = text();
        float size = h * (th().lcd ? 0.62f : 0.72f);
        juce::Font f = serif (size, true);
        for (int i = 0; i < 12; ++i)
        {
            juce::GlyphArrangement ga; ga.addLineOfText (f, t, 0, 0);
            if (ga.getBoundingBox (0, -1, true).getWidth() <= r.getWidth() - 12.f) break;
            size *= 0.9f; f = serif (size, true);
        }
        g.setFont (f);
        const bool remapped = (int) std::lround (proc.apvts.getRawParameterValue ("mode")->load()) > 0;
        g.setColour (th().amber.withAlpha (proc.shownRoot() < 0 ? 0.45f : 1.0f));
        g.drawText (t, r.withTrimmedRight (12.f), juce::Justification::centredLeft);
        // a small arrow just after it: there is a menu; a dot when the notes are moved to another scale
        juce::GlyphArrangement gt; gt.addLineOfText (f, t, 0, 0);
        const float tw = gt.getBoundingBox (0, -1, true).getWidth();
        juce::Path a; const float cx = juce::jmin (r.getRight() - 6.f, r.getX() + tw + 9.f), cy = r.getCentreY();
        a.addTriangle (cx - 4.f, cy - 2.f, cx + 4.f, cy - 2.f, cx, cy + 3.f);
        g.fillPath (a);
        if (remapped) g.fillEllipse (cx - 2.f, cy - 10.f, 4.f, 4.f);
    }

    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked()) showMenu(); }

    void showMenu()
    {
        const int root = proc.shownRoot();
        const int written = proc.keyScale.load();
        const int mode = (int) std::lround (proc.apvts.getRawParameterValue ("mode")->load());
        const bool known = root >= 0 && written >= 0;
        juce::PopupMenu m;
        m.addSectionHeader ("Home note");
        static const int order[12] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 2 };   // C to B
        for (int i = 0; i < 12; ++i)
            m.addItem (1 + order[i], juce::String::fromUTF8 (gg::rootName (order[i])), known, root == order[i]);
        m.addColumnBreak();
        m.addSectionHeader ("Scale");
        m.addItem (100, juce::String::fromUTF8 ("as written (") + juce::String::fromUTF8 (known ? gg::scaleName (written) : "none") + ")", known, mode == 0);
        for (int s = 0; s < 7; ++s)
            m.addItem (101 + s, juce::String::fromUTF8 (gg::scaleName (s)), known, mode == s + 1);
        m.addSeparator();
        m.addItem (200, "New songs (GENERATE, EXPERIMENT) in this key", true, proc.apvts.getRawParameterValue ("genkey")->load() > 0.5f);
        m.addItem (201, "Listen again (find the key)");
        juce::Component::SafePointer<KeyBox> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r) { if (safe != nullptr) safe->choose (r); });
    }

    void choose (int r)
    {
        auto set = [this] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        if (r >= 1 && r <= 12 && proc.shownRoot() >= 0)
        {   // a new home note: the whole song moves (TUNE), the shortest way
            const int want = r - 1, now = proc.shownRoot();
            const int move = ((want - now + 18) % 12) - 6;
            float tune = proc.apvts.getRawParameterValue ("tune")->load() + (float) move;
            if (tune > 24.f) tune -= 12.f;
            if (tune < -24.f) tune += 12.f;
            set ("tune", tune);
        }
        else if (r == 100) set ("mode", 0.f);
        else if (r >= 101 && r <= 107)
        {
            const int s = r - 101;
            set ("mode", s == proc.keyScale.load() ? 0.f : (float) (s + 1));   // its own scale: as written
        }
        else if (r == 200) set ("genkey", proc.apvts.getRawParameterValue ("genkey")->load() > 0.5f ? 0.f : 1.f);
        else if (r == 201) { proc.findKey(); set ("mode", 0.f); }
        if (r >= 1 && r <= 107 && r != 100) set ("genkey", 1.f);   // choosing a key: new songs follow it
        repaint();
        if (onChanged) onChanged();
    }

private:
    GraphProcessor& proc;
    juce::String last;
    void timerCallback() override { const auto t = text(); if (t != last) { last = t; repaint(); } }
};
