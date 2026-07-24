#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "rushgrid/simulation/simulation.hpp"
#include "rushgrid/telemetry/statistics.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int initialWidth = 960;
constexpr int initialHeight = 540;
constexpr char windowClassName[] = "RushGridWindowClass";

struct Pixel {
    std::uint8_t blue{};
    std::uint8_t green{};
    std::uint8_t red{};
    std::uint8_t reserved{};
};

static_assert(sizeof(Pixel) == 4);

struct BackBuffer {
    HDC deviceContext{};
    HBITMAP bitmap{};
    HGDIOBJ previousBitmap{};
    Pixel* pixels{};
    int width{};
    int height{};

    void resize(const HDC reference, const int newWidth, const int newHeight) {
        if (newWidth <= 0 || newHeight <= 0 ||
            (newWidth == width && newHeight == height)) {
            return;
        }
        release();

        BITMAPINFO information{};
        information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        information.bmiHeader.biWidth = newWidth;
        information.bmiHeader.biHeight = -newHeight;
        information.bmiHeader.biPlanes = 1;
        information.bmiHeader.biBitCount = 32;
        information.bmiHeader.biCompression = BI_RGB;

        void* bits{};
        bitmap = CreateDIBSection(
            reference,
            &information,
            DIB_RGB_COLORS,
            &bits,
            nullptr,
            0);
        if (bitmap == nullptr || bits == nullptr) {
            throw std::runtime_error("CreateDIBSection failed");
        }
        deviceContext = CreateCompatibleDC(reference);
        if (deviceContext == nullptr) {
            DeleteObject(bitmap);
            bitmap = nullptr;
            throw std::runtime_error("CreateCompatibleDC failed");
        }
        previousBitmap = SelectObject(deviceContext, bitmap);
        pixels = static_cast<Pixel*>(bits);
        width = newWidth;
        height = newHeight;
    }

    void release() noexcept {
        if (deviceContext != nullptr) {
            if (previousBitmap != nullptr) {
                SelectObject(deviceContext, previousBitmap);
            }
            DeleteDC(deviceContext);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        deviceContext = nullptr;
        bitmap = nullptr;
        previousBitmap = nullptr;
        pixels = nullptr;
        width = 0;
        height = 0;
    }

    ~BackBuffer() { release(); }
};

struct ViewerState {
    rushgrid::SimulationConfig config{};
    std::unique_ptr<rushgrid::Simulation> simulation;
    rushgrid::SampleWindow samples{600};
    rushgrid::TickMetrics lastMetrics{};
    BackBuffer backBuffer;
    std::vector<std::uint32_t> occupancy;
    bool running{true};
    bool paused{};
    bool showGrid{};
    bool showHeatmap{true};
    bool showFlow{};
    bool gateOpen[2]{true, true};
    std::uint64_t autoExitTicks{};

    void reset() {
        simulation = std::make_unique<rushgrid::Simulation>(config);
        samples = rushgrid::SampleWindow(600);
        lastMetrics = {};
        gateOpen[0] = true;
        gateOpen[1] = true;
    }
};

[[nodiscard]] Pixel color(
    const std::uint8_t red,
    const std::uint8_t green,
    const std::uint8_t blue) noexcept {
    return {blue, green, red, 0};
}

void put_pixel(
    BackBuffer& buffer,
    const int x,
    const int y,
    const Pixel value) noexcept {
    if (x < 0 || y < 0 || x >= buffer.width || y >= buffer.height) {
        return;
    }
    buffer.pixels[static_cast<std::size_t>(y * buffer.width + x)] = value;
}

void fill_rectangle(
    BackBuffer& buffer,
    const int left,
    const int top,
    const int right,
    const int bottom,
    const Pixel value) noexcept {
    const int clippedLeft = std::clamp(left, 0, buffer.width);
    const int clippedTop = std::clamp(top, 0, buffer.height);
    const int clippedRight = std::clamp(right, 0, buffer.width);
    const int clippedBottom = std::clamp(bottom, 0, buffer.height);
    for (int y = clippedTop; y < clippedBottom; ++y) {
        Pixel* row = buffer.pixels + static_cast<std::size_t>(y * buffer.width);
        std::fill(row + clippedLeft, row + clippedRight, value);
    }
}

void draw_line(
    BackBuffer& buffer,
    int x0,
    int y0,
    const int x1,
    const int y1,
    const Pixel value) noexcept {
    const int deltaX = std::abs(x1 - x0);
    const int stepX = x0 < x1 ? 1 : -1;
    const int deltaY = -std::abs(y1 - y0);
    const int stepY = y0 < y1 ? 1 : -1;
    int error = deltaX + deltaY;
    while (true) {
        put_pixel(buffer, x0, y0, value);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubled = 2 * error;
        if (doubled >= deltaY) {
            error += deltaY;
            x0 += stepX;
        }
        if (doubled <= deltaX) {
            error += deltaX;
            y0 += stepY;
        }
    }
}

void draw_world(ViewerState& state) {
    BackBuffer& buffer = state.backBuffer;
    const rushgrid::NavigationGrid& navigation =
        state.simulation->navigation_grid();
    const rushgrid::RenderSnapshot& snapshot =
        state.simulation->render_snapshot();
    const float scaleX =
        static_cast<float>(buffer.width) / static_cast<float>(navigation.width());
    const float scaleY =
        static_cast<float>(buffer.height) / static_cast<float>(navigation.height());

    fill_rectangle(buffer, 0, 0, buffer.width, buffer.height, color(15, 23, 32));

    state.occupancy.assign(navigation.cell_count(), 0U);
    for (std::size_t index = 0; index < snapshot.size(); ++index) {
        const rushgrid::CellId cell = navigation.cell_at({
            snapshot.positionX[index],
            snapshot.positionY[index]});
        if (cell < state.occupancy.size()) {
            ++state.occupancy[cell];
        }
    }

    for (std::uint32_t y = 0; y < navigation.height(); ++y) {
        for (std::uint32_t x = 0; x < navigation.width(); ++x) {
            const rushgrid::CellId cell = navigation.cell_at(
                static_cast<int>(x),
                static_cast<int>(y));
            const int left = static_cast<int>(x * scaleX);
            const int top = static_cast<int>(y * scaleY);
            const int right = static_cast<int>((x + 1) * scaleX) + 1;
            const int bottom = static_cast<int>((y + 1) * scaleY) + 1;
            if (!navigation.is_walkable(cell)) {
                fill_rectangle(
                    buffer,
                    left,
                    top,
                    right,
                    bottom,
                    color(65, 78, 92));
            } else if (state.showHeatmap && state.occupancy[cell] > 0) {
                const std::uint32_t density =
                    std::min<std::uint32_t>(state.occupancy[cell], 80U);
                fill_rectangle(
                    buffer,
                    left,
                    top,
                    right,
                    bottom,
                    color(
                        static_cast<std::uint8_t>(20 + density * 2),
                        static_cast<std::uint8_t>(28 + density),
                        38));
            }
        }
    }

    if (state.showGrid) {
        const Pixel gridColor = color(42, 64, 83);
        for (int x = 0; x <= static_cast<int>(navigation.width()); x += 2) {
            const int pixelX = static_cast<int>(x * scaleX);
            draw_line(buffer, pixelX, 0, pixelX, buffer.height - 1, gridColor);
        }
        for (int y = 0; y <= static_cast<int>(navigation.height()); y += 2) {
            const int pixelY = static_cast<int>(y * scaleY);
            draw_line(buffer, 0, pixelY, buffer.width - 1, pixelY, gridColor);
        }
    }

    if (state.showFlow && !state.simulation->flow_fields().empty()) {
        const rushgrid::FlowField& field = state.simulation->flow_fields().front();
        const Pixel flowColor = color(92, 230, 157);
        for (std::uint32_t y = 2; y < navigation.height(); y += 4) {
            for (std::uint32_t x = 2; x < navigation.width(); x += 4) {
                const rushgrid::CellId cell = navigation.cell_at(
                    static_cast<int>(x),
                    static_cast<int>(y));
                if (!navigation.is_walkable(cell)) {
                    continue;
                }
                const rushgrid::Vec2 center = navigation.cell_center(cell);
                const rushgrid::Vec2 direction = field.sample(center, navigation);
                const int startX = static_cast<int>(center.x * scaleX);
                const int startY = static_cast<int>(center.y * scaleY);
                draw_line(
                    buffer,
                    startX,
                    startY,
                    startX + static_cast<int>(direction.x * scaleX * 1.4F),
                    startY + static_cast<int>(direction.y * scaleY * 1.4F),
                    flowColor);
            }
        }
    }

    for (std::size_t index = 0; index < snapshot.size(); ++index) {
        const int x = static_cast<int>(snapshot.positionX[index] * scaleX);
        const int y = static_cast<int>(snapshot.positionY[index] * scaleY);
        const Pixel agentColor = snapshot.goal[index] == 0
            ? color(255, 180, 57)
            : color(62, 200, 255);
        fill_rectangle(buffer, x - 1, y - 1, x + 2, y + 2, agentColor);
    }

    SetBkMode(buffer.deviceContext, TRANSPARENT);
    SetTextColor(buffer.deviceContext, RGB(225, 235, 245));
    char metrics[384]{};
    std::snprintf(
        metrics,
        sizeof(metrics),
        "%s | %zu agents | %zu workers | tick %llu | %.3f ms  "
        "p50 %.3f  p95 %.3f  p99 %.3f | arrivals %llu%s",
        rushgrid::to_string(state.config.implementation).data(),
        state.config.agentCount,
        state.config.workerCount,
        static_cast<unsigned long long>(state.simulation->tick_index()),
        state.lastMetrics.totalMs,
        state.samples.percentile(0.50),
        state.samples.percentile(0.95),
        state.samples.percentile(0.99),
        static_cast<unsigned long long>(state.simulation->total_arrivals()),
        state.paused ? " | PAUSED" : "");
    TextOutA(
        buffer.deviceContext,
        12,
        10,
        metrics,
        static_cast<int>(std::char_traits<char>::length(metrics)));

    const char help[] =
        "Space pause | 1/2 gates | B baseline/optimized | +/- agents | "
        "[/] workers | G grid | H heat | F flow | R reset | Esc quit";
    TextOutA(
        buffer.deviceContext,
        12,
        buffer.height - 24,
        help,
        static_cast<int>(sizeof(help) - 1));
}

void reset_with_change(ViewerState& state) {
    const std::size_t maximumAgents =
        state.config.implementation == rushgrid::Implementation::baseline
        ? 5'000
        : 50'000;
    state.config.agentCount = std::clamp<std::size_t>(
        state.config.agentCount,
        1'000,
        maximumAgents);
    if (state.config.implementation == rushgrid::Implementation::baseline) {
        state.config.workerCount = 0;
    }
    state.config.workerCount = std::min<std::size_t>(
        state.config.workerCount,
        63);
    state.reset();
}

void handle_key(ViewerState& state, const WPARAM key) {
    switch (key) {
    case VK_ESCAPE:
        state.running = false;
        break;
    case VK_SPACE:
        state.paused = !state.paused;
        break;
    case '1':
    case '2': {
        const std::size_t gate = key == '1' ? 0U : 1U;
        state.gateOpen[gate] = !state.gateOpen[gate];
        state.simulation->set_gate_open(gate, state.gateOpen[gate]);
        break;
    }
    case 'B':
        state.config.implementation =
            state.config.implementation == rushgrid::Implementation::optimized
            ? rushgrid::Implementation::baseline
            : rushgrid::Implementation::optimized;
        if (state.config.implementation == rushgrid::Implementation::baseline) {
            state.config.agentCount = std::min<std::size_t>(
                state.config.agentCount,
                5'000);
            state.config.workerCount = 0;
        } else if (state.config.workerCount == 0) {
            const std::size_t hardwareThreads =
                std::thread::hardware_concurrency();
            state.config.workerCount =
                hardwareThreads > 1 ? hardwareThreads - 1 : 0;
        }
        reset_with_change(state);
        break;
    case VK_OEM_PLUS:
    case VK_ADD:
        state.config.agentCount = std::min<std::size_t>(
            50'000,
            state.config.agentCount * 2);
        reset_with_change(state);
        break;
    case VK_OEM_MINUS:
    case VK_SUBTRACT:
        state.config.agentCount = std::max<std::size_t>(
            1'000,
            state.config.agentCount / 2);
        reset_with_change(state);
        break;
    case VK_OEM_4:
        if (state.config.workerCount > 0) {
            --state.config.workerCount;
            reset_with_change(state);
        }
        break;
    case VK_OEM_6:
        ++state.config.workerCount;
        reset_with_change(state);
        break;
    case 'G':
        state.showGrid = !state.showGrid;
        break;
    case 'H':
        state.showHeatmap = !state.showHeatmap;
        break;
    case 'F':
        state.showFlow = !state.showFlow;
        break;
    case 'R':
        state.reset();
        break;
    default:
        break;
    }
}

LRESULT CALLBACK window_procedure(
    HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam) {
    ViewerState* state = reinterpret_cast<ViewerState*>(
        GetWindowLongPtrA(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
        state = static_cast<ViewerState*>(create->lpCreateParams);
        SetWindowLongPtrA(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_KEYDOWN:
        if (state != nullptr && (lParam & (1LL << 30)) == 0) {
            handle_key(*state, wParam);
        }
        return 0;
    case WM_CLOSE:
        if (state != nullptr) {
            state->running = false;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }
    default:
        return DefWindowProcA(window, message, wParam, lParam);
    }
}

[[nodiscard]] std::uint64_t parse_auto_exit(const std::string& commandLine) {
    constexpr char prefix[] = "--auto-exit-ticks=";
    const std::size_t position = commandLine.find(prefix);
    if (position == std::string::npos) {
        return 0;
    }
    const std::size_t valueBegin = position + sizeof(prefix) - 1;
    const std::size_t valueEnd = commandLine.find(' ', valueBegin);
    return std::stoull(commandLine.substr(valueBegin, valueEnd - valueBegin));
}

} // namespace

int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE,
    LPSTR commandLine,
    int showCommand) {
    try {
        const std::size_t hardwareThreads = std::thread::hardware_concurrency();
        ViewerState state;
        state.config = {
            10'000,
            hardwareThreads > 1 ? hardwareThreads - 1 : 0,
            1337,
            rushgrid::Implementation::optimized};
        const std::string commandLineText =
            commandLine != nullptr ? commandLine : "";
        state.autoExitTicks = parse_auto_exit(commandLineText);
        state.reset();

        WNDCLASSA windowClass{};
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = window_procedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.hbrBackground = static_cast<HBRUSH>(
            GetStockObject(BLACK_BRUSH));
        windowClass.lpszClassName = windowClassName;
        if (RegisterClassA(&windowClass) == 0) {
            throw std::runtime_error("RegisterClass failed");
        }

        RECT windowRectangle{0, 0, initialWidth, initialHeight};
        AdjustWindowRect(&windowRectangle, WS_OVERLAPPEDWINDOW, FALSE);
        HWND window = CreateWindowExA(
            0,
            windowClassName,
            "RushGrid | Large-Entity Transit Simulation",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRectangle.right - windowRectangle.left,
            windowRectangle.bottom - windowRectangle.top,
            nullptr,
            nullptr,
            instance,
            &state);
        if (window == nullptr) {
            throw std::runtime_error("CreateWindow failed");
        }

        const bool hidden =
            commandLineText.find("--hidden") != std::string::npos;
        ShowWindow(window, hidden ? SW_HIDE : showCommand);
        UpdateWindow(window);

        HDC windowDeviceContext = GetDC(window);
        state.backBuffer.resize(windowDeviceContext, initialWidth, initialHeight);
        ReleaseDC(window, windowDeviceContext);

        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();
        auto lastRender = previous;
        double accumulator{};
        constexpr double fixedStep = 1.0 / 60.0;

        while (state.running) {
            MSG message{};
            while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    state.running = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageA(&message);
            }

            const auto now = Clock::now();
            const double elapsed = std::min(
                0.25,
                std::chrono::duration<double>(now - previous).count());
            previous = now;
            if (!state.paused) {
                accumulator += elapsed;
            }

            int catchUpTicks{};
            while (!state.paused && accumulator >= fixedStep && catchUpTicks < 4) {
                state.lastMetrics = state.simulation->tick();
                state.samples.push(state.lastMetrics.totalMs);
                accumulator -= fixedStep;
                ++catchUpTicks;
                if (state.autoExitTicks > 0 &&
                    state.simulation->tick_index() >= state.autoExitTicks) {
                    state.running = false;
                    break;
                }
            }
            if (catchUpTicks == 4) {
                accumulator = 0.0;
            }

            if (now - lastRender >= std::chrono::milliseconds(16)) {
                if (!hidden) {
                    RECT clientRectangle{};
                    GetClientRect(window, &clientRectangle);
                    HDC deviceContext = GetDC(window);
                    state.backBuffer.resize(
                        deviceContext,
                        clientRectangle.right - clientRectangle.left,
                        clientRectangle.bottom - clientRectangle.top);
                    ReleaseDC(window, deviceContext);
                }
                draw_world(state);
                if (!hidden) {
                    HDC deviceContext = GetDC(window);
                    BitBlt(
                        deviceContext,
                        0,
                        0,
                        state.backBuffer.width,
                        state.backBuffer.height,
                        state.backBuffer.deviceContext,
                        0,
                        0,
                        SRCCOPY);
                    ReleaseDC(window, deviceContext);
                }
                lastRender = now;
            }
            Sleep(1);
        }

        if (IsWindow(window)) {
            DestroyWindow(window);
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "RushGrid error", MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
}
