import sys
import math
import glob
import os
from reportlab.pdfgen import canvas
from reportlab.lib.pagesizes import letter
from reportlab.lib import colors
from collections import namedtuple

from shapely.geometry import Polygon, MultiPolygon, Point
from shapely.ops import nearest_points
import numpy as np


def most_inland_point(polygon_points, step=0.1):
    """
    Finds the most inland point of a polygon and the diameter of the largest circle
    that can fit inside it using negative buffers and distance-to-boundary check.
    This is an approximation method.
    """
    polygon = Polygon(polygon_points)
    if not polygon.is_valid:
        polygon = polygon.buffer(0)

    if polygon.is_empty:
        return (0, 0), 0

    min_x, min_y, max_x, max_y = polygon.bounds
    max_possible_radius = max(max_x - min_x, max_y - min_y) / 2.0
    last_valid_shrunken_polygon = polygon

    for r in np.arange(step, max_possible_radius + step, step):
        shrunken = polygon.buffer(-r)
        if shrunken.is_empty:
            break
        if isinstance(shrunken, MultiPolygon):
            shrunken = max(shrunken.geoms, key=lambda p: p.area)
        last_valid_shrunken_polygon = shrunken

    inland_point = last_valid_shrunken_polygon.representative_point()
    nearest = nearest_points(inland_point, polygon.boundary)[1]
    radius = inland_point.distance(nearest)

    return (inland_point.x, inland_point.y), radius * 2


def get_polygon_bbox(points):
    """Calculates the bounding box of a polygon."""
    if not points:
        return 0, 0, 0, 0
    min_x = min(p[0] for p in points)
    min_y = min(p[1] for p in points)
    max_x = max(p[0] for p in points)
    max_y = max(p[1] for p in points)
    return min_x, min_y, max_x, max_y


def parse_problem_file(file_path):
    """
    Parses the problem file to extract original piece geometries, their initial
    bounding boxes, and the bin dimensions.
    """
    original_pieces_data = {}
    with open(file_path, 'r') as f:
        lines = f.readlines()

    BinDimension = namedtuple('BinDimension', ['width', 'height'])
    bin_width, bin_height = map(float, lines[0].strip().split())
    bin_dimension = BinDimension(width=bin_width, height=bin_height)

    piece_id_counter = 1
    for line in lines[2:]:
        line = line.strip()
        if not line:
            continue
        
        points_str = line.split(' ')
        vertices = []
        for point_str in points_str:
            try:
                x_str, y_str = point_str.split(',')
                vertices.append((float(x_str), float(y_str)))
            except ValueError:
                continue
        
        if vertices:
            min_x, min_y, max_x, max_y = get_polygon_bbox(vertices)
            # The rotation pivot is the center of the piece's initial bounding box.
            pivot_x = min_x + (max_x - min_x) / 2
            pivot_y = min_y + (max_y - min_y) / 2
            original_pieces_data[piece_id_counter] = (vertices, (pivot_x, pivot_y))
            piece_id_counter += 1
            
    return bin_dimension, original_pieces_data


def parse_bin_files():
    """
    Parses all Bin-*.txt files in the current directory to get placement data.
    """
    bins_data = []
    # Glob for files and sort them numerically based on the number in the filename.
    # This correctly handles cases like Bin-1.txt, Bin-2.txt, Bin-10.txt.
    bin_files = sorted(
        glob.glob('Bin-*.txt'),
        key=lambda f: int(os.path.basename(f).replace('Bin-', '').replace('.txt', ''))
    )

    for bin_file in bin_files:
        try:
            bin_number = int(os.path.basename(bin_file).replace('Bin-', '').replace('.txt', ''))
        except ValueError:
            continue  # Skip if file name is not in the expected format

        placed_pieces = []
        with open(bin_file, 'r') as f:
            lines = f.readlines()
        
        # First line is number of pieces, we can skip it.
        for line in lines[1:]:
            line = line.strip()
            if not line:
                continue
            
            parts = line.split()
            if len(parts) < 3:
                continue
            
            piece_id = int(parts[0])
            rotation = float(parts[1])
            x_str, y_str = parts[2].split(',')
            x = float(x_str)
            y = float(y_str)
            
            placed_pieces.append({'id': piece_id, 'rotation': rotation, 'x': x, 'y': y})
        
        if placed_pieces:
            bins_data.append({'number': bin_number, 'placed_pieces': placed_pieces})
    
    return bins_data


def rotate_point(point, angle_degrees, center):
    """
    Rotates a point around a given center.
    """
    # Invert angle for ReportLab's coordinate system (Y-up).
    angle_degrees = 360 - angle_degrees
    angle_rad = math.radians(angle_degrees)
    cos_theta = math.cos(angle_rad)
    sin_theta = math.sin(angle_rad)
    
    x, y = point
    cx, cy = center
    
    new_x = cos_theta * (x - cx) - sin_theta * (y - cy) + cx
    new_y = sin_theta * (x - cx) + cos_theta * (y - cy) + cy
    
    return new_x, new_y


class Romans:
    def __init__(self):
        self.f = {}
        self.l = {}
        self.scale = 1.0
        self.t = {}
        self._initialize_font()

    def _initialize_font(self):
        self.l[32] = 12 # Width of space character
        # This dictionary contains the vector definitions for each character.
        self.f = {0x21:"10;5,21 5,7;5,2 4,1 5,0 6,1 5,2",0x22:"16;4,21 4,14;12,21 12,14",0x23:"21;11.5,25 4.5,-7;17.5,25 10.5,-7;4.5,12 18.5,12;3.5,6 17.5,6",0x24:"20;8,25 8,-4;12,25 12,-4;17,18 15,20 12,21 8,21 5,20 3,18 3,16 4,14 5,13 7,12 13,10 15,9 16,8 17,6 17,3 15,1 12,0 8,0 5,1 3,3",0x25:"24;21,21 3,0;8,21 10,19 10,17 9,15 7,14 5,14 3,16 3,18 4,20 6,21 8,21 10,20 13,19 16,19 19,20 21,21;17,7 15,6 14,4 14,2 16,0 18,0 20,1 21,3 21,5 19,7 17,7",0x26:"26;23,12 23,13 22,14 21,14 20,13 19,11 17,6 15,3 13,1 11,0 7,0 5,1 4,2 3,4 3,6 4,8 5,9 12,13 13,14 14,16 14,18 13,20 11,21 9,20 8,18 8,16 9,13 11,10 16,3 18,1 20,0 22,0 23,1 23,2",0x27:"10;5,19 4,20 5,21 6,20 6,18 5,16 4,15",0x28:"14;11,25 9,23 7,20 5,16 4,11 4,7 5,2 7,-2 9,-5 11,-7",0x29:"14;3,25 5,23 7,20 9,16 10,11 10,7 9,2 7,-2 5,-5 3,-7",0x2A:"16;8,21 8,9;3,18 13,12;13,18 3,12",0x2B:"26;13,18 13,0;4,9 22,9",0x2C:"10;6,1 5,0 4,1 5,2 6,1 6,-1 5,-3 4,-4",0x2D:"26;4,9 22,9",0x2E:"10;5,2 4,1 5,0 6,1 5,2",0x2F:"22;20,25 2,-7",0x30:"20;9,21 6,20 4,17 3,12 3,9 4,4 6,1 9,0 11,0 14,1 16,4 17,9 17,12 16,17 14,20 11,21 9,21",0x31:"20;6,17 8,18 11,21 11,0",0x32:"20;4,16 4,17 5,19 6,20 8,21 12,21 14,20 15,19 16,17 16,15 15,13 13,10 3,0 17,0",0x33:"20;5,21 16,21 10,13 13,13 15,12 16,11 17,8 17,6 16,3 14,1 11,0 8,0 5,1 4,2 3,4",0x34:"20;13,21 3,7 18,7;13,21 13,0",0x35:"20;15,21 5,21 4,12 5,13 8,14 11,14 14,13 16,11 17,8 17,6 16,3 14,1 11,0 8,0 5,1 4,2 3,4",0x36:"20;16,18 15,20 12,21 10,21 7,20 5,17 4,12 4,7 5,3 7,1 10,0 11,0 14,1 16,3 17,6 17,7 16,10 14,12 11,13 10,13 7,12 5,10 4,7",0x37:"20;17,21 7,0;3,21 17,21",0x38:"20;8,21 5,20 4,18 4,16 5,14 7,13 11,12 14,11 16,9 17,7 17,4 16,2 15,1 12,0 8,0 5,1 4,2 3,4 3,7 4,9 6,11 9,12 13,13 15,14 16,16 16,18 15,20 12,21 8,21",0x39:"20;16,14 15,11 13,9 10,8 9,8 6,9 4,11 3,14 3,15 4,18 6,20 9,21 10,21 13,20 15,18 16,14 16,9 15,4 13,1 10,0 8,0 5,1 4,3",0x3A:"10;5,14 4,13 5,12 6,13 5,14;5,2 4,1 5,0 6,1 5,2",0x3B:"10;5,14 4,13 5,12 6,13 5,14;6,1 5,0 4,1 5,2 6,1 6,-1 5,-3 4,-4",0x3C:"24;20,18 4,9 20,0",0x3D:"26;4,12 22,12;4,6 22,6",0x3E:"24;4,18 20,9 4,0",0x3F:"18;3,16 3,17 4,19 5,20 7,21 11,21 13,20 14,19 15,17 15,15 14,13 13,12 9,10 9,7;9,2 8,1 9,0 10,1 9,2",0x40:"27;18.5,13 17.5,15 15.5,16 12.5,16 10.5,15 9.5,14 8.5,11 8.5,8 9.5,6 11.5,5 14.5,5 16.5,6 17.5,8;12.5,16 10.5,14 9.5,11 9.5,8 10.5,6 11.5,5;18.5,16 17.5,8 17.5,6 19.5,5 21.5,5 23.5,7 24.5,10 24.5,12 23.5,15 22.5,17 20.5,19 18.5,20 15.5,21 12.5,21 9.5,20 7.5,19 5.5,17 4.5,15 3.5,12 3.5,9 4.5,6 5.5,4 7.5,2 9.5,1 12.5,0 15.5,0 18.5,1 20.5,2 21.5,3;19.5,16 18.5,8 18.5,6 19.5,5",0x41:"18;9,21 1,0;9,21 17,0;4,7 14,7",0x42:"21;3.5,21 3.5,0;3.5,21 12.5,21 15.5,20 16.5,19 17.5,17 17.5,15 16.5,13 15.5,12 12.5,11;3.5,11 12.5,11 15.5,10 16.5,9 17.5,7 17.5,4 16.5,2 15.5,1 12.5,0 3.5,0",0x43:"21;18.5,16 17.5,18 15.5,20 13.5,21 9.5,21 7.5,20 5.5,18 4.5,16 3.5,13 3.5,8 4.5,5 5.5,3 7.5,1 9.5,0 13.5,0 15.5,1 17.5,3 18.5,5",0x44:"21;3.5,21 3.5,0;3.5,21 10.5,21 13.5,20 15.5,18 16.5,16 17.5,13 17.5,8 16.5,5 15.5,3 13.5,1 10.5,0 3.5,0",0x45:"19;3.5,21 3.5,0;3.5,21 16.5,21;3.5,11 11.5,11;3.5,0 16.5,0",0x46:"18;3,21 3,0;3,21 16,21;3,11 11,11",0x47:"21;18.5,16 17.5,18 15.5,20 13.5,21 9.5,21 7.5,20 5.5,18 4.5,16 3.5,13 3.5,8 4.5,5 5.5,3 7.5,1 9.5,0 13.5,0 15.5,1 17.5,3 18.5,5 18.5,8;13.5,8 18.5,8",0x48:"22;4,21 4,0;18,21 18,0;4,11 18,11",0x49:"8;4,21 4,0",0x4A:"16;12,21 12,5 11,2 10,1 8,0 6,0 4,1 3,2 2,5 2,7",0x4B:"21;3.5,21 3.5,0;17.5,21 3.5,7;8.5,12 17.5,0",0x4C:"17;2.5,21 2.5,0;2.5,0 14.5,0",0x4D:"24;4,21 4,0;4,21 12,0;20,21 12,0;20,21 20,0",0x4E:"22;4,21 4,0;4,21 18,0;18,21 18,0",0x4F:"22;9,21 7,20 5,18 4,16 3,13 3,8 4,5 5,3 7,1 9,0 13,0 15,1 17,3 18,5 19,8 19,13 18,16 17,18 15,20 13,21 9,21",0x50:"21;3.5,21 3.5,0;3.5,21 12.5,21 15.5,20 16.5,19 17.5,17 17.5,14 16.5,12 15.5,11 12.5,10 3.5,10",0x51:"22;9,21 7,20 5,18 4,16 3,13 3,8 4,5 5,3 7,1 9,0 13,0 15,1 17,3 18,5 19,8 19,13 18,16 17,18 15,20 13,21 9,21;12,4 18,-2",0x52:"21;3.5,21 3.5,0;3.5,21 12.5,21 15.5,20 16.5,19 17.5,17 17.5,15 16.5,13 15.5,12 12.5,11 3.5,11;10.5,11 17.5,0",0x53:"20;17,18 15,20 12,21 8,21 5,20 3,18 3,16 4,14 5,13 7,12 13,10 15,9 16,8 17,6 17,3 15,1 12,0 8,0 5,1 3,3",0x54:"16;8,21 8,0;1,21 15,21",0x55:"22;4,21 4,6 5,3 7,1 10,0 12,0 15,1 17,3 18,6 18,21",0xDC:"22;4,21 4,6 5,3 7,1 10,0 12,0 15,1 17,3 18,6 18,21;6,23 6,25;16,25 16,23",0x56:"18;1,21 9,0;17,21 9,0",0x57:"24;2,21 7,0;12,21 7,0;12,21 17,0;22,21 17,0",0x58:"20;3,21 17,0;17,21 3,0",0x59:"18;1,21 9,11 9,0;17,21 9,11",0x5A:"20;17,21 3,0;3,21 17,21;3,0 17,0",0x5B:"14;4,25 4,-7;5,25 5,-7;4,25 11,25;4,-7 11,-7",0x5C:"14;0,21 14,-3",0x5D:"14;9,25 9,-7;10,25 10,-7;3,25 10,25;3,-7 10,-7",0x5E:"16;6,15 8,18 10,15;3,12 8,17 13,12;8,17 8,0",0x5F:"16;0,-2 16,-2",0x60:"10;6,21 5,20 4,18 4,16 5,15 6,16 5,17",0x61:"19;15.5,14 15.5,0;15.5,11 13.5,13 11.5,14 8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3",0xe1:"19;15.5,14 15.5,0;15.5,11 13.5,13 11.5,14 8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3;10,17 13,19",0x62:"19;3.5,21 3.5,0;3.5,11 5.5,13 7.5,14 10.5,14 12.5,13 14.5,11 15.5,8 15.5,6 14.5,3 12.5,1 10.5,0 7.5,0 5.5,1 3.5,3",0x63:"18;15,11 13,13 11,14 8,14 6,13 4,11 3,8 3,6 4,3 6,1 8,0 11,0 13,1 15,3",0x64:"19;15.5,21 15.5,0;15.5,11 13.5,13 11.5,14 8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3",0x65:"18;3,8 15,8 15,10 14,12 13,13 11,14 8,14 6,13 4,11 3,8 3,6 4,3 6,1 8,0 11,0 13,1 15,3",0xE9:"18;3,8 15,8 15,10 14,12 13,13 11,14 8,14 6,13 4,11 3,8 3,6 4,3 6,1 8,0 11,0 13,1 15,3;10,17 13,19",0x66:"12;11,21 9,21 7,20 6,17 6,0;3,14 10,14",0x67:"19;15.5,14 15.5,-2 14.5,-5 13.5,-6 11.5,-7 8.5,-7 6.5,-6;15.5,11 13.5,13 11.5,14 8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3",0x68:"19;4.5,21 4.5,0;4.5,10 7.5,13 9.5,14 12.5,14 14.5,13 15.5,10 15.5,0",0x69:"8;3,21 4,20 5,21 4,22 3,21;4,14 4,0",0xED:"8;4,14 4,0;4,17 7,19",0x6A:"10;5,21 6,20 7,21 6,22 5,21;6,14 6,-3 5,-6 3,-7 1,-7",0x6B:"17;3.5,21 3.5,0;13.5,14 3.5,4;7.5,8 14.5,0",0x6C:"8;4,21 4,0",0x6D:"30;4,14 4,0;4,10 7,13 9,14 12,14 14,13 15,10 15,0;15,10 18,13 20,14 23,14 25,13 26,10 26,0",0x6E:"19;4.5,14 4.5,0;4.5,10 7.5,13 9.5,14 12.5,14 14.5,13 15.5,10 15.5,0",0xF1:"19;4.5,14 4.5,0;4.5,10 7.5,13 9.5,14 12.5,14 14.5,13 15.5,10 15.5,0;6,18 14,18",0x6F:"19;8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3 16.5,6 16.5,8 15.5,11 13.5,13 11.5,14 8.5,14",0xF3:"19;8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3 16.5,6 16.5,8 15.5,11 13.5,13 11.5,14 8.5,14;10,17 13,19",0x70:"19;3.5,14 3.5,-7;3.5,11 5.5,13 7.5,14 10.5,14 12.5,13 14.5,11 15.5,8 15.5,6 14.5,3 12.5,1 10.5,0 7.5,0 5.5,1 3.5,3",0x71:"19;15.5,14 15.5,-7;15.5,11 13.5,13 11.5,14 8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3",0x72:"13;3.5,14 3.5,0;3.5,8 4.5,11 6.5,13 8.5,14 11.5,14",0x73:"17;14.5,11 13.5,13 10.5,14 7.5,14 4.5,13 3.5,11 4.5,9 6.5,8 11.5,7 13.5,6 14.5,4 14.5,3 13.5,1 10.5,0 7.5,0 4.5,1 3.5,3",0x74:"12;6,21 6,4 7,1 9,0 11,0;3,14 10,14",0x75:"19;4.5,14 4.5,4 5.5,1 7.5,0 10.5,0 12.5,1 15.5,4;15.5,14 15.5,0",0xFA:"19;4.5,14 4.5,4 5.5,1 7.5,0 10.5,0 12.5,1 15.5,4;15.5,14 15.5,0;10,17 13,19 ",0xFC:"19;4.5,14 4.5,4 5.5,1 7.5,0 10.5,0 12.5,1 15.5,4;15.5,14 15.5,0;6.5,17 6.5,19;13.5,19 13.5,17",0x76:"16;2,14 8,0;14,14 8,0",0x77:"22;3,14 7,0;11,14 7,0;11,14 15,0;19,14 15,0",0x78:"17;3.5,14 14.5,0;14.5,14 3.5,0",0x79:"16;2,14 8,0;14,14 8,0 6,-4 4,-6 2,-7 1,-7",0x7A:"17;14.5,14 3.5,0;3.5,14 14.5,14;3.5,0 14.5,0",0x7B:"14;9,25 7,24 6,23 5,21 5,19 6,17 7,16 8,14 8,12 6,10;7,24 6,22 6,20 7,18 8,17 9,15 9,13 8,11 4,9 8,7 9,5 9,3 8,1 7,0 6,-2 6,-4 7,-6;6,8 8,6 8,4 7,2 6,1 5,-1 5,-3 6,-5 7,-6 9,-7",0x7C:"8;4,25 4,-7",0x7D:"14;5,25 7,24 8,23 9,21 9,19 8,17 7,16 6,14 6,12 8,10;7,24 8,22 8,20 7,18 6,17 5,15 5,13 6,11 10,9 6,7 5,5 5,3 6,1 7,0 8,-2 8,-4 7,-6;8,8 6,6 6,4 7,2 8,1 9,-1 9,-3 8,-5 7,-6 5,-7",0x7E:"24;3,6 3,8 4,11 6,12 8,12 10,11 14,8 16,7 18,7 20,8 21,10;3,8 4,10 6,11 8,11 10,10 14,7 16,6 18,6 20,7 21,10 21,12",0x7F:"14;6,21 4,20 3,18 3,16 4,14 6,13 8,13 10,14 11,16 11,18 10,20 8,21 6,21",0xD1:"22;4,21 4,0;4,21 18,0;18,21 18,0;8,22 15,22"}

        for i in range(256):
            if i in self.f:
                data = self.f[i].replace(':', ';').split(';')
                paths = []
                max_x = 0
                for path_data in data[1:]:
                    path = []
                    points = path_data.strip().split(' ')
                    for point_str in points:
                        if not point_str: continue
                        coords = point_str.split(',')
                        if len(coords) == 2:
                            x, y = float(coords[0]), float(coords[1])
                            path.append((x, y))
                            if x > max_x: max_x = x
                    if path: paths.append(path)
                self.t[i] = paths
                self.l[i] = max_x + 4

    def get_length(self, c): return self.l.get(c, 0) * self.scale
    def get_string_length(self, line): return sum(self.get_length(ord(char)) for char in line)
    def get_char(self, c): return self.t.get(c)
    def get_string(self, line):
        x = 0
        out = []
        for char in line:
            c = ord(char)
            ch = self.get_char(c)
            if ch:
                for path in ch:
                    new_path = [(p[0] * self.scale + x, p[1] * self.scale) for p in path]
                    out.append(new_path)
            x += self.get_length(c)
        return out


def create_packing_visual_pdf(bins_data, bin_dimension, original_pieces_data, file_name="nesting_visualization_from_files.pdf"):
    """
    Creates a PDF visualizing the nesting result by reading placement from files.
    """
    c = canvas.Canvas(file_name, pagesize=(bin_dimension.width + 50, bin_dimension.height + 50))
    font = Romans()

    for bin_info in bins_data:
        print(f"Drawing Bin {bin_info['number']}...")
        c.setPageSize((bin_dimension.width + 50, bin_dimension.height + 50))
        c.setStrokeColor(colors.lightgrey)
        c.translate(25, 48)
        c.rect(0, 0, bin_dimension.width, bin_dimension.height)
        
        for piece_info in bin_info['placed_pieces']:
            piece_id = piece_info['id']

            if piece_id not in original_pieces_data:
                print(f"  - Warning: Could not find original geometry for Piece ID: {piece_id}")
                continue

            # --- Transformation Logic ---
            # 1. Get original vertices and the pre-calculated rotation pivot.
            original_vertices, rotation_pivot = original_pieces_data[piece_id]
            rotation_angle = piece_info['rotation']

            # 2. Rotate the original vertices around their pivot point.
            rotated_vertices = [rotate_point(p, rotation_angle, rotation_pivot) for p in original_vertices]

            # 3. The file gives the final top-left (x,y) of the piece's bounding box.
            #    We find the top-left of our rotated shape.
            final_placed_x = piece_info['x']
            final_placed_y = piece_info['y']
            rotated_min_x, rotated_min_y, _, _ = get_polygon_bbox(rotated_vertices)

            # 4. Calculate the translation needed to move our rotated shape to its final position.
            translation_x = final_placed_x - rotated_min_x
            translation_y = final_placed_y - rotated_min_y

            # 5. Apply the translation to get the final coordinates for drawing.
            final_vertices = [(p[0] + translation_x, p[1] + translation_y) for p in rotated_vertices]

            # --- Drawing Logic ---
            p = c.beginPath()
            p.moveTo(final_vertices[0][0], final_vertices[0][1])
            for point in final_vertices[1:]:
                p.lineTo(point[0], point[1])
            p.close()
            c.setStrokeColor(colors.blue)
            c.setLineWidth(2)
            c.drawPath(p, fill=0)

            final_centroid, size = most_inland_point(final_vertices, 10)
            c.setFillColor(colors.black)
            
            text = str(piece_id)
            font.scale = size / 80 
            text_width = font.get_string_length(text)
            paths = font.get_string(text)
            c.setStrokeColor(colors.black)
            c.setLineWidth(1)
            x_offset = final_centroid[0] - text_width / 2
            y_offset = final_centroid[1] - 10 * font.scale

            for path in paths:
                p = c.beginPath()
                p.moveTo(path[0][0] + x_offset, path[0][1] + y_offset)
                for point in path[1:]:
                    p.lineTo(point[0] + x_offset, point[1] + y_offset)
                c.drawPath(p)

        c.showPage()

    print(f"\nSaving PDF to {file_name}...")
    c.save()
    print("PDF saved successfully.")


def main():
    print("--- Visualizing Nesting from Bin Files ---")
    
    if len(sys.argv) < 2:
        print("Usage: python visual_vector.py <original_problem_file>")
        print("Example: python visual_vector.py samples/S266.txt")
        sys.exit(1)
        
    input_file = sys.argv[1]
        
    try:
        bin_dimension, original_pieces_data = parse_problem_file(input_file)
    except FileNotFoundError:
        print(f"Error: Original problem file not found at '{input_file}'")
        sys.exit(1)

    print(f"Loaded {len(original_pieces_data)} original piece geometries from {input_file}.")
    print(f"Bin dimensions: {bin_dimension.width}x{bin_dimension.height}")

    print("\nSearching for packing result files (Bin-*.txt)...")
    bins_data = parse_bin_files()
    if not bins_data:
        print("\n[ERROR] No packing data found. Cannot generate PDF.")
        print("Reason: No files matching 'Bin-*.txt' were found, or the files were empty/malformed.")
        print(f"Please ensure that the packing result files (e.g., 'Bin-1.txt') are present in the current working directory: {os.getcwd()}")
        sys.exit(1)
    print(f"Found and parsed {len(bins_data)} bin result file(s).")

    output_filename = "nesting_visualization_from_files.pdf"
    create_packing_visual_pdf(bins_data, bin_dimension, original_pieces_data, file_name=output_filename)


if __name__ == "__main__":
    main()
