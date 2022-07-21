#!/usr/bin/env python

###############################################
# Copyright (c) Shiv Patel, 2022
# BC, Canada.
# The University of British Columbia, Okanagan
###############################################

"""Example script to generate traffic in the simulation"""

from asyncore import write
import time
import carla
from TOR_implementations.TOR_utility import wait
from carla import VehicleLightState as vls
from DReyeVR_utils import find_ego_vehicle

import argparse
import logging
from numpy import random

# Importing TOR scenerio implementations
import TOR_implementations.LVAD as LVAD
import TOR_implementations.ExtremeWeather as ExtremeWeather
import TOR_implementations.CSA as CSA
import TOR_implementations.HumanCrossing as HumanCrosssing

SIGNAL_FILE_PATH = "E:/DReyeVR/carla/Build/UE4Carla/CARLA_0.9.13-13-g367980aa6-dirty/WindowsNoEditor/CarlaUE4/Content/ConfigFiles/SignalFile.txt"

def get_actor_blueprints(world, filter, generation):
    bps = world.get_blueprint_library().filter(filter)

    if generation.lower() == "all":
        return bps

    # If the filter returns only one bp, we assume that this one needed
    # and therefore, we ignore the generation
    if len(bps) == 1:
        return bps

    try:
        int_generation = int(generation)
        # Check if generation is in available generations
        if int_generation in [1, 2]:
            bps = [x for x in bps if int(x.get_attribute('generation')) == int_generation]
            return bps
        else:
            print("   Warning! Actor Generation is not valid. No actor will be spawned.")
            return []
    except:
        print("   Warning! Actor Generation is not valid. No actor will be spawned.")
        return []


def main():
    args = generate_args()
    logging.basicConfig(format='%(levelname)s: %(message)s', level=logging.INFO)

    vehicles_list = []
    walkers_list = []
    all_id = []
    client = carla.Client(args.host, args.port)
    client.set_timeout(10.0)
    synchronous_master = False
    random.seed(args.seed if args.seed is not None else int(time.time()))

    try:
        world = client.get_world()

        traffic_manager = client.get_trafficmanager(args.tm_port)
        traffic_manager.set_global_distance_to_leading_vehicle(2.5)
        if args.respawn:
            traffic_manager.set_respawn_dormant_vehicles(True)
        if args.seed is not None:
            traffic_manager.set_random_device_seed(args.seed)

        settings = world.get_settings()
        if not args.asynch:
            traffic_manager.set_synchronous_mode(True)
            if not settings.synchronous_mode:
                synchronous_master = True
                settings.synchronous_mode = True
                settings.fixed_delta_seconds = 1/40.0
            else:
                synchronous_master = False
        else:
            print("You are currently in asynchronous mode. If this is a traffic simulation, \
            you could experience some issues. If it's not working correctly, switch to synchronous \
            mode by using traffic_manager.set_synchronous_mode(True)")

        if args.no_rendering:
            settings.no_rendering_mode = True
        world.apply_settings(settings)

        blueprints = get_actor_blueprints(world, args.filterv, args.generationv)
        blueprints = [blueprint for blueprint in blueprints if "dreyevr" not in blueprint.id and "bike" not in blueprint.id]
        blueprintsWalkers = get_actor_blueprints(world, args.filterw, args.generationw)
        blueprints = sorted(blueprints, key=lambda bp: bp.id)

        spawn_points = world.get_map().get_spawn_points()
        number_of_spawn_points = len(spawn_points)

        if args.number_of_vehicles < number_of_spawn_points:
            random.shuffle(spawn_points)
        elif args.number_of_vehicles > number_of_spawn_points:
            msg = 'requested %d vehicles, but could only find %d spawn points'
            logging.warning(msg, args.number_of_vehicles, number_of_spawn_points)
            args.number_of_vehicles = number_of_spawn_points

        # @todo cannot import these directly.
        SpawnActor = carla.command.SpawnActor
        SetAutopilot = carla.command.SetAutopilot
        FutureActor = carla.command.FutureActor

        # --------------
        # Spawn vehicles
        # --------------
        batch = []
        for n, transform in enumerate(spawn_points):
            if n >= args.number_of_vehicles:
                break
            blueprint = random.choice(blueprints)
            if blueprint.has_attribute('color'):
                color = random.choice(blueprint.get_attribute('color').recommended_values)
                blueprint.set_attribute('color', color)
            if blueprint.has_attribute('driver_id'):
                driver_id = random.choice(blueprint.get_attribute('driver_id').recommended_values)
                blueprint.set_attribute('driver_id', driver_id)

            # spawn the cars and set their autopilot and light state all together
            batch.append(SpawnActor(blueprint, transform)
                         .then(SetAutopilot(FutureActor, True, traffic_manager.get_port())))

        for response in client.apply_batch_sync(batch, synchronous_master):
            if response.error:
                logging.error(response.error)
            else:
                vehicles_list.append(response.actor_id)

        # Set automatic vehicle lights update if specified
        if args.car_lights_on:
            all_vehicle_actors = world.get_actors(vehicles_list)
            for actor in all_vehicle_actors:
                traffic_manager.update_vehicle_lights(actor, True)

        # -------------
        # Spawn Walkers
        # -------------
        # some settings
        percentagePedestriansRunning = 50        # how many pedestrians will run?
        percentagePedestriansCrossing = 0     # how many pedestrians will walk through the road?
        if args.seedw:
            world.set_pedestrians_seed(args.seedw)
            random.seed(args.seedw)
        # 1. take all the random locations to spawn
        spawn_points = []
        for i in range(args.number_of_walkers):
            spawn_point = carla.Transform()
            loc = world.get_random_location_from_navigation()
            if (loc != None):
                spawn_point.location = loc
                spawn_points.append(spawn_point)
        # 2. we spawn the walker object
        batch = []
        walker_speed = []
        for spawn_point in spawn_points:
            walker_bp = random.choice(blueprintsWalkers)
            # set as not invincible
            if walker_bp.has_attribute('is_invincible'):
                walker_bp.set_attribute('is_invincible', 'false')
            # set the max speed
            if walker_bp.has_attribute('speed'):
                if (random.random() > percentagePedestriansRunning):
                    # walking
                    walker_speed.append(walker_bp.get_attribute('speed').recommended_values[1])
                else:
                    # running
                    walker_speed.append(walker_bp.get_attribute('speed').recommended_values[2])
            else:
                print("Walker has no speed")
                walker_speed.append(0.0)
            batch.append(SpawnActor(walker_bp, spawn_point))
        results = client.apply_batch_sync(batch, True)
        walker_speed2 = []
        for i in range(len(results)):
            if results[i].error:
                logging.error(results[i].error)
            else:
                walkers_list.append({"id": results[i].actor_id})
                walker_speed2.append(walker_speed[i])
        walker_speed = walker_speed2
        # 3. we spawn the walker controller
        batch = []
        walker_controller_bp = world.get_blueprint_library().find('controller.ai.walker')
        for i in range(len(walkers_list)):
            batch.append(SpawnActor(walker_controller_bp, carla.Transform(), walkers_list[i]["id"]))
        results = client.apply_batch_sync(batch, True)
        for i in range(len(results)):
            if results[i].error:
                logging.error(results[i].error)
            else:
                walkers_list[i]["con"] = results[i].actor_id
        # 4. we put together the walkers and controllers id to get the objects from their id
        for i in range(len(walkers_list)):
            all_id.append(walkers_list[i]["con"])
            all_id.append(walkers_list[i]["id"])
        all_actors = world.get_actors(all_id)

        # wait for a tick to ensure client receives the last transform of the walkers we have just created
        if args.asynch or not synchronous_master:
            world.wait_for_tick()
        else:
            world.tick()

        # 5. initialize each controller and set target to walk to (list is [controler, actor, controller, actor ...])
        # set how many pedestrians can cross the road
        world.set_pedestrians_cross_factor(percentagePedestriansCrossing)
        for i in range(0, len(all_id), 2):
            # start walker
            all_actors[i].start()
            # set walk to random point
            all_actors[i].go_to_location(world.get_random_location_from_navigation())
            # max speed
            all_actors[i].set_max_speed(float(walker_speed[int(i/2)]))

        print('spawned %d vehicles and %d walkers, press Ctrl+C to exit.' % (len(vehicles_list), len(walkers_list)))

        # Increase the speed of the spawned and ego vehicle (in autopilot mode)
        traffic_manager.global_percentage_speed_difference(-300.0)

        # Enable autonomous mode for the ego-vehicle while doing the reading task.
        DReyeVR_vehicle = find_ego_vehicle(world)
        if DReyeVR_vehicle is not None:
            DReyeVR_vehicle.set_autopilot(True, traffic_manager.get_port())
            print("Successfully set autopilot on ego vehicle.")

        # Give a signal to start reading comprehension task
        write_signal_file(SIGNAL_FILE_PATH, 0)
        print("Starting reading comprehension task.")

        # Check for signal to execute TOR and turn traffic light green at traffic signals.
        while True:
            if not args.asynch and synchronous_master:
                world.tick()
            else:
                world.wait_for_tick()

            turn_traffic_light_to(DReyeVR_vehicle, carla.TrafficLightState.Red, carla.TrafficLightState.Green)
    
            # Check for permission to execute TOR scenerio
            if read_signal_file(SIGNAL_FILE_PATH) == 1:
                break

        # Execute TOR scenerio
        # 1: LVAD, 2: EW, 3: CSA, 4: HCR
        print("TOR scenerio choosen: ", str(args.tor_scenario))
        if args.tor_scenario == 1:
            print("Leading Vehicle Abrupt Deceleration scenario executed.")
            pass
        elif args.tor_scenario == 2:
            print("Extreme Weather scenario executed.")
            # Wait for the TOR to be issued
            wait_for_TOR(world)
            print("TOR issued")

            # Generate Extreme weather conditions
            old_weather = ExtremeWeather.generate_fog(world)
            print("Extreme weather set")

            # Disable autopilot once TOR is issued
            DReyeVR_vehicle.set_autopilot(False, args.tm_port)
            print("Disabling autopilot")
            world.tick()

            # Revert back to original weather after 10 seconds
            ExtremeWeather.set_to_weather(world, old_weather, 10)
        elif args.tor_scenario == 3:
            print("Construction Site Ahead scenario executed.")
            pass
        else:
            print("Human Crossing scenario executed.")
            pass

        # Once original conditions are attained, enable autopilot
        DReyeVR_vehicle.set_autopilot(True, args.tm_port)
        print("Enabling autopilot back again")
        # for 5 seconds than exit
        wait(5)

    finally:
        if not args.asynch and synchronous_master:
            settings = world.get_settings()
            settings.synchronous_mode = False
            settings.no_rendering_mode = False
            settings.fixed_delta_seconds = None
            world.apply_settings(settings)

        print('\ndestroying %d non-ego vehicles' % len(vehicles_list))
        client.apply_batch([carla.command.DestroyActor(x) for x in vehicles_list])

        # stop walker controllers (list is [controller, actor, controller, actor ...])
        for i in range(0, len(all_id), 2):
            all_actors[i].stop()

        print('\ndestroying %d walkers' % len(walkers_list))
        client.apply_batch([carla.command.DestroyActor(x) for x in all_id])

        if DReyeVR_vehicle is not None:
            DReyeVR_vehicle.set_autopilot(False, traffic_manager.get_port())
            print("Successfully set manual control on ego vehicle")

        time.sleep(0.5)


def turn_traffic_light_to(DReyeVR_vehicle, from_light_state, to_light_state):
    if DReyeVR_vehicle.is_at_traffic_light():
        traffic_light = DReyeVR_vehicle.get_traffic_light()
        if traffic_light.get_state() == from_light_state:
            traffic_light.set_state(to_light_state)


def write_signal_file(file_path, signal):
    try:
        f = open(file_path, "w")
        f.write(str(signal))
        f.close()
    except:
        print("Error occured while opening/writing to the signal file")


def read_signal_file(file_path):
    try:
        f = open(file_path, "r")
        signal = int(f.read())
        f.close()
        return signal
    except:
        print("Error occured while opening/reading the signal file")


def generate_args():
    argparser = argparse.ArgumentParser(
        description=__doc__)
    argparser.add_argument(
        '--host',
        metavar='H',
        default='127.0.0.1',
        help='IP of the host server (default: 127.0.0.1)')
    argparser.add_argument(
        '-p', '--port',
        metavar='P',
        default=2000,
        type=int,
        help='TCP port to listen to (default: 2000)')
    argparser.add_argument(
        '-n', '--number-of-vehicles',
        metavar='N',
        default=50,
        type=int,
        help='Number of vehicles (default: 50)')
    argparser.add_argument(
        '-w', '--number-of-walkers',
        metavar='W',
        default=30,
        type=int,
        help='Number of walkers (default: 30)')
    argparser.add_argument(
        '--filterv',
        metavar='PATTERN',
        default='vehicle.*',
        help='Filter vehicle model (default: "vehicle.*")')
    argparser.add_argument(
        '--generationv',
        metavar='G',
        default='All',
        help='restrict to certain vehicle generation (values: "1","2","All" - default: "All")')
    argparser.add_argument(
        '--filterw',
        metavar='PATTERN',
        default='walker.pedestrian.*',
        help='Filter pedestrian type (default: "walker.pedestrian.*")')
    argparser.add_argument(
        '--generationw',
        metavar='G',
        default='2',
        help='restrict to certain pedestrian generation (values: "1","2","All" - default: "2")')
    argparser.add_argument(
        '--tm-port',
        metavar='P',
        default=8000,
        type=int,
        help='Port to communicate with TM (default: 8000)')
    argparser.add_argument(
        '--asynch',
        action='store_true',
        help='Activate asynchronous mode execution')
    argparser.add_argument(
        '-s', '--seed',
        metavar='S',
        type=int,
        help='Set random device seed and deterministic mode for Traffic Manager')
    argparser.add_argument(
        '--seedw',
        metavar='S',
        default=0,
        type=int,
        help='Set the seed for pedestrians module')
    argparser.add_argument(
        '--car-lights-on',
        action='store_true',
        default=False,
        help='Enable automatic car light management')
    argparser.add_argument(
        '--respawn',
        action='store_true',
        default=False,
        help='Automatically respawn dormant vehicles (only in large maps)')
    argparser.add_argument(
        '--no-rendering',
        action='store_true',
        default=False,
        help='Activate no rendering mode')
    argparser.add_argument(
        '--tor-scenario',
        action='store_true',
        default=2,
        help='1: LVAD, 2: EW, 3: CSA, 4: HCR')
    return argparser.parse_args()


def wait_for_TOR(world):
    while read_signal_file(SIGNAL_FILE_PATH) != 2:
        world.tick()


if __name__ == '__main__':

    try:
        main()
    except KeyboardInterrupt:
        pass
    finally:
        print('\ndone.')
