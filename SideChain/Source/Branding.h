/*
    SideChain - Music-Prod product branding: the SideChainer logo.

    An original vector lockup (no copied assets). Construction:

      - "Side" in primary white, heavy weight
      - an interlocking chain-link mark as the connector between the two
        halves: two small rounded-rectangle links drawn in mint, the second
        one offset through the first - the visual pun on "chain" and on the
        link between the sidechain trigger and the ducked main signal
      - "Chainer" in mint, heavy weight
      - a baseline hairline under the whole lockup with a solid mint
        end-cap (restrained accent line, Music-Prod.com design language)

    Everything is pure vector (paths + fills): crisp at any size, no blur.
    Used identically in the main header and the INFO page.

    Technical identifiers (SideChain / sidechain) are NOT affected.
*/

#pragma once

#include <JuceHeader.h>

namespace sid::branding
{
    // Shared palette (must match PluginEditor.cpp / InfoPage.cpp).
    constexpr uint32_t kTextPrimary = 0xffe8ecf3;
    constexpr uint32_t kAccent      = 0xff7fd1c0;   // Music-Prod mint

    // Draw the SideChainer logo left-aligned, vertically centred in `area`,
    // scaled by `fontHeight` (the cap height of the letterforms).
    inline void drawWordmark (juce::Graphics& g,
                              juce::Rectangle<float> area,
                              float fontHeight,
                              juce::Colour primaryColour = juce::Colour (kTextPrimary),
                              juce::Colour accentColour  = juce::Colour (kAccent))
    {
        const juce::String first  = "Side";
        const juce::String second = "Chainer";

        juce::Font heavy (juce::Font (juce::Font::getDefaultSansSerifFontName(),
                                      fontHeight, juce::Font::bold));
        heavy.setExtraKerningFactor (0.02f);
        juce::Font tight (heavy);
        tight.setExtraKerningFactor (-0.03f);

        const float w1 = heavy.getStringWidthFloat (first);

        // ---- chain-link connector geometry -------------------------------
        // Two rounded-rectangle links interlocked: link A vertical, link B
        // horizontal, overlapping in the middle. Sized off the x-height.
        const float linkH = fontHeight * 0.52f;
        const float linkW = linkH * 0.62f;
        const float gap   = fontHeight * 0.12f;
        const float centreY = area.getCentreY();

        const float x1 = area.getX();
        const float linkAx = x1 + w1 + gap;
        const float linkBx = linkAx + linkW * 0.55f;
        const float x2 = linkBx + linkW + gap;
        const float w2 = tight.getStringWidthFloat (second);
        const float totalW = w1 + gap + linkW * 1.55f + gap + w2;

        // Letterforms as paths (artwork, not labels).
        juce::Path glyphs;
        auto addRun = [&] (const juce::String& text, float x, const juce::Font& f)
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (f, text, x, area.getCentreY() + fontHeight * 0.38f);
            ga.createPath (glyphs);
        };
        addRun (first, x1, heavy);
        addRun (second, x2, tight);

        g.setColour (primaryColour);
        g.fillPath (glyphs);

        // Chain links (drawn as stroke outlines so they interlock visually).
        juce::Rectangle<float> linkA (linkAx, centreY - linkH * 0.5f,
                                      linkW, linkH);
        juce::Rectangle<float> linkB (linkBx, centreY - linkH * 0.24f,
                                      linkW, linkH * 0.48f);
        const float stroke = juce::jmax (1.4f, fontHeight * 0.075f);

        g.setColour (accentColour);
        g.drawRoundedRectangle (linkA, linkW * 0.5f, stroke);

        // Second link drawn "through" the first: fill the overlap region of
        // link B with the background-equivalent mask by drawing B's outline
        // on top (the shared dark field makes the interlock read).
        g.drawRoundedRectangle (linkB, linkH * 0.24f, stroke);

        // Baseline hairline + mint end-cap.
        const float ruleY   = area.getCentreY() + fontHeight * 0.62f;
        const float capSize = juce::jmax (2.0f, fontHeight * 0.09f);
        g.setColour (accentColour.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (area.getX(), ruleY, totalW, 1.0f));
        g.setColour (accentColour);
        g.fillRoundedRectangle (
            juce::Rectangle<float> (area.getX() + totalW - capSize,
                                    ruleY - (capSize - 1.0f) * 0.5f,
                                    capSize, capSize),
            capSize * 0.2f);
    }
}
