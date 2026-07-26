#pragma once

class Value
{
public:
    float value;


    void edit()
    {
        ImGui::PushID(this);
        ImGui::InputFloat("##value",&value);
        ImGui::PopID();
    }
};
