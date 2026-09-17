#include"component.h"

#include <utility>

JSBSimComponent::JSBSimComponent(sen::Duration tickDuration, std::string publishingBus)
    : tickDuration_(tickDuration), publishingBus_(std::move(publishingBus)), dr_(*aircraft_, {})
{
}

sen::kernel::FuncResult JSBSimComponent::load(sen::kernel::LoadApi &&load_api) {
    return Component::load(std::move(load_api));
}

sen::kernel::PassResult JSBSimComponent::init(sen::kernel::InitApi &&api)
{
    source_ = api.getSource(publishingBus_);
    api.getTypes().add(rpr::AircraftBase<>::meta());

    rpr::EntityTypeStruct entityType;
    rpr::EntityIdentifierStruct entityIdentifier;
    rpr::EntityTypeStruct alternateEntityType;
    aircraft_ = std::make_unique<rpr::AircraftBase<>>("jsbsim", entityType, entityIdentifier, alternateEntityType);
    source_->add(aircraft_);

    return Component::init(std::move(api));
}

sen::kernel::FuncResult JSBSimComponent::run(sen::kernel::RunApi &api){
    return api.execLoop(tickDuration_, [this]()
    {
        // TODO: Pass the data in here of the JSBSIM aircraft
        sen::util::GeodeticSituation situation;
        /*
       situation.worldLocation.latitude = data.latitude;
       situation.worldLocation.longitude = data.longitude;
       situation.worldLocation.altitude = data.altitude;
       situation.orientation.phi = data.roll;
       situation.orientation.theta = data.pitch;
       situation.orientation.psi = data.yaw;
       situation.velocityVector = velocity;
        */
       dr_.setSpatial(situation);
    });
}

sen::kernel::FuncResult JSBSimComponent::unload(sen::kernel::UnloadApi &&api) {
    return Component::unload(std::move(api));
}
