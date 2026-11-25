// Short lived setup token that ties the setup form to the POST that saves it.
//
// The token state lives here, free of Arduino code, so the rule that matters on
// the device is unit tested on the host: serving the setup page again must not
// invalidate the form the browser is already showing. `web_server.cpp` supplies
// the random generator.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fw {

// Fills `out` with a new token of at most kSetupTokenLength characters plus the
// terminator.
using SetupTokenGenerator = void (*)(char* out, size_t outSize);

class SetupSession {
 public:
  // Eight random bytes rendered as hex: unique per form, unguessable from the
  // network, and short enough for the page.
  static constexpr size_t kTokenLength = 16;

  // Returns the token a form page must carry. A token that is still valid is
  // handed back unchanged, so a background request that lands on the setup page
  // (/favicon.ico, a captive portal probe, /robots.txt) cannot break the form
  // the browser is showing. A new token is issued only when none is held or the
  // current one has outlived `lifetimeMs`.
  const char* token(uint32_t nowMs, uint32_t lifetimeMs, SetupTokenGenerator generate);

  // True when `candidate` is the live token and it has not expired.
  bool authorised(const char* candidate, uint32_t nowMs, uint32_t lifetimeMs) const;

  // Forgets the token, so the next form page gets a new one. Called after a
  // save has been accepted and when the setup AP stops.
  void reset();

  bool active() const { return token_[0] != '\0'; }

  // The token currently held, or "" when the session has no token.
  const char* current() const { return token_; }

 private:
  char token_[kTokenLength + 1] = {0};
  uint32_t issuedMs_ = 0;
};

}  // namespace fw
