# GHOST hosts the owner's plug-ins with JUCE's VST3 host. Ghosty Pro 2 (Vital's JUCE 6 wrapper) only accepts a layout whose
# input is OFF, but JUCE's host describes a bus that is off with its last layout (stereo), so Ghosty Pro 2 says no and
# comes up with no output. This adds a second try to JUCE's host: buses that are off are described as empty, and the
# check afterwards only compares the buses that are on. Nothing changes for a plug-in that accepts the first try.
set(_f "${juce_SOURCE_DIR}/modules/juce_audio_processors/format_types/juce_VST3PluginFormat.cpp")
file(READ "${_f}" _src)
if(NOT _src MATCHES "GHOST: second try")
  set(_old [=[        if (processor->setBusArrangements (inputArrangementData,  static_cast<int32> (inputArrangements .size()),
                                           outputArrangementData, static_cast<int32> (outputArrangements.size())) != kResultTrue)
            return false;

        // check if the layout matches the request
        const auto inArrActual  = getActualArrangements (true);
        const auto outArrActual = getActualArrangements (false);

        return (inArrActual == inputArrangements && outArrActual == outputArrangements);]=])
  set(_new [=[        if (processor->setBusArrangements (inputArrangementData,  static_cast<int32> (inputArrangements .size()),
                                           outputArrangementData, static_cast<int32> (outputArrangements.size())) != kResultTrue)
        {
            // GHOST: second try - the buses that are off described as empty (JUCE 6 plug-ins that want their input off)
            for (size_t i = 0; i < inputArrangements.size(); ++i)  if (layouts.getChannelSet (true,  (int) i).isDisabled()) inputArrangements[i]  = Vst::SpeakerArr::kEmpty;
            for (size_t i = 0; i < outputArrangements.size(); ++i) if (layouts.getChannelSet (false, (int) i).isDisabled()) outputArrangements[i] = Vst::SpeakerArr::kEmpty;
            inputArrangementData  = inputArrangements .empty() ? &nullArrangement : inputArrangements .data();
            outputArrangementData = outputArrangements.empty() ? &nullArrangement : outputArrangements.data();
            if (processor->setBusArrangements (inputArrangementData,  static_cast<int32> (inputArrangements .size()),
                                               outputArrangementData, static_cast<int32> (outputArrangements.size())) != kResultTrue)
                return false;
        }

        // check if the layout matches the request (GHOST: only the buses that are on)
        const auto inArrActual  = getActualArrangements (true);
        const auto outArrActual = getActualArrangements (false);
        const auto same = [&] (bool isInput, const std::vector<Vst::SpeakerArrangement>& actual, const std::vector<Vst::SpeakerArrangement>& wanted)
        {
            if (actual.size() != wanted.size()) return false;
            for (size_t i = 0; i < actual.size(); ++i)
                if (! layouts.getChannelSet (isInput, (int) i).isDisabled() && actual[i] != wanted[i]) return false;
            return true;
        };
        return same (true, inArrActual, inputArrangements) && same (false, outArrActual, outputArrangements);]=])
  string(FIND "${_src}" "${_old}" _at)
  if(_at EQUAL -1)
    message(FATAL_ERROR "GHOST: JUCE's VST3 host code is not as expected (PatchJuceHost.cmake)")
  endif()
  string(REPLACE "${_old}" "${_new}" _src "${_src}")
  # the same second try when it is prepared to play
  set(_old2 [=[        warnOnFailure (processor->setBusArrangements (inData,  static_cast<int32> (inArrangements .size()),
                                                      outData, static_cast<int32> (outArrangements.size())));]=])
  set(_new2 [=[        if (processor->setBusArrangements (inData,  static_cast<int32> (inArrangements .size()),
                                           outData, static_cast<int32> (outArrangements.size())) != kResultTrue)
        {
            // GHOST: second try - the buses that are off described as empty
            for (size_t i = 0; i < inArrangements.size(); ++i)  if (! getBus (true,  (int) i)->isEnabled()) inArrangements[i]  = Vst::SpeakerArr::kEmpty;
            for (size_t i = 0; i < outArrangements.size(); ++i) if (! getBus (false, (int) i)->isEnabled()) outArrangements[i] = Vst::SpeakerArr::kEmpty;
            inData  = inArrangements .empty() ? &nullArrangement : inArrangements .data();
            outData = outArrangements.empty() ? &nullArrangement : outArrangements.data();
            warnOnFailure (processor->setBusArrangements (inData,  static_cast<int32> (inArrangements .size()),
                                                          outData, static_cast<int32> (outArrangements.size())));
        }]=])
  string(FIND "${_src}" "${_old2}" _at2)
  if(_at2 EQUAL -1)
    message(FATAL_ERROR "GHOST: JUCE's VST3 host prepare code is not as expected (PatchJuceHost.cmake)")
  endif()
  string(REPLACE "${_old2}" "${_new2}" _src "${_src}")
  file(WRITE "${_f}" "${_src}")
  message(STATUS "GHOST: JUCE's VST3 host patched (second try for buses that are off)")
endif()
