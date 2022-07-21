import time

def wait(world, duration):
  target_time = time.time() + duration
  while time.time() < target_time:
    world.tick()