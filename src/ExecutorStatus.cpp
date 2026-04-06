// ExecutorStatus.cpp
#include "ExecutorStatus.h"
#include "Utils.h"
#include "KeyLedControl.h"

// List of executors we track, ordered the same way they are sent over OSC
const uint16_t EXECUTOR_IDS[NUM_EXECUTORS_TRACKED] = {
  101, 102, 103, 104, 105, 106, 107, 108, 109, 110,
  201, 202, 203, 204, 205, 206, 207, 208, 209, 210,
  301, 302, 303, 304, 305, 306, 307, 308, 309, 310,
  401, 402, 403, 404, 405, 406, 407, 408, 409, 410
};

// 0 = not populated, 1 = populated/off, 2 = populated/on
uint8_t executorStatus[NUM_EXECUTORS_TRACKED] = {0};
uint8_t executorColors[NUM_EXECUTORS_TRACKED][3] = {{0}};

int executorIndexFromID(uint16_t execId) {
  for (int i = 0; i < NUM_EXECUTORS_TRACKED; ++i) {
    if (EXECUTOR_IDS[i] == execId) {
      return i;
    }
  }
  return -1;
}

bool setExecutorStateByIndex(int index, uint8_t status) {
  if (index < 0 || index >= NUM_EXECUTORS_TRACKED) {
    return false;
  }

  uint8_t clamped = status > 2 ? 2 : status;
  bool changed = executorStatus[index] != clamped;
  executorStatus[index] = clamped;

  return changed;
}

bool setExecutorStateByID(uint16_t execId, uint8_t status) {
  return setExecutorStateByIndex(executorIndexFromID(execId), status);
}

bool setExecutorColorByIndex(int index, uint8_t r, uint8_t g, uint8_t b) {
  if (index < 0 || index >= NUM_EXECUTORS_TRACKED) {
    return false;
  }

  uint8_t rr = r;
  uint8_t gg = g;
  uint8_t bb = b;

  bool changed = (executorColors[index][0] != rr) ||
                 (executorColors[index][1] != gg) ||
                 (executorColors[index][2] != bb);

  executorColors[index][0] = rr;
  executorColors[index][1] = gg;
  executorColors[index][2] = bb;

  if (changed) {
    markKeyLedsDirty();
  }

  return changed;
}

bool setExecutorColorByID(uint16_t execId, uint8_t r, uint8_t g, uint8_t b) {
  return setExecutorColorByIndex(executorIndexFromID(execId), r, g, b);
}
