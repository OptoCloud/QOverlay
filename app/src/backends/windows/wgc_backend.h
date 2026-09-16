#pragma once

#include "capture/capture_backend.h"
#include "backends/windows/surface_enum.h"

#include <vector>

namespace QOverlay::Capture::Win {

// Windows.Graphics.Capture backend: enumerates monitors + windows, grabs GDI thumbnails,
// and creates WGC-backed sources. Caches the last enumeration so ids resolve to native
// handles for thumbnails and source creation.
class WgcBackend : public ICaptureBackend {
	Q_OBJECT
public:
	using ICaptureBackend::ICaptureBackend;

	QString name() const override { return QStringLiteral("Windows.Graphics.Capture"); }
	QList<CaptureSurface> enumerateSurfaces() override;
	QImage grabThumbnail(const QString& surfaceId, int maxDim) override;
	ICaptureSource* createSource(const QString& surfaceId, QObject* parent) override;

private:
	const CaptureTarget* find(const QString& id) const;

	std::vector<CaptureTarget> m_targets; // most recent enumeration
};

}
