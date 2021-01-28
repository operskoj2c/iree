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

#ifndef IREE_HAL_VMLA_VMLA_DRIVER_H_
#define IREE_HAL_VMLA_VMLA_DRIVER_H_

#include "iree/hal/cc/driver.h"
#include "iree/vm/api.h"

namespace iree {
namespace hal {
namespace vmla {

class VMLADriver final : public Driver {
 public:
  static StatusOr<ref_ptr<Driver>> Create();

  VMLADriver(iree_vm_instance_t* instance, iree_vm_module_t* vmla_module);
  ~VMLADriver() override;

  StatusOr<std::vector<DeviceInfo>> EnumerateAvailableDevices() override;

  StatusOr<ref_ptr<Device>> CreateDefaultDevice() override;

  StatusOr<ref_ptr<Device>> CreateDevice(
      iree_hal_device_id_t device_id) override;

 private:
  iree_vm_instance_t* instance_ = nullptr;
  iree_vm_module_t* vmla_module_ = nullptr;
};

}  // namespace vmla
}  // namespace hal
}  // namespace iree

#endif  // IREE_HAL_VMLA_VMLA_DRIVER_H_
