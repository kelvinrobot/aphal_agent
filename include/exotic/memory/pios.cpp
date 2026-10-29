// ============================================================================
// Partially Observable Stochastic Game (POSG): Hide and Seek in a Corridor
// ============================================================================
//
// Two agents share a corridor of four cells (cell 0 on the left, cell 3 on the
// right).
//
//   - The HIDER wants to avoid being found for as long as possible.
//   - The SEEKER wants to find the hider by searching the cell it stands in.
//
// What makes this a POSG:
//   1. Both agents act at the same time (simultaneous moves).
//   2. Movement is stochastic: a move succeeds 90 percent of the time and the
//      agent stays put the other 10 percent of the time.
//   3. Neither agent sees the true state. Each one only receives a noisy
//      "near" or "far" signal that tells it whether the other agent is within
//      one cell of it. That signal is correct 80 percent of the time.
//   4. The agents have opposing goals (the game is zero-sum-like in the
//      capture reward, so what one gains the other loses).
//
// Each agent keeps its own belief: a probability for every possible true state.
// After every step, each agent updates that belief using its own action and its
// own observation. To do this, each agent has to make an ASSUMPTION about how
// the other agent behaves. Here, both agents assume the opponent picks actions
// uniformly at random. This is the simplest possible assumption, and it is the
// part you would improve first in a more serious version.
//
// Build:  g++ -std=c++17 -O2 -o posg_hide_and_seek posg_hide_and_seek.cpp
// Run:    ./posg_hide_and_seek
// ============================================================================

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// Game size constants
// ----------------------------------------------------------------------------

const int NUMBER_OF_CELLS = 4;

// One normal state for every pairing of (hider cell, seeker cell).
const int NUMBER_OF_NORMAL_STATES = NUMBER_OF_CELLS * NUMBER_OF_CELLS;

// One extra state that means "the hider was caught and the game is over".
const int TERMINAL_STATE = NUMBER_OF_NORMAL_STATES;
const int NUMBER_OF_STATES = NUMBER_OF_NORMAL_STATES + 1;

// Hider actions: stay, move left, move right.
const int HIDER_STAY = 0;
const int HIDER_LEFT = 1;
const int HIDER_RIGHT = 2;
const int NUMBER_OF_HIDER_ACTIONS = 3;

// Seeker actions: move left, move right, search the current cell.
const int SEEKER_LEFT = 0;
const int SEEKER_RIGHT = 1;
const int SEEKER_SEARCH = 2;
const int NUMBER_OF_SEEKER_ACTIONS = 3;

// Observations (the same two possible signals for both agents).
const int OBSERVATION_FAR = 0;
const int OBSERVATION_NEAR = 1;
const int NUMBER_OF_OBSERVATIONS = 2;

// Agent identifiers, used to index into per-agent tables.
const int HIDER_AGENT = 0;
const int SEEKER_AGENT = 1;
const int NUMBER_OF_AGENTS = 2;

// ----------------------------------------------------------------------------
// Probabilities and rewards
// ----------------------------------------------------------------------------

const double MOVE_SUCCESS_PROBABILITY = 0.9;
const double OBSERVATION_CORRECT_PROBABILITY = 0.8;

const double CAPTURE_REWARD_FOR_SEEKER = 10.0;
const double CAPTURE_PENALTY_FOR_HIDER = -10.0;
const double SURVIVAL_REWARD_FOR_HIDER = 1.0;
const double SEARCH_COST_FOR_SEEKER = -1.0;

// How much each agent randomizes its choice. A higher number means more random.
const double SOFTMAX_TEMPERATURE = 1.0;

const int MAXIMUM_STEPS_PER_EPISODE = 20;

// ----------------------------------------------------------------------------
// Type names so the long nested tables are easier to read
// ----------------------------------------------------------------------------

// transition[state][hiderAction][seekerAction][nextState] = probability
typedef std::vector<std::vector<std::vector<std::vector<double> > > > TransitionTable;

// reward[state][hiderAction][seekerAction] = expected reward for one agent
typedef std::vector<std::vector<std::vector<double> > > RewardTable;

// observation[agent][nextState][observation] = probability
typedef std::vector<std::vector<std::vector<double> > > ObservationTable;

// ----------------------------------------------------------------------------
// Helper functions for turning two cells into one state number and back
// ----------------------------------------------------------------------------

int encodeState(int hiderCell, int seekerCell) {
    return hiderCell * NUMBER_OF_CELLS + seekerCell;
}

int decodeHiderCell(int state) {
    return state / NUMBER_OF_CELLS;
}

int decodeSeekerCell(int state) {
    return state % NUMBER_OF_CELLS;
}

// Moves a cell by a step of -1, 0, or +1 and keeps it inside the corridor.
int moveCell(int cell, int step) {
    int newCell = cell + step;
    if (newCell < 0) {
        newCell = 0;
    }
    if (newCell > NUMBER_OF_CELLS - 1) {
        newCell = NUMBER_OF_CELLS - 1;
    }
    return newCell;
}

int hiderActionToStep(int hiderAction) {
    if (hiderAction == HIDER_LEFT) {
        return -1;
    }
    if (hiderAction == HIDER_RIGHT) {
        return 1;
    }
    return 0;
}

int seekerActionToStep(int seekerAction) {
    if (seekerAction == SEEKER_LEFT) {
        return -1;
    }
    if (seekerAction == SEEKER_RIGHT) {
        return 1;
    }
    return 0;
}

std::string hiderActionName(int hiderAction) {
    if (hiderAction == HIDER_STAY) {
        return "stay";
    }
    if (hiderAction == HIDER_LEFT) {
        return "left";
    }
    return "right";
}

std::string seekerActionName(int seekerAction) {
    if (seekerAction == SEEKER_LEFT) {
        return "left";
    }
    if (seekerAction == SEEKER_RIGHT) {
        return "right";
    }
    return "search";
}

std::string observationName(int observation) {
    if (observation == OBSERVATION_NEAR) {
        return "near";
    }
    return "far";
}

// ----------------------------------------------------------------------------
// A small record describing one possible result of a move attempt
// ----------------------------------------------------------------------------

struct CellOutcome {
    int cell;
    double probability;
};

// Lists where an agent could end up after trying to move, and how likely each
// result is. If the agent is not trying to move (step of zero), it stays put
// with certainty. Otherwise it either moves (90 percent) or slips and stays
// where it was (10 percent).
std::vector<CellOutcome> listMoveOutcomes(int startCell, int step) {
    std::vector<CellOutcome> outcomes;

    if (step == 0) {
        CellOutcome stayOutcome;
        stayOutcome.cell = startCell;
        stayOutcome.probability = 1.0;
        outcomes.push_back(stayOutcome);
        return outcomes;
    }

    CellOutcome successOutcome;
    successOutcome.cell = moveCell(startCell, step);
    successOutcome.probability = MOVE_SUCCESS_PROBABILITY;
    outcomes.push_back(successOutcome);

    CellOutcome slipOutcome;
    slipOutcome.cell = startCell;
    slipOutcome.probability = 1.0 - MOVE_SUCCESS_PROBABILITY;
    outcomes.push_back(slipOutcome);

    return outcomes;
}

// ----------------------------------------------------------------------------
// The complete game definition
// ----------------------------------------------------------------------------

struct PosgModel {
    TransitionTable transition;
    RewardTable hiderExpectedReward;
    RewardTable seekerExpectedReward;
    ObservationTable observation;
};

PosgModel buildModel() {
    PosgModel model;

    // ---- Allocate the transition table, filled with zeros ----
    model.transition.resize(NUMBER_OF_STATES);
    for (int state = 0; state < NUMBER_OF_STATES; state++) {
        model.transition[state].resize(NUMBER_OF_HIDER_ACTIONS);
        for (int hiderAction = 0; hiderAction < NUMBER_OF_HIDER_ACTIONS; hiderAction++) {
            model.transition[state][hiderAction].resize(NUMBER_OF_SEEKER_ACTIONS);
            for (int seekerAction = 0; seekerAction < NUMBER_OF_SEEKER_ACTIONS; seekerAction++) {
                model.transition[state][hiderAction][seekerAction].assign(NUMBER_OF_STATES, 0.0);
            }
        }
    }

    // ---- Allocate the two reward tables, filled with zeros ----
    model.hiderExpectedReward.resize(NUMBER_OF_STATES);
    model.seekerExpectedReward.resize(NUMBER_OF_STATES);
    for (int state = 0; state < NUMBER_OF_STATES; state++) {
        model.hiderExpectedReward[state].resize(NUMBER_OF_HIDER_ACTIONS);
        model.seekerExpectedReward[state].resize(NUMBER_OF_HIDER_ACTIONS);
        for (int hiderAction = 0; hiderAction < NUMBER_OF_HIDER_ACTIONS; hiderAction++) {
            model.hiderExpectedReward[state][hiderAction].assign(NUMBER_OF_SEEKER_ACTIONS, 0.0);
            model.seekerExpectedReward[state][hiderAction].assign(NUMBER_OF_SEEKER_ACTIONS, 0.0);
        }
    }

    // ---- Fill in transitions and rewards for every normal state ----
    for (int state = 0; state < NUMBER_OF_NORMAL_STATES; state++) {
        int hiderCell = decodeHiderCell(state);
        int seekerCell = decodeSeekerCell(state);

        for (int hiderAction = 0; hiderAction < NUMBER_OF_HIDER_ACTIONS; hiderAction++) {
            for (int seekerAction = 0; seekerAction < NUMBER_OF_SEEKER_ACTIONS; seekerAction++) {

                std::vector<CellOutcome> hiderOutcomes =
                    listMoveOutcomes(hiderCell, hiderActionToStep(hiderAction));
                std::vector<CellOutcome> seekerOutcomes =
                    listMoveOutcomes(seekerCell, seekerActionToStep(seekerAction));

                bool seekerIsSearching = (seekerAction == SEEKER_SEARCH);

                // Go through every combination of hider result and seeker result.
                for (size_t h = 0; h < hiderOutcomes.size(); h++) {
                    for (size_t s = 0; s < seekerOutcomes.size(); s++) {

                        double jointProbability =
                            hiderOutcomes[h].probability * seekerOutcomes[s].probability;
                        int newHiderCell = hiderOutcomes[h].cell;
                        int newSeekerCell = seekerOutcomes[s].cell;

                        // The hider is caught if the seeker searches and the hider
                        // ends up in the same cell after movement.
                        bool hiderWasCaught = seekerIsSearching && (newHiderCell == newSeekerCell);

                        int nextState;
                        double hiderReward;
                        double seekerReward;

                        if (hiderWasCaught) {
                            nextState = TERMINAL_STATE;
                            hiderReward = CAPTURE_PENALTY_FOR_HIDER;
                            seekerReward = CAPTURE_REWARD_FOR_SEEKER;
                        } else {
                            nextState = encodeState(newHiderCell, newSeekerCell);
                            hiderReward = SURVIVAL_REWARD_FOR_HIDER;
                            seekerReward = SEARCH_COST_FOR_SEEKER;
                        }

                        model.transition[state][hiderAction][seekerAction][nextState] += jointProbability;
                        model.hiderExpectedReward[state][hiderAction][seekerAction] += jointProbability * hiderReward;
                        model.seekerExpectedReward[state][hiderAction][seekerAction] += jointProbability * seekerReward;
                    }
                }
            }
        }
    }

    // ---- The terminal state loops back to itself and pays nothing ----
    for (int hiderAction = 0; hiderAction < NUMBER_OF_HIDER_ACTIONS; hiderAction++) {
        for (int seekerAction = 0; seekerAction < NUMBER_OF_SEEKER_ACTIONS; seekerAction++) {
            model.transition[TERMINAL_STATE][hiderAction][seekerAction][TERMINAL_STATE] = 1.0;
        }
    }

    // ---- Observation table ----
    // Each agent is told "near" if the two agents are within one cell of each
    // other, and "far" otherwise. The signal is correct 80 percent of the time.
    model.observation.resize(NUMBER_OF_AGENTS);
    for (int agent = 0; agent < NUMBER_OF_AGENTS; agent++) {
        model.observation[agent].resize(NUMBER_OF_STATES);
        for (int state = 0; state < NUMBER_OF_STATES; state++) {
            model.observation[agent][state].assign(NUMBER_OF_OBSERVATIONS, 0.0);

            if (state == TERMINAL_STATE) {
                // Once the game is over, both agents just see "far".
                model.observation[agent][state][OBSERVATION_FAR] = 1.0;
                continue;
            }

            int hiderCell = decodeHiderCell(state);
            int seekerCell = decodeSeekerCell(state);
            int distance = std::abs(hiderCell - seekerCell);
            bool trulyNear = (distance <= 1);

            if (trulyNear) {
                model.observation[agent][state][OBSERVATION_NEAR] = OBSERVATION_CORRECT_PROBABILITY;
                model.observation[agent][state][OBSERVATION_FAR] = 1.0 - OBSERVATION_CORRECT_PROBABILITY;
            } else {
                model.observation[agent][state][OBSERVATION_FAR] = OBSERVATION_CORRECT_PROBABILITY;
                model.observation[agent][state][OBSERVATION_NEAR] = 1.0 - OBSERVATION_CORRECT_PROBABILITY;
            }
        }
    }

    return model;
}

// ----------------------------------------------------------------------------
// Beliefs
// ----------------------------------------------------------------------------

// A belief is just a list of probabilities, one for each state.
typedef std::vector<double> Belief;

// Spreads probability evenly over all normal states and gives the terminal
// state zero. This is what an agent believes before it has learned anything.
Belief makeUniformBelief() {
    Belief belief(NUMBER_OF_STATES, 0.0);
    for (int state = 0; state < NUMBER_OF_NORMAL_STATES; state++) {
        belief[state] = 1.0 / NUMBER_OF_NORMAL_STATES;
    }
    return belief;
}

// Updates one agent's belief after it took an action and saw an observation.
//
// In plain English, for every possible new state we compute:
//   how likely this agent was to see what it saw in that new state,
//   multiplied by how likely we are to land in that new state, considering
//   every old state we might have been in and every action the opponent
//   might have taken (assumed to be uniformly random).
// Then we scale all the numbers so they add up to one.
//
// Both agents are told whether the game has ended (a capture is not a secret),
// so when the game is still going, the "game over" state gets zero weight.
Belief updateBelief(const PosgModel& model,
                    int agent,
                    const Belief& oldBelief,
                    int ownAction,
                    int observationSeen,
                    bool gameIsStillGoing) {

    int numberOfOpponentActions;
    if (agent == HIDER_AGENT) {
        numberOfOpponentActions = NUMBER_OF_SEEKER_ACTIONS;
    } else {
        numberOfOpponentActions = NUMBER_OF_HIDER_ACTIONS;
    }
    double chanceOfEachOpponentAction = 1.0 / numberOfOpponentActions;

    Belief newBelief(NUMBER_OF_STATES, 0.0);
    double totalWeight = 0.0;

    for (int nextState = 0; nextState < NUMBER_OF_STATES; nextState++) {

        // The game is known to still be going, so the game-over state is ruled out.
        if (gameIsStillGoing && nextState == TERMINAL_STATE) {
            continue;
        }

        // Chance of landing in this next state, averaged over old states and
        // opponent actions.
        double chanceOfReachingNextState = 0.0;

        for (int oldState = 0; oldState < NUMBER_OF_STATES; oldState++) {
            if (oldBelief[oldState] == 0.0) {
                continue;
            }
            for (int opponentAction = 0; opponentAction < numberOfOpponentActions; opponentAction++) {
                double transitionChance;
                if (agent == HIDER_AGENT) {
                    transitionChance = model.transition[oldState][ownAction][opponentAction][nextState];
                } else {
                    transitionChance = model.transition[oldState][opponentAction][ownAction][nextState];
                }
                chanceOfReachingNextState +=
                    oldBelief[oldState] * chanceOfEachOpponentAction * transitionChance;
            }
        }

        double chanceOfSeeingObservation = model.observation[agent][nextState][observationSeen];
        double weight = chanceOfReachingNextState * chanceOfSeeingObservation;

        newBelief[nextState] = weight;
        totalWeight += weight;
    }

    // If something impossible was observed under this agent's assumptions, fall
    // back to knowing nothing instead of dividing by zero.
    if (totalWeight <= 0.0) {
        return makeUniformBelief();
    }

    for (int state = 0; state < NUMBER_OF_STATES; state++) {
        newBelief[state] /= totalWeight;
    }
    return newBelief;
}

// Adds up belief over all states where the hider is in a given cell.
double beliefHiderIsInCell(const Belief& belief, int cell) {
    double total = 0.0;
    for (int state = 0; state < NUMBER_OF_NORMAL_STATES; state++) {
        if (decodeHiderCell(state) == cell) {
            total += belief[state];
        }
    }
    return total;
}

// Adds up belief over all states where the seeker is in a given cell.
double beliefSeekerIsInCell(const Belief& belief, int cell) {
    double total = 0.0;
    for (int state = 0; state < NUMBER_OF_NORMAL_STATES; state++) {
        if (decodeSeekerCell(state) == cell) {
            total += belief[state];
        }
    }
    return total;
}

// ----------------------------------------------------------------------------
// Choosing actions
// ----------------------------------------------------------------------------

// Average reward the hider expects right now for one action, given what it
// believes and assuming the seeker picks uniformly at random.
double hiderScoreForAction(const PosgModel& model, const Belief& belief, int hiderAction) {
    double score = 0.0;
    for (int state = 0; state < NUMBER_OF_NORMAL_STATES; state++) {
        for (int seekerAction = 0; seekerAction < NUMBER_OF_SEEKER_ACTIONS; seekerAction++) {
            score += belief[state]
                     * (1.0 / NUMBER_OF_SEEKER_ACTIONS)
                     * model.hiderExpectedReward[state][hiderAction][seekerAction];
        }
    }
    return score;
}

// Average reward the seeker expects right now for one action, given what it
// believes and assuming the hider picks uniformly at random.
double seekerScoreForAction(const PosgModel& model, const Belief& belief, int seekerAction) {
    double score = 0.0;
    for (int state = 0; state < NUMBER_OF_NORMAL_STATES; state++) {
        for (int hiderAction = 0; hiderAction < NUMBER_OF_HIDER_ACTIONS; hiderAction++) {
            score += belief[state]
                     * (1.0 / NUMBER_OF_HIDER_ACTIONS)
                     * model.seekerExpectedReward[state][hiderAction][seekerAction];
        }
    }
    return score;
}

// Picks an action at random, but favors actions with higher scores. Actions
// with equal scores are equally likely. This is called a softmax choice.
int pickActionBySoftmax(const std::vector<double>& scores, std::mt19937& randomGenerator) {
    double bestScore = scores[0];
    for (size_t i = 1; i < scores.size(); i++) {
        if (scores[i] > bestScore) {
            bestScore = scores[i];
        }
    }

    std::vector<double> weights(scores.size(), 0.0);
    for (size_t i = 0; i < scores.size(); i++) {
        weights[i] = std::exp((scores[i] - bestScore) / SOFTMAX_TEMPERATURE);
    }

    std::discrete_distribution<int> chooser(weights.begin(), weights.end());
    return chooser(randomGenerator);
}

int chooseHiderAction(const PosgModel& model, const Belief& belief, std::mt19937& randomGenerator) {
    std::vector<double> scores(NUMBER_OF_HIDER_ACTIONS, 0.0);
    for (int action = 0; action < NUMBER_OF_HIDER_ACTIONS; action++) {
        scores[action] = hiderScoreForAction(model, belief, action);
    }
    return pickActionBySoftmax(scores, randomGenerator);
}

int chooseSeekerAction(const PosgModel& model, const Belief& belief, std::mt19937& randomGenerator) {
    std::vector<double> scores(NUMBER_OF_SEEKER_ACTIONS, 0.0);
    for (int action = 0; action < NUMBER_OF_SEEKER_ACTIONS; action++) {
        scores[action] = seekerScoreForAction(model, belief, action);
    }
    return pickActionBySoftmax(scores, randomGenerator);
}

// ----------------------------------------------------------------------------
// Printing helpers
// ----------------------------------------------------------------------------

void printBeliefLine(const std::string& label, const Belief& belief, bool aboutHider) {
    std::cout << "    " << label << " ";
    for (int cell = 0; cell < NUMBER_OF_CELLS; cell++) {
        double chance;
        if (aboutHider) {
            chance = beliefHiderIsInCell(belief, cell);
        } else {
            chance = beliefSeekerIsInCell(belief, cell);
        }
        std::cout << "cell" << cell << "=" << std::fixed << std::setprecision(0)
                  << (chance * 100.0) << "%  ";
    }
    std::cout << "\n";
}

// ----------------------------------------------------------------------------
// Running one full game
// ----------------------------------------------------------------------------

struct EpisodeResult {
    int stepsTaken;
    bool hiderWasCaught;
    double hiderTotalReward;
    double seekerTotalReward;
};

EpisodeResult runEpisode(const PosgModel& model, std::mt19937& randomGenerator, bool printDetails) {
    EpisodeResult result;
    result.stepsTaken = 0;
    result.hiderWasCaught = false;
    result.hiderTotalReward = 0.0;
    result.seekerTotalReward = 0.0;

    // The true starting state: hider at the far right, seeker at the far left.
    int trueState = encodeState(NUMBER_OF_CELLS - 1, 0);

    // Neither agent knows the starting state, so both start with an even guess.
    Belief hiderBelief = makeUniformBelief();
    Belief seekerBelief = makeUniformBelief();

    if (printDetails) {
        std::cout << "Starting game. Hider in cell " << decodeHiderCell(trueState)
                  << ", seeker in cell " << decodeSeekerCell(trueState) << ".\n\n";
    }

    for (int step = 1; step <= MAXIMUM_STEPS_PER_EPISODE; step++) {

        // Both agents choose at the same time, each using only its own belief.
        int hiderAction = chooseHiderAction(model, hiderBelief, randomGenerator);
        int seekerAction = chooseSeekerAction(model, seekerBelief, randomGenerator);

        // The world moves to a new state according to the transition table.
        const std::vector<double>& nextStateChances =
            model.transition[trueState][hiderAction][seekerAction];
        std::discrete_distribution<int> nextStateChooser(nextStateChances.begin(), nextStateChances.end());
        int nextState = nextStateChooser(randomGenerator);

        // Work out the rewards that were actually earned this step.
        double hiderReward;
        double seekerReward;
        if (nextState == TERMINAL_STATE) {
            hiderReward = CAPTURE_PENALTY_FOR_HIDER;
            seekerReward = CAPTURE_REWARD_FOR_SEEKER;
        } else {
            hiderReward = SURVIVAL_REWARD_FOR_HIDER;
            seekerReward = SEARCH_COST_FOR_SEEKER;
        }
        result.hiderTotalReward += hiderReward;
        result.seekerTotalReward += seekerReward;
        result.stepsTaken = step;

        // Each agent receives its own noisy signal about the new state.
        const std::vector<double>& hiderObservationChances =
            model.observation[HIDER_AGENT][nextState];
        std::discrete_distribution<int> hiderObservationChooser(
            hiderObservationChances.begin(), hiderObservationChances.end());
        int hiderObservation = hiderObservationChooser(randomGenerator);

        const std::vector<double>& seekerObservationChances =
            model.observation[SEEKER_AGENT][nextState];
        std::discrete_distribution<int> seekerObservationChooser(
            seekerObservationChances.begin(), seekerObservationChances.end());
        int seekerObservation = seekerObservationChooser(randomGenerator);

        // Each agent updates its own belief using only what it knows.
        bool gameIsStillGoing = (nextState != TERMINAL_STATE);
        hiderBelief = updateBelief(model, HIDER_AGENT, hiderBelief, hiderAction,
                                   hiderObservation, gameIsStillGoing);
        seekerBelief = updateBelief(model, SEEKER_AGENT, seekerBelief, seekerAction,
                                    seekerObservation, gameIsStillGoing);

        if (printDetails) {
            std::cout << "Step " << step << "\n";
            std::cout << "    hider chose " << hiderActionName(hiderAction)
                      << ", seeker chose " << seekerActionName(seekerAction) << "\n";
            if (nextState == TERMINAL_STATE) {
                std::cout << "    The seeker searched the hider's cell. HIDER CAUGHT.\n";
            } else {
                std::cout << "    true positions now: hider cell " << decodeHiderCell(nextState)
                          << ", seeker cell " << decodeSeekerCell(nextState) << "\n";
            }
            std::cout << "    hider heard: " << observationName(hiderObservation)
                      << ", seeker heard: " << observationName(seekerObservation) << "\n";
            std::cout << "    rewards: hider " << std::fixed << std::setprecision(1) << hiderReward
                      << ", seeker " << seekerReward << "\n";
            if (nextState != TERMINAL_STATE) {
                printBeliefLine("seeker believes hider is in:  ", seekerBelief, true);
                printBeliefLine("hider believes seeker is in:  ", hiderBelief, false);
            }
            std::cout << "\n";
        }

        trueState = nextState;

        if (trueState == TERMINAL_STATE) {
            result.hiderWasCaught = true;
            break;
        }
    }

    return result;
}

// ----------------------------------------------------------------------------
// Main program
// ----------------------------------------------------------------------------

int main() {
    PosgModel model = buildModel();
    std::mt19937 randomGenerator(12345);

    // First, one game with every step printed so you can watch the beliefs.
    std::cout << "=== One detailed game ===\n\n";
    EpisodeResult detailedResult = runEpisode(model, randomGenerator, true);
    std::cout << "Game finished after " << detailedResult.stepsTaken << " steps. "
              << (detailedResult.hiderWasCaught ? "Hider was caught." : "Hider survived.")
              << "\n\n";

    // Then, many games with no printing, to see average behavior.
    const int NUMBER_OF_TEST_GAMES = 2000;
    int numberOfCaptures = 0;
    double totalSteps = 0.0;
    double totalHiderReward = 0.0;
    double totalSeekerReward = 0.0;

    for (int game = 0; game < NUMBER_OF_TEST_GAMES; game++) {
        EpisodeResult result = runEpisode(model, randomGenerator, false);
        if (result.hiderWasCaught) {
            numberOfCaptures++;
        }
        totalSteps += result.stepsTaken;
        totalHiderReward += result.hiderTotalReward;
        totalSeekerReward += result.seekerTotalReward;
    }

    std::cout << "=== Summary over " << NUMBER_OF_TEST_GAMES << " games ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Hider caught within " << MAXIMUM_STEPS_PER_EPISODE << " steps: "
              << (100.0 * numberOfCaptures / NUMBER_OF_TEST_GAMES) << "% of games\n";
    std::cout << "Average game length: " << (totalSteps / NUMBER_OF_TEST_GAMES) << " steps\n";
    std::cout << "Average hider reward: " << (totalHiderReward / NUMBER_OF_TEST_GAMES) << "\n";
    std::cout << "Average seeker reward: " << (totalSeekerReward / NUMBER_OF_TEST_GAMES) << "\n";

    return 0;
}