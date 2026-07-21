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

# Periodic latency summary published by the latency probe subscriber on
# /ros_portal/latency/stats. It carries precomputed percentiles so
# latency can be viewed live (ros2 topic echo / rqt_plot / PlotJuggler) and
# bagged for trivial post-processing, without recomputing percentiles offline.

# Stamp = when this summary was computed.
std_msgs/Header header

# One entry per latency segment, in a fixed order so plotting index paths stay stable:
#   [e2e, bridge_internal, t0_t1, t1_t2, t2_t3, t3_t4, t4_t5].
# Segments with no samples in the current window have count=0.
LatencyMetric[] metrics
