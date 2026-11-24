#include "AbxSession.h"

namespace earlab
{

void AbxSession::start (int numTrials, juce::Random& rng)
{
    trials         = juce::jmax (1, numTrials);
    random         = rng;
    index          = -1;
    correct        = 0;
    finished       = false;
    awaitingAnswer = false;
    xIsWet         = false;
    answers.clear();
}

bool AbxSession::nextTrial()
{
    if (finished)
        return false;

    if (awaitingAnswer)
        return true;

    if (index + 1 >= trials)
    {
        finished = true;
        return false;
    }

    ++index;
    xIsWet = random.nextBool();
    awaitingAnswer = true;
    return true;
}

void AbxSession::submitAnswer (bool answerSaysWet)
{
    if (! awaitingAnswer)
        return;

    answers.push_back (answerSaysWet);

    if (answerSaysWet == xIsWet)
        ++correct;

    awaitingAnswer = false;
}

} // namespace earlab
