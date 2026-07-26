
#include <SpecialK/stdafx.h>
#include "MemorySacanner/MemoryMod.hpp"
#include "SpeedHack/SpeedMod.hpp"

namespace  my_mod
{
    bool initialized = false;

    void init()
    {
        speed_mod::init();
        memory::init();
    }

    void per_frame_update()
    {
        if (!initialized)
        {
            init();
            initialized = true;
        }

        speed_mod::per_frame_update();
        memory::per_frame_update();
    }

    void draw()
    {
        speed_mod::speed_hack_window();
        memory::draw();
    }
}
