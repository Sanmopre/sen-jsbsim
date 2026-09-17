#include <FGFDMExec.h>
#include <initialization/FGInitialCondition.h>
#include <input_output/FGLog.h>
#include <models/FGPropagate.h>
#include <simgear/misc/sg_path.hxx>

#include <SDL3/SDL.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr SDL_JoystickID kNoJoystick = 0;

std::vector<std::string> ListAircraft(const std::string& rootDir)
{
    std::vector<std::string> names;

    const std::filesystem::path aircraftDir = std::filesystem::path(rootDir) / "aircraft";
    if (!std::filesystem::is_directory(aircraftDir)) {
        return names;
    }

    for (const auto& entry : std::filesystem::directory_iterator(aircraftDir)) {
        if (entry.is_directory()) {
            names.push_back(entry.path().filename().string());
        }
    }

    std::sort(names.begin(), names.end());
    return names;
}

struct JoystickInfo {
    SDL_JoystickID id;
    std::string name;
};

std::vector<JoystickInfo> ListJoysticks()
{
    std::vector<JoystickInfo> joysticks;

    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    if (ids) {
        for (int i = 0; i < count; ++i) {
            const char* name = SDL_GetJoystickNameForID(ids[i]);
            joysticks.push_back({ids[i], name ? name : "Unknown joystick"});
        }
        SDL_free(ids);
    }

    return joysticks;
}

// Routes JSBSim's log records into a small in-memory ring buffer instead of
// stdout/stderr. Records below `minLevel` (the config echo, model
// instantiation notices, etc. - all emitted at DEBUG/INFO) are dropped;
// WARN/ERROR/FATAL records are kept so the run screen can surface them
// without corrupting the ftxui display with raw console writes.
class TuiLogger : public JSBSim::FGLogger {
public:
    explicit TuiLogger(JSBSim::LogLevel minLevel) : minLevel_(minLevel) {}

    void SetLevel(JSBSim::LogLevel level) override
    {
        JSBSim::FGLogger::SetLevel(level);
        buffer_.clear();
    }

    void Message(const std::string& message) override { buffer_ += message; }

    void Flush() override
    {
        // LogLevel::STDOUT is JSBSim's highest-valued enumerator (used for
        // unconditional dumps like table printouts), so a plain `>= minLevel_`
        // check lets it through regardless of minLevel_. Only WARN/ERROR/FATAL
        // are ever "important" for this dashboard.
        if (IsImportant(log_level) && !buffer_.empty()) {
            messages_.push_back(LevelName(log_level) + ": " + Sanitize(buffer_));
            if (messages_.size() > kMaxMessages) messages_.pop_front();
        }
        buffer_.clear();
    }

    std::vector<std::string> Snapshot() const
    {
        return std::vector<std::string>(messages_.begin(), messages_.end());
    }

private:
    static constexpr std::size_t kMaxMessages = 8;

    bool IsImportant(JSBSim::LogLevel level) const
    {
        return level >= minLevel_ && level != JSBSim::LogLevel::STDOUT;
    }

    static std::string LevelName(JSBSim::LogLevel level)
    {
        switch (level) {
        case JSBSim::LogLevel::WARN: return "WARN";
        case JSBSim::LogLevel::ERROR: return "ERROR";
        case JSBSim::LogLevel::FATAL: return "FATAL";
        default: return "LOG";
        }
    }

    static std::string Sanitize(std::string text)
    {
        std::replace(text.begin(), text.end(), '\n', ' ');
        const auto first = text.find_first_not_of(" \t");
        const auto last = text.find_last_not_of(" \t");
        if (first == std::string::npos) return "";
        return text.substr(first, last - first + 1);
    }

    JSBSim::LogLevel minLevel_;
    std::string buffer_;
    std::deque<std::string> messages_;
};

struct SelectionResult {
    std::string aircraftName;
    SDL_JoystickID joystickId = kNoJoystick;
};

// Shows a combined selection screen: an aircraft dropdown and a joystick
// dropdown. The joystick list is refreshed live so devices plugged in or
// unplugged while the menu is open show up without restarting the app.
SelectionResult RunSelectionMenu(const std::vector<std::string>& aircraftNames)
{
    using namespace ftxui;

    SelectionResult result;
    bool confirmed = false;

    int selectedAircraft = 0;
    int selectedJoystick = 0;

    std::vector<JoystickInfo> joysticks = ListJoysticks();
    std::vector<std::string> joystickLabels;

    auto refreshJoystickLabels = [&] {
        joystickLabels.clear();
        for (const auto& joystick : joysticks) {
            joystickLabels.push_back(joystick.name);
        }
        if (joystickLabels.empty()) {
            joystickLabels.push_back("No joystick detected");
        }
        if (selectedJoystick >= static_cast<int>(joystickLabels.size())) {
            selectedJoystick = 0;
        }
    };
    refreshJoystickLabels();

    auto screen = ScreenInteractive::TerminalOutput();

    auto aircraftDropdown = Dropdown(&aircraftNames, &selectedAircraft);
    auto joystickDropdown = Dropdown(&joystickLabels, &selectedJoystick);

    auto startButton = Button("Start", [&] {
        confirmed = true;
        screen.Exit();
    });

    auto container = Container::Vertical({aircraftDropdown, joystickDropdown, startButton});

    auto renderer = Renderer(container, [&] {
        return vbox({
                   text("Select an aircraft") | bold,
                   separator(),
                   aircraftDropdown->Render(),
                   separator(),
                   text("Select a joystick") | bold,
                   separator(),
                   joystickDropdown->Render(),
                   separator(),
                   startButton->Render(),
               }) |
               border;
    });

    Loop loop(&screen, renderer);
    while (!loop.HasQuitted()) {
        loop.RunOnce();

        bool joysticksChanged = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_JOYSTICK_ADDED || event.type == SDL_EVENT_JOYSTICK_REMOVED) {
                joysticksChanged = true;
            }
        }

        if (joysticksChanged) {
            joysticks = ListJoysticks();
            refreshJoystickLabels();
            screen.RequestAnimationFrame();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (!confirmed) {
        return {};
    }

    result.aircraftName = aircraftNames[selectedAircraft];
    if (!joysticks.empty()) {
        result.joystickId = joysticks[selectedJoystick].id;
    }
    return result;
}

SDL_Joystick* OpenJoystick(SDL_JoystickID joystickId)
{
    if (joystickId == kNoJoystick) {
        std::cout << "No joystick selected\n";
        return nullptr;
    }

    SDL_Joystick* joystick = SDL_OpenJoystick(joystickId);
    if (!joystick) {
        std::cerr << "Failed to open joystick: " << SDL_GetError() << '\n';
        return nullptr;
    }

    std::cout << "Opened joystick: " << SDL_GetJoystickName(joystick) << '\n';
    return joystick;
}

// Colors cycled across joystick axes so each slider is visually distinct.
const std::array<ftxui::Color, 6> kAxisPalette = {
    ftxui::Color::CyanLight,  ftxui::Color::MagentaLight, ftxui::Color::YellowLight,
    ftxui::Color::GreenLight, ftxui::Color::BlueLight,    ftxui::Color::RedLight,
};

ftxui::Color AxisColor(std::size_t index) { return kAxisPalette[index % kAxisPalette.size()]; }

// Resamples a rolling history buffer down (or up) to exactly `width` points,
// normalized into [0, height) as ftxui's graph() element expects.
std::vector<int> BuildGraphSeries(const std::deque<double>& history, int width, int height)
{
    std::vector<int> output(static_cast<std::size_t>(std::max(width, 0)),
                             static_cast<int>(height / 2));
    if (history.empty() || width <= 0 || height <= 0) return output;

    double minValue = history.front();
    double maxValue = history.front();
    for (double value : history) {
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
    }
    const double range = (maxValue - minValue) > 1e-6 ? (maxValue - minValue) : 1.0;

    const std::size_t sampleCount = history.size();
    for (int x = 0; x < width; ++x) {
        const std::size_t idx =
            sampleCount > 1
                ? static_cast<std::size_t>((static_cast<double>(x) / (width - 1)) *
                                            static_cast<double>(sampleCount - 1))
                : 0;
        const double normalized = (history[idx] - minValue) / range;
        output[static_cast<std::size_t>(x)] = static_cast<int>(normalized * (height - 1));
    }
    return output;
}

// Drives JSBSim through its property tree (there's no fixed "input" API -
// every stock/community aircraft's FCS reads these normalized cmd-norm
// properties by convention). axisIndex/buttonIndex are physical SDL
// indices - use the live numeric readout next to each axis slider in
// RunSimulationScreen to find out which index your controller reports for
// which stick/trigger, then adjust these tables to match.
struct AxisControlMapping {
    int axisIndex;
    std::string property;
    bool bipolar;   // true: raw axis normalizes to [-1, 1]; false: to [0, 1]
    bool invert;
    float deadzone; // raw SDL units (0..32768); ignored when !bipolar
};

const std::vector<AxisControlMapping> kAxisMappings = {
    {0, "fcs/aileron-cmd-norm", true, false, 2000.f},
    {1, "fcs/elevator-cmd-norm", true, true, 2000.f},
    {3, "fcs/rudder-cmd-norm", true, false, 2000.f},
    {2, "fcs/throttle-cmd-norm", false, false, 0.f},
};

struct ButtonControlMapping {
    int buttonIndex;
    std::string property;
    double onValue;
    double offValue;
    bool toggle; // true: flips on each press (e.g. gear); false: tracks held state
};

const std::vector<ButtonControlMapping> kButtonMappings = {
    {0, "gear/gear-cmd-norm", 1.0, 0.0, true},
};

float NormalizeAxis(float raw, bool bipolar, bool invert, float deadzone)
{
    if (invert) raw = -raw;
    if (bipolar) {
        if (std::fabs(raw) < deadzone) return 0.f;
        return std::clamp(raw / 32768.f, -1.f, 1.f);
    }
    return std::clamp((raw + 32768.f) / 65535.f, 0.f, 1.f);
}

void ApplyJoystickControls(JSBSim::FGFDMExec& fdmExec, const std::vector<float>& axisValues,
                            const bool* buttonStates, std::vector<bool>& previousButtonStates)
{
    for (const auto& mapping : kAxisMappings) {
        if (mapping.axisIndex < 0 || static_cast<std::size_t>(mapping.axisIndex) >= axisValues.size()) {
            continue;
        }
        const float normalized = NormalizeAxis(axisValues[static_cast<std::size_t>(mapping.axisIndex)],
                                                mapping.bipolar, mapping.invert, mapping.deadzone);
        fdmExec.SetPropertyValue(mapping.property, normalized);
    }

    for (const auto& mapping : kButtonMappings) {
        if (mapping.buttonIndex < 0 ||
            static_cast<std::size_t>(mapping.buttonIndex) >= previousButtonStates.size()) {
            continue;
        }
        const auto index = static_cast<std::size_t>(mapping.buttonIndex);
        const bool pressed = buttonStates[index];
        const bool wasPressed = previousButtonStates[index];

        if (mapping.toggle) {
            if (pressed && !wasPressed) {
                const double current = fdmExec.GetPropertyValue(mapping.property);
                const double target = (current == mapping.onValue) ? mapping.offValue : mapping.onValue;
                fdmExec.SetPropertyValue(mapping.property, target);
            }
        } else {
            fdmExec.SetPropertyValue(mapping.property, pressed ? mapping.onValue : mapping.offValue);
        }
        previousButtonStates[index] = pressed;
    }
}

// Runs the simulation to completion while showing a live dashboard: joystick
// axes as colored sliders, joystick buttons as checkboxes, a scrolling
// altitude/latitude/longitude plot, and any WARN/ERROR/FATAL messages JSBSim
// logged (via `logger`). Press 'q' to stop early.
void RunSimulationScreen(JSBSim::FGFDMExec& fdmExec, SDL_Joystick* joystick, const TuiLogger& logger)
{
    using namespace ftxui;

    auto propagate = fdmExec.GetPropagate();
    const double dt = fdmExec.GetDeltaT();
    const int steps = static_cast<int>(60.0 / dt); // simulate 60 seconds

    const int numAxes = joystick ? SDL_GetNumJoystickAxes(joystick) : 0;
    const int numButtons = joystick ? SDL_GetNumJoystickButtons(joystick) : 0;

    std::vector<float> axisValues(static_cast<std::size_t>(numAxes), 0.f);
    std::unique_ptr<bool[]> buttonStates(new bool[numButtons > 0 ? numButtons : 1]());
    std::vector<bool> previousButtonStates(static_cast<std::size_t>(numButtons), false);

    std::vector<std::string> axisLabels;
    Components axisSliders;
    for (int i = 0; i < numAxes; ++i) {
        SliderOption<float> option;
        option.value = &axisValues[i];
        option.min = -32768.f;
        option.max = 32767.f;
        option.color_active = AxisColor(static_cast<std::size_t>(i));
        option.color_inactive = AxisColor(static_cast<std::size_t>(i));
        axisSliders.push_back(Slider(option));
        axisLabels.push_back("Axis " + std::to_string(i));
    }

    CheckboxOption buttonStyle;
    buttonStyle.transform = [](const EntryState& state) {
        auto label = text((state.state ? "[x] " : "[ ] ") + state.label);
        return state.state ? label | color(Color::GreenLight) | bold
                            : label | color(Color::GrayDark);
    };

    Components buttonCheckboxes;
    for (int i = 0; i < numButtons; ++i) {
        buttonCheckboxes.push_back(
            Checkbox("Button " + std::to_string(i), &buttonStates[i], buttonStyle));
    }

    Components allControls = axisSliders;
    allControls.insert(allControls.end(), buttonCheckboxes.begin(), buttonCheckboxes.end());
    auto container = Container::Vertical(allControls);

    double simTime = 0.0;
    double altitudeM = 0.0;
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;

    // ~10 seconds of history, resampled to whatever width the graph is drawn at.
    const std::size_t historyCapacity = std::max<std::size_t>(50, static_cast<std::size_t>(10.0 / dt));
    std::deque<double> altitudeHistory;
    std::deque<double> latitudeHistory;
    std::deque<double> longitudeHistory;

    auto pushHistory = [historyCapacity](std::deque<double>& history, double value) {
        history.push_back(value);
        if (history.size() > historyCapacity) history.pop_front();
    };

    auto screen = ScreenInteractive::TerminalOutput();

    auto renderer = Renderer(container, [&] {
        Elements axisRows;
        for (std::size_t i = 0; i < axisSliders.size(); ++i) {
            // The gauge quantizes the -32768..32767 range to terminal columns,
            // so small stick movements may not visibly shift it; the raw
            // number always reflects the live value regardless of resolution.
            axisRows.push_back(hbox({
                text(axisLabels[i]) | color(AxisColor(i)) | size(WIDTH, EQUAL, 8),
                text(std::to_string(static_cast<int>(axisValues[i]))) |
                    color(AxisColor(i)) | size(WIDTH, EQUAL, 7),
                text(" "),
                axisSliders[i]->Render() | flex,
            }));
        }

        Elements buttonRows;
        for (const auto& checkbox : buttonCheckboxes) {
            buttonRows.push_back(checkbox->Render() | size(WIDTH, EQUAL, 14));
        }

        Elements logRows;
        for (const auto& line : logger.Snapshot()) {
            Color lineColor = Color::White;
            if (line.rfind("ERROR", 0) == 0 || line.rfind("FATAL", 0) == 0) {
                lineColor = Color::RedLight;
            } else if (line.rfind("WARN", 0) == 0) {
                lineColor = Color::YellowLight;
            }
            logRows.push_back(text(line) | color(lineColor));
        }

        auto telemetryPlot = [](const std::deque<double>& history, Color plotColor,
                                 const std::string& label, double value) {
            return vbox({
                       text(label + ": " + std::to_string(value)) | color(plotColor) | bold,
                       graph([&history](int w, int h) { return BuildGraphSeries(history, w, h); }) |
                           color(plotColor) | flex,
                   }) |
                   flex;
        };

        return vbox({
                   text("JSBSim running - press 'q' to stop") | bold,
                   separator(),
                   text("t=" + std::to_string(simTime) + "s"),
                   separator(),
                   text("Joystick axes") | bold | color(Color::CyanLight),
                   numAxes > 0 ? vbox(axisRows) : text("(no joystick)"),
                   separator(),
                   text("Joystick buttons") | bold | color(Color::CyanLight),
                   numButtons > 0 ? flexbox(buttonRows) : text("(no joystick)"),
                   separator(),
                   text("Flight telemetry") | bold | color(Color::GreenLight),
                   hbox({
                       telemetryPlot(altitudeHistory, Color::CyanLight, "Altitude (m)", altitudeM),
                       separator(),
                       telemetryPlot(latitudeHistory, Color::GreenLight, "Latitude (deg)", latitudeDeg),
                       separator(),
                       telemetryPlot(longitudeHistory, Color::YellowLight, "Longitude (deg)", longitudeDeg),
                   }) | size(HEIGHT, EQUAL, 8),
                   separator(),
                   text("JSBSim messages") | bold | color(Color::YellowLight),
                   logRows.empty() ? text("(none)") : vbox(logRows),
               }) |
               border;
    });

    bool quit = false;
    auto eventCatcher = CatchEvent(renderer, [&](const Event& event) {
        if (event == Event::Character('q')) {
            quit = true;
            screen.Exit();
            return true;
        }
        return false;
    });

    // ftxui's RunOnce() only redraws when it processes a task, and tasks
    // normally come from terminal input (keypresses, mouse-motion escapes).
    // Since our display data changes from polling/simulation, not from
    // terminal input, we must explicitly force a redraw on a fixed cadence -
    // otherwise the screen only updates while the mouse is generating motion
    // events over the terminal, and appears frozen the rest of the time.
    using Clock = std::chrono::steady_clock;
    const auto redrawInterval = std::chrono::milliseconds(33); // ~30 FPS UI refresh
    auto lastRedraw = Clock::now() - redrawInterval;

    Loop loop(&screen, eventCatcher);
    for (int i = 0; i < steps && !quit && !loop.HasQuitted(); ++i) {
        SDL_PumpEvents();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Draining the queue refreshes SDL's cached joystick state; hot
            // plug/unplug events don't need special handling here.
        }

        if (joystick) {
            for (int a = 0; a < numAxes; ++a) {
                axisValues[a] = static_cast<float>(SDL_GetJoystickAxis(joystick, a));
            }
            for (int b = 0; b < numButtons; ++b) {
                buttonStates[b] = SDL_GetJoystickButton(joystick, b);
            }
            ApplyJoystickControls(fdmExec, axisValues, buttonStates.get(), previousButtonStates);
        }

        fdmExec.Run();

        simTime = fdmExec.GetSimTime();
        altitudeM = propagate->GetAltitudeASLmeters();
        latitudeDeg = propagate->GetLatitudeDeg();
        longitudeDeg = propagate->GetLongitudeDeg();

        pushHistory(altitudeHistory, altitudeM);
        pushHistory(latitudeHistory, latitudeDeg);
        pushHistory(longitudeHistory, longitudeDeg);

        const auto now = Clock::now();
        if (now - lastRedraw >= redrawInterval) {
            lastRedraw = now;
            screen.PostEvent(Event::Custom); // forces the next RunOnce() to actually redraw
        }
        loop.RunOnce(); // still called every tick so the 'q' quit key stays responsive

        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(dt * 1000.0)));
    }
}

} // namespace

int main(int argc, char** argv)
{
    const std::string rootDir = argc > 1 ? argv[1] : "aircraft_data";

    if (!SDL_Init(SDL_INIT_JOYSTICK)) {
        std::cerr << "Failed to init SDL joystick subsystem: " << SDL_GetError() << '\n';
        return 1;
    }

    std::string aircraftName;
    SDL_JoystickID joystickId = kNoJoystick;

    if (argc > 2) {
        aircraftName = argv[2];
        const std::vector<JoystickInfo> joysticks = ListJoysticks();
        if (!joysticks.empty()) {
            joystickId = joysticks.front().id;
        }
    } else {
        const std::vector<std::string> aircraftNames = ListAircraft(rootDir);
        if (aircraftNames.empty()) {
            std::cerr << "No aircraft found under '" << rootDir << "/aircraft'\n";
            SDL_Quit();
            return 1;
        }

        SelectionResult selection = RunSelectionMenu(aircraftNames);
        if (selection.aircraftName.empty()) {
            std::cerr << "No aircraft selected\n";
            SDL_Quit();
            return 1;
        }

        aircraftName = selection.aircraftName;
        joystickId = selection.joystickId;
    }

    SDL_Joystick* joystick = OpenJoystick(joystickId);

    auto tuiLogger = std::make_shared<TuiLogger>(JSBSim::LogLevel::WARN);
    JSBSim::SetLogger(tuiLogger);

    auto fdmExec = std::make_unique<JSBSim::FGFDMExec>();

    fdmExec->SetRootDir(SGPath(rootDir));
    fdmExec->SetAircraftPath(SGPath("aircraft"));
    fdmExec->SetEnginePath(SGPath("engine"));
    fdmExec->SetSystemsPath(SGPath("systems"));

    if (!fdmExec->LoadModel(aircraftName)) {
        std::cerr << "Failed to load aircraft model '" << aircraftName << "' from '"
                   << rootDir << "'\n";
        if (joystick) SDL_CloseJoystick(joystick);
        SDL_Quit();
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
        if (joystick) SDL_CloseJoystick(joystick);
        SDL_Quit();
        return 1;
    }

    RunSimulationScreen(*fdmExec, joystick, *tuiLogger);

    if (joystick) SDL_CloseJoystick(joystick);
    SDL_Quit();

    return 0;
}
