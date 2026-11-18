#pragma once
// UNDER THE HOOD: every CDP process Ghost Lab ships (pvoc stretch strange focus blur modify combine morph), each with ALL
// of its own modes and parameters, exactly as the CDP programs print them. The list is read from the usage text of the
// real programs (cdp/gen-hood-catalog.py -> HoodCatalog.inc); running a process runs the real program with exactly the
// arguments typed here, and the command line that ran is shown.
#include "Actions.h"
#include <juce_graphics/juce_graphics.h>

namespace glab::hood
{
struct Param
{
    juce::String name;       // CDP's own word ("timestretch"), a flag word ("-l"), or "-fN|-pN"
    juce::String kind;       // value | datafile | switch | flagvalue | either | dataout (a data file the program writes)
    juce::String flag;       // "-r" for flagvalue, "-l" for a switch, "-fN|-pN" for either
    juce::String help;       // CDP's own description (from its usage text)
    juce::String defaultText, rangeText;   // only when the CDP text states them
    juce::String ext;        // dataout: the extension the engine knows the file by (.frq pitch, .for formant, .evl envelope, .txt)
    bool optional = false;
    bool main = false;       // dataout: the process's main output (there is no sound output beside it)
    bool pre = false;        // comes before the output file on the command line (combine make pitchfile formantfile outfile)
};

struct Form                  // one USAGE line of the process
{
    juce::String usage;      // the usage line as CDP prints it (after "prog name")
    juce::String modeSpec;   // "" (no mode), "2-4", "5", "mode"
    std::vector<int> modes;  // the concrete mode numbers this form serves
    int inputs = 1;          // sounds it reads (1 = the specimen, 2 = the specimen and SPECIMEN 002)
    bool multi = false;      // takes more sounds after those (infile2 ...): Request::extraFiles
    juce::String out;        // sound | text | none | data (a data file: pitch, formant, envelope data)
    juce::String outExt;     // data: its extension
    std::vector<Param> params;
};

struct Process
{
    juce::String prog, name, title, progTitle, usage;   // usage: the whole text CDP prints for it, verbatim
    std::map<int, juce::String> labels;                 // mode number -> CDP's own mode text
    std::vector<Form> forms;
    juce::String id() const { return prog + " " + name; }
};

const std::vector<Process>& catalog();
const Process* find (const juce::String& prog, const juce::String& name);
const Form* formFor (const Process& p, int mode);              // mode 0 = the process has no mode
std::vector<int> modesOf (const Process& p);                    // every mode number the process offers (empty: none)
// the programs in the order CDP's own front-ends group them, with their one-line titles
std::vector<std::pair<juce::String, juce::String>> groups();
// what CDP says for each, so the page can start with them filled in (only values the CDP text itself states)
std::map<juce::String, juce::String> startValues (const Form& f);

// one run: the process, its mode and the typed values (by parameter name; a switch is "1"). The analysis size and overlap
// belong to pvoc anal, which the spectral programs run first.
struct Request
{
    juce::String prog, name;
    int mode = 0;
    std::map<juce::String, juce::String> values;
    juce::String points = "1024", overlap = "3";
    juce::StringArray extraFiles;   // more sounds for a process that takes several (sausage ...): paths of sound files
    juce::String dataDir;           // where data files the process writes go (the session's data folder)
    juce::String toData() const;
    static bool fromData (const juce::String& d, Request& out);
    juce::String describe() const;
    bool spectral() const;   // runs pvoc anal first (and pvoc synth after, when the result is a sound)
};

// the words of the CDP command (after the program names), files by name; makeFile turns typed text into a data file in the
// job folder and returns its name. Returns false and a plain-words reason when a needed value is missing.
bool buildWords (const Process& p, const Form& f, const Request& r, const juce::StringArray& inFiles, const juce::String& outFile,
                 const std::function<juce::String (const juce::String& text)>& makeFile, juce::StringArray& words, juce::String& error,
                 const std::function<juce::String (const Param& dataOut)>& makeOut = nullptr);   // names an extra data output (default: word + ext)

// the data files combine make / make2 need, made from a sound by the engine's own programs (what MAKE FROM SOUND runs):
// pitch = pvoc anal + repitch getpitch 1 (its binary pitch file), formant = pvoc anal + formants get -p4, envelope = pvoc anal +
// repitch analenv. The file lands in dataDir; the commands that ran are returned (the page shows them)
struct DataMade { juce::File file; juce::StringArray commands; juce::String error; };
DataMade makeData (const Cdp& cdp, const Audio& sound, const juce::String& kind, const juce::String& points, const juce::String& overlap, const juce::File& dataDir);
juce::String dataKindFor (const juce::String& paramName);   // "pitch" / "formant" / "envelope" / "" from a usage word (pitchfile, formantfile, envfile)
juce::File writeSoundFile (const Audio& a, const juce::File& dataDir, const juce::String& stem);   // a sound as a file the engine can read (32-bit wav)

// what the page shows of the engine's own text: the same words, minus the engine's release banner line and its project name
// (the UI never names the open-source project; the one About / Credits item does)
juce::String shownText (const juce::String& engineText);

// ------------------------------------------------------------------------------------------------ the ORIGINAL pages
// The engine's own front end draws each process from what its `cdparams` program prints for the process and mode: every
// parameter with its display kind, its own name, type, range, default and the slider's starting sub-range, then the check-button
// flags. Ghost Lab reads the same (cdp/gen-hood-dialogs.py -> HoodDialogs.inc, with a reference sound), and at run time asks
// the shipped cdparams again with the real sound (liveDialog), so ranges that depend on the sound are the real ones.
struct DialogItem
{
    juce::String kind;             // LINEAR LOG POWTWO PLOG NUMERIC LOGNUMERIC SWITCHED FILE_OR_VAL FILENAME CHECKBUTTON STRING_A ...
    juce::String name, name2, name3;   // the engine's own control name(s) (SWITCHED: the two choices)
    juce::String type;             // i d (fixed int / number), I D (may also be a breakpoint file: time-varying)
    double lo = 0, hi = 0, dflt = 0, sublo = 0, subhi = 0, lo2 = 0, hi2 = 0, dflt2 = 0;
    int ranges = 0;                // FILENAME: how many ranges the data must keep to
    juce::String param, flag;      // the usage word this control drives (the key in Request::values) and its flag letter
    bool optional = false, extra = false;   // extra: in the engine's own table but not in its usage text (still sent)
    bool numeric() const { return kind == "LINEAR" || kind == "LOG" || kind == "POWTWO" || kind == "PLOG" || kind == "NUMERIC" || kind == "LOGNUMERIC" || kind == "FILE_OR_VAL"; }
    bool logarithmic() const { return kind == "LOG" || kind == "PLOG" || kind == "LOGNUMERIC"; }
    bool slider() const { return kind == "LINEAR" || kind == "LOG" || kind == "POWTWO" || kind == "PLOG" || kind == "SWITCHED"; }
    bool varying() const { return type == "D" || type == "I" || kind == "FILE_OR_VAL"; }
    bool integer() const { return type == "i" || type == "I"; }
    bool textual() const { return kind == "FILENAME" || kind.startsWith ("STRING") || kind == "VOWELS" || kind == "GENERICNAME"; }
};
struct Dialog
{
    juce::String prog, name, constName;
    int procNo = 0, mode = 0, maxmode = 0, brklen = 0;
    std::vector<DialogItem> items;
    juce::StringArray problems, notes;
    juce::String raw;              // exactly what cdparams printed
    juce::String id() const { return prog + " " + name + (mode > 0 ? " " + juce::String (mode) : juce::String()); }
};
const std::vector<Dialog>& dialogs();
const Dialog* dialogFor (const juce::String& prog, const juce::String& name, int mode);   // mode 0 = no modes

// what the engine's front end tells cdparams about the sound; points / overlap are the analysis the spectral programs get
struct SoundInfo
{
    double sampleRate = 44100.0;
    int channels = 1;
    juce::int64 samples = 441000;
    int points = 1024, overlap = 3;
};
SoundInfo referenceSound();                                  // the one the baked pages were made with
juce::StringArray cdparamsArgs (const Dialog& d, const SoundInfo& s);   // the words cdparams is started with
bool parseDialogText (const juce::String& text, Dialog& out);          // its printed lines -> items (the generator's parse, in C++)
// the page for THIS sound: cdparams run now (needs the shipped program); nullptr when it can't run or prints something else
std::unique_ptr<Dialog> liveDialog (const Cdp& cdp, const Dialog& baked, const SoundInfo& s);
// a short plain-words line for a control (our name, printed small under the engine's own): from the engine's own usage help
juce::String plainWords (const Process& p, const Form& f, const DialogItem& it);
// the engine's own default values for a page, keyed the way Request::values is
std::map<juce::String, juce::String> dialogDefaults (const Dialog& d);
juce::String formatValue (double v, bool integer);

RenderResult renderHood (const Step& s, const Audio& in, const Audio* partner, const Cdp& cdp, const std::atomic<bool>* cancel);

// ------------------------------------------------------------------------------------------------ in plain words
// Every process + mode with our own name, a category (what it does to the sound), a one-line description and a longer one;
// every engine control with our own name (cdp/gen-hood-friendly.py -> HoodFriendly.inc). The engine's own names stay on
// the page, small, under ours.
struct Category { juce::String id, title, blurb; juce::Colour colour; };
struct Entry
{
    juce::String prog, name, cat, title, shortText, about, credit;
    int mode = 0;
    const Process* proc = nullptr;
    int inputs = 1;          // sounds it needs (2: the second sound too)
    bool multi = false;      // takes more sounds
    juce::String out;        // sound | text | none | data
    juce::String key() const { return prog + " " + name + (mode > 0 ? " " + juce::String (mode) : juce::String()); }
};
const std::vector<Category>& categories();
const Category* category (const juce::String& id);
const std::vector<Entry>& entries();
const Entry* entryFor (const juce::String& prog, const juce::String& name, int mode);
std::vector<const Entry*> versionsOf (const Process& p);        // the entries of every mode of a process, in mode order
juce::String friendlyControl (const juce::String& engineName);  // "TIMESTRETCH_MULTIPLIER" -> "Stretch amount"
}
