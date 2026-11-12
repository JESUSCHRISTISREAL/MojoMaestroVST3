// Drives the real editor with simulated mouse events and reads the parameters back, then checks the
// processor's latency and Bypass behaviour. Prints PASS/FAIL lines and the measured numbers.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
static void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    std::printf ("%s  %s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8(), detail.toRawUTF8());
    if (! ok) ++failures;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    MojoMaestroProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* editor = static_cast<MojoMaestroEditor*> (ed.get());

    // ---- window ---------------------------------------------------------------------------------
    check (ed->getWidth() == 720 && ed->getHeight() == 588, "opens at 720 x 588",
           juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight()));
    auto* c = ed->getConstrainer();
    check (c != nullptr && c->getMinimumWidth() == 360 && c->getMinimumHeight() == 294
               && c->getMaximumWidth() == 1440 && c->getMaximumHeight() == 1176,
           "resize limits 360 x 294 to 1440 x 1176");
    {
        juce::Rectangle<int> b (0, 0, 900, 900);
        c->checkBounds (b, { 0, 0, 720, 588 }, { -10000, -10000, 20000, 20000 }, false, false, false, false);
        check (std::abs ((double) b.getWidth() / b.getHeight() - 480.0 / 392.0) < 0.01, "aspect ratio kept when resizing",
               juce::String (b.getWidth()) + " x " + juce::String (b.getHeight()));
    }

    const float scale = (float) ed->getWidth() / 480.0f;            // design px -> editor px
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    juce::Time t0 = juce::Time::getCurrentTime();
    auto ev = [&] (float dx, float dy, juce::Point<float> downDesign, int clicks, bool dragged)
    {
        return juce::MouseEvent (src, juce::Point<float> (dx, dy) * scale, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 ed.get(), ed.get(), t0, downDesign * scale, t0, clicks, dragged);
    };
    auto prm = [&] (const char* id) { return p.apvts.getParameter (id); };
    auto setNorm = [&] (const char* id, float n) { prm (id)->setValueNotifyingHost (n); };
    auto norm = [&] (const char* id) { return prm (id)->getValue(); };
    auto raw  = [&] (const char* id) { return p.apvts.getRawParameterValue (id)->load(); };
    auto press = [&] (float x, float y) { editor->mouseDown (ev (x, y, { x, y }, 1, false)); };
    auto dragTo = [&] (float x0, float y0, float x, float y) { editor->mouseDrag (ev (x, y, { x0, y0 }, 1, true)); };
    auto release = [&] (float x, float y) { editor->mouseUp (ev (x, y, { x, y }, 1, true)); };
    auto near = [] (float a, float b, float tol) { return std::abs (a - b) <= tol; };

    // ---- Mode slide switch: x = 96, positions 158 / 190 / 222 ---------------------------------------
    struct ModeCase { float y; int expect; const char* name; };
    const ModeCase modeCases[] { { 158.0f, 1, "Glue" }, { 190.0f, 0, "Bypass" }, { 222.0f, 2, "Mojo" } };
    for (const auto& mc : modeCases)
    {
        setNorm ("mode", 0.5f);
        press (96, mc.y); release (96, mc.y);
        check ((int) raw ("mode") == mc.expect, juce::String ("Mode: click at y=") + juce::String (mc.y) + " gives " + mc.name);
    }
    setNorm ("mode", 0.5f); press (96, 158); dragTo (96, 158, 96, 222); release (96, 222);
    check ((int) raw ("mode") == 2, "Mode: dragging the cap from top to bottom ends on Mojo");

    // ---- Harmonics: x = 384, Even top / Odd bottom ---------------------------------------------------
    setNorm ("harmonics", 1.0f); press (384, 158); release (384, 158); check ((int) raw ("harmonics") == 0, "Harmonics: top = Even");
    setNorm ("harmonics", 0.0f); press (384, 222); release (384, 222); check ((int) raw ("harmonics") == 1, "Harmonics: bottom = Odd");

    // ---- Amount: drag right or up, 200 design px for the whole range -------------------------------
    setNorm ("amount", 0.5f); press (240, 190); dragTo (240, 190, 260, 190); release (260, 190);
    check (near (norm ("amount"), 0.6f, 0.005f), "Amount: 20 px right = +10 %", juce::String (norm ("amount"), 3));
    setNorm ("amount", 0.5f); press (240, 190); dragTo (240, 190, 240, 170); release (240, 170);
    check (near (norm ("amount"), 0.6f, 0.005f), "Amount: 20 px up = +10 %", juce::String (norm ("amount"), 3));
    setNorm ("amount", 0.5f); press (240, 190); dragTo (240, 190, 240, 210); release (240, 210);
    check (near (norm ("amount"), 0.4f, 0.005f), "Amount: 20 px down = -10 %", juce::String (norm ("amount"), 3));
    setNorm ("amount", 0.0f); press (240, 190); dragTo (240, 190, 440, 190); release (440, 190);
    check (near (norm ("amount"), 1.0f, 0.005f), "Amount: 200 px right covers the whole range", juce::String (norm ("amount"), 3));

    // ---- Reference (x=72) and Trim (x=156): drag up/down, 150 px for the whole range -----------------
    setNorm ("reflevel", 0.2f); press (72, 332); dragTo (72, 332, 72, 302); release (72, 302);
    check (near (norm ("reflevel"), 0.4f, 0.005f), "Reference: 30 px up = +20 % of range", juce::String (norm ("reflevel"), 3));
    setNorm ("trim", 0.8f); press (156, 332); dragTo (156, 332, 156, 362); release (156, 362);
    check (near (norm ("trim"), 0.6f, 0.005f), "Trim: 30 px down = -20 % of range", juce::String (norm ("trim"), 3));
    setNorm ("trim", 0.0f); press (156, 332); dragTo (156, 332, 156, 182); release (156, 182);
    check (near (norm ("trim"), 1.0f, 0.005f), "Trim: 150 px up covers the whole range", juce::String (norm ("trim"), 3));

    // ---- Click-to-step: Oversample (x=240), Model (x=324), DC Block (x=408) --------------------------
    setNorm ("oversampling", 2.0f / 3.0f);
    juce::String seq;
    for (int i = 0; i < 4; ++i) { press (240, 332); release (240, 332); seq << (int) raw ("oversampling") << " "; }
    check (seq.trim() == "3 0 1 2", "Oversample: 4x -> 8x -> Off -> 2x -> 4x (wraps)", seq);
    setNorm ("model", 1.0f); press (324, 332); release (324, 332); check ((int) raw ("model") == 0, "Model: Coupled -> Simple");
    press (324, 332); release (324, 332); check ((int) raw ("model") == 1, "Model: Simple -> Coupled");
    setNorm ("dcblock", 1.0f); press (408, 332); release (408, 332); check (raw ("dcblock") < 0.5f, "DC Block: on -> off");
    press (408, 332); release (408, 332); check (raw ("dcblock") > 0.5f, "DC Block: off -> on");
    setNorm ("oversampling", 2.0f / 3.0f);
    press (240, 332); dragTo (240, 332, 240, 250); release (240, 250);
    check ((int) raw ("oversampling") == 3, "Oversample: dragging after the click changes nothing more");

    // ---- Double click returns to default -----------------------------------------------------------
    setNorm ("amount", 0.3f); editor->mouseDoubleClick (ev (240, 190, { 240, 190 }, 2, false));
    check (near (raw ("amount"), 1.0f, 0.001f), "Amount: double click -> default 1.0", juce::String (raw ("amount"), 3));
    setNorm ("reflevel", 0.9f); editor->mouseDoubleClick (ev (72, 332, { 72, 332 }, 2, false));
    check (near (raw ("reflevel"), 1.24f, 0.02f), "Reference: double click -> default 1.24 V", juce::String (raw ("reflevel"), 3));
    setNorm ("trim", 0.9f); editor->mouseDoubleClick (ev (156, 332, { 156, 332 }, 2, false));
    check (near (raw ("trim"), 0.0f, 0.05f), "Trim: double click -> default 0 dB", juce::String (raw ("trim"), 3));
    setNorm ("oversampling", 0.0f); editor->mouseDoubleClick (ev (240, 332, { 240, 332 }, 2, false));
    // (a double click on Oversample is two single clicks; the editor adds nothing)

    // ---- Wheel: one Windows notch (120) arrives as deltaY = 0.5 * 120 / 256 = 0.234 ----------------------
    {
        juce::MouseWheelDetails w; w.deltaX = 0; w.deltaY = 0.5f * 120.0f / 256.0f; w.isReversed = false; w.isSmooth = false; w.isInertial = false;
        setNorm ("amount", 0.5f); editor->mouseWheelMove (ev (240, 190, { 240, 190 }, 1, false), w);
        const float step = norm ("amount") - 0.5f;
        std::printf ("INFO  wheel: one notch moves Amount by %.2f %% of its range\n", step * 100.0f);
        check (step > 0.02f && step < 0.03f, "Amount: a wheel notch up raises it by about 2.3 %", juce::String (step * 100.0f, 2) + " %");
        w.deltaY = -w.deltaY; editor->mouseWheelMove (ev (240, 190, { 240, 190 }, 1, false), w);
        check (near (norm ("amount"), 0.5f, 0.001f), "Amount: a notch down undoes it");
        setNorm ("trim", 0.5f); w.deltaY = 0.5f * 120.0f / 256.0f; editor->mouseWheelMove (ev (156, 332, { 156, 332 }, 1, false), w);
        check (norm ("trim") > 0.5f, "Trim: wheel works");
        setNorm ("oversampling", 2.0f / 3.0f); editor->mouseWheelMove (ev (240, 332, { 240, 332 }, 1, false), w);
        check ((int) raw ("oversampling") == 2, "Oversample: wheel does nothing");
    }

    // ---- Latency is the same at every Oversampling setting, sample rate and block size ----------------------
    {
        const double rates[] { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
        const int blocks[] { 32, 64, 512, 4096 };
        bool allSame = true; int first = -1; juce::String seen;
        for (double sr : rates)
            for (int bs : blocks)
                for (int os = 0; os < 4; ++os)
                {
                    setNorm ("oversampling", (float) os / 3.0f);
                    p.prepareToPlay (sr, bs);
                    const int l = p.getLatencySamples();
                    if (first < 0) first = l;
                    if (l != first) { allSame = false; seen << (int) sr << "/" << bs << "/os" << os << "=" << l << " "; }
                }
        check (allSame, "Reported latency is identical for every oversampling setting, sample rate (44.1 to 192 kHz) and block size (32 to 4096)",
               allSame ? juce::String (first) + " samples" : seen);
        std::printf ("INFO  latency: %d samples (%.2f ms at 48 kHz)\n", first, 1000.0 * first / 48000.0);
        p.prepareToPlay (48000.0, 512);
    }

    // ---- Bypass: what is still applied -----------------------------------------------------------------------
    {
        auto run = [&] (int os, bool dc, float trimNorm, std::vector<float>& out, std::vector<float>& in)
        {
            setNorm ("mode", 0.0f); setNorm ("oversampling", (float) os / 3.0f); setNorm ("dcblock", dc ? 1.0f : 0.0f); setNorm ("trim", trimNorm);
            p.prepareToPlay (48000.0, 512);
            const int n = 48000;
            juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
            in.assign ((size_t) n, 0.0f); out.assign ((size_t) n, 0.0f);
            for (int s = 0; s < n; s += 512)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float x = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) (s + i) / 48000.0f) + 0.3f;
                    b.setSample (0, i, x); b.setSample (1, i, x); if (s + i < n) in[(size_t) (s + i)] = x;
                }
                p.processBlock (b, m);
                for (int i = 0; i < 512 && s + i < n; ++i) out[(size_t) (s + i)] = b.getSample (0, i);
            }
        };
        std::vector<float> o, in;
        run (0, false, 0.5f, o, in);
        double e = 0; for (size_t i = 0; i < o.size(); ++i) e = std::max (e, (double) std::abs (o[i] - in[i]));
        (void) e;
        const int L = p.getLatencySamples(); double e2 = 0;
        for (size_t i = (size_t) (L + 2000); i < o.size(); ++i) e2 = std::max (e2, (double) std::abs (o[i] - in[i - (size_t) L]));
        check (e2 < 1e-5, "Bypass with everything neutral = input delayed by the latency, exactly", "max difference " + juce::String (e2, 8));
        run (2, false, 0.5f, o, in);
        double e3 = 0; for (size_t i = (size_t) (L + 4000); i < o.size(); ++i) e3 = std::max (e3, (double) std::abs (o[i] - in[i - (size_t) L]));
        std::printf ("INFO  Bypass at 4x vs delayed input: max difference %.6f (%.1f dB)\n", e3, 20.0 * std::log10 (std::max (e3, 1e-12)));
        run (0, true, 0.5f, o, in);
        double dcOut = 0, dcIn = 0; for (size_t i = 40000; i < o.size(); ++i) { dcOut += o[i]; dcIn += in[i]; }
        std::printf ("INFO  Bypass, DC Block on: mean of input %.4f, mean of output %.4f\n", dcIn / (o.size() - 40000), dcOut / (o.size() - 40000));
        check (std::abs (dcOut / (o.size() - 40000)) < 0.05 * std::abs (dcIn / (o.size() - 40000)), "Bypass with DC Block on still removes the DC offset");
        run (0, false, 1.0f, o, in);
        double mx = 0; for (size_t i = 40000; i < o.size(); ++i) mx = std::max (mx, (double) std::abs (o[i]));
        check (mx > 10.0, "Bypass with Trim +24 dB still raises the level", "peak " + juce::String (mx, 2));
    }

    // ---- Display texts: what the read-out shows for each control at its default ------------------------------
    {
        const char* ids[] { "mode", "harmonics", "amount", "reflevel", "trim", "oversampling", "dcblock", "model" };
        for (auto* id : ids)
        {
            auto* q = p.apvts.getParameter (id);
            q->setValueNotifyingHost (q->getDefaultValue());
            auto txt = q->getCurrentValueAsText(); if (q->getLabel().isNotEmpty()) txt << " " << q->getLabel();
            std::printf ("INFO  readout %-12s \"%s\"\n", id, txt.toUpperCase().toRawUTF8());
        }
        auto* tr = p.apvts.getParameter ("trim"); tr->setValueNotifyingHost (tr->convertTo0to1 (-6.0f));
        std::printf ("INFO  readout trim -6 dB  \"%s %s\"\n", tr->getCurrentValueAsText().toRawUTF8(), tr->getLabel().toRawUTF8());
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
