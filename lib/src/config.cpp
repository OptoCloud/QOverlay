#include "config.h"

#include "log.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

QOverlay::Config& QOverlay::Config::Instance() {
	static Config instance;
	return instance;
}

QString QOverlay::Config::configPath() const {
	QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
	QDir().mkpath(dir);
	return dir + "/config.json";
}

bool QOverlay::Config::Load() {
	QFile file(configPath());
	if (!file.open(QIODevice::ReadOnly)) {
		LOG_WARN("No config file found at {}, using defaults", configPath().toStdString());
		return false;
	}

	QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
	if (!doc.isObject()) return false;

	QJsonObject obj = doc.object();
	m_leftHanded = obj.value("leftHanded").toBool(m_leftHanded);
	m_clickThreshold = static_cast<float>(obj.value("clickThreshold").toDouble(m_clickThreshold));
	m_grabThreshold = static_cast<float>(obj.value("grabThreshold").toDouble(m_grabThreshold));
	m_cursorSmoothing = static_cast<float>(obj.value("cursorSmoothing").toDouble(m_cursorSmoothing));
	m_dragSmoothing = static_cast<float>(obj.value("dragSmoothing").toDouble(m_dragSmoothing));

	LOG_INFO("Config loaded from {}", configPath().toStdString());
	return true;
}

bool QOverlay::Config::Save() const {
	QJsonObject obj;
	obj["leftHanded"] = m_leftHanded;
	obj["clickThreshold"] = m_clickThreshold;
	obj["grabThreshold"] = m_grabThreshold;
	obj["cursorSmoothing"] = m_cursorSmoothing;
	obj["dragSmoothing"] = m_dragSmoothing;

	QFile file(configPath());
	if (!file.open(QIODevice::WriteOnly)) {
		LOG_ERROR("Failed to save config to {}", configPath().toStdString());
		return false;
	}

	file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
	LOG_INFO("Config saved to {}", configPath().toStdString());
	return true;
}

void QOverlay::Config::SetLeftHanded(bool left) {
	m_leftHanded = left;
	Save();
	emit Changed();
}

void QOverlay::Config::SetClickThreshold(float threshold) {
	// 0.05 = 5% trigger pull (very sensitive, near-hair-trigger); 1.0 = full pull (nearly
	// impossible to hit in practice, effectively "disabled"). Lower bound keeps a tiny dead
	// zone so analog noise near 0 can't register spurious clicks.
	m_clickThreshold = std::clamp(threshold, 0.05f, 1.0f);
	Save();
	emit Changed();
}

void QOverlay::Config::SetGrabThreshold(float threshold) {
	// Same rationale as SetClickThreshold: 0.05-1.0 spans "barely squeeze" to "full grip".
	m_grabThreshold = std::clamp(threshold, 0.05f, 1.0f);
	Save();
	emit Changed();
}

void QOverlay::Config::SetCursorSmoothing(float amount) {
	// 0.0 = no smoothing (cursor tracks the raw raycast hit instantly); 0.95 = heaviest
	// smoothing we allow. 1.0 is deliberately excluded since an EMA factor of exactly 1.0
	// never converges toward the new sample, effectively freezing the cursor.
	m_cursorSmoothing = std::clamp(amount, 0.0f, 0.95f);
	Save();
	emit Changed();
}

void QOverlay::Config::SetDragSmoothing(float amount) {
	// Same rationale as SetCursorSmoothing.
	m_dragSmoothing = std::clamp(amount, 0.0f, 0.95f);
	Save();
	emit Changed();
}
