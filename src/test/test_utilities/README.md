# Test Utilities

Support launch files and scripts for manual ROS Portal testing and integration
fixtures. These are not part of ROS Portal package itself.

## Service stub

[`launch/stubs/server_stub.launch.py`](./launch/stubs/server_stub.launch.py) is a
standalone launch file that starts inline `std_srvs/srv/SetBool` and
`std_srvs/srv/Trigger` servers:

| Service | Type | Behavior |
| --- | --- | --- |
| `/test/set_bool` | `std_srvs/srv/SetBool` | Stores `request.data` and echoes it into `success` / `message` |
| `/test/get_bool` | `std_srvs/srv/Trigger` | Returns `message: "state is: <last set_bool value>"` |

Launch it on the remote side of a two-ROS-Portal-node setup:

```bash
ros2 launch test_utilities server_stub.launch.py
```

Service names can be overridden with launch arguments `service_name` and
`get_service_name`.

### Remote `ros2 service call` examples

**Set state on the remote side**:

```bash
ros2 service call /ros_portal/ros2_service_call \
  ros_portal_msgs/srv/Ros2ServiceCall \
  "{participant_id: 'ros-portal-b', service: '/test/set_bool', msg_type: 'std_srvs/srv/SetBool', payload: '{data: true}'}"
```

On success, `output` contains YAML similar to native `ros2 service call`:

```yaml
success: true
message: enabled
```

**Read state back:**

```bash
ros2 service call /ros_portal/ros2_service_call \
  ros_portal_msgs/srv/Ros2ServiceCall \
  "{participant_id: 'ros-portal-b', service: '/test/get_bool', msg_type: 'std_srvs/srv/Trigger', payload: '{}'}"
```

Expected `output`:

```yaml
success: true
message: 'state is: True'
```

See [ROS2 CLI calls](../../docs/ros2_cli_calls.md) for the
full `ros2 service call` proxy reference.
