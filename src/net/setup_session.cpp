#include "net/setup_session.h"

#include <cstring>

namespace fw {

const char* SetupSession::token(uint32_t nowMs, uint32_t lifetimeMs,
                                SetupTokenGenerator generate) {
  if (token_[0] != '\0' && (nowMs - issuedMs_) <= lifetimeMs) {
    return token_;
  }
  reset();
  if (generate != nullptr) {
    generate(token_, sizeof(token_));
    // A generator that filled the whole buffer must not leave it unterminated.
    token_[kTokenLength] = '\0';
    issuedMs_ = nowMs;
  }
  return token_;
}

bool SetupSession::authorised(const char* candidate, uint32_t nowMs,
                              uint32_t lifetimeMs) const {
  if (token_[0] == '\0' || candidate == nullptr || candidate[0] == '\0') {
    return false;
  }
  if ((nowMs - issuedMs_) > lifetimeMs) {
    return false;
  }
  return std::strcmp(candidate, token_) == 0;
}

void SetupSession::reset() {
  token_[0] = '\0';
  issuedMs_ = 0;
}

}  // namespace fw
