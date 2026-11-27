// A program built only from an installed Teserra (#143). It uses modules without a window, and references the App
// so that the link needs every library of the package and every dependency they link (GLFW, the OpenGL loader,
// ImGui); the App itself runs only when asked, since test machines may have no display.

#include <teserra/audio/audio_engine.hpp>
#include <teserra/core/version.hpp>
#include <teserra/teserra.hpp>

#include <cstdio>
#include <string_view>
#include <vector>

using namespace teserra;

int main(int argc, char** argv) {
    ecs::World world;
    const ecs::Entity entity = world.create();
    world.add<scene::Transform>(entity, scene::Transform{.position = {1.0F, 2.0F, 0.0F}});

    auto audio = audio::AudioEngine::create({.output = audio::Output::Offline});
    if (!audio) {
        std::printf("audio failed\n");
        return 1;
    }
    const std::vector<f32> samples(480, 0.5F);
    (*audio)->play(*audio::AudioClip::create(48'000, 1, samples));
    std::vector<f32> mix(256);
    (*audio)->render(mix);

    if (argc > 1 && std::string_view{argv[1]} == "--window") {
        engine::App app{{.title = "Install consumer", .maxFrames = 1, .args = {argc, argv}}};
        return app.run();
    }
    std::printf("Teserra %s from the installed package: %zu entity, mixed %.2f\n", TESERRA_VERSION_STRING,
                static_cast<std::size_t>(world.entityCount()), static_cast<double>(mix[100]));
    return 0;
}
