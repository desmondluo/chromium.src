// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nacl_io/pipe/pipe_node.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>

#include "nacl_io/ioctl.h"
#include "nacl_io/kernel_handle.h"
#include "nacl_io/pipe/pipe_event_emitter.h"

namespace {
const size_t kDefaultPipeSize = 512 * 1024;
}

namespace nacl_io {

PipeNode::PipeNode(Filesystem* fs)
    : StreamNode(fs), pipe_(new PipeEventEmitter(kDefaultPipeSize)) {}

EventEmitter* PipeNode::GetEventEmitter() { return pipe_.get(); }

Error PipeNode::Read(const HandleAttr& attr,
                     void* buf,
                     size_t count,
                     int* out_bytes) {
  int ms = attr.IsBlocking() ? read_timeout_ : 0;

  EventListenerLock wait(GetEventEmitter());
  Error err = wait.WaitOnEvent(POLLIN, ms);
  if (err)
    return err;

  *out_bytes = pipe_->Read_Locked(static_cast<char*>(buf), count);
  return 0;
}

Error PipeNode::Write(const HandleAttr& attr,
                      const void* buf,
                      size_t count,
                      int* out_bytes) {
  int ms = attr.IsBlocking() ? write_timeout_ : 0;

  EventListenerLock wait(GetEventEmitter());
  Error err = wait.WaitOnEvent(POLLOUT, ms);
  if (err)
    return err;

  *out_bytes = pipe_->Write_Locked(static_cast<const char*>(buf), count);
  return 0;
}

}  // namespace nacl_io

