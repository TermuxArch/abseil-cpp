// Copyright 2019 The Abseil Authors
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

#include "absl/strings/internal/cordz_info.h"

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/strings/cordz_test_helpers.h"
#include "absl/strings/internal/cord_rep_flat.h"
#include "absl/strings/internal/cordz_handle.h"
#include "absl/strings/internal/cordz_statistics.h"
#include "absl/strings/internal/cordz_update_tracker.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Ne;

// Used test values
auto constexpr kUnknownMethod = CordzUpdateTracker::kUnknown;
auto constexpr kTrackCordMethod = CordzUpdateTracker::kConstructorString;
auto constexpr kChildMethod = CordzUpdateTracker::kConstructorCord;
auto constexpr kUpdateMethod = CordzUpdateTracker::kAppendString;

// Local less verbose helper
std::vector<const CordzHandle*> DeleteQueue() {
  return CordzHandle::DiagnosticsGetDeleteQueue();
}

std::string FormatStack(absl::Span<void* const> raw_stack) {
  static constexpr size_t buf_size = 1 << 14;
  std::unique_ptr<char[]> buf(new char[buf_size]);
  std::string output;
  for (void* stackp : raw_stack) {
    if (absl::Symbolize(stackp, buf.get(), buf_size)) {
      absl::StrAppend(&output, "    ", buf.get(), "\n");
    }
  }
  return output;
}

TEST(CordzInfoTest, TrackCord) {
  TestCordData data;
  CordzInfo::TrackCord(data.data, kTrackCordMethod);
  CordzInfo* info = data.data.cordz_info();
  ASSERT_THAT(info, Ne(nullptr));
  EXPECT_FALSE(info->is_snapshot());
  EXPECT_THAT(CordzInfo::Head(CordzSnapshot()), Eq(info));
  EXPECT_THAT(info->GetCordRepForTesting(), Eq(data.rep.rep));
  CordzInfo::UntrackCord(info);
}

TEST(CordzInfoTest, UntrackCord) {
  TestCordData data;
  CordzInfo::TrackCord(data.data, kTrackCordMethod);
  CordzInfo* info = data.data.cordz_info();

  CordzSnapshot snapshot;
  CordzInfo::UntrackCord(info);
  EXPECT_THAT(CordzInfo::Head(CordzSnapshot()), Eq(nullptr));
  EXPECT_THAT(info->GetCordRepForTesting(), Eq(nullptr));
  EXPECT_THAT(DeleteQueue(), ElementsAre(info, &snapshot));
}

TEST(CordzInfoTest, SetCordRep) {
  TestCordData data;
  CordzInfo::TrackCord(data.data, kTrackCordMethod);
  CordzInfo* info = data.data.cordz_info();

  TestCordRep rep;
  info->Lock(CordzUpdateTracker::kAppendCord);
  info->SetCordRep(rep.rep);
  info->Unlock();
  EXPECT_THAT(info->GetCordRepForTesting(), Eq(rep.rep));

  CordzInfo::UntrackCord(info);
}

TEST(CordzInfoTest, SetCordRepNullUntracksCordOnUnlock) {
  TestCordData data;
  CordzInfo::TrackCord(data.data, kTrackCordMethod);
  CordzInfo* info = data.data.cordz_info();

  info->Lock(CordzUpdateTracker::kAppendString);
  info->SetCordRep(nullptr);
  EXPECT_THAT(info->GetCordRepForTesting(), Eq(nullptr));
  EXPECT_THAT(CordzInfo::Head(CordzSnapshot()), Eq(info));

  info->Unlock();
  EXPECT_THAT(CordzInfo::Head(CordzSnapshot()), Eq(nullptr));
}

#if GTEST_HAS_DEATH_TEST

TEST(CordzInfoTest, SetCordRepRequiresMutex) {
  TestCordData data;
  CordzInfo::TrackCord(data.data, kTrackCordMethod);
  CordzInfo* info = data.data.cordz_info();
  TestCordRep rep;
  EXPECT_DEBUG_DEATH(info->SetCordRep(rep.rep), ".*");
  CordzInfo::UntrackCord(info);
}

#endif  // GTEST_HAS_DEATH_TEST

TEST(CordzInfoTest, TrackUntrackHeadFirstV2) {
  CordzSnapshot snapshot;
  EXPECT_THAT(CordzInfo::Head(snapshot), Eq(nullptr));

  TestCordData data;
  CordzInfo::TrackCord(data.data, kTrackCordMethod);
  CordzInfo* info1 = data.data.cordz_info();
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(info1));
  EXPECT_THAT(info1->Next(snapshot), Eq(nullptr));

  TestCordData data2;
  CordzInfo::TrackCord(data2.data, kTrackCordMethod);
  CordzInfo* info2 = data2.data.cordz_info();
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(info2));
  EXPECT_THAT(info2->Next(snapshot), Eq(info1));
  EXPECT_THAT(info1->Next(snapshot), Eq(nullptr));

  CordzInfo::UntrackCord(info2);
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(info1));
  EXPECT_THAT(info1->Next(snapshot), Eq(nullptr));

  CordzInfo::UntrackCord(info1);
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(nullptr));
}

TEST(CordzInfoTest, TrackUntrackTailFirstV2) {
  CordzSnapshot snapshot;
  EXPECT_THAT(CordzInfo::Head(snapshot), Eq(nullptr));

  TestCordData data;
  CordzInfo::TrackCord(data.data, kTrackCordMethod);
  CordzInfo* info1 = data.data.cordz_info();
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(info1));
  EXPECT_THAT(info1->Next(snapshot), Eq(nullptr));

  TestCordData data2;
  CordzInfo::TrackCord(data2.data, kTrackCordMethod);
  CordzInfo* info2 = data2.data.cordz_info();
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(info2));
  EXPECT_THAT(info2->Next(snapshot), Eq(info1));
  EXPECT_THAT(info1->Next(snapshot), Eq(nullptr));

  CordzInfo::UntrackCord(info1);
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(info2));
  EXPECT_THAT(info2->Next(snapshot), Eq(nullptr));

  CordzInfo::UntrackCord(info2);
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(nullptr));
}

TEST(CordzInfoTest, StackV2) {
  TestCordData data;
  // kMaxStackDepth is intentionally less than 64 (which is the max depth that
  // Cordz will record) because if the actual stack depth is over 64
  // (which it is on Apple platforms) then the expected_stack will end up
  // catching a few frames at the end that the actual_stack didn't get and
  // it will no longer be subset. At the time of this writing 58 is the max
  // that will allow this test to pass (with a minimum os version of iOS 9), so
  // rounded down to 50 to hopefully not run into this in the future if Apple
  // makes small modifications to its testing stack. 50 is sufficient to prove
  // that we got a decent stack.
  static constexpr int kMaxStackDepth = 50;
  CordzInfo::TrackCord(data.data, kTrackCordMethod);
  CordzInfo* info = data.data.cordz_info();
  std::vector<void*> local_stack;
  local_stack.resize(kMaxStackDepth);
  // In some environments we don't get stack traces. For example in Android
  // absl::GetStackTrace will return 0 indicating it didn't find any stack. The
  // resultant formatted stack will be "", but that still equals the stack
  // recorded in CordzInfo, which is also empty. The skip_count is 1 so that the
  // line number of the current stack isn't included in the HasSubstr check.
  local_stack.resize(absl::GetStackTrace(local_stack.data(), kMaxStackDepth,
                                         /*skip_count=*/1));

  std::string got_stack = FormatStack(info->GetStack());
  std::string expected_stack = FormatStack(local_stack);
  // If TrackCord is inlined, got_stack should match expected_stack. If it isn't
  // inlined, got_stack should include an additional frame not present in
  // expected_stack. Either way, expected_stack should be a substring of
  // got_stack.
  EXPECT_THAT(got_stack, HasSubstr(expected_stack));

  CordzInfo::UntrackCord(info);
}

// Local helper functions to get different stacks for child and parent.
CordzInfo* TrackChildCord(InlineData& data, const InlineData& parent) {
  CordzInfo::TrackCord(data, parent, kChildMethod);
  return data.cordz_info();
}
CordzInfo* TrackParentCord(InlineData& data) {
  CordzInfo::TrackCord(data, kTrackCordMethod);
  return data.cordz_info();
}

TEST(CordzInfoTest, GetStatistics) {
  TestCordData data;
  CordzInfo* info = TrackParentCord(data.data);

  CordzStatistics statistics = info->GetCordzStatistics();
  EXPECT_THAT(statistics.size, Eq(data.rep.rep->length));
  EXPECT_THAT(statistics.method, Eq(kTrackCordMethod));
  EXPECT_THAT(statistics.parent_method, Eq(kUnknownMethod));
  EXPECT_THAT(statistics.update_tracker.Value(kTrackCordMethod), Eq(1));

  CordzInfo::UntrackCord(info);
}

TEST(CordzInfoTest, LockCountsMethod) {
  TestCordData data;
  CordzInfo* info = TrackParentCord(data.data);

  info->Lock(kUpdateMethod);
  info->Unlock();
  info->Lock(kUpdateMethod);
  info->Unlock();

  CordzStatistics statistics = info->GetCordzStatistics();
  EXPECT_THAT(statistics.update_tracker.Value(kUpdateMethod), Eq(2));

  CordzInfo::UntrackCord(info);
}

TEST(CordzInfoTest, FromParent) {
  TestCordData parent;
  TestCordData child;
  CordzInfo* info_parent = TrackParentCord(parent.data);
  CordzInfo* info_child = TrackChildCord(child.data, parent.data);

  std::string stack = FormatStack(info_parent->GetStack());
  std::string parent_stack = FormatStack(info_child->GetParentStack());
  EXPECT_THAT(stack, Eq(parent_stack));

  CordzStatistics statistics = info_child->GetCordzStatistics();
  EXPECT_THAT(statistics.size, Eq(child.rep.rep->length));
  EXPECT_THAT(statistics.method, Eq(kChildMethod));
  EXPECT_THAT(statistics.parent_method, Eq(kTrackCordMethod));
  EXPECT_THAT(statistics.update_tracker.Value(kChildMethod), Eq(1));

  CordzInfo::UntrackCord(info_parent);
  CordzInfo::UntrackCord(info_child);
}

TEST(CordzInfoTest, FromParentInlined) {
  InlineData parent;
  TestCordData child;
  CordzInfo* info = TrackChildCord(child.data, parent);
  EXPECT_TRUE(info->GetParentStack().empty());
  CordzStatistics statistics = info->GetCordzStatistics();
  EXPECT_THAT(statistics.size, Eq(child.rep.rep->length));
  EXPECT_THAT(statistics.method, Eq(kChildMethod));
  EXPECT_THAT(statistics.parent_method, Eq(kUnknownMethod));
  EXPECT_THAT(statistics.update_tracker.Value(kChildMethod), Eq(1));
  CordzInfo::UntrackCord(info);
}

TEST(CordzInfoTest, RecordMetrics) {
  TestCordData data;
  CordzInfo* info = TrackParentCord(data.data);

  CordzStatistics expected;
  expected.size = 100;
  info->RecordMetrics(expected.size);

  CordzStatistics actual = info->GetCordzStatistics();
  EXPECT_EQ(actual.size, expected.size);

  CordzInfo::UntrackCord(info);
}

}  // namespace
}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
