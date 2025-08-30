import sys
import math
from reportlab.pdfgen import canvas
from reportlab.lib.pagesizes import letter
from reportlab.lib import colors

# Add the build directory to the path so Python can find the module
sys.path.append('./build')
import packing_py
from shapely.geometry import Polygon
import numpy as np

from shapely.ops import nearest_points

def get_radius(polygon, point):
    boundary = polygon.boundary
    nearest = nearest_points(point, boundary)[1]
    return point.distance(nearest)

def get_center_offsets(canvas, text, font_name="Helvetica", font_size=12):
    """
    Calculate the X and Y offsets to center a string drawn with drawString
    on a given (x, y) point.

    Args:
        canvas: ReportLab canvas object.
        text (str): The text to measure.
        font_name (str): Font name (default: Helvetica).
        font_size (int or float): Font size in points.

    Returns:
        (float, float): (offset_x, offset_y) to subtract from (x, y)
                        before calling drawString.
    """
    canvas.setFont(font_name, font_size)
    text_width = canvas.stringWidth(text, font_name, font_size)

    # Horizontal offset: half the text width
    offset_x = text_width / 2

    # Vertical offset: approximate centering using font size
    offset_y = font_size / 4  # heuristic for vertical centering

    return offset_x, offset_y


from shapely.geometry import Polygon, MultiPolygon, Point
from shapely.ops import nearest_points
import numpy as np

def most_inland_point(polygon_points, step=0.1):
    """
    Finds the most inland point of a polygon and the diameter of the largest circle
    that can fit inside it using negative buffers and distance-to-boundary check.
    This is an approximation method.

    Args:
        polygon_points (list of tuples): Points defining the polygon.
        step (float): Step size for shrinking (smaller = more accurate).

    Returns:
        (tuple, float): (most inland point coordinates), diameter of largest circle.
    """
    polygon = Polygon(polygon_points)
    if not polygon.is_valid:
        polygon = polygon.buffer(0)

    if polygon.is_empty:
        return (0, 0), 0

    # Determine a reasonable search range for the radius based on the polygon's bounds.
    min_x, min_y, max_x, max_y = polygon.bounds
    max_possible_radius = max(max_x - min_x, max_y - min_y) / 2.0

    last_valid_shrunken_polygon = polygon

    # Iteratively shrink the polygon to find the most "inland" area.
    for r in np.arange(step, max_possible_radius + step, step):
        shrunken = polygon.buffer(-r)

        if shrunken.is_empty:
            break

        if isinstance(shrunken, MultiPolygon):
            shrunken = max(shrunken.geoms, key=lambda p: p.area)

        last_valid_shrunken_polygon = shrunken

    # Use representative_point, which is guaranteed to be inside the polygon.
    # Centroid can fall outside for non-convex polygons.
    inland_point = last_valid_shrunken_polygon.representative_point()

    # The radius of the inscribed circle at this point is its distance to the boundary.
    nearest = nearest_points(inland_point, polygon.boundary)[1]
    radius = inland_point.distance(nearest)

    return (inland_point.x, inland_point.y), radius * 2




def parse_problem_file(file_path):
    """
    Parses the problem file to extract original piece geometries and bin dimensions.
    """
    original_pieces = {}
    with open(file_path, 'r') as f:
        lines = f.readlines()

    bin_width, bin_height = map(float, lines[0].strip().split())
    bin_dimension = packing_py.Rectangle(packing_py.MPointDouble(0, 0), packing_py.MPointDouble(bin_width, bin_height))

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
            original_pieces[piece_id_counter] = vertices
            piece_id_counter += 1
            
    return bin_dimension, original_pieces

def rotate_point(point, angle_degrees, center):
    """
    Rotates a point around a given center.
    """
    angle_degrees = 360 - angle_degrees
    angle_rad = math.radians(angle_degrees)
    cos_theta = math.cos(angle_rad)
    sin_theta = math.sin(angle_rad)
    
    x, y = point
    cx, cy = center
    
    new_x = cos_theta * (x - cx) - sin_theta * (y - cy) + cx
    new_y = sin_theta * (x - cx) + cos_theta * (y - cy) + cy
    
    return new_x, new_y

def get_polygon_bbox(points):
    """Calculates the bounding box of a polygon."""
    if not points:
        return 0, 0, 0, 0
    min_x = min(p[0] for p in points)
    min_y = min(p[1] for p in points)
    max_x = max(p[0] for p in points)
    max_y = max(p[1] for p in points)
    return min_x, min_y, max_x, max_y

def get_polygon_centroid(points):
    """
    Calculates the centroid of a polygon for label placement.
    """
    if not points:
        return (0, 0)
    x_sum = sum(p[0] for p in points)
    y_sum = sum(p[1] for p in points)
    return x_sum / len(points), y_sum / len(points)

class Romans:
    def __init__(self):
        self.f = {}
        self.l = {}
        self.scale = 1.0
        self.t = {}
        self._initialize_font()

    def _initialize_font(self):
        self.l[32] = 12
        self.f[0x21]="10; 5,21 5,7; 5,2 4,1 5,0 6,1 5,2"
        self.f[0x22]="16; 4,21 4,14; 12,21 12,14"
        self.f[0x23]="21; 11.5,25 4.5,-7; 17.5,25 10.5,-7; 4.5,12 18.5,12; 3.5,6 17.5,6"
        self.f[0x24]="20; 8,25 8,-4; 12,25 12,-4; 17,18 15,20 12,21 8,21 5,20 3,18 3,16 4,14 5,13 7,12 13,10 15,9 16,8 17,6 17,3 15,1 12,0 8,0 5,1 3,3"
        self.f[0x25]="24; 21,21 3,0; 8,21 10,19 10,17 9,15 7,14 5,14 3,16 3,18 4,20 6,21 8,21 10,20 13,19 16,19 19,20 21,21; 17,7 15,6 14,4 14,2 16,0 18,0 20,1 21,3 21,5 19,7 17,7"
        self.f[0x26]="26; 23,12 23,13 22,14 21,14 20,13 19,11 17,6 15,3 13,1 11,0 7,0 5,1 4,2 3,4 3,6 4,8 5,9 12,13 13,14 14,16 14,18 13,20 11,21 9,20 8,18 8,16 9,13 11,10 16,3 18,1 20,0 22,0 23,1 23,2"
        self.f[0x27]="10; 5,19 4,20 5,21 6,20 6,18 5,16 4,15"
        self.f[0x28]="14; 11,25 9,23 7,20 5,16 4,11 4,7 5,2 7,-2 9,-5 11,-7"
        self.f[0x29]="14; 3,25 5,23 7,20 9,16 10,11 10,7 9,2 7,-2 5,-5 3,-7"
        self.f[0x2A]="16; 8,21 8,9; 3,18 13,12; 13,18 3,12"
        self.f[0x2B]="26; 13,18 13,0; 4,9 22,9"
        self.f[0x2C]="10; 6,1 5,0 4,1 5,2 6,1 6,-1 5,-3 4,-4"
        self.f[0x2D]="26; 4,9 22,9"
        self.f[0x2E]="10; 5,2 4,1 5,0 6,1 5,2"
        self.f[0x2F]="22; 20,25 2,-7"
        self.f[0x30]="20; 9,21 6,20 4,17 3,12 3,9 4,4 6,1 9,0 11,0 14,1 16,4 17,9 17,12 16,17 14,20 11,21 9,21"
        self.f[0x31]="20; 6,17 8,18 11,21 11,0"
        self.f[0x32]="20; 4,16 4,17 5,19 6,20 8,21 12,21 14,20 15,19 16,17 16,15 15,13 13,10 3,0 17,0"
        self.f[0x33]="20; 5,21 16,21 10,13 13,13 15,12 16,11 17,8 17,6 16,3 14,1 11,0 8,0 5,1 4,2 3,4"
        self.f[0x34]="20; 13,21 3,7 18,7; 13,21 13,0"
        self.f[0x35]="20; 15,21 5,21 4,12 5,13 8,14 11,14 14,13 16,11 17,8 17,6 16,3 14,1 11,0 8,0 5,1 4,2 3,4"
        self.f[0x36]="20; 16,18 15,20 12,21 10,21 7,20 5,17 4,12 4,7 5,3 7,1 10,0 11,0 14,1 16,3 17,6 17,7 16,10 14,12 11,13 10,13 7,12 5,10 4,7"
        self.f[0x37]="20; 17,21 7,0; 3,21 17,21"
        self.f[0x38]="20; 8,21 5,20 4,18 4,16 5,14 7,13 11,12 14,11 16,9 17,7 17,4 16,2 15,1 12,0 8,0 5,1 4,2 3,4 3,7 4,9 6,11 9,12 13,13 15,14 16,16 16,18 15,20 12,21 8,21"
        self.f[0x39]="20; 16,14 15,11 13,9 10,8 9,8 6,9 4,11 3,14 3,15 4,18 6,20 9,21 10,21 13,20 15,18 16,14 16,9 15,4 13,1 10,0 8,0 5,1 4,3"
        self.f[0x3A]="10; 5,14 4,13 5,12 6,13 5,14; 5,2 4,1 5,0 6,1 5,2"
        self.f[0x3B]="10; 5,14 4,13 5,12 6,13 5,14; 6,1 5,0 4,1 5,2 6,1 6,-1 5,-3 4,-4"
        self.f[0x3C]="24; 20,18 4,9 20,0"
        self.f[0x3D]="26; 4,12 22,12; 4,6 22,6"
        self.f[0x3E]="24; 4,18 20,9 4,0"
        self.f[0x3F]="18; 3,16 3,17 4,19 5,20 7,21 11,21 13,20 14,19 15,17 15,15 14,13 13,12 9,10 9,7; 9,2 8,1 9,0 10,1 9,2"
        self.f[0x40]="27; 18.5,13 17.5,15 15.5,16 12.5,16 10.5,15 9.5,14 8.5,11 8.5,8 9.5,6 11.5,5 14.5,5 16.5,6 17.5,8; 12.5,16 10.5,14 9.5,11 9.5,8 10.5,6 11.5,5; 18.5,16 17.5,8 17.5,6 19.5,5 21.5,5 23.5,7 24.5,10 24.5,12 23.5,15 22.5,17 20.5,19 18.5,20 15.5,21 12.5,21 9.5,20 7.5,19 5.5,17 4.5,15 3.5,12 3.5,9 4.5,6 5.5,4 7.5,2 9.5,1 12.5,0 15.5,0 18.5,1 20.5,2 21.5,3; 19.5,16 18.5,8 18.5,6 19.5,5"
        self.f[0x41]="18; 9,21 1,0; 9,21 17,0; 4,7 14,7"
        self.f[0x42]="21; 3.5,21 3.5,0; 3.5,21 12.5,21 15.5,20 16.5,19 17.5,17 17.5,15 16.5,13 15.5,12 12.5,11; 3.5,11 12.5,11 15.5,10 16.5,9 17.5,7 17.5,4 16.5,2 15.5,1 12.5,0 3.5,0"
        self.f[0x43]="21; 18.5,16 17.5,18 15.5,20 13.5,21 9.5,21 7.5,20 5.5,18 4.5,16 3.5,13 3.5,8 4.5,5 5.5,3 7.5,1 9.5,0 13.5,0 15.5,1 17.5,3 18.5,5"
        self.f[0x44]="21; 3.5,21 3.5,0; 3.5,21 10.5,21 13.5,20 15.5,18 16.5,16 17.5,13 17.5,8 16.5,5 15.5,3 13.5,1 10.5,0 3.5,0"
        self.f[0x45]="19; 3.5,21 3.5,0; 3.5,21 16.5,21; 3.5,11 11.5,11; 3.5,0 16.5,0"
        self.f[0x46]="18; 3,21 3,0; 3,21 16,21; 3,11 11,11"
        self.f[0x47]="21; 18.5,16 17.5,18 15.5,20 13.5,21 9.5,21 7.5,20 5.5,18 4.5,16 3.5,13 3.5,8 4.5,5 5.5,3 7.5,1 9.5,0 13.5,0 15.5,1 17.5,3 18.5,5 18.5,8; 13.5,8 18.5,8"
        self.f[0x48]="22; 4,21 4,0; 18,21 18,0; 4,11 18,11"
        self.f[0x49]="8; 4,21 4,0"
        self.f[0x4A]="16; 12,21 12,5 11,2 10,1 8,0 6,0 4,1 3,2 2,5 2,7"
        self.f[0x4B]="21; 3.5,21 3.5,0; 17.5,21 3.5,7; 8.5,12 17.5,0"
        self.f[0x4C]="17; 2.5,21 2.5,0; 2.5,0 14.5,0"
        self.f[0x4D]="24; 4,21 4,0; 4,21 12,0; 20,21 12,0; 20,21 20,0"
        self.f[0x4E]="22; 4,21 4,0; 4,21 18,0; 18,21 18,0"
        self.f[0x4F]="22; 9,21 7,20 5,18 4,16 3,13 3,8 4,5 5,3 7,1 9,0 13,0 15,1 17,3 18,5 19,8 19,13 18,16 17,18 15,20 13,21 9,21"
        self.f[0x50]="21; 3.5,21 3.5,0; 3.5,21 12.5,21 15.5,20 16.5,19 17.5,17 17.5,14 16.5,12 15.5,11 12.5,10 3.5,10"
        self.f[0x51]="22; 9,21 7,20 5,18 4,16 3,13 3,8 4,5 5,3 7,1 9,0 13,0 15,1 17,3 18,5 19,8 19,13 18,16 17,18 15,20 13,21 9,21; 12,4 18,-2"
        self.f[0x52]="21; 3.5,21 3.5,0; 3.5,21 12.5,21 15.5,20 16.5,19 17.5,17 17.5,15 16.5,13 15.5,12 12.5,11 3.5,11; 10.5,11 17.5,0"
        self.f[0x53]="20; 17,18 15,20 12,21 8,21 5,20 3,18 3,16 4,14 5,13 7,12 13,10 15,9 16,8 17,6 17,3 15,1 12,0 8,0 5,1 3,3"
        self.f[0x54]="16; 8,21 8,0; 1,21 15,21"
        self.f[0x55]="22; 4,21 4,6 5,3 7,1 10,0 12,0 15,1 17,3 18,6 18,21"
        self.f[0xDC]="22; 4,21 4,6 5,3 7,1 10,0 12,0 15,1 17,3 18,6 18,21; 6,23 6,25; 16,25 16,23"
        self.f[0x56]="18; 1,21 9,0; 17,21 9,0"
        self.f[0x57]="24; 2,21 7,0; 12,21 7,0; 12,21 17,0; 22,21 17,0"
        self.f[0x58]="20; 3,21 17,0; 17,21 3,0"
        self.f[0x59]="18; 1,21 9,11 9,0; 17,21 9,11"
        self.f[0x5A]="20; 17,21 3,0; 3,21 17,21; 3,0 17,0"
        self.f[0x5B]="14; 4,25 4,-7; 5,25 5,-7; 4,25 11,25; 4,-7 11,-7"
        self.f[0x5C]="14; 0,21 14,-3"
        self.f[0x5D]="14; 9,25 9,-7; 10,25 10,-7; 3,25 10,25; 3,-7 10,-7"
        self.f[0x5E]="16; 6,15 8,18 10,15; 3,12 8,17 13,12; 8,17 8,0"
        self.f[0x5F]="16; 0,-2 16,-2"
        self.f[0x60]="10; 6,21 5,20 4,18 4,16 5,15 6,16 5,17"
        self.f[0x61]="19; 15.5,14 15.5,0; 15.5,11 13.5,13 11.5,14 8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3"
        self.f[0xe1]="19; 15.5,14 15.5,0; 15.5,11 13.5,13 11.5,14 8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3; 10,17 13,19"
        self.f[0x62]="19; 3.5,21 3.5,0; 3.5,11 5.5,13 7.5,14 10.5,14 12.5,13 14.5,11 15.5,8 15.5,6 14.5,3 12.5,1 10.5,0 7.5,0 5.5,1 3.5,3"
        self.f[0x63]="18; 15,11 13,13 11,14 8,14 6,13 4,11 3,8 3,6 4,3 6,1 8,0 11,0 13,1 15,3"
        self.f[0x64]="19; 15.5,21 15.5,0; 15.5,11 13.5,13 11.5,14 8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3"
        self.f[0x65]="18; 3,8 15,8 15,10 14,12 13,13 11,14 8,14 6,13 4,11 3,8 3,6 4,3 6,1 8,0 11,0 13,1 15,3"
        self.f[0xE9]="18; 3,8 15,8 15,10 14,12 13,13 11,14 8,14 6,13 4,11 3,8 3,6 4,3 6,1 8,0 11,0 13,1 15,3; 10,17 13,19"
        self.f[0x66]="12; 11,21 9,21 7,20 6,17 6,0; 3,14 10,14"
        self.f[0x67]="19; 15.5,14 15.5,-2 14.5,-5 13.5,-6 11.5,-7 8.5,-7 6.5,-6; 15.5,11 13.5,13 11.5,14 8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3"
        self.f[0x68]="19; 4.5,21 4.5,0; 4.5,10 7.5,13 9.5,14 12.5,14 14.5,13 15.5,10 15.5,0"
        self.f[0x69]="8; 3,21 4,20 5,21 4,22 3,21; 4,14 4,0"
        self.f[0xED]="8; 4,14 4,0; 4,17 7,19"
        self.f[0x6A]="10; 5,21 6,20 7,21 6,22 5,21; 6,14 6,-3 5,-6 3,-7 1,-7"
        self.f[0x6B]="17; 3.5,21 3.5,0; 13.5,14 3.5,4; 7.5,8 14.5,0"
        self.f[0x6C]="8; 4,21 4,0"
        self.f[0x6D]="30; 4,14 4,0; 4,10 7,13 9,14 12,14 14,13 15,10 15,0; 15,10 18,13 20,14 23,14 25,13 26,10 26,0"
        self.f[0x6E]="19; 4.5,14 4.5,0; 4.5,10 7.5,13 9.5,14 12.5,14 14.5,13 15.5,10 15.5,0"
        self.f[0xF1]="19; 4.5,14 4.5,0; 4.5,10 7.5,13 9.5,14 12.5,14 14.5,13 15.5,10 15.5,0; 6,18 14,18"
        self.f[0x6F]="19; 8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3 16.5,6 16.5,8 15.5,11 13.5,13 11.5,14 8.5,14"
        self.f[0xF3]="19; 8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3 16.5,6 16.5,8 15.5,11 13.5,13 11.5,14 8.5,14; 10,17 13,19"
        self.f[0x70]="19; 3.5,14 3.5,-7; 3.5,11 5.5,13 7.5,14 10.5,14 12.5,13 14.5,11 15.5,8 15.5,6 14.5,3 12.5,1 10.5,0 7.5,0 5.5,1 3.5,3"
        self.f[0x71]="19; 15.5,14 15.5,-7; 15.5,11 13.5,13 11.5,14 8.5,14 6.5,13 4.5,11 3.5,8 3.5,6 4.5,3 6.5,1 8.5,0 11.5,0 13.5,1 15.5,3"
        self.f[0x72]="13; 3.5,14 3.5,0; 3.5,8 4.5,11 6.5,13 8.5,14 11.5,14"
        self.f[0x73]="17; 14.5,11 13.5,13 10.5,14 7.5,14 4.5,13 3.5,11 4.5,9 6.5,8 11.5,7 13.5,6 14.5,4 14.5,3 13.5,1 10.5,0 7.5,0 4.5,1 3.5,3"
        self.f[0x74]="12; 6,21 6,4 7,1 9,0 11,0; 3,14 10,14"
        self.f[0x75]="19; 4.5,14 4.5,4 5.5,1 7.5,0 10.5,0 12.5,1 15.5,4; 15.5,14 15.5,0"
        self.f[0xFA]="19; 4.5,14 4.5,4 5.5,1 7.5,0 10.5,0 12.5,1 15.5,4; 15.5,14 15.5,0; 10,17 13,19 "
        self.f[0xFC]="19; 4.5,14 4.5,4 5.5,1 7.5,0 10.5,0 12.5,1 15.5,4; 15.5,14 15.5,0; 6.5,17 6.5,19; 13.5,19 13.5,17"
        self.f[0x76]="16; 2,14 8,0; 14,14 8,0"
        self.f[0x77]="22; 3,14 7,0; 11,14 7,0; 11,14 15,0; 19,14 15,0"
        self.f[0x78]="17; 3.5,14 14.5,0; 14.5,14 3.5,0"
        self.f[0x79]="16; 2,14 8,0; 14,14 8,0 6,-4 4,-6 2,-7 1,-7"
        self.f[0x7A]="17; 14.5,14 3.5,0; 3.5,14 14.5,14; 3.5,0 14.5,0"
        self.f[0x7B]="14; 9,25 7,24 6,23 5,21 5,19 6,17 7,16 8,14 8,12 6,10; 7,24 6,22 6,20 7,18 8,17 9,15 9,13 8,11 4,9 8,7 9,5 9,3 8,1 7,0 6,-2 6,-4 7,-6; 6,8 8,6 8,4 7,2 6,1 5,-1 5,-3 6,-5 7,-6 9,-7"
        self.f[0x7C]="8; 4,25 4,-7"
        self.f[0x7D]="14; 5,25 7,24 8,23 9,21 9,19 8,17 7,16 6,14 6,12 8,10; 7,24 8,22 8,20 7,18 6,17 5,15 5,13 6,11 10,9 6,7 5,5 5,3 6,1 7,0 8,-2 8,-4 7,-6; 8,8 6,6 6,4 7,2 8,1 9,-1 9,-3 8,-5 7,-6 5,-7"
        self.f[0x7E]="24; 3,6 3,8 4,11 6,12 8,12 10,11 14,8 16,7 18,7 20,8 21,10; 3,8 4,10 6,11 8,11 10,10 14,7 16,6 18,6 20,7 21,10 21,12"
        self.f[0x7F]="14; 6,21 4,20 3,18 3,16 4,14 6,13 8,13 10,14 11,16 11,18 10,20 8,21 6,21"
        self.f[0xD1]="22; 4,21 4,0; 4,21 18,0; 18,21 18,0; 8,22 15,22"

        for i in range(255):
            if i in self.f:
                data = self.f[i].replace(':', ';').split(';')
                #self.l[i] = int(data[0])
                paths = []
                max_x = 0
                for path_data in data[1:]:
                    path = []
                    points = path_data.strip().split(' ')
                    for point_str in points:
                        if not point_str:
                            continue
                        coords = point_str.split(',')
                        if len(coords) == 2:
                            x, y = float(coords[0]), float(coords[1])
                            path.append((x, y))
                            if x > max_x:
                                max_x = x
                    if path:
                        paths.append(path)
                self.t[i] = paths
                self.l[i] = max_x + 4

    def get_length(self, c):
        return self.l.get(c, 0) * self.scale

    def get_string_length(self, line):
        return sum(self.get_length(ord(char)) for char in line)

    def get_char(self, c):
        return self.t.get(c)

    def get_string(self, line):
        x = 0
        out = []
        for char in line:
            c = ord(char)
            ch = self.get_char(c)
            if ch:
                for path in ch:
                    new_path = []
                    for p in path:
                        new_path.append((p[0] * self.scale + x, p[1] * self.scale))
                    out.append(new_path)
            x += self.get_length(c)
        return out

def create_packing_visual_pdf(bins, bin_dimension, original_pieces_data, initial_bboxes, file_name="nesting_visualization_vector.pdf"):
    """
    Creates a PDF visualizing the nesting result with precise transformations.
    """
    c = canvas.Canvas(file_name, pagesize=(bin_dimension.width+50, bin_dimension.height+50))
    font = Romans()

    for i, bin_instance in enumerate(bins):
        print(f"Drawing Bin {i+1}...")
        c.setPageSize((bin_dimension.width+50, bin_dimension.height+50))
        c.setStrokeColor(colors.lightgrey)
        c.translate(25,48)
        c.rect(0, 0, bin_dimension.width, bin_dimension.height)
        
        for piece in bin_instance.placed_pieces:
            piece_id = piece.get_id()
            original_vertices = original_pieces_data.get(piece_id)
            initial_bbox = initial_bboxes.get(piece_id)

            if not original_vertices or not initial_bbox:
                print(f"  - Warning: Could not find data for Piece ID: {piece_id}")
                continue

            initial_bbox_center_x = initial_bbox.x + initial_bbox.width / 2
            initial_bbox_center_y = initial_bbox.y + initial_bbox.height / 2
            rotation_pivot = (initial_bbox_center_x, initial_bbox_center_y)

            rotation_angle = piece.get_rotation()
            rotated_vertices = [rotate_point(p, rotation_angle, rotation_pivot) for p in original_vertices]

            final_placed_bbox = piece.get_bounding_box()
            rotated_min_x, rotated_min_y, _, _ = get_polygon_bbox(rotated_vertices)

            translation_x = final_placed_bbox.x - rotated_min_x
            translation_y = final_placed_bbox.y - rotated_min_y

            final_vertices = [(p[0] + translation_x, p[1] + translation_y) for p in rotated_vertices]

            p = c.beginPath()
            p.moveTo(final_vertices[0][0], final_vertices[0][1])
            for point in final_vertices[1:]:
                p.lineTo(point[0], point[1])
            p.close()
            c.setFillColor(colors.lightgrey)
            c.setStrokeColor(colors.blue)
            c.setLineWidth(2)
            c.drawPath(p, fill=0)

            final_centroid, size = most_inland_point(final_vertices,10)
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
    """
    Main function to run the packing and generate the PDF.
    """
    print("--- Visualizing Nesting with PDF Output (Precise Transformations) ---")
    
    input_file = "samples/S266.txt"
    if len(sys.argv)>1: input_file=sys.argv[1]

    # 1. Parse the problem file to get original vertices for drawing.
    bin_dimension, original_pieces_vertices = parse_problem_file(input_file)
    print(f"Loaded {len(original_pieces_vertices)} pieces from {input_file}.")
    print(f"Bin dimensions: {bin_dimension.width}x{bin_dimension.height}")

    # 2. Load the problem using the C++ library to get the initial state of MArea objects.
    # This is crucial for getting the correct initial bounding boxes for rotation pivots.
    problem_for_packer = packing_py.load_pieces(input_file)

    # 3. Create a map of piece ID -> initial C++-calculated bounding box.
    initial_bboxes = {p.get_id(): p.get_bounding_box() for p in problem_for_packer.pieces}

    # 4. Run the packing algorithm.
    # A copy of the list is passed to pack to avoid modification issues within the C++ library.
    bins = packing_py.pack(list(problem_for_packer.pieces), bin_dimension)
    print(f"Packing finished. Used {len(bins)} bins.")

    # 5. Generate the PDF visualization.
    if bins:
        create_packing_visual_pdf(bins, bin_dimension, original_pieces_vertices, initial_bboxes)
    else:
        print("No bins were used, nothing to visualize.")

if __name__ == "__main__":
    main()
