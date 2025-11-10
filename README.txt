Tetris PBR

A modern 3D Tetris game with Physically-Based Rendering (PBR) implemented in C++ using OpenGL, GLFW, and GLM.

🎮 Features

Classic Tetris gameplay with accurate piece rotation and movement
Physically-Based Rendering (PBR) for realistic lighting and materials
Modern OpenGL 3.3+ rendering pipeline
Smooth controls with proper input handling
Score system with line clearing bonuses
Game state management with restart functionality
Wall kick system for piece rotation
🎯 Gameplay

Clear lines by filling horizontal rows with blocks
Different tetromino pieces (I, O, T, S, Z, J, L) with unique colors
Increasing difficulty as you progress
Real-time score tracking
🕹️ Controls

Key	Action
← →	Move piece left/right
↑	Rotate piece clockwise
↓	Soft drop (move down faster)
Space	Hard drop (instant drop)
R	Restart game (after game over)
🛠️ Requirements

Development Dependencies

C++ Compiler with C++11 support
CMake 3.10 or higher
Homebrew (for macOS)
Libraries

GLFW 3.0+
GLM (OpenGL Mathematics)
Glad (OpenGL loader)
📥 Installation & Setup

1. Install Dependencies

bash
# Install Homebrew (if not already installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install required libraries
brew install glfw glm pkg-config cmake
2. Download Glad

Visit glad.dav1d.de
Configure with:

Language: C/C++
Specification: OpenGL
API: gl Version 3.3
Profile: Core
Check "Generate a loader"
Click "Generate" and download the ZIP
Extract to libs/glad/ in the project root
3. Project Structure

text
tetris_pbr/
├── CMakeLists.txt
├── README.md
├── src/
│   └── main.cpp
└── libs/
    └── glad/
        ├── include/
        └── src/
            └── glad.c
🚀 Building the Game

Using CMake (Recommended)

bash
# Create build directory
mkdir build
cd build

# Generate build files
cmake ..

# Compile the game
make

# Run the game
./TetrisPBR
Manual Compilation

bash
g++ -std=c++11 src/main.cpp libs/glad/src/glad.c \
    -Ilibs/glad/include \
    -I/usr/local/include \
    -L/usr/local/lib \
    -lglfw -lm \
    -o TetrisPBR \
    -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
🎨 Technical Features

Graphics

PBR Shading with metallic-roughness workflow
GGX BRDF for accurate specular highlights
HDR Tone Mapping and gamma correction
Multiple Light Sources for dynamic lighting
Real-time Rendering at 60+ FPS
Game Engine

Proper Collision Detection with wall kicks
Piece Rotation System following Tetris guidelines
Line Clearing with cascading row updates
Game State Management with pause/restart functionality
Random Piece Generator with uniform distribution
🐛 Troubleshooting

Common Issues

"GLFW not found"

bash
brew reinstall glfw
OpenGL version errors

Ensure your system supports OpenGL 3.3+
Update graphics drivers if needed
Build failures on macOS

bash
xcode-select --install
Permission denied when running

Grant execution permission: chmod +x TetrisPBR
📁 Project Structure

text
src/
└── main.cpp                 # Main game implementation
    ├── Shader Code          # PBR vertex and fragment shaders
    ├── Game Logic           # Tetris mechanics and rules
    ├── Rendering System     # OpenGL rendering pipeline
    └── Input Handling       # GLFW keyboard input

libs/
└── glad/                    # OpenGL function loader
    ├── include/GL/glad.h
    └── src/glad.c
🎵 Game Rules

Scoring: 100 points per line cleared
Level: Increases as more lines are cleared
Game Over: When new pieces cannot spawn
Piece Storage: Not implemented (classic mode)
🔧 Customization

You can modify the following constants in main.cpp:

cpp
const int BOARD_W = 10;      // Board width
const int BOARD_H = 20;      // Board height  
const float fallInterval = 1.0f; // Fall speed (seconds)
📄 License

This project is open source and available under the MIT License.

🙏 Acknowledgments

Tetris concept by Alexey Pajitnov
PBR implementation based on LearnOpenGL
GLFW for window management
GLM for mathematics
Glad for OpenGL loading
Enjoy playing Tetris PBR! 🎮

For issues and contributions, please open an issue or pull request.






# Tetris with PBR, HDR and Bloom

Modern Tetris implementation with Physically Based Rendering, HDR tone mapping and Bloom effects.

## Features
- **PBR Rendering** with albedo, normal, and roughness maps
- **HDR Pipeline** with tone mapping
- **Bloom Post-processing**
- Real-time lighting with 4 light sources
- Material switching (3 different materials)

## Requirements
- OpenGL 3.3+
- GLFW
- GLM
- Glad

## Build Instructions

### Linux
```bash
mkdir build && cd build
cmake ..
make
./TetrisPBR