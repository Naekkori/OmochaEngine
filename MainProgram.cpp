#include "DxLib.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>   
#include <stdexcept> 
#include "engine/Engine.h"
#include "resource.h" 

using namespace std;


void setConsoleTitle(const string &s);
void setConsoleUTF8();



int main(int argc, char *argv[])
{
    SetOutApplicationLogValidFlag(TRUE); 

    
    int targetFpsFromArg = -1; 
    bool setVsync = true;      
    for (int i = 1; i < argc; ++i)
    {
        string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            string engineString = string(OMOCHA_ENGINE_NAME) + " v" + 
            string(OMOCHA_ENGINE_VERSION) + " " + string(OMOCHA_DEVELOPER_NAME) + "\n";
            engineString +="See Project page "+string(OMOCHA_ENGINE_GITHUB)+"\n";
            engineString +="**************************\n";
            engineString +="*         도움말         *\n";
            engineString +="**************************\n";
            // Print detailed usage information
            std:printf(engineString.c_str());
            std::printf("사용법: %s [옵션]\n\n", argv[0]);
            std::printf("옵션:\n");
            std::printf("  --setfps <value>   frames per second (FPS) 를 설정합니다.\n");
            std::printf("                     기본값은 엔진 내부세팅 60fps 입니다.\n");
            std::printf("  --setVsync <0|1>   수직동기화 를 설정합니다 0 은 비활성 1 은 활성입니다.\n");
            std::printf("  -h, --help         도움말 을 출력하고 엔진을 종료합니다.\n\n");
            std::printf("예제:\n");
            std::printf("  %s --setfps 120 --setVsync 0\n", argv[0]);
            return 0; // Exit after displaying help
        }

        if (arg == "--setfps" && i + 1 < argc)
        {
            try
            {
                targetFpsFromArg = stoi(argv[i + 1]);
                if (targetFpsFromArg <= 0)
                {
                    cerr << "Warning: Invalid FPS value provided for --setfps. Using default." << endl;
                    targetFpsFromArg = -1;
                }
                i++; // Increment i because we consumed the next argument (the value)
            }
            catch (const std::invalid_argument &e)
            {
                cerr << "Warning: Invalid argument for --setfps. Expected a number." << endl;
                targetFpsFromArg = -1; // Reset to default on error
            }
            catch (const std::out_of_range &e)
            {
                cerr << "Warning: FPS value for --setfps out of range." << endl;
                targetFpsFromArg = -1; // Reset to default on error
            }
        }
        else if (arg == "--setVsync" && i + 1 < argc) // Use else if to avoid re-checking if it was --setfps
        {
            try
            {
                string argValue = argv[i + 1];
                int vsyncValue = stoi(argValue);
                if (vsyncValue == 0) {
                    setVsync = false;
                } else if (vsyncValue == 1) {
                    setVsync = true;
                } else {
                     cerr << "Warning: Invalid value for --setVsync. Expected 0 or 1. Using default (1)." << endl;
                     setVsync = true; // Default to true on invalid value
                }
                i++; // Increment i because we consumed the next argument (the value)
            }
            catch (const std::invalid_argument &e)
            {
                std::cerr << "Warning: Invalid argument for --setVsync. Expected a number (0 or 1). Using default (1)." << std::endl;
                setVsync = true; // Default to true on error
            }
             catch (const std::out_of_range &e)
            {
                std::cerr << "Warning: Value for --setVsync out of range. Using default (1)." << std::endl;
                setVsync = true; // Default to true on error
            }
        }
        
    }
    

    setConsoleUTF8(); 

    setConsoleTitle(OMOCHA_ENGINE_NAME); 

    
    string insideprojectPath = string(BASE_ASSETS) + "temp/project.json";
    string outSideprojectPath = string(BASE_ASSETS) + "project.json";
    

    string projectPath = "";

    Engine engine; 

    
    if (filesystem::exists(insideprojectPath))
    {
        engine.EngineStdOut("Found project.json inside 'assets/temp/' folder.", 0);
        projectPath = insideprojectPath;
    }
    else if (filesystem::exists(outSideprojectPath))
    {
        engine.EngineStdOut("Found project.json inside 'assets/' folder.", 0);
        projectPath = outSideprojectPath;
    }
    else
    {
        projectPath = ""; 
        engine.EngineStdOut("project.json not found in standard locations ('assets/temp/' or 'assets/').", 2);
        engine.showMessageBox("project.json not found!\nPlease place the project file in the 'assets' folder.",engine.msgBoxIconType.ICON_ERROR);
        
        if (!filesystem::exists("assets"))
        {
            engine.EngineStdOut("Creating 'assets' folder. Please add project assets there.", 1);
            try
            {
                filesystem::create_directory("assets");
            }
            catch (const std::filesystem::filesystem_error &e)
            {
                engine.EngineStdOut("Failed to create 'assets' directory: " + string(e.what()), 2);
                engine.showMessageBox("Failed to create 'assets' directory.",engine.msgBoxIconType.ICON_ERROR);
            }
        }
        return 1; 
    }

    
    bool projectDataLoaded = false;
    if (engine.loadProject(projectPath))
    {
        setConsoleTitle(PROJECT_NAME + " - " + OMOCHA_ENGINE_NAME); 
        projectDataLoaded = true;
        engine.EngineStdOut("Project loaded successfully: " + PROJECT_NAME, 0);
    }
    else
    {
        
        engine.EngineStdOut("Project data loading failed. Exiting.", 2);
        return 1; 
    }

    
    if (targetFpsFromArg > 0)
    {
        engine.EngineStdOut("Setting target FPS from command line argument: " + to_string(targetFpsFromArg), 0);
        engine.setfps(targetFpsFromArg); 
    }

    if (setVsync)
    {
        SetWaitVSyncFlag(TRUE); 
    }
    else
    {
        SetWaitVSyncFlag(FALSE); 
    }
    

    
    if (projectDataLoaded)
    {
        if (!engine.initGE())
        {
            
            engine.EngineStdOut("Engine/Graphic initialization failed. Exiting.", 2);
            return 1; 
        }
        else
        {
            
            SetWindowIconID(IDI_OMOC);
            engine.renderLoadingScreen();

            if (!engine.loadImages())
            {
                engine.EngineStdOut("Image loading process completed with errors.", 1);
            }
            else
            {
                engine.EngineStdOut("Image loading process completed successfully.", 0);
            }
            engine.renderLoadingScreen();

            engine.EngineStdOut("Entering game loop.", 0);


            long long loopStartTime = 0;
            int targetFps = engine.getTargetFps();
            int targetFrameTimeMillis = (targetFps > 0) ? (1000 / targetFps) : (1000 / 60);

            while (ProcessMessage() == 0)
            {                                  
                loopStartTime = GetNowCount();

                engine.processInput();
                engine.drawAllEntities(); 
                engine.drawHUD();         
                ScreenFlip();             
                engine.updateFps();       

                
                long long elapsedTime = GetNowCount() - loopStartTime;                
                int waitTime = targetFrameTimeMillis - static_cast<int>(elapsedTime); 
                if (waitTime > 0)
                {
                    WaitTimer(waitTime); 
                }
                
            }
            

            engine.EngineStdOut("Game loop ended.", 0);
        }
    }

    
    engine.terminateGE();

    return 0; 
}


void setConsoleTitle(const string &s)
{
#ifdef _WIN32 
    SetConsoleTitleA(s.c_str());
#else 
    cout << "\033]0;" << s << "\007";
#endif
}


void setConsoleUTF8()
{
#ifdef _WIN32                   
    system("chcp 65001 > nul"); 
#endif
    
}
