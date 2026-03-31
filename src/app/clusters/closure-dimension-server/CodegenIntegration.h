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

#pragma once

#include <app/clusters/closure-dimension-server/ClosureDimensionCluster.h>
#include <app/clusters/closure-dimension-server/ClosureDimensionClusterDelegate.h>

namespace chip {
namespace app {
namespace Clusters {
namespace ClosureDimension {

/**
 * @brief Context for the Closure Dimension cluster.
 *
 * @param delegate The delegate for the cluster.
 * @param conformance The conformance for the cluster.
 * @param initParams The init params for the cluster.
 */
struct ClosureDimensionClusterContext
{
    ClosureDimensionClusterDelegate * delegate;
    ClusterConformance * conformance;
    ClusterInitParameters * initParams;
};

/**
 * @brief Get the instance of the Closure Dimension cluster.
 * @note Cluster Instance only available after the cluster is initialized.
 *
 * @param endpointId The endpoint ID for the cluster.
 * @return Pointer to the instance of the Closure Dimension cluster. nullptr if the cluster is not initialized.
 */
ClosureDimensionCluster * GetInstance(EndpointId endpointId);

/**
 * @brief Set the start up params for the Closure Dimension cluster.
 * @note This function should be called before the cluster is initialized and GetInstance() is called.
 *
 * @param endpointId The endpoint ID for the cluster.
 * @param context The context for the cluster.
 */
void SetStartUpParams(EndpointId endpointId, const ClosureDimensionClusterContext & context);

} // namespace ClosureDimension
} // namespace Clusters
} // namespace app
} // namespace chip
