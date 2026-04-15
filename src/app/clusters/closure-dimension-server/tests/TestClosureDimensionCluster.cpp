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
#include <clusters/ClosureDimension/Commands.h>
#include <clusters/ClosureDimension/Metadata.h>
#include <lib/support/CHIPMem.h>
#include <platform/CHIPDeviceLayer.h>
#include <vector>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters::ClosureDimension;
using namespace chip::app::Clusters;
using namespace chip::Testing;

using Status = chip::Protocols::InteractionModel::Status;

namespace {

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
        mCluster(kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, mockConformance, initParams }),
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
    // Add resolution and step value attributes as Positioning feature is added by default.
    expectedAttributes.push_back(ClosureDimension::Attributes::Resolution::kMetadataEntry);
    expectedAttributes.push_back(ClosureDimension::Attributes::StepValue::kMetadataEntry);
    EXPECT_TRUE(IsAttributesListEqualTo(mCluster, expectedAttributes));

    MockClusterConformance latchConformance;
    latchConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kMotionLatching);
    ClosureDimensionCluster latchCluster(kTestEndpointId,
                                         ClosureDimensionCluster::Context{ mockDelegate, latchConformance, initParams });
    expectedAttributes.push_back(ClosureDimension::Attributes::LatchControlModes::kMetadataEntry);
    EXPECT_TRUE(IsAttributesListEqualTo(latchCluster, expectedAttributes));

    MockClusterConformance unitConformance;
    unitConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kUnit);
    ClosureDimensionCluster unitCluster(kTestEndpointId,
                                        ClosureDimensionCluster::Context{ mockDelegate, unitConformance, initParams });
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
    // Step command is added by default as Positioning feature is available by default in MockClusterConformance.
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
    ClosureDimensionCluster cluster(kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, latchOnly, initParams });
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
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, speedConformance, initParams });
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
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, speedConformance, initParams });
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
    ClosureDimensionCluster cluster(kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, unitConformance, initParams });
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
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, limitConformance, initParams });
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
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, latchConformance, initParams });
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
    ClosureDimensionCluster cluster(kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, tr, params });
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
    ClosureDimensionCluster cluster(kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, rot, params });
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
    ClosureDimensionCluster cluster(kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, mod, params });
    ASSERT_EQ(cluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    EXPECT_EQ(cluster.GetModulationType(), ModulationTypeEnum::kOpacity);
    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetNoArguments)
{
    ClosureDimension::Commands::SetTarget::Type request;
    EXPECT_EQ(mClusterTester.Invoke(request).status, Status::InvalidCommand);
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetSuccess)
{
    ClosureDimension::Commands::SetTarget::Type request;
    request.position.SetValue(static_cast<Percent100ths>(5200));
    EXPECT_TRUE(mClusterTester.Invoke(request).IsSuccess());
    EXPECT_EQ(mockDelegate.setTargetCalls, 1);
    DataModel::Nullable<GenericDimensionStateStruct> ts = mCluster.GetTargetState();
    EXPECT_FALSE(ts.IsNull());
    EXPECT_EQ(ts.Value().position.Value().Value(), static_cast<Percent100ths>(5200));
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetConstraintError)
{
    ClosureDimension::Commands::SetTarget::Type request;
    request.position.SetValue(static_cast<Percent100ths>(10001));
    EXPECT_EQ(mClusterTester.Invoke(request).status, Status::ConstraintError);
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetDelegateFailure)
{
    mockDelegate.setTargetStatus = Status::Busy;
    ClosureDimension::Commands::SetTarget::Type request;
    request.position.SetValue(static_cast<Percent100ths>(5300));
    EXPECT_EQ(mClusterTester.Invoke(request).status, Status::Failure);
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetInvalidInStateWhenLatched)
{
    MockClusterConformance latchConformance;
    latchConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kMotionLatching);
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, latchConformance, initParams });
    ClusterTester tester(cluster);
    ASSERT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);
    BitFlags<LatchControlModesBitmap> modes;
    modes.Set(LatchControlModesBitmap::kRemoteLatching).Set(LatchControlModesBitmap::kRemoteUnlatching);
    ASSERT_EQ(cluster.SetLatchControlModes(modes), CHIP_NO_ERROR);

    ASSERT_EQ(cluster.SetCurrentState(PositionLatchSpeedState(5000, true, NullOptional)), CHIP_NO_ERROR);

    ClosureDimension::Commands::SetTarget::Type requestWithoutLatch;
    requestWithoutLatch.position.SetValue(static_cast<Percent100ths>(4000));
    EXPECT_EQ(tester.Invoke(requestWithoutLatch).status, Status::InvalidInState);

    ClosureDimension::Commands::SetTarget::Type requestWithLatchFalse;
    requestWithLatchFalse.position.SetValue(static_cast<Percent100ths>(4000));
    requestWithLatchFalse.latch.SetValue(false);
    EXPECT_TRUE(tester.Invoke(requestWithLatchFalse).IsSuccess());

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestHandleStepUnsupportedWithoutPositioning)
{
    MockClusterConformance latchOnly;
    latchOnly.FeatureMap().ClearAll().Set(Feature::kMotionLatching);
    ClosureDimensionCluster cluster(kTestEndpointId, ClosureDimensionCluster::Context{ mockDelegate, latchOnly, initParams });
    ClusterTester tester(cluster);
    ASSERT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);

    ClosureDimension::Commands::Step::Type request;
    request.direction     = StepDirectionEnum::kIncrease;
    request.numberOfSteps = 1;
    EXPECT_EQ(tester.Invoke(request).status, Status::UnsupportedCommand);

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestHandleStepConstraintError)
{
    // An out-of-range wire value is mapped to kUnknownEnumValue by the decoder; sending literal
    // kUnknownEnumValue is rejected by the encoder, so use an undefined value instead.
    ClosureDimension::Commands::Step::Type requestUnknownDirection;
    requestUnknownDirection.direction     = static_cast<StepDirectionEnum>(99);
    requestUnknownDirection.numberOfSteps = 1;
    EXPECT_EQ(mClusterTester.Invoke(requestUnknownDirection).status, Status::ConstraintError);

    ClosureDimension::Commands::Step::Type requestZeroSteps;
    requestZeroSteps.direction     = StepDirectionEnum::kIncrease;
    requestZeroSteps.numberOfSteps = 0;
    EXPECT_EQ(mClusterTester.Invoke(requestZeroSteps).status, Status::ConstraintError);
}

TEST_F(TestClosureDimensionCluster, TestHandleStepSuccess)
{
    ClosureDimension::Commands::Step::Type request;
    request.direction     = StepDirectionEnum::kIncrease;
    request.numberOfSteps = 2;
    EXPECT_TRUE(mClusterTester.Invoke(request).IsSuccess());
    EXPECT_EQ(mockDelegate.stepCalls, 1);
    DataModel::Nullable<GenericDimensionStateStruct> ts = mCluster.GetTargetState();
    EXPECT_FALSE(ts.IsNull());
    EXPECT_EQ(ts.Value().position.Value().Value(), static_cast<Percent100ths>(7000)); // 5000 + 2 * 1000
}

TEST_F(TestClosureDimensionCluster, TestHandleStepWhenLatchedInvalidInState)
{
    MockClusterConformance latchConformance;
    latchConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kMotionLatching);
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, latchConformance, initParams });
    ClusterTester tester(cluster);
    ASSERT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetCurrentState(PositionLatchSpeedState(5000, true, NullOptional)), CHIP_NO_ERROR);

    ClosureDimension::Commands::Step::Type request;
    request.direction     = StepDirectionEnum::kIncrease;
    request.numberOfSteps = 1;
    EXPECT_EQ(tester.Invoke(request).status, Status::InvalidInState);

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestHandleStepDelegateFailure)
{
    mockDelegate.stepStatus = Status::Busy;
    ClosureDimension::Commands::Step::Type request;
    request.direction     = StepDirectionEnum::kDecrease;
    request.numberOfSteps = 1;
    EXPECT_EQ(mClusterTester.Invoke(request).status, Status::Failure);
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetLatchOnlySuccess)
{
    MockClusterConformance latchConformance;
    latchConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kMotionLatching);
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, latchConformance, initParams });
    ClusterTester tester(cluster);
    ASSERT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);
    BitFlags<LatchControlModesBitmap> modes;
    modes.Set(LatchControlModesBitmap::kRemoteLatching).Set(LatchControlModesBitmap::kRemoteUnlatching);
    ASSERT_EQ(cluster.SetLatchControlModes(modes), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetCurrentState(PositionLatchSpeedState(5000, false, NullOptional)), CHIP_NO_ERROR);

    ClosureDimension::Commands::SetTarget::Type request;
    request.latch.SetValue(true);
    EXPECT_TRUE(tester.Invoke(request).IsSuccess());
    EXPECT_EQ(mockDelegate.setTargetCalls, 1);
    DataModel::Nullable<GenericDimensionStateStruct> ts = cluster.GetTargetState();
    ASSERT_FALSE(ts.IsNull());
    ASSERT_TRUE(ts.Value().latch.HasValue());
    ASSERT_FALSE(ts.Value().latch.Value().IsNull());
    EXPECT_TRUE(ts.Value().latch.Value().Value());

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetLatchTrueWithoutRemoteLatching)
{
    MockClusterConformance latchConformance;
    latchConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kMotionLatching);
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, latchConformance, initParams });
    ClusterTester tester(cluster);
    ASSERT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);
    // Only RemoteUnlatching is allowed; latch=true should be rejected with InvalidInState.
    BitFlags<LatchControlModesBitmap> modes;
    modes.Set(LatchControlModesBitmap::kRemoteUnlatching);
    ASSERT_EQ(cluster.SetLatchControlModes(modes), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetCurrentState(PositionLatchSpeedState(5000, false, NullOptional)), CHIP_NO_ERROR);

    ClosureDimension::Commands::SetTarget::Type request;
    request.latch.SetValue(true);
    EXPECT_EQ(tester.Invoke(request).status, Status::InvalidInState);

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetSpeedConstraintError)
{
    MockClusterConformance speedConformance;
    speedConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kSpeed);
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, speedConformance, initParams });
    ClusterTester tester(cluster);
    ASSERT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetCurrentState(PositionState(5000)), CHIP_NO_ERROR);

    // Literal kUnknownEnumValue is rejected at the encoder, so send an undefined wire value
    // that the decoder will map to kUnknownEnumValue.
    ClosureDimension::Commands::SetTarget::Type request;
    request.position.SetValue(static_cast<Percent100ths>(5000));
    request.speed.SetValue(static_cast<Globals::ThreeLevelAutoEnum>(99));
    EXPECT_EQ(tester.Invoke(request).status, Status::ConstraintError);

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetPositionRoundedToResolution)
{
    // mCluster has Resolution = 100 (set in SetUp). 5150 should round to 5200.
    ClosureDimension::Commands::SetTarget::Type request;
    request.position.SetValue(static_cast<Percent100ths>(5150));
    EXPECT_TRUE(mClusterTester.Invoke(request).IsSuccess());

    DataModel::Nullable<GenericDimensionStateStruct> ts = mCluster.GetTargetState();
    ASSERT_FALSE(ts.IsNull());
    EXPECT_EQ(ts.Value().position.Value().Value(), static_cast<Percent100ths>(5200));
}

TEST_F(TestClosureDimensionCluster, TestHandleSetTargetPositionClampedByLimitRange)
{
    MockClusterConformance limitConformance;
    limitConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kLimitation);
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, limitConformance, initParams });
    ClusterTester tester(cluster);
    ASSERT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);
    Structs::RangePercent100thsStruct::Type lr{ .min = static_cast<Percent100ths>(2000), .max = static_cast<Percent100ths>(5000) };
    ASSERT_EQ(cluster.SetLimitRange(lr), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetCurrentState(PositionState(3000)), CHIP_NO_ERROR);

    // Position above max should clamp to LimitRange.max
    ClosureDimension::Commands::SetTarget::Type aboveMax;
    aboveMax.position.SetValue(static_cast<Percent100ths>(9000));
    EXPECT_TRUE(tester.Invoke(aboveMax).IsSuccess());
    {
        DataModel::Nullable<GenericDimensionStateStruct> ts = cluster.GetTargetState();
        ASSERT_FALSE(ts.IsNull());
        EXPECT_EQ(ts.Value().position.Value().Value(), static_cast<Percent100ths>(5000));
    }

    // Position below min should clamp to LimitRange.min
    ClosureDimension::Commands::SetTarget::Type belowMin;
    belowMin.position.SetValue(static_cast<Percent100ths>(500));
    EXPECT_TRUE(tester.Invoke(belowMin).IsSuccess());
    {
        DataModel::Nullable<GenericDimensionStateStruct> ts = cluster.GetTargetState();
        ASSERT_FALSE(ts.IsNull());
        EXPECT_EQ(ts.Value().position.Value().Value(), static_cast<Percent100ths>(2000));
    }

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}

TEST_F(TestClosureDimensionCluster, TestHandleStepDecreaseSuccess)
{
    // SetUp: currentPosition=5000, stepValue=1000. Decrease by 2 steps → 3000.
    ClosureDimension::Commands::Step::Type request;
    request.direction     = StepDirectionEnum::kDecrease;
    request.numberOfSteps = 2;
    EXPECT_TRUE(mClusterTester.Invoke(request).IsSuccess());
    EXPECT_EQ(mockDelegate.stepCalls, 1);
    DataModel::Nullable<GenericDimensionStateStruct> ts = mCluster.GetTargetState();
    ASSERT_FALSE(ts.IsNull());
    EXPECT_EQ(ts.Value().position.Value().Value(), static_cast<Percent100ths>(3000));
}

TEST_F(TestClosureDimensionCluster, TestHandleStepDecreaseClampsToZero)
{
    // Starting at 500, stepValue=1000. Decreasing by 1 would underflow; must clamp to 0.
    ASSERT_EQ(mCluster.SetCurrentState(PositionState(500)), CHIP_NO_ERROR);

    ClosureDimension::Commands::Step::Type request;
    request.direction     = StepDirectionEnum::kDecrease;
    request.numberOfSteps = 1;
    EXPECT_TRUE(mClusterTester.Invoke(request).IsSuccess());
    DataModel::Nullable<GenericDimensionStateStruct> ts = mCluster.GetTargetState();
    ASSERT_FALSE(ts.IsNull());
    EXPECT_EQ(ts.Value().position.Value().Value(), static_cast<Percent100ths>(0));
}

TEST_F(TestClosureDimensionCluster, TestHandleStepIncreaseClampsToMax)
{
    // Starting at 9500, stepValue=1000. Increasing by 1 would exceed max (10000); must clamp.
    ASSERT_EQ(mCluster.SetCurrentState(PositionState(9500)), CHIP_NO_ERROR);

    ClosureDimension::Commands::Step::Type request;
    request.direction     = StepDirectionEnum::kIncrease;
    request.numberOfSteps = 1;
    EXPECT_TRUE(mClusterTester.Invoke(request).IsSuccess());
    DataModel::Nullable<GenericDimensionStateStruct> ts = mCluster.GetTargetState();
    ASSERT_FALSE(ts.IsNull());
    EXPECT_EQ(ts.Value().position.Value().Value(), static_cast<Percent100ths>(10000));
}

TEST_F(TestClosureDimensionCluster, TestHandleStepSpeedConstraintError)
{
    MockClusterConformance speedConformance;
    speedConformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kSpeed);
    ClosureDimensionCluster cluster(kTestEndpointId,
                                    ClosureDimensionCluster::Context{ mockDelegate, speedConformance, initParams });
    ClusterTester tester(cluster);
    ASSERT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetResolution(100), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetStepValue(1000), CHIP_NO_ERROR);
    ASSERT_EQ(cluster.SetCurrentState(PositionState(5000)), CHIP_NO_ERROR);

    // Undefined wire value is decoded back to kUnknownEnumValue on the cluster side.
    ClosureDimension::Commands::Step::Type request;
    request.direction     = StepDirectionEnum::kIncrease;
    request.numberOfSteps = 1;
    request.speed.SetValue(static_cast<Globals::ThreeLevelAutoEnum>(99));
    EXPECT_EQ(tester.Invoke(request).status, Status::ConstraintError);

    cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
}
