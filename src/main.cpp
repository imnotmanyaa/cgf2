// src/main.cpp
// Tetris game with PBR rendering - Complete working version with FIXED rotation
// Mac (GLFW + Glad + GLM). Build with provided CMakeLists.txt.

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <array>
#include <iostream>
#include <chrono>
#include <random>

// --------------------------- Простая реализация шейдера ----------------------------
static const char* pbrVertex = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 WorldPos;
out vec3 Normal;
void main(){
    WorldPos = vec3(uModel * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uProj * uView * vec4(WorldPos, 1.0);
}
)glsl";

static const char* pbrFragment = R"glsl(
#version 330 core
in vec3 WorldPos;
in vec3 Normal;
out vec4 FragColor;
uniform vec3 camPos;
uniform vec3 albedo; // base color
uniform float metallic;
uniform float roughness;
uniform float ao;
const float PI = 3.14159265359;

// lights (single white point for simplicity)
uniform vec3 lightPositions[4];
uniform vec3 lightColors[4];

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N,H), 0.0);
    float NdotH2 = NdotH*NdotH;
    float denom = (NdotH2*(a2-1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.000001);
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N,V), 0.0);
    float NdotL = max(dot(N,L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

void main(){
    vec3 N = normalize(Normal);
    vec3 V = normalize(camPos - WorldPos);
    vec3 R = reflect(-V, N);
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    vec3 Lo = vec3(0.0);
    for(int i=0;i<4;i++){
        vec3 L = normalize(lightPositions[i] - WorldPos);
        vec3 H = normalize(V + L);
        float distance = length(lightPositions[i] - WorldPos);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance = lightColors[i] * attenuation;
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 numerator = NDF * G * F;
        float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denom;
        float NdotL = max(dot(N, L), 0.0);
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }
    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color = ambient + Lo;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));
    FragColor = vec4(color, 1.0);
}
)glsl";

// --------------------------- End shaders ----------------------------

// Helper: compile shader
GLuint compileShader(GLenum type, const char* src){
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){
        char log[1024];
        glGetShaderInfoLog(s, 1024, nullptr, log);
        std::cerr << "Shader compile error: " << log << std::endl;
    }
    return s;
}

// Helper: link program
GLuint makeProgram(const char* vsSrc, const char* fsSrc){
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    int ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok){
        char log[1024];
        glGetProgramInfoLog(p, 1024, nullptr, log);
        std::cerr << "Program link error: " << log << std::endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

// Cube data (positions + normals)
float cubeVertices[] = {
    -0.5f,-0.5f, 0.5f, 0.0f,0.0f,1.0f,
     0.5f,-0.5f, 0.5f, 0.0f,0.0f,1.0f,
     0.5f, 0.5f, 0.5f, 0.0f,0.0f,1.0f,
     0.5f, 0.5f, 0.5f, 0.0f,0.0f,1.0f,
    -0.5f, 0.5f, 0.5f, 0.0f,0.0f,1.0f,
    -0.5f,-0.5f, 0.5f, 0.0f,0.0f,1.0f,
    -0.5f,-0.5f,-0.5f, 0.0f,0.0f,-1.0f,
    -0.5f, 0.5f,-0.5f, 0.0f,0.0f,-1.0f,
     0.5f, 0.5f,-0.5f, 0.0f,0.0f,-1.0f,
     0.5f, 0.5f,-0.5f, 0.0f,0.0f,-1.0f,
     0.5f,-0.5f,-0.5f, 0.0f,0.0f,-1.0f,
    -0.5f,-0.5f,-0.5f, 0.0f,0.0f,-1.0f,
    -0.5f, 0.5f,-0.5f, -1.0f,0.0f,0.0f,
    -0.5f, 0.5f, 0.5f, -1.0f,0.0f,0.0f,
    -0.5f,-0.5f, 0.5f, -1.0f,0.0f,0.0f,
    -0.5f,-0.5f, 0.5f, -1.0f,0.0f,0.0f,
    -0.5f,-0.5f,-0.5f, -1.0f,0.0f,0.0f,
    -0.5f, 0.5f,-0.5f, -1.0f,0.0f,0.0f,
     0.5f, 0.5f,-0.5f, 1.0f,0.0f,0.0f,
     0.5f,-0.5f, 0.5f, 1.0f,0.0f,0.0f,
     0.5f, 0.5f, 0.5f, 1.0f,0.0f,0.0f,
     0.5f,-0.5f, 0.5f, 1.0f,0.0f,0.0f,
     0.5f, 0.5f,-0.5f, 1.0f,0.0f,0.0f,
     0.5f,-0.5f,-0.5f, 1.0f,0.0f,0.0f,
    -0.5f,-0.5f,-0.5f, 0.0f,-1.0f,0.0f,
     0.5f,-0.5f,-0.5f, 0.0f,-1.0f,0.0f,
     0.5f,-0.5f, 0.5f, 0.0f,-1.0f,0.0f,
     0.5f,-0.5f, 0.5f, 0.0f,-1.0f,0.0f,
    -0.5f,-0.5f, 0.5f, 0.0f,-1.0f,0.0f,
    -0.5f,-0.5f,-0.5f, 0.0f,-1.0f,0.0f,
    -0.5f, 0.5f,-0.5f, 0.0f,1.0f,0.0f,
    -0.5f, 0.5f, 0.5f, 0.0f,1.0f,0.0f,
     0.5f, 0.5f, 0.5f, 0.0f,1.0f,0.0f,
     0.5f, 0.5f, 0.5f, 0.0f,1.0f,0.0f,
     0.5f, 0.5f,-0.5f, 0.0f,1.0f,0.0f,
    -0.5f, 0.5f,-0.5f, 0.0f,1.0f,0.0f
};

// --------------------------- Tetris logic ----------------------------
const int BOARD_W = 10;
const int BOARD_H = 20;

struct PieceDef {
    std::vector<glm::ivec2> blocks;
    glm::vec3 color;
};

std::vector<PieceDef> PIECES;

// Game state
int board[BOARD_H][BOARD_W] = {0};
PieceDef currentPiece;
glm::ivec2 currentPos;
float fallTime = 0.0f;
const float fallInterval = 1.0f; // 1 second per fall
bool gameOver = false;
int score = 0;

// Random generator
std::random_device rd;
std::mt19937 gen(rd());
std::uniform_int_distribution<> pieceDist(0, 6);

// Input handling
bool keysProcessed[512] = {false};

void initPieces(){
    PIECES = {
        {{{ -1,0 }, {0,0}, {1,0}, {2,0} }, {0.0f,0.8f,1.0f}}, // I
        {{{0,0},{1,0},{0,1},{1,1} }, {1.0f,0.9f,0.0f}},       // O
        {{{ -1,0 }, {0,0}, {1,0}, {0,1} }, {0.8f,0.0f,0.8f}}, // T
        {{{ -1,0 }, {0,0}, {0,1}, {1,1} }, {0.0f,0.9f,0.0f}}, // S
        {{{ 1,0 }, {0,0}, {0,1}, {-1,1} }, {0.9f,0.0f,0.0f}}, // Z
        {{{ -1,0 }, {0,0}, {1,0}, {1,1} }, {0.0f,0.0f,0.9f}}, // J
        {{{ -1,0 }, {0,0}, {1,0}, {-1,1} }, {1.0f,0.5f,0.0f}}  // L
    };
}

void spawnNewPiece() {
    int pieceIndex = pieceDist(gen);
    currentPiece = PIECES[pieceIndex];
    currentPos = glm::ivec2(BOARD_W / 2 - 1, 0);
    
    // Check if game over
    for (const auto& block : currentPiece.blocks) {
        int x = currentPos.x + block.x;
        int y = currentPos.y + block.y;
        if (y >= 0 && board[y][x] != 0) {
            gameOver = true;
            std::cout << "Game Over! Final Score: " << score << std::endl;
            break;
        }
    }
}

bool isValidMove(const glm::ivec2& newPos, const std::vector<glm::ivec2>& blocks) {
    for (const auto& block : blocks) {
        int x = newPos.x + block.x;
        int y = newPos.y + block.y;
        
        if (x < 0 || x >= BOARD_W || y >= BOARD_H) {
            return false;
        }
        if (y >= 0 && board[y][x] != 0) {
            return false;
        }
    }
    return true;
}

void rotatePiece() {
    // Don't rotate O piece (square)
    bool isOPiece = true;
    for (const auto& block : currentPiece.blocks) {
        if (!(block.x >= 0 && block.x <= 1 && block.y >= 0 && block.y <= 1)) {
            isOPiece = false;
            break;
        }
    }
    if (isOPiece) return;

    // Create rotated version using proper Tetris rotation logic
    std::vector<glm::ivec2> rotated = currentPiece.blocks;
    
    // Apply rotation matrix (90 degrees clockwise)
    for (auto& block : rotated) {
        // For Tetris, we rotate around (0,0) as the center
        int newX = block.y;
        int newY = -block.x;
        block.x = newX;
        block.y = newY;
    }
    
    // Test rotation with wall kicks
    std::vector<glm::ivec2> testOffsets = {
        {0, 0},   // No offset
        {1, 0},   // Right
        {-1, 0},  // Left
        {0, 1},   // Down
        {0, -1},  // Up
        {1, 1},   // Right + Down
        {-1, 1},  // Left + Down
        {1, -1},  // Right + Up  
        {-1, -1}  // Left + Up
    };
    
    // Try all wall kick offsets
    for (const auto& offset : testOffsets) {
        glm::ivec2 testPos = currentPos + offset;
        if (isValidMove(testPos, rotated)) {
            currentPiece.blocks = rotated;
            currentPos = testPos;
            return;
        }
    }
}

void mergePiece() {
    for (const auto& block : currentPiece.blocks) {
        int x = currentPos.x + block.x;
        int y = currentPos.y + block.y;
        if (y >= 0) {
            board[y][x] = 1; // Mark as occupied
        }
    }
}

void clearLines() {
    int linesCleared = 0;
    for (int y = BOARD_H - 1; y >= 0; y--) {
        bool lineComplete = true;
        for (int x = 0; x < BOARD_W; x++) {
            if (board[y][x] == 0) {
                lineComplete = false;
                break;
            }
        }
        
        if (lineComplete) {
            linesCleared++;
            // Move all lines above down
            for (int yy = y; yy > 0; yy--) {
                for (int x = 0; x < BOARD_W; x++) {
                    board[yy][x] = board[yy-1][x];
                }
            }
            // Clear top line
            for (int x = 0; x < BOARD_W; x++) {
                board[0][x] = 0;
            }
            y++; // Check same line again
        }
    }
    
    if (linesCleared > 0) {
        score += linesCleared * 100;
        std::cout << "Score: " << score << " (Cleared " << linesCleared << " lines)" << std::endl;
    }
}

void updateGame(float deltaTime) {
    if (gameOver) return;
    
    fallTime += deltaTime;
    if (fallTime >= fallInterval) {
        glm::ivec2 newPos = currentPos;
        newPos.y += 1;
        
        if (isValidMove(newPos, currentPiece.blocks)) {
            currentPos = newPos;
        } else {
            mergePiece();
            clearLines();
            spawnNewPiece();
        }
        
        fallTime = 0.0f;
    }
}

void processInput(GLFWwindow* window) {
    if (gameOver) {
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !keysProcessed[GLFW_KEY_R]) {
            // Reset game
            for (int y = 0; y < BOARD_H; y++) {
                for (int x = 0; x < BOARD_W; x++) {
                    board[y][x] = 0;
                }
            }
            score = 0;
            gameOver = false;
            spawnNewPiece();
            std::cout << "Game restarted!" << std::endl;
            keysProcessed[GLFW_KEY_R] = true;
        }
        
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE) {
            keysProcessed[GLFW_KEY_R] = false;
        }
        return;
    }
    
    // Movement controls with key repeat prevention
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS && !keysProcessed[GLFW_KEY_LEFT]) {
        glm::ivec2 newPos = currentPos;
        newPos.x -= 1;
        if (isValidMove(newPos, currentPiece.blocks)) {
            currentPos = newPos;
        }
        keysProcessed[GLFW_KEY_LEFT] = true;
    }
    
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_RELEASE) {
        keysProcessed[GLFW_KEY_LEFT] = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS && !keysProcessed[GLFW_KEY_RIGHT]) {
        glm::ivec2 newPos = currentPos;
        newPos.x += 1;
        if (isValidMove(newPos, currentPiece.blocks)) {
            currentPos = newPos;
        }
        keysProcessed[GLFW_KEY_RIGHT] = true;
    }
    
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_RELEASE) {
        keysProcessed[GLFW_KEY_RIGHT] = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !keysProcessed[GLFW_KEY_DOWN]) {
        glm::ivec2 newPos = currentPos;
        newPos.y += 1;
        if (isValidMove(newPos, currentPiece.blocks)) {
            currentPos = newPos;
        }
        keysProcessed[GLFW_KEY_DOWN] = true;
    }
    
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        keysProcessed[GLFW_KEY_DOWN] = false;
    }
    
    // Rotation
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !keysProcessed[GLFW_KEY_UP]) {
        rotatePiece();
        keysProcessed[GLFW_KEY_UP] = true;
    }
    
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        keysProcessed[GLFW_KEY_UP] = false;
    }
    
    // Hard drop
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && !keysProcessed[GLFW_KEY_SPACE]) {
        glm::ivec2 newPos = currentPos;
        while (isValidMove(newPos, currentPiece.blocks)) {
            currentPos = newPos;
            newPos.y += 1;
        }
        mergePiece();
        clearLines();
        spawnNewPiece();
        keysProcessed[GLFW_KEY_SPACE] = true;
    }
    
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_RELEASE) {
        keysProcessed[GLFW_KEY_SPACE] = false;
    }
}

void drawCube(const glm::mat4& model, const glm::vec3& color, GLuint shader) {
    glUniformMatrix4fv(glGetUniformLocation(shader, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform3f(glGetUniformLocation(shader, "albedo"), color.r, color.g, color.b);
    glUniform1f(glGetUniformLocation(shader, "metallic"), 0.1f);
    glUniform1f(glGetUniformLocation(shader, "roughness"), 0.7f);
    glUniform1f(glGetUniformLocation(shader, "ao"), 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

// --------------------------- Main ----------------------------
int main(){
    if(!glfwInit()){
        std::cerr << "GLFW init failed\n";
        return -1;
    }
    
    // Configure GLFW for macOS
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    
    GLFWwindow* win = glfwCreateWindow(800,600,"Tetris PBR - Complete Game",nullptr,nullptr);
    if(!win){ 
        glfwTerminate(); 
        return -1; 
    }
    glfwMakeContextCurrent(win);
    
    // Initialize Glad
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize Glad\n";
        return -1;
    }

    GLuint shader = makeProgram(pbrVertex, pbrFragment);

    // Cube VAO
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glEnable(GL_DEPTH_TEST);

    // Initialize game
    initPieces();
    spawnNewPiece();
    
    std::cout << "Tetris PBR Started!" << std::endl;
    std::cout << "Controls: LEFT/RIGHT - Move, UP - Rotate, DOWN - Soft Drop, SPACE - Hard Drop, R - Restart" << std::endl;
    std::cout << "Note: O piece (square) cannot be rotated (as in original Tetris)" << std::endl;

    double lastTime = glfwGetTime();
    
    while(!glfwWindowShouldClose(win)){
        double currentTime = glfwGetTime();
        double deltaTime = currentTime - lastTime;
        lastTime = currentTime;
        
        processInput(win);
        updateGame(deltaTime);

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shader);
        glBindVertexArray(VAO);
        
        // Set camera and lighting
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 800.0f/600.0f, 0.1f, 100.0f);
        glm::mat4 view = glm::lookAt(
            glm::vec3(BOARD_W/2, BOARD_H/2, 25.0f), 
            glm::vec3(BOARD_W/2, BOARD_H/2, 0.0f), 
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        
        glUniformMatrix4fv(glGetUniformLocation(shader, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniformMatrix4fv(glGetUniformLocation(shader, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniform3f(glGetUniformLocation(shader, "camPos"), BOARD_W/2, BOARD_H/2, 25.0f);
        
        // Lights
        std::array<glm::vec3,4> lightPos = { 
            glm::vec3(BOARD_W/2, BOARD_H/2, 20.0f), 
            glm::vec3(-5, 10, 15), 
            glm::vec3(BOARD_W + 5, 10, 15), 
            glm::vec3(BOARD_W/2, BOARD_H + 5, 15) 
        };
        std::array<glm::vec3,4> lightCol = { 
            glm::vec3(200.0f), 
            glm::vec3(100.0f), 
            glm::vec3(100.0f), 
            glm::vec3(100.0f) 
        };
        
        for(int i = 0; i < 4; i++){
            glUniform3fv(glGetUniformLocation(shader, ("lightPositions[" + std::to_string(i) + "]").c_str()), 1, &lightPos[i][0]);
            glUniform3fv(glGetUniformLocation(shader, ("lightColors[" + std::to_string(i) + "]").c_str()), 1, &lightCol[i][0]);
        }

        // Draw board blocks
        for (int y = 0; y < BOARD_H; y++) {
            for (int x = 0; x < BOARD_W; x++) {
                if (board[y][x] != 0) {
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, BOARD_H - y - 1, 0.0f));
                    drawCube(model, glm::vec3(0.7f, 0.7f, 0.7f), shader);
                }
            }
        }

        // Draw current piece
        if (!gameOver) {
            for (const auto& block : currentPiece.blocks) {
                int x = currentPos.x + block.x;
                int y = currentPos.y + block.y;
                if (y >= 0) {
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, BOARD_H - y - 1, 0.0f));
                    drawCube(model, currentPiece.color, shader);
                }
            }
        }

        // Draw grid (optional)
        for (int x = 0; x <= BOARD_W; x++) {
            for (int y = 0; y <= BOARD_H; y++) {
                glm::mat4 model = glm::scale(
                    glm::translate(glm::mat4(1.0f), glm::vec3(x - 0.5f, BOARD_H - y - 0.5f, -0.1f)),
                    glm::vec3(1.0f, 1.0f, 0.05f)
                );
                drawCube(model, glm::vec3(0.3f, 0.3f, 0.3f), shader);
            }
        }

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shader);

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}