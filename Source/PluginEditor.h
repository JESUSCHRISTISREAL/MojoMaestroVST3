#pragma once

#include <array>
#include <vector>
#include "PluginProcessor.h"

namespace vfdtext
{
    // one character position of the VFD: the character and whether its decimal point is lit
    constexpr int kCells = 13;
    struct Cell { char ch = ' '; bool dp = false; };
    using Cells = std::array<Cell, kCells>;
}

// Faceplate editor. The plate, knobs and switch caps are pre-rendered images (see art/),
// the pointers, switch positions and the VFD's segments are drawn live.
// Design space: 480 x 392 logical px, centre line x = 240. The window scales it.
class MojoMaestroEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit MojoMaestroEditor (MojoMaestroProcessor&);
    ~MojoMaestroEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /// For snapshots and tests: shows a control's read-out as if it were hovered (0 = none, 1 = Amount knob ... see Target).
    void previewReadout (int target) { hover = (Target) target; repaint(); }

private:
    enum ParamIndex { pMode, pHarm, pAmount, pRef, pTrim, pOs, pModel, pDc, numParams };
    enum class Target { none, mode, harm, knob, ref, trim, os, model, dc };

    struct Param
    {
        juce::RangedAudioParameter* param = nullptr;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        float norm = 0.0f;   // 0..1, kept in step with the host
    };

    void setupParam (ParamIndex, const char* id);
    void setNorm (Param&, float norm, bool partOfGesture);
    void setIndex (Param&, int index, bool partOfGesture);
    int  indexOf (const Param&) const;
    int  countOf (const Param&) const;
    Target hit (juce::Point<float> p) const;
    Param& paramFor (Target);
    float switchTargetY (int which) const;
    juce::String readoutName (Target);
    juce::String readoutValue (Target);
    void paintDisplay (juce::Graphics&);
    void captureDisplayBase();
    void renderDisplay (const vfdtext::Cells&, bool heart, float level);
    juce::Rectangle<int> vfdBounds() const;
    void applyDrag (juce::Point<float> p);
    void timerCallback() override;

    MojoMaestroProcessor& proc;
    std::array<Param, numParams> params;
    juce::ComponentBoundsConstrainer constrainer;

    juce::Image plate, knobBig, knobSmall, cap, ptrBig, ptrSmall;

    float capY[2] { 190.0f, 158.0f };     // displayed cap centres (mode, harmonics), eased towards their targets
    Target active = Target::none, hover = Target::none, lastShown = Target::none;
    juce::uint32 releasedAt = 0, idleStart = juce::Time::getMillisecondCounter();
    float startNorm = 0.0f;
    juce::Point<float> startPos;
    float scale = 1.0f;

    // lit-display rendering (see renderDisplay): the baked glass under the window, the fixed ghost-segment and mesh
    // layers, and the cached lit and glow maps of what is shown now
    std::vector<juce::uint8> vfdBase;
    std::vector<float> vfdLitMap, vfdGlowMap, vfdGhost, vfdMesh;
    std::array<unsigned short, vfdtext::kCells + 1> vfdKey {};
    bool vfdKeyValid = false;
    juce::Image vfdImage;
    int vfdX = 0, vfdY = 0, vfdW = 0, vfdH = 0;
    int vfdLevelQ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MojoMaestroEditor)
};
