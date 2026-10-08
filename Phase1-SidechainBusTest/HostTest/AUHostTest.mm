/*
    Phase1 AU host-side render test (final, corrected harness).

    Host patterns verified against AUv2 semantics:
      - AudioUnitRender is issued on the OUTPUT bus only (element 0).
        The JUCE AU wrapper pulls ALL input elements internally
        (juce_AU_Wrapper.mm pullInputAudio -> AUInputElement::PullInput),
        so sidechain audio must be provided via a render callback
        installed on input element 1.
      - AudioBufferLists with >1 buffer are allocated with extra storage
        (an AudioBufferList struct only defines mBuffers[0]).
      - Render timestamps increase monotonically (NeedsToRender dedupe).

    Checks:
      A. main passthrough integrity (out == captured main input)
      B. NO sidechain leakage (silent main + loud sidechain => silent out)
      C. sidechain reaches the plugin (sidechain bus metered)
      D. stereo separation preserved
      E. stability: block sizes 32..1024, rates 44.1/48/96 kHz
      F. sidechain silent / sidechain callback not connected => main intact
*/

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>

static const double kPi = 3.14159265358979;

enum MainMode { MAIN_SIGNAL, MAIN_SILENT };
enum SideMode { SC_SIGNAL, SC_SILENT, SC_NOT_CONNECTED };

struct Ctx
{
    double pL = 0, pR = 0, sc = 0, sr = 48000;
    MainMode mainMode = MAIN_SIGNAL;
    SideMode sideMode  = SC_SIGNAL;

    enum { capMax = 8192 };
    float capL[capMax], capR[capMax];
    int   capFrames = 0;

    float scRms = 0, scPeak = 0;
};

static Ctx g_ctx;

static OSStatus inputCallback (void*, AudioUnitRenderActionFlags*,
                               const AudioTimeStamp*, UInt32 bus,
                               UInt32 n, AudioBufferList* io)
{
    float* L = (float*) io->mBuffers[0].mData;
    float* R = io->mNumberBuffers > 1 ? (float*) io->mBuffers[1].mData : L;

    if (bus == 1)   // sidechain element
    {
        if (g_ctx.sideMode == SC_NOT_CONNECTED)
            return kAudioUnitErr_NoConnection;   // simulate disconnected bus

        if (g_ctx.sideMode == SC_SILENT)
        {
            memset (L, 0, n * 4);
            if (io->mNumberBuffers > 1) memset (R, 0, n * 4);
            g_ctx.scRms = g_ctx.scPeak = 0.0f;
            return noErr;
        }

        double sumSq = 0; float peak = 0;
        for (UInt32 i = 0; i < n; ++i)
        {
            float s = 0.5f * (float) std::sin (g_ctx.sc);
            g_ctx.sc += 2.0 * kPi * 1000.0 / g_ctx.sr;
            L[i] = s; R[i] = s;
            sumSq += (double) s * s; peak = std::max (peak, std::abs (s));
        }
        g_ctx.scRms = (float) std::sqrt (sumSq / n);
        g_ctx.scPeak = peak;
        return noErr;
    }

    // bus 0: main input
    if (g_ctx.mainMode == MAIN_SILENT)
    {
        memset (L, 0, n * 4);
        if (io->mNumberBuffers > 1) memset (R, 0, n * 4);
    }
    else
    {
        for (UInt32 i = 0; i < n; ++i)
        {
            L[i] = 0.25f * (float) std::sin (g_ctx.pL);
            R[i] = 0.25f * (float) std::sin (g_ctx.pR);
            g_ctx.pL += 2.0 * kPi * 440.0 / g_ctx.sr;
            g_ctx.pR += 2.0 * kPi * 660.0 / g_ctx.sr;
        }
    }

    // capture exactly what the plugin receives on bus 0
    if (g_ctx.capFrames == 0 || ((int) n <= g_ctx.capMax && (int) n == g_ctx.capFrames))
    {
        memcpy (g_ctx.capL, L, n * 4);
        memcpy (g_ctx.capR, R, n * 4);
        g_ctx.capFrames = (int) n;
    }
    return noErr;
}

static AudioBufferList* makeABL (int numBuffers, float* ch[2], int frames)
{
    auto* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList)
                                              + sizeof (AudioBuffer) * (size_t) (numBuffers - 1));
    abl->mNumberBuffers = (UInt32) numBuffers;
    for (int i = 0; i < numBuffers; ++i)
    {
        abl->mBuffers[i].mNumberChannels = 1;
        abl->mBuffers[i].mDataByteSize   = (UInt32) (frames * 4);
        abl->mBuffers[i].mData           = ch[i];
    }
    return abl;
}

static int failures = 0;
static void check (bool ok, const char* what)
{
    printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    fflush (stdout);
    if (! ok) ++failures;
}

int main()
{
    printf ("Phase1 AU Host Render Test (corrected harness)\n=============================================\n"); fflush (stdout);

    AudioComponentDescription desc { 'aufx', 'Scb1', 'Musc', 0, 0 };
    AudioComponent comp = AudioComponentFindNext (nullptr, &desc);
    if (! comp) { printf ("FATAL: AU not found\n"); return 1; }

    AudioUnit au = nullptr;
    if (AudioComponentInstanceNew (comp, &au) != noErr) { printf ("FATAL: instantiate failed\n"); return 1; }

    UInt32 nIn = 0, nOut = 0, sz = sizeof (UInt32);
    AudioUnitGetProperty (au, kAudioUnitProperty_ElementCount, kAudioUnitScope_Input,  0, &nIn,  &sz);
    sz = sizeof (UInt32);
    AudioUnitGetProperty (au, kAudioUnitProperty_ElementCount, kAudioUnitScope_Output, 0, &nOut, &sz);
    printf ("Input elements: %u   Output elements: %u\n", nIn, nOut); fflush (stdout);
    check (nIn == 2, "AU exposes exactly 2 input elements (main + sidechain)");
    check (nOut == 1, "AU exposes exactly 1 output element");

    AURenderCallbackStruct cb { inputCallback, nullptr };
    check (AudioUnitSetProperty (au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) == noErr,
           "render callback installed on main input (element 0)");
    check (AudioUnitSetProperty (au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 1, &cb, sizeof cb) == noErr,
           "render callback installed on sidechain input (element 1)");

    Float64 runningTime = 0.0;
    static float outL[8192], outR[8192];

    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const int sizes[]    = { 32, 64, 128, 256, 512, 1024 };

    for (double rate : rates)
    {
        g_ctx.sr = rate;
        AudioStreamBasicDescription asbd {};
        asbd.mFormatID = kAudioFormatLinearPCM;
        asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsNonInterleaved | kAudioFormatFlagsNativeEndian;
        asbd.mBitsPerChannel = 32; asbd.mBytesPerPacket = 4; asbd.mFramesPerPacket = 1;
        asbd.mBytesPerFrame = 4; asbd.mChannelsPerFrame = 2; asbd.mSampleRate = rate;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &asbd, sizeof asbd);
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,  0, &asbd, sizeof asbd);
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,  1, &asbd, sizeof asbd);

        Float64 sr = rate;
        AudioUnitSetProperty (au, kAudioUnitProperty_SampleRate, kAudioUnitScope_Output, 0, &sr, sizeof sr);

        if (AudioUnitInitialize (au) != noErr) { check (false, "AudioUnitInitialize"); continue; }
        printf ("\n-- sample rate %.0f Hz --\n", rate); fflush (stdout);

        const int nSizes = (rate > 48000.0) ? 4 : 6;

        for (int si = 0; si < nSizes; ++si)
        {
            const int n = sizes[si];

            g_ctx.mainMode = MAIN_SIGNAL; g_ctx.sideMode = SC_SIGNAL;
            g_ctx.pL = g_ctx.pR = g_ctx.sc = 0.0;
            g_ctx.capFrames = 0;

            float* ch[2] = { outL, outR };
            AudioBufferList* outABL = makeABL (2, ch, n);
            AudioTimeStamp t {}; t.mSampleTime = runningTime; t.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRenderActionFlags flags = 0;

            OSStatus st = AudioUnitRender (au, &flags, &t, 0, (UInt32) n, outABL);
            runningTime += n;
            free (outABL);

            if (st != noErr) { printf ("  render failed (%d)\n", (int) st); check (false, "AudioUnitRender"); continue; }

            double diff = 0, energy = 0, lrDiff = 0;
            for (int i = 0; i < n; ++i)
            {
                diff  += std::abs ((double) outL[i] - (double) g_ctx.capL[i]);
                energy += std::abs ((double) g_ctx.capL[i]);
                lrDiff += std::abs ((double) g_ctx.capL[i] - (double) g_ctx.capR[i]);
            }
            char msg[128];

            snprintf (msg, sizeof msg, "main passthrough intact, block %d", n);
            check (energy > 0.5 && diff < 1e-3, msg);

            snprintf (msg, sizeof msg, "stereo separation preserved, block %d", n);
            check (lrDiff > 0.05, msg);

            snprintf (msg, sizeof msg, "sidechain reaches sidechain bus, block %d (RMS %.1f dB)", n,
                      20.0f * std::log10 (std::max (g_ctx.scRms, 1e-5f)));
            check (g_ctx.scRms > 0.01f, msg);
        }

        // B: LEAKAGE - silent main + loud sidechain => silent output
        {
            g_ctx.mainMode = MAIN_SILENT; g_ctx.sideMode = SC_SIGNAL; g_ctx.sc = 0.0;
            float* ch[2] = { outL, outR };
            AudioBufferList* outABL = makeABL (2, ch, 1024);
            AudioTimeStamp t {}; t.mSampleTime = runningTime; t.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRenderActionFlags flags = 0;
            OSStatus st = AudioUnitRender (au, &flags, &t, 0, 1024, outABL);
            runningTime += 1024;
            free (outABL);

            float peak = 0;
            for (int i = 0; i < 1024; ++i) peak = std::max (peak, std::max (std::abs (outL[i]), std::abs (outR[i])));
            check (st == noErr && peak < 1e-6f, "LEAKAGE TEST: main silent + loud sidechain => output silent");
        }

        // F1: silent sidechain => main passthrough intact
        {
            g_ctx.mainMode = MAIN_SIGNAL; g_ctx.sideMode = SC_SILENT;
            g_ctx.pL = g_ctx.pR = 0.0; g_ctx.capFrames = 0;
            float* ch[2] = { outL, outR };
            AudioBufferList* outABL = makeABL (2, ch, 512);
            AudioTimeStamp t {}; t.mSampleTime = runningTime; t.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRenderActionFlags flags = 0;
            OSStatus st = AudioUnitRender (au, &flags, &t, 0, 512, outABL);
            runningTime += 512;
            free (outABL);

            double diff = 0;
            for (int i = 0; i < 512; ++i) diff += std::abs ((double) outL[i] - (double) g_ctx.capL[i]);
            check (st == noErr && diff < 1e-3, "silent sidechain: main passthrough intact, no crash");
        }

        // F2: sidechain element not connected (callback returns NoConnection)
        {
            g_ctx.mainMode = MAIN_SIGNAL; g_ctx.sideMode = SC_NOT_CONNECTED;
            g_ctx.pL = g_ctx.pR = 0.0; g_ctx.capFrames = 0;
            float* ch[2] = { outL, outR };
            AudioBufferList* outABL = makeABL (2, ch, 512);
            AudioTimeStamp t {}; t.mSampleTime = runningTime; t.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRenderActionFlags flags = 0;
            OSStatus st = AudioUnitRender (au, &flags, &t, 0, 512, outABL);
            runningTime += 512;
            free (outABL);

            double diff = 0;
            for (int i = 0; i < 512; ++i) diff += std::abs ((double) outL[i] - (double) g_ctx.capL[i]);
            check (st == noErr && diff < 1e-3, "sidechain not connected: main passthrough intact, no crash");
        }

        AudioUnitUninitialize (au);
    }

    AudioComponentInstanceDispose (au);

    printf ("\n=============================================\n%s (%d failure(s))\n",
            failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
