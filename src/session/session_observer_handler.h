// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Session Observer Handler managing observer instances.

#ifndef MOZC_SESSION_SESSION_OBSERVER_HANDLER_H_
#define MOZC_SESSION_SESSION_OBSERVER_HANDLER_H_

#include <vector>

#include "base/port.h"

namespace mozc {
namespace commands {
class Command;
}

namespace session {

class SessionObserverInterface;

class SessionObserverHandler {
 public:
  SessionObserverHandler();
  SessionObserverHandler(const SessionObserverHandler &) = delete;
  SessionObserverHandler &operator=(const SessionObserverHandler &) = delete;
  virtual ~SessionObserverHandler();

  void AddObserver(SessionObserverInterface *observer);
  void EvalCommandHandler(const commands::Command &command);

 private:
  std::vector<SessionObserverInterface *> observers_;
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_SESSION_OBSERVER_HANDLER_H_
