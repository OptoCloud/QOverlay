#include "config.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <fmt/core.h>

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
		fmt::print("No config file found at {}, using defaults\n", configPath().toStdString());
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

	fmt::print("Config loaded from {}\n", configPath().toStdString());
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
		fmt::print("Failed to save config to {}\n", configPath().toStdString());
		return false;
	}

	file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
	fmt::print("Config saved to {}\n", configPath().toStdString());
	return true;
}

void QOverlay::Config::SetLeftHanded(bool left) {
	m_leftHanded = left;
	Save();
	emit Changed();
}

void QOverlay::Config::SetClickThreshold(float threshold) {
	m_clickThreshold = qBound(0.05f, threshold, 1.0f);
	Save();
	emit Changed();
}

void QOverlay::Config::SetGrabThreshold(float threshold) {
	m_grabThreshold = qBound(0.05f, threshold, 1.0f);
	Save();
	emit Changed();
}

void QOverlay::Config::SetCursorSmoothing(float amount) {
	m_cursorSmoothing = qBound(0.0f, amount, 0.95f);
	Save();
	emit Changed();
}

void QOverlay::Config::SetDragSmoothing(float amount) {
	m_dragSmoothing = qBound(0.0f, amount, 0.95f);
	Save();
	emit Changed();
}
