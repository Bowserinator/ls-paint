#include "appwindow.h"

#include <iostream>

int main(int argc, char **argv) {
    auto ui = AppWindow::create();

    ui->on_request_increase_value([&]{
        ui->set_counter(ui->get_counter() + 1);
    });

    constexpr int WIDTH = 600;
    constexpr int HEIGHT = 700;

    slint::Rgb8Pixel data[WIDTH * HEIGHT];
    for (auto i = 0; i < WIDTH * HEIGHT; i++) {
        data[i] = {0xFF, 0xFF,0xFF};

    }
    slint::Image image(slint::SharedPixelBuffer<slint::Rgb8Pixel>(WIDTH, HEIGHT, data));
    ui->set_canvas_source(image);

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

        ui->set_canvas_source(image);
        std::cout << "test lol\n";
    });

    ui->global<UserState>().on_mouse_click([&](float x, float y){
        if (x < WIDTH && x >= 0 && y >= 0 && y < HEIGHT) {
            data[(int)y * WIDTH + (int)x] = {0};
        }
        slint::Image image(slint::SharedPixelBuffer<slint::Rgb8Pixel>(WIDTH, HEIGHT, data));
        ui->set_canvas_source(image);
    });

    ui->run();
    return 0;
}
