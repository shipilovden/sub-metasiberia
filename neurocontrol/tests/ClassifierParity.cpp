#include "NeuroClassifier.h"
#include <json.hpp>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
    if(argc != 3) { std::cerr << "usage: neuro_classifier_check MODEL.json RECORDING.f32\n"; return 2; }
    try
    {
        std::ifstream model_file(argv[1]);
        nlohmann::json json;
        model_file >> json;
        neurocontrol::Model model;
        model.sample_rate_hz = json.at("sample_rate_hz").get<int>();
        model.window_samples = json.at("window_samples").get<int>();
        model.step_samples = json.value("step_samples", 0);
        model.channels = json.at("channels").get<std::vector<std::string>>();
        for(const auto& section : json.at("sos_bandpass_8_30_hz"))
        {
            const auto values = section.get<std::vector<double>>();
            if(values.size() != 6) return 3;
            model.sos.push_back({values[0], values[1], values[2], values[3], values[4], values[5]});
        }
        model.csp_filters = json.at("csp_filters").get<std::vector<std::vector<double>>>();
        model.lda_coefficients = json.at("lda_coefficients").get<std::vector<double>>();
        model.lda_intercept = json.at("lda_intercept").get<double>();
        model.forward_threshold = json.at("forward_threshold").get<double>();
        neurocontrol::NeuroClassifier classifier(model);
        std::ifstream recording(argv[2], std::ios::binary);
        if(!recording) return 4;
        std::vector<float> row(model.channels.size());
        while(recording.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(float))))
        {
            const auto result = classifier.pushSample(row.data(), row.size());
            if(result.available)
                std::cout << result.sample_index << ',' << result.forward_probability << ','
                          << result.signal_good << ',' << result.forward << '\n';
        }
        if(!recording.eof()) return 5;
    }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 6; }
    return 0;
}
