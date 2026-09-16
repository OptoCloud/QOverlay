find_package (
    Qt6
    REQUIRED
    COMPONENTS
        Gui
        OpenGL
        Qml
        Quick
        Widgets       # for the keyboard tester tool
        QuickWidgets  # for the keyboard tester tool
)

qt_standard_project_setup()

message ("Found Qt ${Qt6Core_VERSION}")