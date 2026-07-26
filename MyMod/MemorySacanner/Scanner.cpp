#include "SpecialK/stdafx.h"
#include "Scanner.h"

#include <iostream>

void Scanner::save(const std::string& file)
{
    nlohmann::json j = nlohmann::json::array();
    for (auto& c : candidates)
    {
        nlohmann::json chain_json;
        chain_json["root"] = GetPermanentAddress(c.attachedChain.value().rootAddress);
        chain_json["offsets"] = c.attachedChain.value().offsets;
        chain_json["previous_value"]=c.previous_value();

        if (c.forcedValue.has_value())
        {
            chain_json["forcedValue"] = c.forcedValue.value();
        }

        j.push_back(chain_json);
    }

    std::ofstream out(file);
    out << j.dump(4);
}

void Scanner::load(const std::string& file)
{
    try
    {
        std::ifstream in(file);
        nlohmann::json j;
        in >> j;
        candidates.clear();
        for (const auto& item : j)
        {
            Chain chain;
            chain.rootAddress = ResolvePermanentPointer(item["root"].get<PermanentPointer>());
            chain.offsets = item["offsets"].get<std::vector<uintptr_t>>();

            std::vector<char> previous_value=item["previous_value"];
            auto candidate = load_chain(chain,static_cast<int>(previous_value.size()));
            if (candidate.isValid())
            {
                if (item.contains("forcedValue"))
                {
                    float forcedValue = item["forcedValue"].get<float>();
                    candidate.forcedValue = forcedValue;
                }
                candidate.set_previous_value(previous_value);
                candidates.push_back(candidate);
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to load candidates: " << e.what() << std::endl;
    }
}
