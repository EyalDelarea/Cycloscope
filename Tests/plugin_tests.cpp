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
#include <cstdlib>
#include <ctime>
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
// Editor helpers
template <typename T>
static T* findChild (juce::Component& parent)
{
    for (auto* c : parent.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (c)) return t;
        if (auto* t = findChild<T> (*c)) return t;
    }
    return nullptr;
}

// Pixels that belong to the bright orange trace (accent 0xffff8a2b), not the dim fill/glow.
static int countTracePixels (const juce::Image& img)
{
    int n = 0;
    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
        {
            const auto c = bd.getPixelColour (x, y);
            if (c.getRed() >= 200 && c.getGreen() >= 100 && c.getGreen() <= 170 && c.getBlue() <= 90) ++n;
        }
    return n;
}

//==============================================================================
// A single NaN/Inf block from upstream (a glitching plugin before us) must not leave
// the display permanently dead. The spectrum ballistics and the Base Shape cycle
// average both carry history, so one poisoned frame used to stick forever.
static void testNonFiniteInputRecovers()
{
    for (int mode : { 1, 2 })
    {
        auto p = makeProcessor();
        setParam (*p, "displayMode", (float) mode);
        SignalGen gen;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (1080, 520);
        auto* scope = findChild<ScopeComponent> (*ed);
        CHECK (scope != nullptr);
        if (scope == nullptr) return;

        auto frame = [&] (int blocks)
        {
            runBlocks (*p, gen, blocks, 512);
            pumpMessages (40);
            return scope->createComponentSnapshot (scope->getLocalBounds(), true, 1.0f);
        };
        for (int i = 0; i < 10; ++i) frame (5);
        const int healthy = countTracePixels (frame (5));
        CHECK (healthy > 300);

        // poison: one block of garbage; audio must still pass through bit-for-bit
        const float garbage[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                  -std::numeric_limits<float>::infinity(), 1.0e30f };
        for (float g : garbage)
        {
            juce::AudioBuffer<float> bad (2, 512);
            for (int i = 0; i < 512; ++i) { bad.setSample (0, i, g); bad.setSample (1, i, i % 2 ? g : 0.0f); }
            juce::AudioBuffer<float> ref; ref.makeCopyOf (bad);
            juce::MidiBuffer midi;
            p->processBlock (bad, midi);
            for (int c = 0; c < 2; ++c)
                CHECK (std::memcmp (bad.getReadPointer (c), ref.getReadPointer (c), sizeof (float) * 512) == 0);
            frame (0);
        }

        // one second of clean signal later the trace must be back
        for (int i = 0; i < 20; ++i) frame (5);
        const auto afterImg = frame (5);
        const int after = countTracePixels (afterImg);
        if (auto* dump = std::getenv ("CYCLOSCOPE_TEST_DUMP")) // optional: look at what the test saw
        {
            juce::File f (juce::String (dump) + "/nonfinite-mode" + juce::String (mode) + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (afterImg, os);
        }
        std::printf ("  non-finite recovery, mode %d: trace pixels before %d, after %d\n", mode, healthy, after);
        CHECK (after > healthy / 2);
    }
}

//==============================================================================
static juce::MemoryBlock stateOf (CycloscopeProcessor& p)
{
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    return mb;
}

static void testStateRoundTrip()
{
    auto a = makeProcessor();
    setParam (*a, "triggerMode", 2.0f);
    setParam (*a, "threshold", -0.25f);
    setParam (*a, "timeZoom", 12.0f);
    setParam (*a, "displayMode", 1.0f);
    setParam (*a, "channelSource", 3.0f);
    a->editorWidth = 900; a->editorHeight = 600; a->gonioWidth = 300; a->gonioShown = false;
    const auto mb = stateOf (*a);

    auto b = makeProcessor();
    b->setStateInformation (mb.getData(), (int) mb.getSize());
    for (auto* id : { "triggerMode", "threshold", "timeZoom", "displayMode", "channelSource", "ampZoom", "cycles" })
        CHECK (std::abs (a->apvts.getRawParameterValue (id)->load() - b->apvts.getRawParameterValue (id)->load()) < 1.0e-6f);
    CHECK (b->editorWidth == 900 && b->editorHeight == 600 && b->gonioWidth == 300 && ! b->gonioShown);
}

// A session saved by v0.1.0 (it still carries the since-removed stereoDecay parameter)
// must load exactly: parameters by index, window and stereo-panel sizes verbatim.
static void testLegacyStateLoads()
{
    const char* legacy =
        R"(<PARAMS editorWidth="1300" editorHeight="700" gonioWidth="260" gonioShown="1">)"
        R"(<PARAM id="triggerMode" value="2.0"/><PARAM id="threshold" value="0.5"/>)"
        R"(<PARAM id="timeZoom" value="8.0"/><PARAM id="ampZoom" value="2.0"/>)"
        R"(<PARAM id="freeze" value="0.0"/><PARAM id="displayMode" value="2.0"/>)"
        R"(<PARAM id="cycles" value="3.0"/><PARAM id="channelSource" value="4.0"/>)"
        R"(<PARAM id="syncDiv" value="3.0"/><PARAM id="triggerSweep" value="1.0"/>)"
        R"(<PARAM id="stereoDecay" value="0.5"/></PARAMS>)";
    auto xml = juce::parseXML (legacy);
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (*xml, mb);

    auto p = makeProcessor();
    p->setStateInformation (mb.getData(), (int) mb.getSize());
    auto v = [&] (const char* id) { return p->apvts.getRawParameterValue (id)->load(); };
    auto approx = [] (float a, float b) { return std::abs (a - b) < 1.0e-4f; };
    CHECK (v ("triggerMode") == 2.0f && approx (v ("threshold"), 0.5f) && approx (v ("timeZoom"), 8.0f));
    CHECK (approx (v ("ampZoom"), 2.0f) && v ("displayMode") == 2.0f && v ("cycles") == 3.0f);
    CHECK (v ("channelSource") == 4.0f && v ("syncDiv") == 3.0f && v ("triggerSweep") == 1.0f);
    CHECK (p->editorWidth == 1300 && p->editorHeight == 700 && p->gonioWidth == 260 && p->gonioShown);
}

// Hosts hand back whatever they stored: truncated chunks, other plugins' data, or values
// edited by hand. None of it may crash, corrupt the state tree, or open an unusable window.
static void testHostileStateIsContained()
{
    auto p = makeProcessor();
    const auto good = stateOf (*p);

    std::mt19937 rng (1234);
    for (int len : { 0, 1, 7, 8, 9, 64, 1000 })
    {
        juce::MemoryBlock junk ((size_t) len);
        for (size_t i = 0; i < junk.getSize(); ++i) junk[i] = (char) (rng() & 0xff);
        p->setStateInformation (junk.getData(), len);
        // truncated copies of a valid chunk
        p->setStateInformation (good.getData(), juce::jmin (len, (int) good.getSize()));
    }
    CHECK (p->apvts.state.hasType ("PARAMS"));

    // foreign XML (another plugin's chunk) must be ignored, not adopted as our state
    {
        setParam (*p, "displayMode", 1.0f);
        auto xml = juce::parseXML (R"(<SomethingElse editorWidth="10"><PARAM id="displayMode" value="2"/></SomethingElse>)");
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (*xml, mb);
        p->setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (p->apvts.state.hasType ("PARAMS"));
        CHECK (p->apvts.getRawParameterValue ("displayMode")->load() == 1.0f);
        CHECK (p->editorWidth >= 560);
    }

    // out-of-range sizes are clamped into the window's own limits
    for (auto* attrs : { R"(editorWidth="-5" editorHeight="0" gonioWidth="-100")",
                         R"(editorWidth="99999" editorHeight="99999" gonioWidth="100000")" })
    {
        auto xml = juce::parseXML (juce::String ("<PARAMS ") + attrs + "/>");
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (*xml, mb);
        p->setStateInformation (mb.getData(), (int) mb.getSize());
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        CHECK (ed->getWidth() >= 560 && ed->getWidth() <= 2400);
        CHECK (ed->getHeight() >= 340 && ed->getHeight() <= 1400);
        CHECK (p->gonioWidth >= 140 && p->gonioWidth <= 480);
    }
}

// The stereo panel's stored width must never squeeze the main display to a sliver:
// at the smallest window the waveform keeps the larger share.
static void testSmallWindowKeepsScopeUsable()
{
    auto p = makeProcessor();
    p->gonioWidth = 480; p->gonioShown = true;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    ed->setSize (560, 340);
    auto* scope = findChild<ScopeComponent> (*ed);
    CHECK (scope != nullptr && scope->getWidth() >= ed->getWidth() / 2);
    ed->setSize (2400, 1400);
    CHECK (p->gonioWidth == 480); // the user's chosen width is kept for when there is room
}

//==============================================================================
// Open/close/resize the editor many times while audio streams, across every mode and
// with the stereo panel toggled; several instances at once. Run under ASan/TSan too.
static void testEditorLifecycleStress()
{
    std::mt19937 rng (42);
    std::vector<std::unique_ptr<CycloscopeProcessor>> procs;
    std::vector<std::unique_ptr<AudioThread>> audio;
    for (int i = 0; i < 3; ++i)
    {
        procs.push_back (makeProcessor (i == 2 ? 96000.0 : 48000.0, 256));
        audio.push_back (std::make_unique<AudioThread> (*procs.back(), i == 2 ? 96000.0 : 48000.0, 256));
    }
    for (int round = 0; round < 12; ++round)
    {
        std::vector<std::unique_ptr<juce::AudioProcessorEditor>> eds;
        for (auto& p : procs)
        {
            setParam (*p, "displayMode", (float) (rng() % 3));
            setParam (*p, "channelSource", (float) (rng() % 5));
            setParam (*p, "syncDiv", (float) (rng() % 4));
            setParam (*p, "triggerSweep", (float) (rng() % 3));
            p->gonioShown = (rng() % 2) == 0;
            eds.emplace_back (p->createEditor());
        }
        for (int k = 0; k < 4; ++k)
        {
            for (auto& e : eds)
            {
                e->setSize (560 + (int) (rng() % 1841), 340 + (int) (rng() % 1061));
                e->createComponentSnapshot (e->getLocalBounds(), true, (rng() % 2) ? 2.0f : 1.0f);
            }
            pumpMessages (20);
        }
    }
    audio.clear();
}


//==============================================================================
// --bench: what one GUI frame costs. "paint" is one full software-rendered editor frame
// (an upper bound: in a host the GL context rasterises); "timers" is the CPU the editor's
// timers burn per second of wall time (spectrum analysis runs there).
static double cpuSeconds()
{
    return (double) std::clock() / CLOCKS_PER_SEC; // process CPU time (all threads)
}

static int bench (const juce::StringArray&)
{
    struct Cfg { double sr; int w, h; };
    const Cfg cfgs[] = { { 48000.0, 1080, 520 }, { 48000.0, 2400, 1400 }, { 192000.0, 2400, 1400 } };
    const char* modeNames[] = { "Live", "Base Shape", "Spectrum" };
    std::printf ("%-10s %-10s %-9s %12s %14s\n", "rate", "size", "mode", "paint ms", "timers cpu %");
    for (auto& c : cfgs)
        for (int mode = 0; mode < 3; ++mode)
        {
            auto p = makeProcessor (c.sr, 512);
            setParam (*p, "displayMode", (float) mode);
            AudioThread audio (*p, c.sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            ed->setSize (c.w, c.h);
            pumpMessages (300);

            const double c0 = cpuSeconds(), t0 = juce::Time::getMillisecondCounterHiRes();
            pumpMessages (2000);
            const double timerPct = 100.0 * (cpuSeconds() - c0) / ((juce::Time::getMillisecondCounterHiRes() - t0) * 0.001);

            const int frames = 30;
            const double p0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < frames; ++i) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            const double paintMs = (juce::Time::getMillisecondCounterHiRes() - p0) / frames;
            std::printf ("%-10.0f %4dx%-5d %-10s %10.2f %14.1f\n", c.sr, c.w, c.h, modeNames[mode], paintMs, timerPct);
        }
    return 0;
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
    if (args.contains ("--bench"))  return bench (args);

    testPassthroughAndNoAllocation();
    testBusLayouts();
    testStateRoundTrip();
    testLegacyStateLoads();
    testHostileStateIsContained();
    testSmallWindowKeepsScopeUsable();
    testNonFiniteInputRecovers();
    testEditorLifecycleStress();

    if (gFailures == 0) std::puts ("plugin OK");
    else                std::printf ("plugin: %d failure(s)\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
