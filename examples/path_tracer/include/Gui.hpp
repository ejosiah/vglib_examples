#pragma once

#include "ImGuiPlugin.hpp"
#include "Model.hpp"
#include "spectrum/spectrum.hpp"

class Gui {
public:
    bool takeScreenShot{};
    bool hide{};

    Gui(Plugin* imGui = nullptr, Model* model = nullptr):
    imGui{imGui}, _model{model} {}

    void update(){

    }

    void render(VkCommandBuffer commandBuffer){
        if(hide) return;
        ImGui::Begin("Path tracer");
        ImGui::SetWindowSize("Path tracer", {0, 0});

        ImGui::Text("num samples: %d, fps: %.0f",_model->sceneConstants.currentSample, _model->fps);
        ImGui::Indent(16);
        takeScreenShot = ImGui::Button("[F1] Screenshot");
        ImGui::SameLine();
        hide = ImGui::Button("[F2] Hide/show GUI");
        ImGui::Indent(-16);
        ImGui::Separator();

        ImGui::Text("Path Tracing:");
        ImGui::Indent(16);
        static int maxBounces = int(_model->sceneConstants.maxBounces);
        _dirty |= ImGui::SliderInt("Max. Bounces", &maxBounces, 1, 16);
        _model->sceneConstants.maxBounces = uint32_t(maxBounces);

        bool accumulate = bool (_model->sceneConstants.adaptiveSampling);

        int samples = static_cast<int>(_model->sceneConstants.numSamples);
        if(!accumulate){
            ImGui::SliderInt("samples", &samples, 1, 100);
        }else{
            ImGui::SliderInt("samples", &samples, 1000, Model::MaxSamples);
        }
        _model->sceneConstants.numSamples = static_cast<uint32_t>(samples);

        _dirty |= ImGui::Checkbox("Accumulate", &accumulate);
        _model->sceneConstants.adaptiveSampling = int(accumulate);

        ImGui::SameLine();

        if(_model->sceneConstants.currentSample < _model->sceneConstants.numSamples) {
            ImGui::Checkbox("denoise", &_model->denoise);
        }else{
            _model->denoise = ImGui::Button("denoise");
        }

        ImGui::Indent(-16);
        ImGui::Separator();


        ImGui::Text("Camera");
        ImGui::Indent(16);
        _dirty |= ImGui::SliderFloat("Exposure", &_model->sceneConstants.exposure, 0, 10);
        ImGui::Indent(-16);
        ImGui::Separator();

        ImGui::Text("Objects:");
        ImGui::Indent(16);
        _dirty |= ImGui::Checkbox("Cornell box", &cornell); ImGui::SameLine();
        _dirty |= ImGui::Checkbox("Floor", &plane);

        if(_model->dragonReady) {
            ImGui::SameLine();
            _dirty |= ImGui::Checkbox("Dragon", &dragon);
        }

        ImGui::Indent(-16);
        ImGui::Separator();

        ImGui::Text("Diffuse BRDF:");
        ImGui::Indent(16);
        recompile |= ImGui::RadioButton("Lamberian", &_model->specializationConstants.diffuseBrdfType, int (DiffuseBrdf::Lambertian));
        ImGui::SameLine();
        recompile |= ImGui::RadioButton("Oren Nayar", &_model->specializationConstants.diffuseBrdfType, int(DiffuseBrdf::OrenNayar));
        ImGui::SameLine();
        recompile |= ImGui::RadioButton("Disney", &_model->specializationConstants.diffuseBrdfType, int(DiffuseBrdf::Disney));
        ImGui::Indent(-16);
        ImGui::Separator();

        ImGui::Text("Specular BRDF:");
        ImGui::Indent(16);
        recompile |= ImGui::RadioButton("Microfacet", &_model->specializationConstants.specularBrdfType, int(SpecularBrdf::Microfacet));
        ImGui::SameLine();
        recompile |= ImGui::RadioButton("Phong", &_model->specializationConstants.specularBrdfType, int(SpecularBrdf::Phong));

        if(_model->specializationConstants.specularBrdfType == int(SpecularBrdf::Microfacet)){
            ImGui::Text("Normal Distribution Function:");
            ImGui::SameLine();
            recompile |= ImGui::RadioButton("GGX", &_model->specializationConstants.ndfFunction, int(Ndf::GGX));
            ImGui::SameLine();
            recompile |= ImGui::RadioButton("Beckmann", &_model->specializationConstants.ndfFunction, int(Ndf::Beckmann));
        }

        if(_model->specializationConstants.ndfFunction == int(Ndf::GGX)){
            static bool optimized = true;
            recompile |= ImGui::Checkbox("optimized", &optimized);
            _model->specializationConstants.useOptimizedG2 = int(optimized);
            ImGui::SameLine();
        }else{
            _model->specializationConstants.useOptimizedG2 = int(false);
        }

        static bool heightCorrelated = true;
        recompile |= ImGui::Checkbox("height correlated", &heightCorrelated);
        _model->specializationConstants.useHeightCorrelatedG2 = int(heightCorrelated);

        ImGui::Indent(-16);


        if(ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)){
            if((_model->sceneConstants.mask & ObjectTypes::eCornellBox) != ObjectTypes::eCornellBox){
                selectedObj = DragonId;
            }

            ImGui::Indent(16);
            static std::array<const char*, 4> items{ "Floor", "Short box", "Tall box", "Dragon" };
            auto size = _model->dragonReady ? items.size() : items.size() - 1;
            ImGui::Combo("Select", &selectedObj, items.data(),size);



            Material* material{};
            if(selectedObj == FloorId){
                material = _model->floorMaterial;
            }else if(selectedObj == DragonId){
                material = _model->dragonMaterial;
            }else if(selectedObj == ShortBoxId){
                material = &_model->cornellMaterials[6];
            }else {
                material = &_model->cornellMaterials[7];
            }

            uint32_t * selectedObjectHitGroup = getHitGroup();
            int hitGroup = static_cast<int>(*selectedObjectHitGroup);


            if(selectedObj == ShortBoxId || selectedObj == TallBoxId || selectedObj == DragonId) {
                static std::array<const char *, 3> hitGroups{"general", "volume", "glass"};
                recompile |= ImGui::Combo("hit group", &hitGroup, hitGroups.data(), hitGroups.size());
                *selectedObjectHitGroup = static_cast<uint32_t>(hitGroup);
            }

            if(hitGroup != HitGroup::Volume) {
                _dirty |= ImGui::SliderFloat("metalness", &material->metalness.x, 0, 1);
            }

            if(hitGroup != HitGroup::Volume) {
                _dirty |= ImGui::SliderFloat("roughness", &material->roughness, 0, 1);
            }

            Medium& medium = _model->mediums[0];
            if(selectedObj != 0){
                if(hitGroup == HitGroup::Volume) {
                    _dirty |= ImGui::SliderFloat("g", &medium.g, -0.99, 0.99);

                    ImGui::Text("Scattering coefficient:");
                    ImGui::Indent(16);
                    _dirty |= ImGui::SliderFloat("r", &medium.scatteringCoeff.r, 0, 100);

                    ImGui::PushID("sc_g");
                    _dirty |= ImGui::SliderFloat("g", &medium.scatteringCoeff.g, 0, 100);
                    ImGui::PopID();

                    _dirty |= ImGui::SliderFloat("b", &medium.scatteringCoeff.b, 0, 100);
                    ImGui::Indent(-16);

                    ImGui::Text("absorption coefficient:");
                    ImGui::Indent(16);

                    ImGui::PushID("ac_r");
                    _dirty |= ImGui::SliderFloat("r", &medium.absorptionCoeff.r, 0, 100);
                    ImGui::PopID();

                    ImGui::PushID("ac_g");
                    _dirty |= ImGui::SliderFloat("g", &medium.absorptionCoeff.g, 0, 100);
                    ImGui::PopID();

                    ImGui::PushID("ac_b");
                    _dirty |= ImGui::SliderFloat("b", &medium.absorptionCoeff.b, 0, 100);
                    ImGui::PopID();
                    ImGui::Indent(-16);

                }else {
                    _dirty |= ImGui::ColorEdit3("albedo", glm::value_ptr(material->diffuse));
                }
            }
            ImGui::Indent(-16);
        }
        ImGui::Separator();

        ImGui::Text("Lighting:");
        ImGui::Indent(16);
        _dirty |= ImGui::SliderFloat("Sky Intensity", &_model->sceneConstants.skyIntensity, 0, 100);
        _dirty |= ImGui::SliderFloat("Color Temp", &_model->colorTemp, 1000, 10640);
        _dirty |= ImGui::Checkbox("Direct Lighting", &_model->directLighting);
        if(_model->directLighting){
            static bool useShadowRay = false;
             recompile |= ImGui::Checkbox("use shadow ray in RIS", &useShadowRay);
            _model->specializationConstants.shadowRayInRis = int(useShadowRay);
            _dirty |= ImGui::Checkbox(fmt::format("Sun: {}", _model->sunDirection()).c_str(), &_model->sun.enabled);
            if(_model->sun.enabled){
                _dirty |= ImGui::SliderFloat("Sun Intensity", &_model->sun.intensity, 0, 100);
                _dirty |= ImGui::SliderFloat("Sun Azimuth", &_model->sun.azimuth, 0, 360);
                _dirty |= ImGui::SliderFloat("Sun Elevation", &_model->sun.elevation, -10, 90);
            }
            _dirty |= ImGui::SliderFloat("env map", &_model->sceneConstants.envMapIntensity, 0, 10000);
            _dirty |= ImGui::Checkbox("Headlight", &_model->headLight.enabled);
            if(_model->headLight.enabled){
                _dirty |= ImGui::SliderFloat("Headlight Intensity", &_model->headLight.intensity, 0, 1000);
            }
        }
        ImGui::Indent(-16);

        ImGui::End();

        imGui->draw(commandBuffer);
    }

    void endFrame(){
        if(_dirty) {
            _dirty = false;

            auto& mask = _model->sceneConstants.mask;
            if(cornell){
                mask = addToMask(mask, eCornellBox);
            }else{
                mask = removeFromMask(mask, eCornellBox);
            }
            if(plane){
                mask = addToMask(mask, ePlane);
            }else{
                mask = removeFromMask(mask, ePlane);
            }
            if(dragon){
                mask = addToMask(mask, eDragon);
            }else{
                mask = removeFromMask(mask, eDragon);
            }
            _model->sceneConstants.currentSample = 0;


            if(_model->directLighting){
                if(_model->sun.enabled) {
                    glm::vec3 position = _model->lights[Model::SunLightId].position;

                    position = glm::angleAxis(glm::radians(_model->sun.elevation), glm::vec3{0, 0, 1}) * position;
                    position = glm::angleAxis(glm::radians(_model->sun.azimuth), glm::vec3{0, 1, 0}) * position;

                    _model->lights[Model::SunLightId].normal = -glm::normalize(position);
                    _model->lights[Model::SunLightId].value = glm::vec3(_model->sun.intensity);
                }

                if(_model->headLight.enabled){
                    _model->lights[1].value = spectrum::blackbodySpectrum({ 6400, _model->headLight.intensity}).front();
                }else{
                    _model->lights[1].value = glm::vec3(0);
                }

                auto radiance = spectrum::blackbodySpectrum( {_model->colorTemp, 1000}).front();
                _model->cornellMaterials[0].emission = radiance;
                _model->lights[0].value = radiance;
                _model->sceneConstants.numLights = _model->numLights;
            }else{
                _model->sceneConstants.numLights = 0;
            }
        }
        if(recompile){
            recompile = false;
            *_model->invalidateSwapChain = true;
            _model->sceneConstants.currentSample = 0;
        }
    }


    uint32_t * getHitGroup(){
        if(selectedObj == ShortBoxId){
            return &(*_model->instances)[0].object.metaData[AS_INSTANCE_SHORT_BOX].hitGroupId;
        }
        if(selectedObj == TallBoxId){
            return &(*_model->instances)[0].object.metaData[AS_INSTANCE_TALL_BOX].hitGroupId;
        }
        if(selectedObj == DragonId){
            return &(*_model->instances)[2].object.metaData[AS_INSTANCE_DRAGON].hitGroupId;
        }
        return &(*_model->instances)[1].object.metaData[AS_INSTANCE_FLOOR].hitGroupId;
    }

    static inline uint32_t addToMask(uint32_t mask, uint32_t entry){
        return mask | entry;
    }

    static inline uint32_t removeFromMask(uint32_t mask, uint32_t entry){
        return mask & ~entry;
    }

private:
    Plugin* imGui{nullptr};
    Model* _model{nullptr};
    bool _dirty{false};
    bool recompile{false};
    bool cornell{true};
    bool plane{true};
    bool dragon{false};
    int selectedObj{1};
//    int hitGroup = 0;

    static constexpr int FloorId = 0;
    static constexpr int ShortBoxId = 1;
    static constexpr int TallBoxId = 2;
    static constexpr int DragonId =3;

    static constexpr int AS_INSTANCE_CORNELL = 0;
    static constexpr int AS_INSTANCE_FLOOR = 0;
    static constexpr int AS_INSTANCE_SHORT_BOX = 6;
    static constexpr int AS_INSTANCE_TALL_BOX = 7;
    static constexpr int AS_INSTANCE_DRAGON = 0;
};