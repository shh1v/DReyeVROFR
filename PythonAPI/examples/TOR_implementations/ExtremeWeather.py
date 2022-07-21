import time
import carla
import numpy as np
from DReyeVR_utils import find_ego_vehicle
from TOR_implementations.TOR_utility import wait

def generate_fog(world):
  old_weather = world.get_weather()
  new_weather = carla.WeatherParameters(
    cloudiness=80.0,
    precipitation=30.0,
    precipitation_deposits=90,
    wind_intensity = 100,
    fog_density = 90,
    fog_distance = 5,
    fog_falloff = 20)
  world.set_weather(new_weather)
  return old_weather

def set_to_weather(world, weather, execution_time):
  wait(world, execution_time)
  world.set_weather(weather)

if __name__ == '__main__':
  try:
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

    # lane_change(world, DReyeVR_vehicle, carla.LaneChange.Left)
    print("lane change done")

    while True:
      world.tick()
  except KeyboardInterrupt:
      pass
  finally:
      print('\ndone.')