#include "PluginEditor.h"

juce::Slider& CycloscopeEditor::makeRotary (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    // Borderless readout (modern-analyzer style). Set directly on the slider, not via the
    // LookAndFeel: makeRotary runs before the slider is parented, so the internal value
    // box caches the default outline unless we override it on the slider itself.
    s.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxTextColourId,       juce::Colour (0xfff0f2f4));
    return s;
}

CycloscopeEditor::CycloscopeEditor (CycloscopeProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), scope (p), goniometer (p),
      modeToggle (p.apvts, "displayMode", juce::StringArray { "Live", "Base Shape", "Spectrum" })
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (scope);
    addAndMakeVisible (goniometer);
    addAndMakeVisible (divider);
    addAndMakeVisible (modeToggle);

    divider.onDrag = [this] (int dx)
    {
        const int w = juce::jlimit (140, 480, processorRef.gonioWidth.load() - dx);
        processorRef.gonioWidth.store (w);
        resized();
    };

    wordmark.setText ("Cycloscope", juce::dontSendNotification);
    wordmark.setFont (juce::Font (juce::FontOptions (15.0f)).boldened());
    wordmark.setColour (juce::Label::textColourId, juce::Colour (0xffc4c9d0));
    addAndMakeVisible (wordmark);

    presetButton.setTooltip ("Factory presets + save / load your own.");
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetButton);

    // Single-cycle workbench: capture A/B for overlay compare + export wavetable WAV
    for (auto* b : { &capAButton, &capBButton, &clearButton, &exportButton })
        addAndMakeVisible (b);
    capAButton.setTooltip ("Capture the current Base Shape cycle into slot A (blue overlay).");
    capBButton.setTooltip ("Capture the current Base Shape cycle into slot B (green overlay).");
    clearButton.setTooltip ("Clear the A/B overlays.");
    exportButton.setTooltip ("Export the current single cycle as a 2048-sample wavetable WAV.");
    capAButton.onClick  = [this] { scope.captureSlot (0); };
    capBButton.onClick  = [this] { scope.captureSlot (1); };
    clearButton.onClick = [this] { scope.clearCaptures(); };
    exportButton.onClick = [this]
    {
        auto fc = std::make_shared<juce::FileChooser> (
            "Export single cycle (wavetable)",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("Cycloscope-cycle.wav"),
            "*.wav");
        fc->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this, fc] (const juce::FileChooser& chooser)
            {
                const auto f = chooser.getResult();
                if (f != juce::File{}) scope.exportHeldCycle (f.withFileExtension ("wav"));
            });
    };

    // Configure controls only. They are owned/parented by their LabeledControl
    // wrappers (added below); re-adding them here would steal them back to the
    // editor at (0,0) and stack them in the corner.
    triggerBox.addItemList ({ "Free", "Rising", "Falling" }, 1);
    sourceBox.addItemList ({ "Mono", "Left", "Right", "Side", "Stereo" }, 1);
    sourceBox.setTooltip ("Which channel the waveform shows: Mono (L+R), Left, Right, Side (L-R), or Stereo (L+R overlaid).");
    syncBox.addItemList ({ "Off", "1/4", "1/2", "1 Bar" }, 1);
    syncBox.setTooltip ("Lock the Live window to a musical division of the host tempo (BPM).");
    sweepBox.addItemList ({ "Auto", "Normal", "Single" }, 1);
    sweepBox.setTooltip ("Auto: free-run. Normal: only update on a trigger (hold otherwise). Single: capture one triggered frame (re-select to re-arm).");
    makeRotary (thresholdSlider); makeRotary (timeSlider);
    makeRotary (ampSlider);       makeRotary (cyclesSlider);
    makeRotary (decaySlider);
    decaySlider.setTooltip ("Goniometer persistence: higher = longer-lasting trails for reading an evolving stereo image (delay/reverb).");

    for (auto* lc : { &sourceLC, &syncLC, &triggerLC, &sweepLC, &thresholdLC, &timeLC, &ampLC, &cyclesLC, &decayLC, &freezeLC })
        addAndMakeVisible (lc);

    triggerAttach   = std::make_unique<ComboAttach>  (p.apvts, "triggerMode",   triggerBox);
    sourceAttach    = std::make_unique<ComboAttach>  (p.apvts, "channelSource", sourceBox);
    syncAttach      = std::make_unique<ComboAttach>  (p.apvts, "syncDiv",       syncBox);
    sweepAttach     = std::make_unique<ComboAttach>  (p.apvts, "triggerSweep",  sweepBox);
    thresholdAttach = std::make_unique<SliderAttach> (p.apvts, "threshold",   thresholdSlider);
    timeAttach      = std::make_unique<SliderAttach> (p.apvts, "timeZoom",    timeSlider);
    ampAttach       = std::make_unique<SliderAttach> (p.apvts, "ampZoom",     ampSlider);
    cyclesAttach    = std::make_unique<SliderAttach> (p.apvts, "cycles",      cyclesSlider);
    decayAttach     = std::make_unique<SliderAttach> (p.apvts, "stereoDecay", decaySlider);
    freezeAttach    = std::make_unique<ButtonAttach> (p.apvts, "freeze",      freezeButton);

    // Clean value formatting. Set AFTER the attachments: a SliderAttachment resets the
    // slider's decimal places from the parameter step (threshold's 0.001 step otherwise
    // shows an over-precise "0.000"). textFromValueFunction takes precedence and sticks.
    thresholdSlider.textFromValueFunction = [] (double v) { return juce::String (v, 2); };
    timeSlider.textFromValueFunction      = [] (double v) { return juce::String (v, 1) + " /px"; };
    ampSlider.textFromValueFunction       = [] (double v) { return juce::String (v, 2) + "×"; };
    cyclesSlider.textFromValueFunction    = [] (double v) { return juce::String ((int) v); };
    decaySlider.textFromValueFunction     = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    for (auto* s : { &thresholdSlider, &timeSlider, &ampSlider, &cyclesSlider, &decaySlider })
        s->updateText();

    triggerBox.setTooltip      ("When the trace restarts: Free scrolls; Rising/Falling lock to a threshold crossing.");
    thresholdSlider.setTooltip ("Level the waveform must cross to retrigger the display.");
    timeSlider.setTooltip      ("Horizontal zoom: samples per pixel. Lower = more detail.");
    ampSlider.setTooltip       ("Vertical zoom: gain applied to the displayed signal.");
    cyclesSlider.setTooltip    ("Base Shape: how many detected periods to show across the screen.");
    freezeButton.setTooltip    ("Pause the display so you can inspect the current frame.");

    setResizable (true, true);
    setResizeLimits (560, 340, 2400, 1400);
    setSize (processorRef.editorWidth.load(), processorRef.editorHeight.load());
    startTimerHz (15);
    applyMode ((int) processorRef.apvts.getRawParameterValue ("displayMode")->load());
}

CycloscopeEditor::~CycloscopeEditor() { stopTimer(); setLookAndFeel (nullptr); }

void CycloscopeEditor::timerCallback()
{
    const int m = (int) processorRef.apvts.getRawParameterValue ("displayMode")->load();
    if (m != shownMode) applyMode (m);
}

void CycloscopeEditor::applyMode (int modeIdx)
{
    shownMode = modeIdx;
    const bool live = (modeIdx == 0);
    sourceLC.setVisible (true);   // channel source applies to both Live and Base Shape
    decayLC.setVisible (true);    // goniometer persistence is always-on
    triggerLC.setVisible (live);
    syncLC.setVisible (live);
    sweepLC.setVisible (live);
    thresholdLC.setVisible (live);
    timeLC.setVisible (live);
    cyclesLC.setVisible (modeIdx == 1);          // Base Shape only
    ampLC.setVisible (live);   // Amplitude affects Live only; Base Shape auto-fits / Spectrum n/a
    freezeLC.setVisible (true);

    // The A/B/Clr/Export workbench captures & exports single Base-Shape cycles, so
    // it's only meaningful in Base Shape mode. Hiding it elsewhere declutters the bar.
    const bool baseShape = (modeIdx == 1);
    for (auto* b : { &capAButton, &capBButton, &clearButton, &exportButton })
        b->setVisible (baseShape);

    resized();
}

int CycloscopeEditor::groupOf (LabeledControl* lc) const
{
    if (lc == &sourceLC  || lc == &syncLC) return 0;                                           // SIGNAL
    if (lc == &triggerLC || lc == &sweepLC || lc == &thresholdLC || lc == &timeLC || lc == &cyclesLC) return 1;  // TRIGGER / SHAPE
    if (lc == &ampLC) return 2;                                                                 // DISPLAY
    if (lc == &decayLC) return 3;                                                               // STEREO
    return 4;                                                                                   // FREEZE
}

void CycloscopeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1c1f));
    g.setColour (juce::Colour (0xff2a2e33));
    for (int x : dividerXs)
        g.drawVerticalLine (x, (float) controlBarTop, (float) controlBarBottom);
}

void CycloscopeEditor::resized()
{
    auto r = getLocalBounds();

    auto top = r.removeFromTop (44);
    wordmark.setBounds (top.removeFromLeft (108).withTrimmedLeft (14));
    presetButton.setBounds (top.removeFromLeft (84).reduced (4, 9));
    modeToggle.setBounds (top.removeFromRight (220).reduced (6, 9));

    // Single-cycle workbench (Base Shape only), right-aligned with content-fit widths
    // so "Export" never truncates. Laid out right-to-left: Export | Clr | B | A.
    if (exportButton.isVisible())
    {
        auto btns = top.removeFromRight (200).reduced (2, 9);
        exportButton.setBounds (btns.removeFromRight (66).reduced (2, 0));
        clearButton.setBounds  (btns.removeFromRight (46).reduced (2, 0));
        capBButton.setBounds   (btns.removeFromRight (38).reduced (2, 0));
        capAButton.setBounds   (btns.removeFromRight (38).reduced (2, 0));
    }

    auto controls = r.removeFromBottom (92);
    auto gonioStrip = r.removeFromRight (processorRef.gonioWidth.load()); // adjustable
    goniometer.setBounds (gonioStrip.reduced (6, 4));
    divider.setBounds (r.removeFromRight (7));
    scope.setBounds (r.reduced (12, 4));

    juce::Array<LabeledControl*> visible;
    for (auto* lc : { &sourceLC, &syncLC, &triggerLC, &sweepLC, &thresholdLC, &timeLC, &cyclesLC, &ampLC, &decayLC, &freezeLC })
        if (lc->isVisible()) visible.add (lc);

    auto bar = controls.reduced (14, 8);
    controlBarTop = bar.getY();
    controlBarBottom = bar.getBottom();
    dividerXs.clear();
    if (! visible.isEmpty())
    {
        const int w = bar.getWidth() / visible.size();
        LabeledControl* prev = nullptr;
        for (auto* lc : visible)
        {
            auto cell = bar.removeFromLeft (w);
            if (prev != nullptr && groupOf (lc) != groupOf (prev))
                dividerXs.push_back (cell.getX() - 3); // hairline between control groups
            lc->setBounds (cell.reduced (6, 0));
            prev = lc;
        }
    }

    processorRef.editorWidth.store (getWidth());
    processorRef.editorHeight.store (getHeight());
}

// ---- Presets -------------------------------------------------------------
// Factory presets are quick starting points (one click to a workflow). User presets
// round-trip the full APVTS state to a portable .smx XML so settings move
// between sessions/machines.

juce::File CycloscopeEditor::presetDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("Cycloscope").getChildFile ("presets");
    d.createDirectory();
    return d;
}

void CycloscopeEditor::showPresetMenu()
{
    static const char* factory[] = { "Default", "Wavetable Lab", "Stereo Field", "Spectrum", "Tempo Lock" };
    juce::PopupMenu fm;
    for (int i = 0; i < 5; ++i) fm.addItem (i + 1, factory[i]);

    juce::PopupMenu m;
    m.addSubMenu ("Factory", fm);
    m.addSeparator();
    m.addItem (1000, "Save preset...");
    m.addItem (1001, "Load preset...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
        [this] (int r)
        {
            if      (r >= 1 && r <= 5) applyFactory (r - 1);
            else if (r == 1000)        savePreset();
            else if (r == 1001)        loadPreset();
        });
}

void CycloscopeEditor::applyFactory (int idx)
{
    auto setChoice = [this] (const char* id, int v)
    { if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (processorRef.apvts.getParameter (id))) *p = v; };
    auto setFloat = [this] (const char* id, float v)
    { if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (processorRef.apvts.getParameter (id))) *p = v; };
    auto setInt = [this] (const char* id, int v)
    { if (auto* p = dynamic_cast<juce::AudioParameterInt*> (processorRef.apvts.getParameter (id))) *p = v; };
    auto setBool = [this] (const char* id, bool v)
    { if (auto* p = dynamic_cast<juce::AudioParameterBool*> (processorRef.apvts.getParameter (id))) *p = v; };

    switch (idx)
    {
        case 0: // Default — general-purpose live scope, rising trigger
            setChoice ("displayMode", 0);   setChoice ("channelSource", 0); setChoice ("syncDiv", 0);
            setChoice ("triggerSweep", 0);  setChoice ("triggerMode", 1);   setFloat ("threshold", 0.0f);
            setFloat  ("timeZoom", 4.0f);   setFloat ("ampZoom", 1.0f);     setBool ("freeze", false);
            break;
        case 1: // Wavetable Lab — single captured cycle for wavetable design
            setChoice ("displayMode", 1);   setChoice ("channelSource", 0); setInt ("cycles", 1);
            setBool   ("freeze", false);
            break;
        case 2: // Stereo Field — stereo source with longer goniometer trails
            setChoice ("displayMode", 0);   setChoice ("channelSource", 4); setFloat ("stereoDecay", 0.85f);
            break;
        case 3: // Spectrum — FFT analyzer
            setChoice ("displayMode", 2);   setChoice ("channelSource", 0);
            break;
        case 4: // Tempo Lock — window locked to one bar, hold on trigger
            setChoice ("displayMode", 0);   setChoice ("syncDiv", 3);       setChoice ("triggerSweep", 1);
            break;
        default: break;
    }
}

void CycloscopeEditor::savePreset()
{
    auto fc = std::make_shared<juce::FileChooser> ("Save preset",
        presetDir().getChildFile ("preset.smx"), "*.smx");
    fc->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, fc] (const juce::FileChooser& c)
        {
            const auto f = c.getResult();
            if (f == juce::File{}) return;
            if (auto xml = processorRef.apvts.copyState().createXml())
                xml->writeTo (f.withFileExtension ("smx"));
        });
}

void CycloscopeEditor::loadPreset()
{
    auto fc = std::make_shared<juce::FileChooser> ("Load preset", presetDir(), "*.smx");
    fc->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, fc] (const juce::FileChooser& c)
        {
            const auto f = c.getResult();
            if (f == juce::File{} || ! f.existsAsFile()) return;
            if (auto xml = juce::XmlDocument::parse (f))
                processorRef.apvts.replaceState (juce::ValueTree::fromXml (*xml));
        });
}
