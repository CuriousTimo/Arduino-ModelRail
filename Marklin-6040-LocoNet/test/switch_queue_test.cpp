// Host tests for SwitchQueue.h, without an Arduino:
//   c++ -std=c++11 -Wall -Wextra -I.. switch_queue_test.cpp -o switch_queue_test && ./switch_queue_test

#include <cstdio>

#include "SwitchQueue.h"

static int failures = 0;

#define CHECK(condition)                                         \
  do {                                                           \
    if (!(condition)) {                                          \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      failures++;                                                \
    }                                                            \
  } while (0)

using switchqueue::Command;
using switchqueue::Queue;

// As recorded on an IB-Switch without track power (docs/ib-switch-analysis.md, test 6):
// 111 red refused, then 111 green and 112 red pressed. After GO they go out in that order;
// here the newer press for 111 replaces the older one, so 111 ends green as there.
static void keepsPressesInOrderAndTheNewestPerTurnout() {
  Queue queue;
  CHECK(queue.pushBack({111, false}));
  CHECK(queue.pushBack({111, true}));
  CHECK(queue.pushBack({112, false}));
  CHECK(queue.size() == 2);
  Command first = queue.popFront();
  CHECK(first.address == 111 && first.green);
  Command second = queue.popFront();
  CHECK(second.address == 112 && !second.green);
  CHECK(queue.empty());
}

// A command sent again and refused again stays first, ahead of newer ones.
static void aRetryRefusedAgainKeepsItsPlace() {
  Queue queue;
  queue.pushBack({1, true});
  queue.pushBack({2, true});
  Command retry = queue.popFront();
  CHECK(queue.pushFront(retry));
  CHECK(queue.front().address == 1);
  CHECK(queue.size() == 2);
}

// Another device switched the turnout: its waiting command is dropped.
static void removesASwitchedTurnout() {
  Queue queue;
  queue.pushBack({5, true});
  queue.pushBack({6, false});
  CHECK(queue.remove(5));
  CHECK(!queue.remove(5));
  CHECK(queue.size() == 1 && queue.front().address == 6);
}

// Full: a new refusal drops the oldest; a refused retry drops the newest.
static void makesRoomWhenFull() {
  Queue queue;
  for (uint16_t address = 1; address <= Queue::kCapacity; address++) {
    CHECK(queue.pushBack({address, true}));
  }
  CHECK(!queue.pushBack({100, true}));
  CHECK(queue.size() == Queue::kCapacity);
  CHECK(queue.front().address == 2);
  CHECK(!queue.pushFront({200, true}));
  CHECK(queue.front().address == 200);
  CHECK(queue.size() == Queue::kCapacity);
  Command last = {0, false};
  while (!queue.empty()) {
    last = queue.popFront();
  }
  CHECK(last.address == 16);  // 100, the newest, made room for the retry
}

int main() {
  keepsPressesInOrderAndTheNewestPerTurnout();
  aRetryRefusedAgainKeepsItsPlace();
  removesASwitchedTurnout();
  makesRoomWhenFull();
  if (failures == 0) {
    std::printf("all switch queue tests passed\n");
  }
  return failures == 0 ? 0 : 1;
}
