// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// The TERRAIN page: GhostBlock's shared landscape (ghosty-pro/Shared/Terrain, our own code) in
// Ghost Mosaic's colours, steering the matching (Source/Engine/TerrainShaping.h says how):
//   across     = frequency: where the land stands up, that band counts more when a piece is picked
//   front-back = time across the target: tall or rough land gets wild there, valleys stick close
// The page only talks to the component through its public API (TerrainView / TerrainEditor) and to
// the engine through Engine::setTerrain (the Processor does that on every change), so a new version
// of the shared component drops in without touching the mapping.
#pragma once
#include "Processor.h"
#include "TerrainView.h"
#include "TerrainShaping.h"

namespace mosaic
{
class TerrainPage : public juce::Component
{
public:
    explicit TerrainPage (Processor& p);
    void tick();                                   // the editor's frame clock
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override { if (! isShowing()) { m_feel = {}; m_proc.engine().setDrag (0, 0, 0); } }   // leaving the page mid-drag: the drag effect is gone, nothing is held
    ~TerrainPage() override { m_proc.engine().setDrag (0, 0, 0); }

    ghost::terrain::TerrainView& view() { return m_view; }
    juce::String readout() const { return m_readout; }
    static ghost::terrain::Palette palette();

    std::vector<Push> shownPushes() const { return m_pushes; }
    juce::ComboBox& shapes() { return m_shapes; }
    juce::Slider& movement() { return m_move; }
    juce::ToggleButton& onSwitch() { return m_on; }
    // MOVING NOW: the original controls TERRAIN is moving right now, with the value you set and the value in use
    struct Moving { juce::String name, set, now; };
    std::vector<Moving> shownMoving() const { return m_moving; }

private:
    juce::Rectangle<int> cardArea() const;
    juce::Rectangle<int> barsArea() const;
    Processor& m_proc;
    ghost::terrain::TerrainView m_view;
    juce::ComboBox m_shapes;
    juce::ToggleButton m_on { "ON" };
    juce::Slider m_move { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };      // MOVEMENT: how lively
    juce::ComboBox m_rate;                                                                   // MOVEMENT RATE: free / on the beat
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_moveAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> m_rateAtt;
    juce::String m_readout, m_live;
    std::vector<Push> m_pushes;
    std::vector<Moving> m_moving;
    juce::String m_soundsLine;
    // DRAG: the land a frame ago, and the feel the engine was last given (it decays a little here between drag events)
    ghost::terrain::Terrain m_lastLand;
    double m_lastT = 0;
    DragFeel m_feel;
public:
    DragFeel dragFeelNow() const { return m_feel; }
};
}
