#include "counter.h"

#include <iostream>

Counter::Counter() = default;

Counter::Counter(std::int64_t initial) : value_(initial) {}

Counter::~Counter() { std::cout << "~Counter" << '\n'; }

std::int64_t Counter::value() const { return value_; }

void Counter::increment() { ++value_; }

std::int64_t Counter::add(std::int64_t delta) {
  value_ += delta;
  return value_;
}

std::string Counter::label() const { return "counter"; }
