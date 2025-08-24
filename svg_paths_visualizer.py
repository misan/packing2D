import matplotlib.pyplot as plt
import matplotlib.patches as patches
from matplotlib.path import Path
import numpy as np

def read_svg_paths(filename):
    """Read SVG paths from a file and return them as a list of strings."""
    with open(filename, 'r') as file:
        content = file.read()
    
    # Split the content into individual paths (assuming paths are separated by blank lines)
    path_strings = [path.strip() for path in content.strip().split('\n\n') if path.strip()]
    return path_strings

def parse_svg_path(path_str):
    """Parse an SVG path string into vertices and codes for matplotlib."""
    # Split the path into tokens
    tokens = []
    current_token = ""
    
    for char in path_str:
        if char in 'MLCZ':  # SVG path commands
            if current_token:
                tokens.append(current_token)
                current_token = ""
            tokens.append(char)
        elif char.isspace() or char == ',':
            if current_token:
                tokens.append(current_token)
                current_token = ""
        else:
            current_token += char
    
    if current_token:
        tokens.append(current_token)
    
    # Process tokens to create vertices and codes
    vertices = []
    codes = []
    i = 0
    
    while i < len(tokens):
        token = tokens[i]
        
        if token == 'M':  # Move to
            x = float(tokens[i+1])
            y = float(tokens[i+2])
            vertices.append((x, y))
            codes.append(Path.MOVETO)
            i += 3
        elif token == 'L':  # Line to
            x = float(tokens[i+1])
            y = float(tokens[i+2])
            vertices.append((x, y))
            codes.append(Path.LINETO)
            i += 3
        elif token == 'C':  # Cubic Bezier curve
            x1 = float(tokens[i+1])
            y1 = float(tokens[i+2])
            x2 = float(tokens[i+3])
            y2 = float(tokens[i+4])
            x = float(tokens[i+5])
            y = float(tokens[i+6])
            vertices.append((x1, y1))
            vertices.append((x2, y2))
            vertices.append((x, y))
            codes.append(Path.CURVE4)
            codes.append(Path.CURVE4)
            codes.append(Path.CURVE4)
            i += 7
        elif token == 'Z':  # Close path
            codes.append(Path.CLOSEPOLY)
            i += 1
        else:
            i += 1
    
    return np.array(vertices), np.array(codes)

def plot_svg_paths(paths, colors=None):
    """Plot SVG paths using matplotlib."""
    fig, ax = plt.subplots(figsize=(12, 10))
    
    if colors is None:
        # Generate a colormap for the paths
        colors = plt.cm.viridis(np.linspace(0, 1, len(paths)))
    
    for i, path_str in enumerate(paths):
        try:
            vertices, codes = parse_svg_path(path_str)
            path = Path(vertices, codes)
            
            # Create a patch for the path
            patch = patches.PathPatch(
                path, 
                facecolor='none', 
                edgecolor=colors[i % len(colors)], 
                linewidth=1.5,
                alpha=0.8
            )
            ax.add_patch(patch)
        except Exception as e:
            print(f"Error processing path {i}: {e}")
    
    # Set axis limits with some padding
    ax.set_xlim(-50, 1050)
    ax.set_ylim(-50, 1050)
    
    # Equal aspect ratio ensures the paths look correct
    ax.set_aspect('equal')
    
    # Remove axes for a cleaner look
    ax.set_axis_off()
    
    plt.title('SVG Paths Visualization', fontsize=16)
    plt.tight_layout()
    plt.show()

# Main execution
if __name__ == "__main__":
    # Read the paths from the file
    paths = read_svg_paths('paths.txt')
    print(f"Read {len(paths)} paths from the file.")
    
    # Plot the paths
    plot_svg_paths(paths)