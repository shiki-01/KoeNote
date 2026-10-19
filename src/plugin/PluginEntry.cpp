#include "plugin/KoeNoteDocumentController.h"
#include "plugin/KoeNoteProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

// VST3 ラッパーが呼び出すプラグインの生成関数
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new koenote::plugin::KoeNoteProcessor();
}

#if JucePlugin_Enable_ARA
const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<koenote::plugin::KoeNoteDocumentController>();
}
#endif
