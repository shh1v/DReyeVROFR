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
  traffic_manager.global_percentage_speed_difference(0)

  DReyeVR_vehicle.set_autopilot(True, 8000)
  wait(world, 5) # wait 5 seconds
  print("Autopilot: True")

  DReyeVR_vehicle.set_autopilot(False, 8000)
  print("Autopilot: False")
  while True:
    world.tick()

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