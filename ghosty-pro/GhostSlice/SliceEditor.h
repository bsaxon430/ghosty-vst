#pragma once
// Ghost Slice window:
//   header   ghost + GHOST SLICE | EDIT / FLEX / PITCH / PLAY | BPM / KEY / GRID readouts | PLAY LOOP | LOAD PROJECT (RELINK) | SYNC | theme UNLOCK
//   info     file (rate, length, channels, fingerprint) | plan (pieces, amber stretch warnings) | QUALITY status | messages
//   view     the waveform workspace (WaveView), the ARRANGE lane strip under it
//   tools    row 1: CUT SPLIT@MARKERS DELETE DUPLICATE REVERSE UNDO REDO | SNAP TRIPLET | A/B | LIVE/STUDIO | EXPORT WAV / MIDI
//            row 2 (EDIT): GAIN FADE IN FADE OUT | PITCH CENTS FORMANT     (FLEX): MOVE STRETCH SLIP | ADD REMOVE ANCHOR | SNAP % | TAP BPM | T-LOCK
//            row 3 (FLEX, GHOST ALIGN): MATERIAL | AUTO ALIGN | TIGHT / GROOVE / MANUAL | STRENGTH | CONF | FEEL | APPLY | CLEAR AUTO | USE BPM
//            PITCH: row 2 PITCH CENTS FORMANT TONALITY | row 3 (the selected notes) FINE FORMANT DRIFT VIBRATO TRANS GLIDE SPLIT MERGE RESET
//                   row 4 SOURCE KEY PROJECT KEY MATCH KEY CHORDS | SCALE SNAP SNAP ALL | DRAW | F0 CURVE
#include "WaveView.h"
#include "PlayPanel.h"
#include "HoldStrip.h"
#include "GhostArt.h"
#include "SliceMascot.h"

// the header SYNC button: the shared one, drawn as a glass pill in the GHOST look (every other look: the shared painting unchanged)
struct GlassSyncButton : ghosty::SyncButton
{
    explicit GlassSyncButton (ghosty::SyncTransport& s) : ghosty::SyncButton (s), st (s) {}
    ghosty::SyncTransport& st; bool glass = false;
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        if (! glass) { ghosty::SyncButton::paintButton (g, over, down); return; }
        const bool own = st.getMode() == ghosty::SyncTransport::Mode::Own;
        ghosty::glass::pill (g, getLocalBounds().toFloat(), own, over, down);
        g.setColour (own ? juce::Colours::white : ghosty::glass::ink());
        g.setFont (juce::Font (juce::FontOptions (juce::jmin (12.5f, (float) getHeight() * 0.48f), juce::Font::bold)));
        g.drawText (own ? "SYNC: OWN" : "SYNC: DAW", ghosty::glass::pillBody (getLocalBounds().toFloat()), juce::Justification::centred);
    }
};

// the ? overlay: the keyboard shortcuts + gestures, with the 120 px mascot (click / Esc / ? closes it)
struct HelpOverlay : juce::Component
{
    gslice::ui::Palette P;
    static juce::StringArray lines();        // "KEY|what it does" (section headers start with '#')
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { setVisible (false); }
};

class SliceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SliceEditor (SliceProcessor&);
    ~SliceEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void setTheme (ghosty::themes::Theme t);            // BLACK / PURPLE / LIGHT (= setLook 0 / 1 / 2)
    ghosty::themes::Theme theme() const { return shownTheme; }
    // the look button: BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK. 0 BLACK, 1 PURPLE, 2 LIGHT (= ghosty::themes::toInt), 3 GHOST (Slice only:
    // liquid glass on the LIGHT base). Saved in the session like the theme always was (SliceProcessor::uiTheme, "theme").
    enum { kLookBlack = 0, kLookPurple = 1, kLookLight = 2, kLookGhost = 3, kLooks = 4 };
    void setLook (int look);
    int look() const { return shownLook; }
    static int nextLook (int l) { return (l + 1) % kLooks; }
    static juce::String lookName (int l) { return l == kLookGhost ? "GHOST" : ghosty::themes::displayName (ghosty::themes::fromInt (l)); }
    ghosty::glass::Backdrop backdrop;        // GHOST: the aurora, cached per size
    ghosty::glass::PanelCache panels;        // GHOST: the big glass panels, cached per size
    static constexpr int kGlassGutter = 8;   // GHOST: the gap around the floating panels
    void paintGlassSurfaces (juce::Graphics&);   // GHOST: the backdrop + panels + wells, no text (the contrast tests sample it)
    void setWorkspace (gslice::ui::Workspace w);
    void toast (const juce::String& s);
    void refresh();                          // enable states, readouts, slider values from the project
    bool regionPitchScope() const { return view.workspace() == gslice::ui::Workspace::Edit && view.selectedRegion() != nullptr; }   // PITCH / CENTS / FORMANT edit the selected region
    void exportWavClicked();
    void exportMidiClicked();
    void loadClicked();
    void projectMenu();

    // a captioned horizontal slider
    struct Field : juce::Component
    {
        juce::Label cap; juce::Slider s { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        Field (const juce::String& name, double lo, double hi, double step, const juce::String& suffix);
        void resized() override { auto r = getLocalBounds(); cap.setBounds (r.removeFromLeft (capW)); s.setBounds (r); }
        int capW = 62;
    };

    SliceProcessor& proc;
    gslice::ui::LnF lnf;
    gslice::ui::Palette P;
    gslice::ui::WaveView view { proc };
    gslice::ui::LaneStrip lane { proc, view };
    juce::TextButton tabs[4];
    juce::Label bpmLbl, keyLbl, gridLbl;
    juce::TextButton playBtn { "PLAY" }, loopBtn { "LOOP" }, loadBtn { "LOAD" }, projBtn { "PROJECT" }, relinkBtn { "RELINK" }, themeBtn;
    GlassSyncButton syncBtn { proc.sync };
    ghosty::UnlockButton unlock { proc.license };
    // row 1
    juce::TextButton cutBtn { "CUT" }, splitMkBtn { "SPLIT @ MARKERS" }, delBtn { "DELETE" }, dupBtn { "DUPLICATE" }, revBtn { "REVERSE" },
                     undoBtn { "UNDO" }, redoBtn { "REDO" }, tripBtn { "T" }, abBtn { "A/B" }, qualityBtn { "STUDIO" }, exportWavBtn { "EXPORT WAV" }, exportMidiBtn { "EXPORT MIDI" };
    juce::ComboBox snapBox;
    // row 2 EDIT
    Field gainF { "GAIN", -48.0, 12.0, 0.1, " dB" }, fadeInF { "FADE IN", 0.0, 2000.0, 1.0, " ms" }, fadeOutF { "FADE OUT", 0.0, 2000.0, 1.0, " ms" },
          pitchF { "PITCH", -24.0, 24.0, 1.0, " st" }, centsF { "CENTS", -100.0, 100.0, 1.0, " ct" }, formantF { "FORMANT", -12.0, 12.0, 0.1, " st" };
    // row 2 FLEX
    juce::TextButton moveBtn { "MOVE" }, stretchBtn { "STRETCH" }, slipBtn { "SLIP" }, addMkBtn { "ADD MARKER" }, remMkBtn { "REMOVE" }, anchorBtn { "ANCHOR" },
                     tapBtn { "TAP BPM" }, lockBtn { "T-LOCK" };
    Field snapStrF { "SNAP %", 0.0, 100.0, 1.0, " %" };
    // row 3 FLEX: GHOST ALIGN (phase 8) + the analysis material
    juce::ComboBox materialBox, alignModeBox;
    juce::TextButton alignBtn { "AUTO ALIGN" }, applyAlignBtn { "APPLY" }, clearAutoBtn { "CLEAR AUTO" }, useBpmBtn { "USE BPM" };
    Field strengthF { "STRENGTH", 0.0, 100.0, 1.0, " %" }, confF { "CONF", 0.0, 100.0, 1.0, " %" }, feelF { "FEEL", 0.0, 100.0, 1.0, " %" };
    // row 3 PITCH (phase 13): the f0 overlay; (phase 9) the source / project keys + MATCH PROJECT KEY; row 2 TONALITY
    juce::TextButton f0Btn { "F0 CURVE" }, matchKeyBtn { "MATCH KEY" };
    juce::TextButton chordsBtn { "CHORDS" };   // (phase 17) the read-only chord lane on / off: PITCH row 4 after MATCH KEY, EDIT row 3 after the CHOP info
    juce::ComboBox srcKeyBox, projKeyBox, keyModeBox;   // + GHOST KEY mode (what MATCH KEY does)
    void keyPicker();                                    // the header KEY readout: pick the target (project) key + the mode
    void mouseUp (const juce::MouseEvent&) override;
    Field tonalityF { "TONALITY", 1000.0, 20000.0, 10.0, " Hz" };
    bool matchProjectKey();
    // PITCH note editing (phase 14): row 3 = the selected notes' FINE / FORMANT / DRIFT / VIBRATO / TRANSITION + GLIDE SPLIT MERGE RESET,
    // row 4 = the keys + SCALE (snap) + DRAW + F0 CURVE
    Field fineF { "FINE", -50.0, 50.0, 1.0, " ct" }, nFormF { "FORMANT", -12.0, 12.0, 0.1, " st" }, driftF { "DRIFT", 0.0, 200.0, 1.0, " %" },
          vibF { "VIBRATO", 0.0, 200.0, 1.0, " %" }, transF { "TRANS", 0.0, 250.0, 1.0, " ms" };
    juce::TextButton glideBtn { "GLIDE" }, splitNoteBtn { "SPLIT" }, mergeBtn { "MERGE" }, resetNoteBtn { "RESET" },
                     drawBtn { "DRAW" }, snapSelBtn { "SNAP" }, snapAllBtn { "SNAP ALL" };
    juce::ComboBox scaleBox;
    juce::ComboBox engineBox;   // phase 15: the note edits' ENGINE (AUTO / VOCAL / WIDE): the selected notes', else the project's
    gslice::pitchedit::Scale chosenScale() const;   // the SCALE box (CHROMATIC / a key); default = the source key
    std::vector<Field*> noteFields() { return { &fineF, &nFormF, &driftF, &vibF, &transF }; }
    void notesToFields();
    // GHOST CHOP in EDIT (row 3): the slice mode (shared with PLAY), its SENS / PARTS / DIV, SPLIT @ SLICES (the cuts preview on the waveform)
    juce::ComboBox chopBox, chopDivBox;
    Field chopSensF { "SENS", 0.0, 100.0, 1.0, " %" }, chopPartsF { "PARTS", 2.0, 64.0, 1.0, "" };
    juce::TextButton splitSlicesBtn { "SPLIT @ SLICES" };
    juce::Label chopInfo;
    void chopChanged (const std::function<void (gslice::PlayMap&)>& f, const juce::String& label);
    // polish (first release): ? = the shortcut list, the STUDIO progress bar in the info strip
    HelpOverlay help;
    juce::TextButton helpBtn { "?" };
    void showHelp (bool on);
    juce::Rectangle<int> progressArea;       // the STUDIO render progress bar (info strip, right)
    gslice::align::Settings alignSettings() const;
    bool proposeAlign (bool announce = true);   // the amber preview
    bool applyAlign();                          // the preview (fresh) as AUTO markers: one undo step
    bool useDetectedBpm();

    std::vector<juce::Component*> headerItems() ;
    std::vector<juce::Component*> toolItems() ;
    juce::Rectangle<int> headerArea, infoArea, logoArea, toolArea;
    // PLAY (phase 10)
    gslice::ui::PlayPanel playPanel { proc };
    // REPEAT (phase 11) / HOLD (phase 12): row-2 buttons in EDIT, the strips under the waveform, the overlay on it
    juce::TextButton repeatBtn { "REPEAT" }, holdBtn { "HOLD" };
    gslice::ui::RepeatStrip repeatStrip { proc };
    gslice::ui::EditOverlay overlay { proc, view };
    gslice::ui::HoldStrip holdStrip { proc };
    void repeatClicked();
    void showRepeat (gslice::int64 id);
    void holdClicked();
    void showHold (gslice::int64 regionId, gslice::int64 holdId);
    void layoutStrips();
    juce::String toastText; double toastUntil = 0;
    juce::String infoLeft() const, infoRight() const;
    juce::String keyText() const, keySub() const, bpmSub() const;   // the readouts (SET vs detected + confidence)
    static constexpr int kReadoutH = 32;
    static constexpr int kHeader = 58, kInfo = 24, kLane = 52, kTools = 84, kTools3 = 122, kTools4 = 160;
    int toolsHeight() const;                 // FLEX / PITCH have a third row
private:
    void timerCallback() override;
    void layoutRow2();
    void sliderToProject (Field& f);
    ghosty::themes::Theme shownTheme = ghosty::themes::Theme::Dark;
    int shownLook = kLookBlack;
    void paintGlass (juce::Graphics&);
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tips { this, 600 };
    bool refreshing = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceEditor)
};
