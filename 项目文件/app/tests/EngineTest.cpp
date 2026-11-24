// A1 引擎测试：交叉淡化连续性、延迟补偿、等响匹配、ABX 状态机。

#include "engine/Assessment.h"
#include "dsp/LoudnessMeter.h"
#include "engine/AbxSession.h"
#include "engine/CompressorCalibration.h"
#include "engine/CompressorProcessor.h"
#include "engine/DualChain.h"
#include "engine/LoudnessMatcher.h"
#include "engine/MaterialPlayer.h"
#include "engine/MultiBandEq.h"
#include "engine/ProfileStore.h"
#include "engine/TrainableProcessor.h"
#include "engine/TrainingTask.h"
#include "ui/SessionRouting.h"
#include "ui/HistoryPagination.h"
#include "util/SingleInstanceLock.h"
#include "util/LatestSnapshot.h"
#include "util/AtomicFile.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

namespace
{
int failures = 0;

/** 测试临时目录：某些受限环境进程写不了 %TEMP%，写到可执行文件旁边最稳。 */
juce::File earlab_test_temp_dir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                   .getParentDirectory()
                   .getChildFile ("earlab-test-tmp");
    dir.createDirectory();
    return dir;
}

void check (const char* name, bool ok, const std::string& detail = {})
{
    std::printf ("[%s] %-36s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    if (! ok)
        ++failures;
}

/** 测试用延迟处理器：输出严格滞后 delay 个样本，用来验证 PDC。 */
class DelayProcessor final : public earlab::ITrainableProcessor
{
public:
    explicit DelayProcessor (int delaySamples) : delay (juce::jmax (1, delaySamples)) {}

    juce::String getName() const override { return "test delay"; }

    void prepare (double, int, int numChannels) override
    {
        buffer.setSize (numChannels, delay);
        buffer.clear();
        pos = 0;
    }

    void reset() override
    {
        buffer.clear();
        pos = 0;
    }

    void process (juce::AudioBuffer<float>& block) override
    {
        const int numSamples = block.getNumSamples();

        for (int ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto* data = block.getWritePointer (ch);
            auto* mem  = buffer.getWritePointer (ch);
            int p = pos;

            for (int i = 0; i < numSamples; ++i)
            {
                const float in = data[i];
                data[i] = mem[p];
                mem[p] = in;
                p = (p + 1) % delay;
            }
        }

        pos = (pos + (numSamples % delay)) % delay;
    }

    int getLatencySamples() const override { return delay; }

private:
    int delay;
    int pos = 0;
    juce::AudioBuffer<float> buffer;
};

/** P1-1 回归用：记录等响离线测量实际交给处理器的一次块长度。 */
class CountingProcessor final : public earlab::ITrainableProcessor
{
public:
    juce::String getName() const override { return "counting"; }
    void prepare (double, int, int) override {}
    void reset() override {}
    void process (juce::AudioBuffer<float>& block) override
    {
        lastBlockSamples = block.getNumSamples();
    }
    int getLatencySamples() const override { return 0; }

    int lastBlockSamples = 0;
};

void processConstant (earlab::DualChain& chain, float value, int blocks,
                      float& maxDelta, float& finalValue)
{
    float last = value;
    maxDelta = 0.0f;
    finalValue = value;

    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, 512);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (ch, i, value);

        chain.process (buffer);

        for (int i = 0; i < 512; ++i)
        {
            const float v = buffer.getSample (0, i);
            maxDelta = juce::jmax (maxDelta, std::abs (v - last));
            last = v;
        }
    }

    finalValue = last;
}

int findPeak (const juce::AudioBuffer<float>& buffer)
{
    int peak = -1;
    float peakValue = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float v = std::abs (buffer.getSample (0, i));

        if (v > peakValue)
        {
            peakValue = v;
            peak = i;
        }
    }

    return peak;
}
} // namespace

int main (int argc, char* argv[])
{
    // D-P0-2 跨进程锁测试的子进程模式：拿住锁、写 ready 文件、短暂停留后退出。
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg.startsWith ("--hold-instance-lock="))
        {
            const juce::File dataDir (arg.substring (arg.indexOfChar ('=') + 1).unquoted());
            juce::File readyFile;

            for (int j = 1; j < argc; ++j)
            {
                const juce::String other (argv[j]);

                if (other.startsWith ("--ready="))
                    readyFile = juce::File (other.substring (other.indexOfChar ('=') + 1).unquoted());
            }

            earlab::SingleInstanceLock guard (dataDir);
            const bool locked = guard.tryAcquire (0);

            if (readyFile != juce::File())
                readyFile.replaceWithText (locked ? "locked" : "failed");

            juce::Thread::sleep (800);
            return locked ? 0 : 1;
        }
    }

    std::printf ("EarLab engine tests\n------------------------------\n");

    // 1) 交叉淡化连续性
    {
        earlab::GainProcessor gain (6.0f);
        earlab::DualChain chain;
        chain.setProcessor (&gain);
        chain.prepare (48000.0, 512, 2);
        chain.setTrims (0.0f, 0.0f);
        chain.setSource (earlab::DualChain::Source::dry, true);

        float maxDelta = 0.0f, finalValue = 0.0f;
        processConstant (chain, 0.25f, 1, maxDelta, finalValue);

        chain.setSource (earlab::DualChain::Source::wet);
        processConstant (chain, 0.25f, 20, maxDelta, finalValue);

        char detail[128];
        std::snprintf (detail, sizeof (detail), "maxDelta=%.5f final=%.4f", maxDelta, finalValue);
        check ("交叉淡化无跳变", maxDelta < 0.01f, detail);
        check ("淡入后到达湿声电平", std::abs (finalValue - 0.5f) < 0.01f, detail);
    }

    // 2) 延迟补偿
    {
        DelayProcessor delay (64);
        earlab::DualChain chain;
        chain.setProcessor (&delay);
        chain.prepare (48000.0, 512, 2);
        chain.setTrims (0.0f, 0.0f);
        chain.setSource (earlab::DualChain::Source::wet, true);

        juce::AudioBuffer<float> wetBuffer (2, 512);
        wetBuffer.clear();
        wetBuffer.setSample (0, 0, 1.0f);
        wetBuffer.setSample (1, 0, 1.0f);
        chain.process (wetBuffer);
        const int wetPeak = findPeak (wetBuffer);

        DelayProcessor delayDry (64);
        earlab::DualChain dryChain;
        dryChain.setProcessor (&delayDry);
        dryChain.prepare (48000.0, 512, 2);
        dryChain.setTrims (0.0f, 0.0f);
        dryChain.setSource (earlab::DualChain::Source::dry, true);

        juce::AudioBuffer<float> dryBuffer (2, 512);
        dryBuffer.clear();
        dryBuffer.setSample (0, 0, 1.0f);
        dryBuffer.setSample (1, 0, 1.0f);
        dryChain.process (dryBuffer);
        const int dryPeak = findPeak (dryBuffer);

        char detail[128];
        std::snprintf (detail, sizeof (detail), "wetPeak=%d dryPeak=%d", wetPeak, dryPeak);
        check ("湿路延迟 64 样本", wetPeak == 64, detail);
        check ("干路补偿后与湿路对齐", dryPeak == wetPeak, detail);
    }

    // 3) 等响匹配
    {
        const int numSamples = 48000 * 5;
        juce::AudioBuffer<float> source (1, numSamples);
        const double amplitude = std::sqrt (2.0) * std::pow (10.0, -20.0 / 20.0);

        for (int i = 0; i < numSamples; ++i)
            source.setSample (0, i, static_cast<float> (
                amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 48000.0)));

        earlab::PeakEqProcessor eq;
        eq.prepare (48000.0, 512, 1);
        eq.setParameters (1000.0f, 12.0f, 1.0f);

        const auto match = earlab::computeLoudnessMatch (eq, source, 48000.0, -23.0f);
        check ("等响匹配计算有效", match.valid);

        juce::AudioBuffer<float> dry (1, numSamples);
        juce::AudioBuffer<float> wet (1, numSamples);
        dry.makeCopyOf (source);
        wet.makeCopyOf (source);

        eq.reset();
        eq.process (wet);
        eq.reset();

        dry.applyGain (juce::Decibels::decibelsToGain (match.dryTrimDb));
        wet.applyGain (juce::Decibels::decibelsToGain (match.wetTrimDb));

        const double dryLufs = earlab::dsp::LoudnessMeter::measureIntegrated (dry, 48000.0, 1);
        const double wetLufs = earlab::dsp::LoudnessMeter::measureIntegrated (wet, 48000.0, 1);

        char detail[160];
        std::snprintf (detail, sizeof (detail), "dry=%.2f wet=%.2f diff=%.3f",
                       dryLufs, wetLufs, std::abs (dryLufs - wetLufs));
        check ("干湿等响差 < 0.1 LU", std::abs (dryLufs - wetLufs) < 0.1, detail);
        check ("对齐到 -23 LUFS", std::abs (dryLufs + 23.0) < 0.2, detail);
    }

    // 3.5) P1-1 回归：等响测量窗口必须收紧到 10 s，避免把 12 s 素材整块交给插件。
    {
        const int totalSamples = 48000 * 12;
        juce::AudioBuffer<float> source (1, totalSamples);

        for (int i = 0; i < totalSamples; ++i)
            source.setSample (0, i, 0.1f * std::sin (2.0 * juce::MathConstants<double>::pi
                                                         * 1000.0 * i / 48000.0));

        CountingProcessor counter;
        counter.prepare (48000.0, 512, 1);

        const auto match = earlab::computeLoudnessMatch (counter, source, 48000.0);
        check ("P1-1 等响测量窗口 <= 10s",
               match.valid && counter.lastBlockSamples > 0
                   && counter.lastBlockSamples <= 48000 * 10,
               std::to_string (counter.lastBlockSamples));
    }

    // 4) 素材载入：44.1 kHz WAV 应被重采样到 48 kHz 且时长不变
    {
        const double sourceRate = 44100.0;
        const int sourceLength  = static_cast<int> (sourceRate);

        juce::AudioBuffer<float> source (1, sourceLength);
        for (int i = 0; i < sourceLength; ++i)
            source.setSample (0, i, static_cast<float> (
                0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sourceRate)));

        auto tempFile = earlab_test_temp_dir().getChildFile ("earlab_test_44100.wav");
        tempFile.deleteFile();

        {
            juce::WavAudioFormat format;
            std::unique_ptr<juce::FileOutputStream> stream (tempFile.createOutputStream());

            if (stream != nullptr)
            {
                std::unique_ptr<juce::AudioFormatWriter> writer (
                    format.createWriterFor (stream.get(), sourceRate, 1, 16, {}, 0));

                if (writer != nullptr)
                {
                    stream.release();   // writer 接管 stream
                    writer->writeFromAudioSampleBuffer (source, 0, sourceLength);
                }
            }
        }

        earlab::MaterialPlayer player;
        const bool loaded = player.load (tempFile, 48000.0);

        char detail[128];
        std::snprintf (detail, sizeof (detail), "duration=%.3fs", player.getDurationSeconds());

        check ("载入 44.1 kHz WAV", loaded, detail);
        check ("重采样到 48 kHz 且时长不变",
               loaded && std::abs (player.getDurationSeconds() - 1.0) < 0.01,
               detail);

        tempFile.deleteFile();
        check ("移走素材后仍能按新采样率播放缓存", player.reloadForSampleRate (44100.0)
               && player.getEngineSampleRate() == 44100.0
               && std::abs (player.getDurationSeconds() - 1.0) < 0.001
               && player.getBuffer().getRMSLevel (0, 0, 4410) > 0.1f);
    }

    // 4.5) 内置噪声源：白噪 / 粉噪
    {
        earlab::MaterialPlayer white;
        const bool whiteOk = white.startNoise (earlab::MaterialPlayer::NoiseType::white, 48000.0, 2.0);

        check ("白噪生成", whiteOk && white.isLoaded() && white.isNoise()
                             && std::abs (white.getDurationSeconds() - 2.0) < 0.01,
               std::to_string (white.getDurationSeconds()));

        const auto& whiteBuffer = white.getBuffer();
        double sumSquares = 0.0;

        for (int i = 0; i < whiteBuffer.getNumSamples(); ++i)
        {
            const double value = whiteBuffer.getSample (0, i);
            sumSquares += value * value;
        }

        const double rms = std::sqrt (sumSquares / juce::jmax (1, whiteBuffer.getNumSamples()));
        check ("白噪 RMS 归一", rms > 0.07 && rms < 0.14, std::to_string (rms));

        // 一阶低通把 500 Hz 以下分离出来，比较低/高频能量比
        const auto lowRatio = [] (const earlab::MaterialPlayer& source)
        {
            const auto& b = source.getBuffer();
            const double alpha = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 500.0 / 48000.0);
            double lp = 0.0, lowSum = 0.0, highSum = 0.0;

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double x = b.getSample (0, i);
                lp += alpha * (x - lp);
                lowSum += lp * lp;
                const double high = x - lp;
                highSum += high * high;
            }

            return highSum > 1.0e-12 ? lowSum / highSum : 0.0;
        };

        earlab::MaterialPlayer pink;
        pink.startNoise (earlab::MaterialPlayer::NoiseType::pink, 48000.0, 2.0);

        check ("粉噪低频更足", lowRatio (pink) > lowRatio (white) * 1.5,
               std::to_string (lowRatio (pink)) + " vs " + std::to_string (lowRatio (white)));

        earlab::MaterialPlayer againA, againB;
        againA.startNoise (earlab::MaterialPlayer::NoiseType::white, 48000.0, 0.1);
        againB.startNoise (earlab::MaterialPlayer::NoiseType::white, 48000.0, 0.1);

        bool identical = againA.getBuffer().getNumSamples() == againB.getBuffer().getNumSamples();

        for (int i = 0; i < againA.getBuffer().getNumSamples() && identical; ++i)
            if (againA.getBuffer().getSample (0, i) != againB.getBuffer().getSample (0, i))
                identical = false;

        check ("噪声可复现", identical, "");
    }

    // 5) 出题器、阶梯法、Elo
    {
        earlab::TaskGenerator genA (4242u);
        earlab::TaskGenerator genB (4242u);

        const auto taskA = genA.makeEqBandTask (6.0f, 1.0f);
        const auto taskB = genB.makeEqBandTask (6.0f, 1.0f);

        check ("同 seed 生成同题",
               taskA.frequencyHz == taskB.frequencyHz
                   && taskA.gainDb == taskB.gainDb
                   && taskA.correctIndex == taskB.correctIndex
                   && taskA.optionsHz == taskB.optionsHz,
               "freq=" + std::to_string (taskA.frequencyHz));

        bool uniqueOptions = true;
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
                if (taskA.optionsHz[static_cast<size_t> (i)] == taskA.optionsHz[static_cast<size_t> (j)])
                    uniqueOptions = false;

        check ("四个选项互不重复", uniqueOptions);
        check ("correctIndex 指向正确频率",
               taskA.optionsHz[static_cast<size_t> (taskA.correctIndex)] == taskA.frequencyHz);

        // P2-1 数字排版：EQ 频率选项按频率升序（保存 value+isCorrect 对后重算 correctIndex）
        {
            bool ascending = true;
            bool indexStillCorrect = true;

            for (int n = 0; n < 40; ++n)
            {
                const auto sortedTask = genA.makeEqBandTask (6.0f, 1.0f);

                for (int i = 1; i < 4; ++i)
                    if (sortedTask.optionsHz[static_cast<size_t> (i - 1)]
                        >= sortedTask.optionsHz[static_cast<size_t> (i)])
                        ascending = false;

                if (sortedTask.optionsHz[static_cast<size_t> (sortedTask.correctIndex)]
                    != sortedTask.frequencyHz)
                    indexStillCorrect = false;
            }

            check ("EQ 频率选项按频率升序", ascending);
            check ("排序后 correctIndex 仍指向正确频率", indexStillCorrect);
        }

        const auto easyTask = genA.makeEqBandTask (11.0f, 1.0f);
        const auto hardTask = genA.makeEqBandTask (2.0f, 1.0f);
        check ("增益越小难度越高", hardTask.difficulty > easyTask.difficulty);

        // 多段题：主段必须在选项里，背景段不能出现在选项里（避免歧义）
        {
            bool layoutOk = true;

            for (int i = 0; i < 40; ++i)
            {
                const auto multi = genA.makeEqBandTask (9.0f, 1.0f);

                if (multi.numBandMoves < 1 || multi.numBandMoves > 2)
                    layoutOk = false;

                if (multi.bandMoves[0].frequencyHz != multi.frequencyHz)
                    layoutOk = false;

                for (int band = 1; band < multi.numBandMoves; ++band)
                    for (const auto option : multi.optionsHz)
                        if (option == multi.bandMoves[static_cast<size_t> (band)].frequencyHz)
                            layoutOk = false;
            }

            check ("多段 EQ 题布局合法", layoutOk);
        }

        earlab::Staircase staircase (9.0f, 1.5f, 12.0f);
        for (int i = 0; i < 40; ++i)
            staircase.registerAnswer (i % 3 != 2);   // 两对一错，制造方向反转

        char detail[160];
        std::snprintf (detail, sizeof (detail), "current=%.2f threshold=%.2f reversals=%d",
                       staircase.getCurrentDb(), staircase.getThresholdDb(), staircase.getReversals());
        check ("阶梯法产生反转", staircase.getReversals() > 0, detail);
        check ("阈值落在设定范围内",
               staircase.getThresholdDb() >= 1.5f && staircase.getThresholdDb() <= 12.0f, detail);

        earlab::EloRating elo;
        for (int i = 0; i < 10; ++i)
            elo.update (true, 0.5f);

        const double afterCorrect = elo.getRating();

        for (int i = 0; i < 10; ++i)
            elo.update (false, 0.5f);

        const double afterWrong = elo.getRating();

        std::snprintf (detail, sizeof (detail), "correct=%.0f wrong=%.0f", afterCorrect, afterWrong);
        check ("答对提升 Elo", afterCorrect > 1000.0, detail);
        check ("答错降低 Elo", afterWrong < afterCorrect, detail);
    }

    // 6) 压缩器：稳态 GR、攻击时间、阈值校准
    {
        earlab::CompressorProcessor comp;
        comp.prepare (48000.0, 512, 1);

        earlab::CompressorProcessor::Params params;
        params.thresholdDb = -30.0f;
        params.ratio       = 4.0f;
        params.attackMs    = 5.0f;
        params.releaseMs   = 100.0f;
        params.kneeDb      = 0.0f;
        params.makeupDb    = 0.0f;

        const int numSamples = 48000 * 2;
        juce::AudioBuffer<float> sine (1, numSamples);
        const double peak = std::pow (10.0, -10.0 / 20.0);   // 峰值 -10 dBFS

        for (int i = 0; i < numSamples; ++i)
            sine.setSample (0, i, static_cast<float> (
                peak * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 48000.0)));

        comp.setParameters (params);
        comp.reset();
        comp.resetMetering();
        comp.process (sine);

        const float steadyGr = comp.getAverageGainReductionDb();

        char detail[160];
        std::snprintf (detail, sizeof (detail), "gr=%.2f expected=15", steadyGr);
        check ("压缩器稳态 GR 约 15 dB", std::abs (steadyGr - 15.0f) < 1.0f, detail);

        auto measureStepResponse = [&params] (float attackMs)
        {
            earlab::CompressorProcessor c;
            c.prepare (48000.0, 512, 1);

            auto p = params;
            p.attackMs = attackMs;
            c.setParameters (p);
            c.reset();

            juce::AudioBuffer<float> step (1, 480);   // 10 ms
            step.clear();

            for (int i = 0; i < step.getNumSamples(); ++i)
                step.setSample (0, i, 0.3162f);

            c.resetMetering();
            c.process (step);
            return c.getAverageGainReductionDb();
        };

        const float fastGr = measureStepResponse (1.0f);
        const float slowGr = measureStepResponse (60.0f);

        std::snprintf (detail, sizeof (detail), "fast=%.2f slow=%.2f", fastGr, slowGr);
        check ("快攻击比慢攻击更早压下", fastGr > slowGr * 2.0f, detail);

        // 校准：目标 6 dB GR，应找到约 -18 dB 的阈值
        earlab::CompressorProcessor calibrated;
        calibrated.prepare (48000.0, 512, 1);

        auto calParams = params;
        calParams.kneeDb = 0.0f;

        const float threshold = earlab::calibrateThresholdForTargetGr (
            calibrated, sine, 48000.0, calParams, 6.0f);

        juce::AudioBuffer<float> checkBuffer (1, numSamples);
        for (int i = 0; i < numSamples; ++i)
            checkBuffer.setSample (0, i, sine.getSample (0, i));

        calibrated.setParameters ([&]
        {
            auto p = calParams;
            p.thresholdDb = threshold;
            return p;
        }());
        calibrated.reset();
        calibrated.resetMetering();
        calibrated.process (checkBuffer);

        const float calibratedGr = calibrated.getAverageGainReductionDb();

        std::snprintf (detail, sizeof (detail), "threshold=%.1f gr=%.2f", threshold, calibratedGr);
        check ("阈值校准到 6 dB GR", std::abs (calibratedGr - 6.0f) < 1.0f, detail);
    }

    // 7) 压缩题生成
    {
        earlab::TaskGenerator genA (99u);
        earlab::TaskGenerator genB (99u);

        const auto c1 = genA.makeCompAmountTask (6.0f);
        const auto c2 = genB.makeCompAmountTask (6.0f);

        check ("压缩题同 seed 一致",
               c1.optionLabels == c2.optionLabels && c1.correctIndex == c2.correctIndex);

        bool unique = true;
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
                if (c1.optionLabels[static_cast<size_t> (i)] == c1.optionLabels[static_cast<size_t> (j)])
                    unique = false;

        check ("压缩题选项互不重复", unique);
        check ("压缩题 targetGr 在选项内",
               c1.optionLabels[static_cast<size_t> (c1.correctIndex)] == earlab::TaskGenerator::formatDbLabel (6.0f));
    }

    // 8) 档案持久化
    {
        auto path = earlab_test_temp_dir().getChildFile ("earlab_profile_test.json");
        path.deleteFile();

        earlab::ProfileStore store;

        earlab::SessionRecord first;
        first.skill       = earlab::Skill::eqBandCenter;
        first.timestamp   = 1;
        first.trials      = 10;
        first.correct     = 7;
        first.thresholdDb = 8.5f;
        first.elo         = 1040.0;
        store.addSession (first);

        earlab::SessionRecord second;
        second.skill       = earlab::Skill::compAmount;
        second.timestamp   = 2;
        second.trials      = 10;
        second.correct     = 5;
        second.thresholdDb = 6.0f;
        second.elo         = 980.0;
        store.addSession (second);

        check ("档案保存", store.save (path));

        earlab::ProfileStore loaded;
        check ("档案载入", loaded.load (path));

        const auto& eqStats = loaded.getStats (earlab::Skill::eqBandCenter);

        char detail[160];
        std::snprintf (detail, sizeof (detail), "sessions=%d elo=%.0f acc=%.2f",
                       eqStats.sessions, eqStats.elo, eqStats.getAccuracy());

        check ("会话数正确", loaded.getSessionCount() == 2, std::to_string (loaded.getSessionCount()));
        check ("EQ 汇总正确", eqStats.sessions == 1 && std::abs (eqStats.elo - 1040.0) < 0.5, detail);
        check ("准确率计算正确", std::abs (eqStats.getAccuracy() - 0.7) < 1.0e-6, detail);

        check ("首次 Elo 可查",
               std::abs (loaded.getFirstElo (earlab::Skill::eqBandCenter) - 1040.0) < 0.5,
               std::to_string (loaded.getFirstElo (earlab::Skill::eqBandCenter)));
        check ("无记录技能首次 Elo 为 0",
               loaded.getFirstElo (earlab::Skill::pluginBand) == 0.0);

        path.deleteFile();

        // CSV 导出（含插件技能行、UTF-8 头）
        {
            earlab::SessionRecord plugin;
            plugin.skill       = earlab::Skill::pluginBand;
            plugin.timestamp   = 3;
            plugin.trials      = 10;
            plugin.correct     = 9;
            plugin.thresholdDb = 2.0f;
            plugin.elo         = 1120.0;
            store.addSession (plugin);

            auto csvFile = earlab_test_temp_dir().getChildFile ("earlab-report.csv");
            csvFile.deleteFile();

            const bool saved = store.saveCsv (csvFile);
            const auto text = csvFile.loadFileAsString();

            check ("CSV 导出", saved && text.contains (juce::String::fromUTF8 ("插件辨别")),
                   std::to_string (static_cast<int> (text.length())));
            auto lines = juce::StringArray::fromLines (text);
            lines.removeEmptyStrings();

            check ("CSV 行数", lines.size() == 4, std::to_string (lines.size()));
            check ("CSV 数值", text.contains ("1120.0") && text.contains ("90.0"),
                   "");

            csvFile.deleteFile();

            auto htmlFile = earlab_test_temp_dir().getChildFile ("earlab-report.html");
            htmlFile.deleteFile();

            const bool htmlSaved = store.writeHtmlReport (htmlFile);
            const auto html = htmlFile.loadFileAsString();

            check ("HTML 报告", htmlSaved && html.contains ("<svg")
                                     && html.contains ("<table")
                                     && html.contains (juce::String::fromUTF8 ("插件辨别")),
                   std::to_string (static_cast<int> (html.length())));

            htmlFile.deleteFile();
        }
    }

    // 9) A/B 线性交叉淡化：相关信号切换途中不应出现 +3 dB 鼓包
    {
        earlab::GainProcessor unity (0.0f);
        earlab::DualChain chain;
        chain.setProcessor (&unity);
        chain.prepare (48000.0, 4800, 2);
        chain.setTrims (0.0f, 0.0f);
        chain.setSource (earlab::DualChain::Source::dry, true);

        juce::AudioBuffer<float> warm (2, 512);
        warm.clear();
        chain.process (warm);

        chain.setSource (earlab::DualChain::Source::wet);

        juce::AudioBuffer<float> fade (2, 4800);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < fade.getNumSamples(); ++i)
                fade.setSample (ch, i, 0.5f);

        chain.process (fade);

        const float peak = fade.getMagnitude (0, fade.getNumSamples());
        check ("A/B 切换无响度鼓包", peak <= 0.505f, std::to_string (peak));
    }

    // 10) setSingleBand 必须保留手动设置的高低切（旧版会清掉）
    {
        earlab::MultiBandEqProcessor eq;
        eq.prepare (48000.0, 512, 2);

        auto settings = eq.getSettings();
        settings.hpEnabled = true;
        settings.hpHz = 120.0f;
        eq.applySettings (settings);
        eq.setSingleBand (1000.0f, 6.0f, 1.0f);

        const auto after = eq.getSettings();
        check ("单段设置保留高低切",
               after.hpEnabled && std::abs (after.hpHz - 120.0f) < 0.01f
                   && after.bands[0].enabled,
               "");
    }

    // 10.5) P1-3 回归：压缩题必须关闭 HP/LP；校准用原始素材，
    //      只有 EQ 全通时校准缓冲才和实际湿链输入一致。
    {
        earlab::MultiBandEqProcessor eq;
        eq.prepare (48000.0, 512, 2);

        auto settings = eq.getSettings();
        settings.hpEnabled = true;
        settings.hpHz = 1000.0f;
        settings.lpEnabled = true;
        settings.lpHz = 2000.0f;
        settings.bands[0].frequency = 1000.0f;
        settings.bands[0].gainDb = 6.0f;
        settings.bands[0].q = 1.0f;
        settings.bands[0].enabled = true;
        eq.applySettings (settings);

        // 与 MainComponent 压缩题分支同序：单段复位（保留高低切）后显式关闭。
        eq.setSingleBand (1000.0f, 0.0f, 1.0f);
        eq.setHighLowPassEnabled (false);

        const auto after = eq.getSettings();
        const auto magnitudes = eq.getMagnitudeDb ({ 100.0f, 1000.0f, 10000.0f }, 48000.0);

        float worst = 0.0f;

        for (const float magnitude : magnitudes)
            worst = juce::jmax (worst, std::abs (magnitude));

        check ("压缩题关闭高低切",
               ! after.hpEnabled && ! after.lpEnabled && ! after.bands[0].enabled, "");
        check ("压缩题 EQ 全通（校准=湿链输入）", worst < 0.01f,
               std::to_string (worst));
    }

    // 11) 压缩题选项：12 dB 时不能出现 24 / 24.5 这种听不出差别的组合
    {
        earlab::TaskGenerator gen (777u);

        auto valuesOf = [] (const earlab::TrainingTask& task)
        {
            std::vector<float> values;

            for (const auto& label : task.optionLabels)
                values.push_back (label.retainCharacters ("0123456789.").getFloatValue());

            return values;
        };

        float worstGap = 100.0f;
        float maxValue = 0.0f;

        for (int i = 0; i < 200; ++i)
        {
            const auto task = gen.makeCompAmountTask (1.0f + static_cast<float> (i % 24));
            const auto values = valuesOf (task);

            for (size_t a = 0; a < values.size(); ++a)
            {
                maxValue = juce::jmax (maxValue, values[a]);

                for (size_t b = a + 1; b < values.size(); ++b)
                    worstGap = juce::jmin (worstGap, std::abs (values[a] - values[b]));
            }
        }

        check ("压缩题选项间距 >= 1 dB", worstGap >= 1.0f, std::to_string (worstGap));
        check ("压缩题选项不越界", maxValue <= 24.0f, std::to_string (maxValue));

        const auto task = gen.makeCompAmountTask (12.0f);
        const auto values = valuesOf (task);
        check ("压缩题正确答案=实测值",
               std::abs (values[static_cast<size_t> (task.correctIndex)] - 12.0f) < 0.01f,
               std::to_string (values[static_cast<size_t> (task.correctIndex)]));
    }

    // 12) 存档原子写：坏档不能清空内存历史，且必须留下 .bak
    {
        auto tmp = earlab_test_temp_dir().getChildFile ("earlab_profile_atomic.json");
        auto bak = tmp.getSiblingFile (tmp.getFileName() + ".bak");
        tmp.deleteFile();
        bak.deleteFile();

        earlab::SessionRecord record;
        record.skill = earlab::Skill::eqBandCenter;
        record.trials = 10;
        record.correct = 8;
        record.elo = 1010.0;
        record.thresholdDb = 6.0f;

        earlab::ProfileStore store;
        store.addSession (record);

        check ("存档写入成功", store.save (tmp) && tmp.existsAsFile(), "");
        check ("存档生成备份", store.save (tmp) && bak.existsAsFile(), "");

        tmp.replaceWithText ("{ 这不是合法 JSON");

        earlab::ProfileStore reloaded;
        reloaded.addSession (record);
        const bool loaded = reloaded.load (tmp);

        // P0-1：坏主档 + 有效 .bak → 从备份恢复（旧语义「不清空」由恢复保证）
        check ("坏档从 .bak 恢复且不清空历史",
               loaded && reloaded.getSessionCount() == 1
                   && reloaded.getLoadState()
                          == earlab::ProfileStore::LoadState::recoveredFromBackup,
               std::to_string (reloaded.getSessionCount()));

        tmp.deleteFile();
        bak.deleteFile();
    }

    // 13.5) 测评结果落盘
    {
        auto path = earlab_test_temp_dir().getChildFile ("earlab_assessments.json");
        path.deleteFile();

        earlab::AssessmentResult result;
        result.timestamp   = 1234;
        result.score       = 83;
        result.grade       = earlab::AssessmentStore::gradeForScore (83);
        result.eqCorrect   = 5;
        result.eqTotal     = 6;
        result.compCorrect = 4;
        result.compTotal   = 6;
        result.eqJndDb     = 3.0f;
        result.compJndDb   = 4.0f;

        earlab::AssessmentStore store;
        store.add (result);

        check ("测评结果保存", store.save (path));

        earlab::AssessmentStore loaded;
        const bool loadedOk = loaded.load (path);

        check ("测评结果载入",
               loadedOk && loaded.getCount() == 1 && loaded.getResults().front().score == 83,
               std::to_string (loaded.getCount()));

        check ("等级阈值",
               earlab::AssessmentStore::gradeForScore (90) == juce::String::fromUTF8 ("专业级")
                   && earlab::AssessmentStore::gradeForScore (60) == juce::String::fromUTF8 ("进阶级")
                   && earlab::AssessmentStore::gradeForScore (30) == juce::String::fromUTF8 ("入门级"),
               "");

        path.deleteFile();
    }

    // 13) ABX 状态机

    {
        juce::Random rng (12345);

        earlab::AbxSession perfect;
        perfect.start (20, rng);
        int trials = 0;

        while (perfect.nextTrial())
        {
            perfect.submitAnswer (perfect.isXWet());
            ++trials;

            if (trials > 100)
                break;
        }

        check ("ABX 完美作答 20/20",
               perfect.getCorrectCount() == 20 && trials == 20,
               std::to_string (perfect.getCorrectCount()));

        earlab::AbxSession allWrong;
        allWrong.start (20, rng);

        while (allWrong.nextTrial())
            allWrong.submitAnswer (! allWrong.isXWet());

        check ("ABX 全错 0/20",
               allWrong.getCorrectCount() == 0,
               std::to_string (allWrong.getCorrectCount()));
    }

    // 14) 设备重启（prepare 再次调用）不应清掉等响 trim
    {
        earlab::GainProcessor gain (0.0f);
        earlab::DualChain chain;
        chain.setProcessor (&gain);
        chain.prepare (48000.0, 512, 2);
        chain.setTrims (6.0f, -3.0f);

        // 模拟 remove/addAudioCallback 触发的 audioDeviceAboutToStart
        chain.prepare (48000.0, 512, 2);

        const float dry = juce::Decibels::gainToDecibels (chain.getDryGain());
        const float wet = juce::Decibels::gainToDecibels (chain.getWetGain());

        char detail[128];
        std::snprintf (detail, sizeof (detail), "dry=%.2f wet=%.2f", dry, wet);
        check ("设备重启后 trim 保留",
               std::abs (dry - 6.0f) < 0.01f && std::abs (wet + 3.0f) < 0.01f,
               detail);
    }

    // 15) 测评仓库可清空（换档案时不能串档）
    {
        earlab::AssessmentStore store;
        earlab::AssessmentResult result;
        result.timestamp = juce::Time::currentTimeMillis();
        result.score = 70;
        store.add (result);

        const bool added = store.getCount() == 1;
        store.clear();

        check ("测评仓库可清空", added && store.getCount() == 0,
               std::to_string (store.getCount()));
    }

    // 16) D-P0-1：主文件损坏但 .bak 有效 → load 必须从备份恢复；
    //     随后的 save 不得把损坏主文件复制成 .bak（旧记录不可恢复）。
    {
        const auto dir = earlab_test_temp_dir();
        auto mainFile = dir.getChildFile ("earlab_p0_assess.json");
        auto bakFile  = mainFile.getSiblingFile (mainFile.getFileName() + ".bak");
        mainFile.deleteFile();
        bakFile.deleteFile();

        auto makeResult = [] (juce::int64 time, int score)
        {
            earlab::AssessmentResult result;
            result.timestamp = time;
            result.score = score;
            result.grade = earlab::AssessmentStore::gradeForScore (score);
            result.eqCorrect = 5; result.eqTotal = 6;
            result.compCorrect = 4; result.compTotal = 6;
            result.eqJndDb = 3.0f; result.compJndDb = 4.0f;
            return result;
        };

        earlab::AssessmentStore seed;
        seed.add (makeResult (3000, 83));
        seed.add (makeResult (2000, 67));
        check ("P0-1 测评种子主文件", seed.save (mainFile) && mainFile.existsAsFile(), "");
        check ("P0-1 测评种子备份", mainFile.copyFileTo (bakFile), "");

        // 截断主文件，模拟异常退出/磁盘损坏
        mainFile.replaceWithText ("{\"version\":1,\"results\":[");

        earlab::AssessmentStore recovered;
        const bool recoveredOk = recovered.load (mainFile);
        check ("P0-1 测评坏主档从 .bak 恢复", recoveredOk && recovered.getCount() == 2,
               std::to_string (recovered.getCount()));
        check ("P0-1 测评恢复状态标记",
               recovered.getLoadState() == earlab::AssessmentStore::LoadState::recoveredFromBackup,
               std::to_string (static_cast<int> (recovered.getLoadState())));

        recovered.add (makeResult (4000, 100));
        check ("P0-1 测评恢复后保存", recovered.save (mainFile), "");
        check ("P0-1 保存成功后恢复状态复位",
               recovered.getLoadState() == earlab::AssessmentStore::LoadState::ok
                   && ! recovered.didRecoverFromBackup(),
               std::to_string (static_cast<int> (recovered.getLoadState())));

        earlab::AssessmentStore bakCheck;
        const bool bakOk = bakCheck.load (bakFile);
        check ("P0-1 测评保存不覆盖好 .bak", bakOk && bakCheck.getCount() == 2,
               std::to_string (bakCheck.getCount()));

        earlab::AssessmentStore mainCheck;
        const bool mainOk = mainCheck.load (mainFile);
        check ("P0-1 测评主文件可解析且记录不减",
               mainOk && mainCheck.getCount() == 3, std::to_string (mainCheck.getCount()));

        mainFile.deleteFile();
        bakFile.deleteFile();
    }

    {
        const auto dir = earlab_test_temp_dir();
        auto mainFile = dir.getChildFile ("earlab_p0_profile.json");
        auto bakFile  = mainFile.getSiblingFile (mainFile.getFileName() + ".bak");
        mainFile.deleteFile();
        bakFile.deleteFile();

        earlab::SessionRecord record;
        record.skill = earlab::Skill::eqBandCenter;
        record.trials = 10;
        record.correct = 8;
        record.thresholdDb = 4.0f;
        record.elo = 1010.0;

        earlab::ProfileStore seed;

        for (int i = 0; i < 6; ++i)
        {
            record.timestamp = 1000 + i;
            seed.addSession (record);
        }

        check ("P0-1 训练种子主文件", seed.save (mainFile) && mainFile.existsAsFile(), "");
        check ("P0-1 训练种子备份", mainFile.copyFileTo (bakFile), "");
        mainFile.replaceWithText ("{\"version\":1,\"sessions\":[");

        earlab::ProfileStore recovered;
        const bool recoveredOk = recovered.load (mainFile);
        check ("P0-1 训练坏主档从 .bak 恢复",
               recoveredOk && recovered.getSessionCount() == 6,
               std::to_string (recovered.getSessionCount()));
        check ("P0-1 训练恢复状态标记",
               recovered.getLoadState() == earlab::ProfileStore::LoadState::recoveredFromBackup,
               std::to_string (static_cast<int> (recovered.getLoadState())));

        record.timestamp = 2000;
        recovered.addSession (record);
        check ("P0-1 训练恢复后保存", recovered.save (mainFile), "");

        earlab::ProfileStore bakCheck;
        const bool bakOk = bakCheck.load (bakFile);
        check ("P0-1 训练保存不覆盖好 .bak",
               bakOk && bakCheck.getSessionCount() == 6,
               std::to_string (bakCheck.getSessionCount()));

        earlab::ProfileStore mainCheck;
        const bool mainOk = mainCheck.load (mainFile);
        check ("P0-1 训练主文件可解析且记录不减",
               mainOk && mainCheck.getSessionCount() == 7,
               std::to_string (mainCheck.getSessionCount()));

        mainFile.deleteFile();
        bakFile.deleteFile();
    }

    // 16.5) P1-5：坏档且没有可用 .bak 时 load 必须报 corrupt；保存不得把坏字节写成 .bak
    {
        const auto dir = earlab_test_temp_dir();
        auto mainFile = dir.getChildFile ("earlab_p1_corrupt_assess.json");
        auto bakFile  = mainFile.getSiblingFile (mainFile.getFileName() + ".bak");
        mainFile.deleteFile();
        bakFile.deleteFile();
        mainFile.replaceWithText ("{ broken json");

        earlab::AssessmentResult result;
        result.timestamp = 5000;
        result.score = 50;

        earlab::AssessmentStore store;
        const bool loadedOk = store.load (mainFile);
        check ("P1-5 坏档载入失败状态",
               ! loadedOk
                   && store.getLoadState() == earlab::AssessmentStore::LoadState::corrupted,
               std::to_string (static_cast<int> (store.getLoadState())));

        store.add (result);
        check ("P1-5 坏档后续保存成功", store.save (mainFile), "");
        check ("P1-5 不生成坏 .bak", ! bakFile.existsAsFile(), "");

        store.add (result);
        check ("P1-5 修复后恢复备份语义", store.save (mainFile) && bakFile.existsAsFile(), "");

        mainFile.deleteFile();
        bakFile.deleteFile();

        // 训练档案同样处理
        auto profileMain = dir.getChildFile ("earlab_p1_corrupt_profile.json");
        auto profileBak  = profileMain.getSiblingFile (profileMain.getFileName() + ".bak");
        profileMain.deleteFile();
        profileBak.deleteFile();
        profileMain.replaceWithText ("{ broken json");

        earlab::ProfileStore profile;
        const bool profileLoaded = profile.load (profileMain);
        check ("P1-5 训练坏档载入失败状态",
               ! profileLoaded
                   && profile.getLoadState() == earlab::ProfileStore::LoadState::corrupted,
               std::to_string (static_cast<int> (profile.getLoadState())));

        earlab::SessionRecord session;
        session.timestamp = 1;
        profile.addSession (session);
        check ("P1-5 训练坏档后续保存不写坏备份",
               profile.save (profileMain) && ! profileBak.existsAsFile(), "");

        profileMain.deleteFile();
        profileBak.deleteFile();
    }

    // 17) D-P1-1：测评文件必须按 profile 文件名派生，避免同目录多档串历史；
    //     新文件不存在时回退旧版同目录 assessments.json，新写入只写新文件。
    {
        const auto dir = earlab_test_temp_dir().getChildFile ("earlab_assess_profiles");
        dir.deleteRecursively();
        dir.createDirectory();

        const auto alicePath = earlab::AssessmentStore::defaultPathFor (
            dir.getChildFile ("alice-profile.json"));
        check ("P1-1 测评文件按 profile 派生",
               alicePath.getFileName() == "assessments-alice-profile.json",
               alicePath.getFileName().toStdString());

        const auto defaultProfilePath = earlab::AssessmentStore::defaultPathFor (
            dir.getChildFile ("profile.json"));
        check ("P1-1 profile.json 派生名",
               defaultProfilePath.getFileName() == "assessments-profile.json",
               defaultProfilePath.getFileName().toStdString());

        // 旧版同目录 assessments.json：新文件名不存在时必须仍可读
        const auto legacyPath = earlab::AssessmentStore::legacyPathFor (
            dir.getChildFile ("alice-profile.json"));

        earlab::AssessmentResult oldResult;
        oldResult.timestamp = 111;
        oldResult.score = 83;
        oldResult.grade = earlab::AssessmentStore::gradeForScore (83);

        earlab::AssessmentStore seed;
        seed.add (oldResult);
        seed.add (oldResult);
        check ("P1-1 旧版测试数据写入", seed.save (legacyPath), "");

        earlab::AssessmentStore fallback;
        const bool fallbackOk = fallback.loadForProfile (dir.getChildFile ("alice-profile.json"));
        check ("P1-1 新文件缺失时回退旧 assessments.json",
               fallbackOk && fallback.getCount() == 2 && fallback.didUseLegacyFile(),
               std::to_string (fallback.getCount()));

        fallback.add (oldResult);
        check ("P1-1 新写入成功", fallback.save (alicePath) && alicePath.existsAsFile(), "");

        earlab::AssessmentStore legacyCheck;
        const bool legacyStillOk = legacyCheck.load (legacyPath);
        check ("P1-1 旧文件不被改写",
               legacyStillOk && legacyCheck.getCount() == 2,
               std::to_string (legacyCheck.getCount()));

        earlab::AssessmentStore preferNew;
        const bool preferOk = preferNew.loadForProfile (dir.getChildFile ("alice-profile.json"));
        check ("P1-1 新文件存在时优先读新文件",
               preferOk && preferNew.getCount() == 3 && ! preferNew.didUseLegacyFile(),
               std::to_string (preferNew.getCount()));

        // 同目录另一个 profile：兼容期仍会读到旧 assessments.json；
        // 关键是不能把新成绩写回共享文件（隔离在写入侧）。
        earlab::AssessmentStore bob;
        const bool bobFallback = bob.loadForProfile (dir.getChildFile ("bob-profile.json"));
        check ("P1-1 第二档案兼容读取旧文件",
               bobFallback && bob.getCount() == 2 && bob.didUseLegacyFile(),
               std::to_string (bob.getCount()));

        bob.add (oldResult);
        const auto bobPath = earlab::AssessmentStore::defaultPathFor (
            dir.getChildFile ("bob-profile.json"));
        check ("P1-1 第二档案写入自己的派生文件",
               bobPath.getFileName() == "assessments-bob-profile.json"
                   && bob.save (bobPath) && bobPath.existsAsFile(),
               bobPath.getFileName().toStdString());

        earlab::AssessmentStore legacyAfterBob;
        legacyAfterBob.load (legacyPath);
        earlab::AssessmentStore aliceAfterBob;
        aliceAfterBob.loadForProfile (dir.getChildFile ("alice-profile.json"));
        check ("P1-1 第二档案不污染他人历史",
               legacyAfterBob.getCount() == 2 && aliceAfterBob.getCount() == 3,
               std::to_string (legacyAfterBob.getCount()) + "/"
                   + std::to_string (aliceAfterBob.getCount()));

        dir.deleteRecursively();
    }

    // 18) D-P1-4：导出失败必须返回 false（Main 会把它转成非零退出码）
    {
        const auto dir = earlab_test_temp_dir().getChildFile ("earlab_export_block");
        dir.deleteRecursively();
        dir.createDirectory();

        auto blocker = dir.getChildFile ("blocker");
        blocker.replaceWithText ("not a directory");
        const auto badCsv  = blocker.getChildFile ("out.csv");
        const auto badHtml = blocker.getChildFile ("out.html");

        earlab::ProfileStore store;
        earlab::SessionRecord record;
        record.timestamp = 1;
        store.addSession (record);

        check ("P1-4 CSV 导出失败返回 false", ! store.saveCsv (badCsv), "");
        check ("P1-4 HTML 导出失败返回 false", ! store.writeHtmlReport (badHtml), "");

        dir.deleteRecursively();
    }

    // 19) D-P0-2：跨进程单实例锁：第二个进程拿不到同一档案目录的锁
    {
        const auto dir = earlab_test_temp_dir().getChildFile ("earlab_instance_lock");
        dir.deleteRecursively();
        dir.createDirectory();

        auto readyFile = dir.getChildFile ("child-ready.txt");
        readyFile.deleteFile();

        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        juce::StringArray command;
        command.add (exe.getFullPathName());
        command.add ("--hold-instance-lock=" + dir.getFullPathName());
        command.add ("--ready=" + readyFile.getFullPathName());

        juce::ChildProcess child;
        const bool started = child.start (command, 0);

        for (int i = 0; i < 200 && ! readyFile.existsAsFile(); ++i)
            juce::Thread::sleep (50);

        const bool childReady = readyFile.existsAsFile();

        earlab::SingleInstanceLock second (dir);
        check ("P0-2 第二个进程拿不到同目录锁",
               started && childReady && ! second.tryAcquire (100), "");

        child.waitForProcessToFinish (10000);

        earlab::SingleInstanceLock after (dir);
        check ("P0-2 首进程退出后可再取得锁", after.tryAcquire (1000), "");

        readyFile.deleteFile();
        dir.deleteRecursively();
    }

    // 20) U-P1-1 / U-P1-3：会话归属与键盘守卫（纯路由逻辑）
    {
        using namespace earlab::ui;

        const auto training = sessionKindOf (true, false);
        const auto assessing = sessionKindOf (true, true);
        const auto idle = sessionKindOf (false, false);

        check ("U-P1-1 训练会话属于训练页",
               training == SessionKind::training
                   && sessionHostedOnPage (true, false, false, training, earlab::Skill::eqBandCenter)
                   && ! sessionHostedOnPage (false, true, false, training, earlab::Skill::eqBandCenter),
               "");

        check ("U-P1-1 插件训练属于 VST 页",
               sessionHostedOnPage (false, true, false, training, earlab::Skill::pluginBand)
                   && ! sessionHostedOnPage (true, false, false, training, earlab::Skill::pluginBand),
               "");

        check ("U-P1-1 测评只属于测评页（训练页不承载）",
               sessionHostedOnPage (false, false, true, assessing, earlab::Skill::eqBandCenter)
                   && ! sessionHostedOnPage (true, false, false, assessing, earlab::Skill::eqBandCenter)
                   && ! sessionHostedOnPage (false, true, false, assessing, earlab::Skill::eqBandCenter),
               "");

        check ("U-P1-1 训练页数字键不提交测评答案",
               ! canAcceptAnswerKey (true, false, false, assessing,
                                     earlab::Skill::eqBandCenter, false, false),
               "");

        check ("U-P1-1 测评页数字键可提交",
               canAcceptAnswerKey (false, false, true, assessing,
                                   earlab::Skill::eqBandCenter, false, false),
               "");

        check ("U-P1-1 已作答/小结时数字键不提交",
               ! canAcceptAnswerKey (false, false, true, assessing,
                                     earlab::Skill::eqBandCenter, true, false)
                   && ! canAcceptAnswerKey (false, false, true, assessing,
                                            earlab::Skill::eqBandCenter, false, true),
               "");

        check ("U-P1-1 无会话时数字键不提交",
               ! canAcceptAnswerKey (true, false, false, idle,
                                     earlab::Skill::eqBandCenter, false, false),
               "");

        // U-P1-3：完成训练后回首页（小结标志仍为 true、按钮已隐藏）按 Enter 不能开新训练
        check ("U-P1-3 首页 Enter 不触发隐藏小结",
               ! canActivateSummaryWithEnter (true, false, false, false, false,
                                              earlab::Skill::eqBandCenter)
                   && ! canActivateSummaryWithEnter (true, true, false, false, false,
                                                     earlab::Skill::eqBandCenter),
               "");

        check ("U-P1-3 训练页可见小结 Enter 可用",
               canActivateSummaryWithEnter (true, true, true, false, false,
                                            earlab::Skill::eqBandCenter),
               "");

        check ("U-P1-3 测评中 Enter 不触发训练小结",
               ! canActivateSummaryWithEnter (true, true, false, false, true,
                                              earlab::Skill::eqBandCenter),
               "");
    }

    {
        struct Frame { std::array<int, 64> samples {}; };
        earlab::LatestSnapshot<Frame> mailbox;
        Frame frame;
        frame.samples.fill (7);
        mailbox.publish (frame);
        const auto& held = mailbox.consume();
        for (int i = 0; i < 1000; ++i)
        {
            frame.samples.fill (i);
            mailbox.publish (frame);
        }
        check ("快照读者持有期间连续发布不覆盖", held.samples.front() == 7 && held.samples.back() == 7);
        check ("快照跳过旧更新并取得最新值", mailbox.consume().samples.front() == 999);

        std::atomic<bool> done { false };
        std::thread producer ([&]
        {
            Frame next;
            for (int i = 1; i <= 50000; ++i)
            {
                next.samples.fill (i);
                mailbox.publish (next);
            }
            done.store (true, std::memory_order_release);
        });
        bool consistent = true;
        do
        {
            const auto& next = mailbox.consume();
            for (const auto value : next.samples)
                consistent = consistent && value == next.samples.front();
        } while (! done.load (std::memory_order_acquire));
        producer.join();
        check ("快照并发 50000 次发布无撕裂", consistent && mailbox.consume().samples.front() == 50000);
    }

    {
        earlab::ui::HistoryPagination pages;
        pages.setRowsPerPage (6);
        pages.setItemCount (200);
        pages.goTo (33);
        check ("历史末页覆盖第 199 和 200 条记录",
               pages.pageCount() == 34 && pages.firstVisible() == 198 && pages.visibleCount() == 2);
        pages.move (1);
        check ("历史翻页不会越界", pages.currentPage() == 33);
        pages.setRowsPerPage (12);
        check ("历史缩放保留当前记录所在页", pages.firstVisible() == 192 && pages.visibleCount() == 8);
        pages.setItemCount (0);
        check ("历史清空重置页码", pages.currentPage() == 0 && pages.visibleCount() == 0);
    }

    {
        const auto dir = earlab_test_temp_dir().getChildFile ("refactor-storage");
        dir.createDirectory();
        const auto file = dir.getChildFile ("profile.json");
        const auto backup = file.getSiblingFile ("profile.json.bak");
        earlab::ProfileStore store;
        earlab::SessionRecord record;
        record.trials = 10;
        record.correct = 7;
        store.addSession (record);
        store.save (file);
        store.save (file);
        file.replaceWithText ("{}");
        earlab::ProfileStore loaded;
        check ("结构损坏的训练档案从备份恢复", loaded.load (file) && loaded.getSessionCount() == 1);
        file.replaceWithText (R"({"sessions":[{}]})");
        check ("缺失成绩字段的行不能取代好备份", loaded.load (file) && loaded.didRecoverFromBackup()
               && loaded.getHistory().front().correct == 7);
        file.deleteFile();
        check ("训练主文件缺失仍恢复备份", loaded.load (file) && loaded.getSessionCount() == 1);

        const auto assessmentFile = dir.getChildFile ("assessments-test.json");
        earlab::AssessmentStore assessment;
        earlab::AssessmentResult result;
        result.score = 75;
        assessment.add (result);
        assessment.save (assessmentFile);
        assessment.save (assessmentFile);
        assessmentFile.replaceWithText ("{\"results\":false}");
        check ("结构损坏的测评档案从备份恢复", assessment.load (assessmentFile) && assessment.getResults().size() == 1);
        assessmentFile.replaceWithText (R"({"results":[{}]})");
        check ("缺失测评分数时不生成零分记录", assessment.load (assessmentFile) && assessment.didRecoverFromBackup()
               && assessment.getResults().front().score == 75);
        assessmentFile.deleteFile();
        check ("测评主文件缺失仍恢复备份", assessment.load (assessmentFile) && assessment.getResults().size() == 1);

        const auto legacy = dir.getChildFile ("assessments.json");
        assessment.save (legacy);
        assessment.save (legacy);
        legacy.deleteFile();
        earlab::AssessmentStore migrated;
        check ("旧版仅剩备份时仍恢复测评", migrated.loadForProfile (file)
               && migrated.didUseLegacyFile() && migrated.didRecoverFromBackup() && migrated.getCount() == 1);
        const auto custom = dir.getChildFile ("custom.csv");
        store.save (custom);
        const auto original = custom.loadFileAsString();
        check ("扩展名合法也不能覆盖当前训练档案", ! store.exportReport (custom, custom)
               && custom.loadFileAsString() == original);
        check ("拒绝非报告文件扩展名", ! store.exportReport (assessmentFile, file));

        const auto target = dir.getChildFile ("atomic.txt");
        target.replaceWithText ("original");
        const auto blockedBackup = dir.getChildFile ("atomic.txt.bak");
        blockedBackup.createDirectory();
        blockedBackup.getChildFile ("keep").replaceWithText ("keep");
        check ("备份失败保留主档案并报告失败",
               ! earlab::writeTextAtomically (target, "replacement") && target.loadFileAsString() == "original");
        dir.deleteRecursively();
    }

    // Refactor regressions: exercise observable behaviour across device/material changes.
    {
        earlab::CompressorProcessor reused, fresh;
        earlab::CompressorProcessor::Params params;
        params.thresholdDb = -30.0f;
        params.ratio = 4.0f;
        reused.prepare (48000.0, 512, 1);
        reused.setParameters (params);
        reused.prepare (96000.0, 512, 1);
        fresh.prepare (96000.0, 512, 1);
        fresh.setParameters (params);
        juce::AudioBuffer<float> a (1, 512), b (1, 512);
        for (int i = 0; i < 512; ++i)
            a.setSample (0, i, 0.5f);
        b.makeCopyOf (a);
        reused.process (a);
        fresh.process (b);
        check ("采样率切换后压缩时间常数与新实例一致",
               std::abs (a.getSample (0, 511) - b.getSample (0, 511)) < 1.0e-6f);
    }
    {
        earlab::MaterialPlayer material;
        material.startNoise (earlab::MaterialPlayer::NoiseType::white, 48000.0, 0.001);
        material.setLooping (false);
        juce::AudioBuffer<float> output (2, 64);
        material.render (output, 64, 1.0f);
        check ("非循环素材结束后补静音", output.getMagnitude (0, 48, 16) == 0.0f);
        material.render (output, 64, 1.0f);
        check ("非循环素材结束后不重复末样本", output.getMagnitude (0, 64) == 0.0f);
    }
    {
        earlab::MultiBandEqProcessor highRate, lowRate;
        highRate.prepare (48000.0, 64, 1);
        lowRate.prepare (8000.0, 64, 1);
        highRate.setSingleBand (18000.0f, 12.0f, 1.0f);
        lowRate.setSingleBand (18000.0f, 12.0f, 1.0f);
        const auto a = highRate.getMagnitudeDb ({ 3000.0f }, 8000.0);
        const auto b = lowRate.getMagnitudeDb ({ 3000.0f }, 8000.0);
        check ("EQ 响应按请求采样率限制 Nyquist", std::abs (a[0] - b[0]) < 0.001f);

        auto settings = lowRate.getSettings();
        settings.bands[0].q = 0.0f;
        lowRate.applySettings (settings);
        juce::AudioBuffer<float> impulse (1, 64);
        impulse.clear();
        impulse.setSample (0, 0, 0.5f);
        lowRate.process (impulse);
        bool finite = true;
        for (int i = 0; i < 64; ++i)
            finite = finite && std::isfinite (impulse.getSample (0, i));
        check ("EQ 非法 Q 不产生非有限输出", finite);
    }

    std::printf ("------------------------------\n%s: %d 项失败\n",
                 failures == 0 ? "GREEN" : "RED", failures);
    return failures == 0 ? 0 : 1;
}
