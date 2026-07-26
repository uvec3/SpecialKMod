#include "SpecialK/stdafx.h"
#include "SpeedMod.hpp"
#include "SpeedHook.h"
#include <imgui/imgui.h>


namespace my_mod::speed_mod
{
    float speedup_koeff = 1.0f;
    bool cancel_queued = false;
    float tweak_value = 1;

    void init()
    {
    }

    void per_frame_update()
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsKeyDown(ImGuiKey_C))
        {
            ApplyTemporarySpedup(1.f + 0.3f * speedup_koeff, tweak_value);
        }
        else
        {
            const bool directionPressed = ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_S) ||
                ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_D);
            if (directionPressed)
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    ApplyTemporarySpedup(1.f + 0.3f * speedup_koeff, 150);
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    ApplyTemporarySpedup(1.f + 0.3f * speedup_koeff, 150);
                }
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            ApplyTemporarySpedup(1.f + 0.3f * speedup_koeff, 150);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            ApplyTemporarySpedup(1.f + 0.3f * speedup_koeff, 150);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_F10, false))
        {
            ApplyTemporarySpedup(1.f + 1.f * speedup_koeff, 40.f);
            cancel_queued = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_F, false))
        {
            ApplyTemporarySpedup(1.f + 0.3f * speedup_koeff, 200.f);
            cancel_queued = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_V, false))
        {
            ApplyTemporarySpedup(1.f + 0.4f * speedup_koeff, 200.f);
            cancel_queued = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_ModShift, false))
        {
            ApplyTemporarySpedup(1.f + 0.2f * speedup_koeff, 200.f);
        }

        if (ImGui::IsKeyReleased(ImGuiKey_Q))
        {
            ApplyTemporarySpedup(1.f + 0.2f * speedup_koeff, 500.f);
            cancel_queued = true;
        }


        TimeOnFrameUpdate();
    }

    void speed_hack_window()
    {
        static float speedFactor = 1.0f;

        //set initial window position at top right corner
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 220, 20), ImGuiCond_Appearing);
        ImGui::Begin("Speed Hack");
        if (ImGui::InputFloat("Speed Factor", &speedFactor, 0.1f, 0.0f, "%.2f"))
        {
            SetSpeedMultiplier(speedFactor);
        }
        static std::string hookingStatus = "";
        if (ImGui::Button("Apply Speed Hack"))
        {
            hookingStatus = InitializeSpeedHook(speedFactor);
        }
        ImGui::Text("%s", hookingStatus.c_str());

        //print QueryPerformanceCounter values for testing
        LARGE_INTEGER qpcValue;
        if (QueryPerformanceCounter(&qpcValue))
        {
            ImGui::Text("Current QPC Value: %lld", qpcValue.QuadPart);
        }
        ImGui::Text("Current speedup %f", g_SpeedMultiplier);
        ImGui::DragFloat("Speed Hack coefficient", &speedup_koeff, 0.1f, 0.0f, 5.0f, "%.1f");
        ImGui::InputFloat("Tweak value", &tweak_value);
        ImGui::End();


        if(ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsKeyDown(ImGuiKey_C))
        {
            ApplyTemporarySpedup(1.f + 0.3f * speedup_koeff, tweak_value);
        }
        else
        {
            const bool directionPressed = ImGui::IsKeyDown(ImGuiKey_E) || ImGui::IsKeyDown(ImGuiKey_D) ||
                ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_F);
            if (directionPressed)
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    ApplyTemporarySpedup(1.f + 0.3f * speedup_koeff, 150);
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    ApplyTemporarySpedup(1.f + 0.3f * speedup_koeff, 150);
                }
            }
        }

        if (ImGui::IsMouseReleased (ImGuiMouseButton_Left))
        {
            ApplyTemporarySpedup(1.f + 0.3f * speedup_koeff, 150);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            ApplyTemporarySpedup(1.f + 0.3f * speedup_koeff, 150);
        }

        if(ImGui::IsKeyPressed (ImGuiKey_F10,false))
        {
            ApplyTemporarySpedup(1.f + 1.f * speedup_koeff, 40.f);
            cancel_queued = true;
        }

        if(ImGui::IsKeyPressed(ImGuiKey_G,false))
        {
            ApplyTemporarySpedup(1.f + 0.3f * speedup_koeff, 200.f);
            cancel_queued = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_V,false))
        {
            ApplyTemporarySpedup(1.f + 0.4f * speedup_koeff, 200.f);
            cancel_queued = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_A,  false))
        {
            ApplyTemporarySpedup(1.f + 0.2f * speedup_koeff, 200.f);
        }

        if(ImGui::IsKeyReleased(ImGuiKey_W))
        {
            ApplyTemporarySpedup(1.f + 0.2f * speedup_koeff, 500.f);
            cancel_queued = true;
        }
    }
}
