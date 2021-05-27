// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_BASE_INTERNAL_STATUS_H_
#define IREE_BASE_INTERNAL_STATUS_H_

#ifndef __cplusplus
#error iree::Status is only usable in C++ code.
#endif  // !__cplusplus

#include <cstdint>
#include <memory>
#include <string>

#include "iree/base/api.h"
#include "iree/base/attributes.h"
#include "iree/base/logging.h"
#include "iree/base/target_platform.h"

namespace iree {

template <class T, class U = T>
constexpr T exchange(T& obj, U&& new_value) {
  T old_value = std::move(obj);
  obj = std::forward<U>(new_value);
  return old_value;
}

// Class representing a specific location in the source code of a program.
class SourceLocation {
 public:
  // Avoid this constructor; it populates the object with dummy values.
  constexpr SourceLocation() : line_(0), file_name_(nullptr) {}

  // `file_name` must outlive all copies of the `iree::SourceLocation` object,
  // so in practice it should be a string literal.
  constexpr SourceLocation(std::uint_least32_t line, const char* file_name)
      : line_(line), file_name_(file_name) {}

  // The line number of the captured source location.
  constexpr std::uint_least32_t line() const { return line_; }

  // The file name of the captured source location.
  constexpr const char* file_name() const { return file_name_; }

 private:
  std::uint_least32_t line_;
  const char* file_name_;
};

// If a function takes an `iree::SourceLocation` parameter, pass this as the
// argument.
#if IREE_STATUS_FEATURES == 0
#define IREE_LOC ::iree::SourceLocation(0, NULL)
#else
#define IREE_LOC ::iree::SourceLocation(__LINE__, __FILE__)
#endif  // IREE_STATUS_FEATURES == 0

enum class StatusCode : uint32_t {
  kOk = IREE_STATUS_OK,
  kCancelled = IREE_STATUS_CANCELLED,
  kUnknown = IREE_STATUS_UNKNOWN,
  kInvalidArgument = IREE_STATUS_INVALID_ARGUMENT,
  kDeadlineExceeded = IREE_STATUS_DEADLINE_EXCEEDED,
  kNotFound = IREE_STATUS_NOT_FOUND,
  kAlreadyExists = IREE_STATUS_ALREADY_EXISTS,
  kPermissionDenied = IREE_STATUS_PERMISSION_DENIED,
  kResourceExhausted = IREE_STATUS_RESOURCE_EXHAUSTED,
  kFailedPrecondition = IREE_STATUS_FAILED_PRECONDITION,
  kAborted = IREE_STATUS_ABORTED,
  kOutOfRange = IREE_STATUS_OUT_OF_RANGE,
  kUnimplemented = IREE_STATUS_UNIMPLEMENTED,
  kInternal = IREE_STATUS_INTERNAL,
  kUnavailable = IREE_STATUS_UNAVAILABLE,
  kDataLoss = IREE_STATUS_DATA_LOSS,
  kUnauthenticated = IREE_STATUS_UNAUTHENTICATED,
};

static inline const char* StatusCodeToString(StatusCode code) {
  return iree_status_code_string(static_cast<iree_status_code_t>(code));
}

// Prints a human-readable representation of `x` to `os`.
std::ostream& operator<<(std::ostream& os, const StatusCode& x);

class IREE_MUST_USE_RESULT Status;

// A Status value can be either OK or not-OK
//   * OK indicates that the operation succeeded.
//   * A not-OK value indicates that the operation failed and contains details
//     about the error.
class Status final {
 public:
  // Return a combination of the error code name and message.
  static IREE_MUST_USE_RESULT std::string ToString(iree_status_t status);

  // Creates an OK status with no message.
  Status() = default;

  // Takes ownership of a C API status instance.
  Status(iree_status_t&& status) noexcept
      : value_(exchange(status,
                        iree_status_from_code(iree_status_code(status)))) {}

  // Takes ownership of a C API status instance wrapped in a Status.
  Status(Status& other) noexcept
      : value_(exchange(other.value_, iree_status_from_code(other.code()))) {}
  Status(Status&& other) noexcept
      : value_(exchange(other.value_, iree_status_from_code(other.code()))) {}
  Status& operator=(Status&& other) {
    if (this != &other) {
      if (IREE_UNLIKELY(value_)) iree_status_ignore(value_);
      value_ = exchange(other.value_, iree_status_from_code(other.code()));
    }
    return *this;
  }

  Status(iree_status_code_t code) : value_(iree_status_from_code(code)) {}
  Status& operator=(const iree_status_code_t& code) {
    if (IREE_UNLIKELY(value_)) iree_status_ignore(value_);
    value_ = iree_status_from_code(code);
    return *this;
  }

  Status(StatusCode code) : value_(iree_status_from_code(code)) {}
  Status& operator=(const StatusCode& code) {
    if (IREE_UNLIKELY(value_)) iree_status_ignore(value_);
    value_ = iree_status_from_code(code);
    return *this;
  }

  // Creates a status with the specified code and error message.
  // If `code` is kOk, `message` is ignored.
  Status(StatusCode code, const char* message) {
    if (IREE_UNLIKELY(code != StatusCode::kOk)) {
      value_ = (!message || !strlen(message))
                   ? iree_status_from_code(code)
                   : iree_status_allocate(static_cast<iree_status_code_t>(code),
                                          /*file=*/nullptr, /*line=*/0,
                                          iree_make_cstring_view(message));
    }
  }
  Status(StatusCode code, SourceLocation location, const char* message) {
    if (IREE_UNLIKELY(code != StatusCode::kOk)) {
      value_ = iree_status_allocate(static_cast<iree_status_code_t>(code),
                                    location.file_name(), location.line(),
                                    iree_make_cstring_view(message));
    }
  }

  ~Status() {
    if (IREE_UNLIKELY((uintptr_t)(value_) & ~IREE_STATUS_CODE_MASK)) {
      iree_status_free(value_);
    }
  }

  // Returns true if the Status is OK.
  IREE_MUST_USE_RESULT bool ok() const { return iree_status_is_ok(value_); }

  // Returns the error code.
  IREE_MUST_USE_RESULT StatusCode code() const {
    return static_cast<StatusCode>(iree_status_code(value_));
  }

  // Return a combination of the error code name and message.
  IREE_MUST_USE_RESULT std::string ToString() const {
    return Status::ToString(value_);
  }

  // Ignores any errors, potentially suppressing complaints from any tools.
  void IgnoreError() { value_ = iree_status_ignore(value_); }

  // Converts to a C API status instance and transfers ownership.
  IREE_MUST_USE_RESULT operator iree_status_t() && {
    return exchange(value_, iree_status_from_code(iree_status_code(value_)));
  }

  IREE_MUST_USE_RESULT iree_status_t release() {
    return exchange(value_, iree_ok_status());
  }

  friend bool operator==(const Status& lhs, const Status& rhs) {
    return lhs.code() == rhs.code();
  }
  friend bool operator!=(const Status& lhs, const Status& rhs) {
    return !(lhs == rhs);
  }

  friend bool operator==(const Status& lhs, const StatusCode& rhs) {
    return lhs.code() == rhs;
  }
  friend bool operator!=(const Status& lhs, const StatusCode& rhs) {
    return !(lhs == rhs);
  }

  friend bool operator==(const StatusCode& lhs, const Status& rhs) {
    return lhs == rhs.code();
  }
  friend bool operator!=(const StatusCode& lhs, const Status& rhs) {
    return !(lhs == rhs);
  }

 private:
  iree_status_t value_ = iree_ok_status();
};

// Returns an OK status, equivalent to a default constructed instance.
IREE_MUST_USE_RESULT static inline Status OkStatus() { return Status(); }

// Prints a human-readable representation of `x` to `os`.
std::ostream& operator<<(std::ostream& os, const Status& x);

IREE_MUST_USE_RESULT static inline bool IsOk(const Status& status) {
  return status.code() == StatusCode::kOk;
}

IREE_MUST_USE_RESULT static inline bool IsOk(const iree_status_t& status) {
  return iree_status_is_ok(status);
}

// TODO(#2843): better logging of status checks.
#undef IREE_CHECK_OK
#undef IREE_QCHECK_OK
#undef IREE_DCHECK_OK
#define IREE_CHECK_OK_IMPL(status, val) \
  auto status = ::iree::Status(val);    \
  IREE_CHECK(::iree::IsOk(status)) << (status)
#define IREE_CHECK_OK(val) \
  IREE_CHECK_OK_IMPL(IREE_STATUS_IMPL_CONCAT_(_status, __LINE__), val)

#define IREE_QCHECK_OK(val) IREE_QCHECK_EQ(::iree::StatusCode::kOk, (val))
#define IREE_DCHECK_OK(val) IREE_DCHECK_EQ(::iree::StatusCode::kOk, (val))

}  // namespace iree

#endif  // IREE_BASE_INTERNAL_STATUS_H_
