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

def create_packing_visual_pdf(bins, bin_dimension, original_pieces_data, initial_bboxes, file_name="nesting_visualization.pdf"):
    """
    Creates a PDF visualizing the nesting result with precise transformations.
    """
    c = canvas.Canvas(file_name, pagesize=(bin_dimension.width+50, bin_dimension.height+50))

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

            # 1. Determine the rotation pivot: center of the original piece's bounding box.
            # This is crucial because the C++ library rotates around this point.
            initial_bbox_center_x = initial_bbox.x + initial_bbox.width / 2
            initial_bbox_center_y = initial_bbox.y + initial_bbox.height / 2
            rotation_pivot = (initial_bbox_center_x, initial_bbox_center_y)

            # 2. Rotate the original vertices around this pivot.
            rotation_angle = piece.get_rotation()
            rotated_vertices = [rotate_point(p, rotation_angle, rotation_pivot) for p in original_vertices]

            # 3. Get the final bounding box of the placed piece from the C++ library.
            final_placed_bbox = piece.get_bounding_box()

            # 4. Calculate the bounding box of the newly rotated vertices.
            rotated_min_x, rotated_min_y, _, _ = get_polygon_bbox(rotated_vertices)

            # 5. Calculate the translation needed to move the rotated shape
            #    so its bottom-left corner matches the final placed position.
            translation_x = final_placed_bbox.x - rotated_min_x
            translation_y = final_placed_bbox.y - rotated_min_y

            # 6. Apply the translation to get the final vertices for drawing.
            final_vertices = [(p[0] + translation_x, p[1] + translation_y) for p in rotated_vertices]

            # Draw the final polygon
            p = c.beginPath()
            p.moveTo(final_vertices[0][0], final_vertices[0][1])
            for point in final_vertices[1:]:
                p.lineTo(point[0], point[1])
            p.close()
            c.setFillColor(colors.lightgrey)
            c.setStrokeColor(colors.blue)
            c.setLineWidth(2)
            c.drawPath(p, fill=0)

            # Draw the piece ID at the centroid of the final shape
            final_centroid, size = most_inland_point(final_vertices,10)
            c.setFillColor(colors.black)
            x,y = get_center_offsets(c,str(piece_id), font_size = size/2 )
            c.drawString(final_centroid[0]-x, final_centroid[1]-y, str(piece_id))
            # print(piece_id, size, final_centroid)

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
