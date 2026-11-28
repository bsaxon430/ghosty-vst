#include "HostedPlugin.h"
#include <cstdio>
int main(){ juce::ScopedJuceInitialiser_GUI init;
 for (auto b : {"ArpEZator.vst3","Ghosty Beats.vst3","Ghost Graph.vst3"}) {
  juce::String e; auto t0=juce::Time::getMillisecondCounterHiRes(); auto p=ghost::Hosted::open(b,48000,512,e); auto t1=juce::Time::getMillisecondCounterHiRes();
  if(!p){printf("%s: %s\n",b,e.toRawUTF8());continue;}
  juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
  juce::MemoryBlock m; auto t2=juce::Time::getMillisecondCounterHiRes(); p->getStateInformation(m); auto t3=juce::Time::getMillisecondCounterHiRes();
  printf("%s: open %.0f ms, state %zu bytes in %.1f ms\n",b,t1-t0,m.getSize(),t3-t2);
 }}
