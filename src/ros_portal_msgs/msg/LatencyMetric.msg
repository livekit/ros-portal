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

# Summary statistics for one latency metric over the current rolling window.
# All values are in milliseconds (unit is carried in each field name).

# Metric key, e.g. "e2e" (T5-T0), "t2_t3" (LiveKit transport T3-T2),
# "t3_t4" (bridge recv overhead T4-T3).
string name

# Number of samples in the window this summary was computed over.
uint64 count

float64 p50_ms
float64 p90_ms
float64 p95_ms
float64 p99_ms
float64 min_ms
float64 max_ms
float64 mean_ms
