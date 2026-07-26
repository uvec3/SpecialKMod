#include "SpecialK/stdafx.h"

#include <imgui/imgui.h>
#include <windows.h>
#include <vector>
#include <map>
#include <iostream>
#include "../json.hpp"
#include <fstream>

#include "memory_view.h"
#include "Scanner.h"
#include "ParallelTaskManager.hpp"
#include "ui_helpers.hpp"

namespace my_mod::memory
{
    Scanner candidates;
    Scanner chains;

    std::vector<SavedResult> saved_results;
    std::map<int, bool> found_chains_selected;

    uintptr_t baseAddress;

    static int chain_depth = 7;
    static int chain_offset = 768;

    static float startValue = 0.1f;
    static float endValue = 1.0f;


    void* matrixToEdit = nullptr;
    ParallelTaskManager taskManager{1,1,1};
    ParallelTaskManager taskManagerChains{1,1,1};
    bool scanning = false;
    bool loaded = false;
    std::chrono::steady_clock::time_point startTime;


    void save_chains()
    {
        nlohmann::json j = nlohmann::json::array();
        for (auto& c : saved_results)
        {
            nlohmann::json chain_json;
            chain_json["root"] = GetPermanentAddress(c.attachedChain.rootAddress);
            chain_json["offsets"] = c.attachedChain.offsets;
            chain_json["label"] = c.label;
            if (c.forcedValue.has_value())
            {
                chain_json["forcedValue"] = c.forcedValue.value();
            }

            j.push_back(chain_json);
        }

        std::ofstream out("chains.json");
        out << j.dump(4);
    }

    void load_chains()
    {
        try
        {
            std::ifstream in("chains.json");
            nlohmann::json j;
            in >> j;
            candidates.clear();
            for (const auto& item : j)
            {
                Chain chain;
                chain.rootAddress = ResolvePermanentPointer(item["root"].get<PermanentPointer>());
                chain.offsets = item["offsets"].get<std::vector<uintptr_t>>();
                auto candidate = load_chain(chain,4);
                SavedResult candidateResult;
                candidateResult.address = candidate.address;
                candidateResult.attachedChain = chain;
                if (item.contains("forcedValue"))
                {
                    float forcedValue = item["forcedValue"].get<float>();
                    candidateResult.forcedValue = forcedValue;
                }
                if (item.contains("label"))
                {
                    candidate.attachedChain = chain;
                    candidateResult.label = item["label"].get<std::string>();
                }
                saved_results.push_back(candidateResult);
            }
        }
        catch (const std::exception&)
        {
            // std::cerr << "Failed to load candidates: " << e.what() << std::endl;
        }
    }



    void updateForcedValues()
    {
        for (auto& candidate : saved_results)
        {
            if (candidate.forcedValue.has_value())
            {
                auto scanResult = load_chain(candidate.attachedChain,4);
                if (scanResult.isValid())
                {
                    candidate.address = scanResult.address;
                    SafeWriteFloat(candidate.address, candidate.forcedValue.value());
                }
            }
        }
    }


    void drawSavedValues()
    {
        // Set a sensible initial window size
        ImGui::SetNextWindowSize(ImVec2(820, 480), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Saved Results"))
        {
            if (ImGui::Button("Save Chains to disk"))
            {
                save_chains();
            }

            if (ImGui::IsItemHovered())
            {
                if (ImGui::BeginTooltip())
                {
                    ImGui::Text("Chains will be saved into chains.json file.");
                    ImGui::EndTooltip();
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Reload Chains from disk"))
            {
                load_chains();
            }

            // Use a table with columns: Label, Address+Offset, Current Value, Forced Value, Actions
            ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchSame;
            if (ImGui::BeginTable("SavedResultsTable", 5, flags))
            {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Address / Offset[0]", ImGuiTableColumnFlags_WidthFixed, 220.0f);
                ImGui::TableSetupColumn("Current Value", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableSetupColumn("Forced Value", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 220.0f);
                ImGui::TableHeadersRow();

                int deleteIndex = -1;
                int duplicateIndex = -1;
                SavedResult duplicateItem; // temporary copy for duplication

                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6, 6));
                for (int i = 0; i < (int)saved_results.size(); ++i)
                {
                    SavedResult& result = saved_results[i];
                    ImGui::PushID(i);
                    ImGui::TableNextRow();

                    // Column 0: editable label
                    ImGui::SetNextItemWidth(-1);
                    ImGui::TableSetColumnIndex(0);

                    InputText("##label", &result.label);

                    // Column 1: address (readonly) and editable offset[0]
                    ImGui::TableSetColumnIndex(1);

                    // Read-only address shown as decimal (per request)
                    uint64_t addr_display = (uint64_t)result.address;
                    ImGui::InputScalar("##addr_readonly", ImGuiDataType_U64, &addr_display, nullptr, nullptr, "%llu",
                                       ImGuiInputTextFlags_ReadOnly);

                    ImGui::SameLine();
                    // Small spacing and label for offset
                    ImGui::Dummy(ImVec2(0.0f, 2.0f));

                    // Editable offset[0]
                    uintptr_t offset0_val = 0;
                    bool hasOffset0 = !result.attachedChain.offsets.empty();
                    if (hasOffset0)
                        offset0_val = result.attachedChain.offsets[0];

                    uint64_t offset0_edit = (uint64_t)offset0_val;
                    ImGui::SameLine(); // keep offset on same row if space permits
                    if (ImGui::InputScalar("##offset0", ImGuiDataType_U64, &offset0_edit, nullptr, nullptr, "%llu"))
                    {
                        // Ensure offsets vector has at least one element
                        if (hasOffset0)
                        {
                            result.attachedChain.offsets[0] = (uintptr_t)offset0_edit;
                        }
                        else
                        {
                            // insert as first offset
                            result.attachedChain.offsets.insert(result.attachedChain.offsets.begin(),
                                                                (uintptr_t)offset0_edit);
                        }

                        // Try to reload chain to update the stored address
                        auto scan = load_chain(result.attachedChain,4);
                        if (scan.isValid())
                        {
                            result.address = scan.address;
                        }
                    }

                    // Column 2: current value editable (writes to memory when changed)
                    ImGui::TableSetColumnIndex(2);
                    float currentValue = 0.0f;
                    SIZE_T bytesRead;
                    bool readOk = ReadProcessMemory(GetCurrentProcess(), (LPCVOID)result.address, &currentValue,
                                                    sizeof(float), &bytesRead) != 0;
                    float displayVal = readOk ? currentValue : 0.0f;
                    float newVal = displayVal;
                    if (ImGui::InputFloat("##currentval", &newVal, 0.0f, 0.0f, "%.9f"))
                    {
                        SafeWriteFloat(result.address, newVal);
                    }
                    if (!readOk)
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(read failed)");
                    }

                    // Column 3: forced value editing
                    ImGui::TableSetColumnIndex(3);
                    bool forceEnabled = result.forcedValue.has_value();
                    ImGui::SetNextItemWidth(18.0f);
                    if (ImGui::Checkbox("##force", &forceEnabled))
                    {
                        if (forceEnabled)
                            result.forcedValue = displayVal; // initialize to current value
                        else
                            result.forcedValue.reset();
                    }

                    ImGui::SameLine();
                    float forcedVal = result.forcedValue.value_or(displayVal);
                    if (forceEnabled)
                    {
                        if (ImGui::InputFloat("##forceval", &forcedVal, 0.0f, 0.0f, "%.9f"))
                        {
                            result.forcedValue = forcedVal;
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("-");
                    }

                    // Column 4: actions
                    ImGui::TableSetColumnIndex(4);

                    if (ImGui::Button("Delete"))
                    {
                        deleteIndex = i;
                    }
                    ImGui::SameLine();

                    if (ImGui::Button("Duplicate"))
                    {
                        duplicateIndex = i;
                        duplicateItem = result;
                    }
                    ImGui::SameLine();

                    if (ImGui::Button("Reload"))
                    {
                        auto scan = load_chain(result.attachedChain,4);
                        if (scan.isValid())
                        {
                            result.address = scan.address;
                        }
                    }
                    ImGui::SameLine();

                    if (ImGui::Button("Open as matrix"))
                    {
                        matrixToEdit = (void*)result.address;
                    }

                    ImGui::PopID();
                }
                ImGui::PopStyleVar();

                // Handle duplication and deletion after the loop to avoid invalidating indices mid-loop
                if (duplicateIndex != -1)
                {
                    saved_results.insert(saved_results.begin() + duplicateIndex + 1, duplicateItem);
                }
                if (deleteIndex != -1)
                {
                    saved_results.erase(saved_results.begin() + deleteIndex);
                }

                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    void init()
    {
        startTime = std::chrono::steady_clock::now();
    }

    void per_frame_update()
    {
        //give 5 sec before loading chains
        if (!loaded && std::chrono::steady_clock::now() - startTime > std::chrono::seconds(5))
        {
            load_chains();
            loaded = true;
        }

        updateForcedValues();
    }

    void select_filter()
    {
        ImGui::InputFloat("Start Value", &startValue, 0.0f, 0.0f, "%.9f");
        ImGui::SameLine();
        ImGui::InputScalar("##starthex", ImGuiDataType_U32, (void*)&startValue, nullptr, nullptr, "0x%08X");
        ImGui::InputFloat("End Value", &endValue, 0.0f, 0.0f, "%.9f");
        ImGui::SameLine();
        ImGui::InputScalar("##endhex", ImGuiDataType_U32, (void*)&endValue, nullptr, nullptr, "0x%08X");
    }


    void candidates_table(bool show_table)
    {


        if (show_table)
        {
            ImGui::PushID("candidates");
            if (ImGui::BeginChild("Results", ImVec2(0, 0), true))
            {
                // Use BeginTable with explicit flags and allow column stretching
                ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Sortable | ImGuiTableFlags_Reorderable |
                    ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings;

                if (!scanning && ImGui::BeginTable("ResultsTable", 6, tableFlags))
                {
                    // Make columns stretch to fill available space
                    ImGui::TableSetupColumn("address", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                    ImGui::TableSetupColumn("relative address", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                    ImGui::TableSetupColumn("value float", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("value hex", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("force value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("##buttons", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableHeadersRow();


                    int removeIndex = -1;
                    for (int i = 0; i < static_cast<int>(candidates.candidates.size()); i++)
                    {
                        auto& candidate = candidates.candidates.at(i);
                        ImGui::PushID(i);
                        //check if candidate is still valid before displaying
                        float currentValue;
                        SIZE_T bytesRead;
                        if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)candidate.address, &currentValue,
                                               sizeof(float), &bytesRead))
                            continue;

                        float newValue = currentValue;
                        ImGui::TableNextRow();
                        // Column 0: address
                        ImGui::TableSetColumnIndex(0);

                        if (candidate.attachedChain.has_value())
                        {
                            ImGui::Text("C");
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                ImGui::Text("Chain: %s", candidate.attachedChain->to_string().c_str());
                                ImGui::EndTooltip();
                            }
                            ImGui::SameLine();
                        }


                        // Print address in portable hex form
                        ImGui::InputScalar("##address", ImGuiDataType_U64, (void*)&candidate.address, nullptr, nullptr,
                                           "0x%016llX");

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            auto permanent = GetPermanentAddress(candidate.address);
                            if (!permanent.moduleName.empty())
                            {
                                ImGui::Text("Module: %s", permanent.moduleName.c_str());
                                ImGui::Text("Relative Offset: 0x%llX", permanent.relativeOffset);
                            }
                            else
                            {
                                ImGui::Text("No module found for this address");
                            }
                            ImGui::EndTooltip();
                        }

                        uintptr_t relativeAddress = candidate.address - baseAddress;
                        ImGui::TableSetColumnIndex(1);
                        ImGui::InputScalar("##relative", ImGuiDataType_U64, (void*)&relativeAddress, nullptr, nullptr);
                        candidate.address = baseAddress + relativeAddress;

                        // Column 1: float value editable
                        ImGui::TableSetColumnIndex(2);
                        ImGui::InputScalar("##value", ImGuiDataType_Float, (void*)&newValue, nullptr, nullptr, "%.12f");
                        if (newValue != currentValue)
                        {
                            SafeWriteFloat(candidate.address, newValue);
                        }

                        // Column 2: float value as hex bits
                        ImGui::TableSetColumnIndex(3);
                        ImGui::InputScalar("##valuehex", ImGuiDataType_U32, (void*)&newValue, nullptr, nullptr,
                                           "0x%08X");

                        ImGui::TableSetColumnIndex(4);

                        bool forceEnabled = candidate.forcedValue.has_value();
                        ImGui::Checkbox("##force", &forceEnabled);
                        if (forceEnabled)
                        {
                            float forcedVal = candidate.forcedValue.value_or(currentValue);
                            ImGui::SameLine();
                            ImGui::InputScalar("##forceval", ImGuiDataType_Float, (void*)&forcedVal, nullptr, nullptr,
                                               "%.9f");
                            if (!candidate.forcedValue.has_value() || forcedVal != candidate.forcedValue.value())
                            {
                                candidate.forcedValue = forcedVal;
                            }
                        }
                        else if (candidate.forcedValue.has_value())
                        {
                            candidate.forcedValue.reset();
                        }

                        ImGui::TableSetColumnIndex(5);
                        if (ImGui::Button("Remove"))
                        {
                            removeIndex = i;
                        }


                        ImGui::SameLine();
                        if (ImGui::Button("Open as matrix"))
                        {
                            matrixToEdit = (void*)candidate.address;
                        }

                        ImGui::SameLine();
                        if (ImGui::Button("Search for chains"))
                        {
                            scanning = true;
                            taskManagerChains.runTask([address = candidate.address,size=candidate.previous_value_size()]()
                                                 {
                                                     auto new_chains=SearchForChains(address, chain_depth, chain_offset,static_cast<int>(size));
                                                     scanning = false;
                                                     return new_chains;
                                                 }, [](std::vector<ScanResult> newChains)
                                                 {
                                                     chains.candidates =std::move(newChains);
                                                 });
                        }


                        ImGui::PopID();
                    }

                    if (removeIndex != -1)
                    {
                        candidates.candidates.erase(candidates.candidates.begin() + removeIndex);
                    }

                    ImGui::EndTable();
                }

                ImGui::EndChild();
            }
            ImGui::PopID();
        }
    }

    void found_chains_table()
    {
        ImGui::PushID("chains");

        ImGui::Begin("Found Chains");
        if (ImGui::Button("Save"))
        {
            chains.save("cache.json");
        }
        ImGui::SameLine();
        if (ImGui::Button("Load"))
        {
            chains.load("cache.json");
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear all"))
        {
            chains.clear();
        }

        chains.draw();

        ImGui::Text("%d elements",chains.candidates.size());

        if (!chains.candidates.empty())
        {
            // Control buttons
            if (ImGui::Button("Select All"))
            {
                for (int i = 0; i < static_cast<int>(chains.candidates.size()); i++)
                {
                    found_chains_selected[i] = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Select None"))
            {
                for (int i = 0; i < static_cast<int>(chains.candidates.size()); i++)
                {
                    found_chains_selected[i] = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Selected"))
            {
                for (int i = 0; i < static_cast<int>(chains.candidates.size()); i++)
                {
                    if (found_chains_selected[i])
                    {
                        SavedResult result;
                        result.address = chains.candidates[i].address;
                        if (chains.candidates[i].attachedChain.has_value())
                        {
                            result.attachedChain = chains.candidates[i].attachedChain.value();
                        }
                        if (chains.candidates[i].forcedValue.has_value())
                        {
                            result.forcedValue = chains.candidates[i].forcedValue.value();
                        }
                        result.label = "Found Chain " + std::to_string(i);
                        saved_results.push_back(result);
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Selected"))
            {
                for (int i = static_cast<int>(chains.candidates.size()) - 1; i >= 0; i--)
                {
                    if (found_chains_selected[i])
                    {
                        chains.candidates.erase(chains.candidates.begin() + i);
                        found_chains_selected.erase(i);
                    }
                }
            }

            ImGui::Separator();

            // Table with selection checkboxes
            ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Sortable | ImGuiTableFlags_Reorderable |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings;

            if (ImGui::BeginTable("FoundChainsTable", 7, tableFlags))
            {
                ImGui::TableSetupColumn("##select", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                ImGui::TableSetupColumn("address", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("relative address", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("value float", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("value hex", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("force value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("##buttons", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableHeadersRow();

                int removeIndex = -1;
                for (int i = 0; i < static_cast<int>(chains.candidates.size()); i++)
                {
                    auto& chain = chains.candidates[i];
                    ImGui::PushID(i);

                    // Check if chain is still valid before displaying
                    float currentValue;
                    SIZE_T bytesRead;
                    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)chain.address, &currentValue,
                                           sizeof(float), &bytesRead))
                    {
                        ImGui::PopID();
                        continue;
                    }

                    float newValue = currentValue;
                    ImGui::TableNextRow();

                    // Column 0: Selection checkbox
                    ImGui::TableSetColumnIndex(0);
                    bool isSelected = found_chains_selected[i];
                    if (ImGui::Checkbox("##select", &isSelected))
                    {
                        found_chains_selected[i] = isSelected;
                    }

                    // Column 1: address
                    ImGui::TableSetColumnIndex(1);
                    if (chain.attachedChain.has_value())
                    {
                        ImGui::Text("C");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            ImGui::Text("Chain: %s", chain.attachedChain->to_string().c_str());
                            ImGui::EndTooltip();
                        }
                        ImGui::SameLine();
                    }

                    ImGui::InputScalar("##address", ImGuiDataType_U64, (void*)&chain.address, nullptr, nullptr,
                                       "0x%016llX");

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        auto permanent = GetPermanentAddress(chain.address);
                        if (!permanent.moduleName.empty())
                        {
                            ImGui::Text("Module: %s", permanent.moduleName.c_str());
                            ImGui::Text("Relative Offset: 0x%llX", permanent.relativeOffset);
                        }
                        else
                        {
                            ImGui::Text("No module found for this address");
                        }
                        ImGui::EndTooltip();
                    }

                    uintptr_t relativeAddress = chain.address - baseAddress;
                    ImGui::TableSetColumnIndex(2);
                    ImGui::InputScalar("##relative", ImGuiDataType_U64, (void*)&relativeAddress, nullptr, nullptr);
                    chain.address = baseAddress + relativeAddress;

                    // Column 3: float value editable
                    ImGui::TableSetColumnIndex(3);
                    ImGui::InputScalar("##value", ImGuiDataType_Float, (void*)&newValue, nullptr, nullptr, "%.12f");
                    if (newValue != currentValue)
                    {
                        SafeWriteFloat(chain.address, newValue);
                    }

                    // Column 4: float value as hex bits
                    ImGui::TableSetColumnIndex(4);
                    ImGui::InputScalar("##valuehex", ImGuiDataType_U32, (void*)&newValue, nullptr, nullptr,
                                       "0x%08X");

                    ImGui::TableSetColumnIndex(5);

                    bool forceEnabled = chain.forcedValue.has_value();
                    ImGui::Checkbox("##force", &forceEnabled);
                    if (forceEnabled)
                    {
                        float forcedVal = chain.forcedValue.value_or(currentValue);
                        ImGui::SameLine();
                        ImGui::InputScalar("##forceval", ImGuiDataType_Float, (void*)&forcedVal, nullptr, nullptr,
                                           "%.9f");
                        if (!chain.forcedValue.has_value() || forcedVal != chain.forcedValue.value())
                        {
                            chain.forcedValue = forcedVal;
                        }
                    }
                    else if (chain.forcedValue.has_value())
                    {
                        chain.forcedValue.reset();
                    }

                    ImGui::TableSetColumnIndex(6);
                    if (ImGui::Button("Remove"))
                    {
                        removeIndex = i;
                    }

                    ImGui::PopID();
                }

                if (removeIndex != -1)
                {
                    chains.candidates.erase(chains.candidates.begin() + removeIndex);
                    found_chains_selected.erase(removeIndex);
                }

                ImGui::EndTable();
            }
        }
        else
        {
            ImGui::Text("No chains found. Use 'Search for chains' from the candidates table.");
        }

        ImGui::End();

        ImGui::PopID();
    }



    void draw()
    {
        ImGui::Begin("Memory Scanner");

        std::lock_guard<std::mutex> lock(candidates.candidates_mutex);
        taskManager.finish();
        taskManagerChains.finish();

        select_filter();

        candidates.draw();

        ImGui::BeginDisabled(scanning);
        if (ImGui::Button("Initial Scan"))
        {
            scanning = true;
            taskManager.runTask([]()
                                {
                                    ProjectionMatFilter filter{1.777f};

                                    return InitialScan(filter);
                                }, [](std::vector<ScanResult> new_candidates)
                                {
                                    scanning = false;
                                    candidates.candidates = std::move(new_candidates);
                                });
        }
        ImGui::EndDisabled();



        ImGui::BeginDisabled(scanning);

        // if (ImGui::Button("Filter Unchanged"))
        // {
        //     FilterUnchanged(candidates);
        // }
        //
        // if (ImGui::Button("Scan Decreased"))
        // {
        //     ScanDecreased(candidates);
        // }
        ImGui::SameLine();

        if (ImGui::Button("Scan Increase"))
        {
        }


        if (chain_iterations == ChainIterationState::Finished)
        {
            ImGui::Text("Scan complete. Total addresses found: %llu", total_addresses_found.load());
        }
        else if (chain_iterations == ChainIterationState::SortingPointers)
        {
            ImGui::Text("Sorting pointers... %llu pointers found", snapshot_size.load());
        }
        else if (chain_iterations == ChainIterationState::SnapshotPhase)
        {
            ImGui::Text("Building snapshot of memory pointers... %llu pointers found so far", snapshot_size.load());
        }
        else if (chain_iterations == ChainIterationState::BuildingChains)
        {
            ImGui::Text("Building chains... Total chains found: %llu, chains created: %llu",
                        total_addresses_found.load(), chains_created);
        }
        else
        {
            ImGui::Text("Searching for chains... Iteration %d ( %llu ) Total chains found: %d, currently in search %d",
                        chain_iterations.load(), bytes_read.load(), total_addresses_found.load(),
                        number_of_addresses_in_search.load());
        }

        static float globalNewValue = 0.0f;
        ImGui::InputFloat(" New Value", &globalNewValue, 0.0f, 0.0f, "%.9f");
        ImGui::SameLine();
        ImGui::InputScalar("##newhex", ImGuiDataType_U32, (void*)&globalNewValue, nullptr, nullptr, "0x%08X");
        if (ImGui::Button("Set All"))
        {
            for (const auto& candidate : candidates.candidates)
            {
                SafeWriteFloat(candidate.address, globalNewValue);
            }
        }


        ImGui::InputInt("Chain Depth", &chain_depth);
        ImGui::InputInt("Chain Offset", &chain_offset);


        baseAddress = (uintptr_t)GetModuleHandle(NULL);
        ImGui::InputScalar("Base Address", ImGuiDataType_U64, (void*)&baseAddress, nullptr, nullptr, "0x%016llX",
                           ImGuiInputTextFlags_ReadOnly);

        ImGui::EndDisabled();
        ImGui::Text("Candidates: %d", (int)candidates.candidates.size());


        static bool show_table = false;
        ImGui::Checkbox("Show Table", &show_table);

        candidates_table(show_table);

        ImGui::End();

        matrixEditor(matrixToEdit);
        drawSavedValues();
        found_chains_table();
    }
}
