#include "EqCurvePanel.h"

#include "Theme.h"

#include <algorithm>
#include <cmath>

namespace earlab
{

namespace
{
const juce::Colour kPanel      = theme::surface;
const juce::Colour kGrid       = theme::border;
const juce::Colour kTextDim    = theme::text3;
const juce::Colour kAccent     = theme::accent;
const juce::Colour kNodeOff    = theme::borderInput;

juce::String zh (const char* text)
{
    return juce::String::fromUTF8 (text);
}

juce::Font uiFont (float height)
{
    return theme::uiFont (height);
}
} // namespace

EqCurvePanel::EqCurvePanel (AudioEngine& engineToUse)
    : engine (engineToUse)
{
    responseFrequencies.resize (180);

    for (size_t i = 0; i < responseFrequencies.size(); ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (responseFrequencies.size() - 1);
        responseFrequencies[i] = minFreq * std::pow (maxFreq / minFreq, t);
    }

    startTimerHz (10);
}

void EqCurvePanel::setEditable (bool shouldBeEditable)
{
    editable = shouldBeEditable;
    repaint();
}

juce::Rectangle<float> EqCurvePanel::getPlot() const
{
    // 左侧留 30 给 dB 刻度，底部留 14 给频率标签
    return getLocalBounds().toFloat().reduced (30.0f, 14.0f);
}

float EqCurvePanel::xForFrequency (float hz) const
{
    return std::log (juce::jlimit (minFreq, maxFreq, hz) / minFreq)
           / std::log (maxFreq / minFreq);
}

float EqCurvePanel::frequencyForX (float normalizedX) const
{
    return minFreq * std::pow (maxFreq / minFreq, juce::jlimit (0.0f, 1.0f, normalizedX));
}

float EqCurvePanel::yForDb (float db) const
{
    const float t = juce::jlimit (-maxDb, maxDb, db) / maxDb;
    return juce::jlimit (0.0f, 1.0f, 0.5f - t * 0.42f);
}

float EqCurvePanel::dbForY (float normalizedY) const
{
    const float t = (0.5f - juce::jlimit (0.0f, 1.0f, normalizedY)) / 0.42f;
    return juce::jlimit (-maxDb, maxDb, t * maxDb);
}

int EqCurvePanel::bandAt (juce::Point<float> point) const
{
    const auto plot = getPlot();
    const auto settings = engine.getEqSettings();
    int best = -1;
    float bestDistance = 18.0f;

    for (int band = 0; band < MultiBandEqProcessor::numBands; ++band)
    {
        const auto& b = settings.bands[band];
        const float x = plot.getX() + xForFrequency (b.frequency) * plot.getWidth();
        const float y = plot.getY() + yForDb (b.enabled ? b.gainDb : 0.0f) * plot.getHeight();
        const float distance = point.getDistanceFrom ({ x, y });

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = band;
        }
    }

    return best;
}

int EqCurvePanel::edgeAt (juce::Point<float> point) const
{
    const auto plot = getPlot();
    const auto settings = engine.getEqSettings();
    const float zeroY = plot.getY() + yForDb (0.0f) * plot.getHeight();

    const float hpX = plot.getX() + xForFrequency (settings.hpEnabled ? settings.hpHz : 20.0f) * plot.getWidth();
    const float lpX = plot.getX() + xForFrequency (settings.lpEnabled ? settings.lpHz : 20000.0f) * plot.getWidth();

    if (point.getDistanceFrom ({ hpX, zeroY - 9.0f }) < 13.0f)
        return 1;

    if (point.getDistanceFrom ({ lpX, zeroY - 9.0f }) < 13.0f)
        return -1;

    return 0;
}

void EqCurvePanel::timerCallback()
{
    // 手停下来 250 ms 后再重算等响：拖动过程中只改系数，不碰离线测量
    if (trimDirty && juce::Time::getMillisecondCounter() - lastEditMs >= 250)
    {
        trimDirty = false;
        engine.refreshLoudnessMatch();
    }

    // 载入/切换素材后刷新空态提示
    const bool hasMaterial = engine.hasMaterial();

    if (hasMaterial != lastHasMaterial)
    {
        lastHasMaterial = hasMaterial;
        repaint();
    }
}

void EqCurvePanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto plot = getPlot();
    const bool hasMaterial = engine.hasMaterial();

    lastHasMaterial = hasMaterial;

    g.setColour (kPanel);
    g.fillRoundedRectangle (bounds, 8.0f);

    // ---- 网格与坐标轴
    g.setFont (uiFont (11.0f));

    for (const auto hz : { 31.5f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f,
                           2000.0f, 4000.0f, 8000.0f, 16000.0f })
    {
        const float x = plot.getX() + xForFrequency (hz) * plot.getWidth();

        g.setColour (kGrid);
        g.drawVerticalLine (static_cast<int> (x), plot.getY(), plot.getBottom());

        const auto label = hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "k"
                                         : (hz < 100.0f ? juce::String (static_cast<double> (hz), 1)
                                                        : juce::String (juce::roundToInt (hz)));

        const int labelX = juce::jlimit (18, getWidth() - 18, static_cast<int> (x));

        g.setColour (kTextDim);
        g.drawText (label, labelX - 18, static_cast<int> (plot.getBottom()) + 1, 36, 13,
                    juce::Justification::centred);
    }

    // dB 刻度（左侧留白内）
    for (const auto db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f })
    {
        const float y = plot.getY() + yForDb (db) * plot.getHeight();

        g.setColour (db == 0.0f ? juce::Colour (0xff3a4453) : kGrid);
        g.drawHorizontalLine (static_cast<int> (y), plot.getX(), plot.getRight());

        const auto label = db > 0.0f ? "+" + juce::String (juce::roundToInt (db))
                                     : juce::String (juce::roundToInt (db));

        g.setColour (kTextDim);
        g.drawText (label, 2, static_cast<int> (y) - 7, 24, 13,
                    juce::Justification::centredRight);
    }

    g.setColour (kTextDim);
    g.drawText ("dB", 2, static_cast<int> (plot.getY()) - 14, 24, 12,
                juce::Justification::centredRight);
    g.drawText ("Hz", getWidth() - 26, static_cast<int> (plot.getBottom()) + 1, 24, 13,
                juce::Justification::centredRight);

    // ---- EQ 曲线
    const auto settings = engine.getEqSettings();
    const auto magnitude = engine.getEq().getMagnitudeDb (responseFrequencies, engine.getSampleRate());

    juce::Path curve;

    for (size_t i = 0; i < responseFrequencies.size() && i < magnitude.size(); ++i)
    {
        const float x = plot.getX() + xForFrequency (responseFrequencies[i]) * plot.getWidth();
        const float y = plot.getY() + yForDb (magnitude[i]) * plot.getHeight();

        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    g.setColour (kAccent);
    g.strokePath (curve, juce::PathStrokeType (2.2f));

    // ---- 节点
    for (int band = 0; band < MultiBandEqProcessor::numBands; ++band)
    {
        const auto& b = settings.bands[band];
        const float x = plot.getX() + xForFrequency (b.frequency) * plot.getWidth();
        const float y = plot.getY() + yForDb (b.enabled ? b.gainDb : 0.0f) * plot.getHeight();
        const float radius = b.enabled ? 5.0f : 3.5f;

        g.setColour (b.enabled ? kAccent : kNodeOff);
        g.fillEllipse (x - radius, y - radius, radius * 2.0f, radius * 2.0f);

        if (b.enabled)
        {
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.setFont (uiFont (9.0f));
            g.drawText (juce::String (band + 1), static_cast<int> (x) - 6, static_cast<int> (y) - 6,
                        12, 12, juce::Justification::centred);
        }
    }

    // ---- 高通/低通手柄
    {
        const float zeroY = plot.getY() + yForDb (0.0f) * plot.getHeight();

        const float hpX = plot.getX() + xForFrequency (settings.hpEnabled ? settings.hpHz : 20.0f) * plot.getWidth();
        const float lpX = plot.getX() + xForFrequency (settings.lpEnabled ? settings.lpHz : 20000.0f) * plot.getWidth();

        g.setColour (settings.hpEnabled ? kAccent : kNodeOff);
        juce::Path hp;
        hp.addTriangle (hpX, zeroY - 16.0f, hpX - 5.0f, zeroY - 5.0f, hpX + 5.0f, zeroY - 5.0f);
        g.fillPath (hp);

        g.setColour (settings.lpEnabled ? kAccent : kNodeOff);
        juce::Path lp;
        lp.addTriangle (lpX, zeroY - 16.0f, lpX - 5.0f, zeroY - 5.0f, lpX + 5.0f, zeroY - 5.0f);
        g.fillPath (lp);

        g.setFont (uiFont (9.0f));
        g.setColour (kTextDim);
        g.drawText ("HP", static_cast<int> (hpX) - 12, static_cast<int> (zeroY) - 30, 24, 12,
                    juce::Justification::centred);
        g.drawText ("LP", static_cast<int> (lpX) - 12, static_cast<int> (zeroY) - 30, 24, 12,
                    juce::Justification::centred);
    }

    if (! hasMaterial)
    {
        auto hint = plot.reduced (juce::jmax (20.0f, plot.getWidth() * 0.18f),
                                  juce::jmax (16.0f, plot.getHeight() * 0.30f));

        g.setColour (theme::surface.withAlpha (0.94f));
        g.fillRoundedRectangle (hint, 10.0f);

        g.setColour (theme::border);
        g.drawRoundedRectangle (hint, 10.0f, 1.0f);

        g.setColour (theme::text2);
        g.setFont (uiFont (13.0f));
        g.drawFittedText (zh ("载入素材或选择白噪 / 粉噪后，可拖动节点调整 EQ"),
                          hint.toNearestInt().reduced (16), juce::Justification::centred, 2);
    }

    if (! editable)
    {
        g.setColour (kTextDim.withAlpha (0.7f));
        g.setFont (uiFont (10.0f));
        g.drawText (zh ("训练中"), static_cast<int> (plot.getRight()) - 72,
                    static_cast<int> (plot.getBottom()) - 14, 70, 12,
                    juce::Justification::centredRight);
    }
}

void EqCurvePanel::mouseDown (const juce::MouseEvent& e)
{
    draggingBand = -1;
    draggingEdge = 0;

    if (! editable)
        return;

    draggingEdge = edgeAt (e.position);

    if (draggingEdge != 0)
        return;

    const auto plot = getPlot();

    if (! plot.contains (e.position))
        return;

    // 先看看是不是点在已有节点上
    draggingBand = bandAt (e.position);

    if (draggingBand >= 0)
        return;

    // 空白处按下：在点击位置新建一段，并立刻进入拖动
    auto settings = engine.getEqSettings();

    int slot = -1;

    for (int band = 0; band < MultiBandEqProcessor::numBands; ++band)
    {
        if (! settings.bands[band].enabled)
        {
            slot = band;
            break;
        }
    }

    if (slot < 0)   // 8 段用满
        return;

    const float nx = (e.position.x - plot.getX()) / juce::jmax (1.0f, plot.getWidth());
    const float ny = (e.position.y - plot.getY()) / juce::jmax (1.0f, plot.getHeight());

    auto& band = settings.bands[static_cast<size_t> (slot)];
    band.frequency = frequencyForX (nx);
    band.gainDb = dbForY (ny);
    band.q = 1.0f;
    band.enabled = true;

    engine.applyEqSettings (settings);

    draggingBand = slot;
    trimDirty = true;
    lastEditMs = juce::Time::getMillisecondCounter();
    repaint();
}

void EqCurvePanel::mouseDrag (const juce::MouseEvent& e)
{
    if (! editable)
        return;

    const auto plot = getPlot();
    const float normalizedX = (e.position.x - plot.getX()) / juce::jmax (1.0f, plot.getWidth());
    const float normalizedY = (e.position.y - plot.getY()) / juce::jmax (1.0f, plot.getHeight());

    if (draggingEdge != 0)
    {
        auto settings = engine.getEqSettings();
        const float hz = frequencyForX (normalizedX);

        if (draggingEdge > 0)
        {
            settings.hpEnabled = true;
            settings.hpHz = juce::jlimit (20.0f, 1000.0f, hz);
        }
        else
        {
            settings.lpEnabled = true;
            settings.lpHz = juce::jlimit (2000.0f, 20000.0f, hz);
        }

        engine.applyEqSettings (settings);
        trimDirty = true;
        lastEditMs = juce::Time::getMillisecondCounter();
        repaint();
        return;
    }

    if (draggingBand < 0)
        return;

    auto settings = engine.getEqSettings();
    auto& band = settings.bands[draggingBand];
    band.frequency = frequencyForX (normalizedX);
    band.gainDb = dbForY (normalizedY);
    band.enabled = true;

    engine.applyEqSettings (settings);
    trimDirty = true;
    lastEditMs = juce::Time::getMillisecondCounter();
    repaint();
}

void EqCurvePanel::mouseUp (const juce::MouseEvent&)
{
    draggingBand = -1;
    draggingEdge = 0;
}

void EqCurvePanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! editable)
        return;

    const int edge = edgeAt (e.position);

    if (edge != 0)
    {
        auto settings = engine.getEqSettings();

        if (edge > 0)
            settings.hpEnabled = ! settings.hpEnabled;
        else
            settings.lpEnabled = ! settings.lpEnabled;

        engine.applyEqSettings (settings);
        trimDirty = true;
        lastEditMs = juce::Time::getMillisecondCounter();
        repaint();
        return;
    }

    const int band = bandAt (e.position);

    if (band < 0)
        return;

    auto settings = engine.getEqSettings();
    settings.bands[band].enabled = ! settings.bands[band].enabled;

    if (settings.bands[band].enabled && std::abs (settings.bands[band].gainDb) < 0.5f)
        settings.bands[band].gainDb = 6.0f;

    engine.applyEqSettings (settings);
    trimDirty = true;
    lastEditMs = juce::Time::getMillisecondCounter();
    repaint();
}

void EqCurvePanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! editable)
        return;

    const int band = bandAt (e.position);

    if (band < 0)
        return;

    auto settings = engine.getEqSettings();
    auto& b = settings.bands[band];
    b.q = juce::jlimit (0.2f, 12.0f, b.q * (wheel.deltaY > 0.0f ? 1.12f : 0.89f));

    engine.applyEqSettings (settings);
    trimDirty = true;
    lastEditMs = juce::Time::getMillisecondCounter();
    repaint();
}

} // namespace earlab
