#include "appwindow.h"

#include <iostream>
#include <cstring>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define DEFINE_SCOPED_BINDING(StructName, ParamName, BindingFn, TargetName)                        \
    struct StructName                                                                              \
    {                                                                                              \
        GLuint saved_value = {};                                                                   \
        StructName() = delete;                                                                     \
        StructName(const StructName &) = delete;                                                   \
        StructName &operator=(const StructName &) = delete;                                        \
        StructName(GLuint new_value)                                                               \
        {                                                                                          \
            glGetIntegerv(ParamName, (GLint *)&saved_value);                                       \
            BindingFn(TargetName, new_value);                                                      \
        }                                                                                          \
        ~StructName() { BindingFn(TargetName, saved_value); }                                      \
    }

DEFINE_SCOPED_BINDING(ScopedTextureBinding, GL_TEXTURE_BINDING_2D, glBindTexture, GL_TEXTURE_2D);
DEFINE_SCOPED_BINDING(ScopedFrameBufferBinding, GL_DRAW_FRAMEBUFFER_BINDING, glBindFramebuffer,
                      GL_DRAW_FRAMEBUFFER);
DEFINE_SCOPED_BINDING(ScopedVBOBinding, GL_ARRAY_BUFFER_BINDING, glBindBuffer, GL_ARRAY_BUFFER);


struct DemoTexture
{
    GLuint texture;
    int width;
    int height;
    GLuint fbo;

    DemoTexture(int width, int height) : width(width), height(height)
    {
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &texture);

        ScopedTextureBinding activeTexture(texture);

        GLint old_unpack_alignment;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &old_unpack_alignment);
        GLint old_unpack_row_length;
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &old_unpack_row_length);
        GLint old_unpack_skip_pixels;
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &old_unpack_skip_pixels);
        GLint old_unpack_skip_rows;
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &old_unpack_skip_rows);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, width);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     nullptr);

        ScopedFrameBufferBinding activeFBO(fbo);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

        // assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

        glPixelStorei(GL_UNPACK_ALIGNMENT, old_unpack_alignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, old_unpack_row_length);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, old_unpack_skip_pixels);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, old_unpack_skip_rows);
    }
    DemoTexture(const DemoTexture &) = delete;
    DemoTexture &operator=(const DemoTexture &) = delete;
    ~DemoTexture() {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &texture);
    }

    template<std::invocable<> Callback>
    void with_active_fbo(Callback callback) {
        ScopedFrameBufferBinding activeFBO(fbo);
        callback();
    }
};

class DemoRenderer {
public:
    DemoRenderer(slint::ComponentWeakHandle<AppWindow> app) : app_weak(app) {}

    void operator()(slint::RenderingState state, slint::GraphicsAPI) {
        switch (state) {
        case slint::RenderingState::RenderingSetup:
            setup();
            break;
        case slint::RenderingState::BeforeRendering:
            if (auto app = app_weak.lock()) {
                auto width = (*app)->get_requested_texture_width();
                auto height = (*app)->get_requested_texture_height();
                // auto texture = render(width, height);
                // (*app)->set_texture(texture);
                
                auto resultTexture = slint::Image::create_from_borrowed_gl_2d_rgba_texture(
                    texture,
                    { static_cast<uint32_t>(width),
                    static_cast<uint32_t>(height) });
                (*app)->set_texture(resultTexture);
                (*app)->window().request_redraw();
            }
            break;
        case slint::RenderingState::AfterRendering:
            break;
        case slint::RenderingState::RenderingTeardown:
            teardown();
            break;
        }
    }

private:
    void setup() {
        //displayed_texture = std::make_unique<DemoTexture>(320, 200);
        //next_texture = std::make_unique<DemoTexture>(320, 200);

        if (auto app = app_weak.lock()) {
            auto width = (*app)->get_requested_texture_width();
            auto height = (*app)->get_requested_texture_height();
            std::cout << width << " " << height << "\n";
            unsigned char * data = new unsigned char[width * height * 4];

            memset(data, 255, sizeof(char) * width * height * 4);
            glEnable(GL_TEXTURE_2D);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glGenTextures(1, &texture);  
            glBindTexture(GL_TEXTURE_2D, texture);  
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
            //glGenerateMipmap(GL_TEXTURE_2D);


            delete[] data;
        }
    }

    /*slint::Image*/void render(int width, int height) {
        if (next_texture->width != width || next_texture->height != height) {
            auto new_texture = std::make_unique<DemoTexture>(width, height);
            std::swap(next_texture, new_texture);
        }

        next_texture->with_active_fbo([&]() {
            GLint saved_viewport[4];
            glGetIntegerv(GL_VIEWPORT, &saved_viewport[0]);
            glViewport(0, 0, next_texture->width, next_texture->height);
            glViewport(saved_viewport[0], saved_viewport[1], saved_viewport[2], saved_viewport[3]);
        });

        glUseProgram(0);

        // auto resultTexture = slint::Image::create_from_borrowed_gl_2d_rgba_texture(
        //         next_texture->texture,
        //         { static_cast<uint32_t>(next_texture->width),
        //           static_cast<uint32_t>(next_texture->height) });
        // std::swap(next_texture, displayed_texture);
        // return resultTexture;
    }

    void teardown() {
        
    }

    unsigned int texture;
    slint::ComponentWeakHandle<AppWindow> app_weak;
    std::unique_ptr<DemoTexture> displayed_texture;
    std::unique_ptr<DemoTexture> next_texture;
};


int main(int argc, char **argv) {
    auto ui = AppWindow::create();

     if (auto error = ui->window().set_rendering_notifier(DemoRenderer(ui))) {
        if (*error == slint::SetRenderingNotifierError::Unsupported) {
            fprintf(stderr,
                    "This example requires the use of a GL renderer. Please run with the "
                    "environment variable SLINT_BACKEND=winit set.\n");
        } else {
            fprintf(stderr, "Unknown error calling set_rendering_notifier\n");
        }
        exit(EXIT_FAILURE);
    }

    ui->on_request_increase_value([&]{
        ui->set_counter(ui->get_counter() + 1);
    });

    constexpr int WIDTH = 600;
    constexpr int HEIGHT = 700;

    slint::Rgb8Pixel data[WIDTH * HEIGHT];
    for (auto i = 0; i < WIDTH * HEIGHT; i++) {
        data[i] = {0xFF, 0xFF,0xFF};

    }
    //slint::Image image(slint::SharedPixelBuffer<slint::Rgb8Pixel>(WIDTH, HEIGHT, data));
    //ui->set_canvas_source(image);

    auto color_palette = std::make_shared<slint::VectorModel<slint::Color>>();
    color_palette->push_back(slint::Color::from_rgb_uint8(0, 0, 0)); // Black
    color_palette->push_back(slint::Color::from_rgb_uint8(120, 120, 120)); // Dark gray
    color_palette->push_back(slint::Color::from_rgb_uint8(130, 0, 0)); // Dark red
    color_palette->push_back(slint::Color::from_rgb_uint8(255, 0, 0)); // Red
    color_palette->push_back(slint::Color::from_rgb_uint8(255, 98, 0)); // Orange
    color_palette->push_back(slint::Color::from_rgb_uint8(255, 225, 0)); // Yellow
    color_palette->push_back(slint::Color::from_rgb_uint8(0, 156, 42)); // Green
    color_palette->push_back(slint::Color::from_rgb_uint8(0, 119, 255)); // Light blue
    color_palette->push_back(slint::Color::from_rgb_uint8(0, 3, 207)); // Dark blue
    color_palette->push_back(slint::Color::from_rgb_uint8(134, 0, 207)); // Purple

    color_palette->push_back(slint::Color::from_rgb_uint8(255, 255, 255)); // White
    color_palette->push_back(slint::Color::from_rgb_uint8(200, 200, 200)); // Light gray
    color_palette->push_back(slint::Color::from_rgb_uint8(156, 88, 0)); // Brown
    color_palette->push_back(slint::Color::from_rgb_uint8(255, 117, 117)); // Pink
    color_palette->push_back(slint::Color::from_rgb_uint8(255, 194, 64)); // Gold
    color_palette->push_back(slint::Color::from_rgb_uint8(255, 219, 140)); // Faded yellow
    color_palette->push_back(slint::Color::from_rgb_uint8(94, 255, 100)); // Light green
    color_palette->push_back(slint::Color::from_rgb_uint8(171, 209, 255)); // Baby blue
    color_palette->push_back(slint::Color::from_rgb_uint8(102, 94, 255)); // Faded blue
    color_palette->push_back(slint::Color::from_rgb_uint8(153, 94, 255)); // Faded purple

    for (int i = 0; i < 10; i++) {
        color_palette->push_back(slint::Color::from_argb_uint8(0, 255, 255, 255));
        ui->global<UserState>().set_color_palette(color_palette);
    }


    ui->global<UserState>().on_on_test([&](){

        // ui->set_canvas_source(image);
        std::cout << "test lol\n";
    });

    // ui->global<UserState>().on_mouse_click([&](float x, float y){
    //     if (x < WIDTH && x >= 0 && y >= 0 && y < HEIGHT) {
    //         data[(int)y * WIDTH + (int)x] = {0};
    //     }
    //     slint::Image image(slint::SharedPixelBuffer<slint::Rgb8Pixel>(WIDTH, HEIGHT, data));
    //     ui->set_canvas_source(image);
    // });

    ui->run();
    return 0;
}
