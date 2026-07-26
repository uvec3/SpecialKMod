#pragma once

struct InputTextCallback_UserData
{
    std::string* Str;
    ImGuiInputTextCallback ChainCallback;
    void* ChainCallbackUserData;
};

static int InputTextCallback(ImGuiInputTextCallbackData* data)
{
    InputTextCallback_UserData* user_data = (InputTextCallback_UserData*)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        // Resize string callback
        // If for some reason we refuse the new length (BufTextLen) and/or capacity (BufSize) we need to set them back to what we want.
        std::string* str = user_data->Str;
        IM_ASSERT(data->Buf == str->c_str());
        str->resize(data->BufTextLen);
        data->Buf = (char*)str->c_str();
    }
    else if (user_data->ChainCallback)
    {
        // Forward to user callback, if any
        data->UserData = user_data->ChainCallbackUserData;
        return user_data->ChainCallback(data);
    }
    return 0;
}

bool InputText(const char* label, std::string* str, ImGuiInputTextFlags flags = 0,
               ImGuiInputTextCallback callback = nullptr, void* user_data = nullptr)
{
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;

    InputTextCallback_UserData cb_user_data;
    cb_user_data.Str = str;
    cb_user_data.ChainCallback = callback;
    cb_user_data.ChainCallbackUserData = user_data;
    return ImGui::InputText(label, (char*)str->c_str(), str->capacity() + 1, flags, InputTextCallback,
                            &cb_user_data);
}


void matrixEditor(void* matrixToEdit)
 {
     if (matrixToEdit == nullptr)
     {
         return;
     }
     bool open = true;
     if (ImGui::Begin("MatrixEditor", &open))
     {
         ImGui::Text("Matrix at 0x%p", matrixToEdit);
         ImGui::Separator();
         std::vector<float> matrixValues(16);
         SIZE_T bytesRead;
         if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)((uintptr_t)matrixToEdit), matrixValues.data(),
                                matrixValues.size() * sizeof(float), &bytesRead))
         {
             ImGui::Text("Failed to read matrix data");
         }
         else
         {
             for (int i = 0; i < 16; i++)
             {
                 float value = matrixValues[i];

                 if (i % 4 != 0)
                     ImGui::SameLine();

                 std::string label = "##mat" + std::to_string(i);
                 ImGui::SetNextItemWidth(40);
                 ImGui::InputFloat(label.c_str(), &value, 0.0f, 0.0f, "%.12f");

                 if (value != matrixValues[i])
                 {
                     SafeWriteFloat((uintptr_t)matrixToEdit + i * sizeof(float), value);
                 }
             }
         }
     }
     ImGui::End();

     if (!open)
     {
         matrixToEdit = nullptr;
     }
 }
