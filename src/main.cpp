// main.cpp
// Tetris PBR + HDR + Bloom, single-file.
// Убрано отображение счета (score). Оставлено только PREVIEW next piece.
// Зависимости: glad, glfw, glm. OpenGL 3.3 Core.

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <array>
#include <iostream>
#include <random>
#include <string>
#include <map>
#include <chrono>
#include <cstring>

// --------------------------- SHADERS ----------------------------

// PBR vertex
static const char* pbrVertex = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 WorldPos;
out vec3 Normal;
out vec2 TexCoord;

void main(){
    WorldPos = vec3(uModel * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(uModel))) * aNormal;
    TexCoord = aPos.xy + vec2(0.5);
    gl_Position = uProj * uView * vec4(WorldPos, 1.0);
}
)glsl";

// PBR fragment
static const char* pbrFragment = R"glsl(
#version 330 core
in vec3 WorldPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

uniform vec3 camPos;

uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D roughnessMap;

uniform vec3 albedo;
uniform float metallic;
uniform float ao;
uniform int uUseAlbedoMap;

uniform vec3 lightPositions[4];
uniform vec3 lightColors[4];

const float PI = 3.14159265359;

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N,H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 1e-6);
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N,V), 0.0);
    float NdotL = max(dot(N,L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 getNormalFromMap(vec3 N) {
    vec3 nm = texture(normalMap, TexCoord).rgb;
    nm = nm * 2.0 - 1.0;
    // blend sampled normal with geometry normal (no tangents available)
    return normalize(mix(N, normalize(vec3(nm.x, nm.y, nm.z)), 0.9));
}

void main(){
    vec3 sampledAlbedo = vec3(1.0);
    if (uUseAlbedoMap == 1) sampledAlbedo = texture(albedoMap, TexCoord).rgb;
    float roughness = clamp(texture(roughnessMap, TexCoord).r, 0.04, 1.0);

    vec3 N = normalize(Normal);
    N = getNormalFromMap(N);
    vec3 V = normalize(camPos - WorldPos);

    vec3 baseColor = sampledAlbedo * albedo;
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, baseColor, metallic);

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        vec3 L = normalize(lightPositions[i] - WorldPos);
        vec3 H = normalize(V + L);
        float distance = length(lightPositions[i] - WorldPos);
        float attenuation = 1.0 / (distance * distance + 1.0);
        vec3 radiance = lightColors[i] * attenuation;

        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 1e-6;
        vec3 specular = numerator / denom;

        float NdotL = max(dot(N, L), 0.0);
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        Lo += (kD * baseColor / PI + specular) * radiance * NdotL;
    }

    vec3 ambient = vec3(0.03) * baseColor * ao;
    vec3 color = ambient + Lo;

    // tone mapping + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));
    FragColor = vec4(color, 1.0);
}
)glsl";

// Fullscreen quad vertex (pos + uv)
static const char* quadVertex = R"glsl(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

// Bright-pass
static const char* brightFragment = R"glsl(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D scene;
uniform float threshold;
void main(){
    vec3 c = texture(scene, vUV).rgb;
    float brightness = max(max(c.r, c.g), c.b);
    if (brightness > threshold) FragColor = vec4(c, 1.0);
    else FragColor = vec4(0.0);
}
)glsl";

// Blur (Gaussian)
static const char* blurFragment = R"glsl(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D image;
uniform int horizontal;
const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
void main(){
    vec2 texelSize = 1.0 / vec2(textureSize(image, 0));
    vec3 result = texture(image, vUV).rgb * weights[0];
    for (int i=1;i<5;i++){
        vec2 off = horizontal == 1 ? vec2(texelSize.x * float(i), 0.0) : vec2(0.0, texelSize.y * float(i));
        result += texture(image, vUV + off).rgb * weights[i];
        result += texture(image, vUV - off).rgb * weights[i];
    }
    FragColor = vec4(result, 1.0);
}
)glsl";

// Final composite (scene + bloom)
static const char* finalFragment = R"glsl(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D scene;
uniform sampler2D bloom;
uniform float bloomFactor;
void main(){
    vec3 hdr = texture(scene, vUV).rgb;
    vec3 b = texture(bloom, vUV).rgb;
    vec3 col = hdr + b * bloomFactor;
    col = col / (col + vec3(1.0));
    col = pow(col, vec3(1.0/2.2));
    FragColor = vec4(col, 1.0);
}
)glsl";

// UI shaders (simple ortho quads)
static const char* uiVertex = R"glsl(
#version 330 core
layout(location=0) in vec2 aPos;
uniform mat4 uOrtho;
uniform mat4 uModel;
void main(){
    gl_Position = uOrtho * uModel * vec4(aPos, 0.0, 1.0);
}
)glsl";
static const char* uiFragment = R"glsl(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main(){
    FragColor = vec4(uColor, 1.0);
}
)glsl";

// --------------------------- HELPERS ----------------------------
GLuint compileShader(GLenum type, const char* src){
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){
        char log[2048];
        glGetShaderInfoLog(s, 2048, nullptr, log);
        std::cerr << "Shader compile error: " << log << std::endl;
    }
    return s;
}
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
        char log[2048];
        glGetProgramInfoLog(p, 2048, nullptr, log);
        std::cerr << "Program link error: " << log << std::endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

// --------------------------- GEOMETRY ----------------------------
// Cube vertices (position + normal) 36 verts
float cubeVertices[] = {
    -0.5f,-0.5f, 0.5f,   0.0f,0.0f,1.0f,
     0.5f,-0.5f, 0.5f,   0.0f,0.0f,1.0f,
     0.5f, 0.5f, 0.5f,   0.0f,0.0f,1.0f,
     0.5f, 0.5f, 0.5f,   0.0f,0.0f,1.0f,
    -0.5f, 0.5f, 0.5f,   0.0f,0.0f,1.0f,
    -0.5f,-0.5f, 0.5f,   0.0f,0.0f,1.0f,
    -0.5f,-0.5f,-0.5f,   0.0f,0.0f,-1.0f,
    -0.5f, 0.5f,-0.5f,   0.0f,0.0f,-1.0f,
     0.5f, 0.5f,-0.5f,   0.0f,0.0f,-1.0f,
     0.5f, 0.5f,-0.5f,   0.0f,0.0f,-1.0f,
     0.5f,-0.5f,-0.5f,   0.0f,0.0f,-1.0f,
    -0.5f,-0.5f,-0.5f,   0.0f,0.0f,-1.0f,
    -0.5f, 0.5f,-0.5f,  -1.0f,0.0f,0.0f,
    -0.5f, 0.5f, 0.5f,  -1.0f,0.0f,0.0f,
    -0.5f,-0.5f, 0.5f,  -1.0f,0.0f,0.0f,
    -0.5f,-0.5f, 0.5f,  -1.0f,0.0f,0.0f,
    -0.5f,-0.5f,-0.5f,  -1.0f,0.0f,0.0f,
    -0.5f, 0.5f,-0.5f,  -1.0f,0.0f,0.0f,
     0.5f, 0.5f,-0.5f,   1.0f,0.0f,0.0f,
     0.5f,-0.5f, 0.5f,   1.0f,0.0f,0.0f,
     0.5f, 0.5f, 0.5f,   1.0f,0.0f,0.0f,
     0.5f,-0.5f, 0.5f,   1.0f,0.0f,0.0f,
     0.5f, 0.5f,-0.5f,   1.0f,0.0f,0.0f,
     0.5f,-0.5f,-0.5f,   1.0f,0.0f,0.0f,
    -0.5f,-0.5f,-0.5f,   0.0f,-1.0f,0.0f,
     0.5f,-0.5f,-0.5f,   0.0f,-1.0f,0.0f,
     0.5f,-0.5f, 0.5f,   0.0f,-1.0f,0.0f,
     0.5f,-0.5f, 0.5f,   0.0f,-1.0f,0.0f,
    -0.5f,-0.5f, 0.5f,   0.0f,-1.0f,0.0f,
    -0.5f,-0.5f,-0.5f,   0.0f,-1.0f,0.0f,
    -0.5f, 0.5f,-0.5f,   0.0f,1.0f,0.0f,
    -0.5f, 0.5f, 0.5f,   0.0f,1.0f,0.0f,
     0.5f, 0.5f, 0.5f,   0.0f,1.0f,0.0f,
     0.5f, 0.5f, 0.5f,   0.0f,1.0f,0.0f,
     0.5f, 0.5f,-0.5f,   0.0f,1.0f,0.0f,
    -0.5f, 0.5f,-0.5f,   0.0f,1.0f,0.0f
};

// Screen quad (pos, uv)
float screenQuad[] = {
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
    -1.0f,  1.0f,  0.0f, 1.0f,
    -1.0f, -1.0f,  0.0f, 0.0f
};

// UI quad for rectangles (0..1 coord)
float uiQuad[] = {
    0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 1.0f,
    1.0f, 1.0f,
    0.0f, 1.0f,
    0.0f, 0.0f
};

// --------------------------- TETRIS LOGIC ----------------------------
const int BOARD_W = 10;
const int BOARD_H = 20;

struct PieceDef {
    std::vector<glm::ivec2> blocks;
    glm::vec3 color;
};

std::vector<PieceDef> PIECES;

int board[BOARD_H][BOARD_W];
PieceDef currentPiece;
glm::ivec2 currentPos;
float fallTime = 0.0f;
const float fallInterval_default = 1.0f;
float fallInterval = fallInterval_default;
bool gameOver = false;

int nextPieceIndex = 0;

std::mt19937 gen((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
std::uniform_int_distribution<> pieceDist(0,6);

bool keysProcessed[512] = {false};
bool materialKeyProcessed[4] = {false,false,false,false};
int currentMaterial = 0; // 0..2

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

void resetBoard() { std::memset(board, 0, sizeof(board)); }

void spawnNewPiece() {
    int pieceIndex = nextPieceIndex;
    currentPiece = PIECES[pieceIndex];
    currentPos = glm::ivec2(BOARD_W / 2 - 1, 0);
    nextPieceIndex = pieceDist(gen);
    for (const auto& b : currentPiece.blocks) {
        int x = currentPos.x + b.x;
        int y = currentPos.y + b.y;
        if (y >= 0 && board[y][x] != 0) {
            gameOver = true;
            break;
        }
    }
}

bool isValidMove(const glm::ivec2& newPos, const std::vector<glm::ivec2>& blocks) {
    for (const auto& block : blocks) {
        int x = newPos.x + block.x;
        int y = newPos.y + block.y;
        if (x < 0 || x >= BOARD_W || y >= BOARD_H) return false;
        if (y >= 0 && board[y][x] != 0) return false;
    }
    return true;
}

void rotatePiece() {
    bool isOPiece = true;
    for (const auto& block : currentPiece.blocks) {
        if (!(block.x >= 0 && block.x <= 1 && block.y >= 0 && block.y <= 1)) { isOPiece = false; break; }
    }
    if (isOPiece) return;
    std::vector<glm::ivec2> rotated = currentPiece.blocks;
    for (auto& b : rotated) { int nx = b.y; int ny = -b.x; b.x = nx; b.y = ny; }
    std::vector<glm::ivec2> kicks = {{0,0},{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}};
    for (auto &k : kicks) { glm::ivec2 pos = currentPos + k; if (isValidMove(pos, rotated)) { currentPiece.blocks = rotated; currentPos = pos; return; } }
}

void mergePiece() {
    for (const auto& b : currentPiece.blocks) {
        int x = currentPos.x + b.x;
        int y = currentPos.y + b.y;
        if (y >= 0) board[y][x] = 1;
    }
}

void clearLines() {
    for (int y = BOARD_H - 1; y >= 0; --y) {
        bool full = true;
        for (int x = 0; x < BOARD_W; ++x) if (!board[y][x]) { full = false; break; }
        if (full) {
            for (int yy = y; yy > 0; --yy) for (int x = 0; x < BOARD_W; ++x) board[yy][x] = board[yy-1][x];
            for (int x = 0; x < BOARD_W; ++x) board[0][x] = 0;
            ++y;
        }
    }
}

void updateGame(float dt) {
    if (gameOver) return;
    fallTime += dt;
    if (fallTime >= fallInterval) {
        glm::ivec2 newPos = currentPos; newPos.y += 1;
        if (isValidMove(newPos, currentPiece.blocks)) currentPos = newPos;
        else { mergePiece(); clearLines(); spawnNewPiece(); }
        fallTime = 0.0f;
    }
}

void processInput(GLFWwindow* window) {
    if (gameOver) {
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !keysProcessed[GLFW_KEY_R]) {
            resetBoard(); gameOver = false; nextPieceIndex = pieceDist(gen); spawnNewPiece(); keysProcessed[GLFW_KEY_R] = true;
        }
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE) keysProcessed[GLFW_KEY_R] = false;
        return;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS && !keysProcessed[GLFW_KEY_LEFT]) { glm::ivec2 p = currentPos; p.x -= 1; if (isValidMove(p, currentPiece.blocks)) currentPos = p; keysProcessed[GLFW_KEY_LEFT] = true; }
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_RELEASE) keysProcessed[GLFW_KEY_LEFT] = false;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS && !keysProcessed[GLFW_KEY_RIGHT]) { glm::ivec2 p = currentPos; p.x += 1; if (isValidMove(p, currentPiece.blocks)) currentPos = p; keysProcessed[GLFW_KEY_RIGHT] = true; }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_RELEASE) keysProcessed[GLFW_KEY_RIGHT] = false;
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !keysProcessed[GLFW_KEY_DOWN]) { glm::ivec2 p = currentPos; p.y += 1; if (isValidMove(p, currentPiece.blocks)) currentPos = p; keysProcessed[GLFW_KEY_DOWN] = true; }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) keysProcessed[GLFW_KEY_DOWN] = false;
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !keysProcessed[GLFW_KEY_UP]) { rotatePiece(); keysProcessed[GLFW_KEY_UP] = true; }
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) keysProcessed[GLFW_KEY_UP] = false;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && !keysProcessed[GLFW_KEY_SPACE]) { glm::ivec2 p = currentPos; while (isValidMove(p, currentPiece.blocks)) { currentPos = p; p.y += 1; } mergePiece(); clearLines(); spawnNewPiece(); keysProcessed[GLFW_KEY_SPACE] = true; }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_RELEASE) keysProcessed[GLFW_KEY_SPACE] = false;

    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS && !materialKeyProcessed[1]) { currentMaterial = 0; materialKeyProcessed[1]=true; }
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_RELEASE) materialKeyProcessed[1]=false;
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS && !materialKeyProcessed[2]) { currentMaterial = 1; materialKeyProcessed[2]=true; }
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_RELEASE) materialKeyProcessed[2]=false;
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS && !materialKeyProcessed[3]) { currentMaterial = 2; materialKeyProcessed[3]=true; }
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_RELEASE) materialKeyProcessed[3]=false;
}

// --------------------------- UI helpers ----------------------------
void drawUIRect(GLuint uiProg, GLuint uiVAO, int winW, int winH, float x, float yBottom, float w, float h, glm::vec3 color) {
    glUseProgram(uiProg);
    glm::mat4 ortho = glm::ortho(0.0f, (float)winW, 0.0f, (float)winH, -1.0f, 1.0f);
    glUniformMatrix4fv(glGetUniformLocation(uiProg, "uOrtho"), 1, GL_FALSE, glm::value_ptr(ortho));
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, yBottom, 0.0f));
    model = glm::scale(model, glm::vec3(w, h, 1.0f));
    glUniformMatrix4fv(glGetUniformLocation(uiProg, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform3f(glGetUniformLocation(uiProg, "uColor"), color.r, color.g, color.b);
    glBindVertexArray(uiVAO);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

void drawPreviewPieceUI(GLuint uiProg, GLuint uiVAO, int winW, int winH, int pieceIdx, float centerX, float centerY, float blockPixelSize) {
    const PieceDef& p = PIECES[pieceIdx];
    int minx=100, maxx=-100, miny=100, maxy=-100;
    for (auto &b : p.blocks) { if (b.x < minx) minx = b.x; if (b.x > maxx) maxx = b.x; if (b.y < miny) miny = b.y; if (b.y > maxy) maxy = b.y; }
    int w = maxx - minx + 1;
    int h = maxy - miny + 1;
    float totalW = w * blockPixelSize;
    float totalH = h * blockPixelSize;
    float startX = centerX - totalW/2.0f;
    float startYTop = centerY - totalH/2.0f;
    for (auto &b : p.blocks) {
        float bx = startX + (b.x - minx) * blockPixelSize;
        float byTop = startYTop + (b.y - miny) * blockPixelSize;
        float byBottom = (float)winH - (byTop + blockPixelSize);
        drawUIRect(uiProg, uiVAO, winW, winH, bx, byBottom, blockPixelSize, blockPixelSize, p.color);
    }
}

// --------------------------- Procedural textures ----------------------------
GLuint genAlbedo(int size, int variant) {
    std::vector<unsigned char> data(size*size*3);
    for (int y=0;y<size;++y) for (int x=0;x<size;++x) {
        int idx = (y*size + x)*3;
        int checker = ((x/8)+(y/8)) & 1;
        if (variant==0) { unsigned char v = checker?200:160; data[idx+0]=v; data[idx+1]=160; data[idx+2]=220; }
        else if (variant==1) { if (checker) { data[idx+0]=220; data[idx+1]=120; data[idx+2]=60; } else { data[idx+0]=60; data[idx+1]=140; data[idx+2]=200; } }
        else { unsigned char v = checker?180:100; data[idx+0]=v; data[idx+1]=v; data[idx+2]=v; }
    }
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,size,size,0,GL_RGB,GL_UNSIGNED_BYTE,data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    return t;
}
GLuint genNormal(int size, int variant) {
    std::vector<unsigned char> data(size*size*3);
    for (int i=0;i<size*size;++i) { unsigned char nx=128, ny=128, nz=255; if (variant==2 && (i%13==0)) nx=138; data[i*3+0]=nx; data[i*3+1]=ny; data[i*3+2]=nz; }
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,size,size,0,GL_RGB,GL_UNSIGNED_BYTE,data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    return t;
}
GLuint genRough(int size, int variant) {
    std::vector<unsigned char> data(size*size*3);
    unsigned char v = (variant==0?200:(variant==1?100:40));
    for (int i=0;i<size*size;++i) data[i*3+0]=data[i*3+1]=data[i*3+2]=v;
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,size,size,0,GL_RGB,GL_UNSIGNED_BYTE,data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    return t;
}

// --------------------------- DRAW CUBE (bind VAO) ----------------------------
void drawCubePBR(GLuint pbrProg, GLuint cubeVAO, const glm::mat4& model, const glm::vec3& color, float metallic, float ao, bool useAlbedoMap) {
    glUseProgram(pbrProg);
    glUniformMatrix4fv(glGetUniformLocation(pbrProg,"uModel"),1,GL_FALSE,glm::value_ptr(model));
    glUniform3f(glGetUniformLocation(pbrProg,"albedo"), color.r, color.g, color.b);
    glUniform1f(glGetUniformLocation(pbrProg,"metallic"), metallic);
    glUniform1f(glGetUniformLocation(pbrProg,"ao"), ao);
    glUniform1i(glGetUniformLocation(pbrProg,"uUseAlbedoMap"), useAlbedoMap?1:0);
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

// --------------------------- FRAMEBUFFER MANAGEMENT ----------------------------
struct Framebuffers {
    GLuint hdrFBO;
    GLuint colorBuffer, brightBuffer;
    GLuint rboDepth;
    GLuint pingpongFBO[2], pingpongTex[2];
    int width, height;
};

void createFramebuffers(Framebuffers& fbo, int width, int height) {
    fbo.width = width;
    fbo.height = height;

    // HDR framebuffer
    glGenFramebuffers(1, &fbo.hdrFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.hdrFBO);
    
    glGenTextures(1, &fbo.colorBuffer);
    glBindTexture(GL_TEXTURE_2D, fbo.colorBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo.colorBuffer, 0);

    glGenTextures(1, &fbo.brightBuffer);
    glBindTexture(GL_TEXTURE_2D, fbo.brightBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, fbo.brightBuffer, 0);

    GLuint attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, attachments);

    glGenRenderbuffers(1, &fbo.rboDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, fbo.rboDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo.rboDepth);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "HDR FBO incomplete\n";

    // ping-pong FBOs for blur
    glGenFramebuffers(2, fbo.pingpongFBO);
    glGenTextures(2, fbo.pingpongTex);
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo.pingpongFBO[i]);
        glBindTexture(GL_TEXTURE_2D, fbo.pingpongTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo.pingpongTex[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "Pingpong FBO incomplete\n";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void deleteFramebuffers(Framebuffers& fbo) {
    glDeleteFramebuffers(1, &fbo.hdrFBO);
    glDeleteTextures(1, &fbo.colorBuffer);
    glDeleteTextures(1, &fbo.brightBuffer);
    glDeleteRenderbuffers(1, &fbo.rboDepth);
    glDeleteFramebuffers(2, fbo.pingpongFBO);
    glDeleteTextures(2, fbo.pingpongTex);
}

// --------------------------- MAIN ----------------------------
int main(){
    // init
    initPieces();
    resetBoard();

    if (!glfwInit()) { std::cerr<<"GLFW init failed\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);
#endif

    const int INIT_WIN_W = 1100, INIT_WIN_H = 750;
    GLFWwindow* window = glfwCreateWindow(INIT_WIN_W, INIT_WIN_H, "Tetris PBR + HDR+BLOOM (NEXT preview only)", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cerr<<"Failed to init glad\n"; return -1; }

    // compile programs
    GLuint pbrProg = makeProgram(pbrVertex, pbrFragment);
    GLuint quadProg_bright = makeProgram(quadVertex, brightFragment);
    GLuint quadProg_blur = makeProgram(quadVertex, blurFragment);
    GLuint quadProg_final = makeProgram(quadVertex, finalFragment);
    GLuint uiProg = makeProgram(uiVertex, uiFragment);

    // cube VAO
    GLuint cubeVAO, cubeVBO;
    glGenVertexArrays(1,&cubeVAO); glGenBuffers(1,&cubeVBO);
    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER,cubeVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(cubeVertices),cubeVertices,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)(3*sizeof(float)));
    glBindVertexArray(0);

    // screen quad VAO
    GLuint quadVAO, quadVBO;
    glGenVertexArrays(1,&quadVAO); glGenBuffers(1,&quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(screenQuad), screenQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glBindVertexArray(0);

    // UI quad VAO
    GLuint uiVAO, uiVBO;
    glGenVertexArrays(1,&uiVAO); glGenBuffers(1,&uiVBO);
    glBindVertexArray(uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(uiQuad), uiQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);

    // textures (3 materials)
    const int TEX = 128;
    std::array<GLuint,3> albedoT, normalT, roughT;
    for (int i=0;i<3;i++){ albedoT[i]=genAlbedo(TEX,i); normalT[i]=genNormal(TEX,i); roughT[i]=genRough(TEX,i); }

    // assign samplers once
    glUseProgram(pbrProg);
    glUniform1i(glGetUniformLocation(pbrProg,"albedoMap"), 0);
    glUniform1i(glGetUniformLocation(pbrProg,"normalMap"), 1);
    glUniform1i(glGetUniformLocation(pbrProg,"roughnessMap"), 2);
    glUseProgram(quadProg_bright); glUniform1i(glGetUniformLocation(quadProg_bright,"scene"), 0);
    glUseProgram(quadProg_blur); glUniform1i(glGetUniformLocation(quadProg_blur,"image"), 0);
    glUseProgram(quadProg_final); glUniform1i(glGetUniformLocation(quadProg_final,"scene"), 0); glUniform1i(glGetUniformLocation(quadProg_final,"bloom"), 1);

    // Create framebuffers with initial size
    Framebuffers mainFBO;
    createFramebuffers(mainFBO, INIT_WIN_W, INIT_WIN_H);

    // init pieces and spawn
    nextPieceIndex = pieceDist(gen);
    spawnNewPiece();

    // runtime params
    float brightThreshold = 1.0f;
    int blurPasses = 8;
    float bloomFactor = 1.0f;

    double last = glfwGetTime();

    while (!glfwWindowShouldClose(window)){
        double cur = glfwGetTime();
        float dt = (float)(cur - last);
        last = cur;

        processInput(window);
        updateGame(dt);

        int winW, winH; 
        glfwGetFramebufferSize(window, &winW, &winH);
        
        // Recreate framebuffers if window size changed
        if (winW != mainFBO.width || winH != mainFBO.height) {
            deleteFramebuffers(mainFBO);
            createFramebuffers(mainFBO, winW, winH);
        }

        // 1) Render scene to HDR FBO
        glBindFramebuffer(GL_FRAMEBUFFER, mainFBO.hdrFBO);
        glViewport(0, 0, winW, winH);
        glClearColor(0.02f,0.02f,0.03f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // bind material textures
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, albedoT[currentMaterial]);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, normalT[currentMaterial]);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, roughT[currentMaterial]);

        // render scene
        glUseProgram(pbrProg);
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)winW/(float)winH, 0.1f, 100.0f);
        glm::mat4 view = glm::lookAt(glm::vec3(BOARD_W/2.0f, BOARD_H/2.0f, 25.0f),
                                     glm::vec3(BOARD_W/2.0f, BOARD_H/2.0f, 0.0f),
                                     glm::vec3(0.0f,1.0f,0.0f));
        glUniformMatrix4fv(glGetUniformLocation(pbrProg,"uProj"),1,GL_FALSE,glm::value_ptr(proj));
        glUniformMatrix4fv(glGetUniformLocation(pbrProg,"uView"),1,GL_FALSE,glm::value_ptr(view));
        glUniform3f(glGetUniformLocation(pbrProg,"camPos"), BOARD_W/2.0f, BOARD_H/2.0f, 25.0f);

        // lights
        std::array<glm::vec3,4> lightPos = {
            glm::vec3(BOARD_W/2.0f, BOARD_H/2.0f, 20.0f),
            glm::vec3(-5.0f, 10.0f, 15.0f),
            glm::vec3(BOARD_W + 5.0f, 10.0f, 15.0f),
            glm::vec3(BOARD_W/2.0f, BOARD_H + 5.0f, 15.0f)
        };
        std::array<glm::vec3,4> lightCol = {
            glm::vec3(400.0f,350.0f,300.0f),
            glm::vec3(100.0f),
            glm::vec3(100.0f),
            glm::vec3(100.0f)
        };
        for (int i=0;i<4;i++){
            std::string posn = "lightPositions[" + std::to_string(i) + "]";
            std::string coln = "lightColors[" + std::to_string(i) + "]";
            glUniform3fv(glGetUniformLocation(pbrProg,posn.c_str()),1,&lightPos[i][0]);
            glUniform3fv(glGetUniformLocation(pbrProg,coln.c_str()),1,&lightCol[i][0]);
        }

        // draw board (occupied cells)
        for (int y=0;y<BOARD_H;++y) for (int x=0;x<BOARD_W;++x) {
            if (board[y][x] != 0) {
                glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3((float)x, (float)(BOARD_H - y - 1), 0.0f));
                model = glm::scale(model, glm::vec3(1.0f,1.0f,0.8f));
                drawCubePBR(pbrProg, cubeVAO, model, glm::vec3(0.5f,0.5f,0.5f), 0.0f, 1.0f, 0);
            }
        }

        // draw current piece (with albedo map)
        if (!gameOver) {
            for (const auto &b : currentPiece.blocks) {
                int x = currentPos.x + b.x;
                int y = currentPos.y + b.y;
                if (y >= 0) {
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3((float)x, (float)(BOARD_H - y - 1), 0.0f));
                    model = glm::scale(model, glm::vec3(1.0f,1.0f,0.8f));
                    float metallic = (currentMaterial==2)?0.6f:0.0f;
                    drawCubePBR(pbrProg, cubeVAO, model, currentPiece.color, metallic, 1.0f, 1);
                }
            }
        }

        // subtle grid lines
        for (int x=0;x<=BOARD_W;++x) for (int y=0;y<=BOARD_H;++y) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x-0.5f, BOARD_H - y - 0.5f, -0.1f));
            model = glm::scale(model, glm::vec3(1.0f,1.0f,0.05f));
            drawCubePBR(pbrProg, cubeVAO, model, glm::vec3(0.12f,0.12f,0.12f), 0.0f, 1.0f, 0);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 2) Bright-pass (from colorBuffer) -> pingpong[0]
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, mainFBO.colorBuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, mainFBO.pingpongFBO[0]);
        glViewport(0,0,winW,winH);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(quadProg_bright);
        glUniform1f(glGetUniformLocation(quadProg_bright,"threshold"), brightThreshold);
        glBindVertexArray(quadVAO); glDrawArrays(GL_TRIANGLES,0,6);

        // 3) Blur ping-pong
        bool horizontal = true; bool first_iter = true;
        glUseProgram(quadProg_blur);
        for (int i=0;i<blurPasses;i++){
            glBindFramebuffer(GL_FRAMEBUFFER, mainFBO.pingpongFBO[horizontal]);
            glUniform1i(glGetUniformLocation(quadProg_blur,"horizontal"), horizontal?1:0);
            glActiveTexture(GL_TEXTURE0);
            if (first_iter) glBindTexture(GL_TEXTURE_2D, mainFBO.pingpongTex[0]);
            else glBindTexture(GL_TEXTURE_2D, mainFBO.pingpongTex[!horizontal]);
            glBindVertexArray(quadVAO); glDrawArrays(GL_TRIANGLES,0,6);
            horizontal = !horizontal;
            if (first_iter) first_iter = false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 4) Final composite: scene(colorBuffer) + bloom (last pingpongTex)
        glViewport(0, 0, winW, winH);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(quadProg_final);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, mainFBO.colorBuffer);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, mainFBO.pingpongTex[!horizontal]);
        glUniform1f(glGetUniformLocation(quadProg_final,"bloomFactor"), bloomFactor);
        glBindVertexArray(quadVAO); glDrawArrays(GL_TRIANGLES,0,6);

        // 5) UI overlay: only NEXT preview (no score)
        glUseProgram(uiProg);
        glBindVertexArray(uiVAO);

        float previewCenterX = (float)winW - 140.0f;
        float previewCenterY = 120.0f;
        float blockPixel = 22.0f;
        float bgW = 6*blockPixel, bgH = 6*blockPixel;
        float bgLeft = previewCenterX - bgW/2.0f;
        float bgTop = previewCenterY - bgH/2.0f;
        float bgBottom = (float)winH - (bgTop + bgH);
        drawUIRect(uiProg, uiVAO, winW, winH, bgLeft, bgBottom, bgW, bgH, glm::vec3(0.03f,0.03f,0.04f));
        drawPreviewPieceUI(uiProg, uiVAO, winW, winH, nextPieceIndex, previewCenterX, previewCenterY, blockPixel);

        // Material hint (small)
        drawUIRect(uiProg, uiVAO, winW, winH, 20, winH - 40, 300, 28, glm::vec3(0.02f,0.02f,0.02f));
        // draw three small boxes indicating material 1/2/3
        for (int i=0;i<3;i++){
            float bx = 26 + i*34; float by = winH - 34;
            glm::vec3 col = (i==0)?glm::vec3(0.8f,0.8f,0.8f):(i==1?glm::vec3(0.8f,0.5f,0.2f):glm::vec3(0.6f,0.6f,0.9f));
            if (i==currentMaterial) col += glm::vec3(0.18f);
            drawUIRect(uiProg, uiVAO, winW, winH, bx, by, 28, 20, col);
        }

        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // cleanup
    deleteFramebuffers(mainFBO);
    glDeleteProgram(pbrProg); glDeleteProgram(quadProg_bright); glDeleteProgram(quadProg_blur);
    glDeleteProgram(quadProg_final); glDeleteProgram(uiProg);
    glDeleteVertexArrays(1,&cubeVAO); glDeleteBuffers(1,&cubeVBO);
    glDeleteVertexArrays(1,&quadVAO); glDeleteBuffers(1,&quadVBO);
    glDeleteVertexArrays(1,&uiVAO); glDeleteBuffers(1,&uiVBO);
    for (int i=0;i<3;i++){ glDeleteTextures(1,&albedoT[i]); glDeleteTextures(1,&normalT[i]); glDeleteTextures(1,&roughT[i]); }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}