# Matter ESP32 Closure Example

This example demonstrates the Matter Closure device application on ESP
platforms. The application implements a closure device composed of a Closure
Control cluster on endpoint 1 and two Closure Dimension (panel) clusters on
endpoints 2 and 3, sharing the closure logic in
[closure-common](../closure-common) with the other platform examples.

Please
[setup ESP-IDF and CHIP Environment](../../../docs/platforms/esp32/setup_idf_chip.md)
and refer
[building and commissioning](../../../docs/platforms/esp32/build_app_and_commission.md)
guides to get started.

---

-   [Cluster control](#cluster-control)

---

### Cluster control

After commissioning, the Closure Control and Closure Dimension clusters can be
exercised with `chip-tool`.

Move the closure to a target position (fully closed) on endpoint 1:

    $ ./out/debug/chip-tool closurecontrol move-to <NODE ID> 1 --Position 0

Stop an ongoing motion:

    $ ./out/debug/chip-tool closurecontrol stop <NODE ID> 1

Set a target position on a panel endpoint (endpoint 2 or 3):

    $ ./out/debug/chip-tool closuredimension set-target <NODE ID> 2 --Position 5000

Read the overall state of the closure:

    $ ./out/debug/chip-tool closurecontrol read overall-current-state <NODE ID> 1

The application simulates the closure hardware: motion is performed in steps
using timers, and the current state attributes are updated until the target
state is reached.
