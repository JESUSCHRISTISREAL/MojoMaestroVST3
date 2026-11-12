// Coupled model vs a 128x backward-Euler reference of the same circuit. Also shows how far Simple is off.
#include "MojoMaestro.hpp"
#include <vector>
#include <cstdio>
#include <cmath>

using Ch = MojoMaestroChannel;
static const double PI = 3.14159265358979323846;

struct Ref {   // reference circuit, BE capacitor, volts domain
    Ch::DiodeParams d; bool odd; double Rb;
    double R1 = 1000, C1 = 0.047e-6, vaPrev = 0, vd = 0;
    double I(double v) const {
        double vt = d.N * d.Vt, ep = std::exp(std::clamp(v/vt,-40.0,40.0)), en = std::exp(std::clamp(-v/vt,-40.0,40.0));
        return odd ? d.Is*ep - d.Is*d.matchRatio*en : -d.Is*(en-1);
    }
    double dI(double v) const {
        double vt = d.N * d.Vt, ep = std::exp(std::clamp(v/vt,-40.0,40.0)), en = std::exp(std::clamp(-v/vt,-40.0,40.0));
        return odd ? d.Is/vt*(ep + d.matchRatio*en) : d.Is/vt*en;
    }
    double step(double Vin, double dt) {
        double G = C1/dt;
        for (int i = 0; i < 50; ++i) {
            double i_ = I(vd), di = dI(vd), VA = vd + Rb*i_, dVA = 1 + Rb*di;
            double f = (Vin-VA)/R1 - i_ - G*(VA-vaPrev);
            double fp = -(1/R1 + G)*dVA - di;
            double s = std::clamp(f/fp, -0.5, 0.5); vd -= s; if (std::abs(s) < 1e-12) break;
        }
        vaPrev = vd + Rb*I(vd);
        return vaPrev;
    }
};

int main() {
    const double fs = 48000, FS_VOLTS = 1.7;
    const int K = 128;                       // reference oversampling
    const double dur = 0.12;
    struct Case { double f; bool odd; double amount; };
    const Case cases[] = {
        {100,false,0.0},{1000,false,0.0},{5000,false,0.0},{10000,false,0.0},
        {1000,true,0.0},{5000,true,0.0},{5000,true,0.5},{10000,true,0.0}};

    std::printf("Mojo mode, input 0.9 FS. Error vs reference, dB re ref RMS (lower = closer).\n");
    std::printf("%-8s %-5s %-6s | %-9s %-9s | %-9s %-9s\n","freq","harm","amt","Simple@1x","Coupled@1x","Simple@4x","Coupled@4x");
    for (const auto& c : cases) {
        Ch::DiodeParams dp;
        Ref ref{dp, c.odd, 120.0 + c.amount*10000.0*0 + 0.0};
        const double potOhms = (c.amount) * 10000.0;
        ref.Rb = 120.0 + potOhms + dp.Rs;
        const int N = int(dur*fs);
        std::vector<double> refOut(N);
        const double dt = 1.0/(fs*K);
        for (int n = 0; n < N; ++n) {
            double v = 0;
            for (int k = 0; k < K; ++k) {
                double t = (n*K + k + 1) * dt;      // sample n ends at (n*K+K)*dt
                v = ref.step(0.9*FS_VOLTS*std::sin(2*PI*c.f*t), dt);
            }
            refOut[n] = v / FS_VOLTS;               // value at t = (n+1)/fs
        }

        auto run = [&](Ch::CircuitModel m, int os) {
            Ch ch; ch.prepare(fs*os); ch.setMode(Ch::Mode::Mojo);
            ch.setHarmonics(c.odd ? Ch::Harmonics::Odd : Ch::Harmonics::Even);
            ch.setAmount(c.amount); ch.setCircuitModel(m);
            std::vector<double> out(N);
            for (int n = 0; n < N; ++n) {
                float y = 0;
                for (int k = 0; k < os; ++k) {
                    double t = (n*os + k + 1) / (fs*os);
                    y = ch.processSample(float(0.9*std::sin(2*PI*c.f*t)));
                }
                out[n] = y;                          // value at t = (n+1)/fs
            }
            return out;
        };
        auto err = [&](const std::vector<double>& o) {
            double e = 0, r = 0;
            for (int n = N/2; n < N; ++n) { e += (o[n]-refOut[n])*(o[n]-refOut[n]); r += refOut[n]*refOut[n]; }
            return 10*std::log10(e/r + 1e-30);
        };
        std::printf("%-8.0f %-5s %-6.1f | %-9.1f %-9.1f | %-9.1f %-9.1f\n", c.f, c.odd?"Odd":"Even", c.amount,
            err(run(Ch::CircuitModel::Simple,1)), err(run(Ch::CircuitModel::Coupled,1)),
            err(run(Ch::CircuitModel::Simple,4)), err(run(Ch::CircuitModel::Coupled,4)));
    }
}
