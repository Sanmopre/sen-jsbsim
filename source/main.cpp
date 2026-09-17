#include <FGFDMExec.h>
#include <initialization/FGInitialCondition.h>
#include <models/FGPropagate.h>
#include <simgear/misc/sg_path.hxx>

#include <iostream>
#include <memory>

int main(int argc, char** argv)
{
    const std::string rootDir = argc > 1 ? argv[1] : "aircraft_data";
    const std::string aircraftName = argc > 2 ? argv[2] : "c172x";

    auto fdmExec = std::make_unique<JSBSim::FGFDMExec>();

    fdmExec->SetRootDir(SGPath(rootDir));
    fdmExec->SetAircraftPath(SGPath("aircraft"));
    fdmExec->SetEnginePath(SGPath("engine"));
    fdmExec->SetSystemsPath(SGPath("systems"));

    if (!fdmExec->LoadModel(aircraftName)) {
        std::cerr << "Failed to load aircraft model '" << aircraftName << "' from '"
                   << rootDir << "'\n";
        return 1;
    }

    auto ic = fdmExec->GetIC();
    ic->SetAltitudeAGLFtIC(6000.0);
    ic->SetVcalibratedKtsIC(120.0);
    ic->SetLatitudeDegIC(0.0);
    ic->SetLongitudeDegIC(0.0);
    ic->SetPsiDegIC(0.0);

    if (!fdmExec->RunIC()) {
        std::cerr << "Failed to run initial conditions\n";
        return 1;
    }

    auto propagate = fdmExec->GetPropagate();
    const double dt = fdmExec->GetDeltaT();
    const int steps = static_cast<int>(60.0 / dt); // simulate 60 seconds

    for (int i = 0; i < steps; ++i) {
        fdmExec->Run();

        if (i % static_cast<int>(1.0 / dt) == 0) { // print once per sim-second
            std::cout << "t=" << fdmExec->GetSimTime()
                      << "s  alt=" << propagate->GetAltitudeASLmeters() << "m"
                      << "  lat=" << propagate->GetLatitudeDeg()
                      << "  lon=" << propagate->GetLongitudeDeg() << '\n';
        }
    }

    return 0;
}
