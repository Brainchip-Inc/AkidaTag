#ifndef AKIDA_PANIC_EXCEPTION_H
#define AKIDA_PANIC_EXCEPTION_H

#include <cstring>

/**
 * @brief Exception thrown by panic() when the Akida engine encounters an
 * unrecoverable error condition. Uses a fixed-size buffer to avoid heap
 * allocation during error handling.
 */
class AkidaPanicException {
public:
  AkidaPanicException(const char *msg) {
    strncpy(message_, msg, sizeof(message_) - 1);
    message_[sizeof(message_) - 1] = '\0';
  }

  const char *what() const { return message_; }

private:
  char message_[128];
};

#endif /* AKIDA_PANIC_EXCEPTION_H */
