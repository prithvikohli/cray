#include "renderer.h"

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
    Renderer renderer(&rc, "shaders/");

    renderer.loadScene("DamagedHelmet.glb", true);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        renderer.render();
    }

    rc.waitIdle();

    glfwDestroyWindow(window);
    glfwTerminate();
}