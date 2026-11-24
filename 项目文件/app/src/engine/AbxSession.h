#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace earlab
{

/** ABX 试次的状态机，不含任何音频与界面逻辑，方便单独测试。 */
class AbxSession
{
public:
    void start (int numTrials, juce::Random& rng);

    /** 进入下一题；已全部完成时返回 false。 */
    bool nextTrial();

    void submitAnswer (bool answerSaysWet);

    [[nodiscard]] bool isXWet() const noexcept { return xIsWet; }
    [[nodiscard]] bool isFinished() const noexcept { return finished; }
    [[nodiscard]] bool isAwaitingAnswer() const noexcept { return awaitingAnswer; }

    [[nodiscard]] int getNumTrials() const noexcept { return trials; }
    [[nodiscard]] int getTrialNumber() const noexcept { return index + 1; }
    [[nodiscard]] int getAnsweredCount() const noexcept { return static_cast<int> (answers.size()); }
    [[nodiscard]] int getCorrectCount() const noexcept { return correct; }

private:
    int trials = 20;
    int index = -1;
    int correct = 0;
    bool xIsWet = false;
    bool finished = false;
    bool awaitingAnswer = false;

    juce::Random random;
    std::vector<bool> answers;
};

} // namespace earlab
