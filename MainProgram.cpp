#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>   
#include <stdexcept> 
#include <fstream> // For std::ofstream
#include <Windows.h>
#include "engine/Engine.h"
#include "MainProgram.h"
#include "resource.h" 
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
using namespace std;


void SetTitle(const string &s);
void setConsoleUTF8();



int main(int argc, char *argv[])
{  
    Engine engine; 
    MainProgram mainProgram;
    
    int targetFpsFromArg = -1;     
    for (int i = 1; i < argc; ++i)
    {
        string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            string helpTitle = string(OMOCHA_ENGINE_NAME) + " 도움말";
            string helpMessage = 
                string(OMOCHA_ENGINE_NAME) + " v" + string(OMOCHA_ENGINE_VERSION) + " by " + string(OMOCHA_DEVELOPER_NAME) + "\n" +
                "프로젝트 페이지: " + string(OMOCHA_ENGINE_GITHUB) + "\n\n" +
                "사용법: " + string(argv[0]) + " [옵션]\n\n" +
                "옵션:\n" +
                "  --setfps <값>      초당 프레임(FPS)을 설정합니다.\n" +
                "                       (기본값: 엔진 내부 설정, 예: 60)\n" +
                "  --useVk <0|1>      Vulkan 렌더러 사용 여부를 설정합니다.\n" +
                "                       0: 사용 안 함 (기본값), 1: 사용 시도\n" +
                "  --setVsync <0|1>   수직 동기화를 설정합니다.\n" +
                "                       0: 비활성, 1: 활성 (기본값)\n" +
                "  -h, --help         이 도움말을 표시하고 종료합니다.\n\n" +
                "예제:\n" +
                "  OmochaEngine.exe --setfps 120 --setVsync 0"; // 예시 실행 파일 이름
            string tempHelpFilePath = "런타임 도움말.txt";
            try {
                ofstream helpFile(tempHelpFilePath);
                if (helpFile.is_open()) {
                    helpFile << helpMessage;
                    helpFile.close();

                    string command = "notepad.exe " + tempHelpFilePath;
                    system(command.c_str()); 
                } else {
                    // 파일 생성 실패 시 메시지 박스로 대체
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "도움말 파일 오류", "도움말 파일을 생성할 수 없습니다.", NULL);
                }
            } catch (const std::exception& e) {
                // 예외 발생 시 메시지 박스로 대체
                string errorMsg = "도움말 표시 중 오류 발생: ";
                errorMsg += e.what();
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "도움말 오류", errorMsg.c_str(), NULL);
            }
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
            catch (const invalid_argument &e)
            {
                cerr << "Warning: Invalid argument for --setfps. Expected a number." << endl;
                targetFpsFromArg = -1; // Reset to default on error
            }
            catch (const out_of_range &e)
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
                   mainProgram.mainProgramValue.setVsync = false;
                } else if (vsyncValue == 1) {
                    mainProgram.mainProgramValue.setVsync = true;
                } else {
                     cerr << "Warning: Invalid value for --setVsync. Expected 0 or 1. Using default (1)." << endl;
                     mainProgram.mainProgramValue.setVsync = true; // Default to true on invalid value
                }
                i++; // Increment i because we consumed the next argument (the value)
            }
            catch (const invalid_argument &e)
            {
                cerr << "Warning: Invalid argument for --setVsync. Expected a number (0 or 1). Using default (1)." << endl;
                mainProgram.mainProgramValue.setVsync = true; // Default to true on error
            }
             catch (const out_of_range &e)
            {
                cerr << "Warning: Value for --setVsync out of range. Using default (1)." << endl;
                mainProgram.mainProgramValue.setVsync = true; // Default to true on error
            }
        }
        else if (arg == "--useVk" && i + 1 < argc)
        {
            try
            {
                string argValue = argv[i + 1];
                int vkValue = stoi(argValue);
                if (vkValue == 0) {
                    mainProgram.mainProgramValue.useVulkan = false;
                } else if (vkValue == 1) {
                    mainProgram.mainProgramValue.useVulkan = true;
                } else {
                    cerr << "Warning: Invalid value for --useVk. Expected 0 or 1. Using default (0)." << endl;
                    mainProgram.mainProgramValue.useVulkan = false; // Default to false
                }
                i++; 
            }
            catch (const invalid_argument &e)
            {
                cerr << "Warning: Invalid argument for --useVk. Expected a number (0 or 1). Using default (0)." << endl;
                mainProgram.mainProgramValue.useVulkan = false; // Default to false
            }
            catch (const out_of_range &e) {
                cerr << "Warning: Value for --useVk out of range. Using default (0)." << endl;
                mainProgram.mainProgramValue.useVulkan = false; // Default to false
            }
        }
    }
    

    setConsoleUTF8(); 

    SetTitle(OMOCHA_ENGINE_NAME); 

    
    string insideprojectPath = string(BASE_ASSETS) + "temp/project.json";
    string outSideprojectPath = string(BASE_ASSETS) + "project.json";
    

    string projectPath = "";

    
    if (filesystem::exists(insideprojectPath))
    {
        engine.EngineStdOut("Found project.json inside 'assets/temp/' folder.", 0);
        projectPath = insideprojectPath;
        engine.IsSysMenu=false;
    }
    else if (filesystem::exists(outSideprojectPath))
    {
        engine.EngineStdOut("Found project.json inside 'assets/' folder.", 0);
        projectPath = outSideprojectPath;
        engine.IsSysMenu=false;
    }
    else if (filesystem::exists("sysmenu/project.json")){ //아무것도 없으면 시스템메뉴 실행
        if (!filesystem::exists("assets"))
        {
            engine.EngineStdOut("Creating 'assets' folder. Please add project assets there.", 1);
            try
            {
                filesystem::create_directory("assets");
            }
            catch (const filesystem::filesystem_error &e)
            {
                engine.EngineStdOut("Failed to create 'assets' directory: " + string(e.what()), 2);
                engine.showMessageBox("Failed to create 'assets' directory.",engine.msgBoxIconType.ICON_ERROR);
            }
        }
        projectPath = "sysmenu/project.json";
        engine.IsSysMenu=true;
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
            catch (const filesystem::filesystem_error &e)
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
        SetTitle(PROJECT_NAME); 
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
    if (projectDataLoaded)
    {
        if (!engine.initGE(mainProgram.mainProgramValue.getVsync(), mainProgram.mainProgramValue.useVulkan)) // VSync 및 Vulkan 사용 여부 전달
        {
            
            engine.EngineStdOut("Engine/Graphic initialization failed. Exiting.", 2);
            return 1; 
        }
        else
        {
            //SDL_SetWindowIcon(IDI_OMOC);
            engine.renderLoadingScreen();

            if (!engine.loadImages())
            {
                engine.EngineStdOut("Image loading process completed with errors.", 1);
            }
            else
            {
                engine.EngineStdOut("Image loading process completed successfully.", 0);
            }
            
            if (!engine.loadSounds())
            {
                engine.EngineStdOut("Sound loading process completed with errors.",1);
            }else{
                engine.EngineStdOut("Sound loading process completed successfully.",0);
            }
            
            engine.renderLoadingScreen();
            engine.EngineStdOut("Entering game loop.", 0);
            engine.runStartButtonScripts();
            bool quit = false;
            SDL_Event event;
            Uint64 loopStartTime = 0;
            int targetFps = engine.getTargetFps();
            int targetFrameTimeMillis = (targetFps > 0) ? (1000 / targetFps) : (1000 / 60);
            while (!quit)
            {                                  
                loopStartTime = SDL_GetTicks();

                // 엔진의 현재 마우스 스테이지 좌표 업데이트
                float windowMouseX_main, windowMouseY_main;
                SDL_GetMouseState(&windowMouseX_main, &windowMouseY_main); 
                engine.updateCurrentMouseStageCoordinates(windowMouseX_main, windowMouseY_main);
                
                Uint64 currentTimeMs = SDL_GetTicks(); 

                for (auto& pair : engine.getEntities_Modifiable()) { 
                    if (pair.second) {
                        pair.second->updateDialog(currentTimeMs);
                    }
                }

                while (SDL_PollEvent(&event))
                {
                    if (event.type == SDL_EVENT_QUIT)
                    {
                        string notice = PROJECT_NAME+" 을(를) 종료하시겠습니까?";
                        
                        if(engine.showMessageBox(notice,engine.msgBoxIconType.ICON_INFORMATION,true)){
                            quit = true;
                        }else{
                            quit = false;
                        }
                    }
                    engine.processInput(event); // 수정: SDL_Event를 processInput으로 전달
                    if (event.type == SDL_EVENT_RENDER_DEVICE_RESET || event.type == SDL_EVENT_RENDER_TARGETS_RESET) {
                        engine.handleRenderDeviceReset(); // 리셋 플래그 설정 및 기존 리소스 해제 준비
                    }
                }
                if (!engine.recreateAssetsIfNeeded())
                {
                    engine.EngineStdOut("Failed to recreate assets. Exiting");
                    engine.showMessageBox("Failed to recreate assets. Exiting",engine.msgBoxIconType.ICON_ERROR);
                    quit = true;
                    continue;
                }
                engine.drawAllEntities(); 
                engine.drawDialogs(); 
                engine.drawHUD();
                SDL_RenderPresent(engine.getRenderer()); // SDL: 화면에 최종 프레임 표시
                engine.updateFps();
                long long elapsedTime = SDL_GetTicks() - loopStartTime;                
                int waitTime = targetFrameTimeMillis - static_cast<int>(elapsedTime); 
                if (waitTime > 0)
                {
                    SDL_Delay(static_cast<Uint32>(waitTime));
                }
                
            }
            

            engine.EngineStdOut("Game loop ended.", 0);
        }
    }

    // engine.terminateGE(); // Engine 객체의 소멸자에서 호출됨

    return 0; 
}


void setConsoleUTF8()
{
#ifdef _WIN32                   
    system("chcp 65001 > nul"); 
#endif
    
}
void SetTitle(const string &s)
{
#ifdef _WIN32
    SetConsoleTitleA(s.c_str());
#endif
}