//------------------------------------------------------------------------------
// viewerapp.cc
// (C) 2018 Individual contributors, see AUTHORS file
//------------------------------------------------------------------------------
#include "stdneb.h"
#include "core/refcounted.h"
#include "timing/timer.h"
#include "io/console.h"
#include "visibility/visibilitycontext.h"
#include "models/streammodelpool.h"
#include "models/modelcontext.h"
#include "input/keyboard.h"
#include "input/mouse.h"
#include "viewerapp.h"
#include "math/vector.h"
#include "math/point.h"
#include "dynui/imguicontext.h"
#include "lighting/lightcontext.h"
#include "characters/charactercontext.h"
#include "imgui.h"
#include "dynui/im3d/im3dcontext.h"
#include "dynui/im3d/im3d.h"
#include "graphics/environmentcontext.h"
#include "clustering/clustercontext.h"
#include "scenes/scenes.h"
#include "debug/framescriptinspector.h"
#include "io/logfileconsolehandler.h"

using namespace Timing;
using namespace Graphics;
using namespace Visibility;
using namespace Models;

namespace Tests
{

//------------------------------------------------------------------------------
/**
*/
const char* stateToString(Resources::Resource::State state)
{
    switch (state)
    {
    case Resources::Resource::State::Pending: return "Pending";
    case Resources::Resource::State::Loaded: return "Loaded";
    case Resources::Resource::State::Failed: return "Failed";
    case Resources::Resource::State::Unloaded: return "Unloaded";
    }
    return "Unknown";
}

//------------------------------------------------------------------------------
/**
*/
SimpleViewerApplication::SimpleViewerApplication()
{
    this->SetAppTitle("Viewer App");
    this->SetCompanyName("Nebula");
}

//------------------------------------------------------------------------------
/**
*/
SimpleViewerApplication::~SimpleViewerApplication()
{
    // empty
}

//------------------------------------------------------------------------------
/**
*/
bool 
SimpleViewerApplication::Open()
{
    if (Application::Open())
    {
#if __NEBULA_HTTP__

		// setup debug subsystem
		this->debugInterface = Debug::DebugInterface::Create();
		this->debugInterface->Open();
#endif
        this->gfxServer = GraphicsServer::Create();
        this->resMgr = Resources::ResourceManager::Create();
        this->inputServer = Input::InputServer::Create();
        this->ioServer = IO::IoServer::Create();
        
#if __WIN32__
        //Ptr<IO::LogFileConsoleHandler> logFileHandler = IO::LogFileConsoleHandler::Create();
        //IO::Console::Instance()->AttachHandler(logFileHandler.upcast<IO::ConsoleHandler>());
#endif

        this->resMgr->Open();
        this->inputServer->Open();
        this->gfxServer->Open();

        SizeT width = this->GetCmdLineArgs().GetInt("-w", 1680);
        SizeT height = this->GetCmdLineArgs().GetInt("-h", 1050);

        CoreGraphics::WindowCreateInfo wndInfo =
        {
            CoreGraphics::DisplayMode{ 100, 100, width, height },
            this->GetAppTitle(), "", CoreGraphics::AntiAliasQuality::None, true, true, false
        };
        this->wnd = CreateWindow(wndInfo);
		this->cam = Graphics::CreateEntity();

        // create contexts, this could and should be bundled together
        CameraContext::Create();
        ModelContext::Create();
		Particles::ParticleContext::Create();

		// make sure all bounding box modifying contexts are created before the observer contexts
        ObserverContext::Create();
        ObservableContext::Create();

		Graphics::RegisterEntity<CameraContext, ObserverContext>(this->cam);
		CameraContext::SetupProjectionFov(this->cam, width / (float)height, Math::n_deg2rad(60.f), 0.1f, 1000.0f);

		Clustering::ClusterContext::Create(0.1f, 1000.0f, this->wnd);
		Lighting::LightContext::Create();
		Characters::CharacterContext::Create();
		Im3d::Im3dContext::Create();
		Dynui::ImguiContext::Create();


		this->view = gfxServer->CreateView("mainview", "frame:vkdefault.json"_uri);
		this->stage = gfxServer->CreateStage("stage1", true);

        Im3d::Im3dContext::SetGridStatus(true);
        Im3d::Im3dContext::SetGridSize(1.0f, 25);
        Im3d::Im3dContext::SetGridColor(Math::float4(0.2f, 0.2f, 0.2f, 0.8f));

		this->globalLight = Graphics::CreateEntity();
		Lighting::LightContext::RegisterEntity(this->globalLight);
		Lighting::LightContext::SetupGlobalLight(this->globalLight, Math::float4(1, 1, 1, 0), 1.0f, Math::float4(0, 0, 0, 0), Math::float4(0, 0, 0, 0), 0.0f, -Math::vector(1, 1, 1), true);

        this->ResetCamera();
        CameraContext::SetTransform(this->cam, this->mayaCameraUtil.GetCameraTransform());

        this->view->SetCamera(this->cam);
        this->view->SetStage(this->stage);

        // register visibility system
        ObserverContext::CreateBruteforceSystem({});

        ObserverContext::Setup(this->cam, VisibilityEntityType::Camera);

        // create environment context for the atmosphere effects
		EnvironmentContext::Create(this->globalLight);

        this->UpdateCamera();

        this->frametimeHistory.Fill(0, 120, 0.0f);

        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
/**
*/
void 
SimpleViewerApplication::Close()
{
	App::Application::Close();
    DestroyWindow(this->wnd);
    this->gfxServer->DiscardStage(this->stage);
    this->gfxServer->DiscardView(this->view);
    ObserverContext::Discard();
    Lighting::LightContext::Discard();

    this->gfxServer->Close();
    this->inputServer->Close();
    this->resMgr->Close();
}

//------------------------------------------------------------------------------
/**
*/
void 
SimpleViewerApplication::Run()
{    
    bool run = true;

    const Ptr<Input::Keyboard>& keyboard = inputServer->GetDefaultKeyboard();
    const Ptr<Input::Mouse>& mouse = inputServer->GetDefaultMouse();
    
    scenes[currentScene]->Open();

    while (run && !inputServer->IsQuitRequested())
    {
        this->inputServer->BeginFrame();
        this->inputServer->OnFrame();

        this->resMgr->Update(this->frameIndex);

#if NEBULA_ENABLE_PROFILING
        // copy because the information has been discarded when we render UI
        this->frameProfilingMarkers = CoreGraphics::GetProfilingMarkers();
#endif NEBULA_ENABLE_PROFILING

		this->gfxServer->BeginFrame();
        
        scenes[currentScene]->Run();

        // put game code which doesn't need visibility data or animation here
        this->gfxServer->BeforeViews();
        this->RenderUI();             

        if (this->renderDebug)
        {
            this->gfxServer->RenderDebug(0);
        }
        
        // put game code which need visibility data here
        this->gfxServer->RenderViews();

        // put game code which needs rendering to be done (animation etc) here
        this->gfxServer->EndViews();

        // do stuff after rendering is done
        this->gfxServer->EndFrame();

        // force wait immediately
        WindowPresent(wnd, frameIndex);
        if (this->inputServer->GetDefaultKeyboard()->KeyPressed(Input::Key::Escape)) run = false;        
                
        if (this->inputServer->GetDefaultKeyboard()->KeyPressed(Input::Key::LeftMenu))
            this->UpdateCamera();
        
		if (this->inputServer->GetDefaultKeyboard()->KeyPressed(Input::Key::F8))
			Resources::ResourceManager::Instance()->ReloadResource("shd:imgui.fxb");

        frameIndex++;             
        this->inputServer->EndFrame();
    }
}

//------------------------------------------------------------------------------
/**
*/
void 
SimpleViewerApplication::RenderUI()
{
    
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, { 0,0,0,0.15f });
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Scenes"))
        {
            int i = 0;
            for (auto scene : scenes)
            {
                bool isSelected = (i == currentScene);
                if (ImGui::MenuItem(scene->name, nullptr, &isSelected))
                {
                    if (i != currentScene)
                    {
                        scenes[currentScene]->Close();
                        currentScene = i;
                        scenes[currentScene]->Open();
                    }
                }
                i++;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows"))
        {
            ImGui::MenuItem("Camera Window", nullptr, &this->showCameraWindow);
            ImGui::MenuItem("Frame Profiler", nullptr, &this->showFrameProfiler);
            ImGui::MenuItem("Scene UI", nullptr, &this->showSceneUI);

            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    ImGui::PopStyleColor();

    float frameTime = (float)this->gfxServer->GetFrameTime();
	this->averageFrameTime += frameTime;
    this->frametimeHistory.Append(frameTime);
    if (this->frametimeHistory.Size() > 120)
        this->frametimeHistory.EraseFront();

	if (this->gfxServer->GetFrameIndex() % 35 == 0)
	{
		this->prevAverageFrameTime = this->averageFrameTime / 35.0f;
		this->averageFrameTime = 0.0f;
	}
    if (this->showCameraWindow)
    {
        ImGui::Begin("Viewer", &showCameraWindow, 0);

        ImGui::SetWindowSize(ImVec2(240, 400));
        if (ImGui::CollapsingHeader("Camera mode", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::RadioButton("Maya", &this->cameraMode, 0))this->ToMaya();
            ImGui::SameLine();
            if (ImGui::RadioButton("Free", &this->cameraMode, 1))this->ToFree();
            ImGui::SameLine();
            if (ImGui::Button("Reset")) this->ResetCamera();
        }
        ImGui::Checkbox("Debug Rendering", &this->renderDebug);

        ImGui::End();
    }

    if (this->showSceneUI)
    {
        scenes[currentScene]->RenderUI();
    }

    

    if (this->showFrameProfiler)
    {
        Debug::FrameScriptInspector::Run(this->view->GetFrameScript());
        ImGui::Begin("Performance Profiler", &this->showFrameProfiler);
        {
            ImGui::Text("ms - %.2f\nFPS - %.2f", this->prevAverageFrameTime * 1000, 1 / this->prevAverageFrameTime);
            ImGui::PlotLines("Frame Times", &this->frametimeHistory[0], this->frametimeHistory.Size(), 0, 0, FLT_MIN, FLT_MAX, { ImGui::GetWindowWidth(), 90 });
            ImGui::Separator();


#if NEBULA_ENABLE_PROFILING
            if (ImGui::CollapsingHeader("Frame profiling markers"))
            {
                ImGui::Columns(4, "framemarkercolumns"); // 4-ways, with border
                ImGui::Separator();
                ImGui::Text("Queue"); ImGui::NextColumn();
                ImGui::Text("Name"); ImGui::NextColumn();
                ImGui::Text("CPU (ms)"); ImGui::NextColumn();
                ImGui::Text("GPU (ms)"); ImGui::NextColumn();
                ImGui::Separator();
                static int selected = -1;
                auto const& frameMarkers = this->frameProfilingMarkers;
                Timing::Time totalCpuTime = 0.0;
                Timing::Time totalGpuTime = 0.0;
                for (int i = 0; i < frameMarkers.Size(); i++)
                {
                    ImGui::Selectable(CoreGraphics::QueueNameFromQueueType(frameMarkers[i].queue), selected == i, ImGuiSelectableFlags_SpanAllColumns);
                    ImGui::NextColumn();
                    ImGui::Text(frameMarkers[i].name);
                    ImGui::NextColumn();
                    ImGui::Text("%f", (frameMarkers[i].timer.GetTime() * 1000));
                    totalCpuTime += (frameMarkers[i].timer.GetTime() * 1000);
                    ImGui::NextColumn();
                    if (frameMarkers[i].gpuTime == -1)
                        ImGui::Text("N/A");
                    else
                    {
                        ImGui::Text("%f", (frameMarkers[i].gpuTime / 1000000.0));
                        totalGpuTime += (frameMarkers[i].gpuTime / 1000000.0);
                    }

                    ImGui::NextColumn();
                }

                ImGui::Separator();
                ImGui::Columns(4);
                ImGui::Text("Draw calls %d", CoreGraphics::GetNumDrawCalls());
                ImGui::NextColumn();// unused column
                ImGui::NextColumn();
                ImGui::Text("Total CPU %f ms, %.1f FPS", totalCpuTime, 1000 / totalCpuTime);
                ImGui::NextColumn();
                ImGui::Text("Total GPU %f ms, %.1f FPS", totalGpuTime, 1000 / totalGpuTime);
                ImGui::Separator();
                ImGui::Columns(1);
            }
#endif NEBULA_ENABLE_PROFILING
        }
        ImGui::End();
    }
}

//------------------------------------------------------------------------------
/**
*/
void 
SimpleViewerApplication::UpdateCamera()
{
    const Ptr<Input::Keyboard>& keyboard = inputServer->GetDefaultKeyboard();
    const Ptr<Input::Mouse>& mouse = inputServer->GetDefaultMouse();

    this->mayaCameraUtil.SetOrbitButton(mouse->ButtonPressed(Input::MouseButton::LeftButton));
    this->mayaCameraUtil.SetPanButton(mouse->ButtonPressed(Input::MouseButton::MiddleButton));
    this->mayaCameraUtil.SetZoomButton(mouse->ButtonPressed(Input::MouseButton::RightButton));
    this->mayaCameraUtil.SetZoomInButton(mouse->WheelForward());
    this->mayaCameraUtil.SetZoomOutButton(mouse->WheelBackward());
    this->mayaCameraUtil.SetMouseMovement(mouse->GetMovement());

    // process keyboard input
    Math::float4 pos(0.0f);
    if (keyboard->KeyDown(Input::Key::Space))
    {
        this->mayaCameraUtil.Reset();
    }
    if (keyboard->KeyPressed(Input::Key::Left))
    {
        panning.x() -= 0.1f;
        pos.x() -= 0.1f;
    }
    if (keyboard->KeyPressed(Input::Key::Right))
    {
        panning.x() += 0.1f;
        pos.x() += 0.1f;
    }
    if (keyboard->KeyPressed(Input::Key::Up))
    {
        panning.y() -= 0.1f;
        if (keyboard->KeyPressed(Input::Key::LeftShift))
        {
            pos.y() -= 0.1f;
        }
        else
        {
            pos.z() -= 0.1f;
        }
    }
    if (keyboard->KeyPressed(Input::Key::Down))
    {
        panning.y() += 0.1f;
        if (keyboard->KeyPressed(Input::Key::LeftShift))
        {
            pos.y() += 0.1f;
        }
        else
        {
            pos.z() += 0.1f;
        }
    }


    this->mayaCameraUtil.SetPanning(panning);
    this->mayaCameraUtil.SetOrbiting(orbiting);
    this->mayaCameraUtil.SetZoomIn(zoomIn);
    this->mayaCameraUtil.SetZoomOut(zoomOut);
    this->mayaCameraUtil.Update();

    
    this->freeCamUtil.SetForwardsKey(keyboard->KeyPressed(Input::Key::W));
    this->freeCamUtil.SetBackwardsKey(keyboard->KeyPressed(Input::Key::S));
    this->freeCamUtil.SetRightStrafeKey(keyboard->KeyPressed(Input::Key::D));
    this->freeCamUtil.SetLeftStrafeKey(keyboard->KeyPressed(Input::Key::A));
    this->freeCamUtil.SetUpKey(keyboard->KeyPressed(Input::Key::Q));
    this->freeCamUtil.SetDownKey(keyboard->KeyPressed(Input::Key::E));

    this->freeCamUtil.SetMouseMovement(mouse->GetMovement());
    this->freeCamUtil.SetAccelerateButton(keyboard->KeyPressed(Input::Key::LeftShift));

    this->freeCamUtil.SetRotateButton(mouse->ButtonPressed(Input::MouseButton::LeftButton));
    this->freeCamUtil.Update();
    
    switch (this->cameraMode)
    {
    case 0:
        CameraContext::SetTransform(this->cam, Math::matrix44::inverse(this->mayaCameraUtil.GetCameraTransform()));
        break;
    case 1:
        CameraContext::SetTransform(this->cam, Math::matrix44::inverse(this->freeCamUtil.GetTransform()));
        break;
    default:
        break;
    }
}

//------------------------------------------------------------------------------
/**
*/
void 
SimpleViewerApplication::ResetCamera()
{
    this->freeCamUtil.Setup(this->defaultViewPoint, Math::float4::normalize(this->defaultViewPoint));
    this->freeCamUtil.Update();
    this->mayaCameraUtil.Setup(Math::point(0.0f, 0.0f, 0.0f), this->defaultViewPoint, Math::vector(0.0f, 1.0f, 0.0f));
}

//------------------------------------------------------------------------------
/**
*/
void 
SimpleViewerApplication::ToMaya()
{
    this->mayaCameraUtil.Setup(this->mayaCameraUtil.GetCenterOfInterest(), this->freeCamUtil.GetTransform().get_position(), Math::vector(0, 1, 0));
}

//------------------------------------------------------------------------------
/**
*/
void 
SimpleViewerApplication::ToFree()
{
    Math::float4 pos = this->mayaCameraUtil.GetCameraTransform().get_position();
    this->freeCamUtil.Setup(pos, Math::float4::normalize(pos - this->mayaCameraUtil.GetCenterOfInterest()));
}

//------------------------------------------------------------------------------
/**
*/
void 
SimpleViewerApplication::Browse()
{
    this->folders = IO::IoServer::Instance()->ListDirectories("mdl:", "*");    
    this->files = IO::IoServer::Instance()->ListFiles("mdl:" + this->folders[this->selectedFolder], "*");
}
}
