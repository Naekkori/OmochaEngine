#include "DxLib.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>
#include "engine/Engine.h"
using namespace std;

void setConsoleTitle(const string& s);

void setConsoleUTF8() {
    system("chcp 65001");
}

int main()
{
    SetOutApplicationLogValidFlag(TRUE);

    setConsoleUTF8();

    setConsoleTitle(OMOCHA_ENGINE_NAME);
    string insideprojectPath = string(BASE_ASSETS) + "temp/project.json";
    string outSideprojectPath = string(BASE_ASSETS) + "project.json";


    string projectPath = "";
    string projectDirectory = "";

    Engine engine;

    if (filesystem::exists(insideprojectPath)) {
        engine.EngineStdOut("Found inside Folder", 0);
        projectPath = insideprojectPath;
        projectDirectory = string(BASE_ASSETS) + "temp";
    }
    else if (filesystem::exists(outSideprojectPath)) {
        engine.EngineStdOut("Found outside Folder", 0);
        projectPath = outSideprojectPath;
        projectDirectory = BASE_ASSETS;
    }
    else {
        projectPath = "";
        projectDirectory = "";
        engine.EngineStdOut("project.json not found in standard locations.", 2);
        engine.showErrorMessageBox("project.json not found!");
        if (!filesystem::exists("assets")) {
            engine.EngineStdOut("Making Assets Folder, Please Adding PlayEntry Assets");
            filesystem::create_directory("assets");
        }
        return 1;
    }

    bool projectDataLoaded = false;
    if (engine.loadProject(projectPath, projectDirectory)) {
        setConsoleTitle(PROJECT_NAME);
        projectDataLoaded = true;
    }
    else {
        engine.showErrorMessageBox("Failed to load project file.");
        engine.EngineStdOut("ERROR: Project data loading failed. Exiting.", 2);
        return 1;
    }

    if (projectDataLoaded) {
        if (!engine.initGE()) {
            engine.EngineStdOut("Engine/Graphic initialization failed", 2);
            engine.showErrorMessageBox("Engine/Graphic initialization failed!");
            return 1;
        }
        else {
            engine.renderLoadingScreen();
            if (engine.loadImages()) {
                engine.renderLoadingScreen();
            }
            engine.renderLoadingScreen();
            engine.EngineStdOut("Entering game loop.", 0);
            while (ProcessMessage() == 0) {
                unsigned int color = GetColor(255, 255, 255);
                engine.processInput();
                engine.drawAllEntities();
                string FPS_STRING = "FPS:" + to_string(static_cast<int>(engine.getFps()));
                DrawStringF(
                    10, 10,
                    FPS_STRING.c_str(),
                    color
                );
                ScreenFlip();
                engine.updateFps();
            }
            engine.EngineStdOut("Game loop ended.", 0);
        }
    }

    engine.terminateGE();

    return 0;
}

void setConsoleTitle(const string& s) {
#ifdef _MSC_VER
    SetConsoleTitleA(s.c_str());
#else
    cout << "\033]0;" << s << "\007";
#endif
}
