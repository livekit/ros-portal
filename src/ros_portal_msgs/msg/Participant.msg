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


# A LiveKit participant visible to this portal.
# Exact LiveKit identity.
string name
# Value of the lk.ros_portal_version attribute. Empty if the participant does
# not set it, which means it is not a ROS Portal. Self-reported, not verified.
string ros_portal_version
