#  Copyright 2026 LiveKit
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

# End-to-end bridge latency probe. Each stage stamps its own timestamp into the
# message as it flows publisher -> bridge -> LiveKit -> bridge -> subscriber, so
# the whole T0..T5 path is carried in the message content (no side channels).
# All stamps use the ROS clock; the two bridges must share a wall clock (e.g. run
# on one host) for the cross-bridge stamps to be comparable.
#
# Unset stamps are left zero. The bridge only forwards / republishes this type
# when measure_latency is enabled.

# Monotonic sequence number set by the publisher; detects drops and orders samples.
uint64 seq

builtin_interfaces/Time t0  # T0: published to the bridge (external publisher)
builtin_interfaces/Time t1  # T1: sending bridge subscriber callback entry
builtin_interfaces/Time t2  # T2: sending bridge forwards over LiveKit
builtin_interfaces/Time t3  # T3: receiving bridge reads the LiveKit frame
builtin_interfaces/Time t4  # T4: receiving bridge republishes to ROS
builtin_interfaces/Time t5  # T5: subscriber receives (filled outside the bridge)

# Optional padding to sweep the serialized message size.
uint8[] payload
