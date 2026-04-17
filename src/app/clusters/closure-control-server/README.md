# Closure Control Cluster

The Closure Control cluster provides an interface for controlling closure
devices. It allows clients to control positioning, motion latching, and monitor
the operational state of closure devices.

## Overview

This directory contains a code-driven C++ implementation of the Matter Closure
Control cluster server. The public surface is split into two layers:

- `ClosureControlCluster` (see `ClosureControlCluster.h`) — the cluster
  implementation. It is configured via a builder-style `Config` object that
  selects features, optional attributes, and initial state.
- `Interface` (see `CodegenIntegration.h`) — owns the lifecycle of a
  `ClosureControlCluster` instance and registers it with the
  `CodegenDataModelProvider`. `Interface` is the entry point applications use
  to stand up a cluster on an endpoint.

It uses a delegate pattern
(`chip::app::Clusters::ClosureControl::ClosureControlClusterDelegate`) to
interact with the application's closure control logic and state management.

## Usage

### 1. Implement the Delegate

Create a class that inherits from
`chip::app::Clusters::ClosureControl::ClosureControlClusterDelegate` and
implement its virtual methods to handle commands and provide closure state
information.

```cpp
#include "app/clusters/closure-control-server/ClosureControlClusterDelegate.h"

class MyClosureControlDelegate : public chip::app::Clusters::ClosureControl::ClosureControlClusterDelegate
{
};
```

### 2. Create an Interface and configure it

`Interface` is constructed with an endpoint and a delegate. Configure the
cluster conformance and initial state, then call `Init` twice — once to stage
the configuration and once to create and register the underlying cluster.

```cpp
#include "app/clusters/closure-control-server/CodegenIntegration.h"

using namespace chip::app::Clusters::ClosureControl;

MyClosureControlDelegate gDelegate;
Interface gInterface(/* endpoint */ 1, gDelegate);

CHIP_ERROR InitClosureControl()
{
    ClusterConformance conformance;
    conformance.FeatureMap()
        .Set(Feature::kPositioning)
        .Set(Feature::kCalibration);
    conformance.OptionalAttributes().Set<Attributes::CountdownTime::Id>();

    ClusterInitParameters initParams;
    initParams.mMainState = MainStateEnum::kStopped;

    ReturnErrorOnFailure(gInterface.Init(conformance, initParams));
    ReturnErrorOnFailure(gInterface.Init());
    return CHIP_NO_ERROR;
}
```

After the second `Init()` call, `gInterface.Cluster()` returns a reference to
the underlying `ClosureControlCluster`, which exposes all setters, getters,
command handlers and event generators.

### 3. Shut down when done

Call `Interface::Shutdown()` to unregister the cluster and destroy the
instance.

```cpp
gInterface.Shutdown();
```

## Constructing a cluster directly (for tests)

Tests and advanced integrations can construct `ClosureControlCluster`
directly via the `Config` builder:

```cpp
ClosureControlCluster cluster(
    ClosureControlCluster::Config(endpointId, delegate, timerDelegate)
        .WithPositioning()
        .WithMotionLatching(BitFlags<LatchControlModesBitmap>()
                                .Set(LatchControlModesBitmap::kRemoteLatching)
                                .Set(LatchControlModesBitmap::kRemoteUnlatching))
        .WithCountdownTime()
        .WithInitialMainState(MainStateEnum::kStopped));
```

The available builder methods mirror the cluster features:

- `WithPositioning()`, `WithInstantaneous()`, `WithSpeed()`, `WithVentilation()`,
  `WithPedestrian()`, `WithCalibration()`, `WithProtection()`,
  `WithManuallyOperable()`
- `WithMotionLatching(latchControlModes)` — enables MotionLatching and
  fixes the value of the `LatchControlModes` attribute (Fixed quality)
- `WithCountdownTime(initial = null)` — toggles the optional
  `CountdownTime` attribute and sets its initial value
- `WithInitialMainState(...)`, `WithInitialOverallCurrentState(...)` —
  initial state

## Conformance Validation

`ClosureControlCluster`'s constructor validates feature combinations and
aborts via `VerifyOrDie` on invalid configurations. `Interface::Init()` also
runs `ClusterConformance::IsValid()` before creating the cluster and returns
`CHIP_ERROR_INCORRECT_STATE` if the configuration is invalid.
