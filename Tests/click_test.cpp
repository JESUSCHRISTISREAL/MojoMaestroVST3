// Click test: plays a sine through the processor, changes a setting mid-playback and compares the
// biggest second difference near the change with the steady signal.
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 512;

    struct Run
    {
        MojoMaestroProcessor p;
        std::vector<float> out;                       // left channel
        double phase = 0.0;
        double freq = 440.0, amp = 0.9;
        Run()
        {
            p.setPlayConfigDetails (2, 2, kSr, kBlock);
            p.prepareToPlay (kSr, kBlock);
        }
        void process (int samples, bool silent = false)
        {
            juce::AudioBuffer<float> b (2, kBlock);
            juce::MidiBuffer m;
            int done = 0;
            while (done < samples)
            {
                const int n = juce::jmin (kBlock, samples - done);
                b.setSize (2, n, false, false, true);
                for (int i = 0; i < n; ++i)
                {
                    const float s = silent ? 0.0f : (float) (amp * std::sin (phase));
                    phase += 2.0 * juce::MathConstants<double>::pi * freq / kSr;
                    b.setSample (0, i, s); b.setSample (1, i, s);
                }
                p.processBlock (b, m);
                for (int i = 0; i < n; ++i) out.push_back (b.getSample (0, i));
                done += n;
            }
        }
        void setChoice (const char* id, int index)
        {
            auto* par = p.apvts.getParameter (id);
            par->setValueNotifyingHost (par->convertTo0to1 ((float) index));
        }
        void setBool (const char* id, bool v) { p.apvts.getParameter (id)->setValueNotifyingHost (v ? 1.0f : 0.0f); }
    };

    double maxD2 (const std::vector<float>& y, size_t a, size_t b)
    {
        double m = 0.0;
        for (size_t i = juce::jmax<size_t> (a, 2); i < b && i < y.size(); ++i)
            m = juce::jmax (m, (double) std::abs (y[i] - 2.0f * y[i - 1] + y[i - 2]));
        return m;
    }

    int failures = 0;
    void report (const char* name, const std::vector<float>& y, size_t at, double extraSteady = 0.0)
    {
        const size_t w200 = (size_t) (0.2 * kSr), w60 = (size_t) (0.06 * kSr);
        const double steadyBefore = maxD2 (y, at - w200, at);
        const double steadyAfter  = maxD2 (y, at + (size_t) (0.2 * kSr), at + (size_t) (0.4 * kSr));   // the new setting's own steady state
        const double steady = juce::jmax (juce::jmax (steadyBefore, steadyAfter), extraSteady);   // extraSteady: a setting the signal passes through on the way
        const double after  = maxD2 (y, at, at + w60);
        const double ratio  = after / juce::jmax (steady, 1e-9);
        const bool ok = ratio < 1.5;
        if (! ok) ++failures;
        std::printf ("%-34s steady %.5f  after %.5f  ratio %5.2f  %s\n", name, steady, after, ratio, ok ? "ok" : "CLICK");
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const char* osNames[] { "off", "2x", "4x", "8x" };
    const size_t at = (size_t) (1.5 * kSr);

    for (int from = 0; from < 4; ++from)
        for (int to = 0; to < 4; ++to)
        {
            if (from == to) continue;
            Run r; r.setChoice ("oversampling", from);
            r.process ((int) at);
            r.setChoice ("oversampling", to);
            r.process ((int) (0.5 * kSr));
            char name[64]; std::snprintf (name, sizeof name, "oversampling %s -> %s", osNames[from], osNames[to]);
            report (name, r.out, at);
        }

    for (int dc = 0; dc < 2; ++dc)
    {
        Run r; r.setBool ("dcblock", dc == 0);
        r.process ((int) at); r.setBool ("dcblock", dc != 0); r.process ((int) (0.5 * kSr));
        report (dc == 0 ? "DC block on -> off" : "DC block off -> on", r.out, at);
    }
    const char* modeNames[] { "Bypass", "Glue", "Mojo" };
    for (int from = 0; from < 3; ++from)
        for (int to = 0; to < 3; ++to)
        {
            if (from == to) continue;
            Run r; r.setChoice ("mode", from); r.process ((int) at); r.setChoice ("mode", to); r.process ((int) (0.5 * kSr));
            char name[64]; std::snprintf (name, sizeof name, "mode %s -> %s", modeNames[from], modeNames[to]);
            report (name, r.out, at);
        }
    { Run r; r.setChoice ("harmonics", 0); r.process ((int) at); r.setChoice ("harmonics", 1); r.process ((int) (0.5 * kSr)); report ("harmonics Even -> Odd", r.out, at); }
    { Run r; r.setChoice ("model", 1); r.process ((int) at); r.setChoice ("model", 0); r.process ((int) (0.5 * kSr)); report ("model Coupled -> Simple", r.out, at); }

    // overlapping changes: each must still be crossfaded, none may step
    auto steadyOf = [&] (int mode)      // the steady-state kink size of a Mode, at 4x
    {
        Run r; r.setChoice ("oversampling", 2); r.setChoice ("mode", mode);
        r.process ((int) (0.8 * kSr));
        return maxD2 (r.out, (size_t) (0.4 * kSr), r.out.size());
    };
    auto combo = [&] (const char* name, int offsetSamples, auto&& first, auto&& second, double extraSteady = 0.0)
    {
        Run r; r.setChoice ("oversampling", 2); r.setChoice ("mode", 1);
        r.process ((int) at);
        first (r);
        r.process (offsetSamples);
        second (r);
        r.process ((int) (0.5 * kSr));
        report (name, r.out, at, extraSteady);
    };
    auto modeTo = [] (int m) { return [m] (Run& r) { r.setChoice ("mode", m); }; };
    auto osTo   = [] (int o) { return [o] (Run& r) { r.setChoice ("oversampling", o); }; };
    auto both   = [] (Run& r) { r.setChoice ("mode", 2); r.setChoice ("oversampling", 3); };
    auto triple = [] (Run& r) { r.setChoice ("mode", 2); r.setChoice ("harmonics", 1); r.setChoice ("model", 0); r.setChoice ("oversampling", 0); };
    auto none   = [] (Run&) {};
    combo ("mode + oversampling together",        0,   both,        none);
    combo ("mode+harm+model+os together",         0,   triple,      none);
    combo ("mode change 3 ms into mode change",   144, modeTo (2),  modeTo (0), steadyOf (2));   // passes through Mojo
    combo ("mode change 5 ms into os change",     240, osTo (1),    modeTo (2), steadyOf (2));
    combo ("os change 5 ms into mode change",     240, modeTo (2),  osTo (3));
    combo ("mode change 12 ms into os change",    576, osTo (3),    modeTo (0));
    combo ("rapid Glue-Bypass-Mojo-Glue",         96,  modeTo (0),  [] (Run& r) { r.setChoice ("mode", 2); r.process (96); r.setChoice ("mode", 1); }, steadyOf (2));
    // starting playback: silence, then a signal that begins at zero phase (so any kink is the plugin's own)
    const double freqs[] { 50.0, 440.0, 3000.0 };
    for (double fq : freqs)
        for (int os = 0; os < 4; ++os)
        {
            Run r; r.setChoice ("oversampling", os); r.freq = fq;
            r.process ((int) (0.3 * kSr), true);
            r.phase = 0.0;
            const size_t start = r.out.size();
            r.process ((int) (1.0 * kSr));
            const double steady = maxD2 (r.out, start + (size_t) (0.5 * kSr), start + (size_t) (1.0 * kSr));
            const double onset  = maxD2 (r.out, start, start + (size_t) (0.05 * kSr));
            const double inputKink = 0.9 * std::sin (2.0 * juce::MathConstants<double>::pi * fq / kSr);   // the signal's own corner where it starts from silence
            const double ratio = onset / juce::jmax (steady, inputKink * 1.05);
            const bool ok = ratio < 1.5; if (! ok) ++failures;
            std::printf ("start of playback %5.0f Hz, os %-4s  steady %.5f  onset %.5f  (input kink %.5f)  ratio %5.2f  %s\n", fq, osNames[os], steady, onset, inputKink, ratio, ok ? "ok" : "CLICK");
        }
    std::printf ("%s\n", failures == 0 ? "ALL OK" : "CLICKS FOUND");
    return failures == 0 ? 0 : 1;
}
