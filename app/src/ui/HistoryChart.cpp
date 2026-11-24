#include "HistoryChart.h"
#include "Theme.h"

#include <algorithm>

namespace earlab
{

namespace
{
const juce::Colour kEqColour     (0xff4f8cff);
const juce::Colour kCompColour   (0xffe0a050);
const juce::Colour kPluginColour (0xff5fd0a8);
const juce::Colour kGrid       (0xff2a313c);
const juce::Colour kDim        (0xff8b93a1);

juce::String zh (const char* text)
{
    return juce::String::fromUTF8 (text);
}
} // namespace

void HistoryChart::setHistory (const std::vector<SessionRecord>& records)
{
    history = records;
    repaint();
}

void HistoryChart::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    if (history.empty())
    {
        auto centre = area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 40.0f, 320.0f), 104.0f);

        // 折线小图
        const float gx = centre.getX() + 40.0f;
        const float gy = centre.getY() + 26.0f;
        const float gw = centre.getWidth() - 80.0f;

        juce::Path glyph;
        glyph.startNewSubPath (gx, gy + 16.0f);
        glyph.cubicTo (gx + gw * 0.22f, gy - 2.0f, gx + gw * 0.32f, gy + 30.0f, gx + gw * 0.5f, gy + 12.0f);
        glyph.cubicTo (gx + gw * 0.68f, gy - 4.0f, gx + gw * 0.8f, gy + 6.0f, gx + gw, gy - 6.0f);

        g.setColour (kDim.withAlpha (0.45f));
        g.strokePath (glyph, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        for (const auto t : { 0.0f, 0.5f, 1.0f })
        {
            const auto point = glyph.getPointAlongPath (t * glyph.getLength());
            g.setColour (kDim.withAlpha (0.65f));
            g.fillEllipse (point.x - 2.5f, point.y - 2.5f, 5.0f, 5.0f);
        }

        g.setColour (kDim);
        g.setFont (theme::uiFont (13.0f));
        g.drawText (zh ("还没有训练记录"), centre.withTrimmedTop (58.0f).removeFromTop (22.0f),
                    juce::Justification::centred);

        g.setColour (kDim.withAlpha (0.7f));
        g.setFont (theme::uiFont (11.0f));
        g.drawText (zh ("完成一轮训练后，这里会出现 Elo 曲线"),
                    centre.withTrimmedTop (80.0f).removeFromTop (18.0f),
                    juce::Justification::centred);
        return;
    }

    std::array<std::vector<double>, 3> series;

    for (const auto& record : history)
    {
        const size_t index = record.skill == Skill::compAmount ? 1u
                           : record.skill == Skill::pluginBand ? 2u : 0u;
        series[index].push_back (record.elo);
    }

    for (auto& s : series)
        if (s.size() > 12)
            s.erase (s.begin(), s.begin() + static_cast<long> (s.size() - 12));

    double lo = 1000.0, hi = 1000.0;

    for (const auto& s : series)
        for (const double value : s)
        {
            lo = juce::jmin (lo, value);
            hi = juce::jmax (hi, value);
        }

    lo -= 30.0;
    hi += 30.0;

    if (hi - lo < 60.0)
        hi = lo + 60.0;

    auto plot = area.reduced (4.0f, 6.0f);
    plot.removeFromBottom (14.0f);

    const auto yFor = [&plot, lo, hi] (double elo)
    {
        return plot.getBottom() - static_cast<float> ((elo - lo) / (hi - lo)) * plot.getHeight();
    };

    g.setColour (kGrid);
    g.drawHorizontalLine (static_cast<int> (yFor (1000.0)), plot.getX(), plot.getRight());

    const auto drawSeries = [&] (const std::vector<double>& values, juce::Colour colour)
    {
        if (values.empty())
            return;

        const int count = static_cast<int> (values.size());
        juce::Path path;
        std::vector<juce::Point<float>> points;

        for (int i = 0; i < count; ++i)
        {
            const float x = count == 1
                              ? plot.getCentreX()
                              : plot.getX() + plot.getWidth() * static_cast<float> (i) / static_cast<float> (count - 1);
            const float y = yFor (values[static_cast<size_t> (i)]);

            points.emplace_back (x, y);

            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (2.0f));

        for (const auto& point : points)
            g.fillEllipse (point.x - 2.0f, point.y - 2.0f, 4.0f, 4.0f);
    };

    drawSeries (series[0], kEqColour);
    drawSeries (series[1], kCompColour);
    drawSeries (series[2], kPluginColour);

    g.setFont (theme::uiFont (11.0f));

    const float legendY = area.getBottom() - 12.0f;

    g.setColour (kEqColour);
    g.fillRect (area.getX(), legendY, 10.0f, 3.0f);
    g.setColour (kDim);
    g.drawText ("EQ", static_cast<int> (area.getX()) + 14, static_cast<int> (legendY) - 6,
                24, 14, juce::Justification::centredLeft);

    g.setColour (kCompColour);
    g.fillRect (area.getX() + 46.0f, legendY, 10.0f, 3.0f);
    g.setColour (kDim);
    g.drawText (zh ("压缩"), static_cast<int> (area.getX()) + 60, static_cast<int> (legendY) - 6,
                40, 14, juce::Justification::centredLeft);

    g.setColour (kPluginColour);
    g.fillRect (area.getX() + 104.0f, legendY, 10.0f, 3.0f);
    g.setColour (kDim);
    g.drawText (zh ("插件"), static_cast<int> (area.getX()) + 118, static_cast<int> (legendY) - 6,
                40, 14, juce::Justification::centredLeft);
}

} // namespace earlab
