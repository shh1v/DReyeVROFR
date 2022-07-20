import time
import carla
import numpy as np
from DReyeVR_utils import find_ego_vehicle


def main():
  client = carla.Client('127.0.0.1', 2000)
  client.set_timeout(10.0)
  world = client.get_world()

  traffic_manager = client.get_trafficmanager(8000)
  traffic_manager.set_global_distance_to_leading_vehicle(2.5)
  settings = world.get_settings()
  traffic_manager.set_synchronous_mode(True)
  if not settings.synchronous_mode:   
      settings.synchronous_mode = True
      settings.fixed_delta_seconds = 1.0/80
  world.tick()
  DReyeVR_vehicle = find_ego_vehicle(world)

  # Increase the speed of the spawned and ego vehicle (in autopilot mode)
  traffic_manager.global_percentage_speed_difference(-600.0)

  DReyeVR_vehicle.set_autopilot(True, 8000)
  wait(world, 3)

  lane_change(world, DReyeVR_vehicle, carla.LaneChange.Left)
  print("lane change done")

  while True:
    world.tick()

def accelerate(world, ego_vehicle, control_vehicle, distance, time_budget):
  ego_velocity = ego_vehicle.get_velocity().length()
  
  pass

def lane_change(world, vehicle, lane_change_type):
  """
  Currently adjusting time based on a fix rotation. However, rotation could be varied based on time budget
  """
  turn_degree = -20 if lane_change_type == carla.LaneChange.Left else 20

  # Computing the new transform required for the lane change
  transform = vehicle.get_transform()
  new_transform = transform
  new_transform.rotation.yaw += turn_degree

  # Disabling autopilot temporarily
  vehicle.set_autopilot(False, 8000)

  # Setting new transform
  vehicle.set_transform(new_transform)
  world.tick()

  # Computing for how long should the new transform be applied
  mtime = (world.get_map().get_waypoint(vehicle.get_location()).lane_width)/(vehicle.get_velocity().length()*np.sin(np.radians(turn_degree)))
  print("transform time: ", np.abs(mtime))

  # Wating for lane change to execute
  wait(world, mtime)

  # Enabling autopilot back again
  vehicle.set_autopilot(True, 8000)
  world.tick()

def apply_brakes():
  pass

def wait(world, duration):
  target_time = time.time() + duration
  while time.time() < target_time:
    world.tick()

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        pass
    finally:
        print('\ndone.')