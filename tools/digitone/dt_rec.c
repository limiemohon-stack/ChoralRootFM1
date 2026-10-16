/* dt_rec: record one CoreAudio input device to a 32-bit float WAV, every sample.
 *
 *   dt_rec DEVICE_NAME OUT.wav
 *
 * Built by tools/digitone/build_rec.sh into build/dt_rec. Records the input device
 * whose name matches DEVICE_NAME exactly (lists the input devices and exits 2 when
 * none does) at its native sample rate and channel count through an AUHAL input
 * unit, until it reads 'q' or EOF on stdin, or gets SIGINT/SIGTERM. Exits 0.
 *
 * stdout, line by line:
 *   start S    as soon as the first buffer arrives: host time of the first sample in
 *              seconds, mach_absolute_time based (the clock of Python's
 *              time.monotonic() on macOS)
 *   frames N overruns K ring R jumps J errors E zero_filled Z dropped D end T
 *              at the end. K = R + J + E counts every discontinuity: R ring overruns
 *              (block dropped, D frames lost), J sample-time jumps between callbacks,
 *              E render errors. Missing frames of a forward jump or a render error are
 *              written as Z zero frames so the file keeps the device timeline. T is the
 *              host time just after the last sample. K must be 0 for a clean take.
 * stderr: device, format, buffer size and latency information.
 *
 * The recording stops at the sample acquired at the host time the stop was asked
 * for ('q' read, EOF, signal): it keeps capturing until a buffer reaches that time
 * and keeps only the frames before it.
 *
 * Real-time callback: no allocation, no locks, no I/O; it renders into a buffer
 * allocated up front and copies into a lock-free single-producer ring (about 43 s);
 * a writer thread moves the ring to disk.
 */
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <errno.h>
#include <mach/mach_time.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RING_FRAMES (1u << 21) /* power of two; 43.7 s at 48 kHz */
#define RENDER_CAP 16384       /* frames per callback we can take */

static struct {
    AudioUnit au;
    UInt32 ch, bpf; /* channels, bytes per frame */
    double rate;
    double tick_s; /* seconds per mach tick */
    float *render;
    AudioBufferList abl;
    float *ring;
    _Atomic uint64_t wpos, rpos; /* frames */
    /* callback state */
    double expected;
    _Atomic int started, done;
    uint64_t first_host;
    double first_sample;
    _Atomic uint64_t stop_host; /* 0: not asked */
    _Atomic uint64_t end_host_ns;
    _Atomic uint64_t n_ring, n_jumps, n_err, zero_filled, dropped, lat_max_us, n_cb;
    /* writer */
    FILE *f;
    _Atomic int finish, io_error;
} g;

static volatile sig_atomic_t g_sig;
static volatile uint64_t g_sig_host;

static void on_signal(int s)
{
    (void)s;
    if (!g_sig)
        g_sig_host = mach_absolute_time();
    g_sig = 1;
}

static double host_s(uint64_t t) { return (double)t * g.tick_s; }

/* ---------------------------------------------------------------- ring */

static uint64_t ring_free(void)
{
    uint64_t w = atomic_load_explicit(&g.wpos, memory_order_relaxed);
    uint64_t r = atomic_load_explicit(&g.rpos, memory_order_acquire);
    return RING_FRAMES - (w - r);
}

/* src NULL: zeros. Caller checked ring_free. */
static void ring_put(const float *src, uint64_t n)
{
    uint64_t w = atomic_load_explicit(&g.wpos, memory_order_relaxed);
    uint64_t done = 0;
    while (done < n) {
        uint64_t at = (w + done) & (RING_FRAMES - 1);
        uint64_t k = n - done;
        if (k > RING_FRAMES - at)
            k = RING_FRAMES - at;
        float *dst = g.ring + at * g.ch;
        if (src)
            memcpy(dst, src + done * g.ch, (size_t)(k * g.bpf));
        else
            memset(dst, 0, (size_t)(k * g.bpf));
        done += k;
    }
    atomic_store_explicit(&g.wpos, w + n, memory_order_release);
}

/* frames that are missing from the stream: zeros if they fit, else counted lost */
static void ring_gap(uint64_t n)
{
    if (n > RING_FRAMES / 2 || ring_free() < n) {
        atomic_fetch_add(&g.n_ring, 1);
        atomic_fetch_add(&g.dropped, n);
        return;
    }
    ring_put(NULL, n);
    atomic_fetch_add(&g.zero_filled, n);
}

/* ------------------------------------------------------------ callback */

static OSStatus input_cb(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts,
                         UInt32 bus, UInt32 n, AudioBufferList *unused)
{
    (void)ref;
    (void)bus;
    (void)unused;
    if (atomic_load_explicit(&g.done, memory_order_acquire))
        return noErr;
    uint64_t now = mach_absolute_time();
    atomic_fetch_add_explicit(&g.n_cb, 1, memory_order_relaxed);

    OSStatus e = -1;
    if (n <= RENDER_CAP) {
        g.abl.mNumberBuffers = 1;
        g.abl.mBuffers[0].mNumberChannels = g.ch;
        g.abl.mBuffers[0].mDataByteSize = n * g.bpf;
        g.abl.mBuffers[0].mData = g.render;
        e = AudioUnitRender(g.au, flags, ts, 1, n, &g.abl);
    }

    if (!atomic_load_explicit(&g.started, memory_order_relaxed)) {
        g.first_host = ts->mHostTime;
        g.first_sample = ts->mSampleTime;
        g.expected = ts->mSampleTime;
        atomic_store_explicit(&g.started, 1, memory_order_release);
    }
    double d = ts->mSampleTime - g.expected;
    if (fabs(d) >= 0.5) {
        atomic_fetch_add(&g.n_jumps, 1);
        if (d > 0)
            ring_gap((uint64_t)llround(d));
    }
    g.expected = ts->mSampleTime + n;
    if (now > ts->mHostTime) {
        uint64_t us = (uint64_t)(host_s(now - ts->mHostTime) * 1e6);
        if (us > atomic_load_explicit(&g.lat_max_us, memory_order_relaxed))
            atomic_store_explicit(&g.lat_max_us, us, memory_order_relaxed);
    }

    /* stop: keep frames acquired before stop_host */
    uint64_t keep = n;
    int last = 0;
    uint64_t stop = atomic_load_explicit(&g.stop_host, memory_order_acquire);
    double t0 = host_s(ts->mHostTime);
    if (stop) {
        double ts_stop = host_s(stop);
        if (ts_stop < t0 + n / g.rate) {
            double k = floor((ts_stop - t0) * g.rate + 0.5);
            keep = k <= 0 ? 0 : k >= n ? n : (uint64_t)k;
            last = 1;
        }
    }

    if (keep) {
        if (e != noErr) {
            atomic_fetch_add(&g.n_err, 1);
            ring_gap(keep);
        } else if (ring_free() < keep) {
            atomic_fetch_add(&g.n_ring, 1);
            atomic_fetch_add(&g.dropped, keep);
        } else {
            ring_put(g.render, keep);
        }
    }
    atomic_store_explicit(&g.end_host_ns, (uint64_t)((t0 + keep / g.rate) * 1e9),
                          memory_order_relaxed);
    if (last)
        atomic_store_explicit(&g.done, 1, memory_order_release);
    return noErr;
}

/* -------------------------------------------------------------- writer */

static void put16(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

#define HDR 58 /* RIFF 12 + fmt 8+18 + fact 8+4 + data 8 */

static void wav_header(uint8_t *h, uint64_t frames)
{
    uint64_t data = frames * g.bpf;
    if (data > 0xFFFFFFFFull - HDR)
        data = 0xFFFFFFFFull - HDR;
    memcpy(h, "RIFF", 4);
    put32(h + 4, (uint32_t)(HDR - 8 + data));
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    put32(h + 16, 18);
    put16(h + 20, 3); /* WAVE_FORMAT_IEEE_FLOAT */
    put16(h + 22, g.ch);
    put32(h + 24, (uint32_t)g.rate);
    put32(h + 28, (uint32_t)g.rate * g.bpf);
    put16(h + 32, g.bpf);
    put16(h + 34, 32);
    put16(h + 36, 0); /* cbSize */
    memcpy(h + 38, "fact", 4);
    put32(h + 42, 4);
    put32(h + 46, (uint32_t)frames);
    memcpy(h + 50, "data", 4);
    put32(h + 54, (uint32_t)data);
}

static void *writer(void *arg)
{
    (void)arg;
    int printed = 0;
    for (;;) {
        if (!printed && atomic_load_explicit(&g.started, memory_order_acquire)) {
            printf("start %.6f\n", host_s(g.first_host));
            fflush(stdout);
            printed = 1;
        }
        uint64_t w = atomic_load_explicit(&g.wpos, memory_order_acquire);
        uint64_t r = atomic_load_explicit(&g.rpos, memory_order_relaxed);
        if (w == r) {
            if (atomic_load(&g.finish))
                break;
            usleep(5000);
            continue;
        }
        while (r < w) {
            uint64_t at = r & (RING_FRAMES - 1);
            uint64_t k = w - r;
            if (k > RING_FRAMES - at)
                k = RING_FRAMES - at;
            if (!atomic_load(&g.io_error) &&
                fwrite(g.ring + at * g.ch, g.bpf, (size_t)k, g.f) != (size_t)k)
                atomic_store(&g.io_error, 1);
            r += k;
            atomic_store_explicit(&g.rpos, r, memory_order_release);
        }
    }
    return NULL;
}

/* ------------------------------------------------------------- devices */

static char *cfstr(CFStringRef s)
{
    if (!s)
        return strdup("");
    CFIndex n = CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1;
    char *b = malloc((size_t)n);
    if (!CFStringGetCString(s, b, n, kCFStringEncodingUTF8))
        b[0] = 0;
    return b;
}

static char *dev_string(AudioObjectID d, AudioObjectPropertySelector sel)
{
    AudioObjectPropertyAddress a = {sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef s = NULL;
    UInt32 sz = sizeof s;
    if (AudioObjectGetPropertyData(d, &a, 0, NULL, &sz, &s) != noErr)
        return strdup("");
    char *r = cfstr(s);
    CFRelease(s);
    return r;
}

static UInt32 dev_input_channels(AudioObjectID d)
{
    AudioObjectPropertyAddress a = {kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeInput,
                                    kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(d, &a, 0, NULL, &sz) != noErr || sz == 0)
        return 0;
    AudioBufferList *l = malloc(sz);
    UInt32 ch = 0;
    if (AudioObjectGetPropertyData(d, &a, 0, NULL, &sz, l) == noErr)
        for (UInt32 i = 0; i < l->mNumberBuffers; i++)
            ch += l->mBuffers[i].mNumberChannels;
    free(l);
    return ch;
}

static UInt32 dev_u32(AudioObjectID d, AudioObjectPropertySelector sel, AudioObjectPropertyScope sc)
{
    AudioObjectPropertyAddress a = {sel, sc, kAudioObjectPropertyElementMain};
    UInt32 v = 0, sz = sizeof v;
    AudioObjectGetPropertyData(d, &a, 0, NULL, &sz, &v);
    return v;
}

static UInt32 stream_latency(AudioObjectID d)
{
    AudioObjectPropertyAddress a = {kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput,
                                    kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(d, &a, 0, NULL, &sz) != noErr || sz < sizeof(AudioStreamID))
        return 0;
    AudioStreamID s[16];
    if (sz > sizeof s)
        sz = sizeof s;
    if (AudioObjectGetPropertyData(d, &a, 0, NULL, &sz, s) != noErr)
        return 0;
    return dev_u32(s[0], kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal);
}

static AudioObjectID find_input(const char *name)
{
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, NULL, &sz) != noErr)
        return kAudioObjectUnknown;
    UInt32 n = sz / sizeof(AudioObjectID);
    AudioObjectID *ids = malloc(sz ? sz : 1);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, ids);
    AudioObjectID hit = kAudioObjectUnknown;
    for (UInt32 i = 0; i < n && hit == kAudioObjectUnknown; i++) {
        if (!dev_input_channels(ids[i]))
            continue;
        char *s = dev_string(ids[i], kAudioObjectPropertyName);
        if (!strcmp(s, name))
            hit = ids[i];
        free(s);
    }
    if (hit == kAudioObjectUnknown) {
        fprintf(stderr, "dt_rec: no input device named '%s'. Input devices:\n", name);
        for (UInt32 i = 0; i < n; i++) {
            UInt32 ch = dev_input_channels(ids[i]);
            if (!ch)
                continue;
            char *s = dev_string(ids[i], kAudioObjectPropertyName);
            fprintf(stderr, "  %s  (%u ch)\n", s, ch);
            free(s);
        }
    }
    free(ids);
    return hit;
}

/* ---------------------------------------------------------------- main */

#define CHECK(x, what)                                                              \
    do {                                                                            \
        OSStatus e_ = (x);                                                          \
        if (e_ != noErr) {                                                          \
            fprintf(stderr, "dt_rec: %s failed (OSStatus %d)\n", what, (int)e_);   \
            return 1;                                                               \
        }                                                                           \
    } while (0)

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: dt_rec DEVICE_NAME OUT.wav\n"
                        "  records until 'q' or EOF on stdin, SIGINT or SIGTERM\n");
        return 2;
    }
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    g.tick_s = (double)tb.numer / tb.denom * 1e-9;

    AudioObjectID dev = find_input(argv[1]);
    if (dev == kAudioObjectUnknown)
        return 2;
    char *uid = dev_string(dev, kAudioDevicePropertyDeviceUID);

    AudioComponentDescription desc = {kAudioUnitType_Output, kAudioUnitSubType_HALOutput,
                                      kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) {
        fprintf(stderr, "dt_rec: no AUHAL component\n");
        return 1;
    }
    CHECK(AudioComponentInstanceNew(comp, &g.au), "AudioComponentInstanceNew");
    UInt32 one = 1, zero = 0;
    CHECK(AudioUnitSetProperty(g.au, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &one,
                               sizeof one), "EnableIO input");
    CHECK(AudioUnitSetProperty(g.au, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &zero,
                               sizeof zero), "DisableIO output");
    CHECK(AudioUnitSetProperty(g.au, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev,
                               sizeof dev), "CurrentDevice");

    AudioStreamBasicDescription hw;
    UInt32 sz = sizeof hw;
    CHECK(AudioUnitGetProperty(g.au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &hw, &sz),
          "device stream format");
    Float64 nominal = 0;
    {
        AudioObjectPropertyAddress a = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                        kAudioObjectPropertyElementMain};
        UInt32 s2 = sizeof nominal;
        AudioObjectGetPropertyData(dev, &a, 0, NULL, &s2, &nominal);
    }
    g.rate = hw.mSampleRate > 0 ? hw.mSampleRate : nominal;
    g.ch = hw.mChannelsPerFrame;
    g.bpf = 4 * g.ch;
    if (g.ch == 0 || g.rate <= 0) {
        fprintf(stderr, "dt_rec: device reports %u channels at %.0f Hz\n", g.ch, g.rate);
        return 1;
    }

    AudioStreamBasicDescription cf = {0};
    cf.mSampleRate = g.rate;
    cf.mFormatID = kAudioFormatLinearPCM;
    cf.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    cf.mChannelsPerFrame = g.ch;
    cf.mBitsPerChannel = 32;
    cf.mFramesPerPacket = 1;
    cf.mBytesPerFrame = cf.mBytesPerPacket = g.bpf;
    CHECK(AudioUnitSetProperty(g.au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &cf,
                               sizeof cf), "client stream format");

    AURenderCallbackStruct cb = {input_cb, NULL};
    CHECK(AudioUnitSetProperty(g.au, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0,
                               &cb, sizeof cb), "SetInputCallback");

    g.render = calloc((size_t)RENDER_CAP * g.ch, sizeof(float));
    g.ring = calloc((size_t)RING_FRAMES * g.ch, sizeof(float));
    if (!g.render || !g.ring) {
        fprintf(stderr, "dt_rec: out of memory\n");
        return 1;
    }
    /* touch the pages now so the callback never faults them in */
    memset(g.ring, 0, (size_t)RING_FRAMES * g.bpf);
    memset(g.render, 0, (size_t)RENDER_CAP * g.bpf);

    g.f = fopen(argv[2], "wb");
    if (!g.f) {
        fprintf(stderr, "dt_rec: cannot write %s: %s\n", argv[2], strerror(errno));
        return 1;
    }
    static char fbuf[1 << 20];
    setvbuf(g.f, fbuf, _IOFBF, sizeof fbuf);
    uint8_t h[HDR];
    wav_header(h, 0);
    fwrite(h, 1, HDR, g.f);

    UInt32 bufsz = dev_u32(dev, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal);
    UInt32 lat = dev_u32(dev, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeInput);
    UInt32 saf = dev_u32(dev, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeInput);
    UInt32 slat = stream_latency(dev);
    fprintf(stderr, "dt_rec: '%s' uid %s, %.0f Hz (nominal %.0f), %u ch, hw format flags 0x%x %u bit; "
                    "buffer %u, input latency %u + safety %u + stream %u frames; writing float32 %s\n",
            argv[1], uid, g.rate, nominal, g.ch, (unsigned)hw.mFormatFlags, (unsigned)hw.mBitsPerChannel,
            bufsz, lat, saf, slat, argv[2]);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    pthread_t wt;
    pthread_create(&wt, NULL, writer, NULL);

    CHECK(AudioUnitInitialize(g.au), "AudioUnitInitialize");
    CHECK(AudioOutputUnitStart(g.au), "AudioOutputUnitStart");

    /* wait for 'q', EOF or a signal */
    uint64_t stop = 0;
    while (!stop) {
        struct pollfd p = {0, POLLIN, 0};
        int r = poll(&p, 1, 20);
        if (g_sig) {
            stop = g_sig_host;
            break;
        }
        if (atomic_load(&g.io_error)) {
            fprintf(stderr, "dt_rec: write error on %s\n", argv[2]);
            stop = mach_absolute_time();
            break;
        }
        if (r > 0) {
            char buf[256];
            ssize_t k = read(0, buf, sizeof buf);
            uint64_t now = mach_absolute_time();
            if (k <= 0 || memchr(buf, 'q', (size_t)k))
                stop = now;
        } else if (r < 0 && errno != EINTR) {
            stop = mach_absolute_time();
        }
    }
    atomic_store_explicit(&g.stop_host, stop, memory_order_release);

    /* the buffer holding the stop time arrives a buffer + safety offset later */
    for (int i = 0; i < 1000 && !atomic_load(&g.done); i++)
        usleep(2000);
    int timed_out = !atomic_load(&g.done);
    atomic_store(&g.done, 1);
    AudioOutputUnitStop(g.au);
    usleep(20000); /* a callback in flight sees done and returns */
    atomic_store(&g.finish, 1);
    pthread_join(wt, NULL);

    uint64_t frames = atomic_load(&g.rpos);
    int rc = 0;
    if (atomic_load(&g.io_error))
        rc = 1;
    wav_header(h, frames);
    if (fflush(g.f) || fseek(g.f, 0, SEEK_SET) || fwrite(h, 1, HDR, g.f) != HDR || fclose(g.f)) {
        fprintf(stderr, "dt_rec: cannot finish %s\n", argv[2]);
        rc = 1;
    }
    AudioUnitUninitialize(g.au);
    AudioComponentInstanceDispose(g.au);

    uint64_t nr = atomic_load(&g.n_ring), nj = atomic_load(&g.n_jumps), ne = atomic_load(&g.n_err);
    if (!atomic_load(&g.started)) {
        fprintf(stderr, "dt_rec: no audio buffer arrived\n");
        rc = 1;
    }
    if (timed_out && atomic_load(&g.started))
        fprintf(stderr, "dt_rec: no buffer reached the stop time within 2 s (device stopped?)\n");
    fprintf(stderr, "dt_rec: %llu callbacks, max callback delay after first sample %.1f ms\n",
            (unsigned long long)atomic_load(&g.n_cb), atomic_load(&g.lat_max_us) / 1000.0);
    printf("frames %llu overruns %llu ring %llu jumps %llu errors %llu zero_filled %llu dropped %llu "
           "end %.6f\n",
           (unsigned long long)frames, (unsigned long long)(nr + nj + ne), (unsigned long long)nr,
           (unsigned long long)nj, (unsigned long long)ne, (unsigned long long)atomic_load(&g.zero_filled),
           (unsigned long long)atomic_load(&g.dropped), atomic_load(&g.end_host_ns) / 1e9);
    fflush(stdout);
    return rc;
}
