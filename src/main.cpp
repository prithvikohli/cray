#include "renderer.h"

// TODO make configurable
#define WINDOW_WIDTH 2560
#define WINDOW_HEIGHT 1440

int main()
{
    int res = glfwInit();
    if (res == GLFW_FALSE)
        throw std::runtime_error("failed to initialize GLFW!");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "cray", nullptr, nullptr);
    if (!window)
        throw std::runtime_error("failed to create GLFW window!");

    vk::RenderContext rc(window);
    Renderer renderer(&rc, "src/shaders/");

    renderer.loadScene("assets/scenes/DamagedHelmet.glb", true, "assets/environment_maps/kart.hdr");

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        renderer.render();
    }

    rc.deviceWaitIdle();

    glfwDestroyWindow(window);
    glfwTerminate();
}