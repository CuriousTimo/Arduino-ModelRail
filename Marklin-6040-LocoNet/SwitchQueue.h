// Switch commands the command station refused, waiting to be sent again, oldest first.
// An IB-Switch keeps them the same way (docs/ib-switch-analysis.md, test 6): it repeats
// the oldest every 3 s and sends them all once the command station takes commands again.
// Plain C++, so the host tests run it without an Arduino.

#ifndef SWITCH_QUEUE_H
#define SWITCH_QUEUE_H

#include <stdint.h>

namespace switchqueue {

/// A turnout and the position a key asked for: green is closed, red thrown.
struct Command {
  uint16_t address;
  bool green;
};

class Queue {
 public:
  static const uint8_t kCapacity = 16;

  uint8_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  const Command& front() const { return items_[0]; }

  /// A newly refused command goes to the back. A command waiting for the same turnout is
  /// dropped: the newer position wins. Returns false if the oldest had to make room.
  bool pushBack(const Command& command) {
    remove(command.address);
    bool fits = count_ < kCapacity;
    if (!fits) {
      removeAt(0);
    }
    items_[count_++] = command;
    return fits;
  }

  /// A command sent again and refused again keeps its place at the front. Returns false
  /// if the newest had to make room.
  bool pushFront(const Command& command) {
    remove(command.address);
    bool fits = count_ < kCapacity;
    if (!fits) {
      count_--;
    }
    for (uint8_t i = count_; i > 0; i--) {
      items_[i] = items_[i - 1];
    }
    items_[0] = command;
    count_++;
    return fits;
  }

  Command popFront() {
    Command command = items_[0];
    removeAt(0);
    return command;
  }

  /// Drops the command waiting for this turnout, if any: it was switched since.
  bool remove(uint16_t address) {
    for (uint8_t i = 0; i < count_; i++) {
      if (items_[i].address == address) {
        removeAt(i);
        return true;
      }
    }
    return false;
  }

 private:
  void removeAt(uint8_t index) {
    for (uint8_t i = index; i + 1 < count_; i++) {
      items_[i] = items_[i + 1];
    }
    count_--;
  }

  Command items_[kCapacity];
  uint8_t count_ = 0;
};

}  // namespace switchqueue

#endif
