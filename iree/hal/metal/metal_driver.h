// Copyright 2020 Google LLC
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

#ifndef IREE_HAL_METAL_METAL_DRIVER_H_
#define IREE_HAL_METAL_METAL_DRIVER_H_

#include <memory>
#include <string>

#include "iree/hal/cc/driver.h"

namespace iree {
namespace hal {
namespace metal {

struct MetalDriverOptions {
  // Whether to enable Metal command capture.
  bool enable_capture;
  // The file to contain the Metal capture. Empty means capturing to Xcode.
  std::string capture_file;
};

// A pseudo Metal GPU driver which retains all available Metal GPU devices
// during its lifetime.
//
// It uses the iree_hal_device_id_t to store the underlying id<MTLDevice>.
class MetalDriver final : public Driver {
 public:
  static StatusOr<ref_ptr<MetalDriver>> Create(
      const MetalDriverOptions& options);

  ~MetalDriver() override;

  StatusOr<std::vector<DeviceInfo>> EnumerateAvailableDevices() override;

  StatusOr<ref_ptr<Device>> CreateDefaultDevice() override;

  StatusOr<ref_ptr<Device>> CreateDevice(
      iree_hal_device_id_t device_id) override;

 private:
  MetalDriver(std::vector<DeviceInfo> devices);

  std::vector<DeviceInfo> devices_;
};

}  // namespace metal
}  // namespace hal
}  // namespace iree

#endif  // IREE_HAL_METAL_METAL_DRIVER_H_
