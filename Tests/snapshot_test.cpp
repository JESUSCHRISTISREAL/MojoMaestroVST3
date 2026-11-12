// Paints the real editor offscreen and writes PNGs, so the look can be checked without a host. Also checks that the
// VFD shows what it should: text lit in every state, the heart only at the end of the idle message.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VfdGeometry.h"
#include <cstdio>

namespace
{
    constexpr float kSnap = 2.0f;                        // snapshots are taken at 2x of the 720 x 588 default window
    constexpr float kScale = 3.0f;                       // = 3 pixels per design px (window 1.5 x snapshot 2)
    // design-px layout of the display, as in PluginEditor.cpp
    constexpr float kWinX = 134.0f, kWinY = 269.0f, kWinW = 212.0f, kWinH = 28.0f;
    constexpr float kHeartW = 9.6f, kHeartGap = 4.8f;
    constexpr float kCellExtent = vfd::kGlyphW + vfd::kGlyphH * vfd::kShear + 1.9f;
    constexpr float kGroupW = 12.0f * vfd::kPitch + kCellExtent + kHeartGap + kHeartW;
    constexpr float kHeartCX = kWinX + (kWinW - kGroupW) * 0.5f + 12.0f * vfd::kPitch + kCellExtent + kHeartGap + kHeartW * 0.5f;
    constexpr float kHeartCY = kWinY + 14.2f;

    // pixels in a design-px rectangle that are clearly lit (white core)
    int litPixels (const juce::Image& img, float x, float y, float w, float h)
    {
        int n = 0;
        for (int py = (int) (y * kScale); py < (int) ((y + h) * kScale); ++py)
            for (int px = (int) (x * kScale); px < (int) ((x + w) * kScale); ++px)
            {
                const auto c = img.getPixelAt (px, py);
                if (c.getRed() > 170 && c.getGreen() > 170 && c.getBlue() > 170) ++n;
            }
        return n;
    }

    int failures = 0;
    void expect (bool ok, const char* what, int value)
    {
        std::printf ("%-52s %5d  %s\n", what, value, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshots");
    dir.createDirectory();

    MojoMaestroProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* editor = static_cast<MojoMaestroEditor*> (ed.get());

    auto save = [&] (const char* name)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, kSnap);
        juce::PNGImageFormat png;
        juce::FileOutputStream out (dir.getChildFile (name));
        out.setPosition (0); out.truncate();
        png.writeImageToStream (img, out);
        std::printf ("wrote %s (%d x %d)\n", name, img.getWidth(), img.getHeight());
        return img;
    };
    auto textLit  = [&] (const juce::Image& i) { return litPixels (i, kWinX, kWinY, kHeartCX - kHeartW * 0.5f - 2.0f - kWinX, kWinH); };
    auto heartLit = [&] (const juce::Image& i) { return litPixels (i, kHeartCX - 4.0f, kHeartCY - 4.0f, 8.0f, 8.0f); };

    juce::Thread::sleep (300);              // past the first fade-in
    auto img = save ("idle_glory.png");     // GLORY TO I AM
    expect (textLit (img) > 400, "idle GLORY: text lit", textLit (img));
    expect (heartLit (img) < 5,  "idle GLORY: heart dark", heartLit (img));

    juce::Thread::sleep (2700);             // 3.0 s: scrolling
    img = save ("idle_scroll.png");
    expect (textLit (img) > 400, "idle scroll: text lit", textLit (img));

    juce::Thread::sleep (2300);             // 5.3 s: scrolled to I AM WHO I AM, heart on
    img = save ("idle_iam.png");
    expect (textLit (img) > 400,  "idle I AM WHO I AM: text lit", textLit (img));
    expect (heartLit (img) > 40,  "idle I AM WHO I AM: heart lit", heartLit (img));

    // every control's read-out (1 Mode, 2 Harmonics, 3 Amount, 4 Reference, 5 Trim, 6 Oversample, 7 Model, 8 DC Block)
    const char* names[] { "", "mode", "harmonics", "amount", "reference", "trim", "oversample", "model", "dcblock" };
    for (int t = 1; t <= 8; ++t)
    {
        editor->previewReadout (t);
        img = save ((juce::String ("readout_") + names[t] + ".png").toRawUTF8());
        char what[64]; std::snprintf (what, sizeof what, "read-out %s: text lit", names[t]);
        expect (textLit (img) > 100, what, textLit (img));
        std::snprintf (what, sizeof what, "read-out %s: heart dark", names[t]);
        expect (heartLit (img) < 5, what, heartLit (img));
    }
    editor->previewReadout (0);
    juce::Thread::sleep (1700);             // read-out hold ends
    juce::Thread::sleep (7300);             // into the second message
    img = save ("idle_code.png");
    expect (textLit (img) > 400, "idle CODE BY SFOG7: text lit", textLit (img));
    expect (heartLit (img) < 5,  "idle CODE BY SFOG7: heart dark", heartLit (img));

    std::printf ("%s\n", failures == 0 ? "ALL OK" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
