#include "ScreenManager.h"

// OpenGL and FreeGlut headers.
#include <GL/glew.h>
#include <GL/freeglut.h>

// GLM headers.
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

// C++ STL headers.
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>

// My headers.
#include "TriangleMesh.h"
#include "ShaderProg.h"
#include "Light.h"
#include "Camera.h"
#include "Skybox.h"
#include "Clock.h"

namespace opengl_homework {

using MeshPtr = std::shared_ptr<opengl_homework::TriangleMesh>;

std::shared_ptr<ScreenManager> ScreenManager::GetInstance() {
    static std::shared_ptr<ScreenManager> instance(new ScreenManager());
    return instance;
}

class SceneObject
{
public:
    SceneObject() {
        mesh = nullptr;
        worldMatrix = glm::mat4x4(1.0f);
    }

    void Update(const glm::mat4& transform) {
        worldMatrix = transform * worldMatrix;
    }

    MeshPtr mesh;
    glm::mat4x4 worldMatrix;
};

// SceneLight (for visualization of a point light).
// T is derived from PointLight
template<typename T>
    requires std::derived_from<T, PointLight>
struct SceneLight
{
    SceneLight() {
        light = nullptr;
        worldMatrix = glm::mat4x4(1.0f);
        visColor = glm::vec3(1.0f, 1.0f, 1.0f);
    }
    std::shared_ptr<T> light;
    glm::mat4x4 worldMatrix;
    glm::vec3 visColor;
};

// ------------------------------------------------------------------------
// Private member implementations. ----------------------------------------
// ------------------------------------------------------------------------
struct ScreenManager::Impl {
    Impl() :
        width(600),
        height(600),
        camera(std::make_unique<Camera>((float)width / (float)height)) {
        sceneObj = std::make_unique<SceneObject>();
        pointLightObj = std::make_unique<SceneLight<PointLight>>();
        spotLightObj = std::make_unique<SceneLight<SpotLight>>();
    };

    int width;
    int height;
    Clock clock;
    std::vector<std::string> objNames;
    std::vector<std::string> skyboxNames;
    std::shared_ptr<FillColorShaderProg> fillColorShader;
    std::shared_ptr<PhongShadingDemoShaderProg> phongShader;
    std::shared_ptr<SkyboxShaderProg> skyboxShader;
    std::unique_ptr<SceneObject> sceneObj;
    std::shared_ptr<Camera> camera;
    std::shared_ptr<DirectionalLight> dirLight;
    std::shared_ptr<SceneLight<PointLight>> pointLightObj;
    std::shared_ptr<SceneLight<SpotLight>> spotLightObj;
    std::shared_ptr<Skybox> skybox;
    glm::vec3 ambientLight;
    float lightMoveSpeed = 0.2f;
};

// ------------------------------------------------------------------------
// Public member functions. -----------------------------------------------
// ------------------------------------------------------------------------

void ScreenManager::Start(int argc, char** argv) {
    // Setting window properties.
    glutInit(&argc, argv);
    glutSetOption(GLUT_MULTISAMPLE, 4);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH | GLUT_MULTISAMPLE);
    glutInitWindowSize(pImpl->width, pImpl->height);
    glutInitWindowPosition(100, 100);
    glutCreateWindow("HW2: Lighting and Shading");

    // Initialize GLEW.
    // Must be done after glut is initialized!
    GLenum res = glewInit();
    if (res != GLEW_OK) {
        std::cerr << "GLEW initialization error: "
            << glewGetErrorString(res) << std::endl;
        exit(EXIT_FAILURE);
    }

    // Initialization.
    SetupFilesystem();
    SetupRenderState();
    SetupLights();
    SetupCamera();
    SetupShaderLib();
    SetupMenu();
    SetupSkybox(0);
    SetupScene(0);

    // Register callback functions.
    glutDisplayFunc([]() { GetInstance()->RenderSceneCB(); });
    glutIdleFunc([]() { glutPostRedisplay(); });
    glutReshapeFunc([](int w, int h) { GetInstance()->ReshapeCB(w, h); });
    glutSpecialFunc([](int key, int x, int y) { GetInstance()->ProcessSpecialKeysCB(key, x, y); });
    glutKeyboardFunc([](unsigned char key, int x, int y) { GetInstance()->ProcessKeysCB(key, x, y); });

    // Start rendering loop.
    glutMainLoop();
}

// ------------------------------------------------------------------------
// Private member functions. ----------------------------------------------
// ------------------------------------------------------------------------

ScreenManager::ScreenManager() {
    pImpl = std::make_unique<Impl>();
}

int ScreenManager::CalculateFrameRate() {
    static int frameCount = 0;
    static int lastFrameCount = 0;
    static auto lastTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count() / 1000.0f;
    frameCount++;

    if (deltaTime >= 1.0f) {
        lastFrameCount = frameCount;
        frameCount = 0;
        lastTime = currentTime;
    }

    return lastFrameCount;
}

// Callback function for glutDisplayFunc.
void ScreenManager::RenderSceneCB() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    double deltaTime = pImpl->clock.GetElapsedTime();
    pImpl->clock.Reset();
    float rotationAngle = 0.1f * deltaTime;

    // Calculate frame rate.
    int frameRate = CalculateFrameRate();
    glColor3f(1.0f, 1.0f, 1.0f);
    glRasterPos2f(-0.95f, 0.9f);
    std::string frameRateStr = "FPS: " + std::to_string(frameRate);
    glutBitmapString(GLUT_BITMAP_HELVETICA_18, (const unsigned char*)frameRateStr.c_str());

    // Rotate the model.
    auto rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4x4 R = glm::rotate(glm::mat4x4(1.0f), rotationAngle, rotationAxis);
    pImpl->sceneObj->Update(R);

    pImpl->sceneObj->mesh->Render(
        pImpl->phongShader,
        pImpl->sceneObj->worldMatrix,
        pImpl->ambientLight,
        pImpl->dirLight,
        pImpl->pointLightObj->light,
        pImpl->spotLightObj->light,
        pImpl->camera
    );

    // Visualize the light with fill color. ------------------------------------------------------
    // Bind shader and set parameters.
    auto pointLight = pImpl->pointLightObj->light;
    if (pointLight != nullptr) {
        glm::mat4x4 T = glm::translate(glm::mat4x4(1.0f), (pointLight->GetPosition()));
        pImpl->pointLightObj->worldMatrix = T;
        glm::mat4x4 MVP = pImpl->camera->GetProjMatrix() * pImpl->camera->GetViewMatrix() * pImpl->pointLightObj->worldMatrix;

        pImpl->fillColorShader->Bind();

        glUniformMatrix4fv(pImpl->fillColorShader->GetLocMVP(), 1, GL_FALSE, glm::value_ptr(MVP));
        glUniform3fv(pImpl->fillColorShader->GetLocFillColor(), 1, glm::value_ptr(pImpl->pointLightObj->visColor));

        // Render the point light.
        pImpl->pointLightObj->light->Draw();

        pImpl->fillColorShader->Unbind();
    }
    auto spotLight = pImpl->spotLightObj->light;
    if (spotLight != nullptr) {
        glm::mat4x4 T = glm::translate(glm::mat4x4(1.0f), (spotLight->GetPosition()));
        pImpl->spotLightObj->worldMatrix = T;
        glm::mat4x4 MVP = pImpl->camera->GetProjMatrix() * pImpl->camera->GetViewMatrix() * pImpl->spotLightObj->worldMatrix;

        pImpl->fillColorShader->Bind();

        glUniformMatrix4fv(pImpl->fillColorShader->GetLocMVP(), 1, GL_FALSE, glm::value_ptr(MVP));
        glUniform3fv(pImpl->fillColorShader->GetLocFillColor(), 1, glm::value_ptr(pImpl->spotLightObj->visColor));

        // Render the point light.
        pImpl->spotLightObj->light->Draw();

        pImpl->fillColorShader->Unbind();
    }
    if (pImpl->skybox != nullptr) {
        pImpl->skybox->SetRotation(pImpl->skybox->GetRotation() + rotationAngle);
        pImpl->skybox->Render(pImpl->camera, pImpl->skyboxShader);
    }

    glutSwapBuffers();
}

// Callback function for glutReshapeFunc.
void ScreenManager::ReshapeCB(int w, int h) {
    // Update viewport.
    pImpl->width = w;
    pImpl->height = h;
    glViewport(0, 0, pImpl->width, pImpl->height);
    // Adjust camera and projection.
    pImpl->camera->UpdateAspectRatio((float)pImpl->width / (float)pImpl->height);
    pImpl->camera->UpdateProjection();
}

void ScreenManager::ProcessSpecialKeysCB(int key, int x, int y) {
    // Light control.
    switch (key) {
    case GLUT_KEY_LEFT:
        if (pImpl->pointLightObj->light != nullptr)
            pImpl->pointLightObj->light->MoveLeft(pImpl->lightMoveSpeed);
        break;
    case GLUT_KEY_RIGHT:
        if (pImpl->pointLightObj->light != nullptr)
            pImpl->pointLightObj->light->MoveRight(pImpl->lightMoveSpeed);
        break;
    case GLUT_KEY_UP:
        if (pImpl->pointLightObj->light != nullptr)
            pImpl->pointLightObj->light->MoveUp(pImpl->lightMoveSpeed);
        break;
    case GLUT_KEY_DOWN:
        if (pImpl->pointLightObj->light != nullptr)
            pImpl->pointLightObj->light->MoveDown(pImpl->lightMoveSpeed);
        break;
    default:
        break;
    }
}

// Callback function for glutKeyboardFunc.
void ScreenManager::ProcessKeysCB(unsigned char key, int x, int y) {
    // Handle other keyboard inputs those are not defined as special keys.
    if (key == 27) {
        exit(0);
    }

    // Spot light control.
    auto spotLight = pImpl->spotLightObj->light;
    if (spotLight != nullptr) {
        if (key == 'a')
            spotLight->MoveLeft(pImpl->lightMoveSpeed);
        if (key == 'd')
            spotLight->MoveRight(pImpl->lightMoveSpeed);
        if (key == 'w')
            spotLight->MoveUp(pImpl->lightMoveSpeed);
        if (key == 's')
            spotLight->MoveDown(pImpl->lightMoveSpeed);
    }
}

void ScreenManager::SetupFilesystem() {
    // Load all obj files in the models directory.
    for (const auto& entry : std::filesystem::directory_iterator("models")) {
        if (entry.is_directory()) {
            pImpl->objNames.push_back(entry.path().filename().string());
        }
    }

    // find the minimum bytes size of the obj files and swap it to the first position
    int min = INT_MAX;
    int minIndex = 0;
    for (int i = 0; i < pImpl->objNames.size(); i++) {
        auto size = std::filesystem::file_size("models/" + pImpl->objNames[i] + "/" + pImpl->objNames[i] + ".obj");
        if (size < min) {
            min = size;
            minIndex = i;
        }
    }
    std::swap(pImpl->objNames[0], pImpl->objNames[minIndex]);

    // Load all skybox textures in the textures directory.
    for (const auto& entry : std::filesystem::directory_iterator("textures")) {
        if (entry.is_regular_file()) {
            pImpl->skyboxNames.push_back(entry.path().filename().string());
        }
    }
}

void ScreenManager::SetupRenderState() {
    glEnable(GL_DEPTH_TEST);

    glm::vec4 clearColor = glm::vec4(0.44f, 0.57f, 0.75f, 1.00f);
    glClearColor(
        (GLclampf)(clearColor.r),
        (GLclampf)(clearColor.g),
        (GLclampf)(clearColor.b),
        (GLclampf)(clearColor.a)
    );
}

// Load a model from obj file and apply transformation.
// You can alter the parameters for dynamically loading a model.
void ScreenManager::SetupScene(int objIndex) {
    glm::mat4x4 S = glm::scale(glm::mat4x4(1.0f), glm::vec3(1.5f, 1.5f, 1.5f));
    pImpl->sceneObj->worldMatrix = S;
    if (pImpl->sceneObj->mesh != nullptr) {
        pImpl->sceneObj->mesh->ReleaseBuffers();
    }
    auto objBasePath = std::filesystem::path("models");
    auto objFilePath = objBasePath / pImpl->objNames[objIndex] / (pImpl->objNames[objIndex] + ".obj");
    pImpl->sceneObj->mesh = std::make_shared<TriangleMesh>(objFilePath, true);
    pImpl->sceneObj->mesh->CreateBuffers();

    pImpl->sceneObj->mesh->PrintMeshInfo();

    pImpl->clock.Reset();
}

void ScreenManager::SetupLights() {
    glm::vec3 dirLightDirection = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 dirLightRadiance = glm::vec3(0.6f, 0.6f, 0.6f);
    glm::vec3 pointLightPosition = glm::vec3(0.8f, 0.0f, 0.8f);
    glm::vec3 pointLightIntensity = glm::vec3(0.5f, 0.1f, 0.1f);
    glm::vec3 spotLightPosition = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 spotLightDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 spotLightIntensity = glm::vec3(0.5f, 0.5f, 0.1f);
    float spotLightCutoffStartInDegree = 30.0f;
    float spotLightTotalWidthInDegree = 45.0f;
    glm::vec3 ambientLight = glm::vec3(0.2f, 0.2f, 0.2f);
    pImpl->dirLight = std::make_unique<DirectionalLight>(dirLightDirection, dirLightRadiance);
    pImpl->pointLightObj->light = std::make_shared<PointLight>(pointLightPosition, pointLightIntensity);
    pImpl->pointLightObj->visColor = glm::normalize((pImpl->pointLightObj->light->GetIntensity()));
    pImpl->spotLightObj->light = std::make_shared<SpotLight>(
        spotLightPosition,
        spotLightIntensity,
        spotLightDirection,
        spotLightCutoffStartInDegree,
        spotLightTotalWidthInDegree);
    pImpl->spotLightObj->visColor = glm::normalize((pImpl->spotLightObj->light->GetIntensity()));
    pImpl->ambientLight = ambientLight;
}

void ScreenManager::SetupCamera() {
    // Create a camera and update view and proj matrices.
    float fovy = 30.0f;
    float zNear = 0.1f;
    float zFar = 1000.0f;
    glm::vec3 cameraPos = glm::vec3(0.0f, 1.0f, 5.0f);
    glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    pImpl->camera->UpdateView(cameraPos, cameraTarget, cameraUp);
    pImpl->camera->UpdateAspectRatio((float)pImpl->width / (float)pImpl->height);
    pImpl->camera->UpdateFovy(fovy);
    pImpl->camera->UpdateNearPlane(zNear);
    pImpl->camera->UpdateFarPlane(zFar);
    pImpl->camera->UpdateProjection();
}

void ScreenManager::SetupSkybox(int skyboxIndex) {
    const int numSlices = 36;
    const int numStacks = 18;
    const float radius = 50.0f;
    auto skyboxDir = std::filesystem::path("textures") / pImpl->skyboxNames[skyboxIndex];
    pImpl->skybox = std::make_shared<Skybox>(skyboxDir, numSlices, numStacks, radius);
}

void ScreenManager::SetupShaderLib() {
    pImpl->fillColorShader = std::make_unique<FillColorShaderProg>();
    pImpl->phongShader = std::make_unique<PhongShadingDemoShaderProg>();
    pImpl->skyboxShader = std::make_unique<SkyboxShaderProg>();

    if (!pImpl->fillColorShader->LoadFromFiles("shaders/fixed_color.vs", "shaders/fixed_color.fs", "")) {
        std::cerr << "Failed to load fixed_color shader." << std::endl;
        exit(EXIT_FAILURE);
    }
    if (!pImpl->phongShader->LoadFromFiles("shaders/phong_shading_demo.vs", "shaders/phong_shading_demo.fs", "shaders/face_culling.gs")) {
        std::cerr << "Failed to load gouraud shader." << std::endl;
        exit(EXIT_FAILURE);
    }
    if (!pImpl->skyboxShader->LoadFromFiles("shaders/skybox.vs", "shaders/skybox.fs", "")) {
        std::cerr << "Failed to load skybox shader." << std::endl;
        exit(EXIT_FAILURE);
    }
}

void ScreenManager::SetupMenu() {
    int skyboxMenu = glutCreateMenu([](int value) { GetInstance()->SkyboxMenuCB(value); });
    for (int i = 0; i < pImpl->skyboxNames.size(); i++) {
        glutAddMenuEntry(pImpl->skyboxNames[i].c_str(), i + 1);
    }

    int objMenu = glutCreateMenu([](int value) { GetInstance()->ObjectMenuCB(value); });
    for (int i = 0; i < pImpl->objNames.size(); i++) {
        glutAddMenuEntry(pImpl->objNames[i].c_str(), i + 1);
    }

    int mainMenu = glutCreateMenu([](int value) { GetInstance()->MainMenuCB(value); });
    glutAddSubMenu("Skybox", skyboxMenu);
    glutAddSubMenu("Model", objMenu);
    glutAttachMenu(GLUT_RIGHT_BUTTON);
}

void ScreenManager::MainMenuCB(int value) {
    return;
}

void ScreenManager::ObjectMenuCB(int value) {
    SetupScene(value - 1);
}

void ScreenManager::SkyboxMenuCB(int value) {
    SetupSkybox(value - 1);
}

} // namespace opengl_homework