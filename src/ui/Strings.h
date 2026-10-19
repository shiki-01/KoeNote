#pragma once

#include <juce_core/juce_core.h>

namespace koenote::ui
{
// UI 文字列（日本語・英語。非機能要件「操作性」）
enum class Language
{
    Japanese,
    English
};

struct Strings
{
    juce::String toolSelect, toolSplit, toolBoundary;
    juce::String snapOff, snapSemitone, snapScale;
    juce::String scaleMajor, scaleMinor;
    juce::String undo, redo, merge, reset, zoomFit, language;
    juce::String paramCenter, paramVibrato, paramDrift, paramTransition, paramPitch, paramNone;
    juce::String statusWaiting, statusAnalyzing, statusDone, statusFailed, statusRendering, statusNoNotes;
    juce::String notARA, noClip, notes;
    juce::String tipSelect, tipSplit, tipBoundary, tipMerge, tipReset;
};

const Strings& getStrings(Language language);
Language detectDefaultLanguage();
} // namespace koenote::ui
