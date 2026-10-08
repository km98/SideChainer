/*
    SideChain - production editor implementation (Phase 6).

    Design language: dark, premium, modern, minimal. Direct JUCE drawing,
    no image assets, system sans-serif typography.

    Control hierarchy: AMOUNT is the large primary knob; RELEASE is a
    clearly smaller secondary knob. Both share the same custom knob
    painting so the visual language stays consistent.
*/

#include "PluginEditor.h"
#include "Branding.h"

namespace
{
    // Shared palette (dark premium direction, pure black field).
    constexpr uint32_t kBackground   = 0xff000000;   // solid black
    constexpr uint32_t kTextPrimary  = 0xffe8ecf3;
    constexpr uint32_t kTextSubtle   = 0xff6b7687;
    constexpr uint32_t kAccent       = 0xff7fd1c0;   // mint (amount / accent)
    constexpr uint32_t kAccentWarm   = 0xffd8b26a;   // soft gold (release)
    constexpr uint32_t kKnobTrack    = 0xff22262e;   // reads on black
    constexpr uint32_t kKnobBody     = 0xff1c2027;
    constexpr uint32_t kControlBg    = 0xff15181e;   // preset selector body
    constexpr uint32_t kControlEdge  = 0xff2c3342;   // preset selector edge
    constexpr uint32_t kPresetText   = 0xffb8d8cf;   // soft mint text (preset name)
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
    auto area = getLocalBounds().toFloat().reduced (6.0f);
    const float diameter = juce::jmin (area.getWidth(), area.getHeight());
    auto knobRect = juce::Rectangle<float> (diameter, diameter).withCentre (area.getCentre());

    const float arcThickness = juce::jmax (3.0f, diameter * 0.055f);
    const float angleSpan = juce::MathConstants<float>::pi * 1.5f;
    const float startAngle = juce::MathConstants<float>::pi * 0.75f;
    const float fraction = getArcFraction();

    // Track arc
    juce::Path track;
    auto arcRect = knobRect.reduced (arcThickness);
    track.addArc (arcRect.getX(), arcRect.getY(), arcRect.getWidth(), arcRect.getHeight(),
                  startAngle, startAngle + angleSpan, true);
    g.setColour (juce::Colour (kKnobTrack));
    g.strokePath (track, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Value arc (fill colour is set per-knob via rotarySliderFillColourId)
    if (fraction > 0.001f)
    {
        juce::Path value;
        value.addArc (arcRect.getX(), arcRect.getY(), arcRect.getWidth(), arcRect.getHeight(),
                      startAngle, startAngle + angleSpan * fraction, true);
        g.setColour (findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (value, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // Knob body
    juce::DropShadow shadow (juce::Colour (0x66000000), 10, { 0, 3 });
    juce::Path bodyPath;
    bodyPath.addEllipse (knobRect.reduced (arcThickness * 1.9f));
    shadow.drawForPath (g, bodyPath);
    g.setColour (juce::Colour (kKnobBody));
    g.fillPath (bodyPath);
    g.setColour (juce::Colour (0xff323a4a));
    g.strokePath (bodyPath, juce::PathStrokeType (1.0f));

    // Pointer line
    const auto centre = knobRect.getCentre();
    const float innerR = diameter * 0.16f;
    const float outerR = diameter * 0.30f;
    const float angle = startAngle + angleSpan * fraction;
    juce::Line<float> pointer (centre.getPointOnCircumference (innerR, angle),
                               centre.getPointOnCircumference (outerR, angle));
    g.setColour (juce::Colour (kTextPrimary));
    g.drawLine (pointer, 2.2f);
}

//==============================================================================
// Editor
//==============================================================================
SideChainAudioProcessorEditor::SideChainAudioProcessorEditor (SideChainAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      authManager (std::make_unique<sid::auth::HttpAuthTransport> (
                       "https://wfpeajmdojcjqyrsnxbk.supabase.co/functions/v1/vyre-plugin-auth"),
                   juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("Music-Prod/SideChain/auth.json").getFullPathName(),
                   "sidechain",     // product identity (server-side allowlist key)
                   "SideChain",     // device_name (human-readable, token record)
                   JucePlugin_VersionString),
      infoPage (authManager, JucePlugin_VersionString)
{
    // Phase 7A-era persisted token (if any) is loaded outside plugin state.
    authManager.loadPersistedToken();

    // Production: let the auth thread poll for browser approval so a
    // successful approval flips WAITING FOR APPROVAL -> SIGNED IN
    // automatically (tests keep this off and drive polls deterministically).
    authManager.setBackgroundPolling (true);
    // PRESET selector: a compact, styled ComboBox. The closed box shows the
    // derived current identity (factory name / Default / Custom); the popup
    // lists the factory presets plus a Default entry. Applying a preset goes
    // through the processor's parameter-notification path only.
    presetBox.setJustificationType (juce::Justification::centred);
    presetBox.setEditableText (false);
    presetBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (kControlBg));
    presetBox.setColour (juce::ComboBox::outlineColourId,    juce::Colour (kControlEdge));
    presetBox.setColour (juce::ComboBox::arrowColourId,      juce::Colour (kTextSubtle));
    presetBox.setColour (juce::ComboBox::buttonColourId,     juce::Colour (kControlBg));
    presetBox.setColour (juce::ComboBox::textColourId,       juce::Colour (kPresetText));
    presetBox.setTooltip ("Factory presets (Amount + Release). Manual changes show Custom.");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id == kDefaultItemId)
        {
            processorRef.applyDefaultPreset();
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
        shape->setFill (juce::Colour (kPresetText));
        b.setImages (shape);
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
        b.setColour (juce::DrawableButton::backgroundColourId, juce::Colour (0xff111419));
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

    offsetValueLabel.setFont (juce::Font (12.0f, juce::Font::bold));
    offsetValueLabel.setColour (juce::Label::textColourId, juce::Colour (kTextPrimary));
    offsetValueLabel.setJustificationType (juce::Justification::centred);
    offsetValueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (offsetValueLabel);

    offsetCaption.setText ("TIMING OFFSET", juce::dontSendNotification);
    offsetCaption.setFont (juce::Font (9.0f, juce::Font::plain));
    offsetCaption.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    offsetCaption.setJustificationType (juce::Justification::centred);
    offsetCaption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (offsetCaption);
    refreshOffsetDisplay();

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
    addAndMakeVisible (sidechainViewButton);
    addAndMakeVisible (analyzerViewButton);
    selectView (GraphComponent::ViewMode::sidechain);   // DEFAULT view

    // PRIMARY control: SIDECHAIN AMOUNT
    amountKnob.setTooltip ("Ducking intensity - how much the main signal ducks");
    amountKnob.setDoubleClickReturnValue (true, 50.0); // double-click resets to default
    addAndMakeVisible (amountKnob);

    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getParameters(), "sidechainAmount", amountKnob);

    amountValueLabel.setFont (juce::Font (26.0f, juce::Font::bold));
    amountValueLabel.setColour (juce::Label::textColourId, juce::Colour (kTextPrimary));
    amountValueLabel.setJustificationType (juce::Justification::centred);

    amountCaption.setText ("AMOUNT", juce::dontSendNotification);
    amountCaption.setFont (juce::Font (12.0f, juce::Font::plain));
    amountCaption.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    amountCaption.setJustificationType (juce::Justification::centred);

    // SECONDARY control: RELEASE (envelope SHAPE; preset character)
    releaseKnob.setTooltip ("Envelope shape: plateau vs tail split (preset character)");
    releaseKnob.setDoubleClickReturnValue (true, 150.0); // reset to 150 ms default
    releaseKnob.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (kAccentWarm));
    addAndMakeVisible (releaseKnob);

    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getParameters(), "release", releaseKnob);

    releaseValueLabel.setFont (juce::Font (19.0f, juce::Font::bold));
    releaseValueLabel.setColour (juce::Label::textColourId, juce::Colour (kTextPrimary));
    releaseValueLabel.setJustificationType (juce::Justification::centred);

    releaseCaption.setText ("RELEASE (SHAPE)", juce::dontSendNotification);
    releaseCaption.setFont (juce::Font (11.0f, juce::Font::plain));
    releaseCaption.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    releaseCaption.setJustificationType (juce::Justification::centred);


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

    duckLengthValueLabel.setFont (juce::Font (19.0f, juce::Font::bold));
    duckLengthValueLabel.setColour (juce::Label::textColourId, juce::Colour (kTextPrimary));
    duckLengthValueLabel.setJustificationType (juce::Justification::centred);

    duckLengthCaption.setText ("DUCK LENGTH", juce::dontSendNotification);
    duckLengthCaption.setFont (juce::Font (11.0f, juce::Font::plain));
    duckLengthCaption.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    duckLengthCaption.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (graph);
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
    infoButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (kControlBg));
    infoButton.setColour (juce::TextButton::textColourOffId, juce::Colour (kTextPrimary));
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
        releaseValueLabel.repaint();
        releaseCaption.repaint();
        graph.repaint();
        presetBox.repaint();
        offsetValueLabel.repaint();
        offsetCaption.repaint();
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
        refreshRenderingAfterLifecycleChange();
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
    presetPrevButton.setVisible (true);
    presetNextButton.setVisible (true);
    offsetPrevButton.setVisible (true);
    offsetNextButton.setVisible (true);
    offsetValueLabel.setVisible (true);
    offsetCaption.setVisible (true);
    sidechainViewButton.setVisible (true);
    analyzerViewButton.setVisible (true);
    amountKnob.setVisible (true);
    duckLengthKnob.setVisible (true);
    releaseKnob.setVisible (true);
    amountValueLabel.setVisible (true);
    amountCaption.setVisible (true);
    duckLengthValueLabel.setVisible (true);
    duckLengthCaption.setVisible (true);
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
    presetBox.setVisible (false);
    presetPrevButton.setVisible (false);
    presetNextButton.setVisible (false);
    offsetPrevButton.setVisible (false);
    offsetNextButton.setVisible (false);
    offsetValueLabel.setVisible (false);
    offsetCaption.setVisible (false);
    sidechainViewButton.setVisible (false);
    analyzerViewButton.setVisible (false);
    amountKnob.setVisible (false);
    duckLengthKnob.setVisible (false);
    releaseKnob.setVisible (false);
    amountValueLabel.setVisible (false);
    amountCaption.setVisible (false);
    duckLengthValueLabel.setVisible (false);
    duckLengthCaption.setVisible (false);
    releaseValueLabel.setVisible (false);
    releaseCaption.setVisible (false);
    infoButton.setVisible (false);
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
    const int w = 110;
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
    g.setOpacity (0.92f);
    g.drawImageWithin (logoMask,
                       area.getCentreX() - w / 2, area.getY(), w, h,
                       juce::RectanglePlacement (juce::RectanglePlacement::centred),
                       false);   // composite via the mask's own alpha channel
    g.setOpacity (1.0f);
}

void SideChainAudioProcessorEditor::buildPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);

    const auto& table = sid::presets::factoryPresets();
    for (int i = 0; i < table.size(); ++i)
        presetBox.addItem (table.getReference (i).name, kPresetItemIdBase + i);

    presetBox.addSeparator();
    presetBox.addItem ("Default (50% / 150 ms)", kDefaultItemId);
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

    // Drain real DSP frames into the graph history (lock-free).
    graph.consumeFrames (processorRef.graphFifo);

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

    // Keep the preset identity and offset readout in sync with the LIVE
    // values (covers host automation, undo, state restore and manual edits).
    refreshPresetDisplay();
    refreshOffsetDisplay();
}

void SideChainAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (14);

    // Header
    auto header = area.removeFromTop (34);
    header.removeFromBottom (6);
    // (header text drawn in paint(); keep the space)

    // INFO button (header, right edge); paint() keeps header text clear of it.
    infoButtonBounds = juce::Rectangle<int> (getWidth() - 14 - 54, 9, 54, 24);
    infoButton.setBounds (infoButtonBounds);

    // View selector (0.3.0): compact segmented [SIDECHAIN | ANALYZER] in
    // the header (title area), just left of the INFO button.
    {
        const int segW = 86, segH = 18;
        auto seg = juce::Rectangle<int> (segW * 2, segH)
                       .withRightX (infoButtonBounds.getX() - 10)
                       .withY (infoButtonBounds.getY() + 3);
        sidechainViewButton.setBounds (seg.removeFromLeft (segW));
        analyzerViewButton.setBounds (seg);
    }

    // INFO page covers the editor when shown (Phase 9 defect fix: without a
    // bounds assignment the page had zero size even when made visible).
    infoPage.setBounds (getLocalBounds());

    // Preset strip: [ prev ] [ selector ] [ next ], centred; the PRESET
    // caption sits clearly BELOW the group with real spacing (no overlap).
    auto presetStrip = area.removeFromTop (30);
    const int arrowSize = 22;
    const int selectorWidth = 210;
    const int groupWidth = arrowSize + 5 + selectorWidth + 5 + arrowSize;
    auto groupBounds = juce::Rectangle<int> (groupWidth, 24)
                            .withCentre (presetStrip.getCentre());
    presetPrevButton.setBounds (groupBounds.removeFromLeft (arrowSize)
                                    .withSizeKeepingCentre (arrowSize, 22));
    groupBounds.removeFromLeft (5);
    presetBox.setBounds (groupBounds.removeFromLeft (selectorWidth));
    groupBounds.removeFromLeft (5);
    presetNextButton.setBounds (groupBounds.removeFromLeft (arrowSize)
                                    .withSizeKeepingCentre (arrowSize, 22));
    presetCaptionBounds = juce::Rectangle<int> (groupBounds.getX() - selectorWidth / 2 - 2,
                                                presetBox.getBottom() + 4,
                                                selectorWidth + 4, 12);
    area.removeFromTop (18); // room for the caption below the selector

    // SIDECHAIN OFFSET row (0.3.0): [◀][ 0 ms ][▶], smaller + dimmer than
    // the preset group so the two navigation rows cannot be confused.
    auto offsetStrip = area.removeFromTop (20);
    const int offBtn = 18;
    const int offLabelW = 64;
    const int offGroupW = offBtn + 4 + offLabelW + 4 + offBtn;
    auto offBounds = juce::Rectangle<int> (offGroupW, 18)
                         .withCentre (offsetStrip.getCentre());
    offsetPrevButton.setBounds (offBounds.removeFromLeft (offBtn)
                                    .withSizeKeepingCentre (offBtn, 16));
    offBounds.removeFromLeft (4);
    offsetValueLabel.setBounds (offBounds.removeFromLeft (offLabelW));
    offBounds.removeFromLeft (4);
    offsetNextButton.setBounds (offBounds.removeFromLeft (offBtn)
                                    .withSizeKeepingCentre (offBtn, 16));
    offsetCaption.setBounds (juce::Rectangle<int> (offGroupW + 40, 12)
                                 .withCentre (offsetStrip.getCentre())
                                 .withY (offsetValueLabel.getBottom() + 1));
    area.removeFromTop (12); // room for the offset caption

    // ---- Bottom reservation: logo strip occupies one 28 px band, the
    // three-knob control row gets its own clearly separated space above it.
    // (Layout defect fix: DUCK LENGTH's value/caption previously ran into
    // the Music-Prod logo band; now every control owns exclusive space.)
    auto bottomBand = area.removeFromBottom (28);
    logoArea = juce::Rectangle<int> (220, 26).withCentre (bottomBand.getCentre());

    // ---- Control row: three knobs, each guaranteed knob + value + caption
    // inside the remaining space (label/caption heights are budgeted below
    // each knob; the row cannot reach the logo band).
    // NOTE (knob-invisibility defect fix): knob rects MUST be constructed
    // directly at their target position (x, y, w, h). The previous code
    // built them at (0,0) and called withTop(y), which keeps the BOTTOM edge
    // and collapses the height to 0 - knobs became invisible while labels
    // (explicit heights) stayed visible.
    auto controlArea = area.removeFromBottom (152);
    controlArea.removeFromTop (6);
    const int captionH = 16, valueH = 22;
    const int knobMax = controlArea.getHeight() - valueH - captionH - 4;

    const int amountSize  = juce::jmin (96, knobMax);
    const int midSize     = juce::jmin (76, knobMax - 4);
    const int releaseSize = juce::jmin (64, knobMax - 10);

    const int centreX = controlArea.getCentreX();
    const juce::Rectangle<int> amountBounds (centreX - 170 - amountSize / 2,
                                             controlArea.getY() + 2,
                                             amountSize, amountSize);
    const juce::Rectangle<int> duckLenBounds (centreX - midSize / 2,
                                              controlArea.getY() + 10,
                                              midSize, midSize);
    const juce::Rectangle<int> releaseBounds (centreX + 170 - releaseSize / 2,
                                              controlArea.getY() + 16,
                                              releaseSize, releaseSize);

    amountKnob.setBounds (amountBounds);
    duckLengthKnob.setBounds (duckLenBounds);
    releaseKnob.setBounds (releaseBounds);

    // Value labels sized knob+40 (centred): wide enough for the text, but
    // narrow enough that adjacent labels can never collide.
    amountValueLabel.setBounds (amountBounds.getX() - 40,
                                amountBounds.getBottom() + 2,
                                amountBounds.getWidth() + 40, valueH);
    amountCaption.setBounds (amountBounds.getX() - 60,
                             amountValueLabel.getBottom(),
                             amountBounds.getWidth() + 80, captionH);

    duckLengthValueLabel.setBounds (duckLenBounds.getX() - 40,
                                    duckLenBounds.getBottom() + 2,
                                    duckLenBounds.getWidth() + 40, valueH);
    duckLengthCaption.setBounds (duckLenBounds.getX() - 40,
                                 duckLengthValueLabel.getBottom(),
                                 duckLenBounds.getWidth() + 40, captionH);

    releaseValueLabel.setBounds (releaseBounds.getX() - 40,
                                 releaseBounds.getBottom() + 2,
                                 releaseBounds.getWidth() + 40, valueH);
    releaseCaption.setBounds (releaseBounds.getX() - 40,
                              releaseValueLabel.getBottom(),
                              releaseBounds.getWidth() + 40, captionH);

    // Graph: everything above the control row (dominant element again).
    graph.setBounds (area);
}

void SideChainAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    // Logo drawn last so it stays visible in both views, above the bg fill
    // but never overlapping the controls (resized() reserves its strip).
    drawMusicProdLogo (g, logoArea);
}

void SideChainAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (kBackground));

    // Header: PumpCurve wordmark (left) - INFO button occupies the right.
    if (showingInfo)
        return; // the INFO page paints its own branding over the base

    sid::branding::drawWordmark (g,
                                 juce::Rectangle<float> (14.0f, 8.0f, 220.0f, 34.0f),
                                 21.0f,
                                 juce::Colour (kTextPrimary),
                                 juce::Colour (kAccent));

    // Panel edge around the graph
    g.setColour (juce::Colour (kControlEdge));
    g.drawRect (graph.getBounds(), 1);

    // PRESET caption, clearly BELOW the selector with real spacing (was a
    // 5px-high label overlapping the selector's bottom edge).
    if (presetBox.isVisible())
    {
        g.setFont (juce::Font (10.0f));
        g.setColour (juce::Colour (kTextSubtle));
        g.drawText ("PRESET", presetCaptionBounds, juce::Justification::centred);
    }
}
