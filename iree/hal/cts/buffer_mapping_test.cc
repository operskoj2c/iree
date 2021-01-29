// Copyright 2021 Google LLC
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

#include "iree/hal/cts/cts_test_base.h"
#include "iree/hal/testing/driver_registry.h"
#include "iree/testing/gtest.h"

namespace iree {
namespace hal {
namespace cts {

using ::testing::ContainerEq;

namespace {

constexpr iree_device_size_t kAllocationSize = 1024;

}  // namespace

class BufferMappingTest : public CtsTestBase {};

// TODO(scotttodd): move this check to SetUp() and skip tests if not supported
//   or add general support for optional features/tests into the CTS framework?
TEST_P(BufferMappingTest, AllocatorSupportsBufferMapping) {
  iree_hal_memory_type_t memory_type = IREE_HAL_MEMORY_TYPE_HOST_VISIBLE;
  iree_hal_buffer_usage_t buffer_usage = IREE_HAL_BUFFER_USAGE_MAPPING;

  iree_hal_buffer_compatibility_t compatibility =
      iree_hal_allocator_query_buffer_compatibility(
          device_allocator_, memory_type,
          /*allowed_usage=*/buffer_usage,
          /*intended_usage=*/buffer_usage, kAllocationSize);
  EXPECT_TRUE(iree_all_bits_set(compatibility,
                                IREE_HAL_BUFFER_COMPATIBILITY_ALLOCATABLE));

  iree_hal_buffer_t* buffer;
  IREE_ASSERT_OK(iree_hal_allocator_allocate_buffer(
      device_allocator_, memory_type, buffer_usage, kAllocationSize, &buffer));

  EXPECT_EQ(device_allocator_, iree_hal_buffer_allocator(buffer));
  EXPECT_TRUE(
      iree_all_bits_set(iree_hal_buffer_memory_type(buffer), memory_type));
  EXPECT_TRUE(
      iree_all_bits_set(iree_hal_buffer_allowed_usage(buffer), buffer_usage));
  EXPECT_GE(iree_hal_buffer_allocation_size(buffer), kAllocationSize);

  iree_hal_buffer_release(buffer);
}

TEST_P(BufferMappingTest, Zero) {
  iree_hal_memory_type_t memory_type = IREE_HAL_MEMORY_TYPE_HOST_VISIBLE;
  iree_hal_buffer_usage_t buffer_usage = IREE_HAL_BUFFER_USAGE_MAPPING;

  iree_hal_buffer_t* buffer;
  IREE_ASSERT_OK(iree_hal_allocator_allocate_buffer(
      device_allocator_, memory_type, buffer_usage, kAllocationSize, &buffer));

  IREE_ASSERT_OK(iree_hal_buffer_zero(buffer, /*byte_offset=*/0,
                                      /*byte_length=*/kAllocationSize));

  std::vector<uint8_t> reference_buffer(kAllocationSize);
  std::memset(reference_buffer.data(), 0, kAllocationSize);

  std::vector<uint8_t> actual_data(kAllocationSize);
  IREE_ASSERT_OK(iree_hal_buffer_read_data(
      buffer, /*source_offset=*/0, actual_data.data(), actual_data.size()));
  EXPECT_THAT(actual_data, ContainerEq(reference_buffer));

  iree_hal_buffer_release(buffer);
}

TEST_P(BufferMappingTest, FillEmpty) {
  iree_hal_memory_type_t memory_type = IREE_HAL_MEMORY_TYPE_HOST_VISIBLE;
  iree_hal_buffer_usage_t buffer_usage = IREE_HAL_BUFFER_USAGE_MAPPING;

  iree_hal_buffer_t* buffer;
  IREE_ASSERT_OK(iree_hal_allocator_allocate_buffer(
      device_allocator_, memory_type, buffer_usage, kAllocationSize, &buffer));

  IREE_ASSERT_OK(iree_hal_buffer_zero(buffer, /*byte_offset=*/0,
                                      /*byte_length=*/kAllocationSize));
  uint8_t fill_value = 0x07;
  IREE_ASSERT_OK(iree_hal_buffer_fill(buffer, /*byte_offset=*/0,
                                      /*byte_length=*/0,  // <---- empty!
                                      /*pattern=*/&fill_value,
                                      /*pattern_length=*/sizeof(fill_value)));

  // Note: reference is all zeros, since fill byte length is 0!
  std::vector<uint8_t> reference_buffer(kAllocationSize);
  std::memset(reference_buffer.data(), 0, kAllocationSize);

  std::vector<uint8_t> actual_data(kAllocationSize);
  IREE_ASSERT_OK(iree_hal_buffer_read_data(
      buffer, /*source_offset=*/0, actual_data.data(), actual_data.size()));
  EXPECT_THAT(actual_data, ContainerEq(reference_buffer));

  iree_hal_buffer_release(buffer);
}

TEST_P(BufferMappingTest, Fill) {
  iree_hal_memory_type_t memory_type = IREE_HAL_MEMORY_TYPE_HOST_VISIBLE;
  iree_hal_buffer_usage_t buffer_usage = IREE_HAL_BUFFER_USAGE_MAPPING;

  iree_hal_buffer_t* buffer;
  IREE_ASSERT_OK(iree_hal_allocator_allocate_buffer(
      device_allocator_, memory_type, buffer_usage, kAllocationSize, &buffer));

  uint8_t fill_value = 0x07;
  IREE_ASSERT_OK(iree_hal_buffer_fill(buffer, /*byte_offset=*/0,
                                      /*byte_length=*/kAllocationSize,
                                      /*pattern=*/&fill_value,
                                      /*pattern_length=*/sizeof(fill_value)));

  std::vector<uint8_t> reference_buffer(kAllocationSize);
  std::memset(reference_buffer.data(), fill_value, kAllocationSize);

  std::vector<uint8_t> actual_data(kAllocationSize);
  IREE_ASSERT_OK(iree_hal_buffer_read_data(
      buffer, /*source_offset=*/0, actual_data.data(), actual_data.size()));
  EXPECT_THAT(actual_data, ContainerEq(reference_buffer));

  iree_hal_buffer_release(buffer);
}

TEST_P(BufferMappingTest, Write) {
  iree_hal_memory_type_t memory_type = IREE_HAL_MEMORY_TYPE_HOST_VISIBLE;
  iree_hal_buffer_usage_t buffer_usage = IREE_HAL_BUFFER_USAGE_MAPPING;

  iree_hal_buffer_t* buffer;
  IREE_ASSERT_OK(iree_hal_allocator_allocate_buffer(
      device_allocator_, memory_type, buffer_usage, kAllocationSize, &buffer));

  uint8_t fill_value = 0x07;
  std::vector<uint8_t> reference_buffer(kAllocationSize);
  std::memset(reference_buffer.data(), fill_value, kAllocationSize);
  IREE_ASSERT_OK(iree_hal_buffer_write_data(buffer, /*target_offset=*/0,
                                            reference_buffer.data(),
                                            reference_buffer.size()));

  std::vector<uint8_t> actual_data(kAllocationSize);
  IREE_ASSERT_OK(iree_hal_buffer_read_data(
      buffer, /*source_offset=*/0, actual_data.data(), actual_data.size()));
  EXPECT_THAT(actual_data, ContainerEq(reference_buffer));

  iree_hal_buffer_release(buffer);
}
TEST_P(BufferMappingTest, Copy) {
  iree_hal_memory_type_t memory_type = IREE_HAL_MEMORY_TYPE_HOST_VISIBLE;
  iree_hal_buffer_usage_t buffer_usage = IREE_HAL_BUFFER_USAGE_MAPPING;

  iree_hal_buffer_t* buffer_a;
  iree_hal_buffer_t* buffer_b;
  IREE_ASSERT_OK(iree_hal_allocator_allocate_buffer(
      device_allocator_, memory_type, buffer_usage, kAllocationSize,
      &buffer_a));
  IREE_ASSERT_OK(iree_hal_allocator_allocate_buffer(
      device_allocator_, memory_type, buffer_usage, kAllocationSize,
      &buffer_b));

  uint8_t fill_value = 0x07;
  IREE_ASSERT_OK(iree_hal_buffer_fill(buffer_a, /*byte_offset=*/0,
                                      /*byte_length=*/kAllocationSize,
                                      /*pattern=*/&fill_value,
                                      /*pattern_length=*/sizeof(fill_value)));
  IREE_ASSERT_OK(iree_hal_buffer_copy_data(
      /*source_buffer=*/buffer_a,
      /*source_offset=*/0, /*target_buffer=*/buffer_b, /*target_offset=*/0,
      /*data_length=*/kAllocationSize));

  std::vector<uint8_t> reference_buffer(kAllocationSize);
  std::memset(reference_buffer.data(), fill_value, kAllocationSize);

  std::vector<uint8_t> actual_data(kAllocationSize);
  IREE_ASSERT_OK(iree_hal_buffer_read_data(
      buffer_b, /*source_offset=*/0, actual_data.data(), actual_data.size()));
  EXPECT_THAT(actual_data, ContainerEq(reference_buffer));

  iree_hal_buffer_release(buffer_a);
  iree_hal_buffer_release(buffer_b);
}

// TODO(scotttodd): iree_hal_allocator_wrap_buffer
// TODO(scotttodd): iree_hal_heap_buffer_wrap
// TODO(scotttodd): iree_hal_buffer_map_range
// TODO(scotttodd): revive old tests:
//   https://github.com/google/iree/blob/440edee8a3190d73dbceb24986eed847cac8bd31/iree/hal/buffer_mapping_test.cc

INSTANTIATE_TEST_SUITE_P(
    AllDrivers, BufferMappingTest,
    ::testing::ValuesIn(testing::EnumerateAvailableDrivers()),
    GenerateTestName());

}  // namespace cts
}  // namespace hal
}  // namespace iree
