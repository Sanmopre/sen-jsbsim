
#pragma once

// sen
#include "rpr/rpr-physical_v2.0.xml.h"
#include "sen/kernel/component.h"

// sen_util
#include "sen/util/dr/settable_dead_reckoner.h"

class JSBSimComponent : public sen::kernel::Component {
  public:
    JSBSimComponent(sen::Duration tickDuration, std::string publishingBus); 
    ~JSBSimComponent() override = default;

    sen::kernel::FuncResult load(sen::kernel::LoadApi &&) override;
    sen::kernel::PassResult init(sen::kernel::InitApi &&api) override;
    sen::kernel::FuncResult run(sen::kernel::RunApi &api) override;
    sen::kernel::FuncResult unload(sen::kernel::UnloadApi &&api) override;

  private:
    sen::Duration tickDuration_;
    std::string publishingBus_;

  private:
    std::shared_ptr<sen::ObjectSource> source_;
    std::shared_ptr<rpr::AircraftBase<>> aircraft_;
    sen::util::SettableDeadReckoner<rpr::PhysicalEntityBase<>> dr_;
};
