#include "analyse.h"
#include "samplescan_ma.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

/*
    The measurements behind samplescan. Four passes over a mono mixdown:

      levels and a 10 ms RMS envelope   - loudness, silence at each end, attack, decay
      long FFT frames                   - brightness, noisiness, low end, pitch-class profile
      YIN on short frames               - the fundamental, and whether there is only one
      Krumhansl-Kessler key profiles    - a key for material that has several notes in it

    Mono because every question asked here is about the sound, not its placement; a stereo pair
    that cancels in mono would be worth knowing about, and would show up as a low active level.
*/

namespace {

const double kPi = 3.14159265358979323846;

//Twenty minutes. A long field recording is still worth cataloguing, but decoding an hour of
//it to answer "what key is this in" costs memory for no better an answer.
const double kMaxSeconds = 20.0 * 60.0;

//Silence is relative to the file's own peak, with an absolute floor, so a quietly mastered
//whisper is not all "silence" and a loud hit's reverb tail is not all "sound".
const double kSilenceBelowPeakDb = 45.0;
const double kSilenceFloorDb = -65.0;

double ToDb(double lin){ return lin > 1e-10 ? 20.0 * std::log10(lin) : -200.0; }
double PowerToDb(double p){ return p > 1e-20 ? 10.0 * std::log10(p) : -200.0; }

typedef std::complex<double> cplx;

//In-place radix-2 FFT. The size must be a power of two.
void FFT(std::vector<cplx>& a, bool f_inverse){
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++){
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1){
        const double ang = 2.0 * kPi / (double)len * (f_inverse ? 1.0 : -1.0);
        const cplx wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len){
            cplx w(1.0);
            for (size_t k = 0; k < len / 2; k++){
                cplx u = a[i + k];
                cplx v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (f_inverse){
        for (auto& x : a) x /= (double)n;
    }
}

size_t PowerOfTwoAtLeast(size_t n){
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

double Median(std::vector<double> v){
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

bool Decode(const std::wstring& path, std::vector<float>& mono, SampleAnalysis& a){
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);    //native channels and rate
    ma_decoder dec;
    if (ma_decoder_init_file_w(path.c_str(), &cfg, &dec) != MA_SUCCESS){
        a.error = "cannot decode";
        return false;
    }
    a.channels = (int)dec.outputChannels;
    a.sample_rate = (int)dec.outputSampleRate;
    if (a.channels <= 0 || a.sample_rate <= 0){
        ma_decoder_uninit(&dec);
        a.error = "no channels or no sample rate";
        return false;
    }

    const ma_uint64 max_frames = (ma_uint64)(kMaxSeconds * a.sample_rate);
    const ma_uint64 chunk_frames = 4096;
    std::vector<float> chunk(chunk_frames * a.channels);
    double sum_sq = 0;
    double peak = 0;
    ma_uint64 total = 0;
    for (;;){
        ma_uint64 got = 0;
        ma_result r = ma_decoder_read_pcm_frames(&dec, chunk.data(), chunk_frames, &got);
        for (ma_uint64 f = 0; f < got; f++){
            double m = 0;
            for (int c = 0; c < a.channels; c++){
                const double s = chunk[f * a.channels + c];
                m += s;
                sum_sq += s * s;
                const double as = std::fabs(s);
                if (as > peak) peak = as;
                if (as >= 0.999) a.clipped++;
            }
            mono.push_back((float)(m / a.channels));
        }
        total += got;
        if (total >= max_frames){ a.f_truncated = true; break; }
        //MA_AT_END can arrive WITH the last frames, so the frames are taken before the check.
        if (r != MA_SUCCESS || got == 0) break;
    }
    ma_decoder_uninit(&dec);

    if (mono.empty()){
        a.error = "no audio";
        return false;
    }
    a.duration_s = (double)mono.size() / a.sample_rate;
    a.peak_db = ToDb(peak);
    a.rms_db = PowerToDb(sum_sq / ((double)mono.size() * a.channels));
    return true;
}

/*
    Envelope, silence and shape.

    Returns the active range in envelope frames and the threshold, so the spectral and pitch
    passes can skip what this one called silence.
*/
struct Envelope{
    std::vector<double> db;         //one value per 10 ms
    int hop = 0;                    //samples per value
    int first = -1, last = -1;      //active range, inclusive
    double threshold_db = 0;
};

void MeasureEnvelope(const std::vector<float>& x, SampleAnalysis& a, Envelope& env){
    env.hop = std::max(1, a.sample_rate / 100);
    const size_t n = (x.size() + env.hop - 1) / env.hop;
    env.db.resize(n);
    double peak_db = -200;
    int peak_i = 0;
    for (size_t i = 0; i < n; i++){
        const size_t s0 = i * env.hop;
        const size_t s1 = std::min(x.size(), s0 + env.hop);
        double sq = 0;
        for (size_t s = s0; s < s1; s++) sq += (double)x[s] * x[s];
        env.db[i] = PowerToDb(sq / (double)(s1 - s0));
        if (env.db[i] > peak_db){ peak_db = env.db[i]; peak_i = (int)i; }
    }

    env.threshold_db = std::max(peak_db - kSilenceBelowPeakDb, kSilenceFloorDb);
    for (size_t i = 0; i < n; i++){
        if (env.db[i] >= env.threshold_db){
            if (env.first < 0) env.first = (int)i;
            env.last = (int)i;
        }
    }
    if (env.first < 0) return;      //silent file; the caller reports it

    a.lead_ms = env.first * 10.0;
    a.tail_ms = ((int)n - 1 - env.last) * 10.0;

    double sq = 0;
    for (size_t s = (size_t)env.first * env.hop; s < std::min(x.size(), (size_t)(env.last + 1) * env.hop); s++){
        sq += (double)x[s] * x[s];
    }
    a.active_rms_db = PowerToDb(sq / ((double)(env.last - env.first + 1) * env.hop));

    int attack_i = env.first;
    while (attack_i < peak_i && env.db[attack_i] < peak_db - 1.0) attack_i++;
    a.attack_ms = (attack_i - env.first) * 10.0;

    a.decay20_ms = -1;
    for (int i = peak_i; i < (int)n; i++){
        if (env.db[i] < peak_db - 20.0){ a.decay20_ms = (i - peak_i) * 10.0; break; }
    }

    std::vector<double> active(env.db.begin() + env.first, env.db.begin() + env.last + 1);
    a.sustain_db = Median(active) - peak_db;

    //Compared as whole-file ends, not active ends: the question is what the seam of a loop
    //would sound like, and a loop's seam is the file's first and last sample.
    const int ends = std::min<int>(20, (int)n / 2);
    if (ends > 0){
        double head = 0, tail = 0;
        for (int i = 0; i < ends; i++){
            head += std::pow(10.0, env.db[i] / 10.0);
            tail += std::pow(10.0, env.db[n - 1 - i] / 10.0);
        }
        //Floored, so a file that starts on digital silence reads as "a lot quieter", not +127 dB.
        a.head_tail_db = std::max(PowerToDb(tail / ends), -100.0) - std::max(PowerToDb(head / ends), -100.0);
    }

    const double active_s = (env.last - env.first + 1) * 0.01;
    if (active_s < 0.3) a.envelope = "short";
    else if (a.attack_ms > 400) a.envelope = "swell";
    else if (a.sustain_db < -15) a.envelope = "decaying";
    else a.envelope = "sustained";
}

/*
    Brightness, noisiness, low end and the pitch-class profile, from ~170 ms frames.

    The frames are long for the sake of the profile: at 48 kHz a 2048-point bin is 23 Hz wide,
    which is wider than a semitone below about 400 Hz, so a short frame cannot tell A2 from
    B-flat 2 at all. 8192 points is 5.9 Hz and resolves semitones down to roughly 100 Hz.
*/
void MeasureSpectrum(const std::vector<float>& x, const Envelope& env, SampleAnalysis& a, double chroma[12]){
    const size_t N = PowerOfTwoAtLeast((size_t)(a.sample_rate * 0.17));
    const size_t hop = N / 2;
    const size_t s0 = (size_t)env.first * env.hop;
    const size_t s1 = std::min(x.size(), (size_t)(env.last + 1) * env.hop);
    const size_t span = s1 > s0 ? s1 - s0 : 0;

    //At most ~3000 frames; past that a long ambience is sampled evenly rather than read whole.
    size_t step = hop;
    if (span / hop > 3000) step = span / 3000;

    std::vector<double> win(N);
    for (size_t i = 0; i < N; i++) win[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (N - 1));

    const double bin_hz = (double)a.sample_rate / N;
    const double flat_hi = std::min(12000.0, a.sample_rate * 0.5);
    const double threshold_power = std::pow(10.0, env.threshold_db / 10.0);

    double w_sum = 0, centroid = 0, flatness = 0, low = 0;
    std::vector<cplx> buf(N);
    for (size_t start = s0; start < std::max(s0 + 1, s1); start += step){
        double frame_sq = 0;
        for (size_t i = 0; i < N; i++){
            const size_t s = start + i;
            const double v = s < x.size() ? x[s] : 0.0;
            frame_sq += v * v;
            buf[i] = cplx(v * win[i], 0.0);
        }
        if (frame_sq / N < threshold_power) continue;
        FFT(buf, false);

        double e = 0, fe = 0, lowe = 0, log_sum = 0, lin_sum = 0;
        int flat_bins = 0;
        for (size_t k = 1; k < N / 2; k++){
            const double f = k * bin_hz;
            const double p = std::norm(buf[k]);
            e += p;
            fe += f * p;
            if (f < 200.0) lowe += p;
            if (f >= 50.0 && f <= flat_hi){
                log_sum += std::log(p + 1e-20);
                lin_sum += p;
                flat_bins++;
            }
            if (f >= 100.0 && f <= 5000.0){
                const double midi = 69.0 + 12.0 * std::log2(f / 440.0);
                const int pc = (((int)std::lround(midi)) % 12 + 12) % 12;
                chroma[pc] += std::sqrt(p);
            }
        }
        if (e <= 0) continue;
        //Weighted by energy, so the loud part of a sound decides what it is.
        w_sum += e;
        centroid += (fe / e) * e;
        low += (lowe / e) * e;
        if (flat_bins > 0 && lin_sum > 0){
            flatness += (std::exp(log_sum / flat_bins) / (lin_sum / flat_bins)) * e;
        }
    }
    if (w_sum > 0){
        a.centroid_hz = centroid / w_sum;
        a.flatness = flatness / w_sum;
        a.low_share = low / w_sum;
    }
}

/*
    The fundamental, by YIN (de Cheveigne and Kawahara, 2002).

    Autocorrelation on its own picks octaves far too readily; YIN's cumulative-mean normalised
    difference is what makes "the first dip under a threshold" a reliable answer. The difference
    function is computed through an FFT cross-correlation rather than directly, which is the
    same numbers in N log N instead of N squared: the direct form is W * tau_max, about 2.5
    million multiplies, per frame.

    40 Hz to 3 kHz: below that is rumble rather than a note, above it is the top of a kalimba,
    a flute or a bell, which is as high as a tonal sample in this library is likely to go.
*/
void MeasurePitch(const std::vector<float>& x, const Envelope& env, SampleAnalysis& a){
    const int rate = a.sample_rate;
    const int W = (int)PowerOfTwoAtLeast((size_t)(rate * 0.04));
    const int tau_max = std::max(2, rate / 40);
    const int tau_min = std::max(2, rate / 3000);
    const int L = W + tau_max;
    const size_t NF = PowerOfTwoAtLeast((size_t)(W + L));

    const size_t s0 = (size_t)env.first * env.hop;
    const size_t s1 = std::min(x.size(), (size_t)(env.last + 1) * env.hop);
    if (s1 < s0 + (size_t)L){
        a.pitch_kind = "none";      //too short to hold two periods of the lowest note
        return;
    }
    size_t step = std::max<size_t>(W / 2, (s1 - s0 - L) / 1500);

    std::vector<double> pre(L + 1);
    std::vector<cplx> A(NF), B(NF);
    std::vector<double> d(tau_max + 2), dn(tau_max + 2);
    const double threshold_power = std::pow(10.0, env.threshold_db / 10.0);

    std::vector<double> voiced_midi;
    int frames = 0;
    for (size_t start = s0; start + L <= s1; start += step){
        pre[0] = 0;
        for (int i = 0; i < L; i++) pre[i + 1] = pre[i] + (double)x[start + i] * x[start + i];
        if ((pre[W] - pre[0]) / W < threshold_power) continue;
        frames++;

        for (size_t i = 0; i < NF; i++){
            A[i] = cplx(i < (size_t)W ? x[start + i] : 0.0, 0.0);
            B[i] = cplx(i < (size_t)L ? x[start + i] : 0.0, 0.0);
        }
        FFT(A, false);
        FFT(B, false);
        for (size_t i = 0; i < NF; i++) A[i] = std::conj(A[i]) * B[i];
        FFT(A, true);       //A[tau] = sum over j < W of x[j] * x[j + tau]

        const double e0 = pre[W] - pre[0];
        double running = 0;
        dn[0] = 1.0;
        for (int tau = 1; tau <= tau_max; tau++){
            d[tau] = e0 + (pre[tau + W] - pre[tau]) - 2.0 * A[tau].real();
            running += d[tau];
            dn[tau] = running > 0 ? d[tau] * tau / running : 1.0;
        }

        int best = -1;
        for (int tau = tau_min; tau <= tau_max; tau++){
            if (dn[tau] < 0.15){
                while (tau + 1 <= tau_max && dn[tau + 1] < dn[tau]) tau++;
                best = tau;
                break;
            }
        }
        if (best < 0) continue;     //nothing periodic enough in this frame

        //Parabolic interpolation, so the answer is not quantised to whole samples - at 48 kHz
        //one sample of period is 36 cents at 1 kHz, far coarser than a tuning question needs.
        double tau_f = best;
        if (best > tau_min && best < tau_max){
            const double y0 = dn[best - 1], y1 = dn[best], y2 = dn[best + 1];
            const double den = y0 - 2.0 * y1 + y2;
            if (std::fabs(den) > 1e-12) tau_f = best + 0.5 * (y0 - y2) / den;
        }
        const double f0 = rate / tau_f;
        voiced_midi.push_back(69.0 + 12.0 * std::log2(f0 / 440.0));
    }

    a.voiced = frames > 0 ? (double)voiced_midi.size() / frames : 0.0;
    if (voiced_midi.size() < 3){
        a.pitch_kind = "none";
        return;
    }
    a.pitch_midi = Median(voiced_midi);
    a.pitch_hz = 440.0 * std::pow(2.0, (a.pitch_midi - 69.0) / 12.0);
    std::vector<double> dev;
    dev.reserve(voiced_midi.size());
    for (double m : voiced_midi) dev.push_back(std::fabs(m - a.pitch_midi) * 100.0);
    a.stability_cents = Median(dev);

    //35 cents lets through a flute's vibrato and a slightly drifting drone, and still rejects
    //a two-note phrase, whose median distance is at least a semitone for half its frames.
    if (a.voiced >= 0.4 && a.stability_cents <= 35.0) a.pitch_kind = "single";
    else if (a.voiced >= 0.25) a.pitch_kind = "multi";
    else a.pitch_kind = "none";
}

/*
    Key, from the pitch-class profile, by correlating against Krumhansl and Kessler's probe-tone
    profiles in all 24 keys. It cannot separate a key from its relative (C major and A minor
    share every note); the profiles lean on which note is emphasised, which is right more often
    than not and is why key_margin is reported next to it.
*/
void EstimateKey(const double chroma[12], SampleAnalysis& a){
    static const double major[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static const double minor[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    double sum = 0;
    for (int i = 0; i < 12; i++) sum += chroma[i];
    if (sum <= 0) return;

    auto correlate = [&](const double* profile, int tonic){
        double mx = 0, mp = 0;
        for (int i = 0; i < 12; i++){ mx += chroma[i]; mp += profile[i]; }
        mx /= 12; mp /= 12;
        double num = 0, dx = 0, dp = 0;
        for (int i = 0; i < 12; i++){
            const double vx = chroma[(i + tonic) % 12] - mx;
            const double vp = profile[i] - mp;
            num += vx * vp; dx += vx * vx; dp += vp * vp;
        }
        return (dx > 0 && dp > 0) ? num / std::sqrt(dx * dp) : 0.0;
    };

    double best = -2, second = -2;
    std::string best_name;
    for (int tonic = 0; tonic < 12; tonic++){
        for (int mode = 0; mode < 2; mode++){
            const double r = correlate(mode ? minor : major, tonic);
            if (r > best){
                second = best;
                best = r;
                best_name = std::string(names[tonic]) + (mode ? " minor" : " major");
            }
            else if (r > second) second = r;
        }
    }
    a.key = best_name;
    a.key_r = best;
    a.key_margin = best - second;
}

} //namespace

std::string NoteName(int midi){
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int pc = ((midi % 12) + 12) % 12;
    const int octave = (midi - pc) / 12 - 1;
    return std::string(names[pc]) + std::to_string(octave);
}

bool AnalyseFile(const std::wstring& path, SampleAnalysis& a){
    a = SampleAnalysis();
    std::vector<float> mono;
    if (!Decode(path, mono, a)) return false;

    Envelope env;
    MeasureEnvelope(mono, a, env);
    if (env.first < 0){
        a.error = "silent";
        return false;
    }

    double chroma[12] = {0};
    MeasureSpectrum(mono, env, a, chroma);
    MeasurePitch(mono, env, a);
    //One note has no key - the profile of a lone A4 correlates best with A major for no
    //reason but its harmonics - so a single-pitch file answers with its note instead.
    if (a.pitch_kind != "single") EstimateKey(chroma, a);

    a.f_ok = true;
    return true;
}
