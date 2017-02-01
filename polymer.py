#! /usr/bin/env python3

import heapq
import random

class Polymer:
    """
    Track `Feature` objects, `Polymerase` objects, and collisions on a single
    polymer. Move `Polymerase` objects along the polymer, maintain a priority
    queue of `Polymerase` objects that are expected to move, and calculate
    time-until-next move from an exponential distribution.
    """
    def __init__(self, name, length, elements, sim):
        self.name = name
        self.length = length
        self.polymerases = []
        self.elements = elements
        self.heap = []
        self.__observers = []
        self.register_sim(sim)

    def register_sim(self, sim):
        self.sim = sim

    def register_observer(self, observer):
        self.__observers.append(observer)

    def notify_observers(self, **kwargs):
        for observer in self.__observers:
            observer.notify(self, **kwargs)

    def bind_polymerase(self, pol):
        """
        Bind a `Polymerase` object to the polymer and add it to priority queue.

        :param pol: `Polymerase` object.
        """
        self.polymerases.append(pol)
        self.push(pol)

    def push(self, pol):
        """
        Calculate time-until-next reaction and add `Polymerase` object to
        priority queue.

        :param pol: `Polymerase` object.
        """
        heapq.heappush(self.heap, (self.next_time(pol), pol))

    def pop(self):
        """
        Remove and return `Polymerase` object from top of priority queue, along
        with its reaction time.

        :returns: time, `Polymerase` object
        """
        pol = heapq.heappop(self.heap)
        return pol[0], pol[1]

    def next_time(self, pol):
        """
        Calculate time-until-next reaction from an exponential distribution
        centered at a `Polymerase` object's `speed` attribute. Adds time to
        current simulation time.

        :param pol: `Polymerase` object.
        :returns: time that `pol` will move next.
        """
        return self.sim.time + random.expovariate(pol.speed)

    def move_polymerase(self, pol):
        # Record old covered elements
        old_covered_elements = self.find_intersections(pol, self.elements)

        # Move polymerase
        pol.move()

        # Record new collisions
        collisions = self.find_intersections(pol, self.polymerases)

        # First resolve any collisions between polymerases
        for other_pol in collisions:
            other_pol.react(pol)

        # Now cover elements
        new_covered_elements = self.find_intersections(pol, self.elements)
        for element in new_covered_elements:
            element.cover()
            element.react(pol)

        # Check for uncovered elements
        for element in old_covered_elements:
            if element not in new_covered_elements:
                self.notify_observers(species = element.name,
                                      action = "free_promoter")

    def execute(self):
        """
        Process `Polymerase` object at the top of the priority queue. Check for
        collisions, uncovering of elements, and terminations.
        """
        time, pol = self.pop()

        self.move_polymerase(pol)

        self.sim.time = time

        if pol.attached == True:
            self.push(pol)
        else:
            self.notify_observers(species = pol.name, action = "terminate")
            self.polymerases.remove(pol)

    def find_intersections(self, pol, elements):
        """
        Detect overlap between a given `Polymerase` object and all other
        `Feature` objects in `elements` currently in or on the polymer that it
        interacts with.

        :param pol: `Polymerase` object with which to check for collisions.
        :returns: list of `Feature` objects that overlap and interact with
            `pol`.
        """
        intersections = []
        for element in elements:
            if pol == element:
                continue
            if self.segments_intersect(pol.start, pol.stop,
                                       element.start, element.stop):
                if element.check_interaction(pol):
                    intersections.append(element)
        return intersections

    def segments_intersect(self, x1, x2, y1, y2):
        """
        Do two line segments (e.g. `Polymerase` objects) overlap?
        """
        return x2 >= y1 and y2 >= x1

    def __str__(self):
        """
        Convert `Polymer` object to string representation showing features and
        polymerases.
        """
        out_string = "Time: " + str(self.sim.time)

        # out_string += str(self.heap)

        feature_locs = [0]*self.length
        for feature in self.polymerases:
            for i in range(feature.start - 1, feature.stop - 1):
                feature_locs[i] = 1
        out_string += "\nfeatures: \n" + ''.join(map(str, feature_locs))
        return out_string