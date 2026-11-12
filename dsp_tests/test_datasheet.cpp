// Diode model vs the 1N270 datasheet limits, and DC output vs an independent solution of the circuit.
// Expected values are worked out here from Shockley and Ohm, not taken from the engine.
// Build: cl /nologo /std:c++17 /EHsc /O2 /W4 /I Source dsp_tests	est_datasheet.cpp

#include "MojoMaestro.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using Ch = MojoMaestroChannel;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what, double value = 0.0) {
    ++g_checks;
    if (ok) {
        std::printf("  ok    %s\n", what.c_str());
    } else {
        ++g_failures;
        std::printf("  FAIL  %s  (measured %.9g)\n", what.c_str(), value);
    }
}

void section(const char* name) { std::printf("\n%s\n", name); }

// 1N270 datasheet limits (Microsemi, one page, 25 C).
constexpr double kMaxForwardVoltage = 1.00;      // volts at 200 mA
constexpr double kForwardTestCurrent = 0.200;    // amps
constexpr double kMaxLeakage = 100.0e-6;         // amps at 50 V reverse

bool meetsDatasheet(const Ch::DiodeParams& d) {
    return Ch::forwardVoltage(d, kForwardTestCurrent) <= kMaxForwardVoltage && Ch::reverseLeakage(d) <= kMaxLeakage;
}

// Independent solution of one channel's loop: Vin = R1*I + Vd + (Rfixed + pot + Rs)*I,
// with the diode current written from the physics (zero current at zero volts).
// Returns node A (the output) in volts.
double nodeA(double vin, double rFixed, double potOhms, const Ch::DiodeParams& d, bool odd) {
    const double vt = d.N * d.Vt;
    auto current = [&](double vd) {
        if (!odd) return -d.Is * (std::exp(-vd / vt) - 1.0);                       // D1 only
        return d.Is * (std::exp(vd / vt) - 1.0) - d.Is * d.matchRatio * (std::exp(-vd / vt) - 1.0); // D1 + D2
    };
    const double rLoop = 1000.0 + rFixed + potOhms + d.Rs;
    double lo = -3.0, hi = 3.0;
    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi);
        (rLoop * current(mid) + mid - vin > 0.0 ? hi : lo) = mid;
    }
    const double vd = 0.5 * (lo + hi);
    return vin - current(vd) * 1000.0;
}

} // namespace

int main() {
    std::printf("Mojo Maestro engine tests\n");

    section("Diode data against the 1N270 datasheet limits");
    {
        Ch::DiodeParams d;
        const double vf = Ch::forwardVoltage(d, kForwardTestCurrent);
        std::printf("        forward voltage at 200 mA = %.3f V, reverse leakage = %.2f uA\n", vf, Ch::reverseLeakage(d) * 1e6);
        check(vf <= kMaxForwardVoltage, "forward voltage at 200 mA is at most 1.00 V", vf);
        check(Ch::reverseLeakage(d) <= kMaxLeakage, "reverse leakage is at most 100 uA", Ch::reverseLeakage(d));
        Ch::DiodeParams tooResistive = d;
        tooResistive.Rs = 8.0;
        check(!meetsDatasheet(tooResistive), "a series resistance of 8 ohm is rejected by the limit check",
              Ch::forwardVoltage(tooResistive, kForwardTestCurrent));
        // Knee: the article says the 1N270 conducts at around 0.2 V; 0.2 V must carry a clearly visible current.
        const double i02 = d.Is * (std::exp(0.2 / (d.N * d.Vt)) - 1.0);
        std::printf("        current at 0.2 V = %.0f uA\n", i02 * 1e6);
        check(i02 > 50.0e-6 && i02 < 5.0e-3, "0.2 V carries between 50 uA and 5 mA", i02);
    }

    section("Bypass leaves the signal exactly unchanged");
    {
        Ch c; c.prepare(48000.0); c.setMode(Ch::Mode::Bypass);
        bool same = true;
        for (float x : {-1.0f, -0.37f, 0.0f, 0.001f, 0.5f, 1.0f}) same = same && (c.processSample(x) == x);
        check(same, "bypass output equals input");
    }

    section("Silence in, silence out (every mode, both harmonics, matched and mismatched diodes)");
    for (double match : {1.0, 1.1}) {
        for (auto mode : {Ch::Mode::Glue, Ch::Mode::Mojo}) {
            for (auto harm : {Ch::Harmonics::Even, Ch::Harmonics::Odd}) {
                Ch c; c.prepare(48000.0); c.setMode(mode); c.setHarmonics(harm); c.setAmount(0.0);
                Ch::DiodeParams d; d.matchRatio = match; c.setDiodeParams(d);
                double worst = 0.0;
                for (int i = 0; i < 2000; ++i) worst = std::max(worst, std::fabs((double) c.processSample(0.0f)));
                std::string name = std::string(mode == Ch::Mode::Glue ? "Glue" : "Mojo") + (harm == Ch::Harmonics::Even ? " Even" : " Odd") +
                                   ", match " + std::to_string(match).substr(0, 3) + ": output stays below 1e-9";
                check(worst < 1.0e-9, name, worst);
            }
        }
    }

    section("DC output against an independent solution of the loop");
    {
        const double fs = 1.7;
        const double inputs[] = {-0.8, -0.3, -0.05, 0.05, 0.3, 0.8};
        const double amounts[] = {0.0, 0.3, 1.0};
        double worstAll = 0.0;
        int count = 0;
        for (auto mode : {Ch::Mode::Glue, Ch::Mode::Mojo}) {
            for (auto harm : {Ch::Harmonics::Even, Ch::Harmonics::Odd}) {
                for (double amt : amounts) {
                    Ch c; c.prepare(48000.0); c.setMode(mode); c.setHarmonics(harm); c.setAmount(amt);
                    c.setFullScaleVolts(fs);
                    Ch::DiodeParams d;
                    const double rFixed = (mode == Ch::Mode::Glue) ? 1000.0 : 120.0;
                    for (double x : inputs) {
                        float y = 0.0f;
                        const int settle = (mode == Ch::Mode::Mojo) ? 20000 : 4;   // C1 charges in Mojo
                        for (int i = 0; i < settle; ++i) y = c.processSample((float) x);
                        const double expected = nodeA((double) (float) x * fs, rFixed, amt * 10000.0, d, harm == Ch::Harmonics::Odd) / fs;
                        worstAll = std::max(worstAll, std::fabs((double) y - expected));
                        ++count;
                    }
                }
            }
        }
        std::printf("        %d points, worst difference %.3g (of full scale)\n", count, worstAll);
        check(worstAll < 5.0e-7, "engine matches the independent loop solution at every point", worstAll);
    }

    section("Even clips the negative side only; Odd clips both (the article's description)");
    {
        const double x = 0.9;
        Ch e; e.prepare(48000.0); e.setMode(Ch::Mode::Glue); e.setHarmonics(Ch::Harmonics::Even); e.setAmount(0.0);
        Ch o = e; o.setHarmonics(Ch::Harmonics::Odd);
        const double ePos = e.processSample((float) x), eNeg = e.processSample((float) -x);
        const double oPos = o.processSample((float) x), oNeg = o.processSample((float) -x);
        std::printf("        Even: +%.4f / %.4f   Odd: +%.4f / %.4f\n", ePos, eNeg, oPos, oNeg);
        check(ePos > 0.8 * x, "Even passes the positive peak almost unchanged", ePos);
        check(-eNeg < 0.6 * x, "Even clips the negative peak hard", eNeg);
        check(std::fabs(oPos + oNeg) < 1.0e-6, "Odd clips both peaks equally (matched diodes)", oPos + oNeg);
        check(oPos < 0.6 * x, "Odd clips the positive peak hard", oPos);
    }

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}