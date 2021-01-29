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

#include "iree/task/topology.h"

#include <cpuinfo.h>

#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace {

using namespace iree::testing::status;

TEST(TopologyTest, Lifetime) {
  iree_task_topology_t topology;
  iree_task_topology_initialize(&topology);
  EXPECT_GT(iree_task_topology_group_capacity(&topology), 0);
  EXPECT_EQ(0, iree_task_topology_group_count(&topology));
  iree_task_topology_deinitialize(&topology);
}

TEST(TopologyTest, Empty) {
  iree_task_topology_t topology;
  iree_task_topology_initialize(&topology);

  EXPECT_EQ(0, iree_task_topology_group_count(&topology));
  EXPECT_EQ(NULL, iree_task_topology_get_group(&topology, 0));
  EXPECT_EQ(NULL, iree_task_topology_get_group(&topology, 100));

  iree_task_topology_deinitialize(&topology);
}

TEST(TopologyTest, Parsing) {
  // TODO(benvanik): implement parsing.
}

TEST(TopologyTest, Formatting) {
  // TODO(benvanik): implement formatting.
}

TEST(TopologyTest, Construction) {
  iree_task_topology_t topology;
  iree_task_topology_initialize(&topology);

  EXPECT_EQ(0, iree_task_topology_group_count(&topology));

  for (iree_host_size_t i = 0; i < 8; ++i) {
    iree_task_topology_group_t group;
    iree_task_topology_group_initialize(i, &group);
    IREE_EXPECT_OK(iree_task_topology_push_group(&topology, &group));
    EXPECT_EQ(i + 1, iree_task_topology_group_count(&topology));
  }
  EXPECT_EQ(8, iree_task_topology_group_count(&topology));

  for (iree_host_size_t i = 0; i < 8; ++i) {
    const iree_task_topology_group_t* group =
        iree_task_topology_get_group(&topology, i);
    EXPECT_EQ(i, group->group_index);
  }

  iree_task_topology_deinitialize(&topology);
}

TEST(TopologyTest, MaxCapacity) {
  iree_task_topology_t topology;
  iree_task_topology_initialize(&topology);

  EXPECT_EQ(0, iree_task_topology_group_count(&topology));

  // Fill up to capacity.
  for (iree_host_size_t i = 0; i < iree_task_topology_group_capacity(&topology);
       ++i) {
    iree_task_topology_group_t group;
    iree_task_topology_group_initialize(i, &group);
    IREE_EXPECT_OK(iree_task_topology_push_group(&topology, &group));
    EXPECT_EQ(i + 1, iree_task_topology_group_count(&topology));
  }
  EXPECT_EQ(iree_task_topology_group_capacity(&topology),
            iree_task_topology_group_count(&topology));

  // Try adding one more - it should it fail because we are at capacity.
  iree_task_topology_group_t extra_group;
  iree_task_topology_group_initialize(UINT8_MAX, &extra_group);
  iree_status_t status = iree_task_topology_push_group(&topology, &extra_group);
  EXPECT_TRUE(iree_status_is_resource_exhausted(status));
  iree_status_ignore(status);

  // Confirm that the only groups we have are the valid ones we added above.
  for (iree_host_size_t i = 0; i < 8; ++i) {
    const iree_task_topology_group_t* group =
        iree_task_topology_get_group(&topology, i);
    EXPECT_EQ(i, group->group_index);
  }

  iree_task_topology_deinitialize(&topology);
}

TEST(TopologyTest, FromGroupCount) {
  static constexpr iree_host_size_t kGroupCount = 4;
  iree_task_topology_t topology;
  iree_task_topology_initialize(&topology);

  iree_task_topology_initialize_from_group_count(kGroupCount, &topology);
  EXPECT_LE(iree_task_topology_group_count(&topology),
            iree_task_topology_group_capacity(&topology));
  EXPECT_EQ(iree_task_topology_group_count(&topology), kGroupCount);
  for (iree_host_size_t i = 0; i < kGroupCount; ++i) {
    const iree_task_topology_group_t* group =
        iree_task_topology_get_group(&topology, i);
    EXPECT_EQ(i, group->group_index);
  }

  iree_task_topology_deinitialize(&topology);
}

// Verifies only that the |topology| is usable.
// If we actually checked the contents here then we'd just be validating that
// cpuinfo was working and the tests would become machine-dependent.
static void EnsureTopologyValid(iree_host_size_t max_group_count,
                                iree_task_topology_t* topology) {
  EXPECT_LE(iree_task_topology_group_count(topology),
            iree_task_topology_group_capacity(topology));
  EXPECT_LE(iree_task_topology_group_count(topology), max_group_count);
  EXPECT_GE(1, iree_task_topology_group_count(topology));
  for (iree_host_size_t i = 0; i < iree_task_topology_group_count(topology);
       ++i) {
    const iree_task_topology_group_t* group =
        iree_task_topology_get_group(topology, i);
    EXPECT_EQ(i, group->group_index);
  }
}

TEST(TopologyTest, FromPhysicalCores) {
  static constexpr iree_host_size_t kMaxGroupCount = 4;
  iree_task_topology_t topology;
  iree_task_topology_initialize(&topology);
  iree_task_topology_initialize_from_physical_cores(kMaxGroupCount, &topology);
  EnsureTopologyValid(kMaxGroupCount, &topology);
  iree_task_topology_deinitialize(&topology);
}

}  // namespace
