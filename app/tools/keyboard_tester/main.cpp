// Desktop tester for qml/Keyboard.qml. Loads the real keyboard QML, injects mouse-driven
// stand-ins for the VR pointer + window manager, and echoes what would be typed — so the
// keyboard layout/behaviour can be iterated without entering VR. Windows-only (it reuses the
// Win32 keyboard-layout query).
#include "tester_context.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#ifndef KB_QML_FILE
#define KB_QML_FILE "qml/Keyboard.qml" // fallback: next to the exe
#endif

namespace {

// A QQuickWidget that turns mouse motion/clicks into VR-pointer updates, so hovering a key
// highlights it and clicking activates it — exactly the paths the controller ray drives.
class KeyboardView : public QQuickWidget {
public:
	explicit KeyboardView(TesterPointer* pointer, QWidget* parent = nullptr)
		: QQuickWidget(parent), m_pointer(pointer)
	{
		setMouseTracking(true);                              // hover without a button held
		setResizeMode(QQuickWidget::SizeViewToRootObject);   // 1:1 widget<->item coords
	}

protected:
	void mouseMoveEvent(QMouseEvent* e) override { drive(e->position(), e->buttons() != Qt::NoButton); QQuickWidget::mouseMoveEvent(e); }
	void mousePressEvent(QMouseEvent* e) override { drive(e->position(), true);  QQuickWidget::mousePressEvent(e); }
	void mouseReleaseEvent(QMouseEvent* e) override { drive(e->position(), false); QQuickWidget::mouseReleaseEvent(e); }
	void leaveEvent(QEvent* e) override { m_pointer->set(m_pointer->x(), m_pointer->y(), false, false); QQuickWidget::leaveEvent(e); }

private:
	void drive(const QPointF& p, bool pressed) { m_pointer->set(p.x(), p.y(), true, pressed); }
	TesterPointer* m_pointer;
};

} // namespace

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QCoreApplication::setApplicationName("QOverlay Keyboard Tester");

	auto* pointerLeft = new TesterPointer(&app);
	auto* pointerRight = new TesterPointer(&app); // stays inactive (mouse drives the left one)
	auto* wm = new TesterWindowManager(&app);
	wm->refreshKeyboardLegends();

	// Auto-refresh legends when Windows changes the active layout, matching the VR app.
	auto* lastToken = new quint64(QOverlay::Capture::currentKeyboardLayoutToken());
	auto* layoutTimer = new QTimer(&app);
	QObject::connect(layoutTimer, &QTimer::timeout, wm, [wm, lastToken]() {
		const quint64 token = QOverlay::Capture::currentKeyboardLayoutToken();
		if (token != *lastToken && token != 0) { *lastToken = token; wm->refreshKeyboardLegends(); }
	});
	layoutTimer->start(700);

	auto* window = new QMainWindow;
	window->setWindowTitle("QOverlay Keyboard Tester");

	auto* view = new KeyboardView(pointerLeft);
	view->engine()->rootContext()->setContextProperty("pointerLeft", pointerLeft);
	view->engine()->rootContext()->setContextProperty("pointerRight", pointerRight);
	view->engine()->rootContext()->setContextProperty("windowManager", wm);

	// Load the real Keyboard.qml. KB_QML_FILE points at the source file (baked by CMake) so
	// "Reload QML" reflects edits immediately; falls back to a copy next to the exe.
	QString qmlPath = QString::fromUtf8(KB_QML_FILE);
	if (!QFileInfo::exists(qmlPath))
		qmlPath = QDir(QCoreApplication::applicationDirPath()).filePath("qml/Keyboard.qml");
	const QUrl qmlUrl = QUrl::fromLocalFile(qmlPath);
	view->setSource(qmlUrl);

	// ── Controls row ──
	auto* layoutLabel = new QLabel;
	auto updateLayoutLabel = [wm, layoutLabel]() {
		const bool iso = wm->keyboardLegends().value("iso").toBool();
		layoutLabel->setText(QStringLiteral("OS layout: %1").arg(iso ? "ISO" : "ANSI"));
	};
	updateLayoutLabel();
	QObject::connect(wm, &TesterWindowManager::keyboardLegendsChanged, layoutLabel, updateLayoutLabel);

	auto* refreshBtn = new QPushButton("Refresh layout");
	QObject::connect(refreshBtn, &QPushButton::clicked, wm, [wm]() { wm->refreshKeyboardLegends(); });

	auto* reloadBtn = new QPushButton("Reload QML");
	QObject::connect(reloadBtn, &QPushButton::clicked, view, [view, qmlUrl]() {
		view->engine()->clearComponentCache();
		view->setSource(QUrl());
		view->setSource(qmlUrl);
	});

	auto* clearBtn = new QPushButton("Clear output");
	QObject::connect(clearBtn, &QPushButton::clicked, wm, [wm]() { wm->clear(); });

	auto* controls = new QHBoxLayout;
	controls->addWidget(layoutLabel);
	controls->addStretch();
	controls->addWidget(refreshBtn);
	controls->addWidget(reloadBtn);
	controls->addWidget(clearBtn);

	// ── Typed-output echo ──
	auto* output = new QPlainTextEdit;
	output->setReadOnly(true);
	output->setMaximumHeight(120);
	output->setPlaceholderText("Keys you press on the keyboard are echoed here…");
	QObject::connect(wm, &TesterWindowManager::typedChanged, output, [wm, output]() {
		output->setPlainText(wm->typed());
		output->moveCursor(QTextCursor::End);
	});

	auto* central = new QWidget;
	auto* v = new QVBoxLayout(central);
	v->addLayout(controls);
	v->addWidget(view);
	v->addWidget(new QLabel("Typed output:"));
	v->addWidget(output);
	window->setCentralWidget(central);
	window->show();

	return app.exec();
}
