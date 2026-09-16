#include "backends/windows/wgc_backend.h"

#include "capture/backend_factory.h"
#include "backends/windows/surface_thumbnail.h"
#include "backends/windows/wgc_source.h"
#include "input/desktop_input.h"

#include <array>
#include <vector>

#include <QVariantMap>

#include <fmt/core.h>

using QOverlay::Capture::Win::WgcBackend;

QList<QOverlay::Capture::CaptureSurface> WgcBackend::enumerateSurfaces() {
	m_targets = EnumerateTargets();

	QList<CaptureSurface> surfaces;
	surfaces.reserve(static_cast<int>(m_targets.size()));
	for (const auto& target : m_targets) {
		surfaces.append(target.toSurface());
	}
	return surfaces;
}

const QOverlay::Capture::Win::CaptureTarget* WgcBackend::find(const QString& id) const {
	for (const auto& target : m_targets) {
		if (target.id == id) return &target;
	}
	return nullptr;
}

QImage WgcBackend::grabThumbnail(const QString& surfaceId, int maxDim) {
	const CaptureTarget* target = find(surfaceId);
	if (target == nullptr) return {};
	return GrabThumbnail(*target, maxDim);
}

QImage WgcBackend::grabIcon(const QString& surfaceId, int maxDim) {
	const CaptureTarget* target = find(surfaceId);
	if (target == nullptr) return {};
	return GrabIcon(*target, maxDim);
}

QOverlay::Capture::ICaptureSource* WgcBackend::createSource(const QString& surfaceId, QObject* parent) {
	const CaptureTarget* target = find(surfaceId);
	if (target == nullptr) {
		fmt::print("WgcBackend: createSource: unknown surface id {}\n", surfaceId.toStdString());
		return nullptr;
	}

	auto* source = new WgcSource(*target, parent);
	if (!source->start()) {
		delete source;
		return nullptr;
	}
	return source;
}

// Factory entry point for CMake backend selection (QOVERLAY_BACKEND=windows).
QOverlay::Capture::ICaptureBackend* QOverlay::Capture::createCaptureBackend(QObject* parent) {
	return new Win::WgcBackend(parent);
}

// currentKeyboardLegends() lives in keyboard_layout_win.cpp so the keyboard tester can reuse it.

void QOverlay::Capture::injectSystemKey(Qt::Key key) {
	// SendInput (global) — the shell opens Start from global input, so this must NOT be posted
	// to a captured window.
	Input::KeyEvent(key, true);
	Input::KeyEvent(key, false);
}
