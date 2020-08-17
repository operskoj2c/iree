// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef IREE_BASE_STATUS_H_
#define IREE_BASE_STATUS_H_

#if defined(IREE_CONFIG_GOOGLE_INTERNAL) && IREE_CONFIG_GOOGLE_INTERNAL
#include "iree/base/google_internal/source_location_google.h"
#include "iree/base/google_internal/status_google.h"
#else
#include "iree/base/internal/source_location.h"
#include "iree/base/internal/status.h"
#include "iree/base/internal/status_builder.h"
#include "iree/base/internal/status_errno.h"
#include "iree/base/internal/status_errors.h"
#include "iree/base/internal/status_macros.h"
#include "iree/base/internal/status_win32_errors.h"
#include "iree/base/internal/statusor.h"
#endif  // IREE_CONFIG_GOOGLE_INTERNAL

#endif  // IREE_BASE_STATUS_H_
