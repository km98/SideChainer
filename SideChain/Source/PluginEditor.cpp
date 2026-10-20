/*
    PumpCurve - production editor implementation (0.5.0 redesign).

    Design language: graphite base, restrained mint accents, hairline rules,
    flat controls with one shared hover/focus/pressed language (see
    PumpCurveTheme.h). Direct JUCE drawing, no image assets besides the
    approved Music-Prod logo, system sans-serif typography.

    Control hierarchy: the curve editor is the focal point; AMOUNT, TIME and
    RELEASE share one knob size and one baseline under it; presets sit in the
    header; the version-mode pill and INFO sit at the header's right edge.
*/

#include "PluginEditor.h"
#include "Branding.h"

namespace
{
    // Palette aliases (the single source of truth is sid::theme).
    constexpr uint32_t kBackground   = sid::theme::kBase;
    constexpr uint32_t kTextPrimary  = sid::theme::kText;
    constexpr uint32_t kTextSubtle   = sid::theme::kTextMuted;
    constexpr uint32_t kAccent       = sid::theme::kAccent;
    constexpr uint32_t kKnobTrack    = sid::theme::kControlDown;
    constexpr uint32_t kKnobBody     = sid::theme::kControl;
    constexpr uint32_t kControlBg    = sid::theme::kControl;
    constexpr uint32_t kControlEdge  = sid::theme::kEdge;
    constexpr uint32_t kPresetText   = sid::theme::kText;
}

// Item-id ranges for the preset ComboBox (1-based item ids)
namespace
{
    constexpr int kPresetItemIdBase = 1;   // item id = index + 1
    constexpr int kDefaultItemId    = 10001; // "Default" menu entry
}

//==============================================================================
// ProdKnob - custom-drawn rotary control
//==============================================================================
ProdKnob::ProdKnob()
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setMouseDragSensitivity (180); // fine adjustment while dragging
    setDoubleClickReturnValue (true, 0.0); // overridden per-knob below
    setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (kAccent));
}

float ProdKnob::getArcFraction() const
{
    const auto range = getRange();
    return (float) juce::jmap (getValue(), range.getStart(), range.getEnd(), 0.0, 1.0);
}

void ProdKnob::paint (juce::Graphics& g)
{
    // One renderer for every rotary control (see sid::theme::drawKnob): track
    // ring, value ring with a faint bloom, a raised cap lit from above and a
    // short pointer - the MP Gain construction in PumpCurve's mint.
    sid::theme::drawKnob (g, getLocalBounds().toFloat().reduced (2.0f), getArcFraction(),
                          findColour (juce::Slider::rotarySliderFillColourId),
                          isMouseOverOrDragging() && isEnabled(),
                          hasKeyboardFocus (false), isEnabled());
}

//==============================================================================
// Editor
//==============================================================================
SideChainAudioProcessorEditor::SideChainAudioProcessorEditor (SideChainAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      authManager (p.getAuthManager()),
      infoPage (authManager, p.getDemoGate(), p.getUpdateChecker(), JucePlugin_VersionString)
{
    // The PROCESSOR owns the AuthManager (0.5.0): it restores the persisted
    // token, wires the entitlement into the audio gate and enables background
    // approval polling before any editor exists. Nothing auth-related is
    // constructed here, so merely opening a window cannot re-trigger network
    // work or reset the demo session.
    lastSignedInState = (authManager.getState() == sid::auth::AuthManager::State::signedIn);
    setLookAndFeel (&lookAndFeel);
    // PRESET selector: a compact, styled ComboBox. The closed box shows the
    // derived current identity (factory name / Default / Custom); the popup
    // lists the factory presets plus a Default entry. Applying a preset goes
    // through the processor's parameter-notification path only.
    presetBox.setJustificationType (juce::Justification::centred);
    presetBox.setEditableText (false);
    presetBox.setColour (juce::ComboBox::textColourId, juce::Colour (kPresetText));
    presetBox.setTooltip ("Factory and User Presets. Save Preset stores all sound settings; edits show Custom.");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id == kDefaultItemId)
        {
            processorRef.applyDefaultPreset();
        }
        else if (id >= 1000 && id - 1000 < userPresets.size())
        {
            const auto result = processorRef.loadUserPreset (userPresetLibrary, userPresets[id - 1000].file);
            showPresetStatus (result.failed() ? result.getErrorMessage() : "Preset loaded.");
        }
        else if (id >= kPresetItemIdBase)
        {
            const int index = id - kPresetItemIdBase;
            const auto& table = sid::presets::factoryPresets();
            if (index < table.size())
                processorRef.applyFactoryPreset (table.getReference (index).name);
        }
        // Re-select the display for the (possibly unchanged) live values.
        refreshPresetDisplay();
    };
    buildPresetMenu();
    addAndMakeVisible (presetBox);
    savePresetButton.onClick = [this] { showSavePresetForm(); };
    confirmPresetButton.onClick = [this] { commitUserPreset(); };
    cancelPresetButton.onClick = [this] { cancelSavePreset(); };
    presetNameEditor.setTextToShowWhenEmpty ("Preset name", juce::Colour (kTextSubtle));
    presetNameEditor.setInputRestrictions (80);
    presetNameEditor.onReturnKey = [this] { commitUserPreset(); };
    presetNameEditor.onEscapeKey = [this] { cancelSavePreset(); };
    savePresetButton.setTooltip ("Save every sound setting (curve, knobs, offset, switch) as a User Preset");
    for (auto* button : { &savePresetButton, &confirmPresetButton, &cancelPresetButton })
        addChildComponent (button);
    // SAVE is the one primary action of the name form.
    sid::theme::setButtonStyle (confirmPresetButton, sid::theme::ButtonStyle::primary);
    sid::theme::setButtonStyle (cancelPresetButton, sid::theme::ButtonStyle::quiet);
    presetNameEditor.setFont (sid::theme::font (13.5f));
    presetNameEditor.setIndents (8, 6);
    addChildComponent (presetNameEditor);
    presetStatusLabel.setFont (sid::theme::font (11.5f));
    presetStatusLabel.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    presetStatusLabel.setJustificationType (juce::Justification::centred);
    // Wrap onto a second line rather than squeezing the glyphs horizontally.
    presetStatusLabel.setMinimumHorizontalScale (1.0f);
    presetStatusLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (presetStatusLabel);
    savePresetButton.setVisible (true);

    // PRESET step arrows (previous / next factory preset). They call the
    // processor's stepPreset - the same parameter-notification path the
    // dropdown uses - so the dropdown, arrows, DSP and graph preview stay
    // synchronised through the live values. Styling matches the selector.
    auto styleArrow = [] (juce::DrawableButton& b, juce::Path& arrow, bool left)
    {
        arrow.addTriangle (left ? 9.0f : 5.0f, 4.0f,
                           left ? 5.0f : 9.0f, 8.0f,
                           left ? 9.0f : 5.0f, 12.0f);
        auto* shape = new juce::DrawablePath();
        shape->setPath (arrow);
        shape->setFill (juce::Colour (kTextSubtle));
        b.setImages (shape);
        b.setEdgeIndent (9);
        b.setColour (juce::DrawableButton::backgroundColourId, juce::Colour (kControlBg));
        b.setColour (juce::DrawableButton::backgroundOnColourId, juce::Colour (kControlEdge));
        b.setTooltip (left ? "Previous preset" : "Next preset");
    };
    styleArrow (presetPrevButton, presetPrevArrow, true);
    styleArrow (presetNextButton, presetNextArrow, false);
    presetPrevButton.onClick = [this]
    {
        processorRef.stepPreset (-1);
        refreshPresetDisplay();
    };
    presetNextButton.onClick = [this]
    {
        processorRef.stepPreset (+1);
        refreshPresetDisplay();
    };
    addAndMakeVisible (presetPrevButton);
    addAndMakeVisible (presetNextButton);

    // ---- SIDECHAIN OFFSET (0.3.0): smaller, second arrow row ---------------
    // LEFT click = envelope earlier, RIGHT click = later, 1 ms per step,
    // bounded by the parameter range (-30..+30 ms). Visually smaller than
    // the preset arrows so the two groups cannot be confused.
    auto styleOffsetArrow = [] (juce::DrawableButton& b, juce::Path& arrow, bool left)
    {
        arrow.addTriangle (left ? 7.0f : 5.0f, 5.0f,
                           left ? 5.0f : 7.0f, 8.0f,
                           left ? 7.0f : 5.0f, 11.0f);
        auto* shape = new juce::DrawablePath();
        shape->setPath (arrow);
        shape->setFill (juce::Colour (kTextSubtle));
        b.setImages (shape);
        b.setEdgeIndent (6);
        b.setColour (juce::DrawableButton::backgroundColourId, juce::Colour (kControlBg));
        b.setColour (juce::DrawableButton::backgroundOnColourId, juce::Colour (kControlEdge));
        b.setTooltip (left ? "Duck earlier (lookahead)" : "Duck later");
    };
    styleOffsetArrow (offsetPrevButton, offsetPrevArrow, true);
    styleOffsetArrow (offsetNextButton, offsetNextArrow, false);

    auto* offsetParam = processorRef.getParameters().getParameter ("sidechainOffset");
    offsetPrevButton.onClick = [this, offsetParam]
    {
        const float v = offsetParam->getValue();
        offsetParam->setValueNotifyingHost (
            juce::jlimit (0.0f, 1.0f, v - 1.0f / 60.0f)); // 60 x 1 ms steps
        refreshOffsetDisplay();
    };
    offsetNextButton.onClick = [this, offsetParam]
    {
        const float v = offsetParam->getValue();
        offsetParam->setValueNotifyingHost (
            juce::jlimit (0.0f, 1.0f, v + 1.0f / 60.0f));
        refreshOffsetDisplay();
    };
    addAndMakeVisible (offsetPrevButton);
    addAndMakeVisible (offsetNextButton);

    offsetValueLabel.setFont (sid::theme::valueFont (12.5f));
    offsetValueLabel.setColour (juce::Label::textColourId, juce::Colour (kTextPrimary));
    // The readout sits in a recessed well painted by the editor (paint()).
    offsetValueLabel.setJustificationType (juce::Justification::centred);
    offsetValueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (offsetValueLabel);

    offsetCaption.setText ("TIMING OFFSET", juce::dontSendNotification);
    offsetCaption.setFont (sid::theme::captionFont (10.5f));
    offsetCaption.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    offsetCaption.setJustificationType (juce::Justification::centredRight);
    offsetCaption.setTooltip ("Moves the duck earlier (lookahead) or later against the beat, 1 ms per click");
    offsetCaption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (offsetCaption);
    refreshOffsetDisplay();

    // ---- TRANSPORT SWITCH (0.4.2): ACTIVE ONLY DURING PLAY/REC -----------
    // Shares the transport row with TIMING OFFSET (left-aligned, the offset
    // group stays centred), visually quieter than the primary knobs but
    // unmistakably a control. Default ON comes from the parameter itself.
    transportGateToggle.setTooltip (
        "ON (default): PumpCurve ducks only while the DAW transport plays or records. "
        "OFF: the ducking keeps running while the transport is stopped.");
    transportGateToggle.setColour (juce::ToggleButton::textColourId, juce::Colour (kTextSubtle));
    transportGateToggle.setColour (juce::ToggleButton::tickColourId, juce::Colour (kAccent));
    transportGateToggle.setColour (juce::ToggleButton::tickDisabledColourId,
                                   juce::Colour (kControlEdge));
    transportGateAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processorRef.getParameters(), "transportGate", transportGateToggle);
    addAndMakeVisible (transportGateToggle);

    // ---- View selector (0.3.0): compact segmented SIDECHAIN | ANALYZER ---
    // Music-Prod design language: black bg, white type, mint selected.
    auto styleViewButton = [] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId,   juce::Colour (kControlBg));
        b.setColour (juce::TextButton::textColourOffId,  juce::Colour (kTextSubtle));
        b.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff0b0f0d));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (kAccent));
        b.setColour (juce::ComboBox::outlineColourId,    juce::Colour (kControlEdge));
        b.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    };
    styleViewButton (sidechainViewButton);
    styleViewButton (analyzerViewButton);
    sidechainViewButton.setTooltip ("Duck envelope view (trigger ticks + cycles)");
    analyzerViewButton.setTooltip ("Signal analyzer (input / sidechain / duck / output)");
    sidechainViewButton.onClick = [this] { selectView (GraphComponent::ViewMode::sidechain); };
    analyzerViewButton.onClick  = [this] { selectView (GraphComponent::ViewMode::analyzer); };
    // Phase C replaces the old analyzer/legacy preview in the editor with
    // the persistent PumpCurve canvas. Keep the fields/selection helper for
    // source compatibility, but do not expose misleading display tabs.
    sidechainViewButton.setVisible (false);
    analyzerViewButton.setVisible (false);

    // PRIMARY control: SIDECHAIN AMOUNT
    amountKnob.setTooltip ("Ducking intensity - how much the main signal ducks");
    amountKnob.setDoubleClickReturnValue (true, 50.0); // double-click resets to default
    addAndMakeVisible (amountKnob);

    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getParameters(), "sidechainAmount", amountKnob);

    amountValueLabel.setFont (sid::theme::valueFont (24.0f));
    amountValueLabel.setColour (juce::Label::textColourId, juce::Colour (kTextPrimary));
    amountValueLabel.setJustificationType (juce::Justification::centredLeft);
    amountValueLabel.setBorderSize ({});

    amountCaption.setText ("AMOUNT", juce::dontSendNotification);
    amountCaption.setFont (sid::theme::captionFont (11.0f));
    amountCaption.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    amountCaption.setJustificationType (juce::Justification::centredLeft);
    amountCaption.setBorderSize ({});

    // SECONDARY control: RELEASE (envelope SHAPE; preset character)
    releaseKnob.setTooltip ("Envelope shape: plateau vs tail split (preset character)");
    releaseKnob.setDoubleClickReturnValue (true, 150.0); // reset to 150 ms default
    releaseKnob.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (kAccent));
    addAndMakeVisible (releaseKnob);

    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getParameters(), "release", releaseKnob);

    releaseValueLabel.setFont (sid::theme::valueFont (24.0f));
    releaseValueLabel.setColour (juce::Label::textColourId, juce::Colour (kTextPrimary));
    releaseValueLabel.setJustificationType (juce::Justification::centredLeft);
    releaseValueLabel.setBorderSize ({});

    releaseCaption.setText ("RELEASE", juce::dontSendNotification);
    releaseCaption.setFont (sid::theme::captionFont (11.0f));
    releaseCaption.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    releaseCaption.setJustificationType (juce::Justification::centredLeft);
    releaseCaption.setBorderSize ({});


    // DUCK LENGTH (0.3.0): total audible duck duration, INDEPENDENT of the
    // preset selection (presets set Amount + Release only).
    duckLengthKnob.setTooltip ("Total duck duration (ms) - independent of presets");
    duckLengthKnob.setDoubleClickReturnValue (
        true, sid::dsp::DuckEngine::kDuckLengthDefaultMs);
    duckLengthKnob.setColour (juce::Slider::rotarySliderFillColourId,
                              juce::Colour (kAccent));
    addAndMakeVisible (duckLengthKnob);

    duckLengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getParameters(), "duckLength", duckLengthKnob);

    duckLengthValueLabel.setFont (sid::theme::valueFont (24.0f));
    duckLengthValueLabel.setColour (juce::Label::textColourId, juce::Colour (kTextPrimary));
    duckLengthValueLabel.setJustificationType (juce::Justification::centredLeft);
    duckLengthValueLabel.setBorderSize ({});

    duckLengthCaption.setText ("TIME", juce::dontSendNotification);
    duckLengthCaption.setFont (sid::theme::captionFont (11.0f));
    duckLengthCaption.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    duckLengthCaption.setJustificationType (juce::Justification::centredLeft);
    duckLengthCaption.setBorderSize ({});

    smoothSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    smoothSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    smoothSlider.setRange (0.0, 100.0, 0.1);
    smoothSlider.setTooltip ("Curve smoothness (visual only in this version)");
    smoothAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getParameters(), "smooth", smoothSlider);
    smoothValueLabel.setFont (sid::theme::valueFont (12.5f));
    smoothValueLabel.setColour (juce::Label::textColourId, juce::Colour (kTextPrimary));
    smoothValueLabel.setJustificationType (juce::Justification::centredRight);
    smoothValueLabel.setInterceptsMouseClicks (false, false);
    smoothCaption.setText ("SMOOTH", juce::dontSendNotification);
    smoothCaption.setFont (sid::theme::captionFont (10.5f));
    smoothCaption.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    smoothCaption.setJustificationType (juce::Justification::centredLeft);
    smoothCaption.setInterceptsMouseClicks (false, false);

    graph.setCurve (processorRef.getPumpCurve(), processorRef.getCurveStateMode());
    graph.setHistoryDisplayEnabled (false); // graph is now a curve editor, never an audio-history view
    // ...but the LIVE audio stays visible behind the curve (0.4.2): the real
    // input and processed signal, drawn from the scope FIFO under the curve.
    graph.setWaveformDisplayEnabled (true);
    graph.setCurveChangedCallback ([this] (const sid::curve::PumpCurve& curve)
    {
        return processorRef.setPumpCurvePoints (curve.storage().data(), curve.size());
    });
    graph.setSmoothness ((float) smoothSlider.getValue());
    smoothSlider.onValueChange = [this]
    {
        graph.setSmoothness ((float) smoothSlider.getValue());
    };
    addAndMakeVisible (graph);
    addAndMakeVisible (smoothSlider);
    addAndMakeVisible (smoothValueLabel);
    addAndMakeVisible (smoothCaption);
    addAndMakeVisible (amountValueLabel);
    addAndMakeVisible (amountCaption);
    addAndMakeVisible (duckLengthValueLabel);
    addAndMakeVisible (duckLengthCaption);
    addAndMakeVisible (releaseValueLabel);
    addAndMakeVisible (releaseCaption);

    // PRESET caption is drawn by the editor itself (see paint()), NOT by a
    // label: a 10px string squeezed into a 5px-high label rect was visually
    // overlapping the selector's bottom edge.

    // ---- Phase 8: INFO button (header) + INFO view swap ------------------
    infoButton.setTooltip ("Licence, Music-Prod+ account and updates");
    infoButton.onClick = [this] { showInfoView(); };
    addAndMakeVisible (infoButton);

    infoPage.onBack = [this] { showMainView(); };
    addAndMakeVisible (infoPage);   // Phase 9 defect fix: page was never added
                                    // to the hierarchy, so it could not render
    infoPage.setVisible (false); // main view first

    // ---- Phase 8: Music-Prod logo (approved brand asset) -----------------
    musicProdLogo = juce::ImageFileFormat::loadFrom (
        BinaryData::musicprodlogo_png, BinaryData::musicprodlogo_pngSize);

    setSize (800, 520);
    refreshPresetDisplay();
    lastTimerCallbackTimeMs = juce::Time::currentTimeMillis();
    startTimerHz (30);
}

SideChainAudioProcessorEditor::~SideChainAudioProcessorEditor()
{
    // Children still reference the theme; detach before it is destroyed.
    setLookAndFeel (nullptr);
}

void SideChainAudioProcessorEditor::refreshRenderingAfterLifecycleChange()
{
    resized();

    if (showingInfo)
    {
        infoPage.refresh();
        infoPage.repaint();
    }
    else
    {
        // Explicitly invalidate the controls and their readouts without
        // changing slider values, APVTS attachments, or processor state.
        amountKnob.repaint();
        duckLengthKnob.repaint();
        releaseKnob.repaint();
        amountValueLabel.repaint();
        amountCaption.repaint();
        duckLengthValueLabel.repaint();
        duckLengthCaption.repaint();
        smoothSlider.repaint();
        smoothValueLabel.repaint();
        smoothCaption.repaint();
        releaseValueLabel.repaint();
        releaseCaption.repaint();
        graph.repaint();
        graph.toFront (false);
        presetBox.repaint();
        offsetValueLabel.repaint();
        offsetCaption.repaint();
        transportGateToggle.repaint();
    }

    repaint();
}

void SideChainAudioProcessorEditor::moved()
{
    refreshRenderingAfterLifecycleChange();
}

void SideChainAudioProcessorEditor::visibilityChanged()
{
    if (isVisible())
    {
        refreshRenderingAfterLifecycleChange();

        // The window is being shown again (the host reopened the editor, or the
        // user came back to it). Re-check the subscription so a purchase made
        // while it was closed is reflected - debounced, so bringing the window
        // forward repeatedly does not become a request each time.
        authManager.refreshEntitlementsIfStale();
    }
}

void SideChainAudioProcessorEditor::parentHierarchyChanged()
{
    if (isShowing())
        refreshRenderingAfterLifecycleChange();
}

void SideChainAudioProcessorEditor::setScaleFactor (float newScale)
{
    AudioProcessorEditor::setScaleFactor (newScale);
    refreshRenderingAfterLifecycleChange();
}

void SideChainAudioProcessorEditor::showMainView()
{
    showingInfo = false;
    infoPage.setVisible (false);
    graph.setVisible (true);
    presetBox.setVisible (true);
    savePresetButton.setVisible (true);
    presetStatusLabel.setVisible (true);
    presetPrevButton.setVisible (true);
    presetNextButton.setVisible (true);
    offsetPrevButton.setVisible (true);
    offsetNextButton.setVisible (true);
    offsetValueLabel.setVisible (true);
    offsetCaption.setVisible (true);
    transportGateToggle.setVisible (true);
    sidechainViewButton.setVisible (false);
    analyzerViewButton.setVisible (false);
    amountKnob.setVisible (true);
    duckLengthKnob.setVisible (true);
    releaseKnob.setVisible (true);
    amountValueLabel.setVisible (true);
    amountCaption.setVisible (true);
    duckLengthValueLabel.setVisible (true);
    duckLengthCaption.setVisible (true);
    smoothSlider.setVisible (true);
    smoothValueLabel.setVisible (true);
    smoothCaption.setVisible (true);
    releaseValueLabel.setVisible (true);
    releaseCaption.setVisible (true);
    infoButton.setVisible (true);
    repaint();
}

void SideChainAudioProcessorEditor::showInfoView()
{
    showingInfo = true;
    infoPage.setVisible (true);
    graph.setVisible (false);
    cancelSavePreset();
    presetBox.setVisible (false);
    savePresetButton.setVisible (false);
    presetStatusLabel.setVisible (false);
    presetPrevButton.setVisible (false);
    presetNextButton.setVisible (false);
    offsetPrevButton.setVisible (false);
    offsetNextButton.setVisible (false);
    offsetValueLabel.setVisible (false);
    offsetCaption.setVisible (false);
    transportGateToggle.setVisible (false);
    sidechainViewButton.setVisible (false);
    analyzerViewButton.setVisible (false);
    amountKnob.setVisible (false);
    duckLengthKnob.setVisible (false);
    releaseKnob.setVisible (false);
    amountValueLabel.setVisible (false);
    amountCaption.setVisible (false);
    duckLengthValueLabel.setVisible (false);
    duckLengthCaption.setVisible (false);
    smoothSlider.setVisible (false);
    smoothValueLabel.setVisible (false);
    smoothCaption.setVisible (false);
    releaseValueLabel.setVisible (false);
    releaseCaption.setVisible (false);
    infoButton.setVisible (false);

    // Opening the page is one of the lifecycle points that re-checks the
    // subscription: the customer may have bought Music-Prod+ since the last
    // answer arrived. Debounced inside the AuthManager, so reopening the page
    // repeatedly cannot become one request per open - and the page's own CHECK
    // SUBSCRIPTION control can still force a check at any moment.
    authManager.refreshEntitlementsIfStale();

    infoPage.refresh();
    repaint();
}

// View-mode switch: pure display state. History, FIFO and DSP untouched.
void SideChainAudioProcessorEditor::selectView (GraphComponent::ViewMode m)
{
    graph.setViewMode (m);
    sidechainViewButton.setToggleState (m == GraphComponent::ViewMode::sidechain,
                                        juce::dontSendNotification);
    analyzerViewButton.setToggleState (m == GraphComponent::ViewMode::analyzer,
                                       juce::dontSendNotification);
}
void SideChainAudioProcessorEditor::drawMusicProdLogo (juce::Graphics& g,
                                                       juce::Rectangle<int> area) const
{
    if (musicProdLogo.isNull())
        return;

    // The approved logo asset is a fully OPAQUE image (white artwork on a
    // near-black field). It is re-rendered once as an alpha mask: the
    // artwork's luminance becomes the ALPHA channel of pure-white pixels.
    //
    // Drawn with the plain alpha-compositing path (fillAlphaChannel=false).
    // NOTE: the previous two renderings were both wrong on macOS:
    //  - the mask colour used juce::Colour(a,1,1,1), whose 4-float ctor is
    //    HSB -> red/orange pixels; and
    //  - drawImageWithin(..., true) routes through CoreGraphics
    //    CGContextClipToMask, which masks by the image's converted GREYSCALE
    //    LUMINANCE (white pixels -> fully opaque) instead of its alpha,
    //    painting a solid rectangle. Compositing the white mask through its
    //    own alpha channel is correct on all backends.
    const int w = juce::jmin (96, area.getWidth());
    const int h = juce::roundToInt (w * (float) musicProdLogo.getHeight()
                                        / (float) musicProdLogo.getWidth());

    if (logoMask.isNull())
    {
        logoMask = juce::Image (juce::Image::ARGB,
                                musicProdLogo.getWidth(), musicProdLogo.getHeight(),
                                true);
        juce::Image::BitmapData pixels (logoMask, juce::Image::BitmapData::writeOnly);
        const float floor_ = 0.35f; // artwork floor: anything >= this is opaque
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
            {
                const auto px = musicProdLogo.getPixelAt (x, y);
                const float lum = (0.2126f * px.getFloatRed()
                                   + 0.7152f * px.getFloatGreen()
                                   + 0.0722f * px.getFloatBlue());
                const float a = juce::jlimit (0.0f, 1.0f,
                                              (lum - floor_) / (1.0f - floor_));
                pixels.setPixelColour (x, y, juce::Colours::white.withAlpha (a));
            }
    }

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (0.62f);   // a signature, not a headline
    g.drawImageWithin (logoMask,
                       area.getCentreX() - w / 2, area.getCentreY() - h / 2, w, h,
                       juce::RectanglePlacement (juce::RectanglePlacement::centred),
                       false);   // composite via the mask's own alpha channel
    g.setOpacity (1.0f);
}

void SideChainAudioProcessorEditor::buildPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);

    presetBox.addSectionHeading ("FACTORY PRESETS");
    const auto& table = sid::presets::factoryPresets();
    for (int i = 0; i < table.size(); ++i)
        presetBox.addItem (table.getReference (i).name, kPresetItemIdBase + i);

    presetBox.addSeparator();
    presetBox.addItem ("Default (50% / 150 ms)", kDefaultItemId);
    presetBox.addSeparator();
    presetBox.addSectionHeading ("USER PRESETS");
    userPresets = userPresetLibrary.list();
    for (int i = 0; i < userPresets.size(); ++i)
        presetBox.addItem (userPresets[i].name, 1000 + i);
    lastPresetLibraryRefreshMs = juce::Time::currentTimeMillis();
}

void SideChainAudioProcessorEditor::showSavePresetForm()
{
    savingPreset = true;
    presetNameEditor.setText ("", false);
    presetStatusLabel.setText ("Type a name for the new preset. Nothing is overwritten.", juce::dontSendNotification);
    resized();
    presetNameEditor.grabKeyboardFocus();
}

void SideChainAudioProcessorEditor::cancelSavePreset()
{
    // The naming instruction and any naming error belong to the form; closing
    // the form must not leave them behind in the toolbar.
    if (savingPreset)
    {
        presetStatusLabel.setText ({}, juce::dontSendNotification);
        presetStatusLabel.setVisible (false);
    }
    savingPreset = false;
    presetNameEditor.setVisible (false);
    confirmPresetButton.setVisible (false);
    cancelPresetButton.setVisible (false);
    presetBox.setVisible (! showingInfo);
    presetPrevButton.setVisible (! showingInfo);
    presetNextButton.setVisible (! showingInfo);
    savePresetButton.setVisible (! showingInfo);
}

void SideChainAudioProcessorEditor::commitUserPreset()
{
    juce::File file;
    const auto result = processorRef.saveUserPreset (userPresetLibrary, presetNameEditor.getText(), file);
    if (result.failed())
    {
        presetStatusLabel.setText (result.getErrorMessage(), juce::dontSendNotification);
        return; // keep the name form open, with an actionable error
    }
    cancelSavePreset();
    buildPresetMenu();
    refreshPresetDisplay();
    showPresetStatus ("Preset saved.");
}

void SideChainAudioProcessorEditor::showPresetStatus (const juce::String& text)
{
    presetStatusLabel.setText (text, juce::dontSendNotification);
    presetStatusLabel.setVisible (! showingInfo && text.isNotEmpty());
    presetStatusShownMs = juce::Time::currentTimeMillis();
}

void SideChainAudioProcessorEditor::refreshOffsetDisplay()
{
    if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (
            processorRef.getParameters().getParameter ("sidechainOffset")))
    {
        const int ms = (int) std::lround (p->get());
        offsetValueLabel.setText ((ms > 0 ? "+" : "") + juce::String (ms) + " ms",
                                  juce::dontSendNotification);
    }
}

void SideChainAudioProcessorEditor::refreshPresetDisplay()
{
    // Display-only identity derived from live values (processor-side, see
    // getCurrentPresetDisplayName). When it is a factory preset we select
    // the matching item so the popup arrow/checkmark behaves naturally;
    // for Default/Custom we select NOTHING and just show the text.
    const auto userName = processorRef.getCurrentUserPresetName();
    if (userName.isNotEmpty())
    {
        for (int i = 0; i < userPresets.size(); ++i)
            if (userPresets[i].name == userName)
            {
                presetBox.setSelectedId (1000 + i, juce::dontSendNotification);
                return;
            }
    }
    const juce::String name = processorRef.getCurrentPresetDisplayName();

    const auto& table = sid::presets::factoryPresets();
    int selectId = 0;
    for (int i = 0; i < table.size(); ++i)
        if (table.getReference (i).name == name)
        {
            selectId = kPresetItemIdBase + i;
            break;
        }

    if (selectId != 0)
    {
        presetBox.setSelectedId (selectId, juce::dontSendNotification);
    }
    else
    {
        presetBox.setSelectedId (0, juce::dontSendNotification);
        presetBox.setText (name, juce::dontSendNotification);
    }
}

void SideChainAudioProcessorEditor::timerCallback()
{
    // mach_absolute_time (used by JUCE's millisecond counter on macOS) does
    // not advance during system sleep. Detect the elapsed wall-clock gap and
    // refresh once on the first timer tick after wake instead of polling
    // repaints continuously.
    const auto nowMs = juce::Time::currentTimeMillis();
    if (lastTimerCallbackTimeMs != 0
        && (nowMs < lastTimerCallbackTimeMs || nowMs - lastTimerCallbackTimeMs > 1000))
        refreshRenderingAfterLifecycleChange();
    lastTimerCallbackTimeMs = nowMs;

    // Entitlement follow-up (0.5.0): signing in is only useful if the
    // subscription answer is actually fetched - the Music-Prod+ line and the
    // full-version gate both depend on it. (Previously this refresh was never
    // triggered in production, so `subscribed` stayed false forever and a
    // subscriber would never have been recognised.) The request is
    // asynchronous; the audio thread never waits for it.
    const bool signedInNow = (authManager.getState() == sid::auth::AuthManager::State::signedIn);
    if (signedInNow && ! lastSignedInState)
        authManager.refreshEntitlements();
    lastSignedInState = signedInNow;

    // Version check (Plugin Versions): the PROCESSOR owns the checker and its
    // six-hour interval; this only lets a check become due. The request runs on
    // the checker's own thread, so an unreachable or slow server can never
    // delay this timer, a repaint, or anything on the audio thread.
    processorRef.getUpdateChecker().refreshIfDue();

    // Header version-mode readout: repaint only when the text changes (the
    // demo countdown ticks once a second, not 30 times a second).
    const auto statusNow = demoStatusText();
    if (statusNow != lastDemoStatusText)
    {
        lastDemoStatusText = statusNow;
        repaint (statusPillBounds.expanded (24, 2));
    }
    // Message-thread directory scan only; refresh other AU/VST3 instances'
    // saves without any audio-thread file access or a 30 Hz disk poll.
    if (! savingPreset && ! presetBox.isPopupActive() && nowMs - lastPresetLibraryRefreshMs > 3000)
        buildPresetMenu();

    // Preset outcomes are transient; the naming instruction stays while the
    // form is open.
    if (! savingPreset && presetStatusLabel.getText().isNotEmpty()
        && nowMs - presetStatusShownMs > kPresetStatusHoldMs)
    {
        presetStatusLabel.setText ({}, juce::dontSendNotification);
        presetStatusLabel.setVisible (false);
    }

    // Drain real DSP frames into the graph history (lock-free).
    graph.consumeFrames (processorRef.graphFifo);

    // ...and the live waveform scope, which paints BEHIND the curve. The
    // message-thread clock lets the layer notice a stalled audio producer and
    // fall back to its no-signal state instead of showing a frozen trace.
    graph.consumeWaveformFifo (processorRef.waveformFifo, juce::Time::currentTimeMillis());

    // Keep the no-signal ducking preview in sync with the LIVE parameter
    // values (covers presets, host automation, undo and manual edits).
    if (auto* amount = dynamic_cast<juce::AudioParameterFloat*> (
            processorRef.getParameters().getParameter ("sidechainAmount")))
        if (auto* release = dynamic_cast<juce::AudioParameterFloat*> (
                processorRef.getParameters().getParameter ("release")))
            if (auto* duckLength = dynamic_cast<juce::AudioParameterFloat*> (
                    processorRef.getParameters().getParameter ("duckLength")))
                graph.setPreviewParams (amount->get(), release->get(), duckLength->get());

    // INFO view: refresh the displayed auth state (cheap; all local reads).
    if (showingInfo)
        infoPage.refresh();

    const auto amount = amountKnob.getValue();
    amountValueLabel.setText (juce::String ((int) std::round (amount)) + "%",
                              juce::dontSendNotification);

    const auto release = releaseKnob.getValue();
    // Skewed range: show a clean human-readable ms value.
    releaseValueLabel.setText (juce::String (std::round (release / 10.0) * 10.0, 0) + " ms",
                               juce::dontSendNotification);

    const auto duckLength = duckLengthKnob.getValue();
    duckLengthValueLabel.setText (juce::String (std::round (duckLength / 10.0) * 10.0, 0) + " ms",
                                  juce::dontSendNotification);
    smoothValueLabel.setText (juce::String ((int) std::round (smoothSlider.getValue())) + "%",
                              juce::dontSendNotification);
    graph.setSmoothness ((float) smoothSlider.getValue());
    graph.setCurve (processorRef.getPumpCurve(), processorRef.getCurveStateMode());

    // Keep the preset identity and offset readout in sync with the LIVE
    // values (covers host automation, undo, state restore and manual edits).
    refreshPresetDisplay();
    refreshOffsetDisplay();
}

void SideChainAudioProcessorEditor::resized()
{
    // One grid for the whole editor: 20 px side margins, every row aligned to
    // the curve well's edges, controls on a 30 px cap height.
    constexpr int margin = 20;
    const int width = getWidth();
    auto area = getLocalBounds().withTrimmedLeft (margin).withTrimmedRight (margin);

    // INFO page covers the editor when shown.
    infoPage.setBounds (getLocalBounds());

    // ---- header: wordmark | presets | mode status + INFO ------------------
    auto header = area.removeFromTop (headerHeight);
    const int capH = 30;
    const int rowY = (headerHeight - capH) / 2;
    infoButtonBounds = juce::Rectangle<int> (width - margin - 58, rowY, 58, capH);
    infoButton.setBounds (infoButtonBounds);
    // Mode status: a dot + words, right-aligned against INFO (no pill box).
    statusPillBounds = juce::Rectangle<int> (infoButtonBounds.getX() - 14 - 104, rowY, 104, capH);

    // View selector (0.3.0) is retired from the UI but keeps valid bounds.
    {
        auto seg = juce::Rectangle<int> (172, 18).withRightX (statusPillBounds.getX() - 10)
                                                 .withY (rowY + 6);
        sidechainViewButton.setBounds (seg.removeFromLeft (86));
        analyzerViewButton.setBounds (seg);
    }

    // Presets: [<][ selector ][>]  [SAVE PRESET], centred on the editor.
    const int arrowSize = 26, selectorWidth = 214, saveWidth = 104, gap = 2;
    const int groupWidth = arrowSize + gap + selectorWidth + gap + arrowSize + 12 + saveWidth;
    auto group = juce::Rectangle<int> (groupWidth, capH)
                     .withCentre ({ width / 2 - 6, header.getCentreY() });
    presetPrevButton.setBounds (group.removeFromLeft (arrowSize));
    group.removeFromLeft (gap);
    presetBox.setBounds (group.removeFromLeft (selectorWidth));
    group.removeFromLeft (gap);
    presetNextButton.setBounds (group.removeFromLeft (arrowSize));
    group.removeFromLeft (12);
    savePresetButton.setBounds (group.removeFromLeft (saveWidth));

    // The name form replaces the preset group in place: same row, same height.
    {
        auto form = juce::Rectangle<int> (232 + 8 + 70 + 6 + 76, capH)
                        .withCentre ({ width / 2 - 6, header.getCentreY() });
        presetNameEditor.setBounds (form.removeFromLeft (232));
        form.removeFromLeft (8);
        confirmPresetButton.setBounds (form.removeFromLeft (70));
        form.removeFromLeft (6);
        cancelPresetButton.setBounds (form.removeFromLeft (76));
    }
    presetBox.setVisible (! showingInfo && ! savingPreset);
    presetPrevButton.setVisible (! showingInfo && ! savingPreset);
    presetNextButton.setVisible (! showingInfo && ! savingPreset);
    savePresetButton.setVisible (! showingInfo && ! savingPreset);
    presetNameEditor.setVisible (! showingInfo && savingPreset);
    confirmPresetButton.setVisible (! showingInfo && savingPreset);
    cancelPresetButton.setVisible (! showingInfo && savingPreset);

    // ---- transport row: switch (left) | preset status (centre) | offset ---
    area.removeFromTop (10);
    auto transportRow = area.removeFromTop (26);
    transportGateToggle.setBounds (transportRow.getX(), transportRow.getY() + 2, 262, 22);
    {
        auto offsetGroup = transportRow.removeFromRight (22 + 2 + 64 + 2 + 22);
        offsetPrevButton.setBounds (offsetGroup.removeFromLeft (22).withSizeKeepingCentre (22, 24));
        offsetGroup.removeFromLeft (2);
        offsetValueLabel.setBounds (offsetGroup.removeFromLeft (64).withSizeKeepingCentre (64, 24));
        offsetGroup.removeFromLeft (2);
        offsetNextButton.setBounds (offsetGroup.removeFromLeft (22).withSizeKeepingCentre (22, 24));
        transportRow.removeFromRight (10);
        offsetCaption.setBounds (transportRow.removeFromRight (110));
    }
    presetStatusLabel.setBounds (juce::Rectangle<int> (232, 28)
                                     .withCentre ({ width / 2, transportRow.getCentreY() }));
    presetStatusLabel.setVisible (! showingInfo && (savingPreset || presetStatusLabel.getText().isNotEmpty()));

    // ---- footer: Music-Prod signature -------------------------------------
    auto footer = area.removeFromBottom (32);
    logoArea = juce::Rectangle<int> (120, 28).withCentre ({ width / 2, footer.getCentreY() });

    // ---- sound controls: three equal knob groups on one baseline ----------
    // NOTE (knob-invisibility defect fix): knob rects are constructed directly
    // at their target position (x, y, w, h), never via withTop() on a (0,0)
    // rect, which keeps the bottom edge and collapses the height to 0.
    auto deck = area.removeFromBottom (96);
    deckRuleY = deck.getY() - 6;
    {
        constexpr int knobSize = 86, textGap = 14, textWidth = 110;
        constexpr int groupW = knobSize + textGap + textWidth;
        const int columnW = deck.getWidth() / 3;
        const int knobY = deck.getY() + (deck.getHeight() - knobSize) / 2;

        auto placeGroup = [&] (int column, ProdKnob& knob, juce::Label& caption, juce::Label& value)
        {
            const int x = deck.getX() + column * columnW + (columnW - groupW) / 2;
            knob.setBounds (x, knobY, knobSize, knobSize);
            const int textX = x + knobSize + textGap;
            const int textY = knobY + knobSize / 2 - 24;
            caption.setBounds (textX, textY, textWidth, 16);
            value.setBounds (textX, textY + 18, textWidth, 30);
        };
        placeGroup (0, amountKnob, amountCaption, amountValueLabel);
        placeGroup (1, duckLengthKnob, duckLengthCaption, duckLengthValueLabel);
        placeGroup (2, releaseKnob, releaseCaption, releaseValueLabel);
        duckLengthCaption.setText ("TIME", juce::dontSendNotification);
    }
    area.removeFromBottom (10);

    // ---- SMOOTH: a property of the curve, so it sits directly under it ----
    auto smoothRow = area.removeFromBottom (22);
    smoothCaption.setBounds (smoothRow.removeFromLeft (70));
    smoothValueLabel.setBounds (smoothRow.removeFromRight (48));
    smoothSlider.setBounds (smoothRow.reduced (8, 0));
    area.removeFromBottom (8);

    // ---- curve editor: everything that is left is the focal point ---------
    area.removeFromTop (8);
    graph.setBounds (area);
    graph.toFront (false);
}

void SideChainAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isRightButtonDown() && graph.isVisible()
        && graph.getBounds().contains (event.getPosition()))
        graph.resetCurve();
}

void SideChainAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    // Logo drawn last so it stays visible in both views, above the bg fill
    // but never overlapping the controls (resized() reserves its strip).
    drawMusicProdLogo (g, logoArea);
}

juce::String SideChainAudioProcessorEditor::demoStatusText() const
{
    const auto& gate = processorRef.getDemoGate();

    if (gate.isFullVersion())
        return "FULL VERSION";

    if (gate.expired())
        return "DEMO EXPIRED";

    return "DEMO " + sid::license::formatClock (gate.remainingSeconds());
}

void SideChainAudioProcessorEditor::paint (juce::Graphics& g)
{
    // The field: one warm-graphite gradient lit from the top (the INFO page
    // paints the same field over this when it is shown).
    sid::theme::fillField (g, getLocalBounds());

    if (showingInfo)
        return;

    // The one hairline: header above, instrument below.
    g.setColour (juce::Colour (sid::theme::kSeparator));
    g.fillRect (0, headerHeight - 1, getWidth(), 1);

    sid::branding::drawWordmark (g,
                                 juce::Rectangle<float> (20.0f, 9.0f, 170.0f, 36.0f),
                                 20.0f,
                                 juce::Colour (kTextPrimary),
                                 juce::Colour (kAccent));

    // Version mode: a dot and the words, right-aligned against INFO. Quiet in
    // the demo, mint when unlocked, amber when the session has ended.
    {
        const auto& gate = processorRef.getDemoGate();
        const auto tone = gate.isFullVersion() ? sid::theme::Tone::good
                        : gate.expired()       ? sid::theme::Tone::warn
                                               : sid::theme::Tone::muted;
        const auto text = demoStatusText();
        const auto f = sid::theme::captionFont (10.5f);
        const float textW = f.getStringWidthFloat (text) + 2.0f;
        auto r = statusPillBounds.toFloat();
        const float dotX = r.getRight() - textW - 12.0f;
        sid::theme::drawStatusDot (g, { dotX, r.getCentreY() }, tone, 6.0f);
        g.setFont (f);
        g.setColour (sid::theme::toneColour (tone == sid::theme::Tone::muted ? sid::theme::Tone::neutral
                                                                              : tone));
        g.drawText (text, r.withLeft (r.getRight() - textW), juce::Justification::centredRight, false);
    }

    // Recessed well behind the timing-offset readout.
    sid::theme::drawWell (g, offsetValueLabel.getBounds().toFloat(), 4.0f);
}
