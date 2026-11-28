#ifndef COO_COUNTER_H
#define COO_COUNTER_H

#include <cstdint>
#include <string>

class Counter {
 public:
  Counter();
  explicit Counter(std::int64_t initial);
  ~Counter();

  std::int64_t value() const;
  void increment();
  std::int64_t add(std::int64_t delta);
  std::string label() const;

 private:
  std::int64_t value_ = 0;
};

#endif /* COO_COUNTER_H */
