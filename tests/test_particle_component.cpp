#include <iostream>
#include <memory>
#include <chrono>
#include <thread>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "src/core/Entity.h"
#include "src/core/components/ParticleComponent.h"
#include "src/core/components/TransformComponent.h"

using namespace IKore;

int main() {
    std::cout << "==== Particle Component Test ====" << std::endl;

    // Attaching a ParticleComponent issues live GL calls (ParticleSystem::initializeBuffers
    // creates the VBO/VAO), so the test needs a current GL context. Create a hidden offscreen
    // window; in CI this runs on Xvfb + a software GL. If no context can be created (e.g. no
    // display at all), skip rather than fail so the job stays reliable.
    if (!glfwInit()) {
        std::cout << "GLFW init failed (no display?); skipping particle GL test" << std::endl;
        return 0;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* glContext = glfwCreateWindow(64, 64, "particle-test", nullptr, nullptr);
    if (!glContext) {
        std::cout << "GL context creation failed (no display?); skipping particle GL test" << std::endl;
        glfwTerminate();
        return 0;
    }
    glfwMakeContextCurrent(glContext);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to load GL functions; skipping particle GL test" << std::endl;
        glfwDestroyWindow(glContext);
        glfwTerminate();
        return 0;
    }

    // Create an entity
    auto entity = std::make_shared<Entity>();
    
    // Add a transform component
    auto transform = entity->addComponent<TransformComponent>();
    transform->setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
    
    // Add a particle component with fire effect
    auto particleComponent = entity->addComponent<ParticleComponent>(ParticleEffectType::FIRE);
    
    std::cout << "Initialized ParticleComponent with fire effect" << std::endl;
    
    // Initialize the particle component
    if (!particleComponent->initialize(500)) {
        std::cerr << "Failed to initialize particle component" << std::endl;
        return 1;
    }
    
    // Start emitting particles
    particleComponent->play();
    std::cout << "Started particle emission" << std::endl;
    
    // Simulate a game loop for 5 seconds
    const float deltaTime = 0.016f; // ~60 fps
    for (int i = 0; i < 300; i++) {
        // Update particle system
        particleComponent->update(deltaTime);
        
        if (i % 60 == 0) {
            std::cout << "Update frame " << i << ", active particles: " 
                      << particleComponent->getActiveParticles() << std::endl;
        }
        
        // Move the entity to test position updates
        if (i == 100) {
            transform->setPosition(glm::vec3(1.0f, 2.0f, 3.0f));
            std::cout << "Moved entity to (1, 2, 3)" << std::endl;
        }
        
        // Change effect type midway to test switching
        if (i == 200) {
            particleComponent->setEffectType(ParticleEffectType::SMOKE);
            std::cout << "Changed effect to smoke" << std::endl;
        }
        
        // Simulate frame timing
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    // Test stopping and restarting
    particleComponent->stop();
    std::cout << "Stopped particle emission" << std::endl;
    
    // Wait a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Restart with explosion
    particleComponent->setEffectType(ParticleEffectType::EXPLOSION);
    particleComponent->play();
    std::cout << "Restarted with explosion effect" << std::endl;
    
    // One more burst
    particleComponent->burst(100);
    std::cout << "Added burst of 100 particles" << std::endl;
    
    // A few more updates
    for (int i = 0; i < 60; i++) {
        particleComponent->update(deltaTime);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    // Remove component
    entity->removeComponent<ParticleComponent>();
    std::cout << "Removed particle component" << std::endl;
    
    std::cout << "Particle Component test completed successfully" << std::endl;

    // Release everything that owns GL resources (ParticleSystem frees its VBO/VAO in its
    // destructor) while the context is still current, then tear the context down. Doing this
    // in the wrong order frees GL objects with no current context and crashes.
    particleComponent.reset();
    entity.reset();
    glfwDestroyWindow(glContext);
    glfwTerminate();
    return 0;
}
