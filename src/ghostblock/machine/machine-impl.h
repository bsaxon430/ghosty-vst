/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: what its parts share (private to this folder).
 *
 *   assets.cpp          the embedded art, the layout files, Poppins, the label cache
 *   world.cpp           sky, cloud decks, sun shafts, land, meadow, storm, lightning
 *   stage.cpp           one page of the machine: its painted layers, what moves and lights, the particles
 *   ui.cpp              every control: building it from ui-layout.json, binding it to the patch, drawing it
 *   ui-input.cpp        the pointer and the keys
 *   ledger.cpp          the preset ledger, the brass dialogs
 *   machine-editor.cpp  the clock, the engine's queues, composing and presenting the picture
 */
#pragma once

#include <array>
#include <functional>
#include <map>
#include <unordered_map>

#include "ghostblock/machine/machine-editor.h"
#include "ghostblock/machine/raster.h"
#include "presets/preset-manager.h"
#include <juce_dsp/juce_dsp.h>

namespace ghostblock::p2::machine
{
using Param = ss::Param;
using FRect = juce::Rectangle<float>;
using FPoint = juce::Point<float>;

// ---------------------------------------------------------------------------------------------- the picture's grid
// The design canvas is 1040 x 744. What changed is tracked in cells of 16 x 16 design px: a part marks the cells it
// touches, and only those cells are composed again and handed to the screen.
constexpr int CW = 1040, CH = 744;
constexpr int CELL = 16, COLS = 65, ROWS = 47, NCELLS = COLS * ROWS;

struct CellSet
{
    std::array<uint64_t, (NCELLS + 63) / 64> w{};
    void clear() { w.fill(0); }
    void fill();
    bool any() const;
    int count() const;
    bool test(int i) const { return ((w[(size_t)(i >> 6)] >> (i & 63)) & 1u) != 0; }
    void set(int i) { w[(size_t)(i >> 6)] |= (uint64_t)1 << (i & 63); }
    void mark(FRect designRect);
    void mark(const std::vector<uint16_t> &cells)
    {
        for (auto c : cells)
            set(c);
    }
    void add(const CellSet &o)
    {
        for (size_t i = 0; i < w.size(); ++i)
            w[i] |= o.w[i];
    }
    void remove(const CellSet &o)
    {
        for (size_t i = 0; i < w.size(); ++i)
            w[i] &= ~o.w[i];
    }
    void rects(std::vector<IRect> &out, int S) const; // merged runs, in device px
    FRect bounds() const;                              // design px (empty if none)
};
std::vector<uint16_t> cellsOf(FRect designRect);
std::vector<uint16_t> cellsOf(const Sprite &s, float designX, float designY, int S); // the cells where it has pixels

// ---------------------------------------------------------------------------------------------- what drives the life
enum Group : int
{
    gNone = 0,
    gJaw,
    gFlank,
    gYard,
    gOrbit,
    gWind,
    gWindmill,
    gFxWarp,   // FX: the town's sails turn with WarpCore's WARP
    gFxScale,  // FX: the town's gears with SCALE
    gSpectral, // FX: the ghost's own gears with the rack
    gCount
};

struct Env
{
    static constexpr float IDLE = 0.15f;
    double t{0};
    float kick{0}, act{IDLE};                   // activity: any control movement, notes, the output
    float stormBase{0}, stormKick{0}, storm{0}; // how heavy the FM is (+ the SKY dial)
    float stormHold{-1};                        // tests: hold the weather here
    struct Op
    {
        bool awake{false};
        float level{0}, heard{0}, out{0};
    } ops[ss::numOps];
    float out{0}, note{0}, noteTarget{0};
    bool morphOn{false}, orbit{false};
    int sel{0}, selLink{-1};
    float hold{0}, holdTarget{0};
    int voices{0};
    float driftRate{0.4f}, driftAmt{0}, pdAmt{0}, pdDecay{0.5f}, pdEnv{0}, drive{0}, master{1};
    float corner[Morph::N]{};
    float boost[gCount]{};
    int page{0}, theme{0};
    bool calm{false};
    struct Fx // SPECTRAL FX, as the FX page's art needs it
    {
        bool on{false}, warpOn{false};
        float in{0}, out{0}, spectral{0}, wet{1}, fhigh{1}, warp01{0.2f}, scale01{0.3f}, order{0};
        int poles{2};
    } fx;
    void bump(float v = 0.25f) { kick = juce::jmin(1.0f, kick + v); }
    void poke(int g, float v = 1.0f) { boost[g] = juce::jmin(3.0f, boost[g] + v); }
    void noteOn()
    {
        noteTarget = 1;
        pdEnv = 1;
        bump(0.3f);
    }
};

// ---------------------------------------------------------------------------------------------- the art
struct Kit // one sprite of the control kit, at the screen's scale
{
    Bitmap bmp, halo;
    float w{0}, h{0}; // design px
    FPoint pivot;     // design px inside the sprite
    int haloPad{0};   // device px the halo is larger by, each side
};

struct TextStyle
{
    float size{7.8f}; // design px (the CSS font-size of the mock-up)
    bool semi{true};
    float track{0.05f}; // em
    juce::Colour col{0xffe6c98a};
    bool shadow{true};
    juce::Colour glow{0x00000000}; // an amber halo (a label that was just touched, the lit page sign)
};
struct TextSprite
{
    Sprite spr;
    float w{0};   // advance width, design px
    float pad{0}; // design px of margin round the glyph box (shadow)
};

struct Assets
{
    explicit Assets(int scale);
    const int S;
    juce::var layout, common, kitJson;
    juce::StringArray pageKeys;   // ui-layout.json "page_order": ghosts, main, morph (and any page added there later)
    std::vector<juce::var> pages; // each page's layers.json
    int pageCount() const { return pageKeys.size(); }
    static juce::String resolvePath(const juce::String &dir, const juce::String &file); // "fx" + "../common/x.png" -> "common/x.png"
    juce::Typeface::Ptr medium, semibold;

    static bool has(const juce::String &path);
    static juce::String textFile(const juce::String &path);
    static juce::Image image(const juce::String &path);           // an embedded PNG as it is stored (2x), for parts drawn by JUCE (the SPYGLASS window)
    static juce::Typeface::Ptr typeface(bool semi);                 // Poppins, loaded once
    juce::var subJson;                                              // common/sub/sub.json: the SUB sign and panel (scripts/ghostblock/make_sub_art.py)
    juce::var spyJson;                                              // common/spyglass/spyglass.json: the SPYGLASS and the size grip (painted by spyglass.py)
    Bitmap bitmap(const juce::String &path, double pngScale) const; // design size x S
    const Kit &kit(const juce::String &name);
    juce::Font font(float designPx, bool semi, float trackEm, float hscale) const;
    float textWidth(const juce::String &t, const TextStyle &st) const; // design px
    const TextSprite &text(const juce::String &t, const TextStyle &st, float squeeze = 1.0f);
    Sprite render(float designW, float designH, const std::function<void(juce::Graphics &)> &draw) const; // paint a small part once
    Bitmap renderBitmap(float designW, float designH, const std::function<void(juce::Graphics &)> &draw) const;

    std::map<juce::String, Kit> kits;
    std::unordered_map<std::string, TextSprite> texts;
    std::map<juce::String, Sprite> made; // procedural parts by key (lamps, seats, backlights)
};

// ---------------------------------------------------------------------------------------------- composing
struct Canvas // an opaque frame at device pixels, with JUCE's software renderer on it when paths / text are needed
{
    juce::Image img;
    Surface s;
    void alloc(int w, int h);
};

struct Ctx
{
    Surface dst;
    int S{1};
    const std::vector<IRect> *clips{nullptr}; // device px, disjoint
    IRect bounds;                             // their bounding box
    juce::Graphics *g{nullptr};               // design coordinates, already clipped to `clips`
    const uint8_t *dim{nullptr};              // storm: the painted (not the lit) layers
    bool touches(FRect designRect) const;
    void sprite(const Sprite &s, float x, float y, float alpha = 1.0f, Blend b = Blend::Over, const uint8_t *lut = nullptr) const;
    // `t` maps the bitmap's own design coordinates (0..w/S, 0..h/S) onto the canvas
    void affine(const Bitmap &b, const juce::AffineTransform &t, float alpha = 1.0f, Blend bl = Blend::Over, float gain = 1.0f) const;
    void scaled(const Bitmap &b, float x, float y, float scale, float alpha, Blend bl, bool mirror = false) const; // scale: design px per bitmap px
    void fill(FRect r, juce::Colour c, float alpha) const;
};

// ---------------------------------------------------------------------------------------------- the world behind the machine
struct World
{
    // dir / list: "common" + common/layers.json "world", or a page that has a world of its own (FX)
    void build(Assets &as, const juce::String &dir, const juce::var &list);
    void reset();
    // moves the sky on; marks the cells whose world picture changed (the caller drops the ones the machine hides)
    void tick(const Env &env, float dt, CellSet &dirty);
    void compose(const Surface &w, const CellSet &cells); // (cells: already without the ones the machine hides)
    bool strike(double now, juce::Random &rng); // lightning (never more than one sequence in 1.5 s); false = too soon
    float strikeLevel(double now) const;        // 0..1 over the 620 ms of a sequence
    void markAll(CellSet &dirty) const { dirty.fill(); }

    int S{1};
    Bitmap sky, skyLit, rays, flash; // skyLit = the sky with the sun shafts baked in, valid in the cells of litValid
    CellSet litValid;
    Sprite land, grass[4];
    struct Deck
    {
        Sprite spr;
        float speed{0}, wrap{2080}, offset{0}, drawn{-1e9f};
        std::vector<std::pair<int16_t, int16_t>> occ; // cells (x in deck space, y) that hold cloud
    } deck[3];
    struct Bolt
    {
        Sprite spr;
        float x{0}, y{0};
        std::vector<uint16_t> cells;
    } bolt[5];
    std::vector<uint16_t> flashCells, raysCells, grassCells, landCells;
    float raysMin{0.6f}, raysMax{1.0f}, raysPeriod{9.5f};
    float grassPeriod{3.6f};
    // what is baked into the world frame now
    float bakedRays{-1}, bakedStorm{-1}, bakedDeckAlpha{-1}, bakedGrass{-1}, bakedStrike{0};
    double grassPh{0}, lastStrike{-99}, sinceSky{0}, sinceGrass{0};
    int boltIx{-1};
    float raysNow{0.8f}, stormNow{0}, deckAlphaNow{1}, strikeNow{0}, boltNow{0};
    uint8_t landLut[256];
    bool calm{false};
};

// ---------------------------------------------------------------------------------------------- one page of the machine
struct LayerNode
{
    enum Kind
    {
        Static,
        Rotate,
        Sway,
        Frames,
        Pump,
        Flap,
        Link,
        Flicker,
        Flare,
        Level,
        Select,
        Corner,
        Beam,
        Pendulum,
        Drop,
        Bellows,
        Strike,
        Skip
    };
    juce::String id;
    Kind kind{Static};
    float x{0}, y{0}, w{0}, h{0}; // design px
    bool light{false};            // "blend": "add"
    bool hidden{false};
    Sprite spr;                 // the kinds that stay where they are
    std::vector<Sprite> frames; // film strips
    Bitmap bmp;                 // the kinds that turn, swing or stretch
    // animation numbers from layers.json
    float cx{0}, cy{0}, period{1}, dir{1}, amp{0}, bias{0}, travel{0}, hingeY{0}, open0{1}, open1{1}, pinX{0}, pinY{0};
    float minA{0}, maxA{1}, gain{1}, crankR{0}, anchorY{0}, sq0{1}, sq1{1}, ax{0}, ay{0}, bx{0}, by{0};
    int op{-1}, corner{-1}, group{gNone}, style{0}, source{0}, nframes{1};
    bool crank{false};
    // where it is now, and what is on the screen
    double ph{0};
    float alpha{1}, drawnAlpha{-1};
    juce::AffineTransform xf, drawnXf;
    int frame{0}, drawnFrame{-1};
    float ty{0}, drawnTy{-1e9f};
    std::vector<uint16_t> cells, home; // what it covers now / at rest
    FRect content;                     // where its bitmap has pixels (its own design coordinates)
    bool slow{false};                  // a big part: drawn again 15 times a second at most
    double since{0};
    juce::String cls; // (a file still to load)
};

struct Emitter
{
    float x{0}, y{0}, size{1}, dx{0}, dy{0}, rate{1};
    int kind{0}, op{-1}, voice{-1}, pole{-1};
    bool fxin{false};
    double acc{0};
};
enum PuffKind : int
{
    pkSmoke = 0,
    pkBlack,
    pkSteam,
    pkBreath,
    pkStream,
    pkSpark,
    pkDrip
};
struct Particle
{
    int kind{0}, img{0};
    float x{0}, y{0}, tx{0}, ty{0}, base{10}, sx{1}, grow{1.7f}, peak{0.7f}, dur{3}, age{0}, vx{0}, vy{0}, op{0};
    FRect drawn;
};

struct Stage
{
    void build(Assets &as, int page);
    void tick(Env &env, float dt, CellSet &dirty, juce::Random &rng, float strike, bool particlesOn);
    void drawBack(const Ctx &c) const;  // the layers under the controls
    void drawFront(const Ctx &c) const; // the lip, the lightning's rim light, the particles
    void puff(float x, float y, int kind, float size, float dx, float dy, const Env &env, juce::Random &rng);
    void setFace(int f, CellSet &dirty); // GHOSTS: 1 = the ghost bench, 2 = the link bench
    LayerNode *find(const juce::String &id);

    int page{0}, S{1};
    Assets *as{nullptr};
    std::vector<LayerNode> nodes; // in z order
    int hookAt{-1};               // before this node drawBack calls `hook` (the page draws something of its own there)
    std::function<void(const Ctx &)> hook;
    int uiAt{0};                  // nodes[0 .. uiAt) are under the controls
    Sprite lip;
    float lipX{0}, lipY{0};
    std::vector<Emitter> emitters;
    std::vector<Particle> parts;
    std::vector<Bitmap> puffs; // 0..2 steam, 3..5 smoke, 6..8 black smoke
    CellSet covered;           // cells the painted hull hides completely: no sky to draw there
    double phi{0}, belPh{0}, penPh{0}, beamPh{0};
    float strikeDrawn{0};
    int face{1};
};

// ---------------------------------------------------------------------------------------------- controls
enum class CType
{
    Wheel,
    Selector, // a stepped dial with a pointer
    Drum,     // a selector without a dial: a window that shows the option
    Switch,
    Plunger,
    Lever,
    Sign,
    Gauge,
    Plate,
    Chamber,
    Socket,
    Lantern,
    Lens,
    Pin,
    Keys,
    Spyglass, // the brass telescope on the jib: opens the SPYGLASS window
    Grip,     // the brass corner grip: resizes the plugin window
    Other
};
enum LCls : int
{
    lcBase = 0,
    lcName,
    lcStatus,
    lcBTitle,
    lcNum,
    lcState,
    lcSign,
    lcDrum,
    lcSnd,
    lcBtn,
    lcTag,
    lcWt,
    lcTiny,
    lcVt,
    lcLt,
    lcLv
};

// what a control is for (set from its id when it is built)
enum Role : int
{
    R_None = 0,
    R_Fixed, // a wheel / dial / switch on one fixed engine parameter (MAIN, the macros, the chambers' mix valves)
    R_Macro,
    R_ChMix,
    R_Chamber,
    R_SockOut,
    R_SockIn,
    R_ChNum,
    R_ChVu,
    R_ChTag,
    R_BenchTitle,
    R_BenchLamp,
    R_GWave,
    R_GAwake,
    R_GRatio,
    R_GOctave,
    R_GOut,
    R_GPan,
    R_GFb,
    R_GEnvAmt,
    R_GEnv,
    R_GTab,
    R_GEnvOn,
    R_GScope,
    R_LMode,
    R_LLinked,
    R_LDepth,
    R_LLfo,
    R_LEnvAmt,
    R_LView,
    R_LEnv,
    R_LBreak,
    R_LEnvOn,
    R_LDrive,
    R_MEnv,
    R_Asm,
    R_Puck,
    R_Lantern,
    R_MorphOn,
    R_OSize,
    R_Orbit,
    R_Rate,
    R_Spectrum,
    R_CornerSound,
    R_CornerVol,
    R_CornerPan,
    R_CornerOct, // MORPH: a corner's OCTAVE (-3 .. +3)
    R_PickName,
    R_Prev,
    R_Next,
    R_Folder,
    R_Save,
    R_Load,
    R_Init,
    R_Title,
    R_Page,
    R_OutGauge,
    R_Voices,
    R_Status,
    R_Keys,
    R_Theme,
    R_Calm,
    R_Ledger,
    R_Spyglass,
    R_Grip,
    R_Sub // the SUB sign: opens / closes the SUB panel
};
constexpr int kSubPage = 99; // the "page" of the SUB panel's parts: on screen while the panel is open, whatever the page

struct Ctl
{
    juce::String id, name, cls, spriteName, group;
    CType type{CType::Other};
    int role{R_None};
    Param *fixed{nullptr};
    int page{-1}; // -1 = on every page
    int face{0};  // GHOSTS bench: 0 both, 1 ghost, 2 link
    FRect rect, hit, label, lamp;
    bool hasLabel{false}, hasLamp{false}, hasCentre{false}, interactive{false}, two{false};
    FPoint centre, pivot;
    int op{-1}, corner{-1}, index{-1};
    int fx{0}, slot{-1}; // FX page: what it is there (FxKind), which slot
    float artR{0}, sprScale{1}, glowSize{0};
    LCls lcls{lcBase};
    // what it shows now
    float v01{0};
    int step{0}, steps{0};
    bool on{false}, labOff{false}, lit{false};
    juce::String t1, t2;
    juce::Colour colour{0x00000000}; // label colour override (ghost / corner colours)
    float angle{0};                  // degrees
    // light
    bool hover{false}, held{false};
    double flashAt{-1e9}, pressAt{-1e9};
    float glowDrawn{-1}, flashDrawn{-1}, animDrawn{-99}, extraDrawn{-99};
    std::vector<uint16_t> cells;
    FRect area; // everything it paints (design px)
};

// what a part of the FX page is (SPECTRAL FX, fx.cpp)
enum FxKind : int
{
    FX_None = 0,
    FX_On,       // master
    FX_Mix,
    FX_Fft,
    FX_Window,
    FX_Overlap,
    FX_Limit,
    FX_ChainMix,
    FX_Order,
    FX_SlotOn,
    FX_SlotMix,  // of the slot on the bench
    FX_P,        // bench wheel `index`: parameter page * 8 + index of the slot's effect
    FX_WarpOn,
    FX_Warp,     // WarpCore's parameter `index`
    FX_Glass,    // (from here on: no value of their own)
    FX_Name,
    FX_Title,
    FX_Page,
    FX_In,
    FX_Out,
    FX_WGauge,
    FX_From,
    // ---- the SUB panel (sub-ui.cpp): the same plumbing, another engine (src/ghostblock/sub)
    FX_Sub,       // SUB's host parameter `index`
    FX_SubPreset, // one of the eight preset plates
    FX_SubClose,
    FX_SubTitle,
    FX_SubRead
};
struct FxV // a value of the rack as a control sees it
{
    bool ok{false};
    uint32_t id{0};      // the host parameter (0: an editor-only parameter of an effect)
    int slot{-1}, p{-1}; // a parameter of a slot's effect (real units)
    float lo{0}, hi{1}, def{0}, val{0};
    bool stepped{false};
    const spectral::ParamDesc *d{nullptr};
};

struct Hose
{
    bool fb{false};
    int src{0}, dst{0};
    FPoint g[4], cur[4]; // the bezier at rest / as drawn (it sways)
    float vt{0.55f};     // where its valve rides
    FPoint vq;
    float ph{0}, dash{0}, pulse{0}, level{0};
    bool dragging{false}, bad{false};
    int target{-1};
    std::vector<uint16_t> cells;
    float drawnSway{-99}, drawnDash{-99}, drawnAlpha{-99}, depthDrawn{-1};
    Sprite body;       // the hose itself, painted once for the shape it has now (the pulse and the plugs go over it)
    FPoint bodyAt;
    int bodyKey{-1};   // what it was painted for (selected / refused / whose cloth)
    bool dirty{true};
};

struct LedgerItem
{
    enum Kind
    {
        None,
        Factory,
        User,
        This,
        Copy,
        Header,
        Fx // an effect of the FX page's ledger (`fxIndex` in spectral::types())
    } kind{None};
    int fxIndex{0};
    juce::String text, hint;
    std::string cat, file;
    fs::path user;
};

struct Ledger
{
    enum Mode
    {
        Closed = 0,
        Sounds,
        Yours,
        CornerPick,
        FxPick // the FX page: the effect of slot `corner`
    } mode{Closed};
    int corner{0}, tab{0}, pageIx{0}, tabScroll{0}, hoverTab{-1}, hoverSlat{-1}, hoverFoot{-1};
    float x{336}, y{112};
    double openedAt{0};
    float reveal{0}, revealDrawn{-1};
    juce::StringArray tabs;
    std::vector<LedgerItem> items;
};

// ---------------------------------------------------------------------------------------------- the editor's inside
struct MachineEditor::Impl
{
    Impl(MachineEditor &e);
    ~Impl();

    MachineEditor &ed;
    ss::Patch &P;
    std::unique_ptr<juce::PropertiesFile> prefs;
    std::unique_ptr<ss::presets::PresetManager> pm, cornerPm;
    std::unique_ptr<juce::LookAndFeel> lnf;

    // ---- the picture
    int S{0}; // device pixels per design pixel (1 or 2); 0 = not built yet
    std::unique_ptr<Assets> as;
    Canvas scene, worldBuf, snap;
    std::vector<juce::Image> tiles;
    int tileCols{0}, tileRows{0};
    juce::Image front;    // the tiles in one picture, for a window not at 100 % (the page change's cloud, the rain)
    bool frontStale{true};
    static constexpr int kTile = 256;
    World world;                     // the common world (GHOSTS, MAIN, MORPH)
    std::vector<std::unique_ptr<World>> pageWorld; // a page with a world of its own (FX: the dark storm sky)
    World &curWorld() { return page < (int)pageWorld.size() && pageWorld[(size_t)page] ? *pageWorld[(size_t)page] : world; }
    std::vector<Stage> stage;     // one per page of ui-layout.json's "page_order" (0 GHOSTS, 1 MAIN, 2 MORPH, then any added there)
    std::vector<char> stageBuilt;
    int pageCount() const { return (int)stage.size(); }
    Env env;
    juce::Random rng{0x90571};
    CellSet sceneDirty, frontDirty;
    std::vector<IRect> rectTmp;
    uint8_t dimLut[256], gradeR[256], gradeG[256], gradeB[256];
    bool dimOn{false}, gradeOn{false};
    float dimDrawn{-1};
    Bitmap bank, rain;
    Sprite rainSpr;
    float rainX{0}, rainY{0}, rainAlpha{0}, rainDrawn{0};
    float rainAcc{0};
    bool rainMoved{false}; // the rain steps 15 times a second (every step repaints the whole picture)
    // page change: the cloud crosses, the pages cross-fade behind it
    int page{0}, pageFrom{0};
    double turnAt{-1e9};
    bool turning{false}, swapped{true};
    double rippleAt{-1e9};
    bool rippleDone{true};
    // clock
    double lastTick{0}, clock{0};
    double lastTouchMs{0}; // when the pointer last did something here (an attended window animates at the full rate)
    int costHigh{0};
    bool testClock{false}, testRun{false}; // testRun: tick on the real clock even without a window (the offline 'live' check)
    double sinceAmbient{0};
    int ambientHz{15};
    double costAvg{0}; // ms of composing per second of wall clock (the governor)
    double lastPaintMs{0}, lastTickMs{0}, costAcc{0}, costTime{0}, ambAcc{0}, bgAcc{0}, nextStrike{1.0};
    long long cellsComposed{0}, worldCells{0}, cellsBy[4]{}; // (by: stage, hoses, lights, rest)
    double prof[10]{}; // ms: 0 world tick, 1 world compose, 2 stage tick, 3 hoses, 4 lights+scope, 5 copy+back, 6 controls, 7 front+grade, 8 text+ledger, 9 tiles
    bool worldAll{true};
    float wheelAcc{0};
    juce::Component::SafePointer<juce::AlertWindow> dialog;

    void build(int scale);
    void ensurePage(int pg);
    void tick(double dt);          // the machine's clock
    void compose(bool fromPaint);  // dirty cells -> scene -> tiles
    void present(juce::Graphics &g, float physScale);
    void markAllDirty();
    void setTheme(int t, bool save);
    void setCalm(bool on, bool save);
    void startTurn(int toPage);
    void strike();

    // ---- selection and pages
    int sel{0}, selLink{-1}, envTab{0};
    int face() const { return selLink >= 0 ? 2 : 1; }
    void setPage(int pg, bool instant);
    void selectGhost(int op, bool quiet);
    void selectLink(int from, int to, bool quiet);

    // ---- controls (ui.cpp)
    std::vector<Ctl> ctls;
    std::map<juce::String, int> ctlIx;
    std::vector<Hose> hoses;
    int dragHose{-1};
    FPoint dragPt;
    int litSrc{-1}; // a hose is being dragged from this ghost: its legal sockets burn
    juce::String statusText;
    double statusAt{0};
    float statusOver{0}, statusShift{0}, statusShiftDrawn{0};
    juce::String panelTitle;
    juce::String macroHint[ss::numMacros];
    juce::String versionLine;
    std::vector<uint16_t> envCells[3], scopeCells, specCells, morphCells, statusCells, linkViewCells, benchCells;
    struct EnvGeom
    {
        float xs[7]{}, ys[7]{}, yb{0}, ht{0};
        FPoint pin[5];
        bool on{true};
    } envGeom[3]; // g-env, l-env, m-env
    juce::Path scopePath;
    bool scopeLive{false};
    std::vector<float> specSmooth = std::vector<float>(96, -90.f);
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fftBuf;
    juce::Path specPath, specSill;
    FPoint puckDrawn{-1, -1};
    uint32_t lastMorphChange{0xffffffffu};
    float cornerW[Morph::N]{};
    bool cornerAny{false};
    int keysDown[128]{};     // > 0 while a key of the on-screen keyboard is held
    int hoverCtl{-1}, hoverHoseValve{-1}, hoverPin{-1}, hoverKey{-1};
    Ctl *find(const juce::String &id);
    const Ctl *find(const juce::String &id) const;
    void buildControls();
    void resolveOverlaps();
    bool visible(const Ctl &c) const; // on this page, this face
    Param *paramOf(const Ctl &c);     // the engine parameter it edits now (nullptr: MORPH or an action)
    void refresh();                   // every control reads the patch; what changed is marked dirty
    void refreshCtl(Ctl &c);
    void refreshEnv();
    void refreshMorph();
    void refreshMacros();
    void refreshWeather();
    void touch(Ctl &c);                       // dirty: its cells before and after
    void flash(const juce::String &id);       // touched: a light that fades over ~600 ms
    void ripple();                            // "find it": every control flashes once in a wave from the face
    void tickLights(double dt);               // lights, needles, the status ticker, press animations
    void drawControlsArt(const Ctx &c);       // glows, moving parts, hoses, rails
    void drawControlsText(const Ctx &c);      // live text (after the theme's grade: text keeps its colour)
    void drawCtlArt(const Ctx &c, Ctl &k);
    void drawCtlText(const Ctx &c, Ctl &k);
    void drawLabel(const Ctx &c, FRect r, LCls cls, bool two, const juce::String &a, const juce::String &b, float light, bool hov, bool on, bool off,
                   float flash, juce::Colour over);
    void drawBacklight(const Ctx &c, FRect r, float level, bool green);
    void drawGlow(const Ctx &c, const juce::String &kit, FPoint at, float size, float level);
    float glowLevel(const Ctl &k, float &flashOut) const;
    juce::String valueText(Param &p, float v) const;
    juce::String compact(juce::String s) const;
    FRect ctlArea(const Ctl &k) const;
    // envelopes
    Param *envParam(int which, int k); // which: 0 g-env, 1 l-env, 2 m-env; k: delay attack hold decay sustain release
    Param *envPower(int which);
    void drawEnv(const Ctx &c, int which, const Ctl &k);
    void drawScope(const Ctx &c);
    void drawLinkView(const Ctx &c);
    void drawMorph(const Ctx &c, bool text);
    void updateScope();
    void updateSpectrum();
    // hoses
    void rebuildHoses();
    void layoutHoses();
    void tickHoses(double dt);
    void drawHose(const Ctx &c, Hose &h, bool text);
    void hoseCells(Hose &h);
    float hoseDepth(const Hose &h) const;
    Param *hoseParam(const Hose &h);
    void startHose(int src, FPoint at, float keepDepth);
    void moveHose(FPoint at);
    void endHose(FPoint at);
    float dragKeep{-1};
    // engine helpers (as the first editor)
    static int posOf(int f, int t);
    bool linked(int f, int t) const;
    bool awake(int o) const;
    void wake(int o, bool mixer, bool self);
    // presets
    int factoryIndex() const;
    juce::String currentFolder() const;
    void loadFactory(int i);
    void stepPreset(int d);
    void nextFolder(int d);
    void saveSound();
    void typeIn(Ctl &c);
    void popupFor(Ctl &c);
    void setCornerSound(int corner, const LedgerItem &it);
    void afterCorner(int c);
    // the ledger (ledger.cpp)
    Ledger ledger;
    void openLedger(Ledger::Mode m, int corner);
    void closeLedger();
    void ledgerItems();
    void ledgerPick(const LedgerItem &it);
    void drawLedger(const Ctx &c);
    bool ledgerDown(FPoint p);
    void ledgerHover(FPoint p);
    void ledgerWheel(FPoint p, float dy);
    FRect ledgerArea() const;
    int ledgerPages() const;
    void touchLedger();

    // ---- the FX page (fx.cpp): SPECTRAL FX
    int benchSlot{0}, benchPage{0}; // the slot on the workbench (-1: none, the bench holds the chain's own settings)
    uint32_t fxChange{0xffffffffu};
    static constexpr int kFxBars = 14;
    float fxBars[kFxBars]{}, fxBarsDrawn[kFxBars]{}, fxSlotLv[spectral::kSlots]{}, fxSlotLvDrawn[spectral::kSlots]{};
    std::vector<float> fxMags;
    double fxGlassAcc{0};
    float fxDash{0}, fxDashDrawn{0}, fxDashAlpha{0}, fxOrderDrawn{0};
    std::vector<uint16_t> fxGlassCells, fxHoseCells;
    void fxBuildControls();
    void fxHoseGeom(FPoint g[4]) const;
    bool fxValue(const Ctl &c, FxV &v) const;
    float fxNorm(const FxV &v, float real) const;
    float fxReal(const FxV &v, float n01) const;
    void fxSet(const Ctl &c, float real, int phase);
    juce::String fxText(const FxV &v, float real) const;
    juce::String fxName(const Ctl &c) const;
    juce::String fxLatencyText() const;
    int fxBenchPages() const;
    bool fxRefresh(Ctl &c);
    bool fxClick(Ctl &c);
    void fxSelectSlot(int s);
    void fxSetSlotType(int slot, int type);
    void fxTick(double dt);
    float fxGaugeAngle(const Ctl &k) const;
    void fxDraw(const Ctx &c);
    void fxDrawHose(const Ctx &c);

    // ---- the SUB sign and its panel (sub-ui.cpp)
    bool subOpen{false};
    FRect subRect;
    std::vector<uint16_t> subCells;
    uint32_t subChange{0xffffffffu};
    bool subOnDrawn{false};
    void subBuildControls();
    void subToggle(bool open);
    bool subValue(const Ctl &c, FxV &v) const;
    bool subRefresh(Ctl &c);
    bool subClick(Ctl &c);
    void subTick(double dt);
    void subDraw(const Ctx &c);
    juce::String subReadout() const;

    // ---- the BACK ROOM (backroom.cpp): the wiring, through the ghost's eyes / mouth
    struct BackCell
    {
        int from{0}, to{-1}, kind{0}; // kind: 0 a link (FROM haunts TO), 1 FROM into itself, 2 how loud FROM is heard
        FPoint c;
        std::vector<uint16_t> cells;
        bool on{false};
        int drawn{-9999};
        float lv{0};
    };
    bool backOpen{false};
    juce::var backJson;
    Sprite backSpr;
    std::vector<BackCell> backCells;
    FRect backExit, backTitle, backHint, backOutHead, backLegend, backHead[2][ss::numOps], backDoors[3];
    int backDoorCount{0}, backHover{-1}, backHeld{-1}, doorHover{-1};
    uint32_t backAwake{0xffffffffu};
    double backAcc{0};
    std::vector<uint16_t> backHintCells, doorCells;
    void setCornerOct(int corner, int oct);
    void backBuild();
    void backToggle(bool open);
    void backTick(double dt);
    void backDraw(const Ctx &c);
    void backText(const Ctx &c, FRect r, const juce::String &t, float size, bool semi, juce::Colour col, juce::Colour glow, float track, float alpha = 1.0f);
    int backAt(FPoint p) const;
    void backHoverAt(FPoint p);
    bool backDown(const juce::MouseEvent &e, FPoint p);
    bool backWheel(FPoint p, float dy);
    Param *backParam(const BackCell &c);
    bool backActive(const BackCell &c) const;
    juce::String backWhat(const BackCell &c) const;
    int backShown(const BackCell &c);
    void backPlug(BackCell &c);
    void backUnplug(BackCell &c);
    bool doorPage() const;
    int doorAt(FPoint p) const;
    void setDoorHover(int d);
    void drawDoors(const Ctx &c);

    // ---- STUDIO VIEW (studio.cpp): a plain, flat screen for the whole plugin
    struct StW
    {
        enum K
        {
            Knob,
            Fader,
            Bar,
            Toggle,
            Choice,
            Button,
            Tab,
            Seg,  // one of a small row of choices (a ghost column's LEVEL / FB / PITCH)
            Cell, // a window of the wiring grid (backCells[cell])
            XY    // the MORPH blend pad
        } k{Knob};
        FRect r, group; // group: the envelope box the bar's shape is drawn over
        Param *p{nullptr};
        std::function<Param *()> pf; // a parameter that follows a choice (the envelope shown, the wire chosen)
        int ctlIx{-1};               // one of the machine's own controls, drawn flat
        int cell{-1};
        juce::String label, tip;
        juce::Colour col;
        int tab{-1}, op{-1}, tabIx{-1};
        bool big{false}, accent{false}, small{false}, bip{false};
        std::function<bool()> enabled, lit, visible;
        std::function<void()> act, onEdit;
        std::function<juce::String()> text;
        std::vector<uint16_t> cells;
        uint64_t sig{~0ull};
    };
    struct StPanel
    {
        int tab;
        FRect r;
        juce::String title;
        juce::Colour col;
    };
    static constexpr int kLoKey = 36, kHiKey = 96, kKeysY = 650;
    static constexpr float kWireX = 72, kWireY = 132, kWireP = 64;
    bool studioOpen{false};
    std::vector<StW> stW;
    std::vector<StPanel> stPanels;
    int stTab{0}, stHover{-1}, stHeld{-1}, stKey{-1}, stSelCell{-1};
    std::array<int, ss::numOps> stEnvSel{};
    uint64_t stKeysSig{0};
    uint32_t stAwake{0xffffffffu};
    FRect stStatusR{8, 626, 1024, 20}, stPad{336, 104, 368, 300};
    bool stButtonHover{false}, stInit{false};
    void pressCtl(Ctl &c, const juce::MouseEvent &e, FPoint xy);
    void studioBuild();
    void studioToggle(bool open);
    void studioTick(double dt);
    void studioDraw(const Ctx &c);
    void studioDrawW(const Ctx &c, StW &w, int i);
    void stText(const Ctx &c, FRect r, const juce::String &t, float size, bool semi, juce::Colour col, int just, float alpha = 1.0f);
    bool studioEnabled(const StW &w) const;
    bool stVisible(const StW &w) const;
    float studioNorm(const Param &p) const;
    Param *stParam(const StW &w) const;
    Ctl *stCtl(const StW &w);
    const Ctl *stCtl(const StW &w) const;
    Param *stEnvParam(int o, int k);
    float stNorm(StW &w);
    juce::String stValText(StW &w);
    juce::String stLabel(StW &w);
    bool stOn(StW &w);
    float x0Of(int o) const;
    float colW() const;
    uint64_t studioSig(StW &w);
    static bool studioBlack(int n);
    int studioWhiteCount() const;
    FRect studioKeyRect(int note) const;
    int studioKeyAt(FPoint p) const;
    int studioAt(FPoint p) const;
    void studioHoverAt(FPoint p);
    void studioEditTo(StW &w, float v);
    void studioMenu(int i);
    bool studioDown(const juce::MouseEvent &e, FPoint p);
    bool studioWheel(FPoint p, float dy);
    FRect studioDoor() const;
    bool studioButtonAt(FPoint p) const;
    void drawStudioButton(const Ctx &c);

    // ---- pointer (ui-input.cpp)
    struct Drag
    {
        std::function<void(FPoint, const juce::MouseEvent &)> move;
        std::function<void(FPoint, const juce::MouseEvent &)> up;
        bool active{false};
    } drag;
    juce::String lastTapId;
    double lastTapAt{0};
    int heldKeyNote{-1};
    std::array<bool, 32> typed{};
    void down(const juce::MouseEvent &e);
    void move(const juce::MouseEvent &e);
    void hoverAt(FPoint p, bool inside);
    int ctlAt(FPoint p) const;
    int keyAt(FPoint p) const;
    void noteOn(int note, float vel);
    void noteOff(int note);
    void allNotesOff();
    void wheelDrag(Ctl &c, const juce::MouseEvent &e, FPoint at, bool dbl);
    void setCtl01(Ctl &c, float v, int phase, bool free);
    float getCtl01(Ctl &c);
    float defaultCtl01(Ctl &c);
    void stepSelector(Ctl &c, int to);
    int selectorCount(Ctl &c);
    int selectorIndex(Ctl &c);
    juce::String selectorText(Ctl &c, int i);
    bool switchOn(Ctl &c);
    void setSwitch(Ctl &c, bool on);
    void pressPlunger(Ctl &c);
    void pullLever(Ctl &c);
    void vent(const Ctl &c);
    void touchCtl(Ctl &c, bool strong);
    void spyglassChanged(); // the SPYGLASS window opened or closed: its telescope stays lit while it is open
};

// helpers shared by the parts
extern const juce::Colour kAmber;
juce::LookAndFeel &brass(MachineEditor::Impl &I); // the brass dialogs' and menus' look (ledger.cpp)
const Sprite &pulseSprite(Assets &as, juce::Colour col); // one bead of the light that runs along a hose
juce::Colour ghostColour(int op);
juce::Colour cornerColour(int c);
juce::Colour clothColour(int op);
inline FRect rectOf(const juce::var &v)
{
    if (auto *a = v.getArray())
        if (a->size() >= 4)
            return {(float)(double)(*a)[0], (float)(double)(*a)[1], (float)(double)(*a)[2], (float)(double)(*a)[3]};
    return {};
}
inline FPoint pointOf(const juce::var &v)
{
    if (auto *a = v.getArray())
        if (a->size() >= 2)
            return {(float)(double)(*a)[0], (float)(double)(*a)[1]};
    return {};
}
inline float numOf(const juce::var &v, float dflt = 0.f) { return v.isVoid() || v.isUndefined() ? dflt : (float)(double)v; }
} // namespace ghostblock::p2::machine
