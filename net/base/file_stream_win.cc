// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/file_stream.h"

#include <windows.h>

#include "base/file_path.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/worker_pool.h"
#include "net/base/file_stream_metrics.h"
#include "net/base/file_stream_net_log_parameters.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"

namespace net {

// Ensure that we can just use our Whence values directly.
COMPILE_ASSERT(FROM_BEGIN == FILE_BEGIN, bad_whence_begin);
COMPILE_ASSERT(FROM_CURRENT == FILE_CURRENT, bad_whence_current);
COMPILE_ASSERT(FROM_END == FILE_END, bad_whence_end);

static void SetOffset(OVERLAPPED* overlapped, const LARGE_INTEGER& offset) {
  overlapped->Offset = offset.LowPart;
  overlapped->OffsetHigh = offset.HighPart;
}

static void IncrementOffset(OVERLAPPED* overlapped, DWORD count) {
  LARGE_INTEGER offset;
  offset.LowPart = overlapped->Offset;
  offset.HighPart = overlapped->OffsetHigh;
  offset.QuadPart += static_cast<LONGLONG>(count);
  SetOffset(overlapped, offset);
}

namespace {

int RecordAndMapError(int error,
                      FileErrorSource source,
                      bool record_uma,
                      const net::BoundNetLog& bound_net_log) {
  net::Error net_error = MapSystemError(error);

  bound_net_log.AddEvent(
      net::NetLog::TYPE_FILE_STREAM_ERROR,
      make_scoped_refptr(
          new FileStreamErrorParameters(GetFileErrorSourceName(source),
                                        error,
                                        net_error)));

  RecordFileError(error, source, record_uma);

  return net_error;
}

// Opens a file with some network logging.
// The opened file and the result code are written to |file| and |result|.
void OpenFile(const FilePath& path,
              int open_flags,
              bool record_uma,
              const net::BoundNetLog& bound_net_log,
              base::PlatformFile* file,
              int* result) {
  bound_net_log.BeginEvent(
      net::NetLog::TYPE_FILE_STREAM_OPEN,
      make_scoped_refptr(
          new net::NetLogStringParameter("file_name",
                                         path.AsUTF8Unsafe())));

  *file = base::CreatePlatformFile(path, open_flags, NULL, NULL);
  if (*file == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    LOG(WARNING) << "Failed to open file: " << error;
    *result = RecordAndMapError(error,
                                FILE_ERROR_SOURCE_OPEN,
                                record_uma,
                                bound_net_log);
    bound_net_log.EndEvent(net::NetLog::TYPE_FILE_STREAM_OPEN, NULL);
    return;
  }
}

// Closes a file with some network logging.
void CloseFile(base::PlatformFile file,
               const net::BoundNetLog& bound_net_log) {
  bound_net_log.AddEvent(net::NetLog::TYPE_FILE_STREAM_CLOSE, NULL);
  if (file == INVALID_HANDLE_VALUE)
    return;

  CancelIo(file);

  if (!base::ClosePlatformFile(file))
    NOTREACHED();
  bound_net_log.EndEvent(net::NetLog::TYPE_FILE_STREAM_OPEN, NULL);
}

}  // namespace

// FileStream::AsyncContext ----------------------------------------------

class FileStream::AsyncContext : public MessageLoopForIO::IOHandler {
 public:
  explicit AsyncContext(const net::BoundNetLog& bound_net_log)
      : context_(), is_closing_(false),
        record_uma_(false), bound_net_log_(bound_net_log),
        error_source_(FILE_ERROR_SOURCE_COUNT) {
    context_.handler = this;
  }
  ~AsyncContext();

  void IOCompletionIsPending(const CompletionCallback& callback,
                             IOBuffer* buf);

  OVERLAPPED* overlapped() { return &context_.overlapped; }
  const CompletionCallback& callback() const { return callback_; }

  void set_error_source(FileErrorSource source) { error_source_ = source; }

  void EnableErrorStatistics() {
    record_uma_ = true;
  }

 private:
  virtual void OnIOCompleted(MessageLoopForIO::IOContext* context,
                             DWORD bytes_read, DWORD error) OVERRIDE;

  MessageLoopForIO::IOContext context_;
  CompletionCallback callback_;
  scoped_refptr<IOBuffer> in_flight_buf_;
  bool is_closing_;
  bool record_uma_;
  const net::BoundNetLog bound_net_log_;
  FileErrorSource error_source_;
};

FileStream::AsyncContext::~AsyncContext() {
  is_closing_ = true;
  bool waited = false;
  base::TimeTicks start = base::TimeTicks::Now();
  while (!callback_.is_null()) {
    waited = true;
    MessageLoopForIO::current()->WaitForIOCompletion(INFINITE, this);
  }
  if (waited) {
    // We want to see if we block the message loop for too long.
    UMA_HISTOGRAM_TIMES("AsyncIO.FileStreamClose",
                        base::TimeTicks::Now() - start);
  }
}

void FileStream::AsyncContext::IOCompletionIsPending(
    const CompletionCallback& callback,
    IOBuffer* buf) {
  DCHECK(callback_.is_null());
  callback_ = callback;
  in_flight_buf_ = buf;  // Hold until the async operation ends.
}

void FileStream::AsyncContext::OnIOCompleted(
    MessageLoopForIO::IOContext* context, DWORD bytes_read, DWORD error) {
  DCHECK_EQ(&context_, context);
  DCHECK(!callback_.is_null());

  if (is_closing_) {
    callback_.Reset();
    in_flight_buf_ = NULL;
    return;
  }

  int result = static_cast<int>(bytes_read);
  if (error && error != ERROR_HANDLE_EOF) {
    result = RecordAndMapError(error, error_source_, record_uma_,
                               bound_net_log_);
  }

  if (bytes_read)
    IncrementOffset(&context->overlapped, bytes_read);

  CompletionCallback temp_callback = callback_;
  callback_.Reset();
  scoped_refptr<IOBuffer> temp_buf = in_flight_buf_;
  in_flight_buf_ = NULL;
  temp_callback.Run(result);
}

// FileStream ------------------------------------------------------------

FileStream::FileStream(net::NetLog* net_log)
    : file_(base::kInvalidPlatformFileValue),
      open_flags_(0),
      auto_closed_(true),
      record_uma_(false),
      bound_net_log_(net::BoundNetLog::Make(net_log,
                                            net::NetLog::SOURCE_FILESTREAM)),
      weak_ptr_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
  bound_net_log_.BeginEvent(net::NetLog::TYPE_FILE_STREAM_ALIVE, NULL);
}

FileStream::FileStream(base::PlatformFile file, int flags, net::NetLog* net_log)
    : file_(file),
      open_flags_(flags),
      auto_closed_(false),
      record_uma_(false),
      bound_net_log_(net::BoundNetLog::Make(net_log,
                                            net::NetLog::SOURCE_FILESTREAM)),
      weak_ptr_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
  bound_net_log_.BeginEvent(net::NetLog::TYPE_FILE_STREAM_ALIVE, NULL);

  // If the file handle is opened with base::PLATFORM_FILE_ASYNC, we need to
  // make sure we will perform asynchronous File IO to it.
  if (flags & base::PLATFORM_FILE_ASYNC) {
    async_context_.reset(new AsyncContext(bound_net_log_));
    MessageLoopForIO::current()->RegisterIOHandler(file_,
                                                   async_context_.get());
  }
}

FileStream::~FileStream() {
  if (auto_closed_) {
    if (async_context_.get()) {
      // Make sure we don't have a request in flight.
      DCHECK(async_context_->callback().is_null());

      // Close the file in the background.
      const bool posted = base::WorkerPool::PostTask(
          FROM_HERE,
          base::Bind(&CloseFile, file_, bound_net_log_),
          true /* task_is_slow */);
      DCHECK(posted);
    } else {
      CloseSync();
    }
  }

  bound_net_log_.EndEvent(net::NetLog::TYPE_FILE_STREAM_ALIVE, NULL);
}

void FileStream::Close(const CompletionCallback& callback) {
  DCHECK(callback_.is_null());
  callback_ = callback;

  // Make sure we don't have a request in flight. Unlike CloseSync(), don't
  // abort existing asynchronous operations, as it'd block.
  DCHECK(async_context_.get());
  DCHECK(async_context_->callback().is_null());

  const bool posted = base::WorkerPool::PostTaskAndReply(
      FROM_HERE,
      base::Bind(&CloseFile, file_, bound_net_log_),
      base::Bind(&FileStream::OnClosed, weak_ptr_factory_.GetWeakPtr()),
      true /* task_is_slow */);
  DCHECK(posted);
}

void FileStream::CloseSync() {
  // The logic here is similar to CloseFile() but async_context_.reset() is
  // caled in this function.

  bound_net_log_.AddEvent(net::NetLog::TYPE_FILE_STREAM_CLOSE, NULL);
  if (file_ != INVALID_HANDLE_VALUE)
    CancelIo(file_);

  // TODO(satorux): Remove this once all async clients are migrated to use
  // Close(). crbug.com/114783
  async_context_.reset();

  if (file_ != INVALID_HANDLE_VALUE) {
    if (!base::ClosePlatformFile(file_))
      NOTREACHED();
    file_ = INVALID_HANDLE_VALUE;

    bound_net_log_.EndEvent(net::NetLog::TYPE_FILE_STREAM_OPEN, NULL);
  }
}

int FileStream::Open(const FilePath& path, int open_flags,
                     const CompletionCallback& callback) {
  if (IsOpen()) {
    DLOG(FATAL) << "File is already open!";
    return ERR_UNEXPECTED;
  }

  DCHECK(callback_.is_null());
  callback_ = callback;

  open_flags_ = open_flags;
  DCHECK(open_flags_ & base::PLATFORM_FILE_ASYNC);

  base::PlatformFile* file =
      new base::PlatformFile(base::kInvalidPlatformFileValue);
  int* result = new int(OK);
  const bool posted = base::WorkerPool::PostTaskAndReply(
      FROM_HERE,
      base::Bind(&OpenFile, path, open_flags, record_uma_, bound_net_log_,
                 file, result),
      base::Bind(&FileStream::OnOpened,
                 weak_ptr_factory_.GetWeakPtr(),
                 base::Owned(file),
                 base::Owned(result)),
      true /* task_is_slow */);
  DCHECK(posted);
  return ERR_IO_PENDING;
}

int FileStream::OpenSync(const FilePath& path, int open_flags) {
  if (IsOpen()) {
    DLOG(FATAL) << "File is already open!";
    return ERR_UNEXPECTED;
  }

  open_flags_ = open_flags;

  int result = OK;
  OpenFile(path, open_flags_, record_uma_, bound_net_log_, &file_, &result);
  if (result != OK)
    return result;

  // TODO(satorux): Remove this once all async clients are migrated to use
  // Open(). crbug.com/114783
  if (open_flags_ & base::PLATFORM_FILE_ASYNC) {
    async_context_.reset(new AsyncContext(bound_net_log_));
    if (record_uma_)
      async_context_->EnableErrorStatistics();
    MessageLoopForIO::current()->RegisterIOHandler(file_,
                                                   async_context_.get());
  }

  return OK;
}

bool FileStream::IsOpen() const {
  return file_ != INVALID_HANDLE_VALUE;
}

int64 FileStream::Seek(Whence whence, int64 offset) {
  if (!IsOpen())
    return ERR_UNEXPECTED;

  DCHECK(!async_context_.get() || async_context_->callback().is_null());

  LARGE_INTEGER distance, result;
  distance.QuadPart = offset;
  DWORD move_method = static_cast<DWORD>(whence);
  if (!SetFilePointerEx(file_, distance, &result, move_method)) {
    DWORD error = GetLastError();
    LOG(WARNING) << "SetFilePointerEx failed: " << error;
    return RecordAndMapError(error,
                             FILE_ERROR_SOURCE_SEEK,
                             record_uma_,
                             bound_net_log_);
  }
  if (async_context_.get()) {
    async_context_->set_error_source(FILE_ERROR_SOURCE_SEEK);
    SetOffset(async_context_->overlapped(), result);
  }
  return result.QuadPart;
}

int64 FileStream::Available() {
  base::ThreadRestrictions::AssertIOAllowed();

  if (!IsOpen())
    return ERR_UNEXPECTED;

  int64 cur_pos = Seek(FROM_CURRENT, 0);
  if (cur_pos < 0)
    return cur_pos;

  LARGE_INTEGER file_size;
  if (!GetFileSizeEx(file_, &file_size)) {
    DWORD error = GetLastError();
    LOG(WARNING) << "GetFileSizeEx failed: " << error;
    return RecordAndMapError(error,
                             FILE_ERROR_SOURCE_GET_SIZE,
                             record_uma_,
                             bound_net_log_);
  }

  return file_size.QuadPart - cur_pos;
}

int FileStream::Read(
    IOBuffer* buf, int buf_len, const CompletionCallback& callback) {
  DCHECK(async_context_.get());

  if (!IsOpen())
    return ERR_UNEXPECTED;

  DCHECK(open_flags_ & base::PLATFORM_FILE_READ);

  OVERLAPPED* overlapped = NULL;
  DCHECK(!callback.is_null());
  DCHECK(async_context_->callback().is_null());
  overlapped = async_context_->overlapped();
  async_context_->set_error_source(FILE_ERROR_SOURCE_READ);

  int rv = 0;

  DWORD bytes_read;
  if (!ReadFile(file_, buf->data(), buf_len, &bytes_read, overlapped)) {
    DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING) {
      async_context_->IOCompletionIsPending(callback, buf);
      rv = ERR_IO_PENDING;
    } else if (error == ERROR_HANDLE_EOF) {
      rv = 0;  // Report EOF by returning 0 bytes read.
    } else {
      LOG(WARNING) << "ReadFile failed: " << error;
      rv = RecordAndMapError(error,
                             FILE_ERROR_SOURCE_READ,
                             record_uma_,
                             bound_net_log_);
    }
  } else if (overlapped) {
    async_context_->IOCompletionIsPending(callback, buf);
    rv = ERR_IO_PENDING;
  } else {
    rv = static_cast<int>(bytes_read);
  }
  return rv;
}

int FileStream::ReadSync(char* buf, int buf_len) {
  DCHECK(!async_context_.get());
  base::ThreadRestrictions::AssertIOAllowed();

  if (!IsOpen())
    return ERR_UNEXPECTED;

  DCHECK(open_flags_ & base::PLATFORM_FILE_READ);

  int rv = 0;

  DWORD bytes_read;
  if (!ReadFile(file_, buf, buf_len, &bytes_read, NULL)) {
    DWORD error = GetLastError();
    if (error == ERROR_HANDLE_EOF) {
      rv = 0;  // Report EOF by returning 0 bytes read.
    } else {
      LOG(WARNING) << "ReadFile failed: " << error;
      rv = RecordAndMapError(error,
                             FILE_ERROR_SOURCE_READ,
                             record_uma_,
                             bound_net_log_);
    }
  } else {
    rv = static_cast<int>(bytes_read);
  }
  return rv;
}

int FileStream::ReadUntilComplete(char *buf, int buf_len) {
  int to_read = buf_len;
  int bytes_total = 0;

  do {
    int bytes_read = ReadSync(buf, to_read);
    if (bytes_read <= 0) {
      if (bytes_total == 0)
        return bytes_read;

      return bytes_total;
    }

    bytes_total += bytes_read;
    buf += bytes_read;
    to_read -= bytes_read;
  } while (bytes_total < buf_len);

  return bytes_total;
}

int FileStream::Write(
    IOBuffer* buf, int buf_len, const CompletionCallback& callback) {
  DCHECK(async_context_.get());

  if (!IsOpen())
    return ERR_UNEXPECTED;

  DCHECK(open_flags_ & base::PLATFORM_FILE_WRITE);

  OVERLAPPED* overlapped = NULL;
  DCHECK(!callback.is_null());
  DCHECK(async_context_->callback().is_null());
  overlapped = async_context_->overlapped();
  async_context_->set_error_source(FILE_ERROR_SOURCE_WRITE);

  int rv = 0;
  DWORD bytes_written = 0;
  if (!WriteFile(file_, buf->data(), buf_len, &bytes_written, overlapped)) {
    DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING) {
      async_context_->IOCompletionIsPending(callback, buf);
      rv = ERR_IO_PENDING;
    } else {
      LOG(WARNING) << "WriteFile failed: " << error;
      rv = RecordAndMapError(error,
                             FILE_ERROR_SOURCE_WRITE,
                             record_uma_,
                             bound_net_log_);
    }
  } else if (overlapped) {
    async_context_->IOCompletionIsPending(callback, buf);
    rv = ERR_IO_PENDING;
  } else {
    rv = static_cast<int>(bytes_written);
  }
  return rv;
}

int FileStream::WriteSync(
    const char* buf, int buf_len) {
  DCHECK(!async_context_.get());
  base::ThreadRestrictions::AssertIOAllowed();

  if (!IsOpen())
    return ERR_UNEXPECTED;

  DCHECK(open_flags_ & base::PLATFORM_FILE_WRITE);

  int rv = 0;
  DWORD bytes_written = 0;
  if (!WriteFile(file_, buf, buf_len, &bytes_written, NULL)) {
    DWORD error = GetLastError();
    LOG(WARNING) << "WriteFile failed: " << error;
    rv = RecordAndMapError(error,
                           FILE_ERROR_SOURCE_WRITE,
                           record_uma_,
                           bound_net_log_);
  } else {
    rv = static_cast<int>(bytes_written);
  }
  return rv;
}

int FileStream::Flush() {
  base::ThreadRestrictions::AssertIOAllowed();

  if (!IsOpen())
    return ERR_UNEXPECTED;

  DCHECK(open_flags_ & base::PLATFORM_FILE_WRITE);
  if (FlushFileBuffers(file_)) {
    return OK;
  }

  return RecordAndMapError(GetLastError(),
                           FILE_ERROR_SOURCE_FLUSH,
                           record_uma_,
                           bound_net_log_);
}

int64 FileStream::Truncate(int64 bytes) {
  base::ThreadRestrictions::AssertIOAllowed();

  if (!IsOpen())
    return ERR_UNEXPECTED;

  // We'd better be open for writing.
  DCHECK(open_flags_ & base::PLATFORM_FILE_WRITE);

  // Seek to the position to truncate from.
  int64 seek_position = Seek(FROM_BEGIN, bytes);
  if (seek_position != bytes)
    return ERR_UNEXPECTED;

  // And truncate the file.
  BOOL result = SetEndOfFile(file_);
  if (!result) {
    DWORD error = GetLastError();
    LOG(WARNING) << "SetEndOfFile failed: " << error;
    return RecordAndMapError(error,
                             FILE_ERROR_SOURCE_SET_EOF,
                             record_uma_,
                             bound_net_log_);
  }

  // Success.
  return seek_position;
}

void FileStream::EnableErrorStatistics() {
  record_uma_ = true;

  if (async_context_.get())
    async_context_->EnableErrorStatistics();
}

void FileStream::SetBoundNetLogSource(
    const net::BoundNetLog& owner_bound_net_log) {
  if ((owner_bound_net_log.source().id == net::NetLog::Source::kInvalidId) &&
      (bound_net_log_.source().id == net::NetLog::Source::kInvalidId)) {
    // Both |BoundNetLog|s are invalid.
    return;
  }

  // Should never connect to itself.
  DCHECK_NE(bound_net_log_.source().id, owner_bound_net_log.source().id);

  bound_net_log_.AddEvent(
      net::NetLog::TYPE_FILE_STREAM_BOUND_TO_OWNER,
      make_scoped_refptr(
          new net::NetLogSourceParameter("source_dependency",
                                         owner_bound_net_log.source())));

  owner_bound_net_log.AddEvent(
      net::NetLog::TYPE_FILE_STREAM_SOURCE,
      make_scoped_refptr(
          new net::NetLogSourceParameter("source_dependency",
                                         bound_net_log_.source())));
}

void FileStream::OnClosed() {
  file_ = INVALID_HANDLE_VALUE;

  CompletionCallback temp = callback_;
  callback_.Reset();
  temp.Run(OK);
}

void FileStream::OnOpened(base::PlatformFile* file, int* result) {
  file_ = *file;

  if (*result == OK) {
    async_context_.reset(new AsyncContext(bound_net_log_));
    if (record_uma_)
      async_context_->EnableErrorStatistics();
    MessageLoopForIO::current()->RegisterIOHandler(file_,
                                                   async_context_.get());
  }

  CompletionCallback temp = callback_;
  callback_.Reset();
  temp.Run(*result);
}

}  // namespace net
