/*
    PumpCurve - shared visual theme (0.5.0, second redesign pass).

    One palette, one set of drawing primitives and one LookAndFeel for every
    surface of the editor (main view, curve editor, INFO page).

    Visual benchmark: the Music-Prod "MP Gain" editor - a warm-neutral graphite
    field lit from above (a single vertical gradient), one hairline rule, large
    knobs with a clear gap between the cap and a rounded value ring, a short
    precise pointer, warm off-white type with a strong name/label/value
    hierarchy, and almost no outlines. PumpCurve keeps its own identity: the
    mint brand accent (0xff7fd1c0, unchanged) and its wordmark.

    Rules this file enforces:
      * surfaces are separated by LIGHT and DEPTH (recessed wells, raised caps),
        not by boxes and borders;
      * buttons are filled, borderless "caps" in three weights (secondary,
        accent, primary) with hover / pressed / focus / disabled states;
      * colour supports state, it never carries it alone - every status keeps
        its own words.

    Header-only on purpose: it adds no translation unit, so the Projucer
    project, the Xcode project and the Windows build need no new entries.
*/

#pragma once

#include <JuceHeader.h>

namespace sid::theme
{
    // ---- field (warm graphite, lit from above) -------------------------------
    constexpr uint32_t kFieldTop    = 0xff18181d;   // rgb(24,24,29)
    constexpr uint32_t kFieldBottom = 0xff0f0f12;   // rgb(15,15,18)
    constexpr uint32_t kBase        = 0xff131317;   // mid-field (solid fallback)
    constexpr uint32_t kHeader      = kFieldTop;    // the header is simply the top of the field

    // ---- depth ---------------------------------------------------------------
    constexpr uint32_t kWell        = 0xff0b0b0e;   // recessed: plot, fields, selector
    constexpr uint32_t kWellEdge    = 0xff1d1d23;   // the lit lower lip of a recess
    constexpr uint32_t kPanel       = kWell;
    constexpr uint32_t kControl     = 0xff24242b;   // raised cap (buttons, knob body)
    constexpr uint32_t kControlHover= 0xff2b2b33;
    constexpr uint32_t kControlDown = 0xff1d1d23;
    constexpr uint32_t kEdge        = 0xff34343d;   // rgb(52,52,61): cap rim
    constexpr uint32_t kEdgeStrong  = 0xff464652;
    constexpr uint32_t kTrack       = 0xff2a2a31;   // rgb(42,42,49): value-ring track
    constexpr uint32_t kSeparator   = 0xff232329;   // rgb(35,35,41): the one hairline
    constexpr uint32_t kGrid        = 0xff17171c;
    constexpr uint32_t kGridMajor   = 0xff202027;

    // ---- text ----------------------------------------------------------------
    constexpr uint32_t kText        = 0xfff5f4f2;   // rgb(245,244,242) warm off-white
    constexpr uint32_t kTextMuted   = 0xff9a99a3;   // labels (>= 6:1 on the field)
    constexpr uint32_t kTextFaint   = 0xff6a6973;   // axis ticks, disabled
    constexpr uint32_t kTextOnAccent= 0xff07120f;

    // ---- accents and states --------------------------------------------------
    constexpr uint32_t kAccent      = 0xff7fd1c0;   // PumpCurve mint (brand, unchanged)
    constexpr uint32_t kAccentDim   = 0xff3f6d64;
    constexpr uint32_t kWarn        = 0xffe0a750;   // rgb(224,167,80): attention
    constexpr uint32_t kError       = 0xffe5786d;   // rejected / revoked / auth error
    constexpr uint32_t kInput       = 0xff8d8c98;   // incoming signal: neutral, behind

    enum class Tone { neutral, muted, good, warn, bad };

    inline juce::Colour toneColour (Tone t) noexcept
    {
        switch (t)
        {
            case Tone::good:    return juce::Colour (kAccent);
            case Tone::warn:    return juce::Colour (kWarn);
            case Tone::bad:     return juce::Colour (kError);
            case Tone::muted:   return juce::Colour (kTextMuted);
            case Tone::neutral: break;
        }
        return juce::Colour (kText);
    }

    // ---- type ----------------------------------------------------------------
    inline juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::Font::getDefaultSansSerifFontName(), size,
                           bold ? juce::Font::bold : juce::Font::plain);
    }

    /** Small uppercase caption (control labels, section names). */
    inline juce::Font captionFont (float size = 10.5f)
    {
        auto f = font (size, true);
        f.setExtraKerningFactor (0.06f);
        return f;
    }

    /** Numeric readouts (values beside knobs, offset, smooth). */
    inline juce::Font valueFont (float size)
    {
        auto f = font (size, true);
        f.setExtraKerningFactor (-0.01f);
        return f;
    }

    //==========================================================================
    // Drawing primitives
    //==========================================================================

    /** The field colour at a vertical position (0 = top, 1 = bottom). */
    inline juce::Colour fieldColourAt (float proportion) noexcept
    {
        return juce::Colour (kFieldTop).interpolatedWith (juce::Colour (kFieldBottom),
                                                          juce::jlimit (0.0f, 1.0f, proportion));
    }

    /** The whole editor field: one gradient, lit from the top. */
    inline void fillField (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (kFieldTop), 0.0f, (float) bounds.getY(),
                                                 juce::Colour (kFieldBottom), 0.0f, (float) bounds.getBottom(),
                                                 false));
        g.fillRect (bounds);
    }

    /** A recessed well: darker than the field, a soft shadow along its top
        edge and a faint lit lip along the bottom - depth without a border. */
    inline void drawWell (juce::Graphics& g, juce::Rectangle<float> r, float radius)
    {
        g.setColour (juce::Colour (kWell));
        g.fillRoundedRectangle (r, radius);

        juce::Path clip;
        clip.addRoundedRectangle (r, radius);
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x66000000), 0.0f, r.getY(),
                                                 juce::Colours::transparentBlack, 0.0f,
                                                 r.getY() + juce::jmin (10.0f, r.getHeight() * 0.5f), false));
        g.fillRect (r.withHeight (juce::jmin (10.0f, r.getHeight() * 0.5f)));
        g.setColour (juce::Colour (kWellEdge));
        g.fillRect (r.getX() + radius, r.getBottom() - 1.0f, r.getWidth() - radius * 2.0f, 1.0f);
    }

    /** A small filled status dot (supports, never replaces, the words). */
    inline void drawStatusDot (juce::Graphics& g, juce::Point<float> centre, Tone tone, float d = 7.0f)
    {
        const auto c = toneColour (tone);
        g.setColour (c.withAlpha (0.18f));
        g.fillEllipse (juce::Rectangle<float> (d * 2.0f, d * 2.0f).withCentre (centre));
        g.setColour (c);
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (centre));
    }

    /** The PumpCurve rotary control, in the MP Gain construction: a rounded
        track ring, a clear gap, a raised cap lit from above and a short
        precise pointer near the cap's rim. `fraction` is 0..1. */
    inline void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float fraction,
                          juce::Colour accent, bool hot, bool focused, bool enabled)
    {
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto centre = bounds.getCentre();
        const float ringThickness = juce::jlimit (3.5f, 5.0f, size * 0.058f);
        const float ringRadius = size * 0.5f - ringThickness * 0.5f - 1.0f;
        const float span = juce::MathConstants<float>::pi * 1.5f;   // 7:30 .. 4:30
        const float from = -span * 0.5f;
        const float valueAngle = from + span * juce::jlimit (0.0f, 1.0f, fraction);
        const auto dim = [enabled] (juce::Colour c) { return enabled ? c : c.withMultipliedAlpha (0.4f); };

        auto arc = [&] (float a0, float a1)
        {
            juce::Path p;
            p.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, a0, a1, true);
            return p;
        };
        const juce::PathStrokeType ring (ringThickness, juce::PathStrokeType::curved,
                                         juce::PathStrokeType::rounded);

        g.setColour (dim (juce::Colour (hot ? kEdge : kTrack)));
        g.strokePath (arc (from, from + span), ring);

        if (fraction > 0.001f)
        {
            // A faint bloom under the value ring, then the ring itself.
            g.setColour (dim (accent.withAlpha (0.16f)));
            g.strokePath (arc (from, valueAngle),
                          juce::PathStrokeType (ringThickness * 2.4f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
            g.setColour (dim (accent));
            g.strokePath (arc (from, valueAngle), ring);
        }

        // Cap: inset well inside the ring (the gap is what makes it read as a
        // physical control), soft drop shadow, lit from the top.
        const float capRadius = ringRadius - ringThickness * 0.5f - juce::jmax (5.0f, size * 0.085f);
        const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
        juce::Path capPath;
        capPath.addEllipse (cap);
        juce::DropShadow (juce::Colour (0x99000000), juce::roundToInt (size * 0.10f), { 0, 3 })
            .drawForPath (g, capPath);
        g.setGradientFill (juce::ColourGradient (juce::Colour (hot ? 0xff31313a : 0xff2c2c34),
                                                 centre.x, cap.getY(),
                                                 juce::Colour (0xff1c1c22), centre.x, cap.getBottom(), false));
        g.fillPath (capPath);
        g.setColour (juce::Colour (hot ? kEdgeStrong : kEdge));
        g.strokePath (capPath, juce::PathStrokeType (1.0f));
        // Specular hint along the upper rim.
        {
            juce::Path rim;
            rim.addCentredArc (centre.x, centre.y, capRadius - 1.5f, capRadius - 1.5f, 0.0f,
                               -juce::MathConstants<float>::pi * 0.38f,
                               juce::MathConstants<float>::pi * 0.38f, true);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.strokePath (rim, juce::PathStrokeType (1.2f));
        }

        // Pointer: short, rounded, near the rim (MP Gain: r-30 .. r-18 of 66).
        const juce::Line<float> pointer (centre.getPointOnCircumference (capRadius * 0.42f, valueAngle),
                                         centre.getPointOnCircumference (capRadius * 0.80f, valueAngle));
        juce::Path pointerPath;
        pointerPath.startNewSubPath (pointer.getStart());
        pointerPath.lineTo (pointer.getEnd());
        g.setColour (dim (juce::Colour (kText)));
        g.strokePath (pointerPath, juce::PathStrokeType (juce::jmax (2.4f, size * 0.035f),
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));

        if (focused)
        {
            g.setColour (accent.withAlpha (0.55f));
            g.drawEllipse (cap.expanded (3.0f), 1.5f);
        }
    }

    //==========================================================================
    // Button weights, set per button with setButtonStyle().
    //   secondary (default): raised graphite cap
    //   accent:   mint-tinted cap with mint text (the action a state invites)
    //   primary:  solid mint with dark text (the one confirm action of a form)
    //   quiet:    text only until hovered (low-emphasis utilities)
    enum class ButtonStyle { secondary, accent, primary, quiet };

    inline void setButtonStyle (juce::Button& b, ButtonStyle style)
    {
        b.getProperties().set ("pcStyle", (int) style);
        b.repaint();
    }

    inline ButtonStyle buttonStyleOf (const juce::Button& b)
    {
        const auto v = b.getProperties()["pcStyle"];
        return v.isVoid() ? ButtonStyle::secondary : (ButtonStyle) (int) v;
    }

    //==========================================================================
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (kBase));

            setColour (juce::TextButton::buttonColourId,   juce::Colour (kControl));
            setColour (juce::TextButton::buttonOnColourId, juce::Colour (kAccent));
            setColour (juce::TextButton::textColourOffId,  juce::Colour (kText));
            setColour (juce::TextButton::textColourOnId,   juce::Colour (kTextOnAccent));

            setColour (juce::ComboBox::backgroundColourId, juce::Colour (kWell));
            setColour (juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
            setColour (juce::ComboBox::textColourId,       juce::Colour (kText));
            setColour (juce::ComboBox::arrowColourId,      juce::Colour (kTextMuted));
            setColour (juce::ComboBox::focusedOutlineColourId, juce::Colour (kAccent));

            setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xff1b1b20));
            setColour (juce::PopupMenu::textColourId,                  juce::Colour (kText));
            setColour (juce::PopupMenu::headerTextColourId,            juce::Colour (kTextMuted));
            setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (kControlHover));
            setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colour (kAccent));

            setColour (juce::TextEditor::backgroundColourId,     juce::Colour (kWell));
            setColour (juce::TextEditor::outlineColourId,        juce::Colours::transparentBlack);
            setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (kAccent));
            setColour (juce::TextEditor::textColourId,           juce::Colour (kText));
            setColour (juce::TextEditor::highlightColourId,      juce::Colour (kAccent).withAlpha (0.30f));
            setColour (juce::TextEditor::highlightedTextColourId,juce::Colour (kText));
            setColour (juce::CaretComponent::caretColourId,      juce::Colour (kAccent));

            setColour (juce::Label::textColourId,       juce::Colour (kText));
            setColour (juce::ToggleButton::textColourId, juce::Colour (kText));
            setColour (juce::ToggleButton::tickColourId, juce::Colour (kAccent));

            setColour (juce::Slider::trackColourId,      juce::Colour (kAccent));
            setColour (juce::Slider::backgroundColourId, juce::Colour (kTrack));
            setColour (juce::Slider::thumbColourId,      juce::Colour (kText));

            setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1b1b20));
            setColour (juce::TooltipWindow::textColourId,       juce::Colour (kText));
            setColour (juce::TooltipWindow::outlineColourId,    juce::Colour (kEdge));
        }

        // ---- buttons: borderless caps ---------------------------------------
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                   bool over, bool down) override
        {
            const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const bool enabled = b.isEnabled();
            const float corner = 4.0f;
            const auto style = buttonStyleOf (b);

            juce::Colour top, bottom;
            switch (style)
            {
                case ButtonStyle::primary:
                {
                    const auto c = juce::Colour (kAccent);
                    top = down ? c.darker (0.15f) : over ? c.brighter (0.10f) : c;
                    bottom = top.darker (0.12f);
                    break;
                }
                case ButtonStyle::accent:
                {
                    const float a = down ? 0.10f : over ? 0.22f : 0.15f;
                    top = bottom = juce::Colour (kAccent).withAlpha (a);
                    break;
                }
                case ButtonStyle::quiet:
                {
                    if (! (over || down) || ! enabled)
                    {
                        top = bottom = juce::Colours::transparentBlack;
                        break;
                    }
                    top = bottom = juce::Colour (down ? kControlDown : kControl);
                    break;
                }
                case ButtonStyle::secondary:
                {
                    top    = juce::Colour (down ? kControlDown : over ? 0xff303039 : 0xff2a2a31);
                    bottom = juce::Colour (down ? kControlDown : over ? 0xff26262d : kControl).darker (0.10f);
                    break;
                }
            }

            if (! enabled)
            {
                top = top.withMultipliedAlpha (0.5f);
                bottom = bottom.withMultipliedAlpha (0.5f);
            }

            if (style != ButtonStyle::quiet && enabled && ! down)
            {
                juce::Path shadowPath;
                shadowPath.addRoundedRectangle (r, corner);
                juce::DropShadow (juce::Colour (0x55000000), 4, { 0, 1 }).drawForPath (g, shadowPath);
            }

            g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, corner);

            if (style == ButtonStyle::secondary && enabled && ! down)
            {
                // Lit upper edge: the cap catches the light, no outline needed.
                g.setColour (juce::Colours::white.withAlpha (over ? 0.08f : 0.05f));
                g.fillRoundedRectangle (r.withHeight (1.0f).reduced (corner, 0.0f), 0.5f);
            }

            if (b.hasKeyboardFocus (false) && enabled)
            {
                g.setColour (juce::Colour (kAccent).withAlpha (0.7f));
                g.drawRoundedRectangle (r.expanded (0.5f), corner + 0.5f, 1.2f);
            }
        }

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
        {
            return captionFont (juce::jlimit (10.5f, 11.5f, (float) buttonHeight * 0.40f));
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool) override
        {
            juce::Colour colour;
            switch (buttonStyleOf (b))
            {
                case ButtonStyle::primary: colour = juce::Colour (kTextOnAccent); break;
                case ButtonStyle::accent:  colour = juce::Colour (kAccent); break;
                case ButtonStyle::quiet:   colour = juce::Colour (over ? kText : kTextMuted); break;
                case ButtonStyle::secondary:
                    colour = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId
                                                              : juce::TextButton::textColourOffId);
                    break;
            }
            if (! b.isEnabled())
                colour = juce::Colour (kTextFaint);
            g.setColour (colour);
            g.setFont (getTextButtonFont (b, b.getHeight()));
            g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (8, 0),
                              juce::Justification::centred, 1, 0.85f);
        }

        // ---- switch (ACTIVE ONLY DURING PLAY/REC) ---------------------------
        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
        {
            const auto bounds = b.getLocalBounds().toFloat();
            const float h = juce::jmin (16.0f, bounds.getHeight() - 2.0f);
            const float w = h * 1.85f;
            juce::Rectangle<float> track (bounds.getX() + 1.0f, bounds.getCentreY() - h * 0.5f, w, h);
            const bool on = b.getToggleState();
            const float alpha = b.isEnabled() ? 1.0f : 0.45f;

            if (on)
            {
                g.setColour (juce::Colour (kAccent).withAlpha ((over ? 1.0f : 0.92f) * alpha));
                g.fillRoundedRectangle (track, h * 0.5f);
            }
            else
            {
                drawWell (g, track, h * 0.5f);
            }

            const float d = h - 4.0f;
            const juce::Rectangle<float> knob (on ? track.getRight() - d - 2.0f : track.getX() + 2.0f,
                                               track.getY() + 2.0f, d, d);
            juce::Path knobPath;
            knobPath.addEllipse (knob);
            juce::DropShadow (juce::Colour (0x88000000), 3, { 0, 1 }).drawForPath (g, knobPath);
            g.setColour ((on ? juce::Colour (kText) : juce::Colour (over ? 0xffb8b7c0 : 0xff8d8c97))
                             .withMultipliedAlpha (alpha));
            g.fillPath (knobPath);

            if (b.hasKeyboardFocus (false))
            {
                g.setColour (juce::Colour (kAccent).withAlpha (0.6f));
                g.drawRoundedRectangle (track.expanded (2.0f), h * 0.5f + 2.0f, 1.0f);
            }

            g.setColour (b.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (alpha));
            g.setFont (captionFont (10.5f));
            g.drawText (b.getButtonText(), bounds.withTrimmedLeft (w + 10.0f).toNearestInt(),
                        juce::Justification::centredLeft, true);
        }

        // ---- preset selector: a recessed display, not a boxed field ---------
        void drawComboBox (juce::Graphics& g, int width, int height, bool,
                           int, int, int, int, juce::ComboBox& box) override
        {
            const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
            drawWell (g, r, 5.0f);
            if (box.isMouseOver (true))
            {
                g.setColour (juce::Colours::white.withAlpha (0.03f));
                g.fillRoundedRectangle (r, 5.0f);
            }
            if (box.hasKeyboardFocus (true))
            {
                g.setColour (juce::Colour (kAccent).withAlpha (0.7f));
                g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            }

            const float cx = (float) width - 15.0f, cy = (float) height * 0.5f;
            juce::Path chevron;
            chevron.startNewSubPath (cx - 3.5f, cy - 1.75f);
            chevron.lineTo (cx, cy + 1.75f);
            chevron.lineTo (cx + 3.5f, cy - 1.75f);
            g.setColour (box.findColour (juce::ComboBox::arrowColourId));
            g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override { return font (14.0f, true); }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (26, 1, box.getWidth() - 52, box.getHeight() - 2);
            label.setFont (getComboBoxFont (box));
            label.setJustificationType (juce::Justification::centred);
        }

        juce::Font getPopupMenuFont() override { return font (13.5f); }

        void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
        {
            g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
            g.setColour (juce::Colour (kSeparator));
            g.drawRect (0, 0, width, height, 1);
        }

        // ---- text fields: recessed, lit with mint only when focused ---------
        void drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                    juce::TextEditor& editor) override
        {
            if (! editor.isEnabled() || ! editor.hasKeyboardFocus (true) || editor.isReadOnly())
                return;
            g.setColour (juce::Colour (kAccent).withAlpha (0.8f));
            g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                                        .reduced (0.5f), 5.0f, 1.0f);
        }

        void fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                       juce::TextEditor&) override
        {
            drawWell (g, juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height), 5.0f);
        }

        // ---- linear slider (SMOOTH): a ring track laid flat ------------------
        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float, float,
                               const juce::Slider::SliderStyle style, juce::Slider& s) override
        {
            if (style != juce::Slider::LinearHorizontal)
            {
                LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, s);
                return;
            }
            const float cy = (float) y + (float) height * 0.5f;
            const juce::Rectangle<float> track ((float) x, cy - 2.0f, (float) width, 4.0f);
            g.setColour (juce::Colour (kTrack));
            g.fillRoundedRectangle (track, 2.0f);
            g.setColour (juce::Colour (kAccent).withAlpha (s.isEnabled() ? 1.0f : 0.35f));
            g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);

            const bool over = s.isMouseOverOrDragging();
            const float r = over ? 7.5f : 7.0f;
            const auto thumb = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ sliderPos, cy });
            juce::Path thumbPath;
            thumbPath.addEllipse (thumb);
            juce::DropShadow (juce::Colour (0x99000000), 5, { 0, 2 }).drawForPath (g, thumbPath);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a44), 0.0f, thumb.getY(),
                                                     juce::Colour (0xff24242b), 0.0f, thumb.getBottom(), false));
            g.fillPath (thumbPath);
            g.setColour (juce::Colour (over ? kEdgeStrong : kEdge));
            g.strokePath (thumbPath, juce::PathStrokeType (1.0f));
            g.setColour (juce::Colour (kText));
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (thumb.getCentre()));
            if (s.hasKeyboardFocus (false))
            {
                g.setColour (juce::Colour (kAccent).withAlpha (0.6f));
                g.drawEllipse (thumb.expanded (2.5f), 1.2f);
            }
        }

        int getSliderThumbRadius (juce::Slider&) override { return 8; }

        // ---- step arrows: icon-only, a cap appears on hover -----------------
        void drawDrawableButton (juce::Graphics& g, juce::DrawableButton& b, bool over, bool down) override
        {
            const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            if (b.isEnabled() && (over || down))
            {
                g.setColour (juce::Colour (down ? kControlDown : kControl));
                g.fillRoundedRectangle (r, 4.0f);
            }
            if (b.hasKeyboardFocus (false))
            {
                g.setColour (juce::Colour (kAccent).withAlpha (0.6f));
                g.drawRoundedRectangle (r, 4.0f, 1.0f);
            }
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LookAndFeel)
    };
}
