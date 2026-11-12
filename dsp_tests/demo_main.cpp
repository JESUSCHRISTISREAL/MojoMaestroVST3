// Validation harness: 100 Hz sine (as in Winer's LTspice test) through each
// mode; prints peak levels, a crude harmonic check via DFT, writes WAVs.
#include "MojoMaestro.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>

using Ch = MojoMaestroChannel;
static const double kPi = 3.14159265358979323846;

static void writeWav16(const std::string& path, const std::vector<float>& s, uint32_t sr) {
    std::vector<int16_t> pcm(s.size());
    for (size_t i = 0; i < s.size(); ++i)
        pcm[i] = int16_t(std::lround(std::clamp(s[i], -1.0f, 1.0f) * 32767.0f));
    uint32_t data = uint32_t(pcm.size() * 2), riff = 36 + data, fmt = 16, br = sr * 2;
    uint16_t pcmTag = 1, ch = 1, align = 2, bits = 16;
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return;
    std::fwrite("RIFF",1,4,f); std::fwrite(&riff,4,1,f); std::fwrite("WAVEfmt ",1,8,f);
    std::fwrite(&fmt,4,1,f); std::fwrite(&pcmTag,2,1,f); std::fwrite(&ch,2,1,f);
    std::fwrite(&sr,4,1,f); std::fwrite(&br,4,1,f); std::fwrite(&align,2,1,f);
    std::fwrite(&bits,2,1,f); std::fwrite("data",1,4,f); std::fwrite(&data,4,1,f);
    std::fwrite(pcm.data(),2,pcm.size(),f); std::fclose(f);
}

// Magnitude of the n-th harmonic of f0 over whole cycles (single-bin DFT).
static double harm(const std::vector<float>& v, double f0, double sr, int n) {
    double re = 0, im = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        double w = 2 * kPi * f0 * n * i / sr;
        re += v[i] * std::cos(w); im += v[i] * std::sin(w);
    }
    return 2.0 * std::sqrt(re * re + im * im) / v.size();
}

static void run(const char* name, const char* file, Ch::Mode m, Ch::Harmonics h,
                double amt, const std::vector<float>& in, double sr) {
    Ch c; c.prepare(sr); c.setMode(m); c.setHarmonics(h); c.setAmount(amt);
    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = c.processSample(in[i]);
    float pk = 0, pn = 0; for (float x : out) { pk = std::max(pk, x); pn = std::min(pn, x); }
    std::printf("%-26s peak +%.3f / %.3f | H2 %.4f  H3 %.4f  H4 %.4f  H5 %.4f\n", name, pk, pn,
                harm(out,100,sr,2), harm(out,100,sr,3), harm(out,100,sr,4), harm(out,100,sr,5));
    writeWav16(file, out, uint32_t(sr));
}

int main() {
    const double sr = 48000;
    std::vector<float> in(static_cast<size_t>(sr), 0.0f); // 1 s = 100 whole cycles
    for (size_t i = 0; i < in.size(); ++i) in[i] = float(0.9 * std::sin(2 * kPi * 100.0 * i / sr));

    run("Bypass",               "out_bypass.wav",    Ch::Mode::Bypass, Ch::Harmonics::Even, 0.5, in, sr);
    run("Glue/Even/Amount .3",  "out_glue_even.wav", Ch::Mode::Glue,   Ch::Harmonics::Even, 0.3, in, sr);
    run("Glue/Odd /Amount .3",  "out_glue_odd.wav",  Ch::Mode::Glue,   Ch::Harmonics::Odd,  0.3, in, sr);
    run("Glue/Odd /Amount 0",   "out_glue_odd_max.wav", Ch::Mode::Glue, Ch::Harmonics::Odd, 0.0, in, sr);
    run("Mojo/Even/Amount .3",  "out_mojo_even.wav", Ch::Mode::Mojo,   Ch::Harmonics::Even, 0.3, in, sr);
    run("Mojo/Odd /Amount .3",  "out_mojo_odd.wav",  Ch::Mode::Mojo,   Ch::Harmonics::Odd,  0.3, in, sr);
    return 0;
}
