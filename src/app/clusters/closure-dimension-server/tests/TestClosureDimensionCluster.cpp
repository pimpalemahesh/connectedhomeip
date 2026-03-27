/**
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <lib/core/StringBuilderAdapters.h>
#include <pw_unit_test/framework.h>

#include <app/clusters/closure-dimension-server/ClosureDimensionCluster.h>
#include <app/clusters/closure-dimension-server/ClosureDimensionClusterDelegate.h>
#include <app/clusters/closure-dimension-server/GenericDimensionState.h>
#include <app/server-cluster/testing/AttributeTesting.h>
#include <app/server-cluster/testing/ClusterTester.h>
#include <app/server-cluster/testing/TestServerClusterContext.h>
#include <app/server-cluster/testing/ValidateGlobalAttributes.h>
#include <clusters/ClosureDimension/Attributes.h>
#include <clusters/ClosureDimension/Metadata.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/TimerDelegateMock.h>
#include <platform/CHIPDeviceLayer.h>
#include <vector>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters::ClosureDimension;
using namespace chip::app::Clusters;
using namespace chip::Testing;

using Status = chip::Protocols::InteractionModel::Status;

namespace {

TimerDelegateMock mockTimerDelegate;

class MockDelegate : public ClosureDimensionClusterDelegate
{
public:
    ~MockDelegate() override = default;

    Status HandleSetTarget(const Optional<Percent100ths> & position, const Optional<bool> & latch,
                           const Optional<Globals::ThreeLevelAutoEnum> & speed) override
    {
        ++setTargetCalls;
        lastSetTargetPosition = position;
        lastSetTargetLatch    = latch;
        lastSetTargetSpeed    = speed;
        return setTargetStatus;
    }

    Status HandleStep(const StepDirectionEnum & direction, const uint16_t & numberOfSteps,
                      const Optional<Globals::ThreeLevelAutoEnum> & speed) override
    {
        ++stepCalls;
        lastStepDirection     = direction;
        lastStepNumberOfSteps = numberOfSteps;
        lastStepSpeed         = speed;
        return stepStatus;
    }

    void Reset()
    {
        setTargetCalls        = 0;
        stepCalls             = 0;
        setTargetStatus       = Status::Success;
        stepStatus            = Status::Success;
        lastSetTargetPosition = NullOptional;
        lastSetTargetLatch    = NullOptional;
        lastSetTargetSpeed    = NullOptional;
        lastStepDirection     = StepDirectionEnum::kUnknownEnumValue;
        lastStepNumberOfSteps = 0;
        lastStepSpeed         = NullOptional;
    }

    Status setTargetStatus = Status::Success;
    Status stepStatus      = Status::Success;
    int setTargetCalls     = 0;
    int stepCalls          = 0;

    Optional<Percent100ths> lastSetTargetPosition;
    Optional<bool> lastSetTargetLatch;
    Optional<Globals::ThreeLevelAutoEnum> lastSetTargetSpeed;

    StepDirectionEnum lastStepDirection = StepDirectionEnum::kUnknownEnumValue;
    uint16_t lastStepNumberOfSteps      = 0;
    Optional<Globals::ThreeLevelAutoEnum> lastStepSpeed;
};

class MockClusterConformance : public ClusterConformance
{
public:
    MockClusterConformance() { FeatureMap().Set(Feature::kPositioning); }
};

DataModel::Nullable<GenericDimensionStateStruct> PositionState(Percent100ths position)
{
    return DataModel::Nullable<GenericDimensionStateStruct>(
        GenericDimensionStateStruct(Optional(DataModel::MakeNullable(position)), NullOptional, NullOptional));
}

DataModel::Nullable<GenericDimensionStateStruct> PositionLatchSpeedState(Percent100ths position, bool latched,
                                                                         Optional<Globals::ThreeLevelAutoEnum> speed)
{
    return DataModel::Nullable<GenericDimensionStateStruct>(GenericDimensionStateStruct(
        Optional(DataModel::MakeNullable(position)), Optional(DataModel::MakeNullable(latched)), speed));
}

class TestClosureDimensionCluster : public ::testing::Test
{
public:
    TestClosureDimensionCluster() :
        mCluster(kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, mockConformance, initParams }),
        mClusterTester(mCluster)
    {}

    static void SetUpTestSuite() { ASSERT_EQ(Platform::MemoryInit(), CHIP_NO_ERROR); }

    static void TearDownTestSuite() { Platform::MemoryShutdown(); }

    void SetUp() override
    {
        ASSERT_EQ(mCluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
        ASSERT_EQ(mCluster.SetResolution(100), CHIP_NO_ERROR);
        ASSERT_EQ(mCluster.SetStepValue(1000), CHIP_NO_ERROR);
        ASSERT_EQ(mCluster.SetCurrentState(PositionState(5000)), CHIP_NO_ERROR);
    }

    void TearDown() override
    {
        mCluster.Shutdown(ClusterShutdownType::kClusterShutdown);
        mockDelegate.Reset();
    }

    MockDelegate mockDelegate;
    MockClusterConformance mockConformance;
    ClusterInitParameters initParams{};
    const EndpointId kTestEndpointId = 1;
    ClosureDimensionCluster mCluster;
    ClusterTester mClusterTester;
};

} // namespace

TEST_F(TestClosureDimensionCluster, TestAttributesList)
{
    std::vector<DataModel::AttributeEntry> expectedAttributes(ClosureDimension::Attributes::kMandatoryMetadata.begin(),
                                                              ClosureDimension::Attributes::kMandatoryMetadata.end());
    expectedAttributes.push_back(ClosureDimension::Attributes::Resolution::kMetadataEntry);
    expectedAttributes.push_back(ClosureDimension::Attributes::StepValue::kMetadataEntry);
    EXPECT_TRUE(IsAttributesListEqualTo(mCluster, expectedAttributes));

    MockClusterConformance latchConformance;
    latchConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kMotionLatching);
    ClosureDimensionCluster latchCluster(
        kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, latchConformance, initParams });
    expectedAttributes.push_back(ClosureDimension::Attributes::LatchControlModes::kMetadataEntry);
    EXPECT_TRUE(IsAttributesListEqualTo(latchCluster, expectedAttributes));

    MockClusterConformance unitConformance;
    unitConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kUnit);
    ClosureDimensionCluster unitCluster(
        kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, unitConformance, initParams });
    std::vector<DataModel::AttributeEntry> unitExpected(ClosureDimension::Attributes::kMandatoryMetadata.begin(),
                                                        ClosureDimension::Attributes::kMandatoryMetadata.end());
    unitExpected.push_back(ClosureDimension::Attributes::Resolution::kMetadataEntry);
    unitExpected.push_back(ClosureDimension::Attributes::StepValue::kMetadataEntry);
    unitExpected.push_back(ClosureDimension::Attributes::Unit::kMetadataEntry);
    unitExpected.push_back(ClosureDimension::Attributes::UnitRange::kMetadataEntry);
    EXPECT_TRUE(IsAttributesListEqualTo(unitCluster, unitExpected));
}

TEST_F(TestClosureDimensionCluster, TestMandatoryAcceptedCommands)
{
    EXPECT_TRUE(IsAcceptedCommandsListEqualTo(mCluster,
                                              {
                                                  ClosureDimension::Commands::SetTarget::kMetadataEntry,
                                                  ClosureDimension::Commands::Step::kMetadataEntry,
                                              }));
}

TEST_F(TestClosureDimensionCluster, TestAcceptedCommandsSetTargetOnlyWithoutPositioning)
{
    MockClusterConformance latchOnly;
    latchOnly.FeatureMap().ClearAll().Set(Feature::kMotionLatching);
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, latchOnly, initParams });
    EXPECT_TRUE(IsAcceptedCommandsListEqualTo(cluster,
                                              {
                                                  ClosureDimension::Commands::SetTarget::kMetadataEntry,
                                              }));
}

TEST_F(TestClosureDimensionCluster, TestReadClusterRevision)
{
    uint16_t clusterRevision = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::ClusterRevision::Id, clusterRevision), CHIP_NO_ERROR);
    EXPECT_EQ(clusterRevision, kRevision);
}

TEST_F(TestClosureDimensionCluster, TestReadFeatureMap)
{
    BitFlags<Feature> featureMap;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::FeatureMap::Id, featureMap), CHIP_NO_ERROR);
    EXPECT_EQ(featureMap, mockConformance.FeatureMap());
}

TEST_F(TestClosureDimensionCluster, TestSetCurrentStateFeatureValidation)
{
    EXPECT_EQ(mCluster.SetCurrentState(PositionState(5100)), CHIP_NO_ERROR);
    EXPECT_EQ(mCluster.GetCurrentState(), PositionState(5100));

    MockClusterConformance speedConformance;
    speedConformance.FeatureMap().ClearAll().Set(Feature::kPositioning).Set(Feature::kSpeed);
    ClosureDimensionCluster cluster(
        kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, speedConformance, initParams });
    ASSERT_EQ(cluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);

    DataModel::Nullable<GenericDimensionStateStruct> withSpeed(GenericDimensionStateStruct(
        Optional(DataModel::MakeNullable<Percent100ths>(5000)), NullOptional, Optional(Globals::ThreeLevelAutoEnum::kLow)));
    EXPECT_EQ(cluster.SetCurrentState(withSpeed), CHIP_NO_ERROR);

    mockDelegate.Reset();
    DataModel::Nullable<GenericDimensionStateStruct> invalidSpeed(
        GenericDimensionStateStruct(Optional(DataModel::MakeNullable<Percent100ths>(5000)), NullOptional,
                                    Optional(Globals::ThreeLevelAutoEnum::kUnknownEnumValue)));
    EXPECT_EQ(cluster.SetCurrentState(invalidSpeed), CHIP_ERROR_INVALID_ARGUMENT);

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestSetTargetStateFeatureValidation)
{
    EXPECT_EQ(mCluster.SetTargetState(PositionState(6000)), CHIP_NO_ERROR);
    EXPECT_EQ(mCluster.GetTargetState(), PositionState(6000));

    MockClusterConformance speedConformance;
    speedConformance.FeatureMap().ClearAll().Set(Feature::kPositioning).Set(Feature::kSpeed);
    ClosureDimensionCluster cluster(
        kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, speedConformance, initParams });
    ASSERT_EQ(cluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);

    DataModel::Nullable<GenericDimensionStateStruct> withSpeed(GenericDimensionStateStruct(
        Optional(DataModel::MakeNullable<Percent100ths>(5000)), NullOptional, Optional(Globals::ThreeLevelAutoEnum::kHigh)));
    EXPECT_EQ(cluster.SetTargetState(withSpeed), CHIP_NO_ERROR);

    mockDelegate.Reset();
    DataModel::Nullable<GenericDimensionStateStruct> invalidSpeed(
        GenericDimensionStateStruct(Optional(DataModel::MakeNullable<Percent100ths>(5000)), NullOptional,
                                    Optional(Globals::ThreeLevelAutoEnum::kUnknownEnumValue)));
    EXPECT_EQ(cluster.SetTargetState(invalidSpeed), CHIP_ERROR_INVALID_ARGUMENT);

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestSetResolutionAndStepValue)
{
    EXPECT_EQ(mCluster.SetResolution(200), CHIP_NO_ERROR);
    EXPECT_EQ(mCluster.GetResolution(), static_cast<Percent100ths>(200));
    EXPECT_EQ(mCluster.SetStepValue(2000), CHIP_NO_ERROR);
    EXPECT_EQ(mCluster.GetStepValue(), static_cast<Percent100ths>(2000));

    EXPECT_EQ(mCluster.SetStepValue(2001), CHIP_ERROR_INVALID_ARGUMENT);
}

TEST_F(TestClosureDimensionCluster, TestSetUnitAndUnitRange)
{
    MockClusterConformance unitConformance;
    unitConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kUnit);
    ClosureDimensionCluster cluster(
        kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, unitConformance, initParams });
    ASSERT_EQ(cluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);

    EXPECT_EQ(cluster.SetUnit(ClosureUnitEnum::kMillimeter), CHIP_NO_ERROR);
    Structs::UnitRangeStruct::Type range{ .min = 0, .max = 1000 };
    EXPECT_EQ(cluster.SetUnitRange(DataModel::MakeNullable(range)), CHIP_NO_ERROR);
    EXPECT_EQ(cluster.GetUnit(), ClosureUnitEnum::kMillimeter);
    EXPECT_FALSE(cluster.GetUnitRange().IsNull());
    EXPECT_EQ(cluster.GetUnitRange().Value().min, 0);
    EXPECT_EQ(cluster.GetUnitRange().Value().max, 1000);

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestSetLimitRange)
{
    MockClusterConformance limitConformance;
    limitConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kLimitation);
    ClosureDimensionCluster cluster(
        kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, limitConformance, initParams });
    ASSERT_EQ(cluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);

    Structs::RangePercent100thsStruct::Type lr{ .min = static_cast<Percent100ths>(0), .max = static_cast<Percent100ths>(10000) };
    EXPECT_EQ(cluster.SetLimitRange(lr), CHIP_NO_ERROR);
    EXPECT_EQ(cluster.GetLimitRange().min, lr.min);
    EXPECT_EQ(cluster.GetLimitRange().max, lr.max);

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestSetLatchControlModes)
{
    MockClusterConformance latchConformance;
    latchConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kMotionLatching);
    ClosureDimensionCluster cluster(
        kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, latchConformance, initParams });
    ASSERT_EQ(cluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);

    BitFlags<LatchControlModesBitmap> modes;
    modes.Set(LatchControlModesBitmap::kRemoteLatching).Set(LatchControlModesBitmap::kRemoteUnlatching);
    EXPECT_EQ(cluster.SetLatchControlModes(modes), CHIP_NO_ERROR);
    EXPECT_EQ(cluster.GetLatchControlModes(), modes);

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestTranslationDirectionInit)
{
    MockClusterConformance tr;
    tr.FeatureMap().Set(Feature::kPositioning).Set(Feature::kTranslation);
    ClusterInitParameters params{};
    params.translationDirection = TranslationDirectionEnum::kUpward;
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, tr, params });
    ASSERT_EQ(cluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    EXPECT_EQ(cluster.GetTranslationDirection(), TranslationDirectionEnum::kUpward);
    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestRotationAxisAndOverflow)
{
    MockClusterConformance rot;
    rot.FeatureMap().Set(Feature::kPositioning).Set(Feature::kRotation);
    ClusterInitParameters params{};
    params.rotationAxis = RotationAxisEnum::kCenteredVertical;
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, rot, params });
    ASSERT_EQ(cluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);
    EXPECT_EQ(cluster.GetRotationAxis(), RotationAxisEnum::kCenteredVertical);
    EXPECT_EQ(cluster.SetOverflow(OverflowEnum::kTopInside), CHIP_NO_ERROR);
    EXPECT_EQ(cluster.GetOverflow(), OverflowEnum::kTopInside);
    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestModulationTypeInit)
{
    MockClusterConformance mod;
    mod.FeatureMap().Set(Feature::kPositioning).Set(Feature::kModulation);
    ClusterInitParameters params{};
    params.modulationType = ModulationTypeEnum::kOpacity;
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, mod, params });
    ASSERT_EQ(cluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    EXPECT_EQ(cluster.GetModulationType(), ModulationTypeEnum::kOpacity);
    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetNoArguments)
{
    EXPECT_EQ(mCluster.HandleSetTargetCommand(NullOptional, NullOptional, NullOptional), Status::InvalidCommand);
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetSuccess)
{
    EXPECT_EQ(mCluster.HandleSetTargetCommand(Optional(static_cast<Percent100ths>(5200)), NullOptional, NullOptional),
              Status::Success);
    EXPECT_EQ(mockDelegate.setTargetCalls, 1);
    DataModel::Nullable<GenericDimensionStateStruct> ts = mCluster.GetTargetState();
    EXPECT_FALSE(ts.IsNull());
    EXPECT_EQ(ts.Value().position.Value().Value(), static_cast<Percent100ths>(5200));
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetConstraintError)
{
    EXPECT_EQ(mCluster.HandleSetTargetCommand(Optional(static_cast<Percent100ths>(10001)), NullOptional, NullOptional),
              Status::ConstraintError);
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetDelegateFailure)
{
    mockDelegate.setTargetStatus = Status::Busy;
    EXPECT_EQ(mCluster.HandleSetTargetCommand(Optional(static_cast<Percent100ths>(5300)), NullOptional, NullOptional),
              Status::Failure);
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetInvalidInStateWhenLatched)
{
    MockClusterConformance latchConformance;
    latchConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kMotionLatching);
    ClosureDimensionCluster cluster(
        kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, latchConformance, initParams });
    ASSERT_EQ(cluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);
    BitFlags<LatchControlModesBitmap> modes;
    modes.Set(LatchControlModesBitmap::kRemoteLatching).Set(LatchControlModesBitmap::kRemoteUnlatching);
    ASSERT_EQ(cluster.SetLatchControlModes(modes), CHIP_NO_ERROR);

    ASSERT_EQ(cluster.SetCurrentState(PositionLatchSpeedState(5000, true, NullOptional)), CHIP_NO_ERROR);

    EXPECT_EQ(cluster.HandleSetTargetCommand(Optional(static_cast<Percent100ths>(4000)), NullOptional, NullOptional),
              Status::InvalidInState);
    EXPECT_EQ(cluster.HandleSetTargetCommand(Optional(static_cast<Percent100ths>(4000)), Optional(false), NullOptional),
              Status::Success);

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestHandleStepUnsupportedWithoutPositioning)
{
    MockClusterConformance latchOnly;
    latchOnly.FeatureMap().ClearAll().Set(Feature::kMotionLatching);
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, latchOnly, initParams });
    ASSERT_EQ(cluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    EXPECT_EQ(cluster.HandleStepCommand(StepDirectionEnum::kIncrease, 1, NullOptional), Status::UnsupportedCommand);
    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestHandleStepConstraintError)
{
    EXPECT_EQ(mCluster.HandleStepCommand(StepDirectionEnum::kUnknownEnumValue, 1, NullOptional), Status::ConstraintError);
    EXPECT_EQ(mCluster.HandleStepCommand(StepDirectionEnum::kIncrease, 0, NullOptional), Status::ConstraintError);
}

TEST_F(TestClosureDimensionCluster, TestHandleStepSuccess)
{
    EXPECT_EQ(mCluster.HandleStepCommand(StepDirectionEnum::kIncrease, 2, NullOptional), Status::Success);
    EXPECT_EQ(mockDelegate.stepCalls, 1);
    DataModel::Nullable<GenericDimensionStateStruct> ts = mCluster.GetTargetState();
    EXPECT_FALSE(ts.IsNull());
    EXPECT_EQ(ts.Value().position.Value().Value(), static_cast<Percent100ths>(7000));
}

TEST_F(TestClosureDimensionCluster, TestHandleStepWhenLatchedInvalidInState)
{
    MockClusterConformance latchConformance;
    latchConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kMotionLatching);
    ClosureDimensionCluster cluster(
        kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, mockTimerDelegate, latchConformance, initParams });
    ASSERT_EQ(cluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetCurrentState(PositionLatchSpeedState(5000, true, NullOptional)), CHIP_NO_ERROR);

    EXPECT_EQ(cluster.HandleStepCommand(StepDirectionEnum::kIncrease, 1, NullOptional), Status::InvalidInState);

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestHandleStepDelegateFailure)
{
    mockDelegate.stepStatus = Status::Busy;
    EXPECT_EQ(mCluster.HandleStepCommand(StepDirectionEnum::kDecrease, 1, NullOptional), Status::Failure);
}

__attribute__((unused)) void
MatterClosureDimensionClusterServerAttributeChangedCallback(const chip::app::ConcreteAttributePath & attributePath)
{
    (void) attributePath;
}
