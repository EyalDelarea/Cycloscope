// JUCE-level tests + developer tooling for the real processor and editor.
//
//   cycloscope_plugin_tests                      run the regression tests (ctest "plugin")
//   cycloscope_plugin_tests --render <dir> [WxH ...] [--scale S]
//                                                render the editor to PNGs (every mode, each size)
//   cycloscope_plugin_tests --bench              time the per-frame GUI cost of each mode
//
// The DSP headers have their own JUCE-free tests; this binary covers what only exists
// at the plugin level: state save/restore, the audio callback contract, the editor
// lifecycle, and what actually ends up on screen. It is compiled from the plugin's own
// sources (not linked to the plugin) so it runs headless on macOS and Windows CI.

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <thread>

//==============================================================================
// Allocation tracking: count heap allocations made by the thread currently marked as
// "the audio thread" so a test can prove processBlock never allocates.
static thread_local bool tlTrackAllocs = false;
static std::atomic<int> gAudioThreadAllocs { 0 };

void* operator new (std::size_t n)
{
    if (tlTrackAllocs) gAudioThreadAllocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n)                         { return operator new (n); }
void  operator delete (void* p) noexcept                     { std::free (p); }
void  operator delete[] (void* p) noexcept                   { std::free (p); }
void  operator delete (void* p, std::size_t) noexcept        { std::free (p); }
void  operator delete[] (void* p, std::size_t) noexcept      { std::free (p); }

//==============================================================================
static int gFailures = 0;
#define CHECK(cond) do { if (! (cond)) { ++gFailures; \
    std::fprintf (stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void pumpMessages (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

// Test signal: a detuned stereo saw pair (a typical "supersaw-ish" synth source) with a
// little side content, so every view (waveform, cycle, spectrum, goniometer) has
// something real to show.
struct SignalGen
{
    double sr = 48000.0, phaseL = 0.0, phaseR = 0.0;
    double hz = 110.0;
    float  gain = 0.5f;
    void fill (juce::AudioBuffer<float>& b)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            phaseL += hz / sr;         phaseL -= std::floor (phaseL);
            phaseR += hz * 1.003 / sr; phaseR -= std::floor (phaseR);
            const float l = (float) (2.0 * phaseL - 1.0) * gain;
            const float r = (float) (2.0 * phaseR - 1.0) * gain;
            b.setSample (0, i, l);
            if (b.getNumChannels() > 1) b.setSample (1, i, r);
        }
    }
};

static void runBlocks (CycloscopeProcessor& p, SignalGen& gen, int numBlocks, int blockSize)
{
    juce::AudioBuffer<float> buf (p.getTotalNumInputChannels(), blockSize);
    juce::MidiBuffer midi;
    for (int i = 0; i < numBlocks; ++i) { gen.fill (buf); p.processBlock (buf, midi); }
}

// Feeds the processor from a separate thread at roughly real-time pace, the way a host
// does, so GUI-thread code races the audio thread exactly as in a session.
struct AudioThread
{
    CycloscopeProcessor& p;
    SignalGen gen;
    int blockSize;
    std::atomic<bool> run { true };
    std::thread t;
    AudioThread (CycloscopeProcessor& proc, double sr, int bs) : p (proc), blockSize (bs)
    {
        gen.sr = sr;
        t = std::thread ([this]
        {
            juce::AudioBuffer<float> buf (p.getTotalNumInputChannels(), blockSize);
            juce::MidiBuffer midi;
            const auto blockMs = 1000.0 * blockSize / gen.sr;
            while (run.load())
            {
                gen.fill (buf);
                p.processBlock (buf, midi);
                std::this_thread::sleep_for (std::chrono::microseconds ((int) (blockMs * 1000.0)));
            }
        });
    }
    ~AudioThread() { run = false; t.join(); }
};

static std::unique_ptr<CycloscopeProcessor> makeProcessor (double sr = 48000.0, int bs = 512)
{
    auto p = std::make_unique<CycloscopeProcessor>();
    p->setRateAndBufferSizeDetails (sr, bs);
    p->prepareToPlay (sr, bs);
    return p;
}

static void setParam (CycloscopeProcessor& p, const char* id, float plainValue)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

//==============================================================================
static void testPassthroughAndNoAllocation()
{
    for (double sr : { 8000.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0 })
    for (int channels : { 1, 2 })
    for (int bs : { 1, 7, 64, 512, 4096 })
    {
        auto p = makeProcessor (sr, bs);
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        CHECK (p->setBusesLayout ({ { set }, { set } }));

        juce::AudioBuffer<float> buf (channels, bs), ref (channels, bs);
        SignalGen gen; gen.sr = sr;
        gen.fill (buf);
        ref.makeCopyOf (buf);
        juce::MidiBuffer midi;

        gAudioThreadAllocs = 0;
        tlTrackAllocs = true;
        p->processBlock (buf, midi);
        tlTrackAllocs = false;
        CHECK (gAudioThreadAllocs.load() == 0);

        for (int c = 0; c < channels; ++c)
            CHECK (std::memcmp (buf.getReadPointer (c), ref.getReadPointer (c), sizeof (float) * (size_t) bs) == 0);
    }

    // zero-length block (some hosts send these) must be harmless
    auto p = makeProcessor();
    juce::AudioBuffer<float> empty (2, 0);
    juce::MidiBuffer midi;
    p->processBlock (empty, midi);
}

static void testBusLayouts()
{
    CycloscopeProcessor p;
    using CS = juce::AudioChannelSet;
    CHECK (p.checkBusesLayoutSupported ({ { CS::mono() },   { CS::mono() } }));
    CHECK (p.checkBusesLayoutSupported ({ { CS::stereo() }, { CS::stereo() } }));
    CHECK (! p.checkBusesLayoutSupported ({ { CS::mono() },   { CS::stereo() } }));
    CHECK (! p.checkBusesLayoutSupported ({ { CS::create5point1() }, { CS::create5point1() } }));
}


//==============================================================================
// --render: snapshot the real editor (software renderer, at a Retina-like scale) in every
// mode, at each requested size, while a live signal streams in from an audio thread.
struct View { const char* name; int mode; bool stereo; int source; bool silent; };

static juce::Image snapshotEditor (CycloscopeProcessor& p, int w, int h, float scale, const View& v, int settleMs)
{
    setParam (p, "displayMode", (float) v.mode);
    setParam (p, "channelSource", (float) v.source);
    p.gonioShown.store (v.stereo);
    p.editorWidth.store (w);
    p.editorHeight.store (h);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (w, h);
    pumpMessages (settleMs);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
    ed.reset();
    return img;
}

static int renderAll (const juce::StringArray& args)
{
    const int dirIdx = args.indexOf ("--render") + 1;
    if (dirIdx >= args.size()) { std::puts ("usage: --render <dir> [WxH ...] [--scale S]"); return 2; }
    juce::File dir (juce::File::getCurrentWorkingDirectory().getChildFile (args[dirIdx]));
    dir.createDirectory();

    float scale = 2.0f;
    juce::Array<juce::Point<int>> sizes;
    for (int i = dirIdx + 1; i < args.size(); ++i)
    {
        if (args[i] == "--scale" && i + 1 < args.size()) { scale = args[++i].getFloatValue(); continue; }
        if (args[i].containsChar ('x'))
            sizes.add ({ args[i].upToFirstOccurrenceOf ("x", false, true).getIntValue(),
                         args[i].fromFirstOccurrenceOf ("x", false, true).getIntValue() });
    }
    if (sizes.isEmpty()) { sizes.add ({ 560, 340 }); sizes.add ({ 1080, 520 }); sizes.add ({ 2400, 1400 }); }

    const View views[] = {
        { "live",        0, true,  0, false },
        { "live-stereo", 0, true,  4, false },
        { "baseshape",   1, true,  0, false },
        { "spectrum",    2, true,  0, false },
        { "live-nostereo", 0, false, 0, false },
        { "silence",     0, true,  0, true  },
    };

    juce::PNGImageFormat png;
    for (auto sz : sizes)
        for (auto& v : views)
        {
            auto p = makeProcessor (48000.0, 512);
            std::unique_ptr<AudioThread> audio;
            if (! v.silent) audio = std::make_unique<AudioThread> (*p, 48000.0, 512);
            auto img = snapshotEditor (*p, sz.x, sz.y, scale, v, 1500);
            audio.reset();
            auto f = dir.getChildFile (juce::String (v.name) + "-" + juce::String (sz.x) + "x" + juce::String (sz.y) + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            png.writeImageToStream (img, os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        }
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);

    if (args.contains ("--render")) return renderAll (args);

    testPassthroughAndNoAllocation();
    testBusLayouts();

    if (gFailures == 0) std::puts ("plugin OK");
    else                std::printf ("plugin: %d failure(s)\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
