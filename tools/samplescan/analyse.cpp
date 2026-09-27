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

    //To the FIRST peak, not the loudest moment: in a minute of ambience the loudest moment can
    //be anywhere, and an attack measured to it says nothing about how the sound begins.
    const int window_end = std::min(env.last, env.first + 300);
    int local_peak = env.first;
    for (int i = env.first; i <= window_end; i++) if (env.db[i] > env.db[local_peak]) local_peak = i;
    int attack_i = env.first;
    while (attack_i < local_peak && env.db[attack_i] < env.db[local_peak] - 1.0) attack_i++;
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

    /*
        Noisiness is flatness measured per OCTAVE, then averaged by energy. Flatness over the
        whole spectrum is useless on real recordings: everything rolls off towards the top, and
        that slope alone drives the geometric mean to nothing, so desert wind scored 0.02 - the
        same as a flute. Within one octave the slope barely matters, and what is left is the
        question actually being asked: is this band a few peaks, or even noise?
    */
    std::vector<double> band_lo;
    for (double f = 62.5; f * 2.0 <= flat_hi; f *= 2.0) band_lo.push_back(f);

    double w_sum = 0, centroid = 0, noisiness = 0, low = 0;
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

        double e = 0, fe = 0, lowe = 0;
        std::vector<double> b_log(band_lo.size(), 0.0), b_lin(band_lo.size(), 0.0);
        std::vector<int> b_bins(band_lo.size(), 0);
        for (size_t k = 1; k < N / 2; k++){
            const double f = k * bin_hz;
            const double p = std::norm(buf[k]);
            e += p;
            fe += f * p;
            if (f < 200.0) lowe += p;
            if (f >= band_lo.front() && f < band_lo.back() * 2.0){
                const int b = std::min((int)band_lo.size() - 1, (int)std::floor(std::log2(f / band_lo.front())));
                b_log[b] += std::log(p + 1e-20);
                b_lin[b] += p;
                b_bins[b]++;
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
        double band_e = 0, band_flat = 0;
        for (size_t b = 0; b < band_lo.size(); b++){
            if (b_bins[b] < 4 || b_lin[b] <= 0) continue;
            band_flat += (std::exp(b_log[b] / b_bins[b]) / (b_lin[b] / b_bins[b])) * b_lin[b];
            band_e += b_lin[b];
        }
        if (band_e > 0) noisiness += (band_flat / band_e) * e;
    }
    if (w_sum > 0){
        a.centroid_hz = centroid / w_sum;
        a.noisiness = noisiness / w_sum;
        a.low_share = low / w_sum;
    }
}

/*
    Onsets and pulse, from the 10 ms envelope.

    An onset is a rise of 6 dB or more over the preceding 50 ms, taken at its steepest frame,
    at least 80 ms after the last one. That catches struck notes and hits, which is what it is
    for; a legato flute changing note without re-tonguing is found by the pitch track instead.

    The pulse is the strongest autocorrelation of the envelope's rises between 60 and 180 bpm.
    Like any tempo estimate it cannot tell a tempo from its double or half; the strength is
    the useful part - it separates a drum loop from a scatter of unrelated hits.
*/
void MeasureRhythm(const Envelope& env, SampleAnalysis& a, std::vector<int>& onsets){
    const int n = (int)env.db.size();

    //Smoothed to 40 ms first. A 10 ms frame is shorter than one period of anything under
    //100 Hz, so on a low drone the raw envelope ripples by several dB at the note's own
    //frequency - and a steady C2 read as 31 onsets at 115 bpm. Forty covers 2.6 periods of the
    //lowest note YIN looks for, and a struck note still rises well inside the 50 ms lookback.
    //CENTRED, not trailing: a trailing window puts every onset 20-40 ms late, after the pitch
    //track has already started the new note, and SegmentNotes then splits that note a second
    //time - the arpeggio test came back with every note doubled.
    std::vector<double> smooth(n);
    for (int i = 0; i < n; i++){
        double p = 0;
        int count = 0;
        for (int k = -2; k <= 1; k++){
            if (i + k < 0 || i + k >= n) continue;
            p += std::pow(10.0, env.db[i + k] / 10.0);
            count++;
        }
        smooth[i] = std::max(PowerToDb(p / count), env.threshold_db);     //silence counts as the threshold, not -200
    }
    auto level = [&](int i){ return smooth[i]; };

    std::vector<double> rise(n, 0.0);
    for (int i = env.first; i <= env.last; i++){
        double lo = level(i);
        for (int k = 1; k <= 5 && i - k >= 0; k++) lo = std::min(lo, level(i - k));
        rise[i] = level(i) - lo;
    }
    int previous = -1000;
    for (int i = env.first; i <= env.last; i++){
        if (rise[i] < 6.0 || level(i) < env.threshold_db + 10.0) continue;
        bool f_steepest = true;
        for (int k = 1; k <= 3 && f_steepest; k++){
            if (i + k < n && rise[i + k] > rise[i]) f_steepest = false;
            if (i - k >= 0 && rise[i - k] >= rise[i]) f_steepest = false;
        }
        if (!f_steepest || i - previous < 8) continue;
        onsets.push_back(i);
        a.onset_s.push_back(i * 0.01);
        a.onset_rise_db.push_back(rise[i]);
        previous = i;
    }
    a.onsets = (int)onsets.size();
    const double active_s = (env.last - env.first + 1) * 0.01;
    a.onset_rate = active_s > 0 ? a.onsets / active_s : 0.0;

    if (a.onsets < 4) return;
    //The first minute is plenty to find a pulse in, and keeps a long file's autocorrelation cheap.
    const int f0 = env.first + 1;
    const int f1 = std::min(env.last, env.first + 6000);
    std::vector<double> flux;
    for (int i = f0; i <= f1; i++) flux.push_back(std::max(0.0, level(i) - level(i - 1)));
    double mean = 0;
    for (double v : flux) mean += v;
    mean /= flux.size();
    for (double& v : flux) v -= mean;

    auto autocorr = [&](int lag){
        double s = 0;
        for (size_t i = lag; i < flux.size(); i++) s += flux[i] * flux[i - lag];
        return s;
    };
    const double r0 = autocorr(0);
    if (r0 <= 0) return;
    const int lag_lo = 33, lag_hi = 100;       //180 and 60 bpm, at 100 frames a second
    if ((int)flux.size() < lag_hi * 3) return;  //fewer than three beats at the slowest tempo
    std::vector<double> r(lag_hi + 2, 0.0);
    int best = lag_lo;
    for (int lag = lag_lo - 1; lag <= lag_hi + 1; lag++){
        r[lag] = autocorr(lag) / r0;
        if (lag >= lag_lo && lag <= lag_hi && r[lag] > r[best]) best = lag;
    }
    double lag_f = best;
    const double den = r[best - 1] - 2.0 * r[best] + r[best + 1];
    if (std::fabs(den) > 1e-12) lag_f = best + 0.5 * (r[best - 1] - r[best + 1]) / den;
    a.beat_strength = std::max(0.0, r[best]);
    a.bpm = 6000.0 / lag_f;
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

    It keeps the whole TRACK, frame by frame, not just the voiced pitches, so SegmentNotes can
    cut a run of notes where the pitch jumps or goes quiet.
*/
struct PitchFrame{
    double t;           //seconds, at the centre of the window
    double midi;
    double db;          //the window's level
    bool f_voiced;
    bool f_weak = false;    //a period under kWeakPeriodic but not under kPeriodic; see below
};

/*
    Two thresholds on YIN's normalised difference. Under kPeriodic a frame is voiced outright.
    Between the two it is WEAK: a period is there, but so is a lot of noise - which is exactly
    a breathy pan flute, where the breath buries the tone and a single threshold called the
    whole F4-to-D4 phrase unpitched (8% voiced). A weak frame counts only if its neighbours
    agree with it, because that is the one thing noise does not do: a wind's best period jumps
    about from frame to frame, a buried flute's stays put.
*/
const double kPeriodic = 0.15;
const double kWeakPeriodic = 0.35;

void SegmentNotes(const std::vector<PitchFrame>& track, const std::vector<int>& onsets, const std::vector<double>& rises,
                  double floor_db, SampleAnalysis& a);

void MeasurePitch(const std::vector<float>& x, const Envelope& env, const std::vector<int>& onsets, SampleAnalysis& a){
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
    //Half-window hops (~21 ms), which is fine enough to see a fast grace note, for up to 4000
    //frames - about 85 s. Past that a file is a mix, not a run of notes, and is sampled evenly.
    size_t step = std::max<size_t>(W / 2, (s1 - s0 - L) / 4000);
    std::vector<PitchFrame> track;

    std::vector<double> pre(L + 1);
    std::vector<cplx> A(NF), B(NF);
    std::vector<double> d(tau_max + 2), dn(tau_max + 2);
    const double threshold_power = std::pow(10.0, env.threshold_db / 10.0);

    std::vector<double> voiced_midi;
    int frames = 0;
    for (size_t start = s0; start + L <= s1; start += step){
        pre[0] = 0;
        for (int i = 0; i < L; i++) pre[i + 1] = pre[i] + (double)x[start + i] * x[start + i];
        const double t = (start + W / 2) / (double)rate;
        if ((pre[W] - pre[0]) / W < threshold_power){
            track.push_back({t, 0.0, -200.0, false});
            continue;
        }
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
            if (dn[tau] < kPeriodic){
                while (tau + 1 <= tau_max && dn[tau + 1] < dn[tau]) tau++;
                best = tau;
                break;
            }
        }
        bool f_weak = false;
        if (best < 0){
            //No clear dip: take the deepest one, as a weak candidate for the neighbour check.
            //The first dip within 10% of the deepest, not the deepest itself, for the same
            //reason YIN takes the first dip - the deepest is often a period two or three long.
            int deepest = tau_min;
            for (int tau = tau_min; tau <= tau_max; tau++) if (dn[tau] < dn[deepest]) deepest = tau;
            if (dn[deepest] < kWeakPeriodic){
                best = deepest;
                for (int tau = tau_min; tau < deepest; tau++){
                    if (dn[tau] < dn[deepest] * 1.1 + 0.01 && dn[tau] <= dn[tau - 1] && dn[tau] <= dn[tau + 1]){ best = tau; break; }
                }
                f_weak = true;
            }
        }
        if (best < 0){              //nothing periodic enough in this frame
            track.push_back({t, 0.0, PowerToDb(e0 / W), false});
            continue;
        }

        //Parabolic interpolation, so the answer is not quantised to whole samples - at 48 kHz
        //one sample of period is 36 cents at 1 kHz, far coarser than a tuning question needs.
        double tau_f = best;
        if (best > tau_min && best < tau_max){
            const double y0 = dn[best - 1], y1 = dn[best], y2 = dn[best + 1];
            const double den = y0 - 2.0 * y1 + y2;
            if (std::fabs(den) > 1e-12) tau_f = best + 0.5 * (y0 - y2) / den;
        }
        const double f0 = rate / tau_f;
        const double midi = 69.0 + 12.0 * std::log2(f0 / 440.0);
        track.push_back({t, midi, PowerToDb(e0 / W), !f_weak, f_weak});
    }

    //The neighbour check: a weak frame is kept when two of the four frames around it, voiced or
    //weak themselves, sit within half a semitone of it. Decided on the track as it came out of
    //YIN, before any weak frame was promoted, so promotions cannot vouch for each other.
    std::vector<bool> promote(track.size(), false);
    for (size_t i = 0; i < track.size(); i++){
        if (!track[i].f_weak) continue;
        int agree = 0;
        for (int k = -2; k <= 2; k++){
            const long j = (long)i + k;
            if (k == 0 || j < 0 || j >= (long)track.size()) continue;
            if ((track[j].f_voiced || track[j].f_weak) && std::fabs(track[j].midi - track[i].midi) < 0.5) agree++;
        }
        promote[i] = agree >= 2;
    }
    for (size_t i = 0; i < track.size(); i++){
        if (promote[i]) track[i].f_voiced = true;
        if (track[i].f_voiced) voiced_midi.push_back(track[i].midi);
    }
    SegmentNotes(track, onsets, a.onset_rise_db, *std::max_element(env.db.begin(), env.db.end()) - 30.0, a);

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
    Cuts the pitch track into notes.

    A note ends where:
      - the track goes unvoiced (or 30 dB under the peak) for more than one frame,
      - the pitch JUMPS more than 0.7 semitone and stays there for three frames, so a single
        bad frame or a vibrato peak does not split a note,
      - or a STRONG envelope onset, 18 dB or more, lands in the middle of it - which is how a
        repeated note, a kalimba played G G G, becomes three notes rather than one long one.

    Why 18 dB, and why only rise size. A soft tremolo and a reverberant flutter both lift the
    level often enough to look like re-struck notes - one tremolo E4 came back as 73 notes, one
    reverb-soaked F#5 as 19. Measured on the library, those swells rise 6-16 dB and the real
    tongued notes of the comedy flute 31-40, so the size separates them. Attack SPEED was tried
    first and does not: on synthetic tones a strike puts all its rise inside 20 ms and a
    tremolo half, but the real tongued flute attacks at 0.5-0.7 of that and the real tremolo
    at 0.5-1.5. The price, measured on a synthetic G3: struck again while still ringing, the
    note rises only 8-9 dB and the repeats merge into one note. That is the safer mistake - a
    slice holding three strikes is obvious on listening; 73 slices of one drone are not.

    Two things deliberately do NOT end a note, both found on the first real library:

      A GLIDE. A pitch that drifts - under 0.35 semitone per frame - past the 0.7 limit is one
      sliding note, not a staircase: a creepy whistle came back as 204 "notes", G#5 G#5 G5 G5...
      A glide is written start~end, "G#4~E4".

      AN OCTAVE, without an onset. YIN's one real failure is jumping an octave (or an octave and
      a fifth) on a frame where a harmonic briefly wins, and a flanged violin read B3 B4 B3 B4.
      A struck octave has an onset; a detector error does not.

    The 30 dB floor is for decay tails, where the fundamental fades before the harmonics and the
    last notes of a kalimba read G3 C2 G1. Anything under three frames (~60 ms) is not kept.
*/
void SegmentNotes(const std::vector<PitchFrame>& track, const std::vector<int>& onsets, const std::vector<double>& rises,
                  double floor_db, SampleAnalysis& a){
    const double kRestrikeDb = 18.0;
    struct Note{
        std::vector<double> midis;
        double start = 0;
        double end = 0;         //last voiced frame
        bool f_glide = false;
    };
    std::vector<Note> notes;
    Note cur;
    size_t next_onset = 0;
    int gap = 0;

    auto close = [&](){
        if (cur.midis.size() >= 3) notes.push_back(cur);
        cur = Note();
    };
    auto open = [&](const PitchFrame& f){
        cur = Note();
        cur.midis.push_back(f.midi);
        cur.start = cur.end = f.t;
    };

    for (size_t i = 0; i < track.size(); i++){
        const PitchFrame& f = track[i];
        //Any onset between the previous frame and this one? And was it a strong one?
        bool f_onset = false, f_restrike = false;
        while (next_onset < onsets.size() && onsets[next_onset] * 0.01 <= f.t){
            f_onset = true;
            if (rises[next_onset] >= kRestrikeDb) f_restrike = true;
            next_onset++;
        }
        if (!f.f_voiced || f.db < floor_db){
            if (++gap >= 2) close();
            continue;
        }
        gap = 0;
        if (cur.midis.empty()){
            //A dropout, not a new note: the same pitch picked up again within half a second
            //with no strong onset. Tremolo troughs and reverb-smeared flutter both lose the
            //pitch for a few frames, and without this the tremolo E4 was still nine notes.
            if (!f_restrike && !notes.empty() && f.t - notes.back().end < 0.5 &&
                std::fabs(f.midi - Median(notes.back().midis)) < 0.7){
                cur = notes.back();
                notes.pop_back();
                cur.midis.push_back(f.midi);
                cur.end = f.t;
                continue;
            }
            open(f);
            continue;
        }
        const double dev = f.midi - Median(cur.midis);
        const double step = f.midi - cur.midis.back();
        const double adev = std::fabs(dev);
        const bool f_octave = std::fabs(adev - 12.0) < 0.7 || std::fabs(adev - 19.0) < 0.7 || std::fabs(adev - 24.0) < 0.7;

        if (adev > 0.7 && f_octave && !f_onset) continue;      //detector error, not a note
        if (adev > 0.7 && std::fabs(step) < 0.35){
            cur.f_glide = true;
            cur.midis.push_back(f.midi);
            cur.end = f.t;
        }
        else if (adev > 0.7){
            bool f_stays = i + 2 < track.size();
            for (size_t k = 1; k <= 2 && f_stays; k++){
                f_stays = track[i + k].f_voiced && std::fabs(track[i + k].midi - f.midi) < 0.7;
            }
            if (!f_stays) continue;     //a glitch, not a new note
            close();
            open(f);
        }
        //The 0.08 s guard is for an onset and a pitch jump that mark the SAME note start, a
        //frame or two apart; either one alone starts the note, and both must not start two.
        else if (f_restrike && f.t - cur.start > 0.08){
            close();
            open(f);
        }
        else{
            cur.midis.push_back(f.midi);
            cur.end = f.t;
        }
    }
    close();

    a.note_count = (int)notes.size();
    bool used[12] = {false};
    for (size_t i = 0; i < notes.size(); i++){
        const Note& n = notes[i];
        const int m = (int)std::lround(Median(n.midis));
        used[((m % 12) + 12) % 12] = true;
        std::string name = NoteName(m);
        if (n.f_glide){
            //Ends from three frames each, so one stray frame at either end does not name it.
            const size_t k = std::min<size_t>(3, n.midis.size());
            const int from = (int)std::lround(Median(std::vector<double>(n.midis.begin(), n.midis.begin() + k)));
            const int to = (int)std::lround(Median(std::vector<double>(n.midis.end() - k, n.midis.end())));
            if (from != to) name = NoteName(from) + "~" + NoteName(to);
        }
        if (i < 32){
            if (!a.notes.empty()) a.notes += " ";
            a.notes += name;
        }
    }
    if (notes.size() > 32) a.notes += " ...";
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    for (int pc = 0; pc < 12; pc++){
        if (!used[pc]) continue;
        if (!a.pitch_classes.empty()) a.pitch_classes += " ";
        a.pitch_classes += names[pc];
    }
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
    std::vector<int> onsets;
    MeasureRhythm(env, a, onsets);
    MeasurePitch(mono, env, onsets, a);
    //One note has no key - the profile of a lone A4 correlates best with A major for no
    //reason but its harmonics - so a single-pitch file answers with its note instead.
    if (a.pitch_kind != "single") EstimateKey(chroma, a);

    a.f_ok = true;
    return true;
}
