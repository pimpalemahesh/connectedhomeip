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

using chip::app::Clusters::ClosureDimension::ClusterConformance;
using chip::app::Clusters::ClosureDimension::Feature;

TEST(TestClosureDimensionConformance, ValidWhenPositioningEnabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kPositioning);

    EXPECT_TRUE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, ValidWhenMotionLatchingEnabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kMotionLatching);

    EXPECT_TRUE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, InvalidWhenNeitherPositioningNorMotionLatchingEnabled)
{
    ClusterConformance conformance;

    EXPECT_FALSE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, ValidWhenSpeedAndPositioningEnabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kSpeed).Set(Feature::kPositioning);

    EXPECT_TRUE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, InvalidWhenSpeedEnabledButPositioningDisabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kSpeed);

    EXPECT_FALSE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, InvalidWhenLimitationEnabledButPositioningDisabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kLimitation);

    EXPECT_FALSE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, InvalidWhenUnitEnabledButPositioningDisabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kUnit);

    EXPECT_FALSE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, ValidWhenLimitationAndPositioningEnabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kLimitation).Set(Feature::kPositioning);

    EXPECT_TRUE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, InvalidWhenTranslationEnabledButPositioningDisabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kTranslation);

    EXPECT_FALSE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, ValidWhenTranslationAndPositioningEnabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kTranslation).Set(Feature::kPositioning);

    EXPECT_TRUE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, InvalidWhenRotationEnabledButPositioningDisabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kRotation);

    EXPECT_FALSE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, ValidWhenRotationAndPositioningEnabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kRotation).Set(Feature::kPositioning);

    EXPECT_TRUE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, InvalidWhenModulationEnabledButPositioningDisabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kModulation);

    EXPECT_FALSE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, ValidWhenModulationAndPositioningEnabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kModulation).Set(Feature::kPositioning);

    EXPECT_TRUE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, InvalidWhenTranslationAndRotationBothEnabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kTranslation).Set(Feature::kRotation);

    EXPECT_FALSE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, InvalidWhenRotationAndModulationBothEnabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kRotation).Set(Feature::kModulation);

    EXPECT_FALSE(conformance.IsValid());
}

TEST(TestClosureDimensionConformance, InvalidWhenTranslationAndModulationBothEnabled)
{
    ClusterConformance conformance;
    conformance.FeatureMap().Set(Feature::kPositioning).Set(Feature::kTranslation).Set(Feature::kModulation);

    EXPECT_FALSE(conformance.IsValid());
}
