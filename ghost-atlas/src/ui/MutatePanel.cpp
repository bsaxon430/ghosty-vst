// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the MUTATE panel. Pressing MUTATE mutates straight away, in the mode last used; choosing another mode
// starts again in that one. What comes back is compared with the original (the two are level-matched, so the
// comparison is fair): keep it, try again, or put the sound back as it was. Reimagine, beside the modes, is the
// experimental offline rebuild (core/Reimagine.h). The work runs on its own thread; the panel only shows how it goes.
#include "Pages.h"
#include "../core/Mutate.h"
#include "../core/Reimagine.h"

namespace atlas::ui
{
namespace
{
// the work, on its own thread: one of MUTATE's three modes, or REIMAGINE
class MutateJob final : public juce::Thread
{
public:
    MutateJob (Processor& p, Preset from, int what, juce::uint32 s)
        : juce::Thread ("Ghost Atlas MUTATE"), proc (p), original (std::move (from)), kind (what), seed (s) { startThread(); }
    ~MutateJob() override { stopThread (60000); }
    void run() override
    {
        const Engine::Resolver resolver = [this] (const juce::String& id) { return proc.library().sfzPath (id); };
        const Progress progress = [this] (float f, const juce::String& what) {
            fraction.store (f);
            { const juce::ScopedLock sl (lock); doing = what; }
            return ! threadShouldExit();
        };
        if (kind == 3) result = reimagine (proc.library(), resolver, original, seed, progress);
        else
        {
            Mutator mutator (proc.library(), resolver);
            result = mutator.mutate (original, kind == 0 ? MutateMode::Subtle : (kind == 1 ? MutateMode::Evolve : MutateMode::Wild), seed, progress);
        }
        // stopped part-way: whatever REIMAGINE had already added to the library goes again
        if (threadShouldExit() && result.madeInstrument.isNotEmpty()) { discardReimagined (proc.library(), result); result = Mutation(); }
        done.store (true);
    }
    juce::String now() const { const juce::ScopedLock sl (lock); return doing; }
    Processor& proc; Preset original; int kind; juce::uint32 seed;
    Mutation result;
    std::atomic<float> fraction { 0.0f };
    std::atomic<bool> done { false };
private:
    juce::CriticalSection lock;
    juce::String doing;
};

class MutatePanel final : public Overlay, private juce::Timer
{
public:
    explicit MutatePanel (Shell& s) : shell (s)
    {
        original = shell.proc.capture();
        kind = juce::jlimit (0, 2, (int) prop (shell.proc.uiState, "mutateMode", 1));
        mode.choices = { "Subtle", "Evolve", "Wild" };
        mode.get = [this] { return kind < 3 ? (float) kind : -1.0f; };
        mode.set = [this] (float v) { start ((int) v); };
        other.text = "Reimagine"; other.onClick = [this] { start (3); };
        other.setTooltip ("Experimental. Rebuilds this sound out of small pieces of other instruments, and adds the result to your library as a new instrument. It takes about half a minute.");
        which.choices = { "The original", "The new one" };
        which.get = [this] { return hearing; };
        which.set = [this] (float v) { hearing = v; shell.proc.loadPreset (v > 0.5f ? result.preset : forComparison()); };
        hear.text = "Play a few notes"; hear.onClick = [this] { shell.proc.engine().audition (0, 60); };
        keep.text = "Keep it"; keep.style = TextBtn::lit;
        keep.onClick = [this] { finish (true); };
        again.text = "Try again"; again.onClick = [this] { start (kind); };
        back.onClick = [this] { finish (false); };
        for (auto* c : std::initializer_list<juce::Component*> { &mode, &other, &back }) addAndMakeVisible (*c);
        for (auto* c : std::initializer_list<juce::Component*> { &which, &hear, &keep, &again }) addChildComponent (*c);
        startTimerHz (15);
        start (kind);                 // pressing MUTATE mutates: the mode last used, straight away
    }
    ~MutatePanel() override { stopTimer(); if (job != nullptr) job->signalThreadShouldExit(); for (auto& j : retired) j->signalThreadShouldExit(); job.reset(); retired.clear(); }

    bool busy() const override { return job != nullptr; }

    void resized() override
    {
        panel = juce::Rectangle<float> (860.0f, 600.0f).withCentre ({ (float) kBaseW * 0.5f, (float) kBaseH * 0.5f + 12.0f });
        const int x = (int) panel.getX() + 30, y = (int) panel.getY(), w = (int) panel.getWidth() - 60;
        mode.setBounds (x, y + 70, 330, 34); other.setBounds (x + w - 150, y + 71, 150, 32);
        which.setBounds (x, y + 190, 330, 34); hear.setBounds (x + 350, y + 191, 160, 32);
        keep.setBounds (x, y + 546, 110, 36); again.setBounds (x + 122, y + 548, 110, 32); back.setBounds (x + 244, y + 548, 190, 32);
        showParts();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        g.fillAll (l.deep.darker (0.85f).withAlpha (0.66f));
        g.setColour (l.deep.darker (0.62f).withAlpha (0.97f)); g.fillRoundedRectangle (panel, 16.0f);
        g.setColour (l.text (0.14f)); g.drawRoundedRectangle (panel.reduced (0.5f), 16.0f, 1.0f);
        const float x = panel.getX() + 30.0f, y = panel.getY(), w = panel.getWidth() - 60.0f;
        g.setColour (l.text (0.98f)); g.setFont (fellRoman (30.0f));
        g.drawText ("A new version of " + original.name, juce::Rectangle<float> (x, y + 16.0f, w, 40.0f), juce::Justification::centredLeft, true);
        static const char* about[4] = { "A close relative: one layer swapped or one added, and small moves of the rest.",
                                        "Keeps the main sound and chooses everything around it again.",
                                        "Sails far away: a different main sound, with layers chosen for it.",
                                        "Experimental: rebuilds the sound out of small pieces of other instruments, as a new instrument in your library." };
        g.setColour (l.text (0.72f)); g.setFont (sans (15.0f));
        g.drawText (about[juce::jlimit (0, 3, kind)], juce::Rectangle<float> (x, y + 112.0f, w, 22.0f), juce::Justification::centredLeft, true);
        g.setColour (l.text (0.1f)); g.fillRect (x, y + 146.0f, w, 1.0f);

        const bool got = have && result.ok && job == nullptr;
        if (job != nullptr)
        {
            g.setColour (l.text (0.92f)); g.setFont (sans (16.0f));
            g.drawText (job->now(), juce::Rectangle<float> (x, y + 166.0f, w, 24.0f), juce::Justification::centredLeft, true);
            const auto bar = juce::Rectangle<float> (x, y + 202.0f, w, 6.0f);
            g.setColour (l.text (0.15f)); g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (l.lantern); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.03f, 1.0f, job->fraction.load())), 3.0f);
            headingAt (g, "How it chooses", x, y + 248.0f);
            g.setColour (l.text (0.74f)); g.setFont (sans (14.5f));
            g.drawFittedText (kind == 3
                ? "The sound is played at thirteen notes, and so are four instruments far from it in timbre. For each note, Samplebrain's mosaic engine rebuilds the original out of "
                  "the others' pieces. The result keeps the original's loudness shape, is looped where the original held, and joins your library as an instrument of its own."
                : "Layers are chosen because they work together, not because they sound alike: each is weighed for the part it would play (a second body, an edge on each note, "
                  "slow air behind, light on top), for where its energy lies against the others, for its tuning and its width. Then the result is played through the engine - "
                  "low, middle and high notes, soft and hard, and a chord - and whatever fails the list below is put right or thrown away.",
                (int) x, (int) y + 276, (int) w, 110, juce::Justification::topLeft, 6);
        }
        else if (have && ! result.ok)
        {
            headingAt (g, "Nothing came of it this time", x, y + 166.0f);
            g.setColour (l.danger); g.setFont (sans (15.0f));
            g.drawFittedText (result.failure, (int) x, (int) y + 196, (int) w, 90, juce::Justification::topLeft, 4);
        }
        else if (got)
        {
            g.setColour (l.text (0.62f)); g.setFont (sans (13.5f));
            g.drawText (result.originalTrimDb < -0.5f ? "Listen to (equally loud: the original is turned down " + juce::String (-result.originalTrimDb, 1) + " dB while you compare)"
                                                      : juce::String ("Listen to (the two are equally loud)"), juce::Rectangle<float> (x, y + 162.0f, 520.0f, 20.0f), juce::Justification::centredLeft, false);
            g.setColour (l.lantern); g.setFont (fellRoman (24.0f));
            g.drawText (result.preset.name, juce::Rectangle<float> (x + 530.0f, y + 190.0f, w - 530.0f, 34.0f), juce::Justification::centredRight, true);
            headingAt (g, "What was done", x, y + 248.0f);
            float ly = y + 276.0f;
            const auto face = sans (14.5f);
            for (int i = 0; i < result.changes.size(); ++i)
            {
                // a long line is given two; whatever does not fit above the list below is left out
                const bool two = juce::GlyphArrangement::getStringWidth (face, result.changes[i]) > w - 20.0f;
                const float h = two ? 38.0f : 22.0f;
                if (ly + h > y + 408.0f) break;
                g.setColour (l.accent.withAlpha (0.9f)); g.fillEllipse (x + 2.0f, ly + 7.0f, 5.0f, 5.0f);
                g.setColour (l.text (0.88f)); g.setFont (face);
                if (two) g.drawFittedText (result.changes[i], (int) x + 16, (int) ly, (int) w - 16, 36, juce::Justification::topLeft, 2, 0.85f);
                else g.drawText (result.changes[i], juce::Rectangle<float> (x + 16.0f, ly, w - 16.0f, 20.0f), juce::Justification::centredLeft, true);
                ly += h;
            }
        }

        // what is listened for: rings while the work goes on, ticks and crosses when it is done
        const float cy = y + 416.0f;
        headingAt (g, job != nullptr ? "What it listens for" : "What was listened for", x, cy);
        const auto names = Mutator::checkNames();
        const bool judged = job == nullptr && have && ! result.checks.empty();
        const int n = judged ? (int) result.checks.size() : names.size();
        for (int i = 0; i < n && i < 9; ++i)
        {
            const float cx = x + (float) (i % 3) * (w / 3.0f), ry = cy + 28.0f + (float) (i / 3) * 30.0f;
            if (judged)
            {
                const auto& c = result.checks[(size_t) i];
                juce::Path mark;
                if (c.passed) { mark.startNewSubPath (cx + 2.0f, ry + 9.0f); mark.lineTo (cx + 6.0f, ry + 13.0f); mark.lineTo (cx + 13.0f, ry + 4.0f); }
                else { mark.startNewSubPath (cx + 3.0f, ry + 4.0f); mark.lineTo (cx + 12.0f, ry + 13.0f); mark.startNewSubPath (cx + 12.0f, ry + 4.0f); mark.lineTo (cx + 3.0f, ry + 13.0f); }
                g.setColour (c.passed ? l.accent : l.danger); g.strokePath (mark, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (l.text (0.92f));
                drawFitted (g, c.name, { cx + 20.0f, ry - 2.0f, w / 3.0f - 26.0f, 18.0f }, juce::Justification::centredLeft, sans (14.0f), 0.75f);
                g.setColour (l.text (0.5f));
                drawFitted (g, c.found, { cx + 20.0f, ry + 13.0f, w / 3.0f - 26.0f, 14.0f }, juce::Justification::centredLeft, sans (11.5f), 0.7f);
            }
            else
            {
                g.setColour (l.text (0.3f)); g.drawEllipse (cx + 2.5f, ry + 3.0f, 10.0f, 10.0f, 1.3f);
                g.setColour (l.text (0.7f));
                drawFitted (g, names[i], { cx + 20.0f, ry - 1.0f, w / 3.0f - 26.0f, 18.0f }, juce::Justification::centredLeft, sans (14.0f), 0.75f);
            }
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { if (! panel.contains (e.position) && job == nullptr) finish (have && result.ok && hearing > 0.5f); }

private:
    // the original as it is heard beside the new sound: turned down, if the new one could not come all the way up to it
    Preset forComparison() const { Preset o = original; o.masterDb += result.originalTrimDb; return o; }

    // a new try: what is running is told to stop and left to finish on its own (the screen never waits for it)
    void start (int what)
    {
        if (job != nullptr) { job->signalThreadShouldExit(); retired.push_back (std::move (job)); }
        if (have && result.ok)
        {
            if (hearing > 0.5f) shell.proc.loadPreset (original);
            if (result.madeInstrument.isNotEmpty()) discardReimagined (shell.proc.library(), result);     // the one not kept goes from the library again
        }
        kind = juce::jlimit (0, 3, what);
        if (kind < 3)
        {
            auto o = shell.proc.uiState.getDynamicObject() != nullptr ? juce::DynamicObject::Ptr (shell.proc.uiState.getDynamicObject()) : newObject();
            o->setProperty ("mutateMode", kind); shell.proc.uiState = o.get();
        }
        hearing = 0.0f; have = false; result = Mutation();
        job = std::make_unique<MutateJob> (shell.proc, original, kind, ++seed + juce::Time::getMillisecondCounter());
        showParts();
    }
    // closes: with the new sound kept, or with everything as it was
    void finish (bool keepNew)
    {
        if (job != nullptr) { job->signalThreadShouldExit(); retired.push_back (std::move (job)); }
        const bool got = have && result.ok;
        if (got && keepNew)
        {
            shell.proc.loadPreset (result.preset);
            if (shell.say) shell.say ("Kept. Choose Save to keep it among your own sounds");
        }
        else
        {
            shell.proc.loadPreset (original);
            if (got && result.madeInstrument.isNotEmpty()) discardReimagined (shell.proc.library(), result);
        }
        if (retired.empty()) { close(); return; }
        closing = true; setVisible (false);                     // (a stopped job is given its moment to end before the panel goes)
    }
    void timerCallback() override
    {
        retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const std::unique_ptr<MutateJob>& j) { return ! j->isThreadRunning(); }), retired.end());
        if (closing) { if (retired.empty()) { closing = false; close(); } return; }
        if (job == nullptr) return;
        if (job->done.load())
        {
            result = job->result;
            job.reset();
            have = true;
            if (result.ok) { hearing = 1.0f; shell.proc.loadPreset (result.preset); }       // the new one is what plays now
            if (result.ok && result.madeInstrument.isNotEmpty() && shell.soundChanged) shell.soundChanged();
            showParts();
        }
        repaint();
    }
    void showParts()
    {
        const bool got = have && result.ok && job == nullptr;
        which.setVisible (got); hear.setVisible (got); keep.setVisible (got);
        again.setVisible (have && job == nullptr);
        back.text = job != nullptr ? "Cancel" : (got ? "Put it back as it was" : "Close");
        const int x = (int) panel.getX() + 30, y = (int) panel.getY();
        if (got) { again.setBounds (x + 122, y + 548, 110, 32); back.setBounds (x + 244, y + 548, 190, 32); }
        else if (have && job == nullptr) { again.setBounds (x, y + 548, 110, 32); back.setBounds (x + 122, y + 548, 110, 32); }
        else back.setBounds (x, y + 548, 110, 32);
        mode.repaint(); other.selected = kind == 3; other.repaint(); back.repaint();
        repaint();
    }

    Shell& shell;
    Preset original;
    Mutation result;
    Segments mode, which;
    TextBtn other, hear, keep, again, back;
    std::unique_ptr<MutateJob> job;
    std::vector<std::unique_ptr<MutateJob>> retired;
    juce::Rectangle<float> panel;
    int kind = 1; juce::uint32 seed = 0;
    float hearing = 0.0f;
    bool have = false, closing = false;
};
} // namespace

std::unique_ptr<Overlay> makeMutatePanel (Shell& s) { return std::make_unique<MutatePanel> (s); }
} // namespace atlas::ui
