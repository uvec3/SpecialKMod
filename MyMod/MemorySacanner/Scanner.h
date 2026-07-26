#pragma once

#include <imgui/imgui.h>
#include "memory_view.h"
#include "Value.h"


class Scanner
{
public:
    std::vector<ScanResult> candidates;
    std::unique_ptr<Filter> selectedFilter;
    bool continues_filtering=false;
    ProjectionMatFilter filter{1.777f};
    std::mutex candidates_mutex;
    Value setValue;
    int binarySearch=0;

public:


    void draw()
    {
        if (ImGui::Button("Filter In Diapason"))
        {
            FilterValues(candidates, filter);
        }

        ImGui::SameLine();

        if (!continues_filtering)
        {
            if (ImGui::Button("Start Continues Filtering"))
            {
                start_continues_filtering();
            }
        }
        else
        {
            if (ImGui::Button("Stop Continues Filtering"))
            {
                continues_filtering = false;
            }
        }



        ImGui::Separator();

        binary_search();

        ImGui::Separator();

    }
    void clear()
    {
        candidates.clear();
    }
    void start_continues_filtering()
    {
        continues_filtering = true;
        std::thread([this]
        {
            while (continues_filtering)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard<std::mutex> lock(candidates_mutex);
                FilterValues(candidates, filter);
            }
        }).detach();
    }
    void save(const std::string& file);
    void load(const std::string& file);


private:
    void binary_search()
    {
        ImGui::Text("Manual binary search:");

        auto alter_values=[&,this]()
        {
            for (int i=0;i<static_cast<int>(candidates.size())/2;++i)
            {
                candidates[i].setValue(setValue);
            }
        };

        auto reset_values=[&,this]()
        {
            for (int i=0;i<static_cast<int>(candidates.size())/2;++i)
            {
                candidates[i].resetValue();
            }
        };

        setValue.edit();

        if(binarySearch==0)
        {

            if (ImGui::Button("Start search"))
            {
                alter_values();
                binarySearch=1;
            }
        }
        else
        {
            if (candidates.size()==1)
            {
                reset_values();
                binarySearch=0;
            }
            ImGui::Text("Value changed?");
            ImGui::SameLine();
            if(ImGui::Button("Yes"))
            {
                candidates.resize(candidates.size()/2);

                if (binarySearch==1)
                {
                    reset_values();
                    binarySearch=2;
                }
                else
                {
                    alter_values();
                    binarySearch=1;
                }
            }

            ImGui::SameLine();

            if(ImGui::Button("No"))
            {
                candidates.erase(candidates.begin(),candidates.begin()+candidates.size()/2);
                if (binarySearch==1)
                {
                    alter_values();
                }
                else
                {
                    reset_values();
                }
            }
            if(ImGui::Button("Reset"))
            {
                reset_values();
                binarySearch=0;
            }
        }
    }

};

