// Mojo Maestro circuit engine (Winer, audioXpress, Oct 2017). Solves the resistor and diode loop
// every sample, Newton on the Shockley equation. Per channel: R1=1k, R2(Glue)=1k, R3(Mojo)=120R,
// C1=0.047uF, 10k linear Amount pot, D1/D2 = 1N270.

#pragma once
#include <cmath>
#include <algorithm>

class MojoMaestroChannel {
public:
    enum class Mode      { Bypass, Glue, Mojo };
    enum class Harmonics { Even /* D1 only: clips negative peaks  */,
                           Odd  /* D1+D2: clips both peaks         */ };

    // Simple: memoryless clipper, then a fixed R1*C1 low-pass in Mojo.
    // Coupled: C1, R1 and the diodes solved together at node A (trapezoidal C1 + Newton),
    // so the corner moves with the clipping like the real thing.
    enum class CircuitModel { Simple, Coupled };

    // No public SPICE model for the 1N270, so these are guesses from similar germanium parts
    // (1N34A, AA112), fitted to the datasheet limits. Never measured.
    struct DiodeParams {
        double Is         = 5.0e-6;  // saturation current (A)
        double N          = 1.75;    // ideality factor
        double Rs         = 2.0;     // series resistance (ohm); keeps forwardVoltage(200 mA) under the datasheet limit
        double Vt         = 0.0259;  // thermal voltage (V)
        double matchRatio = 1.0;     // Is(D2)/Is(D1) in Odd mode; (try 0.9-1.1)
    };

    // Forward voltage at a given current. Datasheet: 1.00 V max at 200 mA.
    static double forwardVoltage(const DiodeParams& d, double amps) {
        return d.N * d.Vt * std::log(amps / d.Is + 1.0) + amps * d.Rs;
    }

    // Reverse leakage, about Is. Datasheet: 100 uA max at 50 V.
    static double reverseLeakage(const DiodeParams& d) { return d.Is; }

    void prepare(double sampleRate_) {
        sampleRate = sampleRate_;
        updateMojoFilterCoeffs();
        reset();
    }
    void reset() { vdState = lpfState = lpfPrevIn = vaPrev = icPrev = 0.0; }

    void setMode(Mode m)           { mode = m; }
    void setHarmonics(Harmonics h) { harm = h; }
    void setCircuitModel(CircuitModel m) { model = m; }
    // 0..1 maps to 0..10k of pot in series, 0 is the most effect
    void setAmount(double a)       { potOhms = std::clamp(a, 0.0, 1.0) * potMaxOhms; }
    // volts at 0 dBFS (diodes turn on at a fixed voltage, so this sets the drive)
    void setFullScaleVolts(double v) { fullScaleVolts = v; }
    void setDiodeParams(const DiodeParams& p) { diode = p; }

    float processSample(float xIn) {
        if (mode == Mode::Bypass) return xIn; // SW1 center-off: true bypass

        const double x      = double(xIn) * fullScaleVolts;
        const double Rfixed = (mode == Mode::Mojo) ? R3_mojo : R2_glue;
        const double Req    = R1 + Rfixed + potOhms + diode.Rs;

        if (mode == Mode::Mojo && model == CircuitModel::Coupled)
            return float(solveCoupled(x, Rfixed + potOhms + diode.Rs) / fullScaleVolts);

        const double vd = solveDiodeVoltage(x, Req);
        double vOut = x - diodeCurrent(vd) * R1;

        // C1 is out of circuit here, but keep it charged to the node voltage so
        // switching to Coupled Mojo doesn't click
        vaPrev = vOut; icPrev = 0.0;

        // Simple model: C1 as a post-filter at its nominal corner (~3.4 kHz)
        if (mode == Mode::Mojo) vOut = mojoLowpass(vOut);

        return float(vOut / fullScaleVolts);
    }

private:
    static constexpr double R1 = 1000.0, R2_glue = 1000.0, R3_mojo = 120.0;
    static constexpr double C1 = 0.047e-6, potMaxOhms = 10000.0;
    static constexpr double kPi = 3.14159265358979323846;

    double potOhms = 0.0, fullScaleVolts = 1.7, sampleRate = 48000.0;
    Mode mode = Mode::Bypass;
    Harmonics harm = Harmonics::Even;
    DiodeParams diode;
    CircuitModel model = CircuitModel::Coupled;

    double vdState = 0.0, lpfState = 0.0, lpfPrevIn = 0.0;
    double lpfA0 = 1.0, lpfB1 = 0.0;
    double Gc = 0.0;                 // trapezoidal companion conductance 2*C1/T
    double vaPrev = 0.0, icPrev = 0.0; // node-A voltage and C1 current, previous sample

    void updateMojoFilterCoeffs() {
        const double wc = 1.0 / (R1 * C1);
        const double k  = wc / sampleRate / 2.0;   // wc*T/2
        Gc = 2.0 * C1 * sampleRate;
        lpfA0 = k / (1.0 + k);
        lpfB1 = (1.0 - k) / (1.0 + k);
    }
    double mojoLowpass(double x) {
        const double y = lpfA0 * (x + lpfPrevIn) + lpfB1 * lpfState;
        lpfPrevIn = x; lpfState = y;
        return y;
    }

    // Diode current and slope together. Even: D1 alone. Odd: antiparallel pair.
    // The exponent clamps at +-40 and the slope goes to zero there, so Newton sees the
    // clamped model's real (flat) slope.
    void diodeIV(double vd, double& I, double& dI) const {
        constexpr double M = 40.0;
        const double vtN = diode.N * diode.Vt;
        const double a   = vd / vtN;
        const bool clamped = std::abs(a) > M;
        const double ap = std::clamp( a, -M, M);
        const double an = std::clamp(-a, -M, M);
        if (harm == Harmonics::Even) {
            const double en = std::exp(an);
            I  = -diode.Is * (en - 1.0);
            dI = clamped ? 0.0 : (diode.Is / vtN) * en;
        } else {
            const double ep = std::exp(ap), en = std::exp(an);
            I  = diode.Is * (ep - 1.0) - diode.Is * diode.matchRatio * (en - 1.0); // zero current at zero volts
            dI = clamped ? 0.0 : (diode.Is / vtN) * (ep + diode.matchRatio * en);
        }
    }
    double diodeCurrent(double vd) const { double I, dI; diodeIV(vd, I, dI); return I; }

    // Newton with a bisection fallback when a step leaves the bracket or stalls.
    // f must be strictly decreasing; fn(vd, f, df) returns f and f'(vd).
    template <class Fn>
    double solveMonotone(Fn&& fn, double vd) const {
        double lo = 0.0, hi = 0.0, lastStep = 1e30;
        bool haveLo = false, haveHi = false;
        for (int it = 0; it < 100; ++it) {
            double f, df;
            fn(vd, f, df);
            if (f == 0.0) break;
            if (f > 0.0) { lo = vd; haveLo = true; } else { hi = vd; haveHi = true; }

            double next = (df < 0.0) ? vd - f / df : vd + (f > 0.0 ? 0.1 : -0.1);
            if (haveLo && haveHi) {
                const bool outside = !(next > lo && next < hi);
                const bool slow    = std::abs(2.0 * f) > std::abs(lastStep * df);
                if (outside || slow) next = 0.5 * (lo + hi);
            }
            lastStep = next - vd;
            const bool done = std::abs(lastStep) < 1e-11;
            vd = next;
            if (done) break;
        }
        return vd;
    }

    // Series loop (no capacitor): Vin = I(Vd)*Req + Vd. Returns Vd.
    double solveDiodeVoltage(double Vin, double Req) {
        vdState = solveMonotone([&](double vd, double& f, double& df) {
            double I, dI; diodeIV(vd, I, dI);
            f  = Vin - Req * I - vd;
            df = -Req * dI - 1.0;
        }, vdState);
        return vdState;
    }

    // Coupled node-A solve (Mojo). KCL at A: (Vin - VA)/R1 = I(Vd) + Ic, VA = Vd + Rb*I(Vd),
    // with C1 as a trapezoidal companion: Ic = Gc*VA - Ieq, Ieq = Gc*VA[n-1] + Ic[n-1].
    // One unknown, Vd. Returns VA.
    double solveCoupled(double Vin, double Rb) {
        const double Ieq  = Gc * vaPrev + icPrev;
        const double gSum = 1.0 / R1 + Gc;
        vdState = solveMonotone([&](double vd, double& f, double& df) {
            double I, dI; diodeIV(vd, I, dI);
            const double VA = vd + Rb * I, dVA = 1.0 + Rb * dI;
            f  = (Vin - VA) / R1 - I - Gc * VA + Ieq;
            df = -gSum * dVA - dI;
        }, vdState);
        const double VA = vdState + Rb * diodeCurrent(vdState);
        icPrev = Gc * VA - Ieq;
        vaPrev = VA;
        return VA;
    }
};

// two channels, shared controls
class MojoMaestroStereo {
public:
    void prepare(double sr) { left.prepare(sr); right.prepare(sr); }
    void setMode(MojoMaestroChannel::Mode m)           { left.setMode(m); right.setMode(m); }
    void setHarmonics(MojoMaestroChannel::Harmonics h) { left.setHarmonics(h); right.setHarmonics(h); }
    void setCircuitModel(MojoMaestroChannel::CircuitModel m) { left.setCircuitModel(m); right.setCircuitModel(m); }
    void setAmount(double a)                           { left.setAmount(a); right.setAmount(a); }
    void setFullScaleVolts(double v)                   { left.setFullScaleVolts(v); right.setFullScaleVolts(v); }
    void setDiodeParams(const MojoMaestroChannel::DiodeParams& p) { left.setDiodeParams(p); right.setDiodeParams(p); }
    void processBlock(float* l, float* r, int n) {
        for (int i = 0; i < n; ++i) { l[i] = left.processSample(l[i]); r[i] = right.processSample(r[i]); }
    }
    MojoMaestroChannel left, right;
};
