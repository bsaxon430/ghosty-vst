#include "LayerEngines.h"

namespace ghost::layers
{
juce::String EngineBase::stateToString()
{
    auto xml = apvts().copyState().createXml();
    return xml ? xml->toString(juce::XmlElement::TextFormat().singleLine().withoutHeader()) : juce::String();
}
bool EngineBase::stateFromString(const juce::String &s)
{
    auto xml = juce::XmlDocument::parse(s);
    if (!xml || !xml->hasTagName(apvts().state.getType()))
        return false;
    apvts().replaceState(juce::ValueTree::fromXml(*xml));
    return true;
}
void EngineBase::setToDefaults()
{
    for (auto *p : apvts().processor.getParameters())
        p->setValue(p->getDefaultValue());
}
bool EngineBase::isDefault()
{
    for (auto *p : apvts().processor.getParameters())
        if (std::abs(p->getValue() - p->getDefaultValue()) > 1.0e-6f)
            return false;
    return true;
}
} // namespace ghost::layers
