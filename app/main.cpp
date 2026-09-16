#include "vr/system.h"

#include "vr/conversion.h"
#include "vr/overlay.h"
#include "vr/tracked_device.h"
#include "vr/transform.h"

#include "config.h"
#include "log.h"
#include "overlay_bridge.h"

#include "window_manager.h"

#include <array>

#include <QGuiApplication>
#include <QDir>
#include <QLockFile>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <fmt/core.h>
#include <glm/trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <openvr.h>

namespace {

// Load a .qml file for an overlay from the deployed qml/ folder.
QUrl QmlUrl(const QString& file) {
    return QUrl::fromLocalFile(QDir(QCoreApplication::applicationDirPath()).filePath("qml/" + file));
}

// Place an overlay as an upright billboard `distance` metres in front of the HMD, facing
// the user. Used when the panel/keyboard is expanded so it appears where you're looking.
void PlaceInFrontOfHmd(QOverlay::VR::Overlay* overlay, float distance, float heightDelta) {
    auto system = vr::VRSystem();
    if (system == nullptr) return;

    std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses{};
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, poses.data(), static_cast<uint32_t>(poses.size()));
    const auto& hmd = poses[vr::k_unTrackedDeviceIndex_Hmd];
    if (!hmd.bPoseIsValid) return;

    const glm::mat4 hmdMat = QOverlay::VR::Conversion::ToGlmMat(hmd.mDeviceToAbsoluteTracking);
    const glm::vec3 hmdPos = glm::vec3(hmdMat[3]);

    // Horizontal forward so the billboard stays upright regardless of head pitch.
    glm::vec3 forward = -glm::vec3(hmdMat[2]);
    forward.y = 0.0f;
    if (glm::length(forward) < 1e-4f) forward = glm::vec3(0.0f, 0.0f, -1.0f);
    forward = glm::normalize(forward);

    const glm::vec3 pos = hmdPos + forward * distance + glm::vec3(0.0f, heightDelta, 0.0f);

    // Panel front (+Z, matching OpenVR's identity-facing convention) points back at the HMD.
    const glm::vec3 z = -forward;                                   // toward the user
    const glm::vec3 x = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), z));
    const glm::vec3 y = glm::normalize(glm::cross(z, x));

    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x, 0.0f);
    m[1] = glm::vec4(y, 0.0f);
    m[2] = glm::vec4(z, 0.0f);
    m[3] = glm::vec4(pos, 1.0f);

    vr::HmdMatrix34_t transform = QOverlay::VR::Conversion::ToHmdMat(m);
    vr::VROverlay()->SetOverlayTransformAbsolute(overlay->Handle(), vr::TrackingUniverseStanding, &transform);
}

}

void at_exit() {
    QOverlay::VR::VRSystem::Shutdown();
}

int main(int argc, char** argv) {
    std::atexit(at_exit);

    // The control panel renders a QML scene via QQuickRenderControl. The Qt Quick scene
    // graph must be pinned to OpenGL before any QQuickWindow exists so the rendered
    // frame is a GL texture we can hand to OpenVR (TextureType_OpenGL).
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QCoreApplication::setApplicationName("QOverlay");
    QCoreApplication::setOrganizationName("QOverlay");
    QGuiApplication app(argc, argv);

    QOverlay::InitLogging();
    LOG_INFO("QOverlay starting (pid {})", QCoreApplication::applicationPid());

    // Single-instance guard: two instances would collide on the same OpenVR overlay keys
    // (CreateOverlay -> KeyInUse), which previously took the whole app down on launch.
    QLockFile instanceLock(QDir::temp().filePath("qoverlay.instance.lock"));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(200)) {
        LOG_ERROR("Another QOverlay instance is already running — exiting.");
        return 0;
    }

    QOverlay::Config::Instance().Load();

    if (!QOverlay::VR::VRSystem::Initialize()) {
        LOG_ERROR("Failed to initialize OpenVR");
        return 1;
    }
    LOG_INFO("OpenVR initialized");

    // Shared bridge (QML `app`) and window manager (QML `windowManager`).
    auto* bridge = new QOverlay::OverlayBridge(&app);
    bridge->setLeftHanded(QOverlay::Config::Instance().LeftHanded());
    LOG_INFO("Creating window manager (enumerating surfaces + thumbnails)...");
    auto* windowManager = new QOverlay::WindowManager(&app);
    LOG_INFO("Window manager ready");

    // ── Wrist launcher: a small overlay on the controller. Click to toggle the panel. ──
    LOG_INFO("Creating launcher overlay...");
    auto* launcher = new QOverlay::VR::Overlay("qoverlay.launcher", "QOverlay", &app);
    if (!launcher->Ok()) {
        LOG_ERROR("Failed to create launcher overlay");
        return 1;
    }
    launcher->SetContextProperty("app", bridge);
    launcher->SetSource(QmlUrl("Launcher.qml"));
    launcher->SetWidth(0.07f);
    launcher->SetVisible(true);
    LOG_INFO("Launcher overlay ready");

    // ── Control panel: full window-manager UI. Hidden until the launcher expands it. ──
    LOG_INFO("Creating control panel overlay...");
    auto* panel = new QOverlay::VR::Overlay("qoverlay.panel", "QOverlay Panel", &app);
    if (!panel->Ok()) {
        LOG_ERROR("Failed to create control panel overlay");
        return 1;
    }
    panel->SetContextProperty("app", bridge);
    panel->SetContextProperty("windowManager", windowManager);
    panel->SetSource(QmlUrl("ControlPanel.qml"));
    panel->SetWidth(0.55f);
    panel->SetVisible(false);
    LOG_INFO("Control panel overlay ready");

    // ── Virtual keyboard: toggled from the panel; injects into the focused overlay. ──
    LOG_INFO("Creating keyboard overlay...");
    auto* keyboard = new QOverlay::VR::Overlay("qoverlay.keyboard", "QOverlay Keyboard", &app);
    if (!keyboard->Ok()) {
        LOG_ERROR("Failed to create keyboard overlay");
        return 1;
    }
    keyboard->SetContextProperty("app", bridge);
    keyboard->SetContextProperty("windowManager", windowManager);
    keyboard->SetSource(QmlUrl("Keyboard.qml"));
    keyboard->SetWidth(0.7f);
    keyboard->SetVisible(false);
    LOG_INFO("Keyboard overlay ready; entering VR loop");

    // Launcher rides the controller; the panel/keyboard float in the world.
    QOverlay::VR::Transform controllerOffset;
    controllerOffset.SetPosition(glm::vec3(0.0f, 0.05f, 0.1f));
    controllerOffset.SetRotation(glm::radians(glm::vec3(-90.0f, 0.0f, 0.0f)));
    bool attachedToController = false;

    // Expand/collapse the panel; place it in front of the user each time it opens.
    // Every callback below is invoked through Qt signal/event delivery — an exception
    // escaping into Qt is unsupported and crashes the app (Qt: "Throwing exceptions from
    // an event handler is not supported"). So each body is wrapped in Guarded, matching
    // the QML-invoked methods. This also logs the thrower's what()+site if one fires.
    QObject::connect(bridge, &QOverlay::OverlayBridge::panelToggleRequested, [&]() { QOverlay::Guarded("panelToggle", [&]() {
        const bool show = !panel->Visible();
        if (show) PlaceInFrontOfHmd(panel, 0.8f, 0.0f);
        panel->SetVisible(show);
        bridge->setPanelVisible(show);
    }); });

    // Toggle the keyboard; place it lower, in front of the user.
    QObject::connect(bridge, &QOverlay::OverlayBridge::keyboardToggleRequested, [&]() { QOverlay::Guarded("keyboardToggle", [&]() {
        const bool show = !keyboard->Visible();
        if (show) {
            windowManager->refreshKeyboardLegends(); // pick up the current OS layout
            PlaceInFrontOfHmd(keyboard, 0.7f, -0.35f);
        }
        keyboard->SetVisible(show);
        bridge->setKeyboardVisible(show);
    }); });

    // Hand switch: move the launcher to the other controller.
    QObject::connect(bridge, &QOverlay::OverlayBridge::switchHandRequested, [&]() { QOverlay::Guarded("switchHand", [&]() {
        auto& cfg = QOverlay::Config::Instance();
        cfg.SetLeftHanded(!cfg.LeftHanded());

        auto targetDevice = cfg.LeftHanded()
            ? QOverlay::VR::TrackedDevice::DeviceType::ControllerLeft
            : QOverlay::VR::TrackedDevice::DeviceType::ControllerRight;
        launcher->MirrorToDevice(targetDevice);

        bridge->setLeftHanded(cfg.LeftHanded());
        bridge->setStatus(cfg.LeftHanded() ? "Left hand" : "Right hand");
        fmt::print("Switched launcher to {} hand\n", cfg.LeftHanded() ? "left" : "right");
    }); });

    QTimer* vrLoopTimer = new QTimer(&app);
    // The VR tick runs the whole raycaster/grab/resize path (PollInput) plus frame
    // submission. It fires from a QTimer signal, so an exception here would unwind through
    // Qt and crash — contain it. A throw is logged (with site) and the loop keeps ticking.
    QObject::connect(vrLoopTimer, &QTimer::timeout, [&]() { QOverlay::Guarded("vrLoop", [&]() {
        QOverlay::VR::VRSystem::Update();

        // Push the latest captured frame to every active capture overlay.
        windowManager->submitFrames();

        if (!attachedToController) {
            auto& cfg = QOverlay::Config::Instance();
            auto deviceType = cfg.LeftHanded()
                ? QOverlay::VR::TrackedDevice::DeviceType::ControllerLeft
                : QOverlay::VR::TrackedDevice::DeviceType::ControllerRight;
            auto idx = QOverlay::VR::TrackedDevice::GetIndex(deviceType);
            if (idx != vr::k_unTrackedDeviceIndexInvalid) {
                launcher->SetTransformRelative(controllerOffset, deviceType);
                attachedToController = true;
                fmt::print("Launcher attached to {} controller\n", cfg.LeftHanded() ? "left" : "right");
            }
        }
    }); });
    vrLoopTimer->start(10);

    return app.exec();
}
